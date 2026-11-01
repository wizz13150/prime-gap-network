#pragma once
#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <functional>
#include <memory>
#include <atomic>
namespace pgn::wallet {
struct AuditOperation {QString action,address,request,body;int slot{-1};qint64 attempt{-1};};
// RPC/signing callbacks must remain bound to the verified owned Core/wallet.
// One durable publication is retained until its consumer acknowledges it.
class AuditEventPublisher final:public QObject {
public:
    using Reply=std::function<void(QJsonValue,QString)>;
    using Rpc=std::function<void(QString,QJsonArray,Reply)>;
    using Sign=std::function<void(QString,QString,Reply)>;
    using Completion=std::function<void(QJsonObject,QString)>;
    using ProposalGate=std::function<bool(const QJsonObject&)>;
    AuditEventPublisher(QString journal,QString genesis,Rpc,Sign,QObject* parent=nullptr);
    ~AuditEventPublisher()override;
    bool busy()const{return busy_;}
    bool pending()const;
    void publish(AuditOperation,Completion,ProposalGate={});
    void recover(Completion);
    bool acknowledge(const QString& blockHash);
private:
    QString file_,genesis_;Rpc rpc_;Sign sign_;Completion completion_;
    bool busy_{};QJsonObject intent_;
    std::shared_ptr<std::atomic_bool> cancelled_=std::make_shared<std::atomic_bool>(false);
    void fail(const QString&);
    bool persist();
    void inspectPublished(bool allowExactRetry=false);
    void retryRetained();
    void build(const QJsonObject& proposal,const QString& envelope);
};
}
