#include "engine/cuda/euler72_lazy_fp8.cuh"
#include <gmp.h>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
namespace f=pgn_euler72_lazy_fp8;
using pgn_euler96::U96;
static void check(bool v,const char* why){if(!v){std::cerr<<why<<'\n';std::exit(1);}}
static uint64_t read64(const unsigned char* p){uint64_t x=0;for(int i=7;i>=0;--i)x=(x<<8)|p[i];return x;}
static void integer(mpz_ptr z,U96 x){mpz_import(z,3,-1,4,0,0,x.w);}
static U96 words(mpz_srcptr z){U96 x{};size_t size=0;mpz_export(x.w,&size,-1,4,0,0,z);return x;}
int main(int argc,char** argv){
 check(argc==2,"one inherited fixture");check(std::filesystem::file_size(argv[1])==289736,"fixture byte bound");
 std::ifstream in(argv[1],std::ios::binary);std::array<unsigned char,8> h{};in.read(reinterpret_cast<char*>(h.data()),8);
 check(bool(in)&&read64(h.data())==(uint64_t(12072)<<32|0x454a3936U),"fixture header");
 mpz_t n,r,rinv,e,two,want,got,a,b;mpz_inits(n,r,rinv,e,two,want,got,a,b,nullptr);
 mpz_set_ui(r,1);mpz_mul_2exp(r,r,72);mpz_set_ui(two,2);
 uint64_t high10=0,products=0,poisons=0,positive=0;
 auto verify=[&](uint64_t lo,uint64_t hi,uint64_t fixture){
  U96 nw{{uint32_t(lo),uint32_t(lo>>32),uint32_t(hi)}};integer(n,nw);
  mpz_sub_ui(e,n,1);mpz_fdiv_q_2exp(e,e,1);mpz_powm(want,two,e,n);
  const int sign=mpz_jacobi(two,n);if(sign==1)mpz_set_ui(got,1);else mpz_sub_ui(got,n,1);
  const bool oracle=mpz_cmp(got,want)==0;check(oracle==bool(fixture),"fixture independently agrees GMP");
  check(f::test(lo,hi)==oracle,"LAZY_FP8 predicate equals GMP");positive+=oracle;
  if(hi!=10)return;
  ++high10;mpz_mod(got,r,n);check(pgn_euler96::equal(pgn_euler72_fp12::one(nw),words(got)),"R72 exact unity");
  mpz_mul(want,want,r);mpz_mod(want,want,n);integer(got,f::evaluateAdmitted(lo));check(mpz_cmp(got,want)==0,"complete R72 exponent residue");
  check(pgn_euler96::equal(f::pack(f::unpack(nw)),nw),"packing exact");check(mpz_invert(rinv,r,n)!=0,"R inverse");
  for(unsigned j=0;j<8;++j){
   if(j==0){mpz_set_ui(a,0);mpz_sub_ui(b,n,1);}else if(j==1){mpz_sub_ui(a,n,1);mpz_set(b,a);}
   else{mpz_set_ui(a,lo^uint64_t(j*0x9e3779b9U));mpz_mul_2exp(a,a,j*7);mpz_mod(a,a,n);mpz_sub_ui(b,n,j);}
   const auto out=f::multiply(f::unpack(words(a)),f::unpack(words(b)),f::unpack(nw),pgn_euler96::inverse(nw.w[0])&255U);
   mpz_mul(want,a,b);mpz_mul(want,want,rinv);mpz_mod(want,want,n);integer(got,f::pack(out));
   check(mpz_cmp(got,want)==0,"CIOS LAZY_FP8 product");++products;
   const auto sq=f::square(f::unpack(words(a)),f::unpack(nw),pgn_euler96::inverse(nw.w[0])&255U);
   mpz_mul(want,a,a);mpz_mul(want,want,rinv);mpz_mod(want,want,n);integer(got,f::pack(sq));check(mpz_cmp(got,want)==0,"lazy symmetric square");++products;
   U96 poison=f::pack(sq);poison.w[0]^=1;integer(got,poison);check(mpz_cmp(got,want)!=0,"flipped square output rejected");++poisons;
  }
 };
 for(unsigned i=0;i<12072;++i){std::array<unsigned char,24> b{};in.read(reinterpret_cast<char*>(b.data()),24);check(bool(in),"complete record");verify(read64(b.data()),read64(b.data()+8),read64(b.data()+16));}
 char extra;check(!in.get(extra)&&in.eof(),"exact tail");
 for(uint64_t lo:std::array<uint64_t,8>{{1,3,UINT64_MAX,UINT64_MAX-2,UINT32_MAX,(1ULL<<32)+1,(1ULL<<63)-1,(1ULL<<63)+1}}){
  U96 nw{{uint32_t(lo),uint32_t(lo>>32),10}};integer(n,nw);mpz_sub_ui(e,n,1);mpz_fdiv_q_2exp(e,e,1);mpz_powm(want,two,e,n);
  const int sign=mpz_jacobi(two,n);if(sign==1)mpz_set_ui(got,1);else mpz_sub_ui(got,n,1);verify(lo,10,mpz_cmp(got,want)==0);
 }
 check(!f::test(2,10),"even guard");
 mpz_clears(n,r,rinv,e,two,want,got,a,b,nullptr);
 std::cout<<"LAZY_FP8_GMP_PASS fixture=12072 high10="<<high10<<" products="<<products<<" flipped_rejected="<<poisons<<" positive="<<positive<<" errors=0\n";
}
