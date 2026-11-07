#include "wallet/exact_rpc_money.h"
#include "wallet/currency_units.h"
#include <pgn/exact_amount.h>
#include <univalue.h>
#include <QJsonArray>
#include <QSet>
#include <QRegularExpression>
#include <stdexcept>
namespace pgn::wallet {
namespace {
constexpr auto Maximum=core::PGN_MAX_SUPPLY_ATOMIC;
const core::ExactAmountPolicy Policy{core::PGN_DECIMAL_PLACES,Maximum};
void require(bool valid){if(!valid)throw std::runtime_error("Invalid exact monetary RPC data");}
qint64 rawAmount(std::string token){bool negative=!token.empty()&&token.front()=='-';if(negative)token.erase(0,1);const auto parsed=core::parseExactAmount(token,Policy,{64});require(parsed.amount.has_value());return negative?-*parsed.amount:*parsed.amount;}
void unique(const UniValue& value,unsigned depth=0){require(depth<=64);if(value.isObject()){QSet<QString> keys;for(const auto& key:value.getKeys()){const auto text=QString::fromStdString(key);require(!keys.contains(text));keys.insert(text);}for(const auto& child:value.getValues())unique(child,depth+1);}else if(value.isArray())for(const auto& child:value.getValues())unique(child,depth+1);}
QJsonValue annotate(const UniValue& source,const QJsonValue& value,unsigned depth=0){
 require(depth<=64);
 if(source.isObject()){
  require(value.isObject());auto object=value.toObject();require(object.size()==qint64(source.size())&&!object.contains("_pgn_atomic"));QJsonObject exact;
  static const QSet<QString> money{"value","amount","fee","trusted","untrusted_pending","immature"};
  const auto& keys=source.getKeys();const auto& children=source.getValues();
  for(size_t i=0;i<keys.size();++i){const auto key=QString::fromStdString(keys[i]);require(object.contains(key));const auto& child=children[i];
   if(money.contains(key)){require(child.isNum()&&object.value(key).isDouble());exact.insert(key,QString::number(rawAmount(child.getValStr())));}
   object.insert(key,annotate(child,object.value(key),depth+1));
  }
  if(!exact.isEmpty())object.insert("_pgn_atomic",exact);return object;
 }
 if(source.isArray()){require(value.isArray());auto array=value.toArray();require(array.size()==qint64(source.size()));const auto& children=source.getValues();for(qsizetype i=0;i<array.size();++i)array[i]=annotate(children[size_t(i)],array[i],depth+1);return array;}
 return value;
}
}
ExactRpcMoney annotateExactRpcMoney(const QByteArray& bytes,const QString& id,const QString& method,const QJsonValue& result){
 static const QSet<QString> methods{"getbalances","listtransactions","listreceivedbyaddress","getblock","gettransaction","getrawtransaction","getpgnexplorerview"};
 if(!methods.contains(method))return {result,{}};
 try{UniValue parsed;require(parsed.read(std::string_view(bytes.constData(),size_t(bytes.size())))&&parsed.isObject());unique(parsed);require(parsed["id"].isStr()&&QString::fromStdString(parsed["id"].get_str())==id);return {annotate(parsed["result"],result),{}};}
 catch(...){return {{},"Core monetary response is not exact, bounded PGN data"};}
}
std::optional<qint64> parsePgnTransfer(const QString& value){const auto parsed=core::parseExactAmount(value.toStdString(),Policy,{64});return parsed.amount;}
std::optional<qint64> exactAtomicField(const QJsonObject& object,const QString& key){const auto value=object.value("_pgn_atomic").toObject().value(key);if(!value.isString())return {};const auto text=value.toString();bool okay{};const auto number=text.toLongLong(&okay);if(!okay||QString::number(number)!=text||number>Maximum||number<-Maximum)return {};return number;}
QString formatAtomicPgn(qint64 value,bool compact){const auto formatted=core::formatExactAmount(value,core::ExactAmountFormat::Delta,Policy,{64});if(!formatted.text)return "—";auto text=QString::fromStdString(*formatted.text);if(compact&&text.contains('.')){while(text.endsWith('0'))text.chop(1);if(text.endsWith('.'))text.chop(1);}return text;}
std::optional<qint64> exactTransactionOutput(const QJsonObject& transaction){if(!transaction.value("vout").isArray())return {};qint64 total{};for(const auto& value:transaction.value("vout").toArray()){const auto amount=exactAtomicField(value.toObject(),"value");if(!amount||*amount<0||*amount>Maximum-total)return {};total+=*amount;}return total;}
}
