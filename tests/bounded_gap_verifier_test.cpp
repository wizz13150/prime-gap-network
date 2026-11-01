#include "verification/bounded_gap_verifier.h"
#include <gmp.h>
#include <iostream>
using namespace pgn::verification;
int main(){
    for(const auto* bad:{"","-1","+7"," 7","07","7x","1e20"})
        if(verifyPrime(bad).verdict!=PrimeVerdict::InvalidInput)return 1;
    if(verifyPrime(PrimeBound).verdict!=PrimeVerdict::Unsupported||
       verifyPrime("318665857834031151167463").verdict!=PrimeVerdict::Unsupported||
       verifyGap("37",PrimeBound,100).verdict!=GapVerdict::Unsupported)return 2;
    // psi11 is composite but fools the first eleven prime bases: do not use
    // eleven bases or confuse Euler/Jacobi/base2 with this bounded predicate.
    const auto liar=verifyPrime("3825123056546413051");
    if(liar.verdict!=PrimeVerdict::Composite||liar.witnessBase!=37)return 3;
    for(unsigned n=0;n<10000;++n){
        bool prime=n>=2;for(unsigned d=2;d*d<=n;++d)if(n%d==0){prime=false;break;}
        if((verifyPrime(std::to_string(n)).verdict==PrimeVerdict::PrimeWithinBound)!=prime)return 4;
    }
    if(verifyGap("2","3",0).verdict!=GapVerdict::VerifiedWithinBound||
       verifyGap("3","5",0).verdict!=GapVerdict::VerifiedWithinBound||
       verifyGap("11","17",10).verdict!=GapVerdict::InteriorPrime||
       verifyGap("9","11",10).verdict!=GapVerdict::CompositeEndpoint||
       verifyGap("11","7",10).verdict!=GapVerdict::InvalidInput)return 5;
    if(verifyGap("11","17",0).verdict!=GapVerdict::BudgetExceeded)return 6;
    std::atomic_bool cancelled{true};
    if(verifyGap("11","17",100,&cancelled).verdict!=GapVerdict::Interrupted)return 7;
    mpz_t p,q;mpz_inits(p,q,nullptr);mpz_set_str(p,"199999990000000000000",10);
    for(int i=0;i<64;++i){
        mpz_nextprime(p,p);mpz_nextprime(q,p);char low[32]{},high[32]{};mpz_get_str(low,10,p);mpz_get_str(high,10,q);
        if(verifyGap(low,high,10000).verdict!=GapVerdict::VerifiedWithinBound)return 8;
        mpz_set(p,q);
    }
    mpz_clears(p,q,nullptr);
    std::cout<<"PASS MR12 strict published bound, psi11 witness37,10000trial-division cases,6468 bitsadjacent endpoints, exactinterior/budget/cancellation guards; supplied-gap verification only/no miner or networkcredit\n";
}
