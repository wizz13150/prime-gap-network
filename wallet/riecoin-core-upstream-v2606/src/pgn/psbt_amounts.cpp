// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#include <pgn/psbt_amounts.h>
#include <psbt.h>
#include <boost/multiprecision/cpp_int.hpp>
#include <limits>
#include <set>

namespace pgn::core {
namespace {
PsbtAmountAdmission ReadPsbtAmounts(const PartiallySignedTransaction& psbt,
                                    CAmount ceiling, PsbtAmountStatus excess)
{
    const auto fail = [](PsbtAmountStatus status) { return PsbtAmountAdmission{status, std::nullopt}; };
    const auto can_add = [ceiling](CAmount total, CAmount value) {
        return total >= 0 && total <= ceiling && value >= 0 && value <= ceiling - total;
    };
    const auto transaction = psbt.GetUnsignedTx();
    if (!transaction || transaction->vin.empty() || transaction->vout.empty() ||
        transaction->vin.size() != psbt.inputs.size() || transaction->vout.size() != psbt.outputs.size())
        return fail(PsbtAmountStatus::InvalidStructure);
    CAmount output_total{0};
    for (const auto& output : transaction->vout) {
        if (output.nValue < 0) return fail(PsbtAmountStatus::NegativeAmount);
        if (!can_add(output_total, output.nValue)) return fail(excess);
        output_total += output.nValue;
    }
    CAmount input_total{0}; bool missing{false}; std::set<COutPoint> seen;
    for (const auto& input : psbt.inputs) {
        if (!seen.insert(input.GetOutPoint()).second) return fail(PsbtAmountStatus::DuplicateInput);
        // Core uses nValue==-1 as an absent witness sentinel. A nonempty
        // script in that sentinel is contradictory metadata, not an amount0.
        if (input.witness_utxo.IsNull() && !input.witness_utxo.scriptPubKey.empty())
            return fail(PsbtAmountStatus::ConflictingInputMetadata);
        if (input.non_witness_utxo) {
            if (input.prev_out >= input.non_witness_utxo->vout.size() ||
                input.non_witness_utxo->GetHash() != input.prev_txid)
                return fail(PsbtAmountStatus::InvalidInputReference);
            if (!input.witness_utxo.IsNull()) {
                const auto& referenced = input.non_witness_utxo->vout[input.prev_out];
                if (input.witness_utxo.nValue != referenced.nValue ||
                    input.witness_utxo.scriptPubKey != referenced.scriptPubKey)
                    return fail(PsbtAmountStatus::ConflictingInputMetadata);
            }
        }
        CTxOut utxo;
        if (!input.GetUTXO(utxo)) { missing = true; continue; }
        if (utxo.nValue < 0) return fail(PsbtAmountStatus::NegativeAmount);
        if (!can_add(input_total, utxo.nValue)) return fail(excess);
        input_total += utxo.nValue;
    }
    if (missing) return fail(PsbtAmountStatus::MissingInputAmounts);
    if (output_total > input_total) return fail(PsbtAmountStatus::OutputsExceedInputs);
    return {PsbtAmountStatus::ArithmeticReadyUnverified,
            PsbtAmounts{input_total, output_total, input_total - output_total}};
}
} // namespace

PsbtAmountAdmission AdmitPsbtAmounts(const PartiallySignedTransaction& psbt,
                                     std::optional<Consensus::AmountRules> rules)
{
    // Local configuration failure precedes untrusted transaction inspection.
    if (!rules) return {PsbtAmountStatus::MissingDomain, std::nullopt};
    return ReadPsbtAmounts(psbt, rules->MaxAtomic(), PsbtAmountStatus::DomainExceeded);
}

PsbtAmountAdmission InspectPsbtAmounts(const PartiallySignedTransaction& psbt)
{
    return ReadPsbtAmounts(psbt, std::numeric_limits<CAmount>::max(), PsbtAmountStatus::AmountOverflow);
}

bool CanExposeFeeRatio(CAmount fee, std::size_t virtual_bytes)
{
    if (fee < 0 || virtual_bytes == 0 ||
        virtual_bytes > static_cast<std::size_t>(std::numeric_limits<int32_t>::max())) return false;
    // Products are each <2^94, well within checked128. ceil(fee*q/size)<=M
    // for every 0<=q<=INT32_MAX iff fee*INT32_MAX<=M*size.
    const boost::multiprecision::checked_int128_t numerator =
        boost::multiprecision::checked_int128_t{fee} * std::numeric_limits<int32_t>::max();
    const boost::multiprecision::checked_int128_t ceiling =
        boost::multiprecision::checked_int128_t{std::numeric_limits<CAmount>::max()} * virtual_bytes;
    return numerator <= ceiling;
}

std::optional<CAmount> QueryFeeRatioDown(CAmount fee, std::size_t virtual_bytes,
                                        int32_t query_bytes)
{
    if (fee < 0 || virtual_bytes == 0 || query_bytes < 0) return std::nullopt;
    const boost::multiprecision::checked_int128_t exact =
        (boost::multiprecision::checked_int128_t{fee} * query_bytes) / virtual_bytes;
    if (exact > std::numeric_limits<CAmount>::max()) return std::nullopt;
    return exact.convert_to<CAmount>();
}
} // namespace pgn::core
