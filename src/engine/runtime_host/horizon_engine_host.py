"""Owned Horizon computation service. It has no Scout application/UI dependency.

The controller supplies authority-validated tickets. This process computes and
checkpoints them; it never accepts network work, assigns rewards or mints coins.
One resident native owner can serve successive tickets on the same device.
"""

from pathlib import Path
from types import SimpleNamespace
import argparse
import datetime as dt
import hashlib
import json
import os
import queue
import re
import sys
import threading
import time

from pgn_artifact_lease import ArtifactLease
from pgn_exact_prefix import certify
from pgn_native_contract import fields, file_sha, module_metadata, piece_fields
from pgn_owned import create_owned, run_checked
from pgn_plan import choose_plan, query_plan
from pgn_prime_proof import PrimeProof
from pgn_publications import PublicationQueue, WorkRates
from pgn_resident import Manager
from pgn_session import SessionControllerLock, SessionStore, atomic_bytes
from pgn_statistics import statistics_state
from pgn_threshold_policy import at_position, verify_pin
from pgn_work_pieces import next_piece

from horizon_engine_capabilities import NATIVE_SOURCE, IMAGES, qualified_workers

GAP = re.compile(r"\bGAP merit=([0-9.eE+\-]+) gap=(\d+) lower=(\d+) upper=(\d+)")
COUNTERS = re.compile(r"\[pgn-counters\].*?\bpositions=(\d+)\b.*?\btests=(\d+)\b")


def canonical(value):
    if not isinstance(value, str) or not re.fullmatch(r"0|[1-9][0-9]{0,28}", value):
        raise ValueError("Canonical decimal integer string required")
    return int(value)


def ticket_configuration(ticket):
    if ticket.get("schema") != "PGN_HORIZON_ENGINE_TICKET_V1":
        raise ValueError("Unsupported Horizon computation ticket")
    identity = ticket.get("ticket_id", "")
    if not isinstance(identity, str) or not re.fullmatch("[0-9a-f]{64}", identity):
        raise ValueError("Exact ticket identity required")
    start, end = canonical(ticket["start"]), canonical(ticket["end"])
    pin = verify_pin(ticket["threshold_policy"], start, end)
    if (pin.get("schema") != "PGN_THRESHOLD_POLICY_V2"
            or pin.get("policy_id") != "POINTWISE_RETAINED_MERIT_20_5"
            or pin["requested"] != "auto" or pin["count_override"] is not None
            or pin["detail_override"] is not None):
        raise ValueError("Network computation uses its issued pointwise policy")
    target = ticket["target_merit_q16"]
    if type(target) is not int or not 0 < target < 1 << 31:
        raise ValueError("Positive fixed-point target required")
    return dict(start=str(start), end=str(end), bins=100, report_top=100,
                target_merit=target / 65536, ticket_id=identity,
                target_merit_q16=target, **fields(start, end, pin=pin))


def emit(value):
    value = dict(schema="PGN_HORIZON_ENGINE_EVENT_V1", **value)
    print(json.dumps(value, separators=(",", ":")), flush=True)


def instance_alive(instance):
    if os.name == "nt":
        from pgn_owned import _kernel, _recorded_alive
        kernel, types = _kernel()
        return _recorded_alive(kernel, types, instance)
    try:
        record = Path(f"/proc/{int(instance['pid'])}/stat").read_text()
        return record.rsplit(")", 1)[1].split()[19] == str(instance["birth"])
    except FileNotFoundError:
        return False


