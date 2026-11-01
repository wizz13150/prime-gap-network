#include "booking/booking_store.h"
#include "booking/decimal_range.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <iostream>
#include <sqlite3.h>

int main(int argc,char** argv){
    QCoreApplication app(argc,argv);using namespace pgn::booking;
    if(canonicalUnsigned("00008")!="8"||scaleDecimal("199999.125",15)!="199999125000000000000"||
       subtractUnsigned("200000000000000000000","199999000000000000000")!="1000000000000000"||
       addUnsigned("199999000000000000000","1000000000000000")!="200000000000000000000"||
       bitLength("200000000000000000000")!=68)return 1;
    if(rangeFraction("199999000000000000001","199999000000000000000","199999000000000000002")!=0.5)return 2;
    if(overlaps("1","2","2","3")||!overlaps("1","3","2","4"))return 3;
    bool invalid=false;try{scaleDecimal("1e20",15);}catch(...){invalid=true;}if(!invalid)return 4;
    SourceRegistry source;
    if(argc>1){source=loadSourceRegistry(QString::fromLocal8Bit(argv[1]));if(!source.valid()){std::cerr<<source.error.toStdString();return 5;}}
    else{source.start="100000000000000000000";source.end="200000000000000000000";
        source.ranges.append({1001,source.start,source.end,"Open",{}});}
    QTemporaryDir dir;const auto file=dir.path()+"/bookings.sqlite";
    const QString first="199997000000000000000",last="199998000000000000000",device="GPU-qa";
    QString queued;
    {BookingStore store(file);if(!store.valid()||!store.queue(source,first,last,device).isEmpty())return 6;
        const auto rows=store.active();if(rows.size()!=1||rows[0].state!="QUEUED"||rows[0].kind!="LOCAL_INTENT")return 7;
        queued=rows[0].id;
        if(store.queue(source,first,last,device).isEmpty())return 8;
        if(!store.release(queued).isEmpty()||store.release(queued).isEmpty())return 9;
        if(!store.active().isEmpty())return 10;
        if(!store.observeRuntime(QString(32,'a'),first,last,device,true).isEmpty())return 11;
        if(store.release(QString(32,'a')).isEmpty())return 12;
        if(!store.recordSourceCompletion(QString(32,'a'),first,last,device).isEmpty())return 13;
        if(store.queue(source,first,last,device).isEmpty())return 14;
        if(store.observeRuntime(QString(32,'a'),first,"199999000000000000000",device,true).isEmpty())return 15;
    }
    {BookingStore recovered(file);const auto rows=recovered.history();
        bool finished=false;for(const auto& row:rows)finished|=row.id==QString(32,'a')&&row.state=="SOURCE_FINISHED";
        if(rows.size()!=2||!recovered.active().isEmpty()||!finished)return 16;
        if(!recovered.observeRuntime(QString(32,'b'),"199998000000000000000","199999000000000000000",device,true).isEmpty())return 17;
        if(!recovered.observeRuntime(QString(32,'b'),"199998000000000000000","199999000000000000000",device,false).isEmpty())return 18;
        if(recovered.active().size()!=1||recovered.active()[0].state!="PAUSED")return 19;
        if(recovered.observeRuntime(QString(32,'z'),first,last,device,true).isEmpty())return 20;
        if(recovered.observeRuntime(QString(32,'A'),first,last,device,true).isEmpty())return 21;
    }
    {
        const auto aliasFile=dir.path()+"/aliases.sqlite";BookingStore store(aliasFile);
        if(!store.queue(source,first,last,device).isEmpty())return 22;
        const auto intent=store.active().front().id;const auto run=QString(32,'c');
        if(!store.observeRuntime(run,first,last,device,true).isEmpty()||store.active().size()!=2)return 23;
        sqlite3* db=nullptr;if(sqlite3_open(aliasFile.toUtf8().constData(),&db)!=SQLITE_OK)return 24;
        const auto sql=QString("CREATE TABLE dispatch_requests(id TEXT PRIMARY KEY,run TEXT);INSERT INTO dispatch_requests VALUES('%1','%2');UPDATE bookings SET state='DISPATCHED' WHERE id='%1';").arg(intent,run).toUtf8();
        if(sqlite3_exec(db,sql.constData(),nullptr,nullptr,nullptr)!=SQLITE_OK)return 25;
        if(store.active().size()!=1||store.active().front().id!=run||store.history().size()!=1)return 26;
        if(!store.recordSourceCompletion(run,first,last,device).isEmpty()||store.completedPositions()!="1000000000000000")return 27;
        sqlite3_stmt* count=nullptr;sqlite3_prepare_v2(db,"SELECT COUNT(*) FROM bookings",-1,&count,nullptr);sqlite3_step(count);
        if(sqlite3_column_int(count,0)!=2)return 28; // both source identities conserved, no deletion
        sqlite3_finalize(count);sqlite3_close(db);
    }
    {
        const auto completedFile=dir.path()+"/completed.sqlite";
        {BookingStore store(completedFile);
            // More than a history page: the first block must never disappear.
            for(int i=0;i<300;++i){const auto id=QString::number(i+1,16).rightJustified(32,'0');
                if(!store.recordSourceCompletion(id,QString::number(1000+i),QString::number(1001+i),device).isEmpty())return 29;
            }
            if(store.history(256).size()!=256)return 30;
        }
        BookingStore recovered(completedFile);const auto ranges=recovered.completedRanges();
        if(ranges.size()!=1||ranges[0].start!="1000"||ranges[0].end!="1300"||ranges[0].state!="SOURCE_FINISHED"||recovered.completedPositions()!="300")return 31;
    }
    std::cout<<"PASS exact ranges/68 bits/adjacent positions, durable local queue/release, overlap guard, physical identity, complete processed union beyond history pagination/relaunch, intent/runtime visible alias without source deletion or double count, source-completion != certificate; source_rows="<<source.ranges.size()<<"\n";
}
