#include "mining/native_runtime_configuration.h"
#include "mining/native_control_command.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

QString script(const pgn::mining::SshObservationCommand& command){const auto value=command.arguments.last();const auto raw=QByteArray::fromBase64(value.mid(value.lastIndexOf(' ')+1).toLatin1());QString result;for(int i=0;i+1<raw.size();i+=2)result+=QChar(uchar(raw[i])|(quint16(uchar(raw[i+1]))<<8));return result;}
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);unsigned checks=0;auto check=[&](bool yes){++checks;if(!yes)throw std::runtime_error("Explicit runtime root check "+std::to_string(checks));};
    using namespace pgn::mining;
    try{
        QTemporaryDir tmp;check(tmp.isValid());QFile key(tmp.filePath("PUBLIC-NOT-A-KEY"));check(key.open(QIODevice::WriteOnly));key.write("PUBLIC TEST DATA");key.close();
        SshObservationEndpoint endpoint{QCoreApplication::applicationFilePath(),key.fileName(),"127.0.0.1","fixture_user",22};
        const auto legacy=script(makeSshObservationCommand(endpoint,{}));check(legacy.contains("'C:/PGN/runtime/native-control.ps1'")&&!legacy.contains("-RuntimeRoot"));
        endpoint.runtimeRoot="C:/PUBLIC TEST/runtime's";endpoint.gpuLeaseRoot="C:/PUBLIC/physical-lease";endpoint.hostKeyAlias="fixture-known-host";
        const auto command=makeSshObservationCommand(endpoint,{});const auto scoped=script(command);
        check(scoped.contains("'C:/PUBLIC TEST/runtime''s/native-control.ps1'"));check(scoped.contains("-RuntimeRoot 'C:/PUBLIC TEST/runtime''s'"));check(scoped.contains("-LeaseRoot 'C:/PUBLIC/physical-lease'"));check(command.arguments.contains("HostKeyAlias=fixture-known-host"));
        const auto control=NativeControlCommand::configured3090Profile(endpoint);check(script(control.ssh()).contains("-RuntimeRoot 'C:/PUBLIC TEST/runtime''s'"));
        for(const auto bad:QStringList{"relative","C:/PUBLIC/../other","C:/PUBLIC/","C:\\PUBLIC","C:/PUBLIC\ncommand"}){auto e=endpoint;e.runtimeRoot=bad;bool rejected=false;try{makeSshObservationCommand(e,{});}catch(const std::exception&){rejected=true;}check(rejected);}
        {auto e=endpoint;e.gpuLeaseRoot.clear();bool rejected=false;try{makeSshObservationCommand(e,{});}catch(const std::exception&){rejected=true;}check(rejected);}
        NativeRuntimePaths paths{tmp.filePath("bookings.sqlite"),tmp.filePath("history.sqlite"),tmp.filePath("mirrors"),tmp.filePath("modules.sqlite"),"C:/legacy/checkpoint"};
        const QJsonObject cuda{{"host","fixture_user@127.0.0.1"},{"key",key.fileName()},{"runtime_root","C:/PUBLIC/isolated"},{"gpu_lease_root","C:/PUBLIC/physical-lease"},{"host_key_alias","fixture-known-host"}};
        const QJsonObject cfg{{"schema","pgn-native-host-transport-2"},{"research",QJsonObject{{"cuda",cuda}}},{"qualification","PUBLIC TEST CONFIGURATION NOT AUTHORITY"}};
        const auto raw=QJsonDocument(cfg).toJson(QJsonDocument::Compact);const auto decoded=decodeNativeRuntimeConfiguration(raw,QCoreApplication::applicationFilePath(),paths);
        check(decoded.available()&&decoded.configuration->sourceUtf8==raw);check(decoded.configuration->paths.checkpointLabel=="C:\\PUBLIC\\isolated\\current-job\\checkpoint");check(decoded.configuration->endpoint.hostKeyAlias=="fixture-known-host");
        check(!decoded.configuration->sourceOnly);
        auto scopedCuda=cuda;scopedCuda.insert("source_only",true);auto sourceCfg=cfg;sourceCfg.insert("research",QJsonObject{{"cuda",scopedCuda}});
        const auto sourceOnly=decodeNativeRuntimeConfiguration(QJsonDocument(sourceCfg).toJson(QJsonDocument::Compact),QCoreApplication::applicationFilePath(),paths);check(sourceOnly.available()&&sourceOnly.configuration->sourceOnly);
        scopedCuda.insert("source_only","true");sourceCfg.insert("research",QJsonObject{{"cuda",scopedCuda}});check(!decodeNativeRuntimeConfiguration(QJsonDocument(sourceCfg).toJson(QJsonDocument::Compact),QCoreApplication::applicationFilePath(),paths).available());
        auto bad=cfg;bad.insert("schema","pgn-existing-host-transport-recovery-1");check(!decodeNativeRuntimeConfiguration(QJsonDocument(bad).toJson(QJsonDocument::Compact),QCoreApplication::applicationFilePath(),paths).available());
        std::cout<<"Explicit native runtime root PASS "<<checks<<" checks; pure paths/argv/config, no SSH/process/GPU/created runtime\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
