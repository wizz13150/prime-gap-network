#include "prime_certificate_builder.h"
#include <algorithm>
#include <array>
#include <limits>
#include <new>
namespace pgn::core {
namespace {
Integer gcd(Integer a,Integer b){if(a<0)a=-a;while(b!=0){Integer next=a%b;a=b;b=std::move(next);}return a;}
PrimeBuildResult finish(const Integer& n,std::vector<PrimeFactorWitness> factors,PrimeBuildBudget budget,std::uint64_t divisions,const std::function<bool()>& stop){
    std::uint64_t candidates{};const auto fail=[&](PrimeBuildVerdict v){return PrimeBuildResult{v,{},divisions,candidates};};
    const Integer predecessor=n-1;
    for(auto& factor:factors){
        bool found=false;
        for(std::uint32_t index=0;index<budget.witness_candidates_per_factor;++index){
            if(stop())return fail(PrimeBuildVerdict::Cancelled);
            const Integer a=Integer(index)+2;if(a>=n)break;++candidates;
            const Integer fermat=boost::multiprecision::powm(a,predecessor,n);
            if(fermat!=1)return fail(PrimeBuildVerdict::Composite);
            const Integer reduced=boost::multiprecision::powm(a,Integer(predecessor/factor.prime),n);
            if(gcd(reduced-1,n)==1){factor.witness=a;found=true;break;}
        }
        if(!found)return fail(PrimeBuildVerdict::BudgetExceeded);
    }
    std::vector<PrimeCertificateNode> nodes{{n,std::move(factors)}};
    if(stop())return fail(PrimeBuildVerdict::Cancelled);
    if(verifyPrimeCertificate(n,nodes,budget.certificate)!=CertificateVerdict::VerifiedPrime)return fail(PrimeBuildVerdict::BudgetExceeded);
    if(stop())return fail(PrimeBuildVerdict::Cancelled);
    return {PrimeBuildVerdict::Built,std::move(nodes),divisions,candidates};
}
}
PrimeBuildResult buildSuppliedPrimeCertificate(const Integer& n,PrimeBuildBudget budget,const std::function<bool()>& cancelled){
    std::uint64_t divisions{},candidates{};
    const auto stop=[&]{return cancelled&&cancelled();};
    const auto fail=[&](PrimeBuildVerdict v){return PrimeBuildResult{v,{},divisions,candidates};};
    if(stop())return fail(PrimeBuildVerdict::Cancelled);
    if(n<2)return fail(PrimeBuildVerdict::InvalidInput);
    const auto& limits=budget.certificate;
    if(!limits.max_integer_bits||!limits.max_nodes||!limits.max_factors_per_node||boost::multiprecision::msb(n)>=limits.max_integer_bits)
        return fail(PrimeBuildVerdict::BudgetExceeded);
    try{
        if(n<mr12ExclusiveBound()){
            const auto leaf=deterministicPrimeBelowMr12Bound(n);
            if(stop())return fail(PrimeBuildVerdict::Cancelled);
            if(leaf!=PrimeVerdict::Prime)return fail(PrimeBuildVerdict::Composite);
            return {PrimeBuildVerdict::Built,std::vector<PrimeCertificateNode>{{n,{}}},divisions,candidates};
        }
        if(!budget.trial_divisions||!budget.witness_candidates_per_factor)return fail(PrimeBuildVerdict::BudgetExceeded);
        // Negative strong witnesses are valid beyond the deterministic prime
        // domain; passing ANY number of bases is never positive evidence.
        for(const unsigned base:std::array<unsigned,12>{2,3,5,7,11,13,17,19,23,29,31,37}){
            if(stop())return fail(PrimeBuildVerdict::Cancelled);
            if(verifyStrongCompositeWitness(n,base))return fail(PrimeBuildVerdict::Composite);
        }
        Integer remaining=n-1,factored=1;std::vector<PrimeFactorWitness> factors;
        const auto append=[&](const Integer& q,std::uint32_t exponent){
            if(factors.size()>=limits.max_factors_per_node)return false;
            factors.push_back({q,exponent,0,{}});return true;
        };
        // Increasing trial divisors have had every smaller prime removed.
        // An opportunistic MR12 residual leaf is admitted only after exact
        // primality verification; no probable residual becomes a factor.
        std::uint64_t divisor=2;
        std::optional<Integer> classified_remaining;
        PrimeVerdict remaining_verdict=PrimeVerdict::UnsupportedDomain;
        while(factored*factored<=n){
            if(stop())return fail(PrimeBuildVerdict::Cancelled);
            if(remaining<mr12ExclusiveBound()){
                // A failed trial division leaves the residual unchanged.
                // Reuse only that exact integer's classification; division
                // invalidates the key on the next iteration automatically.
                if(!classified_remaining||*classified_remaining!=remaining){
                    remaining_verdict=deterministicPrimeBelowMr12Bound(remaining);
                    classified_remaining=remaining;
                }
                if(remaining_verdict==PrimeVerdict::Prime){
                    if(!append(remaining,1))return fail(PrimeBuildVerdict::BudgetExceeded);
                    factored*=remaining;remaining=1;break;
                }
            }
            std::uint32_t exponent{};
            while(true){
                if(stop())return fail(PrimeBuildVerdict::Cancelled);
                if(divisions==budget.trial_divisions)return fail(PrimeBuildVerdict::BudgetExceeded);
                ++divisions;
                if(remaining%divisor!=0)break;
                remaining/=divisor;factored*=divisor;
                if(exponent==limits.max_integer_bits)return fail(PrimeBuildVerdict::BudgetExceeded);
                ++exponent;
            }
            if(exponent&&!append(Integer(divisor),exponent))return fail(PrimeBuildVerdict::BudgetExceeded);
            if(divisor==2)divisor=3;
            else if(divisor<=std::numeric_limits<std::uint64_t>::max()-2)divisor+=2;
            else return fail(PrimeBuildVerdict::BudgetExceeded);
        }
        std::sort(factors.begin(),factors.end(),[](const auto& a,const auto& b){return a.prime<b.prime;});
        return finish(n,std::move(factors),budget,divisions,stop);
    }catch(const std::bad_alloc&){return fail(PrimeBuildVerdict::BudgetExceeded);}
}
PrimeBuildResult buildSuppliedPrimeCertificateFromFactors(const Integer& n,std::span<const PrimeFactorPower> hints,
    PrimeBuildBudget budget,const std::function<bool()>& cancelled){
    const auto stop=[&]{return cancelled&&cancelled();};const auto fail=[](PrimeBuildVerdict v){return PrimeBuildResult{v,{},0,0};};
    if(stop())return fail(PrimeBuildVerdict::Cancelled);
    if(n<2)return fail(PrimeBuildVerdict::InvalidInput);
    const auto& limits=budget.certificate;
    if(!limits.max_integer_bits||!limits.max_nodes||!limits.max_factors_per_node||!budget.witness_candidates_per_factor||
        boost::multiprecision::msb(n)>=limits.max_integer_bits||hints.size()>limits.max_factors_per_node)return fail(PrimeBuildVerdict::BudgetExceeded);
    if(hints.empty())return fail(PrimeBuildVerdict::InvalidInput);
    // Full structural admission before modular arithmetic. Products are capped
    // by n-1 before multiplication, not after a hostile exponent allocation.
    Integer previous=1;
    for(const auto& hint:hints){
        if(hint.prime<=previous||hint.prime>=n||!hint.exponent)return fail(PrimeBuildVerdict::InvalidInput);
        if(boost::multiprecision::msb(hint.prime)>=limits.max_integer_bits||hint.exponent>limits.max_integer_bits)return fail(PrimeBuildVerdict::BudgetExceeded);
        previous=hint.prime;
    }
    try{
        Integer product=1;const Integer predecessor=n-1;std::vector<PrimeFactorWitness> factors;
        for(const auto& hint:hints){
            if(stop())return fail(PrimeBuildVerdict::Cancelled);
            if(hint.prime>=mr12ExclusiveBound())return fail(PrimeBuildVerdict::BudgetExceeded);
            if(deterministicPrimeBelowMr12Bound(hint.prime)!=PrimeVerdict::Prime)return fail(PrimeBuildVerdict::InvalidInput);
            for(std::uint32_t e=0;e<hint.exponent;++e){
                if(stop())return fail(PrimeBuildVerdict::Cancelled);
                if(product>predecessor/hint.prime)return fail(PrimeBuildVerdict::InvalidInput);
                product*=hint.prime;
            }
            factors.push_back({hint.prime,hint.exponent,0,{}});
        }
        if(predecessor%product!=0)return fail(PrimeBuildVerdict::InvalidInput);
        if(product*product<=n)return fail(PrimeBuildVerdict::BudgetExceeded);
        return finish(n,std::move(factors),budget,0,stop);
    }catch(const std::bad_alloc&){return fail(PrimeBuildVerdict::BudgetExceeded);}
}
}
