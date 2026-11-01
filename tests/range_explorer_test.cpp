#include "booking/range_explorer.h"
#include "booking/bit_length_catalog.h"
#include <QCoreApplication>
#include <iostream>

int main(int argc,char** argv){
    QCoreApplication app(argc,argv);using namespace pgn::booking;int checks=0,failures=0;
    auto check=[&](bool condition){++checks;if(!condition)++failures;};
    auto reject=[&](auto operation){bool failed=false;try{operation();}catch(const std::exception&){failed=true;}check(failed);};
    check(rangeInputInteger("1.5e20")=="150000000000000000000");
    check(rangeInputInteger(" 150 000_000000000000000 ")=="150000000000000000000");
    check(rangeInputInteger("1.00000000000000000001e20")=="100000000000000000001");
    check(rangeInputInteger("120e-1")=="12");check(rangeInputInteger(".5e2")=="50");
    check(rangeInputInteger("0e999")=="0");
    for(const auto* value:{"1.5","12e-1","1e999","NaN","-1","1e2e3","1,5e20","1.5e+",""})reject([&]{rangeInputInteger(value);});
    auto selected=rangeSelection("1.5e20","1e15",RangeBound::Length);
    check(selected.start=="150000000000000000000"&&selected.end=="150001000000000000000"&&selected.length=="1000000000000000");
    check(selected.firstBits==68&&selected.lastBits==68);
    for(int bits=67;bits<=95;++bits){
        const auto begin=powerOfTwo(bits-1),end=powerOfTwo(bits);
        const auto whole=rangeSelection(begin,end,RangeBound::ExclusiveEnd);check(whole.firstBits==bits&&whole.lastBits==bits);
        const auto last=rangeSelection(subtractUnsigned(end,"1"),"1",RangeBound::Length);check(last.end==end&&last.firstBits==bits&&last.lastBits==bits);
        check(rangePositionAtPixel(last.start,last.end,0,1000)==last.start);check(rangePositionAtPixel(last.start,last.end,1000,1000)==last.end);
    }
    const auto end95=powerOfTwo(95),near95=subtractUnsigned(end95,"1000");
    check(rangePositionAtPixel(near95,end95,501,1000)==addUnsigned(near95,"501"));
    reject([&]{rangeSelection("5","0",RangeBound::Length);});reject([&]{rangeSelection("5","4",RangeBound::ExclusiveEnd);});reject([&]{rangePositionAtPixel("0","10",1,0);});
    SourceRegistry source;source.ranges={{1,"100","200","Open",{}},{2,"200","300","Open",{}},{3,"300","400","Reserved","Someone"}};
    auto status=rangeAvailability(source,rangeSelection("150","250",RangeBound::ExclusiveEnd));check(status.fullyPublished&&status.declaredOpen&&status.rows==2);
    status=rangeAvailability(source,rangeSelection("250","350",RangeBound::ExclusiveEnd));check(status.fullyPublished&&!status.declaredOpen&&status.status=="Mixed"&&status.owners==QStringList{"Someone"});
    status=rangeAvailability(source,rangeSelection("50","150",RangeBound::ExclusiveEnd));check(!status.fullyPublished&&!status.declaredOpen&&status.status=="Partially published");
    status=rangeAvailability(source,rangeSelection("1000","1001",RangeBound::ExclusiveEnd));check(!status.fullyPublished&&!status.declaredOpen&&status.status=="Unknown");
    source.ranges={{1,"100","200","Open",{}},{2,"201","300","Open",{}}};
    status=rangeAvailability(source,rangeSelection("150","250",RangeBound::ExclusiveEnd));check(!status.fullyPublished&&!status.declaredOpen);
    std::cout<<"range explorer checks="<<checks<<" failures="<<failures<<"; exact integers/scientific input,all29widths,95-bitpixelprecision,unknownisnotfree; no DB/GPU/network\n";
    return failures?1:0;
}
