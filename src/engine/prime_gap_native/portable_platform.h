#pragma once
#include <cstdint>
#include <atomic>
#include <chrono>
#include <thread>
#include <string>
#include <system_error>
#include <filesystem>
#include <stdexcept>
namespace pgn_platform {
inline uint64_t observedNs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count());
}
inline void sleepMs(unsigned milliseconds) {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}
inline void memoryBarrier() {
    std::atomic_thread_fence(std::memory_order_seq_cst);
}
inline bool exists(const std::string &file) {
    if (file.empty())
        return false;
    std::error_code ec;
    const bool found = std::filesystem::exists(std::filesystem::u8path(file), ec);
    if (ec)
        throw std::runtime_error("Cannot inspect path " + file + ": " + ec.message());
    return found;
}
} // namespace pgn_platform
