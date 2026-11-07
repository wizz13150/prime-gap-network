#include "wallet/confirmed_snapshot_anchor.h"
#include <QCoreApplication>
#include <iostream>
#include <stdexcept>
using namespace pgn::wallet;
int main(int argc,char** argv){QCoreApplication app(argc,argv);
 auto require=[](bool okay,const char* why){if(!okay)throw std::runtime_error(why);};
 const QString first(64,'a'),fork(64,'b');QJsonObject state{{"height",50},{"bestblockhash",first}};
 bool live=true;int calls=0,finished=0;qint64 requested=-1;QString error;std::function<void(QJsonValue,QString)> pending;
 auto call=[&](qint64 height,auto done){++calls;requested=height;pending=std::move(done);};
 auto done=[&](QString result){++finished;error=result;};
 verifyConfirmedSnapshotAnchor(state,[&]{return live;},call,done);require(calls==1&&finished==0&&requested==50,"Read was not asynchronous or pinned");
 // Extension changes the current tip, but the already captured block remains active.
 state["height"]=51;state["bestblockhash"]=fork;pending(first,{});require(finished==1&&error.isEmpty(),"Valid extension discarded a coherent older snapshot");
 auto original=QJsonObject{{"height",50},{"bestblockhash",first}};
 verifyConfirmedSnapshotAnchor(original,[&]{return live;},call,done);pending(fork,{});require(!error.isEmpty(),"Orphan anchor was accepted");
 verifyConfirmedSnapshotAnchor(original,[&]{return live;},call,done);live=false;pending(first,{});require(!error.isEmpty(),"Response crossed Core epoch");
 const int before=calls;verifyConfirmedSnapshotAnchor(original,[&]{return live;},call,done);require(calls==before&&!error.isEmpty(),"Inactive Core was queried");live=true;
 auto paged=original;paged["height"]=25;paged["tip_height"]=100;verifyConfirmedSnapshotAnchor(paged,[&]{return live;},call,done);require(requested==100,"Page top confused with snapshot anchor height");pending(first,{});require(error.isEmpty(),"Valid paginated snapshot refused");
 for(const QJsonValue& value:QJsonArray{-1,1.5,"50",QJsonValue(),2147483648.0}){auto bad=original;bad["height"]=value;const int n=calls;verifyConfirmedSnapshotAnchor(bad,[&]{return live;},call,done);require(calls==n&&!error.isEmpty(),"Invalid anchor height reached RPC");}
 for(const auto& hash:QStringList{QString(64,'0'),QString(64,'A'),"short"}){auto bad=original;bad["bestblockhash"]=hash;const int n=calls;verifyConfirmedSnapshotAnchor(bad,[&]{return live;},call,done);require(calls==n&&!error.isEmpty(),"Malformed anchor reached RPC");}
 verifyConfirmedSnapshotAnchor(original,[&]{return live;},call,done);pending(first,"Height no longer exists");require(error=="Height no longer exists","RPC failure was erased");
 verifyConfirmedSnapshotAnchor(original,[&]{return live;},call,done);pending(50,{});require(!error.isEmpty(),"Non-hash result accepted");
 std::cout<<"PASS coherent snapshot: advancing tip accepted, fork and epoch refused, paged anchor preserved, malformed and failed reads closed\n";
}
