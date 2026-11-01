#include "mining/source_history.h"
#include "mining/source_mirror.h"
#include "mining/native_observation_session.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <sqlite3.h>
#include <windows.h>
#include <functional>
#include <iostream>
#include <stdexcept>

using namespace pgn::mining;
namespace {
int checks{},failures{};
void check(bool good,const char* description){++checks;if(!good){++failures;std::cerr<<"FAIL "<<description<<'\n';}}
void need(bool good){if(!good)throw std::runtime_error("fixture construction failed");}
const QString run(32,'a'),device="GPU-11111111-2222-3333-4444-555555555555";
QByteArray hash(const QByteArray& value){return QCryptographicHash::hash(value,QCryptographicHash::Sha256).toHex();}
void write(const QString& file,const QByteArray& bytes){QFile out(file);need(out.open(QIODevice::WriteOnly|QIODevice::Truncate));need(out.write(bytes)==bytes.size());out.close();}
void sql(const QString& path,const QByteArray& command){sqlite3* db{};need(sqlite3_open(path.toUtf8().constData(),&db)==SQLITE_OK);const auto rc=sqlite3_exec(db,command.constData(),nullptr,nullptr,nullptr);sqlite3_close(db);need(rc==SQLITE_OK);}
QByteArray query(const QString& path,const QByteArray& command){
    sqlite3* db{};need(sqlite3_open_v2(path.toUtf8().constData(),&db,SQLITE_OPEN_READONLY,nullptr)==SQLITE_OK);sqlite3_stmt* row{};
    need(sqlite3_prepare_v2(db,command.constData(),-1,&row,nullptr)==SQLITE_OK);QByteArray result;int rc;
    while((rc=sqlite3_step(row))==SQLITE_ROW){for(int i=0;i<sqlite3_column_count(row);++i){result+=QByteArray::number(sqlite3_column_type(row,i))+":";
        result+=QByteArray(reinterpret_cast<const char*>(sqlite3_column_blob(row,i)),sqlite3_column_bytes(row,i)).toHex()+";";}result+='\n';}
    need(rc==SQLITE_DONE);sqlite3_finalize(row);sqlite3_close(db);return result;
}
QByteArray schema(const QString& path){return query(path,"SELECT type,name,tbl_name,sql FROM sqlite_schema WHERE name NOT LIKE 'sqlite_%' ORDER BY type,name");}
QByteArray business(const QString& path){return query(path,"SELECT * FROM gaps ORDER BY id")+query(path,"SELECT * FROM origins ORDER BY gap_id,run,source")+query(path,"SELECT * FROM cursors ORDER BY source")+query(path,"SELECT * FROM opaque_metadata ORDER BY id");}
QJsonObject manifest(){return {{"schema","pgn-native-process-1"},{"run",run},{"uuid",device},{"pid",qint64(1234)},
    {"birth","2026-10-14T02:24:30.3928458Z"},{"start","199932000000000000000"},{"end","199933000000000000000"}};}
struct Fixture {
    QTemporaryDir temp;QString database,journal;QByteArray prefix;QJsonObject actual=manifest();
    explicit Fixture(bool large=false,bool tail=false){
        need(temp.isValid());database=temp.filePath("legacy.sqlite");journal=temp.filePath(run+".stdout");
        prefix="[phase0-gpu] GAP merit=17.25 gap=18 lower=9007199254740993 upper=9007199254741011 verified=1\n"
               "[phase0-gpu] GAP merit=20 gap=20 lower=101 upper=121 verified=1 unix_ts=1700000000\n";
        if(large)for(int i=0;i<1500;++i)prefix+="[phase0-gpu] diagnostic unchanged source bytes, no event date invented\n";
        write(journal,prefix+(tail?QByteArray("uncommitted remote tail\n"):QByteArray{}));
        const auto source=("native/"+run+".stdout").toUtf8();
        sql(database,"CREATE TABLE gaps(id INTEGER PRIMARY KEY,p TEXT,q TEXT,gap TEXT,merit TEXT,unix_ts INTEGER,legacy_extension BLOB);"
            "CREATE TABLE origins(gap_id INTEGER,run TEXT,source TEXT,UNIQUE(gap_id,run,source));"
            "CREATE TABLE cursors(source TEXT PRIMARY KEY,offset INTEGER,run TEXT);"
            "CREATE TABLE opaque_metadata(id INTEGER PRIMARY KEY,payload BLOB); INSERT INTO opaque_metadata VALUES(1,X'00FF004142');"
            "INSERT INTO gaps VALUES(1,'9007199254740993','9007199254741011','18','17.25',NULL,X'00FF');"
            "INSERT INTO gaps VALUES(2,'101','121','20','20',1700000000,NULL);"
            "INSERT INTO origins VALUES(1,'"+run.toUtf8()+"','"+source+"'); INSERT INTO origins VALUES(2,'"+run.toUtf8()+"','"+source+"');"
            "INSERT INTO cursors VALUES('"+source+"',"+QByteArray::number(prefix.size())+",'"+run.toUtf8()+"');");
    }
};
template<class Operation> bool rejected(Operation operation){try{operation();return false;}catch(const std::exception&){return true;}}
void enroll(Fixture& f,const QByteArray& digest={},qint64 offset=-1,const std::function<void()>& stable={}){
    SourceMirror mirror(f.temp.path());auto freeze=mirror.freezeForLineage();auto held=freeze->open(run);
    SourceHistory::enrollLegacySourceExisting(f.database,run,device,f.actual,offset<0?f.prefix.size():offset,digest.isEmpty()?hash(f.prefix):digest,
        [&](qint64 at,qsizetype count){need(count<=65536);return held->read(at,count);},[&]{held->verifyStable();if(stable)stable();});
    held->closeAndVerifyStable();
}
void positive(){
    Fixture f(true,true);const auto oldBusiness=business(f.database),oldSchema=schema(f.database);const auto oldRaw=hash(f.prefix+"uncommitted remote tail\n");
    SourceMirror mirror(f.temp.path());auto freeze=mirror.freezeForLineage();auto held=freeze->open(run);qint64 reads=0,bytes=0;bool stableCalled=false;
    const auto denyWriter=[&]{const auto path=f.journal.toStdWString();const auto file=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
        const bool denied=file==INVALID_HANDLE_VALUE;if(!denied)CloseHandle(file);check(denied,"WRITE excluded by held reader, not mere path stat");check(!DeleteFileW(path.c_str()),"DELETE excluded by held reader");};
    SourceHistory::enrollLegacySourceExisting(f.database,run,device,f.actual,f.prefix.size(),hash(f.prefix),
        [&](qint64 at,qsizetype count){check(count>0&&count<=65536,"64KiB bounded prefix reads");++reads;auto data=held->read(at,count);bytes+=data.size();return data;},
        [&]{stableCalled=true;held->verifyStable();denyWriter();check(query(f.database,"SELECT name FROM sqlite_schema WHERE name IN ('native_source_batches','native_source_identities')").isEmpty(),"digest AND identity schema invisible before commit");});
    check(stableCalled&&reads>1&&bytes==f.prefix.size(),"whole prefix only, including >64KiB but not uncommitted tail");denyWriter();
    check(held->size()==f.prefix.size()+QByteArray("uncommitted remote tail\n").size(),"tail retained without enrollment");held->closeAndVerifyStable();freeze.reset();
    check(business(f.database)==oldBusiness,"all legacy values/types/dates/cursor/opaque metadata unchanged");
    check(schema(f.database)!=oldSchema,"only explicit enrollment adds schema");
    check(query(f.database,"SELECT name FROM sqlite_schema WHERE name IN ('gaps_time','native_plot_coordinates','native_plot_bounds')").isEmpty(),"no general constructor/index migrations");
    check(query(f.database,"SELECT name FROM pragma_table_info('gaps') WHERE name='time_basis'").isEmpty(),"no silent legacy ALTER TABLE");
    check(query(f.database,"SELECT SUM(length) FROM native_source_batches")=="1:"+QByteArray::number(f.prefix.size()).toHex()+";\n","batch coverage exact committed prefix");
    const auto identity=QString::fromUtf8(QJsonDocument(nativeSourceObservationIdentity(f.actual,run,device)).toJson(QJsonDocument::Compact));
    check(query(f.database,"SELECT identity FROM native_source_identities")=="3:"+identity.toUtf8().toHex()+";\n","stored identity equals actual observation ABI");
    QFile raw(f.journal);need(raw.open(QIODevice::ReadOnly));check(hash(raw.readAll())==oldRaw,"no raw byte rewrite or truncation");raw.close();
    const auto before=schema(f.database)+business(f.database)+query(f.database,"SELECT * FROM native_source_batches ORDER BY offset")+query(f.database,"SELECT * FROM native_source_identities");
    check(rejected([&]{enroll(f);}),"already enrolled is held, not blind overwrite/retry");
    check(before==schema(f.database)+business(f.database)+query(f.database,"SELECT * FROM native_source_batches ORDER BY offset")+query(f.database,"SELECT * FROM native_source_identities"),"rejected replay leaves all data unchanged");
}
void poisons(){
    for(int kind=0;kind<17;++kind){Fixture f;QByteArray digest=hash(f.prefix);qint64 offset=f.prefix.size();
        switch(kind){
        case 0:digest.fill('0');break;
        case 1:++offset;break;
        case 2:sql(f.database,"DELETE FROM cursors");break;
        case 3:sql(f.database,"UPDATE cursors SET offset='not-integer'");break;
        case 4:sql(f.database,"UPDATE cursors SET run=CAST(run AS BLOB)");break;
        case 5:f.actual["run"]=QString(32,'b');break;
        case 6:f.actual["uuid"]="GPU-00000000-2222-3333-4444-555555555555";break;
        case 7:f.actual["pid"]="1234";break;
        case 8:sql(f.database,"DELETE FROM origins WHERE gap_id=1");break;
        case 9:sql(f.database,"INSERT INTO gaps VALUES(3,'131','151','20','10',NULL,NULL); INSERT INTO origins SELECT 3,run,source FROM origins WHERE gap_id=1");break;
        case 10:sql(f.database,"UPDATE gaps SET gap='19' WHERE id=1");break;
        case 11:f.prefix.chop(1);write(f.journal,f.prefix);sql(f.database,"UPDATE cursors SET offset="+QByteArray::number(f.prefix.size()));offset=f.prefix.size();digest=hash(f.prefix);break;
        case 12:f.prefix.replace("verified=1","verified=0");write(f.journal,f.prefix);digest=hash(f.prefix);break;
        case 13:sql(f.database,"CREATE TABLE native_source_batches(source TEXT PRIMARY KEY); INSERT INTO native_source_batches VALUES('native/"+run.toUtf8()+".stdout')");break;
        case 14:sql(f.database,"CREATE TABLE native_source_identities(run TEXT PRIMARY KEY,identity TEXT); INSERT INTO native_source_identities VALUES('"+run.toUtf8()+"','opaque')");break;
        case 15:sql(f.database,"CREATE TABLE native_source_batches(source TEXT,offset INTEGER,length INTEGER,sha256 TEXT,PRIMARY KEY(source,offset)); CREATE TABLE native_source_identities(run TEXT PRIMARY KEY,identity TEXT); CREATE TRIGGER afterfail BEFORE INSERT ON native_source_identities BEGIN SELECT RAISE(ABORT,'AFTERFAIL'); END;");break;
        case 16:break;
        }
        const auto oldSchema=schema(f.database),oldBusiness=business(f.database);QFile in(f.journal);need(in.open(QIODevice::ReadOnly));const auto oldRaw=in.readAll();in.close();
        const bool refused=rejected([&]{enroll(f,digest,offset,kind==16?std::function<void()>([]{throw std::runtime_error("AFTERFAIL before COMMIT");}):std::function<void()>{});});
        check(refused,"independent digest/cursor/origin/identity/fragment/AFTERFAIL poison refuses");
        check(schema(f.database)==oldSchema,"failure rolls back newly created enrollment schema exactly");
        check(business(f.database)==oldBusiness,"failure retains every legacy row/type/value");
        need(in.open(QIODevice::ReadOnly));check(in.readAll()==oldRaw,"failure never repairs/replaces source bytes");
    }
    Fixture f;const auto old=schema(f.database)+business(f.database);
    for(int kind=0;kind<3;++kind){check(rejected([&]{SourceHistory::enrollLegacySourceExisting(f.database,run,device,f.actual,f.prefix.size(),hash(f.prefix),
        [&](qint64,qsizetype count){return kind==0?QByteArray{}:QByteArray(count+1,'x');},kind==2?std::function<void()>{}:std::function<void()>([]{}));}),"reader short/oversize and missing stable capability refuse");check(old==schema(f.database)+business(f.database),"reader failure schema/data rollback");}
    const auto missing=f.temp.filePath("must-not-create.sqlite");check(rejected([&]{SourceHistory::enrollLegacySourceExisting(missing,run,device,f.actual,f.prefix.size(),hash(f.prefix),[](qint64,qsizetype){return QByteArray{};},[]{});}),"missing DB refuses existing-only open");check(!QFile::exists(missing),"missing DB is never created");
}
QJsonObject receipt(const Fixture& f){return {{"schema","pgn-root-legacy-source-audit-1"},{"writer","root"},{"run",run},{"device",device},
    {"local_history",f.database},{"local_mirror_directory",f.temp.path()},{"local_mirror_size",QString::number(f.prefix.size())},
    {"committed_offset",QString::number(f.prefix.size())},{"expected_prefix_sha256",QString::fromLatin1(hash(f.prefix))},{"manifest",f.actual},
    {"remote",QJsonObject{{"owned",false},{"state","OFF"},{"source_size",QString::number(f.prefix.size()+20000000)},
        {"prefix_interval","[0,"+QString::number(f.prefix.size())+")"},{"prefix_sha256",QString::fromLatin1(hash(f.prefix))}}}};}
int invoke(const QString& program,Fixture& f,const QByteArray& raw,QByteArray& output){
    const auto file=f.temp.filePath("audit.json");write(file,raw);QProcess child;child.setProgram(program);child.setArguments({"--database-existing",f.database,"--mirror-directory",f.temp.path(),"--run",run,"--device",device,"--manifest-receipt",file,"--expected-offset",QString::number(f.prefix.size()),"--expected-prefix-sha256",QString::fromLatin1(hash(f.prefix))});
    child.start();need(child.waitForStarted(5000));need(child.waitForFinished(15000));output=child.readAllStandardOutput();check(child.exitStatus()==QProcess::NormalExit,"only own native CLI exits normally");return child.exitCode();
}
void cli(const QString& program){
    Fixture f;QByteArray output;const auto prior=business(f.database);const auto raw=QJsonDocument(receipt(f)).toJson();
    check(invoke(program,f,raw,output)==0,"actual native CLI own legacy fixture commits");const auto result=QJsonDocument::fromJson(output).object();
    check(result.value("state")=="COMMITTED"&&result.value("audit_receipt_sha256")==QString::fromLatin1(hash(raw)),"CLI emits exact receipt SHA and committed state");check(business(f.database)==prior,"CLI does not alter legacy data");
    for(int kind=0;kind<7;++kind){Fixture bad;auto bytes=QJsonDocument(receipt(bad)).toJson(QJsonDocument::Compact);
        switch(kind){case 0:bytes.replace("\"pid\":1234","\"pid\":1234.0000000000000001");break;
        case 1:bytes.replace("\"pid\":1234","\"pid\":9007199254740993.0");break;
        case 2:bytes.replace("\"pid\":1234","\"pid\":1234,\"p\\u0069d\":1234");break;
        case 3:bytes.prepend("\xEF\xBB\xBF");break;
        case 4:bytes=QByteArray(16385,' ');break;
        case 5:bytes.replace("\"owned\":false","\"owned\":true");break;
        case 6:bytes.replace("\"run\":\""+run.toUtf8()+"\"","\"run\":\""+QString(32,'b').toUtf8()+"\"");break;}
        const auto before=schema(bad.database)+business(bad.database);check(invoke(program,bad,bytes,output)!=0,"CLI lexical/identity/size/UTF8 poison held before enrollment");check(before==schema(bad.database)+business(bad.database),"CLI refusal leaves schema and data unchanged");
    }
}
void realAdmission(){
    Fixture f;enroll(f);SourceHistory history(f.database);SourceMirror mirror(f.temp.path());NativeObservationSession session(history,mirror,device);
    SshObservationFrame frame;frame.offset=f.prefix.size();frame.nextOffset=f.prefix.size();frame.pendingBytes=qint64(0);
    frame.packet={{"schema","pgn-runtime-snapshot-1"},{"manifest",f.actual},{"next_offset",frame.nextOffset},{"pending_bytes",qint64(0)}};
    const auto result=session.consume(frame,run,1,false,true);
    check(result.state==NativeObservationState::Source&&result.nextOffset==f.prefix.size()&&result.committedOffset==f.prefix.size(),"real NativeObservationSession accepts exact enrolled identity+prefix, no template/fallback");
}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{positive();poisons();realAdmission();if(argc==2)cli(QString::fromLocal8Bit(argv[1]));else check(false,"explicit own CLI image argument required");}catch(const std::exception& error){++failures;std::cerr<<"EXCEPTION "<<error.what()<<'\n';}std::cout<<"legacy-source-enrollment checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;}
