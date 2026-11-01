#include "ui/catalog_projection.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <sqlite3.h>
#include <iostream>
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);if(argc!=3)return 2;
 const auto database=QString::fromLocal8Bit(argv[1]),axis=QString::fromLocal8Bit(argv[2]);
 if(axis!="length"&&axis!="time")return 2;
 sqlite3* db=nullptr;const auto filename=database.toUtf8();
 if(sqlite3_open_v2(filename.constData(),&db,SQLITE_OPEN_READONLY,nullptr)!=SQLITE_OK)return 3;
 sqlite3_stmt* query=nullptr;
 if(sqlite3_prepare_v2(db,"SELECT min_x,max_x,min_y,max_y,points,revision FROM native_plot_bounds WHERE scope='' AND axis=?",-1,&query,nullptr)!=SQLITE_OK){sqlite3_close(db);return 4;}
 const auto axisBytes=axis.toUtf8();sqlite3_bind_text(query,1,axisBytes.constData(),axisBytes.size(),SQLITE_TRANSIENT);
 if(sqlite3_step(query)!=SQLITE_ROW){sqlite3_finalize(query);sqlite3_close(db);return 5;}
 const qint64 expected=sqlite3_column_int64(query,4);
 const pgn::ui::DenseView view{sqlite3_column_double(query,0)-1,sqlite3_column_double(query,1)+1,
  sqlite3_column_double(query,2)-1,sqlite3_column_double(query,3)+1,QSize(1000,350),static_cast<quint64>(sqlite3_column_int64(query,5))};
 sqlite3_finalize(query);sqlite3_close(db);
 const auto cancel=std::make_shared<std::atomic_bool>(false);const auto color=qRgb(90,220,170);
 const auto raw=pgn::ui::renderNativeCatalog(database,{},axis,view,cancel,color,nullptr,false);
 const auto compressed=pgn::ui::renderNativeCatalog(database,{},axis,view,cancel,color,nullptr,true);
 const bool exact=raw.complete&&compressed.complete&&raw.error.isEmpty()&&compressed.error.isEmpty()&&
  raw.points==expected&&compressed.points==expected&&raw.image==compressed.image;
 const auto digest=[](const QImage& image){return QString::fromLatin1(QCryptographicHash::hash(
  QByteArrayView(reinterpret_cast<const char*>(image.constBits()),image.sizeInBytes()),QCryptographicHash::Sha256).toHex());};
 const QJsonObject receipt{{"schema","pgn-real-native-cache-parity-1"},{"axis",axis},{"revision",static_cast<qint64>(view.revision)},
  {"expected_points",expected},{"raw_points",raw.points},{"compressed_points",compressed.points},{"exact_pixels",exact},
  {"raw_pixel_sha256",digest(raw.image)},{"compressed_pixel_sha256",digest(compressed.image)},
  {"raw_rows_read",raw.rowsRead},{"compressed_rows_read",compressed.rowsRead},{"raw_ms",raw.elapsedNs/1e6},{"compressed_ms",compressed.elapsedNs/1e6},
  {"raw_error",raw.error},{"compressed_error",compressed.error},{"gpu_control_calls",0},{"source_rows_modified",0},
  {"scope","One exact read-only viewport at fixed revision; not all-viewport latency or mining throughput"}};
 std::cout<<QJsonDocument(receipt).toJson(QJsonDocument::Compact).constData()<<'\n';return exact?0:6;
}
