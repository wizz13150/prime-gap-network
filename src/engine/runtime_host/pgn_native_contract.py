"""Release67..95 Python/Native threshold and checkpoint contract.
No Driver import. Native observations are bound by query_plan after execution.
Logical ranges are lazy; each native child stays within one high word and policy interval.
"""

from pathlib import Path
import os
import hashlib, json, re, struct
from pgn_threshold_policy import pin_range, verify_pin, at_position
from pgn_artifact_lease import file_digest

PROTOCOL = "PGN_NATIVE_GAP_RUNTIME_V1"
MODULE_SCHEMA = "PGN_NATIVE_MODULE_METADATA_V1"
LOW = 1 << 66
HIGH = 1 << 95
MAX_GAP = 2147483647
from pgn_threshold_pointwise import MINIMUM_GAP as MINIMUM_POLICY_GAP


def logical_range(start, end):
    start, end = int(start), int(end)
    if not LOW <= start < end <= HIGH:
        raise ValueError("Require2^66 <= START < END <=2^95 (67..95-bit range)")
    return start, end


def admitted_range(start, end):
    start, end = logical_range(start, end)
    if start >> 64 != (end - 1) >> 64:
        raise ValueError("A native child must stay within one high word")
    return start, end


def requested_gap(value):
    if str(value).strip().lower() == "auto":
        return "auto"
    if isinstance(value, bool):
        raise ValueError(f"MIN_GAP is Auto or integer{MINIMUM_POLICY_GAP}..2147483647")
    text = str(value).strip()
    if not text.isdecimal() or not MINIMUM_POLICY_GAP <= int(text) <= MAX_GAP:
        raise ValueError(f"MIN_GAP is Auto or integer{MINIMUM_POLICY_GAP}..2147483647")
    return int(text)


def override_gap(value):
    if value is None or value == "":
        return None
    gap = requested_gap(value)
    if gap == "auto":
        raise ValueError(
            f"COUNT_GAP/DETAIL_GAP are blank or integer{MINIMUM_POLICY_GAP}..2147483647"
        )
    return gap


def fields(start, end, minimum="auto", count=None, detail=None, pin=None):
    start, end = logical_range(start, end)
    if pin is None:
        pin = pin_range(
            start,
            end,
            requested_gap(minimum),
            override_gap(count),
            override_gap(detail),
            policy_id="POINTWISE_RETAINED_MERIT_20_5",
        )
    else:
        verify_pin(pin, start, end)
    for segment in pin["segments"]:
        if any(
            not 30 <= int(segment[key]) <= MAX_GAP
            for key in ("base_gap", "count_gap", "detail_gap", "effective_gap_min")
        ):
            raise ValueError("Pinned threshold exceeds native integer domain")
    result = _fields_for_segment(pin, at_position(pin, start))
    result["emit_min_gap"] = min(
        min(int(s["count_gap"]), int(s["detail_gap"])) for s in pin["segments"]
    )
    return result


def _fields_for_segment(pin, s):
    return {
        "native_protocol": PROTOCOL,
        "threshold_policy": pin,
        "min_gap_requested": pin["requested"],
        "count_gap_requested": pin["count_override"],
        "detail_gap_requested": pin["detail_override"],
        "min_gap": int(s["base_gap"]),
        "count_gap": int(s["count_gap"]),
        "detail_gap": int(s["detail_gap"]),
        "effective_gap_min": int(s["effective_gap_min"]),
        "emit_min_gap": min(int(s["count_gap"]), int(s["detail_gap"])),
    }


def piece_fields(pin, start, end):
    """Bind one physical word/threshold interval to its immutable logical policy."""
    start, end = admitted_range(start, end)
    verify_pin(pin)
    if not int(pin["start"]) <= start < end <= int(pin["end"]):
        raise ValueError("Physical work is outside its logical range")
    segment = at_position(pin, start)
    if end > int(segment["end"]):
        raise ValueError("Physical work crosses a retained-gap transition")
    if (start & ((1 << 64) - 1)) < start % 30:
        raise ValueError(
            "The initial short fragment requires an exact prefix certificate"
        )
    if any(
        not 30 <= int(segment[key]) <= MAX_GAP
        for key in ("base_gap", "count_gap", "detail_gap", "effective_gap_min")
    ):
        raise ValueError("Physical threshold is outside the native integer domain")
    return _fields_for_segment(pin, segment)


def resolve_args(args, end):
    pin = getattr(args, "_threshold_pin", None)
    minimum = getattr(args, "min_gap_requested", args.min_gap)
    count = getattr(args, "count_gap_requested", args.count_gap)
    detail = getattr(args, "detail_gap_requested", args.detail_gap)
    result = fields(args.start, end, minimum, count, detail, pin)
    for k, v in result.items():
        setattr(args, k, v)
    args._threshold_pin = result["threshold_policy"]
    return result


