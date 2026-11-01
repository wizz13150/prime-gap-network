import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
import {DatabaseSync} from 'node:sqlite';
import {resumeAdmissionFromStore} from '../tools/prepared-checkpoint-resume-transport.mjs';

// CLOSED ABI test: an actual QtCore caller/BookingStore backup, not a database
// reconstructed in JS. The exported observations/source bytes remain synthetic
// TESTONLY data. Admission decoding is NOT checkpoint loading or GPU ownership.
const defaultFixture = fileURLToPath(new URL('../evidence/root554-native-resume-caller-contract-v1/', import.meta.url));
const directory = path.resolve(process.argv[2] ?? defaultFixture);
assert.match(path.basename(directory), /^root554-native-resume-caller-contract-v[1-9][0-9]*$/);
const artifacts = ['native-bookings.sqlite', 'command.json', 'receipt.json', 'snapshot.json', 'old-source.stdout', 'fixture.json'];
const limits = [1048576, 16384, 24576, 98304, 65536, 16384];
const sha = bytes => createHash('sha256').update(bytes).digest('hex');
const bytes = new Map();
for (const [index, name] of artifacts.entries()) {
  const file = path.join(directory, name), stat = fs.lstatSync(file);
  assert.ok(stat.isFile() && !stat.isSymbolicLink() && stat.size > 0 && stat.size <= limits[index], 'Bounded regular TESTONLY artifact: ' + name);
  bytes.set(name, fs.readFileSync(file));
}
const hashes = new Map([...bytes].map(([name, value]) => [name, sha(value)]));
function utf8(name) {
  const original = bytes.get(name), value = new TextDecoder('utf-8', {fatal: true}).decode(original);
  assert.ok(Buffer.from(value, 'utf8').equals(original), 'Exact UTF8 without BOM: ' + name);
  return value;
}
const descriptor = JSON.parse(utf8('fixture.json'));
assert.equal(descriptor.schema, 'pgn-closed-native-resume-caller-contract-1');
assert.equal(descriptor.scope, 'TEST_ONLY_NO_RUNTIME_OR_PHYSICAL_PROOF');
assert.equal(descriptor.profile_scope, 'FIXTURE_BINDING_HAS_NO_SDK_PINS_EXTERNAL_TEST_PROFILE_QUALIFICATION_REQUIRED');
assert.deepEqual(descriptor.artifacts, artifacts);
const command = JSON.parse(utf8('command.json'));
const receiptText = utf8('receipt.json'), receipt = JSON.parse(receiptText);
const snapshot = JSON.parse(utf8('snapshot.json'));
const canonical = value => value && typeof value === 'object'
  ? '{' + Object.keys(value).sort().map(key => JSON.stringify(key) + ':' + canonical(value[key])).join(',') + '}'
  : JSON.stringify(value);
