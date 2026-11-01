"""Complete range statistics, independent of the bounded Best/Recent views."""

import math


def statistics_rows(projection, target):
    """Rows are (label, count, section). Merit bands partition counted gaps.

    Consecutive empty merit bands share a single zero row on screen. The saved
    histogram retains each band. Cumulative gap-size counts deliberately overlap.
    """
    rows = [("Larger gaps", projection["count_total"], False),
            ("GAP SIZE · cumulative", None, True)]
    for bound, count in sorted(projection["threshold_counts"].items(), key=lambda p: int(p[0])):
        rows.append((f">= {bound}", count, False))
    rows.append(("MERIT · bands", None, True))
    if projection.get("merit_band_counts") is None:
        rows.append(("Resume to load bands", None, False))
        return rows
    bands = {int(k): v for k, v in projection["merit_band_counts"].items() if v}
    first = min(bands, default=min(20, math.ceil(target)-1))
    last = math.ceil(target)-1
    band = first
    while band <= last:
        end = band + 1
        count = bands.get(band, 0)
        if not count:
            while end <= last and not bands.get(end, 0):
                end += 1
        upper = min(float(end), target)
        rows.append((f"{band:g} <= m < {upper:g}", count, False))
        band = end
    rows.append((f"m >= {target:g}", projection["at_or_above_target"], False))
    return rows


def statistics_state(value):
    """Copy bounded scalar aggregates only; never query all gaps per frame."""
    return {key: value.get(key) for key in
            ("count_total", "threshold_counts", "merit_band_counts", "at_or_above_target", "statistics_target")}


def saved_range_preview(folder, configuration, limit=10):
    """Read the same committed prefix as Resume, without a migration or scan."""
    from pathlib import Path
    from pgn_index import read_metadata_snapshot
    import hashlib
    import json

    snapshot = read_metadata_snapshot(Path(folder) / "session-index.sqlite3")
    binding = snapshot["binding"]
    digest = hashlib.sha256(json.dumps(configuration, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    if binding["configuration_hash"] != digest:
        raise ValueError("Saved preview configuration does not match its index")
    progress = snapshot["progress"]
    positions = int(progress["positions"])
    projection = snapshot["projection"]
    if not 0 <= positions <= int(configuration["end"])-int(configuration["start"]) or int(projection["prefix"]) != int(configuration["start"])+positions:
        raise ValueError("Saved statistics and checkpoint frontiers disagree")
    rows = min(30, max(10, int(limit)))
    recent = projection["recent"][:rows]
    recent_lowers = {r[1] for r in recent}
    records = [r for r in projection["count_top"][:rows] if r[1] not in recent_lowers] + list(reversed(recent))
    statistics = statistics_state(projection)
    if statistics["statistics_target"] != float(configuration.get("target_merit", 32)):
        statistics.update(merit_band_counts=None, at_or_above_target=None)
    return dict(position=positions, tests=int(progress["tests"]), gap_count=int(projection["count_total"]),
                range_statistics=statistics,
                gaps=[(int(gap), int(gap)/math.log(int(low)), int(low)) for gap, low, sequence in records])
