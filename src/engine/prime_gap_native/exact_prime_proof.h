#pragma once

// Rare gap endpoints above the deterministic MR13 bound need a primality
// proof. The sieve and modular walk remain on the GPU; this stage accepts
// only FLINT's proved result, never its probable-prime predicate.
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include "proof_runtime_config.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

namespace scout {
inline bool needsEndpointProof(uint64_t low, uint64_t high) {
    constexpr uint64_t boundHigh = 0x2be69;
    constexpr uint64_t boundLow = 0x51adc5b22410a5fdULL;
    return high > boundHigh || (high == boundHigh && low >= boundLow);
}

class ExactPrimeProof {
    // FLINT's fmpz is one signed machine word on the shipped 64-bit targets.
    // Initializing it to zero is fmpz_init's documented inline operation.
    using Integer = intptr_t;
    using SetString = int (*)(Integer *, const char *, int);
    using Clear = void (*)(Integer *);
    using IsPrime = int (*)(const Integer *);
    using SetThreads = void (*)(int);
#ifdef _WIN32
    HMODULE module_ = nullptr;
#else
    void *module_ = nullptr;
#endif
    SetString set_ = nullptr;
    Clear clear_ = nullptr;
    IsPrime prime_ = nullptr;

    void *symbol(const char *name) {
#ifdef _WIN32
        return reinterpret_cast<void *>(GetProcAddress(module_, name));
#else
        return dlsym(module_, name);
#endif
    }

    static std::filesystem::path executableDirectory() {
#ifdef _WIN32
        wchar_t text[32768];
        const DWORD count = GetModuleFileNameW(nullptr, text, 32768);
        if (!count || count == 32768)
            throw std::runtime_error("Cannot locate the worker executable");
        return std::filesystem::path(text).parent_path();
#else
        char text[32768];
        const ssize_t count = readlink("/proc/self/exe", text, sizeof(text));
        if (count <= 0 || size_t(count) == sizeof(text))
            throw std::runtime_error("Cannot locate the worker executable");
        return std::filesystem::path(std::string(text, size_t(count))).parent_path();
#endif
    }

  public:
    ExactPrimeProof() = default;
    ExactPrimeProof(const ExactPrimeProof &) = delete;
    ExactPrimeProof &operator=(const ExactPrimeProof &) = delete;
    ~ExactPrimeProof() {
        if (module_) {
#ifdef _WIN32
            FreeLibrary(module_);
#else
            dlclose(module_);
#endif
        }
    }

    void initialize() {
        if (module_)
            return;
        static_assert(sizeof(Integer) == 8,
                      "The bundled exact proof runtime requires a 64-bit host");
        const auto path =
            executableDirectory() / "../lib/proof" / SCOUT_PROOF_SUBDIRECTORY / SCOUT_PROOF_LIBRARY;
        if (!std::filesystem::is_regular_file(path) ||
            std::filesystem::file_size(path) != SCOUT_PROOF_LIBRARY_BYTES)
            throw std::runtime_error("Exact proof runtime is missing or has the wrong size");
#ifdef _WIN32
        module_ =
            LoadLibraryExW(path.lexically_normal().c_str(), nullptr,
                           LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
#else
        module_ = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
        if (!module_)
            throw std::runtime_error("Cannot load the bundled exact proof runtime");
        set_ = reinterpret_cast<SetString>(symbol("fmpz_set_str"));
        clear_ = reinterpret_cast<Clear>(symbol("fmpz_clear"));
        prime_ = reinterpret_cast<IsPrime>(symbol("fmpz_is_prime"));
        const auto threads = reinterpret_cast<SetThreads>(symbol("flint_set_num_threads"));
        if (!set_ || !clear_ || !prime_ || !threads)
            throw std::runtime_error("Exact proof runtime has an incompatible ABI");
        threads(1);
    }

    bool prime(const std::string &decimal) {
        initialize();
        Integer value = 0;
        if (set_(&value, decimal.c_str(), 10) != 0) {
            clear_(&value);
            throw std::runtime_error("Invalid exact proof integer");
        }
        const int decision = prime_(&value);
        clear_(&value);
        if (decision != 0 && decision != 1)
            throw std::runtime_error("Exact proof did not decide primality");
        return decision == 1;
    }
};
} // namespace scout
