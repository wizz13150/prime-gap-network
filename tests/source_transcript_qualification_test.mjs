import test from 'node:test';import assert from 'node:assert/strict';
import fs from 'node:fs';import path from 'node:path';import os from 'node:os';
import {qualifySourceTranscript} from '../tools/qualify-source-transcript.mjs';
const source=new URL('../evidence/continuous-native-v1-golden-stdout-20261012.txt',import.meta.url);
assert.ok(fs.statSync(source).size<65536);
const declaration=fs.readFileSync(new URL('../evidence/root460-b22-source-context-20261013.json',import.meta.url),'utf8');
const base=JSON.parse(declaration),context={...base.context,job:{start:'199999990000000000000',length:'1030792151040'}};
test('completed genuine producer wire remains historical, not hardware/proof/credit or fresh data',()=>{
 const receipt=qualifySourceTranscript(source,context,base.termination,175);
 assert.equal(receipt.state,'COMPLETION_ACCEPTED');assert.equal(receipt.retained_result_identity_count,'175');
 assert.equal(receipt.original_counter.positions,'1030792151040');
 assert.equal(receipt.last_transport.observed_utc,null);assert.equal(receipt.last_transport.age_at_receipt_ns,null);
 assert.equal(receipt.evidence.independent_coverage,null);assert.equal(receipt.evidence.network_credit,null);
 assert.equal(receipt.gpu_control_calls,0);assert.equal(receipt.cpu_mining,false);
});
test('explicit termination, identity and result budget remain mandatory',()=>{
 assert.throws(()=>qualifySourceTranscript(source,context,{...base.termination,exit_code:1},175));
 assert.throws(()=>qualifySourceTranscript(source,context,{...base.termination,identity:{...context.identity,run_id:'wrong'}},175));
 assert.throws(()=>qualifySourceTranscript(source,context,base.termination,174));
 assert.throws(()=>qualifySourceTranscript(source,{...context,evidence_scope:'FINITE_TEST'},base.termination,175));
});
test('truncated/non-ASCII source rejected without modifying supplied bytes',()=>{
 const directory=fs.mkdtempSync(path.join(os.tmpdir(),'pgn-source-transcript-'));
 try{
  const original=fs.readFileSync(source),file=path.join(directory,'source.stdout');
  fs.writeFileSync(file,original.subarray(0,original.length-1));const truncated=fs.readFileSync(file);
  assert.throws(()=>qualifySourceTranscript(file,context,base.termination,175));assert.deepEqual(fs.readFileSync(file),truncated);
  fs.writeFileSync(file,Buffer.concat([original,Buffer.from([255])]));const corrupt=fs.readFileSync(file);
  assert.throws(()=>qualifySourceTranscript(file,context,base.termination,175));assert.deepEqual(fs.readFileSync(file),corrupt);
 }finally{fs.rmSync(directory,{recursive:true,force:true});}
});
