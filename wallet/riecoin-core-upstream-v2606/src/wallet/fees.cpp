// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/fees.h>

#include <chainparams.h>
#include <pgn/fee_rate_admission.h>
#include <policy/fees/block_policy_estimator.h>
#include <wallet/coincontrol.h>
#include <wallet/wallet.h>


namespace wallet {
namespace {
WalletFeePolicySnapshot SelectedSnapshot()
{
    // Selected chain parameters are immutable after startup. Copy each field
    // once, never reread selected state inside this computation's subcalls.
    return {SelectedPgnAtomicFeeRatePolicy(), SelectedPgnConsensusAmountRules()};
}
pgn::core::AtomicFeeRatePolicy RequirePolicy(const WalletFeePolicySnapshot& snapshot)
{
    if (pgn::core::ValidateAtomicFeeAdmissionPolicy(snapshot.rate_policy) != pgn::core::AtomicFeeAdmissionError::None)
        throw WalletFeePolicyError("PGN fee-rate policy is unavailable or incompatible");
    // AmountRules' public construction already enforces a positive bound.
    if (!snapshot.amount_rules || snapshot.amount_rules->MaxAtomic() <= 0)
        throw WalletFeePolicyError("PGN absolute fee amount domain is unavailable or incompatible");
    return *snapshot.rate_policy;
}
void RequireBackend(const CWallet& wallet)
{
    if (!wallet.HaveChain()) throw WalletFeePolicyError("PGN wallet fee backend is unavailable");
}
pgn::core::AtomicFeeRatePolicy RequireWalletPolicy(const CWallet& wallet, const WalletFeePolicySnapshot& snapshot)
{
    const auto policy = RequirePolicy(snapshot); // Metadata absence still fails first.
    RequireBackend(wallet);
    return policy;
}
CFeeRate AdmitRate(CFeeRate rate, const pgn::core::AtomicFeeRatePolicy& policy)
{
    if (pgn::core::AdmitAtomicFeeRate(rate, policy) != pgn::core::AtomicFeeAdmissionError::None)
        throw WalletFeePolicyError("PGN native fee rate exceeds its explicit policy or arithmetic domain");
    return rate; // Preserve the exact rational rate; no rounding to atomic/kvB.
}
CAmount QuoteRate(const CFeeRate& rate, unsigned int bytes,
                  const WalletFeePolicySnapshot& snapshot)
{
    const auto quote = pgn::core::QuoteAtomicFee(rate, bytes, snapshot.rate_policy, snapshot.amount_rules);
    if (!quote.amount)
        throw WalletFeePolicyError("PGN absolute fee quote is unavailable or outside its explicit size/amount domain");
    return *quote.amount;
}
void CheckSize(unsigned int bytes)
{
    if (bytes > static_cast<unsigned int>(std::numeric_limits<int32_t>::max()))
        throw WalletFeePolicyError("PGN fee size exceeds the native signed size domain");
}
} // namespace

void CheckWalletFeePolicy()
{
    (void)RequirePolicy(SelectedSnapshot());
}
void CheckWalletFeePolicy(const CWallet& wallet)
{
    CheckWalletFeePolicy();
    RequireBackend(wallet);
}

CAmount GetRequiredFee(const CWallet& wallet, unsigned int nTxBytes)
{
    return GetRequiredFee(wallet, nTxBytes, SelectedSnapshot());
}
CAmount GetRequiredFee(const CWallet& wallet, unsigned int nTxBytes, const WalletFeePolicySnapshot& snapshot)
{
    (void)RequireWalletPolicy(wallet, snapshot);
    CheckSize(nTxBytes); // Before narrowing or any provider call.
    return QuoteRate(GetRequiredFeeRate(wallet, snapshot), nTxBytes, snapshot);
}


CAmount GetMinimumFee(const CWallet& wallet, unsigned int nTxBytes, const CCoinControl& coin_control, FeeCalculation* feeCalc)
{
    return GetMinimumFee(wallet, nTxBytes, coin_control, feeCalc, SelectedSnapshot());
}
CAmount GetMinimumFee(const CWallet& wallet, unsigned int nTxBytes, const CCoinControl& coin_control, FeeCalculation* feeCalc, const WalletFeePolicySnapshot& snapshot)
{
    (void)RequireWalletPolicy(wallet, snapshot);
    CheckSize(nTxBytes);
    FeeCalculation candidate;
    if (feeCalc) candidate = *feeCalc;
    const auto rate = GetMinimumFeeRate(wallet, coin_control, feeCalc ? &candidate : nullptr, snapshot);
    const auto result = QuoteRate(rate, nTxBytes, snapshot);
    if (feeCalc) *feeCalc = candidate;
    return result;
}

CFeeRate GetRequiredFeeRate(const CWallet& wallet)
{
    return GetRequiredFeeRate(wallet, SelectedSnapshot());
}
CFeeRate GetRequiredFeeRate(const CWallet& wallet, const WalletFeePolicySnapshot& snapshot)
{
    const auto policy = RequireWalletPolicy(wallet, snapshot);
    const auto configured = AdmitRate(wallet.m_min_fee, policy);
    const auto relay = AdmitRate(wallet.chain().relayMinFee(), policy);
    return std::max(configured, relay);
}

CFeeRate GetMinimumFeeRate(const CWallet& wallet, const CCoinControl& coin_control, FeeCalculation* feeCalc)
{
    return GetMinimumFeeRate(wallet, coin_control, feeCalc, SelectedSnapshot());
}
CFeeRate GetMinimumFeeRate(const CWallet& wallet, const CCoinControl& coin_control, FeeCalculation* feeCalc, const WalletFeePolicySnapshot& snapshot)
{
    const auto policy = RequireWalletPolicy(wallet, snapshot);
    FeeCalculation candidate;
    if (feeCalc) candidate = *feeCalc;
    FeeCalculation* observation = feeCalc ? &candidate : nullptr;
    const auto publish = [&](CFeeRate rate) {
        rate = AdmitRate(rate, policy);
        if (feeCalc) *feeCalc = candidate;
        return rate;
    };
    /* User control of how to calculate fee uses the following parameter precedence:
       1. coin_control.m_feerate
       2. coin_control.m_confirm_target
       3. m_confirm_target (user-set member variable of wallet)
       The first parameter that is set is used.
    */
    CFeeRate feerate_needed;
    if (coin_control.m_feerate) { // 1.
        feerate_needed = AdmitRate(*coin_control.m_feerate, policy);
        // Allow to override automatic min/max check over coin control instance
        if (coin_control.fOverrideFeeRate) return publish(feerate_needed);
    }
    else { // 2. or 3.
        // We will use smart fee estimation
        unsigned int target = coin_control.m_confirm_target ? *coin_control.m_confirm_target : wallet.m_confirm_target;
        // By default estimates are economical iff we are signaling opt-in-RBF
        bool conservative_estimate = !coin_control.m_signal_bip125_rbf.value_or(wallet.m_signal_rbf);
        // Allow to override the default fee estimate mode over the CoinControl instance
        if (coin_control.m_fee_mode == FeeEstimateMode::CONSERVATIVE) conservative_estimate = true;
        else if (coin_control.m_fee_mode == FeeEstimateMode::ECONOMICAL) conservative_estimate = false;

        feerate_needed = AdmitRate(wallet.chain().estimateSmartFee(target, conservative_estimate, observation), policy);
        if (feerate_needed == CFeeRate(0)) {
            // if we don't have enough data for estimateSmartFee, then use fallback fee
            feerate_needed = AdmitRate(wallet.m_fallback_fee, policy);
            if (observation) observation->reason = FeeReason::FALLBACK;

            // Disabled fallback means no estimate, NOT a known zero quote.
            if (wallet.m_fallback_fee == CFeeRate(0))
                throw WalletFeePolicyError("PGN fee estimate is unavailable and fallback estimation is disabled");
        }
        // Obey mempool min fee when using smart fee estimation
        CFeeRate min_mempool_feerate = AdmitRate(wallet.chain().mempoolMinFee(), policy);
        if (feerate_needed < min_mempool_feerate) {
            feerate_needed = min_mempool_feerate;
            if (observation) observation->reason = FeeReason::MEMPOOL_MIN;
        }
    }

    // prevent user from paying a fee below the required fee rate
    CFeeRate required_feerate = GetRequiredFeeRate(wallet, snapshot);
    if (required_feerate > feerate_needed) {
        feerate_needed = required_feerate;
        if (observation) observation->reason = FeeReason::REQUIRED;
    }
    return publish(feerate_needed);
}

CFeeRate GetDiscardRate(const CWallet& wallet)
{
    return GetDiscardRate(wallet, SelectedSnapshot());
}
CFeeRate GetDiscardRate(const CWallet& wallet, const WalletFeePolicySnapshot& snapshot)
{
    const auto policy = RequireWalletPolicy(wallet, snapshot);
    unsigned int highest_target = wallet.chain().estimateMaxBlocks();
    CFeeRate discard_rate = AdmitRate(wallet.chain().estimateSmartFee(highest_target, /*conservative=*/false), policy);
    // Don't let discard_rate be greater than longest possible fee estimate if we get a valid fee estimate
    const auto configured = AdmitRate(wallet.m_discard_rate, policy);
    discard_rate = (discard_rate == CFeeRate(0)) ? configured : std::min(discard_rate, configured);
    // Discard rate must be at least dust relay feerate
    discard_rate = std::max(discard_rate, AdmitRate(wallet.chain().relayDustFee(), policy));
    return AdmitRate(discard_rate, policy);
}
} // namespace wallet
