// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <txdb.h>

#include <coins.h>
#include <dbwrapper.h>
#include <logging/timer.h>
#include <primitives/transaction.h>
#include <random.h>
#include <serialize.h>
#include <uint256.h>
#include <util/byte_units.h>
#include <util/log.h>
#include <util/vector.h>

#include <cassert>
#include <cstdlib>
#include <iterator>
#include <utility>

static constexpr uint8_t DB_COIN{'C'};
static constexpr uint8_t DB_BEST_BLOCK{'B'};
static constexpr uint8_t DB_HEAD_BLOCKS{'H'};
static constexpr uint8_t DB_PGN_ISSUANCE{'I'};
static constexpr uint8_t DB_PGN_ISSUANCE_PENDING{'J'};
static constexpr uint8_t DB_PGN_ISSUANCE_ENROLLED{'K'};
// Local-only history namespace. L keys include format version + block hash;
// values, anchor M and pending ordered trail N are independently versioned.
static constexpr uint8_t DB_PGN_ISSUANCE_HISTORY{'L'};
static constexpr uint8_t DB_PGN_ISSUANCE_ANCHOR{'M'};
static constexpr uint8_t DB_PGN_ISSUANCE_TRAIL{'N'};
// Local versioned registry, not a deployed consensus encoding.
static constexpr uint8_t DB_PGN_FACT_HEAD{'O'};
static constexpr uint8_t DB_PGN_FACT{'P'};
static constexpr uint8_t DB_PGN_FACT_PENDING{'Q'};
static constexpr uint8_t DB_PGN_FACT_ENROLLED{'R'};
static constexpr uint8_t DB_PGN_FACT_FORWARD{'S'};
static constexpr uint8_t DB_PGN_FACT_ANCHOR{'T'};
static constexpr uint8_t DB_PGN_FACT_TRAIL{'U'};
// Keys used in previous version that might still be found in the DB:
static constexpr uint8_t DB_COINS{'c'};

// Threshold for warning when writing this many dirty cache entries to disk.
static constexpr size_t WARN_FLUSH_COINS_COUNT{10'000'000};

bool CCoinsViewDB::NeedsUpgrade()
{
    std::unique_ptr<CDBIterator> cursor{m_db->NewIterator()};
    // DB_COINS was deprecated in v0.15.0, commit
    // 1088b02f0ccd7358d2b7076bb9e122d59d502d02
    cursor->Seek(std::make_pair(DB_COINS, uint256{}));
    return cursor->Valid();
}

namespace {

struct CoinEntry {
    COutPoint* outpoint;
    uint8_t key{DB_COIN};
    explicit CoinEntry(const COutPoint* ptr) : outpoint(const_cast<COutPoint*>(ptr)) {}

    SERIALIZE_METHODS(CoinEntry, obj) { READWRITE(obj.key, obj.outpoint->hash, VARINT(obj.outpoint->n)); }
};

} // namespace

CCoinsViewDB::CCoinsViewDB(DBParams db_params, CoinsViewOptions options) :
    m_db_params{std::move(db_params)},
    m_options{std::move(options)},
    m_db{std::make_unique<CDBWrapper>(m_db_params)} { }

void CCoinsViewDB::ResizeCache(size_t new_cache_size)
{
    // We can't do this operation with an in-memory DB since we'll lose all the coins upon
    // reset.
    if (!m_db_params.memory_only) {
        // Have to do a reset first to get the original `m_db` state to release its
        // filesystem lock.
        m_db.reset();
        m_db_params.cache_bytes = new_cache_size;
        m_db_params.wipe_data = false;
        m_db = std::make_unique<CDBWrapper>(m_db_params);
    }
}

std::optional<Coin> CCoinsViewDB::GetCoin(const COutPoint& outpoint) const
{
    if (Coin coin; m_db->ReadStrict(CoinEntry(&outpoint), coin)) {
        Assert(!coin.IsSpent()); // The UTXO database should never contain spent coins
        return coin;
    }
    return std::nullopt;
}

std::optional<Coin> CCoinsViewDB::PeekCoin(const COutPoint& outpoint) const
{
    return GetCoin(outpoint);
}

