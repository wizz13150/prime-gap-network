#include "booking/booking_model.h"
#include "booking/round_range_catalog.h"
#include "ui/community_view.h"
#include "ui/range_calendar.h"
#include "ui/range_atlas.h"
#include "ui/prototype_community_atlas.h"
#include "ui/booking_context_menu.h"
#include "horizon_workspace_style.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QMenu>
#include <QPointer>
#include <QAction>
#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QSpinBox>
#include <QLineEdit>
#include <QPushButton>
#include <iostream>
#include <memory>

int main(int argc,char** argv){
 QApplication app(argc,argv);app.setStyleSheet(HorizonWorkspaceControlStyle());if(argc<2)return 1;
 QTemporaryDir temp;QDir().mkpath(temp.path()+"/data/runtime");
 for(const auto name:{"binding.json","community-source.csv"})if(!QFile::copy(QString::fromLocal8Bit(argv[1])+"/"+name,temp.path()+"/data/runtime/"+name))return 2;
 pgn::booking::BookingModel model(temp.path());if(!model.source().valid())return 3;
 const QString run(32,'a');model.observeRuntime(run,"199998000000000000000","199999000000000000000","GPU-qa","QA","0",QDateTime::currentDateTimeUtc(),true);
 std::unique_ptr<QWidget> view(pgn::ui::createCommunityView(&model));view->resize(1200,540);view->show();app.processEvents();
 if(view->size()!=QSize(1200,540)||view->findChild<QPushButton*>("pgn_booking_queue"))return 4;
 if(!model.viewIsRoundRange()||model.viewCount()!=1000||model.viewRanges().first().start!="100000000000000000000"||model.viewRanges().last().end!="200000000000000000000")return 5;
 auto* grid=view->findChild<QWidget*>("pgn-range-calendar-grid");
 auto* jump=view->findChild<QSpinBox*>("pgn-round-range-jump");if(!jump||!grid)return 6;
 auto* editor=jump->findChild<QLineEdit*>();if(!editor)return 30;editor->setFocus();editor->selectAll();
 for(const auto key:{Qt::Key_9,Qt::Key_1}){QKeyEvent event(QEvent::KeyPress,key,Qt::NoModifier,key==Qt::Key_9?"9":"1");QApplication::sendEvent(editor,&event);}
 for(int i=1;i<=4;++i){model.observeRuntime(run,"199998000000000000000","199999000000000000000","GPU-qa","QA",QString::number(i),model.runtime().observedUtc.addMSecs(1),true);app.processEvents();if(editor->text()!="91"||model.viewPage()!=1)return 31;}
 QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(editor,&enter);app.processEvents();if(model.viewPage()!=91)return 32;
 auto* home=view->findChild<QPushButton*>("pgn-default-range");if(!home)return 33;home->click();app.processEvents();if(model.viewPage()!=1||model.jobs().size()!=1)return 34;
 jump->setValue(2);app.processEvents();if(model.viewRanges().first().start!="200000000000000000000"||model.viewRanges().last().end!="300000000000000000000"||!view->findChild<QLineEdit*>("pgn_booking_start")->text().isEmpty()||model.jobs().size()!=1)return 7;
 for(int page:{0,1,3022}){const auto c=pgn::booking::roundRangeCatalog(page,model.source());if(c.ranges.size()!=1000)return 8;
  for(int i=0;i<1000;++i){const auto& row=c.ranges[i];if(pgn::booking::subtractUnsigned(row.end,row.start)!="100000000000000000"||(i&&c.ranges[i-1].end!=row.start)||pgn::ui::rangeCalendarSlots(row.start,row.end).size()!=100)return 9;}
 }
 auto* first=view->findChild<QPushButton*>("pgn-calendar-first-open");if(!first)return 10;first->click();app.processEvents();
 const auto free=pgn::ui::rangeCalendarSelection(grid);if(!free||!model.source().declaredOpenContaining(free->start,free->end)||model.jobs().size()!=1)return 11;
 const auto context=[&](QWidget* target,QPoint point){QContextMenuEvent event(QContextMenuEvent::Mouse,point,target->mapToGlobal(point));QApplication::sendEvent(target,&event);app.processEvents();auto menus=view->findChildren<QMenu*>("pgn-booking-context");return menus.isEmpty()?static_cast<QMenu*>(nullptr):menus.last();};
 int pointerClicks=0;
 const auto trigger=[&](QMenu* menu,const QString& name){
  if(!menu)return false;auto* action=menu->findChild<QAction*>(name);if(!action)return false;
  const auto geometry=menu->actionGeometry(action);if(geometry.isEmpty())return false;
  QPointer<QMenu> guarded(menu);bool clicked=false;const auto connection=QObject::connect(action,&QAction::triggered,menu,[&]{clicked=true;});
  const auto point=geometry.center(),global=menu->mapToGlobal(point);
  QMouseEvent move(QEvent::MouseMove,point,global,Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(guarded.data(),&move);
  QMouseEvent press(QEvent::MouseButtonPress,point,global,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);if(guarded)QApplication::sendEvent(guarded.data(),&press);
  QMouseEvent release(QEvent::MouseButtonRelease,point,global,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);if(guarded)QApplication::sendEvent(guarded.data(),&release);
  app.processEvents();QObject::disconnect(connection);++pointerClicks;return clicked;
 };
 auto* menu=context(grid,pgn::ui::rangeCalendarCell(grid,0).center().toPoint());if(!trigger(menu,"pgn-reserve-zone")||model.jobs().size()!=101)return 12;
 // First open skips our entire reservation and the active interval.
 first->click();app.processEvents();const auto following=pgn::ui::rangeCalendarSelection(grid);if(!following||pgn::booking::compareUnsigned(following->start,free->end)<0)return 13;
 pgn::ui::selectRangeCalendar(grid,free->start,free->end);app.processEvents();
 menu=context(grid,pgn::ui::rangeCalendarCell(grid,0).center().toPoint());if(!trigger(menu,"pgn-release-zone")||model.jobs().size()!=1||model.runtime().run!=run)return 14;
 // Block reservations are independent, so any selected block can be released.
 const auto cells=pgn::ui::rangeCalendarSlots(free->start,free->end);pgn::ui::selectRangeCalendar(grid,cells[0].start,cells[1].end);
 menu=context(grid,pgn::ui::rangeCalendarCell(grid,0).center().toPoint());if(!trigger(menu,"pgn-reserve-selected")||model.jobs().size()!=3)return 15;
 pgn::ui::selectRangeCalendar(grid,cells[0].start,cells[0].end);menu=context(grid,pgn::ui::rangeCalendarCell(grid,0).center().toPoint());if(!trigger(menu,"pgn-release-selected")||model.jobs().size()!=2)return 16;
 menu=context(grid,pgn::ui::rangeCalendarCell(grid,0).center().toPoint());if(!trigger(menu,"pgn-reserve-selected")||model.jobs().size()!=3)return 35;
 pgn::booking::Booking queued;for(const auto& job:model.jobs())if(job.kind=="LOCAL_INTENT")queued=job;
 if(queued.id.isEmpty())return 23;
 auto* atlas=view->findChild<QWidget*>("pgn-source-atlas");atlas->repaint();const auto rect=pgn::ui::rangeAtlasJobRect(atlas,queued.id);if(rect.isEmpty())return 17;
 menu=context(atlas,rect.center().toPoint());if(!menu||menu->findChild<QAction*>("pgn-reserve-selected")||menu->findChild<QAction*>("pgn-release-selected"))return 18;
 if(!trigger(menu,"pgn-reserve-zone")||model.jobs().size()!=101||model.runtime().run!=run)return 19;
 atlas->repaint();menu=context(atlas,pgn::ui::rangeAtlasJobRect(atlas,model.jobs().first().id).center().toPoint());
 if(!trigger(menu,"pgn-release-zone")||model.jobs().size()!=1)return 36;
 model.observeSourceCompletion({{"schema","pgn-source-range-completion-1"},{"run",QString(32,'b')},{"start",free->start},{"end",free->end},{"source_positions",pgn::booking::subtractUnsigned(free->end,free->start)},{"source_range_complete",true},{"independent_coverage",false}},false);
 pgn::ui::selectRangeCalendar(grid,free->start,free->end);menu=context(grid,pgn::ui::rangeCalendarCell(grid,0).center().toPoint());
 if(!menu||menu->findChild<QAction*>("pgn-reserve-zone")->isEnabled()||menu->findChild<QAction*>("pgn-release-zone")->isEnabled()||model.completedRanges().size()!=1)return 21;
 // A disabled action must reject an actual press/release, not just look disabled.
 std::cerr<<"POINTER_STAGE complete\n";
 for(const auto* name:{"pgn-reserve-selected","pgn-release-selected","pgn-reserve-zone","pgn-release-zone"}){
  QPointer<QMenu> completeMenu(context(grid,pgn::ui::rangeCalendarCell(grid,0).center().toPoint()));
  if(!completeMenu)return 46;auto* action=completeMenu->findChild<QAction*>(QString::fromLatin1(name));
  if(!action||action->isEnabled()||completeMenu->actionGeometry(action).isEmpty()||trigger(completeMenu.data(),QString::fromLatin1(name))||model.jobs().size()!=1||model.completedRanges().size()!=1||model.runtime().run!=run)return 46;
  if(completeMenu)completeMenu->close();app.processEvents();
 }
 app.processEvents();first->click();app.processEvents();const auto after=pgn::ui::rangeCalendarSelection(grid);
 if(!after||pgn::booking::compareUnsigned(after->start,free->end)<0||model.completedRanges().size()!=1)return 22;
 // One zone contains active99 and completed100. Selecting ALL100 must reserve
 // the98 free blocks, not silently disable the menu because of the two overlaps.
 model.observeSourceCompletion({{"schema","pgn-source-range-completion-1"},{"run",QString(32,'c')},{"start","199999000000000000000"},{"end","200000000000000000000"},{"source_positions","1000000000000000"},{"source_range_complete",true},{"independent_coverage",false}},false);
 pgn::ui::selectRangeCalendar(grid,"199900000000000000000","200000000000000000000");menu=context(grid,pgn::ui::rangeCalendarCell(grid,0).center().toPoint());
 if(!trigger(menu,"pgn-reserve-selected")||model.jobs().size()!=99||model.completedRanges().size()!=2||model.runtime().run!=run)return 37;
 menu=context(grid,pgn::ui::rangeCalendarCell(grid,0).center().toPoint());if(menu->findChild<QAction*>("pgn-reserve-selected")->isEnabled())return 38;
 if(!trigger(menu,"pgn-release-zone")||model.jobs().size()!=1||model.completedRanges().size()!=2||model.runtime().run!=run)return 39;
 // Older grouped rows must not silently disable an explicit block release.
 first->click();app.processEvents();const auto legacyZone=pgn::ui::rangeCalendarSelection(grid);if(!legacyZone)return 40;
 const auto legacyCells=pgn::ui::rangeCalendarSlots(legacyZone->start,legacyZone->end);
 if(!model.queue(legacyCells[0].start,legacyCells[3].end,model.runtime().device).isEmpty())return 41;
 pgn::ui::selectRangeCalendar(grid,legacyCells[1].start,legacyCells[1].end);menu=context(grid,pgn::ui::rangeCalendarCell(grid,1).center().toPoint());
 if(!trigger(menu,"pgn-release-selected")||model.jobs().size()!=3||model.runtime().run!=run)return 42;
 for(const auto& job:model.jobs())if(job.kind=="LOCAL_INTENT"&&pgn::booking::overlaps(job.start,job.end,legacyCells[1].start,legacyCells[1].end))return 43;
 menu=context(grid,pgn::ui::rangeCalendarCell(grid,0).center().toPoint());if(!trigger(menu,"pgn-release-zone")||model.jobs().size()!=1||model.runtime().run!=run)return 44;
 pgn::ui::BookingContextRequest large{legacyZone->start,legacyZone->end,legacyZone->start,legacyZone->end,{},false};const auto viewed=model.viewRanges();
 for(int i=400;i<441;++i)large.zones.append({viewed[i].start,viewed[i].end,viewed[i].start,viewed[i].end});
 QPointer<QMenu> capacityMenu(pgn::ui::createBookingContextMenu(view.get(),&model,large));auto* capacityAction=capacityMenu->findChild<QAction*>("pgn-reserve-zone");
 if(!capacityAction||capacityAction->isEnabled()||!capacityAction->toolTip().contains("4096")||model.jobs().size()!=1)return 45;
 capacityMenu->popup(view->mapToGlobal(QPoint(20,20)));app.processEvents();
 std::cerr<<"POINTER_STAGE capacity\n";
 if(capacityMenu->actionGeometry(capacityAction).isEmpty()||trigger(capacityMenu.data(),"pgn-reserve-zone")||model.jobs().size()!=1||model.runtime().run!=run)return 47;
 if(capacityMenu)capacityMenu->close();app.processEvents();
 // Active block99 is protected even when its surrounding zone has free blocks.
 pgn::ui::selectRangeCalendar(grid,"199998000000000000000","199999000000000000000");
 std::cerr<<"POINTER_STAGE active\n";
 for(const auto* name:{"pgn-reserve-selected","pgn-release-selected","pgn-release-zone"}){
  QPointer<QMenu> activeMenu(context(grid,pgn::ui::rangeCalendarCell(grid,98).center().toPoint()));
  if(!activeMenu)return 48;auto* action=activeMenu->findChild<QAction*>(QString::fromLatin1(name));
  if(!action||action->isEnabled()||activeMenu->actionGeometry(action).isEmpty()||trigger(activeMenu.data(),QString::fromLatin1(name))||model.jobs().size()!=1||model.completedRanges().size()!=2||model.runtime().run!=run)return 48;
  if(activeMenu)activeMenu->close();app.processEvents();
 }
 app.processEvents();
 std::cout<<"POINTER_MATRIX actual_Qt_press_release="<<pointerClicks<<" disabled_complete=4 disabled_capacity=1 disabled_active=3; cloned fixture only, no OS-compositor or public-lease claim\n";
 std::cout<<"PASS actual left/right context actions, whole/selected/legacy partial reservations, atomic release/conservation, explicit capacity feedback, active/history protection, FirstOpen, round1000x100 navigation; no network/miner mutation\n";
}
