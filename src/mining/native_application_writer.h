#pragma once
#include <QObject>
#include <QString>
#include <functional>
#include <memory>

namespace pgn::mining {
enum class NativeApplicationOperation { Read, Mutation, Stop };
struct NativeApplicationWriteResult {
    bool entered{}; // operation actually entered, NOT remote application
    QString error;
};
// The ONE in-process application control writer. It never launches, retries,
// cancels or certifies a remote operation. Work owns its transport deadline;
// completion settles the local attempt and retains its durable handoff. A PS
// reply does NOT settle physical launch/stop: later correlated inventory and
// pending-state checks still control reconciliation and permission to quit.
// Freeze is synchronous; queued mutations are rechecked before entry, while
// an already entered operation keeps its result path. SOURCE ingestion is
// separately owned and is not silently dropped or frozen by this barrier.
class NativeApplicationWriter final : public QObject {
public:
    using Done=std::function<void(QString)>;
    using Work=std::function<void(Done)>;
    using Completion=std::function<void(const NativeApplicationWriteResult&)>;
    static constexpr qsizetype PendingLimit=256; // explicit command refusal, NOT a history limit
    explicit NativeApplicationWriter(QObject* parent=nullptr);
    ~NativeApplicationWriter() override;
    bool enqueue(NativeApplicationOperation,Work,Completion);
    // The canonical request retains its latch. Duplicate pending registration
    // is refused; after completion it receives the same local-barrier result.
    // A different request cannot replace the close. Accepted is NOT quit.
    bool prepareClose(const QString& requestId,Completion);
    bool frozen()const;
    QString closeRequestId()const;
private:
    class Impl;
    std::unique_ptr<Impl> d_;
};
}
