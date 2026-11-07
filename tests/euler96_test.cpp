#ifdef PGN_EULER68_TEST
#include "engine/cuda/euler68.cuh"
namespace arithmetic = pgn_euler68;
constexpr int radixBits=68;
#else
#include "engine/prime_gap_native/euler96.cuh"
namespace arithmetic = pgn_euler96;
constexpr int radixBits=96;
#endif
#include <gmp.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <random>
#include <vector>

// Fixture-only native layout: all fields are explicit64-bit words on both
// checked Windows hosts. This is not a network or consensus proof format.
struct Vector { uint64_t low,high,expected; };
static pgn_euler96::U96 words(mpz_srcptr value){
 pgn_euler96::U96 result{};size_t count=0;mpz_export(result.w,&count,-1,4,0,0,value);return result;
}
static void integer(mpz_ptr result,const pgn_euler96::U96& words){mpz_import(result,3,-1,4,0,0,words.w);}
int main(int argc,char** argv){
 mpz_t n,r,e,actual,expected,a,b,ab,rinv,two;mpz_inits(n,r,e,actual,expected,a,b,ab,rinv,two,nullptr);
 mpz_set_ui(r,1);mpz_mul_2exp(r,r,radixBits);mpz_set_ui(two,2);
 std::mt19937_64 random(0x96e17e3);std::vector<Vector> vectors;
 size_t cases=0,positive=0;
#ifdef PGN_EULER96_WINDOW4
 size_t powerChecks=0,hintCorrectionChecks=0;
#endif
#ifdef PGN_EULER96_BATCH_Q
 size_t hintedChecks=0;std::array<uint64_t,3> hints{};
#endif
 auto check=[&](uint64_t low,uint64_t high,bool makePrime){
  const uint64_t input[2]={low|1,high};mpz_import(n,2,-1,8,0,0,input);
  if(makePrime)mpz_nextprime(n,n);if(mpz_sizeinbase(n,2)>radixBits)return true;
  uint64_t normalized[2]={0,0};size_t used=0;mpz_export(normalized,&used,-1,8,0,0,n);
  const auto modulus=words(n),one=arithmetic::montgomeryOne(modulus);
  integer(actual,one);mpz_mod(expected,r,n);
  if(mpz_cmp(actual,expected)!=0){std::cerr<<"Rmod mismatch atcase "<<cases<<'\n';return false;}
#ifdef PGN_EULER96_BATCH_Q
  const std::array<uint64_t,3> widths{1,65ULL*(1ULL<<22),97ULL*(1ULL<<22)};
  for(size_t j=0;j<widths.size();++j){
   const uint64_t span=30ULL*widths[j]*8ULL-1,baseLow=normalized[0]-std::min(normalized[0],span);
   hints[j]=pgn_euler96::batchQuotient(baseLow,normalized[1],widths[j]);
   const uint64_t base[2]={baseLow,normalized[1]};mpz_import(b,2,-1,8,0,0,base);mpz_fdiv_q(expected,r,b);
   uint64_t exact=0;size_t count=0;mpz_export(&exact,&count,-1,8,0,0,expected);
   if(hints[j]==0||hints[j]!=exact){std::cerr<<"Batch quotient mismatch atcase "<<cases<<'\n';return false;}
   integer(actual,pgn_euler96::montgomeryOneHinted(modulus,hints[j]));mpz_mod(expected,r,n);
   if(mpz_cmp(actual,expected)!=0){std::cerr<<"Hinted remainder mismatch atcase "<<cases<<'\n';return false;}
   ++hintedChecks;
  }
#endif
  if(uint32_t(modulus.w[0]*pgn_euler96::inverse(modulus.w[0]))!=UINT32_MAX)return false;
  for(int j=0;j<3;++j){
   const pgn_euler96::U96 candidateA{{uint32_t(random()),uint32_t(random()),uint32_t(random())}},candidateB{{uint32_t(random()),uint32_t(random()),uint32_t(random())}};
   integer(a,candidateA);mpz_mod(a,a,n);integer(b,candidateB);mpz_mod(b,b,n);
#ifdef PGN_EULER96_SQUARE
   // Force the worst a=n-1 carry path and zero for every modulus; keep the
   // third square randomized. These checks do not generate search work.
   if(j==0)mpz_sub_ui(a,n,1);else if(j==1)mpz_set_ui(a,0);
#endif
   integer(actual,arithmetic::multiply(words(a),words(b),modulus,pgn_euler96::inverse(modulus.w[0])));
   mpz_invert(rinv,r,n);mpz_mul(ab,a,b);mpz_mul(ab,ab,rinv);mpz_mod(expected,ab,n);
   if(mpz_cmp(actual,expected)!=0){std::cerr<<"Montgomery product mismatch atcase "<<cases<<'\n';return false;}
#ifdef PGN_EULER96_SQUARE
   integer(actual,pgn_euler96::square(words(a),modulus,pgn_euler96::inverse(modulus.w[0])));
   mpz_mul(ab,a,a);mpz_mul(ab,ab,rinv);mpz_mod(expected,ab,n);
   if(mpz_cmp(actual,expected)!=0){std::cerr<<"Montgomery square mismatch atcase "<<cases<<'\n';return false;}
#endif
   integer(actual,pgn_euler96::twice(words(a),modulus));mpz_mul_2exp(expected,a,1);mpz_mod(expected,expected,n);
   if(mpz_cmp(actual,expected)!=0){std::cerr<<"Double mismatch atcase "<<cases<<'\n';return false;}
#ifdef PGN_EULER96_WINDOW4
   if(j==0)mpz_sub_ui(a,n,1);else if(j==1)mpz_set_ui(a,0);
   for(unsigned shift=0;shift<=16;++shift){
    integer(actual,pgn_euler96::timesPowerOfTwo(words(a),modulus,shift,pgn_euler96::reciprocalHint(modulus)));
    mpz_mul_2exp(ab,a,shift);mpz_mod(expected,ab,n);
    if(mpz_cmp(actual,expected)!=0){std::cerr<<"Power-of-two mismatch atcase "<<cases<<" shift"<<shift<<'\n';return false;}
    ++powerChecks;
    if(modulus.w[2]==10&&shift<=15){
     mpz_fdiv_q(ab,ab,n);const auto q=uint32_t(mpz_get_ui(ab));
     for(const auto hint:std::array<uint32_t,3>{q,q? q-1:0,q+1}){
      integer(actual,pgn_euler96::powerOfTwoRemainderHint(words(a),modulus,shift,hint));
      if(mpz_cmp(actual,expected)!=0){std::cerr<<"Signed quotient correction mismatch atcase "<<cases<<'\n';return false;}
      ++hintCorrectionChecks;
     }
    }
   }
#endif
  }
  mpz_sub_ui(e,n,1);mpz_fdiv_q_2exp(e,e,1);mpz_powm(expected,two,e,n);
  const auto sign=mpz_fdiv_ui(n,8);mpz_set_ui(actual,1);if(sign==3||sign==5)mpz_sub_ui(actual,n,1);
  const bool verdict=mpz_cmp(actual,expected)==0;
  if(arithmetic::test(normalized[0],normalized[1])!=verdict){std::cerr<<"Euler/Jacobi mismatch atcase "<<cases<<'\n';return false;}
#ifdef PGN_EULER96_BATCH_Q
  for(const auto hint:hints)if(arithmetic::test(normalized[0],normalized[1],hint)!=verdict){std::cerr<<"Hinted Euler mismatch atcase "<<cases<<'\n';return false;}
#endif
  ++cases;if(verdict)++positive;
  if(vectors.size()<16384)vectors.push_back({normalized[0],normalized[1],verdict?1ULL:0ULL});return true;
 };
#ifdef PGN_EULER68_TEST
 const std::array<uint64_t,9> highs{8,9,10,11,12,13,14,15,15};
#else
 const std::array<uint64_t,9> highs{1,2,7,10,15,16,0x7fffffff,0xfffffffe,0xffffffff};
#endif
 const std::array<uint64_t,8> lows{1,3,5,0xfffffffd,0xffffffff,0x100000001,UINT64_MAX-2,UINT64_MAX};
 for(auto high:highs)for(auto low:lows)if(!check(low,high,false))return 1;
 for(int i=0;i<12000;++i){const uint64_t high=i%2?10:radixBits==68?8+(random()%8):1+(random()%UINT32_MAX);if(!check(random(),high,i%3==0))return 2;}
 if(positive<1000||arithmetic::test(3,0)||arithmetic::test(2,1)||arithmetic::test(3,1ULL<<32))return 3;
#ifdef PGN_EULER96_BATCH_Q
 constexpr uint64_t MaxWords=97ULL*(1ULL<<22),span=30ULL*MaxWords*8ULL-1;
 if(pgn_euler96::batchQuotient(0,0,1)||pgn_euler96::batchQuotient(0,1ULL<<32,1)||
    pgn_euler96::batchQuotient(0,1,0)||pgn_euler96::batchQuotient(0,1,MaxWords+1)||
    pgn_euler96::batchQuotient(UINT64_MAX-span+1,1,MaxWords)||
    !pgn_euler96::batchQuotient(UINT64_MAX-span,1,MaxWords))return 7;
#endif
 if(argc==2){
  if(std::filesystem::exists(argv[1]))return 4;
  FILE* output=std::fopen(argv[1],"wb");if(!output)return 5;
  const uint32_t header[2]={0x454a3936U,uint32_t(vectors.size())};
  const bool okay=std::fwrite(header,sizeof(header),1,output)==1&&std::fwrite(vectors.data(),sizeof(Vector),vectors.size(),output)==vectors.size();
  if(std::fclose(output)!=0||!okay)return 6;
 }
 std::cout<<"PASS exact GMP oracle: "<<cases<<" odd moduli / Rbits="<<radixBits<<", "<<positive<<" positive Euler verdicts; Rmod/inverse/products/doubling, domain guards; no prime-proof claim\n";
#ifdef PGN_EULER96_SQUARE
 std::cout<<"PASS dedicated_square_checks="<<3*cases<<" including every n-1/zero/random, exact GMP REDC parity\n";
#endif
#ifdef PGN_EULER96_BATCH_Q
 std::cout<<"PASS hinted_remainder_and_verdict_checks="<<hintedChecks<<" widths1/65regions/97regions; exact base quotient, worst-span/carry-boundary, zero-hint fallback and guard rejection\n";
#endif
#ifdef PGN_EULER96_WINDOW4
 std::cout<<"PASS power_of_two_checks="<<powerChecks<<" signed_quotient_correction_checks="<<hintCorrectionChecks<<" n-1/zero/random, shifts0..16, high10 fast domain/other-high fallback; exact GMP Euler verdict parity\n";
#endif
 mpz_clears(n,r,e,actual,expected,a,b,ab,rinv,two,nullptr);
}
