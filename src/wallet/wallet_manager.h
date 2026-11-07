#pragma once
#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <QUrl>
#include <functional>
#include <optional>

namespace pgn::wallet {
using WalletReply=std::function<void(QJsonValue,QString)>;
// walletName is empty for node RPCs. The transport, rather than this model,
// owns cookie authentication, process identity and the verified chain epoch.
using WalletTransport=std::function<void(const QString&,const QJsonArray&,const QString&,WalletReply)>;
QString walletRpcPath(const QString& name);
QString validateNewWalletName(const QString& name);

class WalletManager final:public QObject {
public:
 explicit WalletManager(WalletTransport transport,QObject* parent=nullptr);
 void activate(const QString& genesis,const QString& chain,const QString& dataDirectory,
               const QString& serviceWallet,const QString& stateFile);
 void invalidate();
 void refresh();
 void create(const QString& name);
 void open(const QString& absolutePath);
 void load(const QString& name);
 void select(const QString& name);
 void unload(const QString& name);
 bool holdDisplayOperation();
 void finishDisplayOperation(quint64 generation);
 QString displayWallet()const{return display_;}
 QString serviceWallet()const{return service_;}
 QString dataDirectory()const{return dataDirectory_;}
 QString walletDirectory()const;
 QString network()const{return chain_;}
 QJsonArray wallets()const{return wallets_;}
 bool ready()const{return ready_;}
 bool busy()const{return busy_||displayOperation_;}
 bool selecting()const{return selecting_;}
 bool isLoaded(const QString& name)const;
 quint64 displayGeneration()const{return displayGeneration_;}
 QString message()const{return message_;}
 std::function<void()> onChanged;
private:
 void changed(const QString& message={});
 void operate(const QString& method,const QString& name,bool create);
 void selectVerified(const QString& name,bool save);
 bool begin();
 QString persistDisplay(const QString& name);
 WalletTransport transport_;QString genesis_,chain_,dataDirectory_,service_,display_,stateFile_,message_;
 QJsonArray wallets_;bool ready_{},busy_{},selecting_{},displayOperation_{};quint64 epoch_{},displayGeneration_{};std::optional<QString> restore_;
};
}
