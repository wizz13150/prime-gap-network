#pragma once
#include <certified_gap_builder.h>
#include <certified_gap_encoding.h>
#include <QString>
#include <QByteArray>
#include <atomic>
#include <optional>

namespace pgn::verification {
struct SourceProofPolicy {
    int pageSize;
    pgn::core::CertifiedGapLimits scientific;
    pgn::core::EncodingLimits encoding;
};
struct SourceProofPage {
    QString run,identitySha256,policySha256,error;
    qint64 after{},through{},total{},verifiedTotal{},rejectedTotal{};
    int processed{},verified{},rejected{};
    bool more{},cancelled{};
};
// Finite, bounded-memory local proof production for ALREADY indexed supplied
// pairs of ONE declared native source. Source connection is READONLY. Cursor
// and each full independent certificate commit atomically in a SEPARATE local
// journal. No source edit, endpoint search, CPU mining, lease/coverage/credit,
// signing, submission or Core chain acceptance. No defaults are consensus.
// Multiple page calls resume the durable cursor; no historical OFFSET scan.
SourceProofPage certifyNativeSourcePage(const QString& source,const QString& journal,
    const QString& run,SourceProofPolicy,const std::atomic_bool* cancelled=nullptr);
struct SourceProofRead {
    QString run,identitySha256,policySha256,canonicalSha256,error;
    qint64 gapId{};
    QByteArray canonical;
    std::optional<pgn::core::CertifiedGap> evidence;
    bool cancelled{};
};
// READONLY point retrieval for the native consumer: declared source origin,
// journal/cursor identity, explicit budgets, bounded zlib stream, digest,
// canonical roundtrip and independent mathematical verification are ALL paid
// again. A stored positive flag or checksum alone is never authority. No
// database creation/repair, range coverage, signing or chain credit.
SourceProofRead readCertifiedNativeSourceProof(const QString& source,const QString& journal,
    const QString& run,qint64 gapId,SourceProofPolicy,const std::atomic_bool* cancelled=nullptr);
struct SourceProofScan {
    QString run,afterRun,error;
    bool more{},cancelled{};
};
// Metadata keyset scan: at most64 declared source identities and indexed
// watermark probes, NOT gaps/whole history. Recovers work omitted by coalesced
// UI events or a restart; cursors remain authoritative in the proof journal.
SourceProofScan scanNativeSourceProofWork(const QString& source,const QString& journal,
    SourceProofPolicy,const QString& afterRun={},const std::atomic_bool* cancelled=nullptr);
} // namespace pgn::verification
