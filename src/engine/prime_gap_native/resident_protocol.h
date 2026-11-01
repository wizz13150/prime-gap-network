#pragma once
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include "checkpoint_publish.h"
#ifdef _WIN32
#include <process.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace scout_resident {
// The bounded binary envelope preserves UTF-8 arguments exactly, including
// spaces. The nonce binds each command and receipt to its durable job identity.
struct Request {
    std::string nonce;
    std::vector<std::string> args;
};

inline uint32_t read_u32(std::istream& stream) {
    unsigned char bytes[4];
    if (!stream.read(reinterpret_cast<char*>(bytes), 4))
        throw std::runtime_error("Truncated resident frame");
    return uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8) |
           (uint32_t(bytes[2]) << 16) | (uint32_t(bytes[3]) << 24);
}

inline bool valid_nonce(const std::string& value) {
    return value.size() == 32 &&
           value.find_first_not_of("0123456789abcdef") == std::string::npos;
}


// Read one immutable, bounded snapshot before parsing. A transient Windows
// file-open denial must not be misreported as a corrupt protocol frame.
inline std::string read_frame_bytes(const std::filesystem::path& file) {
    constexpr uint64_t maximum = 1048576 + 96 * 4 + 44;
#ifdef _WIN32
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    for (;;) {
        const HANDLE handle = CreateFileW(file.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            const auto error = GetLastError();
            if ((error == ERROR_ACCESS_DENIED || error == ERROR_SHARING_VIOLATION ||
                 error == ERROR_LOCK_VIOLATION) && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            throw std::runtime_error("Resident command open failed: Win32 " +
                                     std::to_string(error));
        }
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(handle, &size)) {
            const auto error = GetLastError();
            CloseHandle(handle);
            throw std::runtime_error("Resident command size failed: Win32 " +
                                     std::to_string(error));
        }
        if (size.QuadPart < 0 || uint64_t(size.QuadPart) > maximum) {
            CloseHandle(handle);
            throw std::runtime_error("Resident frame byte budget exceeded");
        }
        std::string bytes(size_t(size.QuadPart), '\0');
        size_t offset = 0;
        DWORD error = ERROR_SUCCESS;
        while (offset < bytes.size()) {
            DWORD received = 0;
            if (!ReadFile(handle, bytes.data() + offset,
                          DWORD(bytes.size() - offset), &received, nullptr)) {
                error = GetLastError();
                break;
            }
            if (!received) {
                error = ERROR_HANDLE_EOF;
                break;
            }
            offset += received;
        }
        CloseHandle(handle);
        if (error == ERROR_SUCCESS)
            return bytes;
        if ((error == ERROR_ACCESS_DENIED || error == ERROR_SHARING_VIOLATION ||
             error == ERROR_LOCK_VIOLATION) && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        throw std::runtime_error("Resident command read failed: Win32 " +
                                 std::to_string(error));
    }
#else
    const auto size = std::filesystem::file_size(file);
    if (size > maximum)
        throw std::runtime_error("Resident frame byte budget exceeded");
    std::ifstream input(file, std::ios::binary);
    if (!input)
        throw std::runtime_error("Resident command open failed");
    std::string bytes(size_t(size), '\0');
    if (size && !input.read(bytes.data(), std::streamsize(size)))
        throw std::runtime_error("Resident command read failed");
    return bytes;
#endif
}

inline Request read_request(const std::filesystem::path& file) {
    std::istringstream input(read_frame_bytes(file), std::ios::in | std::ios::binary);
    char magic[8];
    if (!input.read(magic, 8) || std::string(magic, 8) != std::string("PGNJOB1\0", 8))
        throw std::runtime_error("Invalid resident frame");
    const auto count = read_u32(input);
    if (!count || count > 96)
        throw std::runtime_error("Resident argument budget exceeded");
    Request request;
    request.nonce.resize(32);
    if (!input.read(request.nonce.data(), 32) || !valid_nonce(request.nonce))
        throw std::runtime_error("Invalid resident nonce");
    size_t total = 0;
    for (uint32_t index = 0; index < count; ++index) {
        const auto bytes = read_u32(input);
        total += bytes;
        if (!bytes || bytes > 32768 || total > 1048576)
            throw std::runtime_error("Resident argument byte budget exceeded");
        std::string argument(bytes, '\0');
        if (!input.read(argument.data(), bytes) || argument.find('\0') != std::string::npos)
            throw std::runtime_error("Invalid resident argument");
        request.args.push_back(std::move(argument));
    }
    if (input.peek() != std::char_traits<char>::eof())
        throw std::runtime_error("Resident trailing bytes");
    return request;
}

inline std::filesystem::path frame_path(const std::filesystem::path& directory,
                                        uint64_t sequence, const char* suffix) {
    return directory / (std::to_string(sequence) + suffix);
}

inline void result(const std::filesystem::path& directory, uint64_t sequence,
                   const std::string& nonce, int code, uint64_t nanoseconds,
                   size_t cached_bytes) {
    const auto path = frame_path(directory, sequence, ".done.json");
    std::ostringstream output;
#ifdef _WIN32
    const auto pid = _getpid();
#else
    const auto pid = getpid();
#endif
    output << "{\"schema\":\"SCOUT_RESIDENT_JOB_V1\",\"sequence\":" << sequence
           << ",\"nonce\":\"" << nonce << "\",\"exit\":" << code
           << ",\"pid\":" << pid << ",\"wall_ns\":" << nanoseconds
           << ",\"cached_device_bytes\":" << cached_bytes << "}\n";
    const auto bytes = output.str();
    const auto publication = pgn_checkpoint::publish(path.u8string(), bytes.data(),
                                                    uint32_t(bytes.size()));
    if (!publication.ok) {
        pgn_checkpoint::report(publication);
        throw std::runtime_error("Resident receipt publication failed");
    }
}
} // namespace scout_resident
