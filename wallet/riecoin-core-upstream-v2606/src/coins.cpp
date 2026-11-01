// Copyright (c) 2012-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <coins.h>

#include <consensus/consensus.h>
#include <random.h>
#include <uint256.h>
#include <util/log.h>
#include <util/trace.h>
#include <algorithm>

TRACEPOINT_SEMAPHORE(utxocache, add);
TRACEPOINT_SEMAPHORE(utxocache, spent);
TRACEPOINT_SEMAPHORE(utxocache, uncache);

namespace {
using IssuanceCommit = pgn::core::IssuanceCommit;
using IssuanceHistoryTrail = pgn::core::IssuanceHistoryTrail;
using IssuanceDirection = pgn::core::IssuanceDirection;

void CheckFactHistoryAgainstView(const pgn::core::FactHistoryTrail& trail,
    const pgn::core::FactCommit& net, const CCoinsView& parent)
{
    if (!trail.ValidFor(net)) throw std::logic_error("Fact history/net mismatch");
    if (const auto pending = parent.GetPendingPgnFactCommit()) {
        if (*pending != net || parent.GetPendingPgnFactHistory() != std::optional{trail})
            throw std::logic_error("Fact history replay requires exact pending trail");
        return; // Actual DB checks the whole trail against raw prior claim keys.
    }
    std::map<uint256, pgn::core::FactCommit> earlier;
    for (const auto& step : trail.steps) {
        const auto known = parent.GetPgnFactForward(step.forward.after.tip);
        if (known && *known != step.forward) throw std::logic_error("Conflicting immutable fact forward");
        if (step.direction == pgn::core::FactDirection::Rewind && !known &&
            !earlier.contains(step.forward.after.tip))
            throw std::logic_error("Fact rewind requires an archived or earlier actual forward");
        if (step.direction == pgn::core::FactDirection::Forward)
            earlier.emplace(step.forward.after.tip, step.forward);
    }
    pgn::core::CheckFactTrailValues(trail, [&](const uint256& key) { return parent.GetPgnFact(key); });
}

std::optional<pgn::core::FactCommit> ComposeFacts(
    const std::optional<pgn::core::FactCommit>& staged,
    const pgn::core::FactCommit& incoming, const uint256& new_tip,
    const uint256& old_tip, const CCoinsView& parent)
{
    if (!incoming.Valid() || incoming.after.tip != new_tip)
        throw std::logic_error("PGN facts/incoming coins tip mismatch");
    if (!staged) {
        if (const auto pending = parent.GetPendingPgnFactCommit()) {
            if (*pending != incoming || !old_tip.IsNull() ||
                parent.GetHeadBlocks() != std::vector<uint256>{new_tip, incoming.OldTip()})
                throw std::logic_error("PGN facts require exact pending replay");
            return incoming; // DB rechecks every prior key before publication.
        }
        if (parent.GetPgnFactHead() != incoming.before || incoming.OldTip() != old_tip ||
            !parent.GetHeadBlocks().empty())
            throw std::logic_error("PGN facts/base snapshot mismatch");
    } else if (incoming.before != std::optional{staged->after} || old_tip != staged->after.tip) {
        throw std::logic_error("PGN facts/staged snapshot mismatch");
    }
    for (const auto& c : incoming.changes) {
        std::optional<pgn::core::FactClaim> existing;
        bool in_staged = false;
        if (staged) {
            const auto it = std::lower_bound(staged->changes.begin(), staged->changes.end(), c.fact,
                [](const auto& value, const auto& key) { return value.fact < key; });
            if (it != staged->changes.end() && it->fact == c.fact) {
                existing = it->after; in_staged = true;
            }
        }
        const bool known_base = staged ? staged->before.has_value() : incoming.before.has_value();
        if (!in_staged && known_base) existing = parent.GetPgnFact(c.fact);
        if (existing != c.before) throw std::logic_error("PGN fact already claimed or prior claim mismatch");
    }
    return staged ? pgn::core::ComposeFactCommits(*staged, incoming) : incoming;
}

void CheckHistoryAgainstView(const IssuanceHistoryTrail& trail, const IssuanceCommit& net,
                            const CCoinsView& parent)
{
    if (!trail.ValidFor(net)) throw std::logic_error("Issuance trail/net mismatch");
    std::map<uint256, IssuanceCommit> earlier_forwards;
    for (const auto& step : trail.steps) {
        const auto known = parent.GetPgnIssuanceForward(step.forward.after.tip);
        if (known && *known != step.forward) throw std::logic_error("Conflicting issuance block history");
        if (step.direction == IssuanceDirection::Rewind && !known &&
            !earlier_forwards.contains(step.forward.after.tip))
            throw std::logic_error("Rewind requires archived or earlier explicit forward");
        if (step.direction == IssuanceDirection::Forward)
            earlier_forwards.emplace(step.forward.after.tip, step.forward);
    }
}

/** All checks happen before the native cursor or coin map can be consumed.
 * Full snapshot comparison prevents matching only a hash with different money.
 * Both the pending object and its callers remain local storage metadata, not a
 * verifier for block/award authenticity or an economic policy. */
std::optional<IssuanceCommit> ComposeIssuanceCommit(
    const std::optional<IssuanceCommit>& staged, const IssuanceCommit& incoming,
    const uint256& incoming_tip, const uint256& parent_tip, const CCoinsView& parent)
{
    if (!incoming.Valid() || incoming.after.tip != incoming_tip)
        throw std::logic_error("PGN issuance commit does not match the incoming coins tip");
    if (staged) {
        if (!staged->Valid() || !incoming.before || *incoming.before != staged->after)
            throw std::logic_error("PGN issuance cache prior snapshot mismatch");
        const IssuanceCommit composed{staged->before, incoming.after};
        if (!composed.Valid())
            throw std::logic_error("PGN issuance composition at the original tip must restore its exact snapshot");
        return composed;
    }
    uint256 expected_old_tip = parent_tip;
    if (expected_old_tip.IsNull()) {
        const auto heads = parent.GetHeadBlocks();
        if (!heads.empty()) {
            if (heads.size() != 2 || heads[0] != incoming.after.tip)
                throw std::logic_error("PGN issuance cache recovery HEAD mismatch");
            expected_old_tip = heads[1];
        }
    }
    if (incoming.OldTip() != expected_old_tip)
        throw std::logic_error("PGN issuance cache parent tip mismatch");
    return incoming;
}
}

