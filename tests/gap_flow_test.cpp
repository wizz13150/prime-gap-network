#include "ui/gap_flow_model.h"
#include "ui/gap_flow_motion.h"
#include "ui/mining_ticket_progress.h"
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
int main(int argc,char** argv){
 QApplication app(argc,argv);GapFlowModel model;auto old=row(1,29.);auto gold=row(2,30.);
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
 model.expire(26000);require(model.departing().size()==1,"Unconfirmed report entered the chain on a timer");model.confirmReport(QString(64,'a'),26010);
 model.expire(28000);require(model.departing().isEmpty(),"Confirmed ticket departure did not close");
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
    QJsonObject{{"id","cpu"},{"visible",true},{"rect",QJsonArray{270,4,240,90}}}});
 auto* button=new QPushButton("Existing control",canvas);button->setGeometry(15,10,100,24);
 auto* rail=new MiningTicketProgress;layout->addWidget(canvas);layout->addWidget(rail);layout->addStretch();
 auto* results=new QWidget(&parent);auto* table=new QTableWidget(results);table->setObjectName("pgn-results-best");table->setColumnCount(1);table->setRowCount(1);auto* item=new QTableWidgetItem("1004");const auto pair=gold.value("lower_prime").toString()+":1004";item->setData(Qt::UserRole,pair);table->setItem(0,0,item);results->setProperty("pgn_visible_best",snapshot({gold,row(54,28.),row(53,27.),row(52,26.)}));results->hide();rail->bindResultsView(results);
 rail->bindSourceCanvas(canvas);parent.resize(1100,400);parent.show();app.processEvents();
 rail->present(frame());rail->present(frame({gold,row(54,28.),row(53,27.),row(52,26.)}));rail->setRecordEligibility({gold});
 QEventLoop measured;QTimer::singleShot(1950,&measured,&QEventLoop::quit);measured.exec();
 for(int width:{640,1100,1920}){
  parent.resize(width,400);app.processEvents();QImage image(parent.size(),QImage::Format_ARGB32);image.fill(Qt::transparent);parent.render(&image);
  require(rail->property("pgn_ticket_progress_fraction").toDouble()==.5,"Animation advanced real progress");
  require(rail->property("pgn_ticket_eta_seconds").toDouble()==100.,"Animation changed ETA");
  require(rail->height()<=156,"Animation exceeded twice the original reserved height");
  auto* overlay=parent.findChild<QWidget*>("pgn-card-gap-flow");require(overlay&&overlay->testAttribute(Qt::WA_TransparentForMouseEvents),"Overlay blocks card controls");
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
