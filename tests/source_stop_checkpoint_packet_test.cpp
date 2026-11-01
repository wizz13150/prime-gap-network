#include "mining/source_stop_checkpoint_packet.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <array>
#include <iostream>
#include <stdexcept>

using namespace pgn::mining;
namespace {
int checks{};
const QString run(32,'a'),device="GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac",label="C:/PGN/runtime/current-job/checkpoint";
void require(bool ok,const char* reason){++checks;if(!ok)throw std::runtime_error(reason);}
QJsonObject fixture(){
 QByteArray bytes("PGNCG001",8);for(const auto word:std::array<quint64,7>{1000,10,10000,3000,9007199254740993ULL,0,4})
  for(unsigned i=0;i<8;++i)bytes.append(char((word>>(8*i))&255));
 const QString tail="[pgn-counters] time_ns=9007199254740995 positions=3000 gaps=0 tests=9007199254740993 batches=4 processed_gaps=0\r\n[phase0-gpu] stopped positions=3000 checkpoint="+label+"\r\n";
 return {{"schema","pgn-runtime-snapshot-1"},{"state","OFF"},{"owned",false},
  {"manifest",QJsonObject{{"schema","pgn-native-process-1"},{"run",run},{"uuid",device},{"start","184467440737095517160"},{"end","184467440737095527160"}}},
  {"last_exit",QJsonObject{{"run",run},{"exit_code",0}}},
  {"checkpoint_evidence",QJsonObject{{"schema","pgn-stop-checkpoint-evidence-1"},{"status","captured"},{"error",""},{"run",run},
   {"checkpoint_label",label},{"checkpoint_base64",QString::fromLatin1(bytes.toBase64())},{"source_tail_utf8",tail}}}};
}
void rejected(const QJsonObject& packet){const auto decoded=decodeSourceStopCheckpointPacket(packet,run,device,label);require(!decoded.evidence&&!decoded.error.isEmpty(),"poisoned capture is unavailable, not usable checkpoint");}
}
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);
 try{
  const auto good=fixture();
  const auto parsed=QJsonDocument::fromJson(QJsonDocument(good).toJson(QJsonDocument::Compact)).object();
  const auto decoded=decodeSourceStopCheckpointPacket(parsed,run,device,label);
  if(!decoded.evidence)throw std::runtime_error("Packet fixture rejected: "+decoded.error.toStdString());
  require(decoded.evidence&&decoded.error.isEmpty(),"wire JSON roundtrip decodes actual producer format");
  const auto checked=validateSourceStopCheckpoint(*decoded.evidence);
  require(checked.resume&&checked.resume->nextStart=="184467440737095520160"&&checked.resume->remaining=="7000"&&
   checked.resume->tests=="9007199254740993"&&checked.resume->counterTimeNs=="9007199254740995","decoded bytes preserve exact range and counters over2^53");
  for(const auto value:{QJsonValue{},QJsonValue(QJsonValue::Null),QJsonValue(true),QJsonValue(0),QJsonValue("false")}){auto p=good;p.insert("owned",value);rejected(p);}
  {auto p=good;p.remove("checkpoint_evidence");rejected(p);}
  {auto p=good;p.insert("state","RUNNING");rejected(p);}
  for(const auto key:{"schema","status","error","run","checkpoint_label","checkpoint_base64","source_tail_utf8"}){
   auto p=good;auto c=p["checkpoint_evidence"].toObject();c.insert(key,37);p.insert("checkpoint_evidence",c);rejected(p);
  }
  for(const auto value:{QString("unavailable"),QString("captured ")}){auto p=good;auto c=p["checkpoint_evidence"].toObject();c.insert("status",value);p.insert("checkpoint_evidence",c);rejected(p);}
  for(const auto value:{QString(88,'!'),QString(88,'A'),good["checkpoint_evidence"].toObject()["checkpoint_base64"].toString()+"\n"}){
   auto p=good;auto c=p["checkpoint_evidence"].toObject();c.insert("checkpoint_base64",value);p.insert("checkpoint_evidence",c);rejected(p);
  }
  for(const auto value:{QString(65537,'x'),QString(50000,'"'),QString(QChar(0xd800)),QString("partial")}){
   auto p=good;auto c=p["checkpoint_evidence"].toObject();c.insert("source_tail_utf8",value);p.insert("checkpoint_evidence",c);rejected(p);
  }
  for(const auto key:{"run","uuid","start","end"}){auto p=good;auto m=p["manifest"].toObject();m.insert(key,"other");p.insert("manifest",m);rejected(p);}
  {auto p=good;p.insert("last_exit",QJsonObject{{"run",QString(32,'b')},{"exit_code",0}});rejected(p);}
  {auto p=good;p.insert("last_exit",QJsonObject{{"run",run},{"exit_code","0"}});rejected(p);}
  require(!decodeSourceStopCheckpointPacket(good,{},device,label).evidence,"caller must select expected run");
  require(!decodeSourceStopCheckpointPacket(good,run,"GPU-other",label).evidence,"caller must select exact GPU");
  require(!decodeSourceStopCheckpointPacket(good,run,device,label+".other").evidence,"packet cannot choose its own expected checkpoint path");
  std::cout<<"Source stop checkpoint packet: "<<checks<<" checks / 0 failures; pure JSON/bytes only, no process/storage/control\n";return 0;
 }catch(const std::exception& e){std::cerr<<"Source stop checkpoint packet failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}
}