CoinsViewEmpty& CoinsViewEmpty::Get()
{
    static CoinsViewEmpty instance;
    return instance;
}

std::optional<Coin> CCoinsViewCache::PeekCoin(const COutPoint& outpoint) const
{
    if (auto it{cacheCoins.find(outpoint)}; it != cacheCoins.end()) {
        return it->second.coin.IsSpent() ? std::nullopt : std::optional{it->second.coin};
    }
    return base->PeekCoin(outpoint);
}

CCoinsViewCache::CCoinsViewCache(CCoinsView* in_base, bool deterministic) :
    CCoinsViewBacked(in_base), m_deterministic(deterministic),
    cacheCoins(0, SaltedOutpointHasher(/*deterministic=*/deterministic), CCoinsMap::key_equal{}, &m_cache_coins_memory_resource)
{
    m_sentinel.second.SelfRef(m_sentinel);
}

size_t CCoinsViewCache::DynamicMemoryUsage() const {
    size_t history_memory{};
    if (m_pgn_fact_history) {
        history_memory = memusage::DynamicUsage(m_pgn_fact_history->steps);
        for (const auto& step : m_pgn_fact_history->steps)
            history_memory += memusage::DynamicUsage(step.forward.changes);
    }
    return memusage::DynamicUsage(cacheCoins) + cachedCoinsUsage +
        (m_pgn_facts ? memusage::DynamicUsage(m_pgn_facts->changes) : 0) + history_memory;
}

std::optional<Coin> CCoinsViewCache::FetchCoinFromBase(const COutPoint& outpoint) const
{
    return base->GetCoin(outpoint);
}

CCoinsMap::iterator CCoinsViewCache::FetchCoin(const COutPoint &outpoint) const {
    const auto [ret, inserted] = cacheCoins.try_emplace(outpoint);
    if (inserted) {
        if (auto coin{FetchCoinFromBase(outpoint)}) {
            ret->second.coin = std::move(*coin);
            cachedCoinsUsage += ret->second.coin.DynamicMemoryUsage();
            Assert(!ret->second.coin.IsSpent());
        } else {
            cacheCoins.erase(ret);
            return cacheCoins.end();
        }
    }
    return ret;
}

std::optional<Coin> CCoinsViewCache::GetCoin(const COutPoint& outpoint) const
{
    if (auto it{FetchCoin(outpoint)}; it != cacheCoins.end() && !it->second.coin.IsSpent()) return it->second.coin;
    return std::nullopt;
}

