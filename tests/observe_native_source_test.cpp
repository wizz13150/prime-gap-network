#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <sqlite3.h>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
int checks=0;void require(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
const QString run(32,'a'),uuid="GPU-11111111-2222-3333-4444-555555555555";
const QByteArray row="[phase0-gpu] GAP merit=1.9000 gap=6 lower=23 upper=29 verified=1 proof=GPU_MR12_EULER_COMPOSITE record=off table=unknown unix_ts=1791982800 time_basis=source_batch_publication\n";
const QString counter="[pgn-counters] time_ns=1000000000 positions=1000000000 gaps=1 tests=123456 batches=1";
QJsonObject packet(qint64 at,const QByteArray& bytes){
    return {{"schema","pgn-runtime-snapshot-1"},{"manifest",QJsonObject{{"schema","pgn-native-process-1"},{"run",run},{"uuid",uuid},
        {"pid",qint64(1234)},{"birth","2026-10-14T17:00:00.1234567Z"},{"start","199900000000000000000"},{"end","199901000000000000000"}}},
        {"offset",at},{"next_offset",at+bytes.size()},{"chunk",QString::fromLatin1(bytes.toBase64())},
        {"observed_utc","2026-10-14T18:00:00.1234567Z"},{"source_clock_ns","1000000007"},{"state","RUNNING"},{"owned",true},
        {"pending_bytes",0},{"telemetry_lines",QJsonArray{counter}},{"binding",QJsonObject{{"private","not viewer protocol"}}}};
}
void output(const QJsonObject& p,bool stream){const auto bytes=QJsonDocument(p).toJson(stream?QJsonDocument::Compact:QJsonDocument::Indented);std::fwrite(bytes.constData(),1,size_t(bytes.size()),stdout);if(stream)std::fputc('\n',stdout);std::fflush(stdout);}
int fixture(const QStringList& args){
    // This test image is the ONLY executable ever used as an SSH fixture.
    const auto encoded=args.last().section(' ',-1);const auto bytes=QByteArray::fromBase64(encoded.toLatin1());QString command;
    for(qsizetype n=0;n+1<bytes.size();n+=2)command.append(QChar(uchar(bytes[n])|(ushort(uchar(bytes[n+1]))<<8)));
    const auto match=QRegularExpression("-Offset ([0-9]+) -ExpectedRun '([0-9a-f]{32})'").match(command);
    if(!match.hasMatch()||match.captured(2)!=run)return 91;
    const bool stream=command.contains("-Action stream ");const auto mode=qEnvironmentVariable("PGN_OBSERVER_FIXTURE");
    QFile marker(qEnvironmentVariable("PGN_OBSERVER_MARKER"));if(!marker.open(QIODevice::WriteOnly|QIODevice::Append))return 92;marker.write("one\n");marker.close();
    const auto at=match.captured(1).toLongLong();
    if(mode=="cold"){auto p=packet(0,{});p["manifest"]=QJsonValue::Null;p["state"]="OFF";p["owned"]=false;output(p,stream);return 0;}
    if(mode=="replay"){output(packet(99,"x"),stream);return 0;}
    if(mode=="identity"){auto p=packet(0,row);auto m=p.value("manifest").toObject();m["uuid"]="GPU-99999999-2222-3333-4444-555555555555";p["manifest"]=m;output(p,stream);return 0;}
    if(mode=="pid-lexeme"){
        auto raw=QJsonDocument(packet(0,row)).toJson(QJsonDocument::Compact);raw.replace("\"pid\":1234","\"pid\":1234.0000000000000001");std::fwrite(raw.constData(),1,size_t(raw.size()),stdout);std::fflush(stdout);return 0;
    }
    if(mode=="injected-gap"){auto p=packet(0,row);p["telemetry_lines"]=QJsonArray{QString::fromUtf8(row)};output(p,stream);return 0;}
    if(mode=="resume"){if(at!=row.size())return 93;output(packet(at,{}),stream);return 0;}
    if(at!=0)return 94;
    if(stream&&mode=="split"){output(packet(0,row.left(27)),true);output(packet(27,row.mid(27)),true);return 0;}
    output(packet(0,row),stream);return mode=="nonzero"?7:0;
}
struct Result{int exit{};QByteArray out,err;QList<QJsonObject> packets;};
Result invoke(const QString& executable,const QStringList& args,const QString& mode,const QString& marker){
    QProcess child;auto env=QProcessEnvironment::systemEnvironment();env.insert("PGN_OBSERVER_FIXTURE",mode);env.insert("PGN_OBSERVER_MARKER",marker);child.setProcessEnvironment(env);
#ifdef Q_OS_WIN
    child.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* a){a->flags|=CREATE_NO_WINDOW;});
