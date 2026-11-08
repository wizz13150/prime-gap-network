#pragma once
#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include "wallet/chain_observation.h"
#include <functional>
class QWidget;
namespace pgn::ui {
using WalletObservationSink=std::function<void(const std::optional<pgn::wallet::ChainObservation>&)>;
QWidget* createWalletCoreView(const QString& productRoot,WalletObservationSink observe={},bool testnet=false);
void readWalletLocalConnection(QWidget*,std::function<void(QJsonObject,QString)>);
void stopWalletCoreForUpdate(QWidget*,std::function<void(QString)>);
void readWalletTicketPage(QWidget*,int,std::function<void(QJsonObject,QString)>);
void readWalletTicketDetails(QWidget*,const QString&,std::function<void(QJsonObject,QString)>);
void readWalletTicketAtlas(QWidget*,const QString&,const QString&,int,std::function<void(QJsonObject,QString)>);
void readWalletAuditActivity(QWidget*,qint64,const QString&,const QJsonArray&,std::function<void(QJsonObject,QString)>);
void readWalletAuditDetails(QWidget*,const QString&,std::function<void(QJsonObject,QString)>);
void readWalletSigningIdentity(QWidget*,const QString&,std::function<void(QJsonValue,QString)>);
void requestWalletAudit(QWidget*,const QString&,const QJsonArray&,std::function<void(QJsonValue,QString)>);
void signWalletAudit(QWidget*,const QString&,const QString&,std::function<void(QJsonValue,QString)>);
void inspectWalletBlock(QWidget*,const QString&);
void bindWalletNetworkMetrics(QWidget*,QWidget* owner,std::function<void(const QJsonObject&,const QString&)>);
}