void CCoinsViewCache::AddCoin(const COutPoint &outpoint, Coin&& coin, bool possible_overwrite) {
    assert(!coin.IsSpent());
    if (coin.out.scriptPubKey.IsUnspendable()) return;
    CCoinsMap::iterator it;
    bool inserted;
    std::tie(it, inserted) = cacheCoins.emplace(std::piecewise_construct, std::forward_as_tuple(outpoint), std::tuple<>());
    bool fresh = false;
    if (!possible_overwrite) {
        if (!it->second.coin.IsSpent()) {
            throw std::logic_error("Attempted to overwrite an unspent coin (when possible_overwrite is false)");
        }
        // If the coin exists in this cache as a spent coin and is DIRTY, then
        // its spentness hasn't been flushed to the parent cache. We're
        // re-adding the coin to this cache now but we can't mark it as FRESH.
        // If we mark it FRESH and then spend it before the cache is flushed
        // we would remove it from this cache and would never flush spentness
        // to the parent cache.
        //
        // Re-adding a spent coin can happen in the case of a re-org (the coin
        // is 'spent' when the block adding it is disconnected and then
        // re-added when it is also added in a newly connected block).
        //
        // If the coin doesn't exist in the current cache, or is spent but not
        // DIRTY, then it can be marked FRESH.
        fresh = !it->second.IsDirty();
    }
    if (!inserted) {
        Assume(TrySub(m_dirty_count, it->second.IsDirty()));
        Assume(TrySub(cachedCoinsUsage, it->second.coin.DynamicMemoryUsage()));
    }
    it->second.coin = std::move(coin);
    CCoinsCacheEntry::SetDirty(*it, m_sentinel);
    ++m_dirty_count;
    if (fresh) CCoinsCacheEntry::SetFresh(*it, m_sentinel);
    cachedCoinsUsage += it->second.coin.DynamicMemoryUsage();
    TRACEPOINT(utxocache, add,
           outpoint.hash.data(),
           (uint32_t)outpoint.n,
           (uint32_t)it->second.coin.nHeight,
           (int64_t)it->second.coin.out.nValue,
           (bool)it->second.coin.IsCoinBase());
}

void CCoinsViewCache::EmplaceCoinInternalDANGER(COutPoint&& outpoint, Coin&& coin) {
    const auto mem_usage{coin.DynamicMemoryUsage()};
    auto [it, inserted] = cacheCoins.try_emplace(std::move(outpoint), std::move(coin));
    if (inserted) {
        CCoinsCacheEntry::SetDirty(*it, m_sentinel);
        ++m_dirty_count;
        cachedCoinsUsage += mem_usage;
    }
}

void AddCoins(CCoinsViewCache& cache, const CTransaction &tx, int nHeight, bool check_for_overwrite) {
    bool fCoinbase = tx.IsCoinBase();
    const Txid& txid = tx.GetHash();
    for (size_t i = 0; i < tx.vout.size(); ++i) {
        bool overwrite = check_for_overwrite ? cache.HaveCoin(COutPoint(txid, i)) : fCoinbase;
        // Coinbase transactions can always be overwritten, in order to correctly
        // deal with the pre-BIP30 occurrences of duplicate coinbase transactions.
        cache.AddCoin(COutPoint(txid, i), Coin(tx.vout[i], nHeight, fCoinbase), overwrite);
    }
}

bool CCoinsViewCache::SpendCoin(const COutPoint &outpoint, Coin* moveout) {
    CCoinsMap::iterator it = FetchCoin(outpoint);
    if (it == cacheCoins.end()) return false;
    Assume(TrySub(m_dirty_count, it->second.IsDirty()));
    Assume(TrySub(cachedCoinsUsage, it->second.coin.DynamicMemoryUsage()));
    TRACEPOINT(utxocache, spent,
           outpoint.hash.data(),
           (uint32_t)outpoint.n,
           (uint32_t)it->second.coin.nHeight,
           (int64_t)it->second.coin.out.nValue,
           (bool)it->second.coin.IsCoinBase());
    if (moveout) {
        *moveout = std::move(it->second.coin);
    }
    if (it->second.IsFresh()) {
        cacheCoins.erase(it);
    } else {
        CCoinsCacheEntry::SetDirty(*it, m_sentinel);
        ++m_dirty_count;
        it->second.coin.Clear();
    }
    return true;
}

static const Coin coinEmpty;

const Coin& CCoinsViewCache::AccessCoin(const COutPoint &outpoint) const {
    CCoinsMap::const_iterator it = FetchCoin(outpoint);
    if (it == cacheCoins.end()) {
        return coinEmpty;
    } else {
        return it->second.coin;
    }
}

bool CCoinsViewCache::HaveCoin(const COutPoint& outpoint) const
{
    CCoinsMap::const_iterator it = FetchCoin(outpoint);
    return (it != cacheCoins.end() && !it->second.coin.IsSpent());
}

bool CCoinsViewCache::HaveCoinInCache(const COutPoint &outpoint) const {
    CCoinsMap::const_iterator it = cacheCoins.find(outpoint);
    return (it != cacheCoins.end() && !it->second.coin.IsSpent());
}

uint256 CCoinsViewCache::GetBestBlock() const {
    if (m_block_hash.IsNull())
        m_block_hash = base->GetBestBlock();
    return m_block_hash;
}

uint256 CCoinsViewCache::PeekBestBlock() const
{
    return m_block_hash.IsNull() ? base->PeekBestBlock() : m_block_hash;
}

