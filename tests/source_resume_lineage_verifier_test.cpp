#include "mining/prime_gap_engine.h"
#include "mining/source_resume_lineage_verifier.h"
#include "mining/source_decimal.h"
#include "mining/source_history.h"
#include "mining/source_session.h"
#include "mining/completion_advance_gate.h"
#include "booking/booking_store.h"
#include "booking/decimal_range.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QMap>
#include <QTemporaryDir>
#include <sqlite3.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#ifdef Q_OS_WIN
#include <windows.h>
#include <io.h>
#else
#include <sys/stat.h>
#endif

// CLOSED synthetic producer-shaped fixtures ONLY. Real temporary SQLite,
// native BookingStore admission and complete raw files; no process, hardware,
// network, user source, UI, independent primality or remote-authentication test.
using namespace pgn::mining;
using namespace pgn::booking;
namespace {
int checks{};
void check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
void need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
const QString Device="GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac",First="184467440737095516260",Last="184467440737095526260",Label="C:/PUBLIC-TESTONLY/current-job/checkpoint";
constexpr quint64 TestBase=9007199254740993ULL;
QString runId(int i){return QString::number(i+1,16).rightJustified(32,'0');}
QString request(int i){return "20000000-0000-4000-8000-"+QString::number(i+1,16).rightJustified(12,'0');}
QString nonce(int i){return "30000000-0000-4000-8000-"+QString::number(i+1,16).rightJustified(12,'0');}
QByteArray compact(const QJsonObject& o){return QJsonDocument(o).toJson(QJsonDocument::Compact);}
QByteArray hashFile(const QString& path){QFile f(path);need(f.open(QIODevice::ReadOnly),"own fixture hash open");return QCryptographicHash::hash(f.readAll(),QCryptographicHash::Sha256);}
class Sql {
public:
 sqlite3* db{};
 explicit Sql(const QString& path){need(sqlite3_open_v2(path.toUtf8().constData(),&db,SQLITE_OPEN_READWRITE,nullptr)==SQLITE_OK,"own temporary SQLite open");}
 ~Sql(){if(db)sqlite3_close(db);}
 void exec(const char* sql,const QStringList& args={}){sqlite3_stmt* q{};need(sqlite3_prepare_v2(db,sql,-1,&q,nullptr)==SQLITE_OK,"own fixture SQL prepare");for(int i=0;i<args.size();++i){const auto b=args[i].toUtf8();sqlite3_bind_text(q,i+1,b.constData(),b.size(),SQLITE_TRANSIENT);}const auto rc=sqlite3_step(q);sqlite3_finalize(q);need(rc==SQLITE_DONE,"own fixture SQL write");}
 QByteArray raw(const char* sql,const QString& id={}){sqlite3_stmt* q{};need(sqlite3_prepare_v2(db,sql,-1,&q,nullptr)==SQLITE_OK,"own fixture read prepare");if(!id.isNull()){const auto b=id.toUtf8();sqlite3_bind_text(q,1,b.constData(),b.size(),SQLITE_TRANSIENT);}QByteArray out;if(sqlite3_step(q)==SQLITE_ROW)out=QByteArray(reinterpret_cast<const char*>(sqlite3_column_blob(q,0)),sqlite3_column_bytes(q,0));sqlite3_finalize(q);return out;}
 void metadata(const QString& key,const QJsonObject& value){exec("UPDATE booking_metadata SET value=? WHERE key=?",{QString::fromUtf8(compact(value)),key});}
 QByteArray dump(){sqlite3_stmt* tables{};need(sqlite3_prepare_v2(db,"SELECT name FROM sqlite_master WHERE type='table' ORDER BY name",-1,&tables,nullptr)==SQLITE_OK,"own fixture dump tables");QByteArray out;while(sqlite3_step(tables)==SQLITE_ROW){const auto name=QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(tables,0)));sqlite3_stmt* q{};const auto sql=("SELECT * FROM \""+name+"\" ORDER BY rowid").toUtf8();if(sqlite3_prepare_v2(db,sql.constData(),-1,&q,nullptr)!=SQLITE_OK){const auto fallback=("SELECT * FROM \""+name+"\"").toUtf8();need(sqlite3_prepare_v2(db,fallback.constData(),-1,&q,nullptr)==SQLITE_OK,"own fixture dump prepare");}out+=name.toUtf8()+':';while(sqlite3_step(q)==SQLITE_ROW){for(int c=0;c<sqlite3_column_count(q);++c){out+=QByteArray::number(sqlite3_column_type(q,c))+':';out+=QByteArray(reinterpret_cast<const char*>(sqlite3_column_blob(q,c)),sqlite3_column_bytes(q,c)).toHex()+';';}out+='\n';}sqlite3_finalize(q);}sqlite3_finalize(tables);return out;}
};
StoredSourceResume checkpoint(const QString& run,quint64 positions,quint64 gaps,quint64 tests,quint64 batches,quint64 time){
 StoredSourceResume result;result.checkpointBytes="PGNCG001";for(const quint64 n:{100ULL,10ULL,10000ULL,positions,tests,gaps,batches})for(int i=0;i<8;++i)result.checkpointBytes.append(char((n>>(i*8))&255));auto& r=result.resume;r.run=run;r.start=First;r.end=Last;r.length="10000";r.positions=QString::number(positions);r.nextStart=addUnsigned(First,r.positions);r.remaining=QString::number(10000-positions);r.tests=QString::number(tests);r.gaps=QString::number(gaps);r.batches=QString::number(batches);r.counterTimeNs=QString::number(time);r.checkpointLabel=Label;r.checkpointSha256=QString::fromLatin1(QCryptographicHash::hash(result.checkpointBytes,QCryptographicHash::Sha256).toHex());return result;
}
QByteArray raw(quint64 records,quint64 positions,quint64 gaps,quint64 tests,quint64 batches,quint64 time,bool complete){
 QByteArray b=QString("[phase0-gpu] engine=GPU_OWNED_CONTINUOUS descriptors=2 original_start=%1 length=10000 proof=GPU_MR12_GAP coverage=SOURCE_PRP_INDEPENDENT_PROOF_PENDING\n").arg(First).toUtf8();
 for(quint64 i=0;i<records;++i)b+=QString("[phase0-gpu] GAP merit=20.0000 gap=2 lower=%1 upper=%2 verified=1 proof=GPU_MR12_EULER_COMPOSITE record=off table=unknown unix_ts=1791936000 time_basis=source_batch_publication\n").arg(addUnsigned(First,QString::number(10+(i%400)*10)),addUnsigned(First,QString::number(12+(i%400)*10))).toUtf8();
 b+=QString("[pgn-counters] time_ns=%1 positions=%2 gaps=%3 tests=%4 batches=%5 processed_gaps=%3\n").arg(time).arg(positions).arg(gaps).arg(tests).arg(batches).toUtf8();
 b+=complete?QString("[phase0-gpu] gaps reported=%1 verification_failures=0 records_new=0\n[phase0-gpu] --- end ---\n").arg(gaps).toUtf8():QString("[phase0-gpu] stopped positions=%1 checkpoint=%2\n").arg(positions).arg(Label).toUtf8();return b;
}
QByteArray heldPin(QFile& file,const QString& path){
#ifdef Q_OS_WIN
 const auto h=reinterpret_cast<HANDLE>(_get_osfhandle(file.handle()));BY_HANDLE_FILE_INFORMATION a{};need(h!=INVALID_HANDLE_VALUE&&GetFileInformationByHandle(h,&a),"test held Win32 file information");
 const auto p=CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);need(p!=INVALID_HANDLE_VALUE,"test path incarnation open");BY_HANDLE_FILE_INFORMATION b{};const bool read=GetFileInformationByHandle(p,&b);CloseHandle(p);need(read&&!(a.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)&&!(b.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT),"test non-reparse regular source");
 const auto pin=[](const BY_HANDLE_FILE_INFORMATION& i){return QString("%1/%2/%3/%4/%5/%6/%7").arg(i.dwVolumeSerialNumber).arg(i.nFileIndexHigh).arg(i.nFileIndexLow).arg(i.nFileSizeHigh).arg(i.nFileSizeLow).arg(i.ftLastWriteTime.dwHighDateTime).arg(i.ftLastWriteTime.dwLowDateTime).toUtf8();};need(pin(a)==pin(b),"test path/held HANDLE incarnation differs");return pin(a);
#else
 struct stat a{},b{};need(fstat(file.handle(),&a)==0&&lstat(path.toUtf8().constData(),&b)==0&&S_ISREG(a.st_mode)&&!S_ISLNK(b.st_mode)&&a.st_dev==b.st_dev&&a.st_ino==b.st_ino,"test held POSIX source identity");return QString("%1/%2/%3/%4/%5").arg(qulonglong(a.st_dev)).arg(qulonglong(a.st_ino)).arg(qlonglong(a.st_size)).arg(qlonglong(a.st_mtim.tv_sec)).arg(qlonglong(a.st_mtim.tv_nsec)).toUtf8();
