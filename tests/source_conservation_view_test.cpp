#include "mining/native_observation_session.h"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <sqlite3.h>
#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace pgn::mining;
namespace {
int checks{};
void require(bool ok,const char* text){++checks;if(!ok)throw std::runtime_error(text);}
template<class F>void rejected(F f,const char* text){bool caught=false;try{f();}catch(const std::exception&){caught=true;}require(caught,text);}
const QString run(32,'a'),device="GPU-11111111-2222-3333-4444-555555555555";
const QByteArray row="[phase0-gpu] GAP merit=1.9000 gap=6 lower=23 upper=29 verified=1 proof=GPU_MR12_EULER_COMPOSITE record=off table=unknown unix_ts=1791982800 time_basis=source_batch_publication\n";
QByteArray contents(const QString& path){QFile f(path);if(!f.open(QIODevice::ReadOnly))throw std::runtime_error("fixture read");return f.readAll();}
class Database {
public:
    sqlite3* db{};
    explicit Database(const QString& path){if(sqlite3_open_v2(path.toUtf8().constData(),&db,SQLITE_OPEN_READWRITE,nullptr)!=SQLITE_OK)throw std::runtime_error("fixture DB open");}
    ~Database(){sqlite3_close(db);}
    void exec(const QString& sql){char* message{};const auto code=sqlite3_exec(db,sql.toUtf8().constData(),nullptr,nullptr,&message);const auto error=QString::fromUtf8(message?message:"");sqlite3_free(message);if(code!=SQLITE_OK)throw std::runtime_error(("fixture SQL: "+error).toStdString());}
    QByteArray query(const QString& sql){
        sqlite3_stmt* st{};if(sqlite3_prepare_v2(db,sql.toUtf8().constData(),-1,&st,nullptr)!=SQLITE_OK)throw std::runtime_error("fixture query");
        QList<QByteArray> rows;int code{};
        while((code=sqlite3_step(st))==SQLITE_ROW){QByteArray value;for(int i=0;i<sqlite3_column_count(st);++i){value+=QByteArray::number(sqlite3_column_type(st,i))+":";value+=QByteArray(reinterpret_cast<const char*>(sqlite3_column_blob(st,i)),sqlite3_column_bytes(st,i)).toHex();value+=';';}rows.push_back(value);}
        sqlite3_finalize(st);if(code!=SQLITE_DONE)throw std::runtime_error("fixture query step");std::sort(rows.begin(),rows.end());return rows.join('\n');
    }
    QStringList tables(){sqlite3_stmt* st{};if(sqlite3_prepare_v2(db,"SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%' ORDER BY name",-1,&st,nullptr)!=SQLITE_OK)throw std::runtime_error("fixture tables");QStringList result;while(sqlite3_step(st)==SQLITE_ROW)result.push_back(QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(st,0))));sqlite3_finalize(st);return result;}
    QByteArray dump(){QByteArray result=query("SELECT type,name,tbl_name,sql FROM sqlite_master");for(auto table:tables()){table.replace('"',"\"\"");result+='\n'+table.toUtf8()+'\n'+query("SELECT * FROM \""+table+"\"");}return result;}
    void rejectWrites(){int n=0;for(auto table:tables()){table.replace('"',"\"\"");for(const auto& verb:{"INSERT","UPDATE","DELETE"})exec(QString("CREATE TRIGGER readonly_%1 BEFORE %2 ON \"%3\" BEGIN SELECT RAISE(ABORT,'view wrote data'); END").arg(n++).arg(verb,table));}}
};
void held(const SourceConservation& view,const char* text){require(!view.ready&&!view.error.isEmpty()&&view.error.size()<=400,text);}
void exact(const SourceConservation& view,qint64 end){
    require(view.ready&&view.error.isEmpty(),"complete admitted frontier ready");
    require(view.requiredEnd==end&&view.committedOffset==end&&view.mirrorSize==end&&view.transportOffset==end,"all exact frontiers equal");
    const auto json=view.json();require(json.value("schema")=="pgn-source-conservation-1"&&json.value("scope")=="LOGICAL_SOURCE_EPOCH_CONSERVATION_NOT_GLOBAL_POWER_CUT_DURABILITY","explicit logical-only schema/scope");
    require(json.value("run")==run&&json.value("source")=="native/"+run+".stdout"&&json.value("ready")==true,"source identity and readiness");
    for(const auto* name:{"required_end","committed_offset","mirror_size","transport_offset"})require(json.value(name)==QJsonValue(end),"JSON exact int64 frontier, no double conversion");
}
QJsonObject packet(qint64 offset,const QByteArray& bytes={}){
    return {{"schema","pgn-runtime-snapshot-1"},{"state","OFF"},{"owned",false},{"operator_stop",true},{"pending_bytes",qint64(0)},
        {"offset",offset},{"next_offset",offset+bytes.size()},{"chunk",QString::fromLatin1(bytes.toBase64())},
        {"manifest",QJsonObject{{"schema","pgn-native-process-1"},{"run",run},{"uuid",device},{"pid",qint64(1234)},
            {"birth","2026-10-14T17:00:00.1234567Z"},{"start","199900000000000000000"},{"end","199901000000000000000"}}}};
}
SshObservationFrame frame(const QJsonObject& p){return decodeSshSnapshot(QJsonDocument(p).toJson(QJsonDocument::Compact));}
void missingAndZero(){
    QTemporaryDir temp;require(temp.isValid(),"own temporary directory");SourceHistory history(temp.filePath("history.sqlite"));SourceMirror mirror(temp.path());SourceSession session(history,mirror);
    require(!history.cursorRecord(run),"absent cursor not stored zero");
    held(session.conservation(run,0,0),"unattached missing source held");require(!QFileInfo::exists(mirror.file(run)),"view never creates missing mirror");
    session.consume(run,0,0,{});const auto absent=session.conservation(run,0,0);held(absent,"empty mirror alone not stored cursor zero");require(!absent.committedOffset&&!history.cursorRecord(run),"read does not invent cursor zero");
    history.append(run,0,{});const auto cursor=history.cursorRecord(run);require(cursor&&cursor->offset==0,"genuine SQLite INTEGER0 present");exact(session.conservation(run,0,0),0);
    rejected([&]{history.cursorRecord(run+"\n");},"new cursor view rejects run suffix");
    const auto invalid=session.conservation(QString(32,'z'),0,0);held(invalid,"invalid run held");require(invalid.json().value("run").isNull(),"invalid identity not exported");
}
void completeAndReadonly(){
    QTemporaryDir temp;require(temp.isValid(),"own temporary directory");const auto path=temp.filePath("history.sqlite");
    {
        SourceHistory history(path);SourceMirror mirror(temp.path());SourceSession session(history,mirror);
        const auto consumed=session.consume(run,0,row.size(),row);require(!consumed.needsReplay&&consumed.committedOffset==row.size(),"actual native mirror to SQLite admission");exact(session.conservation(run,row.size(),0),row.size());
        Database db(path);db.rejectWrites();SourceSession refreshed(history,mirror);refreshed.consume(run,row.size(),row.size(),{}); // explicit paid attachment OUTSIDE view
        const auto before=db.dump(),raw=contents(mirror.file(run)),version=db.query("PRAGMA data_version");const auto stamp=QFileInfo(mirror.file(run)).lastModified();
        for(int n=0;n<4;++n)exact(refreshed.conservation(run,row.size(),0),row.size());
        require(db.dump()==before&&db.query("PRAGMA data_version")==version,"view performs no logical DB write with all writes trapped");
        require(contents(mirror.file(run))==raw&&QFileInfo(mirror.file(run)).lastModified()==stamp,"view preserves exact raw bytes and last-write timestamp");
        held(refreshed.conservation(run,row.size()-1,0),"observation behind source not conserved");held(refreshed.conservation(run,row.size(),1),"unreceived pending bytes not conserved");
        const auto overflow=refreshed.conservation(run,std::numeric_limits<qint64>::max(),1);held(overflow,"required_end checked before addition");require(!overflow.requiredEnd,"overflow never wraps to plausible end");
        SourceSession restarted(history,mirror);held(restarted.conservation(run,row.size(),0),"new session view cannot attach itself");restarted.consume(run,row.size(),row.size(),{});exact(restarted.conservation(run,row.size(),0),row.size());
        require(db.dump()==before,"all refusals and restarted read views leave logical DB unchanged");
    }
    {SourceHistory history(path);SourceMirror mirror(temp.path());SourceSession reopened(history,mirror);held(reopened.conservation(run,row.size(),0),"close/reopen view not automatically verified");reopened.consume(run,row.size(),row.size(),{});exact(reopened.conservation(run,row.size(),0),row.size());}
}
void fragments(){
    QTemporaryDir temp;require(temp.isValid(),"own temporary directory");SourceHistory history(temp.filePath("history.sqlite"));SourceMirror mirror(temp.path());SourceSession session(history,mirror);
    history.append(run,0,{});session.consume(run,0,20,row.left(20));const auto before=contents(mirror.file(run));
    held(session.conservation(run,20,0),"incomplete durable fragment not complete source");require(history.cursorRecord(run)->offset==0&&contents(mirror.file(run))==before,"fragment view cannot admit or drop bytes");
    session.consume(run,20,row.size(),row.mid(20));exact(session.conservation(run,row.size(),0),row.size());
}
void evictionAndFiles(){
    for(int variant=0;variant<3;++variant){
        QTemporaryDir temp;require(temp.isValid(),"own temporary directory");SourceHistory history(temp.filePath("history.sqlite"));SourceMirror mirror(temp.path());SourceSession session(history,mirror);session.consume(run,0,row.size(),row);
        for(std::size_t n=1;n<=SourceMirror::HandleLimit;++n)mirror.append(QString::number(n,16).rightJustified(32,'0'),0,{});
        if(variant==1)require(QFile::remove(mirror.file(run)),"evicted own file removed");
        if(variant==2){QFile f(mirror.file(run));require(f.open(QIODevice::ReadWrite)&&f.seek(3)&&f.write("z")==1&&f.flush(),"evicted own prefix mutation");}
        const auto cursor=history.cursorRecord(run)->offset;held(session.conservation(run,row.size(),0),"evicted/new incarnation never inherits paid proof");held(session.conservation(run,row.size(),0),"repeated view never adopts new handle");
        require(history.cursorRecord(run)->offset==cursor,"eviction view no cursor mutation");
        if(variant==0){session.consume(run,row.size(),row.size(),{});exact(session.conservation(run,row.size(),0),row.size());}
        if(variant==1)require(!QFileInfo::exists(mirror.file(run)),"missing mirror view does not recreate file");
        if(variant==2){const auto changed=contents(mirror.file(run));rejected([&]{session.consume(run,row.size(),row.size(),{});},"explicit reattach detects changed prefix hash");require(contents(mirror.file(run))==changed&&history.cursorRecord(run)->offset==cursor,"refusal preserves altered source, never repairs it");}
    }
}
void externalDatabaseChanges(){
    const QString source="native/"+run+".stdout";
    const QStringList poison={"UPDATE native_source_batches SET sha256='"+QString(64,'0')+"'", "DELETE FROM cursors", "UPDATE cursors SET offset=x'30'", "UPDATE cursors SET run=run||char(0)", "UPDATE cursors SET offset=offset+1", "CREATE TABLE unrelated_external_commit(value TEXT)"};
    for(qsizetype n=0;n<poison.size();++n){
        QTemporaryDir temp;require(temp.isValid(),"own temporary directory");const auto path=temp.filePath("history.sqlite");SourceHistory history(path);SourceMirror mirror(temp.path());SourceSession session(history,mirror);session.consume(run,0,row.size(),row);Database db(path);db.exec(poison[n]);
        const auto before=db.dump(),raw=contents(mirror.file(run));const auto view=session.conservation(run,row.size(),0);held(view,"external DB commit invalidates already paid integrity epoch");require(db.dump()==before&&contents(mirror.file(run))==raw,"external corruption view does not repair/write");
        if(n==1)require(!view.committedOffset&&!history.cursorRecord(run),"missing stored cursor remains unknown, not zero");
        if(n==2||n==3)rejected([&]{history.cursorRecord(run);},"public cursor reader rejects BLOB and embedded NUL metadata");
        if(n==0){SourceSession retry(history,mirror);rejected([&]{retry.consume(run,row.size(),row.size(),{});},"explicit fresh prefix verification rejects altered digest");}
        if(n==5){SourceSession retry(history,mirror);retry.consume(run,row.size(),row.size(),{});exact(retry.conservation(run,row.size(),0),row.size());}
    }
    QTemporaryDir temp;require(temp.isValid(),"own temporary directory");const auto path=temp.filePath("wide.sqlite");SourceHistory history(path);Database db(path);db.exec("INSERT INTO cursors(source,offset,run) VALUES('"+source+"',9007199254740993,'"+run+"')");const auto exact=history.cursorRecord(run);require(exact&&exact->offset==qint64(9007199254740993LL),"cursor metadata above 2^53 remains exact, no giant/sparse raw fixture");
}
void observationView(){
    QTemporaryDir temp;require(temp.isValid(),"own temporary directory");const auto path=temp.filePath("history.sqlite");SourceHistory history(path);SourceMirror mirror(temp.path());NativeObservationSession session(history,mirror,device);
    const auto admitted=session.consume(frame(packet(0,row)),run,0,false,true);require(!admitted.advance&&admitted.committedOffset==row.size(),"actual observation consume, no completion ticket");const auto inventory=frame(packet(row.size()));exact(session.conservation(inventory,run),row.size());
    auto legacy=packet(row.size());legacy.remove("pending_bytes");held(session.conservation(frame(legacy),run),"legacy absent pending cannot Ready");const auto legacyConsume=session.consume(frame(legacy),run,0,false,true);require(!legacyConsume.advance&&legacyConsume.committedOffset==row.size(),"legacy pending-absent frame still conserves SOURCE");
    auto wrong=packet(row.size());auto manifest=wrong.value("manifest").toObject();manifest["pid"]=qint64(1235);wrong["manifest"]=manifest;held(session.conservation(frame(wrong),run),"changed declared source identity cannot inherit epoch");held(session.conservation(inventory,QString(32,'b')),"selected different run refuses");
    auto contradiction=inventory;contradiction.pendingBytes=1;held(session.conservation(contradiction,run),"typed pending and packet disagreement refuses");contradiction=inventory;contradiction.nextOffset++;held(session.conservation(contradiction,run),"typed next and packet disagreement refuses");
    Database db(path);const auto before=db.dump(),raw=contents(mirror.file(run));const auto unconsumed=frame(packet(row.size(),"status\n"));held(session.conservation(unconsumed,run),"inventory bytes not implicitly consumed");require(db.dump()==before&&contents(mirror.file(run))==raw,"inventory view leaves cursor/raw/identity unchanged");
    const auto actual=session.consume(unconsumed,run,0,false,true);require(actual.committedOffset==row.size()+7&&!actual.advance,"subsequent actual consumption proves view did not consume inventory");exact(session.conservation(frame(packet(row.size()+7)),run),row.size()+7);
}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{missingAndZero();completeAndReadonly();fragments();evictionAndFiles();externalDatabaseChanges();observationView();std::cout<<"SOURCE_CONSERVATION "<<checks<<" checks / 0 failures; real own Win32 mirror + SQLite; READONLY view, exact/absent0, fragment/restart/eviction/corruption, no SSH/GPU/UI/Node/user data\n";return 0;}catch(const std::exception& e){std::cerr<<"SOURCE_CONSERVATION failed after "<<checks<<": "<<e.what()<<'\n';return 1;}}