std::optional<pgn::core::IssuanceSnapshot> CCoinsViewCache::GetPgnIssuanceState() const
{
    // A staged snapshot is provisional caller-supplied metadata, not a claim
    // that the underlying DB has committed it (or consensus authorized it).
    const auto state = m_pgn_issuance_commit
        ? std::optional<pgn::core::IssuanceSnapshot>{m_pgn_issuance_commit->after}
        : base->GetPgnIssuanceState();
    if (!state) return {};

    // Do not call our lazy GetBestBlock(): even a rejected read must not fill
    // this cache's tip. Strict base read failures propagate, never become zero.
    const uint256 current_tip = PeekBestBlock();
    if ((m_pgn_issuance_commit && !m_pgn_issuance_commit->Valid()) ||
        !state->Valid() || state->tip != current_tip) {
        throw std::logic_error("PGN issuance read requires valid metadata at the exact cache tip");
    }
    return state;
}

void CCoinsViewCache::SetBestBlock(const uint256& in_block_hash)
{
    m_block_hash = in_block_hash;
}

std::optional<pgn::core::IssuanceSnapshot> CCoinsViewCache::GetPgnIssuanceHistoryAnchor() const
{
    const auto inherited = base->GetPgnIssuanceHistoryAnchor();
    if (inherited || !m_pgn_issuance_history) return inherited;
    const auto net = m_pgn_issuance_history->Net();
    if (!net) throw std::logic_error("Invalid staged issuance history");
    return net->before;
}

std::optional<IssuanceCommit> CCoinsViewCache::GetPgnIssuanceForward(const uint256& tip) const
{
    // Active blocks may exist only in overlay trails. Find these first, while
    // retaining the independent immutable-parent conflict check below.
    std::optional<IssuanceCommit> result;
    if (m_pgn_issuance_history) for (const auto& step : m_pgn_issuance_history->steps) {
        if (step.forward.after.tip != tip) continue;
        if (!step.Valid() || (result && *result != step.forward))
            throw std::logic_error("Conflicting staged issuance history");
        result = step.forward;
    }
    const auto inherited = base->GetPgnIssuanceForward(tip);
    if (result && inherited && *result != *inherited)
        throw std::logic_error("Conflicting inherited issuance history");
    return result ? result : inherited;
}

void CCoinsViewCache::StagePgnIssuanceHistoryStep(const pgn::core::IssuanceHistoryStep& step)
{
    const auto applied = step.Applied();
    const uint256 current_tip = m_block_hash.IsNull() ? base->GetBestBlock() : m_block_hash;
    if (!applied || applied->after.tip != current_tip)
        throw std::logic_error("Set exact history step target tip before staging");
    const auto prior = m_pgn_issuance_commit ? std::optional{m_pgn_issuance_commit->after} : base->GetPgnIssuanceState();
    const auto known = GetPgnIssuanceForward(step.forward.after.tip);
    if (known && *known != step.forward) throw std::logic_error("Conflicting immutable issuance history");
    if (prior == applied->after && known && *known == step.forward) return; // Exact repeated application, no mutation.
    if (!prior || applied->before != prior || (m_pgn_issuance_commit && !m_pgn_issuance_history))
        throw std::logic_error("History requires exact known prior, no untracked staged prefix");
    const auto next_commit = ComposeIssuanceCommit(m_pgn_issuance_commit, *applied, current_tip,
        m_pgn_issuance_commit ? uint256{} : base->GetBestBlock(), *base);
    auto next_history = m_pgn_issuance_history.value_or(IssuanceHistoryTrail{});
    next_history.steps.push_back(step);
    CheckHistoryAgainstView(next_history, *next_commit, *base);
    m_pgn_issuance_commit = next_commit;
    m_pgn_issuance_history = std::move(next_history);
}

void CCoinsViewCache::StagePgnIssuanceCommit(const pgn::core::IssuanceCommit& commit)
{
    // Avoid GetBestBlock's lazy mutation on a rejected stage operation.
    const uint256 current_tip = m_block_hash.IsNull() ? base->GetBestBlock() : m_block_hash;
    if (commit.after.tip != current_tip)
        throw std::logic_error("Stage PGN issuance only after setting its exact cache tip");
    const auto next = ComposeIssuanceCommit(m_pgn_issuance_commit, commit, current_tip,
        m_pgn_issuance_commit ? uint256{} : base->GetBestBlock(), *base);
    std::optional<IssuanceHistoryTrail> recovered;
    if (m_pgn_issuance_history || base->GetPgnIssuanceHistoryAnchor()) {
        // Existing ReplayBlocks stages only its exact J commit. Recover N from
        // the native base, NOT from hashes, UTXO or a caller's guessed trail.
        recovered = base->GetPendingPgnIssuanceHistory();
        if (m_pgn_issuance_commit || !recovered || !recovered->ValidFor(commit))
            throw std::logic_error("Enrolled history requires explicit steps or exact pending replay");
        CheckHistoryAgainstView(*recovered, commit, *base);
    }
    m_pgn_issuance_commit = next;
    m_pgn_issuance_history = std::move(recovered);
}

