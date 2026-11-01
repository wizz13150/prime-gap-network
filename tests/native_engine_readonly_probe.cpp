#include "mining/native_runtime_configuration.h"
#include "mining/native_control_transport.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>

// Explicit opt-in to the actual PGN read-only transports. No start/stop command.
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);if(argc!=4)return 2;
    QTemporaryDir own;if(!own.isValid())return 7;
    pgn::mining::NativeRuntimePaths paths{own.filePath("booking.sqlite"),own.filePath("history.sqlite"),own.filePath("mirror"),own.filePath("modules.sqlite"),"C:/legacy/checkpoint"};
    const auto config=pgn::mining::loadNativeRuntimeConfiguration(QString::fromLocal8Bit(argv[1]),QString::fromLocal8Bit(argv[2]),paths);
    if(!config.available()){std::cerr<<config.error.toStdString()<<'\n';return 3;}
    pgn::mining::NativeControlTransport profile;
    pgn::mining::SshSnapshotTransport snapshot;
    QJsonObject result{{"PASS",false},{"GPU",false},{"driver",false},{"physical_control",false}};
    bool done=false;
    auto finish=[&](bool pass,const QString& error){if(done)return;done=true;result.insert("PASS",pass);result.insert("error",error);QSaveFile out(QString::fromLocal8Bit(argv[3]));if(!out.open(QIODevice::WriteOnly)){app.exit(4);return;}out.write(QJsonDocument(result).toJson());if(!out.commit()){app.exit(5);return;}std::cout<<"PGN_NATIVE_READONLY_PROBE "<<pass<<'\n';app.exit(pass?0:6);};
    profile.start(pgn::mining::NativeControlCommand::configured3090Profile(config.configuration->endpoint),{5000,30000},[&](const pgn::mining::NativeControlTransportResult& observed){
        if(observed.status!=pgn::mining::NativeControlTransportStatus::CompleteResponse||!observed.control.configuredProfile||!observed.control.configuredProfile->sourceCapacity){finish(false,"Actual configured profile failed: "+observed.error+observed.control.error);return;}
        const auto& p=*observed.control.configuredProfile;const auto& cap=*p.sourceCapacity;
        if(!cap.engineJob||!cap.nativePlanRequired){finish(false,"Actual native profile did not bind an exact engine job/plan requirement");return;}
        result.insert("bits",cap.engineJob->bits);result.insert("minimum_gap",int(cap.engineJob->minimumGap));
        result.insert("image_sha256",p.imageSha256);result.insert("configured_binding_sha256",p.bindingSha256);result.insert("scope_start",cap.scopeStart);result.insert("scope_end",cap.scopeEnd);
        pgn::mining::SshObservationRequest request;request.noDriver=true;request.snapshotTimeoutMs=30000;
        snapshot.start(config.configuration->endpoint,request,[&,binding=p.binding](const pgn::mining::SshObservationFrame& frame){
            result.insert("snapshot_state",frame.packet.value("state"));result.insert("owned",frame.packet.value("owned"));
            result.insert("snapshot_binding_matches_profile",frame.packet.value("binding").toObject()==binding);
            return true;
        },[&](const pgn::mining::SshObservationResult& end){finish(end.status==pgn::mining::SshObservationStatus::Complete&&result.value("snapshot_binding_matches_profile")==true,end.error);});
    });
    QTimer::singleShot(65000,&app,[&]{profile.cancel();snapshot.cancel();finish(false,"Read-only actual transport deadline");});
    return app.exec();
}
