#include <QCoreApplication>
#include <QProcess>
#include <QDir>
#include <QTemporaryDir>
#include <QFileInfo>
#include <iostream>
#include <cstdlib>
#include "app/horizon_ui_lease.h"
int main(int argc,char**argv){
 QCoreApplication app(argc,argv);const auto args=app.arguments();
 if(args.size()==3&&(args[1]=="--child"||args[1]=="--die")){
  pgn::app::HorizonUiLease lease(args[2]);if(!lease.owned())return 17;
  std::cout<<"owned"<<std::endl;
  if(args[1]=="--die")std::_Exit(0);return 0;
 }
 if(args.size()!=1)return 2;QTemporaryDir fixture;if(!fixture.isValid())return 3;
 const auto root=fixture.path();
 auto directory=root;while(directory.size()<330)directory+="/unicode-\u00e9\u00e8-ownership-0123456789";
 const auto file=directory+"/application.lock";const auto check=[&](bool okay,const char* text){if(!okay){std::cerr<<text<<std::endl;std::exit(1);}};
 {
  pgn::app::HorizonUiLease first(file);if(!first.owned()){std::cerr<<"long-path ownership refused: "<<first.error().toStdString()<<std::endl;return 1;}
  pgn::app::HorizonUiLease second(file);check(!second.owned()&&second.error().isEmpty(),"Two leases owned the same data scope");
  QProcess child;child.start(app.applicationFilePath(),{"--child",file});check(child.waitForFinished(10000)&&child.exitCode()==17,"Another process acquired an already owned scope");
 }
 {pgn::app::HorizonUiLease after(file);check(after.owned(),"Lease did not release after normal closure");}
 QProcess dying;dying.start(app.applicationFilePath(),{"--die",file});check(dying.waitForFinished(10000)&&dying.exitCode()==0&&dying.readAllStandardOutput().trimmed()=="owned","Process-death witness did not acquire before exiting");
 {pgn::app::HorizonUiLease after(file);check(after.owned(),"Kernel lease survived process death");}
 std::cout<<"PASS Unicode long path, same-process contention, cross-process contention, normal release, process-death release"<<std::endl;
 return 0;
}