std::optional<pgn::core::FactHead> CCoinsViewCache::GetPgnFactHead() const
{
    const auto head = m_pgn_facts ? std::optional{m_pgn_facts->after} : base->GetPgnFactHead();
    if (head && head->tip != PeekBestBlock()) throw std::logic_error("PGN fact cache/tip mismatch");
    return head;
}

std::optional<pgn::core::FactClaim> CCoinsViewCache::GetPgnFact(const uint256& fact) const
{
    if (!GetPgnFactHead()) throw std::logic_error("PGN fact registry is not enrolled");
    if (m_pgn_facts) {
        const auto it = std::lower_bound(m_pgn_facts->changes.begin(), m_pgn_facts->changes.end(), fact,
            [](const auto& value, const auto& key) { return value.fact < key; });
        if (it != m_pgn_facts->changes.end() && it->fact == fact) return it->after;
        if (!m_pgn_facts->before) return {};
    }
    return base->GetPgnFact(fact);
}

void CCoinsViewCache::StagePgnFactCommit(const pgn::core::FactCommit& commit)
{
    if (m_block_hash != commit.after.tip) throw std::logic_error("Stage PGN facts after SetBestBlock");
    auto next = ComposeFacts(m_pgn_facts, commit, m_block_hash,
        m_pgn_facts ? m_pgn_facts->after.tip : base->GetBestBlock(), *base);
    std::optional<pgn::core::FactHistoryTrail> recovered;
    if (m_pgn_fact_history || base->GetPgnFactHistoryAnchor()) {
        recovered = base->GetPendingPgnFactHistory();
        if (m_pgn_facts || !recovered || !recovered->ValidFor(commit))
            throw std::logic_error("Enrolled fact history requires explicit steps or exact pending replay");
        CheckFactHistoryAgainstView(*recovered, commit, *base);
    }
    m_pgn_facts = std::move(next);
    m_pgn_fact_history = std::move(recovered);
}

std::optional<pgn::core::FactHead> CCoinsViewCache::GetPgnFactHistoryAnchor() const
{
    const auto inherited = base->GetPgnFactHistoryAnchor();
    if (inherited || !m_pgn_fact_history) return inherited;
    const auto net = m_pgn_fact_history->Net();
    if (!net) throw std::logic_error("Invalid staged fact history");
    return net->before;
}
std::optional<pgn::core::FactCommit> CCoinsViewCache::GetPgnFactForward(const uint256& tip) const
{
    std::optional<pgn::core::FactCommit> result;
    if (m_pgn_fact_history) for (const auto& step : m_pgn_fact_history->steps) {
        if (step.forward.after.tip != tip) continue;
        if (!step.Valid() || (result && *result != step.forward))
            throw std::logic_error("Conflicting staged fact forwards");
        result = step.forward;
    }
    const auto inherited = base->GetPgnFactForward(tip);
    if (result && inherited && *result != *inherited)
        throw std::logic_error("Conflicting inherited fact forward");
    return result ? result : inherited;
}
void CCoinsViewCache::StagePgnFactHistoryStep(const pgn::core::FactHistoryStep& step)
{
    const auto applied = step.Applied();
    if (!applied || applied->after.tip != m_block_hash)
        throw std::logic_error("Set exact target tip before fact history staging");
    const auto prior = m_pgn_facts ? std::optional{m_pgn_facts->after} : base->GetPgnFactHead();
    const auto known = GetPgnFactForward(step.forward.after.tip);
    if (known && *known != step.forward) throw std::logic_error("Conflicting immutable fact history");
    if (prior == std::optional{applied->after} && known && *known == step.forward) return;
    if (!prior || applied->before != prior || (m_pgn_facts && !m_pgn_fact_history))
        throw std::logic_error("Fact history requires exact prior, no untracked prefix");
    const auto next = ComposeFacts(m_pgn_facts, *applied, m_block_hash,
        m_pgn_facts ? m_pgn_facts->after.tip : base->GetBestBlock(), *base);
    auto trail = m_pgn_fact_history.value_or(pgn::core::FactHistoryTrail{});
    trail.steps.push_back(step);
    CheckFactHistoryAgainstView(trail, *next, *base);
    m_pgn_facts = next;
    m_pgn_fact_history = std::move(trail);
}

