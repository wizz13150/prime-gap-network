#pragma once
#include "wallet/exact_rpc_money.h"
#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QString>
#include <QRegularExpression>
#include <cmath>
namespace pgn::wallet {
struct CoreEpoch {
 qint64 pid{};QString birth;quint64 generation{};
 bool matches(const CoreEpoch& other)const{return pid>0&&!birth.isEmpty()&&pid==other.pid&&birth==other.birth&&generation==other.generation;}
};
struct CoreRpcResult {QJsonValue value;QString error;bool applicationError{};};
inline CoreRpcResult decodeCoreRpcResponse(const QByteArray& bytes,const QString& expectedId,const QString& method={}){
 if(bytes.size()>262144||expectedId.isEmpty())return {{},"Core RPC response exceeds bound or request identity is missing"};
 QJsonParseError parse;const auto document=QJsonDocument::fromJson(bytes,&parse);
 if(parse.error!=QJsonParseError::NoError||!document.isObject())return {{},"Core RPC returned invalid JSON"};
 const auto object=document.object();
 // JSON-RPC2 success can omit error; legacy1 success includes null error.
 // Both must echo this unique string id and contain a result. Never turn an
 // actual RPC error, foreign id or unsupported protocol into a value.
 const bool v2=object.contains("jsonrpc");
 if(v2&&(!object.value("jsonrpc").isString()||object.value("jsonrpc").toString()!="2.0"))
  return {{},"Core RPC version mismatch"};
 if(!object.value("id").isString()||object.value("id").toString()!=expectedId)
  return {{},"Core RPC response identity/result/error mismatch"};
 if(object.contains("error")&&!object.value("error").isNull()){
  const auto failure=object.value("error").toObject();const auto code=failure.value("code");
  if(!code.isDouble()||!std::isfinite(code.toDouble())||code.toDouble()!=code.toInt()||!failure.value("message").isString()||
     object.contains("result")&&!object.value("result").isNull())return {{},"Core RPC error envelope invalid"};
  auto message=failure.value("message").toString().left(1000);message.replace(QRegularExpression("prv[0-9a-fA-F]{64}"),"[private key]");
  return {{},QString("Core RPC error (%1): %2").arg(code.toInt()).arg(message),true};
 }
 if(!object.contains("result")||(!v2&&!object.contains("error")))return {{},"Core RPC response identity/result/error mismatch"};
 const auto exact=annotateExactRpcMoney(bytes,expectedId,method,object.value("result"));return {exact.value,exact.error};
}
inline bool verifiedRpcHttpError(const CoreRpcResult& decoded,int status){return decoded.applicationError&&(status==400||status==404||status==500);}
}
