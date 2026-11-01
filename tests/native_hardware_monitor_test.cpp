#include "mining/native_hardware_monitor.h"
#include "mining/native_bridge.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>
#include <iostream>

int main(int argc,char** argv){
    QCoreApplication app(argc,argv);
    if(argc>1&&QString::fromLocal8Bit(argv[1])=="-F"){
        const auto remote=app.arguments().last();
        const auto encoded=remote.section("-EncodedCommand ",1);
        const auto bytes=QByteArray::fromBase64(encoded.toLatin1());
        const auto script=QString::fromUtf16(reinterpret_cast<const char16_t*>(bytes.constData()),bytes.size()/2);
        if(!script.contains("nvidia-smi.exe")||script.contains("native-control.ps1")||script.contains("-Action start"))return 23;
        const QJsonObject packet{{"schema","pgn-native-hardware-observation-1"},{"driver_valid",true},
            {"observed_utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {"gpus",QJsonArray{"0, GPU-11111111-2222-3333-4444-555555555555, SampleGPU, 0000:03:00.0, 0, 12, 24576, 75, 350, 60, 1500, 5001"}}};
        std::cout<<QJsonDocument(packet).toJson(QJsonDocument::Compact).constData()<<std::endl;return 0;
    }
    QTemporaryDir directory;QFile key(directory.filePath("metadata-only-key"));if(!key.open(QIODevice::WriteOnly))return 2;key.write("not a real SSH key");key.close();
    pgn::mining::NativeBridge bridge;int resources=0,source=0,gaps=0,checks=0,failures=0;
    auto check=[&](bool value){++checks;if(!value)++failures;};
    pgn::mining::SshObservationEndpoint endpoint{QCoreApplication::applicationFilePath(),key.fileName(),"fake.invalid","test",22};
    pgn::mining::NativeHardwareMonitor monitor(endpoint,&bridge);
    bridge.onDevice=[&](const auto&){++source;};bridge.onGap=[&](const auto&){++gaps;};bridge.onSourceSnapshot=[&](const auto&){++source;};
    bridge.onResources=[&](const auto& value){
        ++resources;check(value.valid&&!value.stale);check(value.devices.size()==1);
        if(value.devices.size()==1){check(value.devices[0].smClockMhz==1500);check(value.devices[0].memoryClockMhz==5001);}
        check(value.receivedMonotonicNs.has_value());
        QTimer::singleShot(0,&monitor,[&]{monitor.stop();QTimer::singleShot(50,&app,&QCoreApplication::quit);});
    };
    check(monitor.start());check(!monitor.start());
    // The real UI requests its catalogue after attachment. Refusing this
    // command locally must keep the hardware channel usable, including after
    // other unavailable actions are attempted.
    int refusedModules=0,refusedControls=0;
    bridge.onModuleRegistry=[&](const auto& packet){if(packet.value("applied")==false)++refusedModules;};
    bridge.onControl=[&](const auto&,const auto&,bool applied,const auto&){if(!applied)++refusedControls;};
    bridge.moduleAction({{"action","module_list"},{"request_id","initial-catalogue"}});
    bridge.control("GPU-11111111-2222-3333-4444-555555555555","test-control",true);
    check(refusedModules==1);check(refusedControls==1);
    QTimer::singleShot(5000,&app,[&]{check(false);monitor.stop();app.quit();});
    app.exec();monitor.stop();check(resources==1);check(source==0&&gaps==0);
    bridge.onResources={};bridge.onDevice={};bridge.onGap={};bridge.onSourceSnapshot={};
    std::cout<<"hardware monitor checks="<<checks<<" failures="<<failures<<"; own fake child only, no SSH/network/GPU/history/control writer\n";return failures?1:0;
}
