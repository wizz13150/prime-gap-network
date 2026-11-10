"""Per-discovery Gold receipts. Core alone verifies proof and monetary acceptance.

The old V2 receipt names a report, not its discovery. Recover that identity
from its canonical block rather than guessing that it contains the best gap.
"""
import json
import math
from pathlib import Path
from horizon_core_rpc import CoreRpcError
from horizon_gold_proof import push


class Reader:
    def __init__(self, data): self.data, self.at = data, 0
    def take(self, size):
        if not 0 <= size <= len(self.data)-self.at:
            raise ValueError('Truncated Gold discovery identity')
        value = self.data[self.at:self.at+size]; self.at += size
        return value
    def count(self):
        first = self.take(1)[0]
        if first < 253: return first
        size = {253: 2, 254: 4, 255: 8}[first]
        value = int.from_bytes(self.take(size), 'little')
        if value < {253: 253, 254: 65536, 255: 4294967296}[first]:
            raise ValueError('Noncanonical Gold length')
        return value
    def field(self, maximum):
        size = self.count()
        if size > maximum: raise ValueError('Gold field exceeds budget')
        return self.take(size)
    def integer(self):
        raw = self.field(32)
        if raw and raw[0] == 0: raise ValueError('Noncanonical Gold integer')
        return int.from_bytes(raw, 'big')
    def domain(self, expected):
        if self.take(len(expected)) != expected: raise ValueError('Foreign Gold proof identity')
    def end(self):
        if self.at != len(self.data): raise ValueError('Trailing Gold identity bytes')


def discovery(script, binding, computed):
    """Decode identity only, from an already Core-accepted block; not a verifier."""
    if not isinstance(script, str) or len(script) > 8*1024*1024:
        raise ValueError('Gold script exceeds budget')
    raw = bytes.fromhex(script)
    if raw.hex() != script or not raw.startswith(b'\x6a'):
        raise ValueError('Canonical Gold script required')
    reader = Reader(raw[1:]); fields = []
    for _ in range(2):
        op = reader.take(1)[0]
        size = op if op <= 75 else int.from_bytes(reader.take({76: 1, 77: 2, 78: 4}.get(op, 0)), 'little')
        value = reader.take(size)
        fields.append(value)
    reader.end()
    if raw != b'\x6a'+push(fields[0])+push(fields[1]) or fields[0] != b'PGNPROOF-UNAPPROVED-1':
        raise ValueError('Foreign or noncanonical Gold envelope')
    proof = Reader(fields[1]); proof.domain(b'PGN/SCIENCE-BLOCK/1\0')
    if (proof.take(32)[::-1].hex() != binding['request_id']
            or proof.take(32).hex() != binding['details_root']):
        raise ValueError('Gold block differs from audited report')
    proof.field(10000); segment = Reader(proof.field(32768))
    slot = int.from_bytes(proof.take(4), 'little'); siblings = proof.count()
    if slot >= 100 or siblings > 32: raise ValueError('Invalid Gold segment proof')
    proof.take(siblings*32); index = int.from_bytes(proof.take(8), 'little')
    proof.field(4*1024*1024); proof.end()
    segment.domain(b'PGN segment result V1\0')
    if segment.take(32).hex() != binding['ticket']: raise ValueError('Gold ticket differs')
    start, length = segment.integer(), segment.integer()
    if (str(start) != computed['start'] or str(start+length) != computed['end']
            or segment.count() != 100 or segment.count() != slot):
        raise ValueError('Gold range differs')
    count = segment.count()
    if not 0 <= index < count <= 100: raise ValueError('Invalid Gold record index')
    lower = start+length*slot//100; chosen = None
    for i in range(count):
        delta, gap, merit = segment.integer(), segment.integer(), segment.count()
        if (i and delta == 0) or gap <= 0 or merit > 4294967295:
            raise ValueError('Invalid Gold segment declaration')
        lower += delta
        if not start+length*slot//100 <= lower < start+length*(slot+1)//100:
            raise ValueError('Gold declaration outside segment')
        if i == index: chosen = (str(lower), gap)
    segment.end()
    if not any((r['lower_prime'], int(r['gap'])) == chosen for r in computed['best']):
        raise ValueError('Accepted discovery absent from retained report')
    return chosen


def load(file):
    if file.is_symlink() or file.stat().st_size > 32768:
        raise ValueError('Gold publication journal exceeds budget')
    with file.open('rb') as stream: raw = stream.read(32769)
    if len(raw) > 32768: raise ValueError('Gold publication journal exceeds budget')
    value = json.loads(raw)
    if not isinstance(value, dict): raise ValueError('Invalid Gold publication journal')
    return value


def active(rpc, receipt):
    try: header = rpc.call('getblockheader', [receipt['block'], True])
    except CoreRpcError as error:
        if error.code == -5: return False
        raise
    return header.get('confirmations', -1) >= 1


