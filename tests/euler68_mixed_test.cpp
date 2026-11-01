#include "engine/cuda/euler68_mixed.cuh"
#include <gmp.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <random>
#include <vector>

namespace mixed = pgn_euler68_mixed;
using pgn_euler96::U96;

static void require(bool condition, const char* message) {
  if (!condition) { std::cerr << "FAIL " << message << '\n'; std::exit(1); }
}
static void integer(mpz_ptr output, const U96& input) { mpz_import(output, 3, -1, 4, 0, 0, input.w); }
static U96 words(mpz_srcptr input) {
  require(mpz_sgn(input) >= 0 && mpz_sizeinbase(input, 2) <= 96, "oracle input must fit96 unsigned bits");
  U96 result{}; size_t count = 0; mpz_export(result.w, &count, -1, 4, 0, 0, input); return result;
}
static U96 native(uint64_t low, uint32_t high = 10U) { return {{uint32_t(low), uint32_t(low >> 32), high}}; }

struct Counts {
  uint64_t moduli{}, products{}, trace_rows{}, powers{}, euler{}, euler_positive{}, conversions{}, doubles{}, poisons{}, aliases{};
  uint64_t subtractions[2]{};
  uint32_t last_digits{}, odd_mod16{}, left_mod16{}, right_mod16{};
  uint32_t max_multiply_carry{}, max_reduce_carry{}, max_small_product_top{}, max_numerator_top{}, max_quotient_top{};
};

