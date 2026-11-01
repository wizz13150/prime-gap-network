import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import { fileURLToPath } from 'node:url';
import { spawnSync, spawn } from 'node:child_process';
import { createHash } from 'node:crypto';
import { ObservationModel, ModelRefusal, DEPENDENCIES } from '../tools/engine-module-observation-model.mjs';
import { SourceJournal } from '../tools/engine-module-journal.mjs';
const sha=b=>createHash('sha256').update(b).digest('hex');
function bounded(file,max=65536){const s=fs.statSync(file);assert.ok(s.isFile()&&s.size<=max);const b=fs.readFileSync(file);assert.equal(b.length,s.size);return b;}
const golden=bounded(new URL('../evidence/continuous-native-v1-golden-stdout-20261012.txt',import.meta.url));
assert.equal(sha(golden),'e860081459982dabe55fc0b3e633f7941793903b7e1a5480faeb074ebafc0e8b');
const rows=golden.toString().trimEnd().split(/\r?\n/),header=golden.subarray(0,golden.indexOf(10)+1),gap=Buffer.from(rows[1]+'\r\n');
const worker=new URL('../tools/engine-module-observer-worker.mjs',import.meta.url),workerFile=fileURLToPath(worker);
const caps={schema:'pgn-engine-module-1',id:'cuda-continuous-source-v1',protocol:'phase0-source-1',host_refill:false,host_verification:false,
  whole_job_gpu_owned:true,cuda_arch:'sm86',prime_certificate:'MR12 below318665857834031151167461',coverage:'SOURCE_PRP_INDEPENDENT_PROOF_PENDING',
  single_low64_window:true,supported_high_word:10};
const context=()=>({identity:{job_id:'closed-model-job',device_id:'external-fixture-device',run_id:'paid-original-run',module_id:caps.id,
  owner:'closed-model-owner',clock_namespace:'original-fixture-clock',artifact_sha256:'ADD6C947E115C477F0DB2DD1C605AA61AECCE838CCECC1D2C2A9FBF65E88C158'},
  job:{start:'199999990000000000000',length:'1030792151040'},capabilities:{...caps},evidence_scope:'FINITE_TEST'});
const journalBudgets=()=>({max_records:10000,max_bytes:1048576,max_record_bytes:65536,max_results:1024,max_index_bytes:24576,
  max_fragment_envelopes:64,max_generations:8,max_generation_bytes:131072});
const envelope=()=>({identity:context().identity,observed_utc:'original-harness-provenance-not-real-hardware',source_clock_ns:null,
  receipt_monotonic_ns:'9007199254740993',age_at_receipt_ns:'0000',source_age_ns:null});
const termination=()=>({identity:context().identity,exit_code:0,evidence_ref:'explicit-paid-fixture-closed-test-termination'});
const transport=()=>({max_command_bytes:16384,max_input_bytes:1048576,max_commands:512,max_id_bytes:64,read_chunk_bytes:257,
  max_response_bytes:16777216,max_output_bytes:67108864});
const budgets=()=>({max_reply_bytes:262144,max_state_bytes:4194304,max_results:1024,max_records:2048,max_requests:512,max_connections:8});
const root=fs.mkdtempSync(path.join(os.tmpdir(),'pgn-observation-model-20261013-'));let serial=0;
function fresh(bytes=golden,chunk=256){const dir=path.join(root,String(++serial));fs.mkdirSync(dir);const f={source_file:path.join(dir,'finite.stdout'),journal_file:path.join(dir,'private.wal'),chunk};
  fs.writeFileSync(f.source_file,bytes,{flag:'wx'});return f;}
const command=(id,op,extra={})=>({id,op,...extra});
function open(f,create=true,id='open'){return command(id,'open',{options:{source_file:f.source_file,journal_file:f.journal_file,context:context(),budgets:journalBudgets(),
  policy:{encoding:'NATIVE_ASCII',max_source_bytes:65536,max_chunk_bytes:f.chunk},
  provenance:{evidence_ref:'closed-fixture-copy-caller-bound',trust_scope:'COOPERATING_APPEND_ONLY_LOCAL_FILE',source_kind:'CLOSED_FIXTURE'},create}});}
const pump=id=>command(id,'pump',{envelope:envelope()});
const snap=id=>command(id,'snapshot');
function run(commands,args=null){const r=spawnSync(process.execPath,args??[workerFile,'--limits-json='+JSON.stringify(transport())],
  {input:commands.map(x=>JSON.stringify(x)+'\n').join(''),maxBuffer:16777216,timeout:20000});
  assert.equal(r.error,undefined);assert.equal(r.stderr.length,0,r.stderr.toString());return {exit:r.status,replies:r.stdout.toString().trimEnd().split('\n').filter(Boolean).map(JSON.parse)};}
