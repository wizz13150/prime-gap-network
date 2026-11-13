"""Independent bounded-duty owner and auditor maintenance wake clocks."""
import math
import time


def owner_poll_delay(elapsed, more):
    if type(elapsed) not in (int, float) or not math.isfinite(elapsed) or elapsed < 0:
        raise ValueError('Invalid owner maintenance duration')
    # The callback has its own bounded publication turn. A slow RPC must not
    # create an additional unbounded idle interval after it finally returns.
    # Keep the audit-answer cadence independent.
    return min(5.0, max(1.0 if more else 5.0, elapsed*3.0))


class ReportPoller:
    def __init__(self, owner, audit_lanes=(), *, clock=None):
        self.owner = owner
        self.audit_lanes = audit_lanes
        self.clock = clock or time.monotonic
        self.owner_next = self.audit_next = 0.0
        self.owner_error = self.audit_error = None
        self.owner_started = self.audit_started = None
        self.owner_elapsed = self.audit_elapsed = None

    def due(self):
        return self.clock() >= min(self.owner_next, self.audit_next)

    def poll_owner(self):
        if self.clock() >= self.owner_next:
            began = self.clock()
            self.owner_started = began
            try:
                self.owner.reveal_owner_reports()
                self.owner_error = None
            except Exception as error:
                self.owner_error = str(error)[:400]
            elapsed = self.clock()-began
            self.owner_elapsed = elapsed
            self.owner_started = None
            more = bool(getattr(self.owner, 'owner_poll_more', False)) and not self.owner_error
            self.owner_next = self.clock()+owner_poll_delay(elapsed, more)
        return self.owner_error

    def poll_audits(self):
        if self.clock() >= self.audit_next:
            began = self.clock()
            self.audit_started = began
            errors = []
            for audit in (self.owner, *self.audit_lanes):
                try:
                    audit.reveal_committed()
                    issues=getattr(getattr(audit,'answers',None),'publication_issues',())
                    if issues:
                        errors.append(str(len(issues))+' retained audit answers require recovery; unrelated valid answers remain publishable')
                except Exception as error:
                    errors.append(str(error)[:400])
            self.audit_error = '; '.join(errors)[:400] if errors else None
            self.audit_elapsed = self.clock()-began
            self.audit_started = None
            self.audit_next = self.clock()+5.0
        return self.audit_error

    def run_owner(self, stopping):
        # A slow owner sweep cannot occupy the auditor's wake clock. Both
        # lanes still use the worker's existing lock for each publication.
        while not stopping.is_set():
            self.poll_owner()
            stopping.wait(max(.05, min(1.0, self.owner_next-self.clock())))

    def snapshot(self):
        now = self.clock()
        return {name: dict(active=started is not None,
                           elapsed_seconds=max(0.0, now-started) if started is not None else elapsed,
                           next_in_seconds=max(0.0, due-now), error=error)
                for name, started, elapsed, due, error in
                [('owner', self.owner_started, self.owner_elapsed, self.owner_next, self.owner_error),
                 ('auditors', self.audit_started, self.audit_elapsed, self.audit_next, self.audit_error)]}

    def poll(self):
        # Synchronous adapter for callers without independent lifecycle lanes.
        self.poll_owner()
        self.poll_audits()
        return self.owner_error or self.audit_error
