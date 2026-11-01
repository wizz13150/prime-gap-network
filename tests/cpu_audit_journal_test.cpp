#include "mining/cpu_audit_journal.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);using namespace pgn::mining;QTemporaryDir directory;int checks=0;auto require=[&](bool value){++checks;if(!value)throw std::runtime_error("audit journal check "+std::to_string(checks));};
 const auto path=directory.filePath("audit.json");auto read=[&]{QFile file(path);if(!file.open(QIODevice::ReadOnly))return QByteArray{};return file.readAll();};
 CpuAuditWork work;work.genesis=QString(64,'a');work.requestId=QString(64,'b');work.reportRoot=QString(64,'c');work.actor="0014"+QString(40,'d');work.slot=0;
 QJsonArray parts;for(int i:{0,9,20,40,99})parts.append(QJsonObject{{"index",i},{"start","150000000000000000000"},{"end","150000002000000000000"}});
 work.serviceRequest={{"schema","PGN_EXPLICIT_CPU_AUDIT_REQUEST_V1"},{"ticket_id",QString(64,'e')},{"ticket_start","150000000000000000000"},{"ticket_end","150000200000000000000"},{"partitions",parts}};
 work.requestSha256=QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(work.serviceRequest).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());const auto id=CpuAuditJournal::key(work);
 QJsonObject result{{"schema","PGN_CPU_SHARED68_AUDIT_COMPUTATION_V1"},{"request_sha256",work.requestSha256},{"state","AUDIT_COMPUTED_SOURCE_PROOF_PENDING"},{"full_audit",true},{"completed_positions","10000000000000"},{"coverage_contract","SOURCE_PRP_INDEPENDENT_PROOF_PENDING"},{"production_admitted",false},{"network_accepted",false},{"counts",QJsonArray{"0","1","2","3","4"}}};
 QJsonObject answer;QByteArray closed;
 {
  CpuAuditJournal journal(path);require(journal.load()&&journal.canRequestAssignment());require(journal.retainAssignment("CPU/fixture/0","tpgn1fixtureauditor",work));const auto first=read();require(journal.retainAssignment("CPU/fixture/0","tpgn1fixtureauditor",work)&&read()==first);
  CpuAuditJournal competing(path);require(!competing.load());
  auto wrong=work;wrong.reportRoot=QString(64,'f');require(!journal.retainAssignment("CPU/fixture/0","tpgn1fixtureauditor",wrong)&&read()==first);
  auto partial=result;partial.insert("completed_positions","99");require(!journal.retainComputation(work,partial)&&read()==first);
  require(journal.retainComputation(work,result));answer=journal.entry(id).value("answer").toObject();closed=read();require(answer.value("nonce").toString().size()==64);
  const auto commitment=QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(answer).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());require(journal.entry(id).value("commitment")==commitment);
  require(journal.retainComputation(work,result)&&read()==closed);auto different=result;different.insert("counts",QJsonArray{"9","1","2","3","4"});require(!journal.retainComputation(work,different)&&read()==closed);
  require(journal.retainAssignment("CPU/fixture/0","tpgn1fixtureauditor",work)&&read()==closed);
 }
 {
  CpuAuditJournal recovered(path);require(recovered.load());require(recovered.entry(id).value("answer")==answer);require(recovered.retainComputation(work,result)&&read()==closed);
  require(!recovered.recordPublication(id,"commit","bad")&&read()==closed);require(recovered.recordPublication(id,"commit",QString(64,'f')));
  const auto confirmed=read();require(recovered.recordPublication(id,"commit",QString(64,'f'))&&read()==confirmed);
  int accepted=1;for(int i=1;i<=20;++i){auto next=work;next.requestId=QString::number(i,16).rightJustified(64,'0');if(!recovered.retainAssignment("CPU/fixture/0","tpgn1fixtureauditor",next))break;++accepted;}
  require(accepted<=16&&recovered.entries().size()==accepted&&read().size()<=32768);require(recovered.entry(id).value("answer")==answer);
  require(!recovered.canRequestAssignment());
  // Admission reserves result space: every already-admitted worker can finish
  // with the largest valid counts even when another assignment is refused.
  for(int i=1;i<accepted;++i){auto next=work;next.requestId=QString::number(i,16).rightJustified(64,'0');auto maximum=result;maximum.insert("counts",QJsonArray{"1000000000000","1000000000000","1000000000000","1000000000000","1000000000000"});require(recovered.retainComputation(next,maximum));for(const auto* phase:{"assignment","commit","reveal"})require(recovered.recordPublication(CpuAuditJournal::key(next),phase,QString(64,'f')));}
  require(read().size()<=32768&&recovered.entry(id).value("answer")==answer);
 }
 std::cout<<"PASS "<<checks<<" bounded private audit outbox checks: one writer, no overwrite, closed result/nonce replay, conflicting counts rejected and capacity backpressure; fixture only, no computation\n";
}
