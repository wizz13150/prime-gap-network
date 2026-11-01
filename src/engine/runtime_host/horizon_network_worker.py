"""Owned Horizon worker controller for the isolated two-node coordination pilot.

The --pilot-coordination flag permits zero-issuance coordination records. It is
not the final Gold-only currency-chain mode and never claims payment acceptance.
"""
from contextlib import contextmanager
from pathlib import Path
import argparse
import ctypes
import hashlib
import json
import math
import os
import sys
import threading
import time
from horizon_core_rpc import CoreRpc
from horizon_coordination import confirm_candidate
from horizon_sync_guard import wait_for_network
from horizon_compute_lease import computation_lease_active
from horizon_ticket_request import signed_request_parameters
from horizon_network_audits import NetworkAudits
from horizon_gold_proof import build as build_gap_proof, assemble as assemble_gold_proof
from horizon_engine_host import EngineService
from horizon_status_stream import ticket_frame
from pgn_native_contract import fields
from pgn_session import atomic_bytes
from report_commitment import seal_report


def save(file, value):
    atomic_bytes(Path(file), (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode())


@contextmanager
def legacy_lease(file):
    handle = None
    if file and os.name == "nt":
        from ctypes import wintypes as T
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.CreateFileW.argtypes = [T.LPCWSTR, T.DWORD, T.DWORD, T.LPVOID, T.DWORD, T.DWORD, T.HANDLE]
        kernel.CreateFileW.restype = T.HANDLE
        kernel.CloseHandle.argtypes = [T.HANDLE]
        handle = kernel.CreateFileW(str(file), 0xC0000000, 0, None, 4, 128, None)
        if handle in (None, T.HANDLE(-1).value):
            raise RuntimeError("GPU is already in use by another controlled worker")
    try:
        yield
    finally:
        if handle:
            kernel.CloseHandle(handle)


class NetworkWorker:
    def __init__(self, args):
        self.network_lock = threading.RLock()
        self.state_lock = threading.RLock()
        self.output_lock = threading.Lock()
        self.network_notice = None
        self.args = args
        self.root = args.state_root.resolve()
        self.root.mkdir(parents=True, exist_ok=True)
        self.rpc = CoreRpc(args.rpc_port, args.cookie, args.genesis, args.wallet)
        self.running, self.quitting = threading.Event(), threading.Event()
        self.running.set()
        self.current = None
        self.source = EngineService(args.bundle, self.root, args.gpu, args.architecture, observer=self.progress)
        self.sequence = 0
        self.last_emit = 0.0
        self.device_lease = None
        self.off_announced = False
        self.audits = NetworkAudits(self)
        if args.import_work and not (self.root / "controller.json").exists():
            work = json.loads(args.import_work.read_bytes())
            if work["genesis"] != args.genesis:
                raise ValueError("Imported owned work belongs to another network")
            save(self.root / "controller.json", dict(stage="ASSIGNED", sequence=2, work=work))

    def emit(self, frame):
        with self.output_lock:
            print(json.dumps(frame, separators=(",", ":")), flush=True)

    def message(self, text, phase="Starting"):
        self.emit(dict(schema="PGN_HORIZON_MINING_VIEW_UNAVAILABLE_V1", phase=phase, message=text[:400]))

    def progress(self, event):
        if self.audits.presentation:
            try:
                item = self.audits.presentation
                frame = ticket_frame(item["folder"], self.sequence)
                frame.update(request_id=item["request_id"], title=item["title"], work_role="audit", record_pairs=[], validation_state="Independent audit computation · original discovery attribution retained")
                if event.get("phase") == "PAUSED_ACK":
                    frame.update(phase="Paused", speed_rate=None, average_rate=None)
                self.emit(frame)
            except (OSError, ValueError, KeyError):
                self.message("Checkpoint saved; GPU released" if event.get("phase") == "PAUSED_ACK" else "Preparing the assigned audit partition", "Paused" if event.get("phase") == "PAUSED_ACK" else "Starting")
            return
        if not self.current:
            return
        if event.get("phase") == "PAUSED":
            return
        now = time.monotonic()
        if event.get("phase") == "SEARCHING" and now - self.last_emit < .5:
            return
        self.last_emit = now
        try:
            frame = ticket_frame(self.root / "tickets" / self.current["ticket"], self.sequence, self.current.get("ticket_number"))
            frame["request_id"] = self.current["request_id"]
            frame["work_role"] = "search"
            if self.network_notice:
                frame["network_notice"] = self.network_notice
            if event.get("phase") == "STARTING":
                frame.update(phase="Starting", speed_rate=None, average_rate=None, compute_tests_rate=None, eta_rate=None)
            if event.get("phase") == "PAUSED_ACK":
                frame.update(phase="Paused", controller_state="off", speed_rate=None, average_rate=None, compute_tests_rate=None, eta_rate=None)
            frame["validation_state"] = "Coordination pilot · independently audited acceptance remains separate"
            self.emit(frame)
        except (OSError, ValueError, KeyError):
            self.message("Checkpoint saved; GPU released" if event.get("phase") == "PAUSED_ACK" else "Starting · preparing the first coherent checkpoint", "Paused" if event.get("phase") == "PAUSED_ACK" else "Starting")

    def controls(self):
        for line in sys.stdin:
            try:
                if len(line) > 4096:
                    continue
                action = json.loads(line).get("action")
                save(self.root / "last-control.json", dict(action=action, observed_utc=time.time()))
                if action in ("pause", "stop", "quit"):
                    self.running.clear()
                    atomic_bytes(self.root / "pause.request", b"user mining off\n")
                    if action == "quit":
                        self.quitting.set()
                elif action in ("resume", "start"):
                    (self.root / "pause.request").unlink(missing_ok=True)
                    self.running.set()
            except (ValueError, OSError):
                self.message("Control request could not be applied", "Needs attention")
        save(self.root / "last-control.json", dict(action="input_closed", observed_utc=time.time()))
        self.running.clear()
        self.quitting.set()
        atomic_bytes(self.root / "pause.request", b"controller disconnected\n")

    def ledger(self, envelopes):
        if not self.args.pilot_coordination:
            raise RuntimeError("Final Gold-only coordinator transport is not connected")
        operation=hashlib.sha256(json.dumps(envelopes,separators=(",", ":")).encode()).hexdigest()
        journal=self.root / "coordination-intent.json"
        if journal.is_file():
            if journal.stat().st_size>262144:
                raise ValueError("Coordination journal exceeds its recovery budget")
            previous=json.loads(journal.read_bytes())
            if not previous.get("confirmed"):
                # Resolve the retained exact candidate before creating another.
                # An unresolved publication never disappears behind a new one.
                confirm_candidate(self.rpc,previous)
                previous["confirmed"]=True
                save(journal,previous)
                if previous.get("operation_sha256")==operation:
                    return
        template = self.rpc.call("getpgnbootstraptemplate", [envelopes])
        block = bytearray.fromhex(template["block"])
        target = int(template["numeric_target"], 16)
        for nonce in range(1000000):
            block[82:90] = nonce.to_bytes(8, "little")
            raw = hashlib.sha256(hashlib.sha256(block[:112]).digest()).digest()
            if int.from_bytes(raw, "little") <= target:
                break
        else:
            raise RuntimeError("Bounded pilot coordination work found no candidate")
        intent = dict(schema="PGN_HORIZON_COORDINATION_INTENT_V2", parent=template["parent"],
                      block=block.hex(), hash=raw[::-1].hex(),operation_sha256=operation)
        if len(json.dumps(intent).encode())>262144:
            raise ValueError("Coordination candidate exceeds its durable journal budget")
        save(self.root / "coordination-intent.json", intent)
        confirm_candidate(self.rpc,intent)
        intent["confirmed"] = True
        save(self.root / "coordination-intent.json", intent)

    def address(self):
        if self.args.miner:
            return self.args.miner
        try:
            known = self.rpc.call("getaddressesbylabel", ["Horizon miner"], wallet=True)
        except RuntimeError:
            known = {}
        return next(iter(known)) if known else self.rpc.call("getnewaddress", ["Horizon miner", "bech32"], wallet=True)

    def request_state(self, request_id):
        if len(request_id) != 64 or any(c not in "0123456789abcdef" for c in request_id):
            raise ValueError("Canonical ticket request required")
        state = self.rpc.call("getpgnstate", [0, 1, request_id])
        if state.get("genesis") != self.args.genesis:
            raise ValueError("Ticket lookup belongs to another chain")
        return state

    def assign(self, recover_only=False):
        journal = self.root / "controller.json"
        if journal.is_file():
            if journal.stat().st_size > 262144:
                raise ValueError("Controller journal exceeds budget")
            saved = json.loads(journal.read_bytes())
            self.sequence = int(saved.get("sequence", 0))
            if saved.get("work") and saved.get("stage") not in ("REPORT_CONFIRMED", "CONSUMED"):
                previous = saved["work"]
                page = self.rpc.call("getpgnstate", [0, 1, previous["request_id"]])
                if page.get("genesis") != self.args.genesis:
                    raise ValueError("Saved ticket belongs to another chain")
                leases = page.get("leases", [])
                lease = next((item for item in leases if item["request"] == previous["request_id"]), None)
                if lease and lease["state"] == "assigned":
                    if (lease["start"] != previous["start"] or
                            int(lease["end"]) - int(lease["start"]) != int(previous["length"]) or
                            not computation_lease_active(page, lease)):
                        raise ValueError("Saved ticket no longer matches its live computation lease")
                    previous["ticket_number"] = lease["ticket_number"]
                    saved["work"] = previous
                    save(journal, saved)
                    return previous
                if lease and lease["state"] == "submitted_pending_audits":
                    report = self.root / "tickets" / previous["ticket"] / "network-report.json"
                    if not report.is_file() or report.stat().st_size > 2 * 1024 * 1024:
                        raise ValueError("Confirmed local report is unavailable; computation remains retained")
                    sealed = json.loads(report.read_bytes())
                    if sealed["report_root"] != lease["report_root"]:
                        raise ValueError("Confirmed report differs from retained local computation")
                    self.audits.track_owner_report(previous, sealed)
                    save(journal, dict(stage="REPORT_CONFIRMED", sequence=self.sequence, work=previous,
                         report_root=sealed["report_root"], scientific_accepted=False))
                else:
                    save(journal, dict(stage="LEASE_ENDED", sequence=self.sequence,
                         previous_request=previous["request_id"], local_work_retained=True))
        if recover_only:
            return None
        state = self.rpc.verify()
        if self.args.wallet not in self.rpc.call("listwallets"):
            self.rpc.call("loadwallet", [self.args.wallet])
        saved = json.loads(journal.read_bytes()) if journal.is_file() else {}
        if saved.get("stage") == "REQUEST_SIGNED":
            request = saved["request"]
            self.sequence = int(saved["sequence"])
        else:
            miner = self.address()
            request = self.rpc.call("getpgnticketrequest", [miner, self.args.reward])
            signature = self.rpc.call("signmessage", [miner, request["challenge"]], wallet=True)
            request = self.rpc.call("getpgnticketrequest", signed_request_parameters(request, miner, self.args.reward, signature))
            self.sequence += 1
            save(journal, dict(stage="REQUEST_SIGNED", sequence=self.sequence, request=request))
        state = self.request_state(request["request_id"])
        if not any(lease["request"] == request["request_id"] for lease in state["leases"]):
            intent = self.root / "coordination-intent.json"
            if intent.is_file():
                previous = json.loads(intent.read_bytes())
                if not previous.get("confirmed"):
                    confirm_candidate(self.rpc,previous)
                    previous["confirmed"] = True
                    save(intent, previous)
                    state = self.request_state(request["request_id"])
            if not any(lease["request"] == request["request_id"] for lease in state["leases"]):
                self.ledger([request["envelope"]])
        for _ in range(8):
            state = self.request_state(request["request_id"])
            if any(lease["request"] == request["request_id"] for lease in state["leases"]):
                break
            self.ledger([])
        else:
            raise RuntimeError("Automatic ticket is not yet assigned")
        work = self.rpc.call("getpgnscientificwork", [request["request_id"], "00" * 32])
        if work["accepted"] is not False or work["genesis"] != self.args.genesis:
            raise ValueError("Unexpected work identity or acceptance state")
        save(journal, dict(stage="ASSIGNED", sequence=self.sequence, work=work))
        return work

    def mark_report_confirmed(self, work, sealed):
        with self.state_lock:
            file = self.root / "controller.json"
            current = json.loads(file.read_bytes()) if file.is_file() else {}
            if current.get("work", {}).get("ticket") == work["ticket"]:
                save(file, dict(stage="REPORT_CONFIRMED", sequence=current.get("sequence", self.sequence), work=work, report_root=sealed["report_root"], scientific_accepted=False))
        self.audits.track_owner_report(work, sealed)

    def submit(self, result, work=None):
        work = self.current if work is None else work
        file = self.root / "tickets" / work["ticket"] / "network-report.json"
        if file.exists():
            sealed = json.loads(file.read_bytes())
        else:
            sealed = seal_report(result, work)
            save(file, sealed)
        confirmed = self.rpc.call("getpgnreports", [0, 1, work["request_id"]])
        if any(r["request_id"] == work["request_id"] and r["report_root"] == sealed["report_root"] for r in confirmed):
            self.mark_report_confirmed(work, sealed)
            return
        miner = self.address()
        challenge = self.rpc.call("getpgnreportcommit", [work["request_id"], sealed["report_root"]])
        signature = self.rpc.call("signmessage", [miner, challenge["challenge"]], wallet=True)
        request = self.rpc.call("getpgnreportcommit", [work["request_id"], sealed["report_root"], signature])
        self.ledger([request["envelope"]])
        confirmed = self.rpc.call("getpgnreports", [0, 1, work["request_id"]])
        if not any(r["request_id"] == work["request_id"] and r["report_root"] == sealed["report_root"] for r in confirmed):
            raise RuntimeError("Completed report was not confirmed; local work is retained")
        self.mark_report_confirmed(work, sealed)

    def submit_gold(self, request, sealed, details_root):
        work = self.rpc.call("getpgnscientificwork", [request, details_root])
        target = int(work["target_numerator"]) / int(work["target_denominator"])
        candidates = [record for record in sealed["body"]["computed"]["best"] if int(record["gap"]) >= target * math.log(int(record["lower_prime"])) * (1 - 1e-12)]
        if not candidates:
            return "no_gold"
        record = max(candidates, key=lambda r: int(r["gap"]) / math.log(int(r["lower_prime"])))
        evidence = build_gap_proof(self.args.proof_builder, work, sealed["body"]["computed"], record["lower_prime"])
        if evidence["details_root"] != details_root:
            raise ValueError("Gold evidence does not match audited report details")
        signature = self.rpc.call("signmessage", [self.address(), work["challenge"]], wallet=True)
        script = assemble_gold_proof(work, evidence, signature)
        candidate = self.rpc.call("getpgnscientifictemplate", [script])
        if candidate.get("work_verified") is not True or int(candidate["winner_atomic"]) <= 0:
            raise ValueError("Gold candidate has no verified positive reward")
        result = self.rpc.call("submitblock", [candidate["block"]])
        if result is not None:
            raise RuntimeError("Gold candidate was not accepted: " + str(result))
        header = self.rpc.call("getblockheader", [candidate["hash"], True])
        if header.get("confirmations", -1) < 1:
            raise RuntimeError("Gold is not yet on the active chain; report retained")
        save(self.root / "tickets" / sealed["body"]["ticket"] / "network-acceptance.json", dict(schema="PGN_HORIZON_GOLD_ACCEPTANCE_V1", block=candidate["hash"], winner_atomic=candidate["winner_atomic"], audited=True, exact_gap_verified=True, monetary_accepted=True))
        return "accepted"

    def acquire_device(self):
        if self.device_lease is None:
            self.device_lease = legacy_lease(self.args.legacy_lease)
            self.device_lease.__enter__()

    def release_device(self):
        self.source.manager.shutdown()
        if self.device_lease is not None:
            self.device_lease.__exit__(None, None, None)
            self.device_lease = None

    def run(self):
        self.rpc.verify()
        if self.args.wallet not in self.rpc.call("listwallets"):
            self.rpc.call("loadwallet", [self.args.wallet])
        threading.Thread(target=self.controls, name="mining-controls", daemon=True).start()
        if getattr(self.args,"wait_for_network_sync",False)and not wait_for_network(self):
            self.source.close()
            return
        from horizon_mining_flow import MiningFlow
        flow = MiningFlow(self)
        try:
            flow.run()
        finally:
            if not flow.closed:
                self.release_device()
                self.source.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--network", action="store_true")
    parser.add_argument("--bundle", required=True, type=Path)
    parser.add_argument("--state-root", required=True, type=Path)
    parser.add_argument("--gpu", required=True)
    parser.add_argument("--architecture", required=True, type=int, choices=(86, 120))
    parser.add_argument("--rpc-port", required=True, type=int)
    parser.add_argument("--cookie", required=True, type=Path)
    parser.add_argument("--genesis", required=True)
    parser.add_argument("--wallet", required=True)
    parser.add_argument("--miner")
    parser.add_argument("--reward", required=True)
    parser.add_argument("--legacy-lease", type=Path)
    parser.add_argument("--proof-builder", type=Path)
    parser.add_argument("--piece-limit", type=int, default=64 * 10**12)
    parser.add_argument("--pilot-coordination", action="store_true")
    parser.add_argument("--wait-for-network-sync", action="store_true")
    parser.add_argument("--bootstrap-height", type=int)
    parser.add_argument("--bootstrap-hash")
    parser.add_argument("--import-work", type=Path)
    args = parser.parse_args()
    if args.proof_builder is None:
        args.proof_builder = args.bundle / "bin" / ("pgn-gap-evidence.exe" if os.name == "nt" else "pgn-gap-evidence")
    try:
        NetworkWorker(args).run()
    except Exception as error:
        print(json.dumps(dict(schema="PGN_HORIZON_MINING_VIEW_UNAVAILABLE_V1", phase="Needs attention", message=str(error)[:400])), flush=True)
        return 1
    return 0
