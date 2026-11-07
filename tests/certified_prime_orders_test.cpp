#include "engine/shared_composite/semiprime_batch.h"
#include <gmp.h>
#include <iostream>
#include <type_traits>
#include <tuple>
using namespace pgn_psp_tail;
static uint64_t checks{},powers{};
static void need(bool b,const char* why){++checks;if(!b)throw std::runtime_error(why);}
static std::vector<uint32_t> flat_primes(uint32_t limit){
    std::vector<bool> composite(size_t(limit)+1);std::vector<uint32_t> out;
    for(uint32_t p=2;p<=limit;++p){if(composite[p])continue;out.push_back(p);if(uint64_t(p)*p<=limit)for(uint32_t n=p*p;n<=limit;n+=p)composite[n]=true;}
    return out;
}
struct GmpPower {
    mpz_t base,exponent,modulus,result;
    GmpPower(){mpz_inits(base,exponent,modulus,result,nullptr);mpz_set_ui(base,2);}
    ~GmpPower(){mpz_clears(base,exponent,modulus,result,nullptr);}
    bool one(uint64_t e,uint64_t m){
        // Windows unsigned long is32bits: never truncate these64bit inputs.
        mpz_import(exponent,1,1,sizeof(e),0,0,&e);mpz_import(modulus,1,1,sizeof(m),0,0,&m);
        mpz_powm(result,base,exponent,modulus);++powers;return mpz_cmp_ui(result,1)==0;
    }
};
static void window(uint64_t lo,uint64_t hi,uint32_t span,const std::vector<uint32_t>& trial,GmpPower& oracle){
    std::vector<PrimeOrder> raw,cert;
    stream(lo,hi,span,[&](PrimeOrder r){raw.push_back(r);});
    const auto stats=stream_certified(lo,hi,span,[&](CertifiedPrimeOrder r){cert.push_back(r.value());});
    need(raw.size()==cert.size()&&stats.rows==cert.size(),"Certified row count differs");
    size_t at=0;
    for(auto p=lo;p<hi;++p){bool prime=true;for(auto r:trial){if(uint64_t(r)*r>p)break;if(p%r==0){prime=false;break;}}
        if(!prime)continue;
        need(at<cert.size()&&cert[at].prime==p,"Independent prime enumeration differs");
        const auto row=cert[at];need(row.prime==raw[at].prime&&row.order==raw[at].order,"Raw/certified exact order differs");
        need(row.order&&(p-1)%row.order==0&&oracle.one(row.order,p),"GMP rejects certified period");
        uint64_t remaining=row.order;
        for(auto r:trial){if(uint64_t(r)*r>remaining)break;if(remaining%r)continue;
            need(!oracle.one(row.order/r,p),"GMP finds a smaller order");while(remaining%r==0)remaining/=r;}
        if(remaining>1)need(!oracle.one(row.order/remaining,p),"GMP finds smaller residual-factor order");
        ++at;
    }
    need(at==cert.size(),"Certified stream contains extra primes");
}
int main()try{
    static_assert(!std::is_default_constructible_v<CertifiedPrimeOrder>);
    static_assert(!std::is_constructible_v<CertifiedPrimeOrder,PrimeOrder>);
    const auto trial=flat_primes(200000);GmpPower oracle;
    window(3,100000,257,trial,oracle);
    window(2142738313ULL-4096,2142738313ULL+4096,997,trial,oracle);
    window((uint64_t(1)<<32)-8192,(uint64_t(1)<<32)+8192,509,trial,oracle);
    window((uint64_t(1)<<34)-4096,(uint64_t(1)<<34)+4096,2048,trial,oracle);
    const auto primes=flat_primes(10000000);size_t at=std::lower_bound(primes.begin(),primes.end(),80001)-primes.begin();const auto first=at;
    const auto complete=stream_certified(80001,10000001,262144,[&](CertifiedPrimeOrder cert){const auto r=cert.value();
        need(at<primes.size()&&primes[at++]==r.prime,"Full canonical prime domain differs from flat sieve");
        need(oracle.one(r.order,r.prime),"GMP rejects full canonical row");
        auto rest=r.order;for(auto q:trial){if(uint64_t(q)*q>rest)break;if(rest%q)continue;need(!oracle.one(r.order/q,r.prime),"Nonminimal canonical order");while(rest%q==0)rest/=q;}
        if(rest>1)need(!oracle.one(r.order/rest,r.prime),"Nonminimal canonical residual order");
    });
    need(at==primes.size()&&complete.rows==primes.size()-first,"Missing full canonical row");
    uint64_t rejects=0,external_emits=0;
    try{visit_row(Scope(11,{121,0},{1331,0}),PrimeOrder{31,3},[](auto){});}catch(const std::invalid_argument&){++rejects;}
    stream_certified(29,32,3,[&](CertifiedPrimeOrder row){
        try{visit_row(Scope(11,{121,0},{1331,0}),row,[](auto){},0);}catch(const std::invalid_argument&){++rejects;}
        try{visit_row(Scope(47,{121,0},{1331,0}),row,[](auto){});}catch(const std::invalid_argument&){++rejects;}
        if(row.value().prime==29){std::vector<Scope> s{Scope(23,{600,0},{1500,0}),Scope(23,{1500,0},{12000,0})};DisjointScopes batch(s);BatchWorkspace tiny(1);
            try{batch.visit(row,tiny,[&](auto){++external_emits;});}catch(const std::invalid_argument&){++rejects;}}
    });
    for(unsigned i=0;i<4;++i)try{
        if(i==0)stream_certified(2,100,10,[](auto){});
        if(i==1)stream_certified(3,MaximumPrimeEnd+1,10,[](auto){});
        if(i==2)stream_certified(3,100,0,[](auto){});
        if(i==3)stream_certified(3,100,MaximumSegmentSpan+1,[](auto){});
    }catch(const std::invalid_argument&){++rejects;}
    need(rejects==10&&external_emits==0,"Certified route lost a shape/budget/atomic-row guard");
    std::vector<Scope> many{Scope(11,{121,0},{600,0}),Scope(11,{600,0},{1331,0})};
    stream_certified(13,97,11,[&](CertifiedPrimeOrder cert){
        using Mark=std::tuple<size_t,uint64_t,uint64_t,uint64_t,uint64_t>;std::vector<Mark> raw,typed;
        const auto collect=[](auto& values){return [target=&values](size_t owner,CompositeWitness w){target->emplace_back(owner,w.n.high,w.n.low,w.factor,w.cofactor);};};
        const auto a=visit_many(many,cert.value(),collect(raw));const auto b=visit_many(many,cert,collect(typed));
        need(raw==typed&&a.odd_progression_events==b.odd_progression_events&&a.cofactor_passes==b.cofactor_passes,"Canonical multi-scope certified path differs");
    });
    std::cout<<"{\"PASS\":true,\"checks\":"<<checks<<",\"independent_GMP_powm\":"<<powers<<",\"canonical_rows\":"<<complete.rows<<",\"wide_windows\":4,\"invalid_or_budget_rejections\":"<<rejects<<",\"GPU_used\":false,\"production_changed\":false}\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