def publish(worker, request, sealed, details_root, save, build, assemble):
    body = sealed['body']; computed = body['computed']
    folder = worker.root/'tickets'/body['ticket']; journals = folder/'gold-publications'
    binding = dict(request_id=request, ticket=body['ticket'], report_root=sealed['report_root'],
                   details_root=details_root, genesis=worker.args.genesis)
    records = computed['best']
    if not isinstance(records, list) or len(records) > 100:
        raise ValueError('Gold report record count exceeds budget')
    by_lower = {r['lower_prime']: r for r in records}
    if len(by_lower) != len(records): raise ValueError('Duplicate Gold declaration')
    for lower, record in by_lower.items():
        if (not isinstance(lower, str) or not lower.isascii() or not lower.isdecimal()
                or len(lower) > 39 or str(int(lower)) != lower or int(record['gap']) <= 0):
            raise ValueError('Invalid Gold discovery identity')
    if journals.is_symlink(): raise ValueError('Redirected Gold journal directory refused')
    journals.mkdir(exist_ok=True)
    accepted = set()
    def validate(receipt, lower=None):
        if any(receipt.get(key) != value for key, value in binding.items()):
            raise ValueError('Gold publication journal differs from audited report')
        if receipt.get('schema') == 'PGN_HORIZON_GOLD_PUBLICATION_V3':
            key = receipt.get('lower_prime')
            if (key not in by_lower or (lower is not None and key != lower)
                    or receipt.get('gap') != int(by_lower[key]['gap'])):
                raise ValueError('Gold journal differs from discovery')
        elif receipt.get('schema') != 'PGN_HORIZON_GOLD_PUBLICATION_V2':
            raise ValueError('Unknown Gold publication journal schema')
    # Preserve existing receipts and recover even a crash after submitblock.
    for name in ('network-acceptance.json', 'gold-publication-intent.json'):
        file = folder/name
        if not file.exists(): continue
        receipt = load(file); validate(receipt)
        if not active(worker.rpc, receipt): continue
        if receipt['schema'] == 'PGN_HORIZON_GOLD_PUBLICATION_V2':
            block = worker.rpc.call('getblock', [receipt['block'], 2])
            if block.get('hash') != receipt['block']: raise ValueError('Gold block identity differs')
            matches = []
            for tx in block.get('tx', [])[:1]:
                for output in tx.get('vout', []):
                    script = output.get('scriptPubKey', {}).get('hex', '')
                    if bytes.fromhex(script).startswith(b'\x6a'+push(b'PGNPROOF-UNAPPROVED-1')):
                        matches.append(discovery(script, binding, computed))
            if len(matches) != 1: raise ValueError('Legacy accepted Gold discovery unavailable')
            lower, gap = matches[0]
            receipt = dict(receipt, schema='PGN_HORIZON_GOLD_PUBLICATION_V3', lower_prime=lower, gap=gap)
            save(journals/(lower+'.json'), receipt)
            save(file, receipt)
        accepted.add(receipt['lower_prime'])
    for lower in by_lower:
        file = journals/(lower+'.json')
        if file.exists():
            receipt = load(file); validate(receipt, lower)
            if active(worker.rpc, receipt): accepted.add(lower)
    work = worker.rpc.call('getpgnscientificwork', [request, details_root])
    target = int(work['target_numerator'])/int(work['target_denominator'])
    candidates = sorted((r for r in records if int(r['gap']) >=
        target*math.log(int(r['lower_prime']))*(1-1e-12)),
        key=lambda r: (-int(r['gap'])/math.log(int(r['lower_prime'])), int(r['lower_prime'])))
    pending = [r for r in candidates if r['lower_prime'] not in accepted]
    if not pending: return 'accepted' if accepted else 'no_gold'
    # One attempt per maintenance visit preserves fairness across reports.
    cursor_file = folder/'gold-publication-cursor.json'; cursor = None
    if cursor_file.exists():
        position = load(cursor_file); validate(position)
        cursor = position.get('lower_prime')
    keys = [r['lower_prime'] for r in candidates]
    if cursor in keys:
        pivot = keys.index(cursor)+1; ordered = keys[pivot:]+keys[:pivot]
        record = next(by_lower[key] for key in ordered if key not in accepted)
    else: record = pending[0]
    lower = record['lower_prime']
    save(cursor_file, dict(binding, schema='PGN_HORIZON_GOLD_PUBLICATION_V3',
                           lower_prime=lower, gap=int(record['gap'])))
    evidence = build(worker.args.proof_builder, work, computed, lower)
    if evidence['details_root'] != details_root:
        raise ValueError('Gold evidence does not match audited report details')
    signature = worker.rpc.call('signmessage', [worker.address(), work['challenge']], wallet=True)
    candidate = worker.rpc.call('getpgnscientifictemplate', [assemble(work, evidence, signature)])
    if candidate.get('work_verified') is not True or int(candidate['winner_atomic']) <= 0:
        raise ValueError('Gold candidate has no verified positive reward')
    intent = dict(binding, schema='PGN_HORIZON_GOLD_PUBLICATION_V3', lower_prime=lower,
                  gap=int(record['gap']), block=candidate['hash'], winner_atomic=candidate['winner_atomic'])
    save(journals/(lower+'.json'), intent)
    save(folder/'gold-publication-intent.json', intent)
    result = worker.rpc.call('submitblock', [candidate['block']])
    if result is not None: raise RuntimeError('Gold candidate was not accepted: '+str(result))
    if not active(worker.rpc, intent):
        raise RuntimeError('Gold is not yet on the active chain; report retained')
    receipt = dict(intent, audited=True, exact_gap_verified=True, monetary_accepted=True)
    save(journals/(lower+'.json'), receipt); save(folder/'network-acceptance.json', receipt)
    return 'pending_gold' if len(pending) > 1 else 'accepted'
