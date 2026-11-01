"""Exact CPU proof for the few range-boundary integers above the MR13 limit."""

from pathlib import Path
import ctypes
import hashlib
import json
import os
import sys
import threading


class PrimeProof:
    def __init__(self, root=None):
        base = (
            Path(root)
            if root is not None
            else (
                Path(sys.executable).parent
                if getattr(sys, "frozen", False)
                else Path(__file__).parent
            )
        )
        self.root = base / "lib" / "proof" / ("windows" if os.name == "nt" else "linux")
        self.library = None
        self.lock = threading.Lock()

    def _initialize(self):
        manifest_path = self.root / "manifest.json"
        if not manifest_path.is_file() or manifest_path.stat().st_size > 32768:
            raise RuntimeError("The bundled exact proof runtime is missing")
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        if manifest.get("schema") != "SCOUT_EXACT_PROOF_RUNTIME_V1":
            raise RuntimeError("Invalid exact proof runtime manifest")
        libraries = manifest.get("libraries", [])
        if not 1 <= len(libraries) <= 16:
            raise RuntimeError("Invalid exact proof library list")
        primary = []
        for item in libraries:
            name = item["file"]
            path = self.root / name
            if (
                Path(name).name != name
                or not path.is_file()
                or path.stat().st_size != item["bytes"]
            ):
                raise RuntimeError("A bundled proof library is missing or changed")
            if hashlib.sha256(path.read_bytes()).hexdigest().upper() != item["sha256"]:
                raise RuntimeError("A bundled proof library fingerprint changed")
            if name.startswith("libflint"):
                primary.append(path)
        if len(primary) != 1:
            raise RuntimeError("Exactly one FLINT proof library is required")
        if os.name == "nt":
            library = ctypes.WinDLL(str(primary[0].resolve()), winmode=0x1100)
        else:
            library = ctypes.CDLL(str(primary[0].resolve()))
        integer = ctypes.POINTER(ctypes.c_int64)
        library.fmpz_set_str.argtypes = [integer, ctypes.c_char_p, ctypes.c_int]
        library.fmpz_set_str.restype = ctypes.c_int
        library.fmpz_clear.argtypes = [integer]
        library.fmpz_clear.restype = None
        library.fmpz_is_prime.argtypes = [integer]
        library.fmpz_is_prime.restype = ctypes.c_int
        library.flint_set_num_threads.argtypes = [ctypes.c_int]
        library.flint_set_num_threads.restype = None
        library.flint_set_num_threads(1)
        self.library = library

    def prime(self, value):
        with self.lock:
            if self.library is None:
                self._initialize()
            integer = ctypes.c_int64(0)
            try:
                if self.library.fmpz_set_str(
                    ctypes.byref(integer), str(int(value)).encode("ascii"), 10
                ):
                    raise ValueError("Invalid exact proof integer")
                decision = self.library.fmpz_is_prime(ctypes.byref(integer))
                if decision not in (0, 1):
                    raise RuntimeError("Primality proof did not return a decision")
                return decision == 1
            finally:
                self.library.fmpz_clear(ctypes.byref(integer))


_proof = PrimeProof()


def is_prime(value):
    return _proof.prime(value)
