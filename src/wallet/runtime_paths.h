#pragma once
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
namespace pgn::wallet {
inline QString installedCoreGenesis(const QString& applicationRoot){
    QFile file(applicationRoot+"/Core/core-template.json");
    if(!file.open(QIODevice::ReadOnly)||file.size()>32768)return {};
    const auto object=QJsonDocument::fromJson(file.readAll()).object();
    const auto genesis=object.value("deployment").toObject().value("genesis_hash").toString().toLower();
    return object.value("schema")=="PGN_CORE_BUNDLE_V1"&&QRegularExpression("^[0-9a-f]{64}$").match(genesis).hasMatch()?genesis:QString{};
}
inline bool runtimeProfileMatches(const QString& directory,const QString& genesis){
    QFile file(directory+"/wallet-core.json");
    if(!file.open(QIODevice::ReadOnly)||file.size()>32768)return false;
    const auto object=QJsonDocument::fromJson(file.readAll()).object();
    return object.value("product")=="PGN Core"&&(genesis.isEmpty()||object.value("genesis_hash").toString().compare(genesis,Qt::CaseInsensitive)==0);
}
inline QString coreRuntimeDirectory(const QString& applicationRoot,bool testnet){
    // An existing development deployment is kept exactly where its ownership
    // receipt points. Fresh installations use the OS user configuration path.
    const auto legacy=applicationRoot+(testnet?"/data/testnet/ui-runtime":"/data/runtime");
    const auto genesis=installedCoreGenesis(applicationRoot);
    if(runtimeProfileMatches(legacy,genesis))return legacy;
    const auto family=QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)).filePath(testnet?"wallet/testnet":"wallet/main");
    // Keep an explicitly matching existing installation. A different genesis
    // gets a new profile; its older wallet/configuration is never overwritten.
    if(runtimeProfileMatches(family,genesis))return family;
    return genesis.isEmpty()?family:QDir(family).filePath(genesis);
}
inline QString normalEngineStateDirectory(const QString& applicationRoot,const QString& genesis){
    if(!QRegularExpression("^[0-9a-f]{64}$").match(genesis).hasMatch()||genesis==QString(64,'0'))return {};
    const auto legacy=applicationRoot+"/data/testnet/local-engine";
    if(QFileInfo(legacy).isDir()&&runtimeProfileMatches(applicationRoot+"/data/testnet/ui-runtime",genesis))return legacy;
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath("engine/"+genesis);
}
}
