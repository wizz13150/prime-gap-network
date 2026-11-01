#pragma once
#include <QDateTime>
#include <QVector>
#include <QString>

namespace pgn::booking {
struct SourceRange {qint64 row{};QString start,end,status,owner;};
struct SourceRegistry {
    QVector<SourceRange> ranges;
    QString start,end,url,sha256,error;
    QDateTime observedUtc;
    bool localValidation{false}; // explicit test authority, never public availability/network acceptance
    bool valid() const {return error.isEmpty()&&!ranges.isEmpty();}
    const SourceRange* declaredOpenContaining(const QString& start,const QString& end) const;
};
SourceRegistry loadSourceRegistry(const QString& runtimeDirectory);
// Read-only owner lookup combines the fresh historical snapshot with the
// published development copy. It is never used as mining/lease authority.
SourceRegistry loadReservationDirectory(const QString& runtimeDirectory,const SourceRegistry& historical);
}
