#include "mining/run_source_profile.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStringList>
#include <cstdio>
#include <stdexcept>

using namespace pgn::mining;
namespace {
int checks{};
void check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
QJsonObject profile(){return {{"schema","pgn-run-source-profile-1"},{"protocol","pgn-source-legacy-a73"},
    {"image_sha256","a73a6e383308560edc36ce351b09eb449fa099275361be74be89020b03864b43"},
    {"executable","C:/PGN/native/20261012-gpu-owned-source-v1/bin/pgn-continuous-gpu.exe"}};}
QJsonObject boundaryProfile(){return {{"schema","pgn-run-source-profile-1"},{"protocol","pgn-source-boundary-v2"},
    {"image_sha256","74a0fdd01ccf242fa3ca8714e4765c23022987c6785889cd8b1ecfa3aa27472b"},
    {"executable","C:/PGN/native/20261015-source-boundary-v2/bin/pgn-continuous-gpu.exe"}};}
QJsonObject lazyProfile(){return {{"schema","pgn-run-source-profile-1"},{"protocol","pgn-source-legacy-lazy-v1"},
    {"image_sha256","a1597b9c4d2431c204bcc27ae69e9840c86cc5253e0fb63c09fa162266f55639"},
    {"executable","C:/PGN/native/20261017-lazy-integer-v1/bin/pgn-continuous-gpu.exe"}};}
QJsonObject sharedProfile(){return {{"schema","pgn-run-source-profile-1"},{"protocol","pgn-source-legacy-lazy-shared-v1"},
    {"image_sha256","fa6f7e44d1187e1cc519fdc0454d36c8e886d1c7d1f3f4c8321adcaa8399aa81"},
    {"executable","C:/PGN/native/20261017-lazy-shared-v1/bin/pgn-continuous-gpu.exe"}};}
QJsonObject periodicProfile(){return {{"schema","pgn-run-source-profile-1"},{"protocol","pgn-source-legacy-lazy-shared-periodic-v1"},
    {"image_sha256","bfebd7f80dd7b0670a3ff51573c476ab0fd61daff7b59949ea4e60af8031bc53"},
    {"executable","C:/PGN/native/20261017-lazy-shared-periodic-v1/bin/pgn-continuous-gpu.exe"}};}
QJsonObject bankProfile(){return {{"schema","pgn-run-source-profile-1"},{"protocol","pgn-source-legacy-lazy-shared-periodic-bank-v1"},
    {"image_sha256","9a76e632fa1761b26b68b33d6d7eee15c1d7a0ff6e0d0d5772da9c6f0c13171e"},
    {"executable","C:/PGN/native/20261017-lazy-shared-periodic-bank-v1/bin/pgn-continuous-gpu.exe"}};}
QJsonObject radix70Profile(){return {{"schema","pgn-run-source-profile-1"},{"protocol","pgn-source-legacy-radix70-v1"},
    {"image_sha256","c6f209ab51936473611e7d34dde30e838793073f2d061d5dc8c0d4da69884684"},
    {"executable","C:/PGN/native/20261017-radix70-v1/bin/pgn-continuous-gpu.exe"}};}
QJsonObject floatPrefixProfile(){return {{"schema","pgn-run-source-profile-1"},{"protocol","pgn-source-legacy-radix70-float-prefix-v1"},
    {"image_sha256","9afba37ae45903e9c279349041784cc0b8f1ebfce129bd10992f84870126644c"},
    {"executable","C:/PGN/native/20261017-radix70-float-prefix-v1/bin/pgn-continuous-gpu.exe"}};}
QJsonObject barrett70Profile(){return {{"schema","pgn-run-source-profile-1"},{"protocol","pgn-source-legacy-barrett70-v1"},
    {"image_sha256","1a909ab796ac1e385dee6f1c09ab6ceb1a3a334b887e46cfbbf2d656726cb41a"},
    {"executable","C:/PGN/native/20261017-barrett70-v1/bin/pgn-continuous-gpu.exe"}};}
QJsonObject window2Recip5Profile(){return {{"schema","pgn-run-source-profile-1"},{"protocol","pgn-source-legacy-window2-recip5-v1"},
    {"image_sha256","c5fbaf75de15631933c87419ec638ca53e3cb6b03668ad0a6b1f5b1818979566"},
    {"executable","C:/PGN/native/20261017-window2-recip5-v1/bin/pgn-continuous-gpu.exe"}};}
QJsonObject top2Profile(){return {{"schema","pgn-run-source-profile-1"},{"protocol","pgn-source-legacy-barrett70-top2-v1"},
    {"image_sha256","bbf75d45431606b39f2bb5e9e7c74f5c8ef1009b0cdfbbe3458e164adb78aff2"},
    {"executable","C:/PGN/native/20261017-barrett70-top2-v1/bin/pgn-continuous-gpu.exe"}};}
QJsonObject rollingRootsProfile(){return {{"schema","pgn-run-source-profile-1"},{"protocol","pgn-source-legacy-rolling-roots-v1"},
    {"image_sha256","3e800bb02f0249c315b4d84eb346ef887660e023beb52ba57981ae4a26f8558b"},
    {"executable","C:/PGN/native/20261017-rolling-roots-v1/bin/pgn-continuous-gpu.exe"}};}
QJsonObject rollingRootsLanes2Profile(){return {{"schema","pgn-run-source-profile-1"},{"protocol","pgn-source-legacy-rolling-roots-lanes2-v1"},
    {"image_sha256","2876a22cb28805062d3f01423e56c918b60b6a4e09c14a888a7333c98d697488"},
    {"executable","C:/PGN/native/20261017-rolling-roots-lanes2-v1/bin/pgn-continuous-gpu.exe"}};}
QJsonObject carryChainProfile(){return {{"schema","pgn-run-source-profile-1"},{"protocol","pgn-source-legacy-carry-chain-v1"},
    {"image_sha256","65c7b566b76d6a90cd94c75b2d31bf8c516074f30cce8e5ea307363816f82b8d"},
    {"executable","C:/PGN/native/20261018-carry-chain-v1/bin/pgn-continuous-gpu.exe"}};}
QJsonObject high47Profile(){return {{"schema","pgn-run-source-profile-1"},{"protocol","pgn-source-legacy-fused-high47-v1"},{"image_sha256","da55d109d9e5bd366dc6a9d8e3ae29f7a5d831b4839ac17fd891adf0f8b23906"},{"executable","C:/PGN/native/20261018-fused-high47-v1/bin/pgn-continuous-gpu.exe"}};}
QJsonObject fusedSquareProfile(){return {{"schema","pgn-run-source-profile-1"},{"protocol","pgn-source-legacy-fused-square-v1"},
    {"image_sha256","12e1c0a2d43955f51a21aca6ff41ded562f44bdbda7b29a303ce3ba1c26fed59"},
    {"executable","C:/PGN/native/20261018-fused-square-v1/bin/pgn-continuous-gpu.exe"}};}
QJsonObject fusedWindow3RingProfile(){return {{"schema","pgn-run-source-profile-1"},{"protocol","pgn-source-legacy-fused-window3-ring-v1"},
    {"image_sha256","42dea3f63af76512d2799248696d4f1f22fdd4d54aad803c2abe1367d29302a1"},
    {"executable","C:/PGN/native/20261018-fused-window3-ring-v1/bin/pgn-continuous-gpu.exe"}};}
QJsonObject manifest(){return {{"schema","pgn-native-process-1"},{"run",QString(32,'a')},
    {"start","199932000019059617039"},{"end","199932000019059617463"},
    {"actual_unknown_extension",QJsonObject{{"preserved","PUBLIC TEST ONLY"}}},{"source_profile",profile()}};}
void inspect(const QJsonObject& m,RunSourceProfileStatus expected){
    const auto before=QJsonDocument(m).toJson(QJsonDocument::Compact);const auto r=inspectRunSourceProfile(m);
    check(r.status==expected,"profile status differs");
    check(QJsonDocument(m).toJson(QJsonDocument::Compact)==before,"inspector must preserve original manifest");
    if(expected==RunSourceProfileStatus::Invalid)check(!r.error.isEmpty()&&r.error.size()<=256&&r.profile.isEmpty(),"invalid profile cannot expose plausible metadata");
    else check(r.error.isEmpty(),"valid or absent metadata has no parsing error");
    if(expected==RunSourceProfileStatus::AbsentHistoricalUnattested)check(r.profile.isEmpty(),"historical absence cannot invent A73 or v2");
    if(expected==RunSourceProfileStatus::KnownLegacyA73||expected==RunSourceProfileStatus::KnownBoundaryV2||expected==RunSourceProfileStatus::KnownLazyV1||expected==RunSourceProfileStatus::KnownLazySharedV1||expected==RunSourceProfileStatus::KnownLazySharedPeriodicV1||expected==RunSourceProfileStatus::KnownLazySharedPeriodicBankV1||expected==RunSourceProfileStatus::KnownRadix70V1||expected==RunSourceProfileStatus::KnownFloatPrefixV1||expected==RunSourceProfileStatus::KnownBarrett70V1||expected==RunSourceProfileStatus::KnownWindow2Recip5V1||expected==RunSourceProfileStatus::KnownTop2V1||expected==RunSourceProfileStatus::KnownRollingRootsV1||expected==RunSourceProfileStatus::KnownRollingRootsLanes2V1||expected==RunSourceProfileStatus::KnownCarryChainV1||expected==RunSourceProfileStatus::KnownFusedSquareV1||expected==RunSourceProfileStatus::KnownFusedHigh47V1||expected==RunSourceProfileStatus::KnownFusedWindow3RingV1)check(r.profile==m.value("source_profile").toObject(),"actual source profile stays exact");
}
}
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);
    try{
        for(int i=0;i<20;++i){RunSourceProfileInspection p;p.status=static_cast<RunSourceProfileStatus>(i);
            check(runSourceProtocolAdmitted(p,{})==(i==1||i==2||i==4||i==5||i==6||i==7||i==8||i==9||i==10||i==11||i==12||i==13||i==14||i==15||i==16||p.status==RunSourceProfileStatus::KnownFusedHigh47V1||p.status==RunSourceProfileStatus::KnownFusedWindow3RingV1),"legacy protocol only for explicitly supported known/absent statuses");
            check(runSourceProtocolAdmitted(p,"pgn-source-boundary-v2")== (p.status==RunSourceProfileStatus::KnownBoundaryV2),"v2 grammar requires the qualified explicit image status");
            check(!runSourceProtocolAdmitted(p,"unknown"),"unknown protocol always Held");
        }
        auto lazy=manifest();lazy.insert("source_profile",lazyProfile());inspect(lazy,RunSourceProfileStatus::KnownLazyV1);
        for(const auto& key:lazyProfile().keys()){
            auto p=lazyProfile();p.remove(key);auto m=lazy;m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
            p=lazyProfile();p.insert(key,QJsonValue::Null);m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
            p=lazyProfile();p.insert(key,p.value(key).toString()+"changed");m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
        }
        for(const auto& oldProfile:{profile(),boundaryProfile()})for(const auto& key:QStringList{"protocol","image_sha256","executable"}){
            auto p=lazyProfile();p.insert(key,oldProfile.value(key));auto m=lazy;m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
            p=oldProfile;p.insert(key,lazyProfile().value(key));m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
        }
        auto shared=manifest();shared.insert("source_profile",sharedProfile());inspect(shared,RunSourceProfileStatus::KnownLazySharedV1);
        for(const auto& key:sharedProfile().keys()){
            auto p=sharedProfile();p.remove(key);auto m=shared;m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
            p=sharedProfile();p.insert(key,QJsonValue::Null);m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
            p=sharedProfile();p.insert(key,p.value(key).toString()+"changed");m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
        }
        for(const auto& oldProfile:{profile(),boundaryProfile(),lazyProfile()})for(const auto& key:QStringList{"protocol","image_sha256","executable"}){
            auto p=sharedProfile();p.insert(key,oldProfile.value(key));auto m=shared;m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
            p=oldProfile;p.insert(key,sharedProfile().value(key));m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
        }
        auto periodic=manifest();periodic.insert("source_profile",periodicProfile());inspect(periodic,RunSourceProfileStatus::KnownLazySharedPeriodicV1);
        for(const auto& key:periodicProfile().keys()){
            auto p=periodicProfile();p.remove(key);auto m=periodic;m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
            p=periodicProfile();p.insert(key,QJsonValue::Null);m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
            p=periodicProfile();p.insert(key,p.value(key).toString()+"changed");m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
        }
        for(const auto& oldProfile:{profile(),boundaryProfile(),lazyProfile(),sharedProfile()})for(const auto& key:QStringList{"protocol","image_sha256","executable"}){
            auto p=periodicProfile();p.insert(key,oldProfile.value(key));auto m=periodic;m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
            p=oldProfile;p.insert(key,periodicProfile().value(key));m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
        }
        auto bank=manifest();bank.insert("source_profile",bankProfile());inspect(bank,RunSourceProfileStatus::KnownLazySharedPeriodicBankV1);
        for(const auto& key:bankProfile().keys()){
            auto p=bankProfile();p.remove(key);auto m=bank;m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
            p=bankProfile();p.insert(key,QJsonValue::Null);m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
            p=bankProfile();p.insert(key,p.value(key).toString()+"changed");m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
        }
        for(const auto& oldProfile:{profile(),boundaryProfile(),lazyProfile(),sharedProfile(),periodicProfile()})for(const auto& key:QStringList{"protocol","image_sha256","executable"}){
            auto p=bankProfile();p.insert(key,oldProfile.value(key));auto m=bank;m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
            p=oldProfile;p.insert(key,bankProfile().value(key));m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
        }
        auto radix70=manifest();radix70.insert("source_profile",radix70Profile());inspect(radix70,RunSourceProfileStatus::KnownRadix70V1);
        for(const auto& key:radix70Profile().keys()){
            auto p=radix70Profile();p.remove(key);auto m=radix70;m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
            p=radix70Profile();p.insert(key,QJsonValue::Null);m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
            p=radix70Profile();p.insert(key,p.value(key).toString()+"changed");m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
        }
        for(const auto& oldProfile:{profile(),boundaryProfile(),lazyProfile(),sharedProfile(),periodicProfile(),bankProfile()})for(const auto& key:QStringList{"protocol","image_sha256","executable"}){
            auto p=radix70Profile();p.insert(key,oldProfile.value(key));auto m=radix70;m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
            p=oldProfile;p.insert(key,radix70Profile().value(key));m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
        }
        auto floatPrefix=manifest();floatPrefix.insert("source_profile",floatPrefixProfile());inspect(floatPrefix,RunSourceProfileStatus::KnownFloatPrefixV1);
        for(const auto& key:floatPrefixProfile().keys()){
            auto p=floatPrefixProfile();p.remove(key);auto m=floatPrefix;m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
            p=floatPrefixProfile();p.insert(key,QJsonValue::Null);m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
            p=floatPrefixProfile();p.insert(key,p.value(key).toString()+"changed");m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
        }
        for(const auto& oldProfile:{profile(),boundaryProfile(),lazyProfile(),sharedProfile(),periodicProfile(),bankProfile(),radix70Profile()})for(const auto& key:QStringList{"protocol","image_sha256","executable"}){
            auto p=floatPrefixProfile();p.insert(key,oldProfile.value(key));auto m=floatPrefix;m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
            p=oldProfile;p.insert(key,floatPrefixProfile().value(key));m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
        }
        auto barrett70=manifest();barrett70.insert("source_profile",barrett70Profile());inspect(barrett70,RunSourceProfileStatus::KnownBarrett70V1);
        for(const auto& key:barrett70Profile().keys()){
            auto p=barrett70Profile();p.remove(key);auto m=barrett70;m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
            p=barrett70Profile();p.insert(key,QJsonValue::Null);m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
            p=barrett70Profile();p.insert(key,p.value(key).toString()+"changed");m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
        }
        for(const auto& oldProfile:{profile(),boundaryProfile(),lazyProfile(),sharedProfile(),periodicProfile(),bankProfile(),radix70Profile(),floatPrefixProfile()})for(const auto& key:QStringList{"protocol","image_sha256","executable"}){
            auto p=barrett70Profile();p.insert(key,oldProfile.value(key));auto m=barrett70;m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
            p=oldProfile;p.insert(key,barrett70Profile().value(key));m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
        }
        auto window2Recip5=manifest();window2Recip5.insert("source_profile",window2Recip5Profile());inspect(window2Recip5,RunSourceProfileStatus::KnownWindow2Recip5V1);
        for(const auto& key:window2Recip5Profile().keys()){
            auto p=window2Recip5Profile();p.remove(key);auto m=window2Recip5;m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
            p=window2Recip5Profile();p.insert(key,QJsonValue::Null);m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
            p=window2Recip5Profile();p.insert(key,p.value(key).toString()+"changed");m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
        }
        for(const auto& oldProfile:{profile(),boundaryProfile(),lazyProfile(),sharedProfile(),periodicProfile(),bankProfile(),radix70Profile(),floatPrefixProfile(),barrett70Profile()})for(const auto& key:QStringList{"protocol","image_sha256","executable"}){
            auto p=window2Recip5Profile();p.insert(key,oldProfile.value(key));auto m=window2Recip5;m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
            p=oldProfile;p.insert(key,window2Recip5Profile().value(key));m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
        }
        auto top2=manifest();top2.insert("source_profile",top2Profile());inspect(top2,RunSourceProfileStatus::KnownTop2V1);
        for(const auto& key:top2Profile().keys()){
            auto p=top2Profile();p.remove(key);auto m=top2;m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
            p=top2Profile();p.insert(key,QJsonValue::Null);m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
            p=top2Profile();p.insert(key,p.value(key).toString()+"changed");m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
        }
        for(const auto& oldProfile:{profile(),boundaryProfile(),lazyProfile(),sharedProfile(),periodicProfile(),bankProfile(),radix70Profile(),floatPrefixProfile(),barrett70Profile(),window2Recip5Profile()})for(const auto& key:QStringList{"protocol","image_sha256","executable"}){
            auto p=top2Profile();p.insert(key,oldProfile.value(key));auto m=top2;m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
            p=oldProfile;p.insert(key,top2Profile().value(key));m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
        }
        auto rollingRoots=manifest();rollingRoots.insert("source_profile",rollingRootsProfile());inspect(rollingRoots,RunSourceProfileStatus::KnownRollingRootsV1);
        for(const auto& key:rollingRootsProfile().keys()){
            auto p=rollingRootsProfile();p.remove(key);auto m=rollingRoots;m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
            p=rollingRootsProfile();p.insert(key,QJsonValue::Null);m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
            p=rollingRootsProfile();p.insert(key,p.value(key).toString()+"changed");m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
        }
        for(const auto& oldProfile:{profile(),boundaryProfile(),lazyProfile(),sharedProfile(),periodicProfile(),bankProfile(),radix70Profile(),floatPrefixProfile(),barrett70Profile(),window2Recip5Profile(),top2Profile()})for(const auto& key:QStringList{"protocol","image_sha256","executable"}){
            auto p=rollingRootsProfile();p.insert(key,oldProfile.value(key));auto m=rollingRoots;m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
            p=oldProfile;p.insert(key,rollingRootsProfile().value(key));m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
        }
        auto rollingRootsLanes2=manifest();rollingRootsLanes2.insert("source_profile",rollingRootsLanes2Profile());inspect(rollingRootsLanes2,RunSourceProfileStatus::KnownRollingRootsLanes2V1);
        for(const auto& key:rollingRootsLanes2Profile().keys()){
            auto p=rollingRootsLanes2Profile();p.remove(key);auto m=rollingRootsLanes2;m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
            p=rollingRootsLanes2Profile();p.insert(key,QJsonValue::Null);m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
            p=rollingRootsLanes2Profile();p.insert(key,p.value(key).toString()+"changed");m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
        }
        for(const auto& oldProfile:{profile(),boundaryProfile(),lazyProfile(),sharedProfile(),periodicProfile(),bankProfile(),radix70Profile(),floatPrefixProfile(),barrett70Profile(),window2Recip5Profile(),top2Profile(),rollingRootsProfile()})for(const auto& key:QStringList{"protocol","image_sha256","executable"}){
            auto p=rollingRootsLanes2Profile();p.insert(key,oldProfile.value(key));auto m=rollingRootsLanes2;m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
            p=oldProfile;p.insert(key,rollingRootsLanes2Profile().value(key));m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
        }
        auto carryChain=manifest();carryChain.insert("source_profile",carryChainProfile());inspect(carryChain,RunSourceProfileStatus::KnownCarryChainV1);
        for(const auto& key:carryChainProfile().keys()){
            auto p=carryChainProfile();p.remove(key);auto m=carryChain;m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
            p=carryChainProfile();p.insert(key,QJsonValue::Null);m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
            p=carryChainProfile();p.insert(key,p.value(key).toString()+"changed");m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
        }
        for(const auto& oldProfile:{profile(),boundaryProfile(),lazyProfile(),sharedProfile(),periodicProfile(),bankProfile(),radix70Profile(),floatPrefixProfile(),barrett70Profile(),window2Recip5Profile(),top2Profile(),rollingRootsProfile(),rollingRootsLanes2Profile()})for(const auto& key:QStringList{"protocol","image_sha256","executable"}){
            auto p=carryChainProfile();p.insert(key,oldProfile.value(key));auto m=carryChain;m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
            p=oldProfile;p.insert(key,carryChainProfile().value(key));m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
        }
        auto high47=manifest();high47.insert("source_profile",high47Profile());inspect(high47,RunSourceProfileStatus::KnownFusedHigh47V1);
        check(runSourceProtocolAdmitted(inspectRunSourceProfile(high47),QString{}),"high47 unchanged RAW grammar admitted");
        check(!runSourceProtocolAdmitted(inspectRunSourceProfile(high47),"pgn-source-boundary-v2"),"high47 does not counterfeit boundary-v2 evidence");
        for(const auto* key:{"protocol","image_sha256","executable"}){auto changed=high47;auto p=high47Profile();p[key]=fusedSquareProfile().value(key);changed["source_profile"]=p;inspect(changed,RunSourceProfileStatus::Invalid);}
        auto fusedSquare=manifest();fusedSquare.insert("source_profile",fusedSquareProfile());inspect(fusedSquare,RunSourceProfileStatus::KnownFusedSquareV1);
        for(const auto& key:fusedSquareProfile().keys()){
            auto p=fusedSquareProfile();p.remove(key);auto m=fusedSquare;m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
            p=fusedSquareProfile();p.insert(key,QJsonValue::Null);m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
            p=fusedSquareProfile();p.insert(key,p.value(key).toString()+"changed");m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
        }
        for(const auto& oldProfile:{profile(),boundaryProfile(),lazyProfile(),sharedProfile(),periodicProfile(),bankProfile(),radix70Profile(),floatPrefixProfile(),barrett70Profile(),window2Recip5Profile(),top2Profile(),rollingRootsProfile(),rollingRootsLanes2Profile(),carryChainProfile()})for(const auto& key:QStringList{"protocol","image_sha256","executable"}){
            auto p=fusedSquareProfile();p.insert(key,oldProfile.value(key));auto m=fusedSquare;m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
            p=oldProfile;p.insert(key,fusedSquareProfile().value(key));m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
        }
        auto fusedWindow3Ring=manifest();fusedWindow3Ring.insert("source_profile",fusedWindow3RingProfile());inspect(fusedWindow3Ring,RunSourceProfileStatus::KnownFusedWindow3RingV1);
        for(const auto& key:fusedWindow3RingProfile().keys()){
            auto p=fusedWindow3RingProfile();p.remove(key);auto m=fusedWindow3Ring;m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
            p=fusedWindow3RingProfile();p.insert(key,QJsonValue::Null);m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
            p=fusedWindow3RingProfile();p.insert(key,p.value(key).toString()+"changed");m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
        }
        for(const auto& oldProfile:{profile(),boundaryProfile(),lazyProfile(),sharedProfile(),periodicProfile(),bankProfile(),radix70Profile(),floatPrefixProfile(),barrett70Profile(),window2Recip5Profile(),top2Profile(),rollingRootsProfile(),rollingRootsLanes2Profile(),carryChainProfile(),fusedSquareProfile(),high47Profile()})for(const auto& key:QStringList{"protocol","image_sha256","executable"}){
            auto p=fusedWindow3RingProfile();p.insert(key,oldProfile.value(key));auto m=fusedWindow3Ring;m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
            p=oldProfile;p.insert(key,fusedWindow3RingProfile().value(key));m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
        }
        auto v2=manifest();v2.insert("source_profile",boundaryProfile());inspect(v2,RunSourceProfileStatus::KnownBoundaryV2);
        for(const auto& key:boundaryProfile().keys()){
            auto p=boundaryProfile();p.remove(key);auto m=v2;m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
            p=boundaryProfile();p.insert(key,QJsonValue::Null);m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
            p=boundaryProfile();p.insert(key,p.value(key).toString()+"changed");m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
        }
        // Every individual value is known, but mixing generations is invalid.
        for(const auto& key:QStringList{"protocol","image_sha256","executable"}){
            auto p=boundaryProfile();p.insert(key,profile().value(key));auto m=v2;m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
            p=profile();p.insert(key,boundaryProfile().value(key));m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
        }
        for(const auto& key:QStringList{"image_sha256","executable"}){
            auto p=boundaryProfile();p.insert(key,p.value(key).toString().toUpper());auto m=v2;m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
        }
        inspect(manifest(),RunSourceProfileStatus::KnownLegacyA73);
        auto old=manifest();old.remove("source_profile");inspect(old,RunSourceProfileStatus::AbsentHistoricalUnattested);
        // Changing a current/future binding is not an input to this API and
        // cannot relabel the old run. Its immutable actual manifest alone wins.
        old.insert("binding",QJsonObject{{"source_boundary_protocol","pgn-source-boundary-v2"}});
        inspect(old,RunSourceProfileStatus::AbsentHistoricalUnattested);
        auto m=manifest();m.insert("binding",QJsonObject{{"executable","unknown-v2.exe"}});
        inspect(m,RunSourceProfileStatus::KnownLegacyA73);
        for(const QJsonValue& v:QJsonArray{QJsonValue::Null,false,0,"pgn-source-legacy-a73",QJsonArray{}}){
            m=manifest();m.insert("source_profile",v);inspect(m,RunSourceProfileStatus::Invalid);
        }
        for(const auto& key:profile().keys()){
            auto p=profile();p.remove(key);m=manifest();m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
            p=profile();p.insert(key,QJsonValue::Null);m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
            p=profile();p.insert(key,p.value(key).toString()+"changed");m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
        }
        auto p=profile();p.insert("trusted",true);m=manifest();m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
        p=profile();p.insert("protocol","pgn-source-boundary-v2");m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
        p=profile();p.insert("image_sha256",p.value("image_sha256").toString().toUpper());m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
        p=profile();p.insert("executable","C:\\PGN\\native\\20261012-gpu-owned-source-v1\\bin\\pgn-continuous-gpu.exe");m.insert("source_profile",p);inspect(m,RunSourceProfileStatus::Invalid);
        {
            auto next=fusedWindow3RingProfile();next["image_sha256"]="ca363cb76cbaf9a008402e0d47b80dc6dc846c331efa1bd42daf91de15dfc6b3";next["executable"]="C:/PGN/native/20261018-fused-window3-seed8r-v1/bin/pgn-continuous-gpu.exe";
            auto current=manifest();current["source_profile"]=next;inspect(current,RunSourceProfileStatus::KnownFusedWindow3RingV1);
            for(const auto& key:next.keys()){
                auto p=next;p.remove(key);auto m=current;m["source_profile"]=p;inspect(m,RunSourceProfileStatus::Invalid);
                p=next;p[key]=QJsonValue::Null;m["source_profile"]=p;inspect(m,RunSourceProfileStatus::Invalid);
                p=next;p[key]=p.value(key).toString()+"changed";m["source_profile"]=p;inspect(m,RunSourceProfileStatus::Invalid);
            }
            // Same legacy wire grammar, distinct immutable image/path triples.
            for(const auto* key:{"image_sha256","executable"}){
                auto p=next;p[key]=fusedWindow3RingProfile().value(key);auto m=current;m["source_profile"]=p;inspect(m,RunSourceProfileStatus::Invalid);
                p=fusedWindow3RingProfile();p[key]=next.value(key);m["source_profile"]=p;inspect(m,RunSourceProfileStatus::Invalid);
            }
        }
        {
            auto next=fusedWindow3RingProfile();next["image_sha256"]="122abd3da35adfce429325f3ed0e16cca8b38b78bc144a11722a9ac342793952";next["executable"]="C:/PGN/native/20261019-frontier16-retry-v2/bin/pgn-continuous-gpu.exe";
            auto current=manifest();current["source_profile"]=next;inspect(current,RunSourceProfileStatus::KnownFusedWindow3RingV1);
            for(const auto& key:next.keys()){
                auto p=next;p.remove(key);auto m=current;m["source_profile"]=p;inspect(m,RunSourceProfileStatus::Invalid);
                p=next;p[key]=QJsonValue::Null;m["source_profile"]=p;inspect(m,RunSourceProfileStatus::Invalid);
                p=next;p[key]=p.value(key).toString()+"changed";m["source_profile"]=p;inspect(m,RunSourceProfileStatus::Invalid);
            }
            // Same legacy wire grammar, distinct immutable image/path triples.
            for(const auto* key:{"image_sha256","executable"}){
                auto p=next;p[key]=fusedWindow3RingProfile().value(key);auto m=current;m["source_profile"]=p;inspect(m,RunSourceProfileStatus::Invalid);
                p=fusedWindow3RingProfile();p[key]=next.value(key);m["source_profile"]=p;inspect(m,RunSourceProfileStatus::Invalid);
            }
        }
        {
            auto next=fusedWindow3RingProfile();next["image_sha256"]="3121a85578aebc61648311216a468574745447a52fed7088285d2222c029d1e3";next["executable"]="C:/PGN/native/20261019-frontier16-event210-hybrid-v1/bin/pgn-continuous-gpu.exe";
            auto current=manifest();current["source_profile"]=next;inspect(current,RunSourceProfileStatus::KnownFusedWindow3RingV1);
            for(const auto& key:next.keys()){
                auto p=next;p.remove(key);auto m=current;m["source_profile"]=p;inspect(m,RunSourceProfileStatus::Invalid);
                p=next;p[key]=QJsonValue::Null;m["source_profile"]=p;inspect(m,RunSourceProfileStatus::Invalid);
                p=next;p[key]=p.value(key).toString()+"changed";m["source_profile"]=p;inspect(m,RunSourceProfileStatus::Invalid);
            }
            // Same legacy wire grammar, distinct immutable image/path triples.
            for(const auto* key:{"image_sha256","executable"}){
                auto p=next;p[key]=fusedWindow3RingProfile().value(key);auto m=current;m["source_profile"]=p;inspect(m,RunSourceProfileStatus::Invalid);
                p=fusedWindow3RingProfile();p[key]=next.value(key);m["source_profile"]=p;inspect(m,RunSourceProfileStatus::Invalid);
            }
        }
        {
            auto next=fusedWindow3RingProfile();next["image_sha256"]="b0439859530229d2c3451690c3a0361a78fe570cdff14f131efb1b8a22253c43";next["executable"]="C:/PGN/native/20261019-frontier16-event210-composed-v1/bin/pgn-continuous-gpu.exe";
            auto current=manifest();current["source_profile"]=next;inspect(current,RunSourceProfileStatus::KnownFusedWindow3RingV1);
            for(const auto& key:next.keys()){
                auto p=next;p.remove(key);auto m=current;m["source_profile"]=p;inspect(m,RunSourceProfileStatus::Invalid);
                p=next;p[key]=QJsonValue::Null;m["source_profile"]=p;inspect(m,RunSourceProfileStatus::Invalid);
                p=next;p[key]=p.value(key).toString()+"changed";m["source_profile"]=p;inspect(m,RunSourceProfileStatus::Invalid);
            }
            // Same legacy wire grammar, distinct immutable image/path triples.
            for(const auto* key:{"image_sha256","executable"}){
                auto p=next;p[key]=fusedWindow3RingProfile().value(key);auto m=current;m["source_profile"]=p;inspect(m,RunSourceProfileStatus::Invalid);
                p=fusedWindow3RingProfile();p[key]=next.value(key);m["source_profile"]=p;inspect(m,RunSourceProfileStatus::Invalid);
            }
        }
        {
            auto next=fusedWindow3RingProfile();next["image_sha256"]="85412118a5686090fa612c090c6c43dc9222231e4ac08a39bc594f4d4235a945";next["executable"]="C:/PGN/native/20261019-event210-smallword7-v1/bin/pgn-continuous-gpu.exe";
            auto current=manifest();current["source_profile"]=next;inspect(current,RunSourceProfileStatus::KnownFusedWindow3RingV1);
            for(const auto& key:next.keys()){
                auto p=next;p.remove(key);auto m=current;m["source_profile"]=p;inspect(m,RunSourceProfileStatus::Invalid);
                p=next;p[key]=QJsonValue::Null;m["source_profile"]=p;inspect(m,RunSourceProfileStatus::Invalid);
                p=next;p[key]=p.value(key).toString()+"changed";m["source_profile"]=p;inspect(m,RunSourceProfileStatus::Invalid);
            }
            // Same legacy wire grammar, distinct immutable image/path triples.
            for(const auto* key:{"image_sha256","executable"}){
                auto p=next;p[key]=fusedWindow3RingProfile().value(key);auto m=current;m["source_profile"]=p;inspect(m,RunSourceProfileStatus::Invalid);
                p=fusedWindow3RingProfile();p[key]=next.value(key);m["source_profile"]=p;inspect(m,RunSourceProfileStatus::Invalid);
            }
        }
        {
            auto next=fusedWindow3RingProfile();next["image_sha256"]="843bcadbaa279a19e5cd0d6c54abd386ddef47b10b1f2e41a8da32c6390a0428";next["executable"]="C:/PGN/native/20261019-event-pair16-v1/bin/pgn-continuous-gpu.exe";
            auto current=manifest();current["source_profile"]=next;inspect(current,RunSourceProfileStatus::KnownFusedWindow3RingV1);
            for(const auto& key:next.keys()){
                auto p=next;p.remove(key);auto m=current;m["source_profile"]=p;inspect(m,RunSourceProfileStatus::Invalid);
                p=next;p[key]=QJsonValue::Null;m["source_profile"]=p;inspect(m,RunSourceProfileStatus::Invalid);
                p=next;p[key]=p.value(key).toString()+"changed";m["source_profile"]=p;inspect(m,RunSourceProfileStatus::Invalid);
            }
            // Same legacy wire grammar, distinct immutable image/path triples.
            for(const auto* key:{"image_sha256","executable"}){
                auto p=next;p[key]=fusedWindow3RingProfile().value(key);auto m=current;m["source_profile"]=p;inspect(m,RunSourceProfileStatus::Invalid);
                p=fusedWindow3RingProfile();p[key]=next.value(key);m["source_profile"]=p;inspect(m,RunSourceProfileStatus::Invalid);
            }
        }
        {
            auto next=fusedWindow3RingProfile();next["image_sha256"]="cf3c215bf30140f89d04324377f64694d106852120634a47af6b08558a6294ed";next["executable"]="C:/PGN/native/20261019-prefix11-final-v1/bin/pgn-continuous-gpu.exe";
            auto current=manifest();current["source_profile"]=next;inspect(current,RunSourceProfileStatus::KnownFusedWindow3RingV1);
            for(const auto& key:next.keys()){
                auto p=next;p.remove(key);auto m=current;m["source_profile"]=p;inspect(m,RunSourceProfileStatus::Invalid);
                p=next;p[key]=QJsonValue::Null;m["source_profile"]=p;inspect(m,RunSourceProfileStatus::Invalid);
                p=next;p[key]=p.value(key).toString()+"changed";m["source_profile"]=p;inspect(m,RunSourceProfileStatus::Invalid);
            }
            // Same legacy wire grammar, distinct immutable image/path triples.
            for(const auto* key:{"image_sha256","executable"}){
                auto p=next;p[key]=fusedWindow3RingProfile().value(key);auto m=current;m["source_profile"]=p;inspect(m,RunSourceProfileStatus::Invalid);
                p=fusedWindow3RingProfile();p[key]=next.value(key);m["source_profile"]=p;inspect(m,RunSourceProfileStatus::Invalid);
            }
        }
        {
            auto next=fusedWindow3RingProfile();next["image_sha256"]="ccd3d4fda96b8c26b74947b6fc8399d5502efbf55a4aefcabfc92659c6277673";next["executable"]="C:/PGN/native/20261020-stateful-u160-live-v1/bin/pgn-continuous-gpu.exe";
            auto current=manifest();current["source_profile"]=next;inspect(current,RunSourceProfileStatus::KnownFusedWindow3RingV1);
            for(const auto& key:next.keys()){
                auto p=next;p.remove(key);auto m=current;m["source_profile"]=p;inspect(m,RunSourceProfileStatus::Invalid);
                p=next;p[key]=QJsonValue::Null;m["source_profile"]=p;inspect(m,RunSourceProfileStatus::Invalid);
                p=next;p[key]=p.value(key).toString()+"changed";m["source_profile"]=p;inspect(m,RunSourceProfileStatus::Invalid);
            }
            // Same legacy wire grammar, distinct immutable image/path triples.
            for(const auto* key:{"image_sha256","executable"}){
                auto p=next;p[key]=fusedWindow3RingProfile().value(key);auto m=current;m["source_profile"]=p;inspect(m,RunSourceProfileStatus::Invalid);
                p=fusedWindow3RingProfile();p[key]=next.value(key);m["source_profile"]=p;inspect(m,RunSourceProfileStatus::Invalid);
            }
        }
        {
            auto next=fusedWindow3RingProfile();next["image_sha256"]="962a98e4d6fd97390ce20ff99143c7bc1befd7a1093998d8b73f730d28525f67";next["executable"]="C:/PGN/native/20261021-live-composition-v22/bin/pgn-continuous-gpu.exe";
            auto current=manifest();current["source_profile"]=next;inspect(current,RunSourceProfileStatus::KnownFusedWindow3RingV1);
            for(const auto& key:next.keys()){
                auto p=next;p.remove(key);auto m=current;m["source_profile"]=p;inspect(m,RunSourceProfileStatus::Invalid);
                p=next;p[key]=QJsonValue::Null;m["source_profile"]=p;inspect(m,RunSourceProfileStatus::Invalid);
                p=next;p[key]=p.value(key).toString()+"changed";m["source_profile"]=p;inspect(m,RunSourceProfileStatus::Invalid);
            }
            // Same legacy wire grammar, distinct immutable image/path triples.
            for(const auto* key:{"image_sha256","executable"}){
                auto p=next;p[key]=fusedWindow3RingProfile().value(key);auto m=current;m["source_profile"]=p;inspect(m,RunSourceProfileStatus::Invalid);
                p=fusedWindow3RingProfile();p[key]=next.value(key);m["source_profile"]=p;inspect(m,RunSourceProfileStatus::Invalid);
            }
        }
        if(argc==2){
            QFile f(QString::fromLocal8Bit(argv[1]));check(f.open(QIODevice::ReadOnly)&&f.size()>0&&f.size()<=4096,"closed PS fixture must be bounded");
            QJsonParseError error;const auto d=QJsonDocument::fromJson(f.readAll(),&error);
            check(error.error==QJsonParseError::NoError&&d.isObject(),"closed PS fixture exact JSON object");
            inspect(d.object(),RunSourceProfileStatus::KnownLegacyA73);
        }
        std::printf("PASS checks=%d failures=0; pure profile mapping, no image/process authentication or launch\n",checks);return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL checks=%d %s\n",checks,e.what());return 1;}
}
