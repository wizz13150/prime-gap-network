#include <filesystem>
#pragma once
#ifdef _WIN32
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdio>
#include <string>
namespace pgn_checkpoint {
struct Result {
    bool ok{};
    DWORD error{};
    unsigned attempts{};
    ULONGLONG elapsedMs{};
    const char *stage{};
    std::string destination;
};
using Retry = void (*)(unsigned, DWORD, void *);
inline Result publish(const std::string &path, const void *bytes, DWORD size, DWORD retryMs = 2000,
                      Retry observed = nullptr, void *context = nullptr) {
    const auto began = GetTickCount64();
    Result r;
    r.stage = "empty";
    if (path.empty()) {
        r.ok = true;
        return r;
    }
    const auto pending = path + ".pending";
    r.stage = "create_pending";
    HANDLE h = CreateFileW(std::filesystem::u8path(pending).c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                           nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        r.error = GetLastError();
        return r;
    }
    DWORD written = 0;
    r.stage = "write_pending";
    bool ok = WriteFile(h, bytes, size, &written, nullptr) != 0;
    if (!ok)
        r.error = GetLastError();
    else if (written != size) {
        ok = false;
        r.error = ERROR_WRITE_FAULT;
    }
    if (ok) {
        r.stage = "flush_pending";
        ok = FlushFileBuffers(h) != 0;
        if (!ok)
            r.error = GetLastError();
    }
    const bool closed = CloseHandle(h) != 0;
    const auto closeError = closed ? ERROR_SUCCESS : GetLastError();
    if (ok && !closed) {
        ok = false;
        r.error = closeError;
        r.stage = "close_pending";
    }
    if (!ok) {
        r.elapsedMs = GetTickCount64() - began;
        return r;
    }
    r.stage = "replace";
    for (;;) {
        ++r.attempts;
        if (MoveFileExW(std::filesystem::u8path(pending).c_str(),
                        std::filesystem::u8path(path).c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            r.ok = true;
            r.error = 0;
            break;
        }
        r.error = GetLastError();
        r.elapsedMs = GetTickCount64() - began;
        if ((r.error != ERROR_SHARING_VIOLATION && r.error != ERROR_LOCK_VIOLATION &&
             r.error != ERROR_ACCESS_DENIED) ||
            r.elapsedMs >= retryMs)
            break;
        if (observed)
            observed(r.attempts, r.error, context);
        else if (r.attempts == 1)
            std::fprintf(stderr,
                         "[pgn-checkpoint-publication-retry] stage=replace win32_error=%lu\n",
                         static_cast<unsigned long>(r.error));
        DWORD delay = r.attempts < 6 ? (1U << (r.attempts - 1)) : 32U;
        const auto remaining = retryMs - r.elapsedMs;
        if (delay > remaining)
            delay = DWORD(remaining);
        Sleep(delay);
    }
    r.elapsedMs = GetTickCount64() - began;
    return r;
}
// Completion archives must preserve earlier files and tolerate the same
// short reader locks as publication. Missing source is normal before the
// first periodic checkpoint; other errors retain the source and fail closed.
inline Result moveArchive(const std::string &source, const std::string &target,
                          DWORD retryMs = 2000, Retry observed = nullptr, void *context = nullptr) {
    const auto began = GetTickCount64();
    Result r;
    r.stage = "archive";
    r.destination = target;
    for (;;) {
        ++r.attempts;
        if (MoveFileExW(std::filesystem::u8path(source).c_str(),
                        std::filesystem::u8path(target).c_str(), MOVEFILE_WRITE_THROUGH)) {
            r.ok = true;
            r.error = 0;
            break;
        }
        r.error = GetLastError();
        r.elapsedMs = GetTickCount64() - began;
        if (r.error == ERROR_FILE_NOT_FOUND) {
            r.ok = true;
            r.error = 0;
            r.destination.clear();
            r.stage = "archive_absent";
            break;
        }
        if ((r.error != ERROR_SHARING_VIOLATION && r.error != ERROR_LOCK_VIOLATION &&
             r.error != ERROR_ACCESS_DENIED) ||
            r.elapsedMs >= retryMs)
            break;
        if (observed)
            observed(r.attempts, r.error, context);
        else if (r.attempts == 1)
            std::fprintf(stderr,
                         "[pgn-checkpoint-publication-retry] stage=archive win32_error=%lu\n",
                         static_cast<unsigned long>(r.error));
        DWORD delay = r.attempts < 6 ? (1U << (r.attempts - 1)) : 32U;
        const auto remaining = retryMs - r.elapsedMs;
        if (delay > remaining)
            delay = DWORD(remaining);
        Sleep(delay);
    }
    r.elapsedMs = GetTickCount64() - began;
    return r;
}
inline Result archive(const std::string &source, DWORD retryMs = 2000, Retry observed = nullptr,
                      void *context = nullptr) {
    if (source.empty()) {
        Result r;
        r.ok = true;
        r.stage = "archive_empty";
        return r;
    }
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    const auto stamp =
        (static_cast<unsigned long long>(now.dwHighDateTime) << 32) | now.dwLowDateTime;
    const auto prefix = source + ".completed-" + std::to_string(GetCurrentProcessId()) + "-" +
                        std::to_string(stamp);
    const auto began = GetTickCount64();
    unsigned attempts = 0;
    Result r;
    for (unsigned suffix = 0; suffix < 32; ++suffix) {
        r = moveArchive(source, prefix + "-" + std::to_string(suffix), retryMs, observed, context);
        attempts += r.attempts;
        if (r.error != ERROR_ALREADY_EXISTS && r.error != ERROR_FILE_EXISTS)
            break;
    }
    r.attempts = attempts;
    r.elapsedMs = GetTickCount64() - began;
    return r;
}
inline void report(const Result &r) {
    if (!r.ok || r.attempts > 1)
        std::fprintf(stderr,
                     "[pgn-checkpoint-publication] ok=%u stage=%s win32_error=%lu attempts=%u "
                     "elapsed_ms=%llu retained_on_failure=1 archive_target=%s\n",
                     unsigned(r.ok), r.stage, static_cast<unsigned long>(r.error), r.attempts,
                     r.elapsedMs, r.destination.c_str());
}
} // namespace pgn_checkpoint

#else
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <fcntl.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/fs.h>
#include "portable_platform.h"
namespace pgn_checkpoint {
struct Result {
    bool ok{};
    uint32_t error{};
    unsigned attempts{};
    uint64_t elapsedMs{};
    const char *stage{};
    std::string destination;
};
using Retry = void (*)(unsigned, uint32_t, void *);
inline uint64_t milliseconds() {
    return pgn_platform::observedNs() / 1000000ULL;
}
inline bool syncFd(int fd) {
    int rc;
    do {
        rc = ::fsync(fd);
    } while (rc < 0 && errno == EINTR);
    return rc == 0;
}
inline bool syncDirectory(const std::string &path, Result &r) {
    const auto slash = path.find_last_of('/');
    const std::string directory =
        slash == std::string::npos ? "." : (slash == 0 ? "/" : path.substr(0, slash));
    r.stage = "open_directory";
    const int fd = ::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) {
        r.error = errno;
        return false;
    }
    r.stage = "sync_directory";
    bool ok = syncFd(fd);
    const int syncError = ok ? 0 : errno;
    const int closed = ::close(fd);
    if (!ok)
        r.error = syncError;
    else if (closed < 0) {
        ok = false;
        r.error = errno;
        r.stage = "close_directory";
    }
    return ok;
}
inline Result publish(const std::string &path, const void *bytes, uint32_t size,
                      uint32_t retryMs = 2000, Retry observed = nullptr, void *context = nullptr) {
    (void)retryMs;
    (void)observed;
    (void)context;
    const auto began = milliseconds();
    Result r;
    r.stage = "empty";
    if (path.empty()) {
        r.ok = true;
        return r;
    }
    const std::string pending = path + ".pending";
    r.stage = "create_pending";
    const int fd =
        ::open(pending.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) {
        r.error = errno;
        return r;
    }
    bool ok = true;
    size_t offset = 0;
    r.stage = "write_pending";
    while (offset < size) {
        const ssize_t n = ::write(fd, static_cast<const char *>(bytes) + offset, size - offset);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0) {
            ok = false;
            r.error = n < 0 ? errno : EIO;
            break;
        }
        offset += static_cast<size_t>(n);
    }
    if (ok) {
        r.stage = "flush_pending";
        ok = syncFd(fd);
        if (!ok)
            r.error = errno;
    }
    const int closed = ::close(fd);
    if (ok && closed < 0) {
        ok = false;
        r.error = errno;
        r.stage = "close_pending";
    }
    if (ok) {
        r.stage = "replace";
        ++r.attempts;
        if (::rename(pending.c_str(), path.c_str()) < 0) {
            ok = false;
            r.error = errno;
        }
    }
    if (ok)
        ok = syncDirectory(path, r);
    r.ok = ok;
    r.elapsedMs = milliseconds() - began;
    return r;
}
inline Result moveArchive(const std::string &source, const std::string &target,
                          uint32_t retryMs = 2000, Retry observed = nullptr,
                          void *context = nullptr) {
    (void)retryMs;
    (void)observed;
    (void)context;
    const auto began = milliseconds();
    Result r;
    r.stage = "archive";
    r.destination = target;
    ++r.attempts;
    const long rc = ::syscall(SYS_renameat2, AT_FDCWD, source.c_str(), AT_FDCWD, target.c_str(),
                              RENAME_NOREPLACE);
    if (rc == 0)
        r.ok = syncDirectory(target, r);
    else {
        r.error = errno;
        if (r.error == ENOENT) {
            r.ok = true;
            r.error = 0;
            r.destination.clear();
            r.stage = "archive_absent";
        }
    }
    r.elapsedMs = milliseconds() - began;
    return r;
}
inline Result archive(const std::string &source, uint32_t retryMs = 2000, Retry observed = nullptr,
                      void *context = nullptr) {
    if (source.empty()) {
        Result r;
        r.ok = true;
        r.stage = "archive_empty";
        return r;
    }
    const auto stamp =
        static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                  std::chrono::system_clock::now().time_since_epoch())
                                  .count());
    const auto prefix =
        source + ".completed-" + std::to_string(::getpid()) + "-" + std::to_string(stamp);
    const auto began = milliseconds();
    unsigned attempts = 0;
    Result r;
    for (unsigned suffix = 0; suffix < 32; ++suffix) {
        r = moveArchive(source, prefix + "-" + std::to_string(suffix), retryMs, observed, context);
        attempts += r.attempts;
        if (r.error != EEXIST)
            break;
    }
    r.attempts = attempts;
    r.elapsedMs = milliseconds() - began;
    return r;
}
inline void report(const Result &r) {
    if (!r.ok || r.attempts > 1)
        std::fprintf(stderr,
                     "[pgn-checkpoint-publication] ok=%u stage=%s posix_error=%u attempts=%u "
                     "elapsed_ms=%llu retained_on_failure=%u archive_target=%s\n",
                     unsigned(r.ok), r.stage, r.error, r.attempts,
                     static_cast<unsigned long long>(r.elapsedMs), unsigned(!r.ok),
                     r.destination.c_str());
}
} // namespace pgn_checkpoint
#endif
