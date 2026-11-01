import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {SourceJournal} from '../tools/engine-module-journal.mjs';
import {readJournalEventPage} from '../tools/engine-module-event-page.mjs';
const dir=path.resolve('pgn/private-qa/engine-module-event-page-20261013');
fs.mkdirSync(dir,{recursive:true});
const goldenFile=new URL('../evidence/continuous-native-v1-golden-stdout-20261012.txt',import.meta.url);
assert.equal(fs.statSync(goldenFile).size,30797);
const golden=fs.readFileSync(goldenFile);
const sha=x=>createHash('sha256').update(x).digest('hex');
assert.equal(sha(golden),'e860081459982dabe55fc0b3e633f7941793903b7e1a5480faeb074ebafc0e8b');
const context={identity:{job_id:'event-page-job',device_id:'closed-fixture-device',run_id:'original-run',
 module_id:'cuda-continuous-source-v1',owner:'finite-owner',clock_namespace:'original-qpc',
 artifact_sha256:'ADD6C947E115C477F0DB2DD1C605AA61AECCE838CCECC1D2C2A9FBF65E88C158'},
 job:{start:'199999990000000000000',length:'1030792151040'},evidence_scope:'FINITE_TEST',
 capabilities:{schema:'pgn-engine-module-1',id:'cuda-continuous-source-v1',protocol:'phase0-source-1',
 host_refill:false,host_verification:false,whole_job_gpu_owned:true,cuda_arch:'sm86',
 prime_certificate:'MR12 below318665857834031151167461',coverage:'SOURCE_PRP_INDEPENDENT_PROOF_PENDING',
 single_low64_window:true,supported_high_word:10}};
const budgets={max_records:10000,max_bytes:1048576,max_record_bytes:65536,max_results:1024,
 max_index_bytes:24576,max_fragment_envelopes:64,max_generations:8,max_generation_bytes:131072};
const envelope={identity:context.identity,observed_utc:'2026-10-12T21:10:00.1234567Z',
 receipt_monotonic_ns:'9007199254740993',age_at_receipt_ns:'0000',source_age_ns:null};
const limits={max_wal_bytes:1048576,max_replay_index_bytes:49152,max_page_events:10,max_page_bytes:16384};
function fixture(finish=false,split=false){
 const folder=fs.mkdtempSync(path.join(dir,'case-')),file=path.join(folder,'journal.jsonl');
 const j=SourceJournal.open({file,context,budgets,create:true});let events=[];
 if(split){events.push(...j.push(golden.subarray(0,17).toString(),envelope).events);
  const second={...envelope};delete second.source_age_ns;events.push(...j.push(golden.subarray(17).toString(),second).events);}
 else events.push(...j.push(golden.toString(),envelope).events);
 if(finish)j.finish({identity:context.identity,exit_code:0,evidence_ref:'fixture-explicit-exit-not-physical-proof'});
 const snapshot=j.snapshot();j.close();return {file,events,snapshot};
}
const query=(f,extra={})=>readJournalEventPage({file:f.file,context,budgets,limits,base_sha256:null,
 cursor:{sequence:'0',event_offset:'0'},...extra});
