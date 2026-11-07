#include "segmented_prime_orders.h"
#include "native_small_factor_sieve.h"
#include <array>
#include <chrono>
#include <iostream>
#include <string>
using namespace pgn_psp_tail;
using U128=unsigned __int128;
static uint64_t checks{};
static void need(bool b,const char* why) { ++checks; if(!b) throw std::runtime_error(why); }
static uint64_t reference_power(uint64_t exponent,uint64_t modulus) {
    uint64_t a=2,r=1;
    while(exponent) { if(exponent&1) r=uint64_t(U128(r)*a%modulus); exponent>>=1; a=uint64_t(U128(a)*a%modulus); }
    return r;
}
static uint64_t independent_order(uint64_t p,const std::vector<uint32_t>& primes) {
    uint64_t remaining=p-1,d=p-1;
    for(const auto r:primes) {
        if(uint64_t(r)*r>remaining) break;
        if(remaining%r) continue;
        while(remaining%r==0) remaining/=r;
        while(d%r==0 && reference_power(d/r,p)==1) d/=r;
    }
    if(remaining>1) while(d%remaining==0 && reference_power(d/remaining,p)==1) d/=remaining;
    return d;
}
static void qualified_window(uint64_t lo,uint64_t hi,uint32_t span) {
    const auto base=base_primes(uint32_t(integer_sqrt(hi-1)));
    std::vector<PrimeOrder> want,got;
    // Independent trial division per integer; no segmented p-1 sieve.
    for(auto p=lo;p<hi;++p) {
        bool prime=true;
        for(const auto r:base) { if(uint64_t(r)*r>p) break; if(p%r==0) { prime=false;break; } }
        if(prime) want.push_back({p,independent_order(p,base)});
    }
    const auto s=stream(lo,hi,span,[&](PrimeOrder r){got.push_back(r);});
    need(got.size()==want.size() && s.rows==want.size(),"Prime stream count differs from trial division");
    for(size_t i=0;i<want.size();++i)
        need(got[i].prime==want[i].prime && got[i].order==want[i].order,"Prime/order stream mismatch");
}
int main() try {
    const auto started=std::chrono::steady_clock::now();
    uint64_t state=0x8472345234902341ULL;
    for(unsigned i=0;i<12000;++i) {
        state=state*6364136223846793005ULL+1442695040888963407ULL;
        const auto m=(state&((uint64_t(1)<<62)-1))|3;
        const auto exponent=state^(state>>31);
        need(Base2Power(m).power(exponent)==reference_power(exponent,m),"Montgomery64 differs from exact128mod oracle");
    }
    for(auto m:std::array<uint64_t,8>{3,5,17,4294967291ULL,4294967311ULL,74995840921ULL,(uint64_t(1)<<62)+57,(uint64_t(1)<<63)-1})
        for(auto e:std::array<uint64_t,5>{0,1,2,2142738312ULL,~uint64_t(0)})
            need(Base2Power(m).power(e)==reference_power(e,m),"Montgomery boundary differs");
    qualified_window(3,100000,257);
    qualified_window(2142738313ULL-4096,2142738313ULL+4096,997);
    qualified_window((uint64_t(1)<<32)-8192,(uint64_t(1)<<32)+8192,2048);
    qualified_window((uint64_t(1)<<34)-4096,(uint64_t(1)<<34)+4096,509);
    // Full established canonical domain, not a sample. Only one bounded bank
    // from the old implementation is retained here for equality qualification.
    const auto ref=pgn_psp_native::SmallFactorTable::build();
    size_t at=0;
    const auto began=std::chrono::steady_clock::now();
    const auto s=stream(80001,10000001,262144,[&](PrimeOrder r){
        need(at<ref.rows().size(),"Extra canonical stream row");
        const auto& expected=ref.rows()[at++];
        need(r.prime==expected.prime && r.order==expected.order,"Canonical1e7bank mismatch");
    });
    need(at==ref.rows().size(),"Missing canonical stream rows");
    const auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-began).count();
    unsigned rejected=0;
    for(unsigned i=0;i<4;++i) try {
        if(i==0) stream(3,MaximumPrimeEnd+1,100,[](auto){});
        if(i==1) stream(2,100,100,[](auto){});
        if(i==2) stream(3,100,0,[](auto){});
        if(i==3) stream(3,100,MaximumSegmentSpan+1,[](auto){});
    } catch(const std::invalid_argument&) { ++rejected; }
    need(rejected==4,"Unsafe stream input not rejected");
    std::cout.precision(12);
    std::cout<<"{\"PASS\":true,\"checks\":"<<checks
      <<",\"Montgomery_oracle_cases\":12040,\"canonical_rows_equal\":"<<s.rows
      <<",\"canonical_seconds\":"<<seconds<<",\"segments\":"<<s.segments
      <<",\"peak_segment_array_bytes\":"<<s.peak_array_bytes
      <<",\"independent_wide_windows\":4,\"invalid_input_rejected\":"<<rejected
      <<",\"full_68bit_enumeration\":false,\"GPU_used\":false,\"production_changed\":false"
      <<",\"total_seconds\":"<<std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()<<"}\n";
    return 0;
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
