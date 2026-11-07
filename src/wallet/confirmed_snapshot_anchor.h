#pragma once
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QString>
#include <functional>
#include <limits>
namespace pgn::wallet {
// Only use for one atomic projection or immutable data linked to this anchor.
// Checking an ancestor does not make independently read mutable reports atomic.
using SnapshotHashCall=std::function<void(qint64,std::function<void(QJsonValue,QString)>)>;
inline void verifyConfirmedSnapshotAnchor(const QJsonObject& state,std::function<bool()> current,
                                         SnapshotHashCall call,std::function<void(QString)> done){
 const auto value=state.value(state.contains("tip_height")?"tip_height":"height");
 const auto height=value.toInteger(-1);const auto hash=state.value("bestblockhash").toString();
 static const QRegularExpression canonical("^[0-9a-f]{64}$");
 if(!value.isDouble()||height<0||height>std::numeric_limits<int>::max()||value.toDouble()!=double(height)||
    !canonical.match(hash).hasMatch()||hash==QString(64,'0')){done("Snapshot chain anchor is invalid");return;}
 if(!current()){done("Core connection changed during the read");return;}
 call(height,[current=std::move(current),hash,done=std::move(done)](QJsonValue result,QString error){
  if(!current()){done("Core connection changed during the read");return;}
  if(!error.isEmpty()){done(error);return;}
  if(!result.isString()||result.toString()!=hash){done("Chain branch changed during the read; refreshing");return;}
  done({});
 });
}
}
