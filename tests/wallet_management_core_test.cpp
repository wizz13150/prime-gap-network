#include "wallet/wallet_manager.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QThread>
#include <iostream>
#include <stdexcept>
using namespace pgn::wallet;
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);if(argc!=2)return 2;QFile configFile(QString::fromLocal8Bit(argv[1]));if(!configFile.open(QIODevice::ReadOnly)||configFile.size()>16384)return 3;const auto config=QJsonDocument::fromJson(configFile.readAll()).object();
 QFile cookie(config.value("cookie").toString());if(!cookie.open(QIODevice::ReadOnly)||cookie.size()>4096)return 4;const auto auth="Basic "+cookie.readAll().trimmed().toBase64();QNetworkAccessManager network;int calls=0,checks=0;
 auto require=[&](bool okay){++checks;if(!okay)throw std::runtime_error("real Core wallet check "+std::to_string(checks));};
 WalletTransport transport=[&](const QString& method,const QJsonArray& params,const QString& wallet,WalletReply done){
  ++calls;const auto endpoint=wallet.isNull()?QString("/"):walletRpcPath(wallet);QNetworkRequest request(QUrl::fromEncoded(("http://127.0.0.1:"+QString::number(config.value("port").toInt())+endpoint).toUtf8()));request.setRawHeader("Authorization",auth);request.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");request.setTransferTimeout(15000);
  auto* reply=network.post(request,QJsonDocument(QJsonObject{{"jsonrpc","2.0"},{"id","isolated-wallet-qa"},{"method",method},{"params",params}}).toJson(QJsonDocument::Compact));QObject::connect(reply,&QNetworkReply::finished,&app,[reply,done=std::move(done)]{const auto result=QJsonDocument::fromJson(reply->readAll()).object();reply->deleteLater();const auto error=result.value("error").toObject();done(result.value("result"),error.isEmpty()?QString{}:QString("Core error: ")+error.value("message").toString());});
 };
 auto spin=[&](auto predicate){QElapsedTimer timer;timer.start();while(!predicate()&&timer.elapsed()<20000){app.processEvents();QThread::msleep(2);}require(predicate());};
 WalletManager manager(transport);manager.activate(config.value("genesis").toString(),"test",config.value("directory").toString(),"service",config.value("state").toString());spin([&]{return !manager.busy();});
 if(config.value("restore_only").toBool()){require(manager.displayWallet()=="Savings"&&manager.serviceWallet()=="service");std::cout<<"PASS actual Core restart restores displayed wallet and preserves service binding\n";return 0;}
 manager.create("Savings");manager.create("Savings");spin([&]{return !manager.busy();});require(manager.message()=="Wallet list refreshed.");manager.select("Savings");spin([&]{return !manager.busy();});require(manager.displayWallet()=="Savings"&&manager.serviceWallet()=="service");
 WalletManager missingService(transport);missingService.activate(config.value("genesis").toString(),"test",config.value("directory").toString(),"MissingService",config.value("state").toString()+".missing");spin([&]{return !missingService.busy();});require(!missingService.isLoaded("MissingService"));missingService.select("Savings");spin([&]{return !missingService.busy();});require(missingService.displayWallet()=="Savings"&&missingService.serviceWallet()=="MissingService");
 const auto before=calls;manager.unload("service");require(calls==before&&manager.message().contains("reserved"));
 bool read=false;transport("getwalletinfo",{},QString("Savings"),[&](QJsonValue result,QString error){require(error.isEmpty()&&result.toObject().value("walletname")=="Savings");read=true;});spin([&]{return read;});
 read=false;transport("getwalletinfo",{},QString("service"),[&](QJsonValue result,QString error){require(error.isEmpty()&&result.toObject().value("walletname")=="service");read=true;});spin([&]{return read;});
 manager.create("Temporary");spin([&]{return !manager.busy();});manager.unload("Temporary");spin([&]{return !manager.busy();});manager.load("Temporary");spin([&]{return !manager.busy();});
 const auto external=config.value("external").toString();read=false;transport("createwallet",{external,false,false,QJsonValue{},false,false,false},{},[&](QJsonValue,QString error){require(error.isEmpty());read=true;});spin([&]{return read;});read=false;transport("unloadwallet",{external,false},{},[&](QJsonValue,QString error){require(error.isEmpty());read=true;});spin([&]{return read;});manager.open(external);spin([&]{return !manager.busy();});
 auto found=false;for(const auto& entry:manager.wallets())found|=entry.toObject().value("loaded").toBool()&&entry.toObject().value("name")==external;require(found);manager.select(external);spin([&]{return !manager.busy();});require(manager.displayWallet()==external&&manager.serviceWallet()=="service");manager.select("Savings");spin([&]{return !manager.busy();});manager.unload(external);spin([&]{return !manager.busy();});
 std::cout<<"PASS "<<checks<<" actual isolated PGN Core checks: create/load/open/select/unload, external encoded wallet, persistence, double click, service binding retained; no peers/mining\n";
}
