#pragma once
#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QString>
#include <algorithm>
namespace pgn::wallet {
inline QString validateConsoleCommand(const QString& method,const QByteArray& parameters){
 if(!QRegularExpression("^[a-z][a-z0-9_]{0,63}$").match(method).hasMatch())return "Enter a Core RPC method, for example getblockchaininfo";
 if(parameters.size()>65536)return "Parameters exceed the 64 KiB limit";
 QJsonParseError error;const auto parsed=QJsonDocument::fromJson(parameters,&error);
 if(error.error!=QJsonParseError::NoError||!parsed.isArray())return "Parameters must be a valid JSON array";
 return {};
}
// Validation never supplies the wire representation: retain the user's exact
// number tokens rather than serializing a QJson double back onto the network.
inline QByteArray consoleRpcEnvelope(const QString& requestId,const QString& method,const QByteArray& parameters){
 const QJsonObject prefix{{"jsonrpc","2.0"},{"id",requestId},{"method",method}};
 auto bytes=QJsonDocument(prefix).toJson(QJsonDocument::Compact);bytes.chop(1);
 return bytes+",\"params\":"+parameters+"}";
}
inline QString formatConsoleJson(const QByteArray& bytes){
 const auto raw=QString::fromUtf8(bytes);QString output;output.reserve(raw.size());
 int depth=0;bool quoted=false,escaped=false;
 const auto newline=[&]{output+='\n';output+=QString(std::min(depth,32)*2,' ');};
 for(const auto c:raw){
  if(quoted){output+=c;if(escaped)escaped=false;else if(c=='\\')escaped=true;else if(c=='"')quoted=false;continue;}
  if(c=='"'){quoted=true;output+=c;}
  else if(c=='{'||c=='['){output+=c;++depth;newline();}
  else if(c=='}'||c==']'){depth=std::max(0,depth-1);newline();output+=c;}
  else if(c==','){output+=c;newline();}
  else if(c==':')output+=QString(": ");
  else if(!c.isSpace())output+=c;
  if(output.size()>300000)return raw; // Exact compact response, never a partial value.
 }
 return output;
}
}
