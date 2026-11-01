#include "booking/booking_store.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <sqlite3.h>
#include <iostream>
int main(int argc,char** argv){QCoreApplication app(argc,argv);QTemporaryDir temp;const auto file=temp.path()+"/abort.sqlite";pgn::booking::BookingStore store(file);
 const QString run(32,'a'),start="134000000000000000000",end="134001000000000000000",device="GPU-qa";
 if(!store.observeRuntime(run,start,end,device,true).isEmpty())return 1;sqlite3* db=nullptr;if(sqlite3_open(file.toUtf8().constData(),&db)!=SQLITE_OK)return 2;
 const auto sql="UPDATE bookings SET state='ABORTED' WHERE id='"+run+"' AND kind='RUNTIME'";
 if(sqlite3_exec(db,sql.toUtf8().constData(),nullptr,nullptr,nullptr)!=SQLITE_OK)return 3;
 if(!store.observeRuntime(run,start,end,device,true).isEmpty()||!store.observeRuntime(run,start,end,device,false).isEmpty()||!store.recordSourceCompletion(run,start,end,device).isEmpty())return 4;
 if(!store.active().isEmpty()||!store.completedRanges().isEmpty())return 5;
 sqlite3_stmt* q=nullptr;sqlite3_prepare_v2(db,"SELECT state FROM bookings WHERE id=?",-1,&q,nullptr);sqlite3_bind_text(q,1,run.toUtf8().constData(),-1,SQLITE_TRANSIENT);if(sqlite3_step(q)!=SQLITE_ROW||QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(q,0)))!="ABORTED")return 6;
 sqlite3_finalize(q);sqlite3_close(db);std::cout<<"PASS explicitly aborted runtime stays unallocated and unprocessed under stale ACTIVE/PAUSED/completion replay; audit row retained\n";return 0;}
