#include "ui/ticket_atlas_view.h"
#include <QApplication>
#include <QFile>
#include <QElapsedTimer>
#include <QThread>
#include <QDir>
#include <iostream>
using pgn::wallet::AtlasInteger;
using pgn::wallet::atlasInteger;
using pgn::wallet::AtlasStates;
using pgn::ui::TicketAtlasView;
static QJsonObject project(QJsonObject base,const QString& start,const QString& end,int requested){
    const AtlasInteger a=atlasInteger(start.isEmpty()?base.value("range_origin"):QJsonValue(start));
    const AtlasInteger b=atlasInteger(end.isEmpty()?base.value("range_limit"):QJsonValue(end)),span=b-a;
    const int n=span<requested?span.convert_to<int>():requested;QJsonArray details,bins;QJsonObject totals;
    for(const auto& row:base.value("details").toArray())if(atlasInteger(row.toObject().value("start"))<b&&atlasInteger(row.toObject().value("end"))>a)details.append(row);
    for(const auto* state:AtlasStates){qint64 count=0;AtlasInteger positions{};for(const auto& entry:details){const auto row=entry.toObject();if(row.value("stage")!=state)continue;++count;const AtlasInteger l=std::max(a,atlasInteger(row.value("start"))),r=std::min(b,atlasInteger(row.value("end")));positions+=r-l;}totals.insert(state,QJsonObject{{"tickets",count},{"positions",QString::fromStdString(positions.str())}});}
    for(int i=0;i<n;++i){const AtlasInteger left=a+span*i/n,right=a+span*(i+1)/n;QJsonObject stages;
        for(const auto* state:AtlasStates){qint64 count=0;AtlasInteger positions{};for(const auto& entry:details){const auto row=entry.toObject();if(row.value("stage")!=state)continue;const AtlasInteger l=std::max(left,atlasInteger(row.value("start"))),r=std::min(right,atlasInteger(row.value("end")));if(l<r){++count;positions+=r-l;}}stages.insert(state,QJsonObject{{"tickets",count},{"positions",QString::fromStdString(positions.str())}});}
        bins.append(QJsonObject{{"start",QString::fromStdString(left.str())},{"end",QString::fromStdString(right.str())},{"stages",stages}});
    }
    base.insert("start",QString::fromStdString(a.str()));base.insert("end",QString::fromStdString(b.str()));base.insert("bins",bins);base.insert("details",details);base.insert("details_truncated",false);base.insert("visible_assignments",details.size());base.insert("totals",totals);return base;
}
static void settle(int ms){QElapsedTimer timer;timer.start();while(timer.elapsed()<ms){QApplication::processEvents();QThread::msleep(1);}}
int main(int argc,char** argv){QApplication app(argc,argv);size_t checks{};auto need=[&](bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);};try{
    need(argc==3,"actual private RPC snapshot and output folder required");
    for(int k=67;k<=95;++k){const AtlasInteger power=AtlasInteger(1)<<k;need(pgn::ui::atlasBitCoordinate(power)==qint64(k)*pgn::ui::AtlasBitUnit,"every power of two owns its exact bit boundary");need(pgn::ui::atlasBitInteger(qint64(k)*pgn::ui::AtlasBitUnit)==power,"inverse bit boundary exact through95bits");const AtlasInteger mid=power+power/2;need(pgn::ui::atlasBitCoordinate(mid)==qint64(k)*pgn::ui::AtlasBitUnit+pgn::ui::AtlasBitUnit/2,"within-bit integer intervals remain linear");}
    for(const auto& start:{QString("150019600000000000000"),QString::fromStdString(((AtlasInteger(1)<<95)-1000000000000000LL).str())}){
        const AtlasInteger a(start.toStdString()),span(8000000000000000LL);QString previous;
        for(int i=0;i<=4;++i){const auto label=pgn::ui::ticketAtlasCoordinateLabel(AtlasInteger(a+span*i/4),span);need(label!=previous,"zoomed 68/95bit axis ticks remain distinct");previous=label;}
    }
    QFile file(QString::fromLocal8Bit(argv[1]));need(file.open(QIODevice::ReadOnly),"snapshot readable");const auto data=file.readAll();need(data.size()<524288,"snapshot byte bound");const auto original=QJsonDocument::fromJson(data).object();const auto genesis=original.value("genesis").toString();
    const auto actualError=pgn::wallet::validateTicketAtlas(original,genesis,{},{},256);
    if(!actualError.isEmpty())std::cerr<<actualError.toStdString()<<'\n';
    need(actualError.isEmpty(),"actual native atlas accepted");
    auto invalid=original;invalid.insert("genesis",QString(64,'c'));need(!pgn::wallet::validateTicketAtlas(invalid,genesis,{},{},256).isEmpty(),"wrong genesis refused");
    invalid=original;auto badBins=invalid.value("bins").toArray();auto first=badBins[0].toObject();first.insert("start","0");badBins[0]=first;invalid.insert("bins",badBins);need(!pgn::wallet::validateTicketAtlas(invalid,genesis,{},{},256).isEmpty(),"broken partition refused");
    invalid=original;invalid.insert("visible_assignments",original.value("tracked_assignments").toInteger()+1);need(!pgn::wallet::validateTicketAtlas(invalid,genesis,{},{},256).isEmpty(),"invented count refused");
    for(int delta:{-1,1}){invalid=original;auto cells=invalid.value("bins").toArray();auto cell=cells[1].toObject();cell.insert("start",QString::fromStdString(AtlasInteger(atlasInteger(cell.value("start"))+delta).str()));cells[1]=cell;invalid.insert("bins",cells);need(!pgn::wallet::validateTicketAtlas(invalid,genesis,{},{},256).isEmpty(),"overlap or gap refused");}
    invalid=original;auto incomplete=invalid.value("bins").toArray();incomplete.removeLast();invalid.insert("bins",incomplete);need(!pgn::wallet::validateTicketAtlas(invalid,genesis,{},{},256).isEmpty(),"missing final viewport cell refused");
    int reads=0;QString fault;QVector<TicketAtlasView::Reply> held;bool hold=false;
    TicketAtlasView view([&](const QString& a,const QString& b,int bins,auto done){++reads;if(hold){held.append(std::move(done));return;}done(fault.isEmpty()?project(original,a,b,bins):QJsonObject{},fault);});
    view.resize(1200,320);view.show();settle(400);need(reads==2,"full domain then one active-area request");need(view.property("pgn_ticket_atlas_error").toString().isEmpty(),"valid coherent active area");
    const auto observed=view.property("pgn_ticket_atlas_snapshot").toJsonObject();need(observed.value("bestblockhash")==original.value("bestblockhash"),"anchor retained");need(view.property("pgn_ticket_atlas_end").toString()!=original.value("range_limit").toString(),"initial view focuses tracked work");
    QDir out(QString::fromLocal8Bit(argv[2]));need(out.exists()||QDir{}.mkpath(out.path()),"output folder");need(view.grab().save(out.filePath("atlas-wide.png")),"wide render saved");
    view.resize(430,300);settle(30);need(view.grab().save(out.filePath("atlas-narrow.png")),"narrow render saved");view.resize(1200,320);settle(20);
    const auto detail=observed.value("details").toArray().first().toObject();const AtlasInteger a=atlasInteger(observed.value("start")),b=atlasInteger(observed.value("end")),middle=(atlasInteger(detail.value("start"))+atlasInteger(detail.value("end")))/2;
    const double fraction=((middle-a)*1000000/(b-a)).convert_to<double>()/1000000.;size_t stage=0;while(stage<5&&detail.value("stage")!=AtlasStates[stage])++stage;
    const QPointF pos=view.inspectionPoint(detail.value("request").toString());need(pos.x()>=0,"rendered exact request is reachable");QString selected;view.inspect=[&](const QString& id){selected=id;};
    QMouseEvent press(QEvent::MouseButtonPress,pos,view.mapToGlobal(pos.toPoint()),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&view,&press);QMouseEvent release(QEvent::MouseButtonRelease,pos,view.mapToGlobal(pos.toPoint()),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(&view,&release);
    if(selected.isEmpty()){for(auto* menu:view.findChildren<QMenu*>())for(auto* action:menu->actions())if(action->text().startsWith(detail.value("request").toString().left(16))){action->trigger();menu->close();}}
    need(selected==detail.value("request").toString(),"click or explicit overlap choice selects exact request");
    const auto retained=view.property("pgn_ticket_atlas_snapshot");fault="Injected offline atlas transport";view.findChild<QPushButton*>("pgn-atlas-overview")->click();settle(180);need(!view.property("pgn_ticket_atlas_error").toString().isEmpty(),"transport loss shown");need(view.property("pgn_ticket_atlas_snapshot")==retained,"transport loss retains prior verified data");
    fault.clear();view.findChild<QPushButton*>("pgn-atlas-overview")->click();settle(180);need(view.property("pgn_ticket_atlas_end").toString()==original.value("range_limit").toString(),"whole95bitdomain available");need(view.property("pgn_ticket_atlas_error").toString().isEmpty(),"transport recovery");
    const auto bitTicks=view.property("pgn_ticket_atlas_bit_ticks").toJsonArray();need(view.property("pgn_ticket_atlas_bit_scale").toBool(),"domain uses bit bands");const int lowBit=boost::multiprecision::msb(atlasInteger(original.value("range_origin")))+1,highBit=boost::multiprecision::msb(AtlasInteger(atlasInteger(original.value("range_limit"))-1))+1;need(bitTicks.size()==highBit-lowBit+1,"one visible graduation for every bit without omissions");for(int i=0;i<bitTicks.size();++i){const auto tick=bitTicks[i].toObject();need(tick.value("bits").toInt()==lowBit+i,"bit labels are consecutive");need(tick.value("scientific").toString().contains("× 10"),"every bit has a scientific coordinate");}
    need(view.grab().save(out.filePath("atlas-bit-domain-wide.png")),"complete bit axis wide render");view.resize(430,300);settle(30);need(view.grab().save(out.filePath("atlas-bit-domain-narrow.png")),"complete bit axis narrow render");view.resize(1200,320);settle(20);
    selected.clear();const QPointF clustered=view.inspectionPoint(detail.value("request").toString());
    QMouseEvent clusterPress(QEvent::MouseButtonPress,clustered,view.mapToGlobal(clustered.toPoint()),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&view,&clusterPress);
    QMouseEvent clusterRelease(QEvent::MouseButtonRelease,clustered,view.mapToGlobal(clustered.toPoint()),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(&view,&clusterRelease);settle(180);
    need(selected.isEmpty(),"overlapping markers never select arbitrary request");need(view.property("pgn_ticket_atlas_end").toString()==original.value("range_limit").toString(),"ambiguous marker never changes zoom"); for(auto* menu:view.findChildren<QMenu*>())menu->close();
    const auto beforeMissStart=view.property("pgn_ticket_atlas_start"),beforeMissEnd=view.property("pgn_ticket_atlas_end");
    const QPointF miss(1190,100);QMouseEvent missPress(QEvent::MouseButtonPress,miss,view.mapToGlobal(miss.toPoint()),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&view,&missPress);QMouseEvent missRelease(QEvent::MouseButtonRelease,miss,view.mapToGlobal(miss.toPoint()),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(&view,&missRelease);settle(180);
    need(view.property("pgn_ticket_atlas_start")==beforeMissStart&&view.property("pgn_ticket_atlas_end")==beforeMissEnd,"missed click preserves both viewport bounds");
    view.embeddedCommand({{"action","bin"},{"index",0}});settle(20);need(view.property("pgn_ticket_atlas_start")==beforeMissStart&&view.property("pgn_ticket_atlas_end")==beforeMissEnd,"embedded bin inspection never changes zoom");for(auto* menu:view.findChildren<QMenu*>())menu->close();
    view.findChild<QPushButton*>("pgn-atlas-overview")->click();settle(180);
    hold=true;view.focusActive();settle(180);need(held.size()==1,"one in-flight request");view.findChild<QPushButton*>("pgn-atlas-overview")->click();settle(180);held.takeFirst()(observed,{});settle(180);need(held.size()==1,"stale viewport replaced by latest request");need(view.property("pgn_ticket_atlas_snapshot").toJsonObject().value("end")==original.value("range_limit"),"stale response not adopted");
    held.takeFirst()(original,{});hold=false;settle(20);view.hide();
    std::cout<<"PASS "<<checks<<" ticketatlas widget checks;actualRPCgeometry,narrow/widerender,exactselection,95bitoverview,transportpreservation,staleviewportrefusal;offscreenonly\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
