#include "mining/held_win32_lineage_sources.h"
#include "mining/source_session.h"
#include "booking/booking_store.h"
#include "booking/decimal_range.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <sqlite3.h>
#include <future>
#include <iostream>
#include <stdexcept>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace pgn::mining;
using namespace pgn::booking;
namespace {
int checks{};
void need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void check(bool ok,const char* why){++checks;need(ok,why);}
template<class F>void rejected(F f,const char* why){bool failed=false;try{f();}catch(const std::exception&){failed=true;}check(failed,why);}
QString run(int n){return QString::number(n+1,16).rightJustified(32,'0');}
const QString Device="GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac",First="184467440737095516260",Last="184467440737095526260",Label="C:/PUBLIC-TESTONLY/current-job/checkpoint";
QByteArray compact(const QJsonObject& o){return QJsonDocument(o).toJson(QJsonDocument::Compact);}
void write(const QString& path,const QByteArray& bytes){QFile f(path);need(f.open(QIODevice::WriteOnly|QIODevice::NewOnly)&&f.write(bytes)==bytes.size()&&f.flush(),"new own fixture file write");}
QByteArray read(const QString& path){QFile f(path);need(f.open(QIODevice::ReadOnly),"own fixture read");return f.readAll();}
void sql(const QString& path,const char* text,const QStringList& args={}){
    sqlite3* db{};need(sqlite3_open_v2(path.toUtf8().constData(),&db,SQLITE_OPEN_READWRITE,nullptr)==SQLITE_OK,"own fixture SQLite open");sqlite3_stmt* q{};
    const auto prepared=sqlite3_prepare_v2(db,text,-1,&q,nullptr);if(prepared!=SQLITE_OK){sqlite3_close(db);throw std::runtime_error("own fixture SQL prepare");}
    for(int i=0;i<args.size();++i){const auto b=args[i].toUtf8();sqlite3_bind_text(q,i+1,b.constData(),b.size(),SQLITE_TRANSIENT);}
    const auto code=sqlite3_step(q);sqlite3_finalize(q);sqlite3_close(db);need(code==SQLITE_DONE,"own fixture SQL mutation");
}
void blocked(const QString& path){
#ifdef Q_OS_WIN
    const auto name=reinterpret_cast<LPCWSTR>(path.utf16());
    const auto writer=CreateFileW(name,GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
    const bool denied=writer==INVALID_HANDLE_VALUE&&GetLastError()==ERROR_SHARING_VIOLATION;if(writer!=INVALID_HANDLE_VALUE)CloseHandle(writer);check(denied,"real external second writer denied while held");
    const auto deleting=CreateFileW(name,DELETE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
    const bool deleteDenied=deleting==INVALID_HANDLE_VALUE&&GetLastError()==ERROR_SHARING_VIOLATION;if(deleting!=INVALID_HANDLE_VALUE)CloseHandle(deleting);check(deleteDenied,"real external delete handle denied while held");
#endif
}
void mirrorLease(){
    QTemporaryDir temp;check(temp.isValid(),"own temporary directory");SourceMirror mirror(temp.path());const auto id=run(0);const QByteArray current="current source\n";mirror.append(id,0,current);const auto before=mirror.inspect(id);
    for(int i=1;i<=12;++i)write(mirror.file(run(i)),QByteArray(140000,char('a'+i)));
    {
        HeldWin32LineageSources scope(mirror);auto open=scope.opener();check(mirror.lineageFrozen(),"writer frozen before worker");
        rejected([&]{mirror.append(id,current.size(),"late\n");},"append refuses while frozen");rejected([&]{mirror.append(run(90),0,{});},"create refuses while frozen");
        rejected([&]{mirror.inspect(run(1));},"inspect cannot open/evict while frozen");rejected([&]{mirror.read(id,0,1,before->handle_epoch);},"normal reader cannot race duplicate file pointer");rejected([&]{HeldWin32LineageSources second(mirror);},"overlapping freeze refused");
        auto active=open(id,"native/"+id+".stdout");check(active->isRegularFile()&&!active->isSymbolicLink()&&active->size()==current.size(),"active RW-held file duplicated without incompatible open");blocked(mirror.file(id));check(active->read(0,65536)==current,"same held current source read");
        rejected([&]{active->read(0,65537);},"read maximum bounded before I/O");rejected([&]{active->read(-1,1);},"negative offset rejected");rejected([&]{active->read(current.size()+1,1);},"past EOF rejected");
        for(int i=1;i<=12;++i){auto history=open(run(i),"native/"+run(i)+".stdout");check(history->size()==140000,"historical existing file exact size");check(history->read(65530,65536)==QByteArray(65536,char('a'+i)),"historical exact offset/64KiB bounded read");blocked(mirror.file(run(i)));history->closeAndVerifyStable();rejected([&]{history->read(0,1);},"closed reader cannot be reused");}
        rejected([&]{open(id,"../"+id);},"wrong source label refused");rejected([&]{open(id+"\n","native/"+id+"\n.stdout");},"run suffix refused");rejected([&]{open(run(90),"native/"+run(90)+".stdout");},"missing historical source never created");check(!QFile::exists(mirror.file(run(90))),"all failed operations leave missing file absent");
        // Two duplicates share a synchronous file pointer. Worker reads are
        // explicitly serialized by the shared mirror mutex, not by chance.
        auto second=open(id,"native/"+id+".stdout");auto task=std::async(std::launch::async,[&]{for(int i=0;i<120;++i)need(active->read(0,7)==current.left(7),"duplicate A exact seek");});
        for(int i=0;i<120;++i)check(second->read(3,9)==current.mid(3,9),"duplicate B independent explicit offset");task.get();active->closeAndVerifyStable();second->closeAndVerifyStable();
    }
    check(!mirror.lineageFrozen(),"last scope/opener/reader release restores writer");const auto after=mirror.inspect(id);check(before->handle_epoch==after->handle_epoch,"history reads beyond LRU limit never evict current incarnation");check(read(mirror.file(id))==current,"blocked writer never changed bytes");mirror.append(id,current.size(),"late\n");check(read(mirror.file(id))==current+"late\n","writer can proceed after release only");
    bool ownerRefused=false;auto wrong=std::async(std::launch::async,[&]{try{HeldWin32LineageSources invalid(mirror);}catch(const std::exception&){ownerRefused=true;}});wrong.get();check(ownerRefused&&!mirror.lineageFrozen(),"worker cannot secretly acquire owner freeze");
}
void lifetimeAndFailures(){
    QTemporaryDir temp;check(temp.isValid(),"own temporary directory");const auto id=run(0);auto mirror=std::make_unique<SourceMirror>(temp.path());mirror->append(id,0,"abc\n");OpenHeldLineageSource open;
    {HeldWin32LineageSources scope(*mirror);open=scope.opener();}check(mirror->lineageFrozen(),"opener OWNS freeze after caller scope destruction");auto reader=open(id,"native/"+id+".stdout");open={};check(mirror->lineageFrozen(),"last reader owns freeze after opener destruction");reader->closeAndVerifyStable();check(!mirror->lineageFrozen(),"explicit stable close releases last reader lease");
    {HeldWin32LineageSources scope(*mirror);auto local=scope.opener();reader=local(id,"native/"+id+".stdout");}mirror.reset();check(reader->read(0,65536)=="abc\n","reader retains root/original exclusion after mirror owner destruction");reader.reset();
    // Own historical file: change ONLY its timestamp through a metadata handle
    // (WRITE_DATA remains denied) to discriminate the final pin check.
    SourceMirror next(temp.path());write(next.file(run(2)),"pin\n");
    {HeldWin32LineageSources scope(next);auto local=scope.opener();reader=local(run(2),"native/"+run(2)+".stdout");
#ifdef Q_OS_WIN
        const auto path=next.file(run(2));const auto attr=CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()),FILE_WRITE_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);check(attr!=INVALID_HANDLE_VALUE,"own metadata handle available");FILETIME time{};GetSystemTimeAsFileTime(&time);ULARGE_INTEGER shifted{};shifted.LowPart=time.dwLowDateTime;shifted.HighPart=time.dwHighDateTime;shifted.QuadPart+=1000000000ULL;time.dwLowDateTime=shifted.LowPart;time.dwHighDateTime=shifted.HighPart;const bool set=SetFileTime(attr,nullptr,nullptr,&time);CloseHandle(attr);check(set,"own timestamp pin poison written");rejected([&]{reader->closeAndVerifyStable();},"same handle timestamp changed rejects before promotion");rejected([&]{reader->size();},"failed close still closes reader");
#endif
        reader.reset();
    }
    check(!next.lineageFrozen(),"failed final pin/reader destruction always releases freeze");
    {HeldWin32LineageSources scope(next);auto local=scope.opener();for(int i=0;i<20;++i)rejected([&]{local(run(70),"native/"+run(70)+".stdout");},"missing source error is bounded/no handle leak");}
    check(!next.lineageFrozen()&&!QFile::exists(next.file(run(70))),"missing failures never create/adopt data");
    check(QDir().mkdir(next.file(run(71))),"own nonregular directory fixture");
    {HeldWin32LineageSources scope(next);rejected([&]{scope.opener()(run(71),"native/"+run(71)+".stdout");},"directory is not an accepted regular source");}
#ifdef Q_OS_WIN
    const auto link=next.file(run(72)),target=next.file(run(2));
    if(CreateSymbolicLinkW(reinterpret_cast<LPCWSTR>(link.utf16()),reinterpret_cast<LPCWSTR>(target.utf16()),0x2)){
        {HeldWin32LineageSources scope(next);rejected([&]{scope.opener()(run(72),"native/"+run(72)+".stdout");},"real reparse/symlink rejected without following");}
        check(QFile::remove(link),"own symlink fixture removed");
    }else std::cout<<"HELD_WIN32_LINEAGE symlink fixture unavailable WindowsError="<<GetLastError()<<"; no reparse runtime claim\n";
#endif
}
void sessionReplay(){
    QTemporaryDir temp;check(temp.isValid(),"own temporary directory");SourceMirror mirror(temp.path());SourceHistory history(temp.filePath("history.sqlite"));SourceSession session(history,mirror);const auto id=run(0);session.consume(id,0,4,"one\n");const auto before=session.conservation(id,4,0);check(before.ready,"session admitted before freeze");
    {HeldWin32LineageSources scope(mirror);rejected([&]{session.consume(id,4,8,"two\n");},"frozen session rejects before offsets/fragment/proof changes");}
    check(session.conservation(id,4,0).ready&&history.cursorRecord(id)->offset==4,"failed frozen consume preserved full prior session proof");const auto replay=session.consume(id,4,8,"two\n");check(!replay.needsReplay&&replay.committedOffset==8&&read(mirror.file(id))=="one\ntwo\n","caller replay conserves rejected chunk exactly once");
}
struct LineageFixture {
    QTemporaryDir temp;QString bookingPath,historyPath,booking;BookingStore store;SourceHistory history;SourceMirror mirror;
    QJsonObject manifest,snapshot;QByteArray bytes;
    LineageFixture():bookingPath(temp.filePath("booking.sqlite")),historyPath(temp.filePath("history.sqlite")),store(bookingPath),history(historyPath),mirror(temp.path()){
        need(temp.isValid()&&store.valid(),"own actual native stores");SourceRegistry registry;registry.url="https://PUBLIC-TESTONLY.invalid/source";registry.sha256=QString(64,'1');registry.observedUtc=QDateTime::fromString("2026-10-14T00:00:00Z",Qt::ISODate);registry.ranges={{1,First,Last,"Open",{}}};need(store.queue(registry,First,Last,Device).isEmpty(),"real native manual intent");const auto initial=store.prepareInitialStart("20000000-0000-4000-8000-000000000001",Device,{First,Last,First,Last,First,"10000"},registry,{WorkBoundary::Ready,"30000000-0000-4000-8000-000000000001"});need(initial.status==InitialStartStatus::Prepared&&initial.dispatch.has_value(),"real initial admission, no launch");booking=initial.dispatch->bookingId;
        // Explicit public DTO fixtures, not an initialized worker/physical ACK.
        sql(bookingPath,"UPDATE dispatch_requests SET state='STARTED',run=? WHERE id=?",{run(0),booking});sql(bookingPath,"UPDATE bookings SET state='DISPATCHED' WHERE id=?",{booking});need(store.observeRuntime(run(0),First,Last,Device,false).isEmpty(),"real paused public runtime row");
        manifest={{"schema","pgn-native-process-1"},{"run",run(0)},{"uuid",Device},{"queued_intent",booking},{"start",First},{"end",Last},{"stdout","C:/PUBLIC-TESTONLY/"+run(0)+".stdout"},{"pid",4242},{"birth","2026-10-14T00:00:00.123456789Z"}};need(store.saveSourceRunManifest(Device,manifest).isEmpty(),"actual immutable manifest store");
        history.bindSourceIdentity(run(0),QString::fromUtf8(compact(QJsonObject{{"run",run(0)},{"uuid",Device.toLower()},{"start",First},{"end",Last},{"pid",manifest.value("pid")},{"birth",manifest.value("birth")}})),true);
        bytes=QString("[phase0-gpu] engine=GPU_OWNED_CONTINUOUS descriptors=2 original_start=%1 length=10000 proof=GPU_MR12_GAP coverage=SOURCE_PRP_INDEPENDENT_PROOF_PENDING\n").arg(First).toUtf8();
        for(int i=0;i<3;++i)bytes+=QString("[phase0-gpu] GAP merit=20.0000 gap=2 lower=%1 upper=%2 verified=1 proof=GPU_MR12_EULER_COMPOSITE record=off table=unknown unix_ts=1791936000 time_basis=source_batch_publication\n").arg(addUnsigned(First,QString::number(10+i*10)),addUnsigned(First,QString::number(12+i*10))).toUtf8();
        bytes+="[pgn-counters] time_ns=111 positions=10000 gaps=3 tests=9007199254740993 batches=1 processed_gaps=3\n[phase0-gpu] gaps reported=3 verification_failures=0 records_new=0\n[phase0-gpu] --- end ---\n";SourceSession session(history,mirror);const auto admitted=session.consume(run(0),0,bytes.size(),bytes);need(!admitted.needsReplay&&admitted.committedOffset==bytes.size(),"actual durable mirror/history admission");
        snapshot={{"schema","pgn-runtime-snapshot-1"},{"state","OFF"},{"owned",false},{"manifest",manifest},{"last_exit",QJsonObject{{"run",run(0)},{"exit_code",0}}},{"source_completion_observation",QJsonObject{{"schema","pgn-source-range-completion-1"},{"run",run(0)},{"queued_intent",booking},{"start",First},{"end",Last},{"source",manifest.value("stdout")},{"source_positions","10000"},{"source_session_gaps","3"},{"source_tests","9007199254740993"},{"source_exit_code",0},{"source_range_complete",true},{"independent_coverage",false},{"proof_status","SOURCE_PRP_INDEPENDENT_PROOF_PENDING"},{"source_tests_complete",true},{"source_position_evidence","final_counter"}}}};
    }
};
void verifierProductAdapter(){
    LineageFixture f;const auto original=f.bytes;QJsonObject proof;
    {
        HeldWin32LineageSources scope(f.mirror);auto open=scope.opener();
        // Freeze starts on owner; verifier/read-only SQLite lives wholly on
        // the owned worker, not on the SourceHistory ingestion connection.
        auto worker=std::async(std::launch::async,[&f,open=std::move(open)]{SourceResumeLineageVerifier verifier(f.bookingPath,f.historyPath,open);return verifier.verify(f.snapshot);});proof=worker.get();check(proof.value("ready")==true,proof.value("error").toString().toUtf8().constData());check(proof.value("epochs")==1&&proof.value("terminal")=="complete"&&proof.value("chain_sha256").toString().size()==64,"actual verifier accepts real held-Win32 adapter/metadata/bytes");
    }
    check(!f.mirror.lineageFrozen()&&read(f.mirror.file(run(0)))==original,"success releases all handles/freeze and preserves raw");
    {HeldWin32LineageSources scope(f.mirror);auto open=scope.opener();int polls=0;SourceResumeLineageVerifier cancelled(f.bookingPath,f.historyPath,open,[&]{return ++polls>=3;});const auto held=cancelled.verify(f.snapshot);check(held.value("ready")==false&&held.value("error").toString().contains("cancelled"),"actual verifier cancellation after acquisition stays Held");}
    check(!f.mirror.lineageFrozen(),"cancelled worker lifetime releases freeze");
    sql(f.historyPath,"UPDATE native_source_batches SET sha256=?",{QString(64,'0')});
    {HeldWin32LineageSources scope(f.mirror);SourceResumeLineageVerifier corrupted(f.bookingPath,f.historyPath,scope.opener());check(corrupted.verify(f.snapshot).value("ready")==false,"actual corrupted metadata cannot become Ready through real adapter");}
    check(!f.mirror.lineageFrozen()&&read(f.mirror.file(run(0)))==original,"failed verification releases freeze without repair");
}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{mirrorLease();lifetimeAndFailures();sessionReplay();verifierProductAdapter();std::cout<<"HELD_WIN32_LINEAGE "<<checks<<" checks / 0 failures; own real Win32 files + SQLite, writer barrier/shared pointer serialization/stable pins/RAII, actual native verifier worker; no SSH/GPU/UI/Node/user files\n";return 0;}catch(const std::exception& e){std::cerr<<"HELD_WIN32_LINEAGE failed after "<<checks<<": "<<e.what()<<'\n';return 1;}}
