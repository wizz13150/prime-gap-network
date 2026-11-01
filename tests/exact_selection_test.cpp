#include "booking/booking_store.h"
#include "booking/decimal_range.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <iostream>
int main(int argc,char** argv){QCoreApplication app(argc,argv);using namespace pgn::booking;QTemporaryDir temp;BookingStore store(temp.path()+"/selection.sqlite");
 SourceRegistry source;const QString a="100000000000000000000",z="100100000000000000000",unit="1000000000000000",device="GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac";source.ranges={{1,a,z,"Open",{}},{2,z,"100200000000000000000","Reserved","Robert"}};
 const auto bound=[&](int i){auto value=a;for(int j=0;j<i;++j)value=addUnsigned(value,unit);return value;};const auto block=[&](int i){return ExactBlockSelection{bound(i),bound(i+1),a,z};};
 const QVector<ExactBlockSelection> disjoint{block(0),block(2),block(2)};
 if(store.availableSelection(source,disjoint)!=2||!store.queueSelection(source,disjoint,device).isEmpty()||store.active().size()!=2)return 1;
 if(store.availableBlocks(source,bound(1),bound(2),a,z)!=1)return 2; // the unselected hole is still Open
 if(!store.releaseSelection(disjoint).isEmpty()||!store.active().isEmpty())return 3;
 if(!store.observeRuntime(QString(32,'a'),bound(9),bound(10),device,true).isEmpty()||!store.recordSourceCompletion(QString(32,'b'),bound(10),bound(11),device).isEmpty())return 4;
 const QVector<ExactBlockSelection> mixed{block(9),block(10),block(11),{z,addUnsigned(z,unit),z,"100200000000000000000"}};
 if(store.availableSelection(source,mixed)!=1||!store.queueSelection(source,mixed,device).isEmpty())return 5;
 const auto count=store.active().size();auto invalid=disjoint;invalid.append({bound(20),addUnsigned(bound(20),"1"),a,z});
 if(store.queueSelection(source,invalid,device).isEmpty()||store.active().size()!=count)return 6; // all-or-nothing invalid tail
 if(!store.releaseSelection(mixed).isEmpty()||store.active().size()!=1||store.completedRanges().isEmpty())return 7;
 // Explicit partial release of a legacy group preserves its unselected hole.
 // The original identity/bounds remain in history; a new remainder is queued.
 if(!store.queue(source,bound(0),bound(3),device).isEmpty())return 8;
 if(!store.releaseSelection(disjoint).isEmpty()||store.active().size()!=2)return 9;
 for(const auto& job:store.active())if(job.kind=="LOCAL_INTENT"&&(job.start!=bound(1)||job.end!=bound(2)))return 12;
 bool originalRetained=false;for(const auto& job:store.history())if(job.kind=="LOCAL_INTENT"&&job.start==bound(0)&&job.end==bound(3)&&job.state=="RELEASED")originalRetained=true;if(!originalRetained)return 13;
 if(!store.releaseSelection({{bound(0),bound(3),a,z}}).isEmpty()||store.active().size()!=1)return 10;
 const QVector<ExactBlockSelection> incompatible{{bound(20),bound(21),a,z},{bound(20),addUnsigned(bound(21),"1"),bound(20),addUnsigned(bound(20),"100000000000000100")}};
 if(store.queueSelection(source,incompatible,device).isEmpty()||store.active().size()!=1)return 11;
 std::cout<<"PASS exact disjoint union, duplicate suppression, unselected hole preserved, atomic reserve/release, active/processed/foreign protection, invalid-tail rollback, conflicting-grid refusal, explicit legacy partial-release conservation/history\n";return 0;}
