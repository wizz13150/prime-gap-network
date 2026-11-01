#include "mining/source_age.h"
#include <QCoreApplication>
#include <QDateTime>
#include <iostream>
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);QJsonObject raw;
 raw.insert("original_utc","2099-01-01T00:00:00Z");
 pgn::mining::stampSourceAge(raw,1200000000,1000000000,5000000000);
 if(pgn::mining::sourceAgeLowerBoundNs(raw,5000000000)!=200000000)return 1;
 if(pgn::mining::sourceAgeLowerBoundNs(raw,5050000000)!=250000000)return 2;
 if(raw.value("original_utc")!="2099-01-01T00:00:00Z")return 3;
 if(pgn::mining::sourceAgeLowerBoundNs(raw,4999999999))return 4;
 pgn::mining::stampSourceAge(raw,999,1000,5000000000);
 if(pgn::mining::sourceAgeLowerBoundNs(raw,6000000000))return 5;
 if(raw.value("original_utc")!="2099-01-01T00:00:00Z")return 6;
 pgn::mining::stampSourceAge(raw,1000,1000,7000000000);
 if(pgn::mining::sourceAgeLowerBoundNs(raw,7000000100)!=100)return 7;
 std::cout<<"PASS cross-machine future UTC preserved, monotonic lower-bound age grows on missing samples, invalid clock refuses false zero/freshness\n";
}