bool CCoinsViewDB::HaveCoin(const COutPoint& outpoint) const
{
    return m_db->Exists(CoinEntry(&outpoint));
}

uint256 CCoinsViewDB::GetBestBlock() const {
    uint256 hashBestChain;
    if (!m_db->ReadStrict(DB_BEST_BLOCK, hashBestChain))
        return uint256();
    return hashBestChain;
}

std::vector<uint256> CCoinsViewDB::GetHeadBlocks() const {
    std::vector<uint256> vhashHeadBlocks;
    if (!m_db->ReadStrict(DB_HEAD_BLOCKS, vhashHeadBlocks)) {
        return std::vector<uint256>();
    }
    return vhashHeadBlocks;
}

namespace {
template <typename T> std::optional<T> ReadIssuanceRecord(const CDBWrapper& db, uint8_t key)
{
    T value;
    if (!db.ReadStrict(key, value)) return {};
    return value;
}
uint8_t ReadIssuanceEnrolled(const CDBWrapper& db)
{
    uint8_t version{};
    if (!db.ReadStrict(DB_PGN_ISSUANCE_ENROLLED, version)) return 0;
    if (version != 1 && version != 2) throw dbwrapper_error("Unsupported PGN issuance database format");
    return version;
}

uint8_t ReadFactEnrolled(const CDBWrapper& db)
{
    uint8_t version{};
    if (!db.ReadStrict(DB_PGN_FACT_ENROLLED, version)) return 0;
    if (version != 1 && version != 2) throw dbwrapper_error("Unsupported PGN fact registry format");
    return version;
}
auto FactKey(const uint256& fact) { return std::pair{DB_PGN_FACT, fact}; }
std::optional<pgn::core::FactClaim> ReadFact(const CDBWrapper& db, const uint256& fact)
{
    pgn::core::FactClaim claim;
    if (!db.ReadStrict(FactKey(fact), claim)) return {};
    return claim;
}
auto FactForwardKey(const uint256& tip) {
    return std::pair{DB_PGN_FACT_FORWARD, std::pair{uint8_t{1}, tip}};
}
using FactForwardMap = std::map<uint256, pgn::core::FactCommit>;
std::optional<pgn::core::FactCommit> ReadFactForward(const CDBWrapper& db, const uint256& tip)
{
    pgn::core::FactCommit value;
    if (!db.ReadStrict(FactForwardKey(tip), value)) return {};
    if (!value.Valid() || !value.before || value.IsIdentity() || value.after.tip != tip)
        throw dbwrapper_error("Invalid fact forward record/key");
    return value;
}
void RequireKnownFactHead(const CDBWrapper& db, const pgn::core::FactHead& anchor,
    const pgn::core::FactHead& state, const FactForwardMap& earlier = {})
{
    if (state.tip == anchor.tip) {
        if (state != anchor) throw dbwrapper_error("Fact history anchor mismatch");
        return;
    }
    const auto it = earlier.find(state.tip);
    const auto known = it == earlier.end() ? ReadFactForward(db, state.tip) : std::optional{it->second};
    if (!known || known->after != state) throw dbwrapper_error("Required fact forward missing/mismatched");
}
FactForwardMap CheckFactHistory(const CDBWrapper& db, const pgn::core::FactHead& anchor,
    const pgn::core::FactHistoryTrail& trail, const pgn::core::FactCommit& net)
{
    if (!anchor.Valid() || !net.before || !trail.ValidFor(net))
        throw dbwrapper_error("Fact history/net mismatch");
    RequireKnownFactHead(db, anchor, *net.before);
    FactForwardMap records, earlier;
    for (const auto& step : trail.steps) {
        const auto& forward = step.forward;
        if (forward.after.tip == anchor.tip) throw dbwrapper_error("Fact forward collides with anchor");
        const auto known = ReadFactForward(db, forward.after.tip);
        if (known && *known != forward) throw dbwrapper_error("Immutable fact forward collision");
        if (step.direction == pgn::core::FactDirection::Rewind && !known &&
            !earlier.contains(forward.after.tip)) throw dbwrapper_error("Unarchived fact rewind");
        if (step.direction == pgn::core::FactDirection::Forward)
            earlier.emplace(forward.after.tip, forward);
        records.emplace(forward.after.tip, forward);
        RequireKnownFactHead(db, anchor, step.Applied()->after, earlier);
    }
    // Net cancellation must not hide a fictitious intermediate claim.
    pgn::core::CheckFactTrailValues(trail, [&](const uint256& key) { return ReadFact(db,key); });
    return records;
}

auto HistoryKey(const uint256& tip) { return std::pair{DB_PGN_ISSUANCE_HISTORY, std::pair{uint8_t{1}, tip}}; }
using HistoryMap = std::map<uint256, pgn::core::IssuanceCommit>;
std::optional<pgn::core::IssuanceCommit> ReadForward(const CDBWrapper& db, const uint256& tip)
{
    pgn::core::IssuanceCommit value;
    if (!db.ReadStrict(HistoryKey(tip), value)) return {};
    if (!value.Valid() || !value.before || value.IsIdentity() || value.after.tip != tip)
        throw dbwrapper_error("Invalid local issuance forward record/key");
    return value;
}
void RequireKnownSnapshot(const CDBWrapper& db, const pgn::core::IssuanceSnapshot& anchor,
                          const pgn::core::IssuanceSnapshot& state, const HistoryMap& staged = {})
{
    if (state.tip == anchor.tip) {
        if (state != anchor) throw dbwrapper_error("Issuance anchor snapshot mismatch");
        return;
    }
    const auto it = staged.find(state.tip);
    const auto known = it == staged.end() ? ReadForward(db, state.tip) : std::optional{it->second};
    if (!known || known->after != state) throw dbwrapper_error("Required local issuance history missing/mismatched");
}
HistoryMap CheckHistory(const CDBWrapper& db, const pgn::core::IssuanceSnapshot& anchor,
                        const pgn::core::IssuanceHistoryTrail& trail, const pgn::core::IssuanceCommit& net)
{
    if (!anchor.Valid() || !net.before || !trail.ValidFor(net))
        throw dbwrapper_error("Local issuance trail/net precondition failed");
    RequireKnownSnapshot(db, anchor, *net.before);
    HistoryMap records, earlier_forwards;
    for (const auto& step : trail.steps) {
        const auto& forward = step.forward;
        if (forward.after.tip == anchor.tip) throw dbwrapper_error("Anchor is not an invented forward block");
        const auto existing = ReadForward(db, forward.after.tip);
        if (existing && *existing != forward) throw dbwrapper_error("Immutable issuance history collision");
        if (step.direction == pgn::core::IssuanceDirection::Rewind && !existing &&
            !earlier_forwards.contains(forward.after.tip))
            throw dbwrapper_error("Rewind without an archived or earlier forward");
        if (step.direction == pgn::core::IssuanceDirection::Forward)
            earlier_forwards.emplace(forward.after.tip, forward);
        records.emplace(forward.after.tip, forward);
        RequireKnownSnapshot(db, anchor, step.Applied()->after, earlier_forwards);
    }
    return records;
}
} // namespace

