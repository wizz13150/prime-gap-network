"""Incremental committed-prefix projection; no full-history scan per frame.

Normal progress touches only the changed lower-prime interval. Rollback clears
only that interval, subtracts its bins, and refills bounded rank/recent caches
through visible/threshold indexes. Disk grows with records; RAM does not.
"""

import copy
import math
from bisect import bisect_right
from pgn_threshold_policy import verify_pin


def encoded(number):
    return int(number).to_bytes(16, "big")


def decoded(value):
    return int.from_bytes(value, "big")


class Projection:
    def __init__(self, index, configuration, rebuild=False):
        self.index = index
        self.db = index.db
        self.start = int(configuration["start"])
        self.end = int(configuration["end"])
        self.count = int(
            configuration.get("count_gap", configuration.get("min_gap", 960))
        )
        self.detail = int(
            configuration.get("detail_gap", configuration.get("min_gap", 960))
        )
        pin = configuration.get("threshold_policy")
        if pin:
            verify_pin(pin)
            self.intervals = tuple(
                (
                    int(s["start"]),
                    int(s["end"]),
                    int(s["count_gap"]),
                    int(s["detail_gap"]),
                )
                for s in pin["segments"]
            )
            self.policy_digest = pin["sha256"]
        else:
            self.intervals = (
                (self.start, int(configuration["end"]), self.count, self.detail),
            )
            self.policy_digest = "static"
        self.starts = tuple(s[0] for s in self.intervals)
        self.count_dynamic = len({s[2] for s in self.intervals}) > 1
        self.detail_dynamic = len({s[3] for s in self.intervals}) > 1
        self.dynamic = self.count_dynamic or self.detail_dynamic
        self.limit = max(30, int(configuration.get("report_top", 100)))
        self.target = float(configuration.get("target_merit", 32))
        first = ((self.count + 99) // 100) * 100
        self.thresholds = sorted(
            {
                self.count,
                *[
                    value
                    for value in range(first, first + 600, 100)
                    if value > self.count
                ],
            }
        )
        self.value = None if rebuild else index.meta("projection")
        flags_identity = "LOWER_PRIME_POLICY_V1:" + self.policy_digest
        if (
            self.dynamic
            and self.value
            and self.value.get("flags_identity") != flags_identity
        ):
            self.value = None
        if self.value is None or "threshold_counts" not in self.value:
            progress = index.meta("progress", {})
            prefix = self.start + int(progress.get("positions", 0))
            self.db.execute("UPDATE gaps SET visible=(lo<?)", (encoded(prefix),))
            for begin, end, count, detail in self.intervals:
                self.db.execute(
                    "UPDATE gaps SET count_ok=(gap>=?),detail_ok=(gap>=?) WHERE lo>=? AND lo<?",
                    (count, detail, encoded(begin), encoded(end)),
                )
            value = {
                "prefix": str(prefix),
                "flags_identity": flags_identity,
                "counts": [0] * (len(index.bounds) - 1),
                "count_total": 0,
                "detail_total": 0,
                "threshold_counts": {str(t): 0 for t in self.thresholds},
            }
            for group, count in self.db.execute(
                "SELECT bin,COUNT(*) FROM gaps WHERE visible=1 AND count_ok=1 GROUP BY bin"
            ):
                value["counts"][group] = count
                value["count_total"] += count
            value["detail_total"] = self.db.execute(
                "SELECT COUNT(*) FROM gaps WHERE visible=1 AND detail_ok=1"
            ).fetchone()[0]
            for threshold in self.thresholds:
                value["threshold_counts"][str(threshold)] = self.db.execute(
                    "SELECT COUNT(*) FROM gaps WHERE visible=1 AND gap>=?", (threshold,)
                ).fetchone()[0]
            self.refill(value)
            index.put_meta("projection", value)
            self.db.commit()
            self.value = value

        # Migrate the derived histogram once, from the committed prefix only.
        # Best/Recent are bounded views and cannot supply complete statistics.
        if self.value.get("statistics_target") != self.target:
            value = copy.deepcopy(self.value)
            value.update(
                merit_band_counts={},
                at_or_above_target=0,
                statistics_target=self.target,
            )
            for gap, low in self.db.execute(
                "SELECT gap,lo FROM gaps WHERE visible=1 AND count_ok=1"
            ):
                self.add_merit(value, gap, decoded(low), 1)
            index.put_meta("projection", value)
            self.db.commit()
            self.value = value

        if set(self.value.get("threshold_counts", {})) != {
            str(t) for t in self.thresholds
        }:
            self.value = copy.deepcopy(self.value)
            self.value["threshold_counts"] = {
                str(t): self.db.execute(
                    "SELECT COUNT(*) FROM gaps WHERE visible=1 AND gap>=?", (t,)
                ).fetchone()[0]
                for t in self.thresholds
            }
        if self.value.get("view_cache_version") != 3:
            value = copy.deepcopy(self.value)
            self.refill(value)
            index.put_meta("projection", value)
            self.db.commit()
            self.value = value

    def eligibility(self, low, gap):
        i = bisect_right(self.starts, int(low)) - 1
        if i < 0 or int(low) >= self.intervals[i][1]:
            raise ValueError("Gap lower prime is outside its pinned policy")
        _, _, count, detail = self.intervals[i]
        return gap >= count, gap >= detail

    def add_merit(self, value, gap, low, delta):
        merit = gap / math.log(low)
        if merit >= self.target:
            value["at_or_above_target"] += delta
        else:
            key = str(math.floor(merit))
            value["merit_band_counts"][key] = (
                value["merit_band_counts"].get(key, 0) + delta
            )

    def refill(self, value):
        value["view_cache_version"] = 3
        for name, column in (("count", "count_ok"), ("detail", "detail_ok")):
            value[name + "_top"] = [
                [int(gap), str(decoded(lo)), int(seq)]
                for gap, lo, seq in self.db.execute(
                    "SELECT gap,lo,seq FROM gaps WHERE visible=1 AND "
                    + column
                    + "=1 ORDER BY gap DESC,lo LIMIT ?",
                    (self.limit,),
                )
            ]
        value["recent"] = [
            [int(gap), str(decoded(lo)), int(seq)]
            for gap, lo, seq in self.db.execute(
                "SELECT gap,lo,seq FROM gaps WHERE visible=1 AND count_ok=1 ORDER BY seq DESC LIMIT 30"
            )
        ]

    def inserted(self, low, high, group, sequence):
        if low >= int(self.value["prefix"]):
            return self.value
        value = copy.deepcopy(self.value)
        gap = high - low
        count_ok, detail_ok = self.eligibility(low, gap)
        if count_ok:
            value["counts"][group] += 1
            value["count_total"] += 1
            self.add_merit(value, gap, low, 1)
        if detail_ok:
            value["detail_total"] += 1
        for threshold in self.thresholds:
            if gap >= threshold:
                value["threshold_counts"][str(threshold)] += 1
        self.refill(value)
        return value

    def move(self, prefix):
        old = int(self.value["prefix"])
        if prefix == old:
            return self.value
        value = dict(self.value)
        value["prefix"] = str(prefix)
        left, right = sorted((old, prefix))
        delta = 1 if prefix > old else -1
        cursor = self.db.execute(
            "SELECT gap,bin,lo FROM gaps WHERE lo>=? AND lo<?",
            (encoded(left), encoded(right)),
        )
        first = cursor.fetchone()
        if first is None:
            # No new/removed qualifying row: update only the scalar frontier.
            return value
        value = copy.deepcopy(value)

        def accept(row):
            gap, group, low = row
            count_ok, detail_ok = self.eligibility(decoded(low), gap)
            if count_ok:
                value["counts"][group] += delta
                value["count_total"] += delta
                self.add_merit(value, gap, decoded(low), delta)
            if detail_ok:
                value["detail_total"] += delta
            for threshold in self.thresholds:
                if gap >= threshold:
                    value["threshold_counts"][str(threshold)] += delta

        accept(first)
        for row in cursor:
            accept(row)
        self.db.execute(
            "UPDATE gaps SET visible=? WHERE lo>=? AND lo<?",
            (1 if delta > 0 else 0, encoded(left), encoded(right)),
        )
        self.refill(value)
        return value