def config_fields(args, end):
    resolve_args(args, end)
    return {
        k: getattr(args, k)
        for k in (
            "native_protocol",
            "threshold_policy",
            "min_gap_requested",
            "count_gap_requested",
            "detail_gap_requested",
            "effective_gap_min",
        )
    }


def reset_new_args(args, values):
    args._threshold_pin = values.get("threshold_policy")
    args.min_gap_requested = values.get(
        "min_gap_requested", values.get("min_gap", "auto")
    )
    args.count_gap_requested = values.get(
        "count_gap_requested", values.get("count_gap")
    )
    args.detail_gap_requested = values.get(
        "detail_gap_requested", values.get("detail_gap")
    )
    args.min_gap = args.min_gap_requested
    args.count_gap = args.count_gap_requested
    args.detail_gap = args.detail_gap_requested


def resume_args(args, configuration):
    require_new_configuration(configuration)
    args._threshold_pin = configuration["threshold_policy"]
    for k in ("min_gap_requested", "count_gap_requested", "detail_gap_requested"):
        setattr(args, k, configuration[k])


def require_new_configuration(config):
    if config.get("native_protocol") != PROTOCOL or not isinstance(
        config.get("threshold_policy"), dict
    ):
        raise ValueError(
            "Legacy session/checkpoint is preserved as read-only history. It cannot resume on the new dynamic-threshold worker; an explicit compatible migration is required."
        )
    f = fields(int(config["start"]), int(config["end"]), pin=config["threshold_policy"])
    for key in ("min_gap", "count_gap", "detail_gap"):
        if int(config[key]) != f[key]:
            raise ValueError(
                "Saved scalar threshold differs from its frozen policy: " + key
            )
    for key in ("min_gap_requested", "count_gap_requested", "detail_gap_requested"):
        if config.get(key) != f[key]:
            raise ValueError("Saved raw threshold request changed: " + key)
    return f


def magic(gap):
    gap = int(gap)
    if not 30 <= gap <= MAX_GAP:
        raise ValueError("Invalid Native checkpoint gap")
    return b"PGNS" + struct.pack("<I", gap)


def checked_checkpoint(data, identity, position=None, tests=None):
    if len(data) != 64:
        raise ValueError("Native checkpoint must be exactly64 bytes")
    if identity.get("native_protocol") != PROTOCOL:
        raise ValueError(
            "Legacy checkpoint is history-only, not a dynamic-worker resume"
        )
    m, low, high, length, p, t, last_low, last_high = struct.unpack("<8s7Q", data)
    start = int(identity["start"])
    if (m, low, high, length) != (
        magic(identity["gap_min"]),
        start & ((1 << 64) - 1),
        start >> 64,
        int(identity["length"]),
    ):
        raise ValueError("Checkpoint/input/effective-gap identity mismatch")
    if (
        not 0 <= p <= length
        or (position is not None and p != int(position))
        or (tests is not None and t != int(tests))
    ):
        raise ValueError("Checkpoint/progress identity mismatch")
    return p, t


def file_sha(path):
    return file_digest(path)


def _digest(value):
    text = str(value).upper()
    if not re.fullmatch("[0-9A-F]{64}", text) or len(set(text)) < 2:
        raise ValueError("Missing/unpaid module fingerprint")
    return text


def module_metadata(worker):
    worker = Path(worker).resolve()
    sidecar = worker.with_name(worker.name + ".module.json")
    if not sidecar.is_file() or sidecar.stat().st_size > 32768:
        raise ValueError("Missing/oversized portable module metadata: " + str(sidecar))
    file_digest(sidecar, release_artifact=True)
    m = json.loads(sidecar.read_text(encoding="utf-8"))
    if m.get("schema") != MODULE_SCHEMA or m.get("gap_runtime_schema") != PROTOCOL:
        raise ValueError("Unbound Native module metadata schema")
    m["image_sha256"] = _digest(m.get("image_sha256"))
    m["source_manifest_sha256"] = _digest(m.get("source_manifest_sha256"))
    if int(m.get("profile_k", 0)) not in (8, 16, 32, 64) or int(
        m.get("build_arch", 0)
    ) not in (
        86,
        120,
    ):
        raise ValueError("Invalid module profile/architecture metadata")
    if file_digest(worker, release_artifact=True) != m["image_sha256"]:
        raise ValueError("Native image differs from module metadata")
    if "exact_proof_manifest_sha256" in m:
        proof_root = (
            worker.parent.parent
            / "lib"
            / "proof"
            / ("windows" if os.name == "nt" else "linux")
        )
        manifest = proof_root / "manifest.json"
        if (
            not manifest.is_file()
            or manifest.stat().st_size > 32768
            or file_digest(manifest, release_artifact=True) != _digest(m["exact_proof_manifest_sha256"])
        ):
            raise ValueError("Exact proof runtime manifest is missing or changed")
        proof = json.loads(manifest.read_text(encoding="utf-8"))
        if (
            proof.get("schema") != "SCOUT_EXACT_PROOF_RUNTIME_V1"
            or not isinstance(proof.get("libraries"), list)
            or not 1 <= len(proof["libraries"]) <= 16
        ):
            raise ValueError("Invalid exact proof runtime manifest")
        for item in proof["libraries"]:
            name = item.get("file", "")
            path = proof_root / name
            if (
                not name
                or Path(name).name != name
                or not path.is_file()
                or path.stat().st_size != item["bytes"]
                or file_digest(path, release_artifact=True) != _digest(item["sha256"])
            ):
                raise ValueError("Bundled exact proof library is missing or changed")
    return m


