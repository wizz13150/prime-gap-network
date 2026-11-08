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
from horizon_coordination import confirm_candidate, active_candidate, validate_intent
from horizon_sync_guard import wait_for_network
from horizon_compute_lease import computation_lease_active
from horizon_ticket_request import signed_request_parameters
from horizon_network_audits import NetworkAudits
from horizon_gold_proof import build as build_gap_proof, assemble as assemble_gold_proof
from horizon_engine_host import EngineService
from horizon_status_stream import ticket_frame
from horizon_audit_progress import whole_audit_frame
from horizon_testnet_admission import AdmissionMonitor
from pgn_native_contract import fields
from pgn_session import atomic_bytes, SessionControllerBusy
from report_commitment import seal_report, verify_report_reveal, hash_text, MAX_REPORT_BYTES
from horizon_public_profile import PublicProfile


def save(file, value):
    atomic_bytes(Path(file), (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode())


def validate_report_proposal(proposal, work, report_root, previous=None):
    """Own-Core proposal must still name the exact locally completed ticket."""
    if not isinstance(proposal, dict) or proposal.get('accepted') is not False:
        raise ValueError('A report proposal is not network acceptance')
    for key, expected in dict(genesis=work['genesis'], request_id=work['request_id'],
                              ticket=work['ticket'], report_root=report_root).items():
        if proposal.get(key) != expected:
            raise ValueError('Report proposal no longer matches completed work: ' + key)
    for key in ('parent', 'report_id'):
        if hash_text(proposal.get(key)) == '0' * 64:
            raise ValueError('Explicit current report proposal identity required')
    challenge = proposal.get('challenge')
    if type(challenge) is not str or not 0 < len(challenge) <= 1024:
        raise ValueError('Bounded report signing challenge required')
    if previous is not None and any(proposal.get(key) != previous.get(key)
                                    for key in ('parent', 'report_id', 'challenge')):
        raise RuntimeError('Report proposal changed while signing; exact local report retained')
    return proposal


def report_confirmed(rows, work, report_root):
    if not isinstance(rows, list) or len(rows) > 1 or any(not isinstance(row, dict) for row in rows):
        raise ValueError('Confirmed report lookup must be an explicit bounded list')
    for row in rows:
        if row.get('request_id') == work['request_id'] and row.get('report_root') == report_root:
            if row.get('ticket') != work['ticket']:
                raise ValueError('Confirmed report belongs to another ticket; local report retained')
            return True
    return False


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
        self.startup_phase("loading_profile")
        self.public_profile = PublicProfile(self.root)
        self.rpc = CoreRpc(args.rpc_port, args.cookie, args.genesis, args.wallet)
        self.running, self.quitting = threading.Event(), threading.Event()
        self.running.set()
        self.current = None
        from horizon_scientific_bundle import DESCRIPTOR, create_engine
        required = getattr(args, 'require_scientific_source', False)
        self.cpu_only = getattr(args, 'cpu_only', False)
        if self.cpu_only:
            if args.import_work:
                raise ValueError('CPU-only mining cannot import a legacy GPU checkpoint')
            from horizon_cpu_source import CpuOnlyNormalSource
            self.source = CpuOnlyNormalSource(self.root, self.emit, stopping=self.quitting,
                                             running=self.running, observer=self.progress)
        elif required or (Path(args.bundle) / DESCRIPTOR).exists():
            self.source = create_engine(args.bundle, self.root, args.gpu, args.architecture,
                                        self.progress, required=required, stopping=self.quitting)
        else:
            self.source = EngineService(args.bundle, self.root, args.gpu, args.architecture, observer=self.progress)
        from horizon_cpu_bridge import LogicalMinerCpuResources
        self.startup_phase("engine_constructed")
        if not self.cpu_only:
            self.source = LogicalMinerCpuResources(self.source, self.root, self.emit)
        self.sequence = 0
        self.device_lease = None
        self.off_announced = False
        self.audits = NetworkAudits(self)
        self.startup_phase("audit_queues_opened")
        self.admission = AdmissionMonitor.from_worker(self)
        if args.import_work and not (self.root / "controller.json").exists():
            work = json.loads(args.import_work.read_bytes())
            if work["genesis"] != args.genesis:
                raise ValueError("Imported owned work belongs to another network")
            save(self.root / "controller.json", dict(stage="ASSIGNED", sequence=2, work=work))

    def startup_phase(self, phase):
        save(self.root / "runtime-startup.json", dict(schema="PGN_CONTROLLER_STARTUP_V1",
             phase=phase, pid=os.getpid(), observed_utc=time.time()))

    def emit(self, frame):
        admission = getattr(self, "admission", None)
        if admission is not None:
            frame["network_admission"] = admission.snapshot()
        policy = getattr(self, "audit_service_snapshot", None)
        if policy is not None:
            frame["audit_service"] = policy()
        with self.output_lock:
            if getattr(self, "output_disconnected", False):
                return False
            encoded = json.dumps(frame, separators=(",", ":"))
            try:
                print(encoded, flush=True)
            except OSError:
                # Losing the presentation pipe must request a safe checkpoint,
                # not escape into EngineService's forced-computation failure.
                self.output_disconnected = True
                self.running.clear()
                self.quitting.set()
                cancel = getattr(getattr(self, "source", None), "cancel_cpu_transport", None)
                if cancel is not None:
                    cancel("Normal miner control output disconnected")
                atomic_bytes(self.root / "pause.request", b"control output disconnected\n")
                return False
            return True

    def message(self, text, phase="Starting"):
        self.emit(dict(schema="PGN_HORIZON_MINING_VIEW_UNAVAILABLE_V1", phase=phase, message=text[:400]))

    def progress(self, event):
        from horizon_progress_cadence import ProgressCadence
        cadence=getattr(self,'progress_cadence',None)
        if cadence is None:self.progress_cadence=cadence=ProgressCadence()
        item=self.audits.presentation
        identity=(str(item.get('folder')),item.get('request_id'),self.sequence) if item else (
            (self.current or {}).get('ticket'),(self.current or {}).get('request_id'),self.sequence)
        started=time.monotonic_ns()
        if not cadence.admit(event,identity,started):return
        try:
            self._present_progress(event)
        finally:
            cadence.observe_cost(time.monotonic_ns()-started)

    def _present_progress(self, event):
        if getattr(self, 'cpu_only', False):
            current = self.source.current_frame
            if current is not None:
                frame = dict(current, local_sequence=self.sequence)
                if event.get('phase') == 'PAUSED_ACK':
                    frame.update(phase='Paused',speed_rate=None,average_rate=None,compute_tests_rate=None,eta_rate=None)
                if self.audits.presentation:
                    item = self.audits.presentation
                    frame = whole_audit_frame(item, frame, paused=event.get('phase') == 'PAUSED_ACK')
                elif self.current:
                    frame.update(request_id=self.current['request_id'],title='Ticket '+str(self.current.get('ticket_number','')),work_role='search')
                self.emit(frame)
            return
        if self.audits.presentation:
            try:
                item = self.audits.presentation
                frame = ticket_frame(item["folder"], self.sequence)
                frame = whole_audit_frame(item, frame, paused=event.get('phase') == 'PAUSED_ACK')
                self.emit(frame)
            except (OSError, ValueError, KeyError) as error:
                # A new internal input may not have its first session yet.
                # Retain the audit's completed prefix instead of resetting it.
                frame = whole_audit_frame(item, paused=event.get('phase') == 'PAUSED_ACK')
                frame['progress_notice'] = str(error)[:300]
                self.emit(frame)
            return
        if not self.current:
            return
        if event.get("phase") == "PAUSED":
            return
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

    def configure_cpu_resource(self, control):
        device = control.get("device_id", "")
        try:
            if set(control) != {"action", "id", "device_id", "inventory", "enabled", "threads", "affinity"}:
                raise ValueError("Exact CPU resource and budget fields required")
            receipt = self.source.configure_cpu(device, control["inventory"], control["enabled"], control["threads"], control["affinity"])
            self.emit(dict(schema="PGN_HORIZON_CPU_NORMAL_EVENT_V1", type="control", device_id=device,
                           id=control["id"], applied=True, resource_enabled=control["enabled"], status=receipt["status"]))
        except (OSError, ValueError, RuntimeError, TimeoutError) as error:
            self.emit(dict(schema="PGN_HORIZON_CPU_NORMAL_EVENT_V1", type="control", device_id=device,
                           id=control.get("id"), applied=False, message=str(error)[:300]))

    def public_name(self):
        return self.public_profile.name()

    def controls(self):
        for line in sys.stdin:
            try:
                action = None
                if len(line) > 65536:
                    continue
                control = json.loads(line)
                if not isinstance(control, dict):
                    raise ValueError("Object control required")
                action = control.get("action")
                if action == 'public_profile':
                    name = self.public_profile.update(control)
                    self.emit(dict(schema='PGN_PUBLIC_PROFILE_STATUS_V1', applied=True, name=name))
                    continue
                if action == "cpu_reply":
                    if set(control) != {"action", "device_id", "bridge_id", "response"}:
                        raise ValueError("Exact CPU bridge response required")
                    self.source.accept_cpu_reply(control["device_id"], control["bridge_id"], control["response"])
                    continue
                if action == "cpu_configure":
                    # The input reader must remain free to deliver CPU replies
                    # while this management operation waits for a boundary.
                    threading.Thread(target=self.configure_cpu_resource, args=(control,), daemon=True,
                                     name="cpu-resource-control").start()
                    continue
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
                if action == 'public_profile':
                    self.emit(dict(schema='PGN_PUBLIC_PROFILE_STATUS_V1', applied=False,
                                   message='Public name was not changed; retry saving the profile'))
                elif action in ("cpu_reply", "cpu_configure"):
                    self.emit(dict(schema="PGN_HORIZON_CPU_NORMAL_EVENT_V1", type="cpu_notice",
                                   device_id=control.get("device_id", ""), message="CPU control reply retained for reconciliation"))
                else:
                    self.message("Control request could not be applied", "Needs attention")
        save(self.root / "last-control.json", dict(action="input_closed", observed_utc=time.time()))
        self.running.clear()
        self.quitting.set()
        cancel = getattr(self.source, "cancel_cpu_transport", None)
        if cancel is not None:
            cancel("Normal miner controller input closed")
        atomic_bytes(self.root / "pause.request", b"controller disconnected\n")

    def ledger(self, envelopes, *, on_intent=None):
        if not self.args.pilot_coordination:
            raise RuntimeError("Final Gold-only coordinator transport is not connected")
        operation=hashlib.sha256(json.dumps(envelopes,separators=(",", ":")).encode()).hexdigest()
        journal=self.root / "coordination-intent.json"
        if journal.is_file():
            if journal.stat().st_size>262144:
                raise ValueError("Coordination journal exceeds its recovery budget")
            previous=json.loads(journal.read_bytes())
            same_operation = previous.get("operation_sha256") == operation
            if not previous.get("confirmed") or (envelopes and same_operation):
                validate_intent(previous)
                accepted = active_candidate(self.rpc, previous)
                tip = self.rpc.call('getbestblockhash')
                if not accepted and tip != previous['parent']:
                    # Preserve the exact old candidate; do not call an orphan
                    # confirmed or retry its obsolete physical block forever.
                    # The caller supplies a fresh Core-authorized operation;
                    # the immutable report/audit identity is not rewritten.
                    archive = self.root / 'coordination-history'
                    archive.mkdir(exist_ok=True)
                    retained = archive / (previous['hash'] + '.json')
                    if retained.exists():
                        if retained.stat().st_size > 262144 or json.loads(retained.read_bytes()) != previous:
                            raise ValueError('Retained coordination candidate changed')
                    else:
                        save(retained, previous)
                else:
                    if not accepted:
                        previous['confirmed'] = False
                        save(journal, previous)
                        if on_intent is not None and same_operation:
                            on_intent(previous)
                        confirm_candidate(self.rpc, previous)
                    previous['confirmed'] = True
                    save(journal, previous)
                    if same_operation:
                        return dict(block_hash=previous['hash'], operation_sha256=operation)
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
        if on_intent is not None:
            on_intent(intent)
        confirm_candidate(self.rpc,intent)
        intent["confirmed"] = True
        save(self.root / "coordination-intent.json", intent)
        return dict(block_hash=intent["hash"], operation_sha256=operation)

    def address(self):
        if self.args.miner:
            selected = self.args.miner
        else:
            try:
                known = self.rpc.call("getaddressesbylabel", ["Horizon miner"], wallet=True)
            except RuntimeError:
                known = {}
            selected = next(iter(known)) if known else self.rpc.call("getnewaddress", ["Horizon miner", "bech32"], wallet=True)
        admission = getattr(self, "admission", None)
        if admission is not None:
            admission.select_identity(selected)
        return selected

    def request_state(self, request_id):
        if len(request_id) != 64 or any(c not in "0123456789abcdef" for c in request_id):
            raise ValueError("Canonical ticket request required")
        state = self.rpc.call("getpgnstate", [0, 1, request_id])
        if state.get("genesis") != self.args.genesis:
            raise ValueError("Ticket lookup belongs to another chain")
        return state

    def assign(self, recover_only=False):
        duty=None
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
            from horizon_native_work_duty import gate
            duty=gate(self.rpc,miner,self.args.genesis)
            request = self.rpc.call("getpgnticketrequest", [miner, self.args.reward])
            signature = self.rpc.call("signmessage", [miner, request["challenge"]], wallet=True)
            request = self.rpc.call("getpgnticketrequest", signed_request_parameters(request, miner, self.args.reward, signature))
            self.sequence += 1
            save(journal, dict(stage="REQUEST_SIGNED", sequence=self.sequence, request=request))
        state = self.request_state(request["request_id"])
        if not any(lease["request"] == request["request_id"] for lease in state["leases"]):
            from horizon_native_work_duty import gate
            duty=gate(self.rpc,self.address(),self.args.genesis)
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
        if duty is not None and duty['enforced']:
            if work.get('native_audit_service',{}).get('enforced')is not True:
                raise ValueError('Core must expose the confirmed native service receipt for this new request')
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
            if file.stat().st_size > MAX_REPORT_BYTES:
                raise ValueError('Retained network report exceeds capacity')
            sealed = json.loads(file.read_bytes())
        else:
            sealed = seal_report(result, work, discoverer=self.public_name())
            save(file, sealed)
        body = verify_report_reveal(sealed, sealed['report_root'])
        if sealed.get('scientific_accepted') is not False or any(body.get(key) != work.get(source)
                for key, source in (('genesis', 'genesis'), ('request_id', 'request_id'), ('ticket', 'ticket'),
                                    ('issue_context', 'work_id'), ('target_numerator', 'target_numerator'),
                                    ('target_denominator', 'target_denominator'))):
            raise ValueError('Retained network report belongs to different issued work')
        if (body['computed']['start'] != work['start'] or
                int(body['computed']['end']) - int(work['start']) != int(work['length'])):
            raise ValueError('Retained network report covers another interval')
        confirmed = self.rpc.call("getpgnreports", [0, 1, work["request_id"]])
        if report_confirmed(confirmed, work, sealed['report_root']):
            self.mark_report_confirmed(work, sealed)
            return
        miner = self.address()
        challenge = validate_report_proposal(self.rpc.call("getpgnreportcommit", [work["request_id"], sealed["report_root"]]), work, sealed['report_root'])
        signature = self.rpc.call("signmessage", [miner, challenge["challenge"]], wallet=True)
        request = validate_report_proposal(self.rpc.call("getpgnreportcommit", [work["request_id"], sealed["report_root"], signature]), work, sealed['report_root'], challenge)
        self.ledger([request["envelope"]])
        confirmed = self.rpc.call("getpgnreports", [0, 1, work["request_id"]])
        if not report_confirmed(confirmed, work, sealed['report_root']):
            raise RuntimeError("Completed report was not confirmed; local work is retained")
        self.mark_report_confirmed(work, sealed)

    def submit_gold(self, request, sealed, details_root):
        folder = self.root / "tickets" / sealed["body"]["ticket"]
        binding = dict(request_id=request, ticket=sealed["body"]["ticket"],
                       report_root=sealed["report_root"], details_root=details_root,
                       genesis=self.args.genesis)
        # An accepted block changes the chain tip. Retrying maintenance, or
        # restarting after submitblock, must not try to mint the same report.
        for name in ("network-acceptance.json", "gold-publication-intent.json"):
            file = folder / name
            if not file.exists():
                continue
            if file.stat().st_size > 32768:
                raise ValueError("Gold publication journal exceeds budget")
            receipt = json.loads(file.read_bytes())
            if receipt.get("schema") != "PGN_HORIZON_GOLD_PUBLICATION_V2":
                continue
            if any(receipt.get(key) != value for key, value in binding.items()):
                raise ValueError("Gold publication journal differs from audited report")
            try:
                header = self.rpc.call("getblockheader", [receipt["block"], True])
            except RuntimeError:
                header = {}
            if header.get("confirmations", -1) >= 1:
                save(folder / "network-acceptance.json", dict(receipt, audited=True,
                     exact_gap_verified=True, monetary_accepted=True))
                return "accepted"
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
        intent = dict(binding, schema="PGN_HORIZON_GOLD_PUBLICATION_V2",
                      block=candidate["hash"], winner_atomic=candidate["winner_atomic"])
        save(folder / "gold-publication-intent.json", intent)
        result = self.rpc.call("submitblock", [candidate["block"]])
        if result is not None:
            raise RuntimeError("Gold candidate was not accepted: " + str(result))
        header = self.rpc.call("getblockheader", [candidate["hash"], True])
        if header.get("confirmations", -1) < 1:
            raise RuntimeError("Gold is not yet on the active chain; report retained")
        save(folder / "network-acceptance.json", dict(intent, audited=True, exact_gap_verified=True, monetary_accepted=True))
        return "accepted"

    def acquire_device(self):
        if getattr(self, 'cpu_only', False):
            return
        if self.device_lease is None:
            self.device_lease = legacy_lease(self.args.legacy_lease)
            self.device_lease.__enter__()

    def release_device(self):
        if getattr(self, 'cpu_only', False):
            return
        self.source.manager.shutdown()
        if self.device_lease is not None:
            self.device_lease.__exit__(None, None, None)
            self.device_lease = None

    def wait_cpu_admission(self):
        from horizon_testnet_admission import join_status
        while not self.quitting.is_set():
            if not self.running.is_set() or not any(v['enabled'] for v in self.source.options.values()):
                self.quitting.wait(.2)
                continue
            status = join_status(self.rpc, self.args.genesis, self.address())
            if status['registered'] and status['normal_cycle_permission']:
                return True
            message = 'Miner registration or audit permission is required; no new CPU work has been requested'
            self.message(message, 'Registration required')
            for device, choice in tuple(self.source.options.items()):
                if choice['enabled']:
                    self.emit(dict(schema='PGN_HORIZON_CPU_NORMAL_EVENT_V1',device_id=device,type='cpu_notice',
                                   reason_code='registration_required',message=message))
            self.quitting.wait(2)
        return False

    def run(self):
        admission = getattr(self, "admission", None)
        try:
            self.startup_phase("verifying_core")
            self.rpc.verify()
            if self.args.wallet not in self.rpc.call("listwallets"):
                self.rpc.call("loadwallet", [self.args.wallet])
            if admission is not None:
                admission.start()
            threading.Thread(target=self.controls, name="mining-controls", daemon=True).start()
            self.startup_phase("controls_ready")
            if getattr(self, 'cpu_only', False) and not self.wait_cpu_admission():
                self.source.close()
                return
            if getattr(self.args,"wait_for_network_sync",False)and not wait_for_network(self):
                self.source.close()
                return
            from horizon_mining_flow import MiningFlow
            self.startup_phase("opening_normal_cycle")
            flow = MiningFlow(self)
            self.startup_phase("normal_cycle_ready")
            try:
                flow.run()
            finally:
                if not flow.closed:
                    self.release_device()
                    self.source.close()
                    flow.cycles.close()
        finally:
            if admission is not None:
                admission.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--network", action="store_true")
    parser.add_argument("--cpu-only", action="store_true", help="Use selected CPU resources within the normal ticket/audit cycle")
    parser.add_argument("--bundle", required=True, type=Path)
    parser.add_argument("--state-root", required=True, type=Path)
    parser.add_argument("--gpu")
    parser.add_argument("--architecture", type=int, choices=(86, 120))
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
    parser.add_argument("--controlled-lab-audit-actors", type=Path,
                        help="Explicit testnet laboratory audit identities; never enabled by default")
    parser.add_argument("--require-scientific-source", action="store_true")
    parser.add_argument("--wait-for-network-sync", action="store_true")
    parser.add_argument("--bootstrap-height", type=int)
    parser.add_argument("--bootstrap-hash")
    parser.add_argument("--import-work", type=Path)
    args = parser.parse_args()
    if not args.cpu_only and (not args.gpu or args.architecture is None):
        parser.error('GPU mining requires --gpu and --architecture')
    if args.cpu_only and (args.gpu or args.architecture is not None or args.import_work or args.legacy_lease):
        parser.error('CPU-only work uses its own normal-cycle state and no GPU identity or legacy checkpoint')
    if args.proof_builder is None:
        args.proof_builder = args.bundle / "bin" / ("pgn-gap-evidence.exe" if os.name == "nt" else "pgn-gap-evidence")
    try:
        NetworkWorker(args).run()
    except SessionControllerBusy as error:
        print(json.dumps(dict(schema="PGN_HORIZON_MINING_VIEW_UNAVAILABLE_V1", phase="Waiting for controller", error_code=error.code, message=str(error)[:400])), flush=True)
        return 1
    except Exception as error:
        print(json.dumps(dict(schema="PGN_HORIZON_MINING_VIEW_UNAVAILABLE_V1", phase="Needs attention", message=str(error)[:400])), flush=True)
        return 1
    return 0
