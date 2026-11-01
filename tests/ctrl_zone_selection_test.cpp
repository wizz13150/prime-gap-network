#include "booking/booking_model.h"
#include "ui/community_view.h"
#include "ui/prototype_community_atlas.h"
#include "horizon_workspace_style.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QMenu>
#include <QAction>
#include <QSpinBox>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <iostream>
#include <memory>

int main(int argc,char** argv){
 QApplication app(argc,argv);app.setStyleSheet(HorizonWorkspaceControlStyle());if(argc<2)return 1;
 QTemporaryDir temp;QDir().mkpath(temp.path()+"/data/runtime");
 for(const auto name:{"binding.json","community-source.csv"})if(!QFile::copy(QString::fromLocal8Bit(argv[1])+"/"+name,temp.path()+"/data/runtime/"+name))return 2;
 pgn::booking::BookingModel model(temp.path());if(!model.source().valid())return 3;
 const QString run(32,'a');model.observeRuntime(run,"199998000000000000000","199999000000000000000","GPU-qa","QA","0",QDateTime::currentDateTimeUtc(),true);
 std::unique_ptr<QWidget> view(pgn::ui::createCommunityView(&model));view->resize(1280,700);view->show();app.processEvents();
 auto* atlas=dynamic_cast<PGNCommunityAtlas*>(view->findChild<QWidget*>("pgn-source-atlas"));if(!atlas)return 4;
 const auto zones=model.viewRanges();int first=-1;for(int i=0;i+2<zones.size();++i)if(model.source().declaredOpenContaining(zones[i].start,zones[i].end)&&model.source().declaredOpenContaining(zones[i+1].start,zones[i+1].end)&&model.source().declaredOpenContaining(zones[i+2].start,zones[i+2].end)){first=i;break;}
 if(first<0)return 5;
 auto refresh=[&]{app.processEvents();atlas->repaint();};
 const auto axis=zones[first].start.left(zones[first].start.size()-15).toDouble();atlas->focusRange(axis-200,axis+900);refresh();
 auto point=[&](int index){return atlas->zonePoint(zones[first+index].start).toPoint();};
 auto send=[&](QEvent::Type type,QPoint p,Qt::KeyboardModifiers mods,Qt::MouseButtons buttons){QMouseEvent event(type,p,atlas->mapToGlobal(p),type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,buttons,mods);QApplication::sendEvent(atlas,&event);};
 auto click=[&](int index,Qt::KeyboardModifiers mods=Qt::NoModifier){const auto p=point(index);send(QEvent::MouseButtonPress,p,mods,Qt::LeftButton);send(QEvent::MouseButtonRelease,p,mods,Qt::NoButton);refresh();};
 auto sweep=[&](int a,int b,Qt::KeyboardModifiers mods=Qt::NoModifier){send(QEvent::MouseButtonPress,point(a),mods,Qt::LeftButton);send(QEvent::MouseMove,point(b),mods,Qt::LeftButton);send(QEvent::MouseButtonRelease,point(b),mods,Qt::NoButton);refresh();};
 auto exact=[&](std::initializer_list<int> indices){const auto selected=atlas->zoneSelections();if(selected.size()!=int(indices.size()))return false;int n=0;for(int i:indices){if(selected[n].start!=zones[first+i].start||selected[n].end!=zones[first+i].end)return false;++n;}return true;};
 auto check=[](bool ok,int code,const char* message){if(!ok)std::cerr<<"FAIL "<<code<<": "<<message<<'\n';return ok;};
 const double lo=atlas->axisStart(),hi=atlas->axisEnd();
 click(0);click(2,Qt::ControlModifier);if(!check(exact({0,2})&&atlas->axisStart()==lo&&atlas->axisEnd()==hi,6,"Ctrl union without middle zone or camera movement"))return 6;
 click(0,Qt::ControlModifier);if(!check(exact({2}),7,"Ctrl removes selected zone"))return 7;click(0,Qt::ControlModifier);
 model.observeRuntime(run,"199998000000000000000","199999000000000000000","GPU-qa","QA","1",model.runtime().observedUtc.addMSecs(1),true);refresh();if(!check(exact({0,2}),8,"live publication preserves exact set"))return 8;
 auto menu=[&](int index){const auto p=point(index);QContextMenuEvent event(QContextMenuEvent::Mouse,p,atlas->mapToGlobal(p));QApplication::sendEvent(atlas,&event);app.processEvents();const auto menus=view->findChildren<QMenu*>("pgn-booking-context");return menus.isEmpty()?static_cast<QMenu*>(nullptr):menus.last();};
 auto trigger=[&](QMenu* m,const QString& name){if(!m)return false;auto* action=m->findChild<QAction*>(name);if(!action||!action->isEnabled())return false;bool fired=false;QObject::connect(action,&QAction::triggered,m,[&]{fired=true;});const auto p=m->actionGeometry(action).center();for(auto type:{QEvent::MouseMove,QEvent::MouseButtonPress,QEvent::MouseButtonRelease}){QMouseEvent event(type,p,m->mapToGlobal(p),type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(m,&event);}app.processEvents();return fired;};
 auto* m=menu(2);if(!check(m&&!m->findChild<QAction*>("pgn-reserve-selected")&&!m->findChild<QAction*>("pgn-release-selected")&&trigger(m,"pgn-reserve-zone")&&model.jobs().size()==201,9,"actual menu click reserves only two whole zones"))return 9;
 for(const auto& job:model.jobs())if(job.kind=="LOCAL_INTENT"&&pgn::booking::overlaps(job.start,job.end,zones[first+1].start,zones[first+1].end))return 10;
 refresh();if(!check(exact({0,2})&&trigger(menu(2),"pgn-release-zone")&&model.jobs().size()==1&&model.runtime().run==run,11,"release exact union, preserve active owner"))return 11;
 refresh();sweep(0,2);if(!check(exact({0,1,2})&&atlas->axisStart()==lo&&atlas->axisEnd()==hi,12,"zone-start drag selects without panning"))return 12;
 sweep(1,2,Qt::ControlModifier);if(!check(exact({0}),13,"Ctrl sweep removes selected contiguous subset"))return 13;
 sweep(1,2,Qt::ControlModifier);if(!check(exact({0,1,2}),14,"Ctrl sweep adds subset"))return 14;
 const QPoint background(300,56);send(QEvent::MouseButtonPress,background,Qt::NoModifier,Qt::LeftButton);send(QEvent::MouseMove,background+QPoint(90,0),Qt::NoModifier,Qt::LeftButton);send(QEvent::MouseButtonRelease,background+QPoint(90,0),Qt::NoModifier,Qt::NoButton);refresh();
 if(!check(exact({0,1,2})&&atlas->axisStart()!=lo&&model.jobs().size()==1,15,"background-start drag pans without booking/selection change"))return 15;
 atlas->focusRange(axis-200,axis+900);refresh();click(4);if(!check(exact({4}),16,"plain click replaces exact selection"))return 16;
 // Semantic cards retain block inspection, but context/selection stay zone-only.
 atlas->focusRange(axis+40,axis+160);refresh();if(!check(atlas->property("pgn_atlas_zoom_blocks").toInt()==200,17,"two detailed zones expose all inner blocks"))return 17;
 click(0);click(1,Qt::ControlModifier);if(!check(exact({0,1}),18,"Ctrl works over inner-block cards as whole zones"))return 18;
 // An in-flight gesture must not index a new page using its old anchor.
 send(QEvent::MouseButtonPress,point(0),Qt::NoModifier,Qt::LeftButton);
 view->findChild<QSpinBox*>("pgn-round-range-jump")->setValue(2);refresh();send(QEvent::MouseMove,QPoint(400,300),Qt::NoModifier,Qt::LeftButton);send(QEvent::MouseButtonRelease,QPoint(400,300),Qt::NoModifier,Qt::NoButton);refresh();
 if(!check(atlas->zoneSelections().isEmpty()&&model.jobs().size()==1,19,"page switch cancels stale gesture and clears old exact IDs"))return 19;
 std::cout<<"PASS right Ctrl union/toggle/sweep, exact atomic zone menu clicks, live preservation, background pan, detailed cards, page-switch cancellation; fixture only, no GPU/network mutation\n";
}