std::optional<pgn::core::FactHead> CCoinsViewDB::GetPgnFactHead() const
{
    const auto head = ReadIssuanceRecord<pgn::core::FactHead>(*m_db, DB_PGN_FACT_HEAD);
    const auto pending = GetPendingPgnFactCommit();
    const bool enrolled = ReadFactEnrolled(*m_db);
    GetPgnFactHistoryAnchor(); // Enrolled archive loss is never legacy absence.
    if (pending || m_db->Exists(DB_PGN_FACT_TRAIL) || (enrolled != head.has_value()) ||
        (head && (GetBestBlock() != head->tip || !GetHeadBlocks().empty())))
        throw dbwrapper_error("PGN fact registry missing, interrupted or incoherent");
    return head;
}
std::optional<pgn::core::FactCommit> CCoinsViewDB::GetPendingPgnFactCommit() const
{
    return ReadIssuanceRecord<pgn::core::FactCommit>(*m_db, DB_PGN_FACT_PENDING);
}
std::optional<pgn::core::FactHead> CCoinsViewDB::GetPgnFactHistoryAnchor() const
{
    const auto anchor = ReadIssuanceRecord<pgn::core::FactHead>(*m_db, DB_PGN_FACT_ANCHOR);
    const auto enrolled = ReadFactEnrolled(*m_db);
    if ((!anchor && m_db->Exists(DB_PGN_FACT_TRAIL)) || (enrolled == 2 && !anchor) || (anchor && enrolled != 2))
        throw dbwrapper_error("Orphaned or missing enrolled fact history anchor");
    return anchor;
}
std::optional<pgn::core::FactCommit> CCoinsViewDB::GetPgnFactForward(const uint256& tip) const
{
    const auto anchor = GetPgnFactHistoryAnchor();
    const auto record = ReadFactForward(*m_db, tip);
    if (record && (!anchor || tip == anchor->tip)) throw dbwrapper_error("Orphaned fact forward");
    return record;
}
std::optional<pgn::core::FactHistoryTrail> CCoinsViewDB::GetPendingPgnFactHistory() const
{
    return ReadIssuanceRecord<pgn::core::FactHistoryTrail>(*m_db, DB_PGN_FACT_TRAIL);
}
std::optional<pgn::core::FactClaim> CCoinsViewDB::GetPgnFact(const uint256& fact) const
{
    if (!GetPgnFactHead()) throw dbwrapper_error("PGN fact registry not enrolled");
    return ReadFact(*m_db, fact);
}

