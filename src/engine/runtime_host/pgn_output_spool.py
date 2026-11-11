"""Bounded native output transport, retired only after a durable consumer ACK.

This module owns no process and calls no GPU API. A reader reaching EOF is NOT
an acknowledgement: its caller must first commit the corresponding work to the
session journal. The active file and every unacknowledged byte are retained.
"""
from pathlib import Path
import hashlib
import json
import os
import re
import time
import uuid
from pgn_output_capacity import OWNER_CAPACITY, admit


SCHEMA = "PGN_OUTPUT_SPOOL_V1"
SEGMENT_BYTES = 8 * 1024 * 1024
MAXIMUM_BYTES = OWNER_CAPACITY
MAXIMUM_LINE = 65536
MAXIMUM_SEGMENTS = 128
ZERO_LINK = "0" * 64
BINDING_SCHEMA = "PGN_OUTPUT_JOB_BINDING_V2"


class SpoolError(RuntimeError):
    pass


def _atomic(path, value):
    path = Path(path)
    tmp = path.with_name(path.name + "." + uuid.uuid4().hex + ".pending")
    with tmp.open("w", encoding="utf-8", newline="\n") as stream:
        json.dump(value, stream, sort_keys=True, separators=(",", ":"))
        stream.flush()
        os.fsync(stream.fileno())
    deadline = time.monotonic() + 5
    while True:
        try:
            os.replace(tmp, path)
            if os.name != 'nt':
                directory = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
                try:
                    os.fsync(directory)
                finally:
                    os.close(directory)
            break
        except PermissionError:
            if time.monotonic() >= deadline:
                raise
            time.sleep(.01)


def _json(path):
    path = Path(path)
    if path.is_symlink() or path.stat().st_size > 65536:
        raise SpoolError("Invalid spool metadata")
    value = json.loads(path.read_bytes())
    if not isinstance(value, dict):
        raise SpoolError("Invalid spool metadata shape")
    return value


def _name(index):
    if type(index) is not int or not 0 <= index < 10**12:
        raise SpoolError("Invalid output segment index")
    return f"{index:012d}.log"


def _link(previous, index, start, size, digest):
    body = f"{previous}:{index}:{start}:{size}:{digest}".encode("ascii")
    return hashlib.sha256(body).hexdigest()


def _encoded(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":")).encode("utf-8")


def _binding_name(sequence, nonce):
    _name(sequence)
    if not isinstance(nonce, str) or not re.fullmatch(r"[0-9a-f]{32}", nonce):
        raise SpoolError("Invalid bound job nonce")
    return f"job-{sequence:012d}-{nonce}.json"


def _immutable(path, data):
    if len(data) > 65536:
        raise SpoolError("Oversized immutable output binding")
    if path.exists():
        if path.is_symlink() or path.read_bytes() != data:
            raise SpoolError("Immutable output binding changed")
        return
    with path.open("xb") as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())


def commit_job(root, token, nonce, sequence, command, output_start, identity_path):
    """Freeze the current calculation, never the resident bootstrap or a plan."""
    if not re.fullmatch(r"[0-9a-f]{32}", token) or type(output_start) is not int or output_start < 0:
        raise SpoolError("Invalid output binding owner/frontier")
    if not isinstance(command, (list, tuple)) or not command or "--plan" in command:
        raise SpoolError("Only a calculation can bind recoverable output")
    command = list(map(str, command))
    if len(command) > 96 or any(not a or "\x00" in a for a in command):
        raise SpoolError("Invalid output binding command")
    root = Path(root)
    saved = _json(identity_path)
    if saved.get("job_nonce") != nonce or type(saved.get("pid")) is not int or not saved.get("birth"):
        raise SpoolError("Current journal identity differs from output job")
    name = _binding_name(sequence, nonce)
    identity_name = name[:-5] + ".identity.json"
    identity_bytes = _encoded(saved)
    _immutable(root / identity_name, identity_bytes)
    binding = dict(schema=BINDING_SCHEMA, token=token, nonce=nonce, sequence=sequence,
                   command=command, output_start=output_start, identity=identity_name,
                   identity_sha256=hashlib.sha256(identity_bytes).hexdigest())
    _immutable(root / name, _encoded(binding))
    return binding


def dispatch_job(root, binding):
    """A dispatch intent is not proof of execution; seal_job requires receipt."""
    name = _binding_name(binding["sequence"], binding["nonce"])
    _atomic(Path(root) / "dispatched-binding.json", dict(schema=BINDING_SCHEMA,
            binding=name, sha256=hashlib.sha256(_encoded(binding)).hexdigest()))


