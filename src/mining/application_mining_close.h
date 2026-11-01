#pragma once
#include <QObject>
#include <QString>
#include <functional>
#include <memory>

class QWidget;
class QCoreApplication;
namespace pgn::mining {
class NativeBridge;
// In-process close barrier for the ONE application. No implicit restart/cancel,
// no second queue, no physical/durability claim from a transport ACK.
class ApplicationMiningClose final : public QObject {
public:
    ApplicationMiningClose(QWidget* window,QCoreApplication* application,NativeBridge* bridge,
                           QString bookingDatabase,QString constantCheckpointLabel,QObject* parent=nullptr,bool finiteManaged=true);
    ~ApplicationMiningClose() override;
    // Explicit retry when Held; original target and admission freeze survive.
    // Each attempt has <=4 correlated inventories and a45s UI budget. Only
    // admitted native RAW-lineage phases suspend it; transport/TTL do not.
    void requestClose();
    // Developer-owned UI replacement ONLY: drain observers while leaving the
    // physical worker/STOP latch untouched. NOT normal Close, checkpoint or
    // ReadyToQuit. Refuses if normal close has already begun or no drain hook.
    bool requestMaintenanceDetach();
    bool readyToQuit()const;
    QString status()const;
    std::function<void(const QString&)> onStatus;
    // Normal Close invokes this AFTER physical/storage gates pass. Developer
    // maintenance invokes it separately, without any physical readiness claim.
    // The caller must freeze future UI actuation before maintenance drain.
    // The unified runtime
    // drains its own SOURCE worker/clients before calling done. Until then,
    // Close/Quit remain intercepted. Duplicate/late completions are harmless.
    std::function<void(std::function<void()>)> onDrainBeforeQuit;
protected:
    bool eventFilter(QObject*,QEvent*) override;
private:
    class Impl;
    std::unique_ptr<Impl> d_;
};
}
