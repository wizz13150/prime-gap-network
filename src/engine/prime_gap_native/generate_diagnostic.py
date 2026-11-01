"""Regenerate the checked-in diagnostic constants with independent Python integers.

This command has no compiler, CUDA, GPU, or external archive dependency. The
non-data portion of the checked-in header preserves the upstream reference.
"""
from pathlib import Path
import argparse
import json
import re


def generate(check=False):
    source = Path(__file__).resolve().parent
    fixture = source.parent / "tests/data/euler68.json"
    values = json.loads(fixture.read_text(encoding="utf-8"))
    lows, expected = [], []
    for case in values["cases"]:
        low = int(case["low"])
        n = (values["high_word"] << 64) + low
        wanted = n - 1 if ((low >> 1) ^ (low >> 2)) & 1 else 1
        result = int(pow(2, n >> 1, n) == wanted)
        if result != case["expected"]:
            raise ValueError("Independent Euler/Jacobi fixture mismatch")
        lows.append(low)
        expected.append(result)
    target = source / "diagnostic_data.h"
    text = target.read_text(encoding="utf-8")
    entries = "\n".join("    " + ", ".join(f"0x{x:016x}ULL" for x in lows[i:i+4]) + "," for i in range(0, len(lows), 4))
    truths = "\n".join("    " + ", ".join(str(x)+"u" for x in expected[i:i+16]) + "," for i in range(0, len(expected), 16))
    text, count = re.subn(r"Inputs\[InputCount\]\s*=\s*\{.*?\};", "Inputs[InputCount] = {\n" + entries + "\n};", text, flags=re.S)
    if count != 1:
        raise ValueError("Diagnostic input declaration missing")
    text, count = re.subn(r"IndependentCpuGold\[InputCount\]\s*=\s*\{.*?\};", "IndependentCpuGold[InputCount] = {\n" + truths + "\n};", text, flags=re.S)
    if count != 1:
        raise ValueError("Diagnostic expected-value declaration missing")
    if not check:
        target.write_text(text, encoding="utf-8")
    print(json.dumps({"passed": True, "cases": len(lows), "independent_python_integer_pow": True, "header_written": not check}))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Verify fixtures without writing the header")
    generate(parser.parse_args().check)
