"""Bounded authenticated loopback Core transport; credentials never enter events."""
from pathlib import Path
import base64
import http.client
import json
import urllib.parse
import threading
import socket
import time

class CoreRpcError(RuntimeError):
    def __init__(self, code, message):
        self.code = code
        super().__init__(str(message)[:500])

class CoreRpc:
    maximum_idle_connections = 8
    maximum_idle_seconds = 10

    def __init__(self, port, cookie, genesis, wallet):
        if type(port) is not int or not 0 < port < 65536:
            raise ValueError("Invalid local Core port")
        self.port = port
        self.cookie, self.genesis, self.wallet = Path(cookie), genesis, wallet
        self._connections = set()
        self._idle = []
        self._generation = 0
        self._lock = threading.Lock()

    def _take_connection(self):
        with self._lock:
            connection = None
            while self._idle:
                candidate, since = self._idle.pop()
                if time.monotonic() - since < self.maximum_idle_seconds:
                    connection = candidate
                    break
                candidate.close()
            if connection is None:
                connection = http.client.HTTPConnection("127.0.0.1", self.port, timeout=30)
            self._connections.add(connection)
            return connection, self._generation

    def _return_connection(self, connection, generation, reusable):
        with self._lock:
            self._connections.discard(connection)
            if reusable and generation == self._generation and len(self._idle) < self.maximum_idle_connections:
                self._idle.append((connection, time.monotonic()))
                return
        connection.close()

    def call(self, method, params=None, wallet=False):
        if self.cookie.stat().st_size > 4096:
            raise ValueError("RPC credential file exceeds budget")
        with self.cookie.open('rb') as stream:
            cookie = stream.read(4097)
        if len(cookie) > 4096:
            raise ValueError("RPC credential file exceeds budget")
        endpoint = "/wallet/" + urllib.parse.quote(self.wallet, safe="") if wallet else "/"
        body = json.dumps(dict(jsonrpc="2.0", id="horizon-worker", method=method, params=params or [])).encode()
        connection, generation = self._take_connection()
        reusable = False
        try:
            with self._lock:
                if generation != self._generation:
                    raise RuntimeError("Local Core request was cancelled before dispatch")
            connection.request("POST", endpoint, body, {"Authorization": "Basic " + base64.b64encode(cookie.strip()).decode(), "Content-Type": "application/json"})
            response = connection.getresponse()
            raw = response.read(4 * 1024 * 1024 + 1)
            status = response.status
            if len(raw) > 4 * 1024 * 1024:
                raise ValueError("Core reply exceeds worker budget")
            if status in (401, 403):
                raise RuntimeError("Local Core authentication refused")
            reply = json.loads(raw)
            if reply.get("id") != "horizon-worker":
                raise ValueError("Core reply identity mismatch")
            reusable = not response.will_close
            if reply.get("error"):
                raise CoreRpcError(reply["error"].get("code"), reply["error"].get("message", "Core request refused"))
            if status != 200 or "result" not in reply:
                reusable = False
                raise RuntimeError("Local Core response unavailable")
            return reply["result"]
        finally:
            # Never replay an RPC after a broken connection: it may already
            # have created a key or submitted a block before the reply was lost.
            self._return_connection(connection, generation, reusable)

    def audit_snapshot(self, requests):
        from horizon_audit_snapshot import read_snapshot
        return read_snapshot(self, requests, self.genesis)

    def cancel_all(self):
        with self._lock:
            self._generation += 1
            active = list(self._connections) + [item[0] for item in self._idle]
            self._idle.clear()
        for connection in active:
            try:
                if connection.sock is not None:
                    connection.sock.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            connection.close()

    def verify(self):
        if self.call("getblockhash", [0]) != self.genesis:
            raise ValueError("Core genesis differs from paired network")
        return self.call("getpgnstate")
