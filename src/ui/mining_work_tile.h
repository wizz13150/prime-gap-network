#pragma once
#include <QPainter>
#include <QImage>
#include <QPainterPath>
#include <QLinearGradient>
#include <QString>
#include <algorithm>
#include <cmath>
#include <optional>

namespace pgn::ui {
inline std::optional<double> observedWorkFraction(std::optional<double> fraction){
    if(!fraction||!std::isfinite(*fraction)||*fraction<0.||*fraction>1.)return {};
    return fraction;
}
// Only the painted frontier eases between known observations (250ms). The
// counter, displayed percentage and source timestamp never enter this filter.
// Never project work beyond the latest source cursor; identity/reset/missing
// observations are immediate, not smoothed across different jobs.
class WorkProgressTransition {
public:
    void observe(const QString& key,std::optional<double> observed,qint64 now){
        observed=observedWorkFraction(observed);
        if(key!=key_||!observed||!target_||now<started_||*observed<*target_){
            key_=key;from_=target_=observed;started_=now;return;
        }
        if(observed==target_)return;
        from_=value(now);target_=observed;started_=now;
    }
    std::optional<double> value(qint64 now)const{
        if(!target_||!from_)return target_;
        const double t=std::clamp((double(now)-double(started_))/250.,0.,1.);
        const double eased=t*t*(3.-2.*t);
        return std::lerp(*from_,*target_,eased);
    }
private:
    QString key_;std::optional<double> from_,target_;qint64 started_{};
};
inline std::optional<double> workTextureSpeed(std::optional<double> positionsPerSecond){
    if(!positionsPerSecond||!std::isfinite(*positionsPerSecond)||*positionsPerSecond<0.)return {};
    // Perceptual pixels/s, NOT a new throughput estimate or work counter.
    return std::min(24.,3.5*std::log1p(*positionsPerSecond/1e6)/std::log(10.));
}
class WorkTextureMotion {
public:
    std::optional<double> advance(const QString& run,std::optional<double> rate,bool running,qint64 now){
        if(run!=run_||now<previousMs_){run_=run;phase_=0.;known_=active_=false;previousMs_=now;}
        const auto speed=running?workTextureSpeed(rate):std::nullopt;
        if(speed){
            if(active_)phase_=std::fmod(phase_+*speed*std::clamp((double(now)-double(previousMs_))/1000.,0.,.25),24.);
            known_=true;
        }
        active_=speed.has_value();previousMs_=now;
        // Once observed, retain a static texture on a missing/stopped source.
        // It must not blink or continue to imply activity without evidence.
        return known_?std::optional<double>(phase_):std::nullopt;
    }
private:
    QString run_;double phase_{};qint64 previousMs_{};bool known_{},active_{};
};
// Mining-only presentation: blue means source traversal, NOT certified coverage.
// Floating-point geometry; no pixel snapping or source-counter extrapolation.
inline void drawMiningWorkTile(QPainter& painter,const QRectF& box,const QColor& deviceAccent,
                              std::optional<double> observed,bool selected,std::optional<double> texturePhase={}){
    if(!std::isfinite(box.x())||!std::isfinite(box.y())||!std::isfinite(box.width())||!std::isfinite(box.height())||box.width()<4||box.height()<8)return;
    const QColor open("#46c8ee"),processed("#3489ec");
    const auto progress=observedWorkFraction(observed);
    const QRectF inside=box.adjusted(1,1,-1,-1);
    painter.save();painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath rounded;rounded.addRoundedRect(inside,5,5);painter.setClipPath(rounded,Qt::IntersectClip);
    QLinearGradient background(inside.topLeft(),inside.bottomLeft());
    background.setColorAt(0,open.darker(680));background.setColorAt(1,open.darker(1050));
    painter.fillRect(inside,background);
    if(progress&&*progress>0.){
        const QRectF covered(inside.topLeft(),QSizeF(inside.width()*(*progress),inside.height()));
        QLinearGradient fill(inside.topLeft(),inside.bottomLeft());
        fill.setColorAt(0,processed.darker(210));fill.setColorAt(1,processed.darker(370));
        painter.fillRect(covered,fill);
        if(texturePhase&&std::isfinite(*texturePhase)){
            static const QImage pattern=[] {
                QImage tile(24,24,QImage::Format_ARGB32_Premultiplied);tile.fill(Qt::transparent);
                QPainter stripes(&tile);stripes.setRenderHint(QPainter::Antialiasing);stripes.setPen(QPen(QColor(160,215,255,16),1));
                stripes.drawLine(QPointF(-12,24),QPointF(12,0));stripes.drawLine(QPointF(12,24),QPointF(36,0));return tile;
            }();
            QBrush texture(pattern);QTransform position;position.translate(*texturePhase,0);texture.setTransform(position);
            painter.fillRect(covered,texture);
        }
        // Static edge follows only the observed cursor. Decorative activity
        // remains a separate operator choice, not a synthetic workload signal.
        if(*progress<1.){
            const qreal x=covered.right();painter.setPen(QPen(processed.lighter(125),1));
            painter.drawLine(QPointF(x,inside.top()+2),QPointF(x,inside.bottom()-2));
        }
    }
    painter.setPen(QPen(QColor(225,244,255,24),1));
    painter.drawLine(inside.topLeft()+QPointF(5,1),inside.topRight()+QPointF(-5,1));
    painter.restore();
    painter.save();painter.setRenderHint(QPainter::Antialiasing);painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(deviceAccent.isValid()?deviceAccent:open,selected?1.6:1.));
    painter.drawRoundedRect(box.adjusted(.8,.8,-.8,-.8),5,5);painter.restore();
}
}
