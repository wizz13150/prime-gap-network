#include "mining/native_observation_session.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <cstdio>
#include <iostream>
#include <stdexcept>

using namespace pgn::mining;
namespace {
void publish(QJsonObject object){
    const auto bytes=QJsonDocument(object).toJson(QJsonDocument::Compact)+'\n';
    if(bytes.size()>SshSnapshotTransport::FrameLimit)throw std::runtime_error("Native viewer publication exceeds protocol bound");
    if(std::fwrite(bytes.constData(),1,size_t(bytes.size()),stdout)!=size_t(bytes.size())||std::fflush(stdout)!=0)
        throw std::runtime_error("Native viewer output unavailable");
}
}
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);QCoreApplication::setApplicationName("pgn-observe-native-source");
    try{
        QCommandLineParser parser;parser.setApplicationDescription(
            "Native observation-only SSH source reader. Explicit direct endpoint, key, run and isolated storage required. "
            "No start/stop/dispatch/automatic succession or Node dependency. Existing SSH aliases/proxies are NOT inherited. "
            "Old unbound source/index data require a separate audited migration; never silently relabeled.");
        parser.addHelpOption();
        for(const auto& name:QStringList{"ssh","key","host","user","port","run","uuid","database","mirror"})
            parser.addOption(QCommandLineOption(name,"Explicit "+name,name));
        parser.addOption(QCommandLineOption("stream","One continuous read connection, no automatic reconnect or lifetime cap."));
        if(!parser.parse(app.arguments()))throw std::runtime_error(parser.errorText().toStdString());
        if(parser.isSet("help"))parser.showHelp(0);
        for(const auto& name:QStringList{"ssh","key","host","user","port","run","uuid","database","mirror"})
            if(parser.value(name).isEmpty())throw std::runtime_error(("Missing explicit --"+name).toStdString());
        bool portOk=false;const auto port=parser.value("port").toUInt(&portOk);
        if(!portOk||port==0||port>65535||QString::number(port)!=parser.value("port"))throw std::runtime_error("Explicit canonical port invalid");
        if(!QRegularExpression("^[0-9a-f]{32}$").match(parser.value("run")).hasMatch())throw std::runtime_error("Explicit run invalid");
        const auto db=parser.value("database"),directory=parser.value("mirror");
        if(!QFileInfo(db).isAbsolute()||!QFileInfo(QFileInfo(db).absolutePath()).isDir()||!QFileInfo(directory).isAbsolute()||!QFileInfo(directory).isDir())
            throw std::runtime_error("Explicit absolute database and existing mirror directory required");
        const SshObservationEndpoint endpoint{parser.value("ssh"),parser.value("key"),parser.value("host"),parser.value("user"),quint16(port)};
        SshObservationRequest request;request.action=parser.isSet("stream")?SshObservationAction::Stream:SshObservationAction::Snapshot;request.expectedRun=parser.value("run");
        // Validate endpoint and declared device before creating storage or a
        // child. This also makes --help/invalid invocations side-effect free.
        makeSshObservationCommand(endpoint,request);
        CompletionAdvanceGate validateDevice(parser.value("uuid"));
        SourceHistory history(db);SourceMirror mirror(directory);NativeObservationSession session(history,mirror,parser.value("uuid"));
        request.offset=history.cursor(request.expectedRun);QElapsedTimer clock;clock.start();
        SshSnapshotTransport transport;int code=1;bool replay=false;QString admissionError;
        transport.start(endpoint,request,[&](const SshObservationFrame& frame){
            try{
                // No control path exists in this viewer: gate permanently
                // disabled/paused, not a pretend successful dispatcher.
                const auto admitted=session.consume(frame,request.expectedRun,clock.elapsed(),false,true);
                if(admitted.state==NativeObservationState::ReplayRequired){
                    replay=true;publish({{"type","native_source_replay_required"},{"run",admitted.run},{"offset",admitted.nextOffset}});return false;
                }
                auto publication=frame.packet;publication.remove("chunk");publication.remove("telemetry_lines");publication.remove("binding");
                QJsonArray lines;
                for(const auto& line:admitted.displayLines)if(!line.startsWith("[pgn-counters] "))lines.append(QString::fromUtf8(line));
                const auto live=frame.packet.value("telemetry_lines");
                if(live.isArray())for(const auto& value:live.toArray()){
                    static const QRegularExpression counter("^\\[pgn-counters\\] time_ns=[0-9]+ positions=[0-9]+ gaps=[0-9]+ tests=[0-9]+ batches=[0-9]+(?: processed_gaps=[0-9]+)?$");
                    if(!value.isString()||!counter.match(value.toString()).hasMatch())throw std::runtime_error("Native live counter row type/content invalid");
                    lines.append(value);
                }
                else for(const auto& line:admitted.displayLines)if(line.startsWith("[pgn-counters] "))lines.append(QString::fromUtf8(line));
                publication["type"]="snapshot";publication["lines"]=lines;publication["native_observation_only"]=true;
                publish(publication);
                if(admitted.state==NativeObservationState::Source)publish(history.indexPublication(admitted.run));
                return true;
            }catch(const std::exception& e){admissionError=QString::fromUtf8(e.what());return false;}
        },[&](const SshObservationResult& result){
            const bool success=result.status==SshObservationStatus::Complete;
            code=success?0:(replay?4:1);
            if(!success){try{publish({{"type","unavailable"},{"error",admissionError.isEmpty()?result.error:admissionError},
                {"native_observation_only",true},{"complete_frames",result.completeFrames}});}catch(...){code=1;}}
            app.exit(code);
        });
        return app.exec();
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
}