void CCoinsViewCache::BatchWrite(CoinsViewCacheCursor& cursor, const uint256& in_block_hash)
{
    // Preserve the legacy no-metadata path without extra parent/database reads.
    auto next_issuance = m_pgn_issuance_commit;
    auto next_history = m_pgn_issuance_history;
    if (m_pgn_issuance_commit || cursor.PgnIssuanceCommit()) {
        const uint256 current_tip = m_block_hash.IsNull() ? base->GetBestBlock() : m_block_hash;
        if (m_pgn_issuance_commit && m_pgn_issuance_commit->after.tip != current_tip)
            throw std::logic_error("PGN issuance cache payload/tip are incoherent");
        if (cursor.PgnIssuanceCommit()) {
            next_issuance = ComposeIssuanceCommit(m_pgn_issuance_commit, *cursor.PgnIssuanceCommit(),
                in_block_hash, current_tip, *base);
        } else if (in_block_hash != m_pgn_issuance_commit->after.tip || cursor.GetDirtyCount() != 0) {
            throw std::logic_error("A child coins cache cannot change a staged issuance ledger without a commit");
        }
    }
    if (cursor.PgnIssuanceHistory()) {
        if (!cursor.PgnIssuanceCommit() || (m_pgn_issuance_commit && !m_pgn_issuance_history))
            throw std::logic_error("History cannot skip an untracked staged prefix");
        if (next_history) {
            const auto& incoming = cursor.PgnIssuanceHistory()->steps;
            next_history->steps.insert(next_history->steps.end(), incoming.begin(), incoming.end());
        } else next_history = cursor.PgnIssuanceHistory();
        if (!next_issuance) throw std::logic_error("History requires issuance commit");
        CheckHistoryAgainstView(*next_history, *next_issuance, *base);
    } else if (cursor.PgnIssuanceCommit() && (next_history || base->GetPgnIssuanceHistoryAnchor())) {
        throw std::logic_error("Child cannot discard enrolled issuance history");
    }
    auto next_facts = m_pgn_facts;
    auto next_fact_history = m_pgn_fact_history;
    if (cursor.PgnFacts()) {
        next_facts = ComposeFacts(m_pgn_facts, *cursor.PgnFacts(), in_block_hash,
            m_block_hash.IsNull() ? base->GetBestBlock() : m_block_hash, *base);
    } else if (const auto known = m_pgn_facts ? std::optional{m_pgn_facts->after} : base->GetPgnFactHead()) {
        if (in_block_hash != known->tip || cursor.GetDirtyCount() != 0)
            throw std::logic_error("Child cache cannot discard PGN fact transition");
    }
    if (cursor.PgnFactHistory()) {
        if (!cursor.PgnFacts() || (m_pgn_facts && !m_pgn_fact_history))
            throw std::logic_error("Child fact history skips an untracked prefix");
        if (next_fact_history) {
            const auto& steps = cursor.PgnFactHistory()->steps;
            next_fact_history->steps.insert(next_fact_history->steps.end(), steps.begin(), steps.end());
        } else next_fact_history = cursor.PgnFactHistory();
        if (!next_facts) throw std::logic_error("Fact history without net transition");
        CheckFactHistoryAgainstView(*next_fact_history, *next_facts, *base);
    } else if (cursor.PgnFacts() && (next_fact_history || base->GetPgnFactHistoryAnchor())) {
        throw std::logic_error("Child cache discards enrolled fact history");
    }
    for (auto it{cursor.Begin()}; it != cursor.End(); it = cursor.NextAndMaybeErase(*it)) {
        if (!it->second.IsDirty()) { // TODO a cursor can only contain dirty entries
            continue;
        }
        auto [itUs, inserted]{cacheCoins.try_emplace(it->first)};
        if (inserted) {
            if (it->second.IsFresh() && it->second.coin.IsSpent()) {
                cacheCoins.erase(itUs); // TODO fresh coins should have been removed at spend
            } else {
                // The parent cache does not have an entry, while the child cache does.
                // Move the data up and mark it as dirty.
                CCoinsCacheEntry& entry{itUs->second};
                assert(entry.coin.DynamicMemoryUsage() == 0);
                if (cursor.WillErase(*it)) {
                    // Since this entry will be erased,
                    // we can move the coin into us instead of copying it
                    entry.coin = std::move(it->second.coin);
                } else {
                    entry.coin = it->second.coin;
                }
                CCoinsCacheEntry::SetDirty(*itUs, m_sentinel);
                ++m_dirty_count;
                cachedCoinsUsage += entry.coin.DynamicMemoryUsage();
                // We can mark it FRESH in the parent if it was FRESH in the child
                // Otherwise it might have just been flushed from the parent's cache
                // and already exist in the grandparent
                if (it->second.IsFresh()) CCoinsCacheEntry::SetFresh(*itUs, m_sentinel);
            }
        } else {
            // Found the entry in the parent cache
            if (it->second.IsFresh() && !itUs->second.coin.IsSpent()) {
                // The coin was marked FRESH in the child cache, but the coin
                // exists in the parent cache. If this ever happens, it means
                // the FRESH flag was misapplied and there is a logic error in
                // the calling code.
                throw std::logic_error("FRESH flag misapplied to coin that exists in parent cache");
            }

            if (itUs->second.IsFresh() && it->second.coin.IsSpent()) {
                // The grandparent cache does not have an entry, and the coin
                // has been spent. We can just delete it from the parent cache.
                Assume(TrySub(m_dirty_count, itUs->second.IsDirty()));
                Assume(TrySub(cachedCoinsUsage, itUs->second.coin.DynamicMemoryUsage()));
                cacheCoins.erase(itUs);
            } else {
                // A normal modification.
                Assume(TrySub(cachedCoinsUsage, itUs->second.coin.DynamicMemoryUsage()));
                if (cursor.WillErase(*it)) {
                    // Since this entry will be erased,
                    // we can move the coin into us instead of copying it
                    itUs->second.coin = std::move(it->second.coin);
                } else {
                    itUs->second.coin = it->second.coin;
                }
                cachedCoinsUsage += itUs->second.coin.DynamicMemoryUsage();
                if (!itUs->second.IsDirty()) {
                    CCoinsCacheEntry::SetDirty(*itUs, m_sentinel);
                    ++m_dirty_count;
                }
                // NOTE: It isn't safe to mark the coin as FRESH in the parent
                // cache. If it already existed and was spent in the parent
                // cache then marking it FRESH would prevent that spentness
                // from being flushed to the grandparent.
            }
        }
    }
    SetBestBlock(in_block_hash);
    m_pgn_issuance_commit = std::move(next_issuance);
    m_pgn_issuance_history = std::move(next_history);
    m_pgn_facts = std::move(next_facts);
    m_pgn_fact_history = std::move(next_fact_history);
}

