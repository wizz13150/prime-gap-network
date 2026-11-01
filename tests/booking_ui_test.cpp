#include "booking/booking_model.h"
#include "ui/mining_canvas.h"
#include "ui/community_view.h"
#include "telemetry.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QPushButton>
#include "ui/range_atlas.h"
#include "ui/range_calendar.h"
#include "ui/prototype_community_atlas.h"
#include "ui/booking_labels.h"
#include "ui/booking_context_menu.h"
#include <QScrollArea>
#include <QScrollBar>
#include "ui/range_visuals.h"
#include "booking/decimal_range.h"
#include <QElapsedTimer>
#include <QThread>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QTabWidget>
#include <QSpinBox>
#include "ui/booking_block_info.h"
#include "booking/range_identity.h"
#include "booking/catalog_coordinates.h"
#include <QCheckBox>
#include <QTableWidget>
#include <QDialog>
#include <QClipboard>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>
#include <memory>

int main(int argc,char** argv){
    QApplication app(argc,argv);if(argc<2)return 1;
    QTemporaryDir dir;const auto runtime=dir.path()+"/data/runtime";QDir().mkpath(runtime);
    for(const auto& file:{"binding.json","community-source.csv"})if(!QFile::copy(QString::fromLocal8Bit(argv[1])+"/"+file,runtime+"/"+file))return 2;
    pgn::booking::BookingModel model(dir.path());if(!model.source().valid())return 3;
    const auto exactSlots=pgn::ui::rangeCalendarSlots("18446744073709551616","18446744073709551723");
    if(exactSlots.size()!=100||exactSlots.first().start!="18446744073709551616"||exactSlots.last().end!="18446744073709551723")return 22;
    for(int i=0;i<exactSlots.size();++i)if(pgn::booking::compareUnsigned(exactSlots[i].start,exactSlots[i].end)>=0||(i&&exactSlots[i-1].end!=exactSlots[i].start))return 23;
    if(pgn::ui::rangeCalendarSlots("5","8").size()!=3||!pgn::ui::rangeCalendarSlots("8","5").isEmpty())return 24;
    const pgn::ui::RangeCalendarSlot partialCell{"100000000000000000000","100000000000000000100"};
    const auto partial=pgn::ui::rangeCalendarInterval(partialCell,"100000000000000000025","100000000000000000075","100000000000000000050");
    if(!partial||partial->begin!=.25||partial->end!=.75||partial->progressEnd!=.5)return 40;
    const auto rawZero=pgn::ui::rangeCalendarInterval(partialCell,"100000000000000000025","100000000000000000075","100000000000000000025");
    if(!rawZero||rawZero->progressEnd!=rawZero->begin)return 41;
    const auto beyond=pgn::ui::rangeCalendarInterval(partialCell,"99999999999999999999","100000000000000000101","100000000000000000101");
    if(!beyond||beyond->begin!=0||beyond->end!=1||beyond->progressEnd!=1)return 42;
    if(pgn::ui::rangeCalendarInterval(partialCell,partialCell.end,"100000000000000000101"))return 43;
    QImage spanImage(80,24,QImage::Format_ARGB32_Premultiplied);spanImage.fill(Qt::black);
    {QPainter painter(&spanImage);pgn::ui::drawRangeSpan(painter,QRectF(0,0,80,24),Qt::green,.25,.75,.5);}
    if(spanImage.pixelColor(6,20)!=QColor(Qt::black)||spanImage.pixelColor(25,20)!=QColor(Qt::green)||spanImage.pixelColor(50,20)==QColor(Qt::green))return 44;
    if(spanImage.pixelColor(25,10)==QColor(Qt::black)||spanImage.pixelColor(6,10)!=QColor(Qt::black))return 82;
    {std::unique_ptr<QWidget> unknownDevice(pgn::ui::createCommunityView(&model));
     unknownDevice->findChild<QLineEdit*>("pgn_booking_start")->setText("199997000000000000000");
     unknownDevice->findChild<QLineEdit*>("pgn_booking_end")->setText("199998000000000000000");
     std::unique_ptr<QMenu> menu(pgn::ui::createBookingContextMenu(unknownDevice.get(),&model,{"199997000000000000000","199998000000000000000","199900000000000000000","200000000000000000000",{}}));
     if(!model.jobs().isEmpty()||menu->findChild<QAction*>("pgn-reserve-selected")->isEnabled()||unknownDevice->findChild<QPushButton*>("pgn_booking_queue"))return 56;
    }
    const auto now=QDateTime::currentDateTimeUtc();const QString run(32,'d'),device="GPU-qa";
    model.observeRuntime(run,"199998000000000000000","199999000000000000000",device,"RTX3090 QA","0",now,true);
    std::unique_ptr<QWidget> canvas(pgn::ui::createMiningCanvas());canvas->resize(1380,900);pgn::ui::bindBookingModel(canvas.get(),&model);
    canvas->show();app.processEvents();canvas->repaint();
    if(canvas->property("pgn_reservation_segments").toInt()!=1||pgn::ui::reservationSegmentRect(canvas.get(),run).isEmpty())return 4;
    QString selected;model.onSelect=[&](const QString& id){selected=id;};const auto hit=pgn::ui::reservationSegmentRect(canvas.get(),run);
    QMouseEvent event(QEvent::MouseButtonPress,hit.center(),canvas->mapToGlobal(hit.center().toPoint()),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(canvas.get(),&event);if(selected!=run)return 5;
    std::unique_ptr<QWidget> community(pgn::ui::createCommunityView(&model,nullptr,[](const QString&){return QColor("#4facfe");}));community->resize(1380,900);community->show();app.processEvents();
    // Catalogue coordinates retain exact half-open boundary ownership. A page
    // or zone boundary belongs to the preceding row only through end-1.
    const auto wholeZone=pgn::booking::catalogCoordinates("199900000000000000000","200000000000000000000");
    if(!wholeZone.valid||wholeZone.numberRange!="1.00 – 2.00 ×10²⁰"||wholeZone.cells!="Zone 1000 · Blocks 1–100")return 101;
    const auto single=pgn::booking::catalogCoordinates("199946000000000000000","199947000000000000000");
    if(single.cells!="Zone 1000 · Block 47")return 102;
    const auto tiny=pgn::booking::catalogCoordinates("199946000000000000001","199946000000000000002");
    if(tiny.cells!="Zone 1000 · Block 47 · partial blocks"||pgn::booking::catalogCoordinates("2","2").valid||pgn::booking::catalogCoordinates("-1","2").valid)return 103;
    const auto cross=pgn::booking::catalogCoordinates("199999999999999999999","200000000000000000001");
    if(cross.numberRange!="1.00 – 3.00 ×10²⁰"||!cross.cells.contains("Range 1 / Zone 1000 / Block 100 → Range 2 / Zone 1 / Block 1"))return 104;
    const auto huge=pgn::booking::catalogCoordinates("79000000000000100000000000000","79000000000002100000000000000");
    if(!huge.valid||!huge.numberRange.startsWith("790000000.00"))return 105;
    auto* registerOpen=community->findChild<QPushButton*>("pgn-public-register-open");
    auto* publicRows=community->findChild<QTableWidget*>("pgn-public-register-results");
    auto* publicSearch=community->findChild<QLineEdit*>("pgn-public-register-search");
    auto* publicExact=community->findChild<QCheckBox*>("pgn-public-register-exact");
    auto* publicDialog=community->findChild<QDialog*>("pgn-public-register-dialog");
    if(!registerOpen||!publicRows||!publicSearch||!publicExact||!publicDialog)return 106;
    const auto originalJobs=model.jobs().size(),originalSourceRows=model.source().ranges.size();const auto originalPage=model.viewPage();
    registerOpen->click();app.processEvents();
    if(!publicDialog->isVisible()||!publicRows->isColumnHidden(4)||!publicRows->isColumnHidden(5)||publicRows->columnCount()!=6)return 107;
    publicSearch->setText("Zone 1000");app.processEvents();
    if(publicRows->rowCount()!=1||publicRows->item(0,1)->text()!="Zone 1000 · Blocks 1–100"||publicRows->item(0,4)->text()!="199900000000000000000"||publicRows->item(0,5)->text()!="200000000000000000000")return 108;
    publicExact->click();app.processEvents();if(publicRows->isColumnHidden(4)||publicRows->isColumnHidden(5))return 109;
    publicExact->click();publicDialog->resize(760,500);app.processEvents();
    if(publicRows->width()>publicDialog->width()||model.jobs().size()!=originalJobs||model.source().ranges.size()!=originalSourceRows||model.viewPage()!=originalPage)return 110;
    if(argc>=3&&!publicDialog->grab().save(QString::fromLocal8Bit(argv[2])+"/public-register-760.png"))return 111;
    auto* copyPublic=community->findChild<QPushButton*>("pgn-public-register-copy");if(!copyPublic)return 115;
    publicRows->selectRow(0);app.processEvents();if(!copyPublic->isEnabled())return 116;copyPublic->click();
    if(QApplication::clipboard()->text()!="[199900000000000000000, 200000000000000000000)")return 117;
    model.setRoundRange(2);app.processEvents();
    QMetaObject::invokeMethod(publicRows,"cellDoubleClicked",Qt::DirectConnection,Q_ARG(int,0),Q_ARG(int,0));app.processEvents();
    if(publicDialog->isVisible()||model.viewPage()!=1||model.jobs().size()!=originalJobs||community->findChild<QLineEdit*>("pgn_booking_start")->text()!="199900000000000000000")return 118;
    publicSearch->clear();publicDialog->hide();
    const auto* observedDevice=community->findChild<QLabel*>("pgn-booking-device");
    if(!observedDevice->text().contains("RTX3090 QA")||observedDevice->toolTip()!=device)return 57;
    auto* localAtlas=community->findChild<QWidget*>("pgn-source-atlas");
    if(!localAtlas||!localAtlas->property("pgn_atlas_combined_context").toBool()||localAtlas->property("pgn_atlas_local_segments").toInt()!=1)return 60;
    const auto* liveWork=community->findChild<QLabel*>("pgn-booking-active-work");
    if(!liveWork||!liveWork->text().contains("MINING NOW")||!liveWork->text().contains("68 bits · Zone 1000 · Block 99"))return 61;
    if(!localAtlas||pgn::ui::rangeAtlasJobRect(localAtlas,run).isEmpty())return 19;
    if(!canvas->property("pgn_total_card_first").toBool()||canvas->property("pgn_central_page").toString()!="TOTAL")return 20;
    auto* calendar=community->findChild<QWidget*>("pgn-range-calendar-grid");
    if(!calendar||calendar->property("pgn_calendar_slots").toInt()!=100)return 25;
    auto* jump=community->findChild<QSpinBox*>("pgn-calendar-jump");if(!jump)return 88;
    const int savedZone=jump->value();jump->setValue(100);app.processEvents();
    if(calendar->property("pgn_calendar_parent").toInt()!=99||model.jobs().size()!=1)return 89;
    localAtlas->repaint();
    if(localAtlas->property("pgn_atlas_inspected_start").toString()!=calendar->property("pgn_calendar_first").toString()||
       localAtlas->property("pgn_atlas_zoom_blocks").toInt()<100){std::cerr<<"FAIL direct zone browsing did not synchronize right100-block view\n";return 93;}
    jump->setValue(savedZone);app.processEvents();
    // Actual dialog buttons: a failed apply cannot close with no selection.
    auto* exactOpen=community->findChild<QPushButton*>("pgn-booking-exact-toggle");
    auto* exactDialog=community->findChild<QWidget*>("pgn-booking-exact-dialog");
    auto* exactApply=community->findChild<QPushButton*>("pgn-booking-exact-apply");
    auto* exactStart=community->findChild<QLineEdit*>("pgn_booking_start");
    auto* exactEnd=community->findChild<QLineEdit*>("pgn_booking_end");
    if(!exactOpen||!exactDialog||!exactApply||!exactStart||!exactEnd)return 104;
    exactOpen->click();exactStart->setText("not-a-number");exactEnd->setText("2");exactApply->click();app.processEvents();
    if(!exactDialog->isVisible()||model.jobs().size()!=1||exactStart->height()<32||exactEnd->height()<32||exactApply->height()<32)return 105;
    exactStart->setText("1");exactEnd->setText("2");exactApply->click();app.processEvents();
    if(!exactDialog->isVisible()||model.jobs().size()!=1)return 106;
    exactStart->setText("199997000000000000000");exactEnd->setText("199998000000000000000");exactApply->click();app.processEvents();
    const auto applied=pgn::ui::rangeCalendarSelection(calendar);
    if(exactDialog->isVisible()||!applied||applied->start!="199997000000000000000"||applied->end!="199998000000000000000"||
       localAtlas->property("pgn_atlas_inspected_start").toString()!=applied->start||model.jobs().size()!=1)return 107;
    if(pgn::ui::selectRangeCalendar(calendar,"1","2"))return 108;
    const auto unchanged=pgn::ui::rangeCalendarSelection(calendar);
    if(!unchanged||unchanged->start!=applied->start||unchanged->end!=applied->end)return 109;
    const auto info=pgn::ui::bookingBlockInfo(model,model.runtime().start,model.runtime().end);
    auto* infoAtlas=dynamic_cast<PGNCommunityAtlas*>(localAtlas);
    if(!infoAtlas||!infoAtlas->blockTooltip||info!=infoAtlas->blockTooltip(model.runtime().start,model.runtime().end)||!info.contains("Processing:")||!info.contains("This block:")||!info.contains(model.researcherName()))return 90;
    auto* queueButton=community->findChild<QPushButton*>("pgn-calendar-first-open");
    for(const auto size:{QSize(1380,900),QSize(1200,600),QSize(1200,540)}){community->resize(size);app.processEvents();
        const QRect queueRect(queueButton->mapTo(community.get(),QPoint()),queueButton->size());
        if(community->size()!=size||!queueButton->isVisible()||!community->rect().contains(queueRect)||queueButton->height()<20){std::cerr<<"FAIL requested viewport "<<size.height()<<" actual "<<community->height()<<" / minhint "<<community->minimumSizeHint().height()<<" / choice "<<community->findChild<QWidget*>("pgn-booking-choice-panel")->minimumSizeHint().height()<<" / atlas "<<localAtlas->minimumHeight()<<"\n";
            std::cerr<<"FirstOpen visible="<<queueButton->isVisible()<<" height="<<queueButton->height()<<" minimum="<<queueButton->minimumHeight()<<" rect="<<queueRect.x()<<","<<queueRect.y()<<","<<queueRect.width()<<","<<queueRect.height()<<" inside="<<community->rect().contains(queueRect)<<"\n";
            for(auto* label:community->findChildren<QLabel*>())if(label->isVisible())std::cerr<<label->text().left(45).toStdString()<<" : height "<<label->height()<<" minhint "<<label->minimumSizeHint().height()<<"\n";
            if(argc>2)community->grab().save(QString::fromLocal8Bit(argv[2])+"/booking-layout-fit-failed.png");return 62;}
        // The left choice rail now scrolls at short human window heights;
        // rejecting every scroll area encoded the superseded fixed layout.
        const auto* choiceScroll=community->findChild<QScrollArea*>("pgn-booking-choice-scroll");
        if(!choiceScroll||!choiceScroll->widgetResizable()||choiceScroll->horizontalScrollBarPolicy()!=Qt::ScrollBarAlwaysOff)return 65;
        if(!community->rect().contains(QRect(calendar->mapTo(community.get(),QPoint()),calendar->size())))return 66;
    }
    community->resize(1380,900);app.processEvents();
    const auto identity=pgn::booking::identifyRange(model.source(),"199997000000000000000","199999000000000000000");
    if(!identity.valid||identity.title!="68 bits · Zone 1000 · Blocks 98–99"||identity.declaration!="Declared open · no listed owner")return 63;
    if(pgn::booking::identifyRange(model.source(),{},{}).valid||pgn::booking::identifyRange(model.source(),"not-a-number","2").valid)return 64;
    const auto calendarHit=pgn::ui::rangeCalendarCell(calendar,97);
    QMouseEvent calendarPress(QEvent::MouseButtonPress,calendarHit.center(),calendar->mapToGlobal(calendarHit.center().toPoint()),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QMouseEvent calendarRelease(QEvent::MouseButtonRelease,calendarHit.center(),calendar->mapToGlobal(calendarHit.center().toPoint()),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
    QApplication::sendEvent(calendar,&calendarPress);QApplication::sendEvent(calendar,&calendarRelease);app.processEvents();
    if(community->findChild<QLineEdit*>("pgn_booking_start")->text()!="199997000000000000000"||community->findChild<QLineEdit*>("pgn_booking_end")->text()!="199998000000000000000")return 26;
    {const auto selected=pgn::ui::rangeCalendarSelection(calendar);if(!selected)return 96;
     std::unique_ptr<QMenu> menu(pgn::ui::createBookingContextMenu(community.get(),&model,{selected->start,selected->end,"199900000000000000000","200000000000000000000",{}}));
     auto* action=menu->findChild<QAction*>("pgn-reserve-selected");if(!action||!action->isEnabled())return 97;action->trigger();}
    app.processEvents();canvas->repaint();
    if(model.jobs().size()!=2||canvas->property("pgn_reservation_segments").toInt()!=2)return 6;
    for(const auto& job:model.jobs())if(job.kind=="LOCAL_INTENT"&&job.device!=device){
        std::cerr<<"FAIL booking card queued another machine's hardcoded GPU instead of observed device\n";return 55;
    }
    // The operator removed these duplicate cards: the header and queue own
    // current/future work. Their absence must not be tested as a regression.
    for(const auto& job:model.jobs())if(community->findChild<QWidget*>("pgn_booking_"+job.id))return 58;
    if(community->property("pgn_reservation_future_card_count").toInt()!=0)return 112;
    for(const auto size:{QSize(1200,600),QSize(1200,540)}){
        community->resize(size);app.processEvents();
        if(community->size()!=size){std::cerr<<"FAIL populated viewport grew to "<<community->height()<<"\n";return 72;}
        auto* choiceScroll=community->findChild<QScrollArea*>("pgn-booking-choice-scroll");
        for(auto* button:community->findChildren<QPushButton*>())if(button->isVisible()){
            if(choiceScroll->widget()->isAncestorOf(button)){
                // Human scrolling must reach each actual control, including
                // those clipped at the initial short-window scroll position.
                choiceScroll->ensureWidgetVisible(button,0,0);app.processEvents();
                if(button->visibleRegion().isEmpty()||!choiceScroll->viewport()->rect().contains(button->mapTo(choiceScroll->viewport(),button->rect().center())))return 113;
            }else if(!community->rect().contains(QRect(button->mapTo(community.get(),QPoint()),button->size()))){const auto point=button->mapTo(community.get(),QPoint());std::cerr<<"FAIL visible control outside: "<<button->text().toStdString()<<" xy="<<point.x()<<","<<point.y()<<" size="<<button->width()<<","<<button->height()<<"\n";return 73;}
        }
        choiceScroll->verticalScrollBar()->setValue(0);app.processEvents();
        if(argc>2&&size.height()==540)community->grab().save(QString::fromLocal8Bit(argv[2])+"/booking-layout-populated-540-qa.png");
    }
    community->resize(1380,900);app.processEvents();
    localAtlas->repaint();
    if(localAtlas->property("pgn_atlas_local_segments").toInt()!=2)return 67;
    auto* combined=dynamic_cast<PGNCommunityAtlas*>(localAtlas);if(!combined)return 68;
    int projected=0;for(const auto& value:combined->observations()){
        const auto row=value.toObject();if(row["status"].toString().startsWith("LOCAL_")){
            if(row["owner"].toString()!=model.researcherName()||row["_device_color"].toString()!="#ffad4a")return 74;
            if(row["owner"].toString().startsWith("OUR ·"))return 75;++projected;
        }else if(row["status"].toString()=="DECLARED_OPEN")for(const auto& job:model.jobs())
            if(pgn::booking::overlaps(row["start"].toString(),row["end"].toString(),job.start,job.end))return 76;
    }if(projected!=2)return 77;
    combined->focusRange(199900,200000);combined->repaint();
    if(combined->property("pgn_atlas_zoom_blocks").toInt()!=200||combined->property("pgn_atlas_zoom_zones").toInt()!=2||combined->property("pgn_atlas_zoom_zone").toInt()!=999)return 78;
    // General semantic zoom on a different public pair, not current1000 only.
    combined->focusRange(100100,100200);combined->repaint();
    if(combined->property("pgn_atlas_zoom_blocks").toInt()!=300||combined->property("pgn_atlas_zoom_zones").toInt()!=3)return 83;
    combined->focusRange(199900,200000);combined->repaint();
    // Right-grid selection uses precisely the same block endpoints as the
    // left calendar; no queue or GPU control occurs just by selecting.
    combined->inspect({{"start","199997000000000000000"},{"end","199998000000000000000"},{"_block",true}});
    const auto rightSelection=pgn::ui::rangeCalendarSelection(calendar);
    if(!rightSelection||rightSelection->start!="199997000000000000000"||rightSelection->end!="199998000000000000000"||model.jobs().size()!=2)return 79;
    if(argc>2)community->grab().save(QString::fromLocal8Bit(argv[2])+"/booking-owner-100-blocks-qa.png");
    combined->clickReset();app.processEvents();combined->repaint();
    const auto firstAxis=combined->axisStart(),lastAxis=combined->axisEnd();
    // Source inspection changes only the named zone on the left. A click
    // must not turn the whole public row into a hundred-block selection.
    const auto jobsBeforeInspection=model.jobs().size();
    const auto& sourceRow=model.source().ranges.last();
    combined->inspect({{"start",sourceRow.start},{"end",sourceRow.end},{"status","DECLARED_OPEN"}});
    if(combined->axisStart()!=firstAxis||combined->axisEnd()!=lastAxis||model.jobs().size()!=jobsBeforeInspection||pgn::ui::rangeCalendarSelection(calendar))return 69;
    // Re-select the same explicit one-block choice for the lifecycle test.
    pgn::ui::selectRangeCalendar(calendar,"199997000000000000000","199998000000000000000");
    community->findChild<QLineEdit*>("pgn_booking_start")->setText("199997000000000000000");community->findChild<QLineEdit*>("pgn_booking_end")->setText("199998000000000000000");
    if(pgn::ui::localWorkLabel("ARMED")!="STARTS AFTER CURRENT RANGE"||pgn::ui::localWorkLabel("QUEUED")!="SAVED · NOT SCHEDULED")return 70;
    canvas->repaint();const auto railHeading=canvas->property("pgn_booking_rail_heading").toRectF(),railTrack=canvas->property("pgn_booking_rail_track").toRectF();
    const auto producerBand=canvas->property("pgn_booking_producer_annotation").toRectF();
    if(railHeading.isEmpty()||railTrack.isEmpty()||producerBand.isEmpty()||railHeading.intersects(railTrack)||railTrack.height()<34||railTrack.bottom()+8>=producerBand.top())return 71;
    // A successful queue write broadcasts lifecycle changes. It must not
    // erase the user's exact calendar selection or move its camera.
    const auto retainedSelection=pgn::ui::rangeCalendarSelection(calendar);
    if(!retainedSelection||retainedSelection->start!="199997000000000000000"||retainedSelection->end!="199998000000000000000")return 45;
    const auto retainedParent=calendar->property("pgn_calendar_parent");
    model.reloadDispatchState();app.processEvents();
    const auto afterReload=pgn::ui::rangeCalendarSelection(calendar);
    if(!afterReload||afterReload->start!=retainedSelection->start||afterReload->end!=retainedSelection->end||calendar->property("pgn_calendar_parent")!=retainedParent)return 46;
    auto* selectionLabel=community->findChild<QLabel*>("pgn-calendar-selection");
    if(!selectionLabel||!selectionLabel->text().contains("68 bits")||!selectionLabel->text().contains("1000000000000000 positions"))return 47;
    // A lifecycle update in the middle of a reversed drag must preserve its
    // anchor, not silently select another range or schedule work.
    QApplication::sendEvent(calendar,&calendarPress);model.reloadDispatchState();
    const auto earlierCell=pgn::ui::rangeCalendarCell(calendar,96);
    QMouseEvent calendarMove(QEvent::MouseMove,earlierCell.center(),calendar->mapToGlobal(earlierCell.center().toPoint()),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(calendar,&calendarMove);model.reloadDispatchState();
    QMouseEvent earlierRelease(QEvent::MouseButtonRelease,earlierCell.center(),calendar->mapToGlobal(earlierCell.center().toPoint()),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
    QApplication::sendEvent(calendar,&earlierRelease);app.processEvents();
    const auto dragged=pgn::ui::rangeCalendarSelection(calendar);
    if(!dragged||dragged->start!="199996000000000000000"||dragged->end!="199998000000000000000"||model.jobs().size()!=2)return 48;
    if(community->findChild<QLineEdit*>("pgn_booking_start")->text()!=dragged->start||community->findChild<QLineEdit*>("pgn_booking_end")->text()!=dragged->end)return 49;
    // Explicit navigation may clear a selection; an unrelated update may not.
    auto* previousRange=community->findChild<QPushButton*>("pgn-calendar-previous");
    auto* activeRange=community->findChild<QPushButton*>("pgn-calendar-active");
    if(!previousRange||!activeRange)return 50;previousRange->click();
    if(pgn::ui::rangeCalendarSelection(calendar))return 51;activeRange->click();
    QKeyEvent selectFirst(QEvent::KeyPress,Qt::Key_Right,Qt::NoModifier);
    QKeyEvent extendSelection(QEvent::KeyPress,Qt::Key_Right,Qt::ShiftModifier);
    QApplication::sendEvent(calendar,&selectFirst);QApplication::sendEvent(calendar,&extendSelection);model.reloadDispatchState();
    const auto keyboardSelection=pgn::ui::rangeCalendarSelection(calendar);
    if(!keyboardSelection||keyboardSelection->start!="199900000000000000000"||keyboardSelection->end!="199902000000000000000"||model.jobs().size()!=2)return 52;
    QString dispatchId;int dispatchRequests=0;
    model.onDispatch=[&](const QString& id){dispatchId=id;++dispatchRequests;};
    for(const auto& job:model.jobs())if(job.kind=="LOCAL_INTENT"){
        std::unique_ptr<QMenu> menu(pgn::ui::createBookingContextMenu(community.get(),&model,{job.start,job.end,"199900000000000000000","200000000000000000000",{}}));
        auto* arm=menu->findChild<QAction*>("pgn-run-next-"+job.id);if(arm||community->findChild<QPushButton*>("pgn-dispatch-"+job.id))return 28;
        if(dispatchRequests!=0||!dispatchId.isEmpty()||model.runtime().run!=run)return 29;
        community->resize(1200,540);app.processEvents();if(community->size()!=QSize(1200,540))return 74;
        community->resize(1380,900);app.processEvents();
        auto* focus=community->findChild<QPushButton*>("pgn-source-atlas-focus");if(!focus)return 30;
        auto* reset=community->findChild<QPushButton*>("pgn-source-atlas-reset");
        if(!reset||focus->size()!=reset->size()||focus->size()!=QSize(114,21)){std::cerr<<"FAIL community zoom controls have different dimensions\n";return 95;}
        jump->setValue(100);app.processEvents();
        focus->click();app.processEvents();localAtlas->repaint();
        if(jump->value()!=1000||calendar->property("pgn_calendar_parent").toInt()!=999){std::cerr<<"FAIL our-work zoom left calendar on an unrelated zone\n";return 94;}
        // Restore the existing selection-preservation fixture after this
        // explicit navigation regression changed its displayed zone.
        pgn::ui::selectRangeCalendar(calendar,keyboardSelection->start,keyboardSelection->end);
        if(pgn::ui::rangeAtlasJobRect(localAtlas,run).isEmpty()||pgn::ui::rangeAtlasJobRect(localAtlas,job.id).isEmpty())return 31;
    }
    if(model.runtime().positions!="0"||model.runtime().fraction!=0.0)return 7;
    model.observeRuntime(run,"199998000000000000000","199999000000000000000",device,"RTX3090 QA","250000000000000",now.addMSecs(1),true);
    const auto retainedTime=model.runtime().observedUtc;
    model.observeRuntime(run,"199998000000000000000","199999000000000000000",device,"RTX3090 QA",{},QDateTime{},true);
    if(model.runtime().positions!="250000000000000"||model.runtime().observedUtc!=retainedTime)return 8;
    model.observeRuntime(run,"199998000000000000000","199999000000000000000",device,"RTX3090 QA","0",now.addMSecs(2),true);
    if(model.runtime().positions!="250000000000000"||model.error().isEmpty())return 9;
    pgn::ui::selectCommunityBooking(community.get(),run);app.processEvents();
    const auto beforeSourceRefresh=pgn::ui::rangeCalendarSelection(calendar);if(!beforeSourceRefresh)return 114;
    const auto initialSourceRows=model.source().ranges.size();
    QFile csv(runtime+"/community-source.csv");if(!csv.open(QIODevice::ReadOnly))return 10;
    auto changedCsv=csv.readAll();csv.close();if(!changedCsv.endsWith('\n'))changedCsv+='\n';changedCsv+="200000,200100,Open,\n";
    if(!csv.open(QIODevice::WriteOnly)||csv.write(changedCsv)!=changedCsv.size())return 11;csv.close();
    QFile binding(runtime+"/binding.json");if(!binding.open(QIODevice::ReadOnly))return 12;
    auto changedBinding=QJsonDocument::fromJson(binding.readAll()).object();binding.close();auto sourceBinding=changedBinding.value("community").toObject();
    sourceBinding.insert("source_sha256",QString::fromLatin1(QCryptographicHash::hash(changedCsv,QCryptographicHash::Sha256).toHex()));changedBinding.insert("community",sourceBinding);
    if(!binding.open(QIODevice::WriteOnly)||binding.write(QJsonDocument(changedBinding).toJson())<0)return 13;binding.close();
    model.observeSourceCompletion({{"schema","pgn-source-range-completion-1"},{"run",QString(32,'e')},
        {"start","199999000000000000000"},{"end","200000000000000000000"},{"source_positions","1000000000000000"},
        {"source_range_complete",true},{"independent_coverage",false}},true);app.processEvents();
    if(model.completedPositions()!="1000000000000000")return 21;
    if(localAtlas->property("pgn_atlas_processed_segments").toInt()!=1)return 80;
    bool processedVisible=false;for(const auto& value:combined->observations()){const auto row=value.toObject();if(row["status"]=="LOCAL_SOURCE_FINISHED"){
        processedVisible=row["owner"].toString()==model.researcherName()&&row["_device_color"].toString()=="#46c8ee";
    }}if(!processedVisible)return 81;
    auto* atlas=community->findChild<QWidget*>("pgn-source-atlas");
    if(!model.source().valid()||model.source().ranges.size()!=initialSourceRows+1||!atlas||atlas->property("pgn_source_atlas_rows").toInt()!=initialSourceRows+1)return 14;
    const auto afterSourceRefresh=pgn::ui::rangeCalendarSelection(calendar);
    if(!afterSourceRefresh||afterSourceRefresh->start!=beforeSourceRefresh->start||afterSourceRefresh->end!=beforeSourceRefresh->end){std::cerr<<"FAIL actual selection changed during source refresh\n";return 53;}
    localAtlas->repaint();if(pgn::ui::rangeAtlasJobRect(localAtlas,run).isEmpty())return 27;
    auto* historyButton=community->findChild<QPushButton*>("pgn-booking-history-open");if(!historyButton)return 15;historyButton->click();
    auto* historyView=community->findChild<QWidget*>("pgn-booking-history-view");if(!historyView)return 16;
    QElapsedTimer timer;timer.start();while(timer.elapsed()<5000&&!historyView->property("pgn_booking_history_rows").isValid()){app.processEvents();QThread::msleep(1);}
    if(historyView->property("pgn_booking_history_rows").toInt()<1)return 17;
    auto* dialog=community->findChild<QWidget*>("pgn-booking-history-dialog");if(!dialog)return 18;dialog->close();app.processEvents();
    if(argc>2){community->grab().save(QString::fromLocal8Bit(argv[2])+"/community-qa.png");canvas->grab().save(QString::fromLocal8Bit(argv[2])+"/booking-mining-qa.png");}
    // Complete virtual domains share the exact left/right partition. Browsing
    // does not allocate work, rewrite old jobs, or infer UNKNOWN as free.
    const auto existingJobs=model.jobs().size();model.setViewBitLength(68);app.processEvents();localAtlas->repaint();
    if(model.viewCount()!=1477||model.viewRanges().size()!=1000||localAtlas->property("pgn_catalog_bits").toInt()!=68||calendar->property("pgn_calendar_slots").toInt()!=100||model.jobs().size()!=existingJobs)return 84;
    for(const auto size:{QSize(1380,900),QSize(1200,540)}){community->resize(size);app.processEvents();if(community->size()!=size||!community->rect().contains(QRect(queueButton->mapTo(community.get(),QPoint()),queueButton->size()))){std::cerr<<"FAIL catalog viewport actual "<<community->width()<<"x"<<community->height()<<" minimum "<<community->minimumSizeHint().height()<<"\n";return 85;}}
    model.setViewPage(1);app.processEvents();if(model.viewRanges().size()!=477||localAtlas->property("pgn_catalog_page_zones").toInt()!=477)return 86;
    model.setViewBitLength(78);model.setViewPage(model.viewPages()-1);app.processEvents();localAtlas->repaint();
    if(model.viewCount()<1000000||model.viewRanges().size()>1000||!pgn::ui::rangeAtlasJobRect(localAtlas,run).isEmpty()||model.jobs().size()!=existingJobs)return 87;
    if(argc>2)community->grab().save(QString::fromLocal8Bit(argv[2])+"/booking-level78-last-page-qa.png");
    model.setViewBitLength(0);app.processEvents();
    std::cout<<"PASS shared context reservation / persisted intent, rail range click / selection, exact raw progress zero, missing-data timestamp hold; NO engine/network mutation\n";
}
