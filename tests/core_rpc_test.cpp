#include "wallet/core_rpc.h"
#include <QCoreApplication>
#include <iostream>
int main(int argc,char** argv){QCoreApplication app(argc,argv);using namespace pgn::wallet;
 const CoreEpoch a{42,"birth-a",1};
 if(!a.matches(a)||a.matches({42,"birth-b",1})||a.matches({42,"birth-a",2})||a.matches({0,"birth-a",1})||CoreEpoch{}.matches({}))return 1;
 for(const auto& reply:{QByteArray("{\"jsonrpc\":\"2.0\",\"result\":7,\"id\":\"unique\"}"),QByteArray("{\"result\":7,\"error\":null,\"id\":\"unique\"}"),QByteArray("{\"jsonrpc\":\"2.0\",\"result\":7,\"error\":null,\"id\":\"unique\"}")}){
  const auto decoded=decodeCoreRpcResponse(reply,"unique");if(!decoded.error.isEmpty()||decoded.value.toInt()!=7)return 2;
 }
 for(const auto& invalid:{QByteArray("{\"jsonrpc\":\"2.0\",\"result\":7,\"error\":{\"code\":-1},\"id\":\"unique\"}"),QByteArray("{\"jsonrpc\":\"2.0\",\"error\":null,\"id\":\"unique\"}"),QByteArray("[]"),QByteArray("{"),QByteArray("{\"result\":7,\"error\":null,\"id\":\"old\"}"),QByteArray("{\"result\":7,\"error\":null,\"id\":1}"),QByteArray("{\"result\":7,\"id\":\"unique\"}"),QByteArray("{\"error\":null,\"id\":\"unique\"}"),QByteArray("{\"result\":7,\"error\":{},\"id\":\"unique\"}"),QByteArray("{\"jsonrpc\":\"1.0\",\"result\":7,\"error\":null,\"id\":\"unique\"}"),QByteArray(262145,' ')} )
  if(decodeCoreRpcResponse(invalid,"unique").error.isEmpty())return 3;
 if(decodeCoreRpcResponse("{\"result\":null,\"error\":null,\"id\":\"unique\"}","unique").value.isUndefined())return 4;
 const QByteArray failure="{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32602,\"message\":\"Wrong request shape\"},\"id\":\"unique\"}";
 if(decodeCoreRpcResponse(failure,"unique").applicationCode!=-32602||decodeCoreRpcResponse(failure,"old").applicationCode)return 10;
 if(!decodeCoreRpcResponse(failure,"unique").error.contains("-32602")||decodeCoreRpcResponse(failure,"old").error.contains("Wrong request shape"))return 5;
 const auto secret=QByteArray("prv")+QByteArray(64,'1');const auto privateError=QByteArray("{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-5,\"message\":\"")+secret+"\"},\"id\":\"unique\"}";
 if(decodeCoreRpcResponse(privateError,"unique").error.contains(QString::fromLatin1(secret)))return 6;
 const auto large=QByteArray("{\"jsonrpc\":\"2.0\",\"id\":\"unique\",\"result\":\"")+QByteArray(400000,'x')+"\"}";
 for(const auto* method:{"getpgnauditactivity","getpgnworkview","getpgnticketatlas"}){
  const auto value=decodeCoreRpcResponse(large,"unique",method);
  if(!value.error.isEmpty()||value.value.toString().size()!=400000)return 7;
 }
 if(decodeCoreRpcResponse(large,"unique","getbalances").error.isEmpty())return 8;
 std::cout<<"PASS exact request identity, valid genuine Core response forms, bounded/error/malformed/foreign rejection and PID-birth-generation lifecycle isolation\n";
}
