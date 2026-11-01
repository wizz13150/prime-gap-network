#include <pgn/sparse_range_pool.h>
#include <iostream>
#include <random>
#include <set>
#include <stdexcept>

int main(){
    using namespace pgn::core;
    unsigned checks=0;
    const auto check=[&](bool condition){++checks;if(!condition)throw std::runtime_error("Rolling pool invariant "+std::to_string(checks));};
    try{
        const Integer origin{"150000000000000000000"},width{"200000000000000"},end=Integer{1}<<95;
        const Integer slots=(end-origin+width-1)/width;
        SparseRangePool pool{origin,width,slots,10000,end};
        std::mt19937_64 random{735291};
        std::set<Integer> issued;
        for(unsigned i=0;i<100000;++i){
            check(pool.SelectionCount()==10000);
            const auto selected=pool.TakeFree(Integer{random()%10000});
            check(selected.has_value());
            check(issued.insert(selected->slot).second);
            check(selected->start==origin+selected->slot*width&&selected->end-selected->start==width);
            check(pool.SelectionCount()==10000&&!pool.SelectFree(10000));
        }
        // Expiry changes membership, never the size of the eligible window.
        unsigned expired=0;
        for(auto at=issued.begin();expired<100;++expired,++at){
            check(pool.Release(*at));
            check(pool.SelectionCount()==10000);
        }
        check(pool.SelectFree(0)->slot==0);
        SparseRangePool tail{5,10,3,2,Integer{26}};
        check(tail.TakeFree(0)->end==15);
        check(tail.TakeFree(0)->end==25);
        const auto last=tail.TakeFree(0);
        check(last->start==25&&last->end==26&&tail.SelectionCount()==0);
        check(!tail.SelectFree(0));
        SparseRangePool legacy{0,1,100000};
        check(legacy.SelectFree(50000)->slot==50000);
        std::cout<<"Rolling ticket pool PASS "<<checks<<" checks; 100000 assignments, expiry, exact tail and legacy decoding primitive\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
