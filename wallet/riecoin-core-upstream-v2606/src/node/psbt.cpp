// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <coins.h>
#include <chainparams.h>
#include <consensus/amount.h>
#include <consensus/tx_verify.h>
#include <node/psbt.h>
#include <policy/policy.h>
#include <policy/settings.h>
#include <pgn/psbt_amounts.h>
#include <tinyformat.h>

#include <numeric>

namespace node {
PSBTAnalysis AnalyzePSBT(PartiallySignedTransaction psbtx)
{
    const auto rules = SelectedPgnConsensusAmountRules();
    if (!rules) {
        PSBTAnalysis result;
        result.SetUnavailable("PSBT monetary analysis unavailable: consensus domain unconfigured");
        return result;
    }
    return AnalyzePSBT(std::move(psbtx), *rules);
}

PSBTAnalysis AnalyzePSBT(PartiallySignedTransaction psbtx, const Consensus::AmountRules& rules)
{
    // Go through each input and build status
    PSBTAnalysis result;
    const auto monetary = pgn::core::AdmitPsbtAmounts(psbtx, rules);
    if (monetary.status != pgn::core::PsbtAmountStatus::ArithmeticReadyUnverified &&
        monetary.status != pgn::core::PsbtAmountStatus::MissingInputAmounts) {
        result.SetInvalid("PSBT amount or input structure is outside the configured domain");
        return result;
    }

    std::optional<CMutableTransaction> unsigned_tx = psbtx.GetUnsignedTx();
    if (!unsigned_tx) {
        result.SetInvalid("PSBT cannot be made into a valid transaction");
        return result;
    }
    CMutableTransaction& mtx = *unsigned_tx;

    bool calc_fee = true;

    result.inputs.resize(psbtx.inputs.size());

    // PrecomputePSBTData calls GetUnsignedTx() which we checked already works
    const PrecomputedTransactionData txdata = *PrecomputePSBTData(psbtx);

    for (unsigned int i = 0; i < psbtx.inputs.size(); ++i) {
        PSBTInput& input = psbtx.inputs[i];
        PSBTInputAnalysis& input_analysis = result.inputs[i];

        // We set next role here and ratchet backwards as required
        input_analysis.next = PSBTRole::EXTRACTOR;

        // Check for a UTXO
        CTxOut utxo;
        if (input.GetUTXO(utxo)) {
            input_analysis.has_utxo = true;
        } else {
            if (input.non_witness_utxo && input.prev_out >= input.non_witness_utxo->vout.size()) {
                result.SetInvalid(strprintf("PSBT is not valid. Input %u specifies invalid prevout", i));
                return result;
            }
            input_analysis.has_utxo = false;
            input_analysis.is_final = false;
            input_analysis.next = PSBTRole::UPDATER;
            calc_fee = false;
        }

        if (!utxo.IsNull() && utxo.scriptPubKey.IsUnspendable()) {
            result.SetInvalid(strprintf("PSBT is not valid. Input %u spends unspendable output", i));
            return result;
        }

        // Check if it is final
        if (!PSBTInputSignedAndVerified(psbtx, i, &txdata)) {
            input_analysis.is_final = false;

            // Figure out what is missing
            SignatureData outdata;
            bool complete = SignPSBTInput(DUMMY_SIGNING_PROVIDER, psbtx, i, &txdata, /*options=*/{}, &outdata) == PSBTError::OK;

            // Things are missing
            if (!complete) {
                input_analysis.missing_pubkeys = outdata.missing_pubkeys;
                input_analysis.missing_redeem_script = outdata.missing_redeem_script;
                input_analysis.missing_witness_script = outdata.missing_witness_script;
                input_analysis.missing_sigs = outdata.missing_sigs;

                // If we are only missing signatures and nothing else, then next is signer
                if (outdata.missing_pubkeys.empty() && outdata.missing_redeem_script.IsNull() && outdata.missing_witness_script.IsNull() && !outdata.missing_sigs.empty()) {
                    input_analysis.next = PSBTRole::SIGNER;
                } else {
                    input_analysis.next = PSBTRole::UPDATER;
                }
            } else {
                input_analysis.next = PSBTRole::FINALIZER;
            }
        } else if (!utxo.IsNull()){
            input_analysis.is_final = true;
        }
    }

    // Calculate next role for PSBT by grabbing "minimum" PSBTInput next role
    result.next = PSBTRole::EXTRACTOR;
    for (unsigned int i = 0; i < psbtx.inputs.size(); ++i) {
        PSBTInputAnalysis& input_analysis = result.inputs[i];
        result.next = std::min(result.next, input_analysis.next);
    }
    assert(result.next > PSBTRole::CREATOR);

    if (calc_fee) {
        // Money was admitted before any signing/role work. Complete supplied
        // UTXO data is arithmetic evidence, not proof of chain membership.
        assert(monetary.amounts.has_value());
        const CAmount fee = monetary.amounts->fee;
        result.fee = fee;

        // Estimate the size
        CCoinsViewCache view{&CoinsViewEmpty::Get()};
        bool success = true;

        for (unsigned int i = 0; i < psbtx.inputs.size(); ++i) {
            PSBTInput& input = psbtx.inputs[i];
            Coin newcoin;

            if (SignPSBTInput(DUMMY_SIGNING_PROVIDER, psbtx, i, nullptr, /*options=*/{}) != PSBTError::OK || !input.GetUTXO(newcoin.out)) {
                success = false;
                break;
            } else {
                mtx.vin[i].scriptSig = input.final_script_sig;
                mtx.vin[i].scriptWitness = input.final_script_witness;
                newcoin.nHeight = 1;
                view.AddCoin(input.GetOutPoint(), std::move(newcoin), true);
            }
        }

        if (success) {
            CTransaction ctx = CTransaction(mtx);
            size_t size(GetVirtualTransactionSize(ctx, GetTransactionSigOpCost(ctx, view, STANDARD_SCRIPT_VERIFY_FLAGS), ::nBytesPerSigOp));
            result.estimated_vsize = size;
            result.estimated_rate_atomic_per_kvb = pgn::core::QueryFeeRatioDown(fee, size, 1000);
            // Do not narrow size_t to int32 or expose a ratio whose permitted
            // GetFee queries violate FeeFrac's exact int64 precondition.
            if (pgn::core::CanExposeFeeRatio(fee, size)) {
                result.estimated_feerate = CFeeRate(fee, static_cast<int32_t>(size));
            }
        }

    }

    return result;
}
} // namespace node
