#pragma once
// Public 68-bit diagnostic. No network, hardware tuning, or device reset.
// Caller selects/validates the device and sets cudaDeviceMapHost before entry.
#include <cuda_runtime.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include "stateful-u160-live.cuh"
#include "perig.cuh"
#if HIGH_64 != 10
#error Release diagnostic corpus is qualified only for HIGH_64=10
#endif

// Root may set this to 0 only if the Windows nvcc capability probe rejects
// device-native int128. PGN correctness still runs; comparison is unavailable,
// never silently replaced with a slower software reference.
#ifndef PGN_RELEASE_ENABLE_PUBLIC_PERIG
#define PGN_RELEASE_ENABLE_PUBLIC_PERIG 1
#endif
#include "diagnostic_data.h"

namespace pgn_release_diag {
constexpr unsigned GuardBytes = 32;
constexpr uint32_t RowSignature = 0x50474e68u;
struct ResultRow {
    uint32_t reference, cold, warm, coldTag, coldMu, warmTag, warmMu, signature;
};
static_assert(sizeof(ResultRow) == 32, "Flat eight-word diagnostic output");

// Independent portable host-integer arithmetic. Values remain below 2^69;
// neither MSVC int128 nor a GPU-produced expected value is used.
struct Host68 {
    uint64_t lo;
    uint32_t hi;
};
inline bool host_ge(Host68 a, Host68 b) {
    return a.hi > b.hi || (a.hi == b.hi && a.lo >= b.lo);
}
inline Host68 host_add(Host68 a, Host68 b) {
    uint64_t lo = a.lo + b.lo;
    return {lo, uint32_t(a.hi + b.hi + (lo < a.lo))};
}
inline Host68 host_sub(Host68 a, Host68 b) {
    return {a.lo - b.lo, uint32_t(a.hi - b.hi - (a.lo < b.lo))};
}
inline Host68 host_addmod(Host68 a, Host68 b, Host68 n) {
    auto t = host_add(a, b);
    return host_ge(t, n) ? host_sub(t, n) : t;
}
inline Host68 host_mulmod(Host68 a, Host68 b, Host68 n) {
    Host68 out{0, 0};
    for (unsigned bit = 0; bit < 68; ++bit) {
        const bool set = bit < 64 ? ((b.lo >> bit) & 1u) : ((b.hi >> (bit - 64)) & 1u);
        if (set)
            out = host_addmod(out, a, n);
        a = host_addmod(a, a, n);
    }
    return out;
}
inline bool host_euler(uint64_t low) {
    const Host68 n{low, 10}, e{low >> 1, 5};
    Host68 out{1, 0}, base{2, 0};
    for (unsigned bit = 0; bit < 67; ++bit) {
        const bool set = bit < 64 ? ((e.lo >> bit) & 1u) : ((e.hi >> (bit - 64)) & 1u);
        if (set)
            out = host_mulmod(out, base, n);
        base = host_mulmod(base, base, n);
    }
    const bool minus = (((low >> 1) ^ (low >> 2)) & 1u) != 0;
    const Host68 wanted = minus ? host_sub(n, {1, 0}) : Host68{1, 0};
    return out.lo == wanted.lo && out.hi == wanted.hi;
}
inline uint32_t host_mu_lower(uint64_t low) {
    const uint64_t last = low | ((uint64_t(1) << 36) - 1);
    const Host68 n{last, 10};
    Host68 remainder{0, 0};
    uint32_t quotient = 0;
    for (int bit = 96; bit >= 0; --bit) {
        remainder = host_add(remainder, remainder);
        if (bit == 96)
            remainder = host_add(remainder, {1, 0});
        const bool ge = host_ge(remainder, n);
        if (ge)
            remainder = host_sub(remainder, n);
        quotient = (quotient << 1) | uint32_t(ge);
    }
    return quotient;
}
inline std::string json_quote(const std::string &s) {
    std::ostringstream o;
    o << '"';
    for (unsigned char c : s) {
        if (c == '"' || c == '\\')
            o << '\\' << char(c);
        else if (c < 32)
            o << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(c) << std::dec;
        else
            o << char(c);
    }
    o << '"';
    return o.str();
}
inline std::string uuid_text(const cudaUUID_t &u) {
    std::ostringstream o;
    o << "GPU-" << std::hex << std::setfill('0');
    for (unsigned i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10)
            o << '-';
        o << std::setw(2) << unsigned(static_cast<unsigned char>(u.bytes[i]));
    }
    return o.str();
}
inline bool all_guard(const std::vector<unsigned char> &b, size_t payload) {
    if (b.size() != payload + 2 * GuardBytes)
        return false;
    for (unsigned i = 0; i < GuardBytes; ++i)
        if (b[i] != 0xc7 || b[GuardBytes + payload + i] != 0xc7)
            return false;
    return true;
}
inline std::string device_json(const cudaDeviceProp &p, int device, int driver, int runtime) {
    std::ostringstream o;
    o << "{\"ordinal\":" << device << ",\"name\":" << json_quote(p.name)
      << ",\"uuid\":" << json_quote(uuid_text(p.uuid)) << ",\"compute_capability\":\"" << p.major
      << '.' << p.minor << "\",\"vram_bytes\":" << uint64_t(p.totalGlobalMem)
      << ",\"driver_api_version\":" << driver << ",\"runtime_version\":" << runtime
      << ",\"actual_clock_sampling\":\"not_collected_here; optional external read-only "
         "log\",\"hardware_tuning\":false}";
    return o.str();
}

