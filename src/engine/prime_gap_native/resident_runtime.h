#pragma once
#include <algorithm>
#include <cstddef>
#include <vector>

// Reuse host/device storage without changing width-specific arithmetic kernels.
namespace scout_resident {
struct Buffer {
    void* pointer = nullptr;
    size_t bytes = 0;
    unsigned flags = 0;
};

struct Arena {
    bool enabled = false;
    bool flags_ready = false;
    unsigned flags = 0;
    size_t device_cursor = 0;
    size_t host_cursor = 0;
    std::vector<Buffer> device;
    std::vector<Buffer> host;
    std::vector<cudaGraph_t> graphs;
    std::vector<cudaGraphExec_t> executables;
    std::vector<cudaStream_t> streams;
    std::vector<cudaEvent_t> events;

    size_t device_bytes() const {
        size_t bytes = 0;
        for (const auto& buffer : device)
            bytes += buffer.bytes;
        return bytes;
    }

    void begin() {
        device_cursor = 0;
        host_cursor = 0;
    }

    // The completed graph owns its launch objects. Buffers and the CUDA context
    // outlive a job; process closure releases them on pause, stop or failure.
    cudaError_t end() {
        for (auto handle : executables) {
            const auto error = ::cudaGraphExecDestroy(handle);
            if (error != cudaSuccess)
                return error;
        }
        executables.clear();
        for (auto handle : graphs) {
            const auto error = ::cudaGraphDestroy(handle);
            if (error != cudaSuccess)
                return error;
        }
        graphs.clear();
        for (auto handle : events) {
            const auto error = ::cudaEventDestroy(handle);
            if (error != cudaSuccess)
                return error;
        }
        events.clear();
        for (auto handle : streams) {
            const auto error = ::cudaStreamDestroy(handle);
            if (error != cudaSuccess)
                return error;
        }
        streams.clear();
        return cudaSuccess;
    }
};

inline Arena arena;

template <class T>
inline cudaError_t device_allocate(T** output, size_t bytes) {
    if (!arena.enabled)
        return ::cudaMalloc(reinterpret_cast<void**>(output), bytes);
    const auto index = arena.device_cursor++;
    if (index == arena.device.size())
        arena.device.emplace_back();
    auto& buffer = arena.device[index];
    if (buffer.bytes < bytes) {
        if (buffer.pointer) {
            const auto error = ::cudaFree(buffer.pointer);
            if (error != cudaSuccess)
                return error;
            buffer = {};
        }
        const auto error = ::cudaMalloc(&buffer.pointer, bytes);
        if (error != cudaSuccess)
            return error;
        buffer.bytes = bytes;
    }
    *output = static_cast<T*>(buffer.pointer);
    return cudaSuccess;
}

template <class T>
inline cudaError_t host_allocate(T** output, size_t bytes, unsigned flags) {
    if (!arena.enabled)
        return ::cudaHostAlloc(reinterpret_cast<void**>(output), bytes, flags);
    const auto index = arena.host_cursor++;
    if (index == arena.host.size())
        arena.host.emplace_back();
    auto& buffer = arena.host[index];
    if (buffer.bytes < bytes || buffer.flags != flags) {
        if (buffer.pointer) {
            const auto error = ::cudaFreeHost(buffer.pointer);
            if (error != cudaSuccess)
                return error;
            buffer = {};
        }
        const auto error = ::cudaHostAlloc(&buffer.pointer, bytes, flags);
        if (error != cudaSuccess)
            return error;
        buffer.bytes = bytes;
        buffer.flags = flags;
    }
    *output = static_cast<T*>(buffer.pointer);
    return cudaSuccess;
}

inline cudaError_t memory_info(size_t* free, size_t* total) {
    const auto error = ::cudaMemGetInfo(free, total);
    if (error == cudaSuccess && arena.enabled) {
        // A new job can reuse this owner's held buffers, not only unallocated VRAM.
        const auto held = arena.device_bytes();
        *free = held > *total - *free ? *total : *free + held;
    }
    return error;
}

inline cudaError_t device_flags(unsigned flags) {
    if (arena.enabled && arena.flags_ready)
        return arena.flags == flags ? cudaSuccess : cudaErrorInvalidValue;
    const auto error = ::cudaSetDeviceFlags(flags);
    if (error == cudaSuccess && arena.enabled) {
        arena.flags_ready = true;
        arena.flags = flags;
    }
    return error;
}

inline cudaError_t graph_create(cudaGraph_t* output, unsigned flags) {
    const auto error = ::cudaGraphCreate(output, flags);
    if (error == cudaSuccess && arena.enabled)
        arena.graphs.push_back(*output);
    return error;
}

template <class... Arguments>
inline cudaError_t graph_instantiate(cudaGraphExec_t* output, Arguments... arguments) {
    const auto error = ::cudaGraphInstantiate(output, arguments...);
    if (error == cudaSuccess && arena.enabled)
        arena.executables.push_back(*output);
    return error;
}

inline cudaError_t stream_create(cudaStream_t* output, unsigned flags) {
    const auto error = ::cudaStreamCreateWithFlags(output, flags);
    if (error == cudaSuccess && arena.enabled)
        arena.streams.push_back(*output);
    return error;
}

inline cudaError_t event_create(cudaEvent_t* output) {
    const auto error = ::cudaEventCreate(output);
    if (error == cudaSuccess && arena.enabled)
        arena.events.push_back(*output);
    return error;
}
} // namespace scout_resident

// Host allocation wrappers preserve the production kernels and allocation order.
// A CUDA error closes the process; it never dispatches another job or attempts
// further CUDA cleanup calls on a failed context.
#define cudaMalloc scout_resident::device_allocate
#define cudaHostAlloc scout_resident::host_allocate
#define cudaMemGetInfo scout_resident::memory_info
#define cudaSetDeviceFlags scout_resident::device_flags
#define cudaGraphCreate scout_resident::graph_create
#define cudaGraphInstantiate scout_resident::graph_instantiate
#define cudaStreamCreateWithFlags scout_resident::stream_create
#define cudaEventCreate scout_resident::event_create
