#pragma once
#include "social/social_data.h"
#include <QByteArray>
#include <QJsonArray>
#include <QJsonValue>
#include <QObject>
#include <memory>

namespace pgn::social {
// Immutable, verified public wallet binding supplied by root. No credential,
// private key, role grant or mining/audit signer is stored here.
struct CoreSocialContext {
    Identity identity;
    QString address,device,channel{"global"},networkLabel{"PGN network"};
};
enum class CoreSocialCall { SubmitEvent, ReadEvents, ReadPeers };
using CoreSocialRpcReply=std::function<void(QJsonValue,QString)>;
using CoreSocialRpc=std::function<void(CoreSocialCall,QJsonArray,CoreSocialRpcReply)>;
struct CoreSocialCrypto {
    using Signed=std::function<void(QString,QString)>;
    using Verified=std::function<void(bool,QString)>;
    std::function<void(QByteArray,Signed)> sign;
    std::function<void(QString,QByteArray,QString,Verified)> verify;
};
// Root owns implementation +Main/Wallet bridge; Social45 owns native Core14.
// Callbacks complete exactly once on GUI thread. Destroy/recreate on wallet
// epoch/genesis/script change; old callbacks must be discarded via weak owner.
// RPC args: SubmitEvent{canonicalUTF8String,signature},
// ReadEvents{generation,cursor,limit<=50}, ReadPeers{}.
class CoreSocialService final:public QObject {
public:
    CoreSocialService(CoreSocialContext,CoreSocialCrypto,CoreSocialRpc,
                      QString outboxPath,QObject* parent=nullptr);
    ~CoreSocialService() override;
    void start();
    void stop();
    const Identity& identity() const;
    Services services();
    std::function<void()> changed;
    std::function<void(QString)> diagnostic;
    // Optional authenticated current-branch Core snapshot provider. Roster is
    // historical, NOT online presence/workers. Missing values stay unavailable.
    std::function<void(ReadRequest,Services::Reply)> leaderboard;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace pgn::social
