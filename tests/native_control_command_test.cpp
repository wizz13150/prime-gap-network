#include "mining/native_control_command.h"
#include "booking/decimal_range.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <sqlite3.h>
#include <iostream>
#include <memory>

using namespace pgn::mining;using namespace pgn::booking;
namespace {
int checks{};
const QString Device="GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac",Old(32,'a'),Next(32,'b'),Wrong(32,'c');
const QString Request="11111111-2222-4333-8444-555555555555",ResumeRequest="aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee",Observation="12345678-1234-4567-89ab-123456789abc";
const QString Start="184467440737095516260",Length="1000000",End=addUnsigned(Start,Length),Birth="2026-10-15T00:00:00.0000000Z";
void check(bool value,const char* what){++checks;if(!value)throw std::runtime_error(what);}
template<class F>void refused(F f,NativeControlErrorCode code=NativeControlErrorCode::InvalidContract){bool caught=false;try{f();}catch(const NativeControlError& e){caught=e.code()==code;}catch(const std::exception&){caught=code==NativeControlErrorCode::InvalidContract;}check(caught,"invalid command must refuse explicitly");}
QByteArray json(const QJsonObject& object){return QJsonDocument(object).toJson(QJsonDocument::Compact);}
struct Fixture {
 QTemporaryDir temp;QString path;BookingStore store;sqlite3* db{};SshObservationEndpoint endpoint;
 InitialStartResult initial;QByteArray receipt;QString booking;StoredSourceResume saved;
 Fixture():path(temp.filePath("native.sqlite")),store(path){
  check(temp.isValid()&&store.valid(),"own temp real native SQLite");
  const auto key=temp.filePath("PUBLIC-NOT-A-KEY-' unicode.txt");QFile out(key);check(out.open(QIODevice::WriteOnly),"own public key-path stand-in");out.write("PUBLIC TEST DATA NOT A KEY");out.close();
  endpoint={QCoreApplication::applicationFilePath(),key,"127.0.0.1","fixture_user",22};
  SourceRegistry registry;registry.url="https://PUBLIC-FIXTURE.invalid/ranges";registry.sha256=QString(64,'1');registry.observedUtc=QDateTime::fromString("2026-10-15T00:00:00Z",Qt::ISODate);registry.ranges={{1000,Start,End,"Open",{}}};
  check(store.queue(registry,Start,End,Device).isEmpty(),"native reservation, no worker");
  initial=store.prepareInitialStart(Request,Device,{Start,End,Start,End,Start,Length},registry,{WorkBoundary::Ready,Observation});
  check(initial.status==InitialStartStatus::Prepared&&initial.dispatch,"native newly PREPARED initial");booking=initial.dispatch->bookingId;
  check(sqlite3_open_v2(path.toUtf8().constData(),&db,SQLITE_OPEN_READWRITE,nullptr)==SQLITE_OK,"own fixture SQLite readback");
  sqlite3_stmt* q{};check(sqlite3_prepare_v2(db,"SELECT value FROM booking_metadata WHERE key=?",-1,&q,nullptr)==SQLITE_OK,"receipt query");const auto name=("mining-initial-start/v1/"+Request).toUtf8();sqlite3_bind_text(q,1,name.constData(),name.size(),SQLITE_TRANSIENT);
  check(sqlite3_step(q)==SQLITE_ROW,"receipt native storage exists");receipt=QByteArray(reinterpret_cast<const char*>(sqlite3_column_blob(q,0)),sqlite3_column_bytes(q,0));check(sqlite3_step(q)==SQLITE_DONE,"unique receipt");sqlite3_finalize(q);
 }
 ~Fixture(){if(db)sqlite3_close(db);}
 void sql(const QString& statement){check(sqlite3_exec(db,statement.toUtf8().constData(),nullptr,nullptr,nullptr)==SQLITE_OK,"own fixture transition");}
 std::pair<CheckpointResumeAdmission,CheckpointResumeEpochRead> resume(){
  sql("UPDATE dispatch_requests SET state='STARTED',run='"+Old+"' WHERE id='"+booking+"';UPDATE bookings SET state='DISPATCHED' WHERE id='"+booking+"';");
  check(store.observeRuntime(Old,Start,End,Device,false).isEmpty(),"synthetic old PAUSED observation, not physical proof");
  auto& bytes=saved.checkpointBytes;bytes="PGNCG001";for(const auto word:{quint64(100),quint64(10),quint64(1000000),quint64(123),quint64(9007199254741995ULL),quint64(41),quint64(17)})for(int i=0;i<8;++i)bytes.append(char((word>>(8*i))&255));
  auto& r=saved.resume;r.run=Old;r.start=Start;r.end=End;r.length=Length;r.positions="123";r.nextStart=addUnsigned(Start,r.positions);r.remaining="999877";r.tests="9007199254741995";r.gaps="41";r.batches="17";r.counterTimeNs="9007199254742995";r.checkpointLabel="C:/PUBLIC-FIXTURE/checkpoint";r.checkpointSha256=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
  check(store.saveResumeCheckpoint(Device,r,bytes).isEmpty(),"native exact checkpoint metadata");
  const auto prepared=store.prepareCheckpointResume(ResumeRequest,Device,booking,Old,Next,r.checkpointSha256,Observation);
  check(prepared.status==CheckpointResumeAdmissionStatus::Prepared,"native reserve new epoch ONLY");const auto read=store.savedCheckpointResumeEpoch(Device,Next);
  check(read.status==CheckpointResumeReadStatus::Present&&read.receiptUtf8==prepared.receiptUtf8,"native immutable receipt readback");return {prepared,read};
 }
};
QString script(const NativeControlCommand& command){const auto remote=command.ssh().arguments.last();const auto bytes=QByteArray::fromBase64(remote.mid(remote.lastIndexOf(' ')+1).toLatin1());check(bytes.size()%2==0,"real UTF16LE EncodedCommand bytes");QString text;for(int i=0;i<bytes.size();i+=2)text+=QChar(quint8(bytes[i])|(quint16(quint8(bytes[i+1]))<<8));return text;}
void unknown(const NativeControlCommand& command,const QByteArray& bytes,bool normal=true,int code=0){const auto r=command.decode(bytes,normal,code);check(r.outcome==NativeControlOutcome::Unconfirmed&&!r.error.isEmpty()&&r.error.size()<=400&&r.status.isEmpty()&&r.requestId.isEmpty()&&r.run.isEmpty()&&r.packet.isEmpty()&&r.requiresFreshObservation(),"failure leaves NO plausible status/identity, never applied/automatic retry");}
void initialAndEndpoint(){
 Fixture f;const auto command=NativeControlCommand::initial(f.endpoint,f.initial,f.receipt,std::nullopt);const auto args=command.ssh().arguments;
 check(args.first()=="-F"&&args[1]=="none"&&args.contains(f.endpoint.keyFile)&&args[args.size()-3]=="--"&&args[args.size()-2]==f.endpoint.host,"DIRECT endpoint argv, key path not shell-concatenated");
 for(const auto* value:{"BatchMode=yes","StrictHostKeyChecking=yes","UpdateHostKeys=no","CheckHostIP=no","ClearAllForwardings=yes","PermitLocalCommand=no","RequestTTY=no"})check(args.contains(value),"reuse exact SSH observation trust policy");
 const auto ps=script(command);check(ps.contains("-Action initial")&&ps.contains("-ExpectedRun ''")&&!ps.contains(f.endpoint.keyFile),"typed script only; no key contents/paths in remote script");
 const auto token=ps.mid(ps.indexOf("-InitialAdmission '")+19);const auto encoded=token.left(token.indexOf('\''));const auto admission=QJsonDocument::fromJson(QByteArray::fromBase64(encoded.toLatin1())).object();
 check(json(admission.value("receipt").toObject())==f.receipt&&admission.value("base").toObject().value("base_run").isNull(),"native immutable receipt preserved, explicit null cold base");
 const auto requested=json({{"status","START_REQUESTED"},{"request_id",Request},{"launch",QJsonObject{{"status","START_REQUESTED"}}}});
 check(command.decode(requested,true,0).outcome==NativeControlOutcome::RequestReported,"request ACK is not physical applied");
 check(command.decode(json({{"status","ALREADY_RUNNING"},{"request_id",Request},{"run",Old}}),true,0).outcome==NativeControlOutcome::AlreadyRunningReported,"initial already running still requires fresh observation");
 check(command.decode(json({{"status","INITIAL_START_HELD"},{"request_id",Request},{"error","explicit local hold"}}),true,0).outcome==NativeControlOutcome::Held,"correlated explicit hold");
 for(auto bytes:{QByteArray{},QByteArray("{}"),QByteArray("[]"),requested+"{}",requested.left(requested.size()-1),QByteArray("{\"status\":\"START_REQUESTED\",\"st\\u0061tus\":\"START_REQUESTED\"}"),QByteArray("{\"status\":\"START_REQUESTED\",\"request_id\":\"")+Request.toUtf8()+"\",\"launch\":{\"status\":\"START_REQUESTED\",\"status\":\"START_REQUESTED\"}}",QByteArray("\xff"),QByteArray(NativeControlCommand::ResponseLimit+1,'x')})unknown(command,bytes);
 unknown(command,requested,true,1);unknown(command,requested,false,0);
 check(command.decode(requested,true,0,QByteArray(8193,'x')).outcome==NativeControlOutcome::Unconfirmed,"stderr bound");
 unknown(command,json({{"status","START_REQUESTED"},{"request_id",ResumeRequest},{"launch",QJsonObject{{"status","START_REQUESTED"}}}}));
 unknown(command,json({{"status","START_REQUESTED"},{"request_id",Request},{"launch",QJsonObject{{"status","ALREADY_RUNNING"}}}}));
 unknown(command,json({{"status","START_REQUESTED"},{"request_id",Request},{"launch",QJsonObject{{"status","START_REQUESTED"},{"run",Wrong}}}}));
 unknown(command,json({{"status","OFF"},{"request_id",Request}}));
 auto replay=f.initial;replay.replayed=true;refused([&]{NativeControlCommand::initial(f.endpoint,replay,f.receipt,std::nullopt);});replay=f.initial;replay.status=InitialStartStatus::ReconcilePrepared;refused([&]{NativeControlCommand::initial(f.endpoint,replay,f.receipt,std::nullopt);});
 refused([&]{NativeControlCommand::initial(f.endpoint,f.initial,f.receipt+' ',std::nullopt);});
 auto bad=f.endpoint;bad.host="-oProxyCommand=anything";refused([&]{NativeControlCommand::initial(bad,f.initial,f.receipt,std::nullopt);});bad=f.endpoint;bad.user="user';anything";refused([&]{NativeControlCommand::initial(bad,f.initial,f.receipt,std::nullopt);});bad=f.endpoint;bad.port=0;refused([&]{NativeControlCommand::initial(bad,f.initial,f.receipt,std::nullopt);});bad=f.endpoint;bad.keyFile=f.temp.filePath("missing");refused([&]{NativeControlCommand::initial(bad,f.initial,f.receipt,std::nullopt);});
 refused([&]{NativeControlCommand::initial(f.endpoint,f.initial,f.receipt,NativeObservedBase{Old,"2026-02-30T00:00:00Z",Start,End});});
 const auto withBase=NativeControlCommand::initial(f.endpoint,f.initial,f.receipt,NativeObservedBase{Old,Birth,Start,End});check(script(withBase).contains("-ExpectedRun '"+Old+"'"),"explicit existing base identity");
}
void resumed(){
 Fixture f;auto [prepared,read]=f.resume();const NativeObservedBase base{Old,Birth,Start,End};const auto command=NativeControlCommand::resume(f.endpoint,prepared,read,base);const auto ps=script(command);
 check(ps.contains("-Action resume")&&ps.contains("-ExpectedRun '"+Old+"'")&&!ps.contains("-Action start"),"resume distinct typed action, not generic/random-run start");
 const auto token=ps.mid(ps.indexOf("-ResumeAdmission '")+18);const auto encoded=token.left(token.indexOf('\''));const auto admission=QJsonDocument::fromJson(QByteArray::fromBase64(encoded.toLatin1())).object();
 check(json(admission.value("receipt").toObject())==read.receiptUtf8&&admission.value("receipt").toObject().value("run")==Next,"wire retains SAME actual new reserved epoch and wide strings");
 const auto already=json({{"status","ALREADY_RUNNING"},{"request_id",ResumeRequest},{"run",Next}});check(command.decode(already,true,0).outcome==NativeControlOutcome::AlreadyRunningReported,"matching reserved run reported only");
 unknown(command,json({{"status","ALREADY_RUNNING"},{"request_id",ResumeRequest},{"run",Wrong}}));
 auto changed=read;changed.receiptUtf8+=' ';refused([&]{NativeControlCommand::resume(f.endpoint,prepared,changed,base);});
 changed=read;changed.epoch->checkpoint.resume.tests="9007199254741994";refused([&]{NativeControlCommand::resume(f.endpoint,prepared,changed,base);});
 auto altered=prepared;altered.epoch->checkpoint.resume.gaps="40";refused([&]{NativeControlCommand::resume(f.endpoint,altered,read,base);});
 altered=prepared;altered.status=CheckpointResumeAdmissionStatus::ReconcilePrepared;refused([&]{NativeControlCommand::resume(f.endpoint,altered,read,base);});
 auto badBase=base;badBase.run=Wrong;refused([&]{NativeControlCommand::resume(f.endpoint,prepared,read,badBase);});
 changed=read;changed.epoch->checkpoint.checkpointBytes[8]^=char(1);refused([&]{NativeControlCommand::resume(f.endpoint,prepared,changed,base);});
}
void queuedAndStop(){
 Fixture f;const QString nextEnd=addUnsigned(End,Length);const NativeQueuedAdvance queued{Device,Request,End,nextEnd,Old,Start,End,f.booking};const auto command=NativeControlCommand::advanceQueued(f.endpoint,queued);
 check(script(command).contains("-Action advance")&&script(command).contains("-QueueStart '"+End+"'"),"queued exact wide decimal, no default automatic allocation");
 QJsonObject completion{{"schema","pgn-source-range-completion-1"},{"run",Old},{"start",Start},{"end",End},{"queued_intent",f.booking},{"source_positions",Length},{"source_tests","9007199254741995"},{"source_session_gaps","41"},{"source_exit_code",0},{"source_exit_utc",Birth},{"source","C:/PUBLIC-FIXTURE/old.stdout"},{"source_range_complete",true},{"independent_coverage",false},{"proof_status","SOURCE_PRP_INDEPENDENT_PROOF_PENDING"}};
 QJsonObject packet{{"status","SUCCESSOR_START_REQUESTED"},{"launch",QJsonObject{{"status","START_REQUESTED"}}},{"completion",completion},{"predecessor_binding",QJsonObject{{"uuid",Device},{"start",Start},{"end",End}}},{"binding",QJsonObject{{"uuid",Device},{"start",End},{"end",nextEnd},{"queued_intent",Request}}},{"community_csv",QString::fromLatin1(QByteArray("PUBLIC FIXTURE\n").toBase64())}};
 check(command.decode(json(packet),true,0).outcome==NativeControlOutcome::RequestReported,"strict queued result is still not admission/proof of launch");
 auto heldPacket=packet;heldPacket.insert("status","SUCCESSOR_HELD");heldPacket.insert("launch",QJsonObject{{"status","OPERATOR_STOP_HELD"}});check(command.decode(json(heldPacket),true,0).outcome==NativeControlOutcome::Held&&!command.decode(json(heldPacket),true,0).error.isEmpty(),"known held successor remains explicit and bounded");heldPacket.insert("launch",QJsonObject{{"status","INVENTED_STATUS"}});unknown(command,json(heldPacket));
 for(const auto* name:{"run","end","source_positions","source_tests","source_exit_code","queued_intent"}){auto bad=packet,part=completion;part.remove(name);bad.insert("completion",part);unknown(command,json(bad));}
 auto bad=json(packet);bad.replace("\"source_exit_code\":0","\"source_exit_code\":0.0");unknown(command,bad);
 auto changed=queued;changed.bookingId="';anything";refused([&]{NativeControlCommand::advanceQueued(f.endpoint,changed);});changed=queued;changed.start="01";refused([&]{NativeControlCommand::advanceQueued(f.endpoint,changed);});
 const NativeStopIdentity identity{Device,Old,"4294967295",Birth,QString(64,'a')};
 refused([&]{NativeControlCommand::stopObservedIdentity(f.endpoint,identity,{});},NativeControlErrorCode::UnsupportedAbi);
 const auto stop=NativeControlCommand::stopObservedIdentity(f.endpoint,identity,"pgn-stop-observed-1");check(script(stop).contains("-Action stop-observed")&&script(stop).contains("-ExpectedPid '4294967295'")&&!script(stop).contains(" -Action stop "),"new identity stop ABI only, NEVER fallback legacy");
 QJsonObject result{{"protocol","pgn-stop-observed-1"},{"status","STOP_REQUESTED"},{"observed",QJsonObject{{"run",Old},{"pid",identity.pid},{"birth",Birth},{"image_sha256",identity.imageSha256.toUpper()}}},{"physical_applied",false}};
 check(stop.decode(json(result),true,0).outcome==NativeControlOutcome::RequestReported,"STOP request/latch is NOT physical stopped/Ready");result.insert("status","ALREADY_EXITED_STOP_HELD");check(stop.decode(json(result),true,0).outcome==NativeControlOutcome::RequestReported,"owner absent latch still fresh observation required");
 for(const auto* name:{"run","pid","birth","image_sha256"}){auto changed=result,observed=result.value("observed").toObject();observed.insert(name,"wrong");changed.insert("observed",observed);unknown(stop,json(changed));}
 result.insert("physical_applied",true);unknown(stop,json(result));unknown(stop,"{\"status\":\"OFF\"}");
 for(const auto pid:{"0","01","4294967296","1234.0","1e3"}){auto changed=identity;changed.pid=pid;refused([&]{NativeControlCommand::stopObservedIdentity(f.endpoint,changed,"pgn-stop-observed-1");});}
 auto invalid=identity;invalid.birth="2026-10-15T00:00:00.000Z";refused([&]{NativeControlCommand::stopObservedIdentity(f.endpoint,invalid,"pgn-stop-observed-1");});
}
void configuredProfileAndLatch(){
 Fixture f;const auto read=NativeControlCommand::configured3090Profile(f.endpoint);
 check(script(read).contains("-Action configured-profile}")&&!script(read).contains("-Action snapshot"),"distinct profile read never invokes Owner snapshot");
 check(script(read).contains("[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)")&&script(read).contains("catch{[Console]::Error.WriteLine($_.Exception.Message);exit 1}"),"control errors retain UTF8 diagnostics andnonzero exit");
 const QString image="C:/PUBLIC-TESTONLY/engine'.exe";const QJsonObject pin{{"path",image},{"sha256",QString(64,'A')}};
 const QJsonObject profile{{"device",Device},{"executable",image},{"image_sha256",QString(64,'A')},{"binding_sha256",QString(64,'B')}};
 const QJsonObject binding{{"uuid",Device},{"executable",image},{"pins",QJsonArray{pin}},{"unknown_extension",QJsonObject{{"original","DO NOT NORMALIZE"}}}};
 const QJsonObject packet{{"schema","pgn-configured-profile-observation-1"},{"protocol","pgn-configured-stop-1"},{"status","CONFIGURED_PROFILE_OBSERVED"},{"profile",profile},{"binding",binding},{"source_capacity",QJsonValue::Null}};
 const auto decoded=read.decode(json(packet),true,0);check(decoded.outcome==NativeControlOutcome::ConfiguredProfileReported&&decoded.configuredProfile.has_value(),"typed profile observation only");
 const auto observed=*decoded.configuredProfile;check(!observed.sourceCapacity&&observed.binding==binding&&observed.bindingSha256==QString(64,'B'),"whole unknown binding preserved and capacity absent not zero");
 for(const auto reason:{Configured3090OffReason::ApplicationClose,Configured3090OffReason::ExplicitCardOff}){
  const auto stop=NativeControlCommand::configured3090StopLatch(f.endpoint,Request,reason,observed);const auto ps=script(stop);
  check(ps.contains("-Action stop-configured")&&ps.contains("-ExpectedExecutable 'C:/PUBLIC-TESTONLY/engine''.exe'")&&!ps.contains("-ExpectedPid")&&!ps.contains("-ExpectedRun"),"profile latch quoted exactly, no fabricated process identity");
  QJsonObject reply{{"protocol","pgn-configured-stop-1"},{"status","CONFIGURED_OFF_LATCHED"},{"request_id",Request},{"reason",reason==Configured3090OffReason::ApplicationClose?"AppClose":"CardOff"},{"profile",profile},{"physical_applied",false}};
  const auto result=stop.decode(json(reply),true,0);check(result.outcome==NativeControlOutcome::ConfiguredOffLatchReported&&result.requiresFreshObservation()&&result.run.isEmpty(),"desired-OFF receipt never RequestReported/physical state");
  for(const auto* key:{"protocol","status","request_id","reason","profile","physical_applied"}){auto bad=reply;bad.remove(key);unknown(stop,json(bad));}
  auto bad=reply;bad["physical_applied"]=true;unknown(stop,json(bad));bad=reply;bad["request_id"]=ResumeRequest;unknown(stop,json(bad));
  bad=reply;auto tuple=profile;tuple["binding_sha256"]=QString(64,'C');bad["profile"]=tuple;unknown(stop,json(bad));
  unknown(stop,json(reply),true,2);unknown(stop,json(reply).chopped(1));
 }
 auto unsupported=observed;unsupported.protocol="legacy";refused([&]{NativeControlCommand::configured3090StopLatch(f.endpoint,Request,Configured3090OffReason::ExplicitCardOff,unsupported);},NativeControlErrorCode::UnsupportedAbi);
 for(const auto* key:{"schema","protocol","status","profile","binding","source_capacity"}){auto bad=packet;bad.remove(key);unknown(read,json(bad));}
 auto bad=packet;bad["source_capacity"]=false;unknown(read,json(bad));bad=packet;bad["owned"]=false;unknown(read,json(bad));
 auto duplicate=json(packet);duplicate.replace("\"device\":","\"device\":\"ambiguous\",\"device\":");unknown(read,duplicate);
 // Synthetic wire declarations only, not an installed SDK/hash verification.
 const QString sdk="C:/PGN/native/20261012-gpu-owned-source-v1",engine=sdk+"/bin/pgn-continuous-gpu.exe",qsha="99E5823D17AA6B7D202B205D797C0C1E18C807D5CCC93CBA2262F35FCDF72684",esha="A73A6E383308560EDC36CE351B09EB449FA099275361BE74BE89020B03864B43";
 auto qualified=packet;qualified["profile"]=QJsonObject{{"device",Device},{"executable",engine},{"image_sha256",esha},{"binding_sha256",QString(64,'B')}};
 auto qb=binding;qb["executable"]=engine;qb["sdk"]=sdk;qb["engine_upgrade"]=QJsonObject{{"policy","GPU_OWNED_HIGH10_SOURCE_V1"},{"qualification_sha256",qsha}};
 qb["pins"]=QJsonArray{QJsonObject{{"path",engine},{"sha256",esha}},QJsonObject{{"path",sdk+"/bin/cudart64_12.dll"},{"sha256","9D9B868955149875AC4EF43442AAAA8913D4A7B3E4D6DD60EE871626AA045768"}}};qualified["binding"]=qb;
 const QJsonObject cap{{"schema","pgn-qualified-source-capacity-1"},{"policy","GPU_OWNED_HIGH10_SOURCE_V1"},{"sdk",sdk},{"scope_start","184467440737095516160"},{"scope_end","202914184810805067776"},{"max_length","1000000000000000"},{"guard","Assert-PgnQualifiedSourceRange"},{"qualification_sha256",qsha}};qualified["source_capacity"]=cap;
 const auto parsed=read.decode(json(qualified),true,0);check(parsed.configuredProfile&&parsed.configuredProfile->sourceCapacity&&parsed.configuredProfile->sourceCapacity->maximumLength=="1000000000000000","qualified guard budget typed separately from transfer/current range");
 // Exact v2 wire declarations only. The one shared profile table pays image
 // mapping; production's actual rehash/transition/freshness remains external.
 const QString sdk2="C:/PGN/native/20261015-source-boundary-v2",engine2=sdk2+"/bin/pgn-continuous-gpu.exe",qsha2="2DB06EAD094579A18588BBEF6429102A5E59052BCF2D09E3A13E0E8E84964C52",esha2="74A0FDD01CCF242FA3CA8714E4765C23022987C6785889CD8B1ECFA3AA27472B",policy2="GPU_OWNED_HIGH10_SOURCE_BOUNDARY_V2";
 auto v2=qualified;auto vp=qualified.value("profile").toObject();vp["executable"]=engine2;vp["image_sha256"]=esha2;v2["profile"]=vp;
 auto vb=qb;vb["executable"]=engine2;vb["sdk"]=sdk2;vb["source_boundary_protocol"]="pgn-source-boundary-v2";vb["engine_upgrade"]=QJsonObject{{"policy",policy2},{"qualification_sha256",qsha2}};
 vb["pins"]=QJsonArray{QJsonObject{{"path",engine2},{"sha256",esha2}},QJsonObject{{"path",sdk2+"/bin/cudart64_12.dll"},{"sha256","9D9B868955149875AC4EF43442AAAA8913D4A7B3E4D6DD60EE871626AA045768"}}};vb["length"]="500";v2["binding"]=vb;
 auto vc=cap;vc["policy"]=policy2;vc["sdk"]=sdk2;vc["qualification_sha256"]=qsha2;v2["source_capacity"]=vc;
 const auto positive=read.decode(json(v2),true,0);
 check(positive.outcome==NativeControlOutcome::ConfiguredProfileReported&&positive.configuredProfile&&positive.configuredProfile->sourceCapacity,"exact v2 configured capacity admitted without a launch");
 const auto& budget=*positive.configuredProfile->sourceCapacity;
 check(budget.policy==policy2&&budget.sdk==sdk2&&budget.qualificationSha256==qsha2&&budget.maximumLength=="1000000000000000"&&budget.scopeStart=="184467440737095516160"&&budget.scopeEnd=="202914184810805067776","v2 high10/full block capacity independent of current fragment500");
 check(positive.configuredProfile->binding==vb&&positive.configuredProfile->bindingSha256==QString(64,'B'),"v2 decode preserves exact binding/hash, no metadata synthesis");
 for(const auto* key:{"schema","policy","sdk","scope_start","scope_end","max_length","guard","qualification_sha256"}){
  auto changed=v2;auto c=vc;c[key]=key==QString("policy")?QString("GPU_OWNED_HIGH10_SOURCE_V1"):QString("UNKNOWN");changed["source_capacity"]=c;unknown(read,json(changed));
  changed=v2;c=vc;c[key]=QJsonValue::Null;changed["source_capacity"]=c;unknown(read,json(changed));
 }
 {auto changed=v2;auto b=vb;b.remove("source_boundary_protocol");changed["binding"]=b;unknown(read,json(changed));}
 for(const auto& value:QJsonArray{QJsonValue::Null,false,0,"pgn-source-legacy-a73","pgn-source-boundary-v1","pgn-source-boundary-v2\n"}){auto changed=v2;auto b=vb;b["source_boundary_protocol"]=value;changed["binding"]=b;unknown(read,json(changed));}
 for(const auto& value:QJsonArray{QJsonObject{{"policy",policy2},{"qualification_sha256",qsha}},QJsonObject{{"policy","GPU_OWNED_HIGH10_SOURCE_V1"},{"qualification_sha256",qsha2}},QJsonValue::Null}){auto changed=v2;auto b=vb;b["engine_upgrade"]=value;changed["binding"]=b;unknown(read,json(changed));}
 for(int poison=0;poison<8;++poison){
  auto changed=v2;auto b=vb;auto p=vp;auto pins=vb.value("pins").toArray();
  if(poison==0)b["sdk"]=sdk;
  if(poison==1){p["image_sha256"]=esha;auto pin=pins[0].toObject();pin["sha256"]=esha;pins[0]=pin;}
  if(poison==2){p["executable"]=engine;b["executable"]=engine;auto pin=pins[0].toObject();pin["path"]=engine;pins[0]=pin;}
  if(poison==3){auto pin=pins[1].toObject();pin["sha256"]=QString(64,'F');pins[1]=pin;}
  if(poison==4){auto pin=pins[1].toObject();pin["path"]=sdk+"/bin/cudart64_12.dll";pins[1]=pin;}
  if(poison==5)pins.removeAt(1);
  if(poison==6)pins.append(pins[1]);
  if(poison==7)b["uuid"]="GPU-OTHER";
  b["pins"]=pins;changed["binding"]=b;changed["profile"]=p;unknown(read,json(changed));
 }
 // Exact lazy image/capacity declaration; mixed generations remain held.
 const QString sdkL="C:/PGN/native/20261017-lazy-integer-v1",engineL=sdkL+"/bin/pgn-continuous-gpu.exe",qshaL="37D549CB05AA4E47E423FB554F1DFEEA6FB5B10CAEC3A37D9E78F33843C168FD",eshaL="A1597B9C4D2431C204BCC27AE69E9840C86CC5253E0FB63C09FA162266F55639",policyL="GPU_OWNED_HIGH10_LAZY_INTEGER_V1";
 auto lazy=qualified;auto lp=qualified.value("profile").toObject();lp["executable"]=engineL;lp["image_sha256"]=eshaL;lazy["profile"]=lp;
 auto lb=qb;lb["executable"]=engineL;lb["sdk"]=sdkL;lb["source_boundary_protocol"]="pgn-source-legacy-lazy-v1";lb["engine_upgrade"]=QJsonObject{{"policy",policyL},{"qualification_sha256",qshaL}};
 lb["pins"]=QJsonArray{QJsonObject{{"path",engineL},{"sha256",eshaL}},QJsonObject{{"path",sdkL+"/bin/cudart64_12.dll"},{"sha256","9D9B868955149875AC4EF43442AAAA8913D4A7B3E4D6DD60EE871626AA045768"}}};lazy["binding"]=lb;
 auto lc=cap;lc["policy"]=policyL;lc["sdk"]=sdkL;lc["qualification_sha256"]=qshaL;lazy["source_capacity"]=lc;
 const auto lazyParsed=read.decode(json(lazy),true,0);check(lazyParsed.configuredProfile&&lazyParsed.configuredProfile->sourceCapacity&&lazyParsed.configuredProfile->sourceCapacity->policy==policyL,"exact lazy capacity admitted, never launch authority");
 for(const auto* key:{"policy","sdk","scope_start","scope_end","max_length","guard","qualification_sha256"}){
  auto changed=lazy;auto c=lc;c[key]="UNKNOWN";changed["source_capacity"]=c;unknown(read,json(changed));
 }
 for(const auto* key:{"sdk","source_boundary_protocol","engine_upgrade","pins"}){
  auto changed=lazy;auto b=lb;b[key]=qb.value(key);changed["binding"]=b;unknown(read,json(changed));
 }
 {
 // Exact shared/L1 image/capacity declaration; mixed generations remain held.
 const QString sdkL="C:/PGN/native/20261017-lazy-shared-v1",engineL=sdkL+"/bin/pgn-continuous-gpu.exe",qshaL="0A58315DB343DEDBECF732CEF9FD13E35DBF505872899B59D88784FE7502823B",eshaL="FA6F7E44D1187E1CC519FDC0454D36C8E886D1C7D1F3F4C8321ADCAA8399AA81",policyL="GPU_OWNED_HIGH10_LAZY_INTEGER_SHARED_V1";
 auto lazy=qualified;auto lp=qualified.value("profile").toObject();lp["executable"]=engineL;lp["image_sha256"]=eshaL;lazy["profile"]=lp;
 auto lb=qb;lb["executable"]=engineL;lb["sdk"]=sdkL;lb["source_boundary_protocol"]="pgn-source-legacy-lazy-shared-v1";lb["engine_upgrade"]=QJsonObject{{"policy",policyL},{"qualification_sha256",qshaL}};
 lb["pins"]=QJsonArray{QJsonObject{{"path",engineL},{"sha256",eshaL}},QJsonObject{{"path",sdkL+"/bin/cudart64_12.dll"},{"sha256","9D9B868955149875AC4EF43442AAAA8913D4A7B3E4D6DD60EE871626AA045768"}}};lazy["binding"]=lb;
 auto lc=cap;lc["policy"]=policyL;lc["sdk"]=sdkL;lc["qualification_sha256"]=qshaL;lazy["source_capacity"]=lc;
 const auto lazyParsed=read.decode(json(lazy),true,0);check(lazyParsed.configuredProfile&&lazyParsed.configuredProfile->sourceCapacity&&lazyParsed.configuredProfile->sourceCapacity->policy==policyL,"exact lazy capacity admitted, never launch authority");
 for(const auto* key:{"policy","sdk","scope_start","scope_end","max_length","guard","qualification_sha256"}){
  auto changed=lazy;auto c=lc;c[key]="UNKNOWN";changed["source_capacity"]=c;unknown(read,json(changed));
 }
 for(const auto* key:{"sdk","source_boundary_protocol","engine_upgrade","pins"}){
  auto changed=lazy;auto b=lb;b[key]=qb.value(key);changed["binding"]=b;unknown(read,json(changed));
 }
 }
 {
 // Exact periodic pre-sieve image/capacity declaration; mixed generations remain held.
 const QString sdkL="C:/PGN/native/20261017-lazy-shared-periodic-v1",engineL=sdkL+"/bin/pgn-continuous-gpu.exe",qshaL="2FC9F2DB60CF745EB28DA80AA3E96CEAC9397E91A6185F314E1E87D7BD7D84AF",eshaL="BFEBD7F80DD7B0670A3FF51573C476AB0FD61DAFF7B59949EA4E60AF8031BC53",policyL="GPU_OWNED_HIGH10_LAZY_INTEGER_SHARED_PERIODIC_V1";
 auto lazy=qualified;auto lp=qualified.value("profile").toObject();lp["executable"]=engineL;lp["image_sha256"]=eshaL;lazy["profile"]=lp;
 auto lb=qb;lb["executable"]=engineL;lb["sdk"]=sdkL;lb["source_boundary_protocol"]="pgn-source-legacy-lazy-shared-periodic-v1";lb["engine_upgrade"]=QJsonObject{{"policy",policyL},{"qualification_sha256",qshaL}};
 lb["pins"]=QJsonArray{QJsonObject{{"path",engineL},{"sha256",eshaL}},QJsonObject{{"path",sdkL+"/bin/cudart64_12.dll"},{"sha256","9D9B868955149875AC4EF43442AAAA8913D4A7B3E4D6DD60EE871626AA045768"}}};lazy["binding"]=lb;
 auto lc=cap;lc["policy"]=policyL;lc["sdk"]=sdkL;lc["qualification_sha256"]=qshaL;lazy["source_capacity"]=lc;
 const auto lazyParsed=read.decode(json(lazy),true,0);check(lazyParsed.configuredProfile&&lazyParsed.configuredProfile->sourceCapacity&&lazyParsed.configuredProfile->sourceCapacity->policy==policyL,"exact lazy capacity admitted, never launch authority");
 for(const auto* key:{"policy","sdk","scope_start","scope_end","max_length","guard","qualification_sha256"}){
  auto changed=lazy;auto c=lc;c[key]="UNKNOWN";changed["source_capacity"]=c;unknown(read,json(changed));
 }
 for(const auto* key:{"sdk","source_boundary_protocol","engine_upgrade","pins"}){
  auto changed=lazy;auto b=lb;b[key]=qb.value(key);changed["binding"]=b;unknown(read,json(changed));
 }
 }
 {
 // Exact fullchunk bank-phase image/capacity declaration; mixed generations remain held.
 const QString sdkL="C:/PGN/native/20261017-lazy-shared-periodic-bank-v1",engineL=sdkL+"/bin/pgn-continuous-gpu.exe",qshaL="FC752A691686BA693EAFBB1273E0C1EB7B9CC5E47B9217B2E4F69E329101A5A8",eshaL="9A76E632FA1761B26B68B33D6D7EEE15C1D7A0FF6E0D0D5772DA9C6F0C13171E",policyL="GPU_OWNED_HIGH10_LAZY_INTEGER_SHARED_PERIODIC_BANK_V1";
 auto lazy=qualified;auto lp=qualified.value("profile").toObject();lp["executable"]=engineL;lp["image_sha256"]=eshaL;lazy["profile"]=lp;
 auto lb=qb;lb["executable"]=engineL;lb["sdk"]=sdkL;lb["source_boundary_protocol"]="pgn-source-legacy-lazy-shared-periodic-bank-v1";lb["engine_upgrade"]=QJsonObject{{"policy",policyL},{"qualification_sha256",qshaL}};
 lb["pins"]=QJsonArray{QJsonObject{{"path",engineL},{"sha256",eshaL}},QJsonObject{{"path",sdkL+"/bin/cudart64_12.dll"},{"sha256","9D9B868955149875AC4EF43442AAAA8913D4A7B3E4D6DD60EE871626AA045768"}}};lazy["binding"]=lb;
 auto lc=cap;lc["policy"]=policyL;lc["sdk"]=sdkL;lc["qualification_sha256"]=qshaL;lazy["source_capacity"]=lc;
 const auto lazyParsed=read.decode(json(lazy),true,0);check(lazyParsed.configuredProfile&&lazyParsed.configuredProfile->sourceCapacity&&lazyParsed.configuredProfile->sourceCapacity->policy==policyL,"exact lazy capacity admitted, never launch authority");
 for(const auto* key:{"policy","sdk","scope_start","scope_end","max_length","guard","qualification_sha256"}){
  auto changed=lazy;auto c=lc;c[key]="UNKNOWN";changed["source_capacity"]=c;unknown(read,json(changed));
 }
 for(const auto* key:{"sdk","source_boundary_protocol","engine_upgrade","pins"}){
  auto changed=lazy;auto b=lb;b[key]=qb.value(key);changed["binding"]=b;unknown(read,json(changed));
 }
 }
 {
 // Exact mixed-radix70 image/capacity declaration; mixed generations remain held.
 const QString sdkL="C:/PGN/native/20261017-radix70-v1",engineL=sdkL+"/bin/pgn-continuous-gpu.exe",qshaL="5C74B73EA8A22A11339595BBA9EB5592253B8ADB64D8FCF142EE66C2E0B94675",eshaL="C6F209AB51936473611E7D34DDE30E838793073F2D061D5DC8C0D4DA69884684",policyL="GPU_OWNED_HIGH10_RADIX70_SHARED_PERIODIC_BANK_V1";
 auto lazy=qualified;auto lp=qualified.value("profile").toObject();lp["executable"]=engineL;lp["image_sha256"]=eshaL;lazy["profile"]=lp;
 auto lb=qb;lb["executable"]=engineL;lb["sdk"]=sdkL;lb["source_boundary_protocol"]="pgn-source-legacy-radix70-v1";lb["engine_upgrade"]=QJsonObject{{"policy",policyL},{"qualification_sha256",qshaL}};
 lb["pins"]=QJsonArray{QJsonObject{{"path",engineL},{"sha256",eshaL}},QJsonObject{{"path",sdkL+"/bin/cudart64_12.dll"},{"sha256","9D9B868955149875AC4EF43442AAAA8913D4A7B3E4D6DD60EE871626AA045768"}}};lazy["binding"]=lb;
 auto lc=cap;lc["policy"]=policyL;lc["sdk"]=sdkL;lc["qualification_sha256"]=qshaL;lazy["source_capacity"]=lc;
 const auto lazyParsed=read.decode(json(lazy),true,0);check(lazyParsed.configuredProfile&&lazyParsed.configuredProfile->sourceCapacity&&lazyParsed.configuredProfile->sourceCapacity->policy==policyL,"exact lazy capacity admitted, never launch authority");
 for(const auto* key:{"policy","sdk","scope_start","scope_end","max_length","guard","qualification_sha256"}){
  auto changed=lazy;auto c=lc;c[key]="UNKNOWN";changed["source_capacity"]=c;unknown(read,json(changed));
 }
 for(const auto* key:{"sdk","source_boundary_protocol","engine_upgrade","pins"}){
  auto changed=lazy;auto b=lb;b[key]=qb.value(key);changed["binding"]=b;unknown(read,json(changed));
 }
 }
 {
 // Exact integer-corrected float-prefix image/capacity declaration; mixed generations remain held.
 const QString sdkL="C:/PGN/native/20261017-radix70-float-prefix-v1",engineL=sdkL+"/bin/pgn-continuous-gpu.exe",qshaL="5EC12E85699ACA23A507E20966168F4D611BA27258FD6BF56B517B14651C1C70",eshaL="9AFBA37AE45903E9C279349041784CC0B8F1EBFCE129BD10992F84870126644C",policyL="GPU_OWNED_HIGH10_RADIX70_FLOAT_PREFIX_SHARED_PERIODIC_BANK_V1";
 auto lazy=qualified;auto lp=qualified.value("profile").toObject();lp["executable"]=engineL;lp["image_sha256"]=eshaL;lazy["profile"]=lp;
 auto lb=qb;lb["executable"]=engineL;lb["sdk"]=sdkL;lb["source_boundary_protocol"]="pgn-source-legacy-radix70-float-prefix-v1";lb["engine_upgrade"]=QJsonObject{{"policy",policyL},{"qualification_sha256",qshaL}};
 lb["pins"]=QJsonArray{QJsonObject{{"path",engineL},{"sha256",eshaL}},QJsonObject{{"path",sdkL+"/bin/cudart64_12.dll"},{"sha256","9D9B868955149875AC4EF43442AAAA8913D4A7B3E4D6DD60EE871626AA045768"}}};lazy["binding"]=lb;
 auto lc=cap;lc["policy"]=policyL;lc["sdk"]=sdkL;lc["qualification_sha256"]=qshaL;lazy["source_capacity"]=lc;
 const auto lazyParsed=read.decode(json(lazy),true,0);check(lazyParsed.configuredProfile&&lazyParsed.configuredProfile->sourceCapacity&&lazyParsed.configuredProfile->sourceCapacity->policy==policyL,"exact lazy capacity admitted, never launch authority");
 for(const auto* key:{"policy","sdk","scope_start","scope_end","max_length","guard","qualification_sha256"}){
  auto changed=lazy;auto c=lc;c[key]="UNKNOWN";changed["source_capacity"]=c;unknown(read,json(changed));
 }
 for(const auto* key:{"sdk","source_boundary_protocol","engine_upgrade","pins"}){
  auto changed=lazy;auto b=lb;b[key]=qb.value(key);changed["binding"]=b;unknown(read,json(changed));
 }
 }
 {
 // Exact lazy Barrett image/capacity declaration; mixed generations remain held.
 const QString sdkL="C:/PGN/native/20261017-barrett70-v1",engineL=sdkL+"/bin/pgn-continuous-gpu.exe",qshaL="A758E586FE3F31EF40D0F07A80857758D8835B0A1E1E1594E7AE02B2490EC1CF",eshaL="1A909AB796AC1E385DEE6F1C09AB6CEB1A3A334B887E46CFBBF2D656726CB41A",policyL="GPU_OWNED_HIGH10_BARRETT70_SHARED_PERIODIC_BANK_V1";
 auto lazy=qualified;auto lp=qualified.value("profile").toObject();lp["executable"]=engineL;lp["image_sha256"]=eshaL;lazy["profile"]=lp;
 auto lb=qb;lb["executable"]=engineL;lb["sdk"]=sdkL;lb["source_boundary_protocol"]="pgn-source-legacy-barrett70-v1";lb["engine_upgrade"]=QJsonObject{{"policy",policyL},{"qualification_sha256",qshaL}};
 lb["pins"]=QJsonArray{QJsonObject{{"path",engineL},{"sha256",eshaL}},QJsonObject{{"path",sdkL+"/bin/cudart64_12.dll"},{"sha256","9D9B868955149875AC4EF43442AAAA8913D4A7B3E4D6DD60EE871626AA045768"}}};lazy["binding"]=lb;
 auto lc=cap;lc["policy"]=policyL;lc["sdk"]=sdkL;lc["qualification_sha256"]=qshaL;lazy["source_capacity"]=lc;
 const auto lazyParsed=read.decode(json(lazy),true,0);check(lazyParsed.configuredProfile&&lazyParsed.configuredProfile->sourceCapacity&&lazyParsed.configuredProfile->sourceCapacity->policy==policyL,"exact lazy capacity admitted, never launch authority");
 for(const auto* key:{"policy","sdk","scope_start","scope_end","max_length","guard","qualification_sha256"}){
  auto changed=lazy;auto c=lc;c[key]="UNKNOWN";changed["source_capacity"]=c;unknown(read,json(changed));
 }
 for(const auto* key:{"sdk","source_boundary_protocol","engine_upgrade","pins"}){
  auto changed=lazy;auto b=lb;b[key]=qb.value(key);changed["binding"]=b;unknown(read,json(changed));
 }
 }
 {
 // Exact lazy two-bit window/coarse-inverse image/capacity declaration; mixed generations remain held.
 const QString sdkL="C:/PGN/native/20261017-window2-recip5-v1",engineL=sdkL+"/bin/pgn-continuous-gpu.exe",qshaL="DB6F8E08FAA9AB0E7A89ED1305E3B298E854A680851B2418D54175F4EADA0D26",eshaL="C5FBAF75DE15631933C87419EC638CA53E3CB6B03668AD0A6B1F5B1818979566",policyL="GPU_OWNED_HIGH10_BARRETT70_WINDOW2_RECIP5_SHARED_PERIODIC_BANK_V1";
 auto lazy=qualified;auto lp=qualified.value("profile").toObject();lp["executable"]=engineL;lp["image_sha256"]=eshaL;lazy["profile"]=lp;
 auto lb=qb;lb["executable"]=engineL;lb["sdk"]=sdkL;lb["source_boundary_protocol"]="pgn-source-legacy-window2-recip5-v1";lb["engine_upgrade"]=QJsonObject{{"policy",policyL},{"qualification_sha256",qshaL}};
 lb["pins"]=QJsonArray{QJsonObject{{"path",engineL},{"sha256",eshaL}},QJsonObject{{"path",sdkL+"/bin/cudart64_12.dll"},{"sha256","9D9B868955149875AC4EF43442AAAA8913D4A7B3E4D6DD60EE871626AA045768"}}};lazy["binding"]=lb;
 auto lc=cap;lc["policy"]=policyL;lc["sdk"]=sdkL;lc["qualification_sha256"]=qshaL;lazy["source_capacity"]=lc;
 const auto lazyParsed=read.decode(json(lazy),true,0);check(lazyParsed.configuredProfile&&lazyParsed.configuredProfile->sourceCapacity&&lazyParsed.configuredProfile->sourceCapacity->policy==policyL,"exact lazy capacity admitted, never launch authority");
 for(const auto* key:{"policy","sdk","scope_start","scope_end","max_length","guard","qualification_sha256"}){
  auto changed=lazy;auto c=lc;c[key]="UNKNOWN";changed["source_capacity"]=c;unknown(read,json(changed));
 }
 for(const auto* key:{"sdk","source_boundary_protocol","engine_upgrade","pins"}){
  auto changed=lazy;auto b=lb;b[key]=qb.value(key);changed["binding"]=b;unknown(read,json(changed));
 }
 }
 {
 // Exact lazy two-bit window/coarse-inverse image/capacity declaration; mixed generations remain held.
 const QString sdkL="C:/PGN/native/20261017-barrett70-v1",engineL=sdkL+"/bin/pgn-continuous-gpu.exe",qshaL="A758E586FE3F31EF40D0F07A80857758D8835B0A1E1E1594E7AE02B2490EC1CF",eshaL="1A909AB796AC1E385DEE6F1C09AB6CEB1A3A334B887E46CFBBF2D656726CB41A",policyL="GPU_OWNED_HIGH10_BARRETT70_SHARED_PERIODIC_BANK_V1";
 auto lazy=qualified;auto lp=qualified.value("profile").toObject();lp["executable"]=engineL;lp["image_sha256"]=eshaL;lazy["profile"]=lp;
 auto lb=qb;lb["executable"]=engineL;lb["sdk"]=sdkL;lb["source_boundary_protocol"]="pgn-source-legacy-barrett70-v1";lb["engine_upgrade"]=QJsonObject{{"policy",policyL},{"qualification_sha256",qshaL}};
 lb["pins"]=QJsonArray{QJsonObject{{"path",engineL},{"sha256",eshaL}},QJsonObject{{"path",sdkL+"/bin/cudart64_12.dll"},{"sha256","9D9B868955149875AC4EF43442AAAA8913D4A7B3E4D6DD60EE871626AA045768"}}};lazy["binding"]=lb;
 auto lc=cap;lc["policy"]=policyL;lc["sdk"]=sdkL;lc["qualification_sha256"]=qshaL;lazy["source_capacity"]=lc;
 const auto lazyParsed=read.decode(json(lazy),true,0);check(lazyParsed.configuredProfile&&lazyParsed.configuredProfile->sourceCapacity&&lazyParsed.configuredProfile->sourceCapacity->policy==policyL,"exact lazy capacity admitted, never launch authority");
 for(const auto* key:{"policy","sdk","scope_start","scope_end","max_length","guard","qualification_sha256"}){
  auto changed=lazy;auto c=lc;c[key]="UNKNOWN";changed["source_capacity"]=c;unknown(read,json(changed));
 }
 for(const auto* key:{"sdk","source_boundary_protocol","engine_upgrade","pins"}){
  auto changed=lazy;auto b=lb;b[key]=qb.value(key);changed["binding"]=b;unknown(read,json(changed));
 }
 }
 {
 // Exact lazy two-bit window/coarse-inverse image/capacity declaration; mixed generations remain held.
 const QString sdkL="C:/PGN/native/20261017-barrett70-top2-v1",engineL=sdkL+"/bin/pgn-continuous-gpu.exe",qshaL="CC3554D5350D8C9551C2A39F66651504E089ABF5AF290DB538C93E3A884801AC",eshaL="BBF75D45431606B39F2BB5E9E7C74F5C8EF1009B0CDFBBE3458E164ADB78AFF2",policyL="GPU_OWNED_HIGH10_BARRETT70_TOP2_SHARED_PERIODIC_BANK_V1";
 auto lazy=qualified;auto lp=qualified.value("profile").toObject();lp["executable"]=engineL;lp["image_sha256"]=eshaL;lazy["profile"]=lp;
 auto lb=qb;lb["executable"]=engineL;lb["sdk"]=sdkL;lb["source_boundary_protocol"]="pgn-source-legacy-barrett70-top2-v1";lb["engine_upgrade"]=QJsonObject{{"policy",policyL},{"qualification_sha256",qshaL}};
 lb["pins"]=QJsonArray{QJsonObject{{"path",engineL},{"sha256",eshaL}},QJsonObject{{"path",sdkL+"/bin/cudart64_12.dll"},{"sha256","9D9B868955149875AC4EF43442AAAA8913D4A7B3E4D6DD60EE871626AA045768"}}};lazy["binding"]=lb;
 auto lc=cap;lc["policy"]=policyL;lc["sdk"]=sdkL;lc["qualification_sha256"]=qshaL;lazy["source_capacity"]=lc;
 const auto lazyParsed=read.decode(json(lazy),true,0);check(lazyParsed.configuredProfile&&lazyParsed.configuredProfile->sourceCapacity&&lazyParsed.configuredProfile->sourceCapacity->policy==policyL,"exact lazy capacity admitted, never launch authority");
 for(const auto* key:{"policy","sdk","scope_start","scope_end","max_length","guard","qualification_sha256"}){
  auto changed=lazy;auto c=lc;c[key]="UNKNOWN";changed["source_capacity"]=c;unknown(read,json(changed));
 }
 for(const auto* key:{"sdk","source_boundary_protocol","engine_upgrade","pins"}){
  auto changed=lazy;auto b=lb;b[key]=qb.value(key);changed["binding"]=b;unknown(read,json(changed));
 }
 }
 {
 // Exact lazy two-bit window/coarse-inverse image/capacity declaration; mixed generations remain held.
 const QString sdkL="C:/PGN/native/20261017-barrett70-v1",engineL=sdkL+"/bin/pgn-continuous-gpu.exe",qshaL="A758E586FE3F31EF40D0F07A80857758D8835B0A1E1E1594E7AE02B2490EC1CF",eshaL="1A909AB796AC1E385DEE6F1C09AB6CEB1A3A334B887E46CFBBF2D656726CB41A",policyL="GPU_OWNED_HIGH10_BARRETT70_SHARED_PERIODIC_BANK_V1";
 auto lazy=qualified;auto lp=qualified.value("profile").toObject();lp["executable"]=engineL;lp["image_sha256"]=eshaL;lazy["profile"]=lp;
 auto lb=qb;lb["executable"]=engineL;lb["sdk"]=sdkL;lb["source_boundary_protocol"]="pgn-source-legacy-barrett70-v1";lb["engine_upgrade"]=QJsonObject{{"policy",policyL},{"qualification_sha256",qshaL}};
 lb["pins"]=QJsonArray{QJsonObject{{"path",engineL},{"sha256",eshaL}},QJsonObject{{"path",sdkL+"/bin/cudart64_12.dll"},{"sha256","9D9B868955149875AC4EF43442AAAA8913D4A7B3E4D6DD60EE871626AA045768"}}};lazy["binding"]=lb;
 auto lc=cap;lc["policy"]=policyL;lc["sdk"]=sdkL;lc["qualification_sha256"]=qshaL;lazy["source_capacity"]=lc;
 const auto lazyParsed=read.decode(json(lazy),true,0);check(lazyParsed.configuredProfile&&lazyParsed.configuredProfile->sourceCapacity&&lazyParsed.configuredProfile->sourceCapacity->policy==policyL,"exact lazy capacity admitted, never launch authority");
 for(const auto* key:{"policy","sdk","scope_start","scope_end","max_length","guard","qualification_sha256"}){
  auto changed=lazy;auto c=lc;c[key]="UNKNOWN";changed["source_capacity"]=c;unknown(read,json(changed));
 }
 for(const auto* key:{"sdk","source_boundary_protocol","engine_upgrade","pins"}){
  auto changed=lazy;auto b=lb;b[key]=qb.value(key);changed["binding"]=b;unknown(read,json(changed));
 }
 }
 {
 // Exact lazy two-bit window/coarse-inverse image/capacity declaration; mixed generations remain held.
 const QString sdkL="C:/PGN/native/20261017-window2-recip5-v1",engineL=sdkL+"/bin/pgn-continuous-gpu.exe",qshaL="DB6F8E08FAA9AB0E7A89ED1305E3B298E854A680851B2418D54175F4EADA0D26",eshaL="C5FBAF75DE15631933C87419EC638CA53E3CB6B03668AD0A6B1F5B1818979566",policyL="GPU_OWNED_HIGH10_BARRETT70_WINDOW2_RECIP5_SHARED_PERIODIC_BANK_V1";
 auto lazy=qualified;auto lp=qualified.value("profile").toObject();lp["executable"]=engineL;lp["image_sha256"]=eshaL;lazy["profile"]=lp;
 auto lb=qb;lb["executable"]=engineL;lb["sdk"]=sdkL;lb["source_boundary_protocol"]="pgn-source-legacy-window2-recip5-v1";lb["engine_upgrade"]=QJsonObject{{"policy",policyL},{"qualification_sha256",qshaL}};
 lb["pins"]=QJsonArray{QJsonObject{{"path",engineL},{"sha256",eshaL}},QJsonObject{{"path",sdkL+"/bin/cudart64_12.dll"},{"sha256","9D9B868955149875AC4EF43442AAAA8913D4A7B3E4D6DD60EE871626AA045768"}}};lazy["binding"]=lb;
 auto lc=cap;lc["policy"]=policyL;lc["sdk"]=sdkL;lc["qualification_sha256"]=qshaL;lazy["source_capacity"]=lc;
 const auto lazyParsed=read.decode(json(lazy),true,0);check(lazyParsed.configuredProfile&&lazyParsed.configuredProfile->sourceCapacity&&lazyParsed.configuredProfile->sourceCapacity->policy==policyL,"exact lazy capacity admitted, never launch authority");
 for(const auto* key:{"policy","sdk","scope_start","scope_end","max_length","guard","qualification_sha256"}){
  auto changed=lazy;auto c=lc;c[key]="UNKNOWN";changed["source_capacity"]=c;unknown(read,json(changed));
 }
 for(const auto* key:{"sdk","source_boundary_protocol","engine_upgrade","pins"}){
  auto changed=lazy;auto b=lb;b[key]=qb.value(key);changed["binding"]=b;unknown(read,json(changed));
 }
 }
 {
 // Exact lazy two-bit window/coarse-inverse image/capacity declaration; mixed generations remain held.
 const QString sdkL="C:/PGN/native/20261017-barrett70-v1",engineL=sdkL+"/bin/pgn-continuous-gpu.exe",qshaL="A758E586FE3F31EF40D0F07A80857758D8835B0A1E1E1594E7AE02B2490EC1CF",eshaL="1A909AB796AC1E385DEE6F1C09AB6CEB1A3A334B887E46CFBBF2D656726CB41A",policyL="GPU_OWNED_HIGH10_BARRETT70_SHARED_PERIODIC_BANK_V1";
 auto lazy=qualified;auto lp=qualified.value("profile").toObject();lp["executable"]=engineL;lp["image_sha256"]=eshaL;lazy["profile"]=lp;
 auto lb=qb;lb["executable"]=engineL;lb["sdk"]=sdkL;lb["source_boundary_protocol"]="pgn-source-legacy-barrett70-v1";lb["engine_upgrade"]=QJsonObject{{"policy",policyL},{"qualification_sha256",qshaL}};
 lb["pins"]=QJsonArray{QJsonObject{{"path",engineL},{"sha256",eshaL}},QJsonObject{{"path",sdkL+"/bin/cudart64_12.dll"},{"sha256","9D9B868955149875AC4EF43442AAAA8913D4A7B3E4D6DD60EE871626AA045768"}}};lazy["binding"]=lb;
 auto lc=cap;lc["policy"]=policyL;lc["sdk"]=sdkL;lc["qualification_sha256"]=qshaL;lazy["source_capacity"]=lc;
 const auto lazyParsed=read.decode(json(lazy),true,0);check(lazyParsed.configuredProfile&&lazyParsed.configuredProfile->sourceCapacity&&lazyParsed.configuredProfile->sourceCapacity->policy==policyL,"exact lazy capacity admitted, never launch authority");
 for(const auto* key:{"policy","sdk","scope_start","scope_end","max_length","guard","qualification_sha256"}){
  auto changed=lazy;auto c=lc;c[key]="UNKNOWN";changed["source_capacity"]=c;unknown(read,json(changed));
 }
 for(const auto* key:{"sdk","source_boundary_protocol","engine_upgrade","pins"}){
  auto changed=lazy;auto b=lb;b[key]=qb.value(key);changed["binding"]=b;unknown(read,json(changed));
 }
 }
 {
 // Exact lazy two-bit window/coarse-inverse image/capacity declaration; mixed generations remain held.
 const QString sdkL="C:/PGN/native/20261017-rolling-roots-v1",engineL=sdkL+"/bin/pgn-continuous-gpu.exe",qshaL="419DA06E9D15A55A8452181E402F086BCDAAC5F1463A2F39C73A7A22B2C86E08",eshaL="3E800BB02F0249C315B4D84EB346EF887660E023BEB52BA57981AE4A26F8558B",policyL="GPU_OWNED_HIGH10_BARRETT70_TOP2_ROLLING_ROOTS_SHARED_PERIODIC_BANK_V1";
 auto lazy=qualified;auto lp=qualified.value("profile").toObject();lp["executable"]=engineL;lp["image_sha256"]=eshaL;lazy["profile"]=lp;
 auto lb=qb;lb["executable"]=engineL;lb["sdk"]=sdkL;lb["source_boundary_protocol"]="pgn-source-legacy-rolling-roots-v1";lb["engine_upgrade"]=QJsonObject{{"policy",policyL},{"qualification_sha256",qshaL}};
 lb["pins"]=QJsonArray{QJsonObject{{"path",engineL},{"sha256",eshaL}},QJsonObject{{"path",sdkL+"/bin/cudart64_12.dll"},{"sha256","9D9B868955149875AC4EF43442AAAA8913D4A7B3E4D6DD60EE871626AA045768"}}};lazy["binding"]=lb;
 auto lc=cap;lc["policy"]=policyL;lc["sdk"]=sdkL;lc["qualification_sha256"]=qshaL;lazy["source_capacity"]=lc;
 const auto lazyParsed=read.decode(json(lazy),true,0);check(lazyParsed.configuredProfile&&lazyParsed.configuredProfile->sourceCapacity&&lazyParsed.configuredProfile->sourceCapacity->policy==policyL,"exact lazy capacity admitted, never launch authority");
 for(const auto* key:{"policy","sdk","scope_start","scope_end","max_length","guard","qualification_sha256"}){
  auto changed=lazy;auto c=lc;c[key]="UNKNOWN";changed["source_capacity"]=c;unknown(read,json(changed));
 }
 for(const auto* key:{"sdk","source_boundary_protocol","engine_upgrade","pins"}){
  auto changed=lazy;auto b=lb;b[key]=qb.value(key);changed["binding"]=b;unknown(read,json(changed));
 }
 }
 {
 // Exact lazy two-bit window/coarse-inverse image/capacity declaration; mixed generations remain held.
 const QString sdkL="C:/PGN/native/20261017-barrett70-v1",engineL=sdkL+"/bin/pgn-continuous-gpu.exe",qshaL="A758E586FE3F31EF40D0F07A80857758D8835B0A1E1E1594E7AE02B2490EC1CF",eshaL="1A909AB796AC1E385DEE6F1C09AB6CEB1A3A334B887E46CFBBF2D656726CB41A",policyL="GPU_OWNED_HIGH10_BARRETT70_SHARED_PERIODIC_BANK_V1";
 auto lazy=qualified;auto lp=qualified.value("profile").toObject();lp["executable"]=engineL;lp["image_sha256"]=eshaL;lazy["profile"]=lp;
 auto lb=qb;lb["executable"]=engineL;lb["sdk"]=sdkL;lb["source_boundary_protocol"]="pgn-source-legacy-barrett70-v1";lb["engine_upgrade"]=QJsonObject{{"policy",policyL},{"qualification_sha256",qshaL}};
 lb["pins"]=QJsonArray{QJsonObject{{"path",engineL},{"sha256",eshaL}},QJsonObject{{"path",sdkL+"/bin/cudart64_12.dll"},{"sha256","9D9B868955149875AC4EF43442AAAA8913D4A7B3E4D6DD60EE871626AA045768"}}};lazy["binding"]=lb;
 auto lc=cap;lc["policy"]=policyL;lc["sdk"]=sdkL;lc["qualification_sha256"]=qshaL;lazy["source_capacity"]=lc;
 const auto lazyParsed=read.decode(json(lazy),true,0);check(lazyParsed.configuredProfile&&lazyParsed.configuredProfile->sourceCapacity&&lazyParsed.configuredProfile->sourceCapacity->policy==policyL,"exact lazy capacity admitted, never launch authority");
 for(const auto* key:{"policy","sdk","scope_start","scope_end","max_length","guard","qualification_sha256"}){
  auto changed=lazy;auto c=lc;c[key]="UNKNOWN";changed["source_capacity"]=c;unknown(read,json(changed));
 }
 for(const auto* key:{"sdk","source_boundary_protocol","engine_upgrade","pins"}){
  auto changed=lazy;auto b=lb;b[key]=qb.value(key);changed["binding"]=b;unknown(read,json(changed));
 }
 }
 {
 // Exact lazy two-bit window/coarse-inverse image/capacity declaration; mixed generations remain held.
 const QString sdkL="C:/PGN/native/20261017-window2-recip5-v1",engineL=sdkL+"/bin/pgn-continuous-gpu.exe",qshaL="DB6F8E08FAA9AB0E7A89ED1305E3B298E854A680851B2418D54175F4EADA0D26",eshaL="C5FBAF75DE15631933C87419EC638CA53E3CB6B03668AD0A6B1F5B1818979566",policyL="GPU_OWNED_HIGH10_BARRETT70_WINDOW2_RECIP5_SHARED_PERIODIC_BANK_V1";
 auto lazy=qualified;auto lp=qualified.value("profile").toObject();lp["executable"]=engineL;lp["image_sha256"]=eshaL;lazy["profile"]=lp;
 auto lb=qb;lb["executable"]=engineL;lb["sdk"]=sdkL;lb["source_boundary_protocol"]="pgn-source-legacy-window2-recip5-v1";lb["engine_upgrade"]=QJsonObject{{"policy",policyL},{"qualification_sha256",qshaL}};
 lb["pins"]=QJsonArray{QJsonObject{{"path",engineL},{"sha256",eshaL}},QJsonObject{{"path",sdkL+"/bin/cudart64_12.dll"},{"sha256","9D9B868955149875AC4EF43442AAAA8913D4A7B3E4D6DD60EE871626AA045768"}}};lazy["binding"]=lb;
 auto lc=cap;lc["policy"]=policyL;lc["sdk"]=sdkL;lc["qualification_sha256"]=qshaL;lazy["source_capacity"]=lc;
 const auto lazyParsed=read.decode(json(lazy),true,0);check(lazyParsed.configuredProfile&&lazyParsed.configuredProfile->sourceCapacity&&lazyParsed.configuredProfile->sourceCapacity->policy==policyL,"exact lazy capacity admitted, never launch authority");
 for(const auto* key:{"policy","sdk","scope_start","scope_end","max_length","guard","qualification_sha256"}){
  auto changed=lazy;auto c=lc;c[key]="UNKNOWN";changed["source_capacity"]=c;unknown(read,json(changed));
 }
 for(const auto* key:{"sdk","source_boundary_protocol","engine_upgrade","pins"}){
  auto changed=lazy;auto b=lb;b[key]=qb.value(key);changed["binding"]=b;unknown(read,json(changed));
 }
 }
 {
 // Exact lazy two-bit window/coarse-inverse image/capacity declaration; mixed generations remain held.
 const QString sdkL="C:/PGN/native/20261017-barrett70-v1",engineL=sdkL+"/bin/pgn-continuous-gpu.exe",qshaL="A758E586FE3F31EF40D0F07A80857758D8835B0A1E1E1594E7AE02B2490EC1CF",eshaL="1A909AB796AC1E385DEE6F1C09AB6CEB1A3A334B887E46CFBBF2D656726CB41A",policyL="GPU_OWNED_HIGH10_BARRETT70_SHARED_PERIODIC_BANK_V1";
 auto lazy=qualified;auto lp=qualified.value("profile").toObject();lp["executable"]=engineL;lp["image_sha256"]=eshaL;lazy["profile"]=lp;
 auto lb=qb;lb["executable"]=engineL;lb["sdk"]=sdkL;lb["source_boundary_protocol"]="pgn-source-legacy-barrett70-v1";lb["engine_upgrade"]=QJsonObject{{"policy",policyL},{"qualification_sha256",qshaL}};
 lb["pins"]=QJsonArray{QJsonObject{{"path",engineL},{"sha256",eshaL}},QJsonObject{{"path",sdkL+"/bin/cudart64_12.dll"},{"sha256","9D9B868955149875AC4EF43442AAAA8913D4A7B3E4D6DD60EE871626AA045768"}}};lazy["binding"]=lb;
 auto lc=cap;lc["policy"]=policyL;lc["sdk"]=sdkL;lc["qualification_sha256"]=qshaL;lazy["source_capacity"]=lc;
 const auto lazyParsed=read.decode(json(lazy),true,0);check(lazyParsed.configuredProfile&&lazyParsed.configuredProfile->sourceCapacity&&lazyParsed.configuredProfile->sourceCapacity->policy==policyL,"exact lazy capacity admitted, never launch authority");
 for(const auto* key:{"policy","sdk","scope_start","scope_end","max_length","guard","qualification_sha256"}){
  auto changed=lazy;auto c=lc;c[key]="UNKNOWN";changed["source_capacity"]=c;unknown(read,json(changed));
 }
 for(const auto* key:{"sdk","source_boundary_protocol","engine_upgrade","pins"}){
  auto changed=lazy;auto b=lb;b[key]=qb.value(key);changed["binding"]=b;unknown(read,json(changed));
 }
 }
 {
 // Exact lazy two-bit window/coarse-inverse image/capacity declaration; mixed generations remain held.
 const QString sdkL="C:/PGN/native/20261017-barrett70-top2-v1",engineL=sdkL+"/bin/pgn-continuous-gpu.exe",qshaL="CC3554D5350D8C9551C2A39F66651504E089ABF5AF290DB538C93E3A884801AC",eshaL="BBF75D45431606B39F2BB5E9E7C74F5C8EF1009B0CDFBBE3458E164ADB78AFF2",policyL="GPU_OWNED_HIGH10_BARRETT70_TOP2_SHARED_PERIODIC_BANK_V1";
 auto lazy=qualified;auto lp=qualified.value("profile").toObject();lp["executable"]=engineL;lp["image_sha256"]=eshaL;lazy["profile"]=lp;
 auto lb=qb;lb["executable"]=engineL;lb["sdk"]=sdkL;lb["source_boundary_protocol"]="pgn-source-legacy-barrett70-top2-v1";lb["engine_upgrade"]=QJsonObject{{"policy",policyL},{"qualification_sha256",qshaL}};
 lb["pins"]=QJsonArray{QJsonObject{{"path",engineL},{"sha256",eshaL}},QJsonObject{{"path",sdkL+"/bin/cudart64_12.dll"},{"sha256","9D9B868955149875AC4EF43442AAAA8913D4A7B3E4D6DD60EE871626AA045768"}}};lazy["binding"]=lb;
 auto lc=cap;lc["policy"]=policyL;lc["sdk"]=sdkL;lc["qualification_sha256"]=qshaL;lazy["source_capacity"]=lc;
 const auto lazyParsed=read.decode(json(lazy),true,0);check(lazyParsed.configuredProfile&&lazyParsed.configuredProfile->sourceCapacity&&lazyParsed.configuredProfile->sourceCapacity->policy==policyL,"exact lazy capacity admitted, never launch authority");
 for(const auto* key:{"policy","sdk","scope_start","scope_end","max_length","guard","qualification_sha256"}){
  auto changed=lazy;auto c=lc;c[key]="UNKNOWN";changed["source_capacity"]=c;unknown(read,json(changed));
 }
 for(const auto* key:{"sdk","source_boundary_protocol","engine_upgrade","pins"}){
  auto changed=lazy;auto b=lb;b[key]=qb.value(key);changed["binding"]=b;unknown(read,json(changed));
 }
 }
 {
 // Exact lazy two-bit window/coarse-inverse image/capacity declaration; mixed generations remain held.
 const QString sdkL="C:/PGN/native/20261017-barrett70-v1",engineL=sdkL+"/bin/pgn-continuous-gpu.exe",qshaL="A758E586FE3F31EF40D0F07A80857758D8835B0A1E1E1594E7AE02B2490EC1CF",eshaL="1A909AB796AC1E385DEE6F1C09AB6CEB1A3A334B887E46CFBBF2D656726CB41A",policyL="GPU_OWNED_HIGH10_BARRETT70_SHARED_PERIODIC_BANK_V1";
 auto lazy=qualified;auto lp=qualified.value("profile").toObject();lp["executable"]=engineL;lp["image_sha256"]=eshaL;lazy["profile"]=lp;
 auto lb=qb;lb["executable"]=engineL;lb["sdk"]=sdkL;lb["source_boundary_protocol"]="pgn-source-legacy-barrett70-v1";lb["engine_upgrade"]=QJsonObject{{"policy",policyL},{"qualification_sha256",qshaL}};
 lb["pins"]=QJsonArray{QJsonObject{{"path",engineL},{"sha256",eshaL}},QJsonObject{{"path",sdkL+"/bin/cudart64_12.dll"},{"sha256","9D9B868955149875AC4EF43442AAAA8913D4A7B3E4D6DD60EE871626AA045768"}}};lazy["binding"]=lb;
 auto lc=cap;lc["policy"]=policyL;lc["sdk"]=sdkL;lc["qualification_sha256"]=qshaL;lazy["source_capacity"]=lc;
 const auto lazyParsed=read.decode(json(lazy),true,0);check(lazyParsed.configuredProfile&&lazyParsed.configuredProfile->sourceCapacity&&lazyParsed.configuredProfile->sourceCapacity->policy==policyL,"exact lazy capacity admitted, never launch authority");
 for(const auto* key:{"policy","sdk","scope_start","scope_end","max_length","guard","qualification_sha256"}){
  auto changed=lazy;auto c=lc;c[key]="UNKNOWN";changed["source_capacity"]=c;unknown(read,json(changed));
 }
 for(const auto* key:{"sdk","source_boundary_protocol","engine_upgrade","pins"}){
  auto changed=lazy;auto b=lb;b[key]=qb.value(key);changed["binding"]=b;unknown(read,json(changed));
 }
 }
 {
 // Exact lazy two-bit window/coarse-inverse image/capacity declaration; mixed generations remain held.
 const QString sdkL="C:/PGN/native/20261017-window2-recip5-v1",engineL=sdkL+"/bin/pgn-continuous-gpu.exe",qshaL="DB6F8E08FAA9AB0E7A89ED1305E3B298E854A680851B2418D54175F4EADA0D26",eshaL="C5FBAF75DE15631933C87419EC638CA53E3CB6B03668AD0A6B1F5B1818979566",policyL="GPU_OWNED_HIGH10_BARRETT70_WINDOW2_RECIP5_SHARED_PERIODIC_BANK_V1";
 auto lazy=qualified;auto lp=qualified.value("profile").toObject();lp["executable"]=engineL;lp["image_sha256"]=eshaL;lazy["profile"]=lp;
 auto lb=qb;lb["executable"]=engineL;lb["sdk"]=sdkL;lb["source_boundary_protocol"]="pgn-source-legacy-window2-recip5-v1";lb["engine_upgrade"]=QJsonObject{{"policy",policyL},{"qualification_sha256",qshaL}};
 lb["pins"]=QJsonArray{QJsonObject{{"path",engineL},{"sha256",eshaL}},QJsonObject{{"path",sdkL+"/bin/cudart64_12.dll"},{"sha256","9D9B868955149875AC4EF43442AAAA8913D4A7B3E4D6DD60EE871626AA045768"}}};lazy["binding"]=lb;
 auto lc=cap;lc["policy"]=policyL;lc["sdk"]=sdkL;lc["qualification_sha256"]=qshaL;lazy["source_capacity"]=lc;
 const auto lazyParsed=read.decode(json(lazy),true,0);check(lazyParsed.configuredProfile&&lazyParsed.configuredProfile->sourceCapacity&&lazyParsed.configuredProfile->sourceCapacity->policy==policyL,"exact lazy capacity admitted, never launch authority");
 for(const auto* key:{"policy","sdk","scope_start","scope_end","max_length","guard","qualification_sha256"}){
  auto changed=lazy;auto c=lc;c[key]="UNKNOWN";changed["source_capacity"]=c;unknown(read,json(changed));
 }
 for(const auto* key:{"sdk","source_boundary_protocol","engine_upgrade","pins"}){
  auto changed=lazy;auto b=lb;b[key]=qb.value(key);changed["binding"]=b;unknown(read,json(changed));
 }
 }
 {
 // Exact lazy two-bit window/coarse-inverse image/capacity declaration; mixed generations remain held.
 const QString sdkL="C:/PGN/native/20261017-barrett70-v1",engineL=sdkL+"/bin/pgn-continuous-gpu.exe",qshaL="A758E586FE3F31EF40D0F07A80857758D8835B0A1E1E1594E7AE02B2490EC1CF",eshaL="1A909AB796AC1E385DEE6F1C09AB6CEB1A3A334B887E46CFBBF2D656726CB41A",policyL="GPU_OWNED_HIGH10_BARRETT70_SHARED_PERIODIC_BANK_V1";
 auto lazy=qualified;auto lp=qualified.value("profile").toObject();lp["executable"]=engineL;lp["image_sha256"]=eshaL;lazy["profile"]=lp;
 auto lb=qb;lb["executable"]=engineL;lb["sdk"]=sdkL;lb["source_boundary_protocol"]="pgn-source-legacy-barrett70-v1";lb["engine_upgrade"]=QJsonObject{{"policy",policyL},{"qualification_sha256",qshaL}};
 lb["pins"]=QJsonArray{QJsonObject{{"path",engineL},{"sha256",eshaL}},QJsonObject{{"path",sdkL+"/bin/cudart64_12.dll"},{"sha256","9D9B868955149875AC4EF43442AAAA8913D4A7B3E4D6DD60EE871626AA045768"}}};lazy["binding"]=lb;
 auto lc=cap;lc["policy"]=policyL;lc["sdk"]=sdkL;lc["qualification_sha256"]=qshaL;lazy["source_capacity"]=lc;
 const auto lazyParsed=read.decode(json(lazy),true,0);check(lazyParsed.configuredProfile&&lazyParsed.configuredProfile->sourceCapacity&&lazyParsed.configuredProfile->sourceCapacity->policy==policyL,"exact lazy capacity admitted, never launch authority");
 for(const auto* key:{"policy","sdk","scope_start","scope_end","max_length","guard","qualification_sha256"}){
  auto changed=lazy;auto c=lc;c[key]="UNKNOWN";changed["source_capacity"]=c;unknown(read,json(changed));
 }
 for(const auto* key:{"sdk","source_boundary_protocol","engine_upgrade","pins"}){
  auto changed=lazy;auto b=lb;b[key]=qb.value(key);changed["binding"]=b;unknown(read,json(changed));
 }
 }
 {
 // Exact lazy two-bit window/coarse-inverse image/capacity declaration; mixed generations remain held.
 const QString sdkL="C:/PGN/native/20261017-rolling-roots-lanes2-v1",engineL=sdkL+"/bin/pgn-continuous-gpu.exe",qshaL="5C18FB2BAFB97E2743E804C2B55B46882FE0FC913983817AAFFEAD039672BDC2",eshaL="2876A22CB28805062D3F01423E56C918B60B6A4E09C14A888A7333C98D697488",policyL="GPU_OWNED_HIGH10_BARRETT70_TOP2_ROLLING_ROOTS_TWO_LANES_SHARED_PERIODIC_BANK_V1";
 auto lazy=qualified;auto lp=qualified.value("profile").toObject();lp["executable"]=engineL;lp["image_sha256"]=eshaL;lazy["profile"]=lp;
 auto lb=qb;lb["executable"]=engineL;lb["sdk"]=sdkL;lb["source_boundary_protocol"]="pgn-source-legacy-rolling-roots-lanes2-v1";lb["engine_upgrade"]=QJsonObject{{"policy",policyL},{"qualification_sha256",qshaL}};
 lb["pins"]=QJsonArray{QJsonObject{{"path",engineL},{"sha256",eshaL}},QJsonObject{{"path",sdkL+"/bin/cudart64_12.dll"},{"sha256","9D9B868955149875AC4EF43442AAAA8913D4A7B3E4D6DD60EE871626AA045768"}}};lazy["binding"]=lb;
 auto lc=cap;lc["policy"]=policyL;lc["sdk"]=sdkL;lc["qualification_sha256"]=qshaL;lazy["source_capacity"]=lc;
 const auto lazyParsed=read.decode(json(lazy),true,0);check(lazyParsed.configuredProfile&&lazyParsed.configuredProfile->sourceCapacity&&lazyParsed.configuredProfile->sourceCapacity->policy==policyL,"exact lazy capacity admitted, never launch authority");
 for(const auto* key:{"policy","sdk","scope_start","scope_end","max_length","guard","qualification_sha256"}){
  auto changed=lazy;auto c=lc;c[key]="UNKNOWN";changed["source_capacity"]=c;unknown(read,json(changed));
 }
 for(const auto* key:{"sdk","source_boundary_protocol","engine_upgrade","pins"}){
  auto changed=lazy;auto b=lb;b[key]=qb.value(key);changed["binding"]=b;unknown(read,json(changed));
 }
 }
 {
 // Exact qualified carry-chain image/capacity declaration; mixed generations remain held.
 const QString sdkL="C:/PGN/native/20261018-carry-chain-v1",engineL=sdkL+"/bin/pgn-continuous-gpu.exe",qshaL="9DF2C6B6605792A9E15D9D4BA627521178FAD0686655559975768A31B482A9AF",eshaL="65C7B566B76D6A90CD94C75B2D31BF8C516074F30CCE8E5EA307363816F82B8D",policyL="GPU_OWNED_HIGH10_BARRETT70_TOP2_ROLLING_ROOTS_TWO_LANES_CARRY_V1";
 auto lazy=qualified;auto lp=qualified.value("profile").toObject();lp["executable"]=engineL;lp["image_sha256"]=eshaL;lazy["profile"]=lp;
 auto lb=qb;lb["executable"]=engineL;lb["sdk"]=sdkL;lb["source_boundary_protocol"]="pgn-source-legacy-carry-chain-v1";lb["engine_upgrade"]=QJsonObject{{"policy",policyL},{"qualification_sha256",qshaL}};
 lb["pins"]=QJsonArray{QJsonObject{{"path",engineL},{"sha256",eshaL}},QJsonObject{{"path",sdkL+"/bin/cudart64_12.dll"},{"sha256","9D9B868955149875AC4EF43442AAAA8913D4A7B3E4D6DD60EE871626AA045768"}}};lazy["binding"]=lb;
 auto lc=cap;lc["policy"]=policyL;lc["sdk"]=sdkL;lc["qualification_sha256"]=qshaL;lazy["source_capacity"]=lc;
 const auto lazyParsed=read.decode(json(lazy),true,0);check(lazyParsed.configuredProfile&&lazyParsed.configuredProfile->sourceCapacity&&lazyParsed.configuredProfile->sourceCapacity->policy==policyL,"exact lazy capacity admitted, never launch authority");
 for(const auto* key:{"policy","sdk","scope_start","scope_end","max_length","guard","qualification_sha256"}){
  auto changed=lazy;auto c=lc;c[key]="UNKNOWN";changed["source_capacity"]=c;unknown(read,json(changed));
 }
 for(const auto* key:{"sdk","source_boundary_protocol","engine_upgrade","pins"}){
  auto changed=lazy;auto b=lb;b[key]=qb.value(key);changed["binding"]=b;unknown(read,json(changed));
 }
 }
 {
 // Exact qualified fused-square image/capacity declaration; mixed generations remain held.
 const QString sdkL="C:/PGN/native/20261018-fused-square-v1",engineL=sdkL+"/bin/pgn-continuous-gpu.exe",qshaL="57503A214735EA72F336EA15EAF02418F3921C7622483B2ACDBCED10CF3D3B18",eshaL="12E1C0A2D43955F51A21ACA6FF41DED562F44BDBDA7B29A303CE3BA1C26FED59",policyL="GPU_OWNED_HIGH10_BARRETT70_TOP2_ROLLING_ROOTS_TWO_LANES_FUSED_SQUARE_V1";
 auto lazy=qualified;auto lp=qualified.value("profile").toObject();lp["executable"]=engineL;lp["image_sha256"]=eshaL;lazy["profile"]=lp;
 auto lb=qb;lb["executable"]=engineL;lb["sdk"]=sdkL;lb["source_boundary_protocol"]="pgn-source-legacy-fused-square-v1";lb["engine_upgrade"]=QJsonObject{{"policy",policyL},{"qualification_sha256",qshaL}};
 lb["pins"]=QJsonArray{QJsonObject{{"path",engineL},{"sha256",eshaL}},QJsonObject{{"path",sdkL+"/bin/cudart64_12.dll"},{"sha256","9D9B868955149875AC4EF43442AAAA8913D4A7B3E4D6DD60EE871626AA045768"}}};lazy["binding"]=lb;
 auto lc=cap;lc["policy"]=policyL;lc["sdk"]=sdkL;lc["qualification_sha256"]=qshaL;lazy["source_capacity"]=lc;
 const auto lazyParsed=read.decode(json(lazy),true,0);check(lazyParsed.configuredProfile&&lazyParsed.configuredProfile->sourceCapacity&&lazyParsed.configuredProfile->sourceCapacity->policy==policyL,"exact lazy capacity admitted, never launch authority");
 for(const auto* key:{"policy","sdk","scope_start","scope_end","max_length","guard","qualification_sha256"}){
  auto changed=lazy;auto c=lc;c[key]="UNKNOWN";changed["source_capacity"]=c;unknown(read,json(changed));
 }
 for(const auto* key:{"sdk","source_boundary_protocol","engine_upgrade","pins"}){
  auto changed=lazy;auto b=lb;b[key]=qb.value(key);changed["binding"]=b;unknown(read,json(changed));
 }
 }
 {
 // Exact qualified window3-ring image/capacity declaration; mixed generations remain held.
 const QString sdkL="C:/PGN/native/20261018-fused-window3-ring-v1",engineL=sdkL+"/bin/pgn-continuous-gpu.exe",qshaL="9B560C15DABBB26BFE22FD4C9D0041AF17B68C273F0211FD45ACEC4C98BF3415",eshaL="42DEA3F63AF76512D2799248696D4F1F22FDD4D54AAD803C2ABE1367D29302A1",policyL="GPU_OWNED_HIGH10_FUSED_WINDOW3_RING_V1";
 auto lazy=qualified;auto lp=qualified.value("profile").toObject();lp["executable"]=engineL;lp["image_sha256"]=eshaL;lazy["profile"]=lp;
 auto lb=qb;lb["executable"]=engineL;lb["sdk"]=sdkL;lb["source_boundary_protocol"]="pgn-source-legacy-fused-window3-ring-v1";lb["engine_upgrade"]=QJsonObject{{"policy",policyL},{"qualification_sha256",qshaL}};
 lb["pins"]=QJsonArray{QJsonObject{{"path",engineL},{"sha256",eshaL}},QJsonObject{{"path",sdkL+"/bin/cudart64_12.dll"},{"sha256","9D9B868955149875AC4EF43442AAAA8913D4A7B3E4D6DD60EE871626AA045768"}}};lazy["binding"]=lb;
 auto lc=cap;lc["policy"]=policyL;lc["sdk"]=sdkL;lc["qualification_sha256"]=qshaL;lazy["source_capacity"]=lc;
 const auto lazyParsed=read.decode(json(lazy),true,0);check(lazyParsed.configuredProfile&&lazyParsed.configuredProfile->sourceCapacity&&lazyParsed.configuredProfile->sourceCapacity->policy==policyL,"exact lazy capacity admitted, never launch authority");
 for(const auto* key:{"policy","sdk","scope_start","scope_end","max_length","guard","qualification_sha256"}){
  auto changed=lazy;auto c=lc;c[key]="UNKNOWN";changed["source_capacity"]=c;unknown(read,json(changed));
 }
 for(const auto* key:{"sdk","source_boundary_protocol","engine_upgrade","pins"}){
  auto changed=lazy;auto b=lb;b[key]=qb.value(key);changed["binding"]=b;unknown(read,json(changed));
 }
 }
 {
 // Exact qualified seed8r image in the unchanged window3 wire family/capacity declaration; mixed generations remain held.
 const QString sdkL="C:/PGN/native/20261018-fused-window3-seed8r-v1",engineL=sdkL+"/bin/pgn-continuous-gpu.exe",qshaL="352F5A43AD79FE24A49F4EB1DECF6D1D7A3A810301D5CD73F48E4CC9691C7548",eshaL="CA363CB76CBAF9A008402E0D47B80DC6DC846C331EFA1BD42DAF91DE15DFC6B3",policyL="GPU_OWNED_HIGH10_FUSED_WINDOW3_RING_V1";
 auto lazy=qualified;auto lp=qualified.value("profile").toObject();lp["executable"]=engineL;lp["image_sha256"]=eshaL;lazy["profile"]=lp;
 auto lb=qb;lb["executable"]=engineL;lb["sdk"]=sdkL;lb["source_boundary_protocol"]="pgn-source-legacy-fused-window3-ring-v1";lb["engine_upgrade"]=QJsonObject{{"policy",policyL},{"qualification_sha256",qshaL}};
 lb["pins"]=QJsonArray{QJsonObject{{"path",engineL},{"sha256",eshaL}},QJsonObject{{"path",sdkL+"/bin/cudart64_12.dll"},{"sha256","9D9B868955149875AC4EF43442AAAA8913D4A7B3E4D6DD60EE871626AA045768"}}};lazy["binding"]=lb;
 auto lc=cap;lc["policy"]=policyL;lc["sdk"]=sdkL;lc["qualification_sha256"]=qshaL;lazy["source_capacity"]=lc;
 const auto lazyParsed=read.decode(json(lazy),true,0);check(lazyParsed.configuredProfile&&lazyParsed.configuredProfile->sourceCapacity&&lazyParsed.configuredProfile->sourceCapacity->policy==policyL&&lazyParsed.configuredProfile->sourceCapacity->sdk==sdkL,"exact lazy capacity admitted, never launch authority");
 for(const auto* key:{"policy","sdk","scope_start","scope_end","max_length","guard","qualification_sha256"}){
  auto changed=lazy;auto c=lc;c[key]="UNKNOWN";changed["source_capacity"]=c;unknown(read,json(changed));
 }
 for(const auto* key:{"sdk","source_boundary_protocol","engine_upgrade","pins"}){
  auto changed=lazy;auto b=lb;b[key]=qb.value(key);changed["binding"]=b;unknown(read,json(changed));
 }
 }
 {
 // Exact qualified frontier16/retry image in the unchanged window3 wire family/capacity declaration; mixed generations remain held.
 const QString sdkL="C:/PGN/native/20261019-frontier16-retry-v2",engineL=sdkL+"/bin/pgn-continuous-gpu.exe",qshaL="9C2C3ED3357A077E8CCD7F60CB400F39DDF4CF25624ED5CCE80D46ABB62B9249",eshaL="122ABD3DA35ADFCE429325F3ED0E16CCA8B38B78BC144A11722A9AC342793952",policyL="GPU_OWNED_HIGH10_FUSED_WINDOW3_RING_V1";
 auto lazy=qualified;auto lp=qualified.value("profile").toObject();lp["executable"]=engineL;lp["image_sha256"]=eshaL;lazy["profile"]=lp;
 auto lb=qb;lb["executable"]=engineL;lb["sdk"]=sdkL;lb["source_boundary_protocol"]="pgn-source-legacy-fused-window3-ring-v1";lb["engine_upgrade"]=QJsonObject{{"policy",policyL},{"qualification_sha256",qshaL}};
 lb["pins"]=QJsonArray{QJsonObject{{"path",engineL},{"sha256",eshaL}},QJsonObject{{"path",sdkL+"/bin/cudart64_12.dll"},{"sha256","9D9B868955149875AC4EF43442AAAA8913D4A7B3E4D6DD60EE871626AA045768"}}};lazy["binding"]=lb;
 auto lc=cap;lc["policy"]=policyL;lc["sdk"]=sdkL;lc["qualification_sha256"]=qshaL;lazy["source_capacity"]=lc;
 const auto lazyParsed=read.decode(json(lazy),true,0);check(lazyParsed.configuredProfile&&lazyParsed.configuredProfile->sourceCapacity&&lazyParsed.configuredProfile->sourceCapacity->policy==policyL&&lazyParsed.configuredProfile->sourceCapacity->sdk==sdkL,"exact lazy capacity admitted, never launch authority");
 for(const auto* key:{"policy","sdk","scope_start","scope_end","max_length","guard","qualification_sha256"}){
  auto changed=lazy;auto c=lc;c[key]="UNKNOWN";changed["source_capacity"]=c;unknown(read,json(changed));
 }
 for(const auto* key:{"sdk","source_boundary_protocol","engine_upgrade","pins"}){
  auto changed=lazy;auto b=lb;b[key]=qb.value(key);changed["binding"]=b;unknown(read,json(changed));
 }
 }
 {
 // Exact qualified event210 hybrid image in the unchanged window3 wire family/capacity declaration; mixed generations remain held.
 const QString sdkL="C:/PGN/native/20261019-frontier16-event210-hybrid-v1",engineL=sdkL+"/bin/pgn-continuous-gpu.exe",qshaL="6598C50D1AA41ECF97F4006ED5721E767399EB55C2EBB4A6F630FA26E13D5AEF",eshaL="3121A85578AEBC61648311216A468574745447A52FED7088285D2222C029D1E3",policyL="GPU_OWNED_HIGH10_FUSED_WINDOW3_RING_V1";
 auto lazy=qualified;auto lp=qualified.value("profile").toObject();lp["executable"]=engineL;lp["image_sha256"]=eshaL;lazy["profile"]=lp;
 auto lb=qb;lb["executable"]=engineL;lb["sdk"]=sdkL;lb["source_boundary_protocol"]="pgn-source-legacy-fused-window3-ring-v1";lb["engine_upgrade"]=QJsonObject{{"policy",policyL},{"qualification_sha256",qshaL}};
 lb["pins"]=QJsonArray{QJsonObject{{"path",engineL},{"sha256",eshaL}},QJsonObject{{"path",sdkL+"/bin/cudart64_12.dll"},{"sha256","9D9B868955149875AC4EF43442AAAA8913D4A7B3E4D6DD60EE871626AA045768"}}};lazy["binding"]=lb;
 auto lc=cap;lc["policy"]=policyL;lc["sdk"]=sdkL;lc["qualification_sha256"]=qshaL;lazy["source_capacity"]=lc;
 const auto lazyParsed=read.decode(json(lazy),true,0);check(lazyParsed.configuredProfile&&lazyParsed.configuredProfile->sourceCapacity&&lazyParsed.configuredProfile->sourceCapacity->policy==policyL&&lazyParsed.configuredProfile->sourceCapacity->sdk==sdkL,"exact lazy capacity admitted, never launch authority");
 for(const auto* key:{"policy","sdk","scope_start","scope_end","max_length","guard","qualification_sha256"}){
  auto changed=lazy;auto c=lc;c[key]="UNKNOWN";changed["source_capacity"]=c;unknown(read,json(changed));
 }
 for(const auto* key:{"sdk","source_boundary_protocol","engine_upgrade","pins"}){
  auto changed=lazy;auto b=lb;b[key]=qb.value(key);changed["binding"]=b;unknown(read,json(changed));
 }
 }
 {
 // Exact qualified event210 composed image in the unchanged window3 wire family/capacity declaration; mixed generations remain held.
 const QString sdkL="C:/PGN/native/20261019-frontier16-event210-composed-v1",engineL=sdkL+"/bin/pgn-continuous-gpu.exe",qshaL="35EE4605BF309A96973E0D1DF875E29382B0533A6AEBFECA357349C49E1E8227",eshaL="B0439859530229D2C3451690C3A0361A78FE570CDFF14F131EFB1B8A22253C43",policyL="GPU_OWNED_HIGH10_FUSED_WINDOW3_RING_V1";
 auto lazy=qualified;auto lp=qualified.value("profile").toObject();lp["executable"]=engineL;lp["image_sha256"]=eshaL;lazy["profile"]=lp;
 auto lb=qb;lb["executable"]=engineL;lb["sdk"]=sdkL;lb["source_boundary_protocol"]="pgn-source-legacy-fused-window3-ring-v1";lb["engine_upgrade"]=QJsonObject{{"policy",policyL},{"qualification_sha256",qshaL}};
 lb["pins"]=QJsonArray{QJsonObject{{"path",engineL},{"sha256",eshaL}},QJsonObject{{"path",sdkL+"/bin/cudart64_12.dll"},{"sha256","9D9B868955149875AC4EF43442AAAA8913D4A7B3E4D6DD60EE871626AA045768"}}};lazy["binding"]=lb;
 auto lc=cap;lc["policy"]=policyL;lc["sdk"]=sdkL;lc["qualification_sha256"]=qshaL;lazy["source_capacity"]=lc;
 const auto lazyParsed=read.decode(json(lazy),true,0);check(lazyParsed.configuredProfile&&lazyParsed.configuredProfile->sourceCapacity&&lazyParsed.configuredProfile->sourceCapacity->policy==policyL&&lazyParsed.configuredProfile->sourceCapacity->sdk==sdkL,"exact lazy capacity admitted, never launch authority");
 for(const auto* key:{"policy","sdk","scope_start","scope_end","max_length","guard","qualification_sha256"}){
  auto changed=lazy;auto c=lc;c[key]="UNKNOWN";changed["source_capacity"]=c;unknown(read,json(changed));
 }
 for(const auto* key:{"sdk","source_boundary_protocol","engine_upgrade","pins"}){
  auto changed=lazy;auto b=lb;b[key]=qb.value(key);changed["binding"]=b;unknown(read,json(changed));
 }
 }
 {
 // Exact qualified smallword7 image in the unchanged window3 wire family/capacity declaration; mixed generations remain held.
 const QString sdkL="C:/PGN/native/20261019-event210-smallword7-v1",engineL=sdkL+"/bin/pgn-continuous-gpu.exe",qshaL="DC503F08ACDC55469F1AF482B1300CB611E177BBA6FD0F5AFD1BBDE6D2E1DDCB",eshaL="85412118A5686090FA612C090C6C43DC9222231E4AC08A39BC594F4D4235A945",policyL="GPU_OWNED_HIGH10_FUSED_WINDOW3_RING_V1";
 auto lazy=qualified;auto lp=qualified.value("profile").toObject();lp["executable"]=engineL;lp["image_sha256"]=eshaL;lazy["profile"]=lp;
 auto lb=qb;lb["executable"]=engineL;lb["sdk"]=sdkL;lb["source_boundary_protocol"]="pgn-source-legacy-fused-window3-ring-v1";lb["engine_upgrade"]=QJsonObject{{"policy",policyL},{"qualification_sha256",qshaL}};
 lb["pins"]=QJsonArray{QJsonObject{{"path",engineL},{"sha256",eshaL}},QJsonObject{{"path",sdkL+"/bin/cudart64_12.dll"},{"sha256","9D9B868955149875AC4EF43442AAAA8913D4A7B3E4D6DD60EE871626AA045768"}}};lazy["binding"]=lb;
 auto lc=cap;lc["policy"]=policyL;lc["sdk"]=sdkL;lc["qualification_sha256"]=qshaL;lazy["source_capacity"]=lc;
 const auto lazyParsed=read.decode(json(lazy),true,0);check(lazyParsed.configuredProfile&&lazyParsed.configuredProfile->sourceCapacity&&lazyParsed.configuredProfile->sourceCapacity->policy==policyL&&lazyParsed.configuredProfile->sourceCapacity->sdk==sdkL,"exact lazy capacity admitted, never launch authority");
 for(const auto* key:{"policy","sdk","scope_start","scope_end","max_length","guard","qualification_sha256"}){
  auto changed=lazy;auto c=lc;c[key]="UNKNOWN";changed["source_capacity"]=c;unknown(read,json(changed));
 }
 for(const auto* key:{"sdk","source_boundary_protocol","engine_upgrade","pins"}){
  auto changed=lazy;auto b=lb;b[key]=qb.value(key);changed["binding"]=b;unknown(read,json(changed));
 }
 }
 {
 // Exact qualified paired dense events image in the unchanged window3 wire family/capacity declaration; mixed generations remain held.
 const QString sdkL="C:/PGN/native/20261019-event-pair16-v1",engineL=sdkL+"/bin/pgn-continuous-gpu.exe",qshaL="6FF25670C9903DD8F958821CEA5A3960ADAEC3CA259D63ECC1AAB03681A7CB86",eshaL="843BCADBAA279A19E5CD0D6C54ABD386DDEF47B10B1F2E41A8DA32C6390A0428",policyL="GPU_OWNED_HIGH10_FUSED_WINDOW3_RING_V1";
 auto lazy=qualified;auto lp=qualified.value("profile").toObject();lp["executable"]=engineL;lp["image_sha256"]=eshaL;lazy["profile"]=lp;
 auto lb=qb;lb["executable"]=engineL;lb["sdk"]=sdkL;lb["source_boundary_protocol"]="pgn-source-legacy-fused-window3-ring-v1";lb["engine_upgrade"]=QJsonObject{{"policy",policyL},{"qualification_sha256",qshaL}};
 lb["pins"]=QJsonArray{QJsonObject{{"path",engineL},{"sha256",eshaL}},QJsonObject{{"path",sdkL+"/bin/cudart64_12.dll"},{"sha256","9D9B868955149875AC4EF43442AAAA8913D4A7B3E4D6DD60EE871626AA045768"}}};lazy["binding"]=lb;
 auto lc=cap;lc["policy"]=policyL;lc["sdk"]=sdkL;lc["qualification_sha256"]=qshaL;lazy["source_capacity"]=lc;
 const auto lazyParsed=read.decode(json(lazy),true,0);check(lazyParsed.configuredProfile&&lazyParsed.configuredProfile->sourceCapacity&&lazyParsed.configuredProfile->sourceCapacity->policy==policyL&&lazyParsed.configuredProfile->sourceCapacity->sdk==sdkL,"exact lazy capacity admitted, never launch authority");
 for(const auto* key:{"policy","sdk","scope_start","scope_end","max_length","guard","qualification_sha256"}){
  auto changed=lazy;auto c=lc;c[key]="UNKNOWN";changed["source_capacity"]=c;unknown(read,json(changed));
 }
 for(const auto* key:{"sdk","source_boundary_protocol","engine_upgrade","pins"}){
  auto changed=lazy;auto b=lb;b[key]=qb.value(key);changed["binding"]=b;unknown(read,json(changed));
 }
 }
 {
 // Exact qualified prefix11 final image in the unchanged window3 wire family/capacity declaration; mixed generations remain held.
 const QString sdkL="C:/PGN/native/20261019-prefix11-final-v1",engineL=sdkL+"/bin/pgn-continuous-gpu.exe",qshaL="B9CD21629CFEB42E95C829E862E8B7300CC2CF0460BD3C691B3C4DBB08943F30",eshaL="CF3C215BF30140F89D04324377F64694D106852120634A47AF6B08558A6294ED",policyL="GPU_OWNED_HIGH10_FUSED_WINDOW3_RING_V1";
 auto lazy=qualified;auto lp=qualified.value("profile").toObject();lp["executable"]=engineL;lp["image_sha256"]=eshaL;lazy["profile"]=lp;
 auto lb=qb;lb["executable"]=engineL;lb["sdk"]=sdkL;lb["source_boundary_protocol"]="pgn-source-legacy-fused-window3-ring-v1";lb["engine_upgrade"]=QJsonObject{{"policy",policyL},{"qualification_sha256",qshaL}};
 lb["pins"]=QJsonArray{QJsonObject{{"path",engineL},{"sha256",eshaL}},QJsonObject{{"path",sdkL+"/bin/cudart64_12.dll"},{"sha256","9D9B868955149875AC4EF43442AAAA8913D4A7B3E4D6DD60EE871626AA045768"}}};lazy["binding"]=lb;
 auto lc=cap;lc["policy"]=policyL;lc["sdk"]=sdkL;lc["qualification_sha256"]=qshaL;lazy["source_capacity"]=lc;
 const auto lazyParsed=read.decode(json(lazy),true,0);check(lazyParsed.configuredProfile&&lazyParsed.configuredProfile->sourceCapacity&&lazyParsed.configuredProfile->sourceCapacity->policy==policyL&&lazyParsed.configuredProfile->sourceCapacity->sdk==sdkL,"exact lazy capacity admitted, never launch authority");
 for(const auto* key:{"policy","sdk","scope_start","scope_end","max_length","guard","qualification_sha256"}){
  auto changed=lazy;auto c=lc;c[key]="UNKNOWN";changed["source_capacity"]=c;unknown(read,json(changed));
 }
 for(const auto* key:{"sdk","source_boundary_protocol","engine_upgrade","pins"}){
  auto changed=lazy;auto b=lb;b[key]=qb.value(key);changed["binding"]=b;unknown(read,json(changed));
 }
 }
 {
 // Exact qualified live reciprocal image in the unchanged window3 wire family/capacity declaration; mixed generations remain held.
 const QString sdkL="C:/PGN/native/20261020-stateful-u160-live-v1",engineL=sdkL+"/bin/pgn-continuous-gpu.exe",qshaL="F71920DE3A8246BBB3D552107BD6D9BBE6F0049E485923098C7A20438C0BF4AB",eshaL="CCD3D4FDA96B8C26B74947B6FC8399D5502EFBF55A4AEFCABFC92659C6277673",policyL="GPU_OWNED_HIGH10_FUSED_WINDOW3_RING_V1";
 auto lazy=qualified;auto lp=qualified.value("profile").toObject();lp["executable"]=engineL;lp["image_sha256"]=eshaL;lazy["profile"]=lp;
 auto lb=qb;lb["executable"]=engineL;lb["sdk"]=sdkL;lb["source_boundary_protocol"]="pgn-source-legacy-fused-window3-ring-v1";lb["engine_upgrade"]=QJsonObject{{"policy",policyL},{"qualification_sha256",qshaL}};
 lb["pins"]=QJsonArray{QJsonObject{{"path",engineL},{"sha256",eshaL}},QJsonObject{{"path",sdkL+"/bin/cudart64_12.dll"},{"sha256","9D9B868955149875AC4EF43442AAAA8913D4A7B3E4D6DD60EE871626AA045768"}}};lazy["binding"]=lb;
 auto lc=cap;lc["policy"]=policyL;lc["sdk"]=sdkL;lc["qualification_sha256"]=qshaL;lazy["source_capacity"]=lc;
 const auto lazyParsed=read.decode(json(lazy),true,0);check(lazyParsed.configuredProfile&&lazyParsed.configuredProfile->sourceCapacity&&lazyParsed.configuredProfile->sourceCapacity->policy==policyL&&lazyParsed.configuredProfile->sourceCapacity->sdk==sdkL,"exact lazy capacity admitted, never launch authority");
 for(const auto* key:{"policy","sdk","scope_start","scope_end","max_length","guard","qualification_sha256"}){
  auto changed=lazy;auto c=lc;c[key]="UNKNOWN";changed["source_capacity"]=c;unknown(read,json(changed));
 }
 for(const auto* key:{"sdk","source_boundary_protocol","engine_upgrade","pins"}){
  auto changed=lazy;auto b=lb;b[key]=qb.value(key);changed["binding"]=b;unknown(read,json(changed));
 }
 } {
 // Exact qualified live composition22 image in the unchanged window3 wire family/capacity declaration; mixed generations remain held.
 const QString sdkL="C:/PGN/native/20261021-live-composition-v22",engineL=sdkL+"/bin/pgn-continuous-gpu.exe",qshaL="0406894ADEF8EF475CF81418964586567465A634CC9C06556700EE3A7F137B6A",eshaL="962A98E4D6FD97390CE20FF99143C7BC1BEFD7A1093998D8B73F730D28525F67",policyL="GPU_OWNED_HIGH10_FUSED_WINDOW3_RING_V1";
 auto lazy=qualified;auto lp=qualified.value("profile").toObject();lp["executable"]=engineL;lp["image_sha256"]=eshaL;lazy["profile"]=lp;
 auto lb=qb;lb["executable"]=engineL;lb["sdk"]=sdkL;lb["source_boundary_protocol"]="pgn-source-legacy-fused-window3-ring-v1";lb["engine_upgrade"]=QJsonObject{{"policy",policyL},{"qualification_sha256",qshaL}};
 lb["pins"]=QJsonArray{QJsonObject{{"path",engineL},{"sha256",eshaL}},QJsonObject{{"path",sdkL+"/bin/cudart64_12.dll"},{"sha256","9D9B868955149875AC4EF43442AAAA8913D4A7B3E4D6DD60EE871626AA045768"}}};lazy["binding"]=lb;
 auto lc=cap;lc["policy"]=policyL;lc["sdk"]=sdkL;lc["qualification_sha256"]=qshaL;lazy["source_capacity"]=lc;
 const auto lazyParsed=read.decode(json(lazy),true,0);check(lazyParsed.configuredProfile&&lazyParsed.configuredProfile->sourceCapacity&&lazyParsed.configuredProfile->sourceCapacity->policy==policyL&&lazyParsed.configuredProfile->sourceCapacity->sdk==sdkL,"exact lazy capacity admitted, never launch authority");
 for(const auto* key:{"policy","sdk","scope_start","scope_end","max_length","guard","qualification_sha256"}){
  auto changed=lazy;auto c=lc;c[key]="UNKNOWN";changed["source_capacity"]=c;unknown(read,json(changed));
 }
 for(const auto* key:{"sdk","source_boundary_protocol","engine_upgrade","pins"}){
  auto changed=lazy;auto b=lb;b[key]=qb.value(key);changed["binding"]=b;unknown(read,json(changed));
 }
 }
 {
 // Exact qualified high47 image/capacity declaration; mixed generations remain held.
 const QString sdkL="C:/PGN/native/20261018-fused-high47-v1",engineL=sdkL+"/bin/pgn-continuous-gpu.exe",qshaL="C09786DACA5D6A6200BC71352BFAE319AB7B731AC0D9F79045BB494B25975EE9",eshaL="DA55D109D9E5BD366DC6A9D8E3AE29F7A5D831B4839AC17FD891ADF0F8B23906",policyL="GPU_OWNED_HIGH47_BARRETT_TWO_LANES_FUSED_SQUARE_V1";
 auto lazy=qualified;auto lp=qualified.value("profile").toObject();lp["executable"]=engineL;lp["image_sha256"]=eshaL;lazy["profile"]=lp;
 auto lb=qb;lb["executable"]=engineL;lb["sdk"]=sdkL;lb["source_boundary_protocol"]="pgn-source-legacy-fused-high47-v1";lb["engine_upgrade"]=QJsonObject{{"policy",policyL},{"qualification_sha256",qshaL}};
 lb["pins"]=QJsonArray{QJsonObject{{"path",engineL},{"sha256",eshaL}},QJsonObject{{"path",sdkL+"/bin/cudart64_12.dll"},{"sha256","9D9B868955149875AC4EF43442AAAA8913D4A7B3E4D6DD60EE871626AA045768"}}};lazy["binding"]=lb;
 auto lc=cap;lc["scope_start"]="866996971464348925952";lc["scope_end"]="885443715538058477568";lc["policy"]=policyL;lc["sdk"]=sdkL;lc["qualification_sha256"]=qshaL;lazy["source_capacity"]=lc;
 const auto lazyParsed=read.decode(json(lazy),true,0);check(lazyParsed.configuredProfile&&lazyParsed.configuredProfile->sourceCapacity&&lazyParsed.configuredProfile->sourceCapacity->policy==policyL,"exact lazy capacity admitted, never launch authority");
 for(const auto* key:{"policy","sdk","scope_start","scope_end","max_length","guard","qualification_sha256"}){
  auto changed=lazy;auto c=lc;c[key]="UNKNOWN";changed["source_capacity"]=c;unknown(read,json(changed));
 }
 for(const auto* key:{"sdk","source_boundary_protocol","engine_upgrade","pins"}){
  auto changed=lazy;auto b=lb;b[key]=qb.value(key);changed["binding"]=b;unknown(read,json(changed));
 }
 }
 // Explicit null never creates capacity, but still permits the existing
 // configured desired-OFF latch for an unknown/historical profile.
 {auto noBudget=v2;noBudget["source_capacity"]=QJsonValue::Null;const auto observedV2=read.decode(json(noBudget),true,0);check(observedV2.configuredProfile&&!observedV2.configuredProfile->sourceCapacity,"null capacity is absent, never inferred from v2 image/header");check(!script(NativeControlCommand::configured3090StopLatch(f.endpoint,Request,Configured3090OffReason::ExplicitCardOff,*observedV2.configuredProfile)).isEmpty(),"null capacity does not block exact configured desired-OFF latch");}
 {auto declared=qualified;auto b=qb;b["source_boundary_protocol"]="pgn-source-legacy-a73";declared["binding"]=b;const auto result=read.decode(json(declared),true,0);check(result.configuredProfile&&result.configuredProfile->sourceCapacity,"A73 explicit legacy protocol preserves existing capacity");}
 for(const auto& value:QJsonArray{QJsonValue::Null,false,"pgn-source-boundary-v2"}){auto mixed=qualified;auto b=qb;b["source_boundary_protocol"]=value;mixed["binding"]=b;unknown(read,json(mixed));}
 {auto mixed=qualified;mixed["source_capacity"]=vc;unknown(read,json(mixed));mixed=v2;mixed["source_capacity"]=cap;unknown(read,json(mixed));}
 auto duplicateV2=json(v2);duplicateV2.replace("\"source_boundary_protocol\":","\"source_boundary_protocol\":\"pgn-source-legacy-a73\",\"source_boundary_protocol\":");unknown(read,duplicateV2);
 for(const auto* key:{"sdk","scope_start","scope_end","max_length","guard","qualification_sha256"}){auto changed=qualified;auto c=cap;c[key]="UNKNOWN";changed["source_capacity"]=c;unknown(read,json(changed));}
 bad=qualified;qb.remove("sdk");bad["binding"]=qb;unknown(read,json(bad));
}
void observedStopSnapshot(){
 Fixture f;SshObservationFrame frame;
 const QJsonObject pin{{"path","c:\\PGN\\bin\\engine.exe"},{"sha256",QString(64,'A')}};
 const QJsonObject binding{{"uuid",Device},{"executable","C:/PGN/bin/engine.exe"},{"pins",QJsonArray{pin}}};
 const QJsonObject manifest{{"schema","pgn-native-process-1"},{"run",Old},{"uuid",Device},{"pid",1234},{"birth",Birth}};
 frame.packet={{"schema","pgn-runtime-snapshot-1"},{"owned",true},{"state","RUNNING"},{"driver_valid",true},{"observed_utc",Birth},{"binding",binding},{"manifest",manifest},{"observed_stop_protocol","pgn-stop-observed-1"}};
 const auto stop=NativeControlCommand::stopObservedSnapshot(f.endpoint,frame),direct=NativeControlCommand::stopObservedIdentity(f.endpoint,{Device,Old,"1234",Birth,QString(64,'a')},"pgn-stop-observed-1");
 check(stop.ssh().arguments==direct.ssh().arguments,"observed snapshot selects ONLY unique matching executable pin, exact PID/birth/run");
 for(const auto& key:QStringList{"schema","owned","driver_valid","observed_utc","binding","manifest","observed_stop_protocol"}){auto changed=frame;changed.packet.remove(key);refused([&]{NativeControlCommand::stopObservedSnapshot(f.endpoint,changed);});}
 auto noDriver=frame;noDriver.packet["driver_valid"]=false;check(NativeControlCommand::stopObservedSnapshot(f.endpoint,noDriver).ssh().arguments==stop.ssh().arguments,"exact observed STOP is independent of driver metrics availability");
 for(const auto& delta:QList<QJsonObject>{{{"state","OFF"}},{{"owned","true"}},{{"observed_stop_protocol","legacy-stop"}}}){
  auto changed=frame;for(auto i=delta.begin();i!=delta.end();++i)changed.packet[i.key()]=i.value();refused([&]{NativeControlCommand::stopObservedSnapshot(f.endpoint,changed);},delta.contains("observed_stop_protocol")?NativeControlErrorCode::UnsupportedAbi:NativeControlErrorCode::InvalidContract);
 }
 for(const auto& delta:QList<QJsonObject>{{{"pid","1234"}},{{"pid",0}},{{"pid",4294967296LL}},{{"pid",1.5}},{{"run",Wrong+"\n"}},{{"uuid","GPU-OTHER"}},{{"birth","2026-10-15T00:00:00.000Z"}}}){
  auto changed=frame;auto m=manifest;for(auto i=delta.begin();i!=delta.end();++i)m[i.key()]=i.value();changed.packet["manifest"]=m;refused([&]{NativeControlCommand::stopObservedSnapshot(f.endpoint,changed);});
 }
 for(const auto& delta:QList<QJsonObject>{{{"pins",QJsonArray{pin,pin}}},{{"pins",QJsonArray{}}},{{"pins",QJsonArray{QJsonObject{{"path","C:/PGN/other.exe"},{"sha256",QString(64,'a')}}}}},{{"executable","relative.exe"}},{{"uuid","GPU-OTHER"}}}){
  auto changed=frame;auto b=binding;for(auto i=delta.begin();i!=delta.end();++i)b[i.key()]=i.value();changed.packet["binding"]=b;refused([&]{NativeControlCommand::stopObservedSnapshot(f.endpoint,changed);});
 }
 auto off=frame;off.packet["owned"]=false;off.packet["state"]="OFF";check(NativeControlCommand::stopObservedSnapshot(f.endpoint,off).ssh().arguments==stop.ssh().arguments,"already exited observation still bound to same epoch, no retarget/physical success");
}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{
 if(argc==5&&QString::fromLocal8Bit(argv[1])=="--check-retained-wire"){
  sqlite3* raw{};check(sqlite3_open_v2(argv[2],&raw,SQLITE_OPEN_READONLY|SQLITE_OPEN_FULLMUTEX,nullptr)==SQLITE_OK,"retained store READONLY");
  std::unique_ptr<sqlite3,decltype(&sqlite3_close)> retained(raw,sqlite3_close);
  const auto read=[&](const QString& key){sqlite3_stmt* rawQuery{};check(sqlite3_prepare_v2(raw,"SELECT value FROM booking_metadata WHERE key=?",-1,&rawQuery,nullptr)==SQLITE_OK,"indexed retained read");std::unique_ptr<sqlite3_stmt,decltype(&sqlite3_finalize)> query(rawQuery,sqlite3_finalize);const auto text=key.toUtf8();sqlite3_bind_text(rawQuery,1,text.constData(),text.size(),SQLITE_TRANSIENT);check(sqlite3_step(rawQuery)==SQLITE_ROW&&sqlite3_column_type(rawQuery,0)==SQLITE_TEXT&&sqlite3_column_bytes(rawQuery,0)<=8192,"bounded retained record");return QByteArray(reinterpret_cast<const char*>(sqlite3_column_text(rawQuery,0)),sqlite3_column_bytes(rawQuery,0));};
  const auto receipt=read("mining-initial-start/v1/"+QString::fromLocal8Bit(argv[3]));const auto r=QJsonDocument::fromJson(receipt).object();
  const auto m=QJsonDocument::fromJson(read("mining-source-manifest/v1/"+QString::fromLocal8Bit(argv[4]))).object();
  const bool automatic=r["mode"]=="automatic";InitialStartResult prepared;prepared.status=InitialStartStatus::Prepared;prepared.mode=automatic?MiningSessionMode::Automatic:MiningSessionMode::ManualReservations;
  prepared.dispatch=InitialStartDispatch{r["booking_id"].toString(),r["start"].toString(),r["end"].toString(),r["device"].toString(),"PREPARED",r["origin"].toString(),{},{},automatic};
  Fixture f;const auto command=NativeControlCommand::initial(f.endpoint,prepared,receipt,NativeObservedBase{m["run"].toString(),m["birth"].toString(),m["start"].toString(),m["end"].toString()});
  check(command.action()==NativeControlAction::Initial,"actual retained receipt encoded as INITIAL");
  std::cout<<"RETAINED_WIRE_ENCODED; source READONLY, actual receipt+base, pure encoder with SELF endpoint; NO SSH/process/GPU/retry"<<std::endl;return 0;
 }
 initialAndEndpoint();resumed();queuedAndStop();observedStopSnapshot();configuredProfileAndLatch();std::cout<<"native control command checks="<<checks<<" failures=0; pure argv/decode, own SQLite only, NO SSH/process/GPU\n";return 0;}catch(const std::exception& error){std::cerr<<"native control command checks="<<checks<<" failure="<<error.what()<<'\n';return 1;}}
