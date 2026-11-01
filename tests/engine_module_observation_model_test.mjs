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
const pinned=['../tools/engine-module-observer-worker.mjs','../tests/engine_module_observer_worker_test.mjs',
  '../evidence/engine-module-observer-worker-head-20261013.json','../evidence/engine-module-observer-worker-tests-20261013.json',
  '../tools/engine-module-file-observer.mjs','../tools/engine-module-journal.mjs','../tools/engine-module-protocol.mjs','../tools/engine-module-contract.mjs',
  '../evidence/continuous-native-v1-golden-stdout-20261012.txt'];
const pins=pinned.map(p=>({file:new URL(p,import.meta.url),hash:sha(bounded(new URL(p,import.meta.url)))}));
const fullFile=fresh(),fullCommands=[open(fullFile)];
for(let i=0;i<Math.ceil(golden.length/256);i++){fullCommands.push(pump('p'+i));if(i===9||i===59)fullCommands.push(snap('s'+i));}
fullCommands.push(snap('before'),command('finish','finish',{termination:termination()}),snap('after'),command('close','close'));
const full=run(fullCommands);assert.equal(full.exit,0);assert.equal(full.replies.length,fullCommands.length);
const modelCache=new Map();
function fullModel(end=fullCommands.length){
  if(!modelCache.has(end)){const m=model(fullFile);for(let i=0;i<end;i++)apply(m,fullCommands[i],full.replies[i]);modelCache.set(end,m);}
  return modelCache.get(end).fork();
}
const beforeIndex=fullCommands.findIndex(x=>x.id==='before'),closeIndex=fullCommands.length-1;
test('actual worker typed replies retain exact 175 GAPs and final native counters',()=>{
  const m=fullModel(),o=m.observation();assert.equal(o.result_records.length,175);
  assert.deepEqual(o.result_records.map(x=>x.record.raw_line.replace(/\r$/,'')),rows.filter(x=>x.includes('] GAP ')));
  assert.equal(o.result_delivery.reported_retained,'175');assert.equal(o.result_delivery.missing_at_least,'0');assert.equal(o.result_delivery.complete_at_current_snapshot,true);
  for(const [k,v] of Object.entries({positions:'1030792151040',tests:'3437901640',gaps:'175',batches:'17'}))assert.equal(o.source_values.counter.record.fields[k],v);
  assert.equal(o.evidence.source_completion_accepted,true);assert.equal(o.evidence.network_credit,null);assert.equal(o.evidence.independent_coverage,null);
  assert.equal(o.evidence.controller_effect,null);assert.equal(o.evidence.lease,null);assert.equal(o.availability.state,'HISTORICAL');
});
test('footer and incremental end do not imply accepted termination',()=>{
  const m=fullModel(beforeIndex+1),o=m.observation();assert.equal(o.protocol_metadata.state,'END_REPORTED');assert.equal(o.evidence.source_completion_accepted,false);
  assert.equal(o.protocol_metadata.termination,null);assert.equal(o.availability.state,'OPEN');
});
test('worker EOF/disconnect changes availability only, preserving exact values and age',()=>{
  const m=fullModel(beforeIndex+1),before=m.observation().source_values.counter.record;m.disconnect({connection:'c1',reason:'EOF'});const after=m.observation();
  assert.equal(after.availability.state,'UNAVAILABLE');assert.deepEqual(after.source_values.counter.record,before);assert.equal(after.source_values.counter.historical,true);
  assert.equal(after.evidence.source_completion_accepted,false);assert.equal(after.source_values.counter.record.transport.age_at_receipt_ns,'0000');
});
test('pipe disconnect AFTER journal commit recovers metadata but proves GAP export deficit',async t=>{
  const f=fresh(golden,4096),marker=path.join(path.dirname(f.source_file),'post-commit-marker'),commands=[open(f),pump('lost')];
  const script=`import fs from 'node:fs';import {runObserverWorker} from ${JSON.stringify(worker.href)};
    const write=process.stdout.write.bind(process.stdout);let count=0;process.stdout.write=(b,cb)=>{
      if(++count===2){fs.writeFileSync(${JSON.stringify(marker)},'POST_COMMIT_PRE_REPLY');
        setTimeout(()=>write(b,cb),500);return false;}return write(b,cb);};
    const r=await runObserverWorker({input:process.stdin,output:process.stdout,limits:${JSON.stringify(transport())}});process.exitCode=r.exit_code;`;
  const child=spawn(process.execPath,['--input-type=module','-e',script],{stdio:['pipe','pipe','pipe']});let output=[],errors=[];
  child.stdin.on('error',()=>{});child.stdout.on('data',b=>output.push(b));child.stderr.on('data',b=>errors.push(b));
  const done=new Promise(resolve=>child.once('close',code=>resolve(code)));child.stdin.end(commands.map(x=>JSON.stringify(x)+'\n').join(''));
  const deadline=Date.now()+3000;while(!fs.existsSync(marker)){assert.ok(Date.now()<deadline,'post-commit marker timeout');await new Promise(r=>setTimeout(r,5));}
  child.stdout.destroy();const lost={exit:await done,replies:Buffer.concat(output).toString().trimEnd().split('\n').filter(Boolean).map(JSON.parse)};
  assert.equal(Buffer.concat(errors).length,0);assert.equal(lost.exit,3);assert.equal(lost.replies.length,1);
  assert.equal(fs.readFileSync(marker,'utf8'),'POST_COMMIT_PRE_REPLY');assert.equal(fs.existsSync(f.journal_file+'.lock'),false);
  const audit=SourceJournal.audit({file:f.journal_file,context:context(),budgets:journalBudgets()});assert.ok(BigInt(audit.protocol.retained_result_identity_count)>0n);
  const m=model(f);apply(m,commands[0],lost.replies[0]);m.expect({connection:'c1',command:commands[1]});m.disconnect({connection:'c1',reason:'PIPE_LOST'});
  m.beginConnection({id:'c2',dependencies:DEPENDENCIES});const recovery=[open(f,false),snap('recover'),command('close','close')],actual=run(recovery);assert.equal(actual.exit,0);
  for(let i=0;i<recovery.length;i++)apply(m,recovery[i],actual.replies[i],'c2');const o=m.observation();
  assert.equal(o.result_records.length,0);assert.equal(o.result_delivery.known_records,'0');assert.equal(o.result_delivery.reported_retained,audit.protocol.retained_result_identity_count);
  assert.equal(o.result_delivery.missing_at_least,audit.protocol.retained_result_identity_count);assert.equal(o.result_delivery.complete_at_current_snapshot,false);
  assert.equal(o.result_delivery.snapshot_result_entries_available,false);assert.deepEqual(o.source_values.counter.record,audit.protocol.counter);
  assert.equal(o.source_values.counter.historical,true);assert.equal(o.evidence.source_completion_accepted,false);
  assert.equal(Object.values(o.request_receipts).find(x=>x.id==='lost').status,'UNKNOWN_NOT_ACKNOWLEDGED');
  t.diagnostic('RESTART_GAP_DEFICIT '+JSON.stringify({committed_cursor:o.consumed_cursor,known:o.result_delivery.known_records,
    retained:o.result_delivery.reported_retained,missing:o.result_delivery.missing_at_least,
    counter:Object.fromEntries(['positions','tests','gaps','batches','time_ns'].map(k=>[k,o.source_values.counter.record.fields[k]]))}));
});
test('exact same reply replay is idempotent, including original ID receipt',()=>{
  const m=fullModel(beforeIndex+1),i=10,r=full.replies[i],before=JSON.stringify(m.observation());
  assert.equal(m.applyReply({connection:'c1',reply:r}).status,'EXACT_REPLY_REPLAY');assert.equal(JSON.stringify(m.observation()),before);
});
test('conflicting duplicate reply refuses before any mutation',()=>{
  const m=fullModel(3),r=copy(full.replies[2]);r.result.cursor='999';unchanged(m,()=>m.applyReply({connection:'c1',reply:r}),'REPLY_CONFLICT');
});
test('events plus full snapshot overlap preserves GAP records and timing identity',()=>{
  const m=fullModel(beforeIndex),prior=m.observation().result_records;apply(m,fullCommands[beforeIndex],full.replies[beforeIndex]);
  assert.deepEqual(m.observation().result_records,prior);assert.equal(m.observation().result_delivery.complete_at_current_snapshot,true);
});
test('clean new worker replay never makes old source counter fresh',()=>{
  const m=fullModel(),prior=m.observation().source_values.counter;
  m.beginConnection({id:'restart',dependencies:DEPENDENCIES});const cs=[open(fullFile,false),snap('replay'),command('close','close')],r=run(cs);assert.equal(r.exit,0);
  apply(m,cs[0],r.replies[0],'restart');apply(m,cs[1],r.replies[1],'restart');const o=m.observation();
  assert.deepEqual(o.source_values.counter.record,prior.record);assert.deepEqual(o.source_values.counter.origin,prior.origin);
  assert.equal(o.source_values.counter.historical,true);assert.equal(o.result_records.length,175);apply(m,cs[2],r.replies[2],'restart');
});
test('known stale snapshot cannot erase newer counters/records or change source head',()=>{
  const m=fullModel();m.beginConnection({id:'restart',dependencies:DEPENDENCIES});const cs=[open(fullFile,false),snap('stale')],r=run(cs);apply(m,cs[0],r.replies[0],'restart');
  const old=copy(full.replies.find(x=>x.id==='s9'));old.id='stale';old.state='OPEN';const before=m.observation();apply(m,cs[1],old,'restart');const after=m.observation();
  assert.deepEqual(after.source_values.counter,before.source_values.counter);assert.deepEqual(after.source_head,before.source_head);assert.equal(after.result_records.length,175);
  assert.equal(after.result_delivery.reported_retained,'175');
});
test('conflicting stale counter record is refused transactionally',()=>{
  const m=fullModel();m.beginConnection({id:'restart',dependencies:DEPENDENCIES});const cs=[open(fullFile,false),snap('stale')],r=run(cs);apply(m,cs[0],r.replies[0],'restart');m.expect({connection:'restart',command:cs[1]});
  const old=copy(full.replies.find(x=>x.id==='s9'));old.id='stale';old.state='OPEN';old.result.protocol.counter.fields.time_ns='1';
  unchanged(m,()=>m.applyReply({connection:'restart',reply:old}),'RECORD_CONFLICT');
});
test('unknown stale journal head is explicitly unproven, not treated as fresh',()=>{
  const m=fullModel();m.beginConnection({id:'restart',dependencies:DEPENDENCIES});const cs=[open(fullFile,false),snap('stale')],r=run(cs);apply(m,cs[0],r.replies[0],'restart');m.expect({connection:'restart',command:cs[1]});
  const old=copy(full.replies.find(x=>x.id==='s9'));old.id='stale';old.state='OPEN';old.result.journal.records='1';old.result.journal.last_sha256='a'.repeat(64);
  unchanged(m,()=>m.applyReply({connection:'restart',reply:old}),'UNPROVEN_STALE_HEAD');
});
test('changed known journal hash refuses exact conflict',()=>{
  const m=fullModel(beforeIndex),c=fullCommands[beforeIndex],r=copy(full.replies[beforeIndex]);m.expect({connection:'c1',command:c});r.result.journal.last_sha256='0'.repeat(64);
  unchanged(m,()=>m.applyReply({connection:'c1',reply:r}),'JOURNAL_CONFLICT');
});
test('known GAP replay with a new captured envelope is harmless only when exactly identical',()=>{
  const m=fullModel(beforeIndex+1),i=full.replies.findIndex(x=>x.result?.events?.some(e=>e.kind==='result')),r=copy(full.replies[i]);
  r.id='overlap';m.expect({connection:'c1',command:pump('overlap')});const count=m.observation().result_records.length;
  m.applyReply({connection:'c1',reply:r});assert.equal(m.observation().result_records.length,count);
});
test('scientific or original-clock conflict on known scoped GAP refuses',()=>{
  for(const field of ['merit','age']){
    const m=fullModel(beforeIndex+1),i=full.replies.findIndex(x=>x.result?.events?.some(e=>e.kind==='result')),r=copy(full.replies[i]);r.id='attack';
    const e=r.result.events.find(x=>x.kind==='result');if(field==='merit')e.fields.merit='99.0000';else e.transport.age_at_receipt_ns='0';
    m.expect({connection:'c1',command:pump('attack')});unchanged(m,()=>m.applyReply({connection:'c1',reply:r}),'RECORD_CONFLICT');
  }
});
test('numerically equal leading-zero GAP cannot create a second cache identity',()=>{
  const m=fullModel(beforeIndex+1),i=full.replies.findIndex(x=>x.result?.events?.some(e=>e.kind==='result')),r=copy(full.replies[i]);r.id='zeros';
  const e=r.result.events.find(x=>x.kind==='result');e.fields.lower='0'+e.fields.lower;e.fields.upper='0'+e.fields.upper;
  m.expect({connection:'c1',command:pump('zeros')});unchanged(m,()=>m.applyReply({connection:'c1',reply:r}),'RECORD_CONFLICT');
});
for(const field of ['run_id','clock_namespace','device_id','job_id','module_id','owner','artifact_sha256'])
  test('typed event '+field+' binding mismatch refuses before mutation',()=>{
    const m=fullModel(1),r=copy(full.replies[1]);m.expect({connection:'c1',command:fullCommands[1]});r.result.events[0].identity[field]='other';
    unchanged(m,()=>m.applyReply({connection:'c1',reply:r}),'IDENTITY_MISMATCH');
  });
