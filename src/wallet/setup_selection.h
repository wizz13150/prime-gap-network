#pragma once
#include <QString>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
namespace pgn::wallet {
struct WalletSetupSelection {
    QString dataDirectory,walletName{"PGN"};
    bool existing{};
};
inline QString defaultCoreDataDirectory(){
#ifdef Q_OS_WIN
    const auto roaming=qEnvironmentVariable("APPDATA");
    if(!roaming.isEmpty())return QDir(roaming).filePath("PGNCore");
#endif
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)).filePath("PGNCore");
}
// This validation is read-only. A dialog can be opened/cancelled without
// installing a node, creating directories or touching an existing wallet.
inline QString validateWalletLocation(const QString& dataDirectory,const QString& applicationDirectory){
    const WalletSetupSelection choice{dataDirectory,"PGN",false};
    static const QRegularExpression name("^[A-Za-z0-9_-]{1,64}$");
    if(!name.match(choice.walletName).hasMatch())return "Use 1–64 letters, digits, underscores or hyphens for the wallet name.";
    if(choice.dataDirectory.isEmpty()||!QFileInfo(choice.dataDirectory).isAbsolute())return "Choose an absolute data folder.";
    const auto target=QDir::cleanPath(QFileInfo(choice.dataDirectory).absoluteFilePath());
    if(QDir(target).isRoot()||target.compare(QDir::cleanPath(QDir::homePath()),Qt::CaseInsensitive)==0)return "Choose a dedicated PGN Core folder.";
    const auto application=QFileInfo(applicationDirectory).canonicalFilePath();
    QFileInfo ancestor(target);while(!ancestor.exists()){
        const QFileInfo parent(ancestor.absolutePath());if(parent.filePath()==ancestor.filePath())break;ancestor=parent;
    }
    const auto resolved=ancestor.canonicalFilePath();
    if(resolved.isEmpty())return "The selected folder's parent is unavailable.";
    const auto effective=QDir::cleanPath(resolved+"/"+QDir(ancestor.absoluteFilePath()).relativeFilePath(target));
    if(!application.isEmpty()&&(effective.compare(application,Qt::CaseInsensitive)==0||effective.startsWith(application+"/",Qt::CaseInsensitive)))return "Keep wallet data outside the Horizon application folder.";
    if(QFileInfo(target).exists()&&!QFileInfo(target).isDir())return "Choose a folder, not a wallet file.";
    return {};
}
inline QString validateWalletSetup(const WalletSetupSelection& choice,const QString& applicationDirectory){
    static const QRegularExpression name("^[A-Za-z0-9_-]{1,64}$");
    if(!name.match(choice.walletName).hasMatch())return "Use 1–64 letters, digits, underscores or hyphens for the wallet name.";
    const auto invalid=validateWalletLocation(choice.dataDirectory,applicationDirectory);if(!invalid.isEmpty())return invalid;
    const auto target=QDir::cleanPath(QFileInfo(choice.dataDirectory).absoluteFilePath());
    if(choice.existing){
        if(!QFileInfo(target).isDir())return "Choose the existing PGN Core data folder.";
        const QDir directory(target);
        if(!directory.exists("blocks")&&!directory.exists("chainstate")&&!directory.exists("wallets")&&!directory.exists("testnet"))return "This folder does not contain an existing Core data layout.";
    }else if(QFileInfo(target).isDir()&&!QDir(target).entryList(QDir::AllEntries|QDir::NoDotAndDotDot|QDir::Hidden).isEmpty())return "Use an empty folder for a new wallet, or choose Open existing.";
    return {};
}
}
