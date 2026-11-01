#include "engine/cuda/euler68_fusedbit.cuh"
#include "euler68_device_vectors.h"
#include <gmp.h>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
#include <vector>
using pgn_euler96::U96;
namespace f=pgn_euler68_fusedbit;
static uint64_t moduli=0,squares=0,traces=0,powers=0,eulers=0,aliases=0,poisons=0,thresholds=0,fixtureRecords=0;
static uint64_t buckets[2][4]{};
static uint32_t lowClasses=0,lastDigits=0;
static void require(bool b,const char* why){if(!b){std::cerr<<"FAIL "<<why<<" modulus="<<moduli<<" square="<<squares<<"\n";std::exit(1);}}
using pgn_euler96::equal;
static void integer(mpz_ptr out,const U96& a){mpz_import(out,3,-1,4,0,0,a.w);}
static U96 words(mpz_srcptr a){
  require(mpz_sgn(a)>=0&&mpz_sizeinbase(a,2)<=96,"oracle word range");
  U96 out{};size_t size=0;mpz_export(out.w,&size,-1,4,0,0,a);return out;
}
static U96 native(pgn68_vectors::Word96 a){return {{a.w[0],a.w[1],a.w[2]}};}
struct Oracle {
  mpz_t n,r,ri,beta,ni,t,u,m,a,v,expected,actual,exponent,two,q,bound;
  U96 nw{};uint32_t inverse=0;
  Oracle(){
    mpz_inits(n,r,ri,beta,ni,t,u,m,a,v,expected,actual,exponent,two,q,bound,nullptr);
    mpz_set_ui(r,1);mpz_mul_2exp(r,r,68);mpz_set_ui(beta,1);mpz_mul_2exp(beta,beta,32);mpz_set_ui(two,2);
  }
  ~Oracle(){mpz_clears(n,r,ri,beta,ni,t,u,m,a,v,expected,actual,exponent,two,q,bound,nullptr);}
  Oracle(const Oracle&)=delete;Oracle& operator=(const Oracle&)=delete;
  void modulus(U96 input){
    nw=input;integer(n,nw);inverse=pgn_euler96::inverse(nw.w[0]);
    require(mpz_invert(ri,r,n)!=0&&mpz_invert(ni,n,beta)!=0,"GMP independent inverses");
    require(inverse==uint32_t(0U-uint32_t(mpz_get_ui(ni))),"independent inverse matches admission");
    U96 one{};require(f::montgomeryOne(nw,one)==f::Status::ok,"Montgomery one status");
    integer(actual,one);mpz_mod(expected,r,n);require(mpz_cmp(actual,expected)==0,"R-one conversion GMP");
    ++moduli;lowClasses|=1U<<(nw.w[0]&15U);
  }
  U96 normalized(U96 x){integer(v,x);mpz_mod(v,v,n);return words(v);}
  // Independent arbitrary precision CIOS derivation; no incumbent product
  // is the oracle. The final exact S checks cancellation and lost carries.
  void numerator(U96 input){
    integer(a,input);mpz_set_ui(t,0);
    for(int digit=0;digit<2;++digit){
      mpz_mul_ui(u,a,input.w[digit]);mpz_add(t,t,u);
      mpz_mul(m,t,ni);mpz_neg(m,m);mpz_fdiv_r_2exp(m,m,32);
      mpz_mul(u,m,n);mpz_add(t,t,u);
      require(mpz_divisible_2exp_p(t,32)!=0,"full row cancellation");
      mpz_fdiv_q_2exp(t,t,32);mpz_mul_ui(bound,n,2);
      require(mpz_cmp(t,bound)<0,"full row T<2n");
    }
    mpz_mul_ui(u,a,input.w[2]);mpz_add(t,t,u);
    mpz_mul(m,t,ni);mpz_neg(m,m);mpz_fdiv_r_2exp(m,m,4);
    lastDigits|=1U<<unsigned(mpz_get_ui(m));mpz_mul(u,m,n);mpz_add(t,t,u);
    mpz_mul_ui(bound,n,27);
    require(mpz_cmp(t,bound)<0&&mpz_sizeinbase(t,2)<=73&&mpz_divisible_2exp_p(t,4),"last row bounds and exact division");
    integer(actual,f::admittedSquareNumerator(input,nw,inverse));
    require(mpz_cmp(actual,t)==0,"GMP exact last numerator, no dropped carry");++traces;
  }
  void square(U96 input){
    numerator(input);
    for(uint32_t bit=0;bit<=1;++bit){
      // Semantic oracle is directly a^2 *2^bit *R^-1 mod n,
      // independently of the CIOS trace above and the old core.
      mpz_mul(expected,a,a);mpz_mul(expected,expected,ri);
      mpz_mul_2exp(expected,expected,bit);mpz_mod(expected,expected,n);
      U96 out{};require(f::squareBit(input,nw,inverse,bit,out)==f::Status::ok,"checked square status");
      integer(actual,out);require(mpz_cmp(actual,expected)==0&&mpz_cmp(actual,n)<0,"square GMP canonical relation");
      const U96 admitted=bit?f::admittedSquareBit<true>(input,nw,inverse):f::admittedSquareBit<false>(input,nw,inverse);
      require(equal(out,admitted),"constant shift and checked mapping");
      require(equal(out,f::admittedSquareBit(input,nw,inverse,bit!=0)),"runtime mapping");
      mpz_fdiv_q_2exp(q,t,bit?3:4);mpz_fdiv_q(u,q,n);
      const unsigned bucket=unsigned(mpz_get_ui(u));require(bucket<(bit?4U:2U),"unnormalized quotient bound");++buckets[bit][bucket];
      U96 incumbent=f::admittedProduct(input,input,nw,inverse);
      if(bit)incumbent=pgn_euler96::twice(incumbent,nw);
      require(equal(out,incumbent),"additional incumbent comparison");
      U96 alias=input;require(f::squareBit(alias,nw,inverse,bit,alias)==f::Status::ok&&equal(out,alias),"operand output alias");++aliases;
      alias=nw;require(f::squareBit(input,alias,inverse,bit,alias)==f::Status::ok&&equal(out,alias),"modulus output alias");++aliases;++squares;
    }
  }
  void power(U96 e){
    integer(exponent,e);mpz_powm(expected,two,exponent,n);mpz_mul(expected,expected,r);mpz_mod(expected,expected,n);
    U96 out{};require(f::powerOfTwo(e,nw,out)==f::Status::ok,"power status");integer(actual,out);
    require(mpz_cmp(actual,expected)==0&&mpz_cmp(actual,n)<0,"power GMP with R conversion");
    U96 alias=e;require(f::powerOfTwo(alias,nw,alias)==f::Status::ok&&equal(alias,out),"exponent output alias");++aliases;
    alias=nw;require(f::powerOfTwo(e,alias,alias)==f::Status::ok&&equal(alias,out),"power modulus alias");++aliases;++powers;
  }
  void euler(){
    mpz_sub_ui(exponent,n,1);mpz_fdiv_q_2exp(exponent,exponent,1);mpz_powm(expected,two,exponent,n);
    const int sign=mpz_jacobi(two,n);require(sign==1||sign==-1,"odd modulus Jacobi domain");
    if(sign==1)mpz_set_ui(v,1);else mpz_sub_ui(v,n,1);
    const bool wanted=mpz_cmp(expected,v)==0;
    mpz_mul(expected,expected,r);mpz_mod(expected,expected,n);
    bool match=!wanted;U96 out{};
    const uint64_t low=uint64_t(nw.w[0])|(uint64_t(nw.w[1])<<32);
    require(f::euler(low,10,match,&out)==f::Status::ok&&match==wanted,"Euler Jacobi decision GMP");
    integer(actual,out);require(mpz_cmp(actual,expected)==0,"Euler residue GMP");
    match=!wanted;require(f::euler(low,10,match)==f::Status::ok&&match==wanted,"optional residue omitted");++eulers;
  }
  void normalizationThresholds(){
    // Synthetic q<4n tests supplement (do not impersonate) actual reachable
    // square quotient buckets recorded separately. Equality is significant.
    for(unsigned k=0;k<4;++k)for(int offset=-1;offset<=1;++offset){
      mpz_mul_ui(q,n,k);
      if(offset<0){if(k==0)continue;mpz_sub_ui(q,q,1);}else if(offset>0)mpz_add_ui(q,q,1);
      mpz_mod(expected,q,n);const U96 normalized=f::admittedNormalizeFourN(words(q),nw);
      integer(actual,normalized);require(mpz_cmp(actual,expected)==0,"0/1/2/3n normalization threshold");++thresholds;
    }
    mpz_mul_ui(q,n,4);mpz_sub_ui(q,q,1);mpz_mod(expected,q,n);
    integer(actual,f::admittedNormalizeFourN(words(q),nw));require(mpz_cmp(actual,expected)==0,"4n-1 normalization bound");++thresholds;
  }
};
static void poisonTests(){
  const U96 sentinel{{0xdeadbeefU,0xabcdef01U,0x12345678U}},good{{3,0,10}},one{{1,0,0}};
  const uint32_t inv=pgn_euler96::inverse(good.w[0]);
  auto check=[&](U96 a,U96 n,uint32_t inverse,uint32_t bit,f::Status wanted){
    U96 out=sentinel;require(f::squareBit(a,n,inverse,bit,out)==wanted&&equal(out,sentinel),"poison square unchanged");++poisons;
    out=a;const U96 before=out;require(f::squareBit(out,n,inverse,bit,out)==wanted&&equal(out,before),"poison alias unchanged");++poisons;
    out=n;require(f::squareBit(a,out,inverse,bit,out)==wanted&&equal(out,n),"poison modulus alias unchanged");++poisons;
  };
  for(U96 bad:std::array<U96,5>{{{{3,0,9}},{{3,0,11}},{{2,0,10}},{{0,0,0}},{{UINT32_MAX,UINT32_MAX,UINT32_MAX}}}}){
    check(one,bad,inv,0,f::Status::unsupported_modulus);
    U96 out=sentinel;require(f::powerOfTwo(one,bad,out)==f::Status::unsupported_modulus&&equal(out,sentinel),"poison power unchanged");++poisons;
  }
  check(good,good,inv,0,f::Status::unnormalized_operand);
  check(U96{{UINT32_MAX,UINT32_MAX,UINT32_MAX}},good,inv,1,f::Status::unnormalized_operand);
  for(uint32_t wrong:std::array<uint32_t,4>{{0,inv^1U,inv^16U,inv^0x80000000U}})check(one,good,wrong,0,f::Status::invalid_inverse);
  for(uint32_t bit:std::array<uint32_t,3>{{2,16,UINT32_MAX}})check(one,good,inv,bit,f::Status::invariant_violation);
  for(auto bad:std::array<std::array<uint64_t,2>,5>{{{{2,10}},{{3,9}},{{3,11}},{{3,10ULL+(1ULL<<32)}},{{UINT64_MAX,UINT64_MAX}}}}){
    for(bool before:std::array<bool,2>{{false,true}}){bool match=before;U96 out=sentinel;
      require(f::euler(bad[0],bad[1],match,&out)==f::Status::unsupported_modulus&&match==before&&equal(out,sentinel),"poison Euler preserves bool and residue");++poisons;
      require(f::euler(bad[0],bad[1],match)==f::Status::unsupported_modulus&&match==before,"poison omitted output");++poisons;
    }
  }
  U96 out=sentinel;
  require(f::multiply(good,one,good,inv,out)==f::Status::unnormalized_operand&&equal(out,sentinel),"preserved multiply left guard");++poisons;
  require(f::multiply(one,good,good,inv,out)==f::Status::unnormalized_operand&&equal(out,sentinel),"preserved multiply right guard");++poisons;
}
int main(int argc,char** argv){
  require(argc==2,"one pinned fixture path required");Oracle o;std::mt19937_64 rng(0x68f05edb17ULL);
  std::vector<uint64_t> lows={1,3,15,17,31,UINT32_MAX-2ULL,UINT32_MAX,1ULL<<32,(1ULL<<32)+1,
    (1ULL<<32)+3,(1ULL<<63)-1,(1ULL<<63)+1,UINT64_MAX-16,UINT64_MAX-2,UINT64_MAX};
  for(unsigned low=1;low<256;low+=2)lows.push_back(low);
  for(unsigned i=0;i<4096;++i)lows.push_back(rng()|1ULL);
  for(uint64_t low:lows){
    low|=1ULL;o.modulus(U96{{uint32_t(low),uint32_t(low>>32),10}});
    std::vector<U96> operands={U96{},{{1,0,0}},{{2,0,0}},{{UINT32_MAX,0,0}},{{0,1,0}},
      {{1,1,0}},{{UINT32_MAX,UINT32_MAX,0}},{{0,0,1}},{{1,0,1}},{{UINT32_MAX,UINT32_MAX,9}}};
    mpz_sub_ui(o.v,o.n,1);operands.push_back(words(o.v));mpz_sub_ui(o.v,o.n,2);operands.push_back(words(o.v));
    mpz_fdiv_q_2exp(o.v,o.n,1);operands.push_back(words(o.v));mpz_sub(o.v,o.r,o.n);operands.push_back(words(o.v));
    for(unsigned cls=0;cls<16;++cls)operands.push_back(o.normalized(U96{{uint32_t(cls),UINT32_MAX,10}}));
    for(unsigned i=0;i<8;++i)operands.push_back(o.normalized(U96{{uint32_t(rng()),uint32_t(rng()),uint32_t(rng()%11)}}));
    for(U96 input:operands)o.square(input);
    for(U96 e:std::array<U96,5>{{U96{},{{1,0,0}},{{0,1,0}},{{0,0,1}},{{UINT32_MAX,UINT32_MAX,UINT32_MAX}}}})o.power(e);
    o.euler();o.normalizationThresholds();
  }
  std::ifstream input(argv[1],std::ios::binary);pgn68_vectors::Header header{};
  input.read(reinterpret_cast<char*>(&header),sizeof(header));
  require(bool(input)&&header.magic==pgn68_vectors::Magic&&header.count==16384&&header.record_bytes==sizeof(pgn68_vectors::Record)&&header.reserved==0,"fixture header");
  for(unsigned i=0;i<header.count;++i){
    pgn68_vectors::Record record{};input.read(reinterpret_cast<char*>(&record),sizeof(record));
    require(bool(input)&&record.reserved==0,"fixture record");o.modulus(native(record.n));
    require(record.inverse==o.inverse,"fixture inverse");o.square(native(record.a));o.square(native(record.b));
    o.power(native(record.exponent));o.euler();
    U96 out{};bool match=false;
    require(f::multiply(native(record.a),native(record.b),o.nw,o.inverse,out)==f::Status::ok&&equal(out,native(record.product)),"preserved core product fixture");
    require(f::powerOfTwo(native(record.exponent),o.nw,out)==f::Status::ok&&equal(out,native(record.power)),"pinned independent power fixture");
    require(f::euler(uint64_t(o.nw.w[0])|(uint64_t(o.nw.w[1])<<32),10,match,&out)==f::Status::ok&&match==bool(record.euler_match)&&equal(out,native(record.euler_residue)),"pinned independent Euler fixture");++fixtureRecords;
  }
  char extra;require(!input.read(&extra,1)&&input.eof(),"fixture exact tail");poisonTests();
  require(lowClasses==0xaaaaU&&lastDigits==0xffffU,"all odd lowmod16 and final reduction digits");
  require(buckets[0][0]&&buckets[0][1]&&buckets[1][0]&&buckets[1][1]&&buckets[1][2]&&buckets[1][3],"actual square quotient reaches all normalization buckets");
  std::cout<<"FUSEDBIT_GMP_PASS moduli="<<moduli<<" squares="<<squares<<" exact_numerator_traces="<<traces<<" powers="<<powers<<" eulers="<<eulers
    <<" aliases="<<aliases<<" poisons="<<poisons<<" synthetic_thresholds="<<thresholds<<" fixture_records="<<fixtureRecords<<" lowmod16_mask="<<lowClasses<<" reduction_digit_mask="<<lastDigits<<"\n";
  std::cout<<"REACHABLE_QUOTIENT_BUCKETS bit0="<<buckets[0][0]<<","<<buckets[0][1]<<","<<buckets[0][2]<<","<<buckets[0][3]
    <<" bit1="<<buckets[1][0]<<","<<buckets[1][1]<<","<<buckets[1][2]<<","<<buckets[1][3]<<"\n";
}