class EngineService:
    def __init__(self, bundle, state_root, gpu_uuid, architecture, observer=emit):
        self.bundle = Path(bundle).resolve(strict=True)
        self.root = Path(state_root).resolve()
        self.root.mkdir(parents=True, exist_ok=True)
        if not re.fullmatch(r"GPU-[0-9a-fA-F-]{36}", gpu_uuid):
            raise ValueError("Actual GPU UUID required")
        self.gpu_uuid, self.architecture = gpu_uuid, architecture
        self.observer = observer
        self.environment = dict(os.environ, CUDA_VISIBLE_DEVICES=gpu_uuid)
        self.manager = Manager(SimpleNamespace(), self.root, gpu_uuid, create_owned)
        self.proof = PrimeProof(self.bundle)
        self.rates = WorkRates()
        self.rate_identity = None
        self.checked = set()
        self.lease = ArtifactLease()
        self.lease.__enter__()

    def workers(self):
        return qualified_workers(self.bundle,self.architecture)

    def boundary_pair(self, end):
        lower = end - 1 if end % 2 == 0 else end - 2
        while not self.proof.prime(lower):
            lower -= 2
        upper = end if end % 2 else end + 1
        while not self.proof.prime(upper):
            upper += 2
        return lower, upper

    def boundary(self, store, end):
        start, final = int(store.meta["configuration"]["start"]), int(store.meta["configuration"]["end"])
        if end <= start:
            return
        lower, upper = self.boundary_pair(end)
        if start <= lower < final:
            local = at_position(store.meta["configuration"]["threshold_policy"], lower)
            if upper - lower >= min(local["count_gap"], local["detail_gap"]):
                store.record_gap(lower, upper, "exact_CPU_boundary")

    def publish(self, store, phase, **extra):
        rates = self.rates.view()
        if phase not in ("SEARCHING", "COMPUTED", "PAUSED"):
            rates = {key: None for key in rates}
        payload = {**rates, **extra, "type": "progress", "phase": phase,
                   "ticket_id": store.meta["configuration"]["ticket_id"],
                   "positions": str(store.meta["positions"]), "tests": str(store.meta["tests"]),
                   "sample_monotonic_ns": str(time.monotonic_ns()), "compute_time_ns": str(int(self.rates.elapsed * 1e9)),
                   "statistics": statistics_state(store.gaps.projection.value)}
        atomic_bytes(store.folder / "engine-status.json", (json.dumps(payload) + "\n").encode())
        self.observer(payload)

    def diagnostic(self, worker, store):
        image = file_sha(worker)
        if image in self.checked:
            return
        self.publish(store, "STARTING", detail="Checking optimized engine")
        folder = store.folder / ("diagnostic-" + str(time.time_ns()))
        folder.mkdir()
        command = [str(worker), "--self-test", "--device", "0", "--output-directory", str(folder)]
        with (folder / "worker.log").open("w", encoding="utf8") as output:
            code, receipt = run_checked(command, self.environment, self.gpu_uuid, folder, output, timeout=180)
        if code or file_sha(worker) != image:
            raise RuntimeError("Optimized engine diagnostic failed")
        self.checked.add(image)

    def run_ticket(self, ticket, maximum_piece=64 * 10**12):
        configuration = ticket_configuration(ticket)
        if type(maximum_piece) is not int or not 0 < maximum_piece <= 64 * 10**12:
            raise ValueError("Physical-piece budget outside engine contract")
        folder = self.root / "tickets" / configuration["ticket_id"]
        folder.mkdir(parents=True, exist_ok=True)
        controller = SessionControllerLock(folder)
        store = None
        try:
            store = SessionStore.load(folder) if SessionStore.exists(folder) else SessionStore.create(
                folder, configuration, {"uuid": self.gpu_uuid, "model": "controller-observed GPU"})
            if store.meta["configuration"] != configuration:
                raise ValueError("Ticket configuration changed")
            return self._run(store, maximum_piece)
        finally:
            if store is not None:
                store.close()
            controller.release()

    def _run(self, store, maximum_piece):
        from pgn_owner_recovery import recover
        cfg = store.meta["configuration"]
        pin, start, end = cfg["threshold_policy"], int(cfg["start"]), int(cfg["end"])
        checkpoint, stop = store.folder / "native-checkpoint.bin", store.folder / "stop.request"
        active = float(store.meta.get("active_seconds", 0))
        paused = float(store.meta.get("paused_seconds", 0))
        self.publish(store, "STARTING")
        candidates = self.workers()
        if store.meta.get("current_chunk"):
            # The shared GPU ownership gate and sealed owner recovery precede
            # reuse. An unsealed newer checkpoint is never promoted to progress.
            current = store.meta["current_chunk"]
            instance = current.get("instance") or {}
            if instance and not instance.get("closed"):
                if instance_alive(instance):
                    atomic_bytes(stop, b"Recovering the previous bound owner\n")
                    until = time.monotonic() + 90
                    while instance_alive(instance) and time.monotonic() < until:
                        time.sleep(0.1)
                    if instance_alive(instance):
                        raise RuntimeError("Previous bound owner remains live; recovery is waiting")
                store.closed("PAUSED", active, paused, instance_closed=True)
            recover(store)
        try:
            while int(store.meta["positions"]) < end - start or store.meta.get("current_chunk"):
                chunk = store.meta.get("current_chunk")
                if chunk is None:
                    piece = next_piece(pin, start + int(store.meta["positions"]), maximum_piece)
                    if piece.kind == "exact_short_prefix":
                        certificate = certify(piece, self.boundary_pair)
                        store.complete_exact_prefix(piece, certificate, self.boundary_pair)
                        continue
                    query = lambda worker, first, length: query_plan(
                        worker, first, length, self.environment, self.child_options(),
                        piece.minimum_gap, pin, plan_provider=self.manager.query)
                    # Probe the live qualified profile first. Trying a larger
                    # cold profile first would destroy an already fitting K32
                    # resident on an 8 GiB card at every work boundary.
                    chosen = choose_plan(candidates, piece.start, piece.end - piece.start, query,
                                         preferred_worker=self.manager.active_worker())
                    worker, plan = chosen["worker"], chosen["plan"]
                    chunk = store.bind_chunk(start=piece.start, length=chosen["length"], worker=worker,
                        worker_sha256=file_sha(worker), profile_k=chosen["profile_k"],
                        base_positions=int(store.meta["positions"]), base_tests=int(store.meta["tests"]),
                        gap_min=piece.minimum_gap, threshold_policy_sha256=pin["sha256"],
                        worker_source_sha256=plan["worker_source_sha256"])
                    chunk["plan"], chunk["threshold_binding"] = plan, plan["threshold_binding"]
                    store.save()
                identity = chunk["identity"]
                worker = next(path for k, path in candidates if k == identity["profile_k"])
                store.trusted_worker_path(worker)
                piece_fields(pin, int(identity["start"]), int(identity["start"]) + int(identity["length"]))
                native_positions, native_tests = store.restore_checkpoint(checkpoint)
                old_positions, old_tests = identity["base_positions"] + native_positions, identity["base_tests"] + native_tests
                if (old_positions, old_tests) != (store.meta["positions"], store.meta["tests"]):
                    store.recovery_rewind(old_positions, old_tests, unknown_wall=True)
                self.diagnostic(worker, store)
                if (self.root / "pause.request").exists():
                    store.closed("PAUSED", active, paused)
                    self.publish(store, "PAUSED")
                    return {"state": "PAUSED", "ticket_id": cfg["ticket_id"]}
                stop.unlink(missing_ok=True)
                store.before_launch()
                command = [str(worker), "--engine", "walk", "--start", identity["start"], "--length", identity["length"],
                    "--gap-min", str(identity["gap_min"]), "--emit-min-gap", str(cfg["emit_min_gap"]),
                    "--device", "0", "--no-records", "--state", str(checkpoint), "--stop-file", str(stop)]
                before = time.monotonic()
                job = self.manager.create(command, self.environment)
                clock = job.clock(before, dt.datetime.now(dt.timezone.utc))
                store.started(job.pid, job.birth, job_nonce=job.nonce)
                owner_file = store.folder / "native-owner-current.json"
                atomic_bytes(owner_file, json.dumps(dict(pid=job.pid, birth=str(job.birth),
                    image_sha256=identity["worker_sha256"], configuration_sha256=store.configuration_hash(),
                    wal_sequence=store.sequence, session_id=store.meta["session_id"],
                    profile_k=identity["profile_k"], emit_min_gap=cfg["emit_min_gap"], job_nonce=job.nonce)).encode())
                job.commit_identity(owner_file)
                rate_identity = (identity["worker_sha256"], identity["profile_k"], identity["gap_min"], int(identity["start"]) >> 64)
                self.rates.begin_piece(preserve_window=not job.first and rate_identity == self.rate_identity,
                                       minimum_positions=native_positions, minimum_tests=native_tests)
                self.rate_identity = rate_identity
                job.resume()
                self.publish(store, "STARTING" if job.first else "SEARCHING", native_pid=job.pid)
                events = PublicationQueue(maxsize=64)
                reader_stop = threading.Event()

                def enqueue(value):
                    while not reader_stop.is_set():
                        try:
                            events.put(value, timeout=0.1)
                            return
                        except queue.Full:
                            pass

                def receive(current):
                    try:
                        for value in current.stdout:
                            if reader_stop.is_set():
                                break
                            if len(value) > 65536:
                                raise ValueError("Oversized native output")
                            enqueue(value.rstrip("\r\n"))
                    except Exception as error:
                        enqueue(("reader_error", str(error)))
                    finally:
                        enqueue(None)

                reader = threading.Thread(target=receive, args=(job,), daemon=True)
                reader.start()
                try:
                    deadline = time.monotonic() + 3600
                    while True:
                        if (self.root / "pause.request").exists() and not stop.exists():
                            atomic_bytes(stop, b"Horizon pause request\n")
                        if time.monotonic() >= deadline:
                            raise TimeoutError("Native physical job exceeded its execution budget")
                        try:
                            line = events.get(timeout=0.1)
                        except queue.Empty:
                            continue
                        if line is None:
                            break
                        if isinstance(line, tuple):
                            raise RuntimeError(line[0] + ": " + line[1])
                        self.rates.add(getattr(line, "samples", ()))
                        for match in GAP.finditer(line):
                            _, gap, lower, upper = match.groups()
                            if int(upper) - int(lower) != int(gap):
                                raise ValueError("Native endpoint conflict")
                            store.record_gap(int(lower), int(upper), "Native_verified")
                        match = COUNTERS.search(line)
                        if match:
                            native_positions, native_tests = map(int, match.groups())
                            store.accepted_progress(identity["base_positions"] + native_positions,
                                                    identity["base_tests"] + native_tests)
                            self.publish(store, "SEARCHING", native_pid=job.pid)
                finally:
                    reader_stop.set()
                    if job.poll() is not None:
                        reader.join(timeout=5)
                code = job.wait(timeout=90)
                duration, _ = clock.finish()
                active += duration
                receipt = job.work_receipt() if code == 0 else None
                job.close()
                instance_closed = job.process_exited()
                if code:
                    raise RuntimeError("Optimized native job failed: " + str(code))
                self.boundary(store, int(identity["start"]) + native_positions)
                complete = native_positions == int(identity["length"])
                if not complete:
                    store.capture_closed_checkpoint(checkpoint, native_positions, native_tests)
                store.closed("CHUNK_COMPLETE" if complete else "PAUSED", active, paused,
                             chunk_complete=complete, instance_closed=instance_closed, work_receipt=receipt)
                if not complete:
                    self.publish(store, "PAUSED")
                    return {"state": "PAUSED", "ticket_id": cfg["ticket_id"]}
            self.boundary(store, end)
            store.closed("COMPLETE", active, paused)
            records = [dict(gap=g, lower_prime=str(lo), upper_prime=str(lo + g))
                       for g, merit, lo in store.gaps.policy_top("detail", end, 100)]
            body = dict(schema="PGN_HORIZON_COMPUTED_REPORT_V1", ticket_id=cfg["ticket_id"],
                        start=str(start), end=str(end), policy_sha256=pin["sha256"], positions=str(end - start),
                        counts=[str(n) for n in store.gaps.policy_bin_counts("count", end)], best=records)
            encoded = json.dumps(body, sort_keys=True, separators=(",", ":")).encode()
            report = dict(body=body, sha256=hashlib.sha256(encoded).hexdigest(), network_accepted=False)
            atomic_bytes(store.folder / "report.json", (json.dumps(report, indent=2) + "\n").encode())
            self.publish(store, "COMPUTED", report_sha256=report["sha256"])
            return report
        except BaseException:
            self.manager.shutdown(force=True)
            store.closed("FAILED", active, paused)
            self.publish(store, "FAILED")
            raise

    @staticmethod
    def child_options():
        return {"creationflags": 0x08000000} if os.name == "nt" else {}

    def close(self):
        try:
            self.manager.shutdown()
        finally:
            self.lease.__exit__(None, None, None)


