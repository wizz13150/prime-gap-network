#include "booking/booking_model.h"
#include "ui/community_view.h"
#include "ui/range_calendar.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QMenu>
#include <QAction>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QPushButton>
#include <iostream>
#include <memory>

int main(int argc,char** argv){
 QApplication app(argc,argv);if(argc<2)return 1;QTemporaryDir temp;QDir().mkpath(temp.path()+"/data/runtime");
 for(const auto name:{"binding.json","community-source.csv"})if(!QFile::copy(QString::fromLocal8Bit(argv[1])+"/"+name,temp.path()+"/data/runtime/"+name))return 2;
 pgn::booking::BookingModel model(temp.path());std::unique_ptr<QWidget> view(pgn::ui::createCommunityView(&model));view->resize(1200,750);view->show();app.processEvents();
 auto* grid=view->findChild<QWidget*>("pgn-range-calendar-grid");auto* first=view->findChild<QPushButton*>("pgn-calendar-first-open");if(!grid||!first)return 3;
 first->click();app.processEvents();const auto zone=pgn::ui::rangeCalendarSelection(grid);if(!zone)return 4;
 const auto cells=pgn::ui::rangeCalendarSlots(zone->start,zone->end);if(cells.size()!=100)return 5;
 const QString run(32,'d');model.observeRuntime(run,cells[89].start,cells[89].end,"GPU-qa","QA","1",QDateTime::currentDateTimeUtc(),true);app.processEvents();
 const auto mouse=[&](QEvent::Type type,int index,Qt::KeyboardModifiers mods=Qt::NoModifier){const auto p=pgn::ui::rangeCalendarCell(grid,index).center();QMouseEvent event(type,p,grid->mapToGlobal(p.toPoint()),type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton,mods);QApplication::sendEvent(grid,&event);app.processEvents();};
 const auto click=[&](int i,Qt::KeyboardModifiers m=Qt::NoModifier){mouse(QEvent::MouseButtonPress,i,m);mouse(QEvent::MouseButtonRelease,i,m);};
 const auto selected=[&](std::initializer_list<int> indices){const auto chosen=pgn::ui::rangeCalendarSelections(grid);if(chosen.size()!=qsizetype(indices.size()))return false;int j=0;for(int i:indices)if(chosen[j++].start!=cells[i].start)return false;return true;};
 click(0);click(2,Qt::ControlModifier);if(!selected({0,2})||pgn::ui::rangeCalendarSelection(grid))return 6;
 for(int i=2;i<6;++i){model.observeRuntime(run,cells[89].start,cells[89].end,"GPU-qa","QA",QString::number(i),model.runtime().observedUtc.addMSecs(1),true);app.processEvents();if(!selected({0,2}))return 7;}
 pgn::ui::focusCurrentRangeCalendar(grid);app.processEvents();if(!selected({0,2}))return 8;
 const auto action=[&](int index,const QString& name){const auto p=pgn::ui::rangeCalendarCell(grid,index).center().toPoint();QContextMenuEvent event(QContextMenuEvent::Mouse,p,grid->mapToGlobal(p));QApplication::sendEvent(grid,&event);app.processEvents();auto menus=view->findChildren<QMenu*>("pgn-booking-context");if(menus.isEmpty())return false;auto* menu=menus.last();auto* a=menu->findChild<QAction*>(name);if(!a||!a->isEnabled())return false;const auto q=menu->actionGeometry(a).center();bool invoked=false;QObject::connect(a,&QAction::triggered,menu,[&]{invoked=true;});for(auto t:{QEvent::MouseMove,QEvent::MouseButtonPress,QEvent::MouseButtonRelease}){QMouseEvent e(t,q,menu->mapToGlobal(q),t==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,t==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(menu,&e);}app.processEvents();return invoked;};
 const auto before=model.jobs().size();if(!action(2,"pgn-reserve-selected")||model.jobs().size()!=before+2||!selected({0,2}))return 9;
 if(model.availableSelection({{cells[1].start,cells[1].end,zone->start,zone->end}})!=1)return 10;
 click(0,Qt::ControlModifier);if(!selected({2}))return 11;
 if(!action(2,"pgn-release-selected")||model.jobs().size()!=before+1)return 12;
 click(0);if(!selected({0})||!action(0,"pgn-release-selected")||model.jobs().size()!=before)return 13;
 mouse(QEvent::MouseButtonPress,3);mouse(QEvent::MouseMove,6);mouse(QEvent::MouseButtonRelease,6);if(!selected({3,4,5,6}))return 14;
 click(5,Qt::ControlModifier);if(!selected({3,4,6}))return 15;
 click(8);if(!selected({8}))return 16;
 model.setRoundRange(2);app.processEvents();if(!pgn::ui::rangeCalendarSelections(grid).isEmpty())return 17;
 std::cout<<"PASS Qt pointer Ctrl disjoint selection/toggle, drag, exact reserve/release preserving holes, live updates/focus preserve selection, plain click and page change reset\n";return 0;
}
