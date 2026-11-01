"""Bounded authenticated loopback Core transport; credentials never enter events."""
from pathlib import Path
import base64
import http.client
import json
import urllib.parse
import threading
import socket

class CoreRpcError(RuntimeError):
    def __init__(self, code, message):
        self.code = code
        super().__init__(str(message)[:500])

class CoreRpc:
    def __init__(self, port, cookie, genesis, wallet):
        if type(port) is not int or not 0 < port < 65536:
            raise ValueError("Invalid local Core port")
        self.port = port
        self.cookie, self.genesis, self.wallet = Path(cookie), genesis, wallet
        self._connections = set()
        self._lock = threading.Lock()

    def call(self, method, params=None, wallet=False):
        if self.cookie.stat().st_size > 4096:
            raise ValueError("RPC credential file exceeds budget")
        cookie = self.cookie.read_bytes()
        endpoint = "/wallet/" + urllib.parse.quote(self.wallet, safe="") if wallet else "/"
        body = json.dumps(dict(jsonrpc="2.0", id="horizon-worker", method=method, params=params or [])).encode()
        connection = http.client.HTTPConnection("127.0.0.1", self.port, timeout=30)
        with self._lock:
            self._connections.add(connection)
        try:
            connection.request("POST", endpoint, body, {"Authorization": "Basic " + base64.b64encode(cookie.strip()).decode(), "Content-Type": "application/json"})
            response = connection.getresponse()
            raw = response.read(4 * 1024 * 1024 + 1)
            status = response.status
        finally:
            with self._lock:
                self._connections.discard(connection)
            connection.close()
        if len(raw) > 4 * 1024 * 1024:
            raise ValueError("Core reply exceeds worker budget")
        if status in (401, 403):
            raise RuntimeError("Local Core authentication refused")
        reply = json.loads(raw)
        if reply.get("id") != "horizon-worker":
            raise ValueError("Core reply identity mismatch")
        if reply.get("error"):
            raise CoreRpcError(reply["error"].get("code"), reply["error"].get("message", "Core request refused"))
        if status != 200 or "result" not in reply:
            raise RuntimeError("Local Core response unavailable")
        return reply["result"]

    def cancel_all(self):
        with self._lock:
            active = list(self._connections)
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