__global__ void numeric_kernel(const uint64_t *lows, ResultRow *out, unsigned count) {
    const unsigned i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= count)
        return;
    const uint64_t low = lows[i];
    cached_euler68::State state{};
    ResultRow r{};
    r.cold = uint32_t(cached_euler68::test(low, state));
    r.coldTag = state.bucketTag;
    r.coldMu = state.muLower;
    r.warm = uint32_t(cached_euler68::test(low, state));
    r.warmTag = state.bucketTag;
    r.warmMu = state.muLower;
#if PGN_RELEASE_ENABLE_PUBLIC_PERIG && defined(__CUDA_ARCH__)
    r.reference = uint32_t(pgn_release_perig_public::ciosFermatTest128(low));
#else
    r.reference = 0xffffffffu;
#endif
    r.signature = RowSignature;
    out[i] = r;
}

template <unsigned Backend>
__global__ void throughput_kernel(const uint64_t *lows, uint32_t *out, unsigned count,
                                  unsigned repeats) {
    const unsigned i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= count)
        return;
    cached_euler68::State state{};
    uint32_t sum = 0;
    for (unsigned r = 0; r < repeats; ++r) {
        // Volatile live input prevents hoisting the repeated predicate.
        const uint64_t low = reinterpret_cast<const volatile uint64_t *>(lows)[i];
        if constexpr (Backend == 0) {
#if PGN_RELEASE_ENABLE_PUBLIC_PERIG && defined(__CUDA_ARCH__)
            sum += uint32_t(pgn_release_perig_public::ciosFermatTest128(low));
#endif
        } else {
            if constexpr (Backend == 1)
                state = {}; // exact setup paid every call
            sum += uint32_t(cached_euler68::test(low, state));
        }
    }
    out[i] = sum;
}
} // namespace pgn_release_diag

