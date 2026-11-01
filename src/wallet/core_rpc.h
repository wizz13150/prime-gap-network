#pragma once
#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QString>
namespace pgn::wallet {
struct CoreEpoch {
 qint64 pid{};QString birth;quint64 generation{};
 bool matches(const CoreEpoch& other)const{return pid>0&&!birth.isEmpty()&&pid==other.pid&&birth==other.birth&&generation==other.generation;}
};
struct CoreRpcResult {QJsonValue value;QString error;};
inline CoreRpcResult decodeCoreRpcResponse(const QByteArray& bytes,const QString& expectedId){
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
 if(!object.value("id").isString()||object.value("id").toString()!=expectedId||
    !object.contains("result")||(!v2&&!object.contains("error"))||
    (object.contains("error")&&!object.value("error").isNull()))
  return {{},"Core RPC response identity/result/error mismatch"};
 return {object.value("result"),{}};
}
}
