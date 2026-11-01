#pragma once
#include "mining/cpu_fleet.h"
#include "mining/cpu_audit_journal.h"
#include "wallet/audit_event_publisher.h"
#include <QTimer>
#include <QElapsedTimer>
#include <QPointer>
namespace pgn::mining {
class CpuAuditBroker final:public QObject {
public:
    struct Services {
        pgn::wallet::AuditEventPublisher::Rpc rpc;
        pgn::wallet::AuditEventPublisher::Sign sign;
        std::function<void(QString,pgn::wallet::AuditEventPublisher::Reply)> identity;
        std::function<void(QString,std::function<void(QJsonObject,QString)>)> details;
        std::function<void(QString,std::function<void(QJsonObject,QString)>)> auditDetails;
        std::function<std::optional<pgn::wallet::ChainObservation>()> observation;
    };
    CpuAuditBroker(QString file,QString genesis,QString address,CpuFleet*,Services,QObject* parent=nullptr);
    std::function<void(QString)> onStatus;
    void pulse();
    void stop(){stopping_=true;timer_.stop();}
    void computed(const QString& device,const CpuAuditWork&,const QJsonObject& result);
private:
    QPointer<CpuFleet> fleet_;Services services_;CpuAuditJournal journal_;
    pgn::wallet::AuditEventPublisher publisher_;
    QString genesis_,address_,actor_,status_;pgn::wallet::CoreEpoch actorEpoch_;
    QTimer timer_;QElapsedTimer clock_;qint64 nextAssignment_{};quint64 round_{};bool busy_{},stopping_{};
    void note(QString);
    void published(QJsonObject,QString);
    void adopt(QJsonObject);
    void maintain(const QString& id,QJsonObject row);
    std::optional<CpuAuditWork> context(const QJsonObject&,const QJsonObject&,QString& error)const;
};
}