function config(f,b=budgets()){
  const o=open(f).options,s=fs.statSync(f.source_file,{bigint:true});return {context:context(),dependencies:{...DEPENDENCIES},budgets:b,
    observer_binding:{schema:'pgn-source-file-observer-1',source:{path:f.source_file,dev:String(s.dev),ino:String(s.ino)},journal_file:f.journal_file,
      context:o.context,budgets:o.budgets,policy:o.policy,provenance:o.provenance,journal_sha256:DEPENDENCIES.journal.toLowerCase(),generation_policy:'REFUSE_EXTERNAL_RESUME'}};
}
function model(f,b=budgets(),id='c1'){const m=new ObservationModel(config(f,b));m.beginConnection({id,dependencies:DEPENDENCIES});return m;}
const apply=(m,c,r,connection='c1')=>{m.expect({connection,command:c});return m.applyReply({connection,reply:r});};
const copy=x=>JSON.parse(JSON.stringify(x));
function refusal(fn,code){assert.throws(fn,e=>e instanceof ModelRefusal&&(!code||e.code===code));}
function unchanged(m,fn,code){const before=JSON.stringify(m.observation());refusal(fn,code);assert.equal(JSON.stringify(m.observation()),before);}

import {readJournalEventPage} from '../tools/engine-module-event-page.mjs';
const READER='87518B38176C7236AE8BA0ADDC6837BEBDF633D1BD048A8FCC4F9110AA17607F';

function cold(){
 const f=fresh(golden,4096),first=[open(f)];
 for(let i=0;i<Math.ceil(golden.length/4096);i++)first.push(pump('lost'+i));
 first.push(command('close','close'));const producer=run(first);assert.equal(producer.exit,0);
 const originals=producer.replies.flatMap(x=>x.result?.events??[]);
 const cs=[open(f,false),snap('snap'),command('close','close')],restarted=run(cs);assert.equal(restarted.exit,0);
 const m=model(f);for(let i=0;i<cs.length;i++)apply(m,cs[i],restarted.replies[i]);
 assert.equal(m.observation().result_delivery.missing_at_least,'175');
 return {f,m,originals};
}
function page(x,cursor={sequence:'0',event_offset:'0'}){
 return readJournalEventPage({file:x.f.journal_file,context:context(),budgets:journalBudgets(),
  base_sha256:x.m.observation().source_head.sha256,cursor,
  limits:{max_wal_bytes:1048576,max_replay_index_bytes:49152,max_page_events:12,max_page_bytes:16384}});
}
test('actual retained events fill all175 cold-client gaps without rebasing values or IPC receipts',()=>{
 const x=cold(),before=x.m.observation(),original=fs.readFileSync(x.f.journal_file);let cursor={sequence:'0',event_offset:'0'},pages=0;
 do{const p=page(x,cursor);x.m.applyHistoricalPage({page:p,reader_sha256:READER});cursor=p.next_cursor;assert.ok(++pages<100);}while(cursor);
 const after=x.m.observation();assert.equal(after.result_records.length,175);assert.equal(after.result_delivery.missing_at_least,'0');
 assert.equal(after.result_delivery.complete_at_current_snapshot,true);
 assert.deepEqual(after.result_records.map(x=>x.record),x.originals.filter(x=>x.kind==='result'));
 assert.deepEqual(after.source_values,before.source_values);assert.deepEqual(after.availability,before.availability);
 assert.deepEqual(after.request_receipts,before.request_receipts);assert.deepEqual(after.source_head,before.source_head);
 assert.equal(after.evidence.source_completion_accepted,false);assert.equal(after.evidence.network_credit,null);
 assert.ok(after.result_records.every(x=>x.origin.historical===true));assert.ok(fs.readFileSync(x.f.journal_file).equals(original));
});
test('historical page overlap is idempotent and conflicting scope/record refuses atomically',()=>{
 const x=cold(),p=page(x);x.m.applyHistoricalPage({page:p,reader_sha256:READER});const before=JSON.stringify(x.m.observation());
 x.m.applyHistoricalPage({page:p,reader_sha256:READER});assert.equal(JSON.stringify(x.m.observation()),before);
 for(const bad of [{...p,base_sha256:'0'.repeat(64)},{...p,context_sha256:'0'.repeat(64)}]){
  unchanged(x.m,()=>x.m.applyHistoricalPage({page:bad,reader_sha256:READER}));
 }
 const bad=copy(p),item=bad.events.find(x=>x.event.kind==='result');item.event.fields.upper='1';
 unchanged(x.m,()=>x.m.applyHistoricalPage({page:bad,reader_sha256:READER}));
 unchanged(x.m,()=>x.m.applyHistoricalPage({page:p,reader_sha256:'0'.repeat(64)}),'EVENT_READER_VERSION');
});
test('page/record budget overflow preserves the previous client and does not silently drop gaps',()=>{
 const x=cold(),p=page(x),m=model(x.f,{...budgets(),max_results:1},'small');
 const cs=[open(x.f,false),snap('s'),command('c','close')],r=run(cs);for(let i=0;i<cs.length;i++)apply(m,cs[i],r.replies[i],'small');
 unchanged(m,()=>m.applyHistoricalPage({page:p,reader_sha256:READER}),'RESULT_BUDGET');
});

