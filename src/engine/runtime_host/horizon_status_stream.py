"""Bounded read-only presentation stream for the owned Horizon worker."""
from pathlib import Path
import argparse
import datetime as dt
import hashlib
import json
import math
import time
from pgn_index import read_presentation_snapshot
from pgn_statistics import statistics_state
from pgn_threshold_policy import at_position


def rows(values, limit=30):
    return [dict(gap=int(gap), lower_prime=str(lower), upper_prime=str(int(lower) + int(gap)),
                 merit=format(int(gap) / math.log(int(lower)), ".17g"))
            for gap, lower, sequence in values[:limit]]


def size_label(number):
    for scale, label in ((10**18, "E"), (10**15, "P"), (10**12, "T"), (10**9, "G"), (10**6, "M"), (10**3, "K")):
        if number >= scale:
            whole, fraction = divmod(number, scale)
            suffix = ("." + str(fraction * 100 // scale).zfill(2).rstrip("0")) if fraction * 100 // scale else ""
            return str(whole) + suffix + " " + label
    return str(number)


def active_position_fields(pin, position):
    """Display the threshold actually pinned at the first unfinished position."""
    position = int(position)
    threshold = at_position(pin, position)
    return dict(bits=position.bit_length(), minimum_gap=threshold["effective_gap_min"],
                count_minimum_gap=threshold["count_gap"], detail_minimum_gap=threshold["detail_gap"],
                active_segment_start=threshold["start"], active_segment_end=threshold["end"])


def ticket_frame(folder, sequence=1, ticket_number=None):
    folder = Path(folder)
    from horizon_ticket_archive import frame as archived_frame
    archived = archived_frame(folder)
    if archived is not None:
        archived["local_sequence"] = sequence
        # A durable report is available, but is not a fresh running sample.
        archived["source_stale"] = True
        archived["source_age_ns"] = None
        for key in ("speed_rate", "average_rate", "compute_tests_rate", "observed_active_seconds", "eta_rate"):
            archived[key] = None
        if ticket_number is not None:
            archived["title"] = "Ticket " + str(ticket_number)
        return archived
    file = folder / "session.json"
    if file.stat().st_size > 262144:
        raise ValueError("Oversized session metadata")
    meta = json.loads(file.read_bytes())
    cfg = meta["configuration"]
    from horizon_engine_capabilities import qualified_presentation_identity
    chunk = meta.get("current_chunk") or ((meta.get("closed_chunks") or [{}])[-1])
    if int(cfg["end"]) - int(cfg["start"]) > 32 and not qualified_presentation_identity(cfg,chunk.get("identity") or {}):
        raise ValueError("No qualified optimized worker identity for this session")
    snapshot = read_presentation_snapshot(folder / "session-index.sqlite3")
    configuration_hash = hashlib.sha256(json.dumps(cfg, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    if snapshot["binding"]["configuration_hash"] != configuration_hash:
        raise ValueError("Session and statistics have different identities")
    progress, projection = snapshot["progress"], snapshot["projection"]
    start, end = int(cfg["start"]), int(cfg["end"])
    positions = int(progress["positions"])
    if not 0 <= positions <= end - start or int(projection["prefix"]) != start + positions:
        raise ValueError("Statistics frontier does not match accepted work")
    stats = statistics_state(projection)
    best = rows(projection["count_top"], 100)
    scope = dict(statistics=stats, best=best, recent=rows(projection["recent"], 100))
    pin = cfg["threshold_policy"]
    active_position = min(start + positions, end - 1)
    active_fields = active_position_fields(pin, active_position)
    state = meta["state"]
    phase = dict(RUNNING="Searching", PAUSED="Paused", COMPLETE="Report ready", FAILED="Needs attention").get(state, state.title())
    frame = dict(schema="PGN_HORIZON_MINING_VIEW_V1", ticket_id=cfg["ticket_id"],
                 request_id=cfg.get("network_request_id", ""),
                 title="Ticket " + str(ticket_number) if ticket_number is not None else "Network ticket",
                 local_sequence=sequence, stream_id=hashlib.sha256(str(folder.parent.resolve()).encode()).hexdigest(),
                 start=str(start), end=str(end), **active_fields,
                 target_merit=cfg["target_merit"], positions_label=size_label(end - start),
                 positions=str(positions), positions_total=str(end-start), current=str(start+positions), tests=str(progress["tests"]), phase=phase,
                 validation_state="Calculation recorded; network audits are separate",
                 record_pairs=best, scopes=dict(ticket=scope),
                 observed_utc=dt.datetime.now(dt.timezone.utc).isoformat(),
                 source_stale=True, source_age_ns=None)
    for key in ("speed_rate", "average_rate", "compute_tests_rate", "observed_active_seconds", "eta_rate"):
        frame[key] = None
    status = folder / "engine-status.json"
    if status.is_file() and status.stat().st_size <= 262144:
        live = json.loads(status.read_bytes())
        if live.get("ticket_id") == cfg["ticket_id"]:
            frame["phase"] = dict(STARTING="Starting", SEARCHING="Searching", PAUSED="Paused", COMPUTED="Report ready", FAILED="Needs attention").get(live.get("phase"), phase)
            sample = live.get("sample_monotonic_ns")
            # The producer writes canonical decimal monotonic nanoseconds.
            # Unknown/future samples never become an invented age of zero.
            if (isinstance(sample, str) and sample.isascii() and sample.isdecimal()
                    and len(sample) <= 20 and (len(sample) == 1 or sample[0] != "0")):
                sample = int(sample)
            valid_sample = type(sample) is int and 0 <= sample <= (1 << 64) - 1
            now = time.monotonic_ns()
            lag = now - sample if valid_sample and sample <= now else None
            frame["source_age_ns"] = str(lag) if lag is not None else None
            frame["source_stale"] = lag is None or lag > 5_000_000_000
            frame["compute_time_ns"] = live.get("compute_time_ns")
            instance = chunk.get("instance") or {}
            frame["compute_epoch"] = str(live.get("native_pid", instance.get("pid", ""))) + ":" + str(instance.get("birth", ""))
            # Paused/computed states remain durable presentations, with no rates.
            if live.get("phase") == "SEARCHING" and frame["source_stale"]:
                frame["phase"] = "Connection lost"
            for key in ("speed_rate", "average_rate", "compute_tests_rate", "observed_active_seconds", "eta_rate"):
                frame[key] = live.get(key) if live.get("phase") == "SEARCHING" and not frame.get("source_stale") else None
    return frame


def fresh_observer_frame(state_root):
    """An empty, never-started profile is OFF, not a broken issued ticket.

    Never mask a missing controller once any durable state has been written.
    This read-only status grants no work, rates, progress or ticket identity.
    """
    folder = Path(state_root)
    if folder.is_symlink() or (folder.exists() and (not folder.is_dir() or any(folder.iterdir()))):
        raise ValueError("Controller is missing from an existing nonempty worker profile")
    return dict(schema="PGN_HORIZON_MINING_STATUS_V1", phase="Paused",
                message="Ready · turn on mining to request network work",
                source_stale=True, mining_started=False, allocation_requested=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--observe", action="store_true")
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--ticket-folder", type=Path)
    source.add_argument("--state-root", type=Path)
    parser.add_argument("--once", action="store_true")
    args = parser.parse_args()
    while True:
        try:
            folder, sequence = args.ticket_folder, 1
            if args.state_root:
                controller = args.state_root / "controller.json"
                if not controller.exists():
                    frame = fresh_observer_frame(args.state_root)
                    print(json.dumps(frame, separators=(",", ":")), flush=True)
                    if args.once:
                        return 0
                    time.sleep(1)
                    continue
                if controller.stat().st_size > 262144:
                    raise ValueError("Controller presentation exceeds budget")
                value = json.loads(controller.read_bytes())
                ticket = value.get("work", {}).get("ticket", "")
                if len(ticket) != 64 or any(c not in "0123456789abcdef" for c in ticket):
                    raise ValueError("No issued network ticket is available")
                folder, sequence = args.state_root / "tickets" / ticket, int(value.get("sequence", 1))
                active = args.state_root / "gpu-active.json"
                if active.is_file():
                    if active.stat().st_size > 32768:
                        raise ValueError("Active GPU pointer exceeds budget")
                    current = json.loads(active.read_bytes())
                    identity = current.get("ticket_id", "")
                    if len(identity) != 64 or any(c not in "0123456789abcdef" for c in identity):
                        raise ValueError("Invalid active GPU identity")
                    folder, sequence = args.state_root / "tickets" / identity, int(current["sequence"])
                else:
                    current = None
            frame = ticket_frame(folder, sequence, value.get("work", {}).get("ticket_number") if args.state_root else None)
            if args.state_root and current is not None:
                context = current["context"]
                frame["work_role"] = current["role"]
                if current["role"] == "audit":
                    frame.update(title=context["presentation"]["title"], request_id=context["presentation"]["request_id"], record_pairs=[])
        except (OSError, ValueError, KeyError) as error:
            frame = dict(schema="PGN_HORIZON_MINING_VIEW_UNAVAILABLE_V1", message=str(error)[:400])
        print(json.dumps(frame, separators=(",", ":")), flush=True)
        if args.once:
            return 0
        time.sleep(1)
