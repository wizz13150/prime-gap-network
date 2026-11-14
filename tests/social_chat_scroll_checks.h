#pragma once
#include "ui/social_view.h"
#include "ui/social_chat_list.h"
#include <QApplication>
#include <QEventLoop>
#include <QFile>
#include <QDir>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>
#include <stdexcept>

namespace pgn::test::scroll {
inline int checks=0;
inline void expect(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
inline void settle(){QEventLoop loop;QTimer::singleShot(35,&loop,&QEventLoop::quit);loop.exec();}
inline QPair<QString,int> anchor(QListWidget* list){QPair<QString,int> partial;for(int row=0;row<list->count();++row){auto* item=list->item(row);const auto rect=list->visualItemRect(item);if(item->isHidden()||rect.bottom()<0)continue;if(rect.top()>=0)return {item->data(Qt::UserRole).toString(),rect.top()};if(partial.first.isEmpty())partial={item->data(Qt::UserRole).toString(),rect.top()};}return partial;}
inline bool bottom(QListWidget* list){return list->verticalScrollBar()->maximum()-list->verticalScrollBar()->value()<=2&&list->visualItemRect(list->item(list->count()-1)).bottom()<=list->viewport()->height();}
inline void sameAnchor(QListWidget* list,const QPair<QString,int>& expected,const char* why){++checks;for(int row=0;row<list->count();++row){auto* item=list->item(row);if(item->data(Qt::UserRole).toString()==expected.first){const auto top=list->visualItemRect(item).top();if(top==expected.second)return;throw std::runtime_error(QString("%1: message %2 expected y=%3, actual y=%4").arg(why,expected.first).arg(expected.second).arg(top).toStdString());}}throw std::runtime_error(QString("%1: anchor %2 disappeared").arg(why,expected.first).toStdString());}
// setValue is a programmatic value change, not a user drag. In particular it
// does not emit sliderPressed/actionTriggered to supersede queued restoration.
// https://doc.qt.io/qt-6/qabstractslider.html#sliderDown-prop
inline void userScrollTo(QListWidget* list,int value){auto* bar=list->verticalScrollBar();bar->setSliderDown(true);bar->setSliderPosition(value);bar->setSliderDown(false);expect(bar->value()==value,"User slider did not reach its requested position");}
inline void eventFence(){for(int n=0;n<8;++n){QCoreApplication::sendPostedEvents();QApplication::processEvents(QEventLoop::AllEvents);}}
inline void pendingInput(){
 ui::SocialChatList list;list.resize(640,400);list.show();list.beginUpdate();for(int n=0;n<80;++n){auto* item=new QListWidgetItem(QString("Synthetic pending-layout input %1").arg(n));item->setData(Qt::UserRole,QString::number(n));item->setSizeHint(QSize(600,60+n%3*20));list.addItem(item);}list.endUpdate();eventFence();expect(bottom(&list),"Pending input fixture did not reach latest");
 // Force an outstanding restoration. The input must win even without a wait.
 list.beginUpdate();list.endUpdate();userScrollTo(&list,list.verticalScrollBar()->maximum()/3+7);const auto drag=anchor(&list);eventFence();expect(!bottom(&list),"Queued restoration overrode an actual slider interaction");sameAnchor(&list,drag,"Queued restoration moved the slider reading anchor");
 list.jumpToLatest();eventFence();list.beginUpdate();list.endUpdate();const auto center=list.viewport()->rect().center();QWheelEvent wheel(QPointF(center),QPointF(list.viewport()->mapToGlobal(center)),QPoint{},QPoint(0,360),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);QApplication::sendEvent(list.viewport(),&wheel);const auto wheeled=anchor(&list);eventFence();expect(!bottom(&list),"Queued restoration overrode a wheel interaction");sameAnchor(&list,wheeled,"Wheel reading anchor changed after delayed layout");
 list.jumpToLatest();eventFence();list.setCurrentRow(list.count()-1);list.beginUpdate();list.endUpdate();for(int n=0;n<2;++n){QKeyEvent down(QEvent::KeyPress,Qt::Key_PageUp,Qt::NoModifier);QApplication::sendEvent(&list,&down);QKeyEvent up(QEvent::KeyRelease,Qt::Key_PageUp,Qt::NoModifier);QApplication::sendEvent(&list,&up);}const auto keyed=anchor(&list);eventFence();expect(!bottom(&list),"Queued restoration overrode keyboard history navigation");sameAnchor(&list,keyed,"Keyboard reading anchor changed after delayed layout");list.close();
}
inline void run(const QString& path,const QString& captures={}){
 pendingInput();
 using namespace social;const Identity identity{QString(64,'a'),"SYNTHETIC-SCROLL-A"};
 QVector<Entry> records;const auto time=QDateTime::currentDateTimeUtc().addSecs(-1200);
 const auto entry=[&](int n,QString author="SYNTHETIC-SCROLL-B"){Entry e;e.id=QString("synthetic-scroll-%1").arg(n);e.authorId=author;e.author=author==identity.subject?"Synthetic author A":"Synthetic author B";e.createdUtc=time.addSecs(n);e.body=QString("Synthetic scroll contract message %1. ").arg(n).repeated(4+(n%3)*4);e.bbcode=true;return e;};
 for(int n=20;n<100;++n)records.append(entry(n));
 bool older=false,defer=false;QVector<QPair<ReadRequest,Services::Reply>> pending;
 std::optional<SendRequest> sent;std::function<void(SendResult)> sendReply;
 Services services;services.read=[&](ReadRequest request,Services::Reply reply){
  if(defer){pending.append({request,reply});return;}
  Snapshot out;out.identity=request.identity;out.section=request.section;out.channel=request.channel.isEmpty()?"one":request.channel;out.authenticated=true;out.source="SYNTHETIC SCROLL INTERACTION FIXTURE";out.observedUtc=QDateTime::currentDateTimeUtc();out.channels=QVector<Channel>{{"one","Synthetic one",{}},{"two","Synthetic two",{}}};out.chatSend=SendAvailability{true,{}};out.chatHistoryGeneration=QString(64,'d');
  if(out.channel=="two")out.entries={entry(300)};
  else if(!request.cursor.isEmpty()){for(int n=0;n<20;++n)out.entries.append(entry(n));older=true;}
  else{out.entries=records;if(!older)out.nextCursor="older-synthetic";}
  reply(out,{});
 };
 services.sendChat=[&](SendRequest request,auto done){sent=request;sendReply=done;};
 QWidget shell;shell.setWindowTitle("Social R183 · isolated interaction review");shell.setStyleSheet("QWidget{background:#041319;color:#c9eee7;}");auto* box=new QVBoxLayout(&shell);box->setContentsMargins(0,0,0,0);box->setSpacing(0);
 auto* notice=new QLabel("REVISION ONLY · synthetic interaction fixture · no network messages or rights");notice->setStyleSheet("font:12px 'Segoe UI';color:#e6bd64;padding:6px 14px;");box->addWidget(notice);
 ui::SocialView view(path,services);box->addWidget(&view,1);shell.resize(1366,990);view.setIdentity(identity);shell.show();settle();
 QJsonArray rendered;const auto capture=[&](QString state){if(captures.isEmpty())return;expect(QDir().mkpath(captures),"Capture directory unavailable");const auto file=QString("chat-%1x%2-%3.png").arg(shell.width()).arg(shell.height()).arg(state);expect(shell.grab().save(captures+"/"+file),"Qt interaction capture failed");rendered.append(QJsonObject{{"file",file},{"width",shell.width()},{"height",shell.height()},{"window_state",int(shell.windowState())},{"normal_window",!shell.isMaximized()&&!shell.isFullScreen()},{"source","Explicit synthetic UI interaction fixture; actual SocialView"}});};
 auto* list=view.findChild<QListWidget*>("pgn-social-list-0");expect(list&&list->count()==80,"Scroll fixture did not load real chronology widget");expect(bottom(list),"Initial chronology did not settle at latest message");
 records.append(entry(100));view.refresh();settle();expect(bottom(list),"Incoming message moved a reader who was following the bottom");
 userScrollTo(list,list->verticalScrollBar()->maximum()/3+7);settle();const auto historical=anchor(list);expect(!historical.first.isEmpty()&&!bottom(list),"History interaction fixture did not leave latest");
 records.append(entry(101));view.refresh();settle();expect(anchor(list)==historical,"Incoming message changed historical message/pixel anchor after layout");
 auto* unseen=view.findChild<QPushButton*>("pgn-social-new-messages");expect(unseen&&unseen->isVisible(),"Incoming history message did not expose a jump-to-latest notice");
 view.refresh();settle();expect(anchor(list)==historical,"Unchanged refresh moved reading anchor");
 capture("history-new-message");shell.resize(1920,1030);settle();sameAnchor(list,historical,"Width/height reflow moved historical message/pixel anchor");capture("history-new-message");
 auto* more=view.findChild<QPushButton*>("pgn-social-more-0");expect(more&&more->isEnabled(),"Older messages control unavailable in scroll fixture");more->click();settle();expect(list->count()==102,"Older messages did not append exact history records");sameAnchor(list,historical,"Prepending older messages moved historical reading anchor");
 records.append(entry(103));view.refresh();settle();expect(list->count()==103,"Latest-page refresh discarded previously loaded older history");sameAnchor(list,historical,"Incoming message after Older moved historical reading anchor");
 unseen->click();settle();expect(bottom(list)&&!unseen->isVisible(),"Jump to latest did not settle/clear new-message notice");
 userScrollTo(list,list->verticalScrollBar()->maximum()/2);settle();
 auto* body=view.findChild<QPlainTextEdit*>("pgn-social-draft-body-0");auto* send=view.findChild<QPushButton*>("pgn-social-send-0");body->setPlainText("[b]Synthetic own send[/b]");send->click();expect(sent&&bool(sendReply),"Own send did not reach Services boundary");
 auto own=entry(102,identity.subject);own.body=sent->bbcode;records.append(own);const auto request=*sent;sendReply({identity,request.channel,request.requestId,SendOutcome::Posted,own.id,"Synthetic local publication"});settle();expect(bottom(list),"Own published message was not visible at bottom after insertion/layout");
 capture("own-posted-bottom");shell.resize(1366,990);settle();expect(bottom(list),"Bottom-following resize lost latest message");capture("own-posted-bottom");
 defer=true;view.refresh();expect(pending.size()==1,"Deferred old channel fixture missing");const auto old=pending[0];auto* channels=view.findChild<QListWidget*>("pgn-social-chat-channels");expect(channels&&channels->count()==2,"Channel fixture directory missing");channels->setCurrentRow(1);QMetaObject::invokeMethod(channels,"itemClicked",Qt::DirectConnection,Q_ARG(QListWidgetItem*,channels->item(1)));expect(pending.size()==2&&pending.last().first.channel=="two","Channel change did not isolate read request");
 Snapshot late;late.identity=identity;late.section=Section::Chat;late.channel="one";late.authenticated=true;late.source="SYNTHETIC STALE CHANNEL";late.observedUtc=QDateTime::currentDateTimeUtc();late.entries={entry(400)};old.second(late,{});settle();expect(list->count()==0,"Old channel completion inserted mixed conversation data");
 auto current=pending.last();late.channel="two";late.entries={entry(300)};current.second(late,{});settle();expect(list->count()==1&&list->item(0)->data(Qt::UserRole)=="synthetic-scroll-300"&&bottom(list),"New channel did not display its own history coherently");
 if(!captures.isEmpty()){QFile file(captures+"/CAPTURES.json");expect(file.open(QIODevice::WriteOnly),"Capture receipt unavailable");file.write(QJsonDocument(QJsonObject{{"actual_component",true},{"network_proof",false},{"fixtures_separate",true},{"images",rendered}}).toJson());}
 shell.close();
 {
  int latest=499;QString generation(64,'e');bool conflict=false;
  Services bounded;bounded.read=[&](ReadRequest request,Services::Reply done){Snapshot out;out.identity=request.identity;out.section=request.section;out.channel=request.channel.isEmpty()?"one":request.channel;out.authenticated=true;out.source="SYNTHETIC BOUNDED HISTORY FIXTURE";out.observedUtc=QDateTime::currentDateTimeUtc();out.chatHistoryGeneration=generation;
   const int end=request.cursor.isEmpty()?latest+1:request.cursor.toInt(),start=qMax(0,end-100);for(int n=start;n<end;++n)out.entries.append(entry(n));if(conflict)out.entries[0].body="Conflicting same-ID source";if(start)out.nextCursor=QString::number(start);done(out,{});
  };
  ui::SocialView capped(path+"-bounded",bounded);capped.setIdentity(identity);capped.resize(1366,990);capped.show();settle();auto* timeline=capped.findChild<QListWidget*>("pgn-social-list-0");auto* previous=capped.findChild<QPushButton*>("pgn-social-more-0");for(int page=0;page<4;++page){previous->click();settle();}expect(timeline->count()==500,"Native continuous history exceeded/lost its bounded loaded window");
  userScrollTo(timeline,timeline->verticalScrollBar()->maximum()/2);settle();const auto reading=anchor(timeline);latest=500;capped.refresh();settle();expect(timeline->count()==500&&timeline->item(499)->data(Qt::UserRole)=="synthetic-scroll-499","Full history window displaced a reading anchor for a new message");sameAnchor(timeline,reading,"Full bounded window moved reader");
  auto* jump=capped.findChild<QPushButton*>("pgn-social-new-messages");expect(jump&&jump->isVisible(),"Full bounded window did not signal new messages");jump->click();settle();expect(timeline->count()==500&&timeline->item(499)->data(Qt::UserRole)=="synthetic-scroll-500"&&bottom(timeline),"Jump latest did not refresh/prune bounded window without losing latest");
  generation=QString(64,'f');capped.refresh();settle();expect(timeline->count()==100&&bottom(timeline),"New authenticated history generation mixed old loaded pages");
  const auto untouched=timeline->item(0)->text();conflict=true;capped.refresh();settle();expect(timeline->count()==100&&timeline->item(0)->text()==untouched&&capped.property("pgn_social_state_0")=="error","Conflicting immutable message ID silently replaced source");
  conflict=false;generation="invalid-generation";capped.refresh();settle();expect(timeline->count()==100&&capped.property("pgn_social_state_0")=="error","Malformed native history generation accepted");capped.close();
 }
}
}
