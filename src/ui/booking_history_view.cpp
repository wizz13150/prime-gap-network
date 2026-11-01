#include "ui/booking_history_view.h"
#include "ui/panel_style.h"
#include "ui/booking_labels.h"
#include "booking/booking_store.h"
#include <QtConcurrent/QtConcurrentRun>
#include <QFutureWatcher>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QTableWidget>
#include <QHeaderView>

namespace pgn::ui {
namespace {
class BookingHistory final:public QWidget {
public:
    BookingHistory(QString database,QWidget* parent):QWidget(parent),database_(std::move(database)){
        setObjectName("pgn-booking-history-view");setStyleSheet(dataPanelStyle());
        auto* layout=new QVBoxLayout(this);layout->setContentsMargins(18,16,18,16);
        auto* title=new QLabel("LOCAL BOOKING HISTORY · ATOMIC STATE + AUDIT");title->setStyleSheet("font:700 20px 'Segoe UI';color:#e9fffb");layout->addWidget(title);
        auto* warning=new QLabel("Read-only lifecycle observations, NOT a public lease or certified coverage. BASELINE_CURRENT_STATE marks the audit installation: earlier transitions are unknown, not reconstructed. UTC is the local observation time, not a mining measurement timestamp.");
        warning->setWordWrap(true);warning->setStyleSheet("color:#ffad4a");layout->addWidget(warning);
        auto* controls=new QHBoxLayout;previous_=new QPushButton("Previous");next_=new QPushButton("Older events");refresh_=new QPushButton("Refresh snapshot");
        previous_->setObjectName("pgn-booking-history-previous");next_->setObjectName("pgn-booking-history-next");refresh_->setObjectName("pgn-booking-history-refresh");
        search_=new QLineEdit;search_->setPlaceholderText("Exact run or booking ID · empty = all");search_->setMaxLength(36);search_->setObjectName("pgn-booking-history-search");
        auto* find=new QPushButton("Find exact ID");controls->addWidget(previous_);controls->addWidget(next_);controls->addWidget(refresh_);controls->addWidget(search_,1);controls->addWidget(find);layout->addLayout(controls);
        status_=new QLabel;layout->addWidget(status_);
        table_=new QTableWidget;table_->setObjectName("pgn-booking-history-records");table_->setColumnCount(9);
        table_->setHorizontalHeaderLabels({"Sequence","Observed UTC","Booking / run ID","From state","To state","Observation kind","Exact start","Exact end (exclusive)","Device / kind"});
        table_->verticalHeader()->hide();table_->setAlternatingRowColors(true);table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table_->setSelectionBehavior(QAbstractItemView::SelectRows);table_->setSelectionMode(QAbstractItemView::SingleSelection);
        table_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft|Qt::AlignVCenter);table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);table_->horizontalHeader()->setStretchLastSection(true);
        const int widths[]{80,205,290,155,155,205,230,230,250};for(int i=0;i<9;++i)table_->setColumnWidth(i,widths[i]);layout->addWidget(table_,1);
        connect(refresh_,&QPushButton::clicked,this,[this]{reset();});connect(find,&QPushButton::clicked,this,[this]{reset();});connect(search_,&QLineEdit::returnPressed,this,[this]{reset();});
        connect(previous_,&QPushButton::clicked,this,[this]{if(before_.size()>1){before_.removeLast();load();}});
        connect(next_,&QPushButton::clicked,this,[this]{if(current_.more&&!current_.rows.isEmpty()){before_.append(current_.rows.last().sequence);load();}});
        connect(&watcher_,&QFutureWatcher<pgn::booking::BookingEventPage>::finished,this,[this]{
            current_=watcher_.result();table_->setUpdatesEnabled(false);table_->setRowCount(current_.rows.size());
            for(int row=0;row<current_.rows.size();++row){const auto& event=current_.rows[row];
                const QStringList values{QString::number(event.sequence),event.observedUtc,event.bookingId,event.fromState.isEmpty()?"Not observed":localWorkLabel(event.fromState),localWorkLabel(event.toState),event.eventKind,event.start,event.end,event.device+" · "+event.kind};
                for(int column=0;column<values.size();++column){auto* item=new QTableWidgetItem(values[column]);item->setToolTip(values[column]);table_->setItem(row,column,item);}}
            table_->setUpdatesEnabled(true);
            if(current_.error.isEmpty()){through_=current_.through;if(before_.size()==1)total_=current_.total;
                status_->setText(QString("%1 conserved events · page %2 · %3 exact rows · sequence watermark %4 · later events appear on Refresh").arg(total_).arg(before_.size()).arg(current_.rows.size()).arg(through_));}
            else status_->setText(current_.error);
            setProperty("pgn_booking_history_rows",current_.rows.size());setProperty("pgn_booking_history_total",total_);controlsEnabled(true);
        });reset();
    }
private:
    void controlsEnabled(bool enabled){previous_->setEnabled(enabled&&before_.size()>1);next_->setEnabled(enabled&&current_.error.isEmpty()&&current_.more);refresh_->setEnabled(enabled);search_->setEnabled(enabled);}
    void reset(){if(watcher_.isRunning())return;before_={0};through_=0;total_=0;load();}
    void load(){if(watcher_.isRunning())return;controlsEnabled(false);status_->setText("READ-ONLY · bounded page loading off UI thread");
        const auto file=database_,id=search_->text().trimmed();const auto before=before_.last(),through=through_;
        watcher_.setFuture(QtConcurrent::run([file,id,before,through]{return pgn::booking::readBookingEvents(file,before,through,id);}));}
    QString database_;QVector<qint64> before_;qint64 through_{},total_{};pgn::booking::BookingEventPage current_;
    QFutureWatcher<pgn::booking::BookingEventPage> watcher_;QLabel* status_{};QPushButton *previous_{},*next_{},*refresh_{};QLineEdit* search_{};QTableWidget* table_{};
};
}
QWidget* createBookingHistoryView(const QString& file,QWidget* parent){return new BookingHistory(file,parent);}
}
