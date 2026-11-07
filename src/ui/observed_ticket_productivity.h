#pragma once
#include "booking/decimal_range.h"
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <deque>
#include <cmath>

namespace pgn::ui {
// UI observation only: exact counter differences / the same monotonic interval.
// No integration of a displayed compute rate, interpolation, or historical claim.
class ObservedTicketProductivity {
    struct Sample {qint64 ns;QString positions;};
    struct Series {QString epoch,offset{"0"},lastRaw,total;bool zeroOrigin{};std::deque<Sample> samples;};
    QHash<QString,Series> series_;
    QString width_;
    qint64 lastNs_{-1};
public:
    QJsonObject observe(const QJsonArray& devices,const QString& width,qint64 ns){
        static const QRegularExpression integer("^(0|[1-9][0-9]{0,28})$");
        const bool widthValid=integer.match(width).hasMatch()&&width!="0";
        const double span=widthValid?width.toDouble():0.;
        if(width_!=width||ns<0||(lastNs_>=0&&(ns<lastNs_||ns-lastNs_>10'000'000'000LL)))series_.clear();
        width_=width;
        lastNs_=ns;
        QSet<QString> seen;QJsonArray rows;double total=0.;bool complete=widthValid;int selected=0;
        QHash<QString,double> sums;QHash<QString,bool> known;QHash<QString,int> counts;
        for(const auto& item:devices){
            const auto input=item.toObject();const auto id=input.value("id").toString(),kind=input.value("kind").toString();
            if(id.isEmpty()||seen.contains(id))continue;seen.insert(id);
            QJsonObject row{{"id",id},{"kind",kind},{"label",input.value("label")},{"tickets_per_hour",QJsonValue{}}};
            if(input.value("selected")!=true){series_.remove(id);row.insert("state","off");rows.append(row);continue;}
            ++selected;++counts[kind];if(!known.contains(kind))known[kind]=true;
            const auto epoch=input.value("counter_epoch").toString(),positions=input.value("completed_positions").toString();
            const bool valid=ns>=0&&widthValid&&input.value("counter_fresh")==true&&!epoch.isEmpty()&&epoch.size()<=512&&integer.match(positions).hasMatch();
            if(!valid){series_.remove(id);complete=false;known[kind]=false;row.insert("state","unavailable");rows.append(row);continue;}
            auto& series=series_[id];auto& samples=series.samples;
            const auto declaredTotal=input.value("counter_total").toString();
            const bool zeroOrigin=input.value("counter_zero_origin")==true&&integer.match(declaredTotal).hasMatch()&&declaredTotal!="0"&&pgn::booking::compareUnsigned(positions,declaredTotal)<=0;
            if(series.epoch!=epoch){
                // A witnessed complete old interval can be stitched to a distinct,
                // explicitly zero-based new interval. Never infer a missing tail.
                if(!samples.empty()&&series.zeroOrigin&&series.lastRaw==series.total&&zeroOrigin)series.offset=pgn::booking::addUnsigned(series.offset,series.lastRaw);
                else{samples.clear();series.offset="0";}
                series.epoch=epoch;
            }else if(!series.lastRaw.isEmpty()&&pgn::booking::compareUnsigned(positions,series.lastRaw)<0){samples.clear();series.offset="0";}
            series.lastRaw=positions;series.total=declaredTotal;series.zeroOrigin=zeroOrigin;
            const auto cumulative=pgn::booking::addUnsigned(series.offset,positions);
            if(!samples.empty()&&samples.back().ns==ns){if(samples.back().positions!=cumulative){samples.clear();samples.push_back({ns,cumulative});}}
            else if(samples.size()>1&&samples.back().ns/1'000'000'000LL==ns/1'000'000'000LL)samples.back()={ns,cumulative};
            else samples.push_back({ns,cumulative});
            while(samples.size()>2&&samples[1].ns<=ns-60'000'000'000LL)samples.pop_front();
            while(samples.size()>65)samples.pop_front();
            const auto elapsed=ns-samples.front().ns;
            if(elapsed<10'000'000'000LL){complete=false;known[kind]=false;row.insert("state","warming");}
            else {
                const auto delta=pgn::booking::subtractUnsigned(cumulative,samples.front().positions);
                const double rate=delta.toDouble()/span*3.6e12/double(elapsed);
                if(std::isfinite(rate)&&rate>=0){row.insert("tickets_per_hour",rate);row.insert("positions_delta",delta);row.insert("wall_time_ns",QString::number(elapsed));row.insert("state","observed");total+=rate;sums[kind]+=rate;}
                else{complete=false;known[kind]=false;row.insert("state","unavailable");}
            }
            rows.append(row);
        }
        for(auto it=series_.begin();it!=series_.end();)if(!seen.contains(it.key()))it=series_.erase(it);else ++it;
        QJsonArray groups;for(const auto& kind:{QString("GPU"),QString("CPU")})groups.append(QJsonObject{{"label",kind},{"selected_devices",counts[kind]},{"tickets_per_hour",counts[kind]&&known.value(kind)?QJsonValue(sums.value(kind)):QJsonValue{}}});
        return {{"schema","PGN_OBSERVED_TICKET_PRODUCTIVITY_V1"},{"devices",rows},{"groups",groups},{"selected_devices",selected},{"total_tickets_per_hour",complete&&selected&&std::isfinite(total)?QJsonValue(total):QJsonValue{}},{"scope","Exact observed position deltas / matched local monotonic wall time, up to60s; includes confirmed waiting, excludes unobserved gaps, resets on counter epoch or stale input; no network acceptance or historical estimate"}};
    }
};
}
