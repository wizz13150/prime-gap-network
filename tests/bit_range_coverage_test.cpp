#include <pgn/bit_range_coverage.h>
#include <array>
#include <iostream>
#include <random>
#include <stdexcept>
int main(){
    using namespace pgn::core;unsigned checks=0;
    const auto require=[&](bool good){++checks;if(!good)throw std::runtime_error("coverage check "+std::to_string(checks));};
    std::mt19937 random(0x51b17);
    // Exhaustive bitmap oracle on each generated small domain. Includes retries,
    // overlapping reports, adjacency, shuffled arrival and clipped bit boundaries.
    for(int trial=0;trial<1000;++trial){
        const int origin=1+random()%64,limit=origin+1+random()%(256-origin);
        std::array<bool,256> reported{},audited{};CoverageSpans reports,audits;
        for(int i=0;i<40;++i){
            const int a=1+random()%254,b=a+1+random()%(256-a);
            reports.emplace_back(a,b);if(i%3==0)reports.emplace_back(a,b);
            for(int n=a;n<b;++n)reported[n]=true;
            if(i%2==0){const int c=a+random()%(b-a);audits.emplace_back(c,b);for(int n=c;n<b;++n)audited[n]=true;}
        }
        std::shuffle(reports.begin(),reports.end(),random);
        const auto rows=ComputeBitRangeCoverage(origin,limit,reports,audits,1,8);
        for(const auto& row:rows){
            const int lo=row.lower.convert_to<int>(),hi=row.upper.convert_to<int>();int r=0,a=0;
            for(int n=std::max(lo,origin);n<std::min(hi,limit);++n){r+=reported[n];a+=audited[n];}
            require(row.reported==r&&row.audited==a);require(row.excluded_prefix==std::max(origin,lo)-lo);
            require(CoveragePartsPerBillion(row.reported,row.upper-row.lower)==uint64_t(r)*1000000000/(hi-lo));
        }
    }
    for(unsigned bits=67;bits<=95;++bits){
        const Integer low=Integer{1}<<(bits-1),high=Integer{1}<<bits;
        const auto row=ComputeBitRangeCoverage(low,high,{{low+1,low+3},{low+2,low+4},{high-2,high}},{{low+2,low+3}},bits,bits).at(0);
        require(row.reported==5&&row.audited==1&&row.excluded_prefix==0);
        require(row.lower==low&&row.upper==high&&row.search_end==high);
    }
    const Integer origin("150000000000000000000"),limit=Integer{1}<<95;
    auto rows=ComputeBitRangeCoverage(origin,limit,{{origin,origin+Integer("200000000000000")}},{});
    require(rows.front().bits==68&&rows.back().bits==95);
    require(rows.front().excluded_prefix==origin-(Integer{1}<<67));
    require(rows.front().reported==Integer("200000000000000"));
    require(rows[1].excluded_prefix==0&&rows[1].reported==0&&rows[1].audited==0);
    for(int kind=0;kind<5;++kind){bool rejected=false;try{
        if(kind==0)ComputeBitRangeCoverage(10,10,{},{});
        if(kind==1)ComputeBitRangeCoverage(0,100,{{10,10}},{});
        if(kind==2)ComputeBitRangeCoverage(0,100,{{10,20}},{{30,31}});
        if(kind==3)ComputeBitRangeCoverage(0,100,{{10,20},{21,40}},{{15,30}});
        if(kind==4)CoveragePartsPerBillion(11,10);
    }catch(const std::invalid_argument&){rejected=true;}require(rejected);}
    CoverageSpans massive;const Integer low=Integer{1}<<67;
    for(int i=0;i<100000;++i)massive.emplace_back(low+i,low+i+2);
    const auto dense=ComputeBitRangeCoverage(low,Integer{1}<<68,std::move(massive),{}).at(0);
    require(dense.reported==100001&&dense.audited==0);
    std::cout<<"PASS "<<checks<<" exact coverage checks; 1000 bitmap-oracle domains, 67..95-bit endpoints, 100000 overlapping reports\n";
}
