#include "ui/gap_flow_model.h"
#include "ui/gap_flow_motion.h"
#include "ui/mining_ticket_progress.h"
#include "ui/mining_cycle_progress.h"
#include "ui/ticket_flow_evidence.h"
#include <QApplication>
#include <QVBoxLayout>
#include <QImage>
#include <QDir>
#include <QEventLoop>
#include <QPushButton>
#include <QLineF>
#include <QTableWidget>
#include <iostream>
#include <stdexcept>
using namespace pgn::ui;
static void require(bool yes,const char* message){if(!yes)throw std::runtime_error(message);}
static QJsonObject row(int n,double merit){return {{"lower_prime",QString::number(19983382423573504ULL)+QString::number(10000+n)},
    {"gap",1000+n*2},{"merit",QString::number(merit,'g',16)}};}
static QJsonObject frame(QJsonArray rows={}){return {{"schema","PGN_HORIZON_MINING_VIEW_V1"},{"ticket_id",QString(64,'a')},
    {"title","Ticket 857"},{"bits",68},{"target_merit",28.5},{"record_pairs",rows},{"phase","Searching"},
    {"work_role","search"},{"start","150000000000000000000"},{"end","150000200000000000000"},
    {"positions_total","200000000000000"},{"positions","100000000000000"},{"speed_rate",1e12},{"eta_rate",1e12}};}
