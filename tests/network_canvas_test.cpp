#include "ui/mining_canvas.h"
#include "telemetry.h"
#include <QApplication>
#include <memory>
#include <iostream>
int main(int argc,char** argv){
 QApplication app(argc,argv);QCoreApplication::setOrganizationName("PGN-QA");
 QCoreApplication::setApplicationName("network-observation-test");
 std::unique_ptr<QWidget> canvas(pgn::ui::createMiningCanvas());canvas->resize(1380,900);canvas->show();
 auto paint=[&]{app.processEvents();canvas->repaint();};paint();
 if(canvas->property("pgn_wallet_connected").toBool())return 1;
 FrontierTelemetrySnapshot lane;lane.gpuUuid="qa-gpu";lane.gpuName="QA GPU";lane.runId="source-only";
 lane.sourceMode="live";lane.authoritative=true;lane.observedUtc=QDateTime::currentDateTimeUtc();
 lane.state="running";lane.producerStatus="running";lane.e2ePositionsPerSecond=0;lane.powmPerSecond=0;
 lane.raw={{"legacy_block_height",9999},{"rpc_success",true},{"network_pps",1e15},{"legacy_miner_bound",true}};
 pgn::ui::presentDevice(canvas.get(),lane);paint();
 if(canvas->property("pgn_wallet_connected").toBool()||canvas->property("pgn_wallet_height").isValid())return 2;
 pgn::wallet::ChainObservation observation{{42,"134041921234567890",1},QString(64,'b'),0,
     QDateTime::currentDateTimeUtc(),false};
 pgn::ui::presentNetwork(canvas.get(),observation);paint();
 if(!canvas->property("pgn_wallet_connected").toBool()||canvas->property("pgn_wallet_height").toString()!="0")return 3;
 pgn::ui::presentDevice(canvas.get(),lane);paint();
 if(canvas->property("pgn_wallet_height").toString()!="0"||
    canvas->property("pgn_wallet_genesis").toString()!=observation.genesis)return 4;
 auto invalid=observation;invalid.genesis="invalid";invalid.height=9999;
 pgn::ui::presentNetwork(canvas.get(),invalid);paint();
 if(canvas->property("pgn_wallet_height").toString()!="0")return 5;
 observation.observedUtc=QDateTime::currentDateTimeUtc().addMSecs(-30001);
 pgn::ui::presentNetwork(canvas.get(),observation);paint();
 if(canvas->property("pgn_wallet_connected").toBool()||canvas->property("pgn_wallet_height").toString()!="0"||
    canvas->property("pgn_wallet_observed_utc").toDateTime()!=observation.observedUtc)return 6;
 pgn::ui::presentNetwork(canvas.get(),std::nullopt);paint();
 if(canvas->property("pgn_wallet_connected").toBool()||canvas->property("pgn_wallet_height").isValid())return 7;
 std::cout<<"PASS GPU source cannot declare a chain; valid zero, original stale timestamp and owner exit remain explicit\n";
}
