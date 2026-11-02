# Clean-clone build and closed tests

Configure and build the public source tree with CMake 3.22+ and a C++20 compiler. On Ubuntu, the build uses Qt 6.2+ (Core, Gui, Widgets, Network, Concurrent, Sql), SQLite, GMP and zlib; the Qt offscreen platform and QSQLITE driver must be installed for GUI/SQLite tests. No CUDA toolkit or device query is part of the default all-target build.

~~~sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 1
QT_QPA_PLATFORM=offscreen GSETTINGS_BACKEND=memory ctest --test-dir build --output-on-failure -j1
QT_QPA_PLATFORM=offscreen GSETTINGS_BACKEND=memory ctest --test-dir build --output-on-failure -j6
~~~

The second CTest command exercises concurrent scheduling, including application-mining-start. A standalone pass does not qualify that case. Tests using real devices or existing runtimes are separate explicit runners; do not run those without their documented environment and authority.

CMake generates small deterministic synthetic registries under build/test-fixtures. The local-validation registry allows tests of manual reservation mechanics. The network-policy registry lacks that authority and verifies that manual dispatch remains disabled. The directory fixture includes synthetic actors and the exact coordinates asserted by its test. These fixtures never read data/runtime, production wallets, keys, profiles, or downloaded community data; their provenance URL is a syntax-only test input and is never fetched. Product runtime data remains excluded from source distribution.

Euler96 CPU/GMP oracles include the arithmetic header actually distributed in src/engine/prime_gap_native. Checkpoint replacement/archive tests use the distributed checkpoint_publish.h implementation on each platform. These host tests do not establish GPU execution or complete mining-engine coverage.

This snapshot does not include the old engine/cuda/euler68.cuh, walk_slice_partition.h, or pgn_walk_continuation.h modules. CMake explicitly reports the corresponding legacy tests as unavailable on every platform where their source header is absent; it does not substitute a generic engine. The optimized current candidate walk is a CUDA implementation with a different API and is not qualified by those omitted tests. The source-resume-lineage-verifier test also requires the Win32 source-session/source-mirror implementation and is registered only when that library is available. Its portable verifier library still builds. With the current snapshot on Linux this leaves 128 registered tests rather than the historical 132; four missing coverage areas must remain visible in result reports.

The offscreen fixture verifies and loads the platform plugin from the loaded Qt SDK before creating QApplication. Windows uses qoffscreen.dll, Linux libqoffscreen.so, and macOS libqoffscreen.dylib. A missing or mismatched plugin is an error, never a desktop fallback.

UTC epoch conversion uses QTimeZone::utc(), supported by the minimum Qt API, with the QDateTime time-zone overload. API references: [QTimeZone](https://doc.qt.io/qt-6/qtimezone.html#utc) and [QDateTime](https://doc.qt.io/qt-6/qdatetime.html#fromSecsSinceEpoch).

The application-mining-start phase fixture shortens the UI timer to 25 ms. It compares the resumed interval with the timer’s observed remaining budget, preserving the no fresh 45 s invariant without assuming that a default coarse timer schedules exactly at its nominal interval. The test prints requested/observed/resumed values. See [QTimer accuracy and resolution](https://doc.qt.io/qt-6/qtimer.html#accuracy-and-timer-resolution).
