#pragma once
#include "wallet/exact_rpc_money.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <functional>
namespace pgn::wallet {
using TransactionReply=std::function<void(QJsonValue,QString)>;
using TransactionRpc=std::function<void(const QString&,const QJsonArray&,TransactionReply)>;
// The caller pins an owned Core epoch. No index activation, wallet mutation,
// network scan or approximate monetary conversion is allowed here.
inline void readNativeTransaction(const QString& txid,const QString& blockHash,
 const std::function<bool()>& current,const TransactionRpc& rpc,
 std::function<void(QJsonObject,QString)> consume){
 static const QRegularExpression id("^[0-9a-f]{64}$");
 if(!current()||!id.match(txid).hasMatch()||(!blockHash.isEmpty()&&!id.match(blockHash).hasMatch())){
  consume({},"Transaction lookup requires a verified Core and a valid transaction ID");return;
 }
 QJsonArray params{txid,1};if(!blockHash.isEmpty())params.append(blockHash);
 rpc("getrawtransaction",params,[txid,blockHash,current,consume=std::move(consume)](QJsonValue value,QString error){
  if(!current()){consume({},"Core connection changed during transaction lookup");return;}
  if(!error.isEmpty()){
   if(error.contains("-txindex"))error="Historical transaction lookup needs the Core transaction index. Open its block instead. The index was not enabled or changed.\n"+error;
   consume({},error);return;
  }
  const auto tx=value.toObject();const auto hash=tx.value("blockhash").toString();
  if(!value.isObject()||tx.value("txid").toString()!=txid||
     (!blockHash.isEmpty()&&hash!=blockHash)||(!hash.isEmpty()&&!id.match(hash).hasMatch())||
     !tx.value("vin").isArray()||tx.value("vin").toArray().isEmpty()||tx.value("vin").toArray().size()>4096||
     !tx.value("vout").isArray()||tx.value("vout").toArray().isEmpty()||tx.value("vout").toArray().size()>4096||!exactTransactionOutput(tx)){
   consume({},"Native transaction identity, shape or exact amounts could not be verified");return;
  }
  consume(tx,{});
 });
}
}