class Oracle {
public:
  Counts counts;
  mpz_t modulus, radix, radix_inverse, beta, inverse_beta, left, right, actual, expected, product,
    state, numerator, digit, scratch, twice_modulus, limit73, radix_squared, exponent, two;
  U96 modulus_words{};
  uint32_t negative_inverse{};
  Oracle() {
    mpz_inits(modulus, radix, radix_inverse, beta, inverse_beta, left, right, actual, expected, product,
      state, numerator, digit, scratch, twice_modulus, limit73, radix_squared, exponent, two, nullptr);
    mpz_set_ui(radix, 1); mpz_mul_2exp(radix, radix, 68);
    mpz_set_ui(beta, 1); mpz_mul_2exp(beta, beta, 32);
    mpz_set_ui(limit73, 1); mpz_mul_2exp(limit73, limit73, 73); mpz_set_ui(two, 2);
  }
  ~Oracle() {
    mpz_clears(modulus, radix, radix_inverse, beta, inverse_beta, left, right, actual, expected, product,
      state, numerator, digit, scratch, twice_modulus, limit73, radix_squared, exponent, two, nullptr);
  }
  void setup(uint64_t low) {
    modulus_words = native(low); integer(modulus, modulus_words);
    require(mixed::supported(modulus_words), "strict high10 odd domain");
    require(mpz_invert(radix_inverse, radix, modulus) != 0, "independent GMP inverse R");
    require(mpz_invert(inverse_beta, modulus, beta) != 0, "independent GMP inverse beta");
    negative_inverse = pgn_euler96::inverse(modulus_words.w[0]);
    require(uint32_t(uint64_t(modulus_words.w[0]) * negative_inverse) == UINT32_MAX, "negative inverse low32");
    mpz_mul_2exp(twice_modulus, modulus, 1);
    mpz_mul(radix_squared, radix, radix); mpz_mod(radix_squared, radix_squared, modulus);
    U96 one{}; require(mixed::montgomeryOne(modulus_words, one) == mixed::Status::ok, "Montgomery one status");
    integer(actual, one); mpz_sub(expected, radix, modulus);
    require(mpz_cmp(actual, expected) == 0 && mpz_cmp(actual, modulus) < 0, "R-one exact normalization");
    ++counts.conversions; ++counts.moduli;
    counts.odd_mod16 |= 1U << (low & 15U);
  }
  U96 checkProduct(const U96& operand_left, const U96& operand_right, bool alias = false) {
    integer(left, operand_left); integer(right, operand_right);
    require(mpz_cmp(left, modulus) < 0 && mpz_cmp(right, modulus) < 0, "normalized oracle operands");
    mixed::MultiplyAudit trace{}; U96 result{};
    if (alias) {
      result = operand_left;
      require(mixed::multiply(result, operand_right, modulus_words, negative_inverse, result, &trace) == mixed::Status::ok, "alias multiply status");
      ++counts.aliases;
    } else require(mixed::multiply(operand_left, operand_right, modulus_words, negative_inverse, result, &trace) == mixed::Status::ok, "multiply status");
    integer(actual, result);
    mpz_mul(product, left, right); mpz_mul(product, product, radix_inverse); mpz_mod(expected, product, modulus);
    require(mpz_cmp(actual, expected) == 0 && mpz_cmp(actual, modulus) < 0, "independent GMP product R^-1");

    mpz_set_ui(state, 0);
    for (unsigned step = 0; step < 2; ++step) {
      mpz_mul_ui(numerator, left, operand_right.w[step]); mpz_add(numerator, numerator, state);
      mpz_mul(digit, numerator, inverse_beta); mpz_neg(digit, digit); mpz_mod(digit, digit, beta);
      mpz_mul(scratch, digit, modulus); mpz_add(numerator, numerator, scratch);
      require(mpz_divisible_2exp_p(numerator, 32) != 0, "full-row exact cancellation");
      mpz_fdiv_q_2exp(state, numerator, 32); integer(actual, trace.after_radix32[step]);
      require(mpz_cmp(actual, state) == 0 && mpz_cmp(state, twice_modulus) < 0, "GMP full-row state and T<2n");
      require(trace.multiply_top_carry[step] <= 11U && trace.reduce_top_carry[step] <= 11U && trace.radix32_cancellation_low[step] == 0U, "full-row carry bounds");
      counts.max_multiply_carry = std::max(counts.max_multiply_carry, trace.multiply_top_carry[step]);
      counts.max_reduce_carry = std::max(counts.max_reduce_carry, trace.reduce_top_carry[step]);
      ++counts.trace_rows;
    }
    mpz_mul_ui(numerator, left, operand_right.w[2]); mpz_add(numerator, numerator, state);
    mpz_mul(digit, numerator, inverse_beta); mpz_neg(digit, digit); mpz_fdiv_r_2exp(digit, digit, 4);
    require(mpz_get_ui(digit) == trace.last_reduction_digit, "independent last digit mod16");
    mpz_mul(scratch, digit, modulus); mpz_add(numerator, numerator, scratch);
    integer(actual, trace.last_numerator); require(mpz_cmp(actual, numerator) == 0, "exact three-word numerator");
    mpz_mul_ui(scratch, modulus, 27);
    require(mpz_cmp(numerator, scratch) < 0 && mpz_cmp(numerator, limit73) < 0 && mpz_divisible_2exp_p(numerator, 4) != 0, "last numerator <27n/<2^73 and divisible16");
    mpz_fdiv_q_2exp(state, numerator, 4); integer(actual, trace.before_normalization);
    require(mpz_cmp(actual, state) == 0 && mpz_cmp(state, twice_modulus) < 0, "last quotient<2n");
    require(trace.normalization_subtractions == uint32_t(mpz_cmp(state, modulus) >= 0), "at most one exact normalization");
    require(trace.last_multiply_top <= 131U && trace.last_numerator.w[2] <= 296U && trace.before_normalization.w[2] <= 18U, "small-row32-bit carry and shift bounds");
    counts.max_small_product_top = std::max(counts.max_small_product_top, trace.last_multiply_top);
    counts.max_numerator_top = std::max(counts.max_numerator_top, trace.last_numerator.w[2]);
    counts.max_quotient_top = std::max(counts.max_quotient_top, trace.before_normalization.w[2]);
    counts.last_digits |= 1U << trace.last_reduction_digit;
    counts.left_mod16 |= 1U << (operand_left.w[0] & 15U); counts.right_mod16 |= 1U << (operand_right.w[0] & 15U);
    ++counts.subtractions[trace.normalization_subtractions]; ++counts.products;
    return result;
  }
  void checkDouble(const U96& input) {
    U96 result{}; require(mixed::twice(input, modulus_words, result) == mixed::Status::ok, "doubling status");
    integer(left, input); mpz_mul_2exp(expected, left, 1); mpz_mod(expected, expected, modulus); integer(actual, result);
    require(mpz_cmp(actual, expected) == 0, "GMP doubling"); ++counts.doubles;
  }
  void checkConversions(const U96& plain) {
    const U96 encoded = checkProduct(plain, words(radix_squared));
    integer(left, plain); mpz_mul(expected, left, radix); mpz_mod(expected, expected, modulus); integer(actual, encoded);
    require(mpz_cmp(actual, expected) == 0, "GMP encoded residue");
    const U96 decoded = checkProduct(encoded, U96{{1U, 0U, 0U}});
    require(pgn_euler96::equal(decoded, plain), "encode/decode roundtrip");
    U96 one{}; mixed::montgomeryOne(modulus_words, one);
    require(pgn_euler96::equal(checkProduct(encoded, one), encoded), "Montgomery-one identity");
    counts.conversions += 3;
  }
  void checkPower(const U96& input_exponent) {
    U96 result{}; require(mixed::powerOfTwo(input_exponent, modulus_words, result) == mixed::Status::ok, "binary ladder status");
    integer(exponent, input_exponent); mpz_powm(expected, two, exponent, modulus);
    mpz_mul(expected, expected, radix); mpz_mod(expected, expected, modulus); integer(actual, result);
    require(mpz_cmp(actual, expected) == 0, "GMP binary ladder in R68 domain"); ++counts.powers;
  }
  void checkEuler() {
    const uint64_t low = uint64_t(modulus_words.w[0]) | (uint64_t(modulus_words.w[1]) << 32);
    bool matches = false; U96 result{};
    require(mixed::euler(low, 10, matches, &result) == mixed::Status::ok, "Euler supported status");
    mpz_sub_ui(exponent, modulus, 1); mpz_fdiv_q_2exp(exponent, exponent, 1); mpz_powm(expected, two, exponent, modulus);
    const int jacobi = mpz_jacobi(two, modulus); require(jacobi == 1 || jacobi == -1, "independent Jacobi for odd modulus");
    mpz_set_ui(scratch, 1); if (jacobi == -1) mpz_sub_ui(scratch, modulus, 1);
    const bool oracle_matches = mpz_cmp(expected, scratch) == 0;
    require(matches == oracle_matches, "independent GMP Euler/Jacobi agreement");
    require(matches == pgn_euler96::test(low, 10), "untouched Euler96 verdict agreement");
    mpz_mul(expected, expected, radix); mpz_mod(expected, expected, modulus); integer(actual, result);
    require(mpz_cmp(actual, expected) == 0, "Euler residue uses same R68 unit/sign representation");
    ++counts.euler; if (matches) ++counts.euler_positive;
  }
};

