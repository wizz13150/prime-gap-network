#include "mining/cpu_audit_work.h"
#include "mining/cpu_audit_result.h"
#include <QCoreApplication>
#include <iostream>
#include <stdexcept>
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);using namespace pgn::mining;using boost::multiprecision::cpp_int;
 int checks=0;auto require=[&](bool okay){++checks;if(!okay)throw std::runtime_error("CPU assignment check "+std::to_string(checks));};
 const auto now=QDateTime::currentDateTimeUtc();const QString genesis(64,'a'),request(64,'b'),ticket(64,'c'),root(64,'d'),actor="0014"+QString(40,'e');
 pgn::wallet::ChainObservation anchor{{42,"owned-epoch",1},genesis,200,now,false,QString(64,'f'),1};
 QJsonObject report{{"request_id",request},{"ticket",ticket},{"report_root",root}};
 QJsonObject lease{{"request",request},{"report_root",root},{"state","submitted_pending_audits"},{"start","150000000000000000000"},{"end","150000200000000000000"}};
 QJsonArray ranges;const cpp_int start("150000000000000000000");
 for(int i:{0,9,20,40,99})ranges.append(QJsonObject{{"index",i},{"start",QString::fromStdString((start+cpp_int("2000000000000")*i).convert_to<std::string>())},{"end",QString::fromStdString((start+cpp_int("2000000000000")*(i+1)).convert_to<std::string>())}});
 QJsonObject job{{"slot",0},{"attempt",0},{"actor",actor},{"state","assigned"},{"expired",false},{"issued",199},{"deadline_clock","median_chain_time"},{"issued_time",1000},{"deadline_time",87400},{"ranges",ranges}};
 auto run=[&](QJsonObject r,QJsonObject l,QJsonObject j){return cpuAuditWork(anchor,genesis,r,l,j,actor,now);};
 const auto good=run(report,lease,job);if(!good.work)std::cerr<<good.error.toStdString()<<"\n";require(good.work.has_value());require(good.work->serviceRequest.size()==5);require(good.work->requestSha256.size()==64);
 require(!good.work->serviceRequest.contains("actor")&&!good.work->serviceRequest.contains("report_root")&&!good.work->serviceRequest.contains("counts"));
 require(!cpuAuditWork(anchor,genesis,report,lease,job,actor,now.addSecs(31)).work);
 require(!cpuAuditWork(anchor,QString(64,'0'),report,lease,job,actor,now).work);
 require(!cpuAuditWork(anchor,genesis,report,lease,job,"0014"+QString(40,'0'),now).work);
 for(auto key:{"counts","expected_counts","answer","nonce"}){auto bad=job;bad.insert(key,QJsonArray{1,2,3,4,5});require(!run(report,lease,bad).work);}
 auto bad=job;bad.insert("expired",true);require(!run(report,lease,bad).work);
 bad=job;bad.insert("deadline_clock","ledger_height");require(!run(report,lease,bad).work);
 bad=job;bad.insert("state","committed");require(!run(report,lease,bad).work);
 bad=job;bad.insert("attempt",0.5);require(!run(report,lease,bad).work);
 bad=job;bad.insert("slot",2);require(!run(report,lease,bad).work);
 bad=job;bad.insert("deadline_time",999);require(!run(report,lease,bad).work);
 auto wrong=report;wrong.insert("report_root",QString(64,'0'));require(!run(wrong,lease,job).work);
 auto smaller=lease;smaller.insert("end","150000100000000000000");require(!run(report,smaller,job).work);
 auto noncanonical=lease;noncanonical.insert("start","0150000000000000000000");require(!run(report,noncanonical,job).work);
 auto partitions=ranges;partitions.removeLast();bad=job;bad.insert("ranges",partitions);require(!run(report,lease,bad).work);
 partitions=ranges;partitions[1]=partitions[0];bad=job;bad.insert("ranges",partitions);require(!run(report,lease,bad).work);
 partitions=ranges;auto shortPart=partitions[0].toObject();shortPart.insert("end","150000001000000000000");partitions[0]=shortPart;bad=job;bad.insert("ranges",partitions);require(!run(report,lease,bad).work);
 require(cpuAuditProposalSupported(job));
 auto crossing=lease;const cpp_int boundary=cpp_int(1)<<68,crossStart=boundary-cpp_int("100000000000000");
 crossing.insert("start",QString::fromStdString(crossStart.convert_to<std::string>()));crossing.insert("end",QString::fromStdString((crossStart+cpp_int("200000000000000")).convert_to<std::string>()));
 QJsonArray lowParts;for(int i:{0,9,20,40,49})lowParts.append(QJsonObject{{"index",i},{"start",QString::fromStdString((crossStart+cpp_int("2000000000000")*i).convert_to<std::string>())},{"end",QString::fromStdString((crossStart+cpp_int("2000000000000")*(i+1)).convert_to<std::string>())}});
 auto edgeJob=job;edgeJob.insert("ranges",lowParts);require(run(report,crossing,edgeJob).work.has_value());
 auto high=lowParts[4].toObject();high.insert("index",99);high.insert("start",QString::fromStdString((crossStart+cpp_int("2000000000000")*99).convert_to<std::string>()));high.insert("end",crossing.value("end"));lowParts[4]=high;edgeJob.insert("ranges",lowParts);require(!cpuAuditProposalSupported(edgeJob)&&!run(report,crossing,edgeJob).work);
 QJsonObject computed{{"schema","PGN_CPU_SHARED68_AUDIT_COMPUTATION_V1"},{"request_sha256",good.work->requestSha256},{"state","AUDIT_COMPUTED_SOURCE_PROOF_PENDING"},{"completed_positions","10000000000000"},{"full_audit",true},{"coverage_contract","SOURCE_PRP_INDEPENDENT_PROOF_PENDING"},{"production_admitted",false},{"network_accepted",false},{"counts",QJsonArray{"0","1","2","3","1000000000000"}}};
 require(cpuAuditResultError(computed,good.work->requestSha256).isEmpty());
 require(!cpuAuditResultError(computed,QString(64,'0')).isEmpty());
 for(const auto& change:{QJsonObject{{"schema","PGN_CPU_SHARED68_PROBE_RESULT_V1"}},QJsonObject{{"full_audit",false}},QJsonObject{{"network_accepted",true}},QJsonObject{{"production_admitted",true}},QJsonObject{{"completed_positions","9999999999999"}},QJsonObject{{"counts",QJsonArray{"0","1","2","3"}}},QJsonObject{{"counts",QJsonArray{"0","1","2","3","1000000000001"}}},QJsonObject{{"counts",QJsonArray{"0","1","2","3",1}}},QJsonObject{{"counts",QJsonArray{"0","1","2","3","-1"}}}}){auto invalid=computed;for(auto i=change.begin();i!=change.end();++i)invalid.insert(i.key(),i.value());require(!cpuAuditResultError(invalid,good.work->requestSha256).isEmpty());}
 std::cout<<"PASS "<<checks<<" CPU assigned-work boundary checks; no computation or network acceptance claimed\n";
}
