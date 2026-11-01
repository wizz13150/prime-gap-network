#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <functional>
#include <memory>

namespace pgn::mining {
// An adapter MUST own a regular, non-reparse source HANDLE for this lifetime.
// size/read refer to that same handle, not a path reopen. closeAndVerifyStable
// compares the initial/final file incarnation, path identity, size and change
// pin, then releases it; it throws on any disagreement. The destructor MUST
// release even after a failed read/verification. These are trusted local I/O
// obligations, NOT cryptographic authentication or remote/power-cut durability.
class HeldSourceLineageReader {
public:
    virtual ~HeldSourceLineageReader()=default;
    virtual QString run() const=0;
    virtual QString source() const=0;
    virtual bool isRegularFile() const=0;
    virtual bool isSymbolicLink() const=0;
    virtual qint64 size() const=0;
    virtual QByteArray read(qint64 offset,qsizetype maximum)=0;
    virtual void closeAndVerifyStable()=0;
};
using OpenHeldLineageSource=std::function<std::unique_ptr<HeldSourceLineageReader>(const QString& run,const QString& source)>;

// Synchronous, read-only logical proof producer. Opens EXISTING booking/history
// SQLite read-only and holds separate read transactions (NOT cross-DB atomic).
// Every actual immutable manifest/epoch and every raw committed journal is
// checked against native_source_batches digests AND native_source_identities;
// legacy unenrolled => HeldNonEnrolled (not unreadable history or adoption).
// O(total raw bytes + epochs + committed batches), indexed metadata seeks,
// 64KiB reads,32KiB rows,O(epochs) IDs;
// no whole-journal buffer, arbitrary ancestry quota, cache, allocation or timer.
// Run on an explicitly owned worker at close/resume boundaries, NOT UI polls.
// verify() never supplies physical freshness, checkpoint load, independent
// primality/coverage, credit, or control/quit permission. A concrete held-handle
// adapter must be qualified before this producer is integrated as Ready.
class SourceResumeLineageVerifier final {
public:
    SourceResumeLineageVerifier(QString bookingDatabase,QString historyDatabase,OpenHeldLineageSource,
                               std::function<bool()> cancelled={});
    QJsonObject verify(const QJsonObject& snapshot) const noexcept;
private:
    QString bookingDatabase_,historyDatabase_;
    OpenHeldLineageSource open_;
    std::function<bool()> cancelled_;
};
}