def recovery_locator(root, binding):
    """A ticket ID is a bounded sibling component, never a supplied path."""
    root = Path(root).resolve()
    owner_ticket = root.parent.name
    if not re.fullmatch(r'[0-9a-f]{64}', owner_ticket):
        raise SpoolError('Output owner is not inside a canonical ticket folder')
    return dict(token=binding['token'], owner_ticket_id=owner_ticket,
                sequence=binding['sequence'], nonce=binding['nonce'],
                binding=_binding_name(binding['sequence'], binding['nonce']),
                binding_sha256=hashlib.sha256(_encoded(binding)).hexdigest())


def bound_job_context(root, token):
    """Resolve stop/checkpoint only from the durable current executed intent.

    Dispatch is enough to request its safe stop; a native WorkReceipt is still
    separately required by seal_job before recovery can accept any work.
    """
    from pgn_session import read_session_metadata
    root = Path(root).resolve()
    pointer_path = root / 'dispatched-binding.json'
    if not pointer_path.exists():
        return None
    pointer = _json(pointer_path)
    name = pointer.get('binding', '')
    if Path(name).name != name or not name.startswith('job-'):
        raise SpoolError('Invalid dispatched owner binding path')
    binding = _json(root / name)
    if binding.get('schema') != BINDING_SCHEMA or binding.get('token') != token or name != _binding_name(binding.get('sequence'), binding.get('nonce')) or hashlib.sha256(_encoded(binding)).hexdigest() != pointer.get('sha256'):
        raise SpoolError('Dispatched current-job binding differs')
    identity_name = name[:-5] + '.identity.json'
    if binding.get('identity') != identity_name:
        raise SpoolError('Dispatched current-job identity path differs')
    identity = _json(root / identity_name)
    if hashlib.sha256((root / identity_name).read_bytes()).hexdigest() != binding.get('identity_sha256'):
        raise SpoolError('Dispatched current-job identity differs')
    ticket = identity.get('ticket_id')
    if not isinstance(ticket, str) or not re.fullmatch(r'[0-9a-f]{64}', ticket):
        return None  # historical same-folder V2 has no cross-ticket authority
    folder = root.parent.parent / ticket
    if folder.resolve().parent != root.parent.parent or folder.is_symlink():
        raise SpoolError('Current-job ticket escaped its owner namespace')
    metadata = read_session_metadata(folder)
    chunk = metadata.get('current_chunk') or {}
    if not chunk:
        return None  # already durably closed work is not a new recovery job
    ready = _json(root / 'ready.json')
    expected_locator = recovery_locator(root, binding)
    checks = (ready.get('token') == token,
              str(ready.get('image_sha256', '')).upper() == str(identity.get('image_sha256', '')).upper(),
              metadata.get('session_id') == identity.get('session_id'),
              hashlib.sha256(_encoded(metadata.get('configuration'))).hexdigest().upper() == str(identity.get('configuration_sha256', '')).upper())
    if not all(checks):
        raise SpoolError('Current-job durable instance/owner binding differs')
    command = binding.get('command', [])
    def argument(flag):
        if command.count(flag) != 1 or command.index(flag) + 1 >= len(command):
            raise SpoolError('Current-job control flag differs')
        return command[command.index(flag) + 1]
    state, stop = folder / 'native-checkpoint.bin', folder / 'stop.request'
    if Path(argument('--state')).resolve() != state.resolve() or Path(argument('--stop-file')).resolve() != stop.resolve():
        raise SpoolError('Current-job controls are not its canonical ticket files')
    def matches(candidate):
        instance = candidate.get('instance') or {}
        worker = candidate.get('identity') or {}
        return (instance.get('output_owner') == expected_locator
                and instance.get('job_nonce') == binding['nonce']
                and instance.get('pid') == identity.get('pid') == ready.get('pid')
                and str(instance.get('birth')) == str(identity.get('birth')) == str(ready.get('birth'))
                and str(worker.get('worker_sha256', '')).upper() == str(identity.get('image_sha256', '')).upper()
                and worker.get('profile_k') == identity.get('profile_k')
                and all(int(argument(flag)) == int(worker.get(key, -1))
                        for flag, key in (('--start','start'),('--length','length'),
                                          ('--gap-min','gap_min'),('--emit-min-gap','emit_min_gap'))))
    if not matches(chunk):
        # A completed job's receipt must not be interpreted against its next,
        # not-yet-dispatched chunk. Only an exact work_closed WAL record makes
        # it safe to finish this owner without publishing a new recovery job.
        previous = metadata.get('closed_chunks', [])
        for retired in previous[-10:]:
            if (retired.get('instance') or {}).get('work_closed') is True and matches(retired):
                return None
        index = chunk.get('index')
        if type(index) is int and index > 0:
            from pgn_index import read_closed_chunk
            retired = read_closed_chunk(folder / 'session-index.sqlite3', index - 1,
                                        metadata['session_id'],
                                        str(identity['configuration_sha256']).lower())
            if retired and (retired.get('instance') or {}).get('work_closed') is True and matches(retired):
                return None
        raise SpoolError('Current-job durable instance/owner binding differs')
    return dict(folder=folder, checkpoint=state, stop=stop, binding=binding,
                locator=expected_locator, identity=identity)


