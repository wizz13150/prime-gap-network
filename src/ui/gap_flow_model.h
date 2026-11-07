#pragma once
#include "ui/merit_palette.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QQueue>
#include <QSet>
#include <QHash>
#include <QVector>
#include <algorithm>
#include <optional>

namespace pgn::ui {
struct GapFlowEvent {
    QString identity,pair,producer;
    int gap{};double merit{},seed{};MeritTier tier{MeritTier::Ordinary};qint64 born{};
    qint64 lifetime()const{return tier==MeritTier::Ordinary?4200:10500;}
    QString ticket()const{return identity.left(std::max<qsizetype>(0,identity.size()-pair.size()-1));}
};
struct CollectedGap {
    GapFlowEvent gap;
    qint64 arrives{},leaves{-1};
    bool ticketTransition{};
    std::optional<GapFlowEvent> flight;
    double flightAge{};
    bool flightFromBest{};
    bool toSink{};
};
// A bounded presentation cache. Discovery, audit validity and rewards remain
// source facts; animation does not create, count or certify any of them.
class GapFlowModel {
public:
    static constexpr int maximumEvents=64;
    // Discoveries fold into their range report; confirmation is shown by the
    // report itself. A disconnected Core must not strand old-ticket particles.
    static constexpr qint64 reportHandoffDuration=4200;
    void observe(const QJsonObject& frame,const QString& producer,qint64 now){
        const auto ticket=frame.value("ticket_id").toString();
        if(!ticket.isEmpty()&&ticket!=ticket_){
            QSet<QString> retired;
            for(auto node:collected_){
                node.leaves=now;node.ticketTransition=true;retired.insert(node.gap.identity);
                if(now<node.arrives)for(const auto& event:events_)if(event.identity==node.gap.identity){node.flight=event;node.flightAge=std::max(0.,(now-event.born)/1000.);node.flightFromBest=true;break;}
                departing_.append(node);
            }
            for(const auto& event:events_)if(!retired.contains(event.identity)){
                CollectedGap node{event,now,now,true};node.flight=event;node.flightAge=std::max(0.,(now-event.born)/1000.);node.toSink=event.tier==MeritTier::Ordinary;departing_.append(node);
            }
            events_.clear();
            collected_.clear();ticket_=ticket;bestSnapshot_={};
            while(departing_.size()>256)departing_.removeFirst();
        }
        searching_=frame.value("phase")=="Searching"&&!frame.value("source_stale").toBool();
        audit_=frame.value("work_role")=="audit";
        const double rate=frame.value("speed_rate").toDouble();
        moving_=searching_&&std::isfinite(rate)&&rate>0;
        activity_=moving_?std::clamp(std::log1p(rate/1e6)/std::log1p(1e6),.1,1.):0.;
        producer_=producer;lastObservation_=now;
        if(audit_){expire(now);return;}
        const auto target=frame.value("target_merit").toDouble();
        const auto threshold=std::isfinite(target)&&target>0?std::optional<double>(target):std::nullopt;
        auto rows=frame.value("record_pairs").toArray();
        const auto scope=frame.value("scopes").toObject().value("ticket").toObject();
        for(const auto& row:scope.value("recent").toArray())rows.append(row);
        for(const auto& row:scope.value("best").toArray())rows.append(row);
        int arrivals=0,examined=0;
        for(const auto& value:rows){
            if(++examined>512)break;
            const auto row=value.toObject();const auto lower=row.value("lower_prime").toString();
            const int gap=row.value("gap").toInt();bool valid=false;
            const auto merit=row.value("merit").isString()?row.value("merit").toString().toDouble(&valid):row.value("merit").toDouble(-1);
            if(!row.value("merit").isString())valid=row.value("merit").isDouble();
            if(lower.isEmpty()||lower.size()>48||gap<=0||!valid||!std::isfinite(merit)||merit<0)continue;
            const auto pair=lower+":"+QString::number(gap);
            const auto key=frame.value("ticket_id").toString()+":"+pair;
            if(seen_.contains(key))continue;
            seen_.insert(key);order_.enqueue(key);
            while(order_.size()>2048)seen_.remove(order_.dequeue());
            if(!initialized_||!moving_)continue;
            const auto tier=meritTier(merit,threshold);
            if(events_.size()>=maximumEvents){
                auto ordinary=std::find_if(events_.begin(),events_.end(),[](const auto& event){return event.tier==MeritTier::Ordinary;});
                events_.erase(ordinary==events_.end()?events_.begin():ordinary);++coalesced_;
            }
            const double seed=double(qHash(key)&0xffff)/65535.;
            events_.append({key,pair,producer,gap,merit,seed,tier,now+std::min(240,arrivals++*24)});
            ++received_;
        }
        initialized_=true;expire(now);
    }
    void setEligibility(const QJsonArray& pairs){
        eligible_.clear();for(const auto& value:pairs){const auto row=value.toObject();
            const auto lower=row.value("lower_prime").toString();const auto gap=row.value("gap").toInt();
            if(!lower.isEmpty()&&gap>0&&eligible_.size()<2048)eligible_.insert(lower+":"+QString::number(gap));}
    }
    void setVisibleBest(const QJsonObject& snapshot,qint64 now){
        if(audit_||snapshot.value("ticket_id").toString()!=ticket_||snapshot==bestSnapshot_)return;
        bestSnapshot_=snapshot;QVector<CollectedGap> next;QSet<QString> retained;
        const double target=snapshot.value("target_merit").toDouble();
        const auto threshold=std::isfinite(target)&&target>0?std::optional<double>(target):std::nullopt;
        int examined=0;
        for(const auto& value:snapshot.value("rows").toArray()){
            if(++examined>128)break;
            const auto row=value.toObject();const auto lower=row.value("lower_prime").toString();
            const int gap=row.value("gap").toInt();const double merit=row.value("merit").toDouble(-1);
            if(lower.isEmpty()||lower.size()>48||gap<=0||merit<0||!std::isfinite(merit))continue;
            const auto tier=meritTier(merit,threshold);
            // Best-table membership alone does not make a coin candidate.
            // Ordinary discoveries keep their left-hand sink trajectory.
            if(tier==MeritTier::Ordinary)continue;
            const auto pair=lower+":"+QString::number(gap);if(retained.contains(pair))continue;
            retained.insert(pair);
            auto previous=std::find_if(collected_.begin(),collected_.end(),[&](const auto& node){return node.gap.pair==pair;});
            if(previous!=collected_.end()){auto node=*previous;node.gap.tier=tier;next.append(node);continue;}
            const double seed=double(qHash(pair)&0xffff)/65535.;
            GapFlowEvent event{ticket_+":"+pair,pair,producer_,gap,merit,seed,tier,now};
            qint64 arrives=now-4000;
            for(const auto& flying:events_)if(flying.identity==event.identity)arrives=flying.born+3700;
            next.append({event,arrives,-1,false});
        }
        for(auto node:collected_)if(!retained.contains(node.gap.pair)){node.leaves=now;departing_.append(node);}
        collected_=std::move(next);while(departing_.size()>256)departing_.removeFirst();
    }
    void expire(qint64 now){
        if(now-lastObservation_>3000)moving_=false;
        events_.erase(std::remove_if(events_.begin(),events_.end(),[now](const auto& event){return now-event.born>event.lifetime();}),events_.end());
        departing_.erase(std::remove_if(departing_.begin(),departing_.end(),[this,now](const auto& node){
            if(!node.ticketTransition)return now-node.leaves>1700;
            if(node.toSink)return now-node.leaves>2200;
            return now-node.leaves>reportHandoffDuration;
        }),departing_.end());
    }
    const QVector<GapFlowEvent>& events()const{return events_;}
    bool record(const GapFlowEvent& event)const{return eligible_.contains(event.pair);}
    bool moving()const{return moving_;}
    double activity()const{return activity_;}
    bool audit()const{return audit_;}
    const QString& producer()const{return producer_;}
    quint64 received()const{return received_;}
    quint64 coalesced()const{return coalesced_;}
    const QVector<CollectedGap>& collected()const{return collected_;}
    const QVector<CollectedGap>& departing()const{return departing_;}
    const CollectedGap* collected(const GapFlowEvent& event)const{for(const auto& node:collected_)if(node.gap.identity==event.identity)return &node;return nullptr;}
    void confirmReport(const QString& ticket,qint64 now){if(!confirmedReports_.contains(ticket)){confirmedReports_.insert(ticket,now);confirmationOrder_.enqueue(ticket);while(confirmationOrder_.size()>64)confirmedReports_.remove(confirmationOrder_.dequeue());}}
    void revokeReport(const QString& ticket){confirmedReports_.remove(ticket);confirmationOrder_.removeAll(ticket);}
    qint64 reportConfirmed(const GapFlowEvent& event)const{return confirmedReports_.value(event.ticket(),-1);}
private:
    QVector<GapFlowEvent> events_;QSet<QString> seen_,eligible_;QQueue<QString> order_;
    QString producer_;double activity_{};qint64 lastObservation_{};bool initialized_{},searching_{},moving_{},audit_{};quint64 received_{},coalesced_{};
    QString ticket_;QJsonObject bestSnapshot_;QVector<CollectedGap> collected_,departing_;
    QHash<QString,qint64> confirmedReports_;QQueue<QString> confirmationOrder_;
};
}
