#include "science/mining_results.h"
#include "mining/source_history.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <sqlite3.h>
#include <iostream>
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);QTemporaryDir temp;const auto file=temp.filePath("history.sqlite");
    pgn::mining::SourceHistory source(file);const QString a(32,'a'),b(32,'b'),other(32,'c');
    const QByteArray first="[phase0-gpu] GAP merit=1.9 gap=6 lower=23 upper=29 verified=1\n";
    const QByteArray second="[phase0-gpu] GAP merit=1.8 gap=4 lower=37 upper=41 verified=1\n";
    source.append(a,0,first+second);source.append(b,0,first+"[phase0-gpu] GAP merit=2.1 gap=8 lower=89 upper=97 verified=1\n");
    source.append(other,0,"[phase0-gpu] GAP merit=2.2 gap=10 lower=139 upper=149 verified=1\n");
    const auto ticket=pgn::science::readMiningResults(file,{a,b},false,10,2.0);
    if(!ticket.error.isEmpty()||ticket.points!=3||ticket.best.size()!=3||ticket.best.first().gap!=8||ticket.atOrAboveTarget!=1||ticket.meritBands.value(1)!=2)return 1;
    const auto empty=pgn::science::readMiningResults(file,{},false,10,2.0);if(!empty.error.isEmpty()||empty.points!=0)return 2;
    const auto all=pgn::science::readMiningResults(file,{},true,30,2.0);if(!all.error.isEmpty()||all.points!=4||all.best.first().gap!=10||all.atOrAboveTarget!=2||all.rowsRead!=4)return 3;
    if(ticket.recent.first().gap!=8||all.recent.first().gap!=10)return 4;
    if(pgn::science::readMiningResults(file,{"bad"},false,10,2.0).error.isEmpty())return 5;
    std::atomic_bool stop{true};if(!pgn::science::readMiningResults(file,{a},false,10,2.0,&stop).cancelled)return 6;
    sqlite3* db{};sqlite3_open(file.toUtf8().constData(),&db);sqlite3_exec(db,"UPDATE native_plot_multiplicity_manifest SET points=99 WHERE axis='length'",nullptr,nullptr,nullptr);sqlite3_close(db);
    const auto poisoned=pgn::science::readMiningResults(file,{},true,10,2.0);if(poisoned.error.isEmpty()||poisoned.points||!poisoned.best.isEmpty())return 7;
    std::cout<<"Mining results PASS: ticket union deduplicated, empty ticket never becomes history, exact merit bands/target, best/recent, cancel and damaged cache refusal\n";
    return 0;
}
