"""Execute authority-assigned partitions without access to expected counts."""
from pathlib import Path
import hashlib
import json
import math
import secrets
from pgn_native_contract import fields
from pgn_session import atomic_bytes
from report_commitment import audit_commitment, canonical_bytes
from horizon_coordination import confirm_candidate, validate_intent
from horizon_audit_answers import AuditAnswers
from horizon_owner_reports import OwnerReports
from horizon_audit_progress import presentation as audit_presentation
from horizon_audit_snapshot import AnchoredAuditStates

def save(file, value):
    atomic_bytes(Path(file), canonical_bytes(value) + b"\n")


def partition_ticket(audit, part):
    start, end = int(part['start']), int(part['end'])
    identity = hashlib.sha256(canonical_bytes(dict(domain='PGN_HORIZON_AUDIT_PARTITION_V1',
        request=audit['request_id'], slot=audit['slot'], attempt=audit['attempt'], index=part['index']))).hexdigest()
    return dict(schema='PGN_HORIZON_ENGINE_TICKET_V1', ticket_id=identity, start=str(start), end=str(end),
                threshold_policy=fields(start, end)['threshold_policy'], target_merit_q16=audit['target_merit_q16'])

class NetworkAudits:
    def __init__(self, worker, *, on_assigned=None, on_committed=None, before_commit=None, on_reassigned=None, reassignment_needed=None):
        self.worker = worker
        self.folder = worker.root / "audits"
        self.folder.mkdir(exist_ok=True)
        self.answers = AuditAnswers(self.folder)
        self.owner_reports = OwnerReports(worker.root)
        # A process may have stopped after scheduling a page but before RPC.
        # Reopen its sweep on restart; retain the durable fair cursor.
        if self.owner_reports.file.exists():
            self.owner_reports.invalidate_sweep()
            self.owner_reports.reopen_for_gold_publications()
        self.owner_poll_more = False
        self.presentation = None
        self.current_audit = None
        self.last_unavailable = None
        self.on_assigned = on_assigned
        self.on_committed = on_committed
        self.before_commit = before_commit
        self.on_reassigned = on_reassigned
        self.reassignment_needed = reassignment_needed

    def event(self, action, request=None, slot=None, attempt=None, body=None):
        owner = self.worker.address()
        args = [action, owner, request, slot, attempt, body]
        proposal = self.worker.rpc.call("getpgnauditevent", args)
        if not proposal.get("available"):
            return proposal
        signature = self.worker.rpc.call("signmessage", [owner, proposal["challenge"]], wallet=True)
        return self.worker.rpc.call("getpgnauditevent", args + [signature])

    def apply(self, action, request=None, slot=None, attempt=None, body=None, *, on_intent=None):
        with self.worker.network_lock:
            proposal = self.event(action, request, slot, attempt, body)
            if proposal.get("available"):
                envelopes = [proposal["envelope"]]
                receipt = (self.worker.ledger(envelopes, on_intent=on_intent)
                           if on_intent is not None else self.worker.ledger(envelopes))
                if receipt is not None:
                    proposal = dict(proposal, confirmed_block=receipt["block_hash"])
            return proposal

    def state(self, request):
        values = self.worker.rpc.call("getpgnauditstate", [0, 1, request])
        return values[0] if values else {}

    def restore_reassigned_answer(self, audit):
        """Recover one exact sealed answer; never scan or recompute a range."""
        if self.on_reassigned is None:
            return audit
        name = audit['request_id'] + '-' + str(audit['slot']) + '-' + str(audit['attempt'])
        if not (self.folder / (name + '.json')).is_file():
            return audit
        cached, _ = self.answers.read(name)
        # register verifies any previous immutable answer digest as well.
        self.answers.register([name])
        if any(cached.get(k) != audit.get(k) for k in ('request_id', 'slot', 'attempt', 'ticket', 'report_root', 'ranges')):
            raise ValueError('Sealed audit replay differs from the reassigned work')
        if not cached.get('publication_intent'):
            return audit
        counts = cached.get('counts')
        if (not isinstance(counts, list) or len(counts) != len(audit['ranges'])
                or any(type(n) is not int or n < 0 for n in counts)):
            raise ValueError('Sealed audit replay has incomplete counts')
        expected = audit_commitment(cached['ticket'], cached['attempt'], [r['index'] for r in cached['ranges']],
                                    [str(n) for n in counts], cached['answer']['body']['nonce'])
        if expected != cached['answer']:
            raise ValueError('Sealed audit replay does not match its committed counts')
        return cached

    def reconcile_reassigned_answer(self, audit):
        if (self.on_reassigned is None or self.reassignment_needed is None
                or not audit.get('publication_intent') or not self.reassignment_needed(audit)):
            return
        intent = audit['publication_intent']
        validate_intent(intent)
        rpc = self.worker.rpc
        before = rpc.call('getbestblockhash')
        state = self.state(audit['request_id'])
        job = next((j for j in state.get('jobs', []) if j.get('slot') == audit['slot'] and j.get('attempt') == audit['attempt']), None)
        if not job or job.get('state') != 'assigned':
            raise RuntimeError('Audit replay requires the exact active reassigned attempt')
        old = rpc.call('getblockheader', [intent['hash'], True])
        tip = rpc.call('getblockheader', [before, True])
        if (not isinstance(old, dict) or old.get('hash') != intent['hash']
                or type(old.get('height')) is not int or old['height'] < 0
                or type(old.get('confirmations')) is not int
                or not isinstance(tip, dict) or tip.get('hash') != before
                or type(tip.get('height')) is not int or tip['height'] < 0):
            raise ValueError('Audit replay requires verified chain header identities')
        if old['confirmations'] > 0 or (old['height'] <= tip['height'] and rpc.call('getblockhash', [old['height']]) == intent['hash']):
            raise RuntimeError('Canonical audit confirmation cannot be replaced')
        if rpc.call('getbestblockhash') != before:
            raise RuntimeError('Chain changed during audit replay verification')
        self.on_reassigned(audit, intent['hash'])

    def states(self, requests):
        """One authenticated reconstruction per bounded group, pinned to one tip."""
        requests = list(dict.fromkeys(requests))
        if len(requests) > 4096 or any(len(r) != 64 or any(c not in "0123456789abcdef" for c in r) for r in requests):
            raise ValueError("Bounded canonical audit lookups required")
        if not requests:
            return {}
        atomic = getattr(self.worker.rpc, 'audit_snapshot', None)
        if callable(atomic) and len(requests) <= 100:
            # The real Core adapter supplies one authenticated atomic snapshot.
            # Invalid responses propagate; never reinterpret them as legacy data.
            return atomic(requests)
        before = self.worker.rpc.call("getbestblockhash")
        result = {}
        for offset in range(0, len(requests), 100):
            group = requests[offset:offset + 100]
            rows = self.worker.rpc.call("getpgnauditstate", [0, 100, None, group])
            if not isinstance(rows, list) or len(rows) > len(group):
                raise ValueError("Audit lookup returned an oversized page")
            for row in rows:
                request = row.get("request_id")
                if request not in group or request in result:
                    raise ValueError("Audit lookup returned a different or duplicate request")
                result[request] = row
        # A changed branch can be retried later. Never disclose from a mixed view.
        if self.worker.rpc.call("getbestblockhash") != before:
            return None
        return result

    def terminal_on_anchor(self, audit, anchor, states=None):
        if isinstance(states, AnchoredAuditStates):
            if states.anchor != anchor:
                return False
            job = next((job for job in states.get(audit['request_id'], {}).get('jobs', [])
                        if job['slot'] == audit['slot'] and job['attempt'] == audit['attempt']), None)
            return bool(job and job['state'] in ('passed', 'failed')
                        and job.get('commitment') == audit.get('answer', {}).get('commitment')
                        and states.current(self.worker.rpc))
        # Only the final ACK needs this exact atomic projection. The ordinary
        # batched progress read stays cheap. Before/after tip equality alone
        # cannot exclude an A→B→A branch race during that read.
        view = self.worker.rpc.call("getpgnworkview", ["audits", 0, 1, audit["request_id"]])
        state = view.get("state", {})
        rows = view.get("audits", [])
        if (view.get("schema") != "PGN_WORK_VIEW_V1" or view.get("kind") != "audits"
                or view.get("request_id") != audit["request_id"]
                or state.get("genesis") != anchor["genesis"]
                or state.get("bestblockhash") != anchor["hash"]
                or state.get("height") != anchor["height"]
                or not isinstance(rows, list) or len(rows) != 1
                or rows[0].get("request_id") != audit["request_id"]):
            return False
        verdict = next((job for job in rows[0].get("jobs", [])
                        if job.get("slot") == audit["slot"] and job.get("attempt") == audit["attempt"]), None)
        return bool(verdict and verdict.get("state") in ("passed", "failed")
                    and self.worker.rpc.call("getbestblockhash") == anchor["hash"])

    def reveal_committed(self):
        # Existing sealed payloads plus a bounded identity-only recovery index.
        index = self.folder / "pending.json"
        if index.is_file() and index.stat().st_size > 1024 * 1024:
            raise ValueError("Pending audit index exceeds budget")
        pending = json.loads(index.read_bytes()) if index.is_file() else []
        if not isinstance(pending, list) or len(pending) > 4096:
            raise ValueError("Pending audit index exceeds its item budget")
        self.answers.restore_pending(pending)
        self.answers.migrate_step()
        if not self.answers.has_answers():
            return
        anchor = self.answers.observe(self.worker.rpc)
        if anchor is None:
            return
        # The legacy file is now a bounded active window, not the backlog.
        # All its identities were durably registered above before replacement.
        # Rotate through the full catalogue even when every older owner waits.
        saved = self.answers.next_page()
        pending = [name for name, _ in saved]
        # Publish recovery before any send: a crash cannot leave its only queue
        # representation in RAM. Catalogue waiting rows independently survive.
        if not index.is_file() or json.loads(index.read_bytes()) != pending:
            save(index, pending)
        states = self.states(audit["request_id"] for _, audit in saved)
        if states is None:
            return
        if isinstance(states, AnchoredAuditStates):
            anchor = states.anchor
        keep = []
        for name, audit in saved:
            stopping = getattr(self.worker, 'quitting', None)
            if stopping is not None and stopping.is_set():
                return  # The full selected window and catalogue stay durable.
            state = states.get(audit["request_id"], {})
            jobs = state.get("jobs", [])
            job = next((j for j in jobs if j["slot"] == audit["slot"] and j["attempt"] == audit["attempt"]), None)
            if not job:
                # A reorganization can temporarily hide this assignment.
                keep.append(name)
                continue
            if job["state"] in ("passed", "failed"):
                if not self.terminal_on_anchor(audit, anchor, states):
                    keep.append(name)
                    continue
                self.answers.acknowledge(name)
                continue
            if job["state"] != "committed" or not state.get("details_root"):
                keep.append(name)
                continue
            proposal = self.apply("reveal", audit["request_id"], audit["slot"], audit["attempt"], canonical_bytes(audit["answer"]["body"]).hex())
            if not proposal.get("available"):
                # The assignment can change between the initial read and send.
                # An unavailable proposal is not a publication acknowledgement.
                keep.append(name)
                continue
            confirmed = self.state(audit["request_id"])
            verdict = next((j for j in confirmed.get("jobs", [])
                            if j["slot"] == audit["slot"] and j["attempt"] == audit["attempt"]), None)
            if not verdict or verdict["state"] not in ("passed", "failed"):
                # Retain the same durable answer across delayed confirmation,
                # reorganization and another attempt's unrelated verdict.
                keep.append(name)
            elif not self.terminal_on_anchor(audit, anchor):
                keep.append(name)
            else:
                self.answers.acknowledge(name)
        if keep != pending:
            save(index, keep)

    def track_owner_report(self, work, sealed):
        self.owner_reports.import_legacy()
        self.owner_reports.register(work["request_id"], dict(ticket=work["ticket"], report_root=sealed["report_root"]))

    def owner_terminal_on_anchor(self, request, anchor, states=None):
        if isinstance(states, AnchoredAuditStates):
            return bool(states.anchor == anchor and states.get(request, {}).get('audited') is True
                        and states.current(self.worker.rpc))
        view = self.worker.rpc.call("getpgnworkview", ["audits", 0, 1, request])
        state, rows = view.get("state", {}), view.get("audits", [])
        return bool(view.get("schema") == "PGN_WORK_VIEW_V1" and view.get("kind") == "audits"
                    and view.get("request_id") == request
                    and state.get("genesis") == anchor["genesis"]
                    and state.get("bestblockhash") == anchor["hash"] and state.get("height") == anchor["height"]
                    and isinstance(rows, list) and len(rows) == 1 and rows[0].get("request_id") == request
                    and rows[0].get("audited") is True
                    and self.worker.rpc.call("getbestblockhash") == anchor["hash"])

    def reveal_owner_reports(self):
        try:
            return self._reveal_owner_reports()
        except Exception:
            self.owner_reports.invalidate_sweep()
            self.owner_poll_more = True
            raise

    def _reveal_owner_reports(self):
        self.owner_poll_more = False
        self.owner_reports.import_legacy()
        if not self.owner_reports.has_reports():
            return
        anchor = self.owner_reports.observe(self.worker.rpc)
        if anchor is None:
            self.owner_poll_more = True
            return
        page, self.owner_poll_more = self.owner_reports.next_on_anchor(anchor)
        pending = dict(page)
        if not pending:
            return
        states = self.states(pending)
        if states is None:
            self.owner_reports.invalidate_sweep()
            self.owner_poll_more = True
            return
        if isinstance(states, AnchoredAuditStates):
            anchor = states.anchor
        first_error = None
        ready = []
        for request, item in list(pending.items()):
            stopping = getattr(self.worker, 'quitting', None)
            if stopping is not None and stopping.is_set():
                self.owner_reports.invalidate_sweep()
                self.owner_poll_more = True
                return
            try:
                state = states.get(request, {})
                jobs = state.get("jobs", [])
                if not state.get("audited") and (state.get("details_root") or len(jobs) != 2 or any(j["state"] == "assigned" for j in jobs)):
                    continue
                source = self.worker.root / "tickets" / item["ticket"] / "network-report.json"
                if source.stat().st_size > 262144:
                    raise ValueError("Owner report exceeds budget")
                sealed = json.loads(source.read_bytes())
                if (sealed["report_root"] != item["report_root"] or not isinstance(sealed.get("body"), dict)
                        or hashlib.sha256(canonical_bytes(sealed["body"])).hexdigest() != item["report_root"]):
                    raise ValueError("Owner report differs from committed index")
                # This score changes scheduling only. Current Core policies and
                # the exact proof still decide whether a Gold can be accepted.
                merit = max((int(record['gap']) / math.log(int(record['lower_prime']))
                             for record in sealed['body']['computed']['best']), default=0.0)
                ready.append((merit, request, item, sealed, state))
            except Exception as error:
                if first_error is None:
                    first_error = error
        # Preserve the durable round-robin page: high-merit reports get the
        # first publication within it, without starving later bounded pages.
        for _, request, item, sealed, state in sorted(ready, key=lambda row: -row[0]):
            stopping = getattr(self.worker, 'quitting', None)
            if stopping is not None and stopping.is_set():
                self.owner_reports.invalidate_sweep()
                self.owner_poll_more = True
                return
            try:
                jobs = state.get('jobs', [])
                if not state.get("details_root") and len(jobs) == 2 and all(j["state"] != "assigned" for j in jobs):
                    proposal = self.apply("reveal_report", request, 0, 0, canonical_bytes(sealed["body"]).hex())
                    if not proposal.get("available"):
                        self.owner_reports.invalidate_sweep()
                        self.owner_poll_more = True
                        continue
                    state = self.state(request)
                if state.get("audited"):
                    with self.worker.network_lock:
                        outcome = self.worker.submit_gold(request, sealed, state["details_root"])
                    # A successful Gold publication advances the active chain.
                    # Confirm the terminal report on that new anchor, not the
                    # necessarily obsolete pre-publication tip.
                    terminal_anchor = self.owner_reports.observe(self.worker.rpc) if outcome == "accepted" else anchor
                    if (outcome in ("accepted", "no_gold") and terminal_anchor is not None
                            and self.owner_terminal_on_anchor(request, terminal_anchor,
                                  None if outcome == 'accepted' else states)):
                        self.owner_reports.acknowledge(request, item)
                    else:
                        self.owner_reports.invalidate_sweep()
                        self.owner_poll_more = True
            except Exception as error:
                if first_error is None:
                    first_error = error
        if first_error is not None:
            raise first_error

    def process_one(self):
        self.last_unavailable = None
        self.current_audit = None
        journal = self.folder / "current.json"
        if journal.is_file():
            if journal.stat().st_size > 262144:
                raise ValueError("Current audit exceeds budget")
            audit = json.loads(journal.read_bytes())
            current = self.state(audit["request_id"])
            job = next((j for j in current.get("jobs", []) if j["slot"] == audit["slot"] and j["attempt"] == audit["attempt"]), None)
            if not job or job["state"] != "assigned":
                if self.on_committed is not None:
                    if (job and job["state"] in ("committed", "passed", "failed")
                            and "commitment" not in job):
                        raise RuntimeError("PGN Core audit RPC does not expose the committed result digest; update the Core component before resuming; private computation retained")
                    if (not job or job["state"] not in ("committed", "passed", "failed")
                            or job.get("commitment") != audit.get("answer", {}).get("commitment")
                            or not audit.get("publication_intent")):
                        raise RuntimeError("Audit assignment changed; private computation retained for reconciliation")
                    # The per-audit intent survives another lane overwriting the
                    # global publication journal before a crash or GUI restart.
                    confirm_candidate(self.worker.rpc, audit["publication_intent"])
                    self.on_committed(audit, audit["publication_intent"]["hash"])
                journal.unlink()
                audit = None
        else:
            audit = None
        if audit is None:
            proposal = self.apply("assign")
            if not proposal.get("available"):
                self.last_unavailable = proposal
                return "none"
            reports = self.worker.rpc.call("getpgnreports", [0, 1, proposal["request_id"]])
            if len(reports) != 1:
                raise ValueError("Assigned audit has no immutable report")
            report = reports[0]
            audit = dict(schema="PGN_HORIZON_ASSIGNED_AUDIT_V1", request_id=proposal["request_id"], ticket=report["ticket"], report_root=report["report_root"], slot=proposal["slot"], attempt=proposal["attempt"], ranges=proposal["ranges"], counts=[])
            audit = self.restore_reassigned_answer(audit)
            save(journal, audit)
        elif audit.get('publication_intent') and self.reassignment_needed is not None and self.reassignment_needed(audit):
            audit = self.restore_reassigned_answer(audit)
        self.current_audit = audit
        self.reconcile_reassigned_answer(audit)
        if self.on_assigned is not None:
            self.on_assigned(audit)
            save(journal, audit)
        if "target_merit_q16" not in audit:
            work = self.worker.rpc.call("getpgnscientificwork", [audit["request_id"], audit["report_root"]])
            numerator, denominator = int(work["target_numerator"]), int(work["target_denominator"])
            if denominator <= 0 or numerator <= 0 or numerator * 65536 % denominator:
                raise ValueError("Invalid fixed-point audit target")
            audit["target_merit_q16"] = numerator * 65536 // denominator
            save(journal, audit)
        if type(audit["target_merit_q16"]) is not int or not 0 < audit["target_merit_q16"] < 1 << 31:
            raise ValueError("Invalid retained audit target")
        register = getattr(self.worker.source, 'register_preparation', None)
        acknowledge = getattr(self.worker.source, 'acknowledge_preparation', None)
        retain = getattr(self.worker.source, 'retain_audit_partition', None)
        if callable(acknowledge):
            for part in audit['ranges'][:len(audit['counts'])]:
                if callable(retain):retain(audit,part)
                acknowledge(partition_ticket(audit, part)['ticket_id'])
        remaining = audit['ranges'][len(audit['counts']):]
        if remaining and callable(register):
            register([partition_ticket(audit, part) for part in remaining])
        for i in range(len(audit["counts"]), len(audit["ranges"])):
            if not self.worker.running.is_set():
                return "paused"
            part = audit["ranges"][i]
            start, end = int(part["start"]), int(part["end"])
            job = partition_ticket(audit, part)
            identity = job['ticket_id']
            self.presentation = audit_presentation(audit, part, self.worker.root / "tickets" / identity)
            result = self.worker.source.run_ticket(job, self.worker.args.piece_limit)
            if result.get('state') != 'PAUSED':
                finalizer = getattr(self.worker.source, 'finalize_report', None)
                if callable(finalizer):
                    from report_commitment import validate_computed_report
                    work = dict(ticket=job['ticket_id'], start=job['start'],
                                length=str(int(job['end'])-int(job['start'])))
                    result = finalizer(work, result)
                    validate_computed_report(result, work)
            if result.get("state") == "PAUSED":
                return "paused"
            audit["counts"].append(sum(int(n) for n in result["body"]["counts"]))
            save(journal, audit)
            if callable(retain):retain(audit,part,result)
            if callable(acknowledge):
                acknowledge(identity)
        if self.before_commit is not None and not self.before_commit(audit):
            return "paused"
        with self.worker.network_lock:
            answer = audit.get("answer")
            if answer is None:
                answer = audit_commitment(audit["ticket"], audit["attempt"], [r["index"] for r in audit["ranges"]], [str(n) for n in audit["counts"]], secrets.token_hex(32))
                audit["answer"] = answer
                save(journal, audit)
            name = audit["request_id"] + "-" + str(audit["slot"]) + "-" + str(audit["attempt"])
            save(self.folder / (name + ".json"), audit)
            # Persist independent recovery before publication, even if the
            # active window is full of older withheld-owner responses.
            self.answers.register([name])
            index = self.folder / "pending.json"
            if index.is_file()and index.stat().st_size>1024*1024:
                raise ValueError("Pending audit index exceeds budget")
            pending = json.loads(index.read_bytes()) if index.is_file() else []
            if not isinstance(pending,list)or len(pending)>4096:
                raise ValueError("Pending audit index exceeds its item budget")
            if name not in pending and len(pending)<64:
                pending.append(name)
            save(index, pending)
            def retain_publication(intent):
                validate_intent(intent)
                audit["publication_intent"] = dict(intent)
                if len(canonical_bytes(audit)) > 262144:
                    raise BufferError("Audit publication journal exceeds its budget")
                save(journal, audit)
                save(self.folder / (name + ".json"), audit)
            receipt = self.apply("commit", audit["request_id"], audit["slot"], audit["attempt"], answer["commitment"],
                                 on_intent=retain_publication if self.on_committed is not None else None)
            if not receipt.get("available"):
                raise RuntimeError("Audit commitment unavailable; private answer retained")
            if self.on_committed is not None:
                if not receipt.get("confirmed_block"):
                    raise RuntimeError("Audit confirmation receipt unavailable; private answer retained")
                self.on_committed(audit, receipt["confirmed_block"])
            journal.unlink()
            self.presentation = None
            self.current_audit = None
            return "committed"
