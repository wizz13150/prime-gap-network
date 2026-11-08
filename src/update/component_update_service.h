#pragma once
#include <QObject>
#include <QJsonObject>
#include <QPointer>
#include <QStringList>
#include <functional>
#include <optional>
class QProcess;
class QTimer;

namespace pgn::update {
struct ComponentUpdateDeployment {
    QString helper;
    QJsonObject context;
};
ComponentUpdateDeployment componentUpdateDeployment(const QString& applicationDirectory,bool testnet);
QJsonObject componentUpdateRequest(const ComponentUpdateDeployment&,const QJsonObject& installedSnapshot,
    const QString& applicationDirectory,const QString& mutableDataRoot,const QString& action,const QStringList& selected);
std::optional<int> componentVersionComparison(QString installed,QString available);
bool componentUpdateNeedsRecovery(const QString& genesis);
bool validComponentUpdateProgress(const QJsonObject&);
QString componentUpdateFailureMessage(const QString& detail);
QJsonObject currentUpdateActivation(const QString& applicationRoot,const QString& mutableDataRoot);
QString startDetachedComponentInstall(const ComponentUpdateDeployment&,const QJsonObject& request);

// One bounded helper at a time. This class never controls a mining process or
// interprets downloaded data as a command. Installed helper identity is checked
// before launch; the helper performs TUF verification itself.
class ComponentUpdateService final : public QObject {
public:
    explicit ComponentUpdateService(ComponentUpdateDeployment,QObject* parent=nullptr);
    ~ComponentUpdateService() override;
    bool start(const QJsonObject& request);
    void cancel();
    bool busy() const;
    std::function<void(const QString&)> onStatus;
    std::function<void(const QJsonObject&)> onResult;
    std::function<void(const QString&)> onError;
    std::function<void(const QJsonObject&)> onProgress;
private:
    void complete();
    ComponentUpdateDeployment deployment_;
    QProcess* process_{};
    QTimer* timer_{};
    QByteArray output_,errors_,progressBuffer_;
    QString action_;
    QMetaObject::Connection startedConnection_;
    bool settled_{true},cancelled_{},oversized_{},timedOut_{};
};
}
