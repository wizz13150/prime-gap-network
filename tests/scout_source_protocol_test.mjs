import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, statSync } from 'node:fs';
import { SourceProtocolSession, decodeSourceCheckpoint } from '../tools/engine-module-protocol.mjs';

const fixture = new URL('./fixtures/scout/', import.meta.url);
function read(name) {
  const path = new URL(name, fixture);
  assert.ok(statSync(path).size < 65536);
  return readFileSync(path, 'utf8');
}
const capabilities = JSON.parse(read('capabilities.json'));
const actual95 = read('completed95.stdout');
const U64 = (1n << 64n) - 1n, BLOCK = 30n << 25n;
function context(k = 64) {
  return { identity: { job_id: 'scout-fixture', device_id: 'fixture-device', run_id: 'first',
    owner: 'fixture', module_id: capabilities.id, artifact_sha256: 'fixture-bound-actual-transcript',
    clock_namespace: 'first-clock' },
  capabilities: { ...capabilities, profile_k: k },
  job: { start: '20000000000000000000000000000', length: '2000000000000', gap_min: '1332' },
  evidence_scope: 'FINITE_TEST' };
}
const envelope = ctx => ({ identity: { ...ctx.identity }, observed_utc: null });
const exit = ctx => ({ identity: { ...ctx.identity }, exit_code: 0, evidence_ref: 'fixture-only' });
function session(ctx) { return new SourceProtocolSession(ctx, { max_results: 64 }); }

test('actual95bit semantic transcript conserves32descriptors,8gaps and2T exact positions', () => {
  const ctx = context(), state = session(ctx);
  for (let offset = 0; offset < actual95.length; offset += 53)
    state.push(actual95.slice(offset, offset + 53), envelope(ctx));
  const result = state.finish(exit(ctx));
  assert.equal(result.state, 'COMPLETION_ACCEPTED');
  assert.equal(result.counter.fields.positions, '2000000000000');
  assert.equal(result.counter.fields.gaps, '8');
  assert.equal(result.committed_segment_batches, '32');
  assert.equal(result.result_index.scope.high_word, '1084202172');
  assert.equal(result.evidence.network_credit, null);
  assert.equal(result.evidence.independent_coverage, null);
});

for (const k of [8,16,32,64]) test(`Scout K${k} stop/resume pins threshold and exact checkpoint words`, () => {
  const ctx = context(k), start = BigInt(ctx.job.start), slot = BigInt(k) * BLOCK;
  const remainder = start % 30n, positions = slot - remainder;
  ctx.job.length = (2n * slot - remainder).toString();
  const state = session(ctx);
  const header = descriptors => `[phase0-gpu] engine=GPU_OWNED_CONTINUOUS descriptors=${descriptors} original_start=${ctx.job.start} length=${ctx.job.length} proof=EXACT_ENDPOINTS_GPU_INTERIOR coverage=SOURCE_PRP_INDEPENDENT_PROOF_PENDING\n`;
  state.push(header(2) + `[pgn-counters] time_ns=10 positions=${positions} gaps=0 tests=1 batches=1 processed_gaps=0 emitted_gaps=0\n[phase0-gpu] stopped positions=${positions} checkpoint=fixture-checkpoint\n`, envelope(ctx));
  state.finish(exit(ctx));
  const bytes = Buffer.alloc(64);bytes.write('PGNS');bytes.writeUInt32LE(1332,4);
  for (const [i,value] of [start & U64,start >> 64n,BigInt(ctx.job.length),positions,1n,0n,1n].entries())bytes.writeBigUInt64LE(value,8+i*8);
  const decoded = decodeSourceCheckpoint(bytes,{abi:'scout-x64-le-64'});
  assert.equal(decoded.gap_min,'1332');assert.equal(decoded.start_from_words,ctx.job.start);
  const next = structuredClone(ctx);next.identity.run_id='second';next.identity.clock_namespace='second-clock';
  const bad = structuredClone(next);bad.job.gap_min='1334';
  assert.throws(() => state.resume(bad,bytes,{abi:'scout-x64-le-64'}));
  const resumed = state.resume(next,bytes,{abi:'scout-x64-le-64'});
  resumed.push(header(1)+`[pgn-counters] time_ns=2 positions=${ctx.job.length} gaps=0 tests=2 batches=2 processed_gaps=0 emitted_gaps=0\n[phase0-gpu] gaps reported=0 verification_failures=0 records_new=0\n[phase0-gpu] --- end ---\n`,envelope(next));
  assert.equal(resumed.finish(exit(next)).state,'COMPLETION_ACCEPTED');
});

test('Scout needs an explicit conserved threshold and refuses legacy ABI/proof substitution', () => {
  const absent=context();delete absent.job.gap_min;assert.throws(()=>session(absent));
  const shifted=context();shifted.job.emit_min_gap='1400';assert.throws(()=>session(shifted));
  const ctx=context();assert.throws(()=>session(ctx).push(actual95.replace('EXACT_ENDPOINTS_GPU_INTERIOR','GPU_MR12_GAP'),envelope(ctx)));
  const bytes=Buffer.alloc(64);bytes.write('PGNCG001');assert.throws(()=>decodeSourceCheckpoint(bytes,{abi:'scout-x64-le-64'}));
});
