#pragma once
#include <QString>
#include <QVector>
namespace pgn::science {
struct Observation {qint64 id{};QString p,q,gap,merit;QString originalUnixSeconds;};
struct ObservationPage {QVector<Observation> rows;qint64 through{},total{};bool more{};QString error;};
struct Origin {QString run,source;};
struct OriginPage {QVector<Origin> rows;bool more{};QString error;};
// One read-only worker connection per request. Fixed watermark + keyset pages
// conserve every identity without OFFSET scans, UI-sized full-history arrays,
// inferred event dates or proof promotion. No mining/wallet dependencies.
ObservationPage readObservations(const QString& database,qint64 after=0,qint64 through=0,
                                const QString& exactLeft={},int limit=100);
OriginPage readOrigins(const QString& database,qint64 gapId,const QString& afterRun={},
                       const QString& afterSource={},int limit=64);
}
