#include "wallet/core_profile.h"
#include "wallet/setup_selection.h"
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QJsonArray>
#include <QDateTime>
namespace pgn::wallet {
QJsonObject coreLaunchIntent(const CoreProfile& p,const QString& transaction,const QString& utc){
 auto intent=coreOwnershipBinding(p,0,{});intent.remove("pid");intent.remove("birth");
 intent.insert("schema","pgn-core-launch-intent-1");intent.insert("transaction",transaction);intent.insert("utc",utc);
 return intent;
}
QString verifyCoreLaunchIntent(const CoreProfile& p,const QJsonObject& intent){
 static const QRegularExpression transaction("^[0-9a-f]{32}$");
 const auto id=intent.value("transaction").toString(),utc=intent.value("utc").toString();
 if(!transaction.match(id).hasMatch()||!QDateTime::fromString(utc,Qt::ISODateWithMs).isValid()||
    intent!=coreLaunchIntent(p,id,utc))return "Unresolved Core launch intent is invalid or belongs to another deployment; no new client launched";
 return {};
}
QJsonObject coreOwnershipBinding(const CoreProfile& p,qint64 pid,const QString& birth){
 QJsonObject owned{{"schema","pgn-core-owned-1"},{"pid",QString::number(pid)},{"birth",birth},
  {"executable",QFileInfo(p.executable).canonicalFilePath()},{"executable_sha256",p.executableSha256.toLower()},
  {"data_directory",QDir::cleanPath(p.dataDirectory)},{"genesis_hash",p.genesisHash.toLower()},
  {"rpc_port",p.rpcPort},{"peer_port",p.peerPort}};
 if(p.chain!="main"||p.frontend!="qt"||!p.walletName.isEmpty()){owned.insert("schema","pgn-core-owned-2");owned.insert("chain",p.chain);owned.insert("frontend",p.frontend);owned.insert("wallet",p.walletName);}
 if(p.userSelectedDirectory){owned.insert("schema","pgn-core-owned-3");owned.insert("approved_data_directory",QDir::cleanPath(p.approvedDataDirectory));}
 return owned;
}
QString verifyCoreOwnership(const CoreProfile& p,const QJsonObject& saved,qint64 pid,
                            const QString& birth,const QString& observedImage){
 if(pid<=0||birth.isEmpty()||saved!=coreOwnershipBinding(p,pid,birth))
  return "Saved Core process ownership does not match this deployment/creation epoch";
 if(QFileInfo(observedImage).canonicalFilePath().compare(QFileInfo(p.executable).canonicalFilePath(),Qt::CaseInsensitive)!=0)
  return "Saved Core process image does not match the pinned executable";
 return {};
}
CoreProfile CoreProfile::fromJson(const QJsonObject& json){
 CoreProfile p;
 if(json.value("schema")=="pgn-core-deployment-3"&&
    (json.value("data_directory_origin")!="user_setup"||!json.value("approved_data_directory").isString()||!json.value("create_wallet").isBool()))return p;
 // No legacy default executable, network, port, genesis or wallet directory.
 if((json.value("schema").toString()!="pgn-core-deployment-1"&&json.value("schema").toString()!="pgn-core-deployment-2"&&json.value("schema").toString()!="pgn-core-deployment-3") ||
    json.value("product").toString()!="PGN Core" ||
    (json.value("chain").toString()!="main"&&json.value("chain").toString()!="test"))return p;
 p.chain=json.value("chain").toString();p.frontend=json.value("frontend").toString("qt");p.walletName=json.value("wallet").toString();
 p.executable=json.value("executable").toString();
 p.executableSha256=json.value("executable_sha256").toString();
 p.dataDirectory=json.value("data_directory").toString();
 p.genesisHash=json.value("genesis_hash").toString();
 p.rpcPort=json.value("rpc_port").toInt();p.peerPort=json.value("peer_port").toInt();
 if(json.value("schema")=="pgn-core-deployment-3"){
  p.userSelectedDirectory=json.value("data_directory_origin")=="user_setup";
  p.approvedDataDirectory=json.value("approved_data_directory").toString();
  p.createWallet=json.value("create_wallet").toBool();
 }
 for(const auto peer:json.value("peers").toArray())p.peers.append(peer.toString());
 return p;
}
QString CoreProfile::validate(const QString& productRoot) const {
 static const QRegularExpression digest("^[0-9a-fA-F]{64}$");
 if(!digest.match(genesisHash).hasMatch()||!digest.match(executableSha256).hasMatch())
  return "Verified PGN Core deployment/genesis receipt is missing";
 if(!QFileInfo(executable).isAbsolute()||!QFileInfo(executable).isFile())
  return "Pinned PGN Core executable is unavailable";
 if(rpcPort<1024||rpcPort>65535||peerPort<1024||peerPort>65535||rpcPort==peerPort)
  return "Separate explicit RPC/P2P ports are required";
 // Resolve existing ancestors as well: a junction must not route the new
 // wallet into an old wallet directory or another product's live data.
 if((chain!="main"&&chain!="test")||(frontend!="qt"&&frontend!="daemon"))return "Explicit supported Core chain and frontend required";
 if(walletName.size()>64||(!walletName.isEmpty()&&!QRegularExpression("^[A-Za-z0-9_-]+$").match(walletName).hasMatch()))return "Explicit wallet name is malformed";
 if(userSelectedDirectory){
  if(walletName.isEmpty()||QDir::cleanPath(approvedDataDirectory)!=QDir::cleanPath(dataDirectory))return "Wallet location does not match the confirmed setup selection";
  const auto invalid=validateWalletLocation(dataDirectory,productRoot);if(!invalid.isEmpty())return invalid;
 }else{
 const QString allowed=QDir(productRoot).absoluteFilePath(chain=="test"?"data/testnet":"data/wallet-core");
 const QString target=QDir::cleanPath(QFileInfo(dataDirectory).absoluteFilePath());
 if(!QFileInfo(dataDirectory).isAbsolute()||
    !target.startsWith(QDir::cleanPath(allowed)+"/",Qt::CaseInsensitive))
  return "Wallet data must use its own PGN data/wallet-core subdirectory";
 QFileInfo ancestor(target);while(!ancestor.exists()){
  const QFileInfo parent(ancestor.absolutePath());if(parent.filePath()==ancestor.filePath())break;ancestor=parent;
 }
 const QString canonicalRoot=QFileInfo(productRoot).canonicalFilePath();
 const QString expectedAncestor=QDir::cleanPath(canonicalRoot+"/"+QDir(productRoot).relativeFilePath(ancestor.absoluteFilePath()));
 if(canonicalRoot.isEmpty()||ancestor.canonicalFilePath().compare(expectedAncestor,Qt::CaseInsensitive)!=0)
  return "Wallet data ancestor resolves outside this PGN product";
 if(QFileInfo(target).exists()&&!QFileInfo(target).isDir())return "Wallet data target is not a directory";
 }
 if(peers.size()>64)return "Explicit peer list exceeds its deployment bound";
 for(const auto& peer:peers)
  if(peer.isEmpty()||peer.startsWith('-')||peer.contains(QRegularExpression("[\\s\\r\\n]")))
   return "Peer endpoint is malformed";
 return {};
}
QString verifyCoreIdentity(const CoreProfile& profile,const QJsonObject& chain,
                           const QString& observedGenesis){
 if(chain.value("chain").toString()!=profile.chain)return "Core is not on the explicitly requested chain";
 if(observedGenesis.compare(profile.genesisHash,Qt::CaseInsensitive)!=0)
  return "Core genesis does not match the pinned PGN chain";
 return {};
}
}
