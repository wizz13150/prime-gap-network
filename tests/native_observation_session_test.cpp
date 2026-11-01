#include "mining/native_observation_session.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <sqlite3.h>
#include <iostream>
#include <stdexcept>

using namespace pgn::mining;
namespace {
int checks=0;
void require(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
template<class F> void rejected(F action,const char* message){bool failed=false;try{action();}catch(const std::exception&){failed=true;}require(failed,message);}
const QString device="GPU-11111111-2222-3333-4444-555555555555",run(32,'a');
const QByteArray row="[phase0-gpu] GAP merit=1.9000 gap=6 lower=23 upper=29 verified=1 proof=GPU_MR12_EULER_COMPOSITE record=off table=unknown unix_ts=1791982800 time_basis=source_batch_publication\n";
QJsonObject packet(const QString& sourceRun=run,qint64 offset=0,const QByteArray& bytes={}){
    return {{"schema","pgn-runtime-snapshot-1"},{"state","OFF"},{"owned",false},{"operator_stop",false},{"pending_bytes",0},
        {"observed_utc","2026-10-14T18:00:00.000000001Z"},{"offset",offset},{"next_offset",offset+bytes.size()},{"chunk",QString::fromLatin1(bytes.toBase64())},
        {"manifest",QJsonObject{{"schema","pgn-native-process-1"},{"run",sourceRun},{"uuid",device},{"pid",qint64(1234)},
            {"birth","2026-10-14T17:00:00.1234567Z"},{"start","199900000000000000000"},{"end","199901000000000000000"}}},
        {"last_exit",QJsonObject{{"run",sourceRun},{"exit_code",0}}},
        {"completion_candidate",QJsonObject{{"schema","pgn-source-range-completion-1"},{"run",sourceRun},
            {"start","199900000000000000000"},{"end","199901000000000000000"},{"source_positions","1000000000000000"},
            {"source_range_complete",true},{"source_exit_code",0}}}};
}
SshObservationFrame frame(const QJsonObject& p){return decodeSshSnapshot(QJsonDocument(p).toJson(QJsonDocument::Indented));}
void field(QJsonObject& p,const char* name,const QJsonValue& value){auto m=p.value("manifest").toObject();m[name]=value;p["manifest"]=m;}
QString scalar(const QString& path,const char* sql){sqlite3* db{};if(sqlite3_open_v2(path.toUtf8().constData(),&db,SQLITE_OPEN_READONLY,nullptr)!=SQLITE_OK)throw std::runtime_error("fixture database open");sqlite3_stmt* st{};if(sqlite3_prepare_v2(db,sql,-1,&st,nullptr)!=SQLITE_OK)throw std::runtime_error("fixture query");QString value;if(sqlite3_step(st)==SQLITE_ROW)value=QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(st,0)));sqlite3_finalize(st);sqlite3_close(db);return value;}
QByteArray contents(const QString& path){QFile f(path);if(!f.open(QIODevice::ReadOnly))throw std::runtime_error("fixture source read");return f.readAll();}
}
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);
    try{
        QTemporaryDir temp;require(temp.isValid(),"temporary directory");const auto database=temp.filePath("history.sqlite");
        {
            SourceHistory history(database);SourceMirror mirror(temp.path());NativeObservationSession session(history,mirror,device);
            auto cold=packet();cold["manifest"]=QJsonValue::Null;
            const auto off=session.consume(frame(cold),{},0,true,false);
            require(off.state==NativeObservationState::Cold&&!off.advance&&off.run.isEmpty(),"cold OFF is not a completion or invented run");
            require(!QFile::exists(mirror.file(run))&&scalar(database,"SELECT COUNT(*) FROM native_source_identities")=="0","cold no source or identity write");
            rejected([&]{session.consume(frame(packet()),{},0,true,false);},"noncold discovery requires explicit selected run");
            for(int n=0;n<11;++n){auto p=packet();switch(n){
                case 0:field(p,"uuid","GPU-99999999-2222-3333-4444-555555555555");break;
                case 1:field(p,"run",QString(32,'b'));break;
                case 2:field(p,"schema","unqualified-process");break;
                case 3:field(p,"start","0199900000000000000000");break;
                case 4:field(p,"end","1");break;
                case 5:field(p,"pid","1234");break;
                case 6:field(p,"pid",0);break;
                case 7:field(p,"birth","not-a-date");break;
                case 8:field(p,"birth","2026-10-14T17:00:00+02:00");break;
                case 9:p.remove("manifest");break;
                case 10:field(p,"pid",qint64(1)<<40);break;
            }rejected([&]{session.consume(frame(p),run,0,true,false);},"bad declared identity held before source mutation");}
            require(history.cursor(run)==0&&!QFile::exists(mirror.file(run)),"identity refusals leave all source bytes untouched");
            auto first=session.consume(frame(packet(run,0,row.left(27))),run,0,true,false);
            require(first.state==NativeObservationState::Source&&first.nextOffset==27&&first.committedOffset==0&&!first.advance,"partial real row conserved but cannot advance");
            require(contents(mirror.file(run))==row.left(27),"partial source exact on disk");
            auto second=session.consume(frame(packet(run,27,row.mid(27))),run,1,true,false);
            require(second.committedOffset==row.size()&&second.displayLines.size()==1&&bool(second.advance),"real decoded frame through durable mirror SQLite then completion gate");
            require(contents(mirror.file(run))==row&&scalar(database,"SELECT COUNT(*) FROM gaps")=="1","complete source and one scientific result exact");
            require(scalar(database,"SELECT unix_ts FROM gaps")=="1791982800"&&scalar(database,"SELECT time_basis FROM gaps")=="source_batch_publication","source clock and basis survive complete adapter");
            require(scalar(database,"SELECT points FROM native_plot_bounds WHERE scope='' AND axis='time'")=="1","actual native timeline indexed");
            const auto ticket=*second.advance;
            require(!session.consume(frame(packet(run,row.size())),run,2,true,false).advance,"in-flight ticket never duplicated");
            require(session.settle(ticket,AdvanceOutcome::UncertainTransport,2).retryable,"uncertain result remains bounded by gate");
            auto retry=packet(run,row.size());retry["observed_utc"]="2026-10-14T18:00:00.000000002Z";
            require(!session.consume(frame(retry),run,10002,true,true).advance,"operator pause dominates completion");
            require(!session.consume(frame(retry),run,10002,false,false).advance,"disabled device dominates completion");
            auto third=session.consume(frame(retry),run,10002,true,false);
            require(third.advance&&third.advance->attempt==2,"new observation after due time produces one second ticket");
            require(session.settle(ticket,AdvanceOutcome::Success,10002).stale,"late old attempt cannot settle new ticket");
            require(session.settle(*third.advance,AdvanceOutcome::Success,10003).held,"success holds this completed run");
            auto changed=packet(run,row.size());field(changed,"end","199902000000000000000");
            rejected([&]{session.consume(frame(changed),run,10004,true,false);},"same run range mutation held");
            require(history.cursor(run)==row.size()&&contents(mirror.file(run))==row,"identity refusal keeps source and cursor exact");
            const auto other=QString(32,'b');auto b=session.consume(frame(packet(other,0,row)),other,10005,true,false);
            require(bool(b.advance)&&b.advance->run==other,"explicit different run gets independent source and gate epoch");
            require(scalar(database,"SELECT COUNT(*) FROM gaps")=="1"&&scalar(database,"SELECT COUNT(*) FROM origins")=="2","same gap across runs deduplicates identity not origins");
            rejected([&]{session.consume(frame(changed),run,10006,true,false);},"durable identity survives switching away and back");
            require(session.settle(*third.advance,AdvanceOutcome::Success,10007).stale,"old run ticket stale after selected run switch");
            cold["offset"]=1;cold["next_offset"]=1;
            rejected([&]{session.consume(frame(cold),{},10008,true,false);},"cold nonzero cursor refused");
        }
        {
            SourceHistory history(database);SourceMirror mirror(temp.path());NativeObservationSession session(history,mirror,device);
            auto changed=packet(run,row.size());field(changed,"pid",qint64(5678));
            rejected([&]{session.consume(frame(changed),run,0,true,false);},"process identity survives actual session and database reopen");
            auto old=session.consume(frame(packet(run,0,row)),run,1,true,false);
            require(old.state==NativeObservationState::ReplayRequired&&old.nextOffset==row.size()&&!old.advance,"old packet after reopen requests cursor, never advances");
            auto p=packet(run,row.size());p["pending_bytes"]=1;
            require(!session.consume(frame(p),run,2,true,false).advance,"remote remaining byte prevents succession");
            p.remove("pending_bytes");require(!session.consume(frame(p),run,3,true,false).advance,"unknown remote remaining bytes never assumed zero");
            require(bool(session.consume(frame(packet(run,row.size())),run,4,true,false).advance),"fresh session proves durable prefix before completion ticket");
            const auto sparse=QString(32,'c');auto missing=session.consume(frame(packet(sparse,99,"x")),sparse,5,true,false);
            require(missing.state==NativeObservationState::ReplayRequired&&missing.nextOffset==0&&!missing.advance&&!QFile::exists(mirror.file(sparse)),"first sparse packet requests zero with no source file");
            const auto rawOnly=QString(32,'d');mirror.append(rawOnly,0,row);
            rejected([&]{session.consume(frame(packet(rawOnly,0,row)),rawOnly,6,true,false);},"unbound legacy raw mirror cannot be silently relabeled");
            require(contents(mirror.file(rawOnly))==row&&history.cursor(rawOnly)==0,"legacy raw refusal retains original bytes without admission");
            const auto indexedOnly=QString(32,'e');history.append(indexedOnly,0,row);
            rejected([&]{session.consume(frame(packet(indexedOnly)),indexedOnly,7,true,false);},"legacy index without source identity requires explicit audit");
            const auto invalid=QString(32,'f');const QByteArray bad="[phase0-gpu] GAP merit=1.9 gap=7 lower=23 upper=29 verified=1\n";
            rejected([&]{session.consume(frame(packet(invalid,0,bad)),invalid,8,true,false);},"scientific mismatch cannot produce a completion ticket");
            require(history.cursor(invalid)==0&&contents(mirror.file(invalid))==bad,"failed scientific admission conserves raw bytes, cursor remains zero");
        }
        std::cout<<"Native observation session: "<<checks<<" checks, 0 failures\n";return 0;
    }catch(const std::exception& e){std::cerr<<"Native observation session failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}
}
