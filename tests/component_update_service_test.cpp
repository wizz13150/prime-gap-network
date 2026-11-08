#include "update/component_update_service.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>
#include <stdexcept>

int main(int argc,char** argv){
 QCoreApplication app(argc,argv);
 if(QFileInfo(app.applicationFilePath()).fileName()=="horizon-updater.exe"){
  QFile input;input.open(stdin,QIODevice::ReadOnly);const auto request=QJsonDocument::fromJson(input.readAll()).object();const auto fixture=request.value("fixture").toString();
  if(fixture=="delay")QThread::sleep(30);
  if(fixture=="oversized"){std::cout<<std::string(140000,'x');return 0;}
  if(fixture=="malformed"){std::cout<<"invalid JSON";return 0;}
  if(fixture=="failure"){std::cout<<"{\"error\":\"Fixture refused\"}";return 1;}
  const auto schema=fixture=="wrong-type"?"WRONG":request.value("action")=="check"?"PGN_AUTHENTICATED_COMPONENT_VIEW_V1":request.value("action")=="prepare"?"PGN_AUTHENTICATED_STAGE_RECEIPT_V1":"PGN_UPDATE_INSTALL_PREPARATION_V1";
  std::cout<<QJsonDocument(QJsonObject{{"schema",schema},{"publisher_authenticated",true},{"installation_performed",false}}).toJson(QJsonDocument::Compact).toStdString();return 0;
 }
 int checks=0;
 const auto require=[&](bool okay,const char* message){++checks;if(!okay)throw std::runtime_error(message);};
 try{
  QTemporaryDir temp;require(temp.isValid(),"Temporary directory unavailable");QDir().mkpath(temp.path()+"/UpdateRuntime");const auto helper=temp.path()+"/UpdateRuntime/horizon-updater.exe";require(QFile::copy(app.applicationFilePath(),helper),"Inert helper copy failed");QFile binary(helper);require(binary.open(QIODevice::ReadOnly),"Helper fixture unreadable");QCryptographicHash digest(QCryptographicHash::Sha256);require(digest.addData(&binary),"Fixture hash failed");binary.close();
  QJsonObject deployment{{"schema","PGN_UPDATE_DEPLOYMENT_V1"},{"network","testnet"},{"genesis_hash",QString(64,'5')},{"helper","UpdateRuntime/horizon-updater.exe"},{"sha256",QString::fromLatin1(digest.result().toHex())}};
  QFile descriptor(temp.path()+"/UpdateRuntime/deployment.json");require(descriptor.open(QIODevice::WriteOnly),"Descriptor fixture unavailable");descriptor.write(QJsonDocument(deployment).toJson());descriptor.close();
  auto resolved=pgn::update::componentUpdateDeployment(temp.path(),true);require(resolved.helper==helper,"Wrong helper selected");bool rejected=false;try{pgn::update::componentUpdateDeployment(temp.path(),false);}catch(const std::exception&){rejected=true;}require(rejected,"Wrong network was admitted");
  require(pgn::update::componentVersionComparison("1.2","1.2.0").value_or(1)==0,"Padded version comparison differs");
  require(pgn::update::componentVersionComparison("1.9","1.10").value_or(0)<0,"Numeric versions compared as text");
  require(!pgn::update::componentVersionComparison("custom","1.2"),"Unknown version became current");
  const auto contextRequest=pgn::update::componentUpdateRequest(resolved,QJsonObject{{"components",QJsonArray{QJsonObject{{"id","ui"},{"state","matched_local_manifest"},{"version","1.0"}},QJsonObject{{"id","cpu"},{"state","unidentified"},{"version","custom"}}}}},temp.path(),temp.path()+"/user-data","check",{"ui"});
  require(contextRequest.value("context").toObject().value("installed").toArray().isEmpty(),"Read-only catalogue comparison was blocked by a newer local build");
  const auto installContext=pgn::update::componentUpdateRequest(resolved,QJsonObject{{"components",QJsonArray{QJsonObject{{"id","ui"},{"state","matched_local_manifest"},{"version","1.0"}},QJsonObject{{"id","cpu"},{"state","unidentified"},{"version","custom"}}}}},temp.path(),temp.path()+"/user-data","prepare_install",{"ui"});
  require(installContext.value("context").toObject().value("installed").toArray().size()==1&&installContext.value("context").toObject().value("installed").toArray()[0].toObject().value("version")=="1.0","Installation lost its real verified anti-downgrade floor");
  require(contextRequest.value("context").toObject().value("protected_roots").toArray().contains(temp.path()+"/user-data"),"User data was not protected from staging");
  const auto recovery=pgn::update::componentUpdateRequest(resolved,{},temp.path(),temp.path()+"/user-data","recover",{});
  require(!recovery.contains("selected")&&!recovery.contains("activation")&&!recovery.contains("base_cohort"),"Recovery invented a new update selection/activation");
  require(recovery.value("installation_store").toString().endsWith("/installation"),"Recovery store is absent");
  pgn::update::ComponentUpdateService service(resolved);int results=0,errors=0;QString lastError;service.onResult=[&](const QJsonObject&){++results;};service.onError=[&](const QString& error){++errors;lastError=error;};
  const auto wait=[&]{QElapsedTimer clock;clock.start();while(service.busy()&&clock.elapsed()<10000){app.processEvents();QThread::msleep(5);}require(!service.busy(),"Helper did not settle");};
  const auto request=[](const QString& fixture=QString{}){return QJsonObject{{"action","check"},{"fixture",fixture}};};
  require(service.start(request()),"Valid helper request refused");require(!service.start(request()),"Concurrent operation was admitted");wait();require(results==1&&errors==0,"Successful helper response lost");
  for(const auto& fixture:{QString("failure"),QString("malformed"),QString("wrong-type"),QString("oversized")}){const auto before=errors;require(service.start(request(fixture)),"Fixture request refused");wait();require(errors==before+1&&!lastError.isEmpty(),"Bad helper response was accepted or completed twice");}
  require(service.start(request("delay")),"Cancellation fixture refused");app.processEvents();service.cancel();wait();require(lastError.contains("cancelled"),"Cancellation was not distinct");
  require(service.start(request()),"Request after cancellation refused");wait();require(results==2,"Cancellation poisoned the next request");
  service.onStatus=[&](const QString&){service.cancel();};require(service.start(request()),"Reentrant cancellation refused");wait();service.onStatus={};require(lastError.contains("cancelled"),"Status callback cancellation launched an unintended operation");
  require(!service.start(QJsonObject{{"action","execute"}}),"Arbitrary execution operation admitted");
  {pgn::update::ComponentUpdateService missing({temp.path()+"/missing.exe",{}});int failed=0;missing.onError=[&](const QString&){++failed;};require(missing.start(request()),"Failed-start fixture refused");QElapsedTimer clock;clock.start();while(missing.busy()&&clock.elapsed()<5000){app.processEvents();QThread::msleep(5);}require(!missing.busy()&&failed==1,"Failed start did not settle exactly once");}
  std::cout<<"PASS "<<checks<<" updater transport/lifecycle checks; inert helper only, no network/GPU/wallet/installed code touched\n";return 0;
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
