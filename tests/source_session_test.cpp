#include "mining/source_session.h"
#include "../tools/source_batch_time.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <sqlite3.h>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
int checks=0;
void require(bool ok,const char* text){++checks;if(!ok)throw std::runtime_error(text);}
template<class F> void rejected(F operation,const char* text){bool failed=false;try{operation();}catch(const std::exception&){failed=true;}require(failed,text);}
QByteArray gap(const QByteArray& p="23",const QByteArray& q="29",const QByteArray& g="6"){
    return "[phase0-gpu] GAP merit=1.9000 gap="+g+" lower="+p+" upper="+q+" verified=1 proof=GPU_MR12_EULER_COMPOSITE record=off table=unknown\n";
}
QString scalar(const QString& file,const char* sql){sqlite3* db{};if(sqlite3_open_v2(file.toUtf8().constData(),&db,SQLITE_OPEN_READONLY,nullptr)!=SQLITE_OK)throw std::runtime_error("fixture DB open");sqlite3_stmt* stmt{};if(sqlite3_prepare_v2(db,sql,-1,&stmt,nullptr)!=SQLITE_OK)throw std::runtime_error("fixture SQL");QString result;if(sqlite3_step(stmt)==SQLITE_ROW)result=QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt,0)));sqlite3_finalize(stmt);sqlite3_close(db);return result;}
void mutate(const QString& file,const char* sql){sqlite3* db{};if(sqlite3_open(file.toUtf8().constData(),&db)!=SQLITE_OK)throw std::runtime_error("fixture mutation open");const auto code=sqlite3_exec(db,sql,nullptr,nullptr,nullptr);sqlite3_close(db);if(code!=SQLITE_OK)throw std::runtime_error("fixture mutation failed");}
QByteArray contents(const QString& path){QFile f(path);if(!f.open(QIODevice::ReadOnly))throw std::runtime_error("fixture source read");return f.readAll();}
}
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);
    try{
        QTemporaryDir temp;require(temp.isValid(),"temporary directory");const QString run(32,'a');const auto database=temp.filePath("history.sqlite");
        const auto first=gap(),second=gap("31","37"),third=gap("41","43","2");
        {
            pgn::mining::SourceHistory history(database);pgn::mining::SourceMirror mirror(temp.path());pgn::mining::SourceSession session(history,mirror);
            auto result=session.consume(run,0,20,first.left(20));
            require(!result.needsReplay&&result.nextOffset==20&&result.committedOffset==0,"partial source durable but not admitted");
            require(contents(mirror.file(run))==first.left(20)&&history.cursor(run)==0,"partial source exact preserved");
        }
        {
            pgn::mining::SourceHistory history(database);pgn::mining::SourceMirror mirror(temp.path());pgn::mining::SourceSession session(history,mirror);
            auto result=session.consume(run,20,first.size(),first.mid(20));
            require(result.needsReplay&&result.nextOffset==0&&history.cursor(run)==0,"restart replays incomplete tail from commit boundary");
            result=session.consume(run,0,first.size(),first);
            require(!result.needsReplay&&result.committedOffset==first.size()&&result.displayLines.size()==1,"matching overlap completes once");
            require(scalar(database,"SELECT COUNT(*) FROM gaps")=="1"&&contents(mirror.file(run))==first,"one exact row and full conserved bytes");
            result=session.consume(run,0,first.size(),first);require(result.needsReplay&&result.nextOffset==first.size(),"old transport offset requests exact replay");
            // Actual crash seam: bytes durable, SQLite has not seen them yet.
            mirror.append(run,first.size(),second);
        }
        {
            pgn::mining::SourceHistory history(database);pgn::mining::SourceMirror mirror(temp.path());pgn::mining::SourceSession session(history,mirror);
            const qint64 end=first.size()+second.size();auto result=session.consume(run,first.size(),end,second);
            require(result.needsReplay&&result.nextOffset==end,"restart admits durable unindexed complete tail");
            require(history.cursor(run)==end&&scalar(database,"SELECT COUNT(*) FROM gaps")=="2","tail exactly admitted before transport continuation");
            result=session.consume(run,end,end+third.size(),third);
            require(!result.needsReplay&&history.cursor(run)==end+third.size()&&scalar(database,"SELECT COUNT(*) FROM gaps")=="3","native mirror to realSQLite continuation");
            const auto committed=history.cursor(run);const auto source=contents(mirror.file(run));
            rejected([&]{session.consume(run,committed,committed+1,"xx");},"packet length mismatch held");
            rejected([&]{session.consume(run,std::numeric_limits<qint64>::max(),0,"x");},"packet overflow held");
            require(history.cursor(run)==committed&&contents(mirror.file(run))==source,"bad packet zero raw/scientific delta");
            // Evict the active handle; an external rewrite is now possible.
            for(int n=1;n<=9;++n)mirror.append(QString::number(n,16).rightJustified(32,'0'),0,{});
            {QFile rewrite(mirror.file(run));require(rewrite.open(QIODevice::ReadWrite),"evicted fixture source writable");require(rewrite.seek(3)&&rewrite.write("z")==1&&rewrite.flush(),"closed prefix rewrite");}
            const auto changed=contents(mirror.file(run));
            rejected([&]{session.consume(run,committed,committed+7,"status\n");},"new handle incarnation revalidates complete old prefix");
            require(history.cursor(run)==committed&&contents(mirror.file(run))==changed,"rewrite refusal never appends new packet");
        }
        const auto missingDatabase=temp.filePath("missing.sqlite");pgn::mining::SourceHistory missing(missingDatabase);const QString absent(32,'b');missing.append(absent,0,first);
        {pgn::mining::SourceMirror mirror(temp.path());pgn::mining::SourceSession session(missing,mirror);
         rejected([&]{session.consume(absent,0,first.size(),first);},"missing conserved prefix no empty substitute");require(!QFileInfo::exists(mirror.file(absent)),"missing prefix does not create file");}
        const auto emptyDatabase=temp.filePath("empty.sqlite");pgn::mining::SourceHistory empty(emptyDatabase);const QString sparse(32,'c');
        {pgn::mining::SourceMirror mirror(temp.path());pgn::mining::SourceSession session(empty,mirror);const auto replay=session.consume(sparse,10,11,"x");require(replay.needsReplay&&replay.nextOffset==0&&!QFileInfo::exists(mirror.file(sparse)),"first sparse packet requests zero without file creation");}
        const auto failedDatabase=temp.filePath("failed.sqlite");pgn::mining::SourceHistory failed(failedDatabase);const QString badRun(32,'d');
        {pgn::mining::SourceMirror mirror(temp.path());pgn::mining::SourceSession session(failed,mirror);const auto bad=gap("41","43","6");
         rejected([&]{session.consume(badRun,0,first.size()+bad.size(),first+bad);},"bad later row holds entire scientific batch");
         require(failed.cursor(badRun)==0&&scalar(failedDatabase,"SELECT COUNT(*) FROM gaps")=="0","batch rollback no rows/cursor/index");
         require(contents(mirror.file(badRun))==first+bad,"failed admission still conserves original raw bytes");}
        const auto bulkDatabase=temp.filePath("bulk.sqlite");pgn::mining::SourceHistory bulk(bulkDatabase);const QString bulkRun(32,'e');
        {pgn::mining::SourceMirror mirror(temp.path());pgn::mining::SourceSession session(bulk,mirror);QByteArray pending,all;qint64 offset=0;
         auto publish=[&]{if(pending.isEmpty())return;const auto result=session.consume(bulkRun,offset,offset+pending.size(),pending);offset=result.nextOffset;require(!result.needsReplay&&result.committedOffset==offset&&result.displayLines.size()<=512,"bulk exact cursor and bounded rich output");pending.clear();};
         for(int i=0;i<1400;++i){const auto row=gap(QByteArray::number(100000+8*i),QByteArray::number(100006+8*i));if(pending.size()+row.size()>64000)publish();pending+=row;all+=row;}publish();
         require(scalar(bulkDatabase,"SELECT COUNT(*) FROM gaps")=="1400","scientific rows beyond rich512 all retained");
         require(scalar(bulkDatabase,"SELECT points FROM native_plot_bounds WHERE scope='' AND axis='length'")=="1400","all native coordinates beyond512 exact");
         require(contents(mirror.file(bulkRun))==all,"bulk complete raw source exact");
         const auto manyLines=QByteArray(700,'\n');const auto result=session.consume(bulkRun,offset,offset+manyLines.size(),manyLines);
         require(result.displayLines.size()==512&&result.committedOffset==offset+700,"rich output bounded without source byte cap");
         require(contents(mirror.file(bulkRun))==all+manyLines,"nonrich source lines also conserved in full");}
        const auto versionDatabase=temp.filePath("version.sqlite");pgn::mining::SourceHistory versionHistory(versionDatabase);const QString versionRun(32,'f');
        {pgn::mining::SourceMirror mirror(temp.path());pgn::mining::SourceSession session(versionHistory,mirror);session.consume(versionRun,0,first.size(),first);
         mutate(versionDatabase,"UPDATE native_source_batches SET sha256=printf('%064d',0);");
         rejected([&]{session.consume(versionRun,first.size(),first.size()+7,"status\n");},"external digest mutation invalidates held-handle prefix proof before SQL admission");
         require(versionHistory.cursor(versionRun)==first.size()&&scalar(versionDatabase,"SELECT COUNT(*) FROM gaps")=="1","external mutation held no new cursor/science");
         require(contents(mirror.file(versionRun))==first+"status\n","unadmitted new bytes conserved durably");
         rejected([&]{session.consume(versionRun,first.size(),first.size()+7,"status\n");},"reattach detects forged old digest against held source bytes");}
        const auto clockDatabase=temp.filePath("clock.sqlite");pgn::mining::SourceHistory clockHistory(clockDatabase);const QString clockRun(32,'1');
        {pgn::mining::SourceMirror mirror(temp.path());pgn::mining::SourceSession session(clockHistory,mirror);char line[400];
         const auto publication=pgn::source::publicationTimeFromFileTime(116444736000000000ULL+1791982800ULL*10000000ULL);
         pgn::source::formatSourceGap(line,sizeof(line),1.9,6,"23","29",publication);const QByteArray timed(line);session.consume(clockRun,0,timed.size(),timed);
         require(scalar(clockDatabase,"SELECT unix_ts FROM gaps")=="1791982800","actual production formatter time survives native mirror and index");
         require(scalar(clockDatabase,"SELECT time_basis FROM gaps")=="source_batch_publication","source-declared publication basis retained");
         require(scalar(clockDatabase,"SELECT points FROM native_plot_bounds WHERE scope='' AND axis='time'")=="1","actual production time populates full timeline index");
         const auto unavailable=pgn::source::publicationTimeFromFileTime(0);pgn::source::formatSourceGap(line,sizeof(line),1.9,6,"31","37",unavailable);const QByteArray undated(line);
         session.consume(clockRun,timed.size(),timed.size()+undated.size(),undated);
         require(scalar(clockDatabase,"SELECT COUNT(*) FROM gaps")=="2"&&scalar(clockDatabase,"SELECT COUNT(*) FROM gaps WHERE unix_ts IS NULL")=="1","invalid clock never drops source or invents time");
         require(scalar(clockDatabase,"SELECT time_basis FROM gaps WHERE p='31'")=="source_batch_publication","unavailable source basis still retained");
         const auto oldTime=gap("23","29","6");session.consume(clockRun,timed.size()+undated.size(),timed.size()+undated.size()+oldTime.size(),oldTime);
         require(scalar(clockDatabase,"SELECT time_basis FROM gaps WHERE p='23'")=="source_batch_publication","timeless duplicate cannot erase original dated basis");
         const qint64 next=timed.size()+undated.size()+oldTime.size();const auto legacyDate=gap("31","37","6") .trimmed()+" unix_ts=1791982700\n";
         session.consume(clockRun,next,next+legacyDate.size(),legacyDate);
         require(scalar(clockDatabase,"SELECT unix_ts FROM gaps WHERE p='31'")=="1791982700"&&scalar(clockDatabase,"SELECT time_basis IS NULL FROM gaps WHERE p='31'")=="1","first legacy dated observation does not inherit unrelated unavailable-batch basis");}
        std::cout<<"SOURCE_SESSION "<<checks<<" checks / 0 failures; real Win32 durable mirror + realSQLite, restart/partial/replay/epoch rewrite, closed synthetic observations only\n";return 0;
    }catch(const std::exception& error){std::cerr<<"SOURCE_SESSION failed after "<<checks<<": "<<error.what()<<'\n';return 1;}
}
