#include "wallet/core_profile.h"
#include "wallet/setup_deployment.h"
#include "wallet/core_runtime.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QEventLoop>
#include <QTimer>
#include <iostream>
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);QTemporaryDir temporary;if(!temporary.isValid())return 1;
 QFile executable(temporary.path()+"/qa-not-executable");executable.open(QIODevice::WriteOnly);executable.write("QA contract fixture, never executed");executable.close();
 const QJsonObject json{{"schema","pgn-core-deployment-1"},{"product","PGN Core"},{"chain","main"},
  {"executable",executable.fileName()},{"executable_sha256",QString(64,'a')},
  {"genesis_hash",QString(64,'b')},{"data_directory",temporary.path()+"/data/wallet-core/node-alex"},
  {"rpc_port",30101},{"peer_port",30102},{"peers",QJsonArray{}}};
 auto profile=pgn::wallet::CoreProfile::fromJson(json);
 if(!profile.validate(temporary.path()).isEmpty())return 2;
 profile.dataDirectory=temporary.path()+"/legacy-wallet";if(profile.validate(temporary.path()).isEmpty())return 3;
 profile=pgn::wallet::CoreProfile::fromJson(json);profile.rpcPort=profile.peerPort;
 if(profile.validate(temporary.path()).isEmpty())return 4;
 profile=pgn::wallet::CoreProfile::fromJson(json);
 if(!pgn::wallet::verifyCoreIdentity(profile,{{"chain","main"}},QString(64,'b')).isEmpty())return 5;
 if(pgn::wallet::verifyCoreIdentity(profile,{{"chain","regtest"}},QString(64,'b')).isEmpty())return 6;
 if(pgn::wallet::verifyCoreIdentity(profile,{{"chain","main"}},QString(64,'c')).isEmpty())return 7;
 const auto binding=pgn::wallet::coreOwnershipBinding(profile,1234,"134041921234567890");
 if(!pgn::wallet::verifyCoreOwnership(profile,binding,1234,"134041921234567890",profile.executable).isEmpty())return 9;
 if(pgn::wallet::verifyCoreOwnership(profile,binding,1234,"134041921234567891",profile.executable).isEmpty())return 10;
 if(pgn::wallet::verifyCoreOwnership(profile,binding,1235,"134041921234567890",profile.executable).isEmpty())return 11;
 auto other=profile;other.genesisHash=QString(64,'c');
 if(pgn::wallet::verifyCoreOwnership(other,binding,1234,"134041921234567890",profile.executable).isEmpty())return 12;
 if(pgn::wallet::verifyCoreOwnership(profile,binding,1234,"134041921234567890",temporary.path()+"/another-client").isEmpty())return 13;
 pgn::wallet::CoreRuntime runtime(temporary.path());QString status;
 runtime.onStatus=[&](const QString& text){status=text;};runtime.launch();
 if(runtime.processId()!=0||runtime.connected()||!status.contains("No verified"))return 8;
 const auto intent=pgn::wallet::coreLaunchIntent(profile,QString(32,'a'),"2026-10-11T02:00:00.000Z");
 if(!pgn::wallet::verifyCoreLaunchIntent(profile,intent).isEmpty())return 14;
 if(pgn::wallet::verifyCoreLaunchIntent(other,intent).isEmpty())return 15;
 auto invalidIntent=intent;invalidIntent.insert("transaction",QString(32,'z'));
 if(pgn::wallet::verifyCoreLaunchIntent(profile,invalidIntent).isEmpty())return 16;
 // Crash fixture: verified inert file + unresolved pre-detach intent. The
 // asynchronous runtime must refuse launch and preserve the journal.
 executable.open(QIODevice::ReadOnly);auto receipt=json;
 receipt.insert("executable_sha256",QString::fromLatin1(QCryptographicHash::hash(executable.readAll(),QCryptographicHash::Sha256).toHex()));executable.close();
 profile=pgn::wallet::CoreProfile::fromJson(receipt);
 QDir().mkpath(temporary.path()+"/data/runtime");
 QFile deployment(temporary.path()+"/data/runtime/wallet-core.json");deployment.open(QIODevice::WriteOnly);deployment.write(QJsonDocument(receipt).toJson());deployment.close();
 QFile pending(temporary.path()+"/data/runtime/wallet-launch-intent.json");pending.open(QIODevice::WriteOnly);
 const auto pendingBytes=QJsonDocument(pgn::wallet::coreLaunchIntent(profile,QString(32,'a'),"2026-10-11T02:00:00.000Z")).toJson();
 pending.write(pendingBytes);pending.close();
 QEventLoop wait;QTimer timeout;timeout.setSingleShot(true);timeout.start(3000);QObject::connect(&timeout,&QTimer::timeout,&wait,&QEventLoop::quit);
 runtime.onStatus=[&](const QString& text){status=text;if(text.contains("unresolved detached"))wait.quit();};
 runtime.launch();wait.exec();
 if(runtime.processId()!=0||runtime.connected()||!status.contains("unresolved detached"))return 17;
 if(!pending.open(QIODevice::ReadOnly)||pending.readAll()!=pendingBytes)return 18;
 auto testReceipt=receipt;testReceipt.insert("schema","pgn-core-deployment-2");testReceipt.insert("chain","test");testReceipt.insert("frontend","daemon");testReceipt.insert("wallet","horizon-testnet");testReceipt.insert("data_directory",temporary.path()+"/data/testnet/node-alex");
 const auto testProfile=pgn::wallet::CoreProfile::fromJson(testReceipt);
 if(!testProfile.validate(temporary.path()).isEmpty()||testProfile.networkDataDirectory()!=temporary.path()+"/data/testnet/node-alex/testnet")return 19;
 if(!pgn::wallet::verifyCoreIdentity(testProfile,{{"chain","test"}},testProfile.genesisHash).isEmpty())return 20;
 if(pgn::wallet::verifyCoreIdentity(testProfile,{{"chain","main"}},testProfile.genesisHash).isEmpty())return 21;
 const auto testBinding=pgn::wallet::coreOwnershipBinding(testProfile,1234,"134041921234567890");
 if(testBinding.value("schema")!="pgn-core-owned-2"||testBinding.value("chain")!="test"||testBinding.value("wallet")!="horizon-testnet")return 22;
 if(pgn::wallet::verifyCoreOwnership(profile,testBinding,1234,"134041921234567890",profile.executable).isEmpty())return 23;
 auto badWallet=testProfile;badWallet.walletName="../other";if(badWallet.validate(temporary.path()).isEmpty())return 24;
 pgn::wallet::CoreRuntime isolated(temporary.path(),nullptr,true);QString isolatedStatus;isolated.onStatus=[&](const QString& text){isolatedStatus=text;};isolated.launch();
 if(isolated.processId()!=0||isolated.connected()||!isolatedStatus.contains("No verified"))return 25;
 QTemporaryDir selectedRoot;const auto selected=selectedRoot.path()+"/PGNCore";auto selectedReceipt=testReceipt;selectedReceipt.insert("schema","pgn-core-deployment-3");selectedReceipt.insert("data_directory",selected);selectedReceipt.insert("approved_data_directory",selected);selectedReceipt.insert("data_directory_origin","user_setup");selectedReceipt.insert("create_wallet",true);
 auto chosen=pgn::wallet::CoreProfile::fromJson(selectedReceipt);if(!chosen.userSelectedDirectory||!chosen.createWallet||!chosen.validate(temporary.path()).isEmpty()||QFileInfo::exists(selected))return 26;
 auto unapproved=selectedReceipt;unapproved.remove("data_directory_origin");if(pgn::wallet::CoreProfile::fromJson(unapproved).validate(temporary.path()).isEmpty())return 27;
 auto moved=selectedReceipt;moved.insert("data_directory",selected+"-other");if(pgn::wallet::CoreProfile::fromJson(moved).validate(temporary.path()).isEmpty())return 28;
 const auto selectedBinding=pgn::wallet::coreOwnershipBinding(chosen,1234,"134041921234567890");if(selectedBinding.value("schema")!="pgn-core-owned-3"||!pgn::wallet::verifyCoreOwnership(chosen,selectedBinding,1234,"134041921234567890",chosen.executable).isEmpty())return 29;
 auto changedApproval=chosen;changedApproval.approvedDataDirectory+="-other";if(pgn::wallet::verifyCoreOwnership(changedApproval,selectedBinding,1234,"134041921234567890",chosen.executable).isEmpty())return 30;
 QDir().mkpath(selected+"/wallets");if(!chosen.validate(temporary.path()).isEmpty())return 31;
 QTemporaryDir setupConfig;const auto newTarget=selectedRoot.path()+"/AnotherPGNCore";const pgn::wallet::WalletSetupSelection selection{newTarget,"PGN",false};
 const auto selectedDeployment=pgn::wallet::selectedWalletDeployment(testReceipt,selection,temporary.path());if(!selectedDeployment.error.isEmpty()||selectedDeployment.profile.isEmpty()||QFileInfo::exists(newTarget))return 32;
 const auto configDirectory=setupConfig.path()+"/config";if(!pgn::wallet::publishWalletSetup(configDirectory,selectedDeployment).isEmpty()||QFileInfo::exists(newTarget)||!QFileInfo::exists(configDirectory+"/wallet-core.json"))return 33;
 if(pgn::wallet::publishWalletSetup(configDirectory,selectedDeployment).isEmpty())return 34;

 std::cout<<"PASS explicit main/test and user-confirmed external data selection, pinned ownership, populated-directory reopen, wrong-chain rejection; absent receipt never starts a wallet\n";
}
