#include "wallet/runtime_paths.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QUuid>
#include <iostream>

static bool json(const QString& path,const QJsonObject& value){
 QDir().mkpath(QFileInfo(path).absolutePath());QFile file(path);
 return file.open(QIODevice::WriteOnly)&&file.write(QJsonDocument(value).toJson())>0;
}
int main(int argc,char**argv){
 QCoreApplication app(argc,argv);QStandardPaths::setTestModeEnabled(true);
 QCoreApplication::setOrganizationName("PGN-TEST");QCoreApplication::setApplicationName("paths-"+QUuid::createUuid().toString(QUuid::Id128));
 QTemporaryDir temp;if(!temp.isValid())return 2;const auto a=temp.filePath("build-a"),b=temp.filePath("build-b");
 const QString first(64,'a'),second(64,'b');
 const auto declaration=[](const QString& genesis){return QJsonObject{{"schema","PGN_CORE_BUNDLE_V1"},{"deployment",QJsonObject{{"genesis_hash",genesis}}}};};
 if(!json(a+"/Core/core-template.json",declaration(first))||!json(b+"/Core/core-template.json",declaration(first)))return 3;
 using namespace pgn::wallet;
 const auto wallet=coreRuntimeDirectory(a,true),engine=normalEngineStateDirectory(a,first);
 if(wallet!=coreRuntimeDirectory(b,true)||engine!=normalEngineStateDirectory(b,first))return 4;
 if(QFileInfo::exists(wallet)||QFileInfo::exists(engine))return 5;
 if(!json(b+"/Core/core-template.json",declaration(second)))return 6;
 if(wallet==coreRuntimeDirectory(b,true)||engine==normalEngineStateDirectory(b,second))return 7;
 if(coreRuntimeDirectory(b,false)==coreRuntimeDirectory(b,true))return 8;
 const auto legacy=a+"/data/testnet/ui-runtime",legacyEngine=a+"/data/testnet/local-engine";
 const QJsonObject profile{{"schema","pgn-core-deployment-3"},{"product","PGN Core"},{"genesis_hash",first}};
 if(!json(legacy+"/wallet-core.json",profile)||!QDir().mkpath(legacyEngine))return 9;
 if(coreRuntimeDirectory(a,true)!=legacy||normalEngineStateDirectory(a,first)!=legacyEngine)return 10;
 QFile saved(legacy+"/wallet-core.json");if(!saved.open(QIODevice::ReadOnly))return 11;const auto original=saved.readAll();saved.close();
 if(!json(a+"/Core/core-template.json",declaration(second)))return 12;
 if(coreRuntimeDirectory(a,true)==legacy||normalEngineStateDirectory(a,second)==legacyEngine)return 13;
 if(!saved.open(QIODevice::ReadOnly)||saved.readAll()!=original)return 14;
 if(!normalEngineStateDirectory(a,"../../unsafe").isEmpty()||!normalEngineStateDirectory(a,QString(64,'0')).isEmpty())return 15;
 std::cout<<"PASS: install relocation retains user state; genesis/mainnet/testnet isolate; matching legacy profiles retained; old files untouched; lookup creates no state\n";
}