def seal_job(root, token, bootstrap_command):
    """Attach only an actually executed exact native resident WorkReceipt."""
    root = Path(root)
    if not (root / "dispatched-binding.json").exists():
        return None
    pointer = _json(root / "dispatched-binding.json")
    name = pointer.get("binding", "")
    if Path(name).name != name or not name.startswith("job-"):
        raise SpoolError("Redirected dispatched binding")
    binding = _json(root / name)
    if pointer.get("schema") != BINDING_SCHEMA or binding.get('schema') != BINDING_SCHEMA or hashlib.sha256(_encoded(binding)).hexdigest() != pointer.get("sha256") or binding.get("token") != token or name != _binding_name(binding.get("sequence"), binding.get("nonce")):
        raise SpoolError("Dispatched output binding changed")
    if bootstrap_command.count("--resident-directory") != 1:
        return None
    directory = Path(bootstrap_command[bootstrap_command.index("--resident-directory") + 1])
    receipt_path = directory / f'{binding["sequence"]}.done.json'
    if not receipt_path.exists():
        return None  # committed/dispatched but not executed is never promoted
    receipt = _json(receipt_path)
    if binding.get('identity') != name[:-5] + '.identity.json':
        raise SpoolError('Redirected bound journal identity')
    identity_path = root / binding["identity"]
    identity = _json(identity_path)
    if hashlib.sha256(identity_path.read_bytes()).hexdigest() != binding["identity_sha256"]:
        raise SpoolError("Bound journal identity changed")
    if (receipt.get("schema"), receipt.get("sequence"), receipt.get("nonce"), receipt.get("pid"), receipt.get("exit")) != ("SCOUT_RESIDENT_JOB_V1", binding["sequence"], binding["nonce"], identity["pid"], 0) or type(receipt.get('exit')) is not int:
        return None
    return dict(binding=binding, binding_sha256=pointer["sha256"], work_receipt=receipt,
                work_receipt_sha256=hashlib.sha256(receipt_path.read_bytes()).hexdigest())