let checks = 0;
function equal(actual, expected, label) { assert.deepEqual(actual, expected, label); ++checks; }
function check(ok, label) { assert.ok(ok, label); ++checks; }
const wireFile = path.join(directory, 'admission.base64');
assert.ok(!fs.existsSync(wireFile), 'admission.base64 must be new; never overwrite a previous qualification');
const db = new DatabaseSync(path.join(directory, 'native-bookings.sqlite'), {readOnly: true});
try {
  const beforeChanges = db.prepare('SELECT total_changes() n').get().n;
  const result = resumeAdmissionFromStore(db, command);
  equal(result.admission.receipt, receipt, 'Production admission uses exact committed native receipt');
  equal(canonical(result.admission.receipt), receiptText, 'Receipt UTF8 bytes remain native canonical JSON');
  equal(Buffer.from(result.encoded, 'base64').toString('base64'), result.encoded, 'Canonical base64');
  equal(Buffer.from(result.encoded, 'base64').toString('utf8'), canonical(result.admission), 'Canonical wire JSON');
  check(Buffer.byteLength(canonical(result.admission), 'utf8') <= 8192, 'Conservative Windows wire budget');
  equal(result.admission.schema, 'pgn-checkpoint-resume-admission-1');
  equal(result.admission.base, {base_run: command.old_run, base_birth: command.base_birth, base_start: command.base_start, base_end: command.base_end});
  equal(receipt.origin, 'RESUME_CHECKPOINT');
  equal(receipt.counter_basis, 'PGNCG001_CUMULATIVE_BASELINE');
  equal(receipt.raw_conservation_requirement, 'LINEAGE_RAW_CONSERVATION_REQUIRED');
  equal([receipt.request_id, receipt.observation_id, receipt.booking_id, receipt.device, receipt.previous_run, receipt.run],
    [command.request_id, command.observation_id, command.booking_id, command.device, command.old_run, command.new_run]);
  check(command.old_run !== command.new_run, 'Reserved new process epoch, not a new interval');
  const tuple = receipt.checkpoint.resume, checkpoint = Buffer.from(receipt.checkpoint.checkpoint_base64, 'base64');
  equal([tuple.start, tuple.end, tuple.length, tuple.positions, tuple.tests, tuple.counterTimeNs],
    ['184467440737095516260', '184467440737095526260', '10000', '123', '9007199254740993', '9007199254740995']);
  check(BigInt(tuple.start) > BigInt(Number.MAX_SAFE_INTEGER) && BigInt(tuple.tests) > BigInt(Number.MAX_SAFE_INTEGER), 'Exact >2^53 counters and bounds, no Number conversion');
  equal(checkpoint.length, 64);
  equal(checkpoint.subarray(0, 8).toString('ascii'), 'PGNCG001');
  equal(sha(checkpoint), tuple.checkpointSha256);
  equal(checkpoint.readBigUInt64LE(16), 10n, 'Qualified high10 fixture');
  for (const [offset, field] of [[24, 'length'], [32, 'positions'], [40, 'tests'], [48, 'gaps'], [56, 'batches']])
    equal(checkpoint.readBigUInt64LE(offset).toString(), tuple[field], 'Checkpoint cumulative baseline ' + field);
  equal(BigInt(tuple.nextStart), BigInt(tuple.start) + BigInt(tuple.positions));
  equal(BigInt(tuple.remaining), BigInt(tuple.length) - BigInt(tuple.positions));
  equal(snapshot.schema, 'pgn-runtime-snapshot-1');
  equal([snapshot.state, snapshot.owned, snapshot.operator_stop, snapshot.has_checkpoint], ['OFF', false, true, true]);
  equal(snapshot.checkpoint_resume_protocol, result.admission.schema);
  equal([snapshot.manifest.schema, snapshot.manifest.run, snapshot.manifest.birth, snapshot.manifest.uuid, snapshot.manifest.queued_intent],
    ['pgn-native-process-1', command.old_run, command.base_birth, command.device, command.booking_id]);
  equal([snapshot.manifest.start, snapshot.manifest.end, snapshot.binding.start, snapshot.binding.end, snapshot.binding.length],
    [tuple.start, tuple.end, tuple.start, tuple.end, tuple.length]);
  equal(snapshot.binding.queued_intent, command.booking_id);
  equal(snapshot.last_exit, {exit_code: 0, run: command.old_run});
  equal([snapshot.checkpoint_evidence.schema, snapshot.checkpoint_evidence.status, snapshot.checkpoint_evidence.run,
    snapshot.checkpoint_evidence.checkpoint_label, snapshot.checkpoint_evidence.checkpoint_base64],
    ['pgn-stop-checkpoint-evidence-1', 'captured', command.old_run, tuple.checkpointLabel, receipt.checkpoint.checkpoint_base64]);
  const tail = Buffer.from(snapshot.checkpoint_evidence.source_tail_utf8, 'utf8'), source = bytes.get('old-source.stdout');
  check(source.subarray(source.length - tail.length).equals(tail), 'Captured stopped tail is from exported immutable old source');
  check(source.toString('utf8').startsWith('[PUBLIC-FIXTURE]'), 'Synthetic source provenance is explicit, not a physical GAP claim');
  equal([snapshot.next_offset, snapshot.pending_bytes], [source.length, 0], 'Actual exported source frontier');
  const dispatch = db.prepare('SELECT id,start,end,device,state,predecessor,run FROM dispatch_requests WHERE id=?').get(command.booking_id);
  equal({...dispatch}, {id: command.booking_id, start: tuple.start, end: tuple.end, device: command.device, state: 'PREPARED', predecessor: receipt.predecessor, run: command.new_run});
  equal({...db.prepare('SELECT kind,state,start,end FROM bookings WHERE id=?').get(command.booking_id)}, {kind: 'LOCAL_INTENT', state: 'DISPATCHING', start: tuple.start, end: tuple.end});
  equal({...db.prepare('SELECT kind,state,start,end FROM bookings WHERE id=?').get(command.old_run)}, {kind: 'RUNTIME', state: 'PAUSED', start: tuple.start, end: tuple.end});
  equal(db.prepare('SELECT count(*) n FROM bookings WHERE id=?').get(command.new_run).n, 0, 'No invented new RUNTIME or applied-control ACK');
  equal(db.prepare('SELECT receipt FROM dispatch_resume_epochs WHERE run=?').get(command.new_run).receipt, receiptText);
  check(receipt.predecessor !== receipt.previous_run, 'Completed predecessor is not process lineage');
  equal(resumeAdmissionFromStore(db, command).encoded, result.encoded, 'Read-only replay retains exact reserved epoch');
  const poisons = [
    v => { delete v.observation_id; }, v => { v.surplus = 'not wire'; },
    v => { v.action = 'start_prepared'; }, v => { v.device = 'GPU-' + '0'.repeat(8) + '-0000-4000-8000-000000000001'; },
    v => { v.request_id = '00000000-0000-0000-0000-000000000000'; }, v => { v.request_id = command.observation_id; },
    v => { v.observation_id = command.request_id; }, v => { v.booking_id = command.request_id; },
    v => { v.old_run = command.new_run; }, v => { v.new_run = 'e'.repeat(32); },
    v => { v.new_run = command.new_run.toUpperCase(); }, v => { v.base_birth += '\n'; },
    v => { v.base_start = Number(command.base_start); }, v => { v.base_start = tuple.nextStart; },
    v => { v.base_end = '0' + command.base_end; }, v => { v.base_end = (BigInt(command.base_end) + 1n).toString(); }
  ];
  for (const poison of poisons) {
    const bad = structuredClone(command); poison(bad);
    assert.throws(() => resumeAdmissionFromStore(db, bad), undefined, 'Malformed/foreign command is Held'); ++checks;
  }
  equal(db.prepare('SELECT total_changes() n').get().n, beforeChanges, 'All admissions/refusals are read-only');
  equal(db.prepare('SELECT state FROM dispatch_requests WHERE id=?').get(command.booking_id).state, 'PREPARED', 'Validation never changes PREPARED to STARTED');
  for (const name of artifacts) equal(sha(fs.readFileSync(path.join(directory, name))), hashes.get(name), 'Unchanged native export: ' + name);
  fs.writeFileSync(wireFile, result.encoded, {encoding: 'ascii', flag: 'wx'});
  assert.throws(() => fs.writeFileSync(wireFile, 'not an admission', {flag: 'wx'}), {code: 'EEXIST'}); ++checks;
  equal(fs.readFileSync(wireFile, 'ascii'), result.encoded, 'Wire output is write-once, not overwritten');
} finally {
  db.close();
  for (const name of artifacts) assert.equal(sha(fs.readFileSync(path.join(directory, name))), hashes.get(name), 'Source artifact changed: ' + name);
}
console.log(`native resume caller contract checks=${checks} failures=0; actual native SQLite -> production Node admission, immutable TESTONLY inputs; no checkpoint load, remote, worker, GPU, or raw-lineage proof`);
