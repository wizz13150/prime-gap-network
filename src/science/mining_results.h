#pragma once
#include <QString>
#include <QStringList>
#include <QVector>
#include <QMap>
#include <atomic>
#include <optional>
namespace pgn::science {
struct MiningResult {qint64 id{};quint32 gap{};QString lower,upper,merit;};
struct MiningResults {
    QVector<MiningResult> best,recent;
    QMap<quint32,qint64> gapSizes;
    QMap<int,qint64> meritBands;
    qint64 points{},atOrAboveTarget{},revision{},rowsRead{};
    QString error;bool cancelled{};
};
// Read-only coherent snapshot. Empty runs means an explicitly selected whole
// local archive; a ticket with no known runs must never use that fallback.
MiningResults readMiningResults(const QString& database,const QStringList& runs,
    bool wholeArchive,int rows,std::optional<double> target,const std::atomic_bool* cancelled=nullptr);
QStringList ticketSourceRuns(const QString& bookingDatabase,const QStringList& requestIds,
    const QString& genesis);
}
