#include "engine/cuda/euler68_core.cuh"
#include "euler68_device_vectors.h"
#include <fstream>
#include <iostream>
using pgn_euler96::U96;
namespace m=pgn_euler68_core;
static U96 native(pgn68_vectors::Word96 v){return {{v.w[0],v.w[1],v.w[2]}};}
static bool equal(U96 actual,pgn68_vectors::Word96 expected){return pgn_euler96::equal(actual,native(expected));}
int main(int argc,char** argv){
  if(argc!=2)return 1;std::ifstream f(argv[1],std::ios::binary);pgn68_vectors::Header h{};f.read(reinterpret_cast<char*>(&h),sizeof(h));
  if(!f||h.magic!=pgn68_vectors::Magic||h.count!=16384||h.record_bytes!=sizeof(pgn68_vectors::Record)||h.reserved)return 2;
  for(unsigned i=0;i<h.count;++i){pgn68_vectors::Record v{};f.read(reinterpret_cast<char*>(&v),sizeof(v));if(!f)return 2;
    U96 product{},power{},residue{};bool match=false;
    if(m::multiply(native(v.a),native(v.b),native(v.n),v.inverse,product)!=m::Status::ok||!equal(product,v.product))return 3;
    if(m::powerOfTwo(native(v.exponent),native(v.n),power)!=m::Status::ok||!equal(power,v.power))return 4;
    if(m::euler(uint64_t(v.n.w[0])|(uint64_t(v.n.w[1])<<32),v.n.w[2],match,&residue)!=m::Status::ok||match!=bool(v.euler_match)||!equal(residue,v.euler_residue))return 5;
    if(!pgn_euler96::equal(m::admittedProduct(native(v.a),native(v.b),native(v.n),v.inverse),product))return 6;
  }
  char extra;if(f.read(&extra,1))return 2;
  const U96 sentinel{{0xdeadbeef,0xabcdef01,0x12345678}},good{{3,0,10}},one{{1,0,0}};U96 out=sentinel;bool match=true;const auto inv=pgn_euler96::inverse(good.w[0]);
  if(m::multiply(one,one,U96{{3,0,11}},inv,out)!=m::Status::unsupported_modulus||!pgn_euler96::equal(out,sentinel))return 7;
  if(m::multiply(good,one,good,inv,out)!=m::Status::unnormalized_operand||!pgn_euler96::equal(out,sentinel))return 7;
  if(m::multiply(one,one,good,inv^1U,out)!=m::Status::invalid_inverse||!pgn_euler96::equal(out,sentinel))return 7;
  if(m::euler(2,10,match,&out)!=m::Status::unsupported_modulus||!match||!pgn_euler96::equal(out,sentinel))return 7;
  std::cout<<"CORE_HOST_GMP_VECTORS records="<<h.count<<" product_power_euler_errors=0 admitted_product_errors=0 poisons=4\n";return 0;
}
