#include "ui/render/graph_view.h"
#include "ui/render/graph_border_ticks.h"
#include <iostream>

int main() {
    int checks=0, failures=0;
    const auto check=[&](bool ok){++checks;if(!ok){++failures;std::cerr<<"FAILED check "<<checks<<'\n';}};
    const FrontierGraphView data{720., 1462., 16.8, 31.5};
    const auto old = FrontierNiceGapGraphView({720.,1462.,14.,34.},5);
    std::cout<<"Prior rounded view: x="<<old.minTimeMs<<".."<<old.maxTimeMs
             <<" y="<<old.minMerit<<".."<<old.maxMerit<<'\n';
    check(old.maxTimeMs > data.maxTimeMs && old.maxMerit > data.maxMerit + 2.); // excess empty domain
    const auto tight = FrontierContentGraphView(data);
    check(tight.has_value());
    check(tight->minTimeMs==720. && tight->maxTimeMs==1462.);
    check(tight->minMerit<16.8 && tight->maxMerit>31.5 && tight->maxMerit<32.);
    for(int n=1;n<=64;++n) {
        const auto ticks=FrontierVisibleMeritTicks(*tight,n,true);
        check(ticks.size()<=static_cast<std::size_t>(3*n+2));
        for(double v:ticks)check(v>=tight->minMerit&&v<=tight->maxMerit&&v==std::floor(v));
    }
    const auto upperTicks=FrontierVisibleMeritTicks({800.,1346.,16.7,28.8},4,false);
    check(upperTicks.size()>=5 && upperTicks.back()>=28. && upperTicks.front()>=16.7);
    for(int zoom=0;zoom<20;++zoom){
        const double width=std::pow(10.,zoom-10);
        const auto ticks=FrontierVisibleMeritTicks({0.,1.,2.*width,12.*width},5,false);
        check(ticks.size()>=5 && ticks.back()<=12.*width && ticks.front()>=2.*width);
    }
    const auto time=FrontierContentGraphView({1720000000123.,1720000001123.,20.,20.});
    check(time && time->maxTimeMs==1720000001123. && time->minTimeMs==1720000000123.);
    check(time->minMerit<20.&&time->maxMerit>20.);
    const auto zero=FrontierContentGraphView({0.,1.,0.,0.});
    check(zero && zero->minMerit==0.&&zero->maxMerit>0.);
    check(FrontierVisibleMeritTicks({0.,1.,22.1,22.4},8,true).empty()); // no false integer
    check(!FrontierContentGraphView({0.,1.,-1.,2.}));
    check(!FrontierContentGraphView({0.,0.,1.,2.}));
    check(!FrontierContentGraphView({0.,1.,2.,1.}));
    check(!FrontierContentGraphView({0.,1.,1.,std::numeric_limits<double>::infinity()}));
    check(!FrontierContentGraphView(data,std::numeric_limits<double>::quiet_NaN()));
    // Arbitrary zoom/pan must retain the exact viewport endpoints without
    // moving data or colliding with the nearest interior label.
    for(int zoom=0;zoom<20;++zoom)for(int pan=0;pan<50;++pan){
        const double span=std::pow(10.,zoom-10),lo=span*(pan+.137),hi=lo+span;
        std::vector<double> nice;
        for(int i=-1;i<15;++i)nice.push_back(lo+span*i/13.);
        std::reverse(nice.begin(),nice.end());nice.push_back(lo+span/2.);
        const auto ticks=FrontierBorderTicks(lo,hi,nice,512.,40.);
        check(ticks.size()>=2&&ticks.front()==lo&&ticks.back()==hi);
        for(std::size_t i=1;i<ticks.size();++i){
            check(ticks[i]>ticks[i-1]&&ticks[i]<=hi);
            check((ticks[i]-ticks[i-1])/span*512.>=40.-1e-8);
        }
    }
    const auto epoch=FrontierBorderTicks(1720000000123.,1720000001123.,
        {1720000000200.,1720000001000.},320.,80.);
    check(epoch==std::vector<double>({1720000000123.,1720000001123.}));
    check(epoch.front()==1720000000123.&&epoch.back()==1720000001123.);
    check(FrontierBorderTicks(1.,1.,{},320.,20.).empty());
    check(FrontierBorderTicks(0.,1.,{},0.,20.).empty());
    check(FrontierBorderTicks(0.,std::numeric_limits<double>::infinity(),{},320.,20.).empty());
    std::cout<<"Content graph fit: checks="<<checks<<" failures="<<failures
             <<"; data domain independent of tick rounding; integer labels exact\n";
    return failures?1:0;
}
