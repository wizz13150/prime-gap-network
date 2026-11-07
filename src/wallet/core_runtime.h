#pragma once
#include "wallet/core_profile.h"
#include "wallet/wallet_manager.h"
#include "wallet/core_rpc.h"
#include "wallet/chain_observation.h"
#include "wallet/core_link_state.h"
#include "wallet/peer_bootstrap.h"
#include <QObject>
#include <QProcess>
#include <QNetworkAccessManager>
#include <QTimer>
#include <QFutureWatcher>
#include <QLockFile>
#include <memory>
#include <functional>
namespace pgn::wallet {
// No mining headers, candidate subscriptions, per-gap hash or wallet calls.
// Only this owned child may be stopped; the GPU process is never an input.
class CoreRuntime final : public QObject {
public:
 explicit CoreRuntime(QString productRoot,QObject* parent=nullptr,bool testnet=false,QString runtimeDirectory={});
 ~CoreRuntime() override;
 void launch();
 void reattach();
 void stop();
 // Read-only historical RPC; no wallet mutation, miner input or guessed chain.
 // Account operations stay on the verified owned Core; the view never owns keys.
 void walletRequest(const QString& method,const QJsonArray& params,std::function<void(QJsonValue,QString)>);
 WalletManager* walletManager()const{return walletManager_;}
 void serviceWalletRequest(const QString&,const QJsonArray&,WalletReply);
 void importPrivateDescriptors(const QJsonArray&,bool scanHistory,std::function<void(QJsonValue,QString)>);
 QString networkGenesis()const{return profile_.genesisHash;}
 QString chainName()const{return profile_.chain;}
 QString walletBinding()const{return QString::number(pid_)+":"+processBirth_+":"+QString::number(identityGeneration_)+":"+walletManager_->displayWallet()+":"+QString::number(walletManager_->displayGeneration());}
 void auditRequest(const QString&,const QJsonArray&,std::function<void(QJsonValue,QString)>);
 void signAuditChallenge(const QString& address,const QString& challenge,std::function<void(QJsonValue,QString)>);
 void readLocalConnection(std::function<void(QJsonObject,QString)>);
 void readNetworkSnapshot(std::function<void(QJsonObject,QString)>);
 void readTicketPage(int offset,std::function<void(QJsonObject,QString)>);
 void readTicketDetails(const QString&,std::function<void(QJsonObject,QString)>);
 void readTicketAtlas(const QString& start,const QString& end,int bins,std::function<void(QJsonObject,QString)>);
 void readBlockAtHeight(int,std::function<void(QJsonObject,QString)>);
 void readBlockPage(int height,std::function<void(QJsonObject,QString)>);
 void readAuditDetails(const QString&,std::function<void(QJsonObject,QString)>);
 void readAuditSnapshot(int offset,std::function<void(QJsonObject,QString)>);
 void readParticipantPage(int offset,std::function<void(QJsonObject,QString)>);
 void readNetworkMetrics(int spanSeconds,std::function<void(QJsonObject,QString)>);
 void readBlockDetails(const QString&,std::function<void(QJsonObject,QString)>);
 void readTransactionDetails(const QString& txid,const QString& blockHash,std::function<void(QJsonObject,QString)>);
 void readTransactionPage(const QString& txid,const QString& blockHash,int offset,std::function<void(QJsonObject,QString)>);
 void readBlockTransactions(const QString& blockHash,int offset,std::function<void(QJsonObject,QString)>);
 void readDifficultyHeader(const ChainObservation& anchor,const QString& hash,std::function<void(QJsonObject,QString,QDateTime)>);
 qint64 processId() const {return pid_;}
 bool startPending()const{return launching_||orphanedStart_||verifier_.isRunning();}
 bool connected() const {return connected_;}
 bool walletReady()const{return walletReady_;}
 bool displayWalletReady()const{return connected_&&(walletManager_->isLoaded(walletManager_->displayWallet())||(walletReady_&&walletManager_->displayWallet()==profile_.walletName));}
 const QString& runtimeDirectory()const{return runtimeDirectory_;}
 std::function<void(const QString&)> onStatus;
 std::function<void(CoreLinkState,const QString&)> onLinkState;
 std::function<void(qint64)> onReady;
 std::function<void()> onStopped;
 std::function<void(const std::optional<ChainObservation>&)> onObservation;
private:
 void readWorkView(const QString&,int,int,const QString&,std::function<void(QJsonObject,QString)>);
 void networkHeaders(const CoreEpoch&,QJsonObject,const QString&,int,QJsonArray,std::function<void(QJsonObject,QString)>);
 void report(const QString&);
 void setLinkState(CoreLinkState,const QString& detail={});
 void inspect();
  void ensurePeerBootstrap();
 void ensureWallet(const CoreEpoch&);
 void verifyLoadedWallet(const CoreEpoch&);
 void walletInitialized(const CoreEpoch&);
 bool processAlive();
 void begin(bool allowNew);
 void failStart(const QString&);
 bool recoverOwned();
 bool persistOwned();
 void rpc(const QString&,const QJsonArray&,std::function<void(QJsonValue,QString)>,bool wallet=false,int timeoutMs=3000,const QString& walletName={});
 WalletManager* walletManager_{};
 QString root_,runtimeDirectory_;CoreProfile profile_;QNetworkAccessManager network_;
 qint64 pid_{};void* processHandle_{};
 std::unique_ptr<QLockFile> ownershipLock_;
 QString processBirth_;
  std::unique_ptr<PeerBootstrap> peerBootstrap_;
 quint64 identityGeneration_{};
 CoreEpoch epoch() const{return {pid_,processBirth_,identityGeneration_};}
 QTimer clock_;QFutureWatcher<QString> verifier_;
 CoreLinkState linkState_{CoreLinkState::Starting};
 QString linkDetail_;
 bool checking_{},connected_{},launching_{},stopping_{},allowNew_{},orphanedStart_{};
 bool walletChecking_{},walletReady_{};
 // UI/application network context is immutable; profiles cannot select it.
 const QString expectedChain_;
};
}
