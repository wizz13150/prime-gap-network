# Building the private testnet preview

## Source layout and provenance

- `src/`: Horizon application, wallet integration, network runtime and UI.
- `wallet/riecoin-core-upstream-v2606/`: the modified Riecoin Core tree, based on upstream commit `809d5f734ff3a7d28c729224c9839d3a92e2e5d5`. Its existing copyright and licence files apply to that component.
- `src/engine/prime_gap_native/`: the exact optimized CUDA input set authenticated by SHA-256 `96c600d3486ded57b400fcd4428e904f6a3743e49f587dc85e93847d9ffdde13` for `SOURCE-MANIFEST.json`. Historical protocol identifiers inside these inputs remain unchanged for compatibility.
- `src/engine/cpu_auditor/gpu_port/` and `shared_service/`: the qualified 68-bit CPU lineage and its resident controller. This is audit computation, not a general 67–95-bit CPU miner.

No Scout console application is required. Retired independent CUDA/CPU prototypes are excluded from this preview snapshot.

## Coherent Linux testnet build

The complete source-build command assembles the UI, Core, computation host,
CPU controller, native images, current-epoch bootstrap, component inventory and
native updater in one folder. Do not combine the output with an old alpha's
Core, configuration, controller or update files.

```sh
sudo apt-get install build-essential cmake ninja-build pkg-config qt6-base-dev \
  libqt6sql6-sqlite libboost-dev libevent-dev libsqlite3-dev libgmp-dev zlib1g-dev \
  python3-venv
python3 -m venv build/runtime-venv
build/runtime-venv/bin/python -m pip install -r tools/requirements-updater.txt \
  -r tools/requirements-engine-host.txt
build/runtime-venv/bin/python tools/build_linux_runtime.py \
  --work-dir build/linux-runtime-work --output build/linux-runtime --jobs 2
./build/linux-runtime/launch-horizon.sh
```

This rebuilds the UI, Core, host, updater and CPU controller from the checkout.
It downloads one SHA256-pinned input archive for the currently qualified native
GPU images, CPU inventory program and their libraries/licences. It rebuilds and
tests the native CPU worker, then freezes its exact build identity into the
controller. It never adopts the archive's old Core, wallet, network settings or
controller. Rebuilding the CUDA images separately still requires the native
image qualification described below.

The launcher clears inherited Snap-specific environment paths. It does not
remove an AppArmor confinement profile. Qt 6 runtime packages remain system
dependencies. Wallet management opens automatically when no wallet is configured.

## Individual Ubuntu application and Core targets

Use a C++20 compiler, CMake, Ninja, Qt 6 development packages (Core, Gui, Widgets, Network, Concurrent and Sql), SQLite3, GMP and zlib. Core additionally needs the dependencies documented in its upstream build guide, including the wallet and cryptography dependencies. The qualified Ubuntu build used GCC 13.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target PGN-Horizon pgn-gap-evidence --parallel 2

cmake -S wallet/riecoin-core-upstream-v2606 -B build-core -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_GUI=OFF -DBUILD_TESTS=OFF \
  -DBUILD_BENCH=OFF -DBUILD_DAEMON=ON -DBUILD_CLI=ON \
  -DENABLE_WALLET=ON -DWITH_ZMQ=OFF -DWITH_USDT=OFF
cmake --build build-core --target riecoind riecoin-cli --parallel 2
```

The Core executable is deployed under the product name `pgn-core`; its deployment manifest must authenticate that exact file. Renaming without updating the manifest is invalid. Wallet creation/opening is performed from Wallet management, not by these build commands.

## Windows application

Use the equivalent Qt 6/SQLite/GMP/zlib development packages with a consistent compiler/runtime. The qualified Windows application used the MSYS2 UCRT64 toolchain. Configure with Ninja and the same CMake application targets. The icon packaging target is part of the application build. Do not mix unrelated Qt DLL and plugin installations when packaging.

## Computation service

With Python 3 and the pinned dependencies in `tools/requirements-engine-host.txt`:

```sh
python -m pip install -r tools/requirements-engine-host.txt
python tools/build_horizon_engine_host.py --output build/engine-host
```

This freezes the computation-only service and its bounded TLS/diagnostic/local-discovery roles. It does not build or package Scout. The CUDA images, exact prime-proof runtime and their authenticated module metadata are separate dependencies.

## Native image qualification

The distributed images are authenticated by their SHA-256 values, source manifest, proof-library manifest and profile metadata. K8/K16/K32/K64 describe supported memory-plan profiles of the qualified image. Rebuilding with another compiler or changing source bytes can change those hashes; it requires explicit correctness and throughput qualification before changing the allowlist. Do not disable verification or introduce an unqualified fallback to make a local build load.

The repository provides source for review and development; binary reproducibility across arbitrary CUDA/compiler environments has not been established. The release qualification document identifies the tested artifacts and limits.

To prepare a native CUDA build, supply an authenticated exact-proof runtime from the matching distribution:

```sh
python tools/build_native_cuda.py --output build/native --proof-runtime PATH_TO_PROOF_RUNTIME --architectures 86 120
```

Use `--dry-run` to authenticate inputs and inspect the commands without compiling. SM120 requires a toolkit that explicitly supports it (CUDA 12.8 or later). The tool preserves the source manifest, enables device LTO and cached resident kernels, and writes module metadata without altering the runtime allowlist. CUDA and proof shared libraries still need to be deployed with their component notices.
