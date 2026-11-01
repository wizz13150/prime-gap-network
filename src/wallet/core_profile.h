#pragma once
#include <QString>
#include <QStringList>
#include <QJsonObject>

namespace pgn::wallet {
// An explicit build/deployment receipt, not invented chain parameters. The
// economics, genesis and consensus belong to the actual Core implementation.
struct CoreProfile {
 QString executable, executableSha256, dataDirectory, genesisHash;
 QStringList peers;
 int rpcPort{}, peerPort{};
 QString chain{"main"},frontend{"qt"},walletName;
 QString approvedDataDirectory;bool userSelectedDirectory{},createWallet{};
 QString networkDataDirectory()const{return dataDirectory+(chain=="test"?QStringLiteral("/testnet"):QString{});}
 static CoreProfile fromJson(const QJsonObject&);
 QString validate(const QString& productRoot) const;
};
QString verifyCoreIdentity(const CoreProfile&,const QJsonObject& chain,
                           const QString& observedGenesis);
// Strings preserve the full Windows creation tick/PID, without JSON rounding.
QJsonObject coreOwnershipBinding(const CoreProfile&,qint64 pid,const QString& birth);
QString verifyCoreOwnership(const CoreProfile&,const QJsonObject&,qint64 pid,
                            const QString& birth,const QString& observedImage);
QJsonObject coreLaunchIntent(const CoreProfile&,const QString& transaction,const QString& utc);
QString verifyCoreLaunchIntent(const CoreProfile&,const QJsonObject&);
}
