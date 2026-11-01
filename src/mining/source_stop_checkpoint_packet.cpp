#include "mining/source_stop_checkpoint_packet.h"
#include <QJsonDocument>
#include <QRegularExpression>
#include <stdexcept>
#include <utility>

namespace pgn::mining {
namespace {
void require(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
}
SourceStopCheckpointPacket decodeSourceStopCheckpointPacket(const QJsonObject& snapshot,
    const QString& expectedRun,const QString& expectedDevice,const QString& expectedLabel,
    const std::optional<PrimeGapEngineJob>& selectedEngine){
 try{
  static const QRegularExpression run("^[0-9a-f]{32}$"),device("^gpu-[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$");
  require(expectedRun.size()==32&&run.match(expectedRun).hasMatch(),"Expected stop run is unavailable");
  require(expectedDevice.size()==40&&device.match(expectedDevice.toLower()).hasMatch(),"Expected stop GPU is unavailable");
  require(!expectedLabel.isEmpty()&&expectedLabel.toUtf8().size()<=4096&&!expectedLabel.contains(QChar(0))&&
   !expectedLabel.contains('\r')&&!expectedLabel.contains('\n')&&QString::fromUtf8(expectedLabel.toUtf8())==expectedLabel,"Expected checkpoint label is invalid");
  require(snapshot.value("schema")=="pgn-runtime-snapshot-1"&&snapshot.value("state")=="OFF"&&
   snapshot.value("owned").isBool()&&!snapshot.value("owned").toBool(),"Stopped source ownership is unconfirmed");
  const auto manifest=snapshot.value("manifest").toObject(),lastExit=snapshot.value("last_exit").toObject();
  require(manifest.value("run")==expectedRun&&manifest.value("uuid").isString()&&
   manifest.value("uuid").toString().compare(expectedDevice,Qt::CaseInsensitive)==0,"Stopped source identity differs from selected target");
  require(snapshot.value("checkpoint_evidence").isObject(),"Checkpoint capture is absent");
  const auto capture=snapshot.value("checkpoint_evidence").toObject();
  require(QJsonDocument(capture).toJson(QJsonDocument::Compact).size()<=98304,"Checkpoint capture envelope exceeds its encoded bound");
  require(capture.value("schema")=="pgn-stop-checkpoint-evidence-1"&&capture.value("status")=="captured"&&
   capture.value("error").isString()&&capture.value("error").toString().isEmpty(),"Checkpoint capture is unavailable");
  require(capture.value("run")==expectedRun&&capture.value("checkpoint_label")==expectedLabel,"Checkpoint capture run or label differs from selected profile");
  require(capture.value("checkpoint_base64").isString()&&capture.value("source_tail_utf8").isString(),"Checkpoint payload types are invalid");
  const auto encoded=capture.value("checkpoint_base64").toString();
  require(encoded.size()==88&&QString::fromLatin1(encoded.toLatin1())==encoded,"Checkpoint base64 representation is invalid");
  const auto decoded=QByteArray::fromBase64Encoding(encoded.toLatin1(),QByteArray::AbortOnBase64DecodingErrors);
  require(bool(decoded)&&decoded.decoded.size()==64&&QString::fromLatin1(decoded.decoded.toBase64())==encoded,"Checkpoint base64 is not canonical64 bytes");
  const auto text=capture.value("source_tail_utf8").toString();const auto tail=text.toUtf8();
  require(!tail.isEmpty()&&tail.size()<=65536&&QString::fromUtf8(tail)==text,"Checkpoint source tail is not bounded exact UTF8");
  SourceStopCheckpointEvidence evidence{decoded.decoded,tail,manifest,lastExit,false,expectedRun,expectedLabel};
  if(selectedEngine){
   const auto job=primeGapEngineJob(selectedEngine->start,selectedEngine->end,selectedEngine->descriptorK,selectedEngine->minimumGap);
   require(manifest.value("start")==job.start&&manifest.value("end")==job.end,"Checkpoint packet differs from caller-selected engine job");
   evidence.dialect=SourceCheckpointDialect::ScoutGapBound;evidence.expectedGapMinimum=job.minimumGap;
  }
  const auto checked=validateSourceStopCheckpoint(evidence);
  if(checked.status!=SourceStopCheckpointStatus::Ready||!checked.resume)return {{},checked.error};
  return {std::move(evidence),{}};
 }catch(const std::exception& e){return {{},QString::fromUtf8(e.what())};}
}
}
