#pragma once
#include "ui/audit_assignment_display.h"
#include "ui/audit_status_presentation.h"
#include "ui/compute_lease_presentation.h"
#include "ui/public_report_name.h"
#include "ui/ticket_atlas_view.h"
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
#include "ui/position_range_item.h"
#include <QApplication>
#include <QClipboard>
#include <QSignalBlocker>
#include <QScrollBar>
#include <QSplitter>
#include <QResizeEvent>
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
    QStringList prefetch_;bool prefetchBusy_{};quint64 prefetchGeneration_{};
    TicketAtlasView* atlas_{};
    QSplitter *content_{},*workspace_{};
    bool rowLayoutPending_{};
    void compactRows(){
        rowLayoutPending_=false;
        if(!table_||!table_->rowCount())return;
        const auto metrics=table_->fontMetrics();
        int intervalWidth=metrics.horizontalAdvance(table_->horizontalHeaderItem(1)->text())+20;
        int inlineWidth=0,wrappedWidth=0,otherWidth=0;
        for(int col:{0,3,4,5}){
            int width=metrics.horizontalAdvance(table_->horizontalHeaderItem(col)->text())+20;
            for(int row=0;row<table_->rowCount();++row)
                width=std::max(width,table_->sizeHintForIndex(table_->model()->index(row,col)).width()+2);
            table_->setColumnWidth(col,width);otherWidth+=width;
        }
        for(int row=0;row<table_->rowCount();++row){
            intervalWidth=std::max(intervalWidth,table_->sizeHintForIndex(table_->model()->index(row,1)).width()+2);
            const auto* item=table_->item(row,2);const auto parts=item->data(Qt::UserRole+1).toStringList();
            if(parts.size()!=3)continue;
            int currentTextWidth=0;for(const auto& line:item->text().split('\n'))currentTextWidth=std::max(currentTextWidth,metrics.horizontalAdvance(line));
            const int chrome=std::max(16,table_->sizeHintForIndex(table_->model()->index(row,2)).width()-currentTextWidth+2);
            inlineWidth=std::max(inlineWidth,metrics.horizontalAdvance(parts.join(" · "))+chrome);
            wrappedWidth=std::max(wrappedWidth,std::max(metrics.horizontalAdvance(parts[0]+" · "+parts[1]),metrics.horizontalAdvance(parts[2]))+chrome);
        }
        const bool oneLine=true;
        const int positionsWidth=oneLine?inlineWidth:wrappedWidth;
        const int rowHeight=std::max(22,metrics.lineSpacing()*(oneLine?1:2)+6);
        const bool updates=table_->updatesEnabled();table_->setUpdatesEnabled(false);
        table_->setColumnWidth(2,positionsWidth);
        // Preserve exact interval text even in narrow panes; scroll horizontally
        // rather than shrink the font or force the position count onto three lines.
        table_->setColumnWidth(1,std::max(intervalWidth,table_->viewport()->width()-otherWidth-positionsWidth));
        for(int row=0;row<table_->rowCount();++row){
            auto* item=table_->item(row,2);const auto parts=item->data(Qt::UserRole+1).toStringList();
            if(parts.size()!=3)continue;
            const auto text=oneLine?parts.join(" · "):parts[0]+" · "+parts[1]+"\n"+parts[2];
            if(item->text()!=text)item->setText(text);
            if(table_->rowHeight(row)!=rowHeight)table_->setRowHeight(row,rowHeight);
        }
        table_->setUpdatesEnabled(updates);
    }
    void scheduleCompactRows(){
        if(rowLayoutPending_)return;
        rowLayoutPending_=true;QTimer::singleShot(0,this,[this]{compactRows();});
    }
    QJsonObject pageDetail(const QString& id)const{
        if(!snapshot_.value("reports").isArray()||!snapshot_.value("audits").isArray())return {};
        for(const auto& item:snapshot_.value("leases").toArray())if(item.toObject().value("request").toString()==id){
            auto value=snapshot_;value.insert("leases",QJsonArray{item});bool report=false,audit=false;
            for(const auto* key:{"reports","audits"}){QJsonArray rows;for(const auto& row:snapshot_.value(key).toArray())if(row.toObject().value("request_id").toString()==id)rows.append(row);if(QString::fromLatin1(key)=="reports")report=!rows.isEmpty();else audit=!rows.isEmpty();value.insert(key,rows);}
            // Empty page arrays can mean omitted details, not absent reports.
            if(item.toObject().contains("report_root")&&(!report||!audit))return {};
            return value;
        }return {};
    }
    void remember(const QString& id,const QJsonObject& value){if(detailCache_.size()>=64&&!detailCache_.contains(id))detailCache_.erase(detailCache_.begin());detailCache_.insert(id,value);setProperty("pgn_ticket_detail_cache_size",detailCache_.size());}
    void prefetchNext(){
        if(prefetchBusy_||prefetch_.isEmpty()||!isVisible())return;
        const auto id=prefetch_.takeFirst(),anchor=snapshot_.value("bestblockhash").toString();const auto generation=prefetchGeneration_;prefetchBusy_=true;const QPointer<NetworkWorkView> guard(this);
        detailReader_(id,[guard,id,anchor,generation](QJsonObject value,QString error){if(!guard)return;guard->prefetchBusy_=false;const auto leases=value.value("leases").toArray();if(error.isEmpty()&&generation==guard->prefetchGeneration_&&value.value("bestblockhash")==anchor&&guard->snapshot_.value("bestblockhash")==anchor&&leases.size()==1&&leases.first().toObject().value("request").toString()==id)guard->remember(id,value);QTimer::singleShot(0,guard,[guard]{if(guard)guard->prefetchNext();});});
    }
    void prefetchVisible(){
        ++prefetchGeneration_;prefetch_.clear();if(!isVisible())return;
        const int first=std::max(0,table_->rowAt(0)),last=std::min(table_->rowCount(),first+8);
        for(int row=first;row<last;++row){const auto id=table_->item(row,0)->data(Qt::UserRole).toString();if(id==selectedId_||!pageDetail(id).isEmpty())continue;const auto cached=detailCache_.value(id);if(!cached.isEmpty()&&cached.value("bestblockhash")==snapshot_.value("bestblockhash"))continue;prefetch_.append(id);}prefetchNext();
    }
    void render(){
        const QSignalBlocker selectionGuard(table_);table_->setUpdatesEnabled(false);const auto leases=snapshot_.value("leases").toArray();int renderedRows=0;const auto query=search_->text().trimmed();const int filter=filter_->currentIndex();
        for(const auto& entry:leases){const auto lease=entry.toObject();QJsonObject auditState;for(const auto& check:snapshot_.value("audits").toArray())if(check.toObject().value("request_id")==lease.value("request")){auditState=check.toObject();break;}const bool submitted=lease.contains("report_root"),audited=lease.value("audited").toBool()||auditState.value("audited").toBool();if((filter==1&&submitted)||(filter==2&&!submitted)||(filter==3&&!audited))continue;
            const auto id=lease.value("request").toString();if(!query.isEmpty()&&!id.contains(query,Qt::CaseInsensitive)&&!lease.value("ticket_number").toString().contains(query)&&!lease.value("start").toString().contains(query)&&!lease.value("end").toString().contains(query)&&!lease.value("miner_script").toString().contains(query,Qt::CaseInsensitive))continue;
            const int row=renderedRows++;if(row>=table_->rowCount())table_->insertRow(row);const auto label=lease.value("ticket_number").toString();const auto positions=positionFormats(pgn::booking::subtractUnsigned(lease.value("end").toString(),lease.value("start").toString()));const QStringList cells{label.isEmpty()?QString("Issued at %1").arg(lease.value("issued").toInteger()):"Ticket "+label,lease.value("start").toString()+" → "+lease.value("end").toString(),positions.inlineText(),audited?"Audits passed":submitted?"Report submitted":"Assigned",QString::number(lease.value("issued").toInteger()),submitted?QString("Report retained"):computeLeaseDeadlineText(lease)};
            for(int col=0;col<cells.size();++col){auto* item=table_->item(row,col);const bool changed=!item||item->text()!=cells[col];if(!item){item=new QTableWidgetItem;table_->setItem(row,col,item);}if(changed)item->setText(cells[col]);if(item->data(Qt::UserRole).toString()!=id)item->setData(Qt::UserRole,id);const auto tip="Exact request: "+id+"\nOwned lower-prime interval: ["+lease.value("start").toString()+", "+lease.value("end").toString()+")";if(item->toolTip()!=tip)item->setToolTip(tip);if(col==3){if(!auditState.isEmpty())presentAuditStatus(item,auditReportPresentation(auditState));else if(audited)presentAuditStatus(item,auditReportPresentation(QJsonObject{{"audited",true}}));else if(submitted)presentAuditStatus(item,auditStatusPresentation("awaiting"));else presentAuditStatus(item,auditStatusPresentation("assigned"));}}
            table_->item(row,2)->setData(Qt::UserRole+1,QStringList{positions.human,positions.decimal,positions.binary});
        }
        table_->setRowCount(renderedRows);
        compactRows();
        bool selectedVisible=false;for(int row=0;row<table_->rowCount();++row)if(table_->item(row,0)->data(Qt::UserRole).toString()==selectedId_){table_->selectRow(row);selectedVisible=true;break;}
        if(!selectedVisible)table_->clearSelection();
        table_->setUpdatesEnabled(true);summary_->setText(QString("%1 shown on this page · %2 tracked ranges · automatic allocation · manual reservations disabled").arg(table_->rowCount()).arg(snapshot_.value("active_lease_count").toInteger(snapshot_.value("leases").toArray().size())));
    }
    void inspect(const QString& id){
        if(atlas_)atlas_->setSelected(id);
        const bool changed=selectedId_!=id;selectedId_=id;const QPointer<NetworkWorkView> guard(this);
        if(changed){const QSignalBlocker guard(table_);table_->clearSelection();for(int row=0;row<table_->rowCount();++row)if(table_->item(row,0)->data(Qt::UserRole).toString()==id){table_->selectRow(row);break;}}
        if(changed){openBlock_->setEnabled(false);copy_->setEnabled(false);status_->setText("Reading report and audit details…");if(!detailCache_.contains(id)){details_->setUpdatesEnabled(false);details_->clear();details_->setProperty("pgn_evidence_id",QString{});for(const auto& item:snapshot_.value("leases").toArray()){const auto lease=item.toObject();if(lease.value("request").toString()!=id)continue;new QTreeWidgetItem(details_,{"Ticket",lease.value("ticket_number").toString()});new QTreeWidgetItem(details_,{"Owned interval","["+lease.value("start").toString()+", "+lease.value("end").toString()+")"});new QTreeWidgetItem(details_,{"Stage",lease.value("state").toString()});if(lease.contains("report_root"))new QTreeWidgetItem(details_,{"Committed report",lease.value("report_root").toString()});break;}details_->setUpdatesEnabled(true);}}
        auto consume=[guard,id](QJsonObject value,QString error){if(!guard||guard->selectedId_!=id)return;if(!error.isEmpty()){guard->status_->setText(error);return;}const auto old=guard->selected_;const bool same=guard->details_->property("pgn_evidence_id").toString()==id&&old.value("leases")==value.value("leases")&&old.value("reports")==value.value("reports")&&old.value("audits")==value.value("audits");guard->selected_=value;guard->setProperty("pgn_selected_ticket_evidence",value);if(same){guard->status_->setText(QString("Evidence verified at ledger record %1 · %2").arg(value.value("height").toInteger()).arg(value.value("bestblockhash").toString().left(12)));return;}const int scroll=guard->details_->verticalScrollBar()->value();guard->details_->setUpdatesEnabled(false);guard->details_->clear();guard->setProperty("pgn_ticket_detail_rebuilds",guard->property("pgn_ticket_detail_rebuilds").toInt()+1);guard->details_->setProperty("pgn_evidence_id",id);const auto lease=value.value("leases").toArray().first().toObject();
            const auto field=[guard](const QString& name,const QString& text){return new QTreeWidgetItem(guard->details_,QStringList{name,text});};
            const auto coordinates=positionRangeFormats(lease.value("start").toString(),lease.value("end").toString());
            field("Ticket",lease.value("ticket_number").toString());field("Ticket request",id);field("Owned interval","["+lease.value("start").toString()+", "+lease.value("end").toString()+")");field("Start · scientific / binary",coordinates.start.decimal+" · "+coordinates.start.binary);field("End · scientific / binary",coordinates.end.decimal+" · "+coordinates.end.binary);field("Issued at ledger record",QString::number(lease.value("issued").toInteger()));field(computeLeaseDeadlineHeading(lease),computeLeaseDeadlineText(lease));if(lease.contains("report_root"))field("Audit request retention","Pending until processed · no expiry of the submitted report");if(const auto name=publicReportName(value))field("Public discoverer name",*name);field("Owner signing script",lease.value("miner_script").toString());field("Reward signing script",lease.value("reward_script").toString());
            const auto reports=value.value("reports").toArray();if(reports.isEmpty())field("Report","Not submitted");else{const auto report=reports.first().toObject();field("Immutable report root",report.value("report_root").toString());field("Report confirmation",QString::number(report.value("height").toInteger()));guard->block_=report.value("block").toString();field("Confirming block",guard->block_);guard->openBlock_->setEnabled(!guard->block_.isEmpty());}
            const auto audits=value.value("audits").toArray();if(audits.isEmpty())field("Audits","Waiting for a committed report");else{const auto audit=audits.first().toObject();presentAuditStatus(field("Audit verdict",{}),auditReportPresentation(audit));const auto jobs=audit.value("jobs").toArray();if(jobs.isEmpty())field("Audit allocation","No independent auditor assigned");for(const auto& item:jobs){const auto job=item.toObject();auto* parent=field(QString("Audit %1 · attempt %2").arg(job.value("slot").toInt()+1).arg(job.value("attempt").toInt()+1),job.value("state").toString());presentAuditStatus(parent,auditStatusPresentation(job.value("state").toString()));new QTreeWidgetItem(parent,{"Auditor",job.value("actor").toString()});const auto leaseLine=auditAssignmentLeaseLine(job);new QTreeWidgetItem(parent,{leaseLine.first,leaseLine.second});for(const auto& r:job.value("ranges").toArray()){const auto range=r.toObject();positionRangeItem(parent,QString("Partition %1").arg(range.value("index").toInt()),range.value("start").toString(),range.value("end").toString(),true);}parent->setExpanded(true);}}
            guard->details_->verticalScrollBar()->setValue(scroll);guard->details_->setUpdatesEnabled(true);guard->copy_->setEnabled(true);guard->status_->setText(QString("Evidence verified at ledger record %1 · %2").arg(value.value("height").toInteger()).arg(value.value("bestblockhash").toString().left(12)));
        };
        // The atomic page already carries the matching reports and audit jobs.
        // Project one ticket from that same anchor instead of issuing N RPCs.
        const auto complete=pageDetail(id);if(!complete.isEmpty()){consume(complete,{});return;}
        const auto cached=detailCache_.value(id);if(!cached.isEmpty()){consume(cached,{});if(cached.value("bestblockhash")==snapshot_.value("bestblockhash"))return;status_->setText("Showing verified evidence · checking for updates…");}
        detailReader_(id,[guard,id,consume](QJsonObject value,QString error){if(!guard||guard->selectedId_!=id)return;if(error.isEmpty())guard->remember(id,value);consume(value,error);});
    }
    void refresh(){if(busy_)return;busy_=true;const int page=offset_;const QPointer<NetworkWorkView> guard(this);pageReader_(page,[guard,page](QJsonObject state,QString error){
        if(!guard)return;guard->busy_=false;if(page!=guard->offset_){guard->refresh();return;}if(!error.isEmpty()){if(guard->snapshot_.isEmpty())guard->timer_.setInterval(500);guard->status_->setText(error);return;}
        guard->timer_.setInterval(5000);const bool changed=guard->snapshot_.value("bestblockhash")!=state.value("bestblockhash")||guard->renderedOffset_!=page;guard->snapshot_=state;guard->renderedOffset_=page;
        if(changed)guard->render();guard->previous_->setEnabled(page>0);guard->next_->setEnabled(state.value("leases").toArray().size()==100);
        if(guard->selectedId_.isEmpty()&&guard->table_->rowCount()){guard->table_->selectRow(0);}else if(changed&&!guard->selectedId_.isEmpty())guard->inspect(guard->selectedId_);
        guard->prefetchVisible();
    });}

