#ifndef PGN_EULER68_DEVICE_VECTORS_H
#define PGN_EULER68_DEVICE_VECTORS_H
#include <cstdint>
namespace pgn68_vectors {
struct Word96 { uint32_t w[3]; };
struct Header { uint32_t magic, count, record_bytes, reserved; };
struct Record {
  Word96 n, a, b, product, exponent, power, euler_residue;
  uint32_t inverse, euler_match, reserved;
};
constexpr uint32_t Magic = 0x68320401U;
static_assert(sizeof(Header) == 16 && sizeof(Record) == 96, "Explicit Windows little-endian fixture layout");
}
#endif
