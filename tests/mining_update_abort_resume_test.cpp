#include "ui/mining_update_abort_resume.h"
#include "ui/mining_worker_close_attempt.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QThread>
#include <iostream>
#include <stdexcept>
static void check(bool value,const char* error){if(!value)throw std::runtime_error(error);}
static bool until(std::function<bool()> value,int ms=4000){QElapsedTimer t;t.start();while(t.elapsed()<ms){QCoreApplication::processEvents();if(value())return true;QThread::msleep(2);}return false;}
static void begin(QObject& window){window.setProperty("pgn_update_shutdown_error",QString{});window.setProperty("pgn_closing",true);window.setProperty("pgn_update_shutdown",true);}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{
    const QJsonObject cpu3{{"kind","cpu"},{"device","CPU/test/0"},{"threads",3}},cpu4{{"kind","cpu"},{"device","CPU/test/0"},{"threads",4}},gpu{{"kind","gpu"},{"device","GPU-test"}};
    const auto merged=pgn::ui::mergeMiningUpdateChoices({cpu3,gpu},{cpu4});
    check(merged.size()==2&&merged.at(0)==cpu4&&merged.at(1)==gpu,"pending choices lost or stale CPU thread budget won");
    for(const auto& phase:QStringList{"Paused","Needs attention","Starting","Searching","Stopped"}){
        for(bool running:{false,true}){
            QJsonObject intents{{"GPU-test",QJsonObject{{"wanted",true},{"running",running},{"phase",phase}}}};
            auto captured=pgn::ui::captureMiningUpdateChoices({}, {cpu3},intents);
            check(captured.size()==2&&captured.at(1)==gpu,"requested ON was lost because backend was paused/failed/not running");
            intents["GPU-test"]=QJsonObject{{"wanted",false},{"running",running},{"phase",phase}};
            captured=pgn::ui::captureMiningUpdateChoices({gpu},{cpu3},intents);
            check(captured.size()==1&&captured.at(0)==cpu3,"latest OFF lost against pending ON or physical RUNNING");
        }
    }
    check(pgn::ui::captureMiningUpdateChoices({}, {},QJsonObject{{"GPU-test",QJsonObject{{"wanted","true"}}}}).isEmpty(),"non-boolean wanted granted automatic ON");
    check(argc==2,"Python test child path required");QObject window;
    pgn::ui::MiningUpdateAbortResume recovery(&window);int resumed=0;recovery.onAbort=[&]{++resumed;};
    pgn::ui::abortMiningUpdateShutdown(&window,"ordinary close failed");QCoreApplication::processEvents();check(resumed==0,"ordinary close granted automatic ON");
    begin(window);pgn::ui::abortMiningUpdateShutdown(&window,"update failed");check(resumed==0,"failure handler was reentered synchronously");
    check(until([&]{return resumed==1;}),"failed update did not request recovery");
    pgn::ui::abortMiningUpdateShutdown(&window,"duplicate failure");QCoreApplication::processEvents();check(resumed==1,"duplicate failure replayed ON");
    begin(window);pgn::ui::abortMiningUpdateShutdown(&window,"old attempt");begin(window);QCoreApplication::processEvents();check(resumed==1,"old callback crossed into a new update");
    pgn::ui::abortMiningUpdateShutdown(&window,"new attempt");check(until([&]{return resumed==2;}),"new attempt did not recover once");
    begin(window);pgn::ui::abortMiningUpdateShutdown(&window,"user closes");window.setProperty("pgn_closing",true);QCoreApplication::processEvents();check(resumed==2,"user close was overridden by automatic ON");
    begin(window);window.setProperty("pgn_update_shutdown",false);window.setProperty("pgn_closing",false);QCoreApplication::processEvents();check(resumed==2,"successful shutdown granted abort recovery");
    QJsonArray choices{QJsonObject{{"kind","gpu"},{"device","GPU-fixture"}}};int onRequests=0;
    recovery.onAbort=[&]{for(const auto& ignored:choices){Q_UNUSED(ignored);++onRequests;}};
    begin(window);pgn::ui::abortMiningUpdateShutdown(&window,"manual OFF before queued recovery");choices={};QCoreApplication::processEvents();check(onRequests==0,"manual OFF was lost");
    // Use the actual asynchronous process-close failure path as well.
    QProcess child;child.start(QString::fromLocal8Bit(argv[1]),{"-I","-c","import sys; sys.stdin.readline(); sys.exit(7)"});check(child.waitForStarted(3000),"test child did not start");
    pgn::ui::MiningWorkerCloseAttempt close(&window,&child);int failures=0;close.onFailure=[&](const QString&){++failures;};
    recovery.onAbort=[&]{++resumed;};begin(window);check(!close.requestClose(true),"live child bypassed drain");
    check(until([&]{return failures==1&&resumed==3&&child.state()==QProcess::NotRunning;}),"real child failure did not recover after exit");
    check(!window.property("pgn_closing").toBool()&&!window.property("pgn_update_shutdown").toBool(),"aborted update retained close authority");
    std::cout<<"PASS deferred recovery, manual OFF, stale attempt, ordinary close and actual child exit7; no GPU/network/wallet\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
