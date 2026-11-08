#pragma once
#include "wallet/core_profile.h"
#include "wallet/setup_selection.h"
#include <QJsonDocument>
#include <QSaveFile>
#include <QCryptographicHash>
namespace pgn::wallet {
struct WalletSetupDeployment {QJsonObject profile;QString error;};
inline WalletSetupDeployment selectedWalletDeployment(const QJsonObject& verifiedBundle,const WalletSetupSelection& choice,const QString& applicationRoot){
    const auto invalid=validateWalletSetup(choice,applicationRoot);if(!invalid.isEmpty())return {{},invalid};
    QJsonObject profile;
    for(const auto* key:{"product","chain","frontend","executable","executable_sha256","genesis_hash","rpc_port","peer_port","peers"})profile.insert(key,verifiedBundle.value(key));
    profile.insert("schema","pgn-core-deployment-3");profile.insert("wallet",choice.walletName);
    const auto directory=QDir::cleanPath(QFileInfo(choice.dataDirectory).absoluteFilePath());
    profile.insert("data_directory",directory);profile.insert("approved_data_directory",directory);profile.insert("data_directory_origin","user_setup");profile.insert("create_wallet",!choice.existing);
    const auto error=CoreProfile::fromJson(profile).validate(applicationRoot);return error.isEmpty()?WalletSetupDeployment{profile,{}}:WalletSetupDeployment{{},error};
}
// Called only after an explicit creation/open selection. Publishes configuration only:
// the selected Core data directory and wallet are created by the subsequent
// verified Core lifecycle, never while a user is browsing or cancelling.
inline QString publishWalletSetup(const QString& runtimeDirectory,const WalletSetupDeployment& selection){
    if(!selection.error.isEmpty()||selection.profile.isEmpty())return "A validated wallet setup selection is required.";
    if(!QFileInfo(runtimeDirectory).isAbsolute()||QDir(runtimeDirectory).isRoot())return "A dedicated user configuration directory is required.";
    const auto target=QDir(runtimeDirectory).filePath("wallet-core.json");
    if(QFileInfo::exists(target))return "Existing wallet configuration requires a controlled change, not an implicit overwrite.";
    const auto bytes=QJsonDocument(selection.profile).toJson(QJsonDocument::Compact);if(bytes.size()>32768)return "Wallet configuration exceeds its size bound.";
    if(!QDir().mkpath(runtimeDirectory))return "Wallet configuration directory unavailable.";
    QSaveFile file(target);if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit())return "Wallet configuration could not be saved; wallet data was not installed.";
    return {};
}
inline QString stageWalletSetupChange(const QString& runtimeDirectory,const WalletSetupDeployment& selection,const QByteArray& expectedBase){
    if(!selection.error.isEmpty()||selection.profile.isEmpty())return "A validated folder selection is required.";
    QFile current(runtimeDirectory+"/wallet-core.json");if(!current.open(QIODevice::ReadOnly)||current.size()>32768)return "Current Core settings are unavailable.";
    const auto bytes=current.readAll();if(bytes!=expectedBase)return "Core settings changed while setup was open. Reopen setup.";
    const auto base=QJsonDocument::fromJson(bytes).object();
    for(const auto* key:{"product","chain","genesis_hash","executable","executable_sha256","frontend","rpc_port","peer_port"})if(base.value(key)!=selection.profile.value(key))return "Folder changes must preserve the verified Core build and network. Component updates are separate.";
    const auto pending=runtimeDirectory+"/wallet-pending-setup.json";if(QFileInfo::exists(pending))return "A folder change is already pending. Cancel it before selecting another folder.";
    const auto encoded=QJsonDocument(QJsonObject{{"schema","PGN_PENDING_CORE_SETUP_V1"},{"base_sha256",QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex())},{"profile",selection.profile}}).toJson(QJsonDocument::Compact);
    QSaveFile file(pending);if(!file.open(QIODevice::WriteOnly)||file.write(encoded)!=encoded.size()||!file.commit())return "The pending folder change could not be saved.";
    return {};
}
// Called only after native ownership inspection confirms the old Core is dead.
// It changes one configuration receipt, never an existing data directory.
inline QString activatePendingWalletSetup(const QString& runtimeDirectory,const QString& applicationRoot,CoreProfile& profile){
    const auto target=runtimeDirectory+"/wallet-core.json",pending=runtimeDirectory+"/wallet-pending-setup.json";
    QFile staged(pending);if(!staged.exists())return {};
    if(!staged.open(QIODevice::ReadOnly)||staged.size()>40000)return "The pending Core folder selection is unreadable.";
    const auto intent=QJsonDocument::fromJson(staged.readAll()).object();staged.close();
    QFile current(target);if(!current.open(QIODevice::ReadOnly)||current.size()>32768)return "Current Core settings are unavailable; pending change retained.";
    const auto base=current.readAll();current.close();const auto next=intent.value("profile").toObject();
    const auto nextBytes=QJsonDocument(next).toJson(QJsonDocument::Compact);
    const auto currentJson=QJsonDocument::fromJson(base).object();
    if(intent.value("schema")!="PGN_PENDING_CORE_SETUP_V1")return "The pending Core folder selection has an unsupported format.";
    for(const auto* key:{"product","chain","genesis_hash","executable","executable_sha256","frontend","rpc_port","peer_port"})if(currentJson.value(key)!=next.value(key))return "Pending Core folder selection belongs to another network or build.";
    // Recovery after atomic publication but before removing the intent.
    if(currentJson!=next&&intent.value("base_sha256").toString()!=QString::fromLatin1(QCryptographicHash::hash(base,QCryptographicHash::Sha256).toHex()))return "Core settings changed; pending folder change was not applied.";
    auto candidate=CoreProfile::fromJson(next);const auto invalid=candidate.validate(applicationRoot);if(!invalid.isEmpty())return invalid;
    const auto layout=validateWalletSetup({candidate.dataDirectory,candidate.walletName,!candidate.createWallet},applicationRoot);if(!layout.isEmpty()&&currentJson!=next)return layout;
    if(currentJson!=next){QSaveFile output(target);if(!output.open(QIODevice::WriteOnly)||output.write(nextBytes)!=nextBytes.size()||!output.commit())return "Pending folder selection could not be activated.";}
    if(!QFile::remove(pending))return "Folder selection saved; pending confirmation could not be cleared. Reopen to recover.";
    profile=candidate;return {};
}
}
