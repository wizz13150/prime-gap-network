#pragma once
#include <QByteArray>
#include <QString>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace pgn::mining {
class SourceMirrorReadFreeze;
class SourceMirrorFrozenFile;
struct SourceMirrorState {
    qint64 size{};
    // Local to ONE SourceMirror instance, not a durable file identity. A new
    // handle after eviction gets a new token even if the filename/size match.
    std::uint64_t handle_epoch{};
};
// Windows source-byte conservation, not scientific validation. One caller on
// one thread; the directory must already exist. No service or source process.
class SourceMirror final {
public:
    static constexpr qsizetype BatchLimit=65536;
    static constexpr std::size_t HandleLimit=8;
    explicit SourceMirror(const QString& directory);
    ~SourceMirror();
    SourceMirror(const SourceMirror&)=delete;
    SourceMirror& operator=(const SourceMirror&)=delete;
    QString file(const QString& run) const;
    // Missing is distinct from an existing empty file; never creates either.
    std::optional<qint64> size(const QString& run);
    std::optional<SourceMirrorState> inspect(const QString& run);
    // Bounded prefix verification through the held handle, never a path reopen.
    // A session must reverify its enrolled prefix if handle_epoch changes.
    QByteArray read(const QString& run,qint64 offset,qsizetype maximum,
                    std::uint64_t expectedHandleEpoch);
    // Compare ALL overlap before writing only its suffix. Successful return
    // means FlushFileBuffers succeeded, or the same exclusive handle already
    // established durability and this packet changed no bytes. No sparse gap.
    // I/O failure can leave a partial appended suffix; never rolls back or
    // overwrites the conserved prefix. Identical retry repairs that suffix.
    void append(const QString& run,qint64 offset,const QByteArray& bytes,
                std::optional<std::uint64_t> expectedHandleEpoch={});
    // Acquire on the creating/ingestion thread, then hand the lease to ONE
    // owned worker. All mirror I/O/LRU mutation refuses until the last lease
    // or reader is destroyed. Rejected transport chunks MUST be replayed.
    // Does not flush, create, adopt or scan any source. Only one freeze.
    std::shared_ptr<SourceMirrorReadFreeze> freezeForLineage();
    bool lineageFrozen() const;
private:
    friend class SourceMirrorReadFreeze;
    friend class SourceMirrorFrozenFile;
    class Impl;
    std::shared_ptr<Impl> impl_;
};

// Lifetime-owned read capability, NOT a physical/durability attestation. No
// raw handle escapes. Opens never touch the ingestion LRU, even for history.
class SourceMirrorReadFreeze final:public std::enable_shared_from_this<SourceMirrorReadFreeze> {
public:
    ~SourceMirrorReadFreeze();
    SourceMirrorReadFreeze(const SourceMirrorReadFreeze&)=delete;
    SourceMirrorReadFreeze& operator=(const SourceMirrorReadFreeze&)=delete;
    std::unique_ptr<SourceMirrorFrozenFile> open(const QString& run);
private:
    friend class SourceMirror;
    friend class SourceMirrorFrozenFile;
    class Impl;
    explicit SourceMirrorReadFreeze(std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl_;
};
class SourceMirrorFrozenFile final {
public:
    ~SourceMirrorFrozenFile();
    SourceMirrorFrozenFile(const SourceMirrorFrozenFile&)=delete;
    SourceMirrorFrozenFile& operator=(const SourceMirrorFrozenFile&)=delete;
    qint64 size() const;
    QByteArray read(qint64 offset,qsizetype maximum);
    // Non-closing validation for an explicit audit's PRE-COMMIT boundary.
    // Keep this reader alive through COMMIT; external WRITE/DELETE stays denied.
    void verifyStable() const;
    // Closes EVEN when stability checks throw. Destructor also always closes.
    void closeAndVerifyStable();
private:
    friend class SourceMirrorReadFreeze;
    class Impl;
    explicit SourceMirrorFrozenFile(std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl_;
};
}
