#include "wallet/transaction_lookup.h"
#include "wallet/core_rpc.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <iostream>
#include <stdexcept>
using namespace pgn::wallet;
static unsigned cases{};
static void need(bool good,const char* why){++cases;if(!good)throw std::runtime_error(why);}
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);const QString id(64,'a'),block(64,'b');bool current=true;unsigned calls{},answers{};TransactionReply pending;QJsonArray params;QString method,error;QJsonObject result;
 const auto raw=QByteArray("{\"jsonrpc\":\"2.0\",\"id\":\"tx-fixture\",\"result\":{\"txid\":\"")+id.toLatin1()+"\",\"blockhash\":\""+block.toLatin1()+"\",\"confirmations\":2,\"vin\":[{\"coinbase\":\"00\"}],\"vout\":[{\"n\":0,\"value\":99999999.9999999999,\"scriptPubKey\":{}}]}}";
 const auto decoded=decodeCoreRpcResponse(raw,"tx-fixture","getrawtransaction");need(decoded.error.isEmpty(),"Real decoder rejected exact native transaction fixture");
 const auto valid=decoded.value.toObject();
 const TransactionRpc rpc=[&](const QString& name,const QJsonArray& args,TransactionReply done){++calls;method=name;params=args;pending=std::move(done);};
 const auto consume=[&](QJsonObject tx,QString why){++answers;result=tx;error=why;};
 const auto read=[&](const QString& hash,const QString& anchor={}){result={};error={};readNativeTransaction(hash,anchor,[&]{return current;},rpc,consume);};
 read(id);need(calls==1&&method=="getrawtransaction"&&params==QJsonArray{id,1},"Lookup changed the native read-only RPC or enabled an index");
 pending(valid,{});need(error.isEmpty()&&result==valid,"Native exact transaction changed during lookup");need(exactTransactionOutput(result)==999999999999999999LL,"Exact near-supply amount lost atomic precision");
 read(id,block);need(params==QJsonArray{id,1,block},"Explicit block lookup did not preserve its anchor");pending(valid,{});need(error.isEmpty(),"Anchored transaction lookup failed");
 read(id,block);auto wrong=valid;wrong["blockhash"]=QString(64,'c');pending(wrong,{});need(!error.isEmpty()&&result.isEmpty(),"Foreign block transaction accepted");
 read(id);wrong=valid;wrong["txid"]=QString(64,'c');pending(wrong,{});need(!error.isEmpty()&&result.isEmpty(),"Foreign transaction ID accepted");
 read(id);current=false;pending(valid,{});need(error.contains("connection changed")&&result.isEmpty(),"Stale Core epoch accepted");
 auto before=calls;read(id);need(calls==before&&!error.isEmpty(),"Disconnected Core lookup issued an RPC");current=true;
 for(const auto& bad:QStringList{"",QString(63,'a'),QString(65,'a'),QString(64,'g'),"../../wallet"}){before=calls;read(bad);need(calls==before&&!error.isEmpty(),"Invalid transaction ID issued an RPC");}
 before=calls;read(id,"not-a-block");need(calls==before&&!error.isEmpty(),"Invalid block anchor issued an RPC");
 read(id);pending({},"Core RPC error (-5): No such mempool transaction. Use -txindex or provide a block hash");need(error.contains("index was not enabled")&&result.isEmpty(),"Missing-index response was hidden or activated an index");
 read(id);pending({},"Core RPC error (-5): No such mempool or blockchain transaction");need(error.contains("No such")&&!error.contains("index was not enabled"),"Unknown indexed transaction misreported as missing index");
 read(id);pending({},"Timeout");need(error=="Timeout"&&result.isEmpty(),"Transport error was hidden");
 for(const auto& key:QStringList{"vin","vout"}){read(id);wrong=valid;wrong[key]=QJsonArray{};pending(wrong,{});need(!error.isEmpty(),"Empty native inputs or outputs accepted");}
 read(id);auto unannotated=QJsonDocument::fromJson(raw).object().value("result").toObject();pending(unannotated,{});need(!error.isEmpty()&&result.isEmpty(),"Unannotated floating monetary value accepted");
 read(id);wrong=valid;wrong["blockhash"]="invalid";pending(wrong,{});need(!error.isEmpty(),"Malformed block hash accepted");
 read(id);auto mempool=valid;mempool.remove("blockhash");mempool.remove("confirmations");pending(mempool,{});need(error.isEmpty()&&result==mempool,"Native mempool transaction rejected");
 std::cout<<"PASS "<<cases<<" native transaction lookup contracts: exact money, epoch/identity, optional block anchor, missing index, mempool and bounded shape\n";
}
