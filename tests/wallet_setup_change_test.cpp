#include "wallet/setup_deployment.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);QTemporaryDir temp;int checks=0;auto require=[&](bool ok){++checks;if(!ok)throw std::runtime_error("setup change check "+std::to_string(checks));};
 const auto root=temp.filePath("app"),runtime=temp.filePath("configuration"),first=temp.filePath("first"),second=temp.filePath("second");QDir().mkpath(root);QDir().mkpath(runtime);QFile exe(root+"/core.exe");exe.open(QIODevice::WriteOnly);exe.write("INERT");exe.close();
 QJsonObject bundle{{"product","PGN Core"},{"chain","test"},{"frontend","daemon"},{"executable",exe.fileName()},{"executable_sha256",QString(64,'a')},{"genesis_hash",QString(64,'b')},{"rpc_port",30123},{"peer_port",30124}};
 auto initial=pgn::wallet::selectedWalletDeployment(bundle,{first,"service",false},root);require(initial.error.isEmpty());require(pgn::wallet::publishWalletSetup(runtime,initial).isEmpty());QFile config(runtime+"/wallet-core.json");config.open(QIODevice::ReadOnly);const auto base=config.readAll();config.close();
 auto selection=pgn::wallet::selectedWalletDeployment(bundle,{second,"next",false},root);require(selection.error.isEmpty());
 require(!pgn::wallet::stageWalletSetupChange(runtime,selection,"stale").isEmpty());require(!QFileInfo::exists(second));
 auto wrong=selection;wrong.profile.insert("genesis_hash",QString(64,'c'));require(!pgn::wallet::stageWalletSetupChange(runtime,wrong,base).isEmpty());
 require(pgn::wallet::stageWalletSetupChange(runtime,selection,base).isEmpty());require(!pgn::wallet::stageWalletSetupChange(runtime,selection,base).isEmpty());
 config.open(QIODevice::ReadOnly);require(config.readAll()==base);config.close();require(!QFileInfo::exists(first)&&!QFileInfo::exists(second));
 auto profile=pgn::wallet::CoreProfile::fromJson(initial.profile);require(pgn::wallet::activatePendingWalletSetup(runtime,root,profile).isEmpty());require(profile.dataDirectory==second&&!QFileInfo::exists(second));require(!QFileInfo::exists(runtime+"/wallet-pending-setup.json"));
 require(pgn::wallet::activatePendingWalletSetup(runtime,root,profile).isEmpty());
 require(config.open(QIODevice::ReadOnly));const auto activeBase=config.readAll();config.close();
 auto third=pgn::wallet::selectedWalletDeployment(bundle,{temp.filePath("third"),"next",false},root);require(third.error.isEmpty());require(pgn::wallet::stageWalletSetupChange(runtime,third,activeBase).isEmpty());
 auto edited=QJsonDocument::fromJson(activeBase).object();edited.insert("external_edit",true);{QSaveFile changed(config.fileName());require(changed.open(QIODevice::WriteOnly));changed.write(QJsonDocument(edited).toJson(QJsonDocument::Compact));require(changed.commit());}
 require(!pgn::wallet::activatePendingWalletSetup(runtime,root,profile).isEmpty());require(QFileInfo::exists(runtime+"/wallet-pending-setup.json")&&!QFileInfo::exists(temp.filePath("third"))&&profile.dataDirectory==second);
 {QSaveFile restore(config.fileName());require(restore.open(QIODevice::WriteOnly));restore.write(activeBase);require(restore.commit());}require(pgn::wallet::activatePendingWalletSetup(runtime,root,profile).isEmpty());
 std::cout<<"PASS "<<checks<<" controlled Core folder checks: stale base/network rejected, staged only, duplicate guarded, atomic activation, no data creation/movement\n";
}