inline int pgn_release_diagnostic(int device, const char *output_directory, bool benchmark) {
    using namespace pgn_release_diag;
    const auto started = std::chrono::steady_clock::now();
    const std::filesystem::path folder = (output_directory && *output_directory)
                                             ? std::filesystem::u8path(output_directory)
                                             : std::filesystem::path("diagnostics");
    try {
        std::filesystem::create_directories(folder);
        if (std::filesystem::exists(folder / "fermat-selftest.json")) {
            std::fprintf(stderr, "Diagnostic directory already contains fermat-selftest.json; "
                                 "choose a fresh --output directory.\n");
            return 1;
        }
    } catch (const std::exception &e) {
        std::fprintf(stderr, "Cannot create diagnostic directory: %s\n", e.what());
        return 1;
    }
    std::string devInfo = "null";
    auto fail = [&](const char *where, int code) {
        std::ofstream f(folder / "fermat-error.json");
        f << "{\"schema\":\"pgn68-diagnostic-error-1\",\"call\":" << json_quote(where)
          << ",\"code\":" << code << ",\"device\":" << devInfo
          << ",\"later_cuda_cleanup\":false}\n";
        std::fprintf(
            stderr,
            "[SELF-TEST] FAILED %s (code %d). No further CUDA calls are made by this diagnostic.\n",
            where, code);
        return 2;
    };
#define PGN_DIAG_CUDA(call)                                                                        \
    do {                                                                                           \
        const cudaError_t pgn_diag_ec = (call);                                                    \
        if (pgn_diag_ec != cudaSuccess)                                                            \
            return fail(#call, int(pgn_diag_ec));                                                  \
    } while (0)
    cudaDeviceProp property{};
    int driver = 0, runtime = 0;
    PGN_DIAG_CUDA(cudaGetDeviceProperties(&property, device));
    PGN_DIAG_CUDA(cudaDriverGetVersion(&driver));
    PGN_DIAG_CUDA(cudaRuntimeGetVersion(&runtime));
    devInfo = device_json(property, device, driver, runtime);
    std::vector<uint32_t> expected(InputCount);
    unsigned positive = 0;
    for (unsigned i = 0; i < InputCount; ++i) {
        expected[i] = uint32_t(host_euler(Inputs[i]));
        if (expected[i] != IndependentCpuGold[i]) {
            std::fprintf(stderr,
                         "[SELF-TEST] Independent CPU oracle disagrees with frozen integer gold at "
                         "row %u.\n",
                         i);
            return 6;
        }
        positive += expected[i];
    }
    if (!positive || positive == InputCount) {
        std::fprintf(stderr,
                     "[SELF-TEST] Corpus must contain true and false Euler/Jacobi verdicts.\n");
        return 6;
    }
    const size_t inBytes = InputCount * sizeof(uint64_t), outBytes = InputCount * sizeof(ResultRow);
    unsigned char *inputBase = nullptr, *outputBase = nullptr;
    PGN_DIAG_CUDA(cudaMalloc(reinterpret_cast<void **>(&inputBase), inBytes + 2 * GuardBytes));
    PGN_DIAG_CUDA(cudaMalloc(reinterpret_cast<void **>(&outputBase), outBytes + 2 * GuardBytes));
    PGN_DIAG_CUDA(cudaMemset(inputBase, 0xc7, inBytes + 2 * GuardBytes));
    PGN_DIAG_CUDA(cudaMemset(outputBase, 0xc7, outBytes + 2 * GuardBytes));
    PGN_DIAG_CUDA(cudaMemcpy(inputBase + GuardBytes, Inputs, inBytes, cudaMemcpyHostToDevice));
    PGN_DIAG_CUDA(cudaMemset(outputBase + GuardBytes, 0xa5, outBytes));
    numeric_kernel<<<(InputCount + 127) / 128, 128>>>(
        reinterpret_cast<const uint64_t *>(inputBase + GuardBytes),
        reinterpret_cast<ResultRow *>(outputBase + GuardBytes), InputCount);
    PGN_DIAG_CUDA(cudaGetLastError());
    PGN_DIAG_CUDA(cudaDeviceSynchronize());
    std::vector<unsigned char> inputBack(inBytes + 2 * GuardBytes),
        outputBack(outBytes + 2 * GuardBytes);
    PGN_DIAG_CUDA(
        cudaMemcpy(inputBack.data(), inputBase, inputBack.size(), cudaMemcpyDeviceToHost));
    PGN_DIAG_CUDA(
        cudaMemcpy(outputBack.data(), outputBase, outputBack.size(), cudaMemcpyDeviceToHost));
    PGN_DIAG_CUDA(cudaFree(outputBase));
    PGN_DIAG_CUDA(cudaFree(inputBase));
    bool guards = all_guard(inputBack, inBytes) && all_guard(outputBack, outBytes) &&
                  std::memcmp(inputBack.data() + GuardBytes, Inputs, inBytes) == 0;
    unsigned bad = guards ? 0u : 1u;
    std::vector<ResultRow> captured(InputCount);
    std::memcpy(captured.data(), outputBack.data() + GuardBytes, outBytes);
    std::ofstream csv(folder / "fermat-selftest.csv");
    if (!csv) {
        std::fprintf(stderr, "Cannot write diagnostic CSV.\n");
        return 1;
    }
    csv << "row,low_hex,expected_euler,perig,pgn_cold,pgn_warm,cold_tag,cold_mu,warm_tag,warm_mu\n";
    for (unsigned i = 0; i < InputCount; ++i) {
        const auto r = captured[i];
        const uint32_t tag = uint32_t(Inputs[i] >> 36), mu = host_mu_lower(Inputs[i]);
        bool rowOk = r.signature == RowSignature && r.cold == expected[i] &&
                     r.warm == expected[i] && r.coldTag == tag && r.warmTag == tag &&
                     r.coldMu == mu && r.warmMu == mu;
#if PGN_RELEASE_ENABLE_PUBLIC_PERIG
        rowOk = rowOk && r.reference == expected[i];
#endif
        if (!rowOk)
            ++bad;
        csv << i << ",0x" << std::hex << Inputs[i] << std::dec << ',' << expected[i] << ','
            << r.reference << ',' << r.cold << ',' << r.warm << ',' << r.coldTag << ',' << r.coldMu
            << ',' << r.warmTag << ',' << r.warmMu << '\n';
    }
    // Full capture and deliberately wrong last value must be distinguishable.
    auto wrong = captured;
    wrong.back().warm ^= 1u;
    const bool comparatorPoison = wrong.back().warm != expected.back();
    if (!comparatorPoison)
        ++bad;
    std::ofstream self(folder / "fermat-selftest.json");
    if (!self) {
        std::fprintf(stderr, "Cannot write diagnostic JSON.\n");
        return 1;
    }
    self << "{\n\"schema\":\"pgn68-fermat-selftest-1\",\n\"predicate\":" << json_quote(Predicate)
         << ",\n\"domain\":\"odd N=10*2^64+LOW64 (68 bits)\",\n\"device\":" << devInfo
         << ",\n\"public_commit\":" << json_quote(PublicCommit)
         << ",\n\"pgn_source_parent\":"
            "\"A6F62BC90379A40C65F2ADAC5E44EADAF497193272D2F2A409668111CFB96CEA\",\n\"cases\":"
         << InputCount << ",\n\"positive_cases\":" << positive
         << ",\n\"false_cases\":" << InputCount - positive
         << ",\n\"input_immutable\":" << (guards ? "true" : "false")
         << ",\n\"guards_checked\":" << (guards ? "true" : "false")
         << ",\n\"cold_and_warm_state_words_checked\":true,\n\"last_value_poison_checked\":"
         << (comparatorPoison ? "true" : "false")
         << ",\n\"public_perig_compiled\":" << (PGN_RELEASE_ENABLE_PUBLIC_PERIG ? "true" : "false")
         << ",\n\"mismatches\":" << bad << ",\n\"passed\":" << (!bad ? "true" : "false")
         << ",\n\"scope\":\"arithmetic component only, not a primality proof or a gap-search "
            "throughput test\"\n}\n";
    self.close();
    csv.close();
    if (!self || !csv) {
        std::fprintf(stderr, "Diagnostic log write failed.\n");
        return 1;
    }
    if (bad) {
        std::fprintf(stderr, "[SELF-TEST] FAILED %u mismatches. See %s\n", bad,
                     (folder / "fermat-selftest.json").string().c_str());
        return 7;
    }
    std::printf("[SELF-TEST] PASS %u independent integer cases (%u true, %u false); cold/warm "
                "state, input and guards checked.\n",
                InputCount, positive, InputCount - positive);
    if (!benchmark)
        return 0;
#if !PGN_RELEASE_ENABLE_PUBLIC_PERIG
    {
        std::ofstream f(folder / "fermat-benchmark.json");
        f << "{\"schema\":\"pgn68-fermat-benchmark-1\",\"available\":false,\"reason\":\"Exact "
             "public device-int128 reference was not supported by this compiler; no slower "
             "replacement was used\"}\n";
        std::fprintf(stderr, "[FERMAT-COMPARE] Exact public reference unavailable in this build; "
                             "PGN self-test passed.\n");
        return 3;
    }
#else
    constexpr unsigned count = 131072, repeats = 128;
    const size_t benchInBytes = size_t(count) * sizeof(uint64_t),
                 benchOutBytes = size_t(count) * sizeof(uint32_t);
    std::vector<uint64_t> benchInputs(count);
    for (unsigned i = 0; i < count; ++i)
        benchInputs[i] = Inputs[i % InputCount];
    unsigned char *benchInput = nullptr, *benchOutput = nullptr;
    cudaEvent_t begin{}, end{};
    PGN_DIAG_CUDA(
        cudaMalloc(reinterpret_cast<void **>(&benchInput), benchInBytes + 2 * GuardBytes));
    PGN_DIAG_CUDA(
        cudaMalloc(reinterpret_cast<void **>(&benchOutput), benchOutBytes + 2 * GuardBytes));
    PGN_DIAG_CUDA(cudaMemset(benchInput, 0xc7, benchInBytes + 2 * GuardBytes));
    PGN_DIAG_CUDA(cudaMemcpy(benchInput + GuardBytes, benchInputs.data(), benchInBytes,
                             cudaMemcpyHostToDevice));
    PGN_DIAG_CUDA(cudaEventCreate(&begin));
    PGN_DIAG_CUDA(cudaEventCreate(&end));
    const uint64_t queries = uint64_t(count) * repeats;
    double refMs[2]{}, pgnMs[2]{};
    std::vector<unsigned char> benchmarkBack(benchOutBytes + 2 * GuardBytes);
    std::ofstream blocks(folder / "fermat-benchmark-blocks.csv");
    if (!blocks) {
        std::fprintf(stderr, "Cannot write benchmark CSV.\n");
        return 1;
    }
    blocks << "mode,order,implementation,queries,gpu_milliseconds\n";
    for (unsigned mode = 0; mode < 2; ++mode) {
        const unsigned order[4] = {0, mode + 1, mode + 1, 0};
        for (unsigned step = 0; step < 4; ++step) {
            const unsigned which = order[step];
            PGN_DIAG_CUDA(cudaMemset(benchOutput, 0xc7, benchOutBytes + 2 * GuardBytes));
            PGN_DIAG_CUDA(cudaMemset(benchOutput + GuardBytes, 0xa5, benchOutBytes));
            PGN_DIAG_CUDA(cudaEventRecord(begin));
            if (which == 0)
                throughput_kernel<0><<<(count + 127) / 128, 128>>>(
                    reinterpret_cast<const uint64_t *>(benchInput + GuardBytes),
                    reinterpret_cast<uint32_t *>(benchOutput + GuardBytes), count, repeats);
            else if (which == 1)
                throughput_kernel<1><<<(count + 127) / 128, 128>>>(
                    reinterpret_cast<const uint64_t *>(benchInput + GuardBytes),
                    reinterpret_cast<uint32_t *>(benchOutput + GuardBytes), count, repeats);
            else
                throughput_kernel<2><<<(count + 127) / 128, 128>>>(
                    reinterpret_cast<const uint64_t *>(benchInput + GuardBytes),
                    reinterpret_cast<uint32_t *>(benchOutput + GuardBytes), count, repeats);
            PGN_DIAG_CUDA(cudaGetLastError());
            PGN_DIAG_CUDA(cudaEventRecord(end));
            PGN_DIAG_CUDA(cudaEventSynchronize(end));
            float ms = 0;
            PGN_DIAG_CUDA(cudaEventElapsedTime(&ms, begin, end));
            PGN_DIAG_CUDA(cudaMemcpy(benchmarkBack.data(), benchOutput, benchmarkBack.size(),
                                     cudaMemcpyDeviceToHost));
            if (!all_guard(benchmarkBack, benchOutBytes) || !(ms > 0))
                return fail("benchmark guard/timing", 7);
            for (unsigned i = 0; i < count; ++i) {
                uint32_t got = 0;
                std::memcpy(&got, benchmarkBack.data() + GuardBytes + size_t(i) * 4, 4);
                if (got != expected[i % InputCount] * repeats)
                    return fail("benchmark numeric output", 7);
            }
            if (which == 0)
                refMs[mode] += ms;
            else
                pgnMs[mode] += ms;
            blocks << (mode ? "warm" : "cold") << ',' << step << ','
                   << (which == 0 ? "public_perig" : "pgn") << ',' << queries << ','
                   << std::setprecision(9) << ms << '\n';
        }
    }
    std::vector<unsigned char> inputAfter(benchInBytes + 2 * GuardBytes);
    PGN_DIAG_CUDA(
        cudaMemcpy(inputAfter.data(), benchInput, inputAfter.size(), cudaMemcpyDeviceToHost));
    if (!all_guard(inputAfter, benchInBytes) ||
        std::memcmp(inputAfter.data() + GuardBytes, benchInputs.data(), benchInBytes))
        return fail("benchmark input changed", 7);
    PGN_DIAG_CUDA(cudaEventDestroy(end));
    PGN_DIAG_CUDA(cudaEventDestroy(begin));
    PGN_DIAG_CUDA(cudaFree(benchOutput));
    PGN_DIAG_CUDA(cudaFree(benchInput));
    const double hostWall =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::ofstream f(folder / "fermat-benchmark.json");
    if (!f)
        return fail("benchmark JSON write", 1);
    f << std::setprecision(12)
      << "{\n\"schema\":\"pgn68-fermat-benchmark-1\",\n\"available\":true,\n\"device\":" << devInfo
      << ",\n\"predicate\":" << json_quote(Predicate)
      << ",\n\"source_reference_commit\":" << json_quote(PublicCommit)
      << ",\n\"order_per_mode\":\"reference,PGN,PGN,reference\",\n\"queries_per_identity_per_"
         "mode\":"
      << queries * 2 << ",\n\"reference_cold_gpu_ms\":" << refMs[0]
      << ",\n\"pgn_cold_gpu_ms\":" << pgnMs[0] << ",\n\"reference_warm_gpu_ms\":" << refMs[1]
      << ",\n\"pgn_warm_gpu_ms\":" << pgnMs[1] << ",\n\"cold_speedup\":" << refMs[0] / pgnMs[0]
      << ",\n\"warm_speedup\":" << refMs[1] / pgnMs[1]
      << ",\n\"clock_normalization\":false,\n\"hardware_tuning\":false,\n\"numeric_outputs_and_"
         "guards_checked_after_every_launch\":true,\n\"host_total_seconds\":"
      << hostWall
      << ",\n\"scope\":\"component GPU-event arithmetic timings, not Gpos/s, Native search speed, "
         "or a promised ratio on another card\"\n}\n";
    f.close();
    blocks.close();
    if (!f || !blocks)
        return fail("benchmark final log write", 1);
    std::printf(
        "[FERMAT-COMPARE] Public Perig vs PGN: cold %.3fx, warm %.3fx (GPU arithmetic only).\n",
        refMs[0] / pgnMs[0], refMs[1] / pgnMs[1]);
    std::printf("[FERMAT-COMPARE] Logs: %s\n", folder.string().c_str());
    return 0;
#endif
#undef PGN_DIAG_CUDA
}
