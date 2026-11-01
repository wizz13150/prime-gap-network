#include "mining/native_control_command.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

int main(int argc,char** argv){
    QCoreApplication app(argc,argv);if(argc!=2)return 2;unsigned checks=0;
    auto check=[&](bool yes){++checks;if(!yes)throw std::runtime_error("Engine capacity check "+std::to_string(checks));};
    try{
        QFile f(QString::fromLocal8Bit(argv[1]));check(f.open(QIODevice::ReadOnly)&&f.size()<32768);
        auto binding=QJsonDocument::fromJson(f.readAll()).object();
        const auto image=binding.value("engine").toObject().value("image_sha256").toString();
        QJsonObject job{{"start",binding.value("start")},{"end",binding.value("end")},{"profile_k",64},{"minimum_gap","1332"}};
        QJsonObject capacity{{"schema","pgn-prime-gap-source-capacity-1"},{"policy","ADAPTIVE_PRIME_GAP_67_95"},
            {"sdk",binding.value("sdk")},{"scope_start","73786976294838206464"},{"scope_end","39614081257132168796771975168"},
            {"max_length","1000000000000000"},{"guard","PRIME_GAP_PLAN_REQUIRED"},
            {"source_manifest_sha256",pgn::mining::primeGapSourceManifest()},{"job",job}};
        QJsonObject packet{{"schema","pgn-configured-profile-observation-1"},{"protocol","pgn-configured-stop-1"},{"status","CONFIGURED_PROFILE_OBSERVED"},
            {"profile",QJsonObject{{"device",binding.value("uuid")},{"executable",binding.value("executable")},{"image_sha256",image},{"binding_sha256",QString(64,'A')}}},
            {"binding",binding},{"source_capacity",capacity}};
        QTemporaryDir tmp;check(tmp.isValid());QFile key(tmp.filePath("PUBLIC-NOT-A-KEY"));check(key.open(QIODevice::WriteOnly));key.write("PUBLIC FIXTURE");key.close();
        pgn::mining::SshObservationEndpoint endpoint{QCoreApplication::applicationFilePath(),key.fileName(),"127.0.0.1","fixture_user",22};
        const auto command=pgn::mining::NativeControlCommand::configured3090Profile(endpoint);
        auto decode=[&](const QJsonObject& p){return command.decode(QJsonDocument(p).toJson(QJsonDocument::Compact),true,0);};
        const auto result=decode(packet);check(result.configuredProfile.has_value());
        const auto& cap=result.configuredProfile->sourceCapacity;check(cap&&cap->engineJob&&cap->engineJob->bits==95&&cap->engineJob->minimumGap==1332&&cap->nativePlanRequired);
        for(const auto name:{"schema","policy","sdk","scope_start","scope_end","max_length","guard","source_manifest_sha256","job"}){
            auto bad=packet;auto c=capacity;c.insert(name,"wrong");bad.insert("source_capacity",c);check(!decode(bad).configuredProfile);
        }
        const auto pins=binding.value("pins").toArray();
        for(int i=0;i<pins.size();++i){auto bad=packet;auto b=binding;auto altered=pins;auto pin=altered[i].toObject();pin.insert("sha256",QString(64,'0'));altered[i]=pin;b.insert("pins",altered);bad.insert("binding",b);check(!decode(bad).configuredProfile);}
        {auto bad=packet;auto b=binding;auto e=b.value("engine").toObject();e.insert("minimum_gap",1334);b.insert("engine",e);bad.insert("binding",b);check(!decode(bad).configuredProfile);}
        std::cout<<"Existing PGN configured engine capacity PASS "<<checks<<" checks; metadata only, no process or reservation\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
