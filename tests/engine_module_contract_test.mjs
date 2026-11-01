import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, statSync } from 'node:fs';
import { validateEngineModuleCapabilities as validate, observeEngineModule as observe,
  createEngineModuleRequest as request, correlateEngineModuleEffect as correlate } from '../tools/engine-module-contract.mjs';
// Extract the actual producer's literal descriptor without launching CUDA.
const source = new URL('../tools/continuous-gpu-runtime.cu', import.meta.url);
assert.ok(statSync(source).size < 131072);
const literal = readFileSync(source, 'utf8').match(/std::puts\(("(?:\\.|[^"\\])*")\);return 0;/);
assert.ok(literal, 'Current producer descriptor literal is present');
const actual = JSON.parse(JSON.parse(literal[1]));
const stamp = '2026-10-12T20:01:02.1234567Z';
const provenance = { observed_utc: stamp, artifact_sha256: null, source: 'closed-source-literal-test' };
const intent = { request_id: 'closed-request-1', device_id: 'fixture-device', owner: 'fixture-owner',
  start: String(10n << 64n), length: '30' };
test('actual pgn-engine-module-1 descriptor round trips exactly', () => {
  assert.deepEqual(validate(actual), actual);
  assert.equal(actual.host_refill, false);
  assert.equal(actual.supported_high_word, 10);
  assert.equal(actual.coverage, 'SOURCE_PRP_INDEPENDENT_PROOF_PENDING');
});
test('extensions and raw zero/false/null survive; inputs stay independent', () => {
  const c = { ...actual, extension: { zero: 0, missing: null, off: false } };
  const o = observe(c, provenance);
  assert.deepEqual(o.capabilities, c);
  assert.equal(o.provenance.observed_utc, stamp);
  assert.equal(o.provenance.artifact_sha256, null);
  c.extension.zero = 8;
  assert.equal(o.capabilities.extension.zero, 0);
  assert.equal(o.evidence_scope, 'PRODUCER_DECLARATION_ONLY');
  assert.equal('applied' in o, false);
});
test('missing observation is absent, never a fabricated timestamp/hash', () => {
  assert.deepEqual(observe(actual).provenance, {});
});
test('required false must be explicit; wrong types/schema/high word reject', () => {
  for (const patch of [{ schema: 'unknown' }, { host_refill: 0 }, { protocol: '' },
    { single_low64_window: 'true' }, { supported_high_word: 9007199254740992 },
    { supported_high_word: '-1' }, { supported_high_word: '01' },
    { supported_high_word: String(1n << 64n) }, { extension: undefined }])
    assert.throws(() => validate({ ...actual, ...patch }));
  const absent = { ...actual }; delete absent.host_verification;
  assert.throws(() => validate(absent));
});
test('supported phase0 request preserves decimal job and stays requested', () => {
  const r = request(observe(actual, provenance), intent);
  assert.equal(r.state, 'REQUESTED');
  assert.deepEqual(r.job, { start: intent.start, length: '30' });
  assert.equal(r.admission, 'CONTROLLER_VALIDATION_REQUIRED');
  for (const key of ['command', 'lease', 'applied', 'private_key']) assert.equal(key in r, false);
});
test('unknown protocol remains inspectable but cannot form an admitted job', () => {
  const o = observe({ ...actual, protocol: 'future-source-2' });
  assert.equal(o.capabilities.protocol, 'future-source-2');
  assert.throws(() => request(o, intent), /Unsupported job protocol/);
});
test('uint128/high-word/low64/canonical decimal constraints are exact', () => {
  for (const patch of [{ start: String(11n << 64n) }, { length: '0' },
    { start: String((11n << 64n) - 1n), length: '1' }, { start: ' 184467440737095516160' },
    { length: '01' }, { length: String(1n << 64n) }, { start: String(1n << 128n) }])
    assert.throws(() => request(observe(actual), { ...intent, ...patch }));
  const generic = { ...actual }; delete generic.supported_high_word; delete generic.single_low64_window;
  assert.throws(() => request(observe(generic), { ...intent, start: String((1n << 128n) - 1n), length: '1' }));
  const c = { ...actual, supported_high_word: '10' };
  assert.equal(request(observe(c), intent).job.start, intent.start);
});
test('effect correlation requires owner/device/request/module/job/run/evidence', () => {
  const r = request(observe(actual), intent);
  const effect = { schema: 'pgn-engine-module-effect-1', request_id: r.request_id,
    module_id: r.module_id, device_id: r.device_id, owner: r.owner, state: 'APPLIED',
    run_id: 'fixture-run', job: { ...r.job }, observed_utc: stamp, evidence_ref: 'closed-fixture-only' };
  const result = correlate(r, effect);
  assert.equal(result.effect.observed_utc, stamp);
  assert.equal(result.evidence_scope, 'SUPPLIED_CONTROLLER_EVIDENCE_UNVERIFIED');
  for (const key of ['request_id', 'module_id', 'device_id', 'owner', 'run_id', 'observed_utc', 'evidence_ref']) {
    const broken = { ...effect }; delete broken[key]; assert.throws(() => correlate(r, broken));
  }
  assert.throws(() => correlate(r, { ...effect, job: { ...r.job, length: '31' } }));
  assert.throws(() => correlate(r, { ...effect, state: 'SUCCEEDED' }));
  assert.throws(() => correlate({ ...r, module_id: 'forged-module' }, { ...effect, module_id: 'forged-module' }));
  assert.throws(() => correlate({ ...r, request_id: undefined }, { ...effect, request_id: undefined }));
  assert.equal(correlate(r, { ...effect, state: 'REJECTED', run_id: null, job: null }).effect.state, 'REJECTED');
});
