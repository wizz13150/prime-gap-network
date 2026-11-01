#ifndef PGN_EULER68_FUSED_VECTORS_H
#define PGN_EULER68_FUSED_VECTORS_H
#include "euler68_device_vectors.h"
namespace pgn68_fused_vectors {
struct Record { pgn68_vectors::Word96 square[2]; };
constexpr uint32_t Magic = 0x683204f1U;
static_assert(sizeof(Record)==24,"Exact little-endian square-bit oracle layout");
}
#endif
