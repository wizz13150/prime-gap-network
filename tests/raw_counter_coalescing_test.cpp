// Real captured packets, replayed CPU-only through the qualified native bridge.
// This is presentation/counter math evidence, never physical mining freshness.
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <cstdio>
#include "mining/native_bridge.h"
#include "mining/raw_counter_coalescing.h"
static QJsonObject replay(QJsonObject packet){
 QObject lifetime;pgn::mining::NativeBridge bridge;
 bridge.attachNativeRuntime(&lifetime,[](const QJsonObject&){return false;});
 QJsonObject last;unsigned callbacks=0;
 bridge.onDevice=[&](const FrontierTelemetrySnapshot& lane){
  ++callbacks;last={{"run",lane.runId},{"stale",lane.stale},{"state",lane.state},
   {"positions",lane.rowsCompleted},{"source_utc",lane.observedUtc.toUTC().toString(Qt::ISODateWithMs)},
   {"interval_ns",lane.raw.value("pgn_source_interval_ns")},
   {"positions_s",lane.e2ePositionsPerSecond?QJsonValue(*lane.e2ePositionsPerSecond):QJsonValue()}};
 };
 bridge.acceptNativeRuntimePacket(&lifetime,packet);last["callbacks"]=int(callbacks);return last;
}
int main(int argc,char**argv){
 QCoreApplication app(argc,argv);if(argc!=2)return 2;
 QFile f(QString::fromLocal8Bit(argv[1]));if(!f.open(QIODevice::ReadOnly)||f.size()>2*1024*1024)return 3;
 const auto document=QJsonDocument::fromJson(f.readAll());if(!document.isArray())return 4;
 QJsonObject oldLatest,newLatest;unsigned carries=0,frames=0;
 for(const auto& value:document.array()){
  auto packet=value.toObject();packet["type"]="snapshot";packet["native_observation_only"]=true;
  packet["lines"]=packet.value("telemetry_lines");packet.remove("telemetry_lines");packet.remove("chunk");
  oldLatest=packet;
  if(!newLatest.isEmpty()&&pgn::mining::carryRawCounterPair(newLatest,packet,524288))++carries;
  newLatest=packet;++frames;
 }
 const auto old=replay(oldLatest),corrected=replay(newLatest);
 if(old.value("positions_s").isDouble()||!corrected.value("positions_s").isDouble()||corrected.value("stale")!=false)return 5;
 for(const auto* field:{"manifest","observed_utc","source_clock_ns","binding","owned","state"})
  if(oldLatest.value(field)!=newLatest.value(field))return 8;
 // A second coalescing boundary may have a different delivery cadence.
 QJsonObject nestedLatest,stageLatest;unsigned staged=0;
 for(const auto& value:document.array()){
  auto packet=value.toObject();packet["type"]="snapshot";packet["lines"]=packet.value("telemetry_lines");
  packet.remove("telemetry_lines");packet.remove("chunk");
  if(!stageLatest.isEmpty())pgn::mining::carryRawCounterPair(stageLatest,packet,524288);
  stageLatest=packet;
  if(++staged%2==0){
   if(!nestedLatest.isEmpty())pgn::mining::carryRawCounterPair(nestedLatest,stageLatest,524288);
   nestedLatest=stageLatest;stageLatest={};
  }
 }
 if(!stageLatest.isEmpty()){
  if(!nestedLatest.isEmpty())pgn::mining::carryRawCounterPair(nestedLatest,stageLatest,524288);
  nestedLatest=stageLatest;
 }
 const auto nested=replay(nestedLatest);
 if(nested.value("positions_s")!=corrected.value("positions_s")||
    nested.value("positions")!=corrected.value("positions"))return 9;
 // Equal clocks with changed counts are corrupt, never interchangeable.
 auto poison=oldLatest;auto pair=newLatest.value("lines").toArray();
 QString conflict=pair.last().toString();
 const QRegularExpression positions(" positions=([0-9]+) ");const auto match=positions.match(conflict);
 conflict.replace(match.capturedStart(1),match.capturedLength(1),QString::number(match.captured(1).toULongLong()+1));
 poison["lines"]=QJsonArray{conflict};bool rejected=false;
 try{pgn::mining::carryRawCounterPair(newLatest,poison,524288);}catch(const std::exception&){rejected=true;}
 if(!rejected)return 10;
 // Synthetic display-only saturation fixture; RAW archival is never modified.
 auto crowded=oldLatest;QJsonArray noise;
 for(int i=0;i<15;++i)noise.append(QString(32700,'x'));
 crowded["lines"]=noise;
 auto remaining=524288-QJsonDocument(crowded).toJson(QJsonDocument::Compact).size()-16;
 if(remaining<1)return 11;
 noise.append(QString(remaining,'x'));crowded["lines"]=noise;
 if(!pgn::mining::carryRawCounterPair(newLatest,crowded,524288)||
    QJsonDocument(crowded).toJson(QJsonDocument::Compact).size()>524288||
    crowded.value("native_display_truncated")!=true)return 12;
 // Real zero-advance intervals must remain zero; no hold-last-positive display.
 auto zeroPrevious=oldLatest,zeroNext=oldLatest;
 zeroPrevious["lines"]=QJsonArray{
  "[pgn-counters] time_ns=1000000000 positions=1000 gaps=1 tests=200 batches=2 processed_gaps=1",
  "[pgn-counters] time_ns=2000000000 positions=1000 gaps=1 tests=200 batches=2 processed_gaps=1"};
 zeroNext["lines"]=QJsonArray{};zeroNext["source_clock_ns"]="2500000000";
 if(!pgn::mining::carryRawCounterPair(zeroPrevious,zeroNext,524288))return 13;
 const auto zero=replay(zeroNext);
 if(!zero.value("positions_s").isDouble()||zero.value("positions_s").toDouble()!=0)return 14;
  auto nativePrevious=zeroPrevious,nativeNext=zeroNext;QJsonArray nativeRows;
  for(const auto& value:zeroPrevious.value("lines").toArray())nativeRows.append(value.toString()+" emitted_gaps=1");
  nativePrevious["lines"]=nativeRows;nativeNext["lines"]=QJsonArray{};
  if(!pgn::mining::carryRawCounterPair(nativePrevious,nativeNext,524288)||nativeNext.value("lines").toArray()!=nativeRows)return 15;
  if(pgn::mining::sourceDisplayCounterGrammar().match("[phase0-gpu] GAP merit=1.0000 gap=2 lower=3 upper=5").hasMatch())return 16;
 // Cross-run and immutable-manifest changes must never inherit counter pairs.
 auto changed=oldLatest;auto manifest=changed.value("manifest").toObject();manifest["run"]="another-producer";changed["manifest"]=manifest;
 if(pgn::mining::carryRawCounterPair(newLatest,changed,524288))return 6;
 changed=oldLatest;manifest=changed.value("manifest").toObject();manifest["pid"]=999999;changed["manifest"]=manifest;
 if(pgn::mining::carryRawCounterPair(newLatest,changed,524288))return 7;
 const auto out=QJsonDocument(QJsonObject{{"state","REAL_STREAM_LAST_EMPTY_FRAME_LOSES_RATES_REPRODUCED_PAIR_CARRY_REPAIRS"},
  {"frames",int(frames)},{"carried_frames",int(carries)},{"old_latest_only",old},{"corrected_latest",corrected},
  {"nested_boundaries_exact",true},{"successor_metadata_unchanged",true},{"duplicate_clock_poison_rejected",true},{"publication_bound_pass",true},{"real_zero_interval_preserved",true},
  {"scope","CPU_REPLAY_NO_GPU_OR_PHYSICAL_FRESHNESS_CLAIM_NO_COUNTER_SMOOTHING"}}).toJson(QJsonDocument::Compact);
 std::fwrite(out.data(),1,out.size(),stdout);std::fputc('\n',stdout);return 0;
}
