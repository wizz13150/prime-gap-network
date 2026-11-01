import assert from 'node:assert/strict';
import test from 'node:test';
import {runtimeSnapshotPublication,runtimeBindingIdentityChanged} from '../tools/runtime-snapshot-publication.mjs';
test('actual publication keeps observed binding/manifest/lineage without promoting ACK',()=>{
 const binding={uuid:'GPU-fixture',start:'199900000000000000000',end:'199901000000000000000',queued_intent:'fixture',resume_start:{run:'b'.repeat(32),previous_run:'a'.repeat(32),checkpoint_sha256:'c'.repeat(64)}};
 const manifest={run:'b'.repeat(32),start:binding.start,end:binding.end,resume_previous_run:'a'.repeat(32),checkpoint_input_sha256:'c'.repeat(64)};
 const snapshot={schema:'pgn-runtime-snapshot-1',type:'untrusted-source-tag',binding,manifest,owned:true,state:'RUNNING',chunk:'raw-byte-payload',telemetry_lines:['original telemetry'],observed_utc:'2026-10-14T00:00:00Z'};
 const before=structuredClone(snapshot),result=runtimeSnapshotPublication(snapshot,['source row'],['raw counter']);
 assert.deepEqual(result.binding,binding);assert.deepEqual(result.manifest,manifest);assert.equal(result.type,'snapshot');
 assert.equal(result.observed_utc,snapshot.observed_utc);assert.equal(result.state,'RUNNING');assert.equal(result.owned,true);
 assert.deepEqual(result.lines,['source row','raw counter']);assert.equal('applied' in result,false);assert.equal('chunk' in result,false);assert.equal('telemetry_lines' in result,false);
 assert.deepEqual(snapshot,before,'Publication must not mutate the source/conservation packet');
});
test('absent/unknown binding, ownership and source ages stay absent/unknown, never invented',()=>{
 for(const fields of [{},{binding:null,owned:null},{binding:{},owned:false,state:'OFF'}]){
  const s={schema:'pgn-runtime-snapshot-1',...fields},p=runtimeSnapshotPublication(s,[],[]);
  assert.equal(Object.hasOwn(p,'binding'),Object.hasOwn(s,'binding'));assert.deepEqual(p.binding,s.binding);assert.equal(p.owned,s.owned);
  assert.equal('observed_utc' in p,false);assert.equal('applied' in p,false);
 }
});
test('malformed inputs cannot become a native observation',()=>{
 for(const [s,d,t] of [[null,[],[]],[{schema:'wrong'},[],[]],[{schema:'pgn-runtime-snapshot-1'},null,[]],[{schema:'pgn-runtime-snapshot-1'},[],[42]]])assert.throws(()=>runtimeSnapshotPublication(s,d,t),/contract/);
});
test('resuming the same range is still a changed physical binding identity',()=>{
 const before={uuid:'GPU-fixture',start:'199900000000000000000',end:'199901000000000000000',queued_intent:'same-job'};
 const resumed={...before,resume_start:{schema:'pgn-checkpoint-resume-marker-1',run:'b'.repeat(32),previous_run:'a'.repeat(32)}};
 assert.equal(runtimeBindingIdentityChanged(before,{...before}),false);assert.equal(runtimeBindingIdentityChanged(before,resumed),true);
 assert.equal(runtimeBindingIdentityChanged(resumed,structuredClone(resumed)),false);assert.equal(runtimeBindingIdentityChanged(resumed,before),true);
 assert.equal(runtimeBindingIdentityChanged(before,{...before,queued_intent:'another-job'}),true);
 assert.throws(()=>runtimeBindingIdentityChanged(null,resumed),/contract/);
});
