#pragma once
#include "ui/audit_assignment_display.h"
#include "wallet/core_runtime.h"
#include <QWidget>
#include <QTableWidget>
#include <QTreeWidget>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QPointer>
#include <QJsonArray>
namespace pgn::ui {
// Public commitments only. Expected counts and unrevealed answers never enter this view.
class AuditExplorerView final:public QWidget {
    pgn::wallet::CoreRuntime* runtime_;QLabel *status_,*summary_;QTableWidget* table_;QTreeWidget* detail_;QPushButton *previous_,*next_;QTimer timer_;QJsonArray reports_,audits_;int offset_{};bool busy_{};
    QJsonObject audit(const QString& id)const{for(const auto& entry:audits_){const auto value=entry.toObject();if(value.value("request_id")==id)return value;}return {};}
    void inspect(int row){
        if(row<0||row>=reports_.size())return;detail_->clear();const auto report=reports_[row].toObject();const auto checks=audit(report.value("request_id").toString());
        const auto field=[this](const QString& name,const QString& value){return new QTreeWidgetItem(detail_,QStringList{name,value});};
        field("Report commitment",report.value("report_root").toString());field("Confirming block",report.value("block").toString());field("Confirmation height",QString::number(report.value("height").toInteger()));field("Owner signing script",report.value("miner_script").toString());
        field("Sampling verdict",checks.value("audited").toBool()?"Both audits passed":"Pending independent checks");
        if(checks.contains("details_root"))field("Larger-gap details commitment",checks.value("details_root").toString());
        for(const auto& value:checks.value("jobs").toArray()){const auto job=value.toObject();auto* parent=field(QString("Audit %1 · attempt %2").arg(job.value("slot").toInt()+1).arg(job.value("attempt").toInt()+1),job.value("state").toString());new QTreeWidgetItem(parent,{"Auditor signing script",job.value("actor").toString()});const auto leaseLine=auditAssignmentLeaseLine(job);new QTreeWidgetItem(parent,{leaseLine.first,leaseLine.second});for(const auto& segment:job.value("ranges").toArray()){const auto range=segment.toObject();new QTreeWidgetItem(parent,{QString("Partition %1").arg(range.value("index").toInt()),"["+range.value("start").toString()+", "+range.value("end").toString()+")"});}parent->setExpanded(true);}
        field("Acceptance boundary","Sampling agreement, exact gap proof and monetary acceptance are separate checks.");
    }
    void refresh(){
        if(busy_||!runtime_->connected())return;busy_=true;const QPointer<AuditExplorerView> guard(this);runtime_->readAuditSnapshot(offset_,[guard](QJsonObject snapshot,QString error){
            if(!guard)return;guard->busy_=false;if(!error.isEmpty()){guard->status_->setText("Audit state unavailable · "+error);guard->status_->setStyleSheet("color:#ffbd69");return;}
            guard->reports_=snapshot.value("reports").toArray();guard->audits_=snapshot.value("audits").toArray();guard->table_->setRowCount(guard->reports_.size());int pending=0,passed=0,failed=0,complete=0;
            for(int i=0;i<guard->reports_.size();++i){const auto report=guard->reports_[i].toObject(),checks=guard->audit(report.value("request_id").toString());QStringList auditStates{"Awaiting auditor","Awaiting auditor"};for(const auto& entry:checks.value("jobs").toArray()){const auto job=entry.toObject();const int slot=job.value("slot").toInt(-1);if(slot<0||slot>1)continue;const auto state=job.value("state").toString();auditStates[slot]=state;if(state=="passed")++passed;else if(state=="failed")++failed;else ++pending;}if(checks.value("audited").toBool())++complete;
                const QStringList cells{QString("Report %1").arg(guard->offset_+i+1),QString::number(report.value("height").toInteger()),auditStates[0],auditStates[1],checks.value("audited").toBool()?"Sampling passed":"Awaiting independent audits"};
                for(int col=0;col<cells.size();++col){auto* item=new QTableWidgetItem(cells[col]);item->setToolTip("Request: "+report.value("request_id").toString()+"\nReport: "+report.value("report_root").toString());guard->table_->setItem(i,col,item);}
            }
            guard->summary_->setText(QString("This page: %1 reports · %2 audit jobs pending · %3 passed · %4 failed · %5 reports with both audits passed").arg(guard->reports_.size()).arg(pending).arg(passed).arg(failed).arg(complete));
            guard->status_->setText(QString("Verified Core · ledger height %1 · observed %2").arg(snapshot.value("height").toInteger()).arg(QDateTime::fromString(snapshot.value("observed_utc").toString(),Qt::ISODateWithMs).toLocalTime().toString("HH:mm:ss")));guard->status_->setStyleSheet("color:#69efc5");guard->previous_->setEnabled(guard->offset_>0);guard->next_->setEnabled(guard->reports_.size()==100);guard->setProperty("pgn_audit_reports",guard->reports_.size());guard->setProperty("pgn_audit_snapshot",snapshot);if(guard->reports_.size())guard->inspect(std::max(0,guard->table_->currentRow()));
        });
    }
public:
    explicit AuditExplorerView(pgn::wallet::CoreRuntime* runtime,QWidget* parent=nullptr):QWidget(parent),runtime_(runtime){
        setObjectName("pgn-audit-explorer");auto* box=new QVBoxLayout(this);box->setContentsMargins(20,14,20,14);status_=new QLabel("Waiting for the verified Core audit state");summary_=new QLabel;summary_->setWordWrap(true);box->addWidget(status_);box->addWidget(summary_);table_=new QTableWidget(0,5);table_->setHorizontalHeaderLabels({"Report","Confirmed at","Audit 1","Audit 2","Validation"});table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);table_->horizontalHeader()->setStretchLastSection(true);table_->verticalHeader()->hide();table_->setEditTriggers(QAbstractItemView::NoEditTriggers);table_->setSelectionBehavior(QAbstractItemView::SelectRows);box->addWidget(table_,1);
        detail_=new QTreeWidget;detail_->setHeaderLabels({"Evidence","Value"});detail_->setColumnWidth(0,270);box->addWidget(detail_,1);auto* controls=new QHBoxLayout;previous_=new QPushButton("Previous reports");next_=new QPushButton("Next reports");auto* reload=new QPushButton("Refresh audits");controls->addWidget(previous_);controls->addWidget(next_);controls->addStretch();controls->addWidget(reload);box->addLayout(controls);previous_->setEnabled(false);next_->setEnabled(false);
        connect(table_,&QTableWidget::cellClicked,this,[this](int row,int){inspect(row);});connect(previous_,&QPushButton::clicked,this,[this]{offset_=std::max(0,offset_-100);refresh();});connect(next_,&QPushButton::clicked,this,[this]{offset_+=100;refresh();});connect(reload,&QPushButton::clicked,this,[this]{refresh();});timer_.setInterval(5000);connect(&timer_,&QTimer::timeout,this,[this]{if(isVisible())refresh();});timer_.start();
    }
};
}