protected:
    bool eventFilter(QObject* watched,QEvent* event)override{
        if(watched==table_->viewport()&&(event->type()==QEvent::Resize||event->type()==QEvent::FontChange))scheduleCompactRows();
        return QWidget::eventFilter(watched,event);
    }
    void resizeEvent(QResizeEvent* event)override{QWidget::resizeEvent(event);if(!content_)return;const auto orientation=width()>=1050?Qt::Horizontal:Qt::Vertical;if(content_->orientation()!=orientation){content_->setOrientation(orientation);content_->setSizes(orientation==Qt::Horizontal?QList<int>{850,420}:QList<int>{320,220});}}
    void showEvent(QShowEvent* event)override{QWidget::showEvent(event);QTimer::singleShot(0,this,[this]{refresh();});}
public:
    NetworkWorkView(PageReader pageReader,DetailReader details,QWidget* parent=nullptr,TicketAtlasView::Reader atlas={}):QWidget(parent),pageReader_(std::move(pageReader)),detailReader_(std::move(details)){
        setObjectName("pgn-network-work-explorer");auto* box=new QVBoxLayout(this);box->setContentsMargins(18,14,18,14);auto* top=new QHBoxLayout;search_=new QLineEdit;search_->setPlaceholderText("Search this page by ticket, interval or owner script");filter_=new QComboBox;filter_->setObjectName("pgn-network-ticket-filter");filter_->addItems({"All work","Assigned","Report submitted","Audits passed"});auto* refreshButton=new QPushButton("Refresh work");top->addWidget(search_,1);top->addWidget(filter_);top->addWidget(refreshButton);box->addLayout(top);status_=new QLabel("Connecting to verified network work…");summary_=new QLabel;summary_->setWordWrap(true);box->addWidget(status_);box->addWidget(summary_);
        workspace_=new QSplitter(Qt::Vertical);workspace_->setObjectName("pgn-network-map-split");workspace_->setChildrenCollapsible(false);workspace_->setHandleWidth(6);
        if(atlas){atlas_=new TicketAtlasView(std::move(atlas));atlas_->inspect=[this](const QString& id){inspect(id);};workspace_->addWidget(atlas_);}
        content_=new QSplitter(Qt::Horizontal);content_->setObjectName("pgn-network-evidence-split");content_->setChildrenCollapsible(false);content_->setHandleWidth(6);workspace_->addWidget(content_);box->addWidget(workspace_,1);
        table_=new QTableWidget(0,6);table_->setObjectName("pgn-network-ticket-table");table_->setHorizontalHeaderLabels({"Ticket","Owned interval","Positions","Stage","Issued","Compute lease"});table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);table_->verticalHeader()->setMinimumSectionSize(22);table_->verticalHeader()->hide();table_->setWordWrap(false);table_->viewport()->installEventFilter(this);table_->setSelectionBehavior(QAbstractItemView::SelectRows);table_->setSelectionMode(QAbstractItemView::SingleSelection);table_->setStyleSheet("QTableWidget#pgn-network-ticket-table{font:12px 'Segoe UI';selection-background-color:#245f68;selection-color:#ffffff;} QTableWidget#pgn-network-ticket-table::item{padding:0px 6px;} QTableWidget#pgn-network-ticket-table::item:selected,QTableWidget#pgn-network-ticket-table::item:selected:!active{background-color:#245f68;color:#ffffff;border-top:1px solid #5be3cc;border-bottom:1px solid #5be3cc;}");table_->setEditTriggers(QAbstractItemView::NoEditTriggers);table_->setMinimumSize(300,160);content_->addWidget(table_);
        details_=new QTreeWidget;details_->setObjectName("pgn-network-ticket-evidence");details_->setHeaderLabels({"Evidence","Value"});details_->setColumnWidth(0,175);details_->setWordWrap(true);details_->setMinimumSize(280,140);details_->setStyleSheet("QTreeWidget{font:12px 'Segoe UI';} QTreeWidget::item{padding:3px 1px;}");content_->addWidget(details_);content_->setStretchFactor(0,2);content_->setStretchFactor(1,1);content_->setSizes({850,420});if(atlas_){workspace_->setStretchFactor(0,0);workspace_->setStretchFactor(1,1);workspace_->setSizes({200,650});}
        auto* actions=new QHBoxLayout;previous_=new QPushButton("Previous page");next_=new QPushButton("Next page");openBlock_=new QPushButton("Open block");openBlock_->setToolTip("Open the selected report's confirming block");copy_=new QPushButton("Copy evidence");auto* audits=new QPushButton("Audit explorer");for(auto* b:{previous_,next_,openBlock_,copy_,audits})actions->addWidget(b);box->addLayout(actions);previous_->setEnabled(false);next_->setEnabled(false);openBlock_->setEnabled(false);copy_->setEnabled(false);
        connect(table_->verticalScrollBar(),&QScrollBar::valueChanged,this,[this]{prefetchVisible();});
        connect(search_,&QLineEdit::textChanged,this,[this]{render();});connect(filter_,&QComboBox::currentIndexChanged,this,[this]{render();});connect(refreshButton,&QPushButton::clicked,this,[this]{refresh();});connect(table_,&QTableWidget::itemSelectionChanged,this,[this]{const auto rows=table_->selectionModel()->selectedRows();if(!rows.isEmpty())if(auto* item=table_->item(rows.first().row(),0))inspect(item->data(Qt::UserRole).toString());});connect(previous_,&QPushButton::clicked,this,[this]{offset_=std::max(0,offset_-100);refresh();});connect(next_,&QPushButton::clicked,this,[this]{offset_+=100;refresh();});connect(openBlock_,&QPushButton::clicked,this,[this]{if(onOpenBlock)onOpenBlock(block_);});connect(audits,&QPushButton::clicked,this,[this]{if(onOpenAudits)onOpenAudits();});connect(copy_,&QPushButton::clicked,this,[this]{QApplication::clipboard()->setText(QString::fromUtf8(QJsonDocument(selected_).toJson(QJsonDocument::Indented)));});timer_.setInterval(500);connect(&timer_,&QTimer::timeout,this,[this]{if(isVisible()||snapshot_.isEmpty())refresh();});timer_.start();QTimer::singleShot(0,this,[this]{refresh();});
    }
};
}
