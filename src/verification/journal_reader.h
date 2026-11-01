#pragma once
#include <QDateTime>
#include <QStringList>
#include <optional>
#include "verification/bounded_gap_verifier.h"

namespace pgn::verification {
struct JournalReceipt {
    qint64 id{},sourceId{},checkedInterior{},elapsedNs{},oddBudget{};
    quint64 verifiedGapSize{}; // Defined only for VerifiedWithinBound.
    QString sourceArchive,lower,upper,validatorSha256;
    GapVerdict verdict{GapVerdict::InvalidInput};
    QDateTime originalCheckedUtc;
};
struct LatestReceipt {std::optional<JournalReceipt> receipt;QString error;};
// One indexed last-row read, never a full archive scan or a source mutation.
// Reads a local qualified-validator receipt; does NOT reverify mathematics,
// authorize network credit or establish coverage of unrecorded intervals.
LatestReceipt readLatestReceipt(const QString& file,const QStringList& qualifiedValidators);
}
