#pragma once
#include "wallet/core_profile.h"
#include "wallet/setup_selection.h"
#include <QJsonDocument>
#include <QSaveFile>
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
// The wizard calls this only from Finish. It publishes configuration only:
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
}
