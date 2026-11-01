#include "booking/booking_model.h"
#include "booking/queue_zone.h"
#include "ui/community_view.h"
#include "ui/range_calendar.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <iostream>
#include <memory>

int main(int argc,char** argv){
 QApplication app(argc,argv);if(argc<2)return 1;QTemporaryDir temp;QDir().mkpath(temp.path()+"/data/runtime");
 for(const auto name:{"binding.json","community-source.csv"})if(!QFile::copy(QString::fromLocal8Bit(argv[1])+"/"+name,temp.path()+"/data/runtime/"+name))return 2;
 pgn::booking::BookingModel model(temp.path());const QString run(32,'a');model.observeRuntime(run,"199998000000000000000","199999000000000000000","GPU-qa","QA","1",QDateTime::currentDateTimeUtc(),true);
 const auto zones=model.viewRanges();int first=-1;for(int i=0;i+2<zones.size();++i)if(model.source().declaredOpenContaining(zones[i].start,zones[i].end)&&model.source().declaredOpenContaining(zones[i+1].start,zones[i+1].end)&&model.source().declaredOpenContaining(zones[i+2].start,zones[i+2].end)){first=i;break;}if(first<0)return 3;
 QVector<pgn::booking::ExactBlockSelection> spans,order;for(int i=0;i<3;++i){const auto& z=zones[first+i];const auto cells=pgn::ui::rangeCalendarSlots(z.start,z.end);spans.append({cells[0].start,cells[1].end,z.start,z.end});order.append({z.start,z.end,z.start,z.end});}
 if(!model.reserveSelection(spans).isEmpty())return 4;
 std::unique_ptr<QWidget> view(pgn::ui::createCommunityView(&model));view->resize(1200,850);view->show();app.processEvents();
 auto* list=view->findChild<QListWidget*>("pgn-zone-queue");if(!list||list->count()!=3||list->dragDropMode()!=QAbstractItemView::InternalMove)return 5;
 auto check=[](bool ok,int code,const char* message){if(!ok)std::cerr<<"FAIL "<<code<<": "<<message<<'\n';return ok;};
 auto queued=[&]{QVector<pgn::booking::Booking> result;for(const auto& job:model.jobs())if(job.kind=="LOCAL_INTENT"&&(job.state=="QUEUED"||job.state=="ARMED"))result.append(job);return result;};
 auto pump=[&]{app.processEvents();app.processEvents();};
 if(!check(queued().size()==6&&queued().first().start==spans[0].start,6,"default order starts first block of first numeric zone"))return 6;
 if(!check(list->model()->moveRow({},2,{},0),7,"Qt internal-move model accepts zone drag"))return 7;pump();
 if(!check(list->item(0)->data(Qt::UserRole).toString()==order[2].start&&queued().first().start==spans[2].start,8,"UI move commits same durable dispatch order"))return 8;
 {pgn::booking::BookingStore reopened(model.historyDatabase());QVector<pgn::booking::Booking> persisted;for(const auto& job:reopened.active())if(job.kind=="LOCAL_INTENT")persisted.append(job);if(!check(persisted.size()==6&&persisted.first().start==spans[2].start,9,"order survives a fresh database connection"))return 9;}
 const auto cell=pgn::ui::rangeCalendarSlots(order[2].start,order[2].end).first();
 if(!model.releaseSelection({{cell.start,cell.end,order[2].start,order[2].end}}).isEmpty()||!model.reserveSelection({{cell.start,cell.end,order[2].start,order[2].end}}).isEmpty())return 10;pump();
 if(!check(queued().size()==6&&queued()[0].start==cell.start&&queued()[1].start==cell.end,11,"re-reservation keeps zone priority and ascending block order"))return 11;
 // Before deferred drop commit, another legal action removes a reservation.
 if(!list->model()->moveRow({},2,{},0))return 12;const auto remove=queued().last().id;if(!model.release(remove).isEmpty())return 13;pump();
 auto* message=view->findChild<QLabel*>("pgn-queue-order-message");if(!check(message&&message->text().contains("Queue changed")&&queued().first().start==cell.start&&model.runtime().run==run&&model.runtime().positions=="1",14,"stale drag rollback preserves prior priority and current raw work"))return 14;
 auto* numeric=view->findChild<QPushButton*>("pgn-queue-numeric-order");if(!numeric)return 15;numeric->click();pump();
 if(!check(queued().first().start==spans[0].start&&list->item(0)->data(Qt::UserRole).toString()==order[0].start,16,"numeric reset restores UI and durable order"))return 16;
 QStringList ids;for(const auto& job:queued())ids.append(job.id);if(!check(!model.reorderQueuedZones({order[0]},ids).isEmpty()&&queued().first().start==spans[0].start,17,"incomplete zone permutation rejected atomically"))return 17;
 for(const auto& job:queued())if(!model.release(job.id).isEmpty())return 18;pump();if(!check(list->count()==1&&list->item(0)->flags()==Qt::NoItemFlags&&model.runtime().run==run,19,"empty queue cannot reorder current runtime"))return 19;
 std::cout<<"PASS Qt internal zone move, durable priority/reopen, re-reservation/numeric blocks, stale-drag atomic refusal, numeric reset and active protection; no physical GPU/network mutation\n";
}
