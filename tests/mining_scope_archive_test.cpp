#include "science/mining_scope_archive.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>
using pgn::science::MiningScopeArchive;
static int checks;
void require(bool value){++checks;if(!value)throw std::runtime_error("check "+std::to_string(checks));}
QJsonObject frame(int sequence,int positions,int n,double target=32,int minimum=954){
 QJsonObject stats{{"count_total",n},{"at_or_above_target",0},{"statistics_target",target},{"threshold_counts",QJsonObject{{QString::number(minimum),n},{"1000",n}}},{"merit_band_counts",QJsonObject{{"21",n}}}};
 QJsonObject row{{"gap",1000},{"lower_prime",QString::number(100000+sequence)},{"upper_prime",QString::number(101000+sequence)},{"merit","21.1"}};
 return {{"stream_id","source"},{"ticket_id",QString(63,'0')+QString::number(sequence)},{"positions",QString::number(positions)},{"local_sequence",sequence},{"target_merit",target},{"scopes",QJsonObject{{"ticket",QJsonObject{{"statistics",stats},{"best",QJsonArray{row}},{"recent",QJsonArray{row}}}}}}};
}
qint64 total(const MiningScopeArchive& a){return a.scope().value("statistics").toObject().value("count_total").toVariant().toLongLong();}
int main(int argc,char** argv){QCoreApplication app(argc,argv);QTemporaryDir dir;require(dir.isValid());const auto file=dir.path()+"/archive.json";
 {MiningScopeArchive a(file);require(a.observe(frame(1,10,3)));require(a.observe(frame(1,10,3)));require(total(a)==3);require(a.observe(frame(1,20,7)));require(total(a)==7);require(a.observe(frame(2,10,2)));require(total(a)==9);require(a.scope().value("ticket_count").toString()=="2");a.flush();MiningScopeArchive locked(file);require(!locked.error().isEmpty());require(QFileInfo(file).size()<32768);}
 {MiningScopeArchive a(file);require(a.error().isEmpty());require(total(a)==9);require(a.observe(frame(2,10,2)));require(total(a)==9);require(a.observe(frame(3,10,1,29,960)));require(total(a)==10);require(a.scope().value("statistics").toObject().value("mixed_targets").toBool());require(!a.scope().value("statistics").toObject().value("threshold_counts").toObject().contains("954"));require(!a.observe(frame(1,10,3)));require(total(a)==10);require(a.observe(frame(3,20,4,29,960)));require(total(a)==13);require(a.error().isEmpty());auto invalid=frame(4,30,3);auto scopes=invalid.value("scopes").toObject(),ticket=scopes.value("ticket").toObject(),stats=ticket.value("statistics").toObject();stats.insert("count_total",-1);ticket.insert("statistics",stats);scopes.insert("ticket",ticket);invalid.insert("scopes",scopes);require(!a.observe(invalid));require(total(a)==13);require(a.observe(frame(3,20,4,29,960)));require(total(a)==13);}
 {QFile out(dir.path()+"/large.json");require(out.open(QIODevice::WriteOnly));out.write(QByteArray(2*1024*1024+1,' '));out.close();MiningScopeArchive a(out.fileName());require(!a.error().isEmpty());require(!a.observe(frame(1,1,1)));require(QFileInfo(out).size()==2*1024*1024+1);}
 {MiningScopeArchive a;auto f=frame(1,0,3);f.insert("tests","30");f.insert("compute_epoch","p1");f.insert("compute_time_ns","0");require(a.observe(f));f=frame(1,100,10);f.insert("tests","100");f.insert("compute_epoch","p1");f.insert("compute_time_ns","10000000000");f.insert("phase","Report ready");auto sc=f.value("scopes").toObject(),t=sc.value("ticket").toObject(),st=t.value("statistics").toObject();st.insert("at_or_above_target",2);st.insert("merit_band_counts",QJsonObject{{"21",8}});t.insert("statistics",st);sc.insert("ticket",t);f.insert("scopes",sc);require(a.observe(f));require(a.scope().value("active_time_ns").toString()=="10000000000");require(a.scope().value("observed_golds").toString()=="2");require(a.scope().value("completed_tickets").toString()=="1");require(a.scope().value("positions").toString()=="100");f.insert("compute_epoch","p2");f.insert("compute_time_ns","0");require(a.observe(f));f.insert("compute_time_ns","1000000000");require(a.observe(f));require(a.scope().value("active_time_ns").toString()=="11000000000");require(a.scope().value("observed_golds").toString()=="2");}
 {
  const auto path=dir.path()+"/search-only.json";MiningScopeArchive a(path);
  auto search=frame(1,10,3);search.insert("work_role","search");require(a.observe(search));a.flush();
  QFile beforeFile(path);require(beforeFile.open(QIODevice::ReadOnly));const auto beforeBytes=beforeFile.readAll();beforeFile.close();const auto before=a.scope();
  auto audit=frame(99,10000,999);audit.insert("work_role","audit");audit.insert("phase","Report ready");audit.insert("ticket_id","other-miner-audit");
  require(!a.observe(audit));require(a.scope()==before&&a.error().isEmpty());a.flush();
  QFile afterFile(path);require(afterFile.open(QIODevice::ReadOnly));require(afterFile.readAll()==beforeBytes);afterFile.close();
  auto future=frame(2,10,7);future.insert("work_role","unrecognized");require(!a.observe(future));require(a.scope()==before&&!a.error().isEmpty());
  search=frame(2,10,2);search.insert("work_role","search");require(a.observe(search));require(a.error().isEmpty()&&total(a)==5&&a.scope().value("ticket_count").toString()=="2");
 }
 {
  const auto path=dir.path()+"/measured-search.json";
  const auto sample=[](int ticket,int positions,quint64 ns,const QString& epoch,bool done,double target=27.5){
   auto value=frame(ticket,positions,5,target);value.insert("work_role","search");value.insert("compute_time_ns",QString::number(ns));value.insert("compute_epoch",epoch);
   auto scopes=value.value("scopes").toObject(),data=scopes.value("ticket").toObject(),stats=data.value("statistics").toObject();
   stats.insert("at_or_above_target",1);stats.insert("merit_band_counts",QJsonObject{{"21",4}});
   stats.insert("rosette_counts",QJsonObject{{"gold",1},{"silver",1},{"bronze",1},{"rust",1},{"ordinary",1}});
   data.insert("statistics",stats);scopes.insert("ticket",data);value.insert("scopes",scopes);
   if(done){value.insert("phase","Report ready");if(epoch!="after-restart")value.insert("completed_search_timing",QJsonObject{{"schema","PGN_COMPLETE_SEARCH_TIMING_V1"},{"ticket_id",value.value("ticket_id")},{"active_time_ns",QString::number(ns)}});}return value;
  };
  {
   MiningScopeArchive a(path);require(a.observe(sample(1,0,0,"epoch1",false)));
   require(a.observe(sample(1,100,30'000'000'000ULL,"epoch1",true)));
   require(a.currentScope().value("completed_timed_tickets")=="1");require(a.currentScope().value("completed_compute_time_ns")=="30000000000");
   require(a.observe(sample(1,100,50'000'000'000ULL,"epoch1",true)));
   require(a.currentScope().value("completed_compute_time_ns")=="30000000000");
   require(a.observe(sample(1,100,50'000'000'000ULL,"epoch1",true)));
   require(a.scope().value("completed_timed_tickets")=="1");a.flush();
  }
  {
   MiningScopeArchive a(path);require(a.error().isEmpty());require(a.currentScope().value("completed_compute_time_ns")=="30000000000");
   require(a.observe(sample(2,0,0,"epoch2",false,29.3)));require(a.observe(sample(2,100,10'000'000'000ULL,"epoch2",true,29.3)));
   const auto all=a.scope();require(all.value("completed_timed_tickets")=="2");require(all.value("completed_compute_time_ns")=="40000000000");
   const auto stats=all.value("statistics").toObject();require(stats.value("mixed_targets").toBool());
   const auto medals=stats.value("rosette_counts").toObject();for(const auto* key:{"gold","silver","bronze","rust","ordinary"})require(medals.value(key)=="2");
   auto invalid=sample(2,100,11'000'000'000ULL,"epoch2",true,29.3);auto scopes=invalid.value("scopes").toObject(),data=scopes.value("ticket").toObject(),bad=data.value("statistics").toObject();
   auto wrong=bad.value("rosette_counts").toObject();wrong.insert("silver",99);bad.insert("rosette_counts",wrong);data.insert("statistics",bad);scopes.insert("ticket",data);invalid.insert("scopes",scopes);
   require(!a.observe(invalid));require(a.scope()==all);
  }
  {
   MiningScopeArchive a;require(a.observe(sample(1,50,3'000'000'000ULL,"partial",false)));require(a.observe(sample(1,100,10'000'000'000ULL,"partial",true)));
   require(a.scope().value("completed_timed_tickets")=="1");require(a.scope().value("completed_compute_time_ns")=="10000000000");
  }
  {
   MiningScopeArchive a;require(a.observe(sample(1,0,0,"before-restart",false)));require(a.observe(sample(1,50,5'000'000'000ULL,"before-restart",false)));
   require(a.observe(sample(1,100,1'000'000'000ULL,"after-restart",true)));require(a.scope().value("completed_timed_tickets")=="0");
  }
  {
   MiningScopeArchive a;require(a.observe(frame(1,10,5)));require(a.observe(sample(2,0,0,"new-version",false)));
   require(a.scope().value("statistics").toObject().value("rosette_counts").isNull());
  }
 }
 std::cout<<"PASS "<<checks<<" compact archive checks\n";
}
