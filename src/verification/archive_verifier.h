#pragma once
#include <QString>
#include <QtGlobal>
#include <atomic>
#include <cstdint>

namespace pgn::verification {
struct ArchiveVerificationPolicy {
    qint64 after{},through{};
    int limit{16};
    std::uint64_t oddInteriorBudget{10000};
};
struct ArchiveVerificationBatch {
    qint64 after{},through{};
    int processed{},evaluated{},cached{},verified{},rejected{};
    bool more{},cancelled{};
    QString error;
};
// Finite verification of already supplied pairs, never a CPU search/miner.
// Source is read-only. Results are independent append-only evidence, NOT
// coverage, ticket ownership, a public reservation or network acceptance.
ArchiveVerificationBatch verifyArchivePage(const QString& source,const QString& journal,
    const QString& validatorSha256,ArchiveVerificationPolicy policy={},
    const std::atomic_bool* cancelled=nullptr);
}
