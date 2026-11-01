#pragma once
#include "booking/range_identity.h"
#include <QRectF>
#include <QVector>
#include <algorithm>
#include <optional>
#include <QStringList>
#include <cmath>

namespace pgn::ui {
// This is a job navigator, not a numeric axis. Unequal interval lengths must
// not make an identifiable job disappear into a sub-pixel cell.
struct BookingRailPage {
    int first{}, count{}, capacity{1};
    QVector<QRectF> cells;
};
inline BookingRailPage bookingRailPage(const QRectF& track,int total,int requested,
                                       qreal minimumWidth=112) {
    BookingRailPage page;
    if(total<=0||track.width()<=0||track.height()<=0)return page;
    constexpr qreal gap=4;
    page.capacity=std::max(1,int((track.width()+gap)/(minimumWidth+gap)));
    page.first=std::clamp(requested,0,std::max(0,total-page.capacity));
    page.count=std::min(page.capacity,total-page.first);
    const auto width=(track.width()-gap*(page.count-1))/page.count;
    for(int i=0;i<page.count;++i){
        const auto left=track.left()+i*(width+gap);
        page.cells.append(QRectF(left,track.top(),std::min(width,track.right()-left),track.height()));
    }
    return page;
}

// Every original job retains a cell and its exact identity. Adjacent quiet
// jobs share a readable group caption; active jobs always own separate tiles.
// Rows grow from the actual required label space, instead of hiding pages.
inline bool bookingRailJobActive(const QString& state,bool current=false){
    return current||state=="ACTIVE"||state=="DISPATCHING"||state=="DISPATCHED"||state=="RUNNING";
}
struct BookingRailGroup {int first{},count{};bool active{};QRectF area;};
struct BookingRailOverview {
    QVector<QRectF> cells;
    QVector<BookingRailGroup> groups;
    qreal height{};
    int rows{};
};
inline BookingRailOverview bookingRailOverview(const QRectF& track,
        const QVector<bool>& active,const QStringList& groupKeys) {
    BookingRailOverview out;out.cells.resize(active.size());
    if(active.isEmpty()||track.width()<=0)return out;
    for(int i=0;i<active.size();){
        int end=i+1;
        if(!active[i])while(end<active.size()&&!active[end]&&
            groupKeys.value(end)==groupKeys.value(i))++end;
        out.groups.append({i,end-i,active[i],{}});i=end;
    }
    constexpr qreal gap=4,rowHeight=36;
    int first=0;
    while(first<out.groups.size()){
        int end=first;qreal required=0,weights=0;
        while(end<out.groups.size()){
            const qreal minimum=std::min(track.width(),out.groups[end].active?146.:104.);
            if(end>first&&required+gap+minimum>track.width())break;
            required+=(end>first?gap:0)+minimum;
            weights+=out.groups[end].active?3.:1.;++end;
        }
        qreal x=track.left();const qreal extra=std::max(0.,track.width()-required);
        for(int j=first;j<end;++j){auto& g=out.groups[j];
            const qreal minimum=std::min(track.width(),g.active?146.:104.);
            const qreal width=minimum+extra*(g.active?3.:1.)/weights;
            g.area=QRectF(x,track.top()+out.rows*(rowHeight+gap),width,rowHeight);
            for(int k=0;k<g.count;++k){
                const qreal l=x+width*k/g.count,rr=x+width*(k+1)/g.count;
                out.cells[g.first+k]=QRectF(l,g.area.top(),rr-l,rowHeight);
            }
            x+=width+gap;
        }
        ++out.rows;first=end;
    }
    out.height=out.rows*rowHeight+(out.rows-1)*gap;return out;
}
inline qreal elasticDeviceScroll(qreal requested,qreal maximum,qreal limit=56.) {
    const qreal resting=std::clamp(requested,0.,std::max(0.,maximum));
    const qreal excess=requested-resting;
    return resting+limit*std::tanh(excess/std::max(1.,limit));
}

inline QString bookingRailTitle(const pgn::booking::RangeIdentity& identity,
                                const QString& jobId) {
    Q_UNUSED(jobId);
    if(!identity.displayTitle.isEmpty())return identity.displayTitle;
    if(identity.zone<0)return identity.title.isEmpty()?QString("Range details unavailable"):identity.title;
    const auto block=identity.firstBlock==identity.lastBlock
        ?QString("B%1").arg(identity.firstBlock,2,10,QChar('0'))
        :QString("B%1–%2").arg(identity.firstBlock,2,10,QChar('0')).arg(identity.lastBlock,2,10,QChar('0'));
    return QString("Z%1 · %2%3").arg(identity.zone+1).arg(block,
        identity.title.contains(" · partial")?QString(" *"):QString{});
}
inline QString bookingRailState(const QString& state){
    if(state=="QUEUED")return "LOCAL QUEUE";
    if(state=="ARMED")return "LOCAL ARMED";
    if(state=="DISPATCHING")return "START REQUESTED";
    if(state=="DISPATCHED")return "START CONFIRMED";
    if(state=="SOURCE_FINISHED")return "COMPLETED";
    return QString(state).replace('_',' ');
}
inline QString bookingRailRuntimeDetail(bool running,const std::optional<double>& fraction,
                                       const QString& jobState){
    if(!running){
        if(jobState=="SOURCE_FINISHED")return "COMPLETED";
        if(fraction&&*fraction>=1.0)return "CALCULATION COMPLETE · VERIFYING RESULTS";
        return jobState=="ABORTED"?"STOPPED":"PAUSED";
    }
    return fraction?QString("MINING · %1%").arg(*fraction*100,0,'f',2):QString("MINING · ?");
}
}