test('paged actual typed events equal all original175 results and original timing, no acknowledgement claim',()=>{
 const f=fixture(false,true),original=fs.readFileSync(f.file),actual=[];let base=null,cursor={sequence:'0',event_offset:'0'},pages=0;
 do{const p=query(f,{base_sha256:base,cursor});assert.ok(Buffer.byteLength(JSON.stringify(p))<=limits.max_page_bytes);
  assert.equal(p.source_state,'END_REPORTED');assert.equal(p.source_evidence.source_completion_accepted,false);
  assert.equal(p.source_evidence.independent_coverage,null);assert.equal(p.source_evidence.network_credit,null);
  base=p.base_sha256;actual.push(...p.events.map(x=>x.event));cursor=p.next_cursor;
  assert.equal(p.complete_at_digest,cursor===null);assert.ok(++pages<100);
 }while(cursor);
 assert.deepEqual(actual,f.events);assert.equal(actual.filter(x=>x.kind==='result').length,175);
 assert.ok(fs.readFileSync(f.file).equals(original));assert.ok(!fs.existsSync(f.file+'.lock'));
});
test('accepted source termination stays separate from historical page completion',()=>{
 const f=fixture(true),p=query(f);assert.equal(p.source_evidence.source_completion_accepted,true);
 assert.equal(p.source_evidence.acceptance_boundary,'SUPPLIED_TRANSCRIPT_AND_TERMINATION_CONFORMANCE_ONLY');
});
test('page cursor can restart inside one committed push without omission or duplicate',()=>{
 const f=fixture(),p=query(f,{limits:{...limits,max_page_events:1}});assert.equal(p.events.length,1);
 assert.equal(p.next_cursor.sequence,'1');assert.equal(p.next_cursor.event_offset,'1');
 const q=query(f,{base_sha256:p.base_sha256,cursor:p.next_cursor,limits:{...limits,max_page_events:1}});
 assert.deepEqual(q.events[0].event,f.events[1]);assert.notDeepEqual(q.events[0],p.events[0]);
});
test('wrong digest and impossible cursor refuse, retain exact bytes and release owned lock',()=>{
 const f=fixture(),old=fs.readFileSync(f.file);
 for(const x of [{base_sha256:'0'.repeat(64)},{cursor:{sequence:'1',event_offset:'9999'}},
  {base_sha256:null,cursor:{sequence:'1',event_offset:'0'}}]){
  assert.throws(()=>query(f,x));assert.ok(fs.readFileSync(f.file).equals(old));assert.ok(!fs.existsSync(f.file+'.lock'));
 }
});
test('both actual packed index allocations must be explicitly paid before acquiring writer',()=>{
 const f=fixture();assert.throws(()=>query(f,{limits:{...limits,max_replay_index_bytes:49151}}),e=>e.code==='REPLAY_INDEX_BUDGET');
 assert.ok(!fs.existsSync(f.file+'.lock'));
});
test('active journal writer excludes historical query; no stale-lock steal',()=>{
 const f=fixture(),j=SourceJournal.open({file:f.file,context,budgets,create:false});
 try{assert.throws(()=>query(f),e=>e.code==='WRITER_LOCKED');assert.ok(fs.existsSync(f.file+'.lock'));}finally{j.close();}
});
test('corrupt or partial journal refuses without repairing bytes',()=>{
 const f=fixture();fs.appendFileSync(f.file,'{incomplete');const old=fs.readFileSync(f.file);
 assert.throws(()=>query(f));assert.ok(fs.readFileSync(f.file).equals(old));
});
test('WAL and response capacity never silently truncate retained evidence',()=>{
 const f=fixture(),old=fs.readFileSync(f.file);
 assert.throws(()=>query(f,{limits:{...limits,max_wal_bytes:1}}),e=>e.code==='WAL_BYTE_BUDGET');
 assert.throws(()=>query(f,{limits:{...limits,max_page_bytes:1024}}),e=>['EVENT_EXCEEDS_PAGE','PAGE_METADATA_BOUND'].includes(e.code));
 assert.ok(fs.readFileSync(f.file).equals(old));
});
test('ordinary source refusal preserves accepted-prefix events, never marks active or completed',()=>{
 const folder=fs.mkdtempSync(path.join(dir,'refusal-')),file=path.join(folder,'journal.jsonl');
 const j=SourceJournal.open({file,context,budgets,create:true});
 const lines=golden.toString().split(/\r?\n/),row=lines.find(x=>x.startsWith('[phase0-gpu] GAP '));
 let original;
 try{j.push(lines[0]+'\r\n'+row+'\r\n'+row+'\r\n',envelope);assert.fail('duplicate must refuse');}
 catch(e){assert.ok(e.journal_commit);original=e.observed_events;}
 finally{j.close();}
 const p=query({file});assert.equal(p.source_state,'REFUSED');
 assert.equal(p.source_evidence.source_completion_accepted,false);
 assert.deepEqual(p.events.map(x=>x.event),original);
 assert.equal(p.events.filter(x=>x.event.kind==='result').length,1);
});