void CCoinsViewCache::Flush(bool reallocate_cache)
{
    if (m_pgn_facts && m_pgn_facts->after.tip != m_block_hash)
        throw std::logic_error("PGN facts changed without matching coins tip");
    if (m_pgn_issuance_commit && m_pgn_issuance_commit->after.tip != m_block_hash)
        throw std::logic_error("PGN issuance cache tip changed without a matching staged commit");
    auto cursor{CoinsViewCacheCursor(m_dirty_count, m_sentinel, cacheCoins, /*will_erase=*/true, m_pgn_issuance_commit, m_pgn_issuance_history, m_pgn_facts, m_pgn_fact_history)};
    base->BatchWrite(cursor, m_block_hash);
    m_pgn_issuance_commit.reset(); // Only after successful publication to the base.
    m_pgn_facts.reset();
    m_pgn_fact_history.reset();
    m_pgn_issuance_history.reset();
    Assume(m_dirty_count == 0);
    cacheCoins.clear();
    if (reallocate_cache) {
        ReallocateCache();
    }
    cachedCoinsUsage = 0;
}

void CCoinsViewCache::Sync()
{
    if (m_pgn_facts && m_pgn_facts->after.tip != m_block_hash)
        throw std::logic_error("PGN facts changed without matching coins tip");
    if (m_pgn_issuance_commit && m_pgn_issuance_commit->after.tip != m_block_hash)
        throw std::logic_error("PGN issuance cache tip changed without a matching staged commit");
    auto cursor{CoinsViewCacheCursor(m_dirty_count, m_sentinel, cacheCoins, /*will_erase=*/false, m_pgn_issuance_commit, m_pgn_issuance_history, m_pgn_facts, m_pgn_fact_history)};
    base->BatchWrite(cursor, m_block_hash);
    m_pgn_issuance_commit.reset();
    m_pgn_facts.reset();
    m_pgn_fact_history.reset();
    m_pgn_issuance_history.reset();
    Assume(m_dirty_count == 0);
    if (m_sentinel.second.Next() != &m_sentinel) {
        /* BatchWrite must clear flags of all entries */
        throw std::logic_error("Not all unspent flagged entries were cleared");
    }
}

void CCoinsViewCache::Reset() noexcept
{
    m_pgn_facts.reset();
    m_pgn_fact_history.reset();
    cacheCoins.clear();
    cachedCoinsUsage = 0;
    m_dirty_count = 0;
    m_pgn_issuance_commit.reset();
    m_pgn_issuance_history.reset();
    SetBestBlock(uint256::ZERO);
}

void CCoinsViewCache::Uncache(const COutPoint& hash)
{
    CCoinsMap::iterator it = cacheCoins.find(hash);
    if (it != cacheCoins.end() && !it->second.IsDirty()) {
        Assume(TrySub(cachedCoinsUsage, it->second.coin.DynamicMemoryUsage()));
        TRACEPOINT(utxocache, uncache,
               hash.hash.data(),
               (uint32_t)hash.n,
               (uint32_t)it->second.coin.nHeight,
               (int64_t)it->second.coin.out.nValue,
               (bool)it->second.coin.IsCoinBase());
        cacheCoins.erase(it);
    }
}

