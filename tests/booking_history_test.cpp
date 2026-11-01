#include "booking/booking_store.h"
#include "ui/booking_history_view.h"
#include "ui/booking_labels.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QThread>
#include <QPushButton>
#include <QTableWidget>
#include <QLineEdit>
#include <QSet>
#include <sqlite3.h>
#include <memory>
#include <iostream>

static bool execute(const QString& file,const char* sql){
    sqlite3* db{};if(sqlite3_open(file.toUtf8().constData(),&db)!=SQLITE_OK){if(db)sqlite3_close(db);return false;}
    const bool okay=sqlite3_exec(db,sql,nullptr,nullptr,nullptr)==SQLITE_OK;sqlite3_close(db);return okay;
}
static bool settled(QWidget* view,int expected){QElapsedTimer timer;timer.start();while(timer.elapsed()<5000){QApplication::processEvents();
    if(view->property("pgn_booking_history_rows").isValid()&&view->property("pgn_booking_history_rows").toInt()==expected)return true;QThread::msleep(1);}return false;}
int main(int argc,char** argv){
    QApplication app(argc,argv);using namespace pgn::booking;QTemporaryDir dir;
    const auto legacy=dir.path()+"/legacy.sqlite";
    if(!execute(legacy,"CREATE TABLE bookings(id TEXT PRIMARY KEY,start TEXT NOT NULL,end TEXT NOT NULL,start_key TEXT NOT NULL,end_key TEXT NOT NULL,state TEXT NOT NULL,kind TEXT NOT NULL,device TEXT NOT NULL,created_utc TEXT NOT NULL);"
        "INSERT INTO bookings VALUES('cccccccccccccccccccccccccccccccc','100','200','003100','003200','PAUSED','RUNTIME','GPU-qa','2020-01-01T00:00:00.000Z');"))return 1;
    {BookingStore migrated(legacy);if(!migrated.valid())return 2;const auto page=readBookingEvents(legacy);
        if(!page.error.isEmpty()||page.total!=1||page.rows[0].eventKind!="BASELINE_CURRENT_STATE"||page.rows[0].toState!="PAUSED"||!page.rows[0].fromState.isEmpty()||page.rows[0].observedUtc.startsWith("2020"))return 3;}
    {BookingStore reopened(legacy);if(!reopened.valid()||readBookingEvents(legacy).total!=1)return 4;}
    const auto file=dir.path()+"/bookings.sqlite";BookingStore store(file);if(!store.valid())return 5;
    SourceRegistry source;source.start="100";source.end="2000";source.ranges.append({1,source.start,source.end,"Open",{}});
    if(!store.queue(source,"100","200","GPU-qa").isEmpty())return 6;
    const auto queued=store.active()[0].id;if(!store.release(queued).isEmpty())return 7;
    const QString run(32,'a');if(!store.observeRuntime(run,"200","300","GPU-qa",true).isEmpty())return 8;
    const auto prior=readBookingEvents(file);if(prior.total!=3)return 9;
    for(int i=0;i<10;++i)if(!store.observeRuntime(run,"200","300","GPU-qa",true).isEmpty())return 10;
    if(readBookingEvents(file).total!=prior.total)return 11; // no identical-state audit spam
    if(!execute(file,"CREATE TRIGGER qa_reject_pause AFTER UPDATE OF state ON bookings WHEN NEW.state='PAUSED' BEGIN SELECT RAISE(ABORT,'QA injected failure'); END;"))return 12;
    if(store.observeRuntime(run,"200","300","GPU-qa",false).isEmpty()||readBookingEvents(file).total!=prior.total||store.active()[0].state!="ACTIVE")return 13;
    if(!execute(file,"DROP TRIGGER qa_reject_pause;"))return 14;
    // A historical receipt cannot pause an unrelated currently active run.
    if(!store.recordSourceCompletion(QString(32,'d'),"500","600","GPU-qa").isEmpty()||store.active()[0].state!="ACTIVE")return 15;
    for(int i=0;i<1007;++i)if(!store.observeRuntime(run,"200","300","GPU-qa",i%2!=0).isEmpty())return 16;
    const auto initial=readBookingEvents(file);if(!initial.error.isEmpty()||initial.rows.size()!=100||!initial.more||initial.total!=1011)return 17;
    if(!store.recordSourceCompletion(run,"200","300","GPU-qa").isEmpty())return 18;
    const auto completion=readBookingEvents(file,0,0,run);if(completion.rows[0].fromState!="PAUSED"||completion.rows[0].toState!="SOURCE_FINISHED")return 19;
    const auto finalCount=readBookingEvents(file).total;if(!store.recordSourceCompletion(run,"200","300","GPU-qa").isEmpty()||readBookingEvents(file).total!=finalCount)return 20;
    qint64 before=0;QSet<qint64> sequences;
    do{const auto page=readBookingEvents(file,before,initial.through);if(!page.error.isEmpty())return 21;
        for(const auto& row:page.rows){if(row.sequence>initial.through||sequences.contains(row.sequence))return 22;sequences.insert(row.sequence);}
        if(!page.more)break;if(page.rows.isEmpty())return 23;before=page.rows.last().sequence;
    }while(true);
    if(sequences.size()!=initial.total)return 24;
    if(readBookingEvents(file,0,0,"' OR 1=1 --").error.isEmpty())return 25;
    const auto missing=dir.path()+"/missing.sqlite";if(readBookingEvents(missing).error.isEmpty()||QFileInfo::exists(missing))return 26;
    if(execute(file,"DELETE FROM booking_events;")||execute(file,"UPDATE booking_events SET to_state='FORGED';"))return 27;
    std::unique_ptr<QWidget> view(pgn::ui::createBookingHistoryView(file));view->resize(1380,750);view->show();if(!settled(view.get(),100))return 28;
    auto* table=view->findChild<QTableWidget*>("pgn-booking-history-records");if(!table||table->item(0,4)->text()!=pgn::ui::localWorkLabel("SOURCE_FINISHED"))return 29;
    auto* next=view->findChild<QPushButton*>("pgn-booking-history-next");next->click();
    QElapsedTimer changed;changed.start();while(changed.elapsed()<5000&&table->item(0,0)->text()==QString::number(finalCount)){app.processEvents();QThread::msleep(1);}
    if(table->item(0,0)->text()!=QString::number(finalCount-100))return 30;
    auto* search=view->findChild<QLineEdit*>("pgn-booking-history-search");search->setText(queued);view->findChild<QPushButton*>("pgn-booking-history-refresh")->click();if(!settled(view.get(),2))return 31;
    if(table->item(0,4)->text()!=pgn::ui::localWorkLabel("RELEASED")||table->item(1,4)->text()!=pgn::ui::localWorkLabel("QUEUED"))return 32;
    if(argc>1){view->repaint();if(!view->grab().save(QString::fromLocal8Bit(argv[1])))return 33;}
    std::cout<<"PASS atomic state/audit rollback, baseline honest migration, immutable events, complete1011 snapshot keyset traversal with later exclusion, exact ID filter, real history buttons\n";
}
