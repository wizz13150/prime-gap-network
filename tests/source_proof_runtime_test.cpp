#include "verification/source_proof_runtime.h"
#include "storage/sqlite_read.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QThread>
#include <QFile>
#include <iostream>
using namespace pgn::verification;
namespace {
unsigned checks{},failures{};
void Check(bool b,const char* text){++checks;if(!b){++failures;std::cerr<<"FAIL "<<text<<'\n';}}
void exec(const QString& path,const char* sql){sqlite3* db{};if(sqlite3_open(path.toUtf8().constData(),&db)!=SQLITE_OK)throw std::runtime_error("fixture open");
    const int rc=sqlite3_exec(db,sql,nullptr,nullptr,nullptr);const std::string error=sqlite3_errmsg(db);sqlite3_close(db);if(rc!=SQLITE_OK)throw std::runtime_error(error);}
bool wait(const std::function<bool()>& done,int ms=5000){QElapsedTimer timer;timer.start();while(!done()&&timer.elapsed()<ms){QCoreApplication::processEvents(QEventLoop::AllEvents,10);QThread::msleep(1);}return done();}
qint64 count(const QString& path){if(!QFile::exists(path))return 0;pgn::storage::ReadOnlyDatabase db(path);pgn::storage::ReadStatement s(db.db,"SELECT COUNT(*) FROM source_gap_proofs");s.step();return sqlite3_column_int64(s.value,0);}
QJsonObject index(const QString& run,qint64 rev){return {{"type","native_index"},{"complete",true},{"current",QJsonObject{{"scope",run},{"length",QJsonObject{{"revision",rev},{"points",rev}}}}}};}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{
    QTemporaryDir dir;const auto source=dir.path()+"/source.sqlite",journal=dir.path()+"/proof.sqlite";const QString A(32,'a'),B(32,'b');
    exec(source,"CREATE TABLE native_source_identities(run TEXT PRIMARY KEY,identity TEXT NOT NULL);"
        "CREATE TABLE origins(gap_id INTEGER NOT NULL,run TEXT NOT NULL,source TEXT NOT NULL,UNIQUE(gap_id,run,source));CREATE INDEX native_origins_run_gap ON origins(run,gap_id);"
        "CREATE TABLE gaps(id INTEGER PRIMARY KEY,p TEXT NOT NULL,q TEXT NOT NULL);"
        "INSERT INTO native_source_identities VALUES('aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa','identity A'),('bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb','identity B');"
        "INSERT INTO gaps VALUES(1,'19','23'),(2,'3','5'),(3,'19','29'),(4,'199932000019059617039','199932000019059617887'),(5,'2179','2203');"
        "INSERT INTO origins SELECT id,'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa','A' FROM gaps WHERE id<=4; INSERT INTO origins VALUES(5,'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb','B');");
    const SourceProofPolicy policy{1,{{128,32,32},1000},{1000,65536,32,1}};
    {
        SourceProofRuntime runtime(source,journal,policy);bool stopped=false;int callbacks=0;
        runtime.onStopped=[&]{stopped=true;};runtime.onPage=[&](const auto& p){++callbacks;Check(p.error.isEmpty()&&p.processed==1&&p.after==1,"first real async page exact; no fabricated advance");runtime.stop();};
        runtime.observeNativeIndex(index(A,4));Check(wait([&]{return stopped;}),"stop callback waits for actual worker completion");
        Check(runtime.stopped()&&callbacks==1&&count(journal)==1,"stop after page commits exactly one row, no queued continuation enters");
    }
    {
        SourceProofRuntime runtime(source,journal,policy);int callbacks=0;QString first;bool complete=false,stopped=false;
        runtime.onStopped=[&]{stopped=true;};runtime.onPage=[&](const auto& p){if(!callbacks++)first=p.run;Check(p.error.isEmpty(),"asynchronous resumed page has no hidden error");
            if(count(journal)==5){complete=true;runtime.stop();}};
        // A notification is coalesced away before worker entry. A's remaining
        // three records MUST survive in native identities+durable proof cursor.
        runtime.observeNativeIndex(index(A,4));runtime.observeNativeIndex(index(B,1));
        Check(wait([&]{return complete&&stopped;}),"coalesced live source plus deferred source both complete after restart");
        Check(first==B&&count(journal)==5&&!runtime.held(),"new source priority does not discard interrupted source A");
    }
    {
        const auto never=dir.path()+"/never.sqlite";SourceProofRuntime runtime(source,never,policy);bool stopped=false;
        runtime.onStopped=[&]{stopped=true;};runtime.observeNativeIndex(index(A,4));runtime.stop();
        Check(stopped&&runtime.stopped()&&!QFile::exists(never),"stop before queued entry creates no journal/worker side effect");
        QCoreApplication::processEvents();Check(!QFile::exists(never),"stale queued callback cannot restart stopped actor");
    }
    {
        SourceProofRuntime runtime(source,source,policy);int errors=0;runtime.onPage=[&](const auto& p){if(!p.error.isEmpty())++errors;};
        runtime.observeNativeIndex(index(A,4));Check(wait([&]{return errors==1;}),"foreign source/output error is actually surfaced");
        Check(runtime.held(),"error holds worker instead of polling/resending");runtime.observeNativeIndex(index(A,5));
        wait([]{return false;},30);Check(errors==1,"new live packet does not auto-retry held error");runtime.retry();
        Check(wait([&]{return errors==2;})&&runtime.held(),"only explicit proof retry makes one new bounded attempt");runtime.stop();Check(runtime.stopped(),"held worker drains normally");
    }
    {
        const auto pausedJournal=dir.path()+"/paused.sqlite";SourceProofRuntime runtime(source,pausedJournal,policy);int pages=0;bool complete=false;
        runtime.setEnabled(false);runtime.observeNativeIndex(index(A,4));wait([]{return false;},40);
        Check(!QFile::exists(pausedJournal),"disabled CPU audit creates no journal and performs no candidate work");
        runtime.onPage=[&](const auto& p){Check(p.error.isEmpty(),"paused audit resumes without a fabricated failure");++pages;if(pages==1)runtime.setEnabled(false);if(count(pausedJournal)==5){complete=true;runtime.stop();}};
        runtime.setEnabled(true);Check(wait([&]{return pages==1;}),"explicit CPU enable executes one real page");wait([]{return false;},40);
        Check(count(pausedJournal)==1&&!runtime.held(),"pause preserves its exact durable cursor and blocks continuation");
        runtime.setEnabled(true);Check(wait([&]{return complete&&runtime.stopped();}),"resume conserves current and deferred sources and drains normally");
    }
    const auto absent=dir.path()+"/scan-no-write.sqlite";auto scan=scanNativeSourceProofWork(source,absent,policy);
    Check(scan.error.isEmpty()&&scan.run==A&&!QFile::exists(absent),"metadata discovery is readonly and finds never-enrolled work");
    auto done=scanNativeSourceProofWork(source,journal,policy);
    Check(done.error.isEmpty()&&done.run.isEmpty()&&!done.more,"durable caught-up source cursors are not falsely queued again");
    QString extra="BEGIN;";for(unsigned i=0;i<70;++i)extra+="INSERT INTO native_source_identities VALUES('"+QString::number(i,16).rightJustified(32,'0')+"','empty source');";extra+="COMMIT;";exec(source,extra.toUtf8().constData());
    auto bounded=scanNativeSourceProofWork(source,journal,policy);
    Check(bounded.error.isEmpty()&&bounded.run.isEmpty()&&bounded.more&&bounded.afterRun==QString::number(63,16).rightJustified(32,'0'),"metadata scan stops after exactly64 rows, no growing full read");
    auto tail=scanNativeSourceProofWork(source,journal,policy,bounded.afterRun);
    Check(tail.error.isEmpty()&&tail.run.isEmpty()&&!tail.more,"keyset successor covers remainder without skipping boundary");
    std::cout<<"SOURCE_PROOF_RUNTIME "<<checks<<" checks / "<<failures<<" failures; private async actor, actual SQLite cursor stop/restart, coalesced deferred sources, error Held/explicit retry, bounded metadata; NO userUI/GPU/network/mining\n";
    return failures?1:0;
}catch(const std::exception& e){std::cerr<<"EXCEPTION "<<e.what()<<'\n';return 2;}}
