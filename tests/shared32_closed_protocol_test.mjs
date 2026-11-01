import test from 'node:test';import assert from 'node:assert/strict';
import fs from 'node:fs';import {createHash} from 'node:crypto';
import {SourceProtocolSession} from '../tools/engine-module-protocol.mjs';
const directory=new URL('../evidence/shared32-finite-20261012-v2/',import.meta.url);
function read(relative,limit=262144) {
  const file=new URL(relative,directory),before=fs.statSync(file);
  assert.ok(before.size<=limit,'Bounded closed artifact');
  const bytes=fs.readFileSync(file);assert.equal(bytes.length,before.size);
  assert.equal(fs.statSync(file).mtimeMs,before.mtimeMs);return bytes;
}
const manifest=JSON.parse(read('manifest.json'));
function verified(relative) {
  const bytes=read(relative),entry=manifest.files.find(x=>x.path===relative);assert.ok(entry);
  assert.equal(bytes.length,entry.bytes);assert.equal(createHash('sha256').update(bytes).digest('hex').toUpperCase(),entry.sha256);
  return bytes.toString('utf8').replace(/^\uFEFF/,'');
}
const comparison=JSON.parse(verified('comparison-result.json'));
// This declaration is identical in both pinned image closures; not authentication.
const capabilities={schema:'pgn-engine-module-1',id:'cuda-continuous-source-v1',protocol:'phase0-source-1',
  host_refill:false,host_verification:false,whole_job_gpu_owned:true,cuda_arch:'sm86',
  prime_certificate:'MR12 below318665857834031151167461',coverage:'SOURCE_PRP_INDEPENDENT_PROOF_PENDING',
  single_low64_window:true,supported_high_word:10};
const observations=new Map();
for(const tag of ['reference-a','candidate-a','candidate-b','reference-b'])test('Closed '+tag+' transcript through modular backend',()=>{
  const process=JSON.parse(verified(tag+'/process.json'));
  const receipt=comparison.cases.find(x=>x.case===tag);assert.ok(receipt);assert.equal(receipt.exit_code,0);
  assert.equal(process.image_sha256,receipt.image_sha256);assert.equal(process.pid,receipt.pid);
  const identity={job_id:'shared32-golden-finite',device_id:process.device,
    run_id:tag+':'+process.pid+':'+process.birth,owner:'root-finite-comparison',module_id:capabilities.id,
    clock_namespace:'windows-qpc:'+process.pid+':'+process.birth,artifact_sha256:process.image_sha256};
  const context={identity,job:{start:'199999990000000000000',length:'1030792151040'},capabilities,evidence_scope:'FINITE_TEST'};
  const transport={identity,observed_utc:comparison.observed_utc,receipt_monotonic_ns:null,source_age_ns:null,
    provenance:'Closed artifact observation, not original emission or authenticated live transport'};
  const session=new SourceProtocolSession(context,{max_results:256,max_index_bytes:8192}),text=verified(tag+'/stdout'),events=[];
  // Force records and time fields across input boundaries; no source timing rewrite.
  for(let i=0;i<text.length;i+=173)events.push(...session.push(text.slice(i,i+173),transport));
  const observation=session.finish({identity,exit_code:receipt.exit_code,evidence_ref:'shared32-finite-20261012-v2/comparison-result.json#'+tag});
  assert.equal(observation.state,'COMPLETION_ACCEPTED');
  const counter=observation.counter.fields;
  for(const [name,expected]of Object.entries({positions:'1030792151040',tests:'3437901640',gaps:'175',batches:'17'}))assert.equal(counter[name],expected);
  assert.equal(observation.retained_result_identity_count,'175');assert.equal(observation.pending_result_count,'0');
  assert.equal(observation.evidence.network_credit,null);assert.equal(observation.evidence.independent_coverage,null);
  const pairs=events.filter(x=>x.kind==='result').map(x=>x.fields.lower+'/'+x.fields.upper+'/'+x.fields.gap).sort();
  assert.equal(pairs.length,175);assert.equal(new Set(pairs).size,175);
  observations.set(tag,pairs);
});
test('All four closed same-input executions conserve exactly the same pairs',()=>{
  assert.equal(observations.size,4);
  for(const pairs of observations.values())assert.deepEqual(pairs,observations.get('reference-a'));
});
