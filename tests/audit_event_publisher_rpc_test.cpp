#include "wallet/audit_event_publisher.h"
#include "wallet/core_rpc.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <QFile>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QNetworkProxy>
#include <QUuid>
#include <iostream>
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);if(argc!=2)return 2;QFile input(QString::fromLocal8Bit(argv[1]));if(!input.open(QIODevice::ReadOnly)||input.size()>32768)return 3;const auto config=QJsonDocument::fromJson(input.readAll()).object();
 const QUrl endpoint(config.value("url").toString());if(endpoint.scheme()!="http"||endpoint.host()!="127.0.0.1"||endpoint.port()!=48962)return 4;
 QFile cookie(config.value("cookie_file").toString());if(!cookie.open(QIODevice::ReadOnly)||cookie.size()>4096)return 5;const auto credential=cookie.readAll().trimmed().toBase64();cookie.close();
 QNetworkAccessManager network;network.setProxy(QNetworkProxy::NoProxy);int submits=0,signs=0;bool lost=false,identical=true;QString submitted;const bool loseBefore=config.value("lose_before_submit").toBool();
 auto call=[&](QString method,QJsonArray params,pgn::wallet::AuditEventPublisher::Reply consume,bool wallet){
  if(method=="signmessage")++signs;
  if(method=="submitblock"){
   ++submits;if(submitted.isEmpty())submitted=params[0].toString();else identical=identical&&submitted==params[0].toString();
   if(loseBefore&&!lost){lost=true;consume({},"Simulated transport loss before sending to Core");return;}
  }
  auto url=endpoint;if(wallet)url.setPath("/wallet/"+config.value("wallet").toString());QNetworkRequest request(url);request.setTransferTimeout(10000);request.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");request.setRawHeader("Authorization","Basic "+credential);request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);const auto id=QUuid::createUuid().toString(QUuid::WithoutBraces);
  auto* reply=network.post(request,QJsonDocument(QJsonObject{{"jsonrpc","2.0"},{"id",id},{"method",method},{"params",params}}).toJson(QJsonDocument::Compact));reply->setReadBufferSize(262145);
  QObject::connect(reply,&QNetworkReply::finished,&app,[&,reply,id,method,consume]{const auto bytes=reply->read(262145);const auto failed=reply->error()!=QNetworkReply::NoError;reply->deleteLater();if(failed){consume({},"Fixture RPC transport failed");return;}const auto parsed=pgn::wallet::decodeCoreRpcResponse(bytes,id);if(!parsed.error.isEmpty()){consume({},parsed.error);return;}
   if(method=="submitblock"&&!lost){lost=true;consume({},"Simulated response loss after actual Core reply");return;}
   consume(parsed.value,{});
  });
 };
 pgn::wallet::AuditEventPublisher publisher(config.value("journal").toString(),config.value("genesis").toString(),[&](auto method,auto params,auto consume){call(method,params,consume,false);},[&](auto address,auto challenge,auto consume){call("signmessage",{address,challenge},consume,true);});
 QEventLoop loop;QTimer timeout;timeout.setSingleShot(true);bool okay=false,recovered=false;QString error;QJsonObject receipt;
 QObject::connect(&timeout,&QTimer::timeout,&loop,[&]{error="Fixture publication timeout";loop.quit();});timeout.start(20000);
 const auto finish=[&](QJsonObject value,QString failure){receipt=value;error=failure;if(error.isEmpty())okay=receipt.value("coordination_confirmed")==true&&publisher.acknowledge(receipt.value("block_hash").toString())&&!publisher.pending();loop.quit();};
 publisher.publish({"commit",config.value("address").toString(),config.value("request").toString(),config.value("commitment").toString(),config.value("slot").toInt(),config.value("attempt").toInteger()},[&](QJsonObject,QString failure){if(failure.isEmpty()||!publisher.pending()){error="Expected retained ambiguous submit outcome";loop.quit();return;}QTimer::singleShot(0,&app,[&]{recovered=true;publisher.recover(finish);});});
 loop.exec();QJsonObject result{{"PASS",okay&&lost&&recovered&&identical&&signs==1&&submits==(loseBefore?2:1)},{"submits",submits},{"signatures",signs},{"identical_block",identical},{"loss_before_submit",loseBefore},{"lost_response_recovered",recovered},{"hash",receipt.value("block_hash")},{"error",error},{"scope","Actual isolated Core signature/publication/confirmation only; synthetic audit commitment, no scientific recomputation"}};std::cout<<QJsonDocument(result).toJson(QJsonDocument::Compact).constData()<<'\n';return result.value("PASS").toBool()?0:1;
}
