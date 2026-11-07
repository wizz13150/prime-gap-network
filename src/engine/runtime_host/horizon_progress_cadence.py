"""Event-driven presentation gate; never queries a device or invents progress."""
import json

class ProgressCadence:
    def __init__(self):
        self.identity=None
        self.last_fingerprint=None
        self.last_ns=None
        self.cost_ns=0

    def admit(self,event,identity,now_ns):
        phase=event.get('phase')
        identity=(identity,phase)
        try:
            fingerprint=json.dumps([event.get('positions'),event.get('tests'),event.get('statistics')],sort_keys=True,separators=(',',':'),allow_nan=False) if phase=='SEARCHING' else None
        except (TypeError,ValueError,OverflowError):
            # This cache key is only an optimization. The frame still comes
            # from the validated durable reader; malformed callback metadata
            # must not stop computation or become invented UI data.
            fingerprint=None
        changed_identity=identity!=self.identity
        # Control/phase transitions are never throttled, even immediately after
        # a SEARCHING update. A new ticket/audit partition gets its first frame.
        if phase!='SEARCHING' or changed_identity:
            self.identity=identity
            self.last_fingerprint=fingerprint
            self.last_ns=now_ns
            return True
        unchanged=fingerprint==self.last_fingerprint
        # At most10changed frames/s, with backoff to the old2Hz rate when the
        # measured presentation cost is high. Identical inputs keep a1Hz source
        # heartbeat; their fresh source clock must not look disconnected.
        interval=1_000_000_000 if unchanged else max(100_000_000,min(500_000_000,self.cost_ns*50))
        if self.last_ns is not None and now_ns-self.last_ns<interval:return False
        self.last_ns=now_ns
        self.last_fingerprint=fingerprint
        return True

    def observe_cost(self,elapsed_ns):
        if elapsed_ns<0:return
        self.cost_ns=elapsed_ns if not self.cost_ns else (7*self.cost_ns+elapsed_ns)//8
