#include "wallet/core_rpc.h"
#include "wallet/exact_rpc_money.h"
#include "wallet/currency_units.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QFile>
#include <QJsonDocument>
#include <functional>
#include <iostream>
#include <stdexcept>
using namespace pgn::wallet;
static void need(bool yes,const char* why){if(!yes)throw std::runtime_error(why);}
static CoreRpcResult reply(const QByteArray& body,const QString& method){return decodeCoreRpcResponse("{\"jsonrpc\":\"2.0\",\"id\":\"test\",\"result\":"+body+"}","test",method);}
int main(int argc,char** argv){QCoreApplication app(argc,argv);
 if(argc==2){QFile input;need(input.open(stdin,QIODevice::ReadOnly),"Native RPC fixture stdin unavailable");const auto raw=input.read(262145);const auto decoded=decodeCoreRpcResponse(raw,"native-money-proof",QString::fromLocal8Bit(argv[1]));need(decoded.error.isEmpty(),"Actual native monetary reply refused");int fields=0;bool gold=false;std::function<void(const QJsonValue&)> visit=[&](const QJsonValue& value){if(value.isObject()){const auto object=value.toObject(),amounts=object.value("_pgn_atomic").toObject();fields+=amounts.size();for(const auto& amount:amounts)gold|=amount.toString()=="1710000000000";for(auto it=object.begin();it!=object.end();++it)if(it.key()!="_pgn_atomic")visit(it.value());}else if(value.isArray())for(const auto& child:value.toArray())visit(child);};visit(decoded.value);std::cout<<QJsonDocument(QJsonObject{{"PASS",true},{"amount_fields",fields},{"gold_171_present",gold}}).toJson(QJsonDocument::Compact).constData()<<"\n";return 0;}
static_assert(CurrencyDecimalPlaces==10);static_assert(AtomicUnitsPerCoin==10000000000LL);
 auto decoded=reply("{\"mine\":{\"trusted\":99999999.9999999999,\"untrusted_pending\":0.0000000001,\"immature\":171.0000000000}}","getbalances");need(decoded.error.isEmpty(),"Exact balance refused");auto mine=decoded.value.toObject()["mine"].toObject();need(exactAtomicField(mine,"trusted")==999999999999999999LL,"Balance lost one atom near supply ceiling");need(exactAtomicField(mine,"untrusted_pending")==1,"Smallest unit vanished");need(exactAtomicField(mine,"immature")==1710000000000LL,"Native denomination changed");need(formatAtomicPgn(*exactAtomicField(mine,"trusted"))=="99999999.9999999999","Display rounded exact amount");
 decoded=reply("[{\"amount\":-0.0000000001,\"fee\":-1.2345678901,\"confirmations\":42,\"description\":\"text: \\\"amount\\\":999\"}]","listtransactions");need(decoded.error.isEmpty(),"Signed native delta refused");auto tx=decoded.value.toArray()[0].toObject();need(exactAtomicField(tx,"amount")==-1&&exactAtomicField(tx,"fee")==-12345678901LL&&tx["confirmations"].toInt()==42,"Delta or numeric metadata changed");
 decoded=reply("{\"tx\":[{\"vout\":[{\"value\":171.0000000001},{\"value\":0.0000000001}]}]}","getblock");need(decoded.error.isEmpty(),"Exact outputs refused");need(exactTransactionOutput(decoded.value.toObject()["tx"].toArray()[0].toObject())==1710000000002LL,"Output sum rounded");
 for(const QByteArray& bad:{QByteArray("{\"mine\":{\"trusted\":0.00000000001}}"),QByteArray("{\"mine\":{\"trusted\":100000000.0000000001}}"),QByteArray("{\"mine\":{\"trusted\":1e-10}}"),QByteArray("{\"mine\":{\"trusted\":1,\"trusted\":2}}"),QByteArray("{\"mine\":{\"trusted\":1,\"_pgn_atomic\":{}}}"),QByteArray("{\"mine\":{\"trusted\":\"1\"}}")})need(!reply(bad,"getbalances").error.isEmpty(),"Ambiguous or out-of-policy money accepted");
 need(parsePgnTransfer("0.0000000001")==1&&!parsePgnTransfer("0.00000000001")&&!parsePgnTransfer("1e-10")&&!parsePgnTransfer("-1"),"Transfer parser did not enforce native decimals");need(!exactAtomicField(QJsonObject{{"amount",1.0}},"amount"),"Floating fallback accepted");
 auto failure=decodeCoreRpcResponse("{\"id\":\"test\",\"result\":null,\"error\":{\"code\":-5,\"message\":\"Invalid address\"}}","test");need(verifiedRpcHttpError(failure,500)&&verifiedRpcHttpError(failure,404)&&!verifiedRpcHttpError(failure,401),"RPC and HTTP failure boundaries confused");need(!verifiedRpcHttpError(decoded,500),"Success accepted as HTTP failure response");
 std::cout<<"PASS exact monetary RPC:10decimals,1atom near supply limit,signed deltas,exact output totals,ambiguous/overprecise refusal,no floating fallback,authenticated HTTP error handling\n";
}
