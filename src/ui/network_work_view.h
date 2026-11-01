#pragma once
#include "ui/audit_assignment_display.h"
#include "ui/compute_lease_presentation.h"
#include <QWidget>
#include <QTableWidget>
#include <QTreeWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QPointer>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include "booking/decimal_range.h"
#include "ui/position_formats.h"
#include <QApplication>
#include <QClipboard>
#include <QSignalBlocker>
#include <QScrollBar>
#include <QHash>
#include <functional>
namespace pgn::ui {
class NetworkWorkView final:public QWidget {
public:
    using Result=std::function<void(QJsonObject,QString)>;
    using PageReader=std::function<void(int,Result)>;
    using DetailReader=std::function<void(const QString&,Result)>;
    std::function<void(const QString&)> onOpenBlock;
    std::function<void()> onOpenAudits;
private:
    PageReader pageReader_;DetailReader detailReader_;QTableWidget* table_;QTreeWidget* details_;QLabel *status_,*summary_;QLineEdit* search_;QComboBox* filter_;QPushButton *previous_,*next_,*openBlock_,*copy_;QTimer timer_;QJsonObject snapshot_,selected_;QString selectedId_,block_;int offset_{},renderedOffset_{-1};bool busy_{};QHash<QString,QJsonObject> detailCache_;
    void render(){
        const QSignalBlocker selectionGuard(table_);table_->setUpdatesEnabled(false);const auto leases=snapshot_.value("leases").toArray();table_->setRowCount(0);const auto query=search_->text().trimmed();const int filter=filter_->currentIndex();
        for(const auto& entry:leases){const auto lease=entry.toObject();const bool submitted=lease.contains("report_root"),audited=lease.value("audited").toBool();if((filter==1&&submitted)||(filter==2&&!submitted)||(filter==3&&!audited))continue;
            const auto id=lease.value("request").toString();if(!query.isEmpty()&&!id.contains(query,Qt::CaseInsensitive)&&!lease.value("ticket_number").toString().contains(query)&&!lease.value("start").toString().contains(query)&&!lease.value("end").toString().contains(query)&&!lease.value("miner_script").toString().contains(query,Qt::CaseInsensitive))continue;
            const int row=table_->rowCount();table_->insertRow(row);table_->setRowHeight(row,46);const auto label=lease.value("ticket_number").toString();const QStringList cells{label.isEmpty()?QString("Issued at %1").arg(lease.value("issued").toInteger()):"Ticket "+label,lease.value("start").toString()+" → "+lease.value("end").toString(),positionFormats(pgn::booking::subtractUnsigned(lease.value("end").toString(),lease.value("start").toString())).stackedText(),audited?"Audits passed":submitted?"Report submitted":"Assigned",QString::number(lease.value("issued").toInteger()),submitted?QString("Report retained"):computeLeaseDeadlineText(lease)};
            for(int col=0;col<cells.size();++col){auto* item=new QTableWidgetItem(cells[col]);item->setData(Qt::UserRole,id);item->setToolTip("Exact request: "+id+"\nOwned lower-prime interval: ["+lease.value("start").toString()+", "+lease.value("end").toString()+")");table_->setItem(row,col,item);}
        }
        for(int row=0;row<table_->rowCount();++row)if(table_->item(row,0)->data(Qt::UserRole).toString()==selectedId_){table_->selectRow(row);break;}
        table_->setUpdatesEnabled(true);summary_->setText(QString("%1 shown on this page · %2 tracked ranges · automatic allocation · manual reservations disabled").arg(table_->rowCount()).arg(snapshot_.value("active_lease_count").toInteger(snapshot_.value("leases").toArray().size())));
    }
    void inspect(const QString& id){
        const bool changed=selectedId_!=id;selectedId_=id;const QPointer<NetworkWorkView> guard(this);
        if(changed){openBlock_->setEnabled(false);copy_->setEnabled(false);status_->setText("Reading report and audit details…");if(!detailCache_.contains(id)){details_->setUpdatesEnabled(false);details_->clear();details_->setProperty("pgn_evidence_id",QString{});for(const auto& item:snapshot_.value("leases").toArray()){const auto lease=item.toObject();if(lease.value("request").toString()!=id)continue;new QTreeWidgetItem(details_,{"Ticket",lease.value("ticket_number").toString()});new QTreeWidgetItem(details_,{"Owned interval","["+lease.value("start").toString()+", "+lease.value("end").toString()+")"});new QTreeWidgetItem(details_,{"Stage",lease.value("state").toString()});if(lease.contains("report_root"))new QTreeWidgetItem(details_,{"Committed report",lease.value("report_root").toString()});break;}details_->setUpdatesEnabled(true);}}
        auto consume=[guard,id](QJsonObject value,QString error){if(!guard||guard->selectedId_!=id)return;if(!error.isEmpty()){guard->status_->setText(error);return;}if(value==guard->selected_&&guard->details_->property("pgn_evidence_id").toString()==id)return;const int scroll=guard->details_->verticalScrollBar()->value();guard->details_->setUpdatesEnabled(false);guard->details_->clear();guard->selected_=value;guard->details_->setProperty("pgn_evidence_id",id);guard->setProperty("pgn_selected_ticket_evidence",value);const auto lease=value.value("leases").toArray().first().toObject();
            const auto field=[guard](const QString& name,const QString& text){return new QTreeWidgetItem(guard->details_,QStringList{name,text});};
            field("Ticket request",id);field("Owned interval","["+lease.value("start").toString()+", "+lease.value("end").toString()+")");field("Start · scientific / binary",positionFormats(lease.value("start").toString()).decimal+" · "+positionFormats(lease.value("start").toString()).binary);field("End · scientific / binary",positionFormats(lease.value("end").toString()).decimal+" · "+positionFormats(lease.value("end").toString()).binary);field("Issued at ledger record",QString::number(lease.value("issued").toInteger()));field(computeLeaseDeadlineHeading(lease),computeLeaseDeadlineText(lease));if(lease.contains("report_root"))field("Audit request retention","Pending until processed · no expiry of the submitted report");field("Owner signing script",lease.value("miner_script").toString());field("Reward signing script",lease.value("reward_script").toString());
            const auto reports=value.value("reports").toArray();if(reports.isEmpty())field("Report","Not submitted");else{const auto report=reports.first().toObject();field("Immutable report root",report.value("report_root").toString());field("Report confirmation",QString::number(report.value("height").toInteger()));guard->block_=report.value("block").toString();field("Confirming block",guard->block_);guard->openBlock_->setEnabled(!guard->block_.isEmpty());}
            const auto audits=value.value("audits").toArray();if(audits.isEmpty())field("Audits","Waiting for a committed report");else{const auto audit=audits.first().toObject();field("Audit verdict",audit.value("audited").toBool()?"Both sampling audits passed":"Awaiting independent checks");const auto jobs=audit.value("jobs").toArray();if(jobs.isEmpty())field("Audit allocation","No independent auditor assigned");for(const auto& item:jobs){const auto job=item.toObject();auto* parent=field(QString("Audit %1 · attempt %2").arg(job.value("slot").toInt()+1).arg(job.value("attempt").toInt()+1),job.value("state").toString());new QTreeWidgetItem(parent,{"Auditor",job.value("actor").toString()});const auto leaseLine=auditAssignmentLeaseLine(job);new QTreeWidgetItem(parent,{leaseLine.first,leaseLine.second});for(const auto& r:job.value("ranges").toArray()){const auto range=r.toObject();new QTreeWidgetItem(parent,{QString("Partition %1").arg(range.value("index").toInt()),range.value("start").toString()+" → "+range.value("end").toString()});}parent->setExpanded(true);}}
            guard->details_->verticalScrollBar()->setValue(scroll);guard->details_->setUpdatesEnabled(true);guard->copy_->setEnabled(true);guard->status_->setText(QString("Evidence verified at ledger record %1 · %2").arg(value.value("height").toInteger()).arg(value.value("bestblockhash").toString().left(12)));
        };
        const auto cached=detailCache_.value(id);if(!cached.isEmpty()){consume(cached,{});if(cached.value("bestblockhash")==snapshot_.value("bestblockhash"))return;status_->setText("Showing verified evidence · checking for updates…");}
        detailReader_(id,[guard,id,consume](QJsonObject value,QString error){if(!guard||guard->selectedId_!=id)return;if(error.isEmpty()){if(guard->detailCache_.size()>=64&&!guard->detailCache_.contains(id))guard->detailCache_.erase(guard->detailCache_.begin());guard->detailCache_.insert(id,value);}consume(value,error);});
    }
    void refresh(){if(busy_)return;busy_=true;const int page=offset_;const QPointer<NetworkWorkView> guard(this);pageReader_(page,[guard,page](QJsonObject state,QString error){
        if(!guard)return;guard->busy_=false;if(page!=guard->offset_){guard->refresh();return;}if(!error.isEmpty()){if(guard->snapshot_.isEmpty())guard->timer_.setInterval(500);guard->status_->setText(error);return;}
        guard->timer_.setInterval(5000);const bool changed=guard->snapshot_.value("bestblockhash")!=state.value("bestblockhash")||guard->renderedOffset_!=page;guard->snapshot_=state;guard->renderedOffset_=page;
        if(changed)guard->render();guard->previous_->setEnabled(page>0);guard->next_->setEnabled(state.value("leases").toArray().size()==100);
        if(guard->selectedId_.isEmpty()&&guard->table_->rowCount()){guard->table_->selectRow(0);}else if(changed&&!guard->selectedId_.isEmpty())guard->inspect(guard->selectedId_);
    });}

protected:
    void showEvent(QShowEvent* event)override{QWidget::showEvent(event);QTimer::singleShot(0,this,[this]{refresh();});}
public:
    NetworkWorkView(PageReader pageReader,DetailReader details,QWidget* parent=nullptr):QWidget(parent),pageReader_(std::move(pageReader)),detailReader_(std::move(details)){
        setObjectName("pgn-network-work-explorer");auto* box=new QVBoxLayout(this);box->setContentsMargins(18,14,18,14);auto* top=new QHBoxLayout;search_=new QLineEdit;search_->setPlaceholderText("Search this page by ticket, interval or owner script");filter_=new QComboBox;filter_->setObjectName("pgn-network-ticket-filter");filter_->addItems({"All work","Assigned","Report submitted","Audits passed"});auto* refreshButton=new QPushButton("Refresh work");top->addWidget(search_,1);top->addWidget(filter_);top->addWidget(refreshButton);box->addLayout(top);status_=new QLabel("Connecting to verified network work…");summary_=new QLabel;summary_->setWordWrap(true);box->addWidget(status_);box->addWidget(summary_);
        table_=new QTableWidget(0,6);table_->setObjectName("pgn-network-ticket-table");table_->setHorizontalHeaderLabels({"Ticket","Owned interval","Positions","Stage","Issued","Compute lease"});table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);table_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);table_->verticalHeader()->hide();table_->setSelectionBehavior(QAbstractItemView::SelectRows);table_->setSelectionMode(QAbstractItemView::SingleSelection);table_->setStyleSheet("QTableWidget#pgn-network-ticket-table{selection-background-color:#245f68;selection-color:#ffffff;} QTableWidget#pgn-network-ticket-table::item:selected,QTableWidget#pgn-network-ticket-table::item:selected:!active{background-color:#245f68;color:#ffffff;border-top:1px solid #5be3cc;border-bottom:1px solid #5be3cc;}");table_->setEditTriggers(QAbstractItemView::NoEditTriggers);box->addWidget(table_,1);
        details_=new QTreeWidget;details_->setHeaderLabels({"Evidence","Value"});details_->setColumnWidth(0,270);box->addWidget(details_,1);auto* actions=new QHBoxLayout;previous_=new QPushButton("Previous page");next_=new QPushButton("Next page");openBlock_=new QPushButton("Open confirming block");copy_=new QPushButton("Copy ticket evidence");auto* audits=new QPushButton("Open audit explorer");for(auto* b:{previous_,next_,openBlock_,copy_,audits})actions->addWidget(b);box->addLayout(actions);previous_->setEnabled(false);next_->setEnabled(false);openBlock_->setEnabled(false);copy_->setEnabled(false);
        connect(search_,&QLineEdit::textChanged,this,[this]{render();});connect(filter_,&QComboBox::currentIndexChanged,this,[this]{render();});connect(refreshButton,&QPushButton::clicked,this,[this]{refresh();});connect(table_,&QTableWidget::itemSelectionChanged,this,[this]{const auto rows=table_->selectionModel()->selectedRows();if(!rows.isEmpty())if(auto* item=table_->item(rows.first().row(),0))inspect(item->data(Qt::UserRole).toString());});connect(previous_,&QPushButton::clicked,this,[this]{offset_=std::max(0,offset_-100);refresh();});connect(next_,&QPushButton::clicked,this,[this]{offset_+=100;refresh();});connect(openBlock_,&QPushButton::clicked,this,[this]{if(onOpenBlock)onOpenBlock(block_);});connect(audits,&QPushButton::clicked,this,[this]{if(onOpenAudits)onOpenAudits();});connect(copy_,&QPushButton::clicked,this,[this]{QApplication::clipboard()->setText(QString::fromUtf8(QJsonDocument(selected_).toJson(QJsonDocument::Indented)));});timer_.setInterval(500);connect(&timer_,&QTimer::timeout,this,[this]{if(isVisible()||snapshot_.isEmpty())refresh();});timer_.start();QTimer::singleShot(0,this,[this]{refresh();});
    }
};
}