std::optional<pgn::core::IssuanceSnapshot> CCoinsViewDB::GetPgnIssuanceHistoryAnchor() const
{
    const auto anchor = ReadIssuanceRecord<pgn::core::IssuanceSnapshot>(*m_db, DB_PGN_ISSUANCE_ANCHOR);
    // Archive lookups need the enrollment/anchor, not n repeated decodes of N.
    // State/replay/publication gates decode and validate the WHOLE trail below.
    const bool trail_present = m_db->Exists(DB_PGN_ISSUANCE_TRAIL);
    const auto enrolled = ReadIssuanceEnrolled(*m_db);
    if ((!anchor && trail_present) || (enrolled == 2 && !anchor) || (anchor && enrolled != 2))
        throw dbwrapper_error("Orphaned local issuance history metadata");
    return anchor;
}

std::optional<pgn::core::IssuanceCommit> CCoinsViewDB::GetPgnIssuanceForward(const uint256& tip) const
{
    const auto anchor = GetPgnIssuanceHistoryAnchor();
    const auto record = ReadForward(*m_db, tip);
    if (record && (!anchor || tip == anchor->tip)) throw dbwrapper_error("Orphaned issuance forward record");
    return record;
}

std::optional<pgn::core::IssuanceHistoryTrail> CCoinsViewDB::GetPendingPgnIssuanceHistory() const
{
    // This same validator is reached by existing ReplayBlocks BEFORE coin work.
    const auto pending = GetPendingPgnIssuanceCommit();
    if (!pending) return {};
    return ReadIssuanceRecord<pgn::core::IssuanceHistoryTrail>(*m_db, DB_PGN_ISSUANCE_TRAIL);
}

std::optional<pgn::core::IssuanceSnapshot> CCoinsViewDB::GetPgnIssuanceState() const
{
    const auto state = ReadIssuanceRecord<pgn::core::IssuanceSnapshot>(*m_db, DB_PGN_ISSUANCE);
    const auto pending = ReadIssuanceRecord<pgn::core::IssuanceCommit>(*m_db, DB_PGN_ISSUANCE_PENDING);
    const bool enrolled = ReadIssuanceEnrolled(*m_db);
    const auto anchor = GetPgnIssuanceHistoryAnchor();
    const auto trail = ReadIssuanceRecord<pgn::core::IssuanceHistoryTrail>(*m_db, DB_PGN_ISSUANCE_TRAIL);
    if (!enrolled) {
        if (state || pending || anchor || trail) throw dbwrapper_error("Orphaned PGN issuance records without format enrollment");
        return {};
    }
    if (!state || pending || trail || GetBestBlock() != state->tip || !GetHeadBlocks().empty())
        throw dbwrapper_error("PGN issuance state is not at a committed chainstate boundary");
    if (anchor) RequireKnownSnapshot(*m_db, *anchor, *state);
    return state;
}