unsigned int CCoinsViewCache::GetCacheSize() const {
    return cacheCoins.size();
}

bool CCoinsViewCache::HaveInputs(const CTransaction& tx) const
{
    if (!tx.IsCoinBase()) {
        for (unsigned int i = 0; i < tx.vin.size(); i++) {
            if (!HaveCoin(tx.vin[i].prevout)) {
                return false;
            }
        }
    }
    return true;
}

void CCoinsViewCache::ReallocateCache()
{
    // Cache should be empty when we're calling this.
    assert(cacheCoins.size() == 0);
    cacheCoins.~CCoinsMap();
    m_cache_coins_memory_resource.~CCoinsMapMemoryResource();
    ::new (&m_cache_coins_memory_resource) CCoinsMapMemoryResource{};
    ::new (&cacheCoins) CCoinsMap{0, SaltedOutpointHasher{/*deterministic=*/m_deterministic}, CCoinsMap::key_equal{}, &m_cache_coins_memory_resource};
}

void CCoinsViewCache::SanityCheck() const
{
    size_t recomputed_usage = 0;
    size_t count_dirty = 0;
    for (const auto& [_, entry] : cacheCoins) {
        if (entry.coin.IsSpent()) {
            assert(entry.IsDirty() && !entry.IsFresh()); // A spent coin must be dirty and cannot be fresh
        } else {
            assert(entry.IsDirty() || !entry.IsFresh()); // An unspent coin must not be fresh if not dirty
        }

        // Recompute cachedCoinsUsage.
        recomputed_usage += entry.coin.DynamicMemoryUsage();

        // Count the number of entries we expect in the linked list.
        if (entry.IsDirty()) ++count_dirty;
    }
    // Iterate over the linked list of flagged entries.
    size_t count_linked = 0;
    for (auto it = m_sentinel.second.Next(); it != &m_sentinel; it = it->second.Next()) {
        // Verify linked list integrity.
        assert(it->second.Next()->second.Prev() == it);
        assert(it->second.Prev()->second.Next() == it);
        // Verify they are actually flagged.
        assert(it->second.IsDirty());
        // Count the number of entries actually in the list.
        ++count_linked;
    }
    assert(count_dirty == count_linked && count_dirty == m_dirty_count);
    assert(recomputed_usage == cachedCoinsUsage);
}

static const uint64_t MIN_TRANSACTION_OUTPUT_WEIGHT{WITNESS_SCALE_FACTOR * ::GetSerializeSize(CTxOut())};
static const uint64_t MAX_OUTPUTS_PER_BLOCK{MAX_BLOCK_WEIGHT / MIN_TRANSACTION_OUTPUT_WEIGHT};

const Coin& AccessByTxid(const CCoinsViewCache& view, const Txid& txid)
{
    COutPoint iter(txid, 0);
    while (iter.n < MAX_OUTPUTS_PER_BLOCK) {
        const Coin& alternate = view.AccessCoin(iter);
        if (!alternate.IsSpent()) return alternate;
        ++iter.n;
    }
    return coinEmpty;
}

template <typename ReturnType, typename Func>
static ReturnType ExecuteBackedWrapper(Func func, const std::vector<std::function<void()>>& err_callbacks)
{
    try {
        return func();
    } catch(const std::runtime_error& e) {
        for (const auto& f : err_callbacks) {
            f();
        }
        LogError("Error reading from database: %s\n", e.what());
        // Starting the shutdown sequence and returning false to the caller would be
        // interpreted as 'entry not found' (as opposed to unable to read data), and
        // could lead to invalid interpretation. Just exit immediately, as we can't
        // continue anyway, and all writes should be atomic.
        std::abort();
    }
}

std::optional<Coin> CCoinsViewErrorCatcher::GetCoin(const COutPoint& outpoint) const
{
    return ExecuteBackedWrapper<std::optional<Coin>>([&]() { return CCoinsViewBacked::GetCoin(outpoint); }, m_err_callbacks);
}

bool CCoinsViewErrorCatcher::HaveCoin(const COutPoint& outpoint) const
{
    return ExecuteBackedWrapper<bool>([&]() { return CCoinsViewBacked::HaveCoin(outpoint); }, m_err_callbacks);
}

std::optional<Coin> CCoinsViewErrorCatcher::PeekCoin(const COutPoint& outpoint) const
{
    return ExecuteBackedWrapper<std::optional<Coin>>([&]() { return CCoinsViewBacked::PeekCoin(outpoint); }, m_err_callbacks);
}
