#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QVector>
#include <algorithm>
#include <cmath>
#include <optional>
namespace pgn::ui {
struct CycleSlice { double weight{},fraction{};QString role;bool skipped{}; };
struct CycleProgress {
 bool known{};double fraction{};QString title,phase;QVector<CycleSlice> slices;
 std::optional<double> taskEta,cycleEta;
};
inline bool coherentMiningCycleFrame(const QJsonObject& frame){
 const auto cycle=frame.value("audit_service").toObject().value("cycle").toObject();
 const auto active=cycle.value("active_audit_id").toString();
 return frame.value("work_role")!="audit"||active.isEmpty()||active==frame.value("ticket_id").toString();
}
// These are observed physical audits, independent of server service credits.
class MiningCycleHistory {
 QString request_;QJsonArray audits_;QJsonObject search_;
public:
 void observe(const QJsonObject& frame){
  const auto cycle=frame.value("audit_service").toObject().value("cycle").toObject();const auto request=cycle.value("request_id").toString();
  if(request.isEmpty()||!coherentMiningCycleFrame(frame))return;
  if(request!=request_){request_=request;audits_={};search_={};}
  const double total=frame.value("positions_total").toString().toDouble(),done=frame.value("positions").toString().toDouble();
  if(frame.value("work_role")=="search"){
   if(total>0&&done>=0&&done<=total&&!frame.value("source_stale").toBool())
    search_={{"total",total},{"fraction",done/total},{"remaining",total-done}};
   return;
  }
  if(frame.value("work_role")!="audit")return;
  const auto identity=frame.value("ticket_id").toString();
  if(identity.isEmpty()||total<=0||done<0||done>total)return;
  const auto stages=cycle.value("audits").toArray();
  const bool regularCommitted=stages.size()==2&&stages[0].toObject().value("state")=="committed"&&stages[1].toObject().value("state")=="committed";
  QJsonObject item{{"identity",identity},{"fraction",done/total},{"remaining",total-done},{"catch_up",cycle.value("catch_up").toBool()&&regularCommitted}};
  for(int i=0;i<audits_.size();++i)if(audits_[i].toObject().value("identity")==identity){audits_[i]=item;return;}
  audits_.append(item);
 }
 const QJsonArray& audits()const{return audits_;}
 const QJsonObject& search()const{return search_;}
};
inline CycleProgress miningCycleProgress(const QJsonObject& frame,QJsonArray observed={},QJsonObject observedSearch={}){
 CycleProgress out;const auto cycle=frame.value("audit_service").toObject().value("cycle").toObject();
 const auto audits=cycle.value("audits").toArray();const int index=cycle.value("active_index").toInt(-1);
 const double total=frame.value("positions_total").toString().toDouble(),done=frame.value("positions").toString().toDouble(),rate=frame.value("eta_rate").toDouble();
 if(cycle.value("schema")!="PGN_MINING_CYCLE_VIEW_V1"||audits.size()!=2||index<0||index>2||total<=0||done<0||done>total)return out;
 const bool auditing=frame.value("work_role")=="audit";const double local=std::clamp(done/total,0.,1.);
 int active=-1;const auto identity=frame.value("ticket_id").toString();
 const bool regularCommitted=audits[0].toObject().value("state")=="committed"&&audits[1].toObject().value("state")=="committed";
 QJsonArray catchups;
 if(cycle.value("extra_audit_ids").isArray()){
  // The execution journal owns admission. Frame arrival order cannot create
  // an extra audit or reclassify a regular audit during a queue handoff.
  for(const auto& id:cycle.value("extra_audit_ids").toArray()){
   QJsonObject match{{"identity",id.toString()},{"fraction",0.}};
   for(const auto& item:observed)if(item.toObject().value("identity")==id){match=item.toObject();break;}
   if(cycle.value("completed_extra_audit_ids").toArray().contains(id)){match.insert("fraction",1.);match.insert("remaining",0.);}
   match.insert("catch_up",true);catchups.append(match);
  }
 }else for(const auto& item:observed)if(item.toObject().value("catch_up").toBool())catchups.append(item);
 if(regularCommitted&&(!catchups.isEmpty()||(auditing&&cycle.value("catch_up").toBool()))){
  observed=QJsonArray{QJsonObject{{"fraction",1.},{"remaining",0.}},QJsonObject{{"fraction",1.},{"remaining",0.}}};
  for(const auto& item:catchups)observed.append(item);
 }
 if(observed.isEmpty()){
  // On attachment mid-cycle, only confirmed completed work is reconstructible.
  for(int i=0;i<2;++i){const auto a=audits[i].toObject();if(a.value("state")=="committed")observed.append(QJsonObject{{"fraction",1.},{"remaining",0.}});else break;}
 }
 if(auditing){
  for(int i=0;i<observed.size();++i)if(observed[i].toObject().value("identity")==identity){active=i;break;}
  if(active<0){active=observed.size();observed.append(QJsonObject{{"identity",identity},{"fraction",local},{"remaining",total-done}});}
 }
 const int count=auditing?std::max(2,int(observed.size())):int(observed.size());
 const double scale=.9+.05*count;double remaining=0;bool etaKnown=true;
 out.known=true;out.title="Ticket "+cycle.value("ticket_number").toString();
 out.phase=auditing?(active<2?QString("Audit %1/2").arg(active+1):QString("Catch-up audit %1").arg(active-1)):QString("Searching");
 for(int i=0;i<count;++i){
  const auto item=i<observed.size()?observed[i].toObject():QJsonObject{};
  const double fraction=i==active?local:std::clamp(item.value("fraction").toDouble(),0.,1.);
  out.slices.append({.05/scale,fraction,i<2?"audit":"catch_up",false});
  if(i==active)remaining+=total-done;else if(item.value("remaining").isDouble())remaining+=item.value("remaining").toDouble();
  else if(i<audits.size()&&audits[i].toObject().value("positions").isString())remaining+=audits[i].toObject().value("positions").toString().toDouble();else etaKnown=false;
 }
 const double search=cycle.value("search_positions").toString().toDouble();
 const bool retainedSearch=search>0&&observedSearch.value("total").toDouble()==search;
 const double searchFraction=auditing?(retainedSearch?std::clamp(observedSearch.value("fraction").toDouble(),0.,1.):0.):local;
 out.slices.append({.9/scale,searchFraction,"search",false});
 if(!auditing)remaining+=total-done;
 else if(retainedSearch)remaining+=observedSearch.value("remaining").toDouble();
 else if(search>0&&active<2)remaining+=search;
 else etaKnown=false; // A mid-search attachment cannot reconstruct its prefix.
 for(const auto& slice:out.slices)out.fraction+=slice.weight*slice.fraction;
 if(rate>0&&std::isfinite(rate)){out.taskEta=(total-done)/rate;if(etaKnown)out.cycleEta=remaining/rate;}
 return out;
}
}