#endif
}
enum class Mode {Normal,Short,Missing,WrongRun,Symlink,ReadFail,Overread,CloseFail,Grow};
struct Stats {int opened{},closed{},finished{},reads{};qsizetype maximum{};};
class Reader final:public HeldSourceLineageReader {
 QFile file_;QString path_,run_,source_;QByteArray pin_;Mode mode_;Stats& stats_;bool closed_{},grew_{};
public:
 Reader(QString path,QString run,QString source,Mode mode,Stats& stats):file_(path),path_(std::move(path)),run_(std::move(run)),source_(std::move(source)),mode_(mode),stats_(stats){need(file_.open(QIODevice::ReadOnly),"own real raw reader open");pin_=heldPin(file_,path_);++stats_.opened;}
 ~Reader() override {if(!closed_){file_.close();closed_=true;++stats_.closed;}}
 QString run() const override{return mode_==Mode::WrongRun?QString(32,'f'):run_;}
 QString source() const override{return source_;}
 bool isRegularFile() const override{return QFileInfo(path_).isFile();}
 bool isSymbolicLink() const override{return mode_==Mode::Symlink||QFileInfo(path_).isSymLink();}
 qint64 size() const override{return file_.size();}
 QByteArray read(qint64 at,qsizetype maximum) override {need(maximum>0&&maximum<=65536&&file_.seek(at),"own real bounded source read");++stats_.reads;stats_.maximum=std::max(stats_.maximum,maximum);if(mode_==Mode::ReadFail)throw std::runtime_error("injected actual read failure");if(mode_==Mode::Overread)return QByteArray(maximum+1,'x');auto bytes=file_.read(mode_==Mode::Short?std::min<qsizetype>(2,maximum):maximum);if(mode_==Mode::Grow&&!grew_){grew_=true;QFile writer(path_);need(writer.open(QIODevice::Append)&&writer.write("late\n")==5&&writer.flush(),"own unstable source fixture append");}return bytes;}
 void closeAndVerifyStable() override {need(!closed_,"test reader duplicate close");++stats_.finished;bool stable=false;try{stable=heldPin(file_,path_)==pin_;}catch(...){file_.close();closed_=true;++stats_.closed;throw;}file_.close();closed_=true;++stats_.closed;need(stable&&mode_!=Mode::CloseFail,"test final held file pin changed/failure");}
};
struct Journal {QString run;QJsonObject manifest;StoredSourceResume checkpoint;QByteArray bytes;quint64 positions{},gaps{},tests{},batches{},time{};};
struct Fixture {
 QTemporaryDir temp;QString bookingFile,historyFile;BookingStore store;SourceHistory history;Sql db,hdb;QString booking;QVector<Journal> journals;Stats stats;
 Fixture(int count=3,bool flat=false,bool zero=false,quint64 records=2,bool index=true):bookingFile(temp.filePath("booking.sqlite")),historyFile(temp.filePath("history.sqlite")),store(bookingFile),history(historyFile),db(bookingFile),hdb(historyFile){
  check(temp.isValid()&&store.valid(),"real owned temporary native store");SourceRegistry registry;registry.url="https://PUBLIC-TESTONLY.invalid/source";registry.sha256=QString(64,'1');registry.observedUtc=QDateTime::fromString("2026-10-14T00:00:00Z",Qt::ISODate);registry.ranges={{1,First,Last,"Open",{}}};check(store.queue(registry,First,Last,Device).isEmpty(),"real native manual intent");const auto initial=store.prepareInitialStart(request(1000),Device,{First,Last,First,Last,First,"10000"},registry,{WorkBoundary::Ready,nonce(1000)});check(initial.status==InitialStartStatus::Prepared&&initial.dispatch.has_value(),"real initial admission only; no launch");booking=initial.dispatch->bookingId;
  for(int i=0;i<count;++i){
   Journal j;j.run=runId(i);j.positions=flat?300:(i==count-1?10000:quint64(300*(i+1)));j.gaps=zero?0:records*quint64(i+1);j.tests=flat?TestBase:TestBase+10*i;j.batches=flat?1:quint64(i+1);j.time=flat?111:quint64(111*(i+1));j.checkpoint=checkpoint(j.run,j.positions==10000?9999:j.positions,j.gaps,j.tests,j.batches,j.time);
   if(i){const auto& previous=journals.last();check(store.saveResumeCheckpoint(Device,previous.checkpoint.resume,previous.checkpoint.checkpointBytes).isEmpty(),"real original cumulative checkpoint");const auto epoch=store.prepareCheckpointResume(request(i),Device,booking,previous.run,j.run,previous.checkpoint.resume.checkpointSha256,nonce(i));check(epoch.status==CheckpointResumeAdmissionStatus::Prepared,"real immutable native resume epoch");}
   db.exec("UPDATE dispatch_requests SET state='STARTED',run=? WHERE id=?",{j.run,booking});db.exec("UPDATE bookings SET state='DISPATCHED' WHERE id=?",{booking});check(store.observeRuntime(j.run,First,Last,Device,false).isEmpty(),"real exact paused physical row (not actual ownership)");
   j.manifest={{"schema","pgn-native-process-1"},{"run",j.run},{"uuid",Device},{"queued_intent",booking},{"start",First},{"end",Last},{"stdout","C:/PUBLIC-TESTONLY/"+j.run+".stdout"},{"pid",4242+i},{"birth","2026-10-14T00:00:00.123456789Z"}};
   if(i){j.manifest.insert("resume_previous_run",journals.last().run);j.manifest.insert("checkpoint_input_sha256",journals.last().checkpoint.resume.checkpointSha256);}check(store.saveSourceRunManifest(Device,j.manifest).isEmpty(),"real immutable actual-manifest-shaped native record");
   history.bindSourceIdentity(j.run,QString::fromUtf8(compact(QJsonObject{{"run",j.run},{"uuid",Device.toLower()},{"start",First},{"end",Last},{"pid",j.manifest.value("pid")},{"birth",j.manifest.value("birth")}})),true);
   j.bytes=raw(zero?0:records,j.positions,j.gaps,j.tests,j.batches,j.time,!flat&&i==count-1);j.bytes.prepend(QString::fromUtf8("TESTONLY diagnostic λ source\n").toUtf8());journals.append(j);if(index){write(i,j.bytes);append(j.run,j.bytes);}
  }
 }
 QString path(int i)const{return temp.filePath(journals[i].run+".stdout");}
 void append(const QString& run,const QByteArray& bytes){qint64 at=0;while(at<bytes.size()){auto b=bytes.mid(at,65536);const auto newline=b.lastIndexOf('\n');need(newline>=0,"own bounded whole-row batch");b=b.left(newline+1);history.append(run,at,b);at+=b.size();}}
 void write(int i,const QByteArray& b){QFile f(path(i));need(f.open(QIODevice::WriteOnly|QIODevice::Truncate)&&f.write(b)==b.size()&&f.flush(),"real own raw fixture write/flush");}
 void rewrite(int i,const QByteArray& b){write(i,b);hdb.exec("UPDATE cursors SET offset=? WHERE run=?",{QString::number(b.size()),journals[i].run});}
 QJsonObject snapshot(int i=-1,bool stopped=false)const {if(i<0)i=journals.size()-1;const auto& j=journals[i];QJsonObject s{{"schema","pgn-runtime-snapshot-1"},{"state","OFF"},{"owned",false},{"manifest",j.manifest},{"last_exit",QJsonObject{{"run",j.run},{"exit_code",0}}}};if(stopped){s.insert("checkpoint_evidence",QJsonObject{{"schema","pgn-stop-checkpoint-evidence-1"},{"status","captured"},{"run",j.run},{"checkpoint_label",Label},{"checkpoint_base64",QString::fromLatin1(j.checkpoint.checkpointBytes.toBase64())},{"source_tail_utf8",QString::fromUtf8(j.bytes)}});}else{s.insert("source_completion_observation",QJsonObject{{"schema","pgn-source-range-completion-1"},{"run",j.run},{"queued_intent",booking},{"start",First},{"end",Last},{"source",j.manifest.value("stdout")},{"source_positions",QString::number(j.positions)},{"source_session_gaps",QString::number(j.gaps)},{"source_tests",QString::number(j.tests)},{"source_exit_code",0},{"source_range_complete",true},{"independent_coverage",false},{"proof_status","SOURCE_PRP_INDEPENDENT_PROOF_PENDING"},{"source_tests_complete",true},{"source_position_evidence","final_counter"}});}return s;}
 QJsonObject verify(const QJsonObject& s,Mode mode=Mode::Normal){SourceResumeLineageVerifier verifier(bookingFile,historyFile,[&](const QString& run,const QString& source)->std::unique_ptr<HeldSourceLineageReader>{if(mode==Mode::Missing)return {};return std::make_unique<Reader>(temp.filePath(run+".stdout"),run,source,mode,stats);});return verifier.verify(s);}
 void manifestPoison(int i,const QJsonObject& m){db.metadata("mining-source-manifest/v1/"+journals[i].run,m);}
};
void held(const QJsonObject& p,const QString& reason={}){check(p.value("ready").isBool()&&!p.value("ready").toBool()&&p.value("chain_sha256")==""&&p.value("epochs")==0,"Held never publishes partial proof/hash/count");check(p.value("error").isString()&&!p.value("error").toString().isEmpty()&&p.value("error").toString().size()<=400,"Held has bounded explicit reason");if(!reason.isEmpty())check(p.value("error").toString().contains(reason),"Held first causal boundary identified");}
void enrollFragmented(Fixture& f){
 auto& j=f.journals[0];QByteArray padding;for(int n=0;n<16;++n)padding+=QByteArray(19999,'x')+'\n';j.bytes.prepend(padding);
 SourceMirror mirror(f.temp.path());SourceSession session(f.history,mirror);
 for(qint64 at=0;at<j.bytes.size();){const auto bytes=j.bytes.mid(at,65536);const auto chunk=session.consume(j.run,at,at+bytes.size(),bytes);check(!chunk.needsReplay&&chunk.nextOffset==at+bytes.size(),"real SourceSession admits64KiB packet plus prior fragment");at=chunk.nextOffset;}
 const auto conservation=session.conservation(j.run,j.bytes.size(),0);check(conservation.ready&&conservation.committedOffset==j.bytes.size(),"real fragmented journal has exact complete committed prefix");
 check(f.hdb.raw("SELECT MAX(length) FROM native_source_batches").toLongLong()>65536,"real producer enrollment exceeds a transport chunk");
}
void fragmentedEnrollment(){
 for(int poison=0;poison<9;++poison){Fixture f(1,false,false,2,false);enrollFragmented(f);const auto source="native/"+f.journals[0].run+".stdout";
  switch(poison){
   case 1:f.hdb.exec("UPDATE native_source_batches SET length=0 WHERE source=? AND offset=0",{source});break;
   case 2:f.hdb.exec("UPDATE native_source_batches SET length=? WHERE source=? AND offset=0",{QString::number(f.journals[0].bytes.size()+1),source});break;
   case 3:f.hdb.exec("UPDATE native_source_batches SET offset=1 WHERE source=? AND offset=0",{source});break;
   case 4:f.hdb.exec("UPDATE native_source_batches SET offset=offset-1 WHERE source=? AND offset=(SELECT MIN(offset) FROM native_source_batches WHERE offset>0)",{source});break;
   case 5:f.hdb.exec("UPDATE native_source_batches SET sha256=? WHERE source=? AND offset=0",{QString(64,'0'),source});break;
   case 6:f.hdb.exec("UPDATE native_source_batches SET length=1.5 WHERE source=? AND offset=0",{source});break;
   case 7:f.hdb.exec("UPDATE native_source_batches SET length=9007199254740992 WHERE source=? AND offset=0",{source});break;
   case 8:f.hdb.exec("INSERT INTO native_source_batches VALUES(?,?,1,?)",{source,QString::number(f.journals[0].bytes.size()),QString(64,'0')});break;
  }
  const auto db=f.db.dump(),history=f.hdb.dump(),rawHash=hashFile(f.path(0));const auto proof=f.verify(f.snapshot());
  if(!poison){check(proof.value("ready")==true,proof.value("error").toString().toUtf8().constData());check(f.stats.maximum==65536&&f.stats.reads>1,"enrollment larger than64KiB is hashed over bounded reads");check(proof==f.verify(f.snapshot(),Mode::Short),"2-byte I/O preserves enrollment/row hashing across both boundaries");}else held(proof);
  check(f.stats.opened==f.stats.closed&&f.stats.finished==f.stats.closed&&f.stats.maximum<=65536,"fragmented producer verification releases every bounded held reader");
  check(f.db.dump()==db&&f.hdb.dump()==history&&hashFile(f.path(0))==rawHash,"verification preserves real source bytes and both SQLite stores even on poison");
 }
}
void positive(){Fixture f;const auto db=f.db.dump(),history=f.hdb.dump(),rawhash=hashFile(f.path(0));const auto s=f.snapshot(),p=f.verify(s);check(p.value("ready")==true,p.value("error").toString().toUtf8().constData());check(p.value("epochs")==3&&p.value("terminal")=="complete"&&p.value("scope")=="LOCAL_RAW_LINEAGE_COUNTER_DELTAS_NOT_INDEPENDENT_PRIME_COVERAGE","three journals/cumulative-baseline proof scope");check(p.value("chain_sha256").toString().size()==64,"content chain hash present");check(f.verify(s)==p,"exact replay deterministic without cache");check(f.verify(s,Mode::Short)==p,"real 2-byte reads split UTF8/framing without changing hash");auto view=s;view.insert("resume_lineage",p);check(sourceResumeLineageMatches(view,"complete"),"actual C++ consumer accepts produced proof only for exact epoch");check(!sourceResumeLineageMatches(view,"stopped"),"complete cannot masquerade as stopped");check(f.db.dump()==db&&f.hdb.dump()==history&&hashFile(f.path(0))==rawhash,"both native DB contents and conserved bytes unchanged");check(f.stats.opened==f.stats.closed&&f.stats.finished==f.stats.closed&&f.stats.maximum<=65536,"all real readers verified/released and bounded");}
void preparedResumeReconciliation(){
 for(int poison=0;poison<9;++poison){
  Fixture f(3,true,true);const auto& current=f.journals.last();const auto snapshot=f.snapshot(-1,true),before=f.verify(snapshot);check(before.value("ready")==true,"stopped RAW qualified before successor reservation");
  check(f.store.saveResumeCheckpoint(Device,current.checkpoint.resume,current.checkpoint.checkpointBytes).isEmpty(),"save actual stopped checkpoint before prepared successor");
  const auto next=runId(90);const auto prepared=f.store.prepareCheckpointResume(request(90),Device,f.booking,current.run,next,current.checkpoint.resume.checkpointSha256,nonce(90));check(prepared.status==CheckpointResumeAdmissionStatus::Prepared&&prepared.epoch,"reserve successor without observing or launching it");
  switch(poison){
   case 1:f.db.exec("UPDATE dispatch_requests SET run=? WHERE id=?",{runId(91),f.booking});break;
   case 2:f.db.exec("UPDATE dispatch_requests SET state='STARTED' WHERE id=?",{f.booking});break;
   case 3:f.db.exec("UPDATE bookings SET state='DISPATCHED' WHERE id=?",{f.booking});break;
   case 4:check(f.store.observeRuntime(next,First,Last,Device,false).isEmpty(),"record observed successor contradicting stopped-parent recovery");break;
   case 5:f.db.exec("INSERT INTO booking_metadata(key,value) VALUES(?,?)",{"mining-source-manifest/v1/"+next,"{}"});break;
   case 6:{auto o=QJsonDocument::fromJson(f.db.raw("SELECT receipt FROM dispatch_resume_epochs WHERE run=?",next)).object();auto cp=o.value("checkpoint").toObject(),tuple=cp.value("resume").toObject();tuple.insert("counterTimeNs","112");cp.insert("resume",tuple);o.insert("checkpoint",cp);f.db.exec("UPDATE dispatch_resume_epochs SET receipt=? WHERE run=?",{QString::fromUtf8(compact(o)),next});break;}
   case 7:{auto o=QJsonDocument::fromJson(f.db.raw("SELECT receipt FROM dispatch_resume_epochs WHERE run=?",next)).object();o.insert("previous_run",runId(91));f.db.exec("UPDATE dispatch_resume_epochs SET receipt=? WHERE run=?",{QString::fromUtf8(compact(o)),next});break;}
   case 8:{auto o=QJsonDocument::fromJson(f.db.raw("SELECT receipt FROM dispatch_resume_epochs WHERE run=?",next)).object();auto cp=o.value("checkpoint").toObject(),tuple=cp.value("resume").toObject();auto bytes=current.checkpoint.checkpointBytes;bytes[32]=char(45);cp.insert("checkpoint_base64",QString::fromLatin1(bytes.toBase64()));tuple.insert("checkpointSha256",QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()));tuple.insert("positions","301");tuple.insert("nextStart",addUnsigned(First,"301"));tuple.insert("remaining","9699");cp.insert("resume",tuple);o.insert("checkpoint",cp);f.db.exec("UPDATE dispatch_resume_epochs SET receipt=? WHERE run=?",{QString::fromUtf8(compact(o)),next});break;}
  }
  const auto db=f.db.dump(),history=f.hdb.dump(),rawHash=hashFile(f.path(2));const auto proof=f.verify(snapshot);
  if(!poison){check(proof==before,"same exact stopped RAW proof survives unobserved prepared successor; no invented epoch or counter delta");const auto replay=f.store.prepareCheckpointResume(request(90),Device,f.booking,current.run,next,current.checkpoint.resume.checkpointSha256,nonce(90));check(replay.status==CheckpointResumeAdmissionStatus::ReconcilePrepared&&replay.replayed,"same retained request reconciles existing successor only");const auto other=f.store.prepareCheckpointResume(request(91),Device,f.booking,current.run,runId(91),current.checkpoint.resume.checkpointSha256,nonce(91));check(other.status==CheckpointResumeAdmissionStatus::Held,"new request cannot allocate another successor");}
  else held(proof);
  check(f.db.dump()==db&&f.hdb.dump()==history&&hashFile(f.path(2))==rawHash,"prepared reconciliation verification preserves both stores and all stopped RAW");
 }
}
void stoppedAndDepth(){
 {Fixture f;const auto& current=f.journals.last();check(f.store.observeRuntime(current.run,First,Last,Device,true).isEmpty(),"current actual-shaped row remains ACTIVE before first completion admission");
  const auto db=f.db.dump(),hdb=f.hdb.dump(),raw=hashFile(f.path(f.journals.size()-1));const auto proof=f.verify(f.snapshot());
  check(proof.value("ready")==true,"complete current RAW may prove its FIRST completion before SQL SOURCE_FINISHED; no circular pre-settlement");
  check(f.db.dump()==db&&f.hdb.dump()==hdb&&hashFile(f.path(f.journals.size()-1))==raw,"first-completion verification is READONLY, no early SQL settlement");
  held(f.verify(f.snapshot(-1,true)));
  check(f.store.recordQueuedSourceCompletion(current.run,First,Last,Device,f.booking).isEmpty(),"first proven current completion settles runtime/local/dispatch atomically");
  check(f.verify(f.snapshot())==proof,"same complete RAW proof after settlement, no weaker proof or retiming");
 }
 {Fixture f;f.db.exec("UPDATE bookings SET state='ACTIVE' WHERE id=?",{f.journals.first().run});held(f.verify(f.snapshot()),"Lineage ancestor");}
 {Fixture f;const auto before=f.verify(f.snapshot());check(before.value("ready")==true,"complete source positive before local settle");check(f.store.recordQueuedSourceCompletion(f.journals.last().run,First,Last,Device,f.booking).isEmpty(),"real native SOURCE/runtime/local/dispatch settlement");check(f.verify(f.snapshot())==before,"settled current SOURCE retains same raw lineage without rewriting ancestor rows");}
 {Fixture f(3,true,true);const auto p=f.verify(f.snapshot(-1,true));check(p.value("ready")==true&&p.value("terminal")=="stopped"&&p.value("epochs")==3,"stopped final source preserves zero-record known baselines");}
 {Fixture f(1,true,true);const auto p=f.verify(f.snapshot(-1,true));check(p.value("ready")==true&&p.value("epochs")==1,"root still requires actual raw and immutable manifest, not assumed zero origin");f.db.exec("DELETE FROM booking_metadata WHERE key=?",{"mining-source-manifest/v1/"+f.journals[0].run});held(f.verify(f.snapshot(-1,true)),"manifest");}
 {Fixture f(72,true,true);const auto p=f.verify(f.snapshot(-1,true));check(p.value("ready")==true&&p.value("epochs")==72,"no64 ancestry cap or journal loss");check(f.stats.opened==72&&f.stats.closed==72,"long chain read/finish every actual journal");}
 {Fixture f(1,false,false,5001);const auto p=f.verify(f.snapshot());check(p.value("ready")==true&&f.stats.reads>10&&f.stats.maximum<=65536,"no whole-journal quota; real multi-batch index/raw scanned in bounded reads");}
}
void readerFailures(){for(const auto mode:{Mode::Missing,Mode::WrongRun,Mode::Symlink,Mode::ReadFail,Mode::Overread,Mode::CloseFail,Mode::Grow}){Fixture f;const auto p=f.verify(f.snapshot(),mode);held(p);check(f.stats.opened==f.stats.closed,"reader release on every failure boundary");if(mode!=Mode::Missing)check(f.stats.finished==1,"final handle validation/release attempted after scan failure");}Fixture f;SourceResumeLineageVerifier metadataOnly(f.bookingFile,f.historyFile,{});held(metadataOnly.verify(f.snapshot()),"adapter");int polls=0;SourceResumeLineageVerifier cancelled(f.bookingFile,f.historyFile,[&](const QString& run,const QString& source){return std::make_unique<Reader>(f.temp.filePath(run+".stdout"),run,source,Mode::Normal,f.stats);},[&]{return ++polls>=3;});held(cancelled.verify(f.snapshot()),"cancelled");check(f.stats.opened==f.stats.closed&&f.stats.finished==1,"cancellation after handle acquisition still verifies/releases it");}
void rawPoisons(){
 for(int poison=0;poison<10;++poison){Fixture f;auto b=f.journals[0].bytes;switch(poison){case 0:b.prepend(QByteArray("bad ")+char(0xff)+'\n');break;case 1:b.prepend(QByteArray(32769,'x')+'\n');break;case 2:b.replace(" gap=2 "," gap=3 ");break;case 3:b.chop(1);break;case 4:b.replace("stopped positions=300","stopped positions=301");break;case 5:b.replace("gaps=2 tests=","gaps=3 tests=");break;case 6:b.replace("tests=9007199254740993","tests=9007199254740992");break;case 7:b.replace("time_ns=111","time_ns=112");break;case 8:b+="late nonempty diagnostic\n";break;case 9:b.replace("original_start="+First.toUtf8(),"original_start=0"+First.toUtf8());break;}f.rewrite(0,b);held(f.verify(f.snapshot()));}
 {Fixture f;const auto p=f.verify(f.snapshot());check(p.value("ready")==true,"baseline positive before missing raw");QFile::remove(f.path(0));held(f.verify(f.snapshot()));}
 {Fixture f;f.hdb.exec("DELETE FROM cursors WHERE run=?",{f.journals[0].run});held(f.verify(f.snapshot()),"cursor absent");}
 {Fixture f;f.hdb.exec("UPDATE cursors SET offset=offset-1 WHERE run=?",{f.journals[0].run});held(f.verify(f.snapshot()),"committed cursor");}
 {Fixture f;f.hdb.exec("UPDATE cursors SET source='foreign' WHERE run=?",{f.journals[0].run});held(f.verify(f.snapshot()),"cursor absent");}
 {Fixture f;auto b=f.journals[0].bytes;b.replace("TESTONLY diagnostic","TEST0NLY diagnostic");f.write(0,b);held(f.verify(f.snapshot()),"prefix SHA");}
 {Fixture f;f.hdb.exec("DELETE FROM native_source_batches WHERE source=?",{"native/"+f.journals[0].run+".stdout"});held(f.verify(f.snapshot()),"enrollment");}
 {Fixture f;f.hdb.exec("DELETE FROM native_source_identities WHERE run=?",{f.journals[0].run});const auto before=f.hdb.dump();held(f.verify(f.snapshot()),"HeldNonEnrolled");check(f.hdb.dump()==before,"legacy identity gap never adopted or rewritten");}
 {Fixture f;f.hdb.exec("UPDATE native_source_identities SET identity=CAST(identity AS BLOB) WHERE run=?",{f.journals[0].run});held(f.verify(f.snapshot()),"TEXT");}
 {Fixture f;f.hdb.exec("UPDATE native_source_identities SET identity=replace(identity,'4242','4243') WHERE run=?",{f.journals[0].run});held(f.verify(f.snapshot()),"source identity differs");}
 {Fixture f;f.hdb.exec("UPDATE native_source_identities SET identity=replace(identity,'{','{\"run\":\"bad\",') WHERE run=?",{f.journals[0].run});held(f.verify(f.snapshot()),"noncanonical");}
 {Fixture f;f.hdb.exec("UPDATE native_source_batches SET sha256=replace(sha256,substr(sha256,1,1), 'z') WHERE source=?",{"native/"+f.journals[0].run+".stdout"});held(f.verify(f.snapshot()),"SHA256");}
 {Fixture f;f.hdb.exec("INSERT INTO cursors SELECT CAST(source AS BLOB),offset,run FROM cursors WHERE run=?",{f.journals[0].run});held(f.verify(f.snapshot()));}
 {Fixture f;f.hdb.exec("UPDATE cursors SET offset=1.5 WHERE run=?",{f.journals[0].run});held(f.verify(f.snapshot()),"INTEGER");}
 {Fixture f(2,true);auto s=f.snapshot(-1,true);auto cp=s.value("checkpoint_evidence").toObject();cp.insert("source_tail_utf8",cp.value("source_tail_utf8").toString()+"\n");s.insert("checkpoint_evidence",cp);held(f.verify(s),"EOF suffix");}
}
void metadataPoisons(){
 for(int poison=0;poison<12;++poison){Fixture f;auto m=f.journals[0].manifest;switch(poison){case 0:m.insert("birth","2026-02-30T00:00:00Z");f.manifestPoison(0,m);break;case 1:m.insert("stdout","");f.manifestPoison(0,m);break;case 2:f.db.exec("UPDATE bookings SET state='ACTIVE' WHERE id=?",{f.journals[0].run});break;case 3:f.db.exec("UPDATE dispatch_requests SET run=?",{f.journals[0].run});break;case 4:f.db.exec("UPDATE bookings SET state='QUEUED' WHERE id=?",{f.booking});break;case 5:f.db.exec("UPDATE dispatch_resume_epochs SET request_id='11111111-2222-4333-8444-555555555555' WHERE run=?",{f.journals[1].run});break;case 6:f.db.exec("UPDATE dispatch_resume_epochs SET receipt=CAST(receipt AS BLOB)");break;case 7:f.db.exec("DELETE FROM dispatch_resume_epochs WHERE run=?",{f.journals[1].run});break;case 8:m.insert("queued_intent",request(999));f.manifestPoison(0,m);break;case 9:f.db.exec("UPDATE booking_metadata SET value=value||' ' WHERE key=?",{"mining-source-manifest/v1/"+f.journals[0].run});break;case 10:f.db.exec("UPDATE bookings SET device='GPU-00000000-0000-4000-8000-000000000000' WHERE id=?",{f.journals[0].run});break;case 11:f.db.exec("DELETE FROM bookings WHERE id=?",{f.journals[0].run});break;}held(f.verify(f.snapshot()));}
 for(const auto v:{QJsonValue(),QJsonValue(QJsonValue::Null),QJsonValue("false"),QJsonValue(0),QJsonValue(true)}){Fixture f;auto s=f.snapshot();if(v.isUndefined())s.remove("owned");else s.insert("owned",v);held(f.verify(s));}
 {Fixture f;auto s=f.snapshot();auto m=s.value("manifest").toObject();m.insert("birth","2026-10-14T00:00:01Z");s.insert("manifest",m);held(f.verify(s),"snapshot differs");}
 {Fixture f;auto s=f.snapshot();auto c=s.value("source_completion_observation").toObject();c.insert("independent_coverage",true);s.insert("source_completion_observation",c);held(f.verify(s),"SOURCE");}
 {Fixture f;auto s=f.snapshot();auto c=s.value("source_completion_observation").toObject();c.insert("source_session_gaps","0"+c.value("source_session_gaps").toString());s.insert("source_completion_observation",c);held(f.verify(s),"canonical");}
 {Fixture f;f.db.exec("UPDATE dispatch_resume_epochs SET run=CAST(run AS BLOB) WHERE run=?",{f.journals[1].run});held(f.verify(f.snapshot()),"TEXT");}
 {Fixture f;auto e=QJsonDocument::fromJson(f.db.raw("SELECT receipt FROM dispatch_resume_epochs WHERE run=?",f.journals[1].run)).object();auto cp=e.value("checkpoint").toObject();auto tuple=cp.value("resume").toObject();tuple.insert("checkpointSha256",QString(64,'0'));cp.insert("resume",tuple);e.insert("checkpoint",cp);f.db.exec("UPDATE dispatch_resume_epochs SET receipt=? WHERE run=?",{QString::fromUtf8(compact(e)),f.journals[1].run});held(f.verify(f.snapshot()),"SHA");}
 {Fixture f;auto s=f.snapshot();s.remove("schema");held(f.verify(s),"schema");}
}
void historicalCompletionScope(){
 {Fixture f(3,true,true);auto s=f.snapshot(-1,true);auto stale=f.snapshot().value("source_completion_observation").toObject();stale.insert("run",QString(32,'e'));s.insert("completion",stale);const auto exact=compact(s),db=f.db.dump(),history=f.hdb.dump(),raw=hashFile(f.path(0));const auto p=f.verify(s);check(p.value("ready")==true&&p.value("terminal")=="stopped","valid foreign cached completion cannot hide the current stopped capture");check(compact(s)==exact&&s.value("completion").toObject()==stale&&f.db.dump()==db&&f.hdb.dump()==history&&hashFile(f.path(0))==raw,"historical receipt/bytes/DB retained verbatim, never relabelled or erased");}
 {Fixture f;auto s=f.snapshot();const auto current=s.value("source_completion_observation").toObject();auto stale=current;stale.insert("run",QString(32,'e'));s.insert("completion",stale);const auto exact=compact(s);check(f.verify(s)==f.verify(f.snapshot()),"foreign cached completion does not alter current complete raw-chain proof/hash");check(compact(s)==exact&&s.value("completion").toObject()==stale,"current complete selection leaves foreign receipt unchanged");auto contradictory=current;contradictory.insert("source_session_gaps","999");s.insert("completion_candidate",contradictory);held(f.verify(s),"contradictory");}
 for(int poison=0;poison<4;++poison){Fixture f(3,true,true);auto s=f.snapshot(-1,true);QJsonObject stale{{"run",QString(32,'e')}};if(poison==0)stale.remove("run");if(poison==1)stale.insert("run","unknown");if(poison==2)stale.insert("run",QString(32,'e')+'\n');if(poison==3)stale.insert("run",123);s.insert("completion",stale);held(f.verify(s));}
 {Fixture f;auto s=f.snapshot();auto contradictory=s.value("source_completion_observation").toObject();contradictory.insert("source_tests",QString::number(TestBase+1));s.insert("completion",contradictory);held(f.verify(s),"contradictory");}
}
void cycle(){Fixture f(3,true,true);auto e=QJsonDocument::fromJson(f.db.raw("SELECT receipt FROM dispatch_resume_epochs WHERE run=?",f.journals.last().run)).object();e.insert("run",f.journals[0].run);e.insert("previous_run",f.journals.last().run);e.insert("request_id",request(50));e.insert("observation_id",nonce(50));auto cp=e.value("checkpoint").toObject();auto tuple=cp.value("resume").toObject();tuple.insert("run",f.journals.last().run);cp.insert("resume",tuple);e.insert("checkpoint",cp);f.db.exec("INSERT INTO dispatch_resume_epochs VALUES(?,?,?,?,?)",{f.journals[0].run,f.journals.last().run,f.booking,request(50),QString::fromUtf8(compact(e))});auto m=f.journals[0].manifest;m.insert("resume_previous_run",f.journals.last().run);m.insert("checkpoint_input_sha256",f.journals.last().checkpoint.resume.checkpointSha256);f.manifestPoison(0,m);held(f.verify(f.snapshot(-1,true)),"cycle");}
QJsonObject legacyProfile(){return {{"schema","pgn-run-source-profile-1"},{"protocol","pgn-source-legacy-a73"},{"image_sha256","a73a6e383308560edc36ce351b09eb449fa099275361be74be89020b03864b43"},{"executable","C:/PGN/native/20261012-gpu-owned-source-v1/bin/pgn-continuous-gpu.exe"}};}
void runSourceProfiles(){
 for(int mode=0;mode<6;++mode){
  Fixture f(1,false,false,0,false);auto& j=f.journals[0];
  if(mode){auto profile=legacyProfile();if(mode==2)j.manifest.insert("source_profile",QJsonValue::Null);else{if(mode==3)profile.insert("protocol","pgn-source-boundary-v2");if(mode==4)profile.remove("image_sha256");if(mode==5)profile.insert("image_sha256",QString(64,'f'));j.manifest.insert("source_profile",profile);}f.manifestPoison(0,j.manifest);}
  f.write(0,j.bytes);f.append(j.run,j.bytes);
  const auto booking=f.db.dump(),history=f.hdb.dump(),rawHash=hashFile(f.path(0));const auto p=f.verify(f.snapshot());
  if(mode<2){check(p.value("ready")==true&&p.value("raw_lineage_ready")==true,"legacy lineage preserved with absent or known exact profile");check(p.value("source_profile_status")== (mode?"known-legacy-a73":"historical-unattested"),"absence never reconstructed as known image");}
  else{held(p,"profile");check(p.value("raw_lineage_ready")!=true&&f.stats.opened==0,"malformed/unknown present profile fails before any RAW reader, never legacy downgrade");}
  check(f.db.dump()==booking&&f.hdb.dump()==history&&hashFile(f.path(0))==rawHash,"profile qualification never rewrites manifests, history or RAW");
 }
}
void terminalBoundaryV2(){
 const auto logicalOnly=[](const QJsonObject& p){return p.value("raw_lineage_ready")==true&&p.value("ready")==false&&p.value("error").toString().contains("not admitted")&&p.value("chain_sha256").toString().size()==64;};
 const auto exercise=[](bool equality,int poison=0,bool noOwned=false){
  Fixture f(1,false,false,noOwned?0:1,false);auto& j=f.journals[0];
  j.bytes.replace("descriptors=2 ","descriptors=1 ");
  j.bytes.replace("coverage=SOURCE_PRP_INDEPENDENT_PROOF_PENDING\n","coverage=SOURCE_PRP_INDEPENDENT_PROOF_PENDING boundary_protocol=pgn-source-boundary-v2\n");
  const auto p=addUnsigned(First,poison==12?"10000":"9000");
  const auto q=addUnsigned(First,equality?"10000":"10002");
  if(!noOwned){
   const auto begin=j.bytes.indexOf("[phase0-gpu] GAP "),end=j.bytes.indexOf('\n',begin);
   need(begin>=0&&end>begin,"terminal fixture GAP row exists");
   const auto gap=source_decimal::difference(q,p);
   j.bytes.replace(begin,end-begin,QString("[phase0-gpu] GAP merit=20.0000 gap=%1 lower=%2 upper=%3 verified=1 proof=GPU_MR12_EULER_COMPOSITE record=off table=unknown unix_ts=1791936000 time_basis=source_batch_publication").arg(gap,p,q).toUtf8());
  }
  const auto examined=noOwned?QString("10000"):QString::number(source_decimal::difference(Last,p).toULongLong()+source_decimal::difference(q,p).toULongLong());
  QByteArray marker=QString("[pgn-source-boundary-v2] descriptor=0 owned_first=%1 owned_end=%2 status=%3 terminal_p=%4 terminal_q=%5 examined=%6 source_tests=2 scope=SOURCE_PRP_TERMINAL_NOT_INDEPENDENT_COVERAGE\n").arg(First,Last,noOwned?"2":"1",noOwned?"none":p,noOwned?"none":q,examined).toUtf8();
  if(poison==1)marker.clear();
  if(poison==2)marker.replace("status=1 ","status=7 ");
  if(poison==3)marker.replace(("terminal_p="+p).toUtf8(),("terminal_p="+addUnsigned(First,"9997")).toUtf8());
  if(poison==4)marker.replace(("terminal_q="+q).toUtf8(),("terminal_q="+addUnsigned(First,"10003")).toUtf8());
  if(poison==5)marker.replace(("owned_first="+First).toUtf8(),("owned_first="+addUnsigned(First,"1")).toUtf8());
  if(poison==6)marker.replace(("owned_end="+Last).toUtf8(),("owned_end="+addUnsigned(First,"9999")).toUtf8());
  if(poison==7)marker.replace("descriptor=0 ","descriptor=1 ");
  if(poison==8)marker+=marker;
  if(poison==10){marker.replace("status=1 ","status=2 ");marker.replace(("terminal_p="+p).toUtf8(),"terminal_p=none");marker.replace(("terminal_q="+q).toUtf8(),"terminal_q=none");}
  if(poison==11)marker.replace("SOURCE_PRP_TERMINAL_NOT_INDEPENDENT_COVERAGE","INDEPENDENT_COVERAGE");
  const auto at=j.bytes.indexOf(poison==9?"[phase0-gpu] GAP ":"[pgn-counters]");need(at>=0,"terminal fixture publication insertion");j.bytes.insert(at,marker);
  if(poison==13){j.manifest.insert("source_profile",legacyProfile());f.manifestPoison(0,j.manifest);}
  f.write(0,j.bytes);f.append(j.run,j.bytes);return f.verify(f.snapshot());
 };
 for(bool equality:{false,true}){const auto p=exercise(equality);check(logicalOnly(p),"v2 crossing/equality grammar conserved, NEVER operationally ready without qualified run profile");check(p.value("source_boundary_protocol")=="pgn-source-boundary-v2"&&p.value("source_boundary_groups")=="1"&&p.value("source_profile_status")=="historical-unattested","v2 logical result reports exact groups and absence, not inferred image authority");}
 {const auto p=exercise(false,0,true);check(logicalOnly(p)&&p.value("source_boundary_protocol")=="pgn-source-boundary-v2","NoOwnedPrime grammar conserved without fabricated endpoint0 or operational admission");}
 for(int poison=1;poison<=12;++poison){const auto p=exercise(false,poison);held(p);check(p.value("raw_lineage_ready")!=true,"bad v2 grammar cannot hide behind the unqualified-image guard");}
 {const auto p=exercise(false,13);check(logicalOnly(p)&&p.value("source_profile_status")=="known-legacy-a73","valid A73 profile cannot license v2 even when RAW grammar is exact");}
 const auto declare=[](QByteArray& bytes){bytes.replace("coverage=SOURCE_PRP_INDEPENDENT_PROOF_PENDING\n","coverage=SOURCE_PRP_INDEPENDENT_PROOF_PENDING boundary_protocol=pgn-source-boundary-v2\n");};
 {Fixture f(1,true,true,0,false);auto& j=f.journals[0];j.positions=j.gaps=j.tests=j.batches=0;j.checkpoint=checkpoint(j.run,0,0,0,0,111);j.bytes=raw(0,0,0,0,0,111,false);declare(j.bytes);f.write(0,j.bytes);f.append(j.run,j.bytes);const auto p=f.verify(f.snapshot(-1,true));check(logicalOnly(p)&&p.value("terminal")=="stopped"&&p.value("source_boundary_protocol")=="pgn-source-boundary-v2"&&p.value("source_boundary_groups")=="0","v2 zero prefix logically conserved, protocol admission still required");}
 {Fixture f(1,true,true,0,false);auto& j=f.journals[0];declare(j.bytes);const auto marker=QString("[pgn-source-boundary-v2] descriptor=0 owned_first=%1 owned_end=%2 status=2 terminal_p=none terminal_q=none examined=300 source_tests=0 scope=SOURCE_PRP_TERMINAL_NOT_INDEPENDENT_COVERAGE\n").arg(First,addUnsigned(First,"300")).toUtf8();j.bytes.insert(j.bytes.indexOf("[pgn-counters]"),marker);const auto at=j.bytes.indexOf("[pgn-counters]"),end=j.bytes.indexOf('\n',at)+1;const auto repeated=j.bytes.mid(at,end-at);j.bytes.insert(j.bytes.indexOf("[phase0-gpu] stopped"),repeated);f.write(0,j.bytes);f.append(j.run,j.bytes);const auto p=f.verify(f.snapshot(-1,true));check(logicalOnly(p)&&p.value("source_boundary_groups")=="1","v2 repeated STOP counter does not fabricate a second descriptor or admission");}
 {Fixture f(2,true,true,0,false);f.write(0,f.journals[0].bytes);f.append(f.journals[0].run,f.journals[0].bytes);auto& j=f.journals.last();declare(j.bytes);f.write(1,j.bytes);f.append(j.run,j.bytes);const auto p=f.verify(f.snapshot(-1,true));check(logicalOnly(p)&&p.value("epochs")==2&&p.value("source_boundary_protocol")=="pgn-source-boundary-v2"&&p.value("source_boundary_groups")=="0","v2 current zero-delta resume over legacy ancestor retains current protocol and Held admission");}
 {Fixture f(2,true,true,0,false);auto& parent=f.journals[0];declare(parent.bytes);const auto marker=QString("[pgn-source-boundary-v2] descriptor=0 owned_first=%1 owned_end=%2 status=2 terminal_p=none terminal_q=none examined=300 source_tests=0 scope=SOURCE_PRP_TERMINAL_NOT_INDEPENDENT_COVERAGE\n").arg(First,addUnsigned(First,"300")).toUtf8();parent.bytes.insert(parent.bytes.indexOf("[pgn-counters]"),marker);for(int i=0;i<2;++i){f.write(i,f.journals[i].bytes);f.append(f.journals[i].run,f.journals[i].bytes);}const auto p=f.verify(f.snapshot(-1,true));check(logicalOnly(p)&&p.value("epochs")==2&&!p.contains("source_boundary_protocol"),"unadmitted ancestor v2 cannot be laundered through a legacy current epoch");}
}
void terminalGroupSemantics(){
 // All bytes are synthetic, enrolled through actual SourceHistory/SQLite.
 // Pair semantics are stronger than matching aggregate counters: the first
 // group crosses an INTERNAL boundary, below the complete request end.
 const auto exercise=[](int poison){
  const bool missing=poison==2,extra=poison==4||poison==5||poison==6;
  const quint64 records=missing?1:(extra?3:2);
  Fixture f(1,false,false,records,false);auto& j=f.journals[0];j.batches=2;
  const auto absolute=[](quint64 n){return addUnsigned(First,QString::number(n));};
  const auto gap=[&](quint64 p,quint64 q){return QString("[phase0-gpu] GAP merit=20.0000 gap=%1 lower=%2 upper=%3 verified=1 proof=GPU_MR12_EULER_COMPOSITE record=off table=unknown time_basis=source_batch_publication time_status=unavailable\n").arg(q-p).arg(absolute(p),absolute(q)).toUtf8();};
  const auto marker=[&](int descriptor,quint64 first,quint64 end,quint64 p,quint64 q,quint64 examined,quint64 tests){return QString("[pgn-source-boundary-v2] descriptor=%1 owned_first=%2 owned_end=%3 status=1 terminal_p=%4 terminal_q=%5 examined=%6 source_tests=%7 scope=SOURCE_PRP_TERMINAL_NOT_INDEPENDENT_COVERAGE\n").arg(descriptor).arg(absolute(first),absolute(end),absolute(p),absolute(q)).arg(examined).arg(tests).toUtf8();};
  const auto counter=[&](int group,quint64 positions,quint64 gaps){return QString("[pgn-counters] time_ns=%1 positions=%2 gaps=%3 tests=%4 batches=%5 processed_gaps=%3\n").arg(110+group).arg(positions).arg(gaps).arg(TestBase-1+group).arg(group+1).toUtf8();};
  QByteArray bytes=QString("[phase0-gpu] engine=GPU_OWNED_CONTINUOUS descriptors=2 original_start=%1 length=10000 proof=GPU_MR12_GAP coverage=SOURCE_PRP_INDEPENDENT_PROOF_PENDING boundary_protocol=pgn-source-boundary-v2\n").arg(First).toUtf8();
  if(!missing)bytes+=gap(poison==1||poison==6?4000:4500,poison==1?4999:(poison==6?5000:5300));
  if(poison==4)bytes+=gap(4600,5500); // two distinct same-group crossings
  if(poison==5)bytes+=gap(4500,5300); // duplicate crossing cannot be paid twice
  if(poison==6)bytes+=gap(4100,4999); // lower beyond terminal p, upper still internal
  const auto tp=poison==1||poison==6?4000ULL:(poison==4?4600ULL:4500ULL),tq=poison==1?4999ULL:(poison==3?5301ULL:(poison==4?5500ULL:(poison==6?5000ULL:5300ULL)));
  const auto examined=5000-tp+tq-tp;
  bytes+=marker(0,0,5000,tp,tq,poison==7?examined+1:examined,poison==8?examined+1:(poison==9?1:2));
  const quint64 firstGaps=missing?0:(extra?2:1);
  bytes+=counter(0,5000,firstGaps);bytes+=gap(9000,10002);
  bytes+=marker(1,5000,10000,9000,10002,2002,2);bytes+=counter(1,10000,records);
  bytes+=QString("[phase0-gpu] gaps reported=%1 verification_failures=0 records_new=0\n[phase0-gpu] --- end ---\n").arg(records).toUtf8();
  j.bytes=bytes;f.write(0,bytes);f.append(j.run,bytes);const auto booking=f.db.dump(),history=f.hdb.dump(),hash=hashFile(f.path(0));const auto proof=f.verify(f.snapshot());
  check(f.db.dump()==booking&&f.hdb.dump()==history&&hashFile(f.path(0))==hash,"v2 group semantics leave DB/history/raw unchanged");
  check(f.stats.opened==1&&f.stats.closed==1&&f.stats.maximum<=65536,"v2 semantic success/failure releases bounded actual reader");
  return proof;
 };
 {const auto p=exercise(0);check(p.value("raw_lineage_ready")==true&&p.value("source_boundary_groups")=="2","exact internal crossing, terminal merge, final clip and separate PRP probes conserved");}
 for(int poison=1;poison<=9;++poison){const auto p=exercise(poison);check(p.value("raw_lineage_ready")!=true,"invalid per-group terminal must fail BEFORE profile admission");held(p);}
 // NoOwnedPrime probes EVERY owned position, including rejected wheel/sieve
 // entries. source_tests counts only surviving PRP calls, never walk.tests.
 for(int poison=0;poison<3;++poison){Fixture f(1,true,true,0,false);auto& j=f.journals[0];j.bytes.replace("coverage=SOURCE_PRP_INDEPENDENT_PROOF_PENDING\n","coverage=SOURCE_PRP_INDEPENDENT_PROOF_PENDING boundary_protocol=pgn-source-boundary-v2\n");const auto marker=QString("[pgn-source-boundary-v2] descriptor=0 owned_first=%1 owned_end=%2 status=2 terminal_p=none terminal_q=none examined=%3 source_tests=%4 scope=SOURCE_PRP_TERMINAL_NOT_INDEPENDENT_COVERAGE\n").arg(First,addUnsigned(First,"300")).arg(poison==1?299:300).arg(poison==2?301:0).toUtf8();j.bytes.insert(j.bytes.indexOf("[pgn-counters]"),marker);f.write(0,j.bytes);f.append(j.run,j.bytes);const auto p=f.verify(f.snapshot(-1,true));if(poison){held(p);check(p.value("raw_lineage_ready")!=true,"NoOwnedPrime probe cost corruption cannot be hidden by source profile");}else check(p.value("raw_lineage_ready")==true,"NoOwnedPrime exact span/zeroPRP is conserved without independent-prime assertion");}
}
void retainedTerminalSource(const QString& root){
 // ONLY this explicitly selected immutable artifact set is opened READONLY.
 // Private SQLite enrollment and manifest/profile are reconstructed TESTONLY,
 // never an adoption/backfill/attestation of the original diagnostic manifest.
 check(QFileInfo(root).isAbsolute()&&QFileInfo(root).isDir()&&!QFileInfo(root).isSymLink(),"explicit retained artifact directory required");
 QMap<QString,QByteArray> pins;QMap<QString,qint64> sizes;
 const auto read=[&](const QString& relative,qint64 maximum){const auto path=QDir(root).filePath(relative);const auto info=QFileInfo(path);check(info.isFile()&&!info.isSymLink()&&info.size()>0&&info.size()<=maximum,"bounded retained regular file required");QFile file(path);need(file.open(QIODevice::ReadOnly),"retained artifact readonly open");const auto bytes=file.read(maximum+1);check(bytes.size()==info.size()&&file.atEnd(),"retained artifact complete bounded read");pins.insert(path,QCryptographicHash::hash(bytes,QCryptographicHash::Sha256));sizes.insert(path,bytes.size());return bytes;};
 const auto object=[&](const QByteArray& bytes){QJsonParseError error;const auto doc=QJsonDocument::fromJson(bytes,&error);check(error.error==QJsonParseError::NoError&&doc.isObject(),"retained metadata JSON object required");return doc.object();};
 const auto receipt=object(read("completion.json",16384));const auto original=object(read("complete/process.json",16384));const auto exit=object(read("complete/last-exit.json",8192));
 const auto run=original.value("run").toString(),device=original.value("uuid").toString(),first=original.value("start").toString(),last=original.value("end").toString();
 check(run=="6229a1173e244b369d7d436ab323981b"&&device=="GPU-33f44d5c-91e4-6ab6-1e45-8c93135fd265"&&first=="199932999868386508800"&&last=="199933000000000000000","exact retained root tail3 identity/window required");
 check(original.value("schema")=="pgn-native-process-1"&&!original.contains("source_profile")&&original.value("image_sha256")=="74A0FDD01CCF242FA3CA8714E4765C23022987C6785889CD8B1ECFA3AA27472B","original diagnostic profile absence preserved, not retroactively authenticated");
 check(exit.value("run")==run&&exit.value("pid")==original.value("pid")&&exit.value("exit_code").isDouble()&&exit.value("exit_code").toDouble()==0&&receipt.value("process")==original&&receipt.value("actual_exit")==exit,"actual retained process/exit/receipt agree");
 check(receipt.value("schema")=="pgn-last-window-complete-1"&&receipt.value("accepted")==true&&receipt.value("positions")=="131613491200"&&receipt.value("tests")=="439506086"&&receipt.value("gaps")=="17"&&receipt.value("batches")=="3"&&receipt.value("local_descriptor_count")==3&&receipt.value("last_owned_length")=="2764472320"&&receipt.value("interior_pairs")==17&&receipt.value("terminal_crossing_pairs")==0&&receipt.value("independent_coverage")==false&&receipt.value("full_original_block_coverage")==false,"exact narrow retained SOURCE counters/clip, never full1e15 or independent coverage");
 const auto rawPath=QDir(root).filePath("complete/current-job/"+run+".stdout");const auto bytes=read("complete/current-job/"+run+".stdout",16777216),gapLog=read("complete/current-job/gaps.log",16777216);
 check(QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()).toUpper()==receipt.value("stdout_sha256").toString()&&QString::fromLatin1(QCryptographicHash::hash(gapLog,QCryptographicHash::Sha256).toHex()).toUpper()==receipt.value("gaplog_sha256").toString(),"actual retained SOURCE/GAP hashes agree with physical receipt");
 QByteArray extracted;for(auto row:bytes.split('\n')){if(row.endsWith('\r'))row.chop(1);if(row.startsWith("[phase0-gpu] GAP "))extracted+=row+'\n';}
 auto normalizedLog=gapLog;normalizedLog.replace("\r\n","\n");check(extracted==normalizedLog,"actual stdout and gap log exact rows conserved, not a JSON-only loading test");
 const auto profile=QJsonObject{{"schema","pgn-run-source-profile-1"},{"protocol","pgn-source-boundary-v2"},{"image_sha256","74a0fdd01ccf242fa3ca8714e4765c23022987c6785889cd8b1ecfa3aa27472b"},{"executable","C:/PGN/native/20261015-source-boundary-v2/bin/pgn-continuous-gpu.exe"}};
 QJsonObject finalProof;
 for(bool enrolledProfile:{false,true}){
  QTemporaryDir temp;check(temp.isValid(),"own temporary retained-native fixture");const auto bookingFile=temp.filePath("booking.sqlite"),historyFile=temp.filePath("history.sqlite");BookingStore store(bookingFile);SourceHistory history(historyFile);Sql db(bookingFile),hdb(historyFile);check(store.valid(),"real temporary BookingStore");
  SourceRegistry registry;registry.url="https://PUBLIC-TESTONLY.invalid/retained-source";registry.sha256=QString(64,'1');registry.observedUtc=QDateTime::fromString("2026-10-15T00:00:00Z",Qt::ISODate);registry.ranges={{1,first,last,"Open",{}}};check(store.queue(registry,first,last,device).isEmpty(),"TESTONLY queued identity at actual retained bounds");const auto length=source_decimal::difference(last,first);const auto initial=store.prepareInitialStart(request(4000),device,{first,last,first,last,first,length},registry,{WorkBoundary::Ready,nonce(4000)});check(initial.status==InitialStartStatus::Prepared&&initial.dispatch.has_value(),"TESTONLY native admission without launching");const auto booking=initial.dispatch->bookingId;
  db.exec("UPDATE dispatch_requests SET state='STARTED',run=? WHERE id=?",{run,booking});db.exec("UPDATE bookings SET state='DISPATCHED' WHERE id=?",{booking});check(store.observeRuntime(run,first,last,device,false).isEmpty(),"TESTONLY paused physical row, not current ownership observation");
  QJsonObject manifest{{"schema","pgn-native-process-1"},{"run",run},{"uuid",device},{"pid",original.value("pid")},{"birth",original.value("birth")},{"start",first},{"end",last},{"stdout",original.value("stdout")},{"queued_intent",booking},{"test_only_provenance","RECONSTRUCTED_SQLITE_ENROLLMENT_NOT_ORIGINAL_PROFILE_OR_PROCESS_ATTESTATION"},{"original_manifest_sha256",QString::fromLatin1(pins.value(QDir(root).filePath("complete/process.json")).toHex())}};
  if(enrolledProfile)manifest.insert("source_profile",profile);
  check(store.saveSourceRunManifest(device,manifest).isEmpty(),"TESTONLY immutable source manifest inserted only in temporary SQLite");history.bindSourceIdentity(run,QString::fromUtf8(compact(QJsonObject{{"run",run},{"uuid",device.toLower()},{"start",first},{"end",last},{"pid",original.value("pid")},{"birth",original.value("birth")}})),true);
  qint64 offset=0;while(offset<bytes.size()){auto chunk=bytes.mid(offset,65536);const auto newline=chunk.lastIndexOf('\n');need(newline>=0,"actual retained full-row enrollment");chunk=chunk.left(newline+1);history.append(run,offset,chunk);offset+=chunk.size();}
  const auto beforeBooking=db.dump(),beforeHistory=hdb.dump();Stats stats;
  SourceResumeLineageVerifier verifier(bookingFile,historyFile,[&](const QString& requested,const QString& source)->std::unique_ptr<HeldSourceLineageReader>{need(requested==run&&source=="native/"+run+".stdout","actual retained reader exact run/source");return std::make_unique<Reader>(rawPath,requested,source,Mode::Normal,stats);});
  QJsonObject completion{{"schema","pgn-source-range-completion-1"},{"run",run},{"queued_intent",booking},{"start",first},{"end",last},{"source",original.value("stdout")},{"source_positions",receipt.value("positions")},{"source_session_gaps",receipt.value("gaps")},{"source_tests",receipt.value("tests")},{"source_exit_code",exit.value("exit_code")},{"source_range_complete",true},{"independent_coverage",false},{"proof_status","SOURCE_PRP_INDEPENDENT_PROOF_PENDING"},{"source_tests_complete",true},{"source_position_evidence","final_counter"}};
  QJsonObject snapshot{{"schema","pgn-runtime-snapshot-1"},{"state","OFF"},{"owned",false},{"manifest",manifest},{"last_exit",exit},{"source_completion_observation",completion},{"completion_candidate",completion}};const auto proof=verifier.verify(snapshot);snapshot.insert("resume_lineage",proof);
  check(proof.value("raw_lineage_ready")==true&&proof.value("epochs")==1&&proof.value("terminal")=="complete"&&proof.value("source_boundary_groups")=="3"&&proof.value("source_boundary_protocol")=="pgn-source-boundary-v2","native verifier reads actual retained terminal RAW/markers/counters via real prefix enrollment");
  if(enrolledProfile){check(proof.value("ready")==true&&proof.value("source_profile_status")=="known-boundary-v2-74a"&&completeSourceMatches(snapshot,run),"TESTONLY known74A profile admits actual retained RAW to native SOURCE gate");finalProof=proof;}
  else check(proof.value("ready")==false&&proof.value("source_profile_status")=="historical-unattested"&&!completeSourceMatches(snapshot,run),"actual original profile absence remains operationally Held, never inferred from header");
  check(stats.opened==1&&stats.closed==1&&stats.finished==1&&stats.maximum<=65536,"actual retained readonly held reader bounded and released");check(db.dump()==beforeBooking&&hdb.dump()==beforeHistory,"native verification did not mutate temporary enrollment/store");
 }
 for(auto i=pins.begin();i!=pins.end();++i){const auto size=sizes.value(i.key());QFile file(i.key());check(QFileInfo(i.key()).isFile()&&!QFileInfo(i.key()).isSymLink()&&file.size()==size&&file.open(QIODevice::ReadOnly),"retained final readonly regular file/size unchanged");const auto after=file.read(size+1);check(after.size()==size&&file.atEnd()&&QCryptographicHash::hash(after,QCryptographicHash::Sha256)==i.value(),"retained RAW/process/exit/receipt all bytes unchanged after both cases");}
 std::cout<<compact(QJsonObject{{"schema","pgn-retained-terminal-native-test-1"},{"checks",checks},{"run",run},{"actual_source_sha256",QString::fromLatin1(pins.value(rawPath).toHex())},{"source_positions",receipt.value("positions")},{"source_tests",receipt.value("tests")},{"source_gaps",receipt.value("gaps")},{"source_groups","3"},{"raw_lineage_ready",true},{"original_absent_profile_operationally_held",true},{"synthetic_known74a_profile_ready",true},{"fixture_chain_sha256",finalProof.value("chain_sha256")},{"scope","ACTUAL_RETAINED_SOURCE_BYTES_NATIVE_VERIFIER_WITH_TESTONLY_SQLITE_PROFILE_NOT_PROCESS_ATTESTATION_OR_PRIME_COVERAGE"}}).constData()<<'\n';
}
}

