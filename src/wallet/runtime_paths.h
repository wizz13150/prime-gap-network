#pragma once
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
namespace pgn::wallet {
inline QString coreRuntimeDirectory(const QString& applicationRoot,bool testnet){
    // An existing development deployment is kept exactly where its ownership
    // receipt points. Fresh installations use the OS user configuration path.
    const auto legacy=applicationRoot+(testnet?"/data/testnet/ui-runtime":"/data/runtime");
    if(QFileInfo::exists(legacy+"/wallet-core.json"))return legacy;
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)).filePath(testnet?"wallet/testnet":"wallet/main");
}
}
