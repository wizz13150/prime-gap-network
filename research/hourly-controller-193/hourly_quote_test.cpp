#include <pgn/hourly_target_quote.h>
#include <pgn/merit_arithmetic.h>
#include <iostream>
#include <stdexcept>
static unsigned checks=0;
static void need(bool v,const char* m){++checks;if(!v)throw std::runtime_error(m);}
int main(){
 using namespace pgn::discussion;
 for(unsigned n=275;n<=640;++n){
  auto q=QuoteHourlyTarget(n,64U*65536U);need(bool(q),"legal target encodes");
  auto back=DecodeHourlyTarget(q->header_q16,64U*65536U);
  need(back&&back->target_tenths==n&&back->proof_numerator==n&&back->proof_denominator==10,"exact rational roundtrip");
 }
 need(!QuoteHourlyTarget(274,64U*65536U),"floor preserved");
 need(!QuoteHourlyTarget(641,64U*65536U),"existing protocol range explicit");
 auto q=QuoteHourlyTarget(276,64U*65536U);need(q&&q->header_q16==1808794,"27.6 header identifier");
 // Arithmetic-only witnesses adjacent to exp(1282/27.6); no primality claim.
 // Native directed log bounds verify each side, independently of the decimal
 // generator used to locate these integers.
 const pgn::core::Integer below("148820917694945778479"),above=below+1;
 const auto accuracy=pgn::core::LogAccuracy{256,256};
 const auto limits=pgn::core::ArithmeticLimits{512,512,512};
 const auto decimal=pgn::core::compareMerit(below,1282,276,10,accuracy,limits);
 const auto decimalNext=pgn::core::compareMerit(above,1282,276,10,accuracy,limits);
 const auto binary=pgn::core::compareMerit(below,1282,q->header_q16,65536,accuracy,limits);
 need(decimal==pgn::core::MeritDecision::Meets,"lower integer meets exact27.6");
 need(decimalNext==pgn::core::MeritDecision::Below,"next integer below exact27.6");
 need(binary==pgn::core::MeritDecision::Below,"rounded binary target would reject exact decimal winner");
 std::cout<<"{\"PASS\":true,\"checks\":"<<checks<<",\"native_directed_log_boundary_witness\":true,\"live_changed\":false}"<<std::endl;
}