std::optional<pgn::core::IssuanceCommit> CCoinsViewDB::GetPendingPgnIssuanceCommit() const
{
    const auto pending = ReadIssuanceRecord<pgn::core::IssuanceCommit>(*m_db, DB_PGN_ISSUANCE_PENDING);
    const auto state = ReadIssuanceRecord<pgn::core::IssuanceSnapshot>(*m_db, DB_PGN_ISSUANCE);
    const bool enrolled = ReadIssuanceEnrolled(*m_db);
    const auto anchor = GetPgnIssuanceHistoryAnchor();
    const auto trail = ReadIssuanceRecord<pgn::core::IssuanceHistoryTrail>(*m_db, DB_PGN_ISSUANCE_TRAIL);
    const auto heads = GetHeadBlocks();
    const auto best = GetBestBlock();
    if (!enrolled) {
        if (state || pending || anchor || trail) throw dbwrapper_error("Orphaned PGN issuance records without format enrollment");
        return {};
    }
    if (!pending) {
        if (!state || trail || best != state->tip || !heads.empty())
            throw dbwrapper_error("PGN issuance recovery payload is missing");
        if (anchor) RequireKnownSnapshot(*m_db, *anchor, *state);
        return {};
    }
    if (!best.IsNull() || heads != std::vector<uint256>{pending->after.tip, pending->OldTip()} ||
        state != pending->before)
        throw dbwrapper_error("PGN issuance recovery payload does not match its chainstate markers");
    if (anchor) {
        if (!trail) throw dbwrapper_error("Required pending issuance history missing");
        CheckHistory(*m_db, *anchor, *trail, *pending);
    } else if (trail) throw dbwrapper_error("Pending history without anchor");
    return pending;
}

