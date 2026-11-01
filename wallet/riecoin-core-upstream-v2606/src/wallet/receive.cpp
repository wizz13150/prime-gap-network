// Copyright (c) 2021-present The Bitcoin Core developers
// Copyright (c) 2013-present The Riecoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/amount.h>
#include <chainparams.h>
#include <consensus/consensus.h>
#include <pgn/balance_arithmetic.h>
#include <pgn/wallet_domain.h>
#include <util/check.h>
#include <wallet/receive.h>
#include <wallet/transaction.h>
#include <wallet/wallet.h>

namespace wallet {
bool InputIsMine(const CWallet& wallet, const CTxIn& txin)
{
    AssertLockHeld(wallet.cs_wallet);
    const CWalletTx* prev = wallet.GetWalletTx(txin.prevout.hash);
    if (prev && txin.prevout.n < prev->tx->vout.size()) {
        return wallet.IsMine(prev->tx->vout[txin.prevout.n]);
    }
    return false;
}

bool AllInputsMine(const CWallet& wallet, const CTransaction& tx)
{
    LOCK(wallet.cs_wallet);
    for (const CTxIn& txin : tx.vin) {
        if (!InputIsMine(wallet, txin)) return false;
    }
    return true;
}

static CAmount OutputCreditForDomain(const CWallet& wallet, const CTxOut& txout,
                                    const Consensus::AmountRules& rules)
{
    AssertLockHeld(wallet.cs_wallet);
    (void)pgn::core::CheckedWalletDomainCredit(rules, 0, txout.nValue);
    return (wallet.IsMine(txout) ? txout.nValue : 0);
}

CAmount OutputGetCredit(const CWallet& wallet, const CTxOut& txout)
{
    const auto rules = pgn::core::RequireWalletDomain(SelectedPgnConsensusAmountRules());
    LOCK(wallet.cs_wallet);
    return OutputCreditForDomain(wallet, txout, rules);
}

CAmount TxGetCredit(const CWallet& wallet, const CTransaction& tx)
{
    const auto rules = pgn::core::RequireWalletDomain(SelectedPgnConsensusAmountRules());
    LOCK(wallet.cs_wallet);
    CAmount nCredit = 0;
    for (const CTxOut& txout : tx.vout)
    {
        nCredit = pgn::core::CheckedWalletDomainCredit(
            rules, nCredit, OutputCreditForDomain(wallet, txout, rules));
    }
    return nCredit;
}

bool ScriptIsChange(const CWallet& wallet, const CScript& script)
{
    // TODO: fix handling of 'change' outputs. The assumption is that any
    // payment to a script that is ours, but is not in the address book
    // is change. That assumption is likely to break when we implement multisignature
    // wallets that return change back into a multi-signature-protected address;
    // a better way of identifying which outputs are 'the send' and which are
    // 'the change' will need to be implemented (maybe extend CWalletTx to remember
    // which output, if any, was change).
    AssertLockHeld(wallet.cs_wallet);
    if (wallet.IsMine(script))
    {
        CTxDestination address;
        if (!ExtractDestination(script, address))
            return true;
        if (!wallet.FindAddressBookEntry(address)) {
            return true;
        }
    }
    return false;
}

bool OutputIsChange(const CWallet& wallet, const CTxOut& txout)
{
    return ScriptIsChange(wallet, txout.scriptPubKey);
}

static CAmount OutputChangeForDomain(const CWallet& wallet, const CTxOut& txout,
                                    const Consensus::AmountRules& rules)
{
    AssertLockHeld(wallet.cs_wallet);
    (void)pgn::core::CheckedWalletDomainCredit(rules, 0, txout.nValue);
    return (OutputIsChange(wallet, txout) ? txout.nValue : 0);
}

CAmount OutputGetChange(const CWallet& wallet, const CTxOut& txout)
{
    const auto rules = pgn::core::RequireWalletDomain(SelectedPgnConsensusAmountRules());
    return OutputChangeForDomain(wallet, txout, rules);
}

CAmount TxGetChange(const CWallet& wallet, const CTransaction& tx)
{
    const auto rules = pgn::core::RequireWalletDomain(SelectedPgnConsensusAmountRules());
    LOCK(wallet.cs_wallet);
    CAmount nChange = 0;
    for (const CTxOut& txout : tx.vout)
    {
        nChange = pgn::core::CheckedWalletDomainCredit(
            rules, nChange, OutputChangeForDomain(wallet, txout, rules));
    }
    return nChange;
}

static CAmount GetCachableAmount(const CWallet& wallet, const CWalletTx& wtx, CWalletTx::AmountType type, bool avoid_reuse)
{
    const auto rules = pgn::core::RequireWalletDomain(SelectedPgnConsensusAmountRules());
    auto& amount = wtx.m_amounts[type];
    if (!amount.IsCached(avoid_reuse)) {
        amount.Set(avoid_reuse, type == CWalletTx::DEBIT ? wallet.GetDebit(*wtx.tx) : TxGetCredit(wallet, *wtx.tx));
        wtx.m_is_cache_empty = false;
    }
    return pgn::core::CheckedWalletDomainCredit(rules, 0, amount.Get(avoid_reuse));
}

CAmount CachedTxGetCredit(const CWallet& wallet, const CWalletTx& wtx, bool avoid_reuse)
{
    AssertLockHeld(wallet.cs_wallet);

    // Must wait until coinbase is safely deep enough in the chain before valuing it
    (void)pgn::core::RequireWalletDomain(SelectedPgnConsensusAmountRules());
    if (wallet.IsTxImmatureCoinBase(wtx))
        return 0;

    // GetBalance can assume transactions in mapWallet won't change
    return GetCachableAmount(wallet, wtx, CWalletTx::CREDIT, avoid_reuse);
}

CAmount CachedTxGetDebit(const CWallet& wallet, const CWalletTx& wtx, bool avoid_reuse)
{
    (void)pgn::core::RequireWalletDomain(SelectedPgnConsensusAmountRules());
    if (wtx.tx->vin.empty())
        return 0;

    return GetCachableAmount(wallet, wtx, CWalletTx::DEBIT, avoid_reuse);
}

CAmount CachedTxGetChange(const CWallet& wallet, const CWalletTx& wtx)
{
    const auto rules = pgn::core::RequireWalletDomain(SelectedPgnConsensusAmountRules());
    if (wtx.fChangeCached)
        return pgn::core::CheckedWalletDomainCredit(rules, 0, wtx.nChangeCached);
    wtx.nChangeCached = TxGetChange(wallet, *wtx.tx);
    wtx.fChangeCached = true;
    return wtx.nChangeCached;
}

void CachedTxGetAmounts(const CWallet& wallet, const CWalletTx& wtx,
                  std::list<COutputEntry>& listReceived,
                  std::list<COutputEntry>& listSent, CAmount& nFee,
                  bool include_change)
{
    const auto rules = pgn::core::RequireWalletDomain(SelectedPgnConsensusAmountRules());
    nFee = 0;
    listReceived.clear();
    listSent.clear();

    // Compute fee:
    CAmount nDebit = CachedTxGetDebit(wallet, wtx, /*avoid_reuse=*/false);
    if (nDebit > 0) // debit>0 means we signed/sent this transaction
    {
        CAmount nValueOut = wtx.tx->GetValueOut(rules);
        nFee = nDebit - nValueOut;
    }

    LOCK(wallet.cs_wallet);
    // Sent/received.
    for (unsigned int i = 0; i < wtx.tx->vout.size(); ++i)
    {
        const CTxOut& txout = wtx.tx->vout[i];
        bool ismine = wallet.IsMine(txout);
        // Only need to handle txouts if AT LEAST one of these is true:
        //   1) they debit from us (sent)
        //   2) the output is to us (received)
        if (nDebit > 0)
        {
            if (!include_change && OutputIsChange(wallet, txout))
                continue;
        }
        else if (!ismine)
            continue;

        // In either case, we need to get the destination address
        CTxDestination address;

        if (!ExtractDestination(txout.scriptPubKey, address) && !txout.scriptPubKey.IsUnspendable())
        {
            wallet.WalletLogPrintf("CWalletTx::GetAmounts: Unknown transaction type found, txid %s\n",
                                    wtx.GetHash().ToString());
            address = CNoDestination();
        }

        COutputEntry output = {address, txout.nValue, (int)i};

        // If we are debited by the transaction, add the output as a "sent" entry
        if (nDebit > 0)
            listSent.push_back(output);

        // If we are receiving the output, add it as a "received" entry
        if (ismine)
            listReceived.push_back(output);
    }

}

bool CachedTxIsFromMe(const CWallet& wallet, const CWalletTx& wtx)
{
    if (!wtx.m_cached_from_me.has_value()) {
        wtx.m_cached_from_me = wallet.IsFromMe(*wtx.tx);
    }
    return wtx.m_cached_from_me.value();
}

// NOLINTNEXTLINE(misc-no-recursion)
bool CachedTxIsTrusted(const CWallet& wallet, const CWalletTx& wtx, std::set<Txid>& trusted_parents)
{
    AssertLockHeld(wallet.cs_wallet);

    // This wtx is already trusted
    if (trusted_parents.contains(wtx.GetHash())) return true;

    if (wtx.isConfirmed()) return true;
    if (wtx.isBlockConflicted()) return false;
    // using wtx's cached debit
    if (!wallet.m_spend_zero_conf_change || !CachedTxIsFromMe(wallet, wtx)) return false;

    // Don't trust unconfirmed transactions from us unless they are in the mempool.
    if (!wtx.InMempool()) return false;

    // Trusted if all inputs are from us and are in the mempool:
    for (const CTxIn& txin : wtx.tx->vin)
    {
        // Transactions not sent by us: not trusted
        const CWalletTx* parent = wallet.GetWalletTx(txin.prevout.hash);
        if (parent == nullptr) return false;
        const CTxOut& parentOut = parent->tx->vout[txin.prevout.n];
        // Check that this specific input being spent is trusted
        if (!wallet.IsMine(parentOut)) return false;
        // If we've already trusted this parent, continue
        if (trusted_parents.contains(parent->GetHash())) continue;
        // Recurse to check that the parent is also trusted
        if (!CachedTxIsTrusted(wallet, *parent, trusted_parents)) return false;
        trusted_parents.insert(parent->GetHash());
    }
    return true;
}

bool CachedTxIsTrusted(const CWallet& wallet, const CWalletTx& wtx)
{
    std::set<Txid> trusted_parents;
    LOCK(wallet.cs_wallet);
    return CachedTxIsTrusted(wallet, wtx, trusted_parents);
}

Balance GetBalance(const CWallet& wallet, const int min_depth, bool avoid_reuse, bool include_nonmempool)
{
    const auto rules = pgn::core::RequireWalletDomain(SelectedPgnConsensusAmountRules());
    Balance ret;
    bool allow_used_addresses = !avoid_reuse || !wallet.IsWalletFlagSet(WALLET_FLAG_AVOID_REUSE);
    {
        LOCK(wallet.cs_wallet);
        std::set<Txid> trusted_parents;
        for (const auto& [outpoint, txo] : wallet.GetTXOs()) {
            const CWalletTx& wtx = txo.GetWalletTx();

            const bool is_trusted{CachedTxIsTrusted(wallet, wtx, trusted_parents)};
            const int tx_depth{wallet.GetTxDepthInMainChain(wtx)};

            bool nonmempool_spent = false;
            switch (wallet.HowSpent(outpoint)) {
            case CWallet::SpendType::CONFIRMED:
            case CWallet::SpendType::MEMPOOL:
                // treat as spent; ignore
                break;
            case CWallet::SpendType::NONMEMPOOL:
                if (!include_nonmempool) break;
                nonmempool_spent = true;
                [[fallthrough]];
            case CWallet::SpendType::UNSPENT:
                CAmount* bucket = nullptr;

                // Set the amounts in the return object
                if (wallet.IsTxImmatureCoinBase(wtx) && wtx.isConfirmed()) {
                    bucket = &ret.m_mine_immature;
                } else if (is_trusted && tx_depth >= min_depth) {
                    bucket = &ret.m_mine_trusted;
                } else if (!is_trusted && wtx.InMempool()) {
                    bucket = &ret.m_mine_untrusted_pending;
                }
                if (bucket) {
                    // Get the amounts for mine
                    CAmount credit_mine = txo.GetTxOut().nValue;
                    (void)pgn::core::CheckedWalletDomainCredit(rules, 0, credit_mine);

                    if (!allow_used_addresses && wallet.IsSpentKey(txo.GetTxOut().scriptPubKey)) {
                        bucket = &ret.m_mine_used;
                    }
                    pgn::core::AccumulateWalletCredit(*bucket, ret.m_mine_nonmempool,
                                                      credit_mine, nonmempool_spent);
                }
            }
        }
    }
    return ret;
}

std::map<CTxDestination, CAmount> GetAddressBalances(const CWallet& wallet)
{
    const auto rules = pgn::core::RequireWalletDomain(SelectedPgnConsensusAmountRules());
    std::map<CTxDestination, CAmount> balances;

    {
        LOCK(wallet.cs_wallet);
        std::set<Txid> trusted_parents;
        for (const auto& [outpoint, txo] : wallet.GetTXOs()) {
            const CWalletTx& wtx = txo.GetWalletTx();

            if (!CachedTxIsTrusted(wallet, wtx, trusted_parents)) continue;
            if (wallet.IsTxImmatureCoinBase(wtx)) continue;

            int nDepth = wallet.GetTxDepthInMainChain(wtx);
            if (nDepth < (CachedTxIsFromMe(wallet, wtx) ? 0 : 1)) continue;

            CTxDestination addr;
            Assume(wallet.IsMine(txo.GetTxOut()));
            if(!ExtractDestination(txo.GetTxOut().scriptPubKey, addr)) continue;

            const auto value = pgn::core::CheckedWalletDomainCredit(rules, 0, txo.GetTxOut().nValue);
            CAmount n = wallet.IsSpent(outpoint) ? 0 : value;
            auto& balance = balances[addr];
            balance = pgn::core::CheckedWalletCredit(balance, n);
        }
    }

    return balances;
}

std::set< std::set<CTxDestination> > GetAddressGroupings(const CWallet& wallet)
{
    AssertLockHeld(wallet.cs_wallet);
    std::set< std::set<CTxDestination> > groupings;
    std::set<CTxDestination> grouping;

    for (const auto& walletEntry : wallet.mapWallet)
    {
        const CWalletTx& wtx = walletEntry.second;

        if (wtx.tx->vin.size() > 0)
        {
            bool any_mine = false;
            // group all input addresses with each other
            for (const CTxIn& txin : wtx.tx->vin)
            {
                CTxDestination address;
                if(!InputIsMine(wallet, txin)) /* If this input isn't mine, ignore it */
                    continue;
                if(!ExtractDestination(wallet.mapWallet.at(txin.prevout.hash).tx->vout[txin.prevout.n].scriptPubKey, address))
                    continue;
                grouping.insert(address);
                any_mine = true;
            }

            // group change with input addresses
            if (any_mine)
            {
               for (const CTxOut& txout : wtx.tx->vout)
                   if (OutputIsChange(wallet, txout))
                   {
                       CTxDestination txoutAddr;
                       if(!ExtractDestination(txout.scriptPubKey, txoutAddr))
                           continue;
                       grouping.insert(txoutAddr);
                   }
            }
            if (grouping.size() > 0)
            {
                groupings.insert(grouping);
                grouping.clear();
            }
        }

        // group lone addrs by themselves
        for (const auto& txout : wtx.tx->vout)
            if (wallet.IsMine(txout))
            {
                CTxDestination address;
                if(!ExtractDestination(txout.scriptPubKey, address))
                    continue;
                grouping.insert(address);
                groupings.insert(grouping);
                grouping.clear();
            }
    }

    std::set< std::set<CTxDestination>* > uniqueGroupings; // a set of pointers to groups of addresses
    std::map< CTxDestination, std::set<CTxDestination>* > setmap;  // map addresses to the unique group containing it
    for (const std::set<CTxDestination>& _grouping : groupings)
    {
        // make a set of all the groups hit by this new group
        std::set< std::set<CTxDestination>* > hits;
        std::map< CTxDestination, std::set<CTxDestination>* >::iterator it;
        for (const CTxDestination& address : _grouping)
            if ((it = setmap.find(address)) != setmap.end())
                hits.insert((*it).second);

        // merge all hit groups into a new single group and delete old groups
        std::set<CTxDestination>* merged = new std::set<CTxDestination>(_grouping);
        for (std::set<CTxDestination>* hit : hits)
        {
            merged->insert(hit->begin(), hit->end());
            uniqueGroupings.erase(hit);
            delete hit;
        }
        uniqueGroupings.insert(merged);

        // update setmap
        for (const CTxDestination& element : *merged)
            setmap[element] = merged;
    }

    std::set< std::set<CTxDestination> > ret;
    for (const std::set<CTxDestination>* uniqueGrouping : uniqueGroupings)
    {
        ret.insert(*uniqueGrouping);
        delete uniqueGrouping;
    }

    return ret;
}
} // namespace wallet
