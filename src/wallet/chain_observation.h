#pragma once
#include "wallet/core_profile.h"
#include "wallet/core_rpc.h"
#include <QDateTime>
#include <QRegularExpression>
#include <cmath>
#include <optional>

namespace pgn::wallet {
// Read-only wallet->presentation boundary. No GPU/miner types or calls.
// Numbers are observations of the verified owned Core, never engine fields,
// fallback defaults, reward rules or inferred network throughput.
struct ChainObservation {
    CoreEpoch owner;
    QString genesis;
    qint64 height{};
    QDateTime observedUtc;
    std::optional<bool> synchronizing;
    QString tipHash;
    std::optional<qint64> peers;
    bool valid()const {
        static const QRegularExpression hash("^[0-9a-fA-F]{64}$");
        return owner.matches(owner)&&height>=0&&height<=9007199254740991LL&&
            observedUtc.isValid()&&hash.match(genesis).hasMatch()&&
            (tipHash.isEmpty()||hash.match(tipHash).hasMatch());
    }
    bool freshAt(const QDateTime& now,qint64 maximumAgeMs=30000)const {
        if(!valid()||!now.isValid()||maximumAgeMs<0)return false;
        const auto age=observedUtc.msecsTo(now);return age>=0&&age<=maximumAgeMs;
    }
};
struct ChainObservationResult {std::optional<ChainObservation> observation;QString error;};
inline ChainObservationResult projectChainObservation(const CoreEpoch& owner,const CoreProfile& profile,
    const QJsonObject& chain,const QString& genesis,const QDateTime& observedUtc){
    static const QRegularExpression hash("^[0-9a-fA-F]{64}$");
    if(!owner.matches(owner)||!observedUtc.isValid()||!hash.match(profile.genesisHash).hasMatch())
        return {{},"Core observation lacks a pinned chain, owned epoch or original observation time"};
    const auto mismatch=verifyCoreIdentity(profile,chain,genesis);
    if(!mismatch.isEmpty())return {{},mismatch};
    const auto height=chain.value("blocks");const double number=height.toDouble(-1);
    if(!height.isDouble()||!std::isfinite(number)||number<0||number>9007199254740991.0||std::trunc(number)!=number)
        return {{},"Core block height missing, invalid or outside exact JSON integer domain"};
    const auto sync=chain.value("initialblockdownload");
    const auto tip=chain.value("bestblockhash");
    if(!tip.isUndefined()&&(!tip.isString()||!hash.match(tip.toString()).hasMatch()))return {{},"Core tip hash is invalid"};
    return {ChainObservation{owner,genesis.toLower(),height.toInteger(),observedUtc.toUTC(),
        sync.isBool()?std::optional<bool>(sync.toBool()):std::nullopt,tip.toString().toLower()},{}};
}
}