class Writer:
    def __init__(self, root, token, segment_bytes=SEGMENT_BYTES,
                 maximum_bytes=MAXIMUM_BYTES):
        if not re.fullmatch(r"[0-9a-f]{32}", token):
            raise SpoolError("Invalid owner token")
        if not 64 <= segment_bytes <= SEGMENT_BYTES or not segment_bytes <= maximum_bytes <= MAXIMUM_BYTES:
            raise SpoolError("Invalid output budget")
        self.root = Path(root)
        self.token = token
        self.limit, self.budget = segment_bytes, maximum_bytes
        self.index = self.offset = self.size = self.base = 0
        self.anchor = self.link = ZERO_LINK
        self.segments, self.retired = [], []
        self.closed = False
        self.last_ack_check = 0.0
        self.digest = hashlib.sha256()
        with admit(self.root, token):
            self.root.mkdir(exist_ok=False)
            self.stream = (self.root / _name(0)).open("xb")
            try:
                self._publish()
            except BaseException:
                self.stream.close()
                raise

    def _document(self, finished=False):
        return dict(schema=SCHEMA, token=self.token, base_offset=self.base,
                    base_link=self.anchor, segments=list(self.segments),
                    next_index=self.index, end_offset=self.offset,
                    finished=finished)

    def _publish(self, finished=None):
        _atomic(self.root / "index.json", self._document(self.closed if finished is None else finished))

    def _reap(self):
        remaining = []
        for entry in self.retired:
            try:
                (self.root / _name(entry["index"])).unlink(missing_ok=True)
            except PermissionError:
                remaining.append(entry)
        self.retired = remaining

    def collect(self):
        """Consume only an ACK matching an exact sealed segment boundary."""
        self._reap()
        ack = self.root / "ack.json"
        if not ack.exists():
            return
        try:
            value = _json(ack)
        except (PermissionError, FileNotFoundError):
            # Windows can briefly deny a read while the consumer atomically
            # replaces this hint. Keep every unacknowledged byte and collect
            # on the next pass; no wait belongs in the GPU output path.
            return
        if value.get("schema") != SCHEMA or value.get("token") != self.token:
            raise SpoolError("Output acknowledgement identity mismatch")
        end = value.get("end_offset")
        if type(end) is not int:
            raise SpoolError("Invalid output acknowledgement offset")
        if end <= self.base:
            # Repeating an acknowledged publication is harmless. A different
            # old digest cannot authorize any new deletion.
            return
        target = next((s for s in self.segments if s["end"] == end), None)
        if target is None or any(value.get(k) != target[k] for k in ("index", "sha256", "link")):
            raise SpoolError("Output acknowledgement has no sealed prefix")
        retired = [s for s in self.segments if s["index"] <= target["index"]]
        self.segments = [s for s in self.segments if s["index"] > target["index"]]
        self.base, self.anchor = target["end"], target["link"]
        # Commit the new recovery boundary BEFORE unlinking the old prefix.
        # A crash in between leaves extra files, never missing referenced data.
        self._publish()
        self.retired.extend(retired)
        self._reap()

    def _seal(self):
        self.stream.flush()
        os.fsync(self.stream.fileno())
        self.stream.close()
        if not self.size:
            (self.root / _name(self.index)).unlink()
            return
        start = self.offset - self.size
        sha = self.digest.hexdigest()
        self.link = _link(self.link, self.index, start, self.size, sha)
        self.segments.append(dict(index=self.index, start=start, end=self.offset,
                                  bytes=self.size, sha256=sha, link=self.link))

    def append(self, line):
        if self.closed:
            raise SpoolError("Output writer is closed")
        if not isinstance(line, bytes) or not line.endswith(b"\n") or b"\n" in line[:-1] or len(line) > MAXIMUM_LINE:
            raise SpoolError("Invalid native output line")
        if len(line) > self.limit:
            raise SpoolError("Native line exceeds segment budget")
        if time.monotonic() - self.last_ack_check >= .25:
            self.collect()
            self.last_ack_check = time.monotonic()
        if self.size and self.size + len(line) > self.limit:
            if len(self.segments) >= MAXIMUM_SEGMENTS - 1:
                self.collect()
                if len(self.segments) >= MAXIMUM_SEGMENTS - 1:
                    raise SpoolError("Output segment table exhausted before durable acknowledgement")
            self._seal()
            self.index += 1
            self.size = 0
            self.digest = hashlib.sha256()
            self.stream = (self.root / _name(self.index)).open("xb")
            self._publish()
            self.collect()
        retained = self.size + sum(s["bytes"] for s in self.segments + self.retired)
        if retained + len(line) > self.budget:
            # Backpressure is an explicit failure, never overwrite uncommitted
            # work or silently acknowledge merely-read data.
            raise SpoolError("Native output budget exhausted before durable acknowledgement")
        self.stream.write(line)
        self.stream.flush()
        self.digest.update(line)
        self.size += len(line)
        self.offset += len(line)

    def finish(self):
        if not self.closed:
            self._seal()
            self.closed = True
            self.collect()
            self._publish(finished=True)
        return self._document(finished=True)


def physical_frontier(root, value):
    """Published sealed prefix plus the current file's actual bounded size."""
    segments = value.get("segments")
    index, end, base = value.get("next_index"), value.get("end_offset"), value.get("base_offset")
    if not isinstance(segments, list) or len(segments) > MAXIMUM_SEGMENTS or type(end) is not int or type(base) is not int or not 0 <= base <= end:
        raise SpoolError("Invalid physical output frontier")
    path = Path(root) / _name(index)
    if value.get("finished") is True or any(s.get("index") == index for s in segments):
        return end
    start = segments[-1]["end"] if segments else base
    if type(start) is not int or start < base or path.is_symlink() or not path.is_file():
        raise SpoolError("Missing active output frontier")
    size = path.stat().st_size
    if not 0 <= size <= SEGMENT_BYTES or end > start + size:
        raise SpoolError("Invalid active output frontier")
    return start + size


