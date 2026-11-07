#pragma once
#include "wallet/transaction_lookup.h"
#include "wallet/currency_units.h"
#include <algorithm>
namespace pgn::wallet {
inline std::optional<qint64> explorerAtomicString(const QJsonValue& value){
 if(!value.isString())return {};const auto text=value.toString();bool okay{};const auto n=text.toLongLong(&okay);
 if(!okay||n<0||n>core::PGN_MAX_SUPPLY_ATOMIC||QString::number(n)!=text)return {};return n;
}
inline std::optional<qint64> explorerOutputTotal(const QJsonObject& tx){
 return tx.contains("output_atomic")?explorerAtomicString(tx.value("output_atomic")):exactTransactionOutput(tx);
}
inline bool explorerCount(const QJsonObject& obj,const QString& key,qint64 max=1000000){
 const auto value=obj.value(key);return value.isDouble()&&value.toInteger(-1)>=0&&value.toInteger()<=max&&value.toDouble()==double(value.toInteger());
}
inline QString validateExplorerTransactionPage(const QJsonObject& tx,const QString& id,const QString& block,int offset){
 static const QRegularExpression hash("^[0-9a-f]{64}$");
 if(tx.value("schema")!="pgn-explorer-view-1"||tx.value("kind")!="transaction"||tx.value("txid").toString()!=id||
    (!block.isEmpty()&&tx.value("blockhash").toString()!=block)||(!tx.value("blockhash").toString().isEmpty()&&!hash.match(tx.value("blockhash").toString()).hasMatch())||
    !explorerCount(tx,"vin_total")||!explorerCount(tx,"vout_total")||tx.value("vin_total").toInteger()==0||tx.value("vout_total").toInteger()==0||!explorerCount(tx,"io_offset")||tx.value("io_offset").toInteger()!=offset||!explorerCount(tx,"io_limit",25)||tx.value("io_limit").toInteger()!=25||!explorerOutputTotal(tx))return "Native transaction page identity or totals are invalid";
 for(const auto& key:QStringList{"vin","vout"}){
  const auto count=tx.value(key+"_total").toInteger();const auto rows=tx.value(key).toArray();
  if(!tx.value(key).isArray()||rows.size()!=std::min<qint64>(25,std::max<qint64>(0,count-offset)))return "Native transaction page is incomplete or unbounded";
  qint64 sum{};
  for(int i=0;i<rows.size();++i){const auto row=rows[i].toObject();if(!explorerCount(row,"n")||row.value("n").toInteger()!=offset+i)return "Native transaction page row identity is invalid";
   if(key=="vin"){
    if(!row.contains("coinbase")&&(!hash.match(row.value("txid").toString()).hasMatch()||!explorerCount(row,"vout",4294967295LL)))return "Native input outpoint is invalid";
   }else{
    const auto amount=exactAtomicField(row,"value"),total=explorerOutputTotal(tx);if(!amount||*amount<0||*amount>*total-sum)return "Native output amounts are not exact";sum+=*amount;
    const auto script=row.value("scriptPubKey").toObject();const auto preview=script.value("hex_preview").toString();
    if(!explorerCount(script,"bytes",100000000)||!script.value("truncated").isBool()||preview.size()!=2*std::min<qint64>(96,script.value("bytes").toInteger())||!QRegularExpression("^[0-9a-f]*$").match(preview).hasMatch()||script.value("truncated").toBool()!=(script.value("bytes").toInteger()>96)||script.contains("hex")||script.contains("asm"))return "Native script preview does not match its explicit bound";
   }
  }
 }
 return {};
}
inline QString validateExplorerBlockPage(const QJsonObject& block,const QString& id,int offset){
 static const QRegularExpression hash("^[0-9a-f]{64}$");
 if(block.value("schema")!="pgn-explorer-view-1"||block.value("kind")!="block"||block.value("hash").toString()!=id||
    !explorerCount(block,"height",2147483647)||!explorerCount(block,"tx_total")||!explorerCount(block,"nTx")||block.value("tx_total")!=block.value("nTx")||
    !explorerCount(block,"tx_offset")||block.value("tx_offset").toInteger()!=offset||!explorerCount(block,"tx_limit",25)||block.value("tx_limit").toInteger()!=25||!explorerAtomicString(block.value("coinbase_output_atomic")))return "Native block page identity or totals are invalid";
 const auto rows=block.value("tx").toArray();if(!block.value("tx").isArray()||rows.size()!=std::min<qint64>(25,std::max<qint64>(0,block.value("tx_total").toInteger()-offset)))return "Native block transaction page is incomplete or unbounded";
 for(const auto& value:rows){const auto tx=value.toObject();if(!hash.match(tx.value("txid").toString()).hasMatch()||!explorerOutputTotal(tx)||!explorerCount(tx,"vin_total")||!explorerCount(tx,"vout_total")||tx.contains("vin")||tx.contains("vout")||tx.contains("hex"))return "Native block transaction summary is invalid";}
 return {};
}
inline void readExplorerBlockPage(const QString& id,int offset,const std::function<bool()>& current,
 const TransactionRpc& rpc,std::function<void(QJsonObject,QString)> consume){
 static const QRegularExpression hash("^[0-9a-f]{64}$");
 if(!current()||offset<0||offset>1000000||!hash.match(id).hasMatch()){consume({},"Verified Core, block hash and bounded page required");return;}
 rpc("getpgnexplorerview",{"block",id,"",offset,25},[id,offset,current,rpc,consume=std::move(consume)](QJsonValue value,QString error){
  if(!current()){consume({},"Core connection changed during block page lookup");return;}
  if(error.startsWith("Core RPC error (-32601):")&&offset==0){
   rpc("getblock",{id,2},[id,current,consume](QJsonValue value,QString error){if(!current()){consume({},"Core connection changed during block lookup");return;}if(error.isEmpty()&&value.toObject().value("hash").toString()!=id)error="Native block identity changed";consume(error.isEmpty()?value.toObject():QJsonObject{},error);});return;
  }
  if(!error.isEmpty()){consume({},error);return;}
  const auto invalid=value.isObject()?validateExplorerBlockPage(value.toObject(),id,offset):QString("Native block page is not an object");consume(invalid.isEmpty()?value.toObject():QJsonObject{},invalid);
 });
}
inline void readExplorerTransactionPage(const QString& id,const QString& block,int offset,
 const std::function<bool()>& current,const TransactionRpc& rpc,std::function<void(QJsonObject,QString)> consume){
 static const QRegularExpression hash("^[0-9a-f]{64}$");
 if(!current()||offset<0||offset>1000000||!hash.match(id).hasMatch()||(!block.isEmpty()&&!hash.match(block).hasMatch())){consume({},"Verified Core, transaction ID and bounded page required");return;}
 rpc("getpgnexplorerview",{"transaction",id,block,offset,25},[id,block,offset,current,rpc,consume=std::move(consume)](QJsonValue value,QString error){
  if(!current()){consume({},"Core connection changed during transaction page lookup");return;}
  if(error.startsWith("Core RPC error (-32601):")&&offset==0){readNativeTransaction(id,block,current,rpc,consume);return;}
  if(!error.isEmpty()){consume({},error);return;}
  const auto invalid=value.isObject()?validateExplorerTransactionPage(value.toObject(),id,block,offset):QString("Native transaction page is not an object");
  consume(invalid.isEmpty()?value.toObject():QJsonObject{},invalid);
 });
}
}
