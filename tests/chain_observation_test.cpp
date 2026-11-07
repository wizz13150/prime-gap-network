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
 auto quiet=chain;quiet.insert("blocks",10);quiet.insert("headers",10);quiet.insert("initialblockdownload",true);quiet.insert("time",utc.addDays(-2).toSecsSinceEpoch());
 auto quietValue=projectChainObservation(epoch,profile,quiet,profile.genesisHash,utc);if(!quietValue.observation)return 9;
 const QString endpoint="127.0.0.1:32100";
 const QJsonObject peer{{"addr",endpoint},{"connection_type","manual"},{"pgn_announced_height",10},{"pgn_syncing",false}};
 const auto caughtUp=projectNativeSynchronization(*quietValue.observation,QJsonArray{peer},endpoint);
 if(caughtUp.synchronizing!=false||caughtUp.coreInitialBlockDownload!=true||!caughtUp.nativePeerCaughtUp||!caughtUp.oldTipAt(utc)||!caughtUp.owner.matches(epoch))return 10;
 for(const auto& pendingPeer:{QJsonObject{{"addr",endpoint},{"connection_type","manual"},{"pgn_announced_height",11},{"pgn_syncing",false}},QJsonObject{{"addr",endpoint},{"connection_type","manual"},{"pgn_announced_height",10},{"pgn_syncing",true}}}){
  const auto held=projectNativeSynchronization(*quietValue.observation,QJsonArray{pendingPeer},endpoint);if(held.synchronizing!=true||held.nativePeerCaughtUp)return 11;
 }
 auto headersAhead=*quietValue.observation;headersAhead.headerHeight=11;
 if(projectNativeSynchronization(headersAhead,QJsonArray{peer},endpoint).synchronizing!=true)return 12;
 auto noHeaders=*quietValue.observation;noHeaders.headerHeight.reset();
 if(projectNativeSynchronization(noHeaders,QJsonArray{peer},endpoint).synchronizing)return 13;
 for(const auto& badHeight:{QJsonValue(),QJsonValue("10"),QJsonValue(true),QJsonValue(-1),QJsonValue(.5),QJsonValue(9007199254740992.0)}){
  auto bad=peer;bad.insert("pgn_announced_height",badHeight);const auto held=projectNativeSynchronization(*quietValue.observation,QJsonArray{bad},endpoint);if(held.synchronizing||held.nativePeerCaughtUp)return 14;
 }
 for(const auto& badSync:{QJsonValue(),QJsonValue("false"),QJsonValue(0)}){
  auto bad=peer;bad.insert("pgn_syncing",badSync);if(projectNativeSynchronization(*quietValue.observation,QJsonArray{bad},endpoint).synchronizing)return 15;
 }
 if(projectNativeSynchronization(*quietValue.observation,{},endpoint).synchronizing!=true)return 16;
 auto missingCoreFlag=*quietValue.observation;missingCoreFlag.coreInitialBlockDownload.reset();if(projectNativeSynchronization(missingCoreFlag,QJsonArray{peer},endpoint).synchronizing)return 17;
 auto fresh=caughtUp;fresh.tipTime=utc.toSecsSinceEpoch();if(fresh.oldTipAt(utc))return 18;
 auto inbound=peer;inbound.insert("connection_type","inbound");if(projectNativeSynchronization(*quietValue.observation,QJsonArray{inbound},endpoint).synchronizing)return 19;
 if(projectNativeSynchronization(*quietValue.observation,QJsonArray{peer},{}).synchronizing)return 20;
 auto other=peer;other.insert("addr","127.0.0.1:32101");if(projectNativeSynchronization(*quietValue.observation,QJsonArray{other},endpoint).synchronizing!=true)return 21;
 std::cout<<"PASS owned chain identity, exact heights, stale rejection, quiet-native catchup preserving raw CoreIBD, pending/unknown/foreignbridge holds\n";
}