test('source snapshot path/inode binding mismatch refuses',()=>{
  const m=fullModel(beforeIndex),r=copy(full.replies[beforeIndex]);m.expect({connection:'c1',command:fullCommands[beforeIndex]});r.result.observer.binding.source.ino='999999';
  unchanged(m,()=>m.applyReply({connection:'c1',reply:r}),'SNAPSHOT_BINDING');
});
test('protocol context or native generation promotion is refused',()=>{
  for(const mutate of [r=>{r.result.protocol.context.identity.clock_namespace='other';},r=>{r.result.protocol.resume_binding={abi:'windows-x64-le-64'};},
    r=>{r.result.journal.generation_lineage.push(copy(r.result.journal.generation_lineage[0]));}]){
    const m=fullModel(beforeIndex),r=copy(full.replies[beforeIndex]);m.expect({connection:'c1',command:fullCommands[beforeIndex]});mutate(r);
    unchanged(m,()=>m.applyReply({connection:'c1',reply:r}));
  }
});
test('proof/network/controller credit promotion is rejected rather than absorbed',()=>{
  for(const field of ['independent_coverage','network_credit','controller_effect']){
    const m=fullModel(beforeIndex),r=copy(full.replies[beforeIndex]);m.expect({connection:'c1',command:fullCommands[beforeIndex]});r.result.protocol.evidence[field]='claimed';
    unchanged(m,()=>m.applyReply({connection:'c1',reply:r}),'EVIDENCE_PROMOTION');
  }
});
test('fabricated completion without explicit termination is rejected',()=>{
  const m=fullModel(beforeIndex),r=copy(full.replies[beforeIndex]);m.expect({connection:'c1',command:fullCommands[beforeIndex]});r.result.protocol.state='COMPLETION_ACCEPTED';r.result.protocol.evidence.source_completion_accepted=true;
  unchanged(m,()=>m.applyReply({connection:'c1',reply:r}),'COMPLETION_CONTRACT');
});
test('worker refusal accepted prefix retains typed record without approving producer duplicate',()=>{
  const f=fresh(Buffer.concat([header,gap,gap]),1024),cs=[open(f),pump('bad')],r=run(cs);assert.equal(r.exit,2);assert.equal(r.replies[1].error.code,'DUPLICATE_RESULT');
  const m=model(f);apply(m,cs[0],r.replies[0]);apply(m,cs[1],r.replies[1]);const o=m.observation();
  assert.equal(o.result_records.length,1);assert.equal(o.availability.state,'UNAVAILABLE');assert.equal(o.last_error.error.code,'DUPLICATE_RESULT');assert.equal(o.evidence.source_completion_accepted,false);
  assert.equal(m.applyReply({connection:'c1',reply:r.replies[1]}).status,'EXACT_REPLY_REPLAY');assert.equal(m.observation().result_records.length,1);
});
test('uncertain journal failure retains last known values without an active observation',()=>{
  const f=fresh(header),cs=[open(f),pump('io')];const script=`import fs from 'node:fs';import {runObserverWorker} from ${JSON.stringify(worker.href)};
    const old=fs.fsyncSync;let n=0;fs.fsyncSync=fd=>{if(++n===4)throw new Error('closed private fsync failure');return old(fd);};
    const r=await runObserverWorker({input:process.stdin,output:process.stdout,limits:${JSON.stringify(transport())}});process.exitCode=r.exit_code;`;
  const r=run(cs,['--input-type=module','-e',script]);assert.equal(r.exit,2);const m=model(f);apply(m,cs[0],r.replies[0]);apply(m,cs[1],r.replies[1]);
  assert.equal(m.observation().availability.state,'UNCERTAIN');assert.equal(m.observation().protocol_metadata,null);assert.equal(m.observation().source_values.counter,undefined);
});
test('actual EOF close IO error reuses last ID without replacing the successful receipt',()=>{
  const f=fresh(header),cs=[open(f),pump('p')],script=`import fs from 'node:fs';import {runObserverWorker} from ${JSON.stringify(worker.href)};
    fs.unlinkSync=()=>{throw new Error('closed EOF lock release injection');};
    const r=await runObserverWorker({input:process.stdin,output:process.stdout,limits:${JSON.stringify(transport())}});process.exitCode=r.exit_code;`;
  const r=run(cs,['--input-type=module','-e',script]);assert.equal(r.exit,2);assert.equal(r.replies.length,3);assert.equal(r.replies[2].id,'p');
  const m=model(f);apply(m,cs[0],r.replies[0]);apply(m,cs[1],r.replies[1]);const before=m.observation().source_values.header;
  assert.equal(m.applyReply({connection:'c1',reply:r.replies[2]}).status,'LATE_CLOSE_IO_UNCERTAINTY');assert.equal(m.observation().availability.state,'UNCERTAIN');
  assert.deepEqual(m.observation().source_values.header.record,before.record);assert.equal(m.observation().source_values.header.historical,true);
  assert.equal(m.applyReply({connection:'c1',reply:r.replies[1]}).status,'EXACT_REPLY_REPLAY');assert.equal(m.observation().availability.state,'UNCERTAIN');
  assert.equal(m.applyReply({connection:'c1',reply:r.replies[2]}).status,'EXACT_TERMINAL_REPLAY');assert.equal(m.observation().evidence.source_completion_accepted,false);
});
test('null and absent timing remain distinct; baseline zero is not an observed counter',()=>{
  for(const timing of ['null','absent']){
    const f=fresh(header),cs=[open(f),pump('p'),snap('s')];if(timing==='absent')cs[1].envelope={identity:context().identity};
    const r=run(cs),m=model(f);for(let i=0;i<cs.length;i++)apply(m,cs[i],r.replies[i]);const o=m.observation();
    assert.equal(o.protocol_metadata.counter,null);assert.equal(o.source_values.counter,undefined);assert.equal(o.protocol_metadata.baseline.gaps,'0');
    const t=o.source_values.header.record.transport;if(timing==='null'){assert.equal(t.source_age_ns,null);assert.equal(t.age_at_receipt_ns,'0000');}
    else assert.equal(Object.hasOwn(t,'source_age_ns'),false);
  }
});
test('older/repeated snapshot cannot change original leading-zero age or opaque UTC',()=>{
  const m=fullModel(beforeIndex+1),t=m.observation().source_values.counter.record.transport;
  assert.equal(t.age_at_receipt_ns,'0000');assert.equal(t.source_age_ns,null);assert.equal(t.receipt_monotonic_ns,'9007199254740993');
  assert.equal(t.observed_utc,'original-harness-provenance-not-real-hardware');
});
test('fragmented clock metadata cannot silently acquire another namespace or malformed age',()=>{
  for(const mutate of [e=>{e.transport_fragments[0].transport.clock_namespace='other';},e=>{e.transport_fragments[0].transport.source_age_ns='not-a-number';}]){
    const m=fullModel(1),r=copy(full.replies[1]);m.expect({connection:'c1',command:fullCommands[1]});mutate(r.result.events[0]);
    unchanged(m,()=>m.applyReply({connection:'c1',reply:r}));
  }
});
test('reply order and unknown IDs refuse with unchanged state',()=>{
  const m=model(fullFile);m.expect({connection:'c1',command:fullCommands[0]});
  unchanged(m,()=>m.applyReply({connection:'c1',reply:full.replies[1]}),'UNKNOWN_ID');m.applyReply({connection:'c1',reply:full.replies[0]});
});
test('two outstanding requests require serial reply order',()=>{
  const m=fullModel(1);m.expect({connection:'c1',command:fullCommands[1]});m.expect({connection:'c1',command:fullCommands[2]});
  unchanged(m,()=>m.applyReply({connection:'c1',reply:full.replies[2]}),'REPLY_ORDER');
});
test('queued open/pump/snapshot commands do not imply availability before real replies',()=>{
  const f=fresh(header),cs=[open(f),pump('p'),snap('s'),command('close','close')],r=run(cs),m=model(f);
  for(const c of cs)m.expect({connection:'c1',command:c});assert.equal(m.observation().availability.state,'UNAVAILABLE');
  for(const reply of r.replies)m.applyReply({connection:'c1',reply});assert.equal(m.observation().protocol_metadata.state,'RUNNING');
  assert.equal(m.observation().evidence.source_completion_accepted,false);assert.equal(m.observation().availability.state,'HISTORICAL');
});
test('pure in-memory fork preserves original values but independent availability changes',()=>{
  const m=fullModel(beforeIndex+1),fork=m.fork(),before=JSON.stringify(m.observation());fork.disconnect({connection:'c1',reason:'PIPE_LOST'});
  assert.equal(JSON.stringify(m.observation()),before);assert.equal(fork.observation().availability.state,'UNAVAILABLE');
  assert.deepEqual(fork.observation().result_records,m.observation().result_records);
});
test('genuine supplied decimal zero age remains zero while absent timing stays absent',()=>{
  const f=fresh(header),cs=[open(f),pump('p'),snap('s')];cs[1].envelope.source_age_ns='0';cs[1].envelope.observed_utc=null;
  const r=run(cs),m=model(f);for(let i=0;i<cs.length;i++)apply(m,cs[i],r.replies[i]);const t=m.observation().source_values.header.record.transport;
  assert.equal(t.source_age_ns,'0');assert.equal(t.age_at_receipt_ns,'0000');assert.equal(t.observed_utc,null);
});
test('ID uniqueness scoped by connection, including colon-containing tuple keys',()=>{
  const f=fresh(header),m=model(f,budgets(),'a:b'),cs=[open(f,true,'c'),command('end','close')],r=run(cs);
  for(let i=0;i<cs.length;i++)apply(m,cs[i],r.replies[i],'a:b');m.beginConnection({id:'a',dependencies:DEPENDENCIES});
  const more=[open(f,false,'b:c'),command('end','close')],rr=run(more);for(let i=0;i<more.length;i++)apply(m,more[i],rr.replies[i],'a');
  assert.equal(Object.keys(m.observation().request_receipts).length,4);
});
test('duplicate expected ID and reused connection refuse transactionally',()=>{
  const m=fullModel(1);unchanged(m,()=>m.expect({connection:'c1',command:fullCommands[0]}),'DUPLICATE_ID');m.disconnect({connection:'c1',reason:'EOF'});
  unchanged(m,()=>m.beginConnection({id:'c1',dependencies:DEPENDENCIES}),'CONNECTION_REUSE');
});
for(const [cap,value,code] of [['max_results',1,'RESULT_BUDGET'],['max_records',1,'RECORD_BUDGET'],['max_state_bytes',32768,'BYTE_BUDGET']])
  test(cap+' refuses before publishing candidate state or losing records',()=>{
    const m=model(fullFile,{...budgets(),[cap]:value});let refused=false;
    for(let i=0;i<fullCommands.length;i++){
      try{m.expect({connection:'c1',command:fullCommands[i]});}catch(e){assert.equal(e.code,code);refused=true;break;}
      const before=JSON.stringify(m.observation());try{m.applyReply({connection:'c1',reply:full.replies[i]});}catch(e){assert.equal(e.code,code);assert.equal(JSON.stringify(m.observation()),before);refused=true;break;}
    }assert.equal(refused,true);
  });
