// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#include <qt/pgnconsumeramount.h>
#include <qt/pgnsendamountdisplay.h>
#include <qt/sendcoinsrecipient.h>
#include <QThread>

namespace pgn::qt {
bool ConsumerOwnersLive(const ConsumerMonetarySnapshot& value)
{
    return value.publisher && value.owner && value.source &&
        value.publisher->thread()==QThread::currentThread() &&
        value.owner->thread()==QThread::currentThread() &&
        value.source->thread()==QThread::currentThread();
}
bool ConsumerSnapshotMatches(const ConsumerMonetarySnapshot& value,
    const ConsumerMonetarySnapshot& current)
{
    return ConsumerOwnersLive(value) && ConsumerOwnersLive(current) &&
        value.publisher==current.publisher && value.owner==current.owner && value.source==current.source &&
        value.context && current.context &&
        core::ValidateMonetaryContext(*value.context)==core::ExactAmountError::None &&
        core::ValidateMonetaryContext(*current.context)==core::ExactAmountError::None &&
        SameTransactionMonetaryContext(value.context,current.context);
}
BumpFeeAmountResult PresentBumpFeeAmounts(std::optional<CAmount> old_fee,
    std::optional<CAmount> new_fee, const std::optional<core::MonetaryContext>& context)
{
    using S=ConsumerAmountStatus;
    if (!context) return {S::MissingContext,{}};
    const SendAmountDisplay display(context);
    if (!display.available()) return {S::InvalidContext,{}};
    if (!old_fee || !new_fee) return {S::MissingAmount,{}};
    if (*old_fee<0 || *new_fee<0) return {S::InvalidAmount,{}};
    if (*new_fee<*old_fee) return {S::InvalidPair,{}};
    const CAmount increase=*new_fee-*old_fee; // 0<=old<=new<=INT64_MAX proved above.
    const auto old_text=display.number(*old_fee,SendAmountKind::Aggregate);
    const auto delta_text=display.number(increase,SendAmountKind::Aggregate);
    const auto new_text=display.number(*new_fee,SendAmountKind::Aggregate);
    const auto scale=display.scale();
    if (!old_text || !delta_text || !new_text || !scale) return {S::Unrepresentable,{}};
    return {S::Ready,BumpFeeAmountTexts{*old_text,*delta_text,*new_text,*scale,increase}};
}
RequestAmountText PresentRequestAmount(const std::optional<RequestAmountObservation>& observation)
{
    using S=ConsumerAmountStatus;
    if (!observation) return {};
    if (!observation->context) return {S::MissingContext,{},{}};
    const SendAmountDisplay display(observation->context);
    if (!display.available()) return {S::InvalidContext,{},{}};
    if (!observation->atoms) return {S::MissingAmount,{},{}};
    if (*observation->atoms<0) return {S::InvalidAmount,{},{}};
    const auto number=display.number(*observation->atoms,SendAmountKind::Transfer);
    const auto scale=display.scale();
    if (!number || !scale) return {S::Unrepresentable,{},{}};
    return {S::Ready,*number,*scale};
}
TransactionAmountPresentation PresentNotificationAmount(const TransactionAmountNotification& value,
    const ConsumerMonetarySnapshot& current)
{
    if (!ConsumerSnapshotMatches(value.monetary,current)) return {};
    return PresentConsumerTransactionAtoms(value.atoms,value.monetary.context,false,false);
}
std::optional<RequestAmountObservation> ObserveLegacyRequestAmount(const SendCoinsRecipient&)
{
    // No deserialization, mutation, copy, URI/address inference, or current
    // context lookup. Historical v1 cannot establish this observation.
    return std::nullopt;
}
TransactionAmountPresentation PresentConsumerTransactionAtoms(const QVariant& atoms,
    const std::optional<core::MonetaryContext>& context,bool grouped,bool bracketed)
{
    auto result=PresentTransactionAtoms(atoms,context,grouped,bracketed);
    if (!result || *result.atoms<=0) return result;
    result.display->insert(bracketed?1:0,QLatin1Char('+'));
    if (static_cast<uint64_t>(result.display->size())>context->text_budget.max_characters) {
        result.status=TransactionAmountStatus::Unrepresentable;
        result.codec_error=core::ExactAmountError::BudgetExceeded;
        result.canonical.reset();
        result.display.reset();
    }
    return result;
}
} // namespace pgn::qt