#endif
    child.start(executable,args);require(child.waitForStarted(5000),"own observer child started");require(child.waitForFinished(10000),"own observer child finished within closed fixture deadline");
    Result r{child.exitCode(),child.readAllStandardOutput(),child.readAllStandardError(),{}};require(child.exitStatus()==QProcess::NormalExit,"own observer normal exit");
    for(const auto& line:r.out.split('\n'))if(!line.isEmpty()&&line.startsWith('{')){QJsonParseError e;const auto j=QJsonDocument::fromJson(line,&e);require(e.error==QJsonParseError::NoError&&j.isObject(),"native observer JSONL output valid");r.packets.push_back(j.object());}return r;
}
QString scalar(const QString& path,const char* sql){sqlite3* db{};if(sqlite3_open_v2(path.toUtf8().constData(),&db,SQLITE_OPEN_READONLY,nullptr)!=SQLITE_OK)throw std::runtime_error("fixture database open");sqlite3_stmt* st{};if(sqlite3_prepare_v2(db,sql,-1,&st,nullptr)!=SQLITE_OK)throw std::runtime_error("fixture query");QString value;if(sqlite3_step(st)==SQLITE_ROW)value=QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(st,0)));sqlite3_finalize(st);sqlite3_close(db);return value;}
QByteArray contents(const QString& file){QFile f(file);if(!f.open(QIODevice::ReadOnly))throw std::runtime_error("fixture file read");return f.readAll();}
}
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);if(app.arguments().value(1)=="-F")return fixture(app.arguments());
    try{
        require(argc==2,"explicit native observer executable");const auto executable=QFileInfo(app.arguments()[1]).absoluteFilePath();QTemporaryDir root;require(root.isValid(),"fixture root");
        const auto key=root.filePath("fixture.key");{QFile f(key);require(f.open(QIODevice::WriteOnly)&&f.write("NOT_A_REAL_KEY\n")==15,"fake fixture key created");}
        auto help=invoke(executable,{"--help"},"none",root.filePath("unused"));require(help.exit==0&&help.out.contains("observation-only"),"help before storage or transport");
        auto missing=invoke(executable,{},"none",root.filePath("unused"));require(missing.exit==2&&!QFile::exists(root.filePath("unused")),"missing arguments never execute client");
        for(const auto& mode:QStringList{"snapshot","split","nonzero","snapshot-failure","identity","replay","cold","pid-lexeme","injected-gap"}){
            const auto path=root.filePath(mode);require(QDir().mkpath(path),"per-case fixture directory");const auto database=path+"/science.sqlite",marker=path+"/attempts";
            QStringList args{"--ssh",QCoreApplication::applicationFilePath(),"--key",key,"--host","127.0.0.1","--user","fixture","--port","22","--run",run,"--uuid",uuid,"--database",database,"--mirror",path};
            const bool stream=mode=="split"||mode=="nonzero";if(stream)args<<"--stream";
            const auto actualMode=mode=="snapshot-failure"?QString("nonzero"):mode;
            const auto r=invoke(executable,args,actualMode,marker);
            require(contents(marker)=="one\n","exactly one client attempt, no reconnect loop");
            if(mode=="snapshot"||mode=="split"||mode=="nonzero"){
                require(r.exit==(mode=="nonzero"?1:0),"native viewer preserves transport success/failure distinction");
                require(contents(path+"/"+run+".stdout")==row&&scalar(database,"SELECT COUNT(*) FROM gaps")=="1","actual transport admission source and science exact");
                require(scalar(database,"SELECT offset FROM cursors")==QString::number(row.size()),"actual observer committed cursor exact");
                require(scalar(database,"SELECT unix_ts FROM gaps")=="1791982800"&&scalar(database,"SELECT time_basis FROM gaps")=="source_batch_publication","actual observer timestamp basis retained");
                int snapshots=0,gaps=0;
                for(const auto& p:r.packets){
                    if(p.value("type")!="snapshot")continue;++snapshots;
                    require(p.value("native_observation_only")==true&&!p.contains("chunk")&&!p.contains("telemetry_lines")&&!p.contains("binding"),"viewer protocol removes transport/private payload and advertises observation-only");
                    require(p.value("observed_utc")=="2026-10-14T18:00:00.1234567Z"&&p.value("source_clock_ns")=="1000000007","original wall and source clocks unchanged");
                    require(p.value("state")=="RUNNING"&&p.value("owned")==true,"physical declaration not replaced by synthetic state");
                    require(p.value("lines").toArray().last()==counter,"raw counter line unchanged");
                    for(const auto& line:p.value("lines").toArray())if(line.toString().startsWith("[phase0-gpu] GAP "))++gaps;
                }
                require(snapshots==(mode=="split"?2:1)&&gaps==1,"fragmented GAP appears only as one complete viewer row");
                if(mode=="nonzero")require(r.packets.last().value("type")=="unavailable","good prefix cannot masquerade as global successful stream");
                if(mode=="snapshot"){
                    const auto resumed=invoke(executable,args,"resume",marker);
                    require(resumed.exit==0&&resumed.packets.size()==2&&scalar(database,"SELECT COUNT(*) FROM gaps")=="1","real observer restart requests durable cursor without duplication");
                    require(resumed.packets.last().value("type")=="native_index"&&resumed.packets.last().value("complete")==true,"actual observer publishes coherent indexed history after restart");
                    require(contents(marker)=="one\none\n"&&contents(path+"/"+run+".stdout")==row,"restart keeps original raw bytes and one attempt each");
                }
            }else if(mode=="injected-gap"){
                require(r.exit==1&&r.packets.size()==1&&r.packets[0].value("type")=="unavailable","telemetry cannot inject an unindexed GAP into viewer");
                require(contents(path+"/"+run+".stdout")==row&&scalar(database,"SELECT COUNT(*) FROM gaps")=="1","publication refusal still retains original scientific source");
            }else{
                require(r.exit==(mode=="cold"?0:(mode=="replay"?4:1)),"cold/replay/refused/failed snapshot exact process status");
                require(!QFile::exists(path+"/"+run+".stdout")&&scalar(database,"SELECT COUNT(*) FROM gaps")=="0","refused or cold snapshots never admit source");
                if(mode=="snapshot-failure")require(r.packets.size()==1&&r.packets[0].value("type")=="unavailable","nonzero snapshot child publishes no unqualified data");
                if(mode=="replay")require(r.packets[0].value("type")=="native_source_replay_required"&&r.packets[0].value("offset").toInteger(-1)==0,"replay cursor explicit, never autonomous retry");
            }
        }
        std::cout<<"Native observer CLI: "<<checks<<" checks, 0 failures; SELF client only, no SSH/network/GPU/UI\n";return 0;
    }catch(const std::exception& e){std::cerr<<"Native observer CLI failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}
}
