// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#include <qt/pgncoincontrolpreview.h>
#include <qt/pgnsendamountdisplay.h>
#include <qt/pgntransactionamount.h>
#include <util/overflow.h>
#include <limits>

namespace pgn::qt {
std::optional<CAmount> CoinControlRawAtoms(const QVariant& value)
{
    if (!value.isValid() || value.isNull() || value.metaType().id()!=QMetaType::LongLong) return std::nullopt;
    return value.value<qint64>();
}
std::optional<QString> CoinControlAmountText(const QVariant& value,
    const std::optional<core::MonetaryContext>& context,bool individual)
{
    const auto amount=CoinControlRawAtoms(value);
    if (!amount) return std::nullopt;
    return SendAmountDisplay(context).number(*amount,individual?SendAmountKind::Transfer:SendAmountKind::Aggregate);
}
bool CoinControlAtomsLess(const QVariant& a,const QVariant& b)
{
    const auto left=CoinControlRawAtoms(a),right=CoinControlRawAtoms(b);
    if (left.has_value()!=right.has_value()) return left.has_value();
    return left && *left<*right;
}
CoinControlPreviewResult PresentCoinControlPreview(const std::vector<CoinControlPreviewInput>& inputs,
    const std::vector<CAmount>& payments,bool subtract_fee,
    const std::optional<core::MonetaryContext>& context,
    const std::function<std::optional<CAmount>(uint32_t)>& quote)
{
    using S=CoinControlPreviewStatus;
    const SendAmountDisplay display(context);
    if (!display.available()) return {S::InvalidContext,{}};
    if (inputs.empty()) return {S::MissingInputs,{}};
    CAmount amount{},pay{};
    uint64_t bytes{};
    bool witness{};
    for (const auto& input:inputs) {
        if (input.amount<0 || !display.number(input.amount,SendAmountKind::Transfer)) return {S::InvalidAmount,{}};
        const auto next=CheckedAdd(amount,input.amount);
        const auto next_bytes=CheckedAdd(bytes,static_cast<uint64_t>(input.estimated_input_vbytes));
        if (!next || !next_bytes) return {S::Overflow,{}};
        if (input.estimated_input_vbytes==0) return {S::SizeUnavailable,{}};
        amount=*next; bytes=*next_bytes; witness|=input.witness;
    }
    for (const CAmount payment:payments) {
        if (payment<0 || !display.number(payment,SendAmountKind::Transfer)) return {S::InvalidAmount,{}};
        const auto next=CheckedAdd(pay,payment);
        if (!next) return {S::Overflow,{}};
        pay=*next;
    }
    // Retain the inherited *preliminary* output-size heuristic, not a final
    // transaction weight. Guard its arithmetic before querying the real Core.
    const auto output_count=CheckedAdd(static_cast<uint64_t>(payments.size()),uint64_t{1});
    if (!output_count) return {S::SizeUnavailable,{}};
    const uint64_t outputs=payments.empty()?2:*output_count;
    if (outputs>(std::numeric_limits<uint64_t>::max()-10)/34) return {S::SizeUnavailable,{}};
    auto next_bytes=CheckedAdd(bytes,outputs*34+10);
    if (!next_bytes) return {S::SizeUnavailable,{}};
    bytes=*next_bytes;
    if (witness) {
        next_bytes=CheckedAdd(bytes,static_cast<uint64_t>(inputs.size()));
        if (!next_bytes) return {S::SizeUnavailable,{}};
        next_bytes=CheckedAdd(*next_bytes,uint64_t{2});
        if (!next_bytes) return {S::SizeUnavailable,{}};
        bytes=*next_bytes;
    }
    if (subtract_fee && amount==pay) bytes-=34; // Included one change output above.
    if (bytes==0 || bytes>static_cast<uint64_t>(std::numeric_limits<int32_t>::max())) return {S::SizeUnavailable,{}};
    const auto amount_text=display.number(amount,SendAmountKind::Aggregate);
    const auto scale=display.scale();
    if (!amount_text || !scale) return {S::Unrepresentable,{}};
    std::optional<CAmount> fee;
    try { if (quote) fee=quote(static_cast<uint32_t>(bytes)); }
    catch (...) { return {S::FeeUnavailable,{}}; }
    if (!fee) return {S::FeeUnavailable,{}};
    if (*fee<0) return {S::InvalidAmount,{}};
    // Negation is safe ONLY after nonnegative fee/pay validation. CheckedAdd
    // still guards the second subtraction when amount-pay is already negative.
    const auto after=CheckedAdd(amount,-*fee);
    auto change=CheckedAdd(amount,-pay);
    if (change && !subtract_fee) change=CheckedAdd(*change,-*fee);
    if (!after || !change) return {S::Overflow,{}};
    const auto fee_text=display.number(*fee,SendAmountKind::Aggregate);
    const auto after_text=PresentTransactionAtoms(QVariant::fromValue<qint64>(*after),context,false,false);
    const auto change_text=PresentTransactionAtoms(QVariant::fromValue<qint64>(*change),context,false,false);
    if (!fee_text || !after_text || (pay>0 && !change_text)) return {S::Unrepresentable,{}};
    return {S::Preliminary,CoinControlPreviewTexts{amount,*fee,*after,pay>0?change:std::nullopt,
        *amount_text,*fee_text,*after_text.canonical,
        pay>0?change_text.canonical:std::nullopt,*scale,static_cast<uint32_t>(bytes),inputs.size(),
        *after<0 || (pay>0 && *change<0)}};
}
} // namespace pgn::qt