void CCoinsViewDB::BatchWrite(CoinsViewCacheCursor& cursor, const uint256& block_hash)
{
    // Compare ALL monetary state before cursor consumption, flags or DB writes.
    // A missing legacy record is not an implicit zero for an existing chain.
    const auto& issuance = cursor.PgnIssuanceCommit();
    const auto stored = ReadIssuanceRecord<pgn::core::IssuanceSnapshot>(*m_db, DB_PGN_ISSUANCE);
    const auto pending = ReadIssuanceRecord<pgn::core::IssuanceCommit>(*m_db, DB_PGN_ISSUANCE_PENDING);
    const bool enrolled = ReadIssuanceEnrolled(*m_db);
    const auto observed_best = GetBestBlock();
    const auto anchor = GetPgnIssuanceHistoryAnchor();
    const auto pending_history = ReadIssuanceRecord<pgn::core::IssuanceHistoryTrail>(*m_db, DB_PGN_ISSUANCE_TRAIL);
    const auto& history = cursor.PgnIssuanceHistory();
    std::optional<pgn::core::IssuanceSnapshot> next_anchor = anchor;
    HistoryMap records;
    if (history && (!issuance || !issuance->before)) throw dbwrapper_error("History requires a complete known prior");
    if (anchor || history) {
        if (!next_anchor) next_anchor = issuance->before;
        if (issuance) {
            if (!history || (pending && (!pending_history || *history != *pending_history)) ||
                (!pending && pending_history)) throw dbwrapper_error("Exact issuance history trail required");
            records = CheckHistory(*m_db, *next_anchor, *history, *issuance);
        } else {
            if (pending_history || !stored) throw dbwrapper_error("Untracked history change");
            RequireKnownSnapshot(*m_db, *next_anchor, *stored);
        }
    }
    if ((!enrolled && (stored || pending)) || (enrolled && !stored && !pending))
        throw dbwrapper_error("PGN issuance enrollment and required records disagree");
    if (issuance) {
        const auto heads = GetHeadBlocks();
        if (!issuance->Valid() || issuance->after.tip != block_hash || stored != issuance->before)
            throw dbwrapper_error("PGN issuance commit precondition failed");
        if (pending) {
            if (*pending != *issuance || !observed_best.IsNull() ||
                heads != std::vector<uint256>{issuance->after.tip, issuance->OldTip()})
                throw dbwrapper_error("PGN issuance replay requires the exact stored transition");
        } else if (!heads.empty() || observed_best != issuance->OldTip()) {
            throw dbwrapper_error("PGN issuance commit does not match the previous chain tip");
        }
        if (issuance->IsIdentity()) {
            // A->B->A before a flush may restore the exact ledger and tip.
            // Keep the typed payload, and prove every would-be coin write is
            // already identical BEFORE consuming any cursor entry or writing
            // HEAD. Then partial writes at HEAD[A,A] are idempotent. Under the
            // native serialized-writer contract this does not need block replay.
            // It is NOT authentication of arbitrary new block/UTXO effects.
            for (auto it = cursor.Begin(); it != cursor.End(); it = it->second.Next()) {
                if (!it->second.IsDirty()) continue;
                const auto existing = GetCoin(it->first); // Strict present-value decoding.
                const auto& desired = it->second.coin;
                if (desired.IsSpent()) {
                    if (existing) throw dbwrapper_error("Identity issuance commit changes an existing coin");
                } else if (!existing || desired.out != existing->out ||
                    desired.nHeight != existing->nHeight || desired.IsCoinBase() != existing->IsCoinBase()) {
                    throw dbwrapper_error("Identity issuance commit changes coin value, script or origin");
                }
            }
        }
    } else if (stored || pending) {
        // Repeated Flush/Sync at an unchanged, clean boundary is a real no-op,
        // not a request to invent a monetary transition. Dirty coins still need
        // explicit before/after state even when the supplied tip is unchanged.
        if (pending || !stored || observed_best != stored->tip || block_hash != stored->tip ||
            !GetHeadBlocks().empty() || cursor.GetDirtyCount() != 0)
            throw dbwrapper_error("PGN chainstate changes require an explicit issuance transition");
    }
    // Facts use the SAME native pending HEAD and final UTXO batch. Validate ALL
    // prior global keys before consuming the native coin cursor or writing DB.
    const auto& facts = cursor.PgnFacts();
    const auto fact_head = ReadIssuanceRecord<pgn::core::FactHead>(*m_db, DB_PGN_FACT_HEAD);
    const auto fact_pending = GetPendingPgnFactCommit();
    const bool fact_enrolled = ReadFactEnrolled(*m_db);
    const auto fact_anchor = GetPgnFactHistoryAnchor();
    const auto pending_fact_history = GetPendingPgnFactHistory();
    const auto& fact_history = cursor.PgnFactHistory();
    auto next_fact_anchor = fact_anchor;
    FactForwardMap fact_records;
    if (fact_history && (!facts || !facts->before)) throw dbwrapper_error("Fact history requires complete prior");
    if (fact_anchor || fact_history) {
        if (!next_fact_anchor) next_fact_anchor = facts->before;
        if (facts) {
            if (!fact_history || (fact_pending && (!pending_fact_history || *pending_fact_history != *fact_history)) ||
                (!fact_pending && pending_fact_history)) throw dbwrapper_error("Exact fact history trail required");
            fact_records = CheckFactHistory(*m_db, *next_fact_anchor, *fact_history, *facts);
        } else {
            if (pending_fact_history || !fact_head) throw dbwrapper_error("Untracked fact history change");
            RequireKnownFactHead(*m_db, *next_fact_anchor, *fact_head);
        }
    }
    if ((!fact_enrolled && (fact_head || fact_pending)) ||
        (fact_enrolled && !fact_head && !fact_pending))
        throw dbwrapper_error("PGN fact enrollment/required records disagree");
    if (facts) {
        if (!facts->Valid() || facts->after.tip != block_hash || facts->before != fact_head)
            throw dbwrapper_error("PGN fact commit precondition failed");
        const auto heads = GetHeadBlocks();
        if (fact_pending) {
            if (*fact_pending != *facts || !observed_best.IsNull() ||
                heads != std::vector<uint256>{facts->after.tip, facts->OldTip()})
                throw dbwrapper_error("PGN facts require exact pending replay");
        } else if (!heads.empty() || observed_best != facts->OldTip()) {
            throw dbwrapper_error("PGN facts/previous chain tip mismatch");
        }
        for (const auto& c : facts->changes) {
            if (ReadFact(*m_db, c.fact) != c.before)
                throw dbwrapper_error("PGN global fact duplicate or incorrect prior claim");
        }
        if (facts->IsIdentity()) {
            for (auto it = cursor.Begin(); it != cursor.End(); it = it->second.Next()) {
                if (!it->second.IsDirty()) continue;
                const auto existing = GetCoin(it->first);
                const auto& desired = it->second.coin;
                if (desired.IsSpent() ? existing.has_value() :
                    (!existing || existing->out != desired.out || existing->nHeight != desired.nHeight ||
                        existing->IsCoinBase() != desired.IsCoinBase()))
                    throw dbwrapper_error("PGN identity fact transition changes coins");
            }
        }
    } else if (fact_head || fact_pending) {
        if (fact_pending || !fact_head || observed_best != fact_head->tip || block_hash != fact_head->tip ||
            !GetHeadBlocks().empty() || cursor.GetDirtyCount() != 0)
            throw dbwrapper_error("PGN coins change discards required fact transition");
    }
    CDBBatch batch(*m_db);
    size_t count = 0;
    const size_t dirty_count{cursor.GetDirtyCount()};
    assert(!block_hash.IsNull());

    uint256 old_tip = observed_best;
    if (old_tip.IsNull()) {
        // We may be in the middle of replaying.
        std::vector<uint256> old_heads = GetHeadBlocks();
        if (old_heads.size() == 2) {
            if (old_heads[0] != block_hash) {
                LogError("The coins database detected an inconsistent state, likely due to a previous crash or shutdown. You will need to restart bitcoind with the -reindex-chainstate or -reindex configuration option.\n");
            }
            assert(old_heads[0] == block_hash);
            old_tip = old_heads[1];
        }
    }

    if (dirty_count > WARN_FLUSH_COINS_COUNT) LogWarning("Flushing large (%d entries) UTXO set to disk, it may take several minutes", dirty_count);
    LOG_TIME_MILLIS_WITH_CATEGORY(strprintf("write coins cache to disk (%d out of %d cached coins)",
        dirty_count, cursor.GetTotalCount()), BCLog::BENCH);

    // In the first batch, mark the database as being in the middle of a
    // transition from old_tip to block_hash.
    // A vector is used for future extensibility, as we may want to support
    // interrupting after partial writes from multiple independent reorgs.
    batch.Erase(DB_BEST_BLOCK);
    batch.Write(DB_HEAD_BLOCKS, Vector(block_hash, old_tip));
    if (facts) {
        batch.Write(DB_PGN_FACT_ENROLLED, fact_history ? uint8_t{2} : uint8_t{1});
        batch.Write(DB_PGN_FACT_PENDING, *facts);
        if (fact_history) {
            batch.Write(DB_PGN_FACT_ANCHOR, *next_fact_anchor);
            batch.Write(DB_PGN_FACT_TRAIL, *fact_history);
        }
    }
    if (issuance) {
        // Persistent enrollment distinguishes a LOST required record from a
        // genuinely old-format DB, including the very first interrupted flush.
        // It is NOT authentication against deletion/forgery of all local state.
        batch.Write(DB_PGN_ISSUANCE_ENROLLED, history ? uint8_t{2} : uint8_t{1});
        batch.Write(DB_PGN_ISSUANCE_PENDING, *issuance);
        if (history) {
            batch.Write(DB_PGN_ISSUANCE_ANCHOR, *next_anchor);
            batch.Write(DB_PGN_ISSUANCE_TRAIL, *history);
        }
    }

    for (auto it{cursor.Begin()}; it != cursor.End();) {
        if (it->second.IsDirty()) {
            CoinEntry entry(&it->first);
            if (it->second.coin.IsSpent()) {
                batch.Erase(entry);
            } else {
                batch.Write(entry, it->second.coin);
            }
        }
        count++;
        it = cursor.NextAndMaybeErase(*it);
        if (batch.ApproximateSize() > m_options.batch_write_bytes) {
            LogDebug(BCLog::COINDB, "Writing partial batch of %.2f MiB\n", batch.ApproximateSize() / double(1_MiB));

            m_db->WriteBatch(batch);
            batch.Clear();
            if (m_options.simulate_crash_ratio) {
                static FastRandomContext rng;
                if (rng.randrange(m_options.simulate_crash_ratio) == 0) {
                    LogError("Simulating a crash. Goodbye.");
                    _Exit(0);
                }
            }
        }
    }

    // In the last batch, mark the database as consistent with block_hash again.
    batch.Erase(DB_HEAD_BLOCKS);
    batch.Write(DB_BEST_BLOCK, block_hash);
    if (issuance) {
        batch.Write(DB_PGN_ISSUANCE, issuance->after);
        batch.Erase(DB_PGN_ISSUANCE_PENDING);
        if (history) {
            // Deliberately one FINAL atomic batch with I/BEST. O(number of
            // distinct blocks), potentially large: never discard intermediates.
            for (const auto& [tip, forward] : records) batch.Write(HistoryKey(tip), forward);
            batch.Erase(DB_PGN_ISSUANCE_TRAIL);
        }
    }

    if (facts) {
        for (const auto& c : facts->changes) {
            if (c.after) batch.Write(FactKey(c.fact), *c.after);
            else batch.Erase(FactKey(c.fact));
        }
        batch.Write(DB_PGN_FACT_HEAD, facts->after);
        batch.Erase(DB_PGN_FACT_PENDING);
        if (fact_history) {
            for (const auto& [tip, forward] : fact_records) batch.Write(FactForwardKey(tip), forward);
            batch.Erase(DB_PGN_FACT_TRAIL);
        }
    }
    LogDebug(BCLog::COINDB, "Writing final batch of %.2f MiB\n", batch.ApproximateSize() / double(1_MiB));
    m_db->WriteBatch(batch);
    LogDebug(BCLog::COINDB, "Committed %u changed transaction outputs (out of %u) to coin database...", (unsigned int)dirty_count, (unsigned int)count);
}

