// Copyright (c) 2026 The PGN developers. MIT.
// Read-only projection. Consensus and work admission never consume this data.
#pragma once
#include <pgn/native_report_audit.h>
#include <pgn/rpc_public_attribution.h>
#include <algorithm>
#include <map>
#include <set>
#include <vector>

namespace pgn::core {
inline constexpr size_t LeaderboardAliasesPerRow = 8;
inline constexpr size_t LeaderboardAliasHexBudget = 32768;
inline constexpr size_t LeaderboardCacheBudget = 64 * 1024 * 1024;

// Enrolled keys cannot change operator_id (ApplyNativeAuditEvent). A first
// enrollment can still replace an earlier script-derived identity. Preserve
// its inclusion height instead of applying the current mapping to past work.
struct LeaderboardBinding { uint256 operator_id; int32_t height{}; };
using LeaderboardBindings = std::map<Bytes, LeaderboardBinding>;
inline uint256 LeaderboardOperatorAt(const LeaderboardBindings& bindings,
                                    const CScript& script, int32_t height) {
    const auto at = bindings.find(Bytes{script.begin(), script.end()});
    if (at != bindings.end() && at->second.height <= height) return at->second.operator_id;
    return NativeAuditOperator(NativeAuditLedger{}, script);
}
inline void LeaderboardBind(LeaderboardBindings& bindings, const Bytes& script,
                            const uint256& id, int32_t height) {
    if (script.empty() || id.IsNull() || height < 0) throw std::runtime_error("Leaderboard binding invalid");
    const auto [at, inserted] = bindings.emplace(script, LeaderboardBinding{id, height});
    if (!inserted && at->second.operator_id != id) throw std::runtime_error("Leaderboard operator transition invalid");
    if (!inserted) at->second.height = std::min(at->second.height, height);
}
struct NativeLeaderboardRow {
    uint256 operator_id;
    uint64_t tickets_reported{}, tickets_validated{}, audits_finalized_valid{};
    Integer validated_search_positions{};
    std::set<Bytes> signing_scripts;
    int32_t last_activity_height{-1};
    std::optional<std::string> display_name;
    int32_t display_name_height{-1}; // Projection provenance only, not an API/authority field.
    uint256 display_name_request;
};
inline void ConsiderLeaderboardDisplayName(NativeLeaderboardRow& row,int32_t height,const uint256& request,
                                           const std::string& disclosure,const uint256& root) {
    if(row.display_name&&(height<row.display_name_height||
        (height==row.display_name_height&&request.GetHex()>=row.display_name_request.GetHex())))return;
    const auto label=DisclosedPublicAttribution(disclosure,root.GetHex(),[](std::string_view raw){return HexStr(AuditSha(Bytes{raw.begin(),raw.end()}));});
    if(!label)return;
    row.display_name=(*label)["name"].get_str();row.display_name_height=height;row.display_name_request=request;
}
struct NativeLeaderboardSnapshot {
    const void* chainstate{};
    uint256 genesis, tip;
    int32_t height{};
    LeaderboardBindings bindings;
    std::vector<NativeLeaderboardRow> rows;
};

// Caller supplies an immutable ledger from ReadRpcAuditLedger and authenticated
// enrollment heights from that exact branch. No caller-authored identity/name
// enters production. All big-integer sums and sorting run outside cs_main.
inline NativeLeaderboardSnapshot BuildNativeLeaderboard(
    const NativeAuditLedger& ledger, const CBlockIndex& tip, const uint256& genesis,
    const void* chainstate, LeaderboardBindings bindings) {
    NativeLeaderboardSnapshot result{chainstate, genesis, tip.GetBlockHash(), tip.nHeight, std::move(bindings), {}};
    for (const auto& [script, member] : ledger.participants) {
        const auto at = result.bindings.find(script);
        if (at == result.bindings.end() || at->second.operator_id != member.operator_id || at->second.height > tip.nHeight)
            throw std::runtime_error("Leaderboard membership provenance unavailable");
    }
    std::map<uint256, NativeLeaderboardRow> grouped;
    const auto rowFor = [&](const CScript& script, int32_t height) -> NativeLeaderboardRow& {
        const auto id = LeaderboardOperatorAt(result.bindings, script, height);
        auto& row = grouped[id]; row.operator_id = id;
        row.signing_scripts.emplace(script.begin(), script.end());
        row.last_activity_height = std::max(row.last_activity_height, height);
        return row;
    };
    for (const auto& [id, report] : ledger) {
        const auto* confirmation = tip.GetAncestor(report.report.height);
        if (!confirmation || confirmation->GetBlockHash() != report.report.block || report.range.length <= 0)
            throw std::runtime_error("Leaderboard report provenance invalid");
        auto& owner = rowFor(report.owner, report.report.height);
        ++owner.tickets_reported;
        // Cosmetic, self-declared/non-unique; the row was already selected by
        // the authenticated historical operator. Never key identity/rights on it.
        ConsiderLeaderboardDisplayName(owner,report.report.height,id,report.owner_reveal,report.report.commitment.root);
        if (NativeAuditReportQualified(ledger, report)) {
            ++owner.tickets_validated;
            owner.validated_search_positions += report.range.length;
        }
        for (size_t slot = 0; slot < report.jobs.size(); ++slot) {
            const auto& job = report.jobs[slot];
            if (!job || job->stage != NativeAuditJob::Passed ||
                !NativeAuditActorAllowed(ledger, report, job->actor, uint8_t(slot), tip.nHeight)) continue;
            if (job->passed_height <= 0 || job->passed_height > tip.nHeight)
                throw std::runtime_error("Leaderboard audit finalization provenance unavailable");
            ++rowFor(job->actor, job->passed_height).audits_finalized_valid;
        }
    }
    // Include known aliases for a productive operator, without creating rows
    // for roster-only identities or labeling anybody online.
    for (const auto& [script, binding] : result.bindings) {
        const auto row = grouped.find(binding.operator_id);
        if (row != grouped.end()) row->second.signing_scripts.insert(script);
    }
    result.rows.reserve(grouped.size());
    for (auto& [id, row] : grouped) result.rows.push_back(std::move(row));
    std::sort(result.rows.begin(), result.rows.end(), [](const auto& a, const auto& b) {
        if (a.validated_search_positions != b.validated_search_positions)
            return a.validated_search_positions > b.validated_search_positions;
        // uint256::operator< compares internal little-endian bytes. Order the
        // displayed canonical hex instead, without allocating comparator strings.
        for (size_t i = a.operator_id.size(); i-- > 0;)
            if (a.operator_id.begin()[i] != b.operator_id.begin()[i])
                return a.operator_id.begin()[i] < b.operator_id.begin()[i];
        return false;
    });
    size_t charge = sizeof(result) + result.rows.capacity() * sizeof(NativeLeaderboardRow);
    const auto add = [&](size_t bytes) {
        if (bytes > LeaderboardCacheBudget - std::min(charge, LeaderboardCacheBudget))
            throw std::runtime_error("Leaderboard projection exceeds read-only cache capacity");
        charge += bytes;
    };
    for (const auto& [script, binding] : result.bindings) add(script.capacity() + 256);
    for (const auto& row : result.rows) {
        add(row.validated_search_positions.convert_to<std::string>().capacity());
        if(row.display_name)add(row.display_name->capacity());
        for (const auto& script : row.signing_scripts) add(script.capacity() + 128);
    }
    return result;
}

inline UniValue NativeLeaderboardPage(const NativeLeaderboardSnapshot& snapshot, uint64_t offset, unsigned limit) {
    if (limit < 1 || limit > 100) throw std::invalid_argument("Leaderboard page must contain 1 through 100 rows");
    UniValue out{UniValue::VOBJ}, rows{UniValue::VARR};
    out.pushKV("schema", "PGN_LEADERBOARD_V1");
    out.pushKV("genesis", snapshot.genesis.GetHex()); out.pushKV("bestblockhash", snapshot.tip.GetHex());
    out.pushKV("height", snapshot.height); out.pushKV("total", uint64_t(snapshot.rows.size()));
    out.pushKV("offset", offset); out.pushKV("limit", limit);
    out.pushKV("sort", "validated_search_positions_desc_then_operator_id_asc");
    out.pushKV("scope", "authenticated_active_branch_history_through_snapshot_tip");
    out.pushKV("identity_attribution", "NativeAuditOperator_at_report_inclusion_or_audit_finalization_height");
    out.pushKV("validation_scope", "native_qualification_at_snapshot_tip");
    out.pushKV("volume_unit", "search_positions");
    out.pushKV("volume_definition", "sum_of_qualified_report_range_lengths_including_repeated_coverage");
    out.pushKV("alias_scope", "historical_contributors_plus_current_authenticated_aliases");
    out.pushKV("alias_limit_per_row", uint64_t(LeaderboardAliasesPerRow));
    out.pushKV("alias_hex_budget_per_page", uint64_t(LeaderboardAliasHexBudget));
    size_t alias_bytes = 0;
    const uint64_t end = offset >= snapshot.rows.size() ? offset :
        offset + std::min(uint64_t(limit), uint64_t(snapshot.rows.size()) - offset);
    for (uint64_t index = offset; index < end; ++index) {
        const auto& source = snapshot.rows[size_t(index)]; UniValue row{UniValue::VOBJ}, aliases{UniValue::VARR};
        row.pushKV("rank", index + 1); row.pushKV("operator_id", source.operator_id.GetHex());
        if(source.display_name)row.pushKV("display_name",*source.display_name);
        row.pushKV("tickets_reported", std::to_string(source.tickets_reported));
        row.pushKV("tickets_validated", std::to_string(source.tickets_validated));
        row.pushKV("audits_finalized_valid", std::to_string(source.audits_finalized_valid));
        row.pushKV("validated_search_positions", source.validated_search_positions.convert_to<std::string>());
        row.pushKV("last_activity_height", source.last_activity_height);
        for (const auto& script : source.signing_scripts) {
            if (aliases.size() >= LeaderboardAliasesPerRow || script.size() * 2 > LeaderboardAliasHexBudget - alias_bytes) break;
            aliases.push_back(HexStr(script)); alias_bytes += script.size() * 2;
        }
        row.pushKV("signing_scripts", aliases); row.pushKV("signing_scripts_total", uint64_t(source.signing_scripts.size()));
        row.pushKV("signing_scripts_more", aliases.size() < source.signing_scripts.size());
        rows.push_back(std::move(row));
    }
    out.pushKV("rows", rows); out.pushKV("next_offset", end);
    out.pushKV("more", end < snapshot.rows.size());
    UniValue unavailable{UniValue::VOBJ};
    unavailable.pushKV("active_workers", "No authenticated per-operator worker measurements in native history");
    unavailable.pushKV("mining_seconds", "Historical mining duration is not measured by the native ledger");
    unavailable.pushKV("online", "Historical scripts and PGN relay connections do not prove wallet presence");
    out.pushKV("unavailable_fields", unavailable);
    return out;
}
} // namespace pgn::core
