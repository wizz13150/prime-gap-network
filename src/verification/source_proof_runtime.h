#pragma once
#include "verification/source_proof_page.h"
#include <QObject>
#include <QJsonObject>
#include <functional>
#include <memory>

namespace pgn::verification {
// ONE private in-process worker thread. Event-driven bounded pages, no timer
// polling, process, GPU/control command, network, UI pointer or mutable queue
// of source records. Coalescing can discard a notification, NOT archived work:
// indexed persistent source identities+cursors recover deferred runs.
class SourceProofRuntime final:public QObject {
public:
    SourceProofRuntime(QString source,QString journal,SourceProofPolicy,QObject* parent=nullptr);
    ~SourceProofRuntime() override;
    void observeNativeIndex(const QJsonObject&);
    void setEnabled(bool); // Pause/resume the optional CPU audit; never physical mining control.
    void retry(); // Explicit local proof error retry; no physical control.
    void stop(); // Cooperative cancellation, onStopped only after worker drain.
    bool stopped()const;
    bool held()const;
    std::function<void(const SourceProofPage&)> onPage;
    std::function<void()> onStopped;
private:
    class Impl;
    std::unique_ptr<Impl> d_;
};
} // namespace pgn::verification
