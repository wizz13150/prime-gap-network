#include "ui/community_view.h"
#include "ui/position_formats.h"
#include "ui/panel_style.h"
#include "ui/booking_history_view.h"
#include "ui/reservation_directory_view.h"
#include "ui/range_calendar.h"
#include "ui/booking_labels.h"
#include "ui/booking_rail_layout.h"
#include "ui/booking_context_menu.h"
#include "ui/round_range_navigation.h"
#include "horizon_workspace_style.h"
#include "ui/queue_view.h"
#include "ui/researcher_profile_view.h"
#include "booking/booking_model.h"
#include "booking/decimal_range.h"
#include "booking/range_identity.h"
#include "booking/catalog_coordinates.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QSplitter>
#include <QScrollArea>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QMessageBox>
#include <QPainter>
#include <QMouseEvent>
#include <QToolTip>
#include <QTimer>
#include <QFrame>
#include <QFont>
#include <QDialog>
#include <QApplication>
#include <QClipboard>
#include <QTabWidget>
#include <QScrollArea>
#include <algorithm>

namespace pgn::ui {
namespace {
QColor statusColor(const QString& state){return state=="Complete"?QColor("#69efc5"):state=="Reserved"?QColor("#ffad4a"):QColor("#287e91");}
class CommunityView final:public QWidget {
public:
    CommunityView(pgn::booking::BookingModel* model,QWidget* parent,DeviceAccent accent):QWidget(parent),model_(model),accent_(std::move(accent)){
        if(!model_->viewBitLength()&&!model_->viewIsRoundRange()&&!model_->source().ranges.isEmpty())model_->setRoundRange(pgn::booking::decimalZoneIndex(model_->source().ranges.first().start)/1000);
        setStyleSheet(dataPanelStyle());
        auto* outer=new QVBoxLayout(this);outer->setContentsMargins(12,4,12,4);outer->setSpacing(2);
        auto* headingRow=new QHBoxLayout;
        auto* title=new QLabel("COVERAGE · TICKETS & HISTORY",this);title->setStyleSheet("font:700 16px 'Segoe UI';color:#e9fffb");headingRow->addWidget(title,1);
        auto* sourceNotice=new QLabel(this);sourceNotice->setStyleSheet("color:#8db5bd;font-size:10px;");headingRow->addWidget(sourceNotice);outer->addLayout(headingRow);
        const auto& source=model_->source();
        sourceNotice->setText(source.valid()?QString("%1 zones · public register read-only").arg(source.ranges.size()):
            "SOURCE UNAVAILABLE · "+source.error);
        sourceNotice->setToolTip("Snapshot "+source.observedUtc.toUTC().toString(Qt::ISODate)+" · declared status is not certified coverage. Local queue does not create a public reservation.");
        activeWork_=new QLabel;activeWork_->setObjectName("pgn-booking-active-work");
        activeWork_->setStyleSheet("background:#0b3033;border:1px solid #42c7a7;border-radius:9px;padding:4px;color:#d8fff4;font-weight:600;font-size:11px;");outer->addWidget(activeWork_);
        auto* split=new QSplitter(this);outer->addWidget(split,1);
        auto* registry=new QWidget(split);auto* registryLayout=new QVBoxLayout(registry);registryLayout->setContentsMargins(4,0,0,0);registryLayout->setSpacing(4);
        registryLayout->addWidget(createRoundRangeNavigation(model,registry));
        auto inspect=[this](const QString& first,const QString& last){selectRangeCalendar(findChild<QWidget*>("pgn-range-calendar-grid"),first,last);start_->setText(first);end_->setText(last);reviewSelection();};
        atlas_=createRangeAtlas(model,false,[this](const QString& first,const QString& last){
            const auto selected=pgn::booking::identifyRange(model_->source(),first,last);
            if(selected.firstBlock&&selected.firstBlock==selected.lastBlock&&!selected.title.contains("partial")){
                selectRangeCalendar(findChild<QWidget*>("pgn-range-calendar-grid"),first,last);
                start_->setText(first);end_->setText(last);reviewSelection();return;
            }
            inspectRangeCalendar(findChild<QWidget*>("pgn-range-calendar-grid"),first,last);
            start_->clear();end_->clear();const auto identity=pgn::booking::identifyRange(model_->source(),first,last);
            selectionSummary_->setText("INSPECTING · "+identity.title+"\n"+identity.declaration+" · choose blocks on the left");selectionSummary_->setToolTip(identity.exact);
        },accent_);registryLayout->addWidget(atlas_,1);
        auto* workHeading=new QHBoxLayout;workHeading->addStretch();
        auto* minerLookup=new QPushButton("Find a miner's reservations");minerLookup->setObjectName("pgn-owner-directory-open");minerLookup->setStyleSheet("padding:4px;");workHeading->addWidget(minerLookup);
        connect(minerLookup,&QPushButton::clicked,this,[this]{
            if(ownerDirectory_){ownerDirectory_->show();ownerDirectory_->raise();ownerDirectory_->activateWindow();return;}
            auto* dialog=new QDialog(this);ownerDirectory_=dialog;dialog->setObjectName("pgn-owner-directory-dialog");dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowTitle("PGN · reservations by miner · all known ranges");dialog->resize(1220,650);
            auto* layout=new QVBoxLayout(dialog);layout->addWidget(createReservationDirectory(model_,[this](const QString& first,const QString& last){model_->setRoundRange(pgn::booking::decimalZoneIndex(first)/1000);inspectRangeCalendar(findChild<QWidget*>("pgn-range-calendar-grid"),first,last);focusRangeAtlas(atlas_,first,last);},dialog));dialog->show();
        });
        auto* registerToggle=new QPushButton("Public register details");registerToggle->setObjectName("pgn-public-register-open");registerToggle->setStyleSheet("padding:4px;");workHeading->addWidget(registerToggle);registryLayout->insertLayout(1,workHeading);
        auto* registerPanel=new QDialog(this);registerPanel->setObjectName("pgn-public-register-dialog");registerPanel->setWindowTitle("PGN · public declarations · read-only");registerPanel->resize(1100,650);auto* registerBox=new QVBoxLayout(registerPanel);
        auto* registerNotice=new QLabel("PUBLIC REGISTER · number ranges, zones and blocks\nHistorical declarations; a declared completion is not a mathematical proof or a live network lease.");registerNotice->setWordWrap(true);registerBox->addWidget(registerNotice);
        connect(registerToggle,&QPushButton::clicked,registerPanel,[registerPanel]{registerPanel->show();registerPanel->raise();registerPanel->activateWindow();});
        auto* filterControls=new QWidget(registerPanel);filterControls->setStyleSheet(HorizonWorkspaceControlStyle());auto* filters=new QHBoxLayout(filterControls);filters->setContentsMargins(0,0,0,0);search_=new QLineEdit;search_->setPlaceholderText("Researcher or exact endpoint…");
        state_=new QComboBox;state_->addItems({"All declarations","Open","Reserved","Complete"});
        search_->setObjectName("pgn-public-register-search");state_->setObjectName("pgn-public-register-state");search_->setPlaceholderText("Researcher, range, zone, block or exact endpoint…");
        auto* exactColumns=new QCheckBox("Exact endpoints");exactColumns->setObjectName("pgn-public-register-exact");
        filters->addWidget(search_,1);filters->addWidget(state_);filters->addWidget(exactColumns);registerBox->addWidget(filterControls);
        table_=new QTableWidget(0,6);table_->setObjectName("pgn-public-register-results");table_->setHorizontalHeaderLabels({"Number range","Zones / blocks","Declaration","Researcher","Exact start (included)","Exact end (excluded)"});
        table_->setSelectionBehavior(QAbstractItemView::SelectRows);table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table_->verticalHeader()->hide();table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        table_->horizontalHeader()->setStretchLastSection(false);table_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);table_->setColumnHidden(4,true);table_->setColumnHidden(5,true);table_->setAlternatingRowColors(true);registerBox->addWidget(table_,1);
        connect(exactColumns,&QCheckBox::toggled,this,[this](bool visible){table_->setColumnHidden(4,!visible);table_->setColumnHidden(5,!visible);});
        auto* pages=new QHBoxLayout;auto* previous=new QPushButton("← Previous");auto* next=new QPushButton("Next →");pageLabel_=new QLabel;
        auto* copyExact=new QPushButton("Copy selected interval");copyExact->setObjectName("pgn-public-register-copy");copyExact->setEnabled(false);
        pages->addWidget(previous);pages->addWidget(pageLabel_,1);pages->addWidget(copyExact);pages->addWidget(next);registerBox->addLayout(pages);
        connect(table_,&QTableWidget::itemSelectionChanged,this,[this,copyExact]{const int row=table_->currentRow();copyExact->setEnabled(row>=0&&page_*100+row<filtered_.size());});
        connect(copyExact,&QPushButton::clicked,this,[this]{const int row=table_->currentRow(),index=page_*100+row;if(row<0||index>=filtered_.size())return;const auto& range=model_->source().ranges[filtered_[index]];QApplication::clipboard()->setText("["+range.start+", "+range.end+")");});
        auto* choiceScroll=new QScrollArea(split);choiceScroll->setObjectName("pgn-booking-choice-scroll");choiceScroll->setWidgetResizable(true);choiceScroll->setFrameShape(QFrame::NoFrame);choiceScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);choiceScroll->setMinimumWidth(330);choiceScroll->setMaximumWidth(430);
        auto* local=new QWidget;local->setObjectName("pgn-booking-choice-panel");auto* localLayout=new QVBoxLayout(local);localLayout->setContentsMargins(0,0,4,8);localLayout->setSpacing(4);localLayout->setSizeConstraint(QLayout::SetMinAndMaxSize);choiceScroll->setWidget(local);split->insertWidget(0,choiceScroll);
        auto* selectionHeading=new QHBoxLayout;auto* heading=new QLabel("EXPLORE COVERAGE");heading->setStyleSheet("font:700 11px 'Segoe UI';color:#69efc5");selectionHeading->addWidget(heading,1);
        bookingDevice_=new QLabel;bookingDevice_->setObjectName("pgn-booking-device");bookingDevice_->setStyleSheet("font-size:10px;color:#91c4cc;");bookingDevice_->setTextInteractionFlags(Qt::TextSelectableByMouse);selectionHeading->addWidget(bookingDevice_);localLayout->addLayout(selectionHeading);
        auto* calendarPanel=createRangeCalendar(model,inspect,accent_,local);
        auto* calendarGrid=calendarPanel->findChild<QWidget*>("pgn-range-calendar-grid");
        bindRangeCalendarBrowse(calendarGrid,[this](const QString& first,const QString& last){focusRangeAtlas(atlas_,first,last);});
        // One browsing context in both directions. Explicit actions move the
        // camera; live progress packets never override a user's zoom/selection.
        connect(findChild<QPushButton*>("pgn-source-atlas-focus"),&QPushButton::clicked,calendarGrid,[calendarGrid]{focusCurrentRangeCalendar(calendarGrid);});
        localLayout->addWidget(calendarPanel);
        if(model_->manualReservationsAllowed())localLayout->addWidget(createQueueView(model_,inspect,local));
        else{auto* policy=new QLabel("AUTOMATIC PGN TICKETS\nWork is assigned by consensus. Browsing a range does not reserve it. Manual reservations are not permitted.\nExisting local plans and completed work remain in Work history.");policy->setObjectName("pgn-ticket-allocation-policy");policy->setWordWrap(true);policy->setStyleSheet("color:#69efc5;padding:8px;border:1px solid #24615f;border-radius:6px;");localLayout->addWidget(policy);}
        calendarPanel->findChild<QTabBar*>("pgn-catalog-bits")->hide();
        // One visible review owns selection presentation; the standalone
        // calendar label remains available to its component contract/tests.
        calendarPanel->findChild<QLabel*>("pgn-calendar-selection")->hide();
        calendarPanel->findChild<QLabel*>("pgn-calendar-heading")->hide();
        selectionSummary_=new QLabel;selectionSummary_->setObjectName("pgn-booking-review");selectionSummary_->setWordWrap(true);
        selectionSummary_->setStyleSheet("background:#0c3035;border:1px solid #337c83;border-radius:9px;padding:4px;color:#e0fff5;font-size:10px;");localLayout->addWidget(selectionSummary_);
        start_=new QLineEdit;start_->setPlaceholderText("Exact start (integer)");end_=new QLineEdit;end_->setPlaceholderText("Exact end (exclusive integer)");
        start_->setObjectName("pgn_booking_start");end_->setObjectName("pgn_booking_end");
        auto* scope=new QLabel("Read-only coverage · ticket assignments come from consensus");scope->setWordWrap(true);scope->setStyleSheet("color:#efbc71;font-size:10px;");localLayout->addWidget(scope);
        auto* exactToggle=new QPushButton("Enter exact endpoints…");exactToggle->setStyleSheet("padding:5px;");exactToggle->setObjectName("pgn-booking-exact-toggle");exactToggle->setToolTip("Advanced selection by exact integers instead of calendar blocks. Does not reserve or start mining.");
        auto* exactPanel=new QDialog(this);exactPanel->setObjectName("pgn-booking-exact-dialog");exactPanel->setWindowTitle("PGN · exact selected interval");exactPanel->setMinimumSize(500,360);exactPanel->resize(520,380);auto* exactLayout=new QVBoxLayout(exactPanel);
        auto* explanation=new QLabel("Inspect an interval inside one displayed zone by exact endpoints. Apply highlights its coverage and focuses the same interval in the overview. This is read-only: PGN work is assigned through consensus tickets.");explanation->setWordWrap(true);exactLayout->addWidget(explanation);
        exactLayout->addWidget(new QLabel("START · inclusive integer"));exactLayout->addWidget(start_);exactLayout->addWidget(new QLabel("END · exclusive integer"));exactLayout->addWidget(end_);auto* exactError=new QLabel;exactError->setWordWrap(true);exactError->setStyleSheet("color:#ffad4a");exactLayout->addWidget(exactError);auto* exactDone=new QPushButton("Apply to block selection");exactDone->setObjectName("pgn-booking-exact-apply");exactLayout->addWidget(exactDone);
        connect(exactToggle,&QPushButton::clicked,exactPanel,[exactPanel]{exactPanel->show();exactPanel->raise();exactPanel->activateWindow();});
        start_->setMinimumHeight(32);end_->setMinimumHeight(32);exactDone->setMinimumHeight(32);
        connect(exactDone,&QPushButton::clicked,exactPanel,[this,exactPanel,exactError]{
            const auto identity=pgn::booking::identifyRange(model_->source(),start_->text().trimmed(),end_->text().trimmed());
            if(!identity.valid){exactError->setText("Enter positive integers with end greater than start.");return;}
            const auto first=start_->text().trimmed(),last=end_->text().trimmed();
            if(!selectRangeCalendar(findChild<QWidget*>("pgn-range-calendar-grid"),first,last)){
                exactError->setText("The interval must fit inside one zone of the current number range. Navigate to that range first, or choose fewer blocks. Nothing was reserved or started.");return;
            }
            focusRangeAtlas(atlas_,first,last);start_->setText(first);end_->setText(last);reviewSelection();exactError->clear();exactPanel->hide();
        });
        connect(start_,&QLineEdit::textChanged,this,[this]{reviewSelection();});connect(end_,&QLineEdit::textChanged,this,[this]{reviewSelection();});
        progress_=new QLabel;progress_->setStyleSheet("font-size:10px;color:#8dafb6;");registryLayout->addWidget(progress_);
        auto* actions=new QHBoxLayout;auto* history=new QPushButton("Work history…");history->setStyleSheet("padding:5px;");history->setObjectName("pgn-booking-history-open");actions->addWidget(exactToggle);actions->addWidget(history);
        auto* profile=new QPushButton("Name…");profile->setObjectName("pgn-researcher-profile-open");profile->setToolTip("Change your local display name. Does not change keys, public identities or reservations.");actions->addWidget(profile);localLayout->addLayout(actions);
        connect(profile,&QPushButton::clicked,this,[this]{
            if(auto* existing=findChild<QDialog*>("pgn-researcher-profile")){existing->show();existing->raise();existing->activateWindow();return;}
            createResearcherProfileDialog(model_,this)->show();
        });
        auto* message=new QLabel;message->setObjectName("pgn-booking-message");message->setWordWrap(true);message->hide();localLayout->addWidget(message);
        const auto context=[this,message](const BookingContextRequest& request){
            start_->setText(request.start);end_->setText(request.end);reviewSelection();
            auto* menu=createBookingContextMenu(this,model_,request,[message](const QString& text){
                message->setText(text);message->show();
                QTimer::singleShot(7000,message,[message]{message->hide();});
            });menu->popup(request.global);
        };
        bindRangeCalendarContext(calendarPanel->findChild<QWidget*>("pgn-range-calendar-grid"),context);
        bindRangeAtlasContext(atlas_,context);
        bindRangeCalendarClear(calendarPanel->findChild<QWidget*>("pgn-range-calendar-grid"),[this]{start_->clear();end_->clear();reviewSelection();});
        bindRangeCalendarSelection(calendarGrid,[this,calendarGrid](const QVector<RangeCalendarSlot>& cells){if(cells.size()>1&&!rangeCalendarSelection(calendarGrid))selectionSummary_->setText(QString("%1 blocks selected · exact, non-contiguous coverage\nRead-only inspection · PGN assignments are automatic.").arg(cells.size()));});
        connect(history,&QPushButton::clicked,this,[this]{
            if(historyDialog_){historyDialog_->show();historyDialog_->raise();historyDialog_->activateWindow();return;}
            auto* dialog=new QDialog(this);historyDialog_=dialog;dialog->setObjectName("pgn-booking-history-dialog");dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowTitle("PGN · local booking history");dialog->resize(1280,720);
            auto* layout=new QVBoxLayout(dialog);layout->setContentsMargins(0,0,0,0);layout->addWidget(createBookingHistoryView(model_->historyDatabase(),dialog));dialog->show();});
        connect(previous,&QPushButton::clicked,this,[this]{if(page_>0){--page_;renderRows();}});
        connect(next,&QPushButton::clicked,this,[this]{if((page_+1)*100<filtered_.size()){++page_;renderRows();}});
        connect(search_,&QLineEdit::textChanged,this,[this]{filter();});connect(state_,&QComboBox::currentTextChanged,this,[this]{filter();});
        connect(table_,&QTableWidget::cellDoubleClicked,this,[this,registerPanel](int row,int){
            const int index=page_*100+row;if(row<0||index>=filtered_.size())return;
            const auto range=model_->source().ranges[filtered_[index]];
            const auto pageOf=[](const QString& n){return n.size()<=20?QString("0"):n.left(n.size()-20);};
            const auto page=pageOf(range.start),lastPage=pageOf(pgn::booking::subtractUnsigned(range.end,"1"));
            if(page!=lastPage||pgn::booking::compareUnsigned(page,QString::number(pgn::booking::roundRangeMaximumPage))>0){pageLabel_->setText("Exact interval retained · spans ranges or lies outside the graph catalogue");return;}
            model_->setRoundRange(page.toULongLong());
            auto* calendar=findChild<QWidget*>("pgn-range-calendar-grid");
            if(!selectRangeCalendar(calendar,range.start,range.end))inspectRangeCalendar(calendar,range.start,range.end);
            start_->setText(range.start);end_->setText(range.end);focusRangeAtlas(atlas_,range.start,range.end);registerPanel->hide();
        });
        table_->setToolTip("Double-click a declaration to inspect its exact interval in the graph. This does not reserve or start work.");
        localLayout->addStretch();split->setStretchFactor(0,0);split->setStretchFactor(1,1);split->setSizes({400,950});
        sourceKey_=source.sha256+"\n"+source.error;
        model_->subscribe(this,[this,sourceNotice]{const auto& current=model_->source();const auto key=current.sha256+"\n"+current.error;
            if(key!=sourceKey_){sourceKey_=key;sourceNotice->setText(current.valid()?QString("%1 zones · public register read-only").arg(current.ranges.size()):"SOURCE UNAVAILABLE · "+current.error);filter();reviewSelection();}
            if(isVisible())renderObservedWork();else dirty_=true;});filter();renderObservedWork();reviewSelection();
    }
    void select(const QString& id){for(const auto& job:model_->jobs())if(job.id==id){selectRangeCalendar(findChild<QWidget*>("pgn-range-calendar-grid"),job.start,job.end);start_->setText(job.start);end_->setText(job.end);reviewSelection();focusRangeAtlas(atlas_,job.start,job.end);break;}}
protected:
    void showEvent(QShowEvent* event)override{QWidget::showEvent(event);if(dirty_){dirty_=false;renderObservedWork();}}
private:
    void reviewSelection(){if(!selectionSummary_||!start_||!end_)return;
        const auto first=start_->text().trimmed(),last=end_->text().trimmed();
        if(first.isEmpty()||last.isEmpty()){selectionSummary_->setText("INSPECT A RANGE\nSelect a block or drag across neighbouring blocks to inspect coverage.");return;}
        const auto identity=pgn::booking::identifyRange(model_->source(),first,last);
        if(!identity.valid){selectionSummary_->setText("2  REVIEW · enter positive exact bounds, with end after start.");return;}
        const auto length=pgn::booking::subtractUnsigned(last,first);
        QString conflict;
        for(const auto& job:model_->jobs())if(job.state!="RELEASED"&&pgn::booking::overlaps(first,last,job.start,job.end)){conflict="Already in our work · "+localWorkLabel(job.state);break;}
        selectionSummary_->setText("2  SELECTED · "+identity.title+"\n"+(conflict.isEmpty()?identity.declaration:conflict));
        selectionSummary_->setToolTip(identity.exact+"\n"+identity.declaration+"\n"+positionFormats(length).stackedText()+" positions\nExact count: "+length+". Zone numbers refer to this snapshot; bits are exact number size, not difficulty. No automatic reservation.");
    }
    void filter(){filtered_.clear();page_=0;const auto query=search_->text();const auto status=state_->currentText();
        for(int i=0;i<model_->source().ranges.size();++i){const auto& r=model_->source().ranges[i];if(status!="All declarations"&&status!=r.status)continue;
            const auto coordinates=pgn::booking::catalogCoordinates(r.start,r.end);
            if(!query.isEmpty()&&!r.owner.contains(query,Qt::CaseInsensitive)&&!r.start.contains(query)&&!r.end.contains(query)&&!coordinates.numberRange.contains(query,Qt::CaseInsensitive)&&!coordinates.cells.contains(query,Qt::CaseInsensitive))continue;filtered_.append(i);}renderRows();}
    void renderRows(){table_->setUpdatesEnabled(false);table_->setRowCount(0);
        for(int i=page_*100;i<std::min((page_+1)*100,int(filtered_.size()));++i){const auto& r=model_->source().ranges[filtered_[i]];const int row=table_->rowCount();table_->insertRow(row);
            const auto coordinates=pgn::booking::catalogCoordinates(r.start,r.end);
            const QStringList values{coordinates.valid?coordinates.numberRange:QString("Unavailable"),coordinates.valid?coordinates.cells:QString("Exact interval"),"DECLARED "+r.status.toUpper(),r.owner.isEmpty()?QString("No listed owner"):r.owner,r.start,r.end};for(int c=0;c<values.size();++c){auto* item=new QTableWidgetItem(values[c]);item->setToolTip(coordinates.exact+"\nPublic snapshot row "+QString::number(r.row)+" · declaration, not verified coverage");if(c==2)item->setForeground(statusColor(r.status));table_->setItem(row,c,item);}}
        table_->setUpdatesEnabled(true);pageLabel_->setText(QString("%1 rows · page %2 / %3 · exact endpoints retained").arg(filtered_.size()).arg(page_+1).arg(std::max<qsizetype>(1,(filtered_.size()+99)/100)));}
    void renderObservedWork(){
        // Read-only observed status; reservations are rendered once by QueueView.
        const auto& observed=model_->runtime();
        const auto deviceText=observed.device.isEmpty()?QStringLiteral("GPU · AWAITING OBSERVED IDENTITY"):
            QStringLiteral("GPU · %1").arg(observed.label);
        if(bookingDevice_->text()!=deviceText)bookingDevice_->setText(deviceText);
        bookingDevice_->setToolTip(observed.device);
        const auto identity=pgn::booking::identifyRange(model_->source(),observed.start,observed.end);
        QString jobState;for(const auto& job:model_->jobs())if(job.id==observed.run){jobState=job.state;break;}
        activeWork_->setText(observed.run.isEmpty()?QString("NO ACTIVE TICKET · automatic assignment by consensus"):
            QString("%1 · %2 · %3 · %4")
                .arg(pgn::ui::bookingRailRuntimeDetail(observed.running,observed.fraction,jobState),observed.label,identity.title,
                    observed.fraction?QString::number(*observed.fraction*100,'f',4)+"%":"progress unavailable"));
        activeWork_->setToolTip(identity.exact+"\nCommunity: "+identity.declaration+" · local work, not a public reservation\nRaw observation "+observed.observedUtc.toUTC().toString(Qt::ISODateWithMs));
        // QueueView owns future reservations; there is no second card/pager model.
        setProperty("pgn_reservation_future_card_count",0);
        if(!progress_)return;const auto& progress=model_->runtime();
        progress_->setText(progress.run.isEmpty()?"No GPU work observed":QString("Raw progress · %1 · observed %2 · independent proof pending")
            .arg(progress.fraction?QString::number(*progress.fraction*100,'f',5)+"%":"unavailable",
                progress.observedUtc.isValid()?progress.observedUtc.toUTC().toString("HH:mm:ss.zzz 'UTC'"):"UNKNOWN"));
        progress_->setToolTip(progress.run.isEmpty()?QString("Observed positions unavailable"):QString("Observed positions\n%1\nTotal positions\n%2\nExact counts: %3 / %4 · no averaging")
            .arg(positionFormats(progress.positions).stackedText(),positionFormats(pgn::booking::subtractUnsigned(progress.end,progress.start)).stackedText(),progress.positions,pgn::booking::subtractUnsigned(progress.end,progress.start)));
        if(!model_->error().isEmpty())progress_->setText(progress_->text()+"\n"+model_->error());
    }
    pgn::booking::BookingModel* model_;QLineEdit *search_{},*start_{},*end_{};QComboBox* state_{};
    QTableWidget* table_{};QLabel *pageLabel_{},*progress_{},*bookingDevice_{},*selectionSummary_{},*activeWork_{};QWidget* atlas_{};
    DeviceAccent accent_;
    QVector<int> filtered_;int page_{};QString sourceKey_;bool dirty_{};QPointer<QDialog> historyDialog_,ownerDirectory_;
};
}
QWidget* createCommunityView(pgn::booking::BookingModel* model,QWidget* parent,DeviceAccent accent){return new CommunityView(model,parent,std::move(accent));}
void selectCommunityBooking(QWidget* page,const QString& id){if(auto* view=dynamic_cast<CommunityView*>(page))view->select(id);}
}
