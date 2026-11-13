#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <iostream>
#include "booking/booking_store.h"
int main(int n,char**v){
 QCoreApplication app(n,v);QTemporaryDir fixture;if(!fixture.isValid())return 2;
 auto root=fixture.path();while(root.size()<320)root+="/booking-\u00e9-012345678901234567890";
 if(!QDir().mkpath(root))return 3;const auto path=root+"/state.sqlite";
 const QString gpu="GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac";
 {
  pgn::booking::BookingStore db(path);
  if(!db.valid()||db.completedPositions()!="0"||!db.setAutomaticAfterManual(gpu,true).isEmpty()){std::cerr<<db.error().toStdString()<<std::endl;return 4;}
 }
 {
  pgn::booking::BookingStore reopened(path);
  if(!reopened.valid()||!reopened.automaticAfterManual(gpu).value_or(false))return 5;
 }
 pgn::booking::BookingStore unavailable(root);
 if(unavailable.valid()||unavailable.error().isEmpty()||!unavailable.completedPositions().isEmpty())return 6;
 std::cout<<"PASS long Unicode WAL creation, exact empty count, committed metadata reopen, explicit unavailable database"<<std::endl;
}
