#include "euler68_device_vectors.h"
#include <gmp.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <vector>
#include <stdexcept>
using namespace pgn68_vectors;
static void check(bool ok, const char* text) { if (!ok) throw std::runtime_error(text); }
static void integer(mpz_ptr out, const Word96& value) { mpz_import(out, 3, -1, 4, 0, 0, value.w); }
static Word96 words(mpz_srcptr value) {
  check(mpz_sgn(value) >= 0 && mpz_sizeinbase(value, 2) <= 96, "Fixture range");
  Word96 out{}; size_t count = 0; mpz_export(out.w, &count, -1, 4, 0, 0, value); return out;
}
int main(int argc, char** argv) try {
  check(argc == 2, "Exactly one new fixture path required");
  check(!std::filesystem::exists(argv[1]), "Preserve existing fixture; no overwrite");
  mpz_t n, a, b, r, rinv, inverse, beta, x, y, exponent, two;
  mpz_inits(n, a, b, r, rinv, inverse, beta, x, y, exponent, two, nullptr);
  mpz_set_ui(r, 1); mpz_mul_2exp(r, r, 68);
  mpz_set_ui(beta, 1); mpz_mul_2exp(beta, beta, 32); mpz_set_ui(two, 2);
  const uint64_t boundary[] = {1,3,15,17,0xffffffffULL,0x100000001ULL,
    0x7fffffffffffffffULL,0x8000000000000001ULL,0xfffffffffffffffdULL,UINT64_MAX};
  std::mt19937_64 random(0x683204d3ULL);
  std::vector<Record> records; records.reserve(16384);
  unsigned positives = 0;
  for (unsigned i = 0; i < 16384; ++i) {
    const uint64_t low = i < 320 ? boundary[(i / 32) % 10] : (random() | 1ULL);
    Record v{}; v.n = {{uint32_t(low),uint32_t(low >> 32),10}}; integer(n,v.n);
    check(mpz_invert(rinv,r,n) != 0 && mpz_invert(inverse,n,beta) != 0, "Independent GMP inverses");
    mpz_neg(inverse,inverse); mpz_mod(inverse,inverse,beta); v.inverse = uint32_t(mpz_get_ui(inverse));
    const Word96 raw_a{{uint32_t(random()),uint32_t(random()),uint32_t(random() & 15U)}};
    const Word96 raw_b{{uint32_t(random()),uint32_t(random()),uint32_t(random() & 15U)}};
    integer(a,raw_a); mpz_mod(a,a,n); integer(b,raw_b); mpz_mod(b,b,n);
    switch (i % 8) {
      case 0: mpz_set_ui(a,0); break;
      case 1: mpz_set_ui(a,1); break;
      case 2: mpz_sub_ui(a,n,1); mpz_sub_ui(b,n,1); break;
      case 3: mpz_sub_ui(a,n,2); mpz_fdiv_q_2exp(b,n,1); break;
      case 4: mpz_sub(a,r,n); break;
    }
    v.a = words(a); v.b = words(b);
    mpz_mul(x,a,b); mpz_mul(x,x,rinv); mpz_mod(x,x,n); v.product = words(x);
    v.exponent = {{uint32_t(random()),uint32_t(random()),uint32_t(random())}};
    if (i % 16 == 0) v.exponent = {{0,0,0}};
    if (i % 16 == 1) v.exponent = {{UINT32_MAX,UINT32_MAX,UINT32_MAX}};
    integer(exponent,v.exponent); mpz_powm(x,two,exponent,n); mpz_mul(x,x,r); mpz_mod(x,x,n); v.power = words(x);
    mpz_sub_ui(exponent,n,1); mpz_fdiv_q_2exp(exponent,exponent,1); mpz_powm(x,two,exponent,n);
    const int jacobi = mpz_jacobi(two,n); check(jacobi == 1 || jacobi == -1,"Odd Jacobi");
    mpz_set_ui(y,1); if (jacobi == -1) mpz_sub_ui(y,n,1);
    v.euler_match = mpz_cmp(x,y) == 0; positives += v.euler_match;
    mpz_mul(x,x,r); mpz_mod(x,x,n); v.euler_residue = words(x); records.push_back(v);
  }
  const Header header{Magic,uint32_t(records.size()),sizeof(Record),0};
  std::ofstream file(argv[1],std::ios::binary | std::ios::out); check(bool(file),"Fixture create");
  file.write(reinterpret_cast<const char*>(&header),sizeof(header));
  file.write(reinterpret_cast<const char*>(records.data()),std::streamsize(records.size()*sizeof(Record)));
  file.close(); check(bool(file),"Fixture complete write");
  mpz_clears(n,a,b,r,rinv,inverse,beta,x,y,exponent,two,nullptr);
  std::cout << "GMP_DEVICE_VECTORS count=" << records.size() << " record_bytes=" << sizeof(Record)
    << " euler_positive=" << positives << " fixture_bytes=" << sizeof(header)+records.size()*sizeof(Record) << '\n';
  return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