size_t CCoinsViewDB::EstimateSize() const
{
    return m_db->EstimateSize(DB_COIN, uint8_t(DB_COIN + 1));
}

/** Specialization of CCoinsViewCursor to iterate over a CCoinsViewDB */
class CCoinsViewDBCursor: public CCoinsViewCursor
{
public:
    // Prefer using CCoinsViewDB::Cursor() since we want to perform some
    // cache warmup on instantiation.
    CCoinsViewDBCursor(CDBIterator* pcursorIn, const uint256& in_block_hash):
        CCoinsViewCursor(in_block_hash), pcursor(pcursorIn) {}
    ~CCoinsViewDBCursor() = default;

    bool GetKey(COutPoint &key) const override;
    bool GetValue(Coin &coin) const override;

    bool Valid() const override;
    void Next() override;

private:
    std::unique_ptr<CDBIterator> pcursor;
    std::pair<char, COutPoint> keyTmp;

    friend class CCoinsViewDB;
};

std::unique_ptr<CCoinsViewCursor> CCoinsViewDB::Cursor() const
{
    auto i = std::make_unique<CCoinsViewDBCursor>(
        const_cast<CDBWrapper&>(*m_db).NewIterator(), GetBestBlock());
    /* It seems that there are no "const iterators" for LevelDB.  Since we
       only need read operations on it, use a const-cast to get around
       that restriction.  */
    i->pcursor->Seek(DB_COIN);
    // Cache key of first record
    if (i->pcursor->Valid()) {
        CoinEntry entry(&i->keyTmp.second);
        i->pcursor->GetKey(entry);
        i->keyTmp.first = entry.key;
    } else {
        i->keyTmp.first = 0; // Make sure Valid() and GetKey() return false
    }
    return i;
}

bool CCoinsViewDBCursor::GetKey(COutPoint &key) const
{
    // Return cached key
    if (keyTmp.first == DB_COIN) {
        key = keyTmp.second;
        return true;
    }
    return false;
}

bool CCoinsViewDBCursor::GetValue(Coin &coin) const
{
    return pcursor->GetValue(coin);
}

bool CCoinsViewDBCursor::Valid() const
{
    return keyTmp.first == DB_COIN;
}

void CCoinsViewDBCursor::Next()
{
    pcursor->Next();
    CoinEntry entry(&keyTmp.second);
    if (!pcursor->Valid() || !pcursor->GetKey(entry)) {
        keyTmp.first = 0; // Invalidate cached key after last record so that Valid() and GetKey() return false
    } else {
        keyTmp.first = entry.key;
    }
}
