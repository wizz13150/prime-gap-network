#pragma once
#include <QHash>
#include <QSet>
#include <QString>
#include <QVector>
#include <atomic>

namespace pgn::science {
struct RecordReference {
    QHash<quint32,QString> starts;
    QSet<quint32> beyondDomain;
    QString observedUtc,sourceUrl,sourceBlob,error;
    quint32 minimumPotentialGap{};
    bool valid()const{return error.isEmpty()&&!starts.isEmpty()&&minimumPotentialGap>0;}
};
RecordReference readRecordReference(const QString&);
bool improvesRecordReference(const RecordReference&,quint32 gap,const QString& lower);
struct SubmissionRecord {quint32 gap{};QString lower,upper,merit;};
struct RecordSubmissions {
    QVector<SubmissionRecord> records;
    QString text,error,referenceUtc,referenceUrl;
    qint64 revision{},examined{},rejected{},pending{};
    bool cancelled{};
};
// READONLY archive comparison and independent exact pair verification. No
// network submission, wallet operation, mining or whole-range certification.
RecordSubmissions collectRecordSubmissionsFromPairs(const QVector<SubmissionRecord>&,const QString& reference,const std::atomic_bool* cancelled=nullptr);
RecordSubmissions collectRecordSubmissions(const QString& database,const QString& reference,
    const std::atomic_bool* cancelled=nullptr);
}