def bind_plan(raw, metadata, observed_sha, start, length, gap, pin=None):
    gap = int(gap)
    admitted_range(start, int(start) + int(length))
    if (
        raw.get("optimized_domain_schema") != "SCOUT_WORD_DOMAINS_V1"
        or raw.get("optimized_high_begin") != 4
        or raw.get("optimized_high_end") != 1 << 31
        or raw.get("word_tail_clipping") is not True
        or raw.get("minimum_gap_floor") != 30
        or raw.get("bounded_scan") is not True
    ):
        raise ValueError(
            "Search engine is out of date; use the engine bundled with this version"
        )
    if (
        raw.get("schema") != "pgn-memory-plan-1"
        or raw.get("gap_runtime_schema") != PROTOCOL
    ):
        raise ValueError(
            "Native plan lacks the actual dynamic-gap echo/schema; old worker plans are not admitted"
        )
    if type(raw.get("gap_min")) is not int or raw["gap_min"] != gap:
        raise ValueError("Native actual gap_min echo mismatch")
    if int(raw.get("start", -1)) != int(start) or int(raw.get("length", -1)) != int(
        length
    ):
        raise ValueError("Native plan input echo mismatch")
    if int(raw.get("profile_k", 0)) != int(metadata["profile_k"]) or int(
        raw.get("build_arch", 0)
    ) != int(metadata["build_arch"]):
        raise ValueError("Native plan/image profile or architecture mismatch")
    if observed_sha != metadata["image_sha256"]:
        raise ValueError("Native image changed around plan execution")
    if type(raw.get("range_valid")) is not bool or type(raw.get("fits")) is not bool:
        raise ValueError("Native plan admission flags must be actual booleans")
    result = dict(raw)
    result.update(
        observed_worker_sha256=observed_sha,
        worker_source_sha256=metadata["source_manifest_sha256"],
    )
    if pin is not None:
        segment = at_position(pin, start)
        if (
            int(start) + int(length) > int(segment["end"])
            or gap != segment["effective_gap_min"]
        ):
            raise ValueError(
                "Native plan crosses or changes a pinned threshold segment"
            )
        if not raw["range_valid"] or not raw["fits"]:
            return result
        result.update(
            threshold_schema="PGN_THRESHOLD_BINDING_V1",
            threshold_policy_sha256=pin["sha256"],
        )
        from pgn_threshold_policy import require_backend_binding

        binding = require_backend_binding(result, pin, start)
        binding.update(
            image_sha256=observed_sha,
            source_manifest_sha256=metadata["source_manifest_sha256"],
            native_protocol=PROTOCOL,
            profile_k=raw["profile_k"],
            build_arch=raw["build_arch"],
        )
        result["threshold_binding"] = binding
    return result


def rebase_pin(pin, start, end):
    import copy

    verify_pin(pin)
    if pin.get("schema") == "PGN_THRESHOLD_POLICY_V2":
        start, end = logical_range(start, end)
        return pin_range(
            start,
            end,
            pin["requested"],
            pin["count_override"],
            pin["detail_override"],
            policy_id=pin["policy_id"],
        )
    start, end = admitted_range(start, end)
    if (
        len(pin["segments"]) != 1
        or start.bit_length() != pin["segments"][0]["bits"]
        or (end - 1).bit_length() != pin["segments"][0]["bits"]
    ):
        raise ValueError("A policy/bit seam requires an explicit new admitted range")
    p = copy.deepcopy(pin)
    p["start"] = str(start)
    p["end"] = str(end)
    p["segments"][0].update(start=str(start), end=str(end))
    p.pop("sha256", None)
    p["sha256"] = (
        hashlib.sha256(json.dumps(p, sort_keys=True, separators=(",", ":")).encode())
        .hexdigest()
        .upper()
    )
    verify_pin(p, start, end)
    return p