def main():
    bootstrap = os.environ.get("PGN_PRIVATE_OWNER_BOOTSTRAP")
    if bootstrap:
        from pgn_detached_worker import boot_main
        return boot_main(bootstrap)
    if "--peer-relay" in sys.argv:
        from horizon_peer_relay import main as relay_main
        return relay_main([value for value in sys.argv[1:] if value != "--peer-relay"])
    if "--hardware" in sys.argv:
        from horizon_hardware_probe import main as hardware_main
        return hardware_main()
    if "--export-diagnostics" in sys.argv:
        from horizon_diagnostics import main as diagnostics_main
        return diagnostics_main()
    if "--prepare-local" in sys.argv:
        from horizon_local_runtime import main as prepare_main
        return prepare_main([value for value in sys.argv[1:] if value != "--prepare-local"])
    if "--network" in sys.argv:
        from horizon_network_worker import main as network_main
        return network_main()
    if "--observe" in sys.argv:
        from horizon_status_stream import main as observe_main
        return observe_main()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", action="version", version="PGN Horizon native engine host")
    parser.add_argument("--bundle", required=True, type=Path)
    parser.add_argument("--state-root", required=True, type=Path)
    parser.add_argument("--gpu", required=True)
    parser.add_argument("--architecture", required=True, type=int, choices=(86, 120))
    parser.add_argument("--job", type=Path)
    parser.add_argument("--piece-limit", type=int, default=64 * 10**12)
    parser.add_argument("--check-installation", action="store_true")
    args = parser.parse_args()
    service = EngineService(args.bundle, args.state_root, args.gpu, args.architecture)
    try:
        if args.check_installation:
            workers = service.workers()
            emit(dict(type="installation", verified=True, optimized_only=True,
                      profiles=[k for k, _ in workers], native_source_manifest=NATIVE_SOURCE,
                      application="PGN Horizon", standalone_product_required=False))
            return 0
        stream = [args.job.read_text(encoding="utf8")] if args.job else sys.stdin
        for line in stream:
            if len(line) > 262144:
                raise ValueError("Oversized controller request")
            if (service.root / "pause.request").exists():
                break
            result = service.run_ticket(json.loads(line), args.piece_limit)
            if result.get("state") == "PAUSED":
                break
    except Exception as error:
        emit(dict(type="error", phase="FAILED", message=str(error)[:8192], network_accepted=False))
        return 1
    finally:
        service.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
