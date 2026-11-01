// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license.
#pragma once
#include <stdint.h>
#ifdef _WIN32
#define PV_API __declspec(dllexport)
#else
#define PV_API
#endif
#ifdef __cplusplus
extern "C" {
#endif
// Public points only: 8 big-endian words per coordinate. All-FF X denotes
// infinity, never a secret scalar. Callers supply Core-validated public points.
typedef struct { uint32_t x[8], y[8]; } PV_Point;
typedef struct { uint32_t low[5], high[5]; } PV_HashRange;
typedef struct {
    uint64_t slot; PV_Point point; uint32_t hash[5];
} PV_Match;
typedef struct {
    PV_Match candidate; uint32_t endomorphism,negated;
} PV_OrbitMatch;
typedef struct {
    uint64_t slots, hashed, infinity, found, first_slot;
    float kernel_ms;
    uint32_t overflow;
} PV_Result;
// No default device, entropy, private scalar, files, network or mining control.
// UUID must name the exact selected CUDA device. Ranges are inclusive,
// disjoint, sorted unsigned HASH160 intervals; zero ranges is invalid.
// The initial N points correspond to seed+i*G, increment=N*G; the Core is
// responsible for that relation. Every returned candidate is UNTRUSTED until
// independently checked by Core against its seed, slot and actual codec.
PV_API int PV_Create(const char* uuid, uint32_t blocks, uint32_t threads,
    uint32_t points_per_thread, const PV_Point* initial, const PV_Point* increment,
    const PV_HashRange* ranges, uint32_t range_count, uint32_t result_capacity,
    void** handle, char* error, uint32_t error_capacity);
// At most64 GPU rounds per launch; no per-candidate host callback. State walks
// forward only on a fully successful launch. Overflow poisons the handle and
// reports failure, never successful partial coverage or silently lost matches.
PV_API int PV_Run(void* handle, uint32_t rounds, PV_Match* matches,
    uint32_t match_capacity, PV_Result* result, char* error, uint32_t error_capacity);
// Same explicit public inputs, six real hash branches per finite base point.
// Result.slots/infinity count BASE points; Result.hashed counts address hashes,
// not a promise of globally distinct keys. Orbit metadata must be verified and
// reconstructed by genuine Core, never treated as a plain additive tweak.
PV_API int PV_CreateOrbit(const char* uuid, uint32_t blocks, uint32_t threads,
    uint32_t points_per_thread, const PV_Point* initial, const PV_Point* increment,
    const PV_HashRange* ranges, uint32_t range_count, uint32_t result_capacity,
    void** handle, char* error, uint32_t error_capacity);
PV_API int PV_RunOrbit(void* handle, uint32_t rounds, PV_OrbitMatch* matches,
    uint32_t match_capacity, PV_Result* result, char* error, uint32_t error_capacity);
PV_API void PV_Destroy(void* handle);
#ifdef __cplusplus
}
#endif
