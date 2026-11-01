#include "verification/bounded_gap_verifier.h"
#include <gmp.h>
#include <array>
#include <algorithm>

namespace pgn::verification {
bool isCanonicalDecimal(std::string_view text){
    return !text.empty()&&text.size()<=PrimeBound.size()&&!(text.size()>1&&text.front()=='0')&&
        std::all_of(text.begin(),text.end(),[](char c){return c>='0'&&c<='9';});
}
namespace {
constexpr std::array<unsigned,12> Bases{2,3,5,7,11,13,17,19,23,29,31,37};
struct Integer {
    mpz_t value;
    Integer(){mpz_init(value);}
    ~Integer(){mpz_clear(value);}
    Integer(const Integer&)=delete;
    Integer& operator=(const Integer&)=delete;
};
bool parse(std::string_view text,mpz_ptr result){
    if(!isCanonicalDecimal(text))return false;
    return mpz_set_str(result,std::string(text).c_str(),10)==0;
}
std::string decimal(mpz_srcptr value){
    std::string result(mpz_sizeinbase(value,10)+2,'\0');
    mpz_get_str(result.data(),10,value);result.resize(result.find('\0'));return result;
}
PrimeResult prime(mpz_srcptr n){
    Integer bound;mpz_set_str(bound.value,PrimeBound.data(),10);
    if(mpz_cmp(n,bound.value)>=0)return {PrimeVerdict::Unsupported};
    if(mpz_cmp_ui(n,2)<0)return {PrimeVerdict::Composite};
    for(const auto base:Bases){
        if(mpz_cmp_ui(n,base)==0)return {PrimeVerdict::PrimeWithinBound};
        if(mpz_divisible_ui_p(n,base))return {PrimeVerdict::Composite,base,0};
    }
    Integer nm1,d,x,a;mpz_sub_ui(nm1.value,n,1);
    const auto s=mpz_scan1(nm1.value,0);mpz_fdiv_q_2exp(d.value,nm1.value,s);
    for(const auto base:Bases){
        mpz_set_ui(a.value,base);mpz_powm(x.value,a.value,d.value,n);
        if(mpz_cmp_ui(x.value,1)==0||mpz_cmp(x.value,nm1.value)==0)continue;
        bool accepted=false;
        for(mp_bitcnt_t r=1;r<s;++r){
            mpz_mul(x.value,x.value,x.value);mpz_mod(x.value,x.value,n);
            if(mpz_cmp(x.value,nm1.value)==0){accepted=true;break;}
            if(mpz_cmp_ui(x.value,1)==0)break;
        }
        if(!accepted)return {PrimeVerdict::Composite,0,base};
    }
    return {PrimeVerdict::PrimeWithinBound};
}
}
PrimeResult verifyPrime(std::string_view text){
    Integer n;if(!parse(text,n.value))return {};
    return prime(n.value);
}
GapResult verifyGap(std::string_view lower,std::string_view upper,std::uint64_t budget,const std::atomic_bool* cancelled){
    Integer p,q,n,span;
    if(!parse(lower,p.value)||!parse(upper,q.value)||mpz_cmp(p.value,q.value)>=0)return {};
    const auto interrupted=[&]{return cancelled&&cancelled->load(std::memory_order_relaxed);};
    if(interrupted())return {GapVerdict::Interrupted};
    const auto pStatus=prime(p.value).verdict,qStatus=prime(q.value).verdict;
    if(pStatus==PrimeVerdict::Unsupported||qStatus==PrimeVerdict::Unsupported)return {GapVerdict::Unsupported};
    if(pStatus!=PrimeVerdict::PrimeWithinBound||qStatus!=PrimeVerdict::PrimeWithinBound)return {GapVerdict::CompositeEndpoint};
    // Enumerate2 specially; all other even integers are composite by factor2.
    mpz_add_ui(n.value,p.value,1);if(mpz_even_p(n.value)&&mpz_cmp_ui(n.value,2)!=0)mpz_add_ui(n.value,n.value,1);
    mpz_sub(span.value,q.value,n.value);if(mpz_sgn(span.value)<=0)return {GapVerdict::VerifiedWithinBound};
    mpz_add_ui(span.value,span.value,1);mpz_fdiv_q_2exp(span.value,span.value,1);
    Integer limit;const auto budgetText=std::to_string(budget);mpz_set_str(limit.value,budgetText.c_str(),10);
    if(mpz_cmp(span.value,limit.value)>0)return {GapVerdict::BudgetExceeded};
    GapResult result;
    for(;mpz_cmp(n.value,q.value)<0;mpz_add_ui(n.value,n.value,mpz_cmp_ui(n.value,2)==0?1:2)){
        if(interrupted()){result.verdict=GapVerdict::Interrupted;return result;}
        ++result.checkedInterior;
        if(prime(n.value).verdict==PrimeVerdict::PrimeWithinBound){
            result.verdict=GapVerdict::InteriorPrime;result.firstInteriorPrime=decimal(n.value);return result;
        }
    }
    result.verdict=GapVerdict::VerifiedWithinBound;return result;
}
}
