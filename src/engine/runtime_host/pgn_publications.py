"""Lossless gap publication queue with coalesced progress and work-time rates."""

from collections import deque
import queue, re, threading, time

COUNTER = re.compile(
    r"\[pgn-counters\].*?\btime_ns=(\d+)\b.*?\bpositions=(\d+)\b.*?\btests=(\d+)\b"
)


class Publication(str):
    def __new__(cls, text, samples=()):
        obj = super().__new__(cls, text)
        obj.samples = tuple(samples)
        return obj


class _Packet:
    def __init__(self, lines, counter, sample):
        self.lines = list(lines)
        self.counter = counter
        self.samples = [sample] if sample else []

    def freeze(self):
        return Publication(
            "\n".join(self.lines + ([self.counter] if self.counter else [])), self.samples
        )


class PublicationQueue:
    """Every GAP survives, in order; only superseded progress counters merge."""

    def __init__(self, maxsize=64):
        self.limit = maxsize
        self.cv = threading.Condition()
        self.events = deque()
        self.pending = []
        self.received_counters = 0
        self.coalesced_counters = 0

    def put(self, value, timeout=None):
        until = time.monotonic() + timeout if timeout is not None else None
        with self.cv:
            sample = None
            if isinstance(value, str):
                match = COUNTER.search(value)
                if match:
                    sample = tuple(map(int, match.groups()))
                elif re.search(r"\bGAP merit=", value):
                    self.pending.append(value)
                    return
            if (
                sample
                and self.events
                and isinstance(self.events[-1], _Packet)
                and len(self.events[-1].lines) + len(self.pending) < 2048
            ):
                item = self.events[-1]
                item.lines.extend(self.pending)
                self.pending = []
                item.counter = value
                item.samples.append(sample)
                if len(item.samples) > 8192:
                    item.samples = item.samples[-8192:]
                self.received_counters += 1
                self.coalesced_counters += 1
                self.cv.notify_all()
                return
            needed = 1 if sample or not self.pending else 2
            while len(self.events) + needed > self.limit:
                remaining = None if until is None else until - time.monotonic()
                if remaining is not None and remaining <= 0:
                    raise queue.Full
                self.cv.wait(remaining)
            additions = []
            if sample:
                additions.append(_Packet(self.pending, value, sample))
                self.pending = []
                self.received_counters += 1
            else:
                if self.pending:
                    additions.append(_Packet(self.pending, None, None))
                    self.pending = []
                additions.append(value)
            for item in additions:
                self.events.append(item)
            self.cv.notify_all()

    def get(self, timeout=None):
        until = time.monotonic() + timeout if timeout is not None else None
        with self.cv:
            while not self.events:
                remaining = None if until is None else until - time.monotonic()
                if remaining is not None and remaining <= 0:
                    raise queue.Empty
                self.cv.wait(remaining)
            item = self.events.popleft()
            self.cv.notify_all()
            return item.freeze() if isinstance(item, _Packet) else item

    def empty(self):
        with self.cv:
            return not self.events and not self.pending


class WorkRates:
    """Native publication time, with a new baseline for every worker instance.

    Startup and pauses cannot enter a compute-speed denominator. Session wall
    throughput remains a separate unchanged report/benchmark measurement.
    """

    def __init__(self):
        self.history = deque()
        self.elapsed = 0.0
        self.positions = 0
        self.tests = 0
        self.last = None
        self.current = None
        self.prp_current = None
        self.minimum = None

    def begin_instance(self):
        # A resumed/restarted worker is a fresh measurement epoch. Previous
        # frequencies, replay/error regimes or changed min-gap/K must not
        # dilute its window, even when the physical GPU is unchanged.
        self.history.clear()
        self.elapsed = 0.0
        self.positions = 0
        self.tests = 0
        self.last = None
        self.current = None
        self.prp_current = None

    def begin_piece(self, preserve_window=False, minimum_positions=0, minimum_tests=0):
        if min(minimum_positions, minimum_tests) < 0:
            raise ValueError("Negative native checkpoint counters")
        if not preserve_window:
            self.begin_instance()
        # A restored checkpoint is prior work, not a new computation delta.
        # Ignore pre-restore startup samples, then establish a measured baseline.
        self.last = None
        self.minimum = (minimum_positions, minimum_tests)

    def add(self, samples):
        for stamp, pos, tests in samples:
            if self.minimum is not None:
                # A restored (or zero-work startup) snapshot has no trustworthy
                # compute-start timestamp. Arm the baseline on fresh coverage,
                # then measure the next native work delta without a speed cap.
                if pos <= self.minimum[0] or tests < self.minimum[1]:
                    continue
                self.minimum = None
            if self.last is not None:
                old_t, old_p, old_n = self.last
                dt = (stamp - old_t) / 1e9
                if stamp <= old_t or pos < old_p or tests < old_n:
                    raise ValueError("Native work-rate samples retreated")
                self.elapsed += dt
                self.positions += pos - old_p
                self.tests += tests - old_n
                self.current = (pos - old_p) / dt
                self.prp_current = (tests - old_n) / dt
                self.history.append((self.elapsed, self.positions, self.tests))
                while len(self.history) > 2 and self.history[1][0] < self.elapsed - 60:
                    self.history.popleft()
            elif not self.history or self.history[-1][0] != self.elapsed:
                self.history.append((self.elapsed, self.positions, self.tests))
            self.last = (stamp, pos, tests)

    def view(self):
        if len(self.history) < 2 or self.current is None:
            return {
                "speed_rate": None,
                "average_rate": None,
                "observed_active_seconds": 0.0,
                "compute_tests_rate": None,
                "eta_rate": None,
            }
        last = self.history[-1]
        first = self.history[0]

        def boundary(seconds):
            at = max(first[0], last[0] - seconds)
            before = first
            for after in self.history:
                if after[0] >= at:
                    if after[0] == before[0]:
                        return at, float(after[1]), float(after[2])
                    f = (at - before[0]) / (after[0] - before[0])
                    return (
                        at,
                        before[1] + f * (after[1] - before[1]),
                        before[2] + f * (after[2] - before[2]),
                    )
                before = after
            return last

        window = boundary(60)
        recent = boundary(1)
        seconds = last[0] - window[0]
        span = last[0] - recent[0]
        # Completed descriptors can be published together after asynchronous GPU
        # work. Sub-millisecond publication gaps are not computation intervals.
        # Warm the advertised one-second window instead of shrinking it at start.
        ready = seconds >= 1.0 and span > 0
        return {
            "speed_rate": (last[1] - recent[1]) / span if ready else None,
            "average_rate": (last[1] - window[1]) / seconds if ready else None,
            "observed_active_seconds": min(60.0, seconds),
            "compute_tests_rate": (last[2] - recent[2]) / span if ready else None,
            "eta_rate": (last[1] - window[1]) / seconds if ready else None,
        }