static void poisonChecks(Oracle& oracle) {
  const U96 sentinel{{0x12345678U, 0x87654321U, 0xabcdef01U}};
  const U96 good_modulus = native(1), zero{}, plain_one{{1U, 0U, 0U}};
  const uint32_t inverse = pgn_euler96::inverse(good_modulus.w[0]);
  const std::array<U96, 7> unsupported{{native(1, 0), native(1, 8), native(1, 9), native(1, 11), native(1, 16), native(1, UINT32_MAX), native(2)}};
  for (const U96& modulus : unsupported) {
    U96 output = sentinel;
    require(mixed::montgomeryOne(modulus, output) == mixed::Status::unsupported_modulus && pgn_euler96::equal(output, sentinel), "unsupported one unchanged");
    require(mixed::multiply(zero, zero, modulus, inverse, output) == mixed::Status::unsupported_modulus && pgn_euler96::equal(output, sentinel), "unsupported product unchanged");
    require(mixed::twice(zero, modulus, output) == mixed::Status::unsupported_modulus && pgn_euler96::equal(output, sentinel), "unsupported double unchanged");
    require(mixed::powerOfTwo(plain_one, modulus, output) == mixed::Status::unsupported_modulus && pgn_euler96::equal(output, sentinel), "unsupported power unchanged");
    oracle.counts.poisons += 4;
  }
  for (const U96& bad : std::array<U96, 3>{{good_modulus, native(UINT64_MAX, 11), native(0, UINT32_MAX)}}) {
    U96 output = sentinel; mixed::MultiplyAudit audit{}; audit.last_reduction_digit = 99U;
    require(mixed::multiply(bad, zero, good_modulus, inverse, output, &audit) == mixed::Status::unnormalized_operand, "unnormalized left");
    require(mixed::multiply(zero, bad, good_modulus, inverse, output, &audit) == mixed::Status::unnormalized_operand, "unnormalized right");
    require(mixed::twice(bad, good_modulus, output) == mixed::Status::unnormalized_operand, "unnormalized double");
    require(pgn_euler96::equal(output, sentinel) && audit.last_reduction_digit == 99U, "bad operand leaves all outputs unchanged");
    oracle.counts.poisons += 3;
  }
  for (uint32_t bad_inverse : std::array<uint32_t, 3>{{0U, inverse ^ 1U, inverse ^ 0x80000000U}}) {
    U96 output = sentinel;
    require(mixed::multiply(plain_one, plain_one, good_modulus, bad_inverse, output) == mixed::Status::invalid_inverse && pgn_euler96::equal(output, sentinel), "invalid inverse unchanged");
    ++oracle.counts.poisons;
  }
  for (uint64_t high : std::array<uint64_t, 6>{{0, 9, 11, 16, uint64_t(10) + (uint64_t(1) << 32), UINT64_MAX}}) {
    bool matches = true; U96 result = sentinel;
    require(mixed::euler(1, high, matches, &result) == mixed::Status::unsupported_modulus && matches && pgn_euler96::equal(result, sentinel), "Euler high argument never masked");
    ++oracle.counts.poisons;
  }
  bool matches = true; U96 result = sentinel;
  require(mixed::euler(2, 10, matches, &result) == mixed::Status::unsupported_modulus && matches && pgn_euler96::equal(result, sentinel), "Euler even argument explicit refusal");
  ++oracle.counts.poisons;
}

