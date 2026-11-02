#include "mining/prime_gap_engine.h"
#include "mining/native_bridge.h"
#include "mining/source_age.h"
#include "mining/resource_age.h"
#include "booking/decimal_range.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTimeZone>
#include <QThread>
#include <QUuid>
#include <cmath>
#include <stdexcept>
#include <utility>
namespace pgn::mining {
static const QString uuid=QStringLiteral("GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac");
static const QString testUuid=QStringLiteral("GPU-33f44d5c-91e4-6ab6-1e45-8c93135fd265");
static bool canonicalRequestId(const QString& value){
 const auto parsed=QUuid::fromString(value);
 return !parsed.isNull()&&parsed.toString(QUuid::WithoutBraces)==value;
}
static bool nativeProcessUtc(const QJsonValue& value){
 if(!value.isString())return false;
 static const QRegularExpression pattern("^(\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2})(?:\\.\\d{1,9})?Z$");
 const auto match=pattern.match(value.toString());if(!match.hasMatch()||match.capturedLength()!=value.toString().size())return false;
 const auto stamp=QDateTime::fromString(match.captured(1)+'Z',Qt::ISODate);
 return stamp.isValid()&&stamp.toUTC().toString("yyyy-MM-dd'T'HH:mm:ss")==match.captured(1);
}
static std::optional<int> homogeneousRangeBits(const QString& start,const QString& end){
 // The manifest's end is exclusive. Keep exact decimal arithmetic; a range
 // crossing a power of two has no single bit size, and malformed input is
 // unavailable metadata, never a guessed number or a transport crash.
 try {
  using namespace pgn::booking;
  const auto first=canonicalUnsigned(start),limit=canonicalUnsigned(end);
  if(first=="0"||compareUnsigned(first,limit)>=0)return {};
  const auto low=bitLength(first),high=bitLength(subtractUnsigned(limit,"1"));
  return low==high?std::optional<int>(low):std::nullopt;
 } catch(const std::invalid_argument&){return {};}
}
NativeBridge::NativeBridge(QObject* parent):QObject(parent) {
 connect(&relay_,&QProcess::started,this,[this]{
  if(!observationOnly_&&onModuleRegistry)moduleAction(QJsonObject{{"action","module_list"},{"request_id","initial-module-catalogue"}});
 });
 connect(&relay_,&QProcess::readyReadStandardOutput,this,[this]{
  // Bound one protocol record, not the complete available replay stream.
  // A valid multi-megabyte history burst must not be silently discarded.
  while(relay_.bytesAvailable()>0)consumeBytes(relay_.read(65536));
 });
 connect(&relay_,&QProcess::readyReadStandardError,this,[this]{const auto text=relay_.readAllStandardError();if(onStatus)onStatus(QString::fromUtf8(text).left(250));});
 connect(&relay_,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){
  if(error==QProcess::FailedToStart)
   relayUnavailable("Relay process error: "+relay_.errorString());
 });
 connect(&relay_,&QProcess::finished,this,[this](int code,QProcess::ExitStatus status){
  while(relay_.bytesAvailable()>0)consumeBytes(relay_.read(65536));
  relayUnavailable(QString("Relay exited (%1, %2)").arg(code).arg(status==QProcess::CrashExit?"crash":"normal"));
 });
}
NativeBridge::~NativeBridge(){
 // QProcess destruction may terminate its own relay child. Disconnect before
 // pending state/callback members are destroyed; it never owns the GPU worker.
 relay_.disconnect(this);
 if(nativeRuntime_)nativeRuntime_->disconnect(this);
}
bool NativeBridge::runtimeAvailable() const{
 if(QThread::currentThread()!=thread())return false;
 return nativeRuntime_ ? nativeRuntime_->thread()==thread()&&nativeUsable_&&bool(nativeSend_) : relay_.state()==QProcess::Running;
}
bool NativeBridge::attachNativeRuntime(QObject* lifetime,NativeRequestSink sink){
 if(!lifetime||lifetime==this||!sink||QThread::currentThread()!=thread()||lifetime->thread()!=thread()||
    nativeRuntime_||relay_.state()!=QProcess::NotRunning||!applicationCloseId_.isEmpty())return false;
 nativeRuntime_=lifetime;nativeSend_=std::move(sink);nativeUsable_=true;observationOnly_=false;incoming_.clear();
 connect(lifetime,&QObject::destroyed,this,[this]{
  nativeRuntime_.clear();nativeSend_={};nativeUsable_=false;
  relayUnavailable("Native runtime destroyed; remote outcome unconfirmed, no automatic retry");
 });
 return true;
}
bool NativeBridge::attachNativeObservation(QObject* lifetime){
 if(!attachNativeRuntime(lifetime,[](const QJsonObject&){return false;}))return false;
 observationOnly_=true;return true;
}
bool NativeBridge::acceptNativeRuntimePacket(QObject* lifetime,const QJsonObject& packet,std::optional<qint64> resourceReceiptNs){
 if(!lifetime||nativeRuntime_.data()!=lifetime||!nativeUsable_||QThread::currentThread()!=thread()||lifetime->thread()!=thread())return false;
 if(packet.value("type")=="application_inventory_phase")return receiveNativeInventoryPhase(packet);
 receive(packet,resourceReceiptNs);return true;
}
bool NativeBridge::receiveNativeInventoryPhase(const QJsonObject& packet){
 QStringList expected{"type","request_id","inventory_id","purpose","phase","phase_id","active"};expected.sort();
 if(packet.keys()!=expected||!packet.value("active").isBool())return false;
 const auto request=packet.value("request_id"),inventory=packet.value("inventory_id"),id=packet.value("phase_id");
 if(!request.isString()||!inventory.isString()||!id.isString()||!canonicalRequestId(request.toString())||!canonicalRequestId(inventory.toString())||!canonicalRequestId(id.toString()))return false;
 NativeApplicationInventoryPhase event;event.requestId=request.toString();event.inventoryId=inventory.toString();event.phaseId=id.toString();event.active=packet.value("active").toBool();
 InventoryPhaseLatch* latch{};
 if(packet.value("purpose")=="start"&&event.requestId==applicationStartId_&&event.inventoryId==applicationStartInventoryId_){event.purpose=NativeInventoryPurpose::Start;latch=&startPhase_;}
 else if(packet.value("purpose")=="close"&&event.requestId==applicationCloseId_&&event.inventoryId==applicationCloseInventoryId_){event.purpose=NativeInventoryPurpose::Close;latch=&closePhase_;}
 else return false; // Boundary has no active UI correlation latch.
 if(!latch->open)return false;
 if(packet.value("phase")=="profile")event.phase=NativeInventoryPhaseKind::Profile;
 else if(packet.value("phase")=="snapshot")event.phase=NativeInventoryPhaseKind::Snapshot;
 else if(packet.value("phase")=="lineage")event.phase=NativeInventoryPhaseKind::Lineage;
 else if(packet.value("phase")=="source_read")event.phase=NativeInventoryPhaseKind::SourceRead;
 else return false;
 if(event.active){
  if(!latch->id.isEmpty()||consumedInventoryPhases_.contains(event.phaseId))return false;
  consumedInventoryPhases_.insert(event.phaseId);latch->id=event.phaseId;latch->phase=event.phase;
 }else{
  if(latch->id!=event.phaseId||latch->phase!=event.phase)return false;
  latch->id.clear();
 }
 const auto callback=onApplicationInventoryPhase;if(callback)callback(event);return true;
}
void NativeBridge::retireApplicationInventoryPhases(NativeInventoryPurpose purpose,const QString& request,const QString& inventory){
 if(QThread::currentThread()!=thread())return;
 if(purpose==NativeInventoryPurpose::Start&&request==applicationStartId_&&inventory==applicationStartInventoryId_)startPhase_={};
 if(purpose==NativeInventoryPurpose::Close&&request==applicationCloseId_&&inventory==applicationCloseInventoryId_)closePhase_={};
}
bool NativeBridge::writeCommand(const QByteArray& bytes){
 if(!runtimeAvailable())return false;
 if(!nativeRuntime_)return relay_.write(bytes)==bytes.size();
 if(bytes.size()>524288)return false;
 QJsonParseError error;const auto document=QJsonDocument::fromJson(bytes,&error);
 if(error.error!=QJsonParseError::NoError||!document.isObject())return false;
 const auto packet=document.object();const auto sink=nativeSend_;const auto lifetime=nativeRuntime_;
 const QPointer<NativeBridge> guard(this);
 return QMetaObject::invokeMethod(lifetime,[guard,lifetime,sink,packet]{
  if(!guard||!lifetime||guard->nativeRuntime_!=lifetime||!guard->nativeUsable_)return;
  try{
   const bool accepted=sink(packet);
   if(guard&&!accepted)guard->relayUnavailable("Native request sink refused; prepared outcome unconfirmed, no automatic retry");
  }catch(const std::exception& e){if(guard)guard->relayUnavailable("Native request sink failed: "+QString::fromUtf8(e.what()).left(250));}
  catch(...){if(guard)guard->relayUnavailable("Native request sink failed; outcome unconfirmed");}
 },Qt::QueuedConnection);
}
void NativeBridge::relayUnavailable(const QString& detail){
 const QPointer<NativeBridge> guard(this);
 const bool closeReading=closePhase_.open;const auto closeRequest=applicationCloseId_,closeInventory=applicationCloseInventoryId_;
 startPhase_={};closePhase_={};
 if(nativeRuntime_)nativeUsable_=false;
 acceptedStartId_.clear();acceptedStartInventoryId_.clear();
 if(!applicationStartId_.isEmpty()){
  const auto id=applicationStartId_,nonce=applicationStartInventoryId_;
  applicationStartId_.clear();applicationStartInventoryId_.clear();acceptedStartId_.clear();acceptedStartInventoryId_.clear();
  if(onApplicationStartInventory)onApplicationStartInventory(id,QJsonObject{{"type","application_start_inventory"},{"request_id",id},{"inventory_id",nonce},{"accepted",false},{"error",detail.left(400)}});
  if(!guard)return;
 }
 if(!applicationCloseId_.isEmpty()&&onApplicationCloseFrozen)
  onApplicationCloseFrozen(applicationCloseId_,false,"Application close is unconfirmed: "+detail);
 if(!guard)return;
 if(closeReading&&!closeRequest.isEmpty()&&!closeInventory.isEmpty()&&onApplicationCloseInventory){
  onApplicationCloseInventory(closeRequest,{{"type","application_close_inventory"},{"request_id",closeRequest},{"inventory_id",closeInventory},{"accepted",false},{"snapshot",QJsonValue::Null},{"error",detail.left(400)}});
  if(!guard)return;
 }
 const auto controls=pending_;pending_.clear();
 const auto dispatches=pendingDispatch_;pendingDispatch_.clear();
 // An interrupted replay is not a completed history. Release its UI batch
 // without inventing records or changing the durable source archive.
 if(historyBatchOpen_){historyBatchOpen_=false;if(onBatchEnd)onBatchEnd();}
 const auto uncertain=detail+" · outcome unconfirmed; reconcile physical state before retrying";
 for(auto it=controls.cbegin();it!=controls.cend();++it)
  if(onControl)onControl(it->device,it.key(),false,uncertain);
 for(const auto& id:dispatches)if(onDispatch)onDispatch(id,false,uncertain);
 if(onStatus)onStatus("RELAY UNAVAILABLE · original measurements/age retained · history replay may be incomplete · "+detail);
}
void NativeBridge::consumeBytes(const QByteArray& bytes){
 incoming_+=bytes;
 int end;while((end=incoming_.indexOf('\n'))>=0){
  const auto line=incoming_.left(end);incoming_.remove(0,end+1);
  if(line.size()>524288){if(onStatus)onStatus("Relay record bound exceeded; history incomplete");continue;}
  QJsonParseError error;auto document=QJsonDocument::fromJson(line,&error);
  if(error.error==QJsonParseError::NoError&&document.isObject())receive(document.object());
  else if(onStatus)onStatus("Relay record invalid; history incomplete");
 }
 if(incoming_.size()>524288){incoming_.clear();if(onStatus)onStatus("Relay record bound exceeded; history incomplete");}
}
void NativeBridge::start(const QString& node,const QString& script,const QString& config,bool indexed,bool residentViewer){
 if(!applicationCloseId_.isEmpty()){if(onStatus)onStatus("Application close is pending; relay launch refused");return;}
 if(nativeRuntime_||relay_.state()!=QProcess::NotRunning){if(onStatus)onStatus("Runtime already attached; launch refused, current mode unchanged");return;}
 observationOnly_=false;
 relay_.setProgram(node);QStringList args{script,config};
 if(indexed)args<<"--indexed-native"<<"--stream-native";
 if(residentViewer)args<<"--resident-viewer";else if(indexed)args<<"--auto-advance";
 relay_.setArguments(args);relay_.start();
}
bool NativeBridge::startNativeObserver(const QString& program,const QStringList& args){
 if(!applicationCloseId_.isEmpty()){if(onStatus)onStatus("Application close is pending; observer launch refused");return false;}
 if(nativeRuntime_||relay_.state()!=QProcess::NotRunning){if(onStatus)onStatus("Runtime already attached; observer launch refused, current mode unchanged");return false;}
 const QFileInfo executable(program);
 if(program.contains(QChar(0))||!executable.isAbsolute()||!executable.isFile()||!executable.isExecutable()){
  if(onStatus)onStatus("Native observer requires an explicit absolute executable; launch refused");return false;
 }
 for(const auto& arg:args)if(arg.contains(QChar(0))){if(onStatus)onStatus("Native observer argument contains NUL; launch refused");return false;}
 observationOnly_=true;incoming_.clear();
 relay_.setProgram(program);relay_.setArguments(args);relay_.start();return true;
}
void NativeBridge::control(const QString& device,const QString& request,bool enabled){
 if(enabled&&!applicationCloseId_.isEmpty()){if(onControl)onControl(device,request,false,"Application is closing; start not submitted");return;}
 if(enabled&&device.compare(uuid,Qt::CaseInsensitive)==0){if(onControl)onControl(device,request,false,"Initial mining start requires native admission; no arbitrary binding launched");return;}
 if(observationOnly_){if(onControl)onControl(device,request,false,"Native observation-only relay has no controller; request not submitted");return;}
 if(device.compare(uuid,Qt::CaseInsensitive)!=0&&device.compare(testUuid,Qt::CaseInsensitive)!=0){if(onControl)onControl(device,request,false,"Selected device has no connected controller");return;}
 if(!runtimeAvailable()){if(onControl)onControl(device,request,false,"Runtime relay is unavailable");return;}
 pending_.insert(request,{device,enabled});
 if(!writeCommand(QJsonDocument(QJsonObject{{"action",enabled?"start":"stop"},{"request_id",request},{"device",device}}).toJson(QJsonDocument::Compact)+'\n')){
  pending_.remove(request);if(onControl)onControl(device,request,false,"Local command channel unavailable; no confirmed control");
 }
}
bool NativeBridge::inspectApplicationStart(const QString& requestId,const QString& inventoryId){
 if(!canonicalRequestId(requestId)||!canonicalRequestId(inventoryId)||!applicationCloseId_.isEmpty()||
    observationOnly_||!runtimeAvailable()||!applicationStartId_.isEmpty())return false;
 acceptedStartId_.clear();acceptedStartInventoryId_.clear();
 applicationStartId_=requestId;applicationStartInventoryId_=inventoryId;
 startPhase_={true,{}};
 const auto message=QJsonDocument(QJsonObject{{"action","application_start_inventory"},{"request_id",requestId},{"inventory_id",inventoryId}}).toJson(QJsonDocument::Compact)+'\n';
 if(!writeCommand(message)){startPhase_={};applicationStartId_.clear();applicationStartInventoryId_.clear();return false;}
 return true;
}
void NativeBridge::cancelApplicationStart(const QString& requestId){
 if(applicationStartId_==requestId){startPhase_={};applicationStartId_.clear();applicationStartInventoryId_.clear();}
 if(acceptedStartId_==requestId){acceptedStartId_.clear();acceptedStartInventoryId_.clear();}
 const auto pending=pending_.constFind(requestId);
 if(pending!=pending_.cend()&&pending->enabled&&pending->device.compare(uuid,Qt::CaseInsensitive)==0)pending_.remove(requestId);
}
bool NativeBridge::controlPreparedInitial(const QString& device,const QString& requestId,const QString& observationId,const QJsonObject& base,const QString& bookingId,const QString& start,const QString& end){
 const auto fail=[&](const QString& error){if(onControl)onControl(device,requestId,false,error);return false;};
 if(device.compare(uuid,Qt::CaseInsensitive)!=0||requestId!=acceptedStartId_||observationId!=acceptedStartInventoryId_||
    !canonicalRequestId(requestId)||!canonicalRequestId(observationId))return fail("Initial start lacks its exact fresh observation");
 if(!applicationCloseId_.isEmpty()||observationOnly_||!runtimeAvailable())return fail("Initial start controller unavailable or closing; prepared intent retained");
 if(base.size()!=4||!base.contains("base_run")||!base.contains("base_birth")||!base.contains("base_start")||!base.contains("base_end"))return fail("Initial base identity incomplete");
 int nulls=0;for(auto it=base.begin();it!=base.end();++it){if(it.value().isNull())++nulls;else if(!it.value().isString()||it.value().toString().isEmpty())return fail("Initial base must be exact strings or explicit nulls");}
 if(nulls!=0&&nulls!=4)return fail("Initial base identity is partially unknown");
 try{
  using namespace pgn::booking;
  if(!canonicalRequestId(bookingId)||start.size()>160||end.size()>160||canonicalUnsigned(start)!=start||canonicalUnsigned(end)!=end||compareUnsigned(start,end)>=0)
   return fail("Prepared booking identity/range invalid");
 }catch(const std::invalid_argument&){return fail("Prepared booking identity/range invalid");}
 const auto bytes=QJsonDocument(QJsonObject{{"action","start_prepared"},{"request_id",requestId},{"device",device},
  {"observation_id",observationId},{"base_run",base.value("base_run")},{"base_birth",base.value("base_birth")},
  {"base_start",base.value("base_start")},{"base_end",base.value("base_end")}}).toJson(QJsonDocument::Compact)+'\n';
 if(bytes.size()>4096)return fail("Initial control identity exceeds frame contract");
 acceptedStartId_.clear();acceptedStartInventoryId_.clear();
 pending_.insert(requestId,{device,true,bookingId,start,end});
 if(!writeCommand(bytes))return fail("Initial control pipe outcome unconfirmed; reconcile prepared identity");
 return true;
}
bool NativeBridge::controlPreparedResume(const QString& device,const QString& requestId,const QString& observationId,const QJsonObject& base,const QString& bookingId,const QString& nextRun,const QString& checkpointSha256){
 const auto fail=[&](const QString& error){if(onControl)onControl(device,requestId,false,error);return false;};
 if(device.compare(uuid,Qt::CaseInsensitive)!=0||requestId!=acceptedStartId_||observationId!=acceptedStartInventoryId_||
    !canonicalRequestId(requestId)||!canonicalRequestId(observationId)||!canonicalRequestId(bookingId))return fail("Resume lacks exact prepared identity/fresh observation");
 if(!applicationCloseId_.isEmpty()||observationOnly_||!runtimeAvailable())return fail("Resume controller unavailable or closing; prepared epoch retained");
 static const QRegularExpression run("^[0-9a-f]{32}$"),sha("^[0-9a-f]{64}$");
 if(base.size()!=4||!base.value("base_run").isString()||!base.value("base_start").isString()||!base.value("base_end").isString()||
    !nativeProcessUtc(base.value("base_birth"))||base.value("base_run").toString().size()!=32||!run.match(base.value("base_run").toString()).hasMatch()||
    nextRun.size()!=32||!run.match(nextRun).hasMatch()||nextRun==base.value("base_run").toString()||checkpointSha256.size()!=64||!sha.match(checkpointSha256).hasMatch())return fail("Resume observed base/new epoch/checkpoint identity invalid");
 const auto start=base.value("base_start").toString(),end=base.value("base_end").toString();
 try{using namespace pgn::booking;if(start.size()>160||end.size()>160||canonicalUnsigned(start)!=start||canonicalUnsigned(end)!=end||compareUnsigned(start,end)>=0)return fail("Resume original range invalid");}
 catch(const std::invalid_argument&){return fail("Resume original range invalid");}
 const auto bytes=QJsonDocument(QJsonObject{{"action","resume_prepared"},{"device",device},{"request_id",requestId},{"observation_id",observationId},
  {"booking_id",bookingId},{"old_run",base.value("base_run")},{"new_run",nextRun},{"base_birth",base.value("base_birth")},{"base_start",start},{"base_end",end}}).toJson(QJsonDocument::Compact)+'\n';
 if(bytes.size()>4096)return fail("Resume control identity exceeds frame contract");
 acceptedStartId_.clear();acceptedStartInventoryId_.clear();
 pending_.insert(requestId,{device,true,bookingId,start,end,nextRun,base.value("base_run").toString(),checkpointSha256});
 if(!writeCommand(bytes))return fail("Resume control pipe outcome unconfirmed; reconcile prepared epoch");
 return true;
}
void NativeBridge::dispatch(const QString& bookingId){
 if(!applicationCloseId_.isEmpty()){if(onDispatch)onDispatch(bookingId,false,"Application is closing; no new work scheduled");return;}
 if(observationOnly_){if(onDispatch)onDispatch(bookingId,false,"Native observation-only relay cannot dispatch; booking unchanged");return;}
 if(!runtimeAvailable()){if(onDispatch)onDispatch(bookingId,false,"Runtime relay unavailable; no work scheduled");return;}
 pendingDispatch_.insert(bookingId);
 if(!writeCommand(QJsonDocument(QJsonObject{{"action","dispatch"},{"booking_id",bookingId}}).toJson(QJsonDocument::Compact)+'\n')){
  pendingDispatch_.remove(bookingId);if(onDispatch)onDispatch(bookingId,false,"Local command channel unavailable; no work confirmed");
 }
}
void NativeBridge::moduleAction(const QJsonObject& command){
 if(!applicationCloseId_.isEmpty()&&!QStringList{"module_list","module_receipt"}.contains(command.value("action").toString())){
  if(onModuleRegistry)onModuleRegistry(QJsonObject{{"type","module_registry"},{"request_id",command.value("request_id")},{"error","Application is closing; module mutation not submitted"},{"applied",false}});return;
 }
 if(observationOnly_){if(onModuleRegistry)onModuleRegistry(QJsonObject{{"type","module_registry"},{"request_id",command.value("request_id")},{"error","Native observation-only relay has no module command service; request not submitted"},{"applied",false}});return;}
 const auto action=command.value("action").toString();
 if(!QStringList{"module_list","module_register","module_select","module_receipt"}.contains(action))return;
 if(!runtimeAvailable()){if(onModuleRegistry)onModuleRegistry(QJsonObject{{"type","module_registry"},{"request_id",command.value("request_id")},{"error","Module catalogue service unavailable; current source unchanged"},{"applied",false}});return;}
 const auto bytes=QJsonDocument(command).toJson(QJsonDocument::Compact);
 if(bytes.size()>65536){if(onModuleRegistry)onModuleRegistry(QJsonObject{{"type","module_registry"},{"request_id",command.value("request_id")},{"error","Module command oversized; no truncated submission"},{"applied",false}});return;}
 if(!writeCommand(bytes+'\n')&&onStatus)onStatus("Local module command unavailable; no change confirmed");
}
bool NativeBridge::prepareApplicationClose(const QString& requestId){
 const auto fail=[&](const QString& reason){if(onApplicationCloseFrozen)onApplicationCloseFrozen(requestId,false,reason);return false;};
 const auto parsed=QUuid::fromString(requestId);
 if(parsed.isNull()||parsed.toString(QUuid::WithoutBraces)!=requestId)return fail("Canonical non-null application-close UUID required");
 if(!applicationCloseId_.isEmpty()&&applicationCloseId_!=requestId)return fail("Another application close is already pending");
 if(observationOnly_)return fail("Observation-only relay cannot freeze a mining controller");
 if(!runtimeAvailable())return fail("Runtime controller unavailable; close is unconfirmed");
 applicationCloseId_=requestId; // Freeze local admission before crossing the pipe.
 startPhase_={};closePhase_={};
 applicationStartId_.clear();applicationStartInventoryId_.clear();acceptedStartId_.clear();acceptedStartInventoryId_.clear();
 const auto message=QJsonDocument(QJsonObject{{"action","application_close_prepare"},{"request_id",requestId}}).toJson(QJsonDocument::Compact)+'\n';
 if(!writeCommand(message))return fail("Application-close request pipe write is unconfirmed");
 return true;
}
bool NativeBridge::inspectApplicationClose(const QString& requestId,const QString& inventoryId){
 if(requestId.isEmpty()||requestId!=applicationCloseId_||observationOnly_||!runtimeAvailable())return false;
 const auto parsed=QUuid::fromString(inventoryId);
 if(parsed.isNull()||parsed.toString(QUuid::WithoutBraces)!=inventoryId)return false;
 applicationCloseInventoryId_=inventoryId;
 closePhase_={true,{}};
 const auto message=QJsonDocument(QJsonObject{{"action","application_close_inventory"},{"request_id",requestId},{"inventory_id",inventoryId}}).toJson(QJsonDocument::Compact)+'\n';
 if(!writeCommand(message)){closePhase_={};return false;}return true;
}
void NativeBridge::publishResources(const QJsonObject& packet,std::optional<qint64> resourceReceiptNs){
 const auto observed=QDateTime::fromString(packet.value("observed_utc").toString(),Qt::ISODateWithMs).toUTC();
 if(!observed.isValid())return;
 FrontierResourceTelemetrySnapshot resource;resource.valid=packet.value("driver_valid").toBool();resource.observedUtc=observed;resource.sequence=observed.toMSecsSinceEpoch();
 resource.receivedMonotonicNs=resourceReceiptNs;
 const auto age=resourceReceiptAgeMs(resource);resource.stale=!age||*age>15000;
 for(const auto value:packet.value("gpus").toArray()){
   auto fields=value.toString().split(',');if(fields.size()!=11&&fields.size()!=12&&fields.size()!=14&&fields.size()!=15)continue;for(auto& f:fields)f=f.trimmed();
   const bool memoryClock=fields.size()==12||fields.size()==15;
  FrontierResourceDevice d;d.kind="GPU";d.deviceId=fields[1];d.name=fields[2];
  const auto number=[](const QString& s)->std::optional<double>{bool ok=false;double n=s.toDouble(&ok);return ok&&std::isfinite(n)?std::optional<double>(n):std::nullopt;};
  d.utilizationPercent=number(fields[4]);
  if(const auto n=number(fields[5]))d.memoryUsedBytes=QString::number(static_cast<qulonglong>(*n*1048576));
  if(const auto n=number(fields[6]))d.memoryTotalBytes=QString::number(static_cast<qulonglong>(*n*1048576));
   d.powerWatts=number(fields[7]);d.powerLimitWatts=number(fields[8]);d.temperatureC=number(fields[9]);d.smClockMhz=number(fields[10]);if(memoryClock)d.memoryClockMhz=number(fields[11]);resource.devices.append(d);
   if(fields.size()==14||fields.size()==15){
   auto& actual=resource.devices.last();actual.throttleClass="NONE";bool allKnown=true,activeKnown=false;
   const QStringList names{"SW_THERMAL","SW_POWER_CAP","HW_SLOWDOWN"};
   const QStringList classes{"THERMAL","POWER/CURRENT","HARDWARE"};
    for(int i=0;i<3;++i){const QString status=fields[(memoryClock?12:11)+i];
    actual.throttleReasons.append(names[i]+":"+status);
    if(status=="Active"){activeKnown=true;if(actual.throttleClass=="NONE")actual.throttleClass=classes[i];}
    else if(status!="Not Active")allKnown=false;
   }
   // An unavailable secondary reason cannot conceal a proved active limit.
   // NONE is authoritative only when every queried reason is known inactive.
   actual.throttleTelemetryAuthoritative=allKnown||activeKnown;
   actual.throttleDetail=actual.throttleReasons.join(" · ");
   if(!allKnown)actual.throttleDetail+=" · PARTIAL REASON INVENTORY";
  }
 }
 // A missing driver sample is absence, not an empty device inventory. Keep
 // the previous measurements and their original age; do not invent freshness.
 if(resource.valid&&!resource.devices.isEmpty()&&onResources)onResources(resource);
}
void NativeBridge::receive(const QJsonObject& packet,std::optional<qint64> resourceReceiptNs){
 const auto type=packet.value("type").toString();
 if(type=="native_hardware_availability")return; // Presentation notice ONLY, never telemetry/control/source authority.
 if(type=="resource_snapshot"){publishResources(packet,resourceReceiptNs);return;} // Never source/control/proof acknowledgement.
 if(type=="application_inventory_phase")return; // Node/observer data can NEVER acquire this local capability.
 if(type=="application_start_inventory"){
  const auto id=packet.value("request_id").toString();
  if(observationOnly_||applicationStartId_.isEmpty()||id!=applicationStartId_||packet.value("inventory_id")!=applicationStartInventoryId_)return;
  startPhase_={}; // Close BEFORE arbitrary terminal callbacks/publication.
  if(packet.value("accepted").isBool()&&packet.value("accepted").toBool()&&packet.value("snapshot").isObject()){
   acceptedStartId_=id;acceptedStartInventoryId_=applicationStartInventoryId_;
  }
  applicationStartId_.clear();applicationStartInventoryId_.clear();
  if(onApplicationStartInventory)onApplicationStartInventory(id,packet);
  return; // Never telemetry, allocation or physical control acknowledgement.
 }
 if(type=="application_close_frozen"||type=="application_close_inventory"){
  const auto id=packet.value("request_id").toString();
  if(applicationCloseId_.isEmpty()||id!=applicationCloseId_||observationOnly_)return;
  if(type=="application_close_frozen"){
   const bool accepted=packet.value("accepted").isBool()&&packet.value("accepted").toBool();
   if(onApplicationCloseFrozen)onApplicationCloseFrozen(id,accepted,packet.value("error").toString().left(400));
  }else if(!applicationCloseInventoryId_.isEmpty()&&packet.value("inventory_id")==applicationCloseInventoryId_){
   closePhase_={};if(onApplicationCloseInventory)onApplicationCloseInventory(id,packet);
  }
  return; // No physical ACK, device update, checkpoint commit or automatic quit.
 }
 // Observation traffic cannot acquire a controller capability or acknowledge
 // commands refused locally, even if it contains an unsolicited reply type.
 if(observationOnly_&&(type=="control"||type=="dispatch"))return;
 if(type=="device_control_snapshot"){
  const auto sample=packet.value("snapshot").toObject();
  const auto epoch=sample.value("manifest").toObject();
  const auto observed=QDateTime::fromString(sample.value("observed_utc").toString(),Qt::ISODateWithMs);
  if(sample.value("schema")!="pgn-device-control-observation-1"||sample.value("device")!=testUuid||
   sample.value("scope")!="BOUNDED_SCIENCE_TEST"||!sample.value("owned").isBool()||!observed.isValid())return;
  const bool owned=sample.value("owned").toBool();
  if(owned&&(epoch.value("uuid")!=testUuid||epoch.value("pid").toInteger()<=0||
   epoch.value("image_sha256")!="A73A6E383308560EDC36CE351B09EB449FA099275361BE74BE89020B03864B43"))return;
  FrontierTelemetrySnapshot lane;lane.engineId="pgn-bounded-cuda-test";lane.gpuUuid=testUuid;
  lane.gpuName="RTX 3060 · BOUNDED TEST";lane.runId=epoch.value("run").toString();
  lane.observedUtc=observed;lane.authoritative=true;lane.stale=false;lane.terminalSealed=!owned;
  lane.state=owned?"running":"stopped";lane.producerStatus=lane.state;
  lane.raw.insert("pgn_process_running",owned);lane.raw.insert("pgn_test_only",true);
  lane.raw.insert("legacy_miner_bound",owned);
  lane.raw.insert("control_scope","BOUNDED_TEST_NO_NETWORK_CREDIT");
  if(onTestDevice)onTestDevice(lane);
  struct Completed {QString id;Pending request;bool applied;QString detail;};QList<Completed> completed;
  const auto exit=sample.value("last_exit").toObject();
  const bool finishedEpoch=!owned&&!sample.value("desired_enabled").toBool()&&
   epoch.value("uuid")==testUuid&&epoch.value("pid").toInteger()>0&&
   epoch.value("image_sha256")=="A73A6E383308560EDC36CE351B09EB449FA099275361BE74BE89020B03864B43"&&
   !epoch.value("run").toString().isEmpty()&&exit.value("run")==epoch.value("run")&&
   exit.value("exit_code").isDouble();
  for(auto it=pending_.begin();it!=pending_.end();){
   const bool sameEpochRequest=epoch.value("request_id").toString()==it.key();
   const bool matches=it->device.compare(testUuid,Qt::CaseInsensitive)==0&&
    (it->enabled?(sameEpochRequest&&((owned&&!sample.value("stop_requested").toBool())||finishedEpoch)):(!owned&&!sample.value("desired_enabled").toBool()));
   if(matches){
    const bool applied=!it->enabled||!finishedEpoch||exit.value("exit_code").toDouble()==0.;
    const QString detail=owned?"Exact3060 bounded-test process observed; no public credit":
     it->enabled?(applied?"3060 bounded test already finished; matching process exit confirmed":"3060 bounded test failed; matching process exit confirmed"):
     "3060 test process is not running";
    completed.append({it.key(),it.value(),applied,detail});it=pending_.erase(it);
   }else ++it;
  }
  for(const auto& c:completed)if(onControl)onControl(c.request.device,c.id,c.applied,c.detail);
  return;
 }
 if(type=="dispatch"){const auto id=packet.value("booking_id").toString();pendingDispatch_.remove(id);if(onDispatch)onDispatch(id,packet.value("accepted").toBool(),packet.value("accepted").toBool()?packet.value("detail").toString():packet.value("error").toString());return;}
 if(type=="native_index"){if(onNativeIndex)onNativeIndex(packet);return;}
 if(type=="module_registry"){
  if(onModuleRegistry)onModuleRegistry(packet);return; // Never source counters or a device effect.
 }
 if(type=="module_capabilities"){
  const auto observed=packet.value("observed_utc").toString(),artifact=packet.value("artifact_sha256").toString();
  const auto declaration=packet.value("capabilities").toObject();
  if(packet.value("evidence_scope")!="PRODUCER_DECLARATION_ONLY"||packet.value("gpu_control_calls").toInt(-1)!=0||
   !QDateTime::fromString(observed,Qt::ISODateWithMs).isValid()||artifact.size()!=64||declaration.value("schema")!="pgn-engine-module-1")return;
  if(onModuleCapabilities)onModuleCapabilities(declaration,observed,artifact);return;
 }
 if(type=="source_completion"){if(onSourceCompletion)onSourceCompletion(packet.value("receipt").toObject(),packet.value("community_updated").toBool());return;}
 if(type=="history_begin"){if(!historyBatchOpen_){historyBatchOpen_=true;if(onBatchBegin)onBatchBegin();}return;}
 if(type=="history_end"){if(historyBatchOpen_){historyBatchOpen_=false;if(onBatchEnd)onBatchEnd();}return;}
 if(type=="history"){
  for(const auto value:packet.value("rows").toArray()){
   const auto row=value.toObject();FrontierGapEvent event;
   event.runId=row.value("run").toString();event.gpuUuid=uuid;event.domainId="pgn-durable-native-history";
   event.sequence=row.value("id").toInteger();event.endpointLeft=row.value("p").toString();event.endpointRight=row.value("q").toString();
   event.gap=row.value("gap").toString();event.merit=row.value("merit").toString();event.meritDecimal=event.merit;
   if(row.value("unix_ts").isDouble())event.observedUtc=QDateTime::fromSecsSinceEpoch(row.value("unix_ts").toInteger(),QTimeZone::utc());
   event.candidateId=event.endpointLeft+":"+event.endpointRight;event.proofState="prp_pending";
   event.epistemicStatus="DURABLE_SOURCE_GMP_PRP_ASSERTION_INDEPENDENT_PROOF_PENDING";
   if(onGap)onGap(event);
  }
  return; // Historical data cannot renew a device card or process state.
 }
 if(type=="status"){
  const auto detail=packet.value("detail").toString();
  if(onStatus&&!detail.isEmpty())onStatus(detail.left(400));
  return; // Informational only: never a device sample or physical control ACK.
 }
 if(type=="unavailable"){if(onStatus)onStatus("TELEMETRY UNAVAILABLE · values unchanged · "+packet.value("error").toString());return;}
 if(type=="control"){
  const QString id=packet.value("request_id").toString();const auto result=packet.value("result").toObject();
  if(result.isEmpty()&&pending_.contains(id)){auto p=pending_.take(id);if(onControl)onControl(p.device,id,false,packet.value("error").toString());}
  return; // A request receipt is not process acknowledgement.
 }
 if(type=="native_process_observation"){
  // ONLY the local native creating-thread channel, never a relay/observer.
  // Match the exact manifest previously admitted by SOURCE; a physical read
  // cannot adopt a new run or publish a byte, counter, proof or close credit.
  if(!nativeRuntime_||!nativeUsable_||observationOnly_)return;
  const auto manifest=packet.value("manifest").toObject();
  const auto exit=packet.value("last_exit").toObject();
  if(packet.value("schema")!="pgn-runtime-snapshot-1"||packet.value("owned")!=false||packet.value("state")!="OFF"||
     !packet.value("operator_stop").isBool()||!nativeProcessUtc(packet.value("observed_utc"))||
     manifest.isEmpty()||manifest!=lane_.raw.value("pgn_native_manifest").toObject()||
     manifest.value("schema")!="pgn-native-process-1"||manifest.value("uuid")!=uuid||manifest.value("run")!=lane_.runId||
     !manifest.value("pid").isDouble()||manifest.value("pid").toInteger()<=0||
     manifest.value("pid").toDouble()!=double(manifest.value("pid").toInteger())||!nativeProcessUtc(manifest.value("birth"))||
     packet.value("binding").toObject().value("uuid")!=uuid||exit.value("run")!=manifest.value("run")||
     !exit.value("exit_code").isDouble()||exit.value("exit_code").toDouble()!=0)return;
  lane_.raw.insert("pgn_process_running",false);lane_.state="stopped";lane_.terminalSealed=true;
  QList<std::pair<QString,Pending>> completed;
  if(packet.value("operator_stop")==true)for(auto it=pending_.begin();it!=pending_.end();){
   if(it->device==uuid&&!it->enabled){completed.append({it.key(),it.value()});it=pending_.erase(it);}else ++it;
  }
  const QPointer<NativeBridge> guard(this);
  if(onDevice)onDevice(lane_);if(!guard)return;
  for(const auto& entry:completed){if(onControl)onControl(entry.second.device,entry.first,true,"Exact native process stopped; SOURCE counters unchanged");if(!guard)return;}
  if(onStatus)onStatus("WIZZ · OFF · source "+lane_.runId.left(8)+" · last SOURCE counters retained");
  return;
 }
 if(type=="native_control_observation"){
  // Local native capability ONLY. Legacy/observer packets cannot mint this
  // correlation. An explicit empty OFF+persisted STOP is a physical profile
  // observation, never a command ACK, fake GPU utilization or source interval.
  if(!nativeRuntime_||!nativeUsable_||observationOnly_)return;
  const auto id=packet.value("request_id").toString();const auto it=pending_.find(id);
  if(it==pending_.end()||it->enabled||it->device!=uuid||packet.value("device")!=it->device)return;
  const auto snapshot=packet.value("snapshot").toObject();
  if(snapshot.value("schema")!="pgn-runtime-snapshot-1"||snapshot.value("owned")!=false||snapshot.value("state")!="OFF"||
     snapshot.value("operator_stop")!=true||!snapshot.value("manifest").isNull()||
     snapshot.value("binding").toObject().value("uuid")!=uuid||!nativeProcessUtc(snapshot.value("observed_utc")))return;
  const auto request=it.value();pending_.erase(it);
  if(onControl)onControl(request.device,id,true,"PGN profile OFF observed with persisted STOP; no new command or GPU-idle claim");
  return;
 }
 if(type!="snapshot")return;
 const auto manifest=packet.value("manifest").toObject();const QString run=manifest.value("run").toString();
 if(run.isEmpty()){if(onStatus)onStatus("SOURCE IDENTITY MISSING · device values unchanged");return;}
 const auto now=QDateTime::fromString(packet.value("observed_utc").toString(),Qt::ISODateWithMs);
 const auto receiptNs=receiptMonotonicNs();
 const bool runChanged=lane_.runId!=run;
 if(runChanged){lane_={};lane_.runId=run;metrics_.clear();lastCounters_={};sequence_=0;lastCounterClock_=0;}
 lane_.gpuUuid=uuid;lane_.gpuName="NVIDIA GeForce RTX 3090";lane_.sourceMode="live";
 lane_.engineId="pgn-native-walk";lane_.engineProfile="GPU WALK";lane_.producerStatus="SOURCE PRP";
 lane_.verifierStatus="INDEPENDENT PROOF PENDING";
 const auto rangeStart=manifest.value("start").toString(),rangeEnd=manifest.value("end").toString();
 if(runChanged||lane_.domainStart!=rangeStart||lane_.domainEnd!=rangeEnd)lane_.numberBits=homogeneousRangeBits(rangeStart,rangeEnd);
 lane_.domainStart=rangeStart;lane_.domainEnd=rangeEnd;
 const QString state=packet.value("state").toString();const bool running=state=="RUNNING"||state=="STOPPING";
 const bool physicalChanged=runChanged||lane_.raw.value("pgn_process_running").toBool()!=running;
 bool sourcePublished=false;
 lane_.raw.insert("pgn_process_running",running);
 lane_.raw.insert("pgn_native_manifest",manifest);
  const auto nativeProfile=manifest.value("source_profile").toObject();const auto nativeJob=nativeProfile.value("job").toObject();
  const bool adaptive=nativeProfile.value("schema")=="pgn-run-source-profile-2"&&nativeProfile.value("protocol")=="PGN_NATIVE_GAP_RUNTIME_V1";
  bool minimumOk=false;const auto minimum=nativeJob.value("minimum_gap").toString().toUInt(&minimumOk);const auto descriptorK=nativeJob.value("profile_k").toInt();
  if(adaptive&&nativeJob.value("start")==rangeStart&&nativeJob.value("end")==rangeEnd&&minimumOk&&minimum>=primeGapMinimumFloor()&&minimum<=2147483647U
     &&QString::number(minimum)==nativeJob.value("minimum_gap").toString()&&(descriptorK==8||descriptorK==16||descriptorK==32||descriptorK==64)){
   lane_.raw.insert("pgn_native_min_gap",QString::number(minimum));lane_.raw.insert("pgn_native_profile_k",descriptorK);
  }else{lane_.raw.remove("pgn_native_min_gap");lane_.raw.remove("pgn_native_profile_k");}
 const auto birth=QDateTime::fromString(manifest.value("birth").toString(),Qt::ISODateWithMs);
 if(birth.isValid())lane_.raw.insert("pgn_run_started_utc",birth.toUTC().toString(Qt::ISODateWithMs));
 lane_.state=running?"running":"stopped";lane_.authoritative=!run.isEmpty();lane_.terminalSealed=!running;
  static const QRegularExpression counter("^\\[pgn-counters\\] time_ns=(\\d+) positions=(\\d+) gaps=(\\d+) tests=(\\d+) batches=(\\d+)(?: processed_gaps=(\\d+))?(?: emitted_gaps=(\\d+))?$");
 static const QRegularExpression gap("^\\[phase0-gpu\\] GAP merit=([\\d.]+) gap=(\\d+) lower=(\\d+) upper=(\\d+) verified=1(?: |$)");
 if(onBatchBegin)onBatchBegin();
 for(const auto value:packet.value("lines").toArray()){
  const QString line=value.toString();auto match=counter.match(line);
  if(match.hasMatch()){
    if((adaptive&&match.captured(7).isEmpty())||(!match.captured(7).isEmpty()&&(match.captured(7)!=match.captured(3)||match.captured(6)!=match.captured(3))))continue;
   const auto sourceClock=match.captured(1).toULongLong();
   // A bounded live tail overlaps previous packets. Duplicates/older replay
   // are not new zero intervals and must not reset the accepted counter base.
   if(sourceClock<=lastCounterClock_)continue;
   lastCounterClock_=sourceClock;
   CounterSample sample{run.toStdString(),uuid.toStdString(),static_cast<std::uint64_t>(++sequence_),
    std::chrono::nanoseconds(match.captured(1).toULongLong()),match.captured(2).toULongLong(),match.captured(3).toULongLong(),match.captured(4).toULongLong()};
   if (!match.captured(6).isEmpty()) sample.processedGaps = match.captured(6).toULongLong();
   const auto result=metrics_.accept(sample);
   bool clockOk=false;const auto producerClock=packet.value("source_clock_ns").toString().toULongLong(&clockOk);
   const auto counterClock=static_cast<std::uint64_t>(sample.sourceTime.count());
   if(clockOk && producerClock>=counterClock && producerClock-counterClock<86400ULL*1000000000ULL)
    lastCounters_=now.addMSecs(-static_cast<qint64>((producerClock-counterClock)/1000000ULL));
   else lastCounters_={};
   if(clockOk)stampSourceAge(lane_.raw,producerClock,counterClock,receiptNs);
   else{lane_.raw.remove("pgn_age_at_receipt_ns");lane_.raw.remove("pgn_receipt_monotonic_ns");}
   lane_.observedUtc=lastCounters_;lane_.sequence=sequence_;
   lane_.rowsCompleted=match.captured(2);lane_.cumulativeGapCandidates=match.captured(3);lane_.gapCandidates=match.captured(3);
   lane_.raw.insert("pgn_source_interval_ns",result.interval?QString::number(result.interval->duration.count()):QString{});
   lane_.raw.insert("pgn_source_time_ns",QString::number(sourceClock));
   // Source timestamps belong to this producer run. A UI process, UTC label,
   // or different run must never manufacture a shared aggregation clock.
   lane_.raw.insert("pgn_clock_namespace",QStringLiteral("native-source-clock/")+run);
   if(result.interval){lane_.e2ePositionsPerSecond=result.interval->positionsPerSecond;lane_.powmPerSecond=result.interval->testsPerSecond;
    lane_.gapPairsTestedPerSecond = result.interval->retainedGapsPerSecond;
    lane_.raw.insert("legacy_estimated_pps",result.interval->positionsPerSecond);
    lane_.raw.insert("pgn_retained_gaps_per_second",result.interval->retainedGapsPerSecond);
    lane_.raw.insert("pgn_cumulative_tests",QString::number(sample.primalityTests));
    if(sample.processedGaps)lane_.raw.insert("pgn_cumulative_processed_gaps",QString::number(*sample.processedGaps));
    else lane_.raw.remove("pgn_cumulative_processed_gaps");
    if(result.interval->processedGapsPerSecond)lane_.raw.insert("pgn_processed_gaps_per_second",*result.interval->processedGapsPerSecond);
    else lane_.raw.remove("pgn_processed_gaps_per_second");
    if(result.interval->retainedGapsPerBillionPositions)lane_.raw.insert("pgn_retained_gaps_per_billion_positions",*result.interval->retainedGapsPerBillionPositions);
    else lane_.raw.remove("pgn_retained_gaps_per_billion_positions");
    if(result.interval->retainedFraction)lane_.raw.insert("pgn_retained_fraction",*result.interval->retainedFraction);
    else lane_.raw.remove("pgn_retained_fraction");
    // Preserve every admitted raw producer interval, including true zero
    // advances between batch commits; never collapse them to a relay average.
    lane_.stale=!running||!lastCounters_.isValid()||lastCounters_.msecsTo(now)>15000;
    lane_.raw.insert("legacy_miner_bound",running);
    if(onDevice)onDevice(lane_);sourcePublished=true;
   }
  } else if((match=gap.match(line)).hasMatch()){
   FrontierGapEvent event;event.runId=run;event.gpuUuid=uuid;event.domainId="pgn-live-source";event.sequence=++sequence_;
   static const QRegularExpression sourceTime("(?:^| )unix_ts=(\\d+)(?: |$)");
   const auto timestamp=sourceTime.match(line);
   if(timestamp.hasMatch())event.observedUtc=QDateTime::fromSecsSinceEpoch(timestamp.captured(1).toLongLong(),QTimeZone::utc());
   event.gap=match.captured(2);event.merit=match.captured(1);event.meritDecimal=event.merit;event.endpointLeft=match.captured(3);event.endpointRight=match.captured(4);
   event.candidateId=event.endpointLeft+":"+event.endpointRight;event.proofState="prp_pending";event.epistemicStatus="SOURCE_GMP_PRP_ASSERTION_INDEPENDENT_PROOF_PENDING";
   if(onGap)onGap(event);
  }
 }
 if(onBatchEnd)onBatchEnd();
 lane_.stale=!running||!lastCounters_.isValid()||lastCounters_.msecsTo(now)>15000;
 lane_.raw.insert("legacy_miner_bound",running);
 publishResources(packet,resourceReceiptNs);
 // Physical control and producer interval freshness remain independent.
 if(!sourcePublished&&physicalChanged&&onDevice)onDevice(lane_);
 struct Completed {QString id;Pending request;};QList<Completed> completed;
 for(auto it=pending_.begin();it!=pending_.end();){
  const auto binding=packet.value("binding").toObject(),initial=binding.value("initial_start").toObject(),resume=binding.value("resume_start").toObject();
  static const QRegularExpression nativeRun("^[0-9a-f]{32}$");
  const bool ownedEpoch=packet.value("schema")=="pgn-runtime-snapshot-1"&&manifest.value("schema")=="pgn-native-process-1"&&
   manifest.value("uuid").isString()&&manifest.value("uuid").toString().compare(uuid,Qt::CaseInsensitive)==0&&
   binding.value("uuid").isString()&&binding.value("uuid").toString().compare(uuid,Qt::CaseInsensitive)==0&&
   manifest.value("run").isString()&&manifest.value("run").toString().size()==32&&nativeRun.match(manifest.value("run").toString()).hasMatch()&&
   nativeProcessUtc(manifest.value("birth"))&&manifest.value("pid").isDouble()&&manifest.value("pid").toInteger()>0&&
   manifest.value("pid").toInteger()<=4294967295LL&&manifest.value("pid").toDouble()==double(manifest.value("pid").toInteger())&&
   binding.value("start")==manifest.value("start")&&binding.value("end")==manifest.value("end");
  const bool preparedBase=ownedEpoch&&manifest.value("queued_intent")==it->bookingId&&binding.value("queued_intent")==it->bookingId&&manifest.value("start")==it->start&&manifest.value("end")==it->end;
  const bool preparedIdentity=it->bookingId.isEmpty()||(preparedBase&&
   (it->resumeRun.isEmpty()?(initial.value("origin")=="INITIAL_START"&&initial.value("request_id")==it.key()):
    (manifest.value("run")==it->resumeRun&&resume.value("schema")=="pgn-checkpoint-resume-marker-1"&&resume.value("run")==it->resumeRun&&
     resume.value("previous_run")==it->previousRun&&resume.value("request_id")==it.key()&&resume.value("checkpoint_sha256")==it->checkpointSha256&&
     manifest.value("resume_previous_run")==it->previousRun&&manifest.value("checkpoint_input_sha256")==it->checkpointSha256)));
  if(it->device.compare(uuid,Qt::CaseInsensitive)==0&&packet.value("owned").isBool()&&
     ((it->enabled&&packet.value("owned").toBool()&&state=="RUNNING"&&preparedIdentity)||(!it->enabled&&!running))){
  completed.append({it.key(),it.value()});it=pending_.erase(it);
 }else ++it;}
 // Callbacks may submit the next command; never mutate a live QHash iterator.
 for(const auto& c:completed)if(onControl)onControl(c.request.device,c.id,true,running?"Exact native process observed":"Native process stopped");
 if(onStatus){
  const auto lineage=packet.value("resume_lineage").toObject();
  if(!running&&!lineage.isEmpty()&&lineage.value("ready").isBool()&&!lineage.value("ready").toBool())onStatus("RESUMED SOURCE HELD · "+lineage.value("error").toString().left(300));
  else onStatus(QStringLiteral("WIZZ · %1 · source %2 · pending bytes %3").arg(state,run.left(8)).arg(packet.value("pending_bytes").toInteger()));
 }
 if(onSourceSnapshot)onSourceSnapshot(packet);
}
}
