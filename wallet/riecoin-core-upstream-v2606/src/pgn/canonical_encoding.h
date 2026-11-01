#pragma once
#include "range_commitment.h"
#include <iterator>
#include <stdexcept>
#include <string_view>

// Shared canonical byte primitives. Format-specific callers supply explicit
// limits and check their own counts and semantic contracts before allocation.
namespace pgn::core::detail {
inline void encodingRequire(bool valid, const char *reason) {
  if (!valid)
    throw std::invalid_argument(reason);
}
struct Writer {
  Bytes data;
  const EncodingLimits &limits;
  void byte(std::uint8_t value) {
    encodingRequire(data.size() < limits.max_encoded_bytes,
                    "Encoded segment exceeds byte limit");
    data.push_back(value);
  }
  void raw(std::span<const std::uint8_t> bytes) {
    for (auto value : bytes)
      byte(value);
  }
  void domain(std::string_view value) {
    for (auto c : value)
      byte(static_cast<std::uint8_t>(c));
    byte(0);
  }
  void little(std::uint64_t value, unsigned size) {
    for (unsigned i = 0; i < size; ++i) {
      byte(value & 255);
      value >>= 8;
    }
  }
  void count(std::uint64_t value) {
    if (value < 253)
      byte(value);
    else if (value <= 65535) {
      byte(253);
      little(value, 2);
    } else if (value <= 0xffffffffULL) {
      byte(254);
      little(value, 4);
    } else {
      byte(255);
      little(value, 8);
    }
  }
  void integer(const Integer &value) {
    encodingRequire(value >= 0, "Negative unsigned integer");
    Bytes magnitude;
    if (value != 0) {
      encodingRequire(boost::multiprecision::msb(value) / 8 <
                          limits.max_integer_bytes,
                      "Integer exceeds explicit byte limit");
      export_bits(value, std::back_inserter(magnitude), 8, true);
    }
    count(magnitude.size());
    raw(magnitude);
  }
};
struct Reader {
  std::span<const std::uint8_t> data;
  const EncodingLimits &limits;
  std::size_t position{};
  std::uint8_t byte() {
    encodingRequire(position < data.size(), "Truncated segment");
    return data[position++];
  }
  std::uint64_t little(unsigned size) {
    std::uint64_t result{};
    for (unsigned i = 0; i < size; ++i)
      result |= std::uint64_t(byte()) << (8 * i);
    return result;
  }
  std::uint64_t count() {
    const auto first = byte();
    if (first < 253)
      return first;
    const auto value = little(first == 253 ? 2 : first == 254 ? 4 : 8);
    encodingRequire(first == 253   ? value >= 253
                    : first == 254 ? value > 65535
                                   : value > 0xffffffffULL,
                    "Noncanonical length encoding");
    return value;
  }
  Integer integer() {
    const auto size = count();
    encodingRequire(size <= limits.max_integer_bytes &&
                        size <= data.size() - position,
                    "Integer length exceeds input or limit");
    if (!size)
      return 0;
    encodingRequire(data[position] != 0, "Noncanonical integer leading zero");
    Integer value;
    import_bits(value, data.begin() + position,
                data.begin() + position + static_cast<std::size_t>(size), 8,
                true);
    position += static_cast<std::size_t>(size);
    return value;
  }
  void domain(std::string_view expected) {
    for (auto c : expected)
      encodingRequire(byte() == static_cast<std::uint8_t>(c),
                      "Wrong segment domain/version");
    encodingRequire(byte() == 0, "Wrong segment domain delimiter");
  }
  Digest digest() {
    Digest result;
    for (auto &value : result)
      value = byte();
    return result;
  }
};
} // namespace pgn::core::detail
