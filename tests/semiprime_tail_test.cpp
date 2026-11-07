#include "semiprime_tail.h"
#include <chrono>
#include <iostream>
#include <string>
#include <tuple>
using namespace pgn_psp_tail;
using U128=unsigned __int128;
static uint64_t checks{};
static void need(bool b,const char* why){++checks;if(!b)throw std::runtime_error(why);}
static Number number(U128 n){return {uint64_t(n),uint64_t(n>>64)};}
static U128 value(Number n){return (U128(n.high)<<64)|n.low;}
static std::vector<uint8_t> independent_sieve(uint32_t hi){
    std::vector<uint8_t> prime(hi,1);prime[0]=prime[1]=0;
    for(uint32_t r=2;uint64_t(r)*r<hi;++r)if(prime[r])for(uint32_t n=r*r;n<hi;n+=r)prime[n]=0;
    return prime;
}
static uint64_t reference_power(uint64_t e,uint64_t m){
    uint64_t a=2,r=1;
    while(e){if(e&1)r=uint64_t(U128(r)*a%m);e>>=1;a=uint64_t(U128(a)*a%m);}return r;
}
static bool ej(uint64_t n){return reference_power((n-1)/2,n)==((n%8==1||n%8==7)?1:n-1);}
static std::vector<uint64_t> native_events(uint32_t a,uint64_t lo,uint64_t hi,const std::vector<uint8_t>& prime){
    std::vector<uint64_t> got;const Scope scope(a,{lo,0},{hi,0});
    if(integer_sqrt(hi-1)<=a)return got;
    stream(a+1,integer_sqrt(hi-1)+1,127,[&](PrimeOrder row){
        visit_row(scope,row,[&](CompositeWitness w){
            need(value(w.n)==U128(w.factor)*w.cofactor,"Invalid composite witness");
            // An independent final classifier, not part of the cheap row filter.
            if(prime[w.cofactor] && ej(w.n.low))got.push_back(w.n.low);
        });
    });
    std::sort(got.begin(),got.end());return got;
}
static void multi_scope_controls(){
    for(const uint64_t cutoff:{23,47,97})for(const uint32_t segment:{1,127,1024}){
        const auto hi=cutoff*cutoff*cutoff;
        const std::vector<Scope> scopes={Scope(cutoff,{3,0},{hi/3,0}),Scope(cutoff,{hi/3+7,0},{2*hi/3,0}),Scope(cutoff,{2*hi/3+11,0},{hi,0})};
        using Row=std::tuple<size_t,U128,uint64_t,uint64_t>;
        std::vector<Row> want,got;RowStats scalar{},multi{};
        stream(cutoff+1,integer_sqrt(hi-1)+1,segment,[&](PrimeOrder row){
            for(size_t i=0;i<scopes.size();++i){
                const auto s=visit_row(scopes[i],row,[&](CompositeWitness w){want.emplace_back(i,value(w.n),w.factor,w.cofactor);});
                scalar.odd_progression_events+=s.odd_progression_events;scalar.cofactor_passes+=s.cofactor_passes;
            }
            const auto s=visit_many(scopes,row,[&](size_t i,CompositeWitness w){got.emplace_back(i,value(w.n),w.factor,w.cofactor);});
            multi.odd_progression_events+=s.odd_progression_events;multi.cofactor_passes+=s.cofactor_passes;
        });
        need(got==want,"Multi-scope witness/ownership differs from scalar union");
        need(multi.odd_progression_events==scalar.odd_progression_events && multi.cofactor_passes==scalar.cofactor_passes,"Multi-scope counters differ");
    }
    size_t emitted=0;unsigned rejected=0;
    for(unsigned c=0;c<5;++c)try{
        std::vector<Scope> scopes;
        if(c==1)for(unsigned i=0;i<65;++i)scopes.emplace_back(97,Number{100+2*i,0},Number{101+2*i,0});
        if(c==2)scopes={Scope(47,{3,0},{100,0}),Scope(47,{99,0},{200,0})};
        if(c==3)scopes={Scope(47,{200,0},{300,0}),Scope(47,{3,0},{100,0})};
        if(c==4)scopes={Scope(97,{3,0},{912673,0})};
        visit_many(scopes,{127,7},[&](size_t,auto){++emitted;},c==4?0:1000000);
    }catch(const std::invalid_argument&){++rejected;}
    need(rejected==5 && emitted==0,"Invalid vector emitted output before refusal");
    bool capped=false;const std::vector<Scope> one={Scope(97,{3,0},{912673,0})};
    try{visit_many(one,{127,7},[](size_t,auto){},1);}catch(const std::invalid_argument&){capped=true;}
    need(capped,"Multi-scope row budget silently truncated");
    const std::vector<Scope> split={Scope(97,{16129,0},{16130,0}),Scope(97,{17907,0},{17908,0})};
    const auto uncapped=visit_many(split,{127,7},[](size_t,auto){},2);
    need(uncapped.odd_progression_events==2,"Split row event fixture changed");
    capped=false;try{visit_many(split,{127,7},[](size_t,auto){},1);}catch(const std::invalid_argument&){capped=true;}
    need(capped,"Group budget was incorrectly reset for the next scope");
    const std::vector<Scope> mixed={Scope(23,{3,0},{12167,0}),Scope(97,{20000,0},{912673,0})};
    std::vector<U128> scalar_values,multi_values;
    const auto scalar=visit_row(mixed[0],{53,52},[&](auto w){scalar_values.push_back(value(w.n));});
    const auto shared=visit_many(mixed,{53,52},[&](size_t i,auto w){need(i==0,"Row below scope cutoff was emitted");multi_values.push_back(value(w.n));});
    need(scalar_values==multi_values && scalar.odd_progression_events==shared.odd_progression_events,"Mixed-cutoff union differs");
}
int main()try{
    const auto began=std::chrono::steady_clock::now();
    const auto prime=independent_sieve(1000000);
    multi_scope_controls();
    std::cout<<"{\"cases\":[";bool first=true;uint64_t total=0;
    for(const uint32_t a:{5,11,23,47,97}){
        const uint64_t hi=uint64_t(a)*a*a;
        std::vector<uint64_t> want;
        for(uint64_t n=3;n<hi;n+=2)if(!prime[n]){
            bool residual=true;for(uint32_t p=2;p<=a;++p)if(prime[p] && n%p==0){residual=false;break;}
            if(residual && ej(n))want.push_back(n);
        }
        const auto got=native_events(a,3,hi,prime);
        need(got==want,"Native tail differs from exhaustive integer oracle");
        for(const auto n:want){
            need(native_events(a,n,n+1,prime)==std::vector<uint64_t>{n},"Native lower inclusive edge lost");
            need(native_events(a,n-1,n,prime).empty(),"Native upper exclusive edge leaked");
        }
        total+=got.size();if(!first)std::cout<<',';first=false;
        std::cout<<"{\"cutoff\":"<<a<<",\"hi_exclusive\":"<<hi<<",\"values\":[";
        for(size_t i=0;i<got.size();++i){if(i)std::cout<<',';std::cout<<got[i];}std::cout<<"]}";
    }
    const uint64_t p=2142738313ULL,q=74995840921ULL;
    const U128 n=U128(p)*q;
    const Scope known(10000000,number(n-410),number(n+547));
    std::vector<CompositeWitness> found;
    const auto s=stream(p,p+1,1,[&](PrimeOrder row){
        need(row.order==1071369156,"Known factor order changed");
        visit_row(known,row,[&](auto w){found.push_back(w);});
    });
    need(s.rows==1 && found.size()==1 && value(found[0].n)==n && found[0].cofactor==q,"Known68bit row missed");
    need(reference_power(p-1,q)==1,"Known68bit short cofactor condition false");
    // Test overflow-safe wide divisions against an independent128bit oracle.
    for(uint64_t high=0;high<55;++high)for(uint64_t divisor:{10000019ULL,2142738313ULL,17179869143ULL}){
        const Number a{~uint64_t(0)-high,high};uint64_t rem;
        need(quotient(a,divisor,rem)==value(a)/divisor && rem==value(a)%divisor,"Wide quotient mismatch");
    }
    unsigned rejected=0;
    for(unsigned i=0;i<6;++i)try{
        if(i==0)Scope(2,{3,0},{562,0});
        if(i==1)Scope(47,{3,0},{103824,0});
        if(i==2)Scope(10000001,{3,0},{100,0});
        if(i==3)Scope(47,{3,0},{3,0});
        if(i==4)visit_row(known,{p,0},[](auto){});
        if(i==5)visit_row(known,{p,1071369156},[](auto){},0);
    }catch(const std::invalid_argument&){++rejected;}
    need(rejected==6,"Unsafe tail scope not rejected");
    // A work cap aborts instead of dropping events or reporting completion.
    bool cap=false;try{visit_row(Scope(97,{3,0},{912673,0}),{127,7},[](auto){},1);}catch(const std::invalid_argument&){cap=true;}
    need(cap,"Tail budget silently truncated");
    std::cout<<"],\"PASS\":true,\"checks\":"<<checks<<",\"exhaustive_matches\":"<<total
      <<",\"known68_row_recovered\":true,\"scope_refusals\":"<<rejected
      <<",\"work_budget_fail_closed\":true,\"multi_scope_vectors\":10,\"multi_scope_refusals\":5,\"GPU_used\":false,\"full68_enumeration\":false"
      <<",\"seconds\":"<<std::chrono::duration<double>(std::chrono::steady_clock::now()-began).count()<<"}\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
