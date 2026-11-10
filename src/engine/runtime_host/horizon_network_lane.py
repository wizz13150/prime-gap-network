"""FIFO, reentrant serialization of this miner's native publications.

Consensus signatures, parent checks and durable journals are unchanged. A
background publisher cannot immediately reenter ahead of waiting audit/ticket
threads. This does not make a slow Core RPC fast or prove network capacity.
"""
from collections import deque
import threading
import time


class NetworkLane:
    def __init__(self):
        self.condition = threading.Condition()
        self.owner = None
        self.depth = 0
        self.queue = deque()
        self.since = None
        self.name = None

    def acquire(self):
        identity = threading.get_ident()
        with self.condition:
            if self.owner == identity:
                self.depth += 1
                return True
            ticket = object()
            item = (ticket, threading.current_thread().name, time.monotonic())
            self.queue.append(item)
            try:
                while self.owner is not None or self.queue[0][0] is not ticket:
                    self.condition.wait()
                self.queue.popleft()
                self.owner, self.depth = identity, 1
                self.name, self.since = item[1], time.monotonic()
                return True
            except BaseException:
                self.queue.remove(item)
                self.condition.notify_all()
                raise

    def release(self):
        with self.condition:
            if self.owner != threading.get_ident():
                raise RuntimeError('Network lane can only be released by its owner')
            self.depth -= 1
            if not self.depth:
                self.owner = self.name = self.since = None
                self.condition.notify_all()

    def snapshot(self):
        with self.condition:
            now = time.monotonic()
            return dict(owner=self.name, held_seconds=0.0 if self.since is None else max(0.0, now-self.since),
                        waiting=len(self.queue), waiters=[dict(thread=n, seconds=max(0.0, now-t))
                                                         for _, n, t in list(self.queue)[:8]])

    def __enter__(self):
        self.acquire()
        return self

    def __exit__(self, *_):
        self.release()
