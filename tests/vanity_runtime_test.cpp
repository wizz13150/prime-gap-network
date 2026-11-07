#include "ui/vanity_runtime.h"
#include "mining/ssh_snapshot_transport.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <iostream>
#include <functional>

int main(int argc,char** argv){
 QCoreApplication app(argc,argv);int checks=0;
 const auto require=[&](bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);};
 const auto rejects=[&](const std::function<void()>& op){bool caught=false;try{op();}catch(const std::exception&){caught=true;}require(caught,"Unsafe deployment was accepted");};
 try{
  QTemporaryDir temp;require(temp.isValid(),"Temporary root unavailable");const auto root=temp.path()+"/bundle",data=temp.path()+"/state";
  QDir().mkpath(root+"/EngineRuntime/vanity");QDir().mkpath(data);
  const auto write=[&](const QString& path,const QByteArray& bytes){QFile f(path);require(f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size(),"Fixture write failed");};
  const QByteArray inert("INERT: NEVER EXECUTED"),module("INERT: NEVER LOADED");
  const auto digest=[](const QByteArray& bytes){return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());};
  const auto name=QString(
#ifdef Q_OS_WIN
   "pgn-vanity-session.exe"
#else
   "pgn-vanity-session"
#endif
  );
  write(root+"/"+name,inert);write(root+"/EngineRuntime/vanity/pgn-vanity-gpu.dll",module);
  QJsonObject profile{{"schema","pgn-vanity-runtime-1"},{"executable",name},{"sha256",digest(inert)},{"local_gpu_module","EngineRuntime/vanity/pgn-vanity-gpu.dll"},{"local_gpu_module_sha256",digest(module)}};
  const auto save=[&](const QJsonObject& object,const QString& path){write(path,QJsonDocument(object).toJson());};
  const auto descriptor=root+"/EngineRuntime/vanity/runtime.json";save(profile,descriptor);
  const auto resolved=pgn::ui::loadVanityRuntime(root,data,root);require(resolved.value("executable")==root+"/"+name,"Portable helper path did not resolve");require(QDir::isAbsolutePath(resolved.value("local_gpu_module").toString()),"Portable GPU module did not resolve");
  require(pgn::ui::loadVanityRuntime(temp.path()+"/private-root",data,root)==resolved,"App resource fallback failed");
  auto wrong=profile;wrong.insert("sha256",QString(64,'0'));save(wrong,descriptor);rejects([&]{pgn::ui::loadVanityRuntime(root,data,root);});
  wrong=profile;wrong.insert("local_gpu_module_sha256",QString(64,'0'));save(wrong,descriptor);rejects([&]{pgn::ui::loadVanityRuntime(root,data,root);});
  wrong=profile;wrong.insert("executable","../"+name);write(temp.path()+"/"+name,inert);save(wrong,descriptor);rejects([&]{pgn::ui::loadVanityRuntime(root,data,root);});
  wrong=profile;wrong.insert("local_gpu_module","../../external.dll");save(wrong,descriptor);rejects([&]{pgn::ui::loadVanityRuntime(root,data,root);});
  save(profile,descriptor);save(QJsonObject{{"schema","invalid"}},data+"/vanity-runtime.json");rejects([&]{pgn::ui::loadVanityRuntime(root,data,root);});require(QFile::remove(data+"/vanity-runtime.json"),"Fixture remove failed");
  auto remote=profile;remote.insert("remote_executable","D:/Compute/vanity/"+name);remote.insert("remote_gpu_module","D:/Compute/vanity/pgn-vanity-gpu.dll");save(remote,descriptor);require(!pgn::ui::loadVanityRuntime(root,data,root).value("remote_executable").toString().isEmpty(),"Generic remote host path was refused");
  for(const auto& path:{QString("D:/Compute/../vanity/")+name,QString("D:/Compute/vanity/")+name+";echo",QString("D:/Compute/vanity/")+name+"\n"}){wrong=remote;wrong.insert("remote_executable",path);save(wrong,descriptor);rejects([&]{pgn::ui::loadVanityRuntime(root,data,root);});}
  const auto key=temp.path()+"/test-key";write(key,"INERT");
  pgn::mining::SshObservationEndpoint endpoint;endpoint.executable=QCoreApplication::applicationFilePath();endpoint.keyFile=key;endpoint.host="worker.example";endpoint.user="miner";endpoint.port=2222;endpoint.hostKeyAlias="qualified-worker";
  const auto command=pgn::mining::makeSshConnectionCommand(endpoint,"D:/Compute/vanity/"+name+" --cpu-inventory");
  require(command.arguments.contains("worker.example")&&command.arguments.contains("2222")&&command.arguments.contains("miner")&&command.arguments.contains("HostKeyAlias=qualified-worker")&&command.arguments.contains("StrictHostKeyChecking=yes"),"SSH endpoint was not preserved");
  endpoint.host="-oProxyCommand=bad";rejects([&]{pgn::mining::makeSshConnectionCommand(endpoint,"status");});endpoint.host="worker.example";
  rejects([&]{pgn::mining::makeSshConnectionCommand(endpoint,"status\nother");});
  std::cout<<"PASS "<<checks<<" portable deployment and generic SSH checks; inert files only, no key generation/GPU/Core/network execution\n";return 0;
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