int main() {
  Oracle oracle; std::mt19937_64 random(0x6800323204ULL);
  const std::vector<uint64_t> boundary_lows{1, 3, 5, 7, 9, 11, 13, 15, 0xfffffff1ULL, 0xfffffff3ULL,
    0xfffffff5ULL, 0xfffffff7ULL, 0xfffffff9ULL, 0xfffffffbULL, 0xfffffffdULL, 0xffffffffULL,
    0x100000001ULL, 0x100000003ULL, 0xffffffff00000001ULL, 0xffffffff0000000fULL,
    UINT64_MAX - 30, UINT64_MAX - 16, UINT64_MAX - 2, UINT64_MAX};
  auto structured = [&](uint64_t low, bool nibble_grid) {
    oracle.setup(low);
    std::vector<U96> operands{{{0, 0, 0}}, {{1, 0, 0}}, {{UINT32_MAX, 0, 0}}, {{0, 1, 0}}, {{UINT32_MAX, UINT32_MAX, 0}}, {{0, 0, 1}}};
    mpz_sub_ui(oracle.scratch, oracle.modulus, 1); operands.push_back(words(oracle.scratch));
    mpz_sub_ui(oracle.scratch, oracle.modulus, 2); operands.push_back(words(oracle.scratch));
    mpz_fdiv_q_2exp(oracle.scratch, oracle.modulus, 1); operands.push_back(words(oracle.scratch));
    U96 one{}; mixed::montgomeryOne(oracle.modulus_words, one); operands.push_back(one);
    for (const U96& operand_left : operands) {
      for (const U96& operand_right : operands) oracle.checkProduct(operand_left, operand_right);
      oracle.checkDouble(operand_left); oracle.checkConversions(operand_left);
    }
    if (nibble_grid) for (uint32_t left_class = 0; left_class < 16; ++left_class)
      for (uint32_t right_class = 0; right_class < 16; ++right_class)
        oracle.checkProduct(U96{{left_class, 0, 0}}, U96{{right_class, 0, 0}});
    for (unsigned bit : std::array<unsigned, 11>{{0, 1, 31, 32, 33, 63, 64, 65, 67, 68, 95}}) {
      U96 exponent{}; exponent.w[bit / 32] = uint32_t(1U << (bit % 32)); oracle.checkPower(exponent);
    }
    oracle.checkPower(U96{}); oracle.checkPower(U96{{UINT32_MAX, UINT32_MAX, UINT32_MAX}}); oracle.checkEuler();
  };
  for (uint64_t low : boundary_lows) structured(low, true);
  for (uint64_t low_byte = 1; low_byte < 256; low_byte += 2) structured(0xfedcba9800000000ULL | low_byte, true);
  for (unsigned sample = 0; sample < 8192; ++sample) {
    oracle.setup(random() | 1ULL);
    mpz_sub_ui(oracle.scratch, oracle.modulus, 1); const U96 last = words(oracle.scratch);
    oracle.checkProduct(last, last); oracle.checkProduct(U96{}, last); oracle.checkProduct(U96{{1, 0, 0}}, last);
    for (unsigned pair = 0; pair < 12; ++pair) {
      const U96 raw_left{{uint32_t(random()), uint32_t(random()), uint32_t(random() & 15U)}};
      const U96 raw_right{{uint32_t(random()), uint32_t(random()), uint32_t(random() & 15U)}};
      integer(oracle.left, raw_left); mpz_mod(oracle.left, oracle.left, oracle.modulus);
      integer(oracle.right, raw_right); mpz_mod(oracle.right, oracle.right, oracle.modulus);
      const U96 operand_left = words(oracle.left), operand_right = words(oracle.right);
      oracle.checkProduct(operand_left, operand_right, pair == 0); oracle.checkDouble(operand_left);
      if (pair == 0) oracle.checkConversions(operand_left);
    }
    oracle.checkPower(U96{{uint32_t(random()), uint32_t(random()), uint32_t(random())}}); oracle.checkEuler();
  }
  poisonChecks(oracle);
  const Counts& counts = oracle.counts;
  require(counts.last_digits == 0xffffU && counts.odd_mod16 == 0xaaaaU && counts.left_mod16 == 0xffffU && counts.right_mod16 == 0xffffU, "all legal modulus and operand/digit low-bit classes");
  require(counts.subtractions[0] != 0 && counts.subtractions[1] != 0 && counts.euler_positive != 0, "both normalization branches and positive Euler cases");
  std::cout << "PASS GMP exact R68: modulus_cases=" << counts.moduli << " products=" << counts.products
    << " full_row_trace_checks=" << counts.trace_rows << " powers=" << counts.powers << " Euler_GMP_and_untouched96=" << counts.euler
    << " Euler_positive=" << counts.euler_positive << " conversions=" << counts.conversions << " doubles=" << counts.doubles
    << " poisons=" << counts.poisons << " alias_products=" << counts.aliases << '\n';
  std::cout << "PASS carry_audit: max_full_multiply_carry=" << counts.max_multiply_carry << " max_full_reduce_carry=" << counts.max_reduce_carry
    << " max_last_product_top=" << counts.max_small_product_top << " max_last_numerator_top=" << counts.max_numerator_top
    << " max_last_quotient_top=" << counts.max_quotient_top << " normalization0=" << counts.subtractions[0]
    << " normalization1=" << counts.subtractions[1] << " last_digits_mask=" << counts.last_digits << '\n';
  std::cout << "Boundary: host arithmetic/oracle only; no prime-proof, GPU, speed, source coverage or network-credit claim\n";
}
