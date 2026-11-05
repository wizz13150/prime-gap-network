#include "booking/booking_store.h"
#include "booking/decimal_range.h"
#include "mining/source_checkpoint_recovery.h"
#include "mining/source_stop_checkpoint_packet.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <iostream>
using namespace pgn::booking;
using namespace pgn::mining;
using checkpoint_recovery::need;
QJsonObject read(const QString& path,int max){QFile f(path);need(f.open(QIODevice::ReadOnly)&&f.size()>0&&f.size()<=max,"Bounded input unavailable");QJsonParseError e;auto d=QJsonDocument::fromJson(f.readAll(),&e);need(e.error==QJsonParseError::NoError&&d.isObject(),"Input JSON invalid");return d.object();}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{
 const auto a=app.arguments();need(a.size()==5&&(a[1]=="--validate"||a[1]=="--apply"),"Usage --validate/--apply DB package snapshot");
 const auto p=read(a[3],16384),s=read(a[4],131072);const auto manifest=s.value("manifest").toObject();const auto run=manifest.value("run").toString(),device=manifest.value("uuid").toString(),parent=manifest.value("resume_previous_run").toString();const QString label="C:\\PGN\\runtime\\current-job\\checkpoint";
 const auto packet=decodeSourceStopCheckpointPacket(s,run,device,label);need(packet.evidence.has_value(),qPrintable(packet.error));const auto stop=validateSourceStopCheckpoint(*packet.evidence);need(stop.resume.has_value(),qPrintable(stop.error));
 const auto bytes=QByteArray::fromBase64(p.value("checkpoint").toString().toLatin1());const auto sha=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
 QJsonObject proof{{"schema","pgn-checkpoint-publication-recovery-1"},{"parent_exit_base64",p.value("parent_exit")},{"publication_base64",p.value("publication")},{"seal_base64",p.value("seal")},{"parent_source_sha256",p.value("source_sha256").toString().toLower()},{"source_image_sha256",manifest.value("source_profile").toObject().value("image_sha256")}};
 const auto ev=checkpoint_recovery::validate(proof,parent,run,sha,bytes);need(ev.childPid==quint64(manifest.value("pid").toInteger())&&QJsonValue(ev.childBirth)==manifest.value("birth")&&QJsonValue(ev.imageSha)==manifest.value("source_profile").toObject().value("image_sha256"),"Actual seal identity differs");
 const auto seal=checkpoint_recovery::decoded(proof,"seal_base64",8192);need(seal.value("checkpoint_sha256").toString().toLower()==stop.resume->checkpointSha256,"Actual stopped output hash differs");
 int rejected=0;for(const auto key:proof.keys()){auto bad=proof;bad.remove(key);try{checkpoint_recovery::validate(bad,parent,run,sha,bytes);}catch(...){++rejected;}}
 auto badBytes=bytes;badBytes[32]=char(badBytes[32]^1);try{checkpoint_recovery::validate(proof,parent,run,sha,badBytes);}catch(...){++rejected;}
 try{checkpoint_recovery::validate(proof,run,parent,sha,bytes);}catch(...){++rejected;}
 need(rejected==8,"Recovery poison failed");
 QJsonObject out{{"state","REAL_RECOVERY_EVIDENCE_VALIDATED"},{"negative_checks",rejected},{"run",run},{"source_started",false}};
 if(a[1]=="--apply"){
  SourceCheckpointResume cp;cp.run=parent;cp.start=manifest.value("start").toString();cp.end=manifest.value("end").toString();cp.length=subtractUnsigned(cp.end,cp.start);cp.positions=QString::number(checkpoint_recovery::word(bytes,32));cp.tests=QString::number(checkpoint_recovery::word(bytes,40));cp.gaps=QString::number(checkpoint_recovery::word(bytes,48));cp.batches=QString::number(checkpoint_recovery::word(bytes,56));cp.counterTimeNs=p.value("counter_time_ns").toString();cp.nextStart=addUnsigned(cp.start,cp.positions);cp.remaining=subtractUnsigned(cp.length,cp.positions);cp.checkpointLabel=label;cp.checkpointSha256=sha;
  BookingStore store(a[2]);auto error=store.saveResumeCheckpoint(device,cp,bytes);need(error.isEmpty(),qPrintable(error));
  const auto admitted=store.prepareCheckpointResume(p.value("request_id").toString(),device,manifest.value("queued_intent").toString(),parent,run,sha,p.value("observation_id").toString(),proof);need(admitted.error.isEmpty(),qPrintable(admitted.error));
  const auto actual=store.reconcileRecoveredDispatch(s,*stop.resume,packet.evidence->checkpointBytes);need(actual.error.isEmpty(),qPrintable(actual.error));need(actual.run==run,"Actual registration run differs");
  out["state"]="REAL_RECOVERY_REGISTERED_PAUSED_NO_LAUNCH";
 }
 std::cout<<QJsonDocument(out).toJson(QJsonDocument::Compact).constData()<<std::endl;return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}}
