# Component source and qualification

These are maintainer build instructions. Installed Horizon does not require
Python, a CUDA compiler, or a separate Scout installation.

## Windows updater

The qualified build used Python 3.12.14 and the versions in
`tools/requirements-updater.txt`. Create a separate environment, install that
file, and pass its `Lib/site-packages` directory to:

```powershell
python tools/build_horizon_updater.py --output build-updater --dependencies .venv-updater/Lib/site-packages
build-updater/dist/horizon-updater/horizon-updater.exe --self-test
```

The generated `UPDATER-SOURCE.json` records source fingerprints and installed
dependency versions. This is source provenance, not a claim of bit-for-bit
reproducibility. Release signing keys are separate and are never included in
the source tree or application.

## Scientific CUDA worker

`src/engine/prime_gap_native/SCIENTIFIC-SOURCE-INPUTS.json` identifies the
complete qualified scientific worker input set. `SOURCE-MANIFEST.json` in the
same directory is retained upstream provenance; the current worker builder
uses the scientific input set, including the composite-page and Beacon
preparation changes.

```powershell
python tools/build_native_cuda.py --output build-native --proof-runtime path/to/EngineRuntime/lib/proof/windows --dry-run
```

Omit `--dry-run` to compile with an installed CUDA toolkit and configured host
compiler. The maintained profiles are SM86/K64 and SM120/K64. The builder does
not create lower-memory profiles by renaming a K64 executable. Rebuilt outputs
are marked unqualified until their correctness and performance are checked;
building SM120 does not constitute a physical SM120 GPU test.

The current network scientific factory is scoped to integers from `2^66`
inclusive to `10^21` exclusive. Arithmetic engine support outside this domain
does not extend that scientific qualification.
