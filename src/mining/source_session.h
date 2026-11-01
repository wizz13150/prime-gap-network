#pragma once
#include "mining/source_history.h"
#include "mining/source_mirror.h"
#include <QList>

namespace pgn::mining {
struct SourceChunk {
    bool needsReplay{};
    qint64 nextOffset{},committedOffset{};
    QList<QByteArray> displayLines; // last512 only; ALL raw/scientific data persist
};
struct SourceConservation {
    QString run,error;
    bool ready{};
    std::optional<qint64> requiredEnd,committedOffset,mirrorSize,transportOffset;
    QJsonObject json() const;
};
// Byte-conservation/admission seam for the native transport. No SSH, GPU, clocks,
// wallet or control action. A single owner calls all methods on one thread.
class SourceSession final {
public:
    SourceSession(SourceHistory& history,SourceMirror& mirror):history_(history),mirror_(mirror){}
    SourceChunk consume(const QString& run,qint64 offset,qint64 nextOffset,const QByteArray& bytes,
                        const std::function<bool()>& cancelled={});
    // READ ONLY, never attach/flush/recover/append/reverify a raw prefix. Uses
    // only this session's already paid proof under its SAME held incarnation.
    // nextOffset/pendingBytes must be exact observed integers, not defaults.
    // Ready is logical conservation at that observation boundary, not EOF,
    // stopped ownership, scientific coverage or global power-cut durability.
    SourceConservation conservation(const QString& run,qint64 nextOffset,qint64 pendingBytes) const;
private:
    void attach(const QString& run,const SourceMirrorState& state,const std::function<bool()>& cancelled);
    SourceHistory& history_;SourceMirror& mirror_;
    QString run_;std::uint64_t handleEpoch_{};
    qint64 expected_{},committed_{},integrityVersion_{-1};QByteArray fragment_;
};
}
