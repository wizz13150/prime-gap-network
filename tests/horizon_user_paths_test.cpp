#include "app/horizon_user_paths.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QUuid>
#include <thread>
#include <atomic>
#include <iostream>
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);app.setApplicationName("PGN-paths-"+QUuid::createUuid().toString(QUuid::WithoutBraces));QStandardPaths::setTestModeEnabled(true);
 QTemporaryDir temporary;if(!temporary.isValid())return 1;const auto a=temporary.path()+"/code-a",b=temporary.path()+"/code-b";QDir().mkpath(a);QDir().mkpath(b);
 const QString genesis(64,'d'),other(64,'e');const auto expected=pgn::app::resolvedMutableDataRoot(a,genesis,false);
 if(expected.isEmpty()||expected.startsWith(a)||QFileInfo::exists(expected))return 2;
 if(expected!=pgn::app::resolvedMutableDataRoot(b,genesis,false)||expected==pgn::app::resolvedMutableDataRoot(a,other,false))return 3;
 if(pgn::app::mutableDataRoot(a)!=a+"/data"||pgn::app::resolvedMutableDataRoot(a,genesis,true)!=a+"/data")return 4;
 if(!pgn::app::bindMutableDataRoot(a,genesis,false)||!pgn::app::bindMutableDataRoot(a,genesis,false))return 5;
 if(pgn::app::mutableDataRoot(a)!=expected||pgn::app::bindMutableDataRoot(b,genesis,false)||pgn::app::mutableDataRoot(a)!=expected)return 6;
 std::atomic<bool> good{true};std::thread one([&]{for(int n=0;n<100;++n)if(pgn::app::mutableDataRoot(a)!=expected)good=false;});std::thread two([&]{for(int n=0;n<100;++n)if(!pgn::app::bindMutableDataRoot(a,genesis,false))good=false;});one.join();two.join();
 if(!good||QFileInfo::exists(expected)||QFileInfo::exists(a+"/data")||QFileInfo::exists(b+"/data"))return 7;
 std::cout<<"PASS: code-folder relocation keeps user data, genesis isolation, explicit legacy paths, no writes, stable concurrent access.\n";
}
