#include "wallet/chain_observation.h"
#include <QCoreApplication>
#include <iostream>
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);using namespace pgn::wallet;
 CoreProfile profile;profile.genesisHash=QString(64,'b');
 const CoreEpoch epoch{42,"134041921234567890",1};
 const auto utc=QDateTime::fromString("2026-10-11T11:00:00.123Z",Qt::ISODateWithMs);
 const QJsonObject chain{{"chain","main"},{"blocks",0},{"initialblockdownload",false}};
 const auto result=projectChainObservation(epoch,profile,chain,QString(64,'B'),utc);
 if(!result.observation||!result.error.isEmpty())return 1;
 const auto& value=*result.observation;
 if(value.height!=0||value.genesis!=profile.genesisHash||value.observedUtc!=utc||
    !value.synchronizing.has_value()||*value.synchronizing||!value.owner.matches(epoch))return 2;
 if(!value.freshAt(utc)||!value.freshAt(utc.addMSecs(30000))||value.freshAt(utc.addMSecs(30001))||
    value.freshAt(utc.addMSecs(-1))||value.freshAt(utc,-1))return 3;
 auto missingSync=chain;missingSync.remove("initialblockdownload");
 const auto unknown=projectChainObservation(epoch,profile,missingSync,profile.genesisHash,utc);
 if(!unknown.observation||unknown.observation->synchronizing.has_value())return 4;
 auto badChain=chain;badChain.insert("chain","regtest");
 if(projectChainObservation(epoch,profile,badChain,profile.genesisHash,utc).observation||
    projectChainObservation(epoch,profile,chain,QString(64,'c'),utc).observation||
    projectChainObservation({},profile,chain,profile.genesisHash,utc).observation||
    projectChainObservation(epoch,profile,chain,profile.genesisHash,{}).observation)return 5;
 for(const auto& height:{QJsonValue(),QJsonValue("0"),QJsonValue(-1),QJsonValue(0.5),QJsonValue(9007199254740992.0),QJsonValue(true)}){
  auto invalid=chain;invalid.insert("blocks",height);
  if(projectChainObservation(epoch,profile,invalid,profile.genesisHash,utc).observation)return 6;
 }
 auto missingHeight=chain;missingHeight.remove("blocks");
 if(projectChainObservation(epoch,profile,missingHeight,profile.genesisHash,utc).observation)return 7;
 auto invalid=value;invalid.genesis="unbound";if(invalid.valid()||invalid.freshAt(utc))return 8;
 std::cout<<"PASS owned mainnet identity, exact height zero, unknown sync, original time and explicit stale rejection\n";
}
