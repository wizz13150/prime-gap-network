#pragma once
#include "ui/booking_context_request.h"
#include "booking/booking_model.h"
#include "booking/decimal_range.h"
#include "booking/range_identity.h"
#include <QMenu>
#include <QAction>
#include <algorithm>

namespace pgn::ui {
// One action contract for both representations. Exact strings, not pixels,
// own admission; each action is revalidated by the transactional store.
inline QMenu* createBookingContextMenu(QWidget* parent,pgn::booking::BookingModel* model,
    const BookingContextRequest& request,std::function<void(const QString&)> report={}){
    using namespace pgn::booking;
    auto* menu=new QMenu(parent);menu->setObjectName("pgn-booking-context");
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->addSection(request.blocks.size()>1?QString("%1 selected blocks · exact selection").arg(request.blocks.size()):request.zones.size()>1?QString("%1 selected zones · exact selection").arg(request.zones.size()):identifyRange(model->source(),request.blockActions?request.start:request.zoneStart,request.blockActions?request.end:request.zoneEnd).title);
    if(!model->manualReservationsAllowed()){
        auto* scope=menu->addAction("PGN · work assigned automatically by consensus");scope->setEnabled(false);
        auto* note=menu->addAction("Manual reservations are not permitted");note->setEnabled(false);
        menu->setProperty("pgn_manual_reservations_allowed",false);
        return menu;
    }
    auto* scope=menu->addAction("Isolated engine validation · no network reservation");scope->setEnabled(false);
    const auto finish=[report](const QString& error,const QString& success){if(report)report(error.isEmpty()?success:error);};
    const auto releasable=[model](QVector<ExactBlockSelection> spans){
        std::sort(spans.begin(),spans.end(),[](const auto& a,const auto& b){return compareUnsigned(a.start,b.start)<0;});QVector<QPair<QString,QString>> merged;
        for(const auto& span:spans){if(!merged.isEmpty()&&compareUnsigned(span.start,merged.last().second)<=0){if(compareUnsigned(merged.last().second,span.end)<0)merged.last().second=span.end;}else merged.append({span.start,span.end});}
        bool any=false;
        for(const auto& job:model->jobs())if(job.kind=="LOCAL_INTENT"&&(job.state=="QUEUED"||job.state=="ARMED")){
            for(const auto& span:merged)any|=overlaps(span.first,span.second,job.start,job.end);
        }return any;
    };
    const auto actions=[&](const QString& first,const QString& last,bool whole){
        auto spans=whole?request.zones:request.blocks;if(spans.isEmpty())spans.append({first,last,request.zoneStart,request.zoneEnd});
        QString availabilityError;const int free=model->runtime().device.isEmpty()?0:model->availableSelection(spans,&availabilityError);const bool manyZones=whole&&request.zones.size()>1;
        if(!availabilityError.isEmpty())menu->addSection("Selection unavailable: "+availabilityError);
        auto* reserve=menu->addAction(whole?(manyZones?"Reserve selected zones":"Reserve whole zone"):"Reserve selected blocks");
        reserve->setObjectName(whole?"pgn-reserve-zone":"pgn-reserve-selected");reserve->setEnabled(free>0);
        reserve->setToolTip(!availabilityError.isEmpty()?availabilityError:free?QString("Reserve %1 free blocks in numeric order. Existing reservations, active and processed blocks stay unchanged.").arg(free):"No free blocks, or no observed GPU. Reserved, active, processed and unknown blocks cannot be overwritten.");
        QObject::connect(reserve,&QAction::triggered,menu,[=]{finish(model->reserveSelection(spans),"Free selected blocks reserved locally; queued automatically in numeric order. Active and processed blocks unchanged.");});
        auto* release=menu->addAction(whole?(manyZones?"Release reserved blocks in selected zones":"Release reserved blocks in this zone"):"Release selected reserved blocks");
        release->setObjectName(whole?"pgn-release-zone":"pgn-release-selected");release->setEnabled(releasable(spans));
        release->setToolTip("Releases only selected reserved blocks. Older grouped reservations keep their exact unselected remainder; active work and processed history are preserved.");
        QObject::connect(release,&QAction::triggered,menu,[=]{finish(model->releaseSelection(spans),"Selected queued reservations released. Active work and completed history unchanged.");});
    };
    if(request.blockActions){actions(request.start,request.end,false);menu->addSeparator();}
    actions(request.zoneStart,request.zoneEnd,true);
    return menu;
}
}
