// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#include <qt/pgnmoneyformat.h>
#include <util/moneystr.h>
#include <util/check.h>
#include <QStringList>
#include <limits>

namespace GUIUtil {
namespace {
QString NativeFixed(CAmount amount)
{
    const auto fixed=FormatMoneyFixed(amount);
    return QString::fromLatin1(fixed.data(),static_cast<qsizetype>(fixed.size()));
}
QString GroupDigits(QString whole,SeparatorStyle style)
{
    const qsizetype count=whole.size();
    if (style==SeparatorStyle::ALWAYS || (style==SeparatorStyle::STANDARD && count>4)) {
        for (qsizetype position=count-3;position>0;position-=3) whole.insert(position,QChar(THIN_SP_CP));
    }
    return whole;
}
bool OuterAsciiSpace(QChar ch)
{
    const auto c=ch.unicode();
    return c==' ' || (c>='\t' && c<='\r');
}
bool Digits(const QString& text)
{
    if (text.isEmpty()) return false;
    for(const QChar c:text) if(c<QLatin1Char('0') || c>QLatin1Char('9')) return false;
    return true;
}
}
QString formatAmount(const CAmount& amount,bool plus,SeparatorStyle separators,bool justify)
{
    QString fixed=NativeFixed(amount); // Never abs/negate signed MIN in Qt.
    const bool negative=fixed.startsWith(QLatin1Char('-'));
    if (negative) fixed.remove(0,1);
    const auto dot=fixed.indexOf(QLatin1Char('.'));
    QString whole=GroupDigits(fixed.first(dot),separators);
    if (justify) {
        const QString maximum=NativeFixed(std::numeric_limits<CAmount>::max());
        const qsizetype width=GroupDigits(maximum.first(maximum.indexOf(QLatin1Char('.'))),separators).size();
        whole=whole.rightJustified(width,QLatin1Char(' ')); // Group digits BEFORE padding.
    }
    if (negative) whole.prepend(QLatin1Char('-'));
    else if (plus && amount>0) whole.prepend(QLatin1Char('+'));
    return whole+fixed.sliced(dot); // Native fixed fraction, never recomputed/truncated.
}
QString formatAmountWithUnit(const CAmount& amount,bool plus,SeparatorStyle separators)
{
    return formatAmount(amount,plus,separators)+QStringLiteral(" PGN");
}
QString formatAmountHtmlWithUnit(const CAmount& amount,bool plus,SeparatorStyle separators)
{
    QString escaped=formatAmountWithUnit(amount,plus,separators).toHtmlEscaped();
    escaped.replace(QChar(THIN_SP_CP),QStringLiteral(THIN_SP_HTML));
    return QStringLiteral("<span style='white-space: nowrap;'>%1</span>").arg(escaped);
}
QString formatAmountWithPrivacy(const CAmount& amount,SeparatorStyle separators,bool privacy)
{
    assert(amount>=0); // Preserved v2606 precondition, not financial admission.
    const QString value=privacy ? formatAmount(0,false,separators,true).replace(QLatin1Char('0'),QLatin1Char('#'))
                                : formatAmount(amount,false,separators,true);
    return value+QStringLiteral(" PGN");
}
bool parse(const QString& value,CAmount* out)
{
    qsizetype start{},end=value.size();
    while(start<end && OuterAsciiSpace(value[start])) ++start;
    while(end>start && OuterAsciiSpace(value[end-1])) --end;
    const QString input=value.sliced(start,end-start);
    const auto parts=input.split(QLatin1Char('.'));
    if (parts.isEmpty() || parts.size()>2 || (parts.size()==2 && !parts[1].isEmpty() && !Digits(parts[1]))) return false;
    const auto groups=parts[0].split(QChar(THIN_SP_CP));
    if (groups.isEmpty() || (!groups[0].isEmpty() && !Digits(groups[0]))) return false;
    if (groups.size()>1) {
        if(groups[0].isEmpty() || groups[0].size()>3) return false;
        for(qsizetype i=1;i<groups.size();++i) if(groups[i].size()!=3 || !Digits(groups[i])) return false;
    }
    const QString normalized=groups.join(QString{})+(parts.size()==2?QLatin1Char('.')+parts[1]:QString{});
    const auto amount=ParseMoney(normalized.toLatin1().toStdString());
    if (!amount) return false;
    if (out) *out=*amount;
    return true;
}
} // namespace GUIUtil