test('reply byte cap refuses before all state mutation',()=>{
  const m=model(fullFile,{...budgets(),max_reply_bytes:4096});apply(m,fullCommands[0],full.replies[0]);m.expect({connection:'c1',command:fullCommands[1]});
  const r=copy(full.replies[1]);r.result.events[0].raw_line='x'.repeat(10000);unchanged(m,()=>m.applyReply({connection:'c1',reply:r}),'BYTE_BUDGET');
});
test('request and connection caps preserve previous complete state',()=>{
  const m=model(fullFile,{...budgets(),max_requests:1,max_connections:1});apply(m,fullCommands[0],full.replies[0]);
  unchanged(m,()=>m.expect({connection:'c1',command:fullCommands[1]}),'REQUEST_BUDGET');m.disconnect({connection:'c1',reason:'EOF'});
  unchanged(m,()=>m.beginConnection({id:'c2',dependencies:DEPENDENCIES}),'CONNECTION_BUDGET');
});
test('exported observations deeply freeze values and do not alias caller input',()=>{
  const c=config(fullFile),m=new ObservationModel(c);c.context.identity.owner='mutated';m.beginConnection({id:'c1',dependencies:DEPENDENCIES});
  const incoming=copy(full.replies[0]);apply(m,fullCommands[0],incoming);incoming.result.cursor='123';const o=m.observation();
  assert.equal(o.binding.context.identity.owner,context().identity.owner);assert.equal(Object.isFrozen(o),true);assert.equal(Object.isFrozen(o.binding.context.identity),true);
  assert.throws(()=>{o.binding.context.identity.owner='bad';},TypeError);assert.equal(m.observation().binding.context.identity.owner,context().identity.owner);
});
test('getters, cycles, functions, holes, symbols and hostile readback refuse before mutation',()=>{
  for(const hostile of ['getter','cycle','function','hole','symbol']){
    const m=fullModel(1),r=copy(full.replies[1]);m.expect({connection:'c1',command:fullCommands[1]});let getterRan=false;
    if(hostile==='getter')Object.defineProperty(r,'bad',{enumerable:true,get(){getterRan=true;return 1;}});
    else if(hostile==='cycle')r.bad=r;else if(hostile==='function')r.bad=()=>{};else if(hostile==='hole')r.bad=Array(3);else r[Symbol('bad')]=1;
    unchanged(m,()=>m.applyReply({connection:'c1',reply:r}));assert.equal(getterRan,false);
  }
});
test('reduced/legacy dependency versions refuse before any model creation',()=>{
  for(const mutate of [c=>{delete c.dependencies.worker;},c=>{c.dependencies.protocol='0'.repeat(64);},c=>{c.context.capabilities.schema='legacy';},c=>{delete c.context.capabilities.whole_job_gpu_owned;}]){
    const c=config(fullFile);mutate(c);refusal(()=>new ObservationModel(c));
  }
});
test('unsupported capability extension cannot smuggle a wallet/controller observation',()=>{
  const c=config(fullFile);c.context.capabilities.wallet_connected=true;c.observer_binding.context.capabilities.wallet_connected=true;
  refusal(()=>new ObservationModel(c),'UNSUPPORTED_METADATA');
});
test('reduced snapshot/result-export extensions are refused instead of pretending supported',()=>{
  for(const mutate of [r=>{delete r.result.protocol.result_index;},r=>{r.schema='pgn-observer-worker-0';},r=>{r.result.protocol.result_index.entries=[];}]){
    const m=fullModel(beforeIndex),r=copy(full.replies[beforeIndex]);m.expect({connection:'c1',command:fullCommands[beforeIndex]});mutate(r);
    unchanged(m,()=>m.applyReply({connection:'c1',reply:r}));
  }
});
test('uncorrelated actual terminal worker reply makes pending work unknown, never acknowledged',()=>{
  const malformed=spawnSync(process.execPath,[workerFile,'--limits-json='+JSON.stringify(transport())],{input:'{bad}\n',timeout:10000,maxBuffer:8192});
  assert.equal(malformed.status,2);const r=JSON.parse(malformed.stdout.toString()),m=fullModel(1);m.expect({connection:'c1',command:fullCommands[1]});
  assert.equal(m.applyReply({connection:'c1',reply:r}).status,'UNCORRELATED_TERMINAL');assert.equal(m.observation().availability.state,'UNAVAILABLE');
  assert.equal(Object.values(m.observation().request_receipts).find(x=>x.id===fullCommands[1].id).status,'UNKNOWN_NOT_ACKNOWLEDGED');
});
test('all frozen incumbent source/test/head/fixture pins are unchanged',()=>{
  for(const p of pins)assert.equal(sha(bounded(p.file)),p.hash,String(p.file));
});