def acknowledge(root, token, consumed_offset):
    """Call only AFTER the journal has durably accepted this whole prefix.

    A partially consumed segment remains on disk. No process exit, reader EOF
    or native result file substitutes for the caller's durable publication.
    """
    if type(consumed_offset) is not int or consumed_offset < 0:
        raise SpoolError("Invalid durable output offset")
    root = Path(root)
    value = _json(root / "index.json")
    if value.get("schema") != SCHEMA or value.get("token") != token:
        raise SpoolError("Output acknowledgement owner changed")
    if consumed_offset > physical_frontier(root, value):
        raise SpoolError("Durable acknowledgement is beyond physical output")
    eligible = [s for s in value["segments"] if s["end"] <= consumed_offset]
    if not eligible:
        return False
    target = eligible[-1]
    ack = dict(schema=SCHEMA, token=token,
               **{k: target[k] for k in ("index", "sha256", "link")},
               end_offset=target["end"])
    _atomic(root / "ack.json", ack)
    return True


def verified_segments(root, seal, token):
    """Verify a closed snapshot in full before the caller replays any bytes."""
    if not isinstance(seal, dict) or seal.get("schema") != SCHEMA or seal.get("token") != token or seal.get("finished") is not True:
        raise SpoolError("Unsealed output snapshot")
    segments = seal.get("segments")
    if not isinstance(segments, list) or len(segments) > MAXIMUM_SEGMENTS:
        raise SpoolError("Invalid output segment table")
    offset, previous = seal.get("base_offset"), seal.get("base_link")
    if type(offset) is not int or offset < 0 or not isinstance(previous, str) or not re.fullmatch(r"[0-9a-f]{64}", previous):
        raise SpoolError("Invalid output prefix anchor")
    paths, retained, prior_index = [], 0, -1
    for s in segments:
        if not isinstance(s, dict):
            raise SpoolError("Invalid output segment entry")
        index, size = s.get("index"), s.get("bytes")
        path = Path(root) / _name(index)
        if type(size) is not int or not 0 < size <= SEGMENT_BYTES or index <= prior_index or s.get("start") != offset or s.get("end") != offset + size:
            raise SpoolError("Noncontiguous output snapshot")
        retained += size
        if retained > MAXIMUM_BYTES or path.is_symlink() or not path.is_file() or path.stat().st_size != size:
            raise SpoolError("Missing, oversized or redirected output segment")
        sha = hashlib.sha256(path.read_bytes()).hexdigest()
        previous = _link(previous, index, offset, size, sha)
        if sha != s.get("sha256") or previous != s.get("link"):
            raise SpoolError("Output segment digest mismatch")
        paths.append(path)
        offset += size
        prior_index = index
    if offset != seal.get("end_offset"):
        raise SpoolError("Output snapshot frontier mismatch")
    return tuple(paths)


class OutputLine(str):
    def __new__(cls, data, end_offset):
        value = str.__new__(cls, data.decode("utf-8"))
        value.end_offset = end_offset
        return value


class Reader:
    """Read one fresh owner's ordered output without implicitly retiring it."""
    def __init__(self, root, finished):
        self.root, self.finished = Path(root), finished
        self.index = self.position = 0
        self.stream = None
        self.partial = b""
        self.closed = False
        self._finished_observed = False

    def __iter__(self):
        return self

    def __next__(self):
        while not self.closed:
            path = self.root / _name(self.index)
            if self.stream is None and path.exists():
                if path.is_symlink():
                    raise SpoolError("Redirected live output segment")
                self.stream = path.open("rb")
            data = self.stream.readline(MAXIMUM_LINE + 1 - len(self.partial)) if self.stream else b""
            if data:
                self.partial += data
                if len(self.partial) > MAXIMUM_LINE:
                    raise SpoolError("Oversized live output line")
                if self.partial.endswith(b"\n"):
                    complete, self.partial = self.partial, b""
                    self.position += len(complete)
                    return OutputLine(complete, self.position)
            elif (self.root / _name(self.index + 1)).exists():
                if self.partial:
                    raise SpoolError("Partial line at output segment boundary")
                if self.stream:
                    self.stream.close()
                    self.stream = None
                self.index += 1
            elif self.finished():
                # The producer can publish its last bytes/next segment between
                # our EOF/next checks and this completion observation. Observe
                # completion first, then reread BOTH boundaries once more.
                if not self._finished_observed:
                    self._finished_observed = True
                    continue
                if self.partial:
                    raise SpoolError("Incomplete final native output line")
                self.close()
                raise StopIteration
            else:
                time.sleep(.01)
        raise StopIteration

    def acknowledge(self, token, consumed_offset):
        """Explicit caller durability is still required; delivered != durable."""
        if type(consumed_offset) is not int or consumed_offset > self.position:
            raise SpoolError("Acknowledgement exceeds actually delivered output")
        return acknowledge(self.root, token, consumed_offset)

    def close(self):
        self.closed = True
        if self.stream:
            self.stream.close()
            self.stream = None
