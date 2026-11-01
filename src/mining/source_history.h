#pragma once
#include <QByteArray>
#include <QString>
#include <QJsonObject>
#include <functional>
#include <memory>
#include <optional>

namespace pgn::mining {
struct SourceIndexSnapshot {QJsonObject publication;qint64 committedOffset{},dataVersion{};};
// Display-only reader. Opens an EXISTING database READONLY: no writer ctor,
// migration, IMMEDIATE transaction or metadata-based replacement for audits.
class SourceHistoryIndexReader final {
public:
    explicit SourceHistoryIndexReader(const QString& database);
    ~SourceHistoryIndexReader();
    SourceIndexSnapshot read(const QString& run,qint64 committedOffset,const std::function<bool()>& cancelled={});
    // Display-only ALL archive. No remote run/cursor or SOURCE readiness is
    // inferred from retained local observations, including undated rows.
    QJsonObject readArchive(const std::function<bool()>& cancelled={});
private:
    class Impl;std::unique_ptr<Impl> impl_;
};
struct SourceImport {
    qint64 offset{}, lines{}, admitted{}, inserted{};
};
struct SourcePrefix { qint64 offset{},dataVersion{}; };
struct SourceCursorRecord {qint64 offset{},dataVersion{};};
// Bounded local source-display cache. The retired scientific-history.sqlite
// filename is refused. Writable stores have a24MiB main-file ceiling and
// guarded WAL admission; a pinned reader cannot cause unbounded journal growth.
// Source PRP
// observations are not independent primality/coverage certificates. One owner
// calls this object on one thread; SQLite still arbitrates other connections.
class SourceHistory final {
public:
    explicit SourceHistory(const QString& database);
    ~SourceHistory();
    SourceHistory(const SourceHistory&) = delete;
    SourceHistory& operator=(const SourceHistory&) = delete;
    qint64 cursor(const QString& run) const;
    // Metadata-only read snapshot; no repair/admission/cache population.
    // Missing is distinct from a genuinely stored INTEGER0. A commit from
    // another SQLite connection during the read refuses instead of returning
    // a cursor paired with a stale prefix-verification token. No raw scan.
    std::optional<SourceCursorRecord> cursorRecord(const QString& run) const;
    // Durable declared identity, not authentication. Never relabel a legacy
    // nonzero cursor; callers also disallow creation over a preexisting raw
    // mirror. Existing bindings must match byte-for-byte after restart.
    void bindSourceIdentity(const QString& run,const QString& identity,bool allowCreate);
    // One coherent committed index snapshot for the existing viewer protocol.
    // First use/external commits audit cardinalities (linear startup I/O);
    // this owner's verified appends reuse that base, not a full recount/frame.
    // "complete" concerns this display index, NEVER scientific coverage/proof.
    QJsonObject indexPublication(const QString& run);
    // One incarnation check through the caller's already-held exclusive file
    // handle. Reader returns at most requested bytes, never more than65536.
    // A read snapshot binds cursor and integrity metadata; no source admission.
    SourcePrefix verifySourcePrefix(const QString& run,qint64 sourceSize,
        const std::function<QByteArray(qint64,qsizetype)>& reader) const;
    // Explicit legacy migration, NEVER an automatic fallback. expectedSha256
    // must come from the caller's independently qualified source receipt. This
    // validates both directions of the stored origin/observed GAP relation and
    // enrolls only digests, atomically; no GAP, origin, time or plot is changed.
    // Already/partly enrolled sources are held, not overwritten on retry.
    void enrollSourcePrefix(const QString& run,qint64 committedOffset,
        const QByteArray& expectedSha256,const std::function<QByteArray(qint64,qsizetype)>& reader);
    // EXPLICIT offline legacy audit, never called by Runtime. Opens an existing
    // database WITHOUT CREATE/general migrations. Digests + declared identity
    // share ONE transaction with the complete prefix/origin audit. Existing or
    // partial enrollment refuses. verifyHeldStable must validate without closing
    // the exclusive source reader: caller retains handle/barrier through COMMIT.
    // Failure rolls back both schema and data; this does not authenticate input.
    static void enrollLegacySourceExisting(const QString& database,const QString& run,
        const QString& device,const QJsonObject& actualManifest,qint64 committedOffset,
        const QByteArray& expectedPrefixSha256,
        const std::function<QByteArray(qint64,qsizetype)>& heldReader,
        const std::function<void()>& verifyHeldStable);
    // Complete source bytes and their cursor commit in ONE FULL-synchronous transaction.
    // Replays from a stale offset fail rather than silently skipping any bytes.
    SourceImport append(const QString& run, qint64 offset, const QByteArray& bytes,
        std::optional<qint64> verifiedDataVersion={});
    // Offline/non-concurrently-mutated journal: captures EOF once; verifies the
    // entire previously committed prefix against transactional batch digests,
    // with O(offset) read cost and bounded memory. Leaves partial EOF unconsumed.
    // Legacy nonzero cursors without native digests require a separate audit;
    // they are NEVER silently enrolled from whichever bytes happen to be here.
    // Does not truncate, rename, lock for writing, or retime the source journal.
    SourceImport importJournal(const QString& run, const QString& journal);
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}
