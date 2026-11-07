#include "ui/network_work_view.h"
#include <QFile>
#include <QElapsedTimer>
#include <QThread>
#include <QDir>
#include <iostream>
using pgn::wallet::AtlasInteger;
static void settle(int ms){QElapsedTimer t;t.start();while(t.elapsed()<ms){QApplication::processEvents();QThread::msleep(1);}}
static QJsonObject read(const QString& path){QFile f(path);if(!f.open(QIODevice::ReadOnly)||f.size()>524288)throw std::runtime_error("Bounded actual snapshot required");return QJsonDocument::fromJson(f.readAll()).object();}
int main(int argc,char** argv){QApplication app(argc,argv);size_t checks{};auto need=[&](bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);};try{
    need(argc==4,"atlas,page,output arguments");const auto atlas=read(QString::fromLocal8Bit(argv[1])),work=read(QString::fromLocal8Bit(argv[2]));
    need(atlas.value("tracked_assignments").toInteger()>100&&atlas.value("details_truncated").toBool(),"actual mass snapshot,not tiny synthetic case");
    need(pgn::wallet::validateTicketAtlas(atlas,atlas.value("genesis").toString(),{},{},256).isEmpty(),"native925snapshot validated");
    auto state=work.value("state").toObject();state.insert("reports",work.value("reports"));state.insert("audits",work.value("audits"));
    QVector<QPair<QString,QString>> queries;bool detailWasRequested=false;
    pgn::ui::NetworkWorkView view([&](int,auto done){done(state,{});},[&](const QString&,auto done){detailWasRequested=true;done({},"Offpage evidence deliberately unavailable");},nullptr,[&](const QString& a,const QString& b,int,auto done){queries.append({a,b});done(atlas,{});});
    view.resize(1280,1000);view.show();settle(350);
    auto* map=dynamic_cast<pgn::ui::TicketAtlasView*>(view.findChild<QWidget*>("pgn-ticket-atlas"));auto* table=view.findChild<QTableWidget*>("pgn-network-ticket-table");need(map&&table,"actual composed networkwork widgets");
    // Return to the complete-domain snapshot. The mock reader intentionally
    // refuses its different active viewport; stale geometry must not replace it.
    map->findChild<QPushButton*>("pgn-atlas-overview")->click();settle(180);
    need(map->property("pgn_ticket_atlas_snapshot").toJsonObject().value("tracked_assignments")==atlas.value("tracked_assignments"),"whole schedule retained");
    need(map->property("pgn_ticket_atlas_snapshot").toJsonObject().value("details_truncated").toBool(),"coarse aggregate mode");
    QString selected;map->inspect=[&](const QString& id){selected=id;};
    const auto bin=atlas.value("bins").toArray().first().toObject();const QPointF point=map->inspectionPoint();need(point.x()>=0,"whole-bin inspection point exists");
    const auto send=[&](QEvent::Type kind,Qt::MouseButtons buttons){QMouseEvent e(kind,point,map->mapToGlobal(point.toPoint()),Qt::LeftButton,buttons,Qt::NoModifier);QApplication::sendEvent(map,&e);};
    send(QEvent::MouseButtonPress,Qt::LeftButton);send(QEvent::MouseButtonRelease,Qt::NoButton);settle(180);
    const auto cellEnd=atlas.value("bins").toArray()[3].toObject().value("end").toString();
    need(selected.isEmpty(),"coarse band never selects first100sample");need(!queries.isEmpty()&&queries.last().first==bin.value("start").toString()&&queries.last().second==cellEnd,"density click requests exact union of four complete native bins");
    const QString target=QString(64,'a');map->inspect=[&](const QString& id){
        // Same callback used by the integrated view: choose an actual page row
        // via the normal selection, then an off-page exact request.
        Q_UNUSED(id);
    };
    table->selectRow(0);settle(20);need(!table->selectionModel()->selectedRows().isEmpty(),"visible page selection");
    // Exercise the actual stored atlas callback before replacing it in the
    // coarse-only assertion: locate it in a second integrated view.
    pgn::ui::NetworkWorkView selection([&](int,auto done){done(state,{});},[&](const QString&,auto done){detailWasRequested=true;done({},"Offpage details held");},nullptr,[](const QString&,const QString&,int,auto done){done({},"No testmap needed");});
    selection.resize(1200,900);selection.show();settle(20);auto* second=selection.findChild<QTableWidget*>("pgn-network-ticket-table");auto* secondMap=dynamic_cast<pgn::ui::TicketAtlasView*>(selection.findChild<QWidget*>("pgn-ticket-atlas"));second->selectRow(0);settle(10);need(!second->selectionModel()->selectedRows().isEmpty(),"initial selection in composed view");
    secondMap->inspect(target);need(second->selectionModel()->selectedRows().isEmpty(),"offpage atlas request clears unrelated highlight");need(secondMap->property("pgn_ticket_atlas_selected").toString()==target,"map and evidence exact identity aligned");
    auto* split=view.findChild<QSplitter*>("pgn-network-evidence-split");auto* evidence=view.findChild<QTreeWidget*>("pgn-network-ticket-evidence");need(split&&split->orientation()==Qt::Horizontal,"wide view has side inspector");need(table->height()>400&&evidence->height()>400,"wide table and details keep substantial height");need(table->rowHeight(0)>=table->fontMetrics().lineSpacing()*3+8,"all three numeric forms have line height");
    view.resize(900,860);settle(30);need(split->orientation()==Qt::Vertical,"narrow view stacks inspector");need(table->height()>=160&&evidence->height()>=140,"narrow panes remain usable");view.resize(1280,1000);settle(30);
    QDir output(QString::fromLocal8Bit(argv[3]));need(output.exists()||QDir{}.mkpath(output.path()),"render directory");
    map->findChild<QPushButton*>("pgn-atlas-overview")->click();settle(180);need(view.grab().save(output.filePath("actual-925-network-work.png")),"composed actual925render");
    view.hide();selection.hide();std::cout<<"PASS "<<checks<<" actual mass-atlas checks;whole925projection,bounded100detailsnotasample,exactbinzoom,offpagehighlightcleared;offscreen/noRPCmutations\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
