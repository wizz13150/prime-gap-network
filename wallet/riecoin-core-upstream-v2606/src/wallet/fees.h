// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WALLET_FEES_H
#define BITCOIN_WALLET_FEES_H

#include <consensus/amount.h>
#include <pgn/fee_policy_error.h>
#include <pgn/monetary_context.h>

class CFeeRate;
struct FeeCalculation;

namespace wallet {
class CCoinControl;
class CWallet;

/** Local metadata/provider failure, never a numeric zero or a consensus verdict. */
using WalletFeePolicyError = pgn::core::FeePolicyError;
/** Immutable metadata for one complete fee computation, including nested
 * getters. Neither field selects/approves a chain or supplies provider prices.
 * Production wrappers below copy selected metadata once; explicit overloads
 * also permit isolated native composition without a global chain/profile. */
struct WalletFeePolicySnapshot {
    const std::optional<pgn::core::AtomicFeeRatePolicy> rate_policy{};
    const std::optional<Consensus::AmountRules> amount_rules{};
};
/** Fail before wallet mutation/key reservation if fee metadata is unavailable. */
void CheckWalletFeePolicy();
/** Also require an actual chain interface before any wallet-provider access. */
void CheckWalletFeePolicy(const CWallet& wallet);

/**
 * Return the minimum required absolute fee for this size
 * based on the required fee rate
 */
CAmount GetRequiredFee(const CWallet& wallet, unsigned int nTxBytes);

/**
 * Estimate the minimum fee considering user set parameters
 * and the required fee
 */
CAmount GetMinimumFee(const CWallet& wallet, unsigned int nTxBytes, const CCoinControl& coin_control, FeeCalculation* feeCalc);

/**
 * Return the minimum required feerate taking into account the
 * minimum relay feerate and user set minimum transaction feerate
 */
CFeeRate GetRequiredFeeRate(const CWallet& wallet);

/**
 * Estimate the minimum fee rate considering user set parameters
 * and the required fee
 */
CFeeRate GetMinimumFeeRate(const CWallet& wallet, const CCoinControl& coin_control, FeeCalculation* feeCalc);

/**
 * Return the maximum feerate for discarding change.
 */
CFeeRate GetDiscardRate(const CWallet& wallet);

/** Same native bodies with an explicitly supplied snapshot. Both metadata
 * fields must be present/valid before providers are queried, including for a
 * rate-only result. The amount domain NEVER becomes a rate ceiling. No API
 * here publishes this snapshot or enables wallet/node startup. */
CAmount GetRequiredFee(const CWallet& wallet, unsigned int nTxBytes, const WalletFeePolicySnapshot& snapshot);
CAmount GetMinimumFee(const CWallet& wallet, unsigned int nTxBytes, const CCoinControl& coin_control, FeeCalculation* feeCalc, const WalletFeePolicySnapshot& snapshot);
CFeeRate GetRequiredFeeRate(const CWallet& wallet, const WalletFeePolicySnapshot& snapshot);
CFeeRate GetMinimumFeeRate(const CWallet& wallet, const CCoinControl& coin_control, FeeCalculation* feeCalc, const WalletFeePolicySnapshot& snapshot);
CFeeRate GetDiscardRate(const CWallet& wallet, const WalletFeePolicySnapshot& snapshot);
} // namespace wallet

#endif // BITCOIN_WALLET_FEES_H
