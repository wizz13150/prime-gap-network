import test from 'node:test';
import assert from 'node:assert/strict';
import { openSync, fstatSync, readSync, closeSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { SourceProtocolSession as ProtocolSession, SourceProtocolRefusal, parseSourceLine, decodeSourceCheckpoint,
  createMemoryResultIndex, inspectResultIndex } from '../tools/engine-module-protocol.mjs';

class SourceProtocolSession extends ProtocolSession {
  constructor(context, options = {}) {
    super(context, options.result_index !== undefined || options.max_results !== undefined ? options : { max_results: 1024, ...options });
  }
}

function readBounded(relative, maximum = 65536) {
  const descriptor = openSync(new URL(relative, import.meta.url), 'r');
  try {
    const size = fstatSync(descriptor).size;
    assert.ok(size > 0 && size <= maximum, 'Stat-bounded finite fixture');
    const bytes = Buffer.alloc(size); assert.equal(readSync(descriptor, bytes, 0, size, 0), size);
    assert.equal(readSync(descriptor, Buffer.alloc(1), 0, 1, size), 0, 'Fixture did not grow');
    return bytes.toString('utf8');
  } finally { closeSync(descriptor); }
}
const source = readBounded('../tools/continuous-gpu-runtime.cu');
const literal = source.match(/std::puts\(("(?:\\.|[^"\\])*")\);return 0;/);
assert.ok(literal);
const capabilities = JSON.parse(JSON.parse(literal[1]));
const fixtures = Object.fromEntries(['golden', 'stopped', 'resumed'].map(name => [name,
  readBounded('../evidence/continuous-native-v1-' + name + '-stdout-20261012.txt')]));
const goldenLines = fixtures.golden.trimEnd().split(/\r?\n/);
const goldenMutationText = goldenLines.join('\n') + '\n';
const firstCounter = goldenLines.findIndex(line => line.startsWith('[pgn-counters]'));
const firstBatch = goldenLines.slice(0, firstCounter + 1).join('\n') + '\n';
const firstHeader = goldenLines[0] + '\n';
const firstResult = goldenLines[1];
const firstCounterText = goldenLines[firstCounter];
const start = '199999990000000000000', length = '1030792151040';
const context = (changes = {}) => ({
  identity: { job_id: 'fixture-job', device_id: 'fixture-device-external-binding', run_id: 'fixture-run-initial',
    owner: 'fixture-owner', module_id: capabilities.id, clock_namespace: 'fixture-qpc-original',
    artifact_sha256: 'fixture-source-' + createHash('sha256').update(source).digest('hex'), ...changes },
  job: { start, length }, capabilities, evidence_scope: 'FINITE_TEST',
});
const envelope = ctx => ({ identity: { ...ctx.identity }, observed_utc: '2026-10-12T21:10:00.1234567Z',
  receipt_monotonic_ns: '9007199254740993', age_at_receipt_ns: '0', source_age_ns: null,
  extension: { unknown: null, zero: 0, flag: false } });
const termination = ctx => ({ identity: { ...ctx.identity }, exit_code: 0, evidence_ref: 'paid-finite-fixture-only' });
function run(text, ctx = context(), chunkSize = 4096) {
  const session = new SourceProtocolSession(ctx), events = [];
  for (let offset = 0; offset < text.length; offset += chunkSize) events.push(...session.push(text.slice(offset, offset + chunkSize), envelope(ctx)));
  return { session, events, observation: session.finish(termination(ctx)) };
}
function refusal(operation, code) {
  assert.throws(operation, error => error instanceof SourceProtocolRefusal && (!code || error.code === code));
}
function checkpointFor(observation, patch = {}) {
  const nativeStart = BigInt(observation.context.job.start);
  const values = { low: (nativeStart & ((1n << 64n) - 1n)).toString(), high: (nativeStart >> 64n).toString(),
    length: observation.context.job.length, ...observation.baseline, ...(observation.counter?.fields ?? {}), ...patch };
  const bytes = Buffer.alloc(64); bytes.write('PGNCG001', 0, 'ascii');
  for (const [index, name] of ['low', 'high', 'length', 'positions', 'tests', 'gaps', 'batches'].entries()) bytes.writeBigUInt64LE(BigInt(values[name]), 8 + index * 8);
  return bytes;
}
const abi = { abi: 'windows-x64-le-64' };
function stopped() { return run(fixtures.stopped); }
function resumeStopped(changes = {}, patch = {}) {
  const before = stopped(), next = context({ run_id: 'fixture-run-resumed', ...changes });
  const session = before.session.resume(next, checkpointFor(before.observation, patch), abi);
  return { before, next, session };
}

test('source schema pin: actual native struct, emitted processed_gaps, terminal and ownership loop', testContext => {
  assert.match(source, /struct Checkpoint \{char magic\[8\];uint64_t low,high,length,positions,tests,gaps,batches;\};/);
  assert.match(source, /\{'P','G','N','C','G','0','0','1'\}/);
  assert.match(source, /processed_gaps=%llu/);
  assert.match(source, /if\(positions!=length\)/);
  assert.match(source, /prior\.positions>=length/);
  assert.match(source, /BlockNumbers=30ULL<<25/);
  assert.match(source, /constexpr unsigned K=64,SlotBlocks=K\+1,Capacity=4096/);
  assert.match(source, /blocks>4194304/);
  assert.match(source, /saved\.positions\+std::min<uint64_t>\(remaining,uint64_t\(next\)\*K\*BlockNumbers-remainder\)/);
  testContext.diagnostic('reviewed_source_sha256=' + createHash('sha256').update(source).digest('hex'));
});
test('paid normal output conserves175 rows,17 slots and original integers without promotion', () => {
  const { observation, events } = run(fixtures.golden);
  assert.equal(observation.state, 'COMPLETION_ACCEPTED');
  assert.equal(observation.counter.fields.positions, length);
  assert.equal(observation.counter.fields.gaps, '175');
  assert.equal(observation.committed_segment_batches, '17');
  assert.equal(events.filter(event => event.kind === 'result').length, 175);
  assert.equal(observation.counter.fields.time_ns, '206375228197500');
  assert.equal(events[1].fields.lower, '199999990013881082917');
  assert.equal(observation.evidence.execution_scope, 'FINITE_TEST');
  assert.equal(observation.evidence.source_completion_accepted, true);
  assert.equal(observation.evidence.independent_coverage, null);
  assert.equal(observation.evidence.network_credit, null);
  assert.equal(observation.evidence.controller_effect, null);
});
test('original age0, large receipt clock, submillisecond UTC and null are untouched', () => {
  const { observation } = run(fixtures.golden, context(), 7);
  assert.equal(observation.last_transport.observed_utc, '2026-10-12T21:10:00.1234567Z');
  assert.equal(observation.last_transport.receipt_monotonic_ns, '9007199254740993');
  assert.equal(observation.last_transport.age_at_receipt_ns, '0');
  assert.equal(observation.last_transport.source_age_ns, null);
  assert.deepEqual(observation.last_transport.extension, { unknown: null, zero: 0, flag: false });
});
test('unavailable timing remains absent/null; constructor does not synthesize clocks', () => {
  const ctx = context(), session = new SourceProtocolSession(ctx);
  assert.equal(session.snapshot().counter, null);
  assert.equal(session.snapshot().last_transport, null);
  session.push(fixtures.golden, { identity: ctx.identity });
  assert.deepEqual(session.finish(termination(ctx)).last_transport, {});
  const parsed = parseSourceLine('[pgn-counters] time_ns=0 positions=0 gaps=0 tests=0 batches=0');
  assert.equal(parsed.fields.time_ns, '0');
  assert.equal(Object.hasOwn(parsed.fields, 'processed_gaps'), false);
});
test('paid stop/resume conserves8+167=175, preserves cumulative totals and no double counting', () => {
  const { before, next, session } = resumeStopped();
  assert.equal(before.observation.state, 'STOPPED_ACCEPTED');
  assert.equal(before.observation.evidence.source_completion_accepted, false);
  assert.equal(before.observation.counter.fields.positions, '64424509430');
  assert.equal(before.observation.counter.fields.gaps, '8');
  const resumedEvents = session.push(fixtures.resumed, envelope(next));
  const after = session.finish(termination(next));
  assert.equal(resumedEvents.filter(event => event.kind === 'result').length, 167);
  assert.equal(after.committed_segment_batches, '16');
  assert.equal(after.baseline.gaps, '8');
  assert.equal(after.retained_result_identity_count, '175');
  assert.deepEqual(after.counter.fields, { ...run(fixtures.golden).observation.counter.fields, time_ns: '206388215601600' });
});
test('native checkpoint decoding uses64-byte little endian integers and explicitly lacks run/device/clock', () => {
  const { observation } = stopped(), decoded = decodeSourceCheckpoint(checkpointFor(observation), abi);
  assert.equal(decoded.start_from_words, start); assert.equal(decoded.length, length);
  assert.equal(decoded.positions, '64424509430'); assert.equal(decoded.tests, '214632708'); assert.equal(decoded.gaps, '8');
  assert.ok(decoded.identity_fields_absent.includes('run_id')); assert.ok(decoded.identity_fields_absent.includes('device_id'));
});
test('checkpoint refuses absent ABI, bad magic, truncated/trailing bytes, completed/wrong-high/window shape', () => {
  const { observation } = stopped(), bytes = checkpointFor(observation);
  refusal(() => decodeSourceCheckpoint(bytes), 'CHECKPOINT_ABI_REQUIRED');
  const poisoned = Buffer.from(bytes); poisoned[0] = 0; refusal(() => decodeSourceCheckpoint(poisoned, abi), 'CHECKPOINT_MAGIC');
  const highBit = Buffer.from(bytes); highBit[0] |= 0x80; refusal(() => decodeSourceCheckpoint(highBit, abi), 'CHECKPOINT_MAGIC');
  for (const bad of [bytes.subarray(0, 63), Buffer.concat([bytes, Buffer.alloc(1)])]) refusal(() => decodeSourceCheckpoint(bad, abi), 'CHECKPOINT_SIZE');
  refusal(() => decodeSourceCheckpoint(checkpointFor(observation, { positions: length }), abi), 'CHECKPOINT_NOT_RESUMABLE');
  refusal(() => decodeSourceCheckpoint(checkpointFor(observation, { high: '11' }), abi), 'CHECKPOINT_JOB_SHAPE');
  refusal(() => decodeSourceCheckpoint(checkpointFor(observation, { low: String((1n << 64n) - 1n) }), abi), 'CHECKPOINT_SPAN');
});
test('checkpoint mismatch refuses candidate without invalidating paid stopped predecessor', () => {
  const { session, observation } = stopped(), next = context({ run_id: 'fixture-run-next' });
  for (const patch of [{ tests: '0' }, { gaps: '7' }, { positions: '0' }, { batches: '0' }])
    refusal(() => session.resume(next, checkpointFor(observation, patch), abi), 'CHECKPOINT_COUNTER_MISMATCH');
  refusal(() => session.resume(next, checkpointFor(observation, { length: '1030792151041' }), abi), 'CHECKPOINT_JOB_MISMATCH');
  assert.equal(session.snapshot().state, 'STOPPED_ACCEPTED');
});
test('resumed stdout without stopped/checkpoint baseline is rejected instead of losing8 results', () => {
  refusal(() => run(fixtures.resumed), 'DESCRIPTOR_GEOMETRY');
});
test('resume rejects incompatible job/device/artifact/owner, reused run and scope promotion', () => {
  const { session, observation } = stopped(), bytes = checkpointFor(observation);
  for (const patch of [{ device_id: 'other-device' }, { job_id: 'other-job' }, { owner: 'other-owner' }, { artifact_sha256: 'other-artifact' }])
    refusal(() => session.resume(context({ run_id: 'next', ...patch }), bytes, abi), 'IDENTITY_MISMATCH');
  refusal(() => session.resume(context(), bytes, abi), 'RESUME_RUN_REUSE');
  const differentSpan = context({ run_id: 'next' }); differentSpan.job.length = '1030792151041';
  refusal(() => session.resume(differentSpan, bytes, abi), 'RESUME_JOB_MISMATCH');
  const promoted = context({ run_id: 'next' }); promoted.evidence_scope = 'SOURCE_TRANSCRIPT';
  refusal(() => session.resume(promoted, bytes, abi), 'RESUME_SCOPE_MISMATCH');
});
test('envelope rejects run/device/job/clock/module/owner/artifact mismatch before parsing', () => {
  for (const name of Object.keys(context().identity)) {
    const ctx = context(), session = new SourceProtocolSession(ctx), packet = envelope(ctx); packet.identity[name] = 'incompatible';
    refusal(() => session.push(firstHeader, packet), 'IDENTITY_MISMATCH'); assert.equal(session.snapshot().state, 'REFUSED');
  }
});
test('wire identity extension cannot override bound envelope', () => {
  refusal(() => run(goldenMutationText.replace('engine=GPU_OWNED_CONTINUOUS', 'engine=GPU_OWNED_CONTINUOUS run_id=imposter')), 'WIRE_IDENTITY_MISMATCH');
});
test('duplicate result and duplicate counter each refuse and latch first failure', () => {
  const duplicatedResult = firstHeader + firstResult + '\n' + firstResult + '\n';
  const ctx = context(), session = new SourceProtocolSession(ctx);
  refusal(() => session.push(duplicatedResult, envelope(ctx)), 'DUPLICATE_RESULT');
  refusal(() => session.push(firstHeader, envelope(ctx)), 'SESSION_REFUSED');
  assert.equal(session.snapshot().refusal.code, 'DUPLICATE_RESULT');
  refusal(() => run(firstBatch + firstCounterText + '\n'), 'CLOCK_REORDER_OR_DUPLICATE');
});
test('numeric row order stays arbitrary inside slot; swapping a row across counter boundary refuses', () => {
  const lines = [...goldenLines]; [lines[1], lines[2]] = [lines[2], lines[1]];
  assert.equal(run(lines.join('\n') + '\n').observation.state, 'COMPLETION_ACCEPTED');
  const moved = [...goldenLines]; const row = moved.splice(1, 1)[0]; moved.splice(firstCounter, 0, row);
  refusal(() => run(moved.join('\n') + '\n'), 'RESULT_CONSERVATION');
});
test('reordered slot counter, regressing tests and incompatible positions refuse', () => {
  const counters = goldenLines.filter(line => line.startsWith('[pgn-counters]'));
  refusal(() => run(firstHeader + counters[1] + '\n'), 'BATCH_CONSERVATION');
  refusal(() => run(goldenMutationText.replace('tests=3434324745', 'tests=0')), 'COUNTER_REGRESSION');
  refusal(() => run(firstBatch.replace('positions=64424509430', 'positions=64424509431')), 'POSITION_CONSERVATION');
});
test('source clock regression/duplication refuses; original packet clock cannot precede counter', () => {
  const text = goldenMutationText.replace(/time_ns=[0-9]+/g, 'time_ns=0');
  refusal(() => run(text), 'CLOCK_REORDER_OR_DUPLICATE');
  const ctx = context(), session = new SourceProtocolSession(ctx), packet = envelope(ctx); packet.source_clock_ns = '0';
  refusal(() => session.push(firstBatch, packet), 'SOURCE_CLOCK_PRECEDES_COUNTER');
});
test('same resume namespace cannot reset; explicit distinct namespace allows new absolute clock', () => {
  let bound = resumeStopped();
  refusal(() => bound.session.push(fixtures.resumed.replace(/time_ns=[0-9]+/g, 'time_ns=0'), envelope(bound.next)), 'RESUME_CLOCK_REGRESSION');
  bound = resumeStopped({ clock_namespace: 'fixture-explicit-new-clock' });
  let tick = 0n;
  const reset = fixtures.resumed.replace(/time_ns=[0-9]+/g, () => 'time_ns=' + (tick++).toString());
  bound.session.push(reset, envelope(bound.next));
  assert.equal(bound.session.finish(termination(bound.next)).counter.fields.time_ns, '15');
});
test('partial tail is buffered exactly; EOF never commits a partial end marker', () => {
  const ctx = context(), session = new SourceProtocolSession(ctx), cut = fixtures.golden.length - 4;
  session.push(fixtures.golden.slice(0, cut), envelope(ctx));
  assert.ok(session.snapshot().partial_tail.length > 0);
  session.push(fixtures.golden.slice(cut), envelope(ctx)); assert.equal(session.finish(termination(ctx)).state, 'COMPLETION_ACCEPTED');
  const broken = new SourceProtocolSession(ctx); broken.push(fixtures.golden.trimEnd(), envelope(ctx));
  refusal(() => broken.finish(termination(ctx)), 'PARTIAL_TAIL');
});
test('split line retains both original receipt ages; incompatible identity and length overflow refuse', () => {
  const ctx = context(), session = new SourceProtocolSession(ctx), packet = envelope(ctx);
  session.push(firstHeader.slice(0, 8), packet); packet.age_at_receipt_ns = '1';
  const events = session.push(firstHeader.slice(8), packet);
  assert.equal(events[0].transport.age_at_receipt_ns, '1');
  assert.deepEqual(events[0].transport_fragments.map(fragment => fragment.transport.age_at_receipt_ns), ['0', '1']);
  const incompatible = new SourceProtocolSession(ctx); incompatible.push('[phase0-', envelope(ctx));
  packet.identity.clock_namespace = 'imposter'; refusal(() => incompatible.push('gpu] --- end ---\n', packet), 'IDENTITY_MISMATCH');
  const large = new SourceProtocolSession(ctx); refusal(() => large.push('x'.repeat(8193), envelope(ctx)), 'PARTIAL_LINE_LIMIT');
  assert.equal(large.snapshot().partial_tail, '');
  const limited = new SourceProtocolSession(ctx, { max_fragment_envelopes: 1 }); limited.push('[phase0-', envelope(ctx));
  refusal(() => limited.push('gpu]', envelope(ctx)), 'FRAGMENT_CAPACITY');
});
test('malformed/unsafe/overflow/missing/duplicate counters refuse without coercion', () => {
  for (const value of ['-1', '+1', '1.0', '1e2', 'null', '18446744073709551616'])
    refusal(() => parseSourceLine(firstCounterText.replace(/tests=[0-9]+/, 'tests=' + value)));
  refusal(() => parseSourceLine(firstCounterText + ' tests=0'), 'DUPLICATE_FIELD');
  refusal(() => run(firstBatch.replace(' processed_gaps=8', '')), 'MISSING_FIELD');
  refusal(() => run(firstBatch.replace('processed_gaps=8', 'processed_gaps=0')), 'PROCESSED_GAP_CONSERVATION');
});
test('unknown extension fields and diagnostic lines survive without promoting independent/network evidence', () => {
  const extended = 'unclassified diagnostic text\n' + goldenMutationText.replace('proof=GPU_MR12_GAP', 'proof=GPU_MR12_GAP independent_coverage=claimed network_credit=claimed');
  const { observation, events } = run(extended);
  assert.equal(events[0].raw_line, 'unclassified diagnostic text');
  assert.equal(observation.header.fields.independent_coverage, 'claimed');
  assert.equal(observation.evidence.independent_coverage, null); assert.equal(observation.evidence.network_credit, null);
  refusal(() => parseSourceLine('[phase0-gpu] new_terminal success=1'), 'UNSUPPORTED_PROTOCOL_RECORD');
});
test('genuine zero results/tests and wire clock0 remain zero while absent metadata stays unavailable', () => {
  const ctx = context(); ctx.job.length = '1000';
  const text = '[phase0-gpu] engine=GPU_OWNED_CONTINUOUS descriptors=1 original_start=' + start + ' length=1000 proof=GPU_MR12_GAP coverage=SOURCE_PRP_INDEPENDENT_PROOF_PENDING\n'
    + '[pgn-counters] time_ns=0 positions=1000 gaps=0 tests=0 batches=1 processed_gaps=0\n'
    + '[phase0-gpu] gaps reported=0 verification_failures=0 records_new=0\n[phase0-gpu] --- end ---\n';
  const session = new SourceProtocolSession(ctx); session.push(text, { identity: ctx.identity });
  const result = session.finish(termination(ctx));
  assert.equal(result.counter.fields.tests, '0'); assert.equal(result.counter.fields.time_ns, '0'); assert.equal(result.counter.fields.gaps, '0');
  assert.equal(result.evidence.network_credit, null); assert.equal(Object.hasOwn(result.last_transport, 'age_at_receipt_ns'), false);
});
test('leading decimal zeros survive raw parser and CRLF survives event without Number conversion', () => {
  const line = firstCounterText.replace('tests=214632708', 'tests=000214632708') + '\r';
  const event = parseSourceLine(line); assert.equal(event.raw_line, line); assert.equal(event.fields.tests, '000214632708');
});
test('result/span/proof poison and missing-result conservation fail closed', () => {
  refusal(() => run(goldenMutationText.replace('gap=892', 'gap=893')), 'RESULT_GEOMETRY');
  refusal(() => run(firstHeader + firstResult.replace(/lower=[0-9]+/, 'lower=' + start).replace(/upper=[0-9]+/, 'upper=' + (BigInt(start) + 892n)) + '\n' + firstCounterText + '\n'), 'RESULT_CONSERVATION');
  refusal(() => run(goldenMutationText.replace('verified=1', 'verified=0')), 'RESULT_DECLARATION');
  refusal(() => run(goldenMutationText.replace(firstResult + '\n', '')), 'RESULT_CONSERVATION');
});
test('summary/end/exit boundaries refuse missing/nonzero/duplicate/after-terminal statements', () => {
  refusal(() => run(goldenMutationText.replace('reported=175', 'reported=174')), 'SUMMARY_CONSERVATION');
  refusal(() => run(goldenMutationText.replace('verification_failures=0', 'verification_failures=1')), 'SUMMARY_DECLARATION');
  refusal(() => run(goldenMutationText.replace('[phase0-gpu] --- end ---\n', '')), 'MISSING_TERMINAL');
  refusal(() => run(goldenMutationText.replace('[phase0-gpu] gaps reported=175 verification_failures=0 records_new=0\n', '')), 'END_ORDER');
  refusal(() => run(goldenMutationText + firstCounterText + '\n'), 'AFTER_TERMINAL');
  const ctx = context(), session = new SourceProtocolSession(ctx); session.push(fixtures.golden, envelope(ctx));
  refusal(() => session.finish({ ...termination(ctx), exit_code: 1 }), 'EXIT_EVIDENCE_REQUIRED');
  assert.equal(session.snapshot().evidence.source_completion_accepted, false);
});
test('result-capacity refusal never silently evicts duplicate evidence', () => {
  const ctx = context(), session = new SourceProtocolSession(ctx, { max_results: 8 });
  refusal(() => session.push(fixtures.golden, envelope(ctx)), 'RESULT_CAPACITY');
  assert.equal(session.snapshot().retained_result_identity_count, '8');
});
test('a refused chunk preserves every parsed prefix event plus the offending raw record', () => {
  const ctx = context(), session = new SourceProtocolSession(ctx);
  let captured;
  const suffix = firstResult + '\n' + 'remaining unparsed source bytes\n';
  try { session.push(firstBatch + suffix, envelope(ctx)); } catch (error) { captured = error; }
  assert.equal(captured.code, 'DUPLICATE_RESULT');
  assert.equal(captured.observed_events.length, 10);
  assert.equal(captured.observed_events.filter(event => event.kind === 'result').length, 8);
  assert.equal(captured.rejected_record.raw_line, firstResult);
  assert.equal(captured.unconsumed_text, suffix);
  assert.equal(captured.observed_events[0].evidence_scope, 'SOURCE_WIRE_RECORD_ONLY');
  assert.equal(session.snapshot().evidence.source_completion_accepted, false);
});
test('caller mutation of returned events/context/snapshots cannot rewrite internal conservation state', () => {
  const ctx = context(), session = new SourceProtocolSession(ctx), packet = envelope(ctx);
  const events = session.push(firstBatch, packet);
  events[0].fields.coverage = 'FAKE_PROOF';
  events.at(-1).fields.gaps = '999';
  events[1].fields.lower = '0';
  ctx.job.length = '1'; packet.identity.run_id = 'imposter';
  const snapshot = session.snapshot(); snapshot.counter.fields.gaps = '1000';
  const originalContext = context();
  session.push(goldenMutationText.slice(firstBatch.length), envelope(originalContext));
  const observation = session.finish(termination(originalContext));
  assert.equal(observation.counter.fields.gaps, '175');
  assert.equal(observation.header.fields.coverage, 'SOURCE_PRP_INDEPENDENT_PROOF_PENDING');
});
test('empty checkpoint path and unconfirmed/finished predecessor cannot imply resumability', () => {
  const ctx = context(), session = new SourceProtocolSession(ctx);
  session.push(firstHeader + '[phase0-gpu] stopped positions=0 checkpoint=\n', envelope(ctx));
  refusal(() => session.resume(context({ run_id: 'next' }), Buffer.alloc(64), abi), 'RESUME_NOT_STOPPED');
  const stoppedObservation = session.finish(termination(ctx));
  assert.equal(stoppedObservation.counter, null);
  assert.equal(stoppedObservation.stopped.fields.positions, '0');
  refusal(() => session.resume(context({ run_id: 'next' }), checkpointFor(stoppedObservation), abi), 'CHECKPOINT_PATH_UNAVAILABLE');
  const complete = run(fixtures.golden);
  refusal(() => complete.session.resume(context({ run_id: 'next' }), Buffer.alloc(64), abi), 'RESUME_NOT_STOPPED');
});
test('resume binding stays supplied evidence; JSON metadata rejects object conversion and timing override', () => {
  const resumed = resumeStopped();
  assert.equal(resumed.session.snapshot().baseline_origin, 'PREDECESSOR_MATCHED_CHECKPOINT_WORDS');
  assert.equal(resumed.session.snapshot().resume_binding.checkpoint.start_from_words, start);
  const ctx = context(), session = new SourceProtocolSession(ctx), packet = envelope(ctx); packet.extension = new Date();
  refusal(() => session.push(firstHeader, packet), 'NON_JSON_METADATA');
  const mismatched = new SourceProtocolSession(ctx), timing = envelope(ctx); timing.clock_namespace = 'wrong-clock';
  refusal(() => mismatched.push(firstHeader, timing), 'TRANSPORT_IDENTITY_MISMATCH');
});

function closedIndex(maxRecords = 1024, maxBytes = 65536) {
  return createMemoryResultIndex({ index_id: 'closed-capacity-fixture-index', max_records: maxRecords, max_bytes: maxBytes });
}
test('no implicit65536 budget; explicit record admission reserves deterministic backing before ingestion', () => {
  refusal(() => new ProtocolSession(context()), 'RESULT_INDEX_ADMISSION_REQUIRED');
  const index = closedIndex(200000, 8 * 1024 * 1024), inspection = inspectResultIndex(index);
  assert.equal(inspection.backing_bytes, '6291456'); assert.equal(inspection.max_records, '200000');
  assert.equal(inspection.retained_records, '0'); assert.equal(inspection.owner_phase, 'UNBOUND');
  assert.equal(inspection.persistence, 'PROCESS_LOCAL_VOLATILE');
  assert.equal(Object.hasOwn(inspection, 'observed_utc'), false);
  const explicit = new ProtocolSession(context(), { max_results: 1024, max_index_bytes: 24576 });
  assert.equal(explicit.snapshot().result_index.backing_bytes, '24576');
  refusal(() => new ProtocolSession(context(), { max_results: 1024, max_index_bytes: 1 }), 'INDEX_MEMORY_ADMISSION');
});
test('memory provider rejects malformed budgets/indexes without calling untrusted membership or persistence', () => {
  for (const maxRecords of [0, -1, NaN, Infinity, '1000', Number.MAX_SAFE_INTEGER])
    refusal(() => closedIndex(maxRecords), 'RESULT_LIMIT');
  for (const maxBytes of [0, -1, NaN, Infinity, '65536', 1]) refusal(() => closedIndex(1024, maxBytes), 'INDEX_MEMORY_ADMISSION');
  let callbacks = 0;
  const fake = { schema: 'pgn-result-identity-index-1', has() { callbacks++; return false; }, add() { callbacks++; } };
  for (const index of [null, false, 0, {}, fake, new Proxy(closedIndex(), {})])
    refusal(() => new ProtocolSession(context(), { result_index: index }), 'UNTRUSTED_RESULT_INDEX');
  assert.equal(callbacks, 0);
});
test('index has one writer, immutable budgets and exact scoped identity; inspection cannot mutate keys', () => {
  const ctx = context(), index = closedIndex(), session = new ProtocolSession(ctx, { result_index: index });
  refusal(() => new ProtocolSession(ctx, { result_index: index }), 'INDEX_ALREADY_BOUND');
  assert.throws(() => { index.provider = 'forged'; }, TypeError);
  const metadata = inspectResultIndex(index); metadata.scope.device_id = 'imposter'; metadata.retained_records = '999';
  session.push(firstBatch, envelope(ctx)); assert.equal(inspectResultIndex(index).retained_records, '8');
  assert.equal(inspectResultIndex(index).scope.device_id, ctx.identity.device_id);
  refusal(() => new ProtocolSession(ctx, { result_index: index, max_results: 100000 }), 'AMBIGUOUS_INDEX_ADMISSION');
});
test('duplicate remains exact after a committed batch and after fragmented input with changing receipt ages', () => {
  const ctx = context(), session = new ProtocolSession(ctx, { result_index: closedIndex() });
  session.push(firstBatch, envelope(ctx));
  const packet = envelope(ctx); session.push(firstResult.slice(0, 21), packet); packet.age_at_receipt_ns = '7';
  let captured;
  try { session.push(firstResult.slice(21) + '\n', packet); } catch (error) { captured = error; }
  assert.equal(captured.code, 'DUPLICATE_RESULT');
  assert.deepEqual(captured.rejected_record.transport_fragments.map(fragment => fragment.transport.age_at_receipt_ns), ['0', '7']);
  assert.equal(session.snapshot().retained_result_identity_count, '8');
});
test('resume transfers same index exactly once; historical duplicate survives without copying or fresh budget', () => {
  const ctx = context(), index = closedIndex(), previous = new ProtocolSession(ctx, { result_index: index });
  previous.push(fixtures.stopped, envelope(ctx)); const before = previous.finish(termination(ctx));
  const next = context({ run_id: 'fixture-index-resumed', clock_namespace: 'fixture-next-namespace' });
  const bytes = checkpointFor(before);
  refusal(() => previous.resume(next, bytes, { ...abi, result_index: closedIndex() }), 'RESUME_INDEX_REPLACEMENT');
  refusal(() => previous.resume(next, bytes, { ...abi, max_results: 8 }), 'RESUME_INDEX_REPLACEMENT');
  assert.equal(inspectResultIndex(index).owner_phase, 'STOPPED');
  const resumed = previous.resume(next, bytes, abi);
  assert.equal(inspectResultIndex(index).owner_run.run_id, next.identity.run_id);
  assert.equal(inspectResultIndex(index).retained_records, '8');
  refusal(() => previous.resume(context({ run_id: 'another-run' }), bytes, abi), 'INDEX_OWNER_TRANSFERRED');
  assert.equal(previous.snapshot().retained_result_identity_count, '8');
  resumed.push(fixtures.resumed.split(/\r?\n/)[0] + '\n', envelope(next));
  refusal(() => resumed.push(firstResult + '\n', envelope(next)), 'DUPLICATE_RESULT');
  assert.equal(inspectResultIndex(index).retained_records, '8');
});
test('host capacity refuses before insertion and preserves offending identity, original age and unread suffix', () => {
  const ctx = context(), index = closedIndex(8, 192), session = new ProtocolSession(ctx, { result_index: index });
  const secondRow = goldenLines[firstCounter + 1], suffix = secondRow + '\nremaining-source-bytes\n';
  let captured;
  try { session.push(firstBatch + suffix, envelope(ctx)); } catch (error) { captured = error; }
  assert.equal(captured.code, 'RESULT_CAPACITY'); assert.equal(captured.observed_events.length, 10);
  assert.equal(captured.rejected_record.raw_line, secondRow); assert.equal(captured.unconsumed_text, suffix);
  assert.equal(captured.rejected_record.transport.age_at_receipt_ns, '0');
  assert.equal(inspectResultIndex(index).retained_records, '8'); assert.equal(inspectResultIndex(index).owner_phase, 'REFUSED');
  assert.equal(session.snapshot().evidence.source_completion_accepted, false);
});

const largeSlot = 64n * 30n * (1n << 25n);
function largeFixtureContext() {
  const ctx = context(); ctx.job.length = '1000000000000000'; ctx.identity.job_id = 'closed1e15-capacity-fixture';
  return ctx;
}
function largeFixtureHeader(ctx, positions = 0n) {
  const remaining = BigInt(ctx.job.length) - positions, remainder = (BigInt(ctx.job.start) + positions) % 30n;
  const count = (remaining + remainder + largeSlot - 1n) / largeSlot;
  return '[phase0-gpu] engine=GPU_OWNED_CONTINUOUS descriptors=' + count + ' original_start=' + ctx.job.start
    + ' length=' + ctx.job.length + ' proof=GPU_MR12_GAP coverage=SOURCE_PRP_INDEPENDENT_PROOF_PENDING\n';
}
function largeFixtureBatch(ctx, batch, rows, cumulative) {
  const remainder = BigInt(ctx.job.start) % 30n;
  const base = BigInt(ctx.job.start) - remainder + BigInt(batch - 1) * largeSlot;
  let text = '';
  for (let row = 0; row < rows; row++) {
    const lower = base + 32n + BigInt(row) * 1000n;
    text += '[phase0-gpu] GAP merit=17.0000 gap=800 lower=' + lower + ' upper=' + (lower + 800n)
      + ' verified=1 proof=GPU_MR12_EULER_COMPOSITE record=off table=unknown\n';
  }
  const prefix = BigInt(batch) * largeSlot - remainder, positions = prefix < BigInt(ctx.job.length) ? prefix : BigInt(ctx.job.length);
  return text + '[pgn-counters] time_ns=' + batch + ' positions=' + positions + ' gaps=' + cumulative
    + ' tests=' + (BigInt(batch) * 10000n) + ' batches=' + batch + ' processed_gaps=' + cumulative + '\n';
}
test('semantic4095-per-slot limit stays independent of a larger host budget', () => {
  const ctx = largeFixtureContext(), index = closedIndex(10000, 393216), session = new ProtocolSession(ctx, { result_index: index });
  session.push(largeFixtureHeader(ctx), envelope(ctx)); let captured;
  try { session.push(largeFixtureBatch(ctx, 1, 4096, 4096), envelope(ctx)); } catch (error) { captured = error; }
  assert.equal(captured.code, 'PRODUCER_RESULT_CAPACITY'); assert.equal(captured.observed_events.length, 4095);
  assert.equal(inspectResultIndex(index).retained_records, '4095');
  assert.equal(session.snapshot().capacity_contract.producer_max_rows_per_slot, '4095');
});
test('actual synthetic160756-record traversal of1e15 span crosses65536, resumes and conserves all exact identities', testContext => {
  const ctx = largeFixtureContext(), index = closedIndex(200000, 8 * 1024 * 1024);
  let session = new ProtocolSession(ctx, { result_index: index }), activeContext = ctx, cumulative = 0, observedRows = 0;
  session.push(largeFixtureHeader(ctx), envelope(ctx));
  const remainder = BigInt(ctx.job.start) % 30n, descriptors = Number((BigInt(ctx.job.length) + remainder + largeSlot - 1n) / largeSlot);
  for (let batch = 1; batch <= descriptors; batch++) {
    const rows = Math.min(2000, 160756 - cumulative); cumulative += rows;
    const events = session.push(largeFixtureBatch(activeContext, batch, rows, cumulative), envelope(activeContext));
    observedRows += events.filter(event => event.kind === 'result').length;
    if (batch === 40) {
      assert.equal(cumulative, 80000);
      const current = session.snapshot();
      session.push('[phase0-gpu] stopped positions=' + current.counter.fields.positions + ' checkpoint=closed-memory.state\n', envelope(activeContext));
      const before = session.finish(termination(activeContext));
      activeContext = { ...ctx, identity: { ...ctx.identity, run_id: 'closed-large-resume' } };
      session = session.resume(activeContext, checkpointFor(before), abi);
      session.push(largeFixtureHeader(activeContext, BigInt(before.counter.fields.positions)), envelope(activeContext));
    }
  }
  session.push('[phase0-gpu] gaps reported=160756 verification_failures=0 records_new=0\n[phase0-gpu] --- end ---\n', envelope(activeContext));
  const observation = session.finish(termination(activeContext)), inspection = inspectResultIndex(index);
  assert.equal(observedRows, 160756); assert.equal(observation.retained_result_identity_count, '160756');
  assert.equal(observation.counter.fields.positions, '1000000000000000'); assert.equal(observation.counter.fields.gaps, '160756');
  assert.equal(observation.counter.fields.batches, String(descriptors)); assert.equal(inspection.backing_bytes, '6291456');
  assert.ok(BigInt(inspection.lookup_probes) > 160756n, 'Actual full-key collision traversal occurred');
  assert.equal(observation.evidence.independent_coverage, null); assert.equal(observation.evidence.network_credit, null);
  assert.equal(observation.counter.fields.time_ns, String(descriptors)); assert.equal(observation.last_transport.age_at_receipt_ns, '0');
  assert.equal(observation.last_transport.source_age_ns, null);
  testContext.diagnostic('synthetic_rows=' + observedRows + ' descriptors=' + descriptors + ' packed_backing_bytes=' + inspection.backing_bytes + ' lookup_probes=' + inspection.lookup_probes + ' persistence=' + inspection.persistence);
});
test('full key distinguishes low/high/gap words including a genuine zero low word', () => {
  const ctx = context(), index = closedIndex(), session = new ProtocolSession(ctx, { result_index: index });
  const word = 1n << 32n, first = ((BigInt(ctx.job.start) + word - 1n) / word) * word;
  session.push(firstHeader, envelope(ctx));
  const pairs = [[first, 800n], [first + word, 800n], [first + word * 2n, 800n], [first, 802n], [first + 2n, 800n]];
  const line = (lower, gap) => '[phase0-gpu] GAP merit=17.0000 gap=' + gap + ' lower=' + lower + ' upper=' + (lower + gap)
    + ' verified=1 proof=GPU_MR12_EULER_COMPOSITE record=off table=unknown\n';
  for (const [lower, gap] of pairs) session.push(line(lower, gap), envelope(ctx));
  assert.equal(inspectResultIndex(index).retained_records, '5'); assert.equal(first & 0xffffffffn, 0n);
  refusal(() => session.push(line(first, 800n), envelope(ctx)), 'DUPLICATE_RESULT');
  assert.equal(inspectResultIndex(index).retained_records, '5');
});
test('historical duplicate is still retained after80000 distinct records and stop/resume with fragmented clocks', () => {
  const ctx = largeFixtureContext(), index = closedIndex(200000, 8 * 1024 * 1024), previous = new ProtocolSession(ctx, { result_index: index });
  previous.push(largeFixtureHeader(ctx), envelope(ctx));
  for (let batch = 1; batch <= 40; batch++) previous.push(largeFixtureBatch(ctx, batch, 2000, batch * 2000), envelope(ctx));
  const current = previous.snapshot();
  previous.push('[phase0-gpu] stopped positions=' + current.counter.fields.positions + ' checkpoint=closed-retention.state\n', envelope(ctx));
  const before = previous.finish(termination(ctx)), next = { ...ctx, identity: { ...ctx.identity, run_id: 'closed-late-duplicate', clock_namespace: 'closed-explicit-next-clock' } };
  const resumed = previous.resume(next, checkpointFor(before), abi);
  resumed.push(largeFixtureHeader(next, BigInt(before.counter.fields.positions)), envelope(next));
  const duplicate = largeFixtureBatch(ctx, 1, 1, 1).split('\n')[0], packet = envelope(next);
  packet.age_at_receipt_ns = '3'; resumed.push(duplicate.slice(0, 30), packet); packet.age_at_receipt_ns = '11';
  let captured;
  try { resumed.push(duplicate.slice(30) + '\n', packet); } catch (error) { captured = error; }
  assert.equal(captured.code, 'DUPLICATE_RESULT'); assert.equal(inspectResultIndex(index).retained_records, '80000');
  assert.equal(inspectResultIndex(index).backing_bytes, '6291456');
  assert.deepEqual(captured.rejected_record.transport_fragments.map(fragment => fragment.transport.age_at_receipt_ns), ['3', '11']);
  assert.equal(resumed.snapshot().counter, null); assert.equal(resumed.snapshot().evidence.source_completion_accepted, false);
});
