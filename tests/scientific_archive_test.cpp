#include "science/archive_reader.h"
#include "ui/native_archive_view.h"
#include "booking/decimal_range.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTableWidget>
#include <QElapsedTimer>
#include <QThread>
#include <QPushButton>
#include <QLineEdit>
#include <QPainter>
#include <memory>
#include <iostream>
int main(int argc,char** argv){
 QApplication app(argc,argv);QTemporaryDir directory;QDir().mkpath(directory.path()+"/data/runtime");
 const auto file=directory.path()+"/data/runtime/scientific-history.sqlite";
 const auto base=QStringLiteral("199998000000000000000");
 {auto db=QSqlDatabase::addDatabase("QSQLITE","archive-fixture");db.setDatabaseName(file);if(!db.open())return 1;
  {QSqlQuery q(db);if(!q.exec("CREATE TABLE gaps(id INTEGER PRIMARY KEY,p TEXT,q TEXT,gap TEXT,merit TEXT,unix_ts INTEGER,UNIQUE(p,q))"))return 2;
   if(!q.exec("CREATE TABLE origins(gap_id INTEGER,run TEXT,source TEXT,UNIQUE(gap_id,run,source))"))return 3;
   db.transaction();q.prepare("INSERT INTO gaps VALUES(?,?,?,?,?,?)");
   for(int i=1;i<=1001;++i){const auto p=pgn::booking::addUnsigned(base,QString::number(i*2048));
    q.bindValue(0,i);q.bindValue(1,p);q.bindValue(2,pgn::booking::addUnsigned(p,"800"));q.bindValue(3,"800");q.bindValue(4,"17.1234567890123456789");
    q.bindValue(5,i==1?QVariant{}:QVariant(qint64(1791682500+i)));if(!q.exec())return 4;}
   q.prepare("INSERT INTO origins VALUES(1,?,?)");for(int i=0;i<131;++i){q.bindValue(0,QString(31,'a')+"b");q.bindValue(1,QString("native/test-%1.stdout").arg(i,3,10,QChar('0')));if(!q.exec())return 5;}
   db.commit();}db.close();}QSqlDatabase::removeDatabase("archive-fixture");
 auto first=pgn::science::readObservations(file);if(!first.error.isEmpty()||first.rows.size()!=100||first.total!=1001||first.through!=1001||!first.more)return 6;
 if(first.rows.first().p!=pgn::booking::addUnsigned(base,"2048")||!first.rows.first().originalUnixSeconds.isEmpty()||first.rows.first().merit!="17.1234567890123456789")return 7;
 int total=0;qint64 after=0;for(;;){const auto page=pgn::science::readObservations(file,after,first.through);
  if(!page.error.isEmpty())return 8;for(const auto& row:page.rows){if(row.id<=after)return 9;after=row.id;++total;}if(!page.more)break;}
 if(total!=1001)return 10;
 const auto exact=pgn::science::readObservations(file,0,0,first.rows.first().p);if(exact.rows.size()!=1||exact.total!=1||exact.rows.first().p!=first.rows.first().p)return 11;
 if(pgn::science::readObservations(file,0,0,"'; DROP TABLE gaps;--").error.isEmpty())return 12;
 if(pgn::science::readObservations(directory.path()+"/must-not-create.sqlite").error.isEmpty()||QFileInfo::exists(directory.path()+"/must-not-create.sqlite"))return 13;
 int originTotal=0;QString afterRun,afterSource;for(;;){const auto page=pgn::science::readOrigins(file,1,afterRun,afterSource);if(!page.error.isEmpty())return 14;
  originTotal+=page.rows.size();if(!page.more)break;afterRun=page.rows.last().run;afterSource=page.rows.last().source;}
 if(originTotal!=131)return 15;
 std::unique_ptr<QWidget> view(pgn::ui::createNativeArchiveView(directory.path()));view->resize(1360,750);
 auto wait=[&](const char* property,int value){QElapsedTimer timeout;timeout.start();do{app.processEvents();QThread::msleep(1);}while(view->property(property).toInt()!=value&&timeout.elapsed()<3000);return view->property(property).toInt()==value;};
 if(!wait("pgn_archive_native_total",1001)||view->property("pgn_archive_native_rows").toInt()!=100)return 16;
 auto* table=view->findChild<QTableWidget*>("pgn-native-archive-records");if(!table||!table->item(0,4)->text().contains("UNKNOWN"))return 17;
 table->selectRow(0);if(!wait("pgn_archive_native_origins",64))return 18;
 QPushButton* next=nullptr;for(auto* button:view->findChildren<QPushButton*>())if(button->text()=="Next")next=button;
 if(!next||!next->isEnabled())return 19;next->click();
 QElapsedTimer timeout;timeout.start();do{app.processEvents();QThread::msleep(1);}while(table->item(0,0)->text()==first.rows.first().p&&timeout.elapsed()<3000);
 if(table->item(0,0)->text()!=pgn::booking::addUnsigned(base,QString::number(101*2048)))return 20;
 if(argc>1){QImage capture(view->size(),QImage::Format_ARGB32_Premultiplied);capture.fill(Qt::transparent);QPainter painter(&capture);view->render(&painter);painter.end();capture.save(QString::fromLocal8Bit(argv[1]));}
 std::cout<<"PASS exact native p/q/merit, fixed-watermark all1001 keyset access, all131 origins, unknown original date, read-only refusal and real archive UI next/selection\n";
}
