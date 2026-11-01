"""Build Horizon's authenticated optimized CUDA inputs without a Scout frontend.

Outputs remain unqualified until correctness and performance checks establish
their hashes. This tool never edits the runtime image allowlist.
"""

from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess


SOURCE_ID = "96c600d3486ded57b400fcd4428e904f6a3743e49f587dc85e93847d9ffdde13"


def digest(path):
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--proof-runtime", required=True, type=Path)
    parser.add_argument("--architectures", nargs="+", type=int, choices=(86, 120), default=[86, 120])
    parser.add_argument("--nvcc", default="nvcc")
    parser.add_argument("--host-compiler")
    parser.add_argument("--dry-run", action="store_true")
    arguments = parser.parse_args()
    source = Path(__file__).resolve().parents[1] / "src/engine/prime_gap_native"
    manifest = source / "SOURCE-MANIFEST.json"
    if digest(manifest) != SOURCE_ID:
        raise ValueError("Native input manifest differs from the qualified source")
    for entry in json.loads(manifest.read_text(encoding="utf-8"))["files"]:
        path = source / entry["file"]
        if path.resolve().parent != source.resolve():
            raise ValueError("Native input must be an immediate source file")
        if path.stat().st_size != entry["bytes"] or digest(path) != entry["sha256"].lower():
            raise ValueError("Native input authentication failed: " + entry["file"])

    proof = arguments.proof_runtime.resolve()
    proof_manifest = proof / "manifest.json"
    proof_data = json.loads(proof_manifest.read_text(encoding="utf-8"))
    for entry in proof_data["libraries"]:
        if Path(entry["file"]).name != entry["file"]:
            raise ValueError("Proof library must be a direct child")
        path = proof / entry["file"]
        if path.stat().st_size != entry["bytes"] or digest(path) != entry["sha256"].lower():
            raise ValueError("Exact proof library authentication failed")

    compiler = shutil.which(arguments.nvcc)
    if not compiler and not arguments.dry_run:
        raise ValueError("CUDA Toolkit nvcc was not found")
    compiler = compiler or arguments.nvcc
    if not arguments.dry_run:
        supported = subprocess.run(
            [compiler, "--list-gpu-code"], check=True, capture_output=True, text=True, timeout=30
        ).stdout.split()
        for architecture in arguments.architectures:
            if "sm_" + str(architecture) not in supported:
                raise ValueError("Requested architecture is unsupported by this nvcc")

    output = arguments.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    flags = [
        "-O3", "-DNDEBUG", "--std=c++17", "--threads", "1", "--split-compile", "4",
        "-lineinfo", "-dlto", "-rdc=true", "--cudart", "shared", "-lcudadevrt",
        "-DPGN_RELEASE_ENABLE_PUBLIC_PERIG=0", "-DPGN_DESCRIPTOR_LANES=2",
        "-DPGN_DESCRIPTOR_BLOCKS=64", "-DPGN_EULER96_LAZY", "-DPGN_EULER96",
        "-DPGN_EULER96_FIXED_HIGH10", "-DPGN_PRESIEVE_GROUPS=1", "-DPGN_MARK_FULL_FAST",
        "-DPGN_MARK_BANK_ROTATE", "-DPGN_FUSED_SIEVE", "-DPGN_VERIFY_GAPS",
    ]
    if os.name == "nt":
        flags += ["-Xcompiler=/MT", "-Xcompiler=/utf-8", "-Xlinker=/MANIFEST:EMBED",
                  "-Xlinker=/MANIFESTINPUT:worker.manifest"]
    else:
        flags += ["-Xlinker=-rpath", "-Xlinker=$ORIGIN/../lib", "-ldl"]
    if arguments.host_compiler:
        flags += ["-ccbin", arguments.host_compiler]

    completed = []
    for architecture in dict.fromkeys(arguments.architectures):
        suffix = ".exe" if os.name == "nt" else ""
        image = output / ("pgn68-worker-sm" + str(architecture) + "-k64" + suffix)
        command = [compiler, *flags, "-arch=sm_" + str(architecture),
                   "-DPGN_RELEASE_BUILD_ARCH=" + str(architecture), "search_worker.cu", "-o", str(image)]
        if arguments.dry_run:
            completed.append({"architecture": architecture, "command": command})
            continue
        subprocess.run(command, cwd=source, check=True)
        image_digest = digest(image)
        for profile in (8, 16, 32, 64):
            target = output / ("pgn68-worker-sm" + str(architecture) + "-k" + str(profile) + suffix)
            if target != image:
                shutil.copyfile(image, target)
            metadata = {
                "schema": "PGN_NATIVE_MODULE_METADATA_V1",
                "resident_job_schema": "SCOUT_RESIDENT_JOB_V1",
                "image_sha256": image_digest.upper(),
                "source_manifest_sha256": SOURCE_ID.upper(),
                "gap_runtime_schema": "PGN_NATIVE_GAP_RUNTIME_V1",
                "profile_k": profile,
                "build_arch": architecture,
                "exact_proof_manifest_sha256": digest(proof_manifest).upper(),
            }
            target.with_name(target.name + ".module.json").write_text(json.dumps(metadata, indent=2) + "\n")
        completed.append({"architecture": architecture, "image_sha256": image_digest})
    print(json.dumps({"schema": "PGN_NATIVE_BUILD_V1", "qualified": False,
                      "dry_run": arguments.dry_run, "outputs": completed}, indent=2))


if __name__ == "__main__":
    main()
