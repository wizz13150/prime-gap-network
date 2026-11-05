#include "mining/native_runtime_configuration.h"
#include "mining/native_source_runtime.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QTimer>
#include <QUuid>
#include <cstdio>
#include <limits>
#include <stdexcept>

// Explicit, finite SOURCE audit. No application allocator, control command,
// queue, UI, lineage scan, automatic retry or remote process action is used.
using namespace pgn::mining;
namespace {
void need(bool ok,const QString& why){if(!ok)throw std::runtime_error(why.toUtf8().constData());}
bool full(const QString& text,const char* pattern){const auto m=QRegularExpression(QString::fromLatin1(pattern)).match(text);return m.hasMatch()&&m.capturedLength()==text.size();}
qint64 exact(const QJsonObject& o,const char* name){const auto v=o.value(QLatin1String(name));need(v.isDouble(),QString("Missing exact integer ")+name);const auto n=v.toInteger(-1);need(n>=0&&n<=9007199254740991LL&&v==QJsonValue(n),QString("Invalid exact integer ")+name);return n;}
bool conservedOffEof(const NativeSourcePublication& p,const QString& run,const QString& device){
    const auto& s=p.snapshot;const auto m=s.value("manifest").toObject();const auto& c=p.conservation;
    if(s.value("schema")!="pgn-runtime-snapshot-1"||s.value("state")!="OFF"||!s.value("owned").isBool()||s.value("owned").toBool()||
       m.value("schema")!="pgn-native-process-1"||m.value("run")!=run||m.value("uuid")!=device||p.run!=run||
       c.value("schema")!="pgn-source-conservation-1"||!c.value("ready").isBool()||!c.value("ready").toBool()||
       c.value("run")!=run||c.value("source")!="native/"+run+".stdout"||
       c.value("scope")!="LOGICAL_SOURCE_EPOCH_CONSERVATION_NOT_GLOBAL_POWER_CUT_DURABILITY")return false;
    try{
        const auto next=exact(s,"next_offset"),pending=exact(s,"pending_bytes");
        need(pending==0&&next<=9007199254740991LL-pending,"SOURCE EOF remains incomplete/overflowed");const auto end=next+pending;
        return exact(c,"required_end")==end&&exact(c,"committed_offset")==end&&exact(c,"mirror_size")==end&&exact(c,"transport_offset")==end;
    }catch(const std::exception&){return false;}
}
void output(const QJsonObject& value){const auto bytes=QJsonDocument(value).toJson(QJsonDocument::Compact)+'\n';need(std::fwrite(bytes.constData(),1,size_t(bytes.size()),stdout)==size_t(bytes.size())&&std::fflush(stdout)==0,"SOURCE receipt stdout unavailable");}
QString status(NativeSourceAttemptStatus s){switch(s){case NativeSourceAttemptStatus::Complete:return "Complete";case NativeSourceAttemptStatus::Failed:return "Failed";case NativeSourceAttemptStatus::TimedOut:return "TimedOut";case NativeSourceAttemptStatus::Cancelled:return "Cancelled";case NativeSourceAttemptStatus::ReplayRequired:return "ReplayRequired";}return "Unknown";}
}
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);QCoreApplication::setApplicationName("pgn-native-source-probe");
    QJsonObject receipt{{"schema","pgn-native-source-probe-1"},{"accepted",false},{"state","Held"},{"error",""},
        {"scope","SOURCE_BYTES_CONSERVED_AT_OBSERVED_EOF_ONLY_NOT_PHYSICAL_IDLE_OR_COVERAGE"},
        {"control_commands_sent",0},{"own_clients_stopped",false},{"snapshot",QJsonValue::Null},{"source_conservation",QJsonValue::Null}};
    try{
        QCommandLineParser args;args.setApplicationDescription("Finite read-only remote SOURCE audit; explicit paths only, no mining controls.");args.addHelpOption();
        const QStringList names{"transport-config","ssh","booking-db","history-db","mirror-dir","module-db","checkpoint-label","device","run","timeout-ms","stop-when"};
        for(const auto& name:names)args.addOption({name,"Explicit "+name,"value"});
        need(args.parse(app.arguments()),args.errorText());if(args.isSet("help")){std::fputs(args.helpText().toUtf8().constData(),stdout);return 0;}
        need(args.positionalArguments().isEmpty(),"Unexpected positional argument");
        for(const auto& name:names)need(args.values(name).size()==1&&!args.value(name).isEmpty(),"Exactly one explicit --"+name+" is required");
        const auto run=args.value("run"),device=args.value("device");need(full(run,"^[0-9a-f]{32}$"),"Exact32lowerhex run required");
        need(full(device,"^GPU-[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$"),"Exact normalized GPU device required");
        need(args.value("stop-when")=="conserved-off-eof","Only explicit conserved-off-eof stop condition is defined");
        const auto timeoutText=args.value("timeout-ms");bool ok=false;const auto timeout=timeoutText.toInt(&ok);
        need(ok&&QString::number(timeout)==timeoutText&&timeout>=15000&&timeout<=300000,"Canonical timeout-ms must be15000..300000");
        const NativeRuntimePaths paths{args.value("booking-db"),args.value("history-db"),args.value("mirror-dir"),args.value("module-db"),args.value("checkpoint-label")};
        const auto loaded=loadNativeRuntimeConfiguration(args.value("transport-config"),args.value("ssh"),paths);
        need(loaded.available(),loaded.errorCode+": "+loaded.error);
        const auto request=QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
        receipt.insert("request_id",request);receipt.insert("run",run);receipt.insert("device",device);receipt.insert("timeout_ms",timeout);
        receipt.insert("configuration_sha256",loaded.configuration->sourceSha256);receipt.insert("stop_when","conserved-off-eof");
        QTimer deadline;deadline.setSingleShot(true);deadline.setTimerType(Qt::PreciseTimer);QElapsedTimer clock;clock.start();
        std::unique_ptr<NativeSourceRuntime> runtime;bool settling=false;int code=3;qint64 publications=0;
        const auto finish=[&](int result,const QString& error){if(settling)return;settling=true;code=result;deadline.stop();receipt.insert("accepted",result==0);receipt.insert("state",result==0?"SOURCE_CONSERVED":"Held");receipt.insert("error",error.left(400));receipt.insert("observation_elapsed_ms",clock.elapsed());runtime->stop();};
        NativeSourceRuntimeCallbacks callbacks;
        QJsonArray progress;
        callbacks.progress=[&](const NativeSourceProgress& stage){
            if(settling||stage.requestId!=request||stage.run!=run)return;
            // Bounded first-frame diagnostics, separate from SOURCE facts.
            // Never copy RAW or replace a source timestamp with this clock.
            if(progress.size()>=5){receipt.insert("source_progress_overflow",true);return;}
            auto event=stage.json();event.insert("probe_elapsed_ms",clock.elapsed());
            progress.append(event);receipt.insert("source_progress",progress);
        };
        callbacks.publication=[&](const NativeSourcePublication& p){
            if(settling||p.requestId!=request||p.run!=run)return;++publications;
            // These are the existing runtime's unchanged projection/index and
            // conservation object, not a re-aged/relabelled physical snapshot.
            receipt.insert("snapshot",p.snapshot);receipt.insert("source_conservation",p.conservation);receipt.insert("native_index",p.nativeIndex);
            receipt.insert("source_epoch",qint64(p.epoch));receipt.insert("coalesced_frames",p.coalescedFrames);
            if(conservedOffEof(p,run,device))finish(0,{});
        };
        callbacks.sourceFinished=[&](const NativeSourceAttempt& a){if(a.requestId!=request)return;
            receipt.insert("source_attempt",QJsonObject{{"status",status(a.status)},{"error",a.error},{"exit_code",a.transport.exitCode?QJsonValue(*a.transport.exitCode):QJsonValue(QJsonValue::Null)}});
            if(!settling)finish(3,a.error.isEmpty()?"SOURCE observation ended before exact conserved OFF EOF":a.error);
        };
        callbacks.stopped=[&]{receipt.insert("own_clients_stopped",true);receipt.insert("publications",publications);receipt.insert("elapsed_ms",clock.elapsed());try{output(receipt);app.exit(code);}catch(const std::exception& e){std::fputs(e.what(),stderr);app.exit(2);}};
        NativeSourceRuntimeConfig config{loaded.configuration->endpoint,device,paths.sourceHistoryDatabase,paths.sourceMirrorDirectory,5000,timeout+1000,timeout+1000};
        runtime=std::make_unique<NativeSourceRuntime>(config,std::move(callbacks));
        QObject::connect(&deadline,&QTimer::timeout,&app,[&]{
            // A timer wakeup is a hint, not proof that the monotonic deadline
            // has elapsed. Millisecond conversion can leave one tick unpaid.
            const auto remaining=qint64(timeout)-clock.elapsed();
            if(remaining>0){deadline.start(int(remaining));return;}
            finish(4,"SOURCE observation deadline elapsed; only OWN observation clients stopped");
        });
        deadline.start(timeout);if(!runtime->startSource(request,run))finish(3,"SOURCE read attempt refused; no retry");
        // Cleanup is asynchronous and always awaited. A pre-existing index
        // audit can delay it; timeout is not a hard worker-termination promise.
        return app.exec();
    }catch(const std::exception& e){receipt.insert("error",QString::fromUtf8(e.what()).left(400));output(receipt);return 2;}
}
