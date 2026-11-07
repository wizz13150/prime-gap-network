#pragma once

#include <QPainter>
#include <QFontMetrics>
#include <QFontDatabase>
#include <QLinearGradient>
#include <algorithm>
#include <cmath>
#include <QCryptographicHash>
#include <QHash>
#include <QSet>
#include <array>

// Extracted from the Gapcoin Mining renderer, not a second visual theme.
// Both coin workspaces call these same drawing primitives.
namespace HorizonDeviceVisuals {
inline QLinearGradient canvasBrush(const QRectF& bounds) {
    QLinearGradient background(bounds.topLeft(),bounds.bottomRight());
    background.setColorAt(0.0,QColor(QStringLiteral("#031313")));
    background.setColorAt(0.43,QColor(QStringLiteral("#07131d")));
    background.setColorAt(1.0,QColor(QStringLiteral("#02070f")));
    return background;
}
inline QFont font(int pixels,int weight=QFont::Normal,bool mono=false,bool riecoin=false) {
    // Keep the Horizon font when installed. Select an explicit monospace
    // fallback instead of letting Qt choose an arbitrary proportional font.
    static const QString monoFamily=[] {
        const auto families=QFontDatabase::families();
        for(const QString& family:{QStringLiteral("Cascadia Mono"),QStringLiteral("Consolas"),QStringLiteral("DejaVu Sans Mono")})
            if(families.contains(family,Qt::CaseInsensitive))return family;
        return QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
    }();
    // The explicit fallback was requested for Riecoin only. Keep the original
    // family request (and Qt fallback) for Gapcoin/Primecoin callers.
    QFont result(mono?(riecoin?monoFamily:QStringLiteral("Cascadia Mono")):QStringLiteral("Segoe UI"));
    result.setPixelSize(std::max(pixels+1,qRound(pixels*1.25)));
    result.setWeight(static_cast<QFont::Weight>(weight));
    if(mono)result.setStyleHint(QFont::Monospace);
    return result;
}
inline QColor alpha(QColor color,int opacity){color.setAlpha(opacity);return color;}
// Reserved summary identity: never assigned to a physical device.
inline QColor totalCardAccent(){return QColor(QStringLiteral("#e6d2b5"));}
inline QColor identityColor(QHash<QString,QColor>& cache,const QString& stableId,
                            const QString& deviceKind=QString{})
    {
        QString key = stableId.trimmed().toLower();
        if (key.isEmpty()) key = QStringLiteral("unbound-%1").arg(deviceKind.toLower());
        const auto existing = cache.constFind(key);
        if (existing != cache.cend() && existing.value().rgb() != totalCardAccent().rgb()) return existing.value();

        // High-contrast, colour-blind-conscious accents on the dark Horizon
        // canvas.  Assignment is stable for a hardware identity and collisions
        // are resolved against every device already visible in this session.
        static const std::array<QColor, 24> palette{
            QColor(QStringLiteral("#69efc5")), QColor(QStringLiteral("#46c8ee")),
            QColor(QStringLiteral("#c69cff")), QColor(QStringLiteral("#f5df76")),
            QColor(QStringLiteral("#ff7aa8")), QColor(QStringLiteral("#7aa7ff")),
            QColor(QStringLiteral("#8fe36f")), QColor(QStringLiteral("#e99cff")),
            QColor(QStringLiteral("#5ce1b9")), QColor(QStringLiteral("#70d1ff")),
            QColor(QStringLiteral("#d3a6ff")), QColor(QStringLiteral("#d8ed70")),
            QColor(QStringLiteral("#ff8f70")), QColor(QStringLiteral("#8bc6ff")),
            QColor(QStringLiteral("#76f2a8")), QColor(QStringLiteral("#f0a6dc")),
            QColor(QStringLiteral("#56d5d0")), QColor(QStringLiteral("#9eb5ff")),
            QColor(QStringLiteral("#b7e66b")), QColor(QStringLiteral("#dc9cff")),
            QColor(QStringLiteral("#5de7a5")), QColor(QStringLiteral("#62c2ff")),
            QColor(QStringLiteral("#a9ee86")), QColor(QStringLiteral("#ef9bc4"))};
        const QByteArray digest = QCryptographicHash::hash(key.toUtf8(),
                                                          QCryptographicHash::Sha256);
        const quint32 seed = (static_cast<quint8>(digest.at(0)) << 24) |
            (static_cast<quint8>(digest.at(1)) << 16) |
            (static_cast<quint8>(digest.at(2)) << 8) |
            static_cast<quint8>(digest.at(3));
        QSet<QString> used;
        used.insert(totalCardAccent().name(QColor::HexRgb));
        for (auto iterator = cache.cbegin();
             iterator != cache.cend(); ++iterator) {
            used.insert(iterator.value().name(QColor::HexRgb));
        }
        QColor selected;
        for (int attempt = 0; attempt < static_cast<int>(palette.size()); ++attempt) {
            const QColor candidate = palette.at((seed + attempt * 7U) % palette.size());
            if (!used.contains(candidate.name(QColor::HexRgb))) {
                selected = candidate;
                break;
            }
        }
        if (!selected.isValid()) {
            selected = QColor::fromHsv(static_cast<int>(seed % 360U), 155, 242);
        }
        cache.insert(key, selected);
        return selected;
    }

inline QColor vramColor(){return QColor(QStringLiteral("#62cfff"));}
inline QColor powerColor(){return QColor(QStringLiteral("#ffd166"));}
inline QColor rateColor(){return QColor(QStringLiteral("#b58cff"));}
inline QColor prpColor(){return QColor(QStringLiteral("#46c8ee"));}
inline QColor yieldColor(){return QColor(QStringLiteral("#ffad4a"));}
inline QColor temperatureColor(double degrees){
    return !std::isfinite(degrees)?QColor(QStringLiteral("#718b8d")):
        QColor(degrees>=85?QStringLiteral("#ff5d67"):degrees>=75?
        QStringLiteral("#ffad4a"):QStringLiteral("#69efc5"));
}
// Per-device, per-unit ring calibration: this class never averages its input.
// PGN's separately requested display-only gap filter runs before drawing;
// position/test rates remain raw. Resource percentages stay true linear values.
class DynamicGaugeScale {
public:
    double fraction(const QString& key,double value,double linearCeiling=0.) {
        if(!std::isfinite(value)||value<0)return 0.;
        if(linearCeiling>0)return std::clamp(value/linearCeiling,0.,1.);
        double& peak=m_peaks[key];peak=std::max(peak,value);
        const double ceiling=std::pow(10.,std::ceil(std::log10(std::max(1.,peak*1.15))));
        return std::clamp(std::log1p(value)/std::log1p(ceiling),0.,1.);
    }
private:
    QHash<QString,double> m_peaks;
};
inline void frame(QPainter& painter,const QRectF& pod,const QColor& color,bool active){
    if(active){painter.setBrush(Qt::NoBrush);painter.setPen(QPen(alpha(color,32),7));
        painter.drawRoundedRect(pod.adjusted(-3,-3,3,3),20,20);}
    QLinearGradient glass(pod.topLeft(),pod.bottomRight());
    glass.setColorAt(0.,active?QColor(6,47,43,255):QColor(4,35,36,255));
    glass.setColorAt(1.,QColor(2,10,21,255));painter.setBrush(glass);
    painter.setPen(QPen(alpha(color,active?210:105),active?1.6:1.0));
    painter.drawRoundedRect(pod,17,17);
}
inline QRectF gauge(QPainter& painter,const QPointF& center,qreal radius,double fraction,
                  const QColor& color,const QString& value,const QString& label,
                  qreal labelWidth=0.,qreal labelGap=4.,int labelPixels=7,bool riecoin=false,
                  const QColor& valueColor=QColor{},QRectF* labelBounds=nullptr){
    fraction=std::clamp(fraction,0.,1.);
    const QRectF ring(center.x()-radius,center.y()-radius,radius*2,radius*2);
    painter.save();painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(38,78,87,88),7,Qt::SolidLine,Qt::RoundCap));
    painter.drawArc(ring,225*16,-270*16);
    painter.setPen(QPen(alpha(color,235),7,Qt::SolidLine,Qt::RoundCap));
    painter.drawArc(ring,225*16,qRound(-270.*16*fraction));
    const qreal angle=(225.-270.*fraction)*3.141592653589793/180.;
    const QPointF direction(std::cos(angle),-std::sin(angle));
    painter.setPen(QPen(alpha(color,185),2.,Qt::SolidLine,Qt::RoundCap));
    painter.drawLine(center+direction*(radius+1.),center+direction*(radius+3.));
    // A changing arc must not bleed into the unchanged numeric glyphs.
    painter.setPen(Qt::NoPen);painter.setBrush(QColor(1,12,20,255));
    const qreal inner=std::max(1.,radius-2.);
    painter.drawEllipse(center,inner,inner);
    QString displayed=value;
    if(riecoin&&displayed.endsWith('%')){QString numeric=displayed.left(displayed.size()-1).trimmed();numeric.replace(',','.');bool ok=false;const double percent=numeric.toDouble(&ok);if(ok&&std::isfinite(percent))displayed=QString::number(percent,'f',0)+"%";}
    QFont valueFont=font(13,QFont::DemiBold,true,riecoin);
    const qreal textWidth=std::max(2.,2.*inner-4.);
    while(valueFont.pixelSize()>6 && QFontMetrics(valueFont).horizontalAdvance(displayed)>textWidth)
        valueFont.setPixelSize(valueFont.pixelSize()-1);
    painter.setFont(valueFont);painter.setPen(valueColor.isValid()?valueColor:QColor(QStringLiteral("#f0fbfa")));
    const QRectF valueArea(center.x()-inner,center.y()-9,inner*2,18);
    const QRectF valueBounds=painter.boundingRect(valueArea,Qt::AlignCenter,displayed);
    painter.drawText(valueArea,Qt::AlignCenter,displayed);
    painter.setFont(font(labelPixels,QFont::DemiBold,true,riecoin));painter.setPen(alpha(color,190));
    const qreal width=labelWidth>0?labelWidth:radius*2;
    const QRectF labelArea(center.x()-width/2,center.y()+radius+labelGap,width,14);
    if(labelBounds)*labelBounds=painter.boundingRect(labelArea,Qt::AlignCenter,label);
    painter.drawText(labelArea,Qt::AlignCenter,label);
    painter.restore();
    return valueBounds;
}
}
