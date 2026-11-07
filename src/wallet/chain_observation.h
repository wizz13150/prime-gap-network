#pragma once
#include "wallet/core_profile.h"
#include "wallet/core_rpc.h"
#include <QDateTime>
#include <QRegularExpression>
#include <QJsonArray>
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
    std::optional<qint64> headerHeight;
    std::optional<bool> coreInitialBlockDownload;
    std::optional<qint64> tipTime;
    bool nativePeerCaughtUp{};
    bool oldTipAt(const QDateTime& now)const {
        return tipTime&&now.isValid()&&now.toSecsSinceEpoch()-*tipTime>24*60*60;
    }
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
    auto observation=ChainObservation{owner,genesis.toLower(),height.toInteger(),observedUtc.toUTC(),
        sync.isBool()?std::optional<bool>(sync.toBool()):std::nullopt,tip.toString().toLower()};
    observation.coreInitialBlockDownload=observation.synchronizing;
    const auto exact=[](const QJsonValue& value)->std::optional<qint64>{
        const auto number=value.toDouble(-1);
        if(!value.isDouble()||!std::isfinite(number)||number<0||number>9007199254740991.0||std::trunc(number)!=number)return {};
        return value.toInteger();
    };
    observation.headerHeight=exact(chain.value("headers"));observation.tipTime=exact(chain.value("time"));
    return {observation,{}};
}
// The owned Core retains its IBD heuristic, including tip age. Native work
// readiness describes validated header/body catch-up with our explicit local
// peer bridge; it never changes Core validation or claims monetary acceptance.
inline ChainObservation projectNativeSynchronization(ChainObservation value,const QJsonArray& peers,const QString& endpoint){
    value.nativePeerCaughtUp=false;
    if(endpoint.isEmpty()||peers.size()>512||!value.coreInitialBlockDownload||!value.headerHeight||*value.headerHeight<value.height){value.synchronizing.reset();return value;}
    bool found=false,pending=*value.headerHeight>value.height;
    for(const auto& entry:peers){
        if(!entry.isObject()){value.synchronizing.reset();return value;}
        const auto peer=entry.toObject();if(peer.value("addr").toString()!=endpoint)continue;
        const auto height=peer.value("pgn_announced_height"),sync=peer.value("pgn_syncing");const auto number=height.toDouble(-1);
        if(peer.value("connection_type")!="manual"||!height.isDouble()||!std::isfinite(number)||number<0||number>9007199254740991.0||std::trunc(number)!=number||!sync.isBool()){value.synchronizing.reset();return value;}
        found=true;pending=pending||sync.toBool()||value.height<height.toInteger();
    }
    value.synchronizing=!found||pending||value.height<1;
    value.nativePeerCaughtUp=found&&!pending&&value.height>0;
    return value;
}
}
