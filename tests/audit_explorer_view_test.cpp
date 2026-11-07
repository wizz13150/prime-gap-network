#include "ui/audit_explorer_view.h"
#include <QApplication>
#include <QTableWidget>
#include <QTreeWidget>
#include <QPushButton>
#include <deque>
#include <iostream>
#include <stdexcept>

static void require(bool value, const char* what) {
    if (!value) throw std::runtime_error(what);
}
static QJsonObject report(int n) {
    return {{"request_id", QString::number(n,16).rightJustified(64,'0')},
        {"report_root", QString(64,'a')}, {"block", QString(64,'b')},
        {"height", n+20}, {"miner_script", "0014"+QString(40,'c')}};
}
static QJsonObject page(int first, int count) {
    QJsonArray reports;
    for (int i=0; i<count; ++i) reports.append(report(first+i));
    QJsonArray audits;
    if (count) audits.append(QJsonObject{{"request_id", report(first).value("request_id")}, {"audited",false},
        {"jobs",QJsonArray{QJsonObject{{"slot",0},{"attempt",0},{"state","assigned"},{"actor","public-actor"},
         {"ranges",QJsonArray{QJsonObject{{"index",1},{"start","150000000000000000000"},{"end","150000001000000000000"}},
          QJsonObject{{"index",95},{"start","39614081257132168796771975127"},{"end","39614081257132168796771975167"}}}}}}}});
    return {{"reports",reports},{"audits",audits},{"height",500},
        {"tip",QString(64,'d')},{"observed_utc","2026-11-01T15:00:00.000Z"}};
}
int main(int argc, char** argv) {
    QApplication app(argc,argv);
    QFont fixtureFont("Segoe UI");fixtureFont.setPixelSize(13);app.setFont(fixtureFont);
    QPalette fixturePalette;fixturePalette.setColor(QPalette::Window,QColor("#020e14"));fixturePalette.setColor(QPalette::WindowText,QColor("#d9f3f4"));fixturePalette.setColor(QPalette::Base,QColor("#051b23"));fixturePalette.setColor(QPalette::AlternateBase,QColor("#0b2831"));fixturePalette.setColor(QPalette::Text,QColor("#d9f3f4"));fixturePalette.setColor(QPalette::Button,QColor("#0d323a"));fixturePalette.setColor(QPalette::ButtonText,QColor("#d9f3f4"));app.setPalette(fixturePalette);
    using View=pgn::ui::AuditExplorerView;
    struct Pending {int offset; View::Result reply;};
    std::deque<Pending> pending;
    View view(View::PageReader{[&](int offset, View::Result reply) {pending.push_back({offset,std::move(reply)});}});
    view.resize(1100,700); view.show(); app.processEvents();
    auto answer=[&](int offset,QJsonObject snapshot,QString error={}) {
        require(!pending.empty() && pending.front().offset==offset,"Wrong requested page");
        auto callback=std::move(pending.front().reply);pending.pop_front();callback(snapshot,error);app.processEvents();
    };
    auto* table=view.findChild<QTableWidget*>("pgn-audit-report-table");
    auto* details=view.findChild<QTreeWidget*>("pgn-audit-report-details");
    auto* reload=view.findChild<QPushButton*>("pgn-audit-refresh");
    auto* next=view.findChild<QPushButton*>("pgn-audit-next");
    auto* previous=view.findChild<QPushButton*>("pgn-audit-previous");
    auto snapshot=page(1,100);answer(0,snapshot);
    require(table->rowCount()==100,"Initial page missing");
    require(table->item(0,2)->text()=="Assigned"&&table->item(0,2)->foreground().color()==QColor("#62cfff")&&!table->item(0,2)->icon().isNull(),"Assigned audit lacks persistent color/icon");
    require(table->item(0,3)->foreground().color()==QColor("#ffbd69"),"Missing auditor must remain awaiting");
    require(table->item(0,2)->toolTip().contains("does not prove"),"Assignment must not imply running computation");
    require(view.property("pgn_audit_waiting_slots").toInt()==199,"Unassigned slots not counted");
    require(view.property("pgn_audit_active_jobs").toInt()==1,"Active audit count wrong");
    const auto partitions=details->findItems("Partition 1",Qt::MatchExactly|Qt::MatchRecursive);
    require(partitions.size()==1,"Audit partition missing");auto* partition=partitions.first();
    require(partition->text(1)=="1 T\n1.0000e12\n≈ 1.819 × 2^39 positions","Partition must show common three notations");
    require(partition->child(0)->text(1)=="[150000000000000000000, 150000001000000000000)","Exact interval changed");
    require(partition->child(1)->text(1)=="[1.5e20, 1.50000001e20)","Nearby scientific bounds must remain distinct");
    const auto top=details->findItems("Partition 95",Qt::MatchExactly|Qt::MatchRecursive);
    require(top.size()==1&&top.first()->text(1).startsWith("40\n"),"95bit interval width lost through floating point");
    partition->setExpanded(true);
    auto invalid=pgn::ui::positionRangeItem(partition,"Invalid","20","10");
    require(invalid->text(1)=="Invalid interval"&&invalid->child(0)->text(1)=="[20, 10)","Invalid interval must not invent a count");
    auto* detailItem=details->topLevelItem(0);auto* tableItem=table->item(0,0);
    int builds=view.property("pgn_audit_detail_rebuilds").toInt();
    for(int i=0;i<3;++i) {
        reload->click();snapshot.insert("observed_utc",QString("2026-11-01T15:00:0%1.000Z").arg(i+1));answer(0,snapshot);
        require(details->topLevelItem(0)==detailItem && table->item(0,0)==tableItem,"Unchanged refresh recreated visible content");
        require(view.property("pgn_audit_detail_rebuilds").toInt()==builds,"Unchanged detail refreshed");
    }
    reload->click();auto notationUpdate=snapshot;auto notationAudits=notationUpdate.value("audits").toArray();auto notationAudit=notationAudits[0].toObject();notationAudit.insert("details_root",QString(64,'9'));notationAudits[0]=notationAudit;notationUpdate.insert("audits",notationAudits);answer(0,notationUpdate);
    require(details->findItems("Partition 1",Qt::MatchExactly|Qt::MatchRecursive).first()->isExpanded(),"Partition expansion lost during real evidence refresh");
    if(argc>1){view.resize(820,700);app.processEvents();view.grab().save(QString::fromLocal8Bit(argv[1])+"/audit-positions-820.png");view.resize(1100,760);app.processEvents();view.grab().save(QString::fromLocal8Bit(argv[1])+"/audit-positions-1100.png");}
    table->setCurrentCell(1,0);table->selectRow(1);app.processEvents();
    const auto selected=report(2).value("request_id").toString();
    require(view.property("pgn_audit_selected_request").toString()==selected,"Row selection not inspected");
    reload->click();auto updated=snapshot;auto audits=updated.value("audits").toArray();
    auto first=audits[0].toObject();first.insert("details_root",QString(64,'e'));audits[0]=first;updated.insert("audits",audits);answer(0,updated);
    require(view.property("pgn_audit_selected_request").toString()==selected && table->currentRow()==1,"Selection changed during refresh");
    next->click();next->click();require(pending.size()==1,"Overlapping page RPC");answer(100,page(101,100));
    require(table->rowCount()==100 && table->item(0,0)->data(Qt::UserRole)==report(1).value("request_id").toString(),"Stale page entered table");
    answer(200,page(201,1));require(table->rowCount()==1 && table->item(0,0)->data(Qt::UserRole)==report(201).value("request_id").toString(),"Latest page not displayed");
    previous->click();answer(100,page(101,100));require(table->rowCount()==100,"Previous page unavailable");
    reload->click();answer(100,{},"Injected transport interruption");require(table->rowCount()==100,"Read error erased verified evidence");
    reload->click();answer(100,page(101,0));require(table->rowCount()==0 && details->topLevelItemCount()==0,"Empty page retained wrong evidence");
    std::deque<Pending> registryPending;
    View registered(View::PageReader{[](int,View::Result consume){consume(page(1,1),{});}},
        View::PageReader{[&](int offset,View::Result consume){registryPending.push_back({offset,std::move(consume)});}});
    registered.resize(1100,700);registered.show();app.processEvents();
    auto registryAnswer=[&](int offset,const QJsonObject& state,QString error={}) {
        require(!registryPending.empty()&&registryPending.front().offset==offset,"Wrong registry page");
        auto consume=std::move(registryPending.front().reply);registryPending.pop_front();consume(state,error);app.processEvents();
    };
    const QJsonObject identity{{"operator_id",QString(64,'e')},{"signing_script","0014"+QString(40,'a')},{"may_audit",true}};
    QJsonArray aliases{identity,identity};auto secondAlias=identity;secondAlias.insert("signing_script","0014"+QString(40,'b'));aliases[1]=secondAlias;
    QJsonObject membership{{"participants",aliases},{"total",2},{"height",600}};
    registryAnswer(0,membership);
    auto* registry=registered.findChild<QTreeWidget*>("pgn-audit-registry-table");
    auto* registrySummary=registered.findChild<QLabel*>("pgn-audit-registry-summary");
    auto* toggle=registered.findChild<QToolButton*>("pgn-audit-registry-toggle");
    require(registry->topLevelItemCount()==2&&registrySummary->text().contains("operators on this page: 1"),"Aliases counted as independent operators");
    require(registrySummary->toolTip().contains("does not prove independence"),"Membership falsely claims independent availability");
    require(!registry->isVisible(),"Registry must start collapsed");toggle->click();app.processEvents();require(registry->isVisible(),"Registry cannot expand");
    require(registry->height()<150,"Small registry unnecessarily consumes the full panel height");
    auto* registryItem=registry->topLevelItem(0);
    registered.findChild<QPushButton*>("pgn-audit-refresh")->click();membership.insert("height",601);registryAnswer(0,membership);
    require(registry->topLevelItem(0)==registryItem,"Unchanged registry page recreated its rows");
    registered.findChild<QPushButton*>("pgn-audit-refresh")->click();registryAnswer(0,{},"Unavailable");
    require(registry->topLevelItemCount()==2,"Registry read error erased verified identities");
    registered.findChild<QPushButton*>("pgn-audit-refresh")->click();
    auto oversized=membership;QJsonArray many;for(int i=0;i<101;++i)many.append(identity);oversized.insert("participants",many);registryAnswer(0,oversized);
    require(registry->topLevelItemCount()==2,"Oversized registry response entered the view");
    for(const auto& state:QStringList{"assigned","committed","passed","failed","future-state"}){
        auto sample=page(1,1);auto checks=sample.value("audits").toArray();auto audit=checks[0].toObject();auto jobs=audit.value("jobs").toArray();auto job=jobs[0].toObject();job.insert("state",state);jobs[0]=job;audit.insert("jobs",jobs);audit.insert("audited",state=="passed");checks[0]=audit;sample.insert("audits",checks);
        View colored(View::PageReader{[sample](int,View::Result reply){reply(sample,{});}});colored.resize(800,500);colored.show();app.processEvents();
        auto* grid=colored.findChild<QTableWidget*>("pgn-audit-report-table");auto expected=pgn::ui::auditStatusPresentation(state);
        require(grid->item(0,2)->text()==expected.text&&grid->item(0,2)->foreground().color()==expected.color,"Audit status color/text missing");
        if(state=="failed"||state=="future-state")require(grid->item(0,4)->text()!="Sampling passed","Unverified or failed status promoted to passed");
        require(colored.findChild<QLabel*>("pgn-audit-status-legend")!=nullptr,"Missing audit legend");
    }
    View::Result detached;
    auto* closing=new View(View::PageReader{[&](int,View::Result consume){detached=std::move(consume);}});
    closing->show();app.processEvents();delete closing;detached(page(1,1),{});
    std::cout<<"PASS audit explorer: missing slots, stable refresh, selection, stale page, errors, empty state, destroyed owner\n";
}
