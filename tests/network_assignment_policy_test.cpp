#include "booking/booking_model.h"
#include "ui/booking_context_menu.h"
#include "ui/community_view.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QCryptographicHash>
#include <QLabel>
#include <iostream>

int main(int argc,char** argv){
    QApplication app(argc,argv);if(argc!=2)return 1;
    QTemporaryDir temp;const auto directory=temp.path()+"/data/runtime";QDir().mkpath(directory);
    for(const auto* name:{"binding.json","community-source.csv"})
        if(!QFile::copy(QString::fromLocal8Bit(argv[1])+"/"+name,directory+"/"+name))return 2;
    const auto source=pgn::booking::loadSourceRegistry(directory);if(!source.valid()||source.localValidation)return 3;
    const pgn::booking::SourceRange* open=nullptr;for(const auto& row:source.ranges)if(row.status=="Open"){open=&row;break;}if(!open)return 4;
    // An old local plan remains available as history, but cannot be dispatched.
    {pgn::booking::BookingStore legacy(directory+"/local-bookings.sqlite");if(!legacy.queue(source,open->start,open->end,"GPU-test").isEmpty())return 5;}
    pgn::booking::BookingModel model(temp.path());if(model.manualReservationsAllowed()||model.jobs().size()!=1)return 6;
    const auto digest=[&]{QFile f(directory+"/local-bookings.sqlite");if(!f.open(QIODevice::ReadOnly))return QByteArray{};return QCryptographicHash::hash(f.readAll(),QCryptographicHash::Sha256);};
    const auto before=digest();bool dispatched=false;model.onDispatch=[&](const QString&){dispatched=true;};
    const QVector<pgn::booking::ExactBlockSelection> spans{{open->start,open->end,open->start,open->end}};
    if(model.queue(open->start,open->end,"GPU-test").isEmpty()||model.reserveBlocks(open->start,open->end,open->start,open->end).isEmpty()||model.reserveSelection(spans).isEmpty()||model.dispatch(model.jobs().first().id).isEmpty()||model.setAutomaticAfterManual("GPU-test",true).isEmpty())return 7;
    if(dispatched||digest()!=before||model.jobs().size()!=1)return 8;
    QWidget host;pgn::ui::BookingContextRequest request;request.start=open->start;request.end=open->end;request.zoneStart=open->start;request.zoneEnd=open->end;
    auto* menu=pgn::ui::createBookingContextMenu(&host,&model,request);
    for(auto* action:menu->actions())if(action->objectName().startsWith("pgn-reserve"))return 9;
    auto* view=pgn::ui::createCommunityView(&model,&host);
    if(!view->findChild<QLabel*>("pgn-ticket-allocation-policy")||view->findChild<QWidget*>("pgn-queue-view"))return 10;
    std::cout<<"PASS: manual create/reserve/dispatch blocked, legacy rows conserved, read-only coverage UI\n";
}