void adaptiveEngineLineage(const QString& fixture){
 QFile input(fixture);check(input.open(QIODevice::ReadOnly)&&input.size()<65536,"bounded actual native95 source fixture");auto original=input.readAll();original.replace("\r\n","\n");
 const QString first="20000000000000000000000000000",last="20000000000000002000000000000";
 const auto job=primeGapEngineJob(first,last,64);
 const auto headerEnd=original.indexOf('\n'),counterEnd=original.indexOf('\n',headerEnd+1);
 check(headerEnd>0&&counterEnd>headerEnd,"actual fixture header and first descriptor counter");
 const auto header=original.left(headerEnd+1),firstCounter=original.mid(headerEnd+1,counterEnd-headerEnd);
 const quint64 prefix=64424509420ULL,tests=167459534ULL,time=222064558046900ULL;
 QTemporaryDir temp;check(temp.isValid(),"owned adaptive lineage root");
 const auto bookingFile=temp.filePath("booking.sqlite"),historyFile=temp.filePath("history.sqlite");
 BookingStore store(bookingFile);SourceHistory history(historyFile);Sql db(bookingFile);
 SourceRegistry registry;registry.url="https://PUBLIC-TESTONLY.invalid/adaptive";registry.sha256=QString(64,'1');registry.observedUtc=QDateTime::fromString("2026-10-30T00:00:00Z",Qt::ISODate);registry.ranges={{1,first,last,"Open",{}}};
 check(store.queue(registry,first,last,Device).isEmpty(),"owned adaptive local intent");
 const auto initial=store.prepareInitialStart(request(8000),Device,{first,last,first,last,first,"2000000000000"},registry,{WorkBoundary::Ready,nonce(8000)});
 check(initial.status==InitialStartStatus::Prepared&&initial.dispatch,"owned adaptive initial admission");const auto booking=initial.dispatch->bookingId;
 const auto parent=runId(8001),child=runId(8002);
 StoredSourceResume saved;saved.checkpointBytes="PGNS";for(unsigned i=0;i<4;++i)saved.checkpointBytes.append(char((job.minimumGap>>(8*i))&255));
 for(quint64 word:{job.lowWord,job.highWord,job.positions,prefix,tests,quint64(0),quint64(1)})for(unsigned i=0;i<8;++i)saved.checkpointBytes.append(char((word>>(8*i))&255));
 auto& cp=saved.resume;cp.run=parent;cp.start=first;cp.end=last;cp.length=job.length;cp.positions=QString::number(prefix);cp.nextStart=addUnsigned(first,cp.positions);cp.remaining=subtractUnsigned(cp.length,cp.positions);cp.tests=QString::number(tests);cp.gaps="0";cp.batches="1";cp.counterTimeNs=QString::number(time);cp.checkpointLabel=Label;cp.checkpointSha256=QString::fromLatin1(QCryptographicHash::hash(saved.checkpointBytes,QCryptographicHash::Sha256).toHex());cp.dialect=SourceCheckpointDialect::ScoutGapBound;cp.gapMinimum=job.minimumGap;
 const QJsonObject profile{{"schema","pgn-run-source-profile-2"},{"protocol","PGN_NATIVE_GAP_RUNTIME_V1"},{"image_sha256","093c8037a6a8bf2082e5ac8a3f85a31b6d57aa6fbcaa741cb99f1fae3845a91d"},{"executable","C:/PUBLIC-TESTONLY/engines/worker-k64.exe"},{"source_manifest_sha256",primeGapSourceManifest().toLower()},{"checkpoint_schema","PGNS_GAP32_64B"},{"job",QJsonObject{{"start",first},{"end",last},{"profile_k",64},{"minimum_gap","1332"}}}};
 QJsonObject finalManifest;
 for(int index=0;index<2;++index){
  const auto run=index?child:parent;
  if(index){check(store.saveResumeCheckpoint(Device,cp,saved.checkpointBytes).isEmpty(),"owned actual-shaped adaptive checkpoint");const auto epoch=store.prepareCheckpointResume(request(8002),Device,booking,parent,child,cp.checkpointSha256,nonce(8002));check(epoch.status==CheckpointResumeAdmissionStatus::Prepared,"native immutable PGNS resume epoch");check(epoch.receiptUtf8.contains("PGNS_GAP32_64B_CUMULATIVE_BASELINE"),"explicit adaptive cumulative baseline tag");}
  db.exec("UPDATE dispatch_requests SET state='STARTED',run=? WHERE id=?",{run,booking});db.exec("UPDATE bookings SET state='DISPATCHED' WHERE id=?",{booking});check(store.observeRuntime(run,first,last,Device,false).isEmpty(),"owned paused runtime observation, not physical proof");
  QJsonObject manifest{{"schema","pgn-native-process-1"},{"run",run},{"uuid",Device},{"queued_intent",booking},{"start",first},{"end",last},{"stdout","C:/PUBLIC-TESTONLY/"+run+".stdout"},{"pid",4242+index},{"birth","2026-10-30T00:00:00.123456789Z"},{"source_profile",profile}};
  if(index){manifest.insert("resume_previous_run",parent);manifest.insert("checkpoint_input_sha256",cp.checkpointSha256);}check(store.saveSourceRunManifest(Device,manifest).isEmpty(),"owned immutable adaptive source manifest");
  history.bindSourceIdentity(run,QString::fromUtf8(compact(QJsonObject{{"run",run},{"uuid",Device.toLower()},{"start",first},{"end",last},{"pid",manifest.value("pid")},{"birth",manifest.value("birth")}})),true);
  const auto bytes=index?header+original.mid(counterEnd+1):header+firstCounter+QString("[phase0-gpu] stopped positions=%1 checkpoint=%2\n").arg(prefix).arg(Label).toUtf8();
  QFile output(temp.filePath(run+".stdout"));check(output.open(QIODevice::WriteOnly)&&output.write(bytes)==bytes.size()&&output.flush(),"own held adaptive raw file");output.close();history.append(run,0,bytes);
  finalManifest=manifest;
 }
 QJsonObject snapshot{{"schema","pgn-runtime-snapshot-1"},{"state","OFF"},{"owned",false},{"manifest",finalManifest},{"last_exit",QJsonObject{{"run",child},{"exit_code",0}}},
  {"source_completion_observation",QJsonObject{{"schema","pgn-source-range-completion-1"},{"run",child},{"queued_intent",booking},{"start",first},{"end",last},{"source",finalManifest.value("stdout")},{"source_positions",job.length},{"source_session_gaps","8"},{"source_tests","5198622089"},{"source_exit_code",0},{"source_range_complete",true},{"independent_coverage",false},{"proof_status","SOURCE_PRP_INDEPENDENT_PROOF_PENDING"},{"source_tests_complete",true},{"source_position_evidence","final_counter"}}}};
 Stats stats;SourceResumeLineageVerifier verifier(bookingFile,historyFile,[&](const QString& run,const QString& source)->std::unique_ptr<HeldSourceLineageReader>{return std::make_unique<Reader>(temp.filePath(run+".stdout"),run,source,Mode::Normal,stats);});
 const auto proof=verifier.verify(snapshot);if(!proof.value("ready").toBool())throw std::runtime_error("Adaptive held lineage: "+proof.value("error").toString().toStdString());
 check(proof.value("epochs").toInt()==2&&proof.value("source_profile_status")=="known-adaptive-prime-gap"&&proof.value("source_boundary_protocol")=="PGN_NATIVE_GAP_RUNTIME_V1","adaptive same-booking two-epoch raw proof");
 check(stats.opened==2&&stats.closed==2&&stats.finished==2,"both actual held source handles verified and released");
}

