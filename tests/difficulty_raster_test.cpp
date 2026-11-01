#include "ui/difficulty_history_view.h"
#include "ui/render/horizon_workspace_style.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QThread>
#include <QPushButton>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QDir>
#include <iostream>
#define REQUIRE(x) do{if(!(x)){std::cerr<<"FAIL "<<__LINE__<<": "<<#x<<'\n';return 1;}}while(0)
static QString hash(int n){return QString::fromLatin1(QCryptographicHash::hash(QByteArray::number(n),QCryptographicHash::Sha256).toHex());}
static QJsonObject header(int n){QJsonObject object{{"hash",hash(n)},{"height",n},{"difficulty",n==128?QJsonValue():QJsonValue(double(n))}};if(n)object.insert("previousblockhash",hash(n-1));return object;}
int main(int argc,char** argv){QApplication app(argc,argv);QTemporaryDir temp;REQUIRE(temp.isValid());using namespace pgn::wallet;using namespace pgn::ui;
 DifficultyHistoryStore store(temp.path()+"/history.sqlite");const CoreEpoch owner{12,"fixture-birth",1};const auto utc=QDateTime::fromString("2026-10-13T13:00:00.123Z",Qt::ISODateWithMs);const auto genesis=hash(0);
 for(int n=0;n<257;++n)REQUIRE(store.record(owner,genesis,hash(n),header(n),utc).isEmpty());ChainObservation anchor{owner,genesis,256,utc,false,hash(256)};REQUIRE(store.activate(anchor,anchor.tipHash).isEmpty());
 const DenseView bounds{0,512,0,512,{640,240},0};const auto cancel=std::make_shared<std::atomic_bool>(false);const auto initial=renderDifficultyHistory(store.file(),store.summary(genesis),bounds,cancel);
 REQUIRE(initial.complete&&initial.points==256&&initial.rowsRead==257&&!initial.image.isNull());
 REQUIRE(store.record(owner,genesis,hash(257),header(257),utc.addMSecs(1)).isEmpty());anchor.height=257;anchor.tipHash=hash(257);anchor.observedUtc=utc.addMSecs(1);REQUIRE(store.activate(anchor,anchor.tipHash).isEmpty());
 const auto extended=renderDifficultyHistory(store.file(),store.summary(genesis),bounds,cancel,&initial),oracle=renderDifficultyHistory(store.file(),store.summary(genesis),bounds,cancel);
 REQUIRE(extended.complete&&extended.incremental&&extended.rowsRead==1&&extended.points==257);REQUIRE(extended.image==oracle.image);
 auto fork=header(257);fork.insert("hash",hash(-257));fork.insert("difficulty",42.);REQUIRE(store.record(owner,genesis,hash(-257),fork,utc.addMSecs(2)).isEmpty());anchor.tipHash=hash(-257);anchor.observedUtc=utc.addMSecs(2);REQUIRE(store.activate(anchor,anchor.tipHash).isEmpty());
 const auto reorganized=renderDifficultyHistory(store.file(),store.summary(genesis),bounds,cancel,&extended),forkOracle=renderDifficultyHistory(store.file(),store.summary(genesis),bounds,cancel);
 REQUIRE(reorganized.complete&&!reorganized.incremental&&reorganized.image==forkOracle.image&&reorganized.points==257);
 cancel->store(true);REQUIRE(!renderDifficultyHistory(store.file(),store.summary(genesis),bounds,cancel).complete);cancel->store(false);
 auto invalid=bounds;invalid.maxX=invalid.minX;REQUIRE(!renderDifficultyHistory(store.file(),store.summary(genesis),invalid,cancel).complete);
 std::unique_ptr<QWidget> view(createDifficultyHistoryView(store.file()));view->resize(960,400);view->show();presentDifficultyHistory(view.get(),store.summary(genesis));QElapsedTimer wait;wait.start();
 view->setStyleSheet("QMainWindow,QWidget{background:#010b10;color:#c9eee7;}"+HorizonWorkspaceControlStyle());
 while(wait.elapsed()<5000&&view->property("pgn_wallet_difficulty_frame_points").toLongLong()!=257){app.processEvents();QThread::msleep(5);}
 REQUIRE(view->property("pgn_wallet_difficulty_frame_points").toLongLong()==257);REQUIRE(view->property("pgn_wallet_difficulty_points").toLongLong()==257);REQUIRE(view->property("pgn_wallet_difficulty_missing").toLongLong()==1);REQUIRE(view->property("pgn_wallet_difficulty_error").toString().isEmpty());
 view->findChild<QPushButton*>("pgn-wallet-difficulty-reset")->click();
 presentDifficultyHistoryError(view.get(),"fixture source failure");REQUIRE(view->property("pgn_wallet_difficulty_source_error").toString()=="fixture source failure");REQUIRE(view->property("pgn_wallet_difficulty_points").toLongLong()==257);
 presentDifficultyHistory(view.get(),{});REQUIRE(view->property("pgn_wallet_difficulty_points").toLongLong()==257);REQUIRE(view->property("pgn_wallet_difficulty_frame_points").toLongLong()==257);
 presentDifficultyHistory(view.get(),store.summary(genesis));REQUIRE(!view->property("pgn_wallet_difficulty_source_error").isValid());
 // Exercise the actual widget after resize, off-grid zoom and two-axis pan.
 // Inspect rendered frame pixels as well as the exact viewport tick endpoints.
 const QString output=argc>1?QString::fromLocal8Bit(argv[1]):QString{};
 if(!output.isEmpty())REQUIRE(QDir(output).exists());
 const auto ready=[&]{QElapsedTimer t;t.start();while(t.elapsed()<5000&&view->property("pgn_wallet_difficulty_loading").toBool()){app.processEvents();QThread::msleep(5);}app.processEvents();return !view->property("pgn_wallet_difficulty_loading").toBool();};
 const auto borders=[&](const QString& name){
  const auto image=view->grab().toImage();const auto area=view->property("pgn_wallet_difficulty_plot").toRectF();
  const auto bounds=view->property("pgn_wallet_difficulty_view").toList(),xt=view->property("pgn_wallet_difficulty_x_ticks").toList(),yt=view->property("pgn_wallet_difficulty_y_ticks").toList();
  if(bounds.size()!=4||xt.size()<2||yt.size()<2||xt.front()!=bounds[0]||xt.back()!=bounds[1]||yt.front()!=bounds[2]||yt.back()!=bounds[3])return false;
  const qreal ratio=image.devicePixelRatio();
  for(const auto point:{QPointF(area.center().x(),area.top()),QPointF(area.center().x(),area.bottom()),QPointF(area.left(),area.center().y()),QPointF(area.right(),area.center().y())}){
   if(image.pixelColor(qRound(point.x()*ratio),qRound(point.y()*ratio))!=QColor("#b6e5dd"))return false;
  }
  return output.isEmpty()||image.save(QDir(output).filePath(name+".png"));
 };
 for(const auto size:{QSize(640,260),QSize(960,400),QSize(1380,768)}){
  view->findChild<QPushButton*>("pgn-wallet-difficulty-reset")->click();
  view->resize(size);app.processEvents();REQUIRE(ready());REQUIRE(borders(QString("difficulty-%1-default").arg(size.width())));
  const auto center=view->property("pgn_wallet_difficulty_plot").toRectF().center();
  QWheelEvent zoom(center,center,QPoint(),QPoint(0,173),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);QApplication::sendEvent(view.get(),&zoom);
  REQUIRE(ready());REQUIRE(borders(QString("difficulty-%1-zoom").arg(size.width())));
  QMouseEvent down(QEvent::MouseButtonPress,center,center,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(view.get(),&down);
  const auto moved=center+QPointF(37.5,-13.25);
  QMouseEvent move(QEvent::MouseMove,moved,moved,Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(view.get(),&move);
  QMouseEvent up(QEvent::MouseButtonRelease,moved,moved,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view.get(),&up);
  REQUIRE(ready());REQUIRE(borders(QString("difficulty-%1-pan").arg(size.width())));
 }
 std::cout<<"PASS full 258-header raster, exact zero/absence, incremental/full pixel parity, reorg fallback, cancellation and actual offscreen widget; no live Core claim\n";
}
