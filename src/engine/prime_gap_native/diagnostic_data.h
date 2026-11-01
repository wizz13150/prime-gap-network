#pragma once
// Generated from frozen inputs. Re-run generate_diagnostic.py for an
// independent Python-integer oracle. Runtime also recomputes an independent
// portable CPU oracle; the GPU never generates expected values.
#include <cstdint>
namespace pgn_release_diag {
constexpr unsigned InputCount = 274;
constexpr const char *PublicCommit = "9d41f2709666d240c073439c37722c6ff6cb5b44";
constexpr const char *Predicate = "Euler/Jacobi base 2: 2^(N>>1) mod N = Jacobi(2,N)";
constexpr uint64_t Inputs[InputCount] = {
    0xd78ebc5ac623d1f9ULL, 0x0000000000000001ULL, 0xffffffffffffffffULL, 0x0000000fffffffffULL,
    0x0000001000000001ULL, 0x0000000ffffffffdULL, 0x0000001000000003ULL, 0x8000000000000001ULL,
    0xd78ebc5ac623d1f9ULL, 0x28788e066f01c4e9ULL, 0x84bd97b958b938ffULL, 0xa999a873f500d1ffULL,
    0xd1479a0e45d09a03ULL, 0x9e60226a89d3dbf7ULL, 0x8649f914e1c69143ULL, 0xdfd564f432cb2ca1ULL,
    0x83fd8b4a57d11fddULL, 0xadfe7a630f050ff7ULL, 0x1846c25a414641cfULL, 0xb788829670339731ULL,
    0x5687bda91a6e1c5fULL, 0x57c2db757f1837a7ULL, 0x016342f633747783ULL, 0x2e63dfdf13d43a93ULL,
    0xaf424137e1f26427ULL, 0x0e4d082b6c72692fULL, 0x44ca2344276f263dULL, 0xc56ec2b93e4dd731ULL,
    0x30775dff241ca0dfULL, 0x4f64399030b0b851ULL, 0xee34c412f137ed61ULL, 0x985b3e059174fafbULL,
    0x48e492c3bacc29d5ULL, 0x7a2ca464575e9f8fULL, 0x1dcd59d9fa0d24b3ULL, 0xabd5074e58381b75ULL,
    0x60892611e5e61603ULL, 0x5df3764808b63aefULL, 0xc626d2cc9d7518f7ULL, 0xf12c388ce1d0bee9ULL,
    0x4582bfa40c0ce3dfULL, 0x2c0d0aef80a05403ULL, 0x3a56a16695e2746bULL, 0xe1dc5a8efa5fca43ULL,
    0x57c58ced833f8517ULL, 0xfe724207d8c05fddULL, 0x01e27dfa69009bd5ULL, 0x28383479d354f2bdULL,
    0x938d07406a1f1cb1ULL, 0x44c191f102d48895ULL, 0x75d39ace7998c68dULL, 0xfbbcff34554a884fULL,
    0xe67ebde1e375ee9fULL, 0x0b7cbf8a261b9c87ULL, 0x0f5bc048262ad055ULL, 0xb5b4acb407b230b5ULL,
    0xe01e3e43e2692327ULL, 0xfa8c59896ac3d8a1ULL, 0xd6d655424f60ce29ULL, 0xdd68f48641e2a5f5ULL,
    0x1303d3bb9da51705ULL, 0xfb380b3959ed3c6bULL, 0xc722717dce3a9cd3ULL, 0x16bb3ee2496e1c81ULL,
    0x1d905d0d972ec979ULL, 0x0e72b10c4bcbeaabULL, 0xd2349e956d508751ULL, 0xbfd7f9daa77fd21fULL,
    0xa8b31b63a3704a7bULL, 0x69efb3051935542fULL, 0x82b7e7ac5e59d547ULL, 0xafc1bcce0462d72dULL,
    0x001d78ce29bc79c3ULL, 0xd758948881193fd3ULL, 0x8df9ad3f0fcfebefULL, 0x15dfa915398cdf27ULL,
    0x094d2261359eef59ULL, 0xa1ed9d91042b24c7ULL, 0xf93d6f3ad6f64b53ULL, 0x576c2ec9f1a91305ULL,
    0x26aa2e016aa82063ULL, 0xcd095ba0d7bf08e3ULL, 0x661abedef76fa803ULL, 0xf4442a254b4d1793ULL,
    0x3021fab1b91592a7ULL, 0xcc284030c3148453ULL, 0xcc63ea91440fd99bULL, 0xbea40d9f3fd2df69ULL,
    0xb40a985eafd98097ULL, 0x12441dd0a20e2e0bULL, 0x7bd97f83e42ed097ULL, 0x56fcc2791b29ede7ULL,
    0x8b850d40f01218b3ULL, 0x513d1327d6dfa041ULL, 0x46e0ef0b84102f41ULL, 0x62dab9416c45ff5fULL,
    0xd659f1f2c6cb4361ULL, 0x2544958da2b42da7ULL, 0x404f7bec11e1b7f9ULL, 0x9b2ac51eb12f2603ULL,
    0xc0344794125a988fULL, 0xbb80a6af0426aba5ULL, 0x2ebbabf4f0202a6dULL, 0x24449695b6755179ULL,
    0xafcc58de2154c59bULL, 0xf4f58efae274b605ULL, 0x4b82bfabe00f7e29ULL, 0x44159b86ceffca95ULL,
    0xb03c53d05a3aeccfULL, 0xa6aae23f37b63fc3ULL, 0x9b83a2c2cf176119ULL, 0x77a88a323044299bULL,
    0x844093f4b01d63cfULL, 0x3850bd0e55f2605dULL, 0x051066cb1e7033ddULL, 0x7e0b366d22606573ULL,
    0x26fdc6225e619979ULL, 0x62ee9dddf982240bULL, 0xa167445ae37f4283ULL, 0x64ee6c043a3de7bfULL,
    0x35e889dd28563097ULL, 0xc64b2068761e3e31ULL, 0xe6ef6f81baa1ae0dULL, 0xc7c12e99b85cce11ULL,
    0x9894361aa247d3cdULL, 0x723b55de9c03fed7ULL, 0xe458d7c7f8954333ULL, 0x2e549c2170b8c575ULL,
    0x9a0f1ade97b849bfULL, 0xe19dd8b8e4585881ULL, 0x8025cddef4a81593ULL, 0x77b33a1c0517bb5dULL,
    0xa74644fa494ee36bULL, 0x1096dbdbbad3c46dULL, 0x25cbf46d7090d8a5ULL, 0x686ad521c34b2293ULL,
    0x9fca312666d399abULL, 0xf393c4f8314d0b6fULL, 0x3cb2bd556672aab9ULL, 0x3c9467ef3d8c1d2fULL,
    0x17b555f55effaed5ULL, 0x3357e172e7c6b041ULL, 0x530505ab3f6f0d61ULL, 0xf753a47f0508ab3bULL,
    0x7a74a4c9851514adULL, 0x4af2efa6d378cbafULL, 0x1559bc1b9d589d9bULL, 0x096ffc5edd3b9095ULL,
    0x324c1eb761486131ULL, 0x3e0f64126ad29db3ULL, 0xd0f54bd4286e8a75ULL, 0xdbf444e09e906a21ULL,
    0x5cf76a7b3399e2b5ULL, 0xdaf682fb24b56eebULL, 0xe9edd5fe65b68ecbULL, 0x74469b06d1ea9831ULL,
    0xbd1be66b376b6141ULL, 0x9b5b826cd873c7c3ULL, 0xc340f62e81bcb6edULL, 0x52260c4b26697b69ULL,
    0xfe263c7ba2a853dfULL, 0xe59425c36a7bde91ULL, 0xb3d60a2145f0698fULL, 0x7c3f26a4b7edfd89ULL,
    0x1add1048d8a38ddbULL, 0x37e75a40caf49d2bULL, 0xe626d65ed8415ed1ULL, 0x155f10ea74754babULL,
    0x8eb4b5502f67d0abULL, 0x30b14db07b2cffe1ULL, 0x7dbf5388f4a2697dULL, 0x35ecd07b2520d2efULL,
    0x5d0180bd85e2859bULL, 0xbe76114388624b15ULL, 0xc5b8618a8b04eac3ULL, 0x9f5854b6ff83be31ULL,
    0xb8451cfa2f4f150dULL, 0xc0ae394b3abb6867ULL, 0xa6c88e91a1f6e777ULL, 0xa9c0c81e4d103779ULL,
    0xbedce16ee1c6e957ULL, 0x9cedfe07321fd145ULL, 0xa783c13a7ee91fa7ULL, 0xcb64510005877e2bULL,
    0xf3e1ce3ebfb89a17ULL, 0x60462abd22658ee3ULL, 0xea7748aa8874492bULL, 0xb84a4f305fa18b79ULL,
    0xc8d4d06b8a4d362fULL, 0x76bcc3c3e7d7c783ULL, 0xecc3d825f8a1581dULL, 0x451789f4cbdfbdedULL,
    0x4167c87613d301e5ULL, 0xb14affbea6159fdbULL, 0xfbd6a5ef83acd487ULL, 0xc507ae02b55b13f7ULL,
    0xc28a66ad7cc3aaebULL, 0xc66a7071a01fc76bULL, 0x9081f47c38094225ULL, 0x7065bb4119af79e1ULL,
    0xc89ce4f15aa680ebULL, 0x77c7d7edeb179255ULL, 0xb0941085170d8831ULL, 0x44e1658f1185bf61ULL,
    0x25e194fa419b4c5fULL, 0x55b155f1a04e8d07ULL, 0x81867d20e2b7b1ddULL, 0xcdbb5a62e928046dULL,
    0x752825a4b83cef25ULL, 0x66f34ce19ce5ffbbULL, 0x4f5792d83c9822f1ULL, 0x407e3f6391d60ce9ULL,
    0x1da958a0289601c5ULL, 0x0e7eff770e0afaffULL, 0xbf692929f078052bULL, 0x8569f57102fedfe1ULL,
    0xfd2e3d27543badcdULL, 0x82d3ab7d9e7b6645ULL, 0x37544e2ed4c4a1c9ULL, 0x5cc0ae4ad26258bfULL,
    0x660742aac2c3d6e9ULL, 0xee3966f437bdef9dULL, 0x55c2c211c1671d65ULL, 0xfa094ad50cb02a1fULL,
    0x4bde5355d4f62d8bULL, 0x68cfa3ed96389a0fULL, 0x779ad5efc18588fbULL, 0x9643cd6e2818afc3ULL,
    0xce5c825cfbdd5a15ULL, 0x38910f6095fcc5e1ULL, 0xefe92f8bf090d253ULL, 0x09f9d324d7610737ULL,
    0xd54cbf8ab79ae8f9ULL, 0x72d50a3ac39d9f5fULL, 0x3feeb3c0c5d493a1ULL, 0x478760e7f7ec84a3ULL,
    0xbbb7ead7c91c4a57ULL, 0xd5c7987fa6d00703ULL, 0xde7c2684e2e607cdULL, 0x6485b5893c894733ULL,
    0x25e8b3b3d6bdf97dULL, 0xda14b6745adf7dcfULL, 0xba58332be8d32555ULL, 0xd41eea6f03df765fULL,
    0x5140259dba28bf73ULL, 0xbea24cc7b90752cdULL, 0xc43fd5c442da139dULL, 0x0dbcd7b710f327bbULL,
    0x254781d0cc1d483fULL, 0x1210e4c2f90e9d6fULL, 0x5bbc73789bdc3b95ULL, 0xd78ebc5000000065ULL,
    0xd78ebc5000004e23ULL, 0xd78ebc500000000bULL, 0xd78ebc5000004e21ULL, 0xd78ebc5000000003ULL,
    0x0053000000000001ULL, 0x0057000000000001ULL, 0x0087000000000001ULL, 0x0099000000000001ULL,
    0x01f73ad728e63c19ULL, 0x08aeb4682a2249f9ULL, 0x0954c71b2d63ef09ULL, 0x12211161944dfff9ULL,
    0x0000000000000005ULL, 0x000000000000000bULL, 0x0000000000000011ULL, 0x0000000000000017ULL,
    0x000000000000001dULL, 0x0000000000000023ULL, 0x0000000000000029ULL, 0x000000000000002fULL,
    0x0000000000000035ULL, 0x000000000000003bULL,
};
constexpr uint32_t IndependentCpuGold[InputCount] = {
    1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u, 0u, 0u, 0u, 0u, 0u, 1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
    0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
    0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
    0u, 1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
    0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
    0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u, 0u, 0u, 0u, 0u,
    0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u,
    0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
    0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
    0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u, 1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
    0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
    0u, 1u, 0u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
};
} // namespace pgn_release_diag
#if PGN_RELEASE_ENABLE_PUBLIC_PERIG && defined(__CUDA_ARCH__)
namespace pgn_release_perig_public {
typedef unsigned __int128 uint128_t;
// ===== Thanks to Perig for this code =====
//       vvvvvvvvvvvvvvvvvvvvvvvvvvvvv

// (a::b) <<= c
__device__ static inline __attribute__((always_inline)) void my_shld64(uint64_t *a, uint64_t *b,
                                                                       uint64_t c) {
#if PARANOID
    assert(c < 64);
#endif
    if (c == 0) {
    } else {
        (*a) = ((*a) << c) | ((*b) >> (64 - c));
        (*b) <<= c;
    }
}

// borrow::diff = a - b - borrow_in
static inline __attribute__((always_inline)) __device__ uint8_t my_sbb64(uint8_t borrow_in,
                                                                         uint64_t a, uint64_t b,
                                                                         uint64_t *diff) {
    uint64_t tmp1 = a - borrow_in;
    uint8_t borrow = (tmp1 > a);

    uint64_t tmp2 = tmp1 - b;
    borrow |= (tmp2 > tmp1);
    *diff = tmp2;
    return borrow;
}

// count leading zeroes in binary representation
static inline __attribute__((always_inline)) __device__ uint64_t my_clz64(uint64_t n) {
#ifdef __CUDA_ARCH__
    return __clzll(n);
#else
    if (n == 0)
        return 64;
    uint64_t r = 0;
    if ((n & (0xFFFFFFFFull << 32)) == 0)
        r += 32, n <<= 32;
    if ((n & (0xFFFFull << 48)) == 0)
        r += 16, n <<= 16;
    if ((n & (0xFFull << 56)) == 0)
        r += 8, n <<= 8;
    if ((n & (0xFull << 60)) == 0)
        r += 4, n <<= 4;
    if ((n & (0x3ull << 62)) == 0)
        r += 2, n <<= 2;
    if ((n & (0x1ull << 63)) == 0)
        r += 1;
    return r;
#endif
}

static inline __attribute__((always_inline)) __device__ uint64_t
montgomeryInverse64(uint64_t mod_lo) {
    uint64_t x = (3ull * mod_lo) ^ 2ull; // 5 bits acurate
    uint64_t t = 1ull - mod_lo * x;
    x *= 1 + t; // 10 bits accurate
    t *= t;
    x *= 1 + t; // 20 bits accurate
    t *= t;
    x *= 1 + t; // 40 bits accurate
    t *= t;
    x *= 1 + t; // 80 bits accurate , i.e. > 64 bits
    return 0 - x;
}

// subtract the modulus 'mod' multiple times from the input number 'res', if needed
static inline __attribute__((always_inline)) __device__ void
ciosSubtract128(uint64_t *res_lo, uint64_t *res_hi, uint64_t mod_lo, uint64_t mod_hi) {
    uint64_t n_lo, n_hi;
    uint64_t t_lo, t_hi;
    uint8_t b;
    n_lo = *res_lo;
    n_hi = *res_hi;
    // save, subtract the modulus until a borrows occurs
    do {
        t_lo = n_lo;
        t_hi = n_hi;
        b = my_sbb64(0, n_lo, mod_lo, &n_lo);
        b = my_sbb64(b, n_hi, mod_hi, &n_hi);
    } while (b == 0);
    // get the saved values
    *res_lo = t_lo;
    *res_hi = t_hi;
}

static inline __attribute__((always_inline)) __device__ void
ciosConstants128(uint64_t mod_lo, uint64_t mod_hi, uint64_t *magic_lo, uint64_t *magic_hi) {
    // computes 2^128 % mod
    uint128_t m = ((uint128_t)mod_hi << 64) + mod_lo;
    uint128_t t = -m; // 2^128-m
    t %= m;           // (2^128-m) % m
    *magic_lo = (uint64_t)t;
    *magic_hi = (uint64_t)(t >> 64);
#if PARANOID
    assert(*magic_hi <= mod_hi);
#endif
}

static inline __attribute__((always_inline)) __device__ void
ciosModSquare128(uint64_t *res_lo, uint64_t *res_hi, uint64_t mod_lo, uint64_t mod_hi,
                 uint64_t mmagic) {
    uint64_t n_lo = *res_lo, n_hi = *res_hi;
    uint128_t cs, cc;
    uint64_t t0, t1, t2, m;

    cc = (uint128_t)n_lo * n_lo; // #1
    t0 = (uint64_t)cc;
    cc = cc >> 64;
    cc += (uint128_t)n_lo * (n_hi + n_hi); // #2
    t1 = (uint64_t)cc;
    cc = cc >> 64;
    t2 = (uint64_t)cc;
#if PARANOID
    assert(cc >> 64 == 0);
#endif

    m = t0 * mmagic;            // #3
    cs = (uint128_t)m * mod_lo; // #4
    cs += t0;
    cs = cs >> 64;

    cs += (uint128_t)m * mod_hi; // CAN OPTIMIZE
    cs += t1;

    t0 = (uint64_t)cs;
    cs = cs >> 64;
    cs += t2;
    t1 = (uint64_t)cs;
    cs = cs >> 64;
    t2 = (uint64_t)cs;
#if PARANOID
    assert(cs >> 64 == 0);
#endif

    cc = (uint128_t)n_hi * n_hi; // #6
    cc += t1;
    t1 = (uint64_t)cc;
    cc = cc >> 64;
    cc += t2;
    t2 = (uint64_t)cc;
#if 0
	// not necessary with 2-bits guard
	cc = cc >> 64;
	uint64_t t3 = (uint64_t) cc;
	assert(t3 == 0);
#endif
#if PARANOID
    assert(cc >> 64 == 0);
#endif

    m = t0 * mmagic;            // #3
    cs = (uint128_t)m * mod_lo; // #8
    cs += t0;
    cs = cs >> 64;

    cs += (uint128_t)m * mod_hi; // CAN OPTIMIZE

    cs += t1;
    t0 = (uint64_t)cs;
    cs = cs >> 64;
    cs += t2;
    t1 = (uint64_t)cs;
#if 0
	// not necessary with 2-bits guard
	cs = cs >> 64;
	cs += t3;
	t2 = (uint64_t) cs;
	assert(t2 == 0);
#endif
#if PARANOID
    assert(cs >> 64 == 0);
#endif

    *res_lo = t0;
    *res_hi = t1;
}

__device__ inline void ciosModSquare3_128(uint64_t *res_lo, uint64_t *res_hi, uint64_t mod_lo,
                                          uint64_t mod_hi, uint64_t mmagic) {
    ciosModSquare128(res_lo, res_hi, mod_lo, mod_hi, mmagic);
    ciosModSquare128(res_lo, res_hi, mod_lo, mod_hi, mmagic);
    ciosModSquare128(res_lo, res_hi, mod_lo, mod_hi, mmagic);
}

#ifdef HIGH_64
__device__ bool ciosFermatTest128(uint64_t n_lo) {
#else
__device__ bool ciosFermatTest128(uint64_t n_lo, uint64_t HIGH_64) {
#endif
#if PARANOID
    assert((n_lo & 1) == 1);
#endif
    //const uint64_t n_hi = <stuff here>;

    uint64_t res_lo, res_hi;
    uint64_t one_lo, one_hi;
    int bit;
    // constant -1/m mod 2^64
    uint64_t mmagic = montgomeryInverse64(n_lo);

    // enter montgomery domain
    // constant 2^128 mod m
    ciosConstants128(n_lo, HIGH_64, &one_lo, &one_hi);
    res_hi = one_hi;
    res_lo = one_lo;

    if (HIGH_64 == 0) {
        bit = 64 - my_clz64(n_lo);
        uint64_t msb_bits = bit < 5 ? bit - 1 : 3;
        uint64_t msb_mask = (1 << msb_bits) - 1;
        bit -= msb_bits;
        my_shld64(&res_hi, &res_lo, (n_lo >> bit) & msb_mask);

    } else {
        bit = 64 - my_clz64(HIGH_64);
        uint64_t msb_bits = bit < 4 ? bit : 3;
        uint64_t msb_mask = (1 << msb_bits) - 1;
        bit -= msb_bits;
        my_shld64(&res_hi, &res_lo, (HIGH_64 >> bit) & msb_mask);

        while (bit >= 3) {
            bit -= 3;
            // square and reduce
            ciosModSquare3_128(&res_lo, &res_hi, n_lo, HIGH_64, mmagic);
            // shift
            my_shld64(&res_hi, &res_lo, ((HIGH_64 >> bit) & 7));
        }

        while (bit) {
            bit -= 1;
            // square and reduce
            ciosModSquare128(&res_lo, &res_hi, n_lo, HIGH_64, mmagic);
            // shift
            my_shld64(&res_hi, &res_lo, ((HIGH_64 >> bit) & 1));
        }

        bit = 64;
    }
    //}
    while (bit >= 5) {
        bit -= 3;
        // square and reduce
        ciosModSquare3_128(&res_lo, &res_hi, n_lo, HIGH_64, mmagic);
        // shift
        my_shld64(&res_hi, &res_lo, ((n_lo >> bit) & 7));
    }

    while (bit > 1) {
        bit -= 1;
        // square and reduce
        ciosModSquare128(&res_lo, &res_hi, n_lo, HIGH_64, mmagic);
        // shift
        my_shld64(&res_hi, &res_lo, ((n_lo >> bit) & 1));
    }

    // make sure result is strictly less than the modulus
    ciosSubtract128(&res_lo, &res_hi, n_lo, HIGH_64);

    uint64_t legendre = ((n_lo >> 1) ^ (n_lo >> 2)) & 1; // shortcut calculation of legendre symbol

    uint64_t m1_lo;
    uint64_t m1_hi;
    uint8_t c;
    c = my_sbb64(0, n_lo, one_lo, &m1_lo);
    my_sbb64(c, HIGH_64, one_hi, &m1_hi);

    return ((res_lo == (legendre ? m1_lo : one_lo)) && (res_hi == (legendre ? m1_hi : one_hi)));
}

// ciosFermatTest128 should be used for up to 119 bits
// montgomeryFermatTest128 should be used fro 120-127 bits

//       ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
// ===== Thanks to Perig for this code ===== (end)

} // namespace pgn_release_perig_public
#endif
