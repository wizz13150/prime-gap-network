"""Bounded ordinary-miner service policy; never changes audit validity or rewards."""
from dataclasses import dataclass
import math
import time

SCALE = 1_000_000


@dataclass(frozen=True)
class AuditServiceLimits:
    normal_share: float = 1 / 11  # Two 5% audits per complete search range.
    maximum_share: float = .8
    normal_slots_per_operator: float = 2
    saturated_slots_per_operator: float = 32
    target_wait_seconds: float = 600
    recovery_seconds: float = 60

    def __post_init__(self):
        values = tuple(self.__dict__.values())
        if (any(not math.isfinite(value) for value in values)
                or not 0 < self.normal_share <= self.maximum_share < 1
                or not 0 <= self.normal_slots_per_operator < self.saturated_slots_per_operator
                or self.target_wait_seconds <= 0 or self.recovery_seconds <= 0):
            raise ValueError("Invalid audit service limits")


class AuditPressure:
    """Queue magnitude and age raise service; recovery is deliberately gradual.

    The operator count is an admitted-capacity hint, not an assertion that those
    operators are online. Age still raises pressure when registered workers are
    absent. A capacity estimate is never used to waive an independent audit.
    """
    def __init__(self, limits=AuditServiceLimits(), clock=time.monotonic):
        self.limits, self.clock = limits, clock
        self.share = limits.normal_share
        self.last = None
        self.snapshot = None

    def update(self, compute_slots, audit_operators, oldest_seconds):
        if (type(compute_slots) is not int or not 0 <= compute_slots < 1 << 64
                or type(audit_operators) is not int or not 0 <= audit_operators < 1 << 32
                or type(oldest_seconds) not in (int, float) or not math.isfinite(oldest_seconds) or oldest_seconds < 0):
            raise ValueError("Invalid public audit backlog measurement")
        now = self.clock()
        if self.last is not None and now < self.last:
            raise ValueError("Audit policy clock retreated")
        per_operator = compute_slots / max(1, audit_operators)
        count_pressure = ((per_operator - self.limits.normal_slots_per_operator) /
                          (self.limits.saturated_slots_per_operator - self.limits.normal_slots_per_operator))
        age_pressure = (oldest_seconds / self.limits.target_wait_seconds - 1) / 3
        pressure = min(1., max(0., count_pressure, age_pressure)) if compute_slots else 0.
        target = self.limits.normal_share + pressure * (self.limits.maximum_share - self.limits.normal_share)
        if target >= self.share:
            self.share = target
        elif self.last is not None:
            elapsed = now - self.last
            fraction = -math.expm1(-elapsed / self.limits.recovery_seconds)
            self.share += (target - self.share) * fraction
        self.last = now
        self.snapshot = dict(compute_slots=compute_slots, audit_operators=audit_operators,
                             oldest_seconds=oldest_seconds, observed_monotonic=now)
        return self.weights()

    def weights(self):
        audit = round(self.share * SCALE)
        return {"audit": audit, "search": SCALE - audit}

    def view(self):
        return {"audit_share": self.share, "maximum_audit_share": self.limits.maximum_share,
                "minimum_search_share": 1 - self.limits.maximum_share,
                "measurement": self.snapshot}


class WeightedService:
    """Two work-conserving lanes, charged by measured execution time.

    Idle lanes do not accumulate unlimited priority. Common virtual service is
    removed after each charge, keeping the state independent of queue history.
    A single large task can still delay the other lane: the caller must provide
    safe execution quanta to achieve a bounded response time.
    """
    def __init__(self, weights):
        self.weights = self.checked(weights)
        self.virtual = {"audit": 0, "search": 0}
        self.remainder = {"audit": 0, "search": 0}

    @staticmethod
    def checked(weights):
        if (set(weights) != {"audit", "search"}
                or any(type(value) is not int or value <= 0 for value in weights.values())
                or sum(weights.values()) != SCALE):
            raise ValueError("Positive normalized work weights required")
        return dict(weights)

    def update_weights(self, weights):
        weights = self.checked(weights)
        for role in self.weights:
            residue = self.virtual[role] * self.weights[role] + self.remainder[role]
            self.virtual[role], self.remainder[role] = divmod(residue, weights[role])
        self.weights = weights

    def choose(self, search_ready, audit_ready):
        if not search_ready and not audit_ready:
            return None
        if search_ready and audit_ready:
            audit = self.virtual["audit"] * self.weights["audit"] + self.remainder["audit"]
            search = self.virtual["search"] * self.weights["search"] + self.remainder["search"]
            return "audit" if audit * self.weights["search"] <= search * self.weights["audit"] else "search"
        role = "search" if search_ready else "audit"
        other = "audit" if search_ready else "search"
        self.virtual[other] = self.virtual[role]
        self.remainder[other] = self.remainder[role] * self.weights[other] // self.weights[role]
        return role

    def charge(self, role, elapsed_ns):
        if role not in self.weights or type(elapsed_ns) is not int or elapsed_ns < 0:
            raise ValueError("Measured nonnegative work duration required")
        cost, self.remainder[role] = divmod(elapsed_ns * SCALE + self.remainder[role], self.weights[role])
        self.virtual[role] += cost
        common = min(self.virtual.values())
        self.virtual = {key: value - common for key, value in self.virtual.items()}

    def should_yield(self, role, elapsed_ns, other_ready):
        if not other_ready:
            return False
        if role not in self.weights or type(elapsed_ns) is not int or elapsed_ns < 0:
            raise ValueError("Invalid current service measurement")
        other = "audit" if role == "search" else "search"
        current = self.virtual[role] * self.weights[role] + self.remainder[role] + elapsed_ns * SCALE
        previous = self.virtual[other] * self.weights[other] + self.remainder[other]
        return current * self.weights[other] > previous * self.weights[role]
