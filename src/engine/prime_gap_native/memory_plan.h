#pragma once
#include <cstdint>
#include <limits>

// Host-only geometry/allocation ledger. Does not contain arithmetic or kernels.
namespace pgn_release_plan {
constexpr uint64_t BlockNumbers = 30ULL << 25, MaxBlocks = 4194304;
constexpr uint64_t Margin = 128ULL << 20;
struct Range {
    bool valid = false, tailClipped = false;
    const char *reason = "invalid_range";
    uint64_t low = 0, high = 0, length = 0, remainder = 0, base = 0, blocks = 0, count = 0;
};
inline Range range(uint64_t low, uint64_t high, uint64_t length, unsigned k) {
    Range r{};
    r.low = low;
    r.high = high;
    r.length = length;
    if (high < 4 || high > 2147483647ULL) {
        r.reason = "unsupported_high_word";
        return r;
    }
    if (!length) {
        r.reason = "empty_range";
        return r;
    }
    if (length - 1 > UINT64_MAX - low) {
        r.reason = "single_low64_window_required";
        return r;
    }
    // Floor the *whole requested N* to its class30 origin. Never ceil an
    // arbitrary requested start: that would discard its first partial30.
    r.remainder = (low % 30 + (high % 30) * 16) % 30;
    if (low < r.remainder) {
        r.reason = "class30_floor_crosses_low64_window";
        return r;
    }
    r.base = low - r.remainder;
    // low>=remainder and length<=MAX-low prove this addition cannot wrap.
    if (length > MaxBlocks * BlockNumbers - r.remainder) {
        r.reason = "descriptor_block_limit";
        return r;
    }
    const uint64_t covered = length + r.remainder;
    r.blocks = covered / BlockNumbers + (covered % BlockNumbers != 0);
    if (r.blocks > MaxBlocks) {
        r.reason = "descriptor_block_limit";
        return r;
    }
    r.tailClipped = (UINT64_MAX - r.base) / BlockNumbers < r.blocks + 2;
    r.count = r.blocks / k + (r.blocks % k != 0);
    r.valid = true;
    r.reason = "ok";
    return r;
}
struct Memory {
    uint64_t bitmap = 0, results = 0, proofs = 0, descriptors = 0, other = 0, allocation = 0,
             required = 0, mapped = 0;
    uint64_t perDescriptor = 0, fixed = 0, maxDescriptors = 0, maxLength = 0;
    bool fits = false;
};
inline Memory memory(const Range &r, unsigned k, unsigned lanes, uint64_t primeCount,
                     uint64_t itemCount, uint64_t preBytes, uint64_t descriptorSize,
                     uint64_t stateSize, uint64_t gapSize, uint64_t freeBytes) {
    Memory m{};
    constexpr uint64_t capacity = 4096, words = (1ULL << 28) / 64;
    m.bitmap = uint64_t(2 * lanes) * (k + 1) * words * 8;
    m.results = r.count * capacity * gapSize;
    m.proofs = r.count * capacity * 4;
    m.descriptors = r.count * descriptorSize;
    // root steps; prime,reciprocal,R64 tables; item/chunk tables; wheel/steps;
    // state lanes; descriptor stats/visits; periodic prefilter; event210 prefix.
    const uint64_t fixedOther = primeCount * 4 + primeCount * 3 * 8 + itemCount * 2 * 4 + 12 + 32 +
                                uint64_t(lanes) * stateSize + preBytes + 4200ULL * 96 * 4;
    m.other = fixedOther + r.count * (16 + 4);
    m.allocation = m.bitmap + m.results + m.proofs + m.descriptors + m.other;
    m.required = m.allocation + Margin;
    m.mapped = m.results + r.count * 16 + (r.count + lanes + 1) * 4;
    m.perDescriptor = capacity * gapSize + capacity * 4 + descriptorSize + 16 + 4;
    m.fixed = m.bitmap + fixedOther;
    if (freeBytes >= m.fixed + Margin) {
        m.maxDescriptors = (freeBytes - m.fixed - Margin) / m.perDescriptor;
        if (m.maxDescriptors > MaxBlocks / k)
            m.maxDescriptors = MaxBlocks / k;
        const uint64_t memoryBlocks =
            m.maxDescriptors > MaxBlocks / k ? MaxBlocks : m.maxDescriptors * k;
        const uint64_t maxBlocks = memoryBlocks < MaxBlocks ? memoryBlocks : MaxBlocks;
        if (maxBlocks && maxBlocks * BlockNumbers > r.remainder) {
            m.maxLength = maxBlocks * BlockNumbers - r.remainder;
            if (m.maxLength - 1 > UINT64_MAX - r.low)
                m.maxLength = UINT64_MAX - r.low + 1;
        }
    }
    m.fits = r.valid && m.required <= freeBytes;
    return m;
}
} // namespace pgn_release_plan
