#pragma once
#include <QObject>
#include <QString>
#include <functional>
#include <memory>

namespace pgn::mining {
class NativeBridge;
// Explicit application starts only. A fresh inventory is not a launch token:
// native SAME-SQLite admission must commit before one prepared control frame.
// Only correlated RAW-lineage work may suspend the admission UI budget.
// After physical submission that budget is diagnostic: retain the SAME
// uncertain request until exact outcome, explicit cancellation or owner loss.
// Transport deadlines and observation/proof freshness are never renewed here.
class ApplicationMiningStart final : public QObject {
public:
    ApplicationMiningStart(NativeBridge*,QString bookingDatabase,QString runtimeDirectory,QObject* parent=nullptr,QString checkpointLabel={});
    ~ApplicationMiningStart() override;
    void control(const QString& device,const QString& requestId,bool enabled);
    QString status()const;
    std::function<void(const QString&)> onStatus;
private:
    class Impl;
    std::unique_ptr<Impl> d_;
};
}
