#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QThread>
#include <sqlite3.h>
#include <cstdio>
#include <iostream>
#include <stdexcept>

// CLOSED: the SSH endpoint is this OWN SELF executable, never a real host.
namespace {
int checks{};const QString Run(32,'a'),Other(32,'b'),Device="GPU-11111111-2222-3333-4444-555555555555";
void need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void check(bool ok,const char* why){++checks;need(ok,why);}
QByteArray contents(const QString& path){QFile f(path);need(f.open(QIODevice::ReadOnly),"read own fixture");return f.readAll();}
void write(const QString& path,const QByteArray& bytes){QFile f(path);need(f.open(QIODevice::WriteOnly|QIODevice::NewOnly)&&f.write(bytes)==bytes.size()&&f.flush(),"new private fixture write");}
QByteArray compact(const QJsonObject& o){return QJsonDocument(o).toJson(QJsonDocument::Compact);}
QByteArray corpus(bool partial=false){QByteArray raw="[phase0-gpu] engine=GPU_OWNED_CONTINUOUS descriptors=2 original_start=100000 length=10000 proof=GPU_MR12_GAP coverage=SOURCE_PRP_INDEPENDENT_PROOF_PENDING\n";
    raw+=(QByteArray("TESTONLY diagnostic ")+QByteArray(19979,'x')+'\n').repeated(16);
    raw+="[phase0-gpu] GAP merit=1.9000 gap=6 lower=100010 upper=100016 verified=1 proof=GPU_MR12_EULER_COMPOSITE record=off table=unknown unix_ts=1791982800 time_basis=source_batch_publication\n";
    raw+="[pgn-counters] time_ns=9007199254740993 positions=10000 gaps=1 tests=9007199254740993 batches=3 processed_gaps=1\n[phase0-gpu] gaps reported=1 verification_failures=0 records_new=0\n[phase0-gpu] --- end ---\n";if(partial)raw+="TESTONLY incomplete final row";return raw;}
QJsonObject packet(const QString& run,qint64 at,const QByteArray& bytes,qint64 pending){return {{"schema","pgn-runtime-snapshot-1"},{"state","OFF"},{"owned",false},{"operator_stop",true},
    {"offset",at},{"next_offset",at+bytes.size()},{"pending_bytes",pending},{"chunk",QString::fromLatin1(bytes.toBase64())},
    {"observed_utc","2026-10-15T17:00:00.123456789Z"},{"source_counter_age_ms",9876543210LL},
    {"manifest",QJsonObject{{"schema","pgn-native-process-1"},{"run",run},{"uuid",Device},{"pid",4242},{"birth","2026-10-14T17:00:00.1234567Z"},{"start","100000"},{"end","110000"},{"stdout","C:/PUBLIC-TESTONLY/"+run+".stdout"}}},
    {"binding",QJsonObject{{"schema","PUBLIC-TESTONLY"},{"uuid",Device}}},{"last_exit",QJsonObject{{"run",run},{"exit_code",0}}}};}
int fake(const QStringList& args){const auto wide=QByteArray::fromBase64(args.last().section(' ',-1).toLatin1());need(wide.size()%2==0,"SELF UTF16 command");auto script=QString::fromUtf16(reinterpret_cast<const char16_t*>(wide.constData()),wide.size()/2);
    const QString wire="[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false);$OutputEncoding=[Console]::OutputEncoding;";
    need(script.startsWith(wire),"SELF exact UTF-8 wire setup");script.remove(0,wire.size());
    const auto m=QRegularExpression("^& 'C:/PGN/runtime/native-control.ps1' -Action stream -Offset ([0-9]+) -ExpectedRun '([0-9a-f]{32})' -StreamCount 150$").match(script);
    need(m.hasMatch()&&m.capturedLength()==script.size(),"ONLY existing readonly stream command permitted");need(std::fgetc(stdin)==EOF,"SELF null stdin");
    QFile log(qEnvironmentVariable("PGN_SOURCE_PROBE_TEST_LOG"));need(log.open(QIODevice::WriteOnly|QIODevice::Append)&&log.write(script.toUtf8()+'\n')>0&&log.flush(),"SELF read action receipt");log.close();
    const auto mode=qEnvironmentVariable("PGN_SOURCE_PROBE_TEST_MODE");if(mode=="timeout"){QThread::msleep(60000);return 0;}if(mode=="malformed"){std::fputs("{bad}\n",stdout);std::fflush(stdout);return 0;}
    const auto all=corpus(mode=="partial");const auto run=mode=="wrong-run"?Other:m.captured(2);qint64 at=m.captured(1).toLongLong();
    const auto sendPacket=[&](QJsonObject p){if(mode=="unknown-owned")p.remove("owned");if(mode=="string-owned")p.insert("owned","false");if(mode=="running"){p.insert("owned",true);p.insert("state","RUNNING");}if(mode=="unknown-pending")p.remove("pending_bytes");const auto b=compact(p)+'\n';need(std::fwrite(b.constData(),1,size_t(b.size()),stdout)==size_t(b.size())&&std::fflush(stdout)==0,"SELF source output");};
    if(at==all.size())sendPacket(packet(run,at,{},0));while(at<all.size()){const auto b=all.mid(at,65536);sendPacket(packet(run,at,b,all.size()-at-b.size()));at+=b.size();}return 0;
}
struct Result{int code{};QJsonObject receipt;};
struct Fixture{
    QTemporaryDir temp;QString config,key,history,mirror,booking,modules,log;
    Fixture():config(temp.filePath("transport.json")),key(temp.filePath("PUBLIC-TESTONLY.key")),history(temp.filePath("source.sqlite")),mirror(temp.filePath("raw")),booking(temp.filePath("never-open-booking.sqlite")),modules(temp.filePath("never-open-module.sqlite")),log(temp.filePath("read-actions.txt")){
        need(temp.isValid()&&QDir().mkdir(mirror),"own private probe fixture");write(key,"PUBLIC-TESTONLY-NOT-A-KEY\n");write(booking,"NOT-SQLITE-MUST-NOT-BE-OPENED\n");write(modules,"NOT-SQLITE-MUST-NOT-BE-OPENED\n");
        write(config,compact({{"schema","pgn-existing-host-transport-recovery-1"},{"research",QJsonObject{{"cuda",QJsonObject{{"host","fixture@PUBLIC-TESTONLY.invalid"},{"key",key}}}}},{"qualification","TESTONLY SELF endpoint, not physical source"}}));
    }
    QStringList args()const{return {"--transport-config",config,"--ssh",QCoreApplication::applicationFilePath(),"--booking-db",booking,"--history-db",history,"--mirror-dir",mirror,"--module-db",modules,"--checkpoint-label","C:/PUBLIC-TESTONLY/current-job/checkpoint","--device",Device,"--run",Run,"--timeout-ms","15000","--stop-when","conserved-off-eof"};}
    Result run(const QString& mode,QStringList arguments={}){if(arguments.isEmpty())arguments=args();const auto before=contents(booking)+contents(modules)+contents(config)+contents(key);auto env=QProcessEnvironment::systemEnvironment();env.insert("PGN_SOURCE_PROBE_TEST_MODE",mode);env.insert("PGN_SOURCE_PROBE_TEST_LOG",log);
        QProcess child;child.setProcessEnvironment(env);child.setStandardInputFile(QProcess::nullDevice());child.start(QCoreApplication::applicationDirPath()+"/pgn-native-source-probe.exe",arguments);
        need(child.waitForStarted(5000)&&child.waitForFinished(mode=="timeout"?25000:10000),"OWN probe completes bounded test");const auto output=child.readAllStandardOutput(),error=child.readAllStandardError();
        if(child.exitStatus()!=QProcess::NormalExit)std::cerr<<error.constData();check(child.exitStatus()==QProcess::NormalExit,"OWN probe exits normally after worker cleanup");
        QJsonParseError parse;const auto doc=QJsonDocument::fromJson(output,&parse);if(parse.error!=QJsonParseError::NoError)std::cerr<<output.constData()<<error.constData();check(parse.error==QJsonParseError::NoError&&doc.isObject(),"single JSON stdout receipt, no worker chatter");
        check(contents(booking)+contents(modules)+contents(config)+contents(key)==before,"booking/module/config/key bytes unchanged, no allocator/control DB opened");return {child.exitCode(),doc.object()};
    }
};
qint64 scalar(const QString& dbFile,const char* sql){sqlite3* db{};need(sqlite3_open_v2(dbFile.toUtf8().constData(),&db,SQLITE_OPEN_READONLY,nullptr)==SQLITE_OK,"own readonly history open");sqlite3_stmt* q{};need(sqlite3_prepare_v2(db,sql,-1,&q,nullptr)==SQLITE_OK&&sqlite3_step(q)==SQLITE_ROW,"own history scalar");const auto n=sqlite3_column_int64(q,0);sqlite3_finalize(q);sqlite3_close(db);return n;}
void positive(){Fixture f;const auto result=f.run("complete");const auto& r=result.receipt;
    if(result.code!=0)std::cerr<<"PROBE_DIAGNOSTIC "<<compact(r).constData()<<'\n';
    check(result.code==0&&r.value("accepted")==true&&r.value("state")=="SOURCE_CONSERVED"&&r.value("own_clients_stopped")==true,"SOURCE conserved only after own clients stopped");check(r.value("control_commands_sent")==0&&r.value("scope")=="SOURCE_BYTES_CONSERVED_AT_OBSERVED_EOF_ONLY_NOT_PHYSICAL_IDLE_OR_COVERAGE","receipt makes no physical idle/coverage claim");
    const auto progress=r.value("source_progress").toArray();
    // The finite conservation probe has no display-index subscriber. Its
    // SOURCE contract must not start or wait for an unrelated index reader.
    check(progress.size()==2&&!r.value("source_progress_overflow").toBool(),"one bounded actual first-frame SOURCE-only stage sequence");
    const QStringList stages{"frame_received","source_admitted"};
    qint64 previous=-1,delivered=-1;
    for(int i=0;i<progress.size();++i){const auto event=progress[i].toObject();
        check(i<stages.size()&&event.value("stage")==stages.value(i)&&event.value("schema")=="pgn-native-source-progress-1"&&event.value("run")==Run&&event.value("request_id")==r.value("request_id"),"actual progress correlation/order, not a synthetic status");
        check(event.value("elapsed_ms").toInteger(-1)>=previous&&event.value("probe_elapsed_ms").toInteger(-1)>=delivered,"monotone source and diagnostic delivery clocks remain distinct");
        previous=event.value("elapsed_ms").toInteger();delivered=event.value("probe_elapsed_ms").toInteger();
    }
    const auto raw=f.mirror+"/"+Run+".stdout";check(contents(raw)==corpus()&&scalar(f.history,"SELECT MAX(offset) FROM cursors")==corpus().size(),"real durable raw bytes and SQLite exact EOF");check(scalar(f.history,"SELECT MAX(length) FROM native_source_batches")>65536,"real fragment plus64KiB enrollment boundary");
    const auto s=r.value("snapshot").toObject(),c=r.value("source_conservation").toObject();check(s.value("observed_utc")=="2026-10-15T17:00:00.123456789Z"&&s.value("source_counter_age_ms")==QJsonValue(9876543210LL),"producer dates/age retained verbatim");check(compact(s).contains("time_ns=9007199254740993 positions=10000 gaps=1 tests=9007199254740993 batches=3 processed_gaps=1"),"actual SOURCE counter strings above2^53 remain exact, not a smoothed/rate replacement");check(c.value("ready")==true&&c.value("required_end").toInteger()==corpus().size()&&c.value("mirror_size")==c.value("transport_offset"),"real logical conservation retained");
    const auto log=contents(f.log);check(log.count('\n')==1&&log.contains("-Action stream")&&!log.contains("-Action start")&&!log.contains("-Action stop"),"one readonly observation, no retry or mutating command");
    const auto hash=QCryptographicHash::hash(contents(raw),QCryptographicHash::Sha256);const auto again=f.run("complete");check(again.code==0&&QCryptographicHash::hash(contents(raw),QCryptographicHash::Sha256)==hash,"explicit second audit resumes existing EOF without altering source");check(contents(f.log).contains("-Offset "+QByteArray::number(corpus().size())),"new explicit audit uses actual stored cursor, not a fabricated zero");
}
void heldCases(){for(const auto mode:{"unknown-owned","string-owned","running","unknown-pending","partial","wrong-run","malformed"}){Fixture f;const auto result=f.run(mode);check(result.code==3&&result.receipt.value("accepted")==false&&result.receipt.value("own_clients_stopped")==true&&!result.receipt.value("error").toString().isEmpty(),"unknown/partial/foreign observation stays Held after cleanup");check(contents(f.log).count('\n')==1,"Held never triggers implicit retry/control");}}
void bounds(){for(int poison=0;poison<8;++poison){Fixture f;auto args=f.args();if(poison==0)args[args.indexOf("--timeout-ms")+1]="14999";if(poison==1)args[args.indexOf("--timeout-ms")+1]="300001";if(poison==2)args<<"--run"<<Run;if(poison==3)args[args.indexOf("--run")+1]=Run+'\n';if(poison==4)args<<"--start";if(poison==5)args[args.indexOf("--stop-when")+1]="gpu-idle";
    if(poison>=6){const auto invalid=f.temp.filePath("invalid-config.json");auto bytes=contents(f.config);if(poison==6)bytes.replace("\"schema\":","\"schema\":\"duplicate\",\"schema\":");else bytes.replace("pgn-existing-host-transport-recovery-1","unknown-transport-schema");write(invalid,bytes);args[args.indexOf("--transport-config")+1]=invalid;}
    const auto result=f.run("complete",args);check(result.code==2&&result.receipt.value("accepted")==false&&!QFile::exists(f.log)&&!QFile::exists(f.history),"invalid explicit contract fails before client/storage, no fallback");}}
void timeout(){Fixture f;const auto result=f.run("timeout");check(result.code==4&&result.receipt.value("accepted")==false&&result.receipt.value("own_clients_stopped")==true&&result.receipt.value("error").toString().contains("deadline"),"local deadline holds and waits own observation cleanup, never remote stop");check(result.receipt.value("observation_elapsed_ms").toInteger()>=15000,"explicit local observation bound is actually paid, not an immediate synthetic timeout");check(contents(f.log).count('\n')==1,"timeout sent only one read, no retry/mining control");}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{if(app.arguments().contains("-F"))return fake(app.arguments());positive();heldCases();bounds();timeout();std::cout<<"NATIVE_SOURCE_PROBE "<<checks<<" checks / 0 failures; CLOSED tempSQLite+OWN SELF readonly SSH protocol only, no real host/GPU/UI/Node/user DB or physical-idle/coverage claim\n";return 0;}catch(const std::exception& e){std::cerr<<"NATIVE_SOURCE_PROBE failed after "<<checks<<": "<<e.what()<<'\n';return 1;}}