static void resourceActivity(){
 ResourceFlowActivity activity;QJsonObject status{{"enabled",true},{"compute_started",true},{"positions_per_second",2e9}};
 activity.observe("CPU/ALEX/0",status,true,0);require(activity.moving(),"Active CPU has no independent activity stream");
 activity.observe("CPU/WIZZ/0",status,true,0);require(activity.rows().size()==2,"CPU sources collapsed into one producer");
 auto off=status;off.insert("enabled",false);activity.observe("CPU/ALEX/0",off,true,10);
 require(!activity.rows()["CPU/ALEX/0"].moving&&activity.rows()["CPU/WIZZ/0"].moving,"CPU OFF suppressed the other device or stayed active");
 activity.observe("CPU/WIZZ/0",status,false,11);require(!activity.moving(),"Disconnected CPU emits activity");
 activity.observe("CPU/ALEX/0",status,true,20);activity.expire(3021);require(!activity.moving(),"Stale CPU telemetry emits activity");
 auto noRate=status;noRate.insert("positions_per_second",QJsonValue{});activity.observe("CPU/ALEX/0",noRate,true,3030);require(!activity.moving(),"Unknown CPU speed was invented");
 auto retained=status;retained.insert("compute_started",false);retained.insert("resident_result_handoff",true);retained.insert("resident_workers",4);retained.insert("resource_dispatch_threads",4);retained.insert("resource_positions_per_second",2e9);activity.observe("CPU/ALEX/0",retained,true,3031);require(!activity.moving(),"Retained average hid real CPU idle");
 for(int i=0;i<30;++i)activity.observe("CPU/EXTRA/"+QString::number(i),status,true,4000);require(activity.rows().size()==8,"Resource activity cache is unbounded");
}
static void resourceWorkSelection(){
 MiningTicketProgress rail;rail.resize(1366,120);QJsonObject selected;int changes=0;
 rail.onWorkSelected=[&](const QJsonObject& value){selected=value;++changes;};
 auto gpu=frame();gpu.insert("ticket_id",QString(64,'a'));gpu.insert("positions","100000000000000");rail.present(gpu);
 auto cpu=frame();cpu.insert("source_device","CPU/ALEX/0");cpu.insert("ticket_id",QString(64,'b'));cpu.insert("positions","1000");cpu.insert("positions_total","10000");rail.present(cpu);
 require(changes==1&&selected.value("ticket_id")==QString(64,'a'),"Background CPU overwrote selected GPU work");
 require(rail.selectWorkSource("CPU/ALEX/0")&&selected.value("positions")=="1000","CPU selection did not restore its own snapshot");
 auto pausedGpu=gpu;pausedGpu.insert("phase","Paused");pausedGpu.insert("positions","110000000000000");rail.present(pausedGpu);
 require(selected.value("ticket_id")==QString(64,'b')&&selected.value("phase")=="Searching","GPU control/recovery overwrote selected CPU work");
 require(rail.selectWorkSource("GPU")&&selected.value("positions")=="110000000000000"&&selected.value("phase")=="Paused","GPU selection did not retain its updated snapshot");
 require(rail.selectWorkSource("CPU/ALEX/0")&&selected.value("ticket_id")==QString(64,'b'),"Switching mixed resource identities");
 require(!rail.selectWorkSource("CPU/UNKNOWN/0"),"Unknown resource borrowed another ticket");
 auto* selector=rail.findChild<QComboBox*>("pgn-work-source");require(selector&&selector->count()==2,"Work selector duplicated or lost resources");
 require(selector->geometry().bottom()<rail.height(),"Work selection added another header row");
}
static void storageWaitPresentation(){
 for(int kind:{0,1,2}){
  QWidget canvas,results;canvas.setProperty("pgn_network_desired_on",true);
  QTableWidget table(&results);table.setObjectName("pgn-results-best");table.setColumnCount(1);table.setRowCount(1);
  table.setItem(0,0,new QTableWidgetItem("retained result"));
  MiningTicketProgress rail;rail.bindSourceCanvas(&canvas);rail.bindResultsView(&results);rail.resize(1366,120);
  QJsonObject meta{{"schema","PGN_MINING_CYCLE_VIEW_V1"},{"request_id",QString(64,'f')},
   {"ticket_number","857"},{"search_positions","1800"},{"active_index",0},
   {"audits",QJsonArray{QJsonObject{{"state","assigned"},{"positions","100"}},QJsonObject{{"state","assigned"},{"positions","100"}}}}};
  auto packet=frame();packet.insert("work_role","audit");packet.insert("positions_total","100");packet.insert("positions","100");packet.insert("eta_rate",10.);
  const auto present=[&]{packet.insert("audit_service",QJsonObject{{"cycle",meta}});rail.present(packet);};
  if(kind!=1){
   packet.insert("ticket_id",QString(64,'1'));present();
   meta.insert("active_index",1);packet.insert("ticket_id",QString(64,'2'));present();
   meta.insert("active_index",2);
   if(kind==2){
    packet.insert("ticket_id",QString(64,'4'));packet.insert("work_role","search");
    packet.insert("positions_total","1800");packet.insert("positions","900");present();
   }
  }
  packet.insert("ticket_id",QString(64,kind==1?'1':kind==2?'3':'4'));
  packet.insert("work_role",kind==0?"search":"audit");
  packet.insert("positions_total",kind==0?"1800":"100");packet.insert("positions",kind==0?"900":"50");
  if(kind==2)meta.insert("catch_up",true);
  present();
  const auto paint=[&]{QImage image(rail.size(),QImage::Format_ARGB32);image.fill(Qt::transparent);rail.render(&image);return image;};
  const auto before=paint();
  const auto fraction=rail.property("pgn_ticket_progress_fraction").toDouble();
  const auto cycleFraction=rail.property("pgn_ticket_cycle_fraction").toDouble();
  const auto presentation=rail.property("pgn_ticket_work_presentation").toJsonObject();
  require(rail.property("pgn_ticket_cycle_eta_seconds").isValid(),"Storage fixture must begin with a measured cycle ETA");
  auto waiting=packet;waiting.insert("phase","Waiting for storage");waiting.insert("message","Data storage is busy. Work is saved; retrying automatically.");
  rail.present(waiting);const auto after=paint();
  const auto waitingPresentation=rail.property("pgn_ticket_work_presentation").toJsonObject();
  require(waitingPresentation.value("phase")=="Waiting for storage"&&waitingPresentation.value("status").toString().startsWith("Waiting for storage"),"Known cycle hid the explicit storage-wait phase");
  require(!rail.property("pgn_ticket_eta_seconds").isValid()&&!rail.property("pgn_ticket_cycle_eta_seconds").isValid()&&waitingPresentation.value("remaining").toString().isEmpty(),"Storage wait retained a stale ETA or fabricated a zero duration");
  require(rail.property("pgn_ticket_progress_fraction").toDouble()==fraction&&rail.property("pgn_ticket_cycle_fraction").toDouble()==cycleFraction,"Storage wait changed measured progress");
  require(waitingPresentation.value("quantity")==presentation.value("quantity")&&waitingPresentation.value("title")==presentation.value("title"),"Storage wait changed work identity/quantity");
  require(before.copy(QRect(14,83,rail.width()-28,3))==after.copy(QRect(14,83,rail.width()-28,3)),"Storage wait changed the painted audit/search segments");
  require(table.rowCount()==1&&table.item(0,0)->text()=="retained result"&&canvas.property("pgn_network_desired_on").toBool(),"Storage notice cleared results or requested OFF");
  rail.present(packet);paint();
  require(rail.property("pgn_ticket_cycle_eta_seconds").isValid()&&rail.property("pgn_ticket_work_presentation").toJsonObject().value("phase")!="Waiting for storage","Fresh work did not restore the measured ETA/status");
 }
 std::cout<<"PASS storage-wait actual renderer: search/regular audit/catch-up, retained fraction/segments/results/ON, hidden stale ETA and fresh resumption; no image files or GPU/Core\n";
}
int main(int argc,char** argv){
 QApplication app(argc,argv);try{storageWaitPresentation();resourceActivity();resourceWorkSelection();}catch(const std::exception& error){std::cerr<<"FAIL storage/resource presentation: "<<error.what()<<"\n";return 77;}GapFlowModel model;auto old=row(1,29.);auto gold=row(2,30.);
 QJsonObject cycleMeta{{"schema","PGN_MINING_CYCLE_VIEW_V1"},{"request_id",QString(64,'f')},{"ticket_number","857"},{"search_positions","1800"},{"active_index",0},{"audits",QJsonArray{QJsonObject{{"state","assigned"},{"positions","100"}},QJsonObject{{"state","assigned"},{"positions","100"}}}}};
 auto cycleFrame=frame();cycleFrame.insert("work_role","audit");cycleFrame.insert("positions_total","100");cycleFrame.insert("positions","50");cycleFrame.insert("eta_rate",10);cycleFrame.insert("audit_service",QJsonObject{{"cycle",cycleMeta}});auto firstCycle=miningCycleProgress(cycleFrame);
 require(firstCycle.known&&std::abs(firstCycle.fraction-.025)<1e-12&&firstCycle.taskEta==5&&firstCycle.cycleEta==195,"First audit uses first5percent and complete measured ETA");
 auto auditStates=cycleMeta["audits"].toArray();auto firstState=auditStates[0].toObject();firstState.insert("state","committed");auditStates[0]=firstState;cycleMeta.insert("audits",auditStates);cycleMeta.insert("active_index",1);cycleFrame.insert("positions","0");cycleFrame.insert("audit_service",QJsonObject{{"cycle",cycleMeta}});require(std::abs(miningCycleProgress(cycleFrame).fraction-.05)<1e-12,"Second audit starts at5percent without reset");
 auto secondState=auditStates[1].toObject();secondState.insert("state","committed");auditStates[1]=secondState;cycleMeta.insert("audits",auditStates);cycleMeta.insert("active_index",2);cycleFrame.insert("positions_total","1800");cycleFrame.insert("positions","900");cycleFrame.insert("work_role","search");cycleFrame.insert("audit_service",QJsonObject{{"cycle",cycleMeta}});const auto searchCycle=miningCycleProgress(cycleFrame);require(std::abs(searchCycle.fraction-.55)<1e-12&&searchCycle.cycleEta==90,"Search occupies last90percent, preserves completed audit slices");
 // Native service credits already fill both regular slots. An actual extra
 // audit must keep its orange slot even when it is the first observed job.
 {
  auto nativeMeta=cycleMeta;nativeMeta.insert("catch_up",true);nativeMeta.insert("active_index",0);
  auto nativeFrame=cycleFrame;nativeFrame.insert("work_role","audit");nativeFrame.insert("ticket_id","native-extra");nativeFrame.insert("positions_total","100");nativeFrame.insert("positions","50");nativeFrame.insert("audit_service",QJsonObject{{"cycle",nativeMeta}});
  MiningCycleHistory nativeHistory;nativeHistory.observe(nativeFrame);auto native=miningCycleProgress(nativeFrame,nativeHistory.audits());
  require(native.slices.size()==4&&native.slices[0].fraction==1&&native.slices[1].fraction==1&&native.slices[2].role=="catch_up"&&native.slices[2].fraction==.5,"Native catch-up replaced a completed regular service slot");
  nativeFrame.insert("positions","100");nativeHistory.observe(nativeFrame);nativeMeta.insert("catch_up",false);nativeMeta.insert("active_index",2);nativeFrame.insert("work_role","search");nativeFrame.insert("positions_total","1800");nativeFrame.insert("positions","900");nativeFrame.insert("audit_service",QJsonObject{{"cycle",nativeMeta}});nativeHistory.observe(nativeFrame);native=miningCycleProgress(nativeFrame,nativeHistory.audits(),nativeHistory.search());
  require(native.slices.size()==4&&native.slices[2].role=="catch_up"&&native.slices[2].fraction==1&&native.slices[3].fraction==.5,"Native completed catch-up disappeared when search resumed");
 }
 // Arrival order is not admission authority: an unrelated previous audit
 // cannot become a second orange slice in a cycle with one admitted extra.
 {
  auto explicitMeta=cycleMeta;explicitMeta.insert("extra_audit_ids",QJsonArray{"real-extra"});
  auto explicitFrame=cycleFrame;explicitFrame.insert("audit_service",QJsonObject{{"cycle",explicitMeta}});
  const QJsonArray observed{QJsonObject{{"identity","previous-audit"},{"catch_up",true},{"fraction",1.}},QJsonObject{{"identity","real-extra"},{"catch_up",true},{"fraction",1.}}};
  const auto explicitCycle=miningCycleProgress(explicitFrame,observed);
  require(explicitCycle.slices.size()==4&&explicitCycle.slices[2].role=="catch_up","Frame order manufactured a second catch-up despite the execution journal");
  explicitMeta.insert("completed_extra_audit_ids",QJsonArray{"real-extra","not-admitted"});
  explicitFrame.insert("audit_service",QJsonObject{{"cycle",explicitMeta}});
  const auto durableExtra=miningCycleProgress(explicitFrame);
  require(durableExtra.slices.size()==4&&durableExtra.slices[2].fraction==1.,"Confirmed extra stays complete after missed final frame or attachment; unadmitted completion cannot manufacture a slot");
  explicitMeta.insert("active_audit_id","real-extra");explicitFrame.insert("work_role","audit");explicitFrame.insert("ticket_id","previous-audit");explicitFrame.insert("audit_service",QJsonObject{{"cycle",explicitMeta}});
  require(!coherentMiningCycleFrame(explicitFrame),"Cross-job audit frame accepted a different active audit's cycle metadata");
 }
 // Laboratory catch-up work must occupy the first two real audit slots,
 // even when the server waived its independent-service credit stages.
 cycleMeta.insert("catch_up",true);cycleMeta.insert("audits",QJsonArray{QJsonObject{{"state","unavailable"}},QJsonObject{{"state","unavailable"}}});
 cycleFrame.insert("work_role","audit");cycleFrame.insert("positions_total","100");cycleFrame.insert("positions","50");cycleFrame.insert("ticket_id","actual-audit-one");cycleFrame.insert("audit_service",QJsonObject{{"cycle",cycleMeta}});
 MiningCycleHistory history;history.observe(cycleFrame);auto physical=miningCycleProgress(cycleFrame,history.audits());
 require(physical.slices.size()==3&&physical.slices[0].fraction==.5&&physical.slices[1].fraction==0&&std::abs(physical.fraction-.025)<1e-12,"Actual first audit fills first slot despite waived server credits");
 cycleFrame.insert("positions","100");history.observe(cycleFrame);cycleFrame.insert("ticket_id","actual-audit-two");cycleFrame.insert("positions","50");history.observe(cycleFrame);physical=miningCycleProgress(cycleFrame,history.audits());
 require(physical.slices.size()==3&&physical.slices[0].fraction==1&&physical.slices[1].fraction==.5&&std::abs(physical.fraction-.075)<1e-12,"Actual second audit fills second slot instead of a transient orange slot");
 cycleFrame.insert("positions","100");history.observe(cycleFrame);cycleFrame.insert("ticket_id","actual-audit-extra");cycleFrame.insert("positions","50");history.observe(cycleFrame);physical=miningCycleProgress(cycleFrame,history.audits());
 require(physical.slices.size()==4&&physical.slices[2].role=="catch_up"&&physical.slices[2].fraction==.5,"Only third actual audit becomes an extra orange slot");
 history.observe(cycleFrame);require(history.audits().size()==3,"Repeated physical frame duplicated an audit slot");
 cycleFrame.insert("positions","100");history.observe(cycleFrame);cycleMeta.insert("catch_up",false);cycleMeta.insert("active_index",2);cycleFrame.insert("work_role","search");cycleFrame.insert("positions_total","1800");cycleFrame.insert("positions","900");cycleFrame.insert("audit_service",QJsonObject{{"cycle",cycleMeta}});history.observe(cycleFrame);const auto retained=miningCycleProgress(cycleFrame,history.audits());
 require(retained.slices.size()==4&&retained.slices[0].fraction==1&&retained.slices[1].fraction==1&&retained.slices[2].fraction==1&&retained.slices[2].role=="catch_up","All three completed physical audit slots remain visible throughout search");
 cycleFrame.insert("positions","1404");history.observe(cycleFrame);
 cycleMeta.insert("catch_up",true);cycleFrame.insert("work_role","audit");cycleFrame.insert("ticket_id","actual-audit-four");cycleFrame.insert("positions_total","100");cycleFrame.insert("positions","50");cycleFrame.insert("audit_service",QJsonObject{{"cycle",cycleMeta}});history.observe(cycleFrame);
 const auto interrupted=miningCycleProgress(cycleFrame,history.audits(),history.search());
 require(std::abs(interrupted.slices.last().fraction-.78)<1e-12,"Catch-up audit erased the search prefix already computed");
 require(interrupted.cycleEta&&std::abs(*interrupted.cycleEta-44.6)<1e-12,"Catch-up ETA counted the entire search again");
 cycleFrame.insert("positions","75");history.observe(cycleFrame);require(history.audits().size()==4&&history.search().value("fraction").toDouble()==.78,"Audit refresh duplicated a slot or replaced search progress");
 cycleMeta.insert("request_id",QString(64,'e'));cycleFrame.insert("audit_service",QJsonObject{{"cycle",cycleMeta}});history.observe(cycleFrame);require(history.audits().size()==1&&history.search().isEmpty(),"A new cycle inherited old audit or search work");
 const QPointF entrance(100,10),destination(400,80);
 require(livingFlowPoint(entrance,destination,0,.2,1,40)==entrance&&livingFlowPoint(entrance,destination,1,.2,1,40)==destination,"Living motion lost its destination");
 require(QLineF(livingFlowPoint(entrance,destination,.5,.2,1,40),livingFlowPoint(entrance,destination,.5,.7,1,40)).length()>1,"All entities follow the same route");
 require(QLineF(livingFlowPoint(entrance,destination,.5,.2,1,40),livingFlowPoint(entrance,destination,.5,.2,3,40)).length()>1,"A passage replays a fixed spline");
 for(int seed=0;seed<100;++seed)for(int step=0;step<=100;++step){const auto point=livingFlowPoint(entrance,destination,step/100.,seed/100.,17.3,40);require(std::isfinite(point.x())&&std::isfinite(point.y())&&point.x()>50&&point.x()<450&&point.y()>-50&&point.y()<140,"Procedural motion escaped its visual bound");}
 GapFlowModel auditFirst;auto firstAudit=frame();firstAudit.insert("work_role","audit");auditFirst.observe(firstAudit,"gpu",0);auditFirst.observe(frame({gold}),"gpu",10);require(auditFirst.received()==0,"First search after audit replayed historical results");
 model.observe(frame({old}),"gpu",0);require(model.received()==0,"Existing results replayed as discoveries");
 model.observe(frame({old,gold}),"gpu",100);require(model.received()==1&&model.events().size()==1,"A new event was not isolated");
 require(model.events()[0].tier==MeritTier::Gold&&!model.record(model.events()[0]),"Award/record boundary wrong");
 model.observe(frame({old,gold}),"gpu",200);require(model.received()==1,"Repeated refresh duplicated a discovery");
 model.setEligibility({gold});require(model.record(model.events()[0]),"Verified eligibility did not enable star");
 model.setEligibility({});require(!model.record(model.events()[0]),"Withdrawn eligibility kept a star");
 auto snapshot=[&](QJsonArray rows,QString ticket=QString(64,'a')){for(auto it=rows.begin();it!=rows.end();++it){auto object=(*it).toObject();object.insert("merit",object.value("merit").toString().toDouble());*it=object;}return QJsonObject{{"ticket_id",ticket},{"target_merit",28.5},{"rows",rows}};};
 GapFlowModel coinCandidates;const QJsonArray mixed{row(50,24.),row(51,26.),row(52,27.),row(53,28.),row(54,29.)};
 coinCandidates.observe(frame(),"gpu",0);coinCandidates.observe(frame(mixed),"gpu",100);
 coinCandidates.setVisibleBest(snapshot(mixed),250);
 require(coinCandidates.collected().size()==4,"Ordinary visible Best entry entered the coin-candidate collection");
 require(std::all_of(coinCandidates.collected().begin(),coinCandidates.collected().end(),[](const auto& node){return node.gap.tier!=MeritTier::Ordinary;}),"Coin-candidate collection contains an ordinary gap");
 require(coinCandidates.events().size()==5&&coinCandidates.events().first().tier==MeritTier::Ordinary,"Filtering the collection erased the ordinary discovery's sink flight");
 model.setVisibleBest(snapshot({old,gold}),250);require(model.collected().size()==2,"Visible Best entries were not collected");
 model.expire(20000);require(model.collected().size()==2,"Current ticket collection expired on a timer");
 model.setVisibleBest(snapshot({gold}),20001);require(model.collected().size()==1&&model.departing().size()==1,"Visible-row reduction did not produce a transition");
 model.expire(22000);auto nextTicket=frame();nextTicket.insert("ticket_id",QString(64,'b'));model.observe(nextTicket,"gpu",23000);
 require(model.collected().isEmpty()&&model.departing().size()==1&&model.departing()[0].ticketTransition,"Ticket switch abruptly erased its visible discoveries");
 model.setVisibleBest(snapshot({old}),23001);require(model.collected().isEmpty(),"Old table snapshot leaked into the next ticket");
 model.expire(26000);require(model.departing().size()==1,"Old-ticket discoveries vanished before their report handoff");
 model.expire(23000+GapFlowModel::reportHandoffDuration+1);
 require(model.departing().isEmpty(),"Disconnected Core stranded old-ticket particles indefinitely");
 require(model.reportConfirmed(GapFlowEvent{QString(64,'a')+":pair","pair"})<0,"Presentation handoff fabricated report confirmation");
 model.confirmReport(QString(64,'a'),28000);
 GapFlowModel inFlight;inFlight.observe(frame(),"gpu",0);inFlight.observe(frame({gold}),"gpu",10);inFlight.observe(nextTicket,"gpu",300);
 require(inFlight.events().isEmpty()&&inFlight.departing().size()==1&&inFlight.departing().first().flight.has_value(),"In-flight old-ticket gap still uses its normal expiry in the new ticket");
 auto audit=frame({row(3,33.)});audit.insert("work_role","audit");model.observe(audit,"gpu",300);
 require(model.received()==1&&model.audit(),"Audit produced a personal discovery");
 auto paused=frame();paused.insert("phase","Paused");model.observe(paused,"gpu",400);require(!model.moving(),"Paused work implies motion");
 model.observe(frame(),"gpu",500);model.expire(3501);require(!model.moving(),"Old source keeps emitting activity");
 GapFlowModel saturated;saturated.observe(frame(),"gpu",0);QJsonArray many;for(int i=0;i<500;++i)many.append(row(i,22.));
 saturated.observe(frame(many),"gpu",1);require(saturated.events().size()==GapFlowModel::maximumEvents&&saturated.coalesced()>0,"Animation memory not bounded");
 saturated.expire(12000);require(saturated.events().isEmpty(),"Expired presentation objects leaked");
 QWidget parent;parent.setAutoFillBackground(true);auto palette=parent.palette();palette.setColor(QPalette::Window,QColor("#031116"));parent.setPalette(palette);auto* layout=new QVBoxLayout(&parent);auto* canvas=new QWidget;canvas->setFixedHeight(120);
 canvas->setProperty("pgn_managed_source_device","gpu");canvas->setProperty("pgn_card_layout",QJsonArray{
    QJsonObject{{"id","gpu"},{"visible",true},{"rect",QJsonArray{10,4,240,110}}},
    QJsonObject{{"id","CPU/ALEX/0"},{"visible",true},{"rect",QJsonArray{270,4,240,90}}}});
 auto* button=new QPushButton("Existing control",canvas);button->setGeometry(15,10,100,24);
 auto* rail=new MiningTicketProgress;layout->addWidget(canvas);layout->addWidget(rail);layout->addStretch();
 auto* results=new QWidget(&parent);auto* table=new QTableWidget(results);table->setObjectName("pgn-results-best");table->setColumnCount(1);table->setRowCount(1);auto* item=new QTableWidgetItem("1004");const auto pair=gold.value("lower_prime").toString()+":1004";item->setData(Qt::UserRole,pair);table->setItem(0,0,item);results->setProperty("pgn_visible_best",snapshot({gold,row(54,28.),row(53,27.),row(52,26.)}));results->hide();rail->bindResultsView(results);
 rail->bindSourceCanvas(canvas);parent.resize(1100,400);parent.show();app.processEvents();
 rail->present(frame());rail->present(frame({gold,row(54,28.),row(53,27.),row(52,26.)}));rail->setRecordEligibility({gold});
 rail->presentResourceActivity("CPU/ALEX/0",{{"enabled",true},{"compute_started",true},{"positions_per_second",2e9}},true);
 QEventLoop measured;QTimer::singleShot(1950,&measured,&QEventLoop::quit);measured.exec();
 for(int width:{640,1100,1920}){
  parent.resize(width,400);app.processEvents();QImage image(parent.size(),QImage::Format_ARGB32);image.fill(Qt::transparent);parent.render(&image);
  require(rail->property("pgn_ticket_progress_fraction").toDouble()==.5,"Animation advanced real progress");
  require(rail->property("pgn_ticket_eta_seconds").toDouble()==100.,"Animation changed ETA");
  require(rail->height()<=156,"Animation exceeded twice the original reserved height");
  auto* overlay=parent.findChild<QWidget*>("pgn-card-gap-flow");require(overlay&&overlay->testAttribute(Qt::WA_TransparentForMouseEvents),"Overlay blocks card controls");
  const auto resourceFlows=rail->property("pgn_ticket_resource_flows").toJsonArray();require(resourceFlows.size()==1&&resourceFlows[0].toObject().value("active")==true&&resourceFlows[0].toObject().value("device")=="CPU/ALEX/0","Concurrent CPU card has no activity flow");
  require(std::abs(resourceFlows[0].toObject().value("x").toDouble()-390.)<30,"CPU flow originates outside its own card");
  if(argc>1){QDir().mkpath(QString::fromLocal8Bit(argv[1]));require(image.save(QString::fromLocal8Bit(argv[1])+QString("/flow-%1.png").arg(width)),"Render receipt failed");}
 }
 QEventLoop collected;QTimer::singleShot(1900,&collected,&QEventLoop::quit);collected.exec();app.processEvents();
 const auto hits=rail->property("pgn_ticket_collection_hits").toJsonArray();require(!hits.isEmpty(),"Persistent collection has no interactive entity");
 const auto hit=hits.first().toObject();const QPointF point(hit.value("x").toDouble(),hit.value("y").toDouble());
 QMouseEvent click(QEvent::MouseButtonPress,point,rail->mapToGlobal(point.toPoint()),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(rail,&click);
 require(table->currentRow()==0&&table->item(0,0)->isSelected(),"Clicking a gap did not select its real Best row");
 if(argc>1){QImage image(parent.size(),QImage::Format_ARGB32);parent.render(&image);require(image.save(QString::fromLocal8Bit(argv[1])+"/flow-collection.png"),"Collection render failed");}
 rail->present(paused);app.processEvents();QImage stopped(parent.size(),QImage::Format_ARGB32);parent.render(&stopped);
 require(!rail->property("pgn_ticket_animation_active").toBool(),"Pause is not reflected in the live rail");
 require(rail->toolTip().isEmpty(),"Unwanted progress tooltip returned");
 const QString request(64,'c');auto reportFrame=frame({gold});reportFrame.insert("request_id",request);reportFrame.insert("phase","Report ready");
 const auto waitReply=[&]{QEventLoop loop;QTimer::singleShot(80,&loop,&QEventLoop::quit);loop.exec();};
 unsigned lookups=0;
 const auto reportState=[&](QString id){return QJsonObject{{"reports",QJsonArray{QJsonObject{{"request_id",id},{"block",QString(64,'d')},{"report_root",QString(64,'e')}}}}};};
 rail->bindTicketLookup([&](const QString& id,auto done){++lookups;require(id==request,"Wrong request sent to Core report lookup");done(reportState(QString(64,'f')),{});});rail->present(reportFrame);waitReply();
 require(!rail->property("pgn_ticket_landing_confirmed").toBool(),"Another ticket's report authorized chain ingress");
 rail->bindTicketLookup([&](const QString&,auto done){++lookups;done(reportState(request),"Core branch changed");});waitReply();
 require(!rail->property("pgn_ticket_landing_confirmed").toBool(),"Failed branch verification authorized chain ingress");
 rail->bindTicketLookup([&](const QString&,auto done){++lookups;done(reportState(request),{});});waitReply();
 require(rail->property("pgn_ticket_landing_confirmed").toBool()&&lookups==3,"Confirmed Core report did not authorize ingress, or polling was unbounded");
 require(!rail->property("pgn_ticket_landing_audited").toBool(),"Report inclusion was presented as an audit verdict");
 const auto auditedState=[&](const QString& id){auto state=reportState(id);state.insert("audits",QJsonArray{QJsonObject{{"request_id",id},{"audited",true},{"details_root",QString(64,'9')}}});return state;};
 for(int fault=0;fault<5;++fault){auto state=auditedState(request);auto reports=state.value("reports").toArray();auto report=reports[0].toObject();auto audits=state.value("audits").toArray();auto audit=audits[0].toObject();
  if(fault==0)reports={};if(fault==1)report.insert("block",QString(64,'0'));if(fault==2)report.insert("ticket",QString(64,'f'));if(fault==3)audit.remove("details_root");if(fault==4)audit.insert("audited",false);
  if(!reports.isEmpty())reports[0]=report;audits[0]=audit;state.insert("reports",reports);state.insert("audits",audits);
  require(!ticketFlowEvidence(request,reportFrame.value("ticket_id").toString(),state,{}).audited,"Incomplete or inconsistent evidence enabled audit halo");
 }
 rail->bindTicketLookup([&](const QString&,auto done){auto state=auditedState(QString(64,'f'));state.insert("reports",reportState(request).value("reports"));done(state,{});});waitReply();
 require(!rail->property("pgn_ticket_landing_audited").toBool(),"Another request's audit verdict enabled the halo");
 rail->bindTicketLookup([&](const QString&,auto done){auto state=auditedState(request);auto audits=state.value("audits").toArray();auto row=audits[0].toObject();row.insert("audited",1);audits[0]=row;state.insert("audits",audits);done(state,{});});waitReply();
 require(!rail->property("pgn_ticket_landing_audited").toBool(),"Untyped audit flag enabled the halo");
 std::function<void(QJsonObject,QString)> obsolete,duplicate;
 rail->bindTicketLookup([&](const QString&,auto done){obsolete=std::move(done);});waitReply();require(bool(obsolete),"Missing obsolete-callback fixture");
 rail->bindTicketLookup([&](const QString&,auto done){duplicate=done;done(auditedState(request),{});});waitReply();
 require(rail->property("pgn_ticket_landing_audited").toBool(),"Verified native audit verdict did not enable halo");
 obsolete({},"Old provider failed");duplicate({},"Duplicate completion");
 require(rail->property("pgn_ticket_landing_audited").toBool(),"Old or duplicate reply overwrote current evidence");
 if(argc>1)for(const int width:{640,1100,1920}){parent.resize(width,400);app.processEvents();QImage image(parent.size(),QImage::Format_ARGB32);parent.render(&image);require(image.save(QString::fromLocal8Bit(argv[1])+QString("/flow-audited-range-%1.png").arg(width)),"Audited range render failed");}
 rail->bindTicketLookup([&](const QString&,auto done){done({},"Core branch changed");});waitReply();
 require(!rail->property("pgn_ticket_landing_audited").toBool()&&!rail->property("pgn_ticket_landing_confirmed").toBool(),"Changed branch retained an approval halo");
 auto auditFrame=frame();auditFrame.insert("work_role","audit");rail->present(auditFrame);app.processEvents();
 if(argc>1){QImage image(parent.size(),QImage::Format_ARGB32);parent.render(&image);require(image.save(QString::fromLocal8Bit(argv[1])+"/flow-audit-work.png"),"Audit work render failed");}
 require(rail->property("pgn_ticket_audit_markers").toInt()==1,"Audit work has no distinct A entity");
 // Seven pending ranges must all receive evidence despite the one-query/s
 // global budget. Choosing the first due entry would starve entries six onward.
 MiningTicketProgress fair;fair.resize(1000,148);fair.show();QSet<QString> queried;unsigned requests=0;
 fair.bindTicketLookup([&](const QString& id,auto done){queried.insert(id);++requests;done(reportState(id),{});});
 for(int i=1;i<=7;++i){auto item=reportFrame;item.insert("ticket_id",QString::number(i).rightJustified(64,'0'));item.insert("request_id",QString::number(i+20).rightJustified(64,'0'));fair.present(item);}
 QEventLoop fairness;QTimer::singleShot(6800,&fairness,&QEventLoop::quit);fairness.exec();
 require(queried.size()==7&&requests<=8,"Bounded audit-status polling starved ranges or exceeded its global budget");
 MiningTicketProgress bounded;bounded.resize(1000,148);bounded.show();unsigned outstandingQueries=0;
 std::function<void(QJsonObject,QString)> evictedReply;
 bounded.bindTicketLookup([&](const QString& id,auto done){++outstandingQueries;if(outstandingQueries==1)evictedReply=std::move(done);else done(reportState(id),{});});
 auto oldest=reportFrame;oldest.insert("ticket_id",QString(64,'1'));oldest.insert("request_id",QString(64,'2'));bounded.present(oldest);waitReply();
 require(bool(evictedReply),"Missing in-flight eviction fixture");
 for(int i=1;i<=40;++i){auto item=reportFrame;item.insert("ticket_id",QString::number(i+100).rightJustified(64,'0'));item.insert("request_id",QString::number(i+200).rightJustified(64,'0'));bounded.present(item);}
 QEventLoop held;QTimer::singleShot(1200,&held,&QEventLoop::quit);held.exec();
 require(outstandingQueries==1,"Evicting a visual range allowed overlapping Core queries");
 evictedReply({},"Old range no longer displayed");waitReply();
 require(outstandingQueries==2,"Evicted range callback left evidence polling permanently blocked");

 std::cout<<"PASS gap flow: event delta/dedup, audit isolation and A marker, R range and typed native-verdict halo, stale/duplicate/foreign/branch-change refusal, fair globally bounded polling, visible Best/stars, resize/controls, exact progress/ETA and confirmation-gated report ingress\n";
}
