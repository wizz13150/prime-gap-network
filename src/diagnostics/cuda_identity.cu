// Read-only CUDA enumeration gate. No cudaSetDevice, allocation, kernel,
// clock change, reset or mining operation belongs to this diagnostic.
#include <cuda_runtime.h>
#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    if (argc != 2) return 1;
    int count = 0;
    if (cudaGetDeviceCount(&count) != cudaSuccess || count != 1) {
        std::printf("{\"status\":\"VISIBLE_COUNT_REJECTED\",\"visible_count\":%d}\n", count);
        return 2;
    }
    cudaDeviceProp properties{};
    char pci[64]{};
    if (cudaGetDeviceProperties(&properties, 0) != cudaSuccess ||
        cudaDeviceGetPCIBusId(pci, sizeof(pci), 0) != cudaSuccess) return 3;
    char uuid[41]{};
    std::strcpy(uuid, "GPU-");
    int at = 4;
    for (int i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) uuid[at++] = '-';
        std::sprintf(uuid + at, "%02x", static_cast<unsigned char>(properties.uuid.bytes[i]));
        at += 2;
    }
    const bool accepted = std::strcmp(uuid, argv[1]) == 0;
    std::printf("{\"status\":\"%s\",\"visible_count\":1,\"ordinal\":0,\"uuid\":\"%s\",\"pci\":\"%s\",\"kernels_launched\":0}\n",
        accepted ? "EXACT_UUID_ACCEPTED" : "UUID_REJECTED", uuid, pci);
    return accepted ? 0 : 4;
}
