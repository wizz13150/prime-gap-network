"""Independent bounded-duty owner and auditor maintenance wake clocks."""
import math
import time


def owner_poll_delay(elapsed, more):
    if type(elapsed) not in (int, float) or not math.isfinite(elapsed) or elapsed < 0:
        raise ValueError('Invalid owner maintenance duration')
    # A cheap unfinished sweep advances once a second. Expensive steps rest
    # for three times their own duration (at most25% callback duty). The audit
    # answer cadence remains separate and is never increased by this choice.
    return max(1.0 if more else 5.0, elapsed*3.0)


class ReportPoller:
    def __init__(self, owner, audit_lanes=(), *, clock=None):
        self.owner = owner
        self.audit_lanes = audit_lanes
        self.clock = clock or time.monotonic
        self.owner_next = self.audit_next = 0.0
        self.owner_error = self.audit_error = None

    def due(self):
        return self.clock() >= min(self.owner_next, self.audit_next)

    def poll(self):
        if self.clock() >= self.owner_next:
            began = self.clock()
            try:
                self.owner.reveal_owner_reports()
                self.owner_error = None
            except Exception as error:
                self.owner_error = str(error)[:400]
            elapsed = self.clock()-began
            more = bool(getattr(self.owner, 'owner_poll_more', False)) and not self.owner_error
            self.owner_next = self.clock()+owner_poll_delay(elapsed, more)
        if self.clock() >= self.audit_next:
            errors = []
            for audit in (self.owner, *self.audit_lanes):
                try:
                    audit.reveal_committed()
                except Exception as error:
                    errors.append(str(error)[:400])
            self.audit_error = '; '.join(errors)[:400] if errors else None
            self.audit_next = self.clock()+5.0
        return self.owner_error or self.audit_error
