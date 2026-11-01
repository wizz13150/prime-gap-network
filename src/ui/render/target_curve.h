#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QVector>
#include <QtGlobal>

inline constexpr char FRONTIER_TARGET_CURVE_SCHEMA[] =
    "gapcoin-frontier-target-curve-v1";
inline constexpr char FRONTIER_TARGET_CURVE_RPC_SOURCE[] =
    "getblockhash+getblockheader";
inline constexpr char FRONTIER_TARGET_CURVE_CHAIN[] = "test";
inline constexpr char FRONTIER_TARGET_CURVE_GENESIS[] =
    "44bbd1908c09ebeb2c420698d8443f75ead2b51d642b02efe2d82d8e32d22db6";
inline constexpr char FRONTIER_TARGET_CURVE_WORK_TYPE[] =
    "frontier-hash-window-v0";
inline constexpr quint64 FRONTIER_TARGET_CURVE_Q48 = 281474976710656ULL;
inline constexpr qint64 FRONTIER_TARGET_CURVE_MAX_AGE_MS = 30'000;

struct FrontierTargetCurvePoint {
    qint64 height{0};
    QString blockHash;
    qint64 blockTime{0};
    qint64 confirmations{0};
    quint64 requiredMeritQ48{0};
    double targetMerit{0.0};
};

struct FrontierTargetCurveSnapshot {
    QDateTime observedUtc;
    QVector<FrontierTargetCurvePoint> points;

    // Returns the point with the greatest block_time at or before unixSeconds.
    // Equal timestamps are resolved to the greatest (latest) height.
    const FrontierTargetCurvePoint* lookupAtOrBefore(qint64 unixSeconds) const noexcept;
    const FrontierTargetCurvePoint* lookupAtOrBefore(const QDateTime& timestamp) const noexcept;
};

struct FrontierTargetCurveLoadResult {
    bool accepted{false};
    FrontierTargetCurveSnapshot snapshot;
    QString error;

    explicit operator bool() const noexcept { return accepted; }
};

FrontierTargetCurveLoadResult FrontierParseTargetCurveSnapshot(
    const QByteArray& json,
    const QDateTime& nowUtc = QDateTime::currentDateTimeUtc());

FrontierTargetCurveLoadResult FrontierLoadTargetCurveSnapshot(
    const QString& path,
    const QDateTime& nowUtc = QDateTime::currentDateTimeUtc());