void nativeSearchOriginLineage(const QString& fixture){
 QFile input(fixture);check(input.open(QIODevice::ReadOnly)&&input.size()<65536,"bounded actual source corpus for native origin");auto bytes=input.readAll();bytes.replace("\r\n","\n");
 const QString first="20000000000000000000000000000",last="20000000000000002000000000000",run=runId(8100),hash(64,'a');const auto job=primeGapEngineJob(first,last,64);
 QTemporaryDir temp;const auto bookingFile=temp.filePath("booking.sqlite"),historyFile=temp.filePath("history.sqlite");BookingStore store(bookingFile);SourceHistory history(historyFile);
 const QJsonObject profile{{"schema","pgn-run-source-profile-2"},{"protocol","PGN_NATIVE_GAP_RUNTIME_V1"},{"image_sha256","093c8037a6a8bf2082e5ac8a3f85a31b6d57aa6fbcaa741cb99f1fae3845a91d"},{"executable","C:/PUBLIC-TESTONLY/engines/worker-k64.exe"},{"source_manifest_sha256",primeGapSourceManifest().toLower()},{"checkpoint_schema","PGNS_GAP32_64B"},{"job",QJsonObject{{"start",first},{"end",last},{"profile_k",64},{"minimum_gap","1332"}}}};
 const QJsonObject work{{"schema","PGN_NATIVE_SCIENTIFIC_WORK1"},{"genesis",hash},{"parent",hash},{"height",5},{"request_id",hash},{"ticket",hash},{"start",first},{"length","4000000000000"},{"segments",256},{"report_root",QString(64,'0')},{"work_id",hash},{"challenge","PUBLIC-TESTONLY search, no approval"},{"miner_script","00"},{"reward_script","00"},{"target_numerator","1899633"},{"target_denominator","65536"},{"science_policy_script","00"},{"accepted",false}};
 const QJsonObject context{{"schema","pgn-native-scientific-source-context-2"},{"authority","SEARCH_CONTEXT_NOT_NATIVE_ACCEPTANCE_OR_RANGE_CERTIFICATE"},{"rpc_work_sha256",hash},{"rpc_work",work},{"search_slice",QJsonObject{{"schema","pgn-native-search-slice-1"},{"work_id",hash},{"start",first},{"end",last}}}};
 const QJsonObject manifest{{"schema","pgn-native-process-1"},{"source_kind","NATIVE_PGN_SCIENCE"},{"run",run},{"uuid",Device},{"queued_intent",QJsonValue::Null},{"start",first},{"end",last},{"stdout","C:/PUBLIC-TESTONLY/"+run+".stdout"},{"pid",4242},{"birth","2026-10-30T00:00:00.123456789Z"},{"source_profile",profile},{"native_scientific_context",context}};
 check(store.saveSourceRunManifest(Device,manifest).isEmpty(),"observed native search origin requires no made-up queue");
 history.bindSourceIdentity(run,QString::fromUtf8(compact(QJsonObject{{"run",run},{"uuid",Device.toLower()},{"start",first},{"end",last},{"pid",manifest.value("pid")},{"birth",manifest.value("birth")}})),true);
 QFile output(temp.filePath(run+".stdout"));check(output.open(QIODevice::WriteOnly)&&output.write(bytes)==bytes.size()&&output.flush(),"owned native-origin held raw file");output.close();history.append(run,0,bytes);
 const QJsonObject completion{{"schema","pgn-source-range-completion-1"},{"run",run},{"queued_intent",QJsonValue::Null},{"start",first},{"end",last},{"source",manifest.value("stdout")},{"source_positions",job.length},{"source_session_gaps","8"},{"source_tests","5198622089"},{"source_exit_code",0},{"source_range_complete",true},{"independent_coverage",false},{"proof_status","SOURCE_PRP_INDEPENDENT_PROOF_PENDING"},{"source_tests_complete",true},{"source_position_evidence","final_counter"}};
 const QJsonObject snapshot{{"schema","pgn-runtime-snapshot-1"},{"state","OFF"},{"owned",false},{"manifest",manifest},{"last_exit",QJsonObject{{"run",run},{"exit_code",0}}},{"source_completion_observation",completion}};
 Stats stats;SourceResumeLineageVerifier verifier(bookingFile,historyFile,[&](const QString& selected,const QString& source)->std::unique_ptr<HeldSourceLineageReader>{return std::make_unique<Reader>(temp.filePath(selected+".stdout"),selected,source,Mode::Normal,stats);});
 const auto proof=verifier.verify(snapshot);if(!proof.value("ready").toBool())throw std::runtime_error("Native search origin: "+proof.value("error").toString().toStdString());
 check(proof.value("epochs").toInt()==1&&proof.value("booking_id").isNull()&&proof.value("source_origin")=="native_search","single physical origin without fake dispatch");
 auto admitted=snapshot;admitted.insert("resume_lineage",proof);admitted.insert("completion_candidate",completion);
 check(completeSourceMatches(admitted,run),"native conservation is accepted by the completion ABI without a fake booking");
 check(stats.opened==1&&stats.closed==1&&stats.finished==1,"held raw pin verified before positive proof");
 auto wrong=snapshot;auto c=completion;c.insert("queued_intent",nonce(8101));wrong.insert("source_completion_observation",c);check(verifier.verify(wrong).value("ready")==false,"native origin cannot borrow community completion identity");
 c=completion;c.insert("source_tests","5198622088");wrong=snapshot;wrong.insert("source_completion_observation",c);check(verifier.verify(wrong).value("ready")==false,"native origin preserves exact raw counter conservation");
 auto m=manifest;auto changed=context;auto bounds=changed.value("search_slice").toObject();bounds.insert("work_id",QString(64,'b'));changed.insert("search_slice",bounds);m.insert("native_scientific_context",changed);wrong=snapshot;wrong.insert("manifest",m);check(verifier.verify(wrong).value("ready")==false,"native origin work binding cannot change");
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{
 if(app.arguments().contains("--native-search-origin-source")){need(app.arguments().size()==3,"native origin actual source fixture required");nativeSearchOriginLineage(app.arguments().at(2));std::cout<<"Native search origin conservation PASS checks="<<checks<<"; held actual95 corpus/private SQLite only, no process/GPU/reservation/whole-ticket claim\n";return 0;}
 if(app.arguments().contains("--adaptive-engine-source")){need(app.arguments().size()==3,"adaptive source fixture argument required");adaptiveEngineLineage(app.arguments().at(2));std::cout<<"Existing PGN adaptive held lineage PASS checks="<<checks<<"; actual95 data split at real descriptor boundary, own SQLite/held files, no process/GPU/coverage claim\n";return 0;}
 if(app.arguments().contains("--retained-terminal-source")){const auto at=app.arguments().indexOf("--retained-terminal-source");need(app.arguments().size()==3&&at==1,"exact retained mode and artifact directory required");retainedTerminalSource(app.arguments().at(2));return 0;}
 if(app.arguments().contains("--terminal-boundary-only")){runSourceProfiles();terminalBoundaryV2();terminalGroupSemantics();std::cout<<"Native terminal v2 lineage/profile: "<<checks<<" checks / 0 failures; logical grammar distinct from operational admission; closed public fixture only\n";return 0;}
 if(app.arguments().contains("--terminal-group-only")){terminalGroupSemantics();std::cout<<"Native terminal v2 per-group semantics: "<<checks<<" checks / 0 failures; closed synthetic RAW only\n";return 0;}
 if(app.arguments().contains("--large-prefix-benchmark")){
  // Structural parser cost ONLY. Repeated public test pairs are intentional:
  // lineage verifies RAW/counter conservation, not primality or unique coverage.
  constexpr quint64 records=20000;
  Fixture f(1,false,false,records);
  const auto beforeBooking=f.db.dump(),beforeHistory=f.hdb.dump(),rawHash=hashFile(f.path(0));
  const auto snapshot=f.snapshot();
  QElapsedTimer timer;timer.start();const auto proof=f.verify(snapshot);const auto elapsed=timer.nsecsElapsed();
  check(proof.value("ready")==true&&proof.value("epochs")==1&&proof.value("terminal")=="complete","large prefix complete exact logical proof");
  check(f.stats.opened==1&&f.stats.closed==1&&f.stats.finished==1&&f.stats.maximum<=65536,"large prefix held reader bounded and drained");
  check(f.db.dump()==beforeBooking&&f.hdb.dump()==beforeHistory&&hashFile(f.path(0))==rawHash,"large prefix leaves native rows and RAW exact");
  const QJsonObject result{{"schema","pgn-lineage-parser-closed-benchmark-1"},{"records",qint64(records)},{"raw_bytes",qint64(f.journals[0].bytes.size())},{"raw_sha256",QString::fromLatin1(rawHash.toHex())},{"verify_ns",elapsed},{"ready",true},{"checks",checks},{"scope","SYNTHETIC_LOGICAL_LINEAGE_NOT_REAL_GUI_GPU_OR_PRIME_COVERAGE"}};
  std::cout<<compact(result).constData()<<'\n';return 0;
 }
 if(!app.arguments().contains("--enrollment-only")){positive();preparedResumeReconciliation();stoppedAndDepth();readerFailures();rawPoisons();metadataPoisons();historicalCompletionScope();cycle();runSourceProfiles();terminalBoundaryV2();terminalGroupSemantics();}fragmentedEnrollment();std::cout<<"Native raw SOURCE resume lineage: "<<checks<<" checks / 0 failures; actual temporary native SQLite and held raw files, bounded reads; TESTONLY producer-shaped data; no remote/GPU/physical freshness/independent coverage or integration claim\n";return 0;}catch(const std::exception& e){std::cerr<<"Native raw SOURCE resume lineage failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
