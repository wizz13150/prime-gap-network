#include "wallet/explorer_projection.h"
#include "wallet/core_rpc.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QFile>
#include <QDir>
#include <iostream>
#include <stdexcept>
using namespace pgn::wallet;
static unsigned checks{};
static void need(bool good,const char* why){++checks;if(!good)throw std::runtime_error(why);}
static const QString ID(64,'a'),BLOCK(64,'b');
static QJsonObject txPage(int offset){
 QJsonArray vin,vout;for(int n=offset;n<std::min(offset+25,17);++n)vin.append(QJsonObject{{"n",n},{"txid",QString(64,'c')},{"vout",n}});
 for(int n=offset;n<std::min(offset+25,71);++n)vout.append(QJsonObject{{"n",n},{"value",1},{"scriptPubKey",QJsonObject{{"bytes",250},{"hex_preview",QString(192,'0')},{"truncated",true}}}});
 QJsonObject tx{{"schema","pgn-explorer-view-1"},{"kind","transaction"},{"txid",ID},{"blockhash",BLOCK},{"vin_total",17},{"vout_total",71},{"io_offset",offset},{"io_limit",25},{"output_atomic","710000000000"},{"vin",vin},{"vout",vout}};
 const QJsonObject envelope{{"jsonrpc","2.0"},{"id","projection"},{"result",tx}};const auto decoded=decodeCoreRpcResponse(QJsonDocument(envelope).toJson(QJsonDocument::Compact),"projection","getpgnexplorerview");need(decoded.error.isEmpty(),"Projection exact decoder failed");return decoded.value.toObject();
}
static QJsonObject blockPage(int offset){
 QJsonArray tx;for(int i=offset;i<std::min(offset+25,71);++i)tx.append(QJsonObject{{"txid",QString::number(i+1,16).rightJustified(64,'0')},{"output_atomic","710000000000"},{"vin_total",17},{"vout_total",71}});
 return {{"schema","pgn-explorer-view-1"},{"kind","block"},{"hash",BLOCK},{"height",100},{"nTx",71},{"tx_total",71},{"tx_offset",offset},{"tx_limit",25},{"coinbase_output_atomic","0"},{"tx",tx}};
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);
 qint64 total{},rows{};for(int offset:{0,25,50,75}){const auto tx=txPage(offset);need(validateExplorerTransactionPage(tx,ID,BLOCK,offset).isEmpty(),"Valid exact transaction page rejected");for(const auto& row:tx.value("vout").toArray()){total+=*exactAtomicField(row.toObject(),"value");++rows;}need(validateExplorerBlockPage(blockPage(offset),BLOCK,offset).isEmpty(),"Valid bounded block page rejected");}
 need(rows==71&&total==710000000000LL,"All output pages did not conserve exact amounts");
 auto tx=txPage(25);auto bad=tx;bad["output_atomic"]="7.1e11";need(!validateExplorerTransactionPage(bad,ID,BLOCK,25).isEmpty(),"Noncanonical atomic total accepted");
 bad=tx;bad["io_offset"]=0;need(!validateExplorerTransactionPage(bad,ID,BLOCK,25).isEmpty(),"Foreign page offset accepted");
 bad=tx;auto out=bad["vout"].toArray();out.removeLast();bad["vout"]=out;need(!validateExplorerTransactionPage(bad,ID,BLOCK,25).isEmpty(),"Missing row accepted");
 bad=tx;out=bad["vout"].toArray();auto row=out[0].toObject();row["n"]=24;out[0]=row;bad["vout"]=out;need(!validateExplorerTransactionPage(bad,ID,BLOCK,25).isEmpty(),"Wrong output index accepted");
 bad=tx;out=bad["vout"].toArray();row=out[0].toObject();row.remove("_pgn_atomic");out[0]=row;bad["vout"]=out;need(!validateExplorerTransactionPage(bad,ID,BLOCK,25).isEmpty(),"Inexact output accepted");
 for(const QString key:{"hex_preview","truncated","bytes"}){bad=tx;out=bad["vout"].toArray();row=out[0].toObject();auto script=row["scriptPubKey"].toObject();script.remove(key);row["scriptPubKey"]=script;out[0]=row;bad["vout"]=out;need(!validateExplorerTransactionPage(bad,ID,BLOCK,25).isEmpty(),"Implicit/unbounded script preview accepted");}
 auto b=blockPage(25);b["tx_total"]=72;need(!validateExplorerBlockPage(b,BLOCK,25).isEmpty(),"Contradicting block transaction count accepted");
 b=blockPage(25);b["tx"]=QJsonArray{};need(!validateExplorerBlockPage(b,BLOCK,25).isEmpty(),"Skipped block transaction rows accepted");
 bool current=true;TransactionReply pending;QString method,error;QJsonArray params;QJsonObject result;unsigned calls{};
 TransactionRpc rpc=[&](const QString& m,const QJsonArray& p,TransactionReply done){++calls;method=m;params=p;pending=std::move(done);};
 auto consume=[&](QJsonObject v,QString e){result=v;error=e;};auto live=[&]{return current;};
 readExplorerTransactionPage(ID,BLOCK,25,live,rpc,consume);need(method=="getpgnexplorerview"&&params==QJsonArray{"transaction",ID,BLOCK,25,25},"Bounded native RPC page changed");pending(tx,{});need(error.isEmpty()&&result==tx,"Reader changed projected data");
 readExplorerTransactionPage(ID,BLOCK,25,live,rpc,consume);current=false;pending(tx,{});need(error.contains("connection changed")&&result.isEmpty(),"Stale projection epoch accepted");current=true;
 auto before=calls;readExplorerTransactionPage(ID,BLOCK,-1,live,rpc,consume);need(calls==before&&!error.isEmpty(),"Invalid offset issued an RPC");
 readExplorerTransactionPage(ID,BLOCK,0,live,rpc,consume);pending({},"Core RPC error (-32601): Method not found");need(method=="getrawtransaction"&&params==QJsonArray{ID,1,BLOCK},"Legacy fallback lost explicit block anchor");pending({},"Not available");need(error=="Not available","Legacy failure hidden");
 readExplorerTransactionPage(ID,BLOCK,25,live,rpc,consume);before=calls;pending({},"Core RPC error (-32601): Method not found");need(calls==before&&!error.isEmpty(),"Legacy fallback silently returned first page for later offset");
 readExplorerBlockPage(BLOCK,25,live,rpc,consume);pending(blockPage(25),{});need(error.isEmpty()&&result==blockPage(25),"Block page lookup failed");
 readExplorerBlockPage(BLOCK,0,live,rpc,consume);pending({},"Core RPC error (-32601): Method not found");need(method=="getblock"&&params==QJsonArray{BLOCK,2},"Legacy native block fallback changed");pending(QJsonObject{{"hash",QString(64,'d')}},{});need(!error.isEmpty()&&result.isEmpty(),"Legacy foreign block accepted");
 if(argc>1){
  const QDir artifacts(QString::fromLocal8Bit(argv[1]));
  for(const QString name:{"large-block-projection.json","default-transaction-page.json"}){
   QFile file(artifacts.filePath(name));need(file.open(QIODevice::ReadOnly)&&file.size()<=65536,"Native artifact is unavailable or oversized");
   const auto decoded=decodeCoreRpcResponse(file.readAll(),"isolated-transaction","getpgnexplorerview");need(decoded.error.isEmpty(),"Actual native projection failed the production exact RPC decoder");
   const auto value=decoded.value.toObject();
   need((name.startsWith("large-block")?validateExplorerBlockPage(value,value.value("hash").toString(),0):validateExplorerTransactionPage(value,value.value("txid").toString(),value.value("blockhash").toString(),0)).isEmpty(),"Actual native projection failed the client schema verifier");
  }
 }
 std::cout<<"PASS "<<checks<<" bounded explorer projection checks: page conservation, exact totals, explicit previews, identity/epoch and version fallback\n";
}
