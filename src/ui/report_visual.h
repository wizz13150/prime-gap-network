#pragma once
#include "ui/gap_flow_motion.h"
#include <QPainter>
#include <QPainterPath>
#include <QHash>
#include <QQueue>
#include <QVector>
namespace pgn::ui {
struct ReportVisual {QVector<QColor> gaps;};
inline QHash<QString,ReportVisual>& reportVisuals(){static QHash<QString,ReportVisual> values;return values;}
inline void rememberReportVisual(const QString& request,QVector<QColor> gaps){
 if(request.isEmpty()||gaps.isEmpty())return;if(gaps.size()>8)gaps.resize(8);
 auto& cache=reportVisuals();if(cache.size()>=32&&!cache.contains(request))cache.erase(cache.begin());cache.insert(request,{std::move(gaps)});
}
// A report carries the actual coloured discoveries observed in Mining. Old
// reports without that local visual memory have a neutral core, never fake gold.
inline void drawReportVisual(QPainter& p,QPointF at,const QString& request,QColor state,double seconds,double scale=1.,bool passed=false){
 const auto seed=flowSeed(uint(qHash(request)));p.save();p.translate(at);p.scale(scale,scale);
 QRadialGradient haze(QPointF(),15);auto c=state;c.setAlpha(45);haze.setColorAt(0,c);c.setAlpha(0);haze.setColorAt(1,c);p.setPen(Qt::NoPen);p.setBrush(haze);p.drawEllipse(QPointF(),15,15);
 QPainterPath envelope;for(int k=0;k<=24;++k){const double a=k*6.28318530718/24;const double r=7.4+std::sin(a*3+seconds*.7+seed*9)*.65+flowNoise(seed+a*.02,seconds*.45)*.3;const QPointF point(std::cos(a)*r,std::sin(a)*r*.8);if(!k)envelope.moveTo(point);else envelope.lineTo(point);}envelope.closeSubpath();p.setPen(QPen(state,1));p.setBrush(QColor(5,23,29,170));p.drawPath(envelope);
 const auto visual=reportVisuals().value(request);const int n=visual.gaps.size();
 if(!n){p.setPen(Qt::NoPen);p.setBrush(QColor("#b4d8dc"));p.drawEllipse(QPointF(),2.1,2.1);}
 for(int k=0;k<n;++k){const double a=seed*6.28+k*6.28318530718/std::max(1,n)+seconds*(k%2?.24:-.18);const QPointF point=n==1?QPointF():QPointF(std::cos(a)*3.8,std::sin(a)*2.8);p.setPen(Qt::NoPen);p.setBrush(visual.gaps[k]);p.drawEllipse(point,1.8,1.8);}
 if(passed){p.setBrush(Qt::NoBrush);p.setPen(QPen(QColor(149,255,202,170),1));p.drawEllipse(QRectF(-10,-12,20,7));}
 p.restore();
}
}
