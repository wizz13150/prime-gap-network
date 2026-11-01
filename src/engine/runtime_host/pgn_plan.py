"""Actual Native memory/threshold plans; templates and legacy fixed800 plans are refused."""

from pathlib import Path
import json, subprocess
from pgn_native_contract import (
    module_metadata,
    file_sha,
    bind_plan,
    require_new_configuration,
)


def query_plan(
    worker, start, length, environment, child_options, gap_min=960, threshold_pin=None,
    plan_provider=None,
):
    meta = module_metadata(worker)
    before = meta["image_sha256"]
    command = [
        str(worker),
        "--plan",
        "--start",
        str(start),
        "--length",
        str(length),
        "--device",
        "0",
        "--gap-min",
        str(gap_min),
    ]
    raw = plan_provider(command) if plan_provider else None
    result = None if raw is not None else subprocess.run(
        command,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        env=environment,
        timeout=30,
        **child_options,
    )
    if result is not None and result.returncode not in (0, 1):
        raise RuntimeError("Native plan query failed: " + result.stderr.strip())
    try:
        if raw is None:
            raw = json.loads(result.stdout)
    except ValueError:
        raise ValueError("Native plan is not valid JSON") from None
    after = file_sha(worker)
    if before != after:
        raise ValueError("Native image changed during plan query")
    return bind_plan(
        raw, meta, after, int(start), int(length), int(gap_min), threshold_pin
    )


def query_endpoints(worker, configuration, environment, child_options):
    from pgn_work_pieces import next_piece
    from pgn_native_contract import piece_fields

    f = require_new_configuration(configuration)
    pin = f["threshold_policy"]
    start, end = int(configuration["start"]), int(configuration["end"])
    probes = []
    cursor = start
    while cursor < end:
        piece = next_piece(pin, cursor, 30)
        if piece.kind == "native":
            probes.append(cursor)
            break
        cursor = piece.end
    last = next_piece(pin, end - 1, 1)
    if last.kind == "native" and end - 1 not in probes:
        probes.append(end - 1)
    plans = []
    for first in probes:
        local = piece_fields(pin, first, first + 1)
        plan = query_plan(
            worker,
            first,
            1,
            environment,
            child_options,
            local["effective_gap_min"],
            pin,
        )
        if not plan["range_valid"]:
            raise ValueError(
                "A physical search endpoint was not admitted: "
                + str(plan.get("reason"))
            )
        plans.append(plan)
    if not plans:
        return {
            "range_valid": True,
            "fits": True,
            "exact_prefix_only": True,
            "reason": "Exact short-fragment coverage; no native allocation required",
        }
    return plans[-1]


def choose_plan(candidates, start, length, query, preferred_worker=None):
    """candidates is [(K,path)]; query is injectable for pure CPU tests."""
    plans = []
    ordered=sorted(candidates, reverse=True)
    if preferred_worker is not None:
        preferred=[entry for entry in ordered if str(entry[1])==str(preferred_worker)]
        if len(preferred)!=1:
            raise ValueError("Resident worker is not in the qualified candidate inventory")
        ordered=preferred+[entry for entry in ordered if entry!=preferred[0]]
    for profile, worker in ordered:
        plan = query(worker, start, length)
        if plan.get("profile_k") != profile:
            raise ValueError("Native binary/profile identity mismatch")
        plans.append((profile, worker, plan))
        if plan.get("range_valid") and plan.get("fits"):
            return {
                "worker": worker,
                "profile_k": profile,
                "length": length,
                "plan": plan,
            }
    possible = [
        (int(plan.get("max_length_fit", 0)), profile, worker, plan)
        for profile, worker, plan in plans
        if plan.get("range_valid")
    ]
    if not possible or max(x[0] for x in possible) <= 0:
        reasons = "; ".join(str(x[2].get("reason", "no fit")) for x in plans)
        raise RuntimeError("No supported profile has a usable exact chunk: " + reasons)
    size, profile, worker, _ = max(possible, key=lambda item: (item[0], item[1]))
    size = min(length, size)
    checked = query(worker, start, size)
    if (
        not checked.get("range_valid")
        or not checked.get("fits")
        or checked.get("profile_k") != profile
    ):
        raise RuntimeError("Native proposed chunk did not pass an exact second plan")
    return {"worker": worker, "profile_k": profile, "length": size, "plan": checked}
