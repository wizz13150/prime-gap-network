// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#include <qt/pgncoincontrollabels.h>
#include <qt/pgnsendamountdisplay.h>
#include <qt/pgntransactionamount.h>
#include <QDialog>
#include <QLabel>
#include <QPointer>
#include <array>

namespace pgn::qt {
namespace {
constexpr std::array<const char*,4> MONEY_LABELS{"labelCoinControlAmount","labelCoinControlFee","labelCoinControlAfterFee","labelCoinControlChange"};
constexpr auto ATOMS="pgn_coincontrol_atoms";
constexpr auto CONTEXT="pgn_coincontrol_context";
constexpr auto READY="pgn_coincontrol_summary_ready";
QVariantList ContextTag(const std::optional<core::MonetaryContext>& context)
{
    if (!SendAmountDisplay(context).available()) return {};
    return {QVariant::fromValue<quint32>(context->transfer.decimals),QVariant::fromValue<qint64>(context->transfer.max_atomic),
        QVariant::fromValue<quint32>(context->display.decimals),QVariant::fromValue<qint64>(context->display.max_atomic),
        QVariant::fromValue<qulonglong>(context->text_budget.max_characters)};
}
QString Notice()
{
    return QStringLiteral("Preliminary estimate before native change/dust/fee adjustment; not a final quote or spend authorization.");
}
}
void InvalidateCoinControlLabels(QDialog& dialog)
{
    const QPointer<QDialog> alive(&dialog);
    dialog.setProperty(READY,false);
    if (!alive) return;
    // Revoke every clipboard payload BEFORE changing any visible text.
    for (const auto* name:MONEY_LABELS) if (const QPointer<QLabel> label=dialog.findChild<QLabel*>(name)) {
        label->setProperty(ATOMS,QVariant{});
        if (!alive) return;
        if (label) label->setProperty(CONTEXT,QVariant{});
        if (!alive) return;
    }
    for (const auto* name:MONEY_LABELS) if (const QPointer<QLabel> label=dialog.findChild<QLabel*>(name)) {
        label->setTextFormat(Qt::PlainText);
        if (!alive) return;
        if (label) label->setText(QStringLiteral("Unavailable"));
        if (!alive) return;
        if (label) label->setToolTip(Notice());
        if (!alive) return;
    }
    for (const auto* name:{"labelCoinControlQuantity","labelCoinControlBytes"}) if (auto* label=dialog.findChild<QLabel*>(name)) {
        label->setText(QStringLiteral("Unavailable"));
        if (!alive) return;
    }
    if (auto* label=dialog.findChild<QLabel*>("labelCoinControlInsuffFunds")) label->hide();
}
bool PublishCoinControlLabels(QDialog& dialog,const CoinControlPreviewResult& result,
    const std::optional<core::MonetaryContext>& context)
{
    const QPointer<QDialog> alive(&dialog);
    InvalidateCoinControlLabels(dialog);
    if (!alive) return false;
    const auto refused=[&] { if (alive) InvalidateCoinControlLabels(dialog); return false; };
    if (!result) return false;
    const auto tag=ContextTag(context);
    if (tag.empty()) return false;
    const auto& t=*result.texts;
    const std::array<std::optional<CAmount>,4> atoms{t.amount_atoms,t.fee_atoms,t.after_fee_atoms,t.change_atoms};
    const std::array<std::optional<QString>,4> numbers{t.amount,t.fee,t.after_fee,t.change};
    std::array<QPointer<QLabel>,4> labels;
    for (size_t i=0;i<labels.size();++i) {
        labels[i]=dialog.findChild<QLabel*>(MONEY_LABELS[i]);
        if (!labels[i] || atoms[i].has_value()!=numbers[i].has_value()) return false;
        if (!atoms[i]) continue;
        const auto canonical=PresentTransactionAtoms(QVariant::fromValue<qint64>(*atoms[i]),context,false,false);
        if (!canonical || canonical.canonical!=numbers[i]) return false;
    }
    if (t.scale!=SendAmountDisplay(context).scale()) return false;
    for (size_t i=0;i<labels.size();++i) {
        if (!labels[i]) return refused();
        const QString text=numbers[i] ? (i?QString::fromUtf8("\xe2\x89\x88"):QString{})+*numbers[i]+QStringLiteral(" units (")+t.scale+QLatin1Char(')') : QStringLiteral("Not applicable");
        labels[i]->setText(text);
        if (!alive || !labels[i]) return refused();
        labels[i]->setToolTip(Notice());
        if (!alive || !labels[i]) return refused();
    }
    if (auto* label=dialog.findChild<QLabel*>("labelCoinControlQuantity")) label->setText(QString::number(t.quantity));
    if (!alive) return false;
    if (const QPointer<QLabel> label=dialog.findChild<QLabel*>("labelCoinControlBytes")) {
        label->setText(QString::fromUtf8("\xe2\x89\x88")+QString::number(t.estimated_vbytes));
        if (!alive || !label) return refused();
        label->setToolTip(Notice());
    }
    if (!alive) return false;
    if (auto* label=dialog.findChild<QLabel*>("labelCoinControlInsuffFunds")) label->setVisible(t.insufficient);
    if (!alive) return false;
    for (size_t i=0;i<labels.size();++i) {
        if (!labels[i]) return refused();
        labels[i]->setProperty(ATOMS,atoms[i]?QVariant::fromValue<qint64>(*atoms[i]):QVariant{});
        if (!alive || !labels[i]) return refused();
        labels[i]->setProperty(CONTEXT,tag);
        if (!alive || !labels[i]) return refused();
    }
    dialog.setProperty(READY,true);
    return !alive.isNull();
}
bool CopyCoinControlLabelAmount(const QLabel& label,const std::optional<core::MonetaryContext>& context,
    const std::function<void(const QString&)>& sink)
{
    const auto tag=ContextTag(context);
    const QDialog* owner{};
    for (auto* parent=label.parentWidget();parent;parent=parent->parentWidget()) {
        if ((owner=qobject_cast<const QDialog*>(parent))) break;
    }
    if (!owner || !owner->property(READY).toBool() || !sink || tag.empty() || label.property(CONTEXT).toList()!=tag) return false;
    const auto canonical=PresentTransactionAtoms(label.property(ATOMS),context,false,false);
    if (!canonical) return false;
    sink(*canonical.canonical);
    return true;
}
} // namespace pgn::qt
