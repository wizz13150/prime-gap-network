#pragma once
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QJsonObject>
#include <QJsonArray>
#include <QHash>
#include <QElapsedTimer>
#include <QList>
#include <functional>
namespace pgn::mining {
// Local or explicitly configured transport. Inventory never opens work.
struct CpuControlEndpoint {QString program;QStringList arguments;QString deviceId,host;};
class CpuControllerClient final:public QObject {
public:
    explicit CpuControllerClient(CpuControlEndpoint,QObject* parent=nullptr);
    void start();
    bool openAudit(const QJsonObject& authenticatedRequest);
    qint64 configure(bool enabled,int workers);
    bool stopped()const{return process_.state()==QProcess::NotRunning;}
    bool available()const{return ready_&&healthy_&&process_.state()==QProcess::Running&&!closing_;}
    bool requestAuditResult(const QString& requestSha256);
    bool acknowledgeAuditResult(const QString& requestSha256);
    bool discardAudit(const QString& requestSha256);
    void pause();
    void close();
    QJsonArray affinityFor(int workers)const;
    const QJsonObject& inventory()const{return inventory_;}
    std::function<void(QJsonObject)> onInventory,onStatus,onAuditResult;
    std::function<void(QString)> onError;
    std::function<void()> onClosed;
private:
    struct Pending {QString action;qint64 started;QString expectedDigest;bool timedOut{};};
    CpuControlEndpoint endpoint_;QProcess process_;QTimer timer_;QElapsedTimer clock_;QByteArray buffer_;QHash<qint64,Pending> pending_;
    QJsonObject inventory_;qint64 sequence_{},generation_{};bool ready_{},healthy_{},closing_{};
    QList<qint64> retiredStatus_;QString terminalDiagnostic_;
    qint64 send(QString,QJsonObject fields={});
    void receive();void fail(const QString&);
};
}
