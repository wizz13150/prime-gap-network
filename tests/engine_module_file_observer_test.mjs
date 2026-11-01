import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import { fileURLToPath } from 'node:url';
import { execFileSync, spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { SourceFileObserver, ObserverRefusal } from '../tools/engine-module-file-observer.mjs';
import { SourceJournal } from '../tools/engine-module-journal.mjs';
const sha=b=>createHash('sha256').update(b).digest('hex');
function bounded(file,max=65536){const s=fs.statSync(file);assert.ok(s.isFile()&&s.size<=max);const b=fs.readFileSync(file);assert.equal(b.length,s.size);return b;}
const fixture=new URL('../evidence/continuous-native-v1-golden-stdout-20261012.txt',import.meta.url);
const golden=bounded(fixture);assert.equal(sha(golden),'e860081459982dabe55fc0b3e633f7941793903b7e1a5480faeb074ebafc0e8b');
const lines=golden.toString().trimEnd().split(/\r?\n/),header=Buffer.from(lines[0]+'\r\n'),result=Buffer.from(lines[1]+'\r\n');
const batchEnd=golden.indexOf(Buffer.from('[pgn-counters]'));
const batch=golden.subarray(0,golden.indexOf(10,batchEnd)+1);
const caps={schema:'pgn-engine-module-1',id:'cuda-continuous-source-v1',protocol:'phase0-source-1',
  host_refill:false,host_verification:false,whole_job_gpu_owned:true,cuda_arch:'sm86',
  prime_certificate:'MR12 below318665857834031151167461',coverage:'SOURCE_PRP_INDEPENDENT_PROOF_PENDING',
  single_low64_window:true,supported_high_word:10};
const context=()=>({identity:{job_id:'closed-observer-job',device_id:'externally-bound-fixture-device',run_id:'paid-original-run',
  module_id:caps.id,owner:'closed-test-owner',clock_namespace:'paid-original-qpc',
  artifact_sha256:'ADD6C947E115C477F0DB2DD1C605AA61AECCE838CCECC1D2C2A9FBF65E88C158'},
  job:{start:'199999990000000000000',length:'1030792151040'},capabilities:caps,evidence_scope:'FINITE_TEST'});
const budgets=()=>({max_records:10000,max_bytes:1048576,max_record_bytes:65536,max_results:1024,max_index_bytes:24576,
  max_fragment_envelopes:64,max_generations:8,max_generation_bytes:131072});
const envelope=()=>({identity:context().identity,observed_utc:'2026-10-12T21:10:00.1234567Z',
  receipt_monotonic_ns:'9007199254740993',age_at_receipt_ns:'0000',source_age_ns:null});
const termination=()=>({identity:context().identity,exit_code:0,evidence_ref:'paid-fixture-termination-supplied-by-test-not-process-proof'});
const policy=()=>({encoding:'NATIVE_ASCII',max_source_bytes:65536,max_chunk_bytes:4096});
const provenance=()=>({evidence_ref:'closed-copy-of-paid-small-fixture-no-hardware-connection',
  trust_scope:'COOPERATING_APPEND_ONLY_LOCAL_FILE',source_kind:'CLOSED_FIXTURE'});
function config(source_file,journal_file,create=false){return {source_file,journal_file,context:context(),budgets:budgets(),policy:policy(),provenance:provenance(),create};}
function drain(o,e=envelope()){let n=0;while(!o.pump(e).at_eof)assert.ok(++n<1000);return o.snapshot();}
const self=fileURLToPath(import.meta.url);
function child(action,f){const result=spawnSync(process.execPath,[self,'--observer-child',action,f.source_file,f.journal_file],
  {encoding:'utf8',maxBuffer:8192,timeout:15000});assert.equal(result.error,undefined);return result;}
if(process.argv[2]==='--observer-child'){
  const [action,s,j]=process.argv.slice(3);const c=config(s,j);
  if(action==='produce-first'){fs.writeFileSync(s,header.subarray(0,13));fs.appendFileSync(s,header.subarray(13));}
  else if(action==='produce-lead'){fs.writeFileSync(s,header.subarray(0,23));}
  else if(action==='produce-complete'){fs.appendFileSync(s,header.subarray(23));}
  else if(action==='produce-rest'){const tail=golden.subarray(header.length);fs.appendFileSync(s,tail.subarray(0,17));fs.appendFileSync(s,tail.subarray(17));}
  else if(action==='observe-first'){c.create=true;const o=SourceFileObserver.open(c);const snap=drain(o);o.close();process.stdout.write(JSON.stringify({cursor:snap.observer.cursor,state:snap.protocol.state}));}
  else if(action==='observe-rest'){const o=SourceFileObserver.open(c);drain(o);o.finish(termination());const snap=o.snapshot();o.close();process.stdout.write(JSON.stringify({cursor:snap.observer.cursor,state:snap.protocol.state,
    count:snap.protocol.retained_result_identity_count,counter:snap.protocol.counter.fields,evidence:snap.protocol.evidence}));}
  else if(action==='cut-close'){
    c.create=true;c.test_fault={at:'after_commit_before_cursor'};const o=SourceFileObserver.open(c);
    try{o.pump(envelope());assert.fail('Success escaped cut');}catch(e){assert.equal(e.code,'TEST_COMMITTED_CURSOR_CUT');}
    // Cooperative close with lost in-memory cursor, NOT an abrupt crash.
    o.close();process.stdout.write('CUT_CLOSED');
  }else if(action==='cut-death'){
    c.create=true;c.test_fault={at:'after_commit_before_cursor'};const o=SourceFileObserver.open(c);
    try{o.pump(envelope());}catch(e){assert.equal(e.code,'TEST_COMMITTED_CURSOR_CUT');process.exit(23);}
    process.exit(24);
  }else if(action==='reopen-eof'){const o=SourceFileObserver.open(c);const snap=o.snapshot();const p=o.pump(envelope());o.close();process.stdout.write(JSON.stringify({state:snap.protocol.state,commit:p.commit,eof:p.at_eof,cursor:p.cursor}));}
  else if(action==='locked'){try{SourceFileObserver.open(c);process.exit(30);}catch(e){process.stdout.write(e.code);}}
  else throw new Error('Unsupported child');
  process.exit(0);
}
const root=fs.mkdtempSync(path.join(os.tmpdir(),'pgn-engine-module-file-observer-20261013-'));
let serial=0;
function fresh(bytes=header){const dir=path.join(root,String(++serial));fs.mkdirSync(dir);const f={source_file:path.join(dir,'native.stdout'),journal_file:path.join(dir,'private.wal')};fs.writeFileSync(f.source_file,bytes,{flag:'wx'});return f;}
function open(f,create=false,changes={}){return SourceFileObserver.open({...config(f.source_file,f.journal_file,create),...changes});}
function refusal(fn,code){assert.throws(fn,e=>!code||e.code===code);}
function closeAfter(f,bytes=header){const o=open(f,true);drain(o);o.close();return f;}
function saved(f){return bounded(f.journal_file,1048576);}
function operations(f){return saved(f).toString().trimEnd().split('\n').map(JSON.parse).slice(1).map(r=>r.payload.operation);}
const pinPaths=['../tools/engine-module-journal.mjs','../tools/engine-module-protocol.mjs','../tools/engine-module-contract.mjs',
  '../tests/engine_module_journal_test.mjs','../evidence/engine-module-journal-head-20261013.json','../evidence/engine-module-journal-tests-20261013.json',
  '../src/engine/cuda/euler96_prefix_seed.cuh','../tests/euler96_prefix_seed_vectors.cpp','../evidence/euler96-prefix-seed-head-20261013.json',
  '../evidence/euler96-prefix-seed-tests-20261013.json','../evidence/continuous-native-v1-golden-stdout-20261012.txt',
  '../evidence/continuous-native-v1-stopped-stdout-20261012.txt','../evidence/continuous-native-v1-resumed-stdout-20261012.txt'];
const pins=pinPaths.map(p=>({file:new URL(p,import.meta.url),hash:sha(bounded(new URL(p,import.meta.url)))}));
test('real producer and observer processes restart, 175 exact retained results and original counters',()=>{
  const f=fresh(Buffer.alloc(0));assert.equal(child('produce-first',f).status,0);
  const first=child('observe-first',f);assert.equal(first.status,0);assert.equal(JSON.parse(first.stdout).state,'RUNNING');
  assert.equal(child('produce-rest',f).status,0);const rest=child('observe-rest',f);assert.equal(rest.status,0,rest.stderr);
  const snap=JSON.parse(rest.stdout);assert.equal(snap.count,'175');assert.equal(snap.state,'COMPLETION_ACCEPTED');
  for(const [k,v] of Object.entries({positions:'1030792151040',tests:'3437901640',gaps:'175',batches:'17'}))assert.equal(snap.counter[k],v);
  assert.equal(snap.cursor,String(golden.length));assert.equal(snap.evidence.independent_coverage,null);assert.equal(snap.evidence.network_credit,null);
  assert.ok(Buffer.concat(operations(f).filter(x=>x.kind==='push').map(x=>Buffer.from(x.text))).equals(golden));
});
test('fresh-process EOF at producer header never finishes source',()=>{
  const f=closeAfter(fresh());const c=child('reopen-eof',f);assert.equal(c.status,0,c.stderr);const s=JSON.parse(c.stdout);
  assert.equal(s.state,'RUNNING');assert.equal(s.commit,null);assert.equal(s.eof,true);
});
test('complete fixture footer and EOF still require explicit termination',()=>{
  const f=fresh(golden),o=open(f,true);const s=drain(o);assert.equal(s.protocol.state,'END_REPORTED');
  assert.equal(s.protocol.evidence.source_completion_accepted,false);o.finish(termination());assert.equal(o.snapshot().protocol.state,'COMPLETION_ACCEPTED');o.close();
});
test('commit-before-cursor cut with cooperative close derives cursor in fresh process',()=>{
  const f=fresh();const c=child('cut-close',f);assert.equal(c.status,0,c.stderr);assert.equal(c.stdout,'CUT_CLOSED');
  const r=child('reopen-eof',f);assert.equal(r.status,0,r.stderr);assert.equal(JSON.parse(r.stdout).cursor,String(header.length));
  assert.equal(operations(f).length,1);assert.equal(fs.existsSync(f.journal_file+'.cursor'),false);
});
test('abrupt death after durable push leaves lock, refuses automatic restart without stealing',()=>{
  const f=fresh();const c=child('cut-death',f);assert.equal(c.status,23,c.stderr);const before=saved(f);
  assert.equal(SourceJournal.audit({file:f.journal_file,context:context(),budgets:budgets()}).protocol.state,'RUNNING');
  refusal(()=>open(f),'WRITER_LOCKED');assert.ok(saved(f).equals(before));assert.ok(fs.existsSync(f.journal_file+'.lock'));
});
test('split source line retains exact fragment envelopes across restart',()=>{
  const f=fresh(Buffer.alloc(0));assert.equal(child('produce-lead',f).status,0);const o=open(f,true);const a=envelope();a.source_age_ns='0007';a.age_at_receipt_ns=null;
  o.pump(a);o.close();assert.equal(child('produce-complete',f).status,0);const p=open(f);const b=envelope();delete b.source_age_ns;delete b.age_at_receipt_ns;
  p.pump(b);const s=p.snapshot();assert.equal(s.protocol.header.transport_fragments.length,2);
  assert.equal(s.protocol.header.transport_fragments[0].transport.source_age_ns,'0007');assert.equal(s.protocol.header.transport_fragments[0].transport.age_at_receipt_ns,null);
  assert.equal(Object.hasOwn(s.protocol.header.transport_fragments[1].transport,'source_age_ns'),false);p.close();
});
test('genuine source counter time, zero age, absent and null remain exact after restart',()=>{
  const f=fresh(batch),o=open(f,true);drain(o);const a=o.snapshot();o.close();const p=open(f),b=p.snapshot();
  assert.deepEqual(a.protocol.counter,b.protocol.counter);assert.equal(b.protocol.counter.transport.age_at_receipt_ns,'0000');
  assert.equal(b.protocol.counter.transport.source_age_ns,null);assert.equal(b.protocol.counter.transport.receipt_monotonic_ns,'9007199254740993');
  assert.equal(b.protocol.counter.fields.time_ns,lines.find(x=>x.startsWith('[pgn-counters]')).match(/time_ns=(\d+)/)[1]);p.close();
});
test('unavailable timing is not invented',()=>{
  const f=fresh(batch),o=open(f,true);drain(o,{identity:context().identity});const s=o.snapshot();assert.deepEqual(s.protocol.last_transport,{});
  assert.equal(Object.hasOwn(s.protocol.counter.transport,'source_age_ns'),false);o.close();
});
test('exact result duplicate after child restart is rejected by incumbent parser',()=>{
  const f=closeAfter(fresh(batch));fs.appendFileSync(f.source_file,result);const o=open(f);const before=o.snapshot().protocol.retained_result_identity_count;
  refusal(()=>o.pump(envelope()),'DUPLICATE_RESULT');o.close();assert.equal(before,'8');refusal(()=>open(f),'REFUSED_JOURNAL');
});
test('source rewind cannot replay old result as new',()=>{
  const f=closeAfter(fresh(batch));fs.writeFileSync(f.source_file,result);const before=saved(f);refusal(()=>open(f),'SOURCE_TRUNCATED');assert.ok(saved(f).equals(before));
});
for(const [name,bytes] of [['UTF8 split lead',Buffer.from([0xc3])],['UTF8 continuation',Buffer.from([0xa9])],['malformed UTF8',Buffer.from([0xff])],
  ['UTF16 BOM',Buffer.from([0xff,0xfe])],['NUL binary',Buffer.from([0])],['ASCII control',Buffer.from([1])]])
  test(name+' refuses without cursor acknowledgement or journal push',()=>{
    const f=fresh(bytes),o=open(f,true),before=saved(f);refusal(()=>o.pump(envelope()),'SOURCE_ENCODING');assert.ok(saved(f).equals(before));o.close();
  });
test('native CRLF TAB diagnostic bytes preserved rather than sanitized',()=>{
  const bytes=Buffer.concat([Buffer.from('diagnostic\tunchanged\r\n'),header]),f=fresh(bytes),o=open(f,true);drain(o);o.close();
  assert.ok(Buffer.concat(operations(f).map(x=>Buffer.from(x.text))).equals(bytes));
});
test('source path replacement refuses before tail consumption',()=>{
  const f=closeAfter(fresh());fs.renameSync(f.source_file,f.source_file+'.old');fs.writeFileSync(f.source_file,header);
  const before=saved(f);refusal(()=>open(f),'BINDING_MISMATCH');assert.ok(saved(f).equals(before));
});
test('in-process path replacement refuses',()=>{
  const f=fresh(),o=open(f,true);o.pump(envelope());fs.renameSync(f.source_file,f.source_file+'.old');fs.writeFileSync(f.source_file,header);
  refusal(()=>o.pump(envelope()),'FILE_IDENTITY');o.close();
});
test('truncated source refuses recovery',()=>{const f=closeAfter(fresh());fs.truncateSync(f.source_file,0);refusal(()=>open(f),'SOURCE_TRUNCATED');});
test('truncation immediately after source read refuses before durable push',()=>{
  const f=fresh(),o=open(f,true),before=saved(f),original=fs.readSync;
  try{fs.readSync=(fd,b,offset,length,position)=>{const n=original(fd,b,offset,length,position);
    if(length===header.length&&position===0)fs.truncateSync(f.source_file,0);return n;};
    refusal(()=>o.pump(envelope()),'SOURCE_TRUNCATED');}finally{fs.readSync=original;}
  assert.ok(saved(f).equals(before));o.close();
});
test('same-size overwritten source prefix refuses recovery',()=>{
  const f=closeAfter(fresh());const b=Buffer.from(header);b[3]^=1;fs.writeFileSync(f.source_file,b);refusal(()=>open(f),'SOURCE_PREFIX_CHANGED');
});
test('overwrite followed by growth refuses during pump before new journal push',()=>{
  const f=fresh(),o=open(f,true);o.pump(envelope());const before=saved(f),b=Buffer.from(header);b[3]^=1;fs.writeFileSync(f.source_file,Buffer.concat([b,result]));
  refusal(()=>o.pump(envelope()),'SOURCE_PREFIX_CHANGED');assert.ok(saved(f).equals(before));o.close();
});
for(const [name,mutate] of [['context',c=>{c.context.identity.owner='another';}],['source path',c=>{c.source_file+='x';}],
  ['budgets',c=>{c.budgets.max_results++;}],['policy',c=>{c.policy.max_chunk_bytes++;}],['provenance',c=>{c.provenance.evidence_ref='other';}]])
  test(name+' exact binding mismatch refuses',()=>{
    const f=closeAfter(fresh()),c=config(f.source_file,f.journal_file);mutate(c);
    if(name==='source path')fs.copyFileSync(f.source_file,c.source_file);refusal(()=>SourceFileObserver.open(c),'BINDING_MISMATCH');
  });
test('unrelated preexisting journal without observer binding is never adopted',()=>{
  const f=fresh(),j=SourceJournal.open({file:f.journal_file,context:context(),budgets:budgets(),create:true});j.close();const b=saved(f);
  refusal(()=>open(f),'INCOMPLETE_BINDING');refusal(()=>open(f,true),'ARTIFACT_EXISTS');assert.ok(saved(f).equals(b));
});
test('binding-only partial creation is preserved and refused',()=>{
  const f=fresh();fs.writeFileSync(f.journal_file+'.observer-binding.json','{"partial":');
  refusal(()=>open(f),'INCOMPLETE_BINDING');refusal(()=>open(f,true),'ARTIFACT_EXISTS');assert.equal(fs.readFileSync(f.journal_file+'.observer-binding.json','utf8'),'{"partial":');
});
test('changed immutable binding is refused',()=>{
  const f=closeAfter(fresh());fs.appendFileSync(f.journal_file+'.observer-binding.json',' ');refusal(()=>open(f),'BINDING_MISMATCH');
});
test('independent process writer exclusion preserves lock and journal',()=>{
  const f=fresh(),o=open(f,true),before=saved(f);const c=child('locked',f);assert.equal(c.status,0,c.stderr);assert.equal(c.stdout,'WRITER_LOCKED');
  assert.ok(saved(f).equals(before));o.close();const p=open(f);p.close();
});
for(const damage of ['truncated','corrupt','reordered','duplicate'])test('actual journal refuses '+damage+' WAL, no repair',()=>{
  const f=closeAfter(fresh(batch)),b=saved(f);let changed;
  if(damage==='truncated')changed=b.subarray(0,b.length-1);
  else if(damage==='corrupt'){changed=Buffer.from(b);changed[changed.indexOf(Buffer.from('paid-original-run'))]^=1;}
  else {const rows=b.toString().trimEnd().split('\n');changed=Buffer.from((damage==='duplicate'?[...rows,rows[1]]:[rows[1],rows[0],...rows.slice(2)]).join('\n')+'\n');}
  fs.writeFileSync(f.journal_file,changed);refusal(()=>open(f));assert.ok(saved(f).equals(changed));
});
test('source byte budget refused before creation and after growth',()=>{
  const f=fresh(),p=policy();p.max_source_bytes=header.length-1;p.max_chunk_bytes=1;
  refusal(()=>open(f,true,{policy:p}),'SOURCE_BYTE_BUDGET');assert.equal(fs.existsSync(f.journal_file),false);
  p.max_source_bytes=header.length;p.max_chunk_bytes=header.length;const o=open(f,true,{policy:p});o.pump(envelope());fs.appendFileSync(f.source_file,result);
  refusal(()=>o.pump(envelope()),'SOURCE_BYTE_BUDGET');o.close();
});
test('journal record admission refuses before source semantics or bytes are lost',()=>{
  const f=fresh(),b=budgets();b.max_records=1;const o=open(f,true,{budgets:b}),before=saved(f);
  refusal(()=>o.pump(envelope()),'JOURNAL_RECORD_BUDGET');assert.ok(saved(f).equals(before));o.close();
  const p=open(f,false,{budgets:b});assert.equal(p.snapshot().observer.cursor,'0');assert.equal(p.snapshot().protocol.counter,null);p.close();
});
test('failed append fsync escapes no success and keeps uncertain committed-looking bytes/lock',()=>{
  const f=fresh(),o=open(f,true),original=fs.fsyncSync;let calls=0;
  try{fs.fsyncSync=()=>{calls++;throw new Error('closed private fsync failure');};refusal(()=>o.pump(envelope()),'JOURNAL_IO_UNKNOWN');}
  finally{fs.fsyncSync=original;}
  assert.equal(calls,1);assert.equal(o.close().lock_retained,true);refusal(()=>open(f),'WRITER_LOCKED');
  const audit=SourceJournal.audit({file:f.journal_file,context:context(),budgets:budgets()});assert.equal(audit.protocol.state,'RUNNING');
  assert.equal(audit.recovery,'COMPLETE_READABLE_CHAIN_PRIOR_ACK_AND_PAST_FSYNC_UNKNOWN');
});
test('injected partial WAL append has no success, corruption survives and lock remains',()=>{
  const f=fresh(),o=open(f,true),original=fs.writeSync;let calls=0;
  try{fs.writeSync=(fd,b,off,len,pos)=>{if(++calls===1)return original(fd,b,off,Math.min(11,len),pos);throw new Error('closed private partial write failure');};
    refusal(()=>o.pump(envelope()),'JOURNAL_IO_UNKNOWN');}finally{fs.writeSync=original;}
  assert.equal(o.close().lock_retained,true);const before=saved(f);refusal(()=>open(f),'WRITER_LOCKED');
  refusal(()=>SourceJournal.audit({file:f.journal_file,context:context(),budgets:budgets()}));assert.ok(saved(f).equals(before));
});
test('termination refuses undrained bytes and wrong identity',()=>{
  const f=fresh(golden),o=open(f,true);refusal(()=>o.finish(termination()),'SOURCE_NOT_DRAINED');o.close();
  const p=open(f);drain(p);const t=termination();t.identity.run_id='other';refusal(()=>p.finish(t),'IDENTITY_MISMATCH');p.close();
});
test('termination and immutable completed source survive restart; later growth refuses',()=>{
  const f=fresh(golden),o=open(f,true);drain(o);o.finish(termination());o.close();const p=open(f);assert.equal(p.snapshot().observer.finished,true);
  refusal(()=>p.pump(envelope()),'OBSERVER_TERMINATED');p.close();fs.appendFileSync(f.source_file,Buffer.from('later\n'));refusal(()=>open(f),'SOURCE_AFTER_TERMINATION');
});
test('native generation resume is explicitly unsupported and cannot clear old index',()=>{
  const f=fresh(batch),o=open(f,true);drain(o);const before=saved(f),count=o.snapshot().protocol.retained_result_identity_count;
  refusal(()=>o.resume(),'UNSUPPORTED_GENERATION');assert.ok(saved(f).equals(before));assert.equal(o.snapshot().protocol.retained_result_identity_count,count);o.close();
});
test('valid public native generation in WAL is refused, not remapped to this source file',()=>{
  const stopped=bounded(new URL('../evidence/continuous-native-v1-stopped-stdout-20261012.txt',import.meta.url));
  assert.equal(sha(stopped),'6029d3db6176701421406220b631863c2740f4a0914690b2a330a5db8e3f4ef2');
  const f=fresh(stopped),o=open(f,true);drain(o);o.finish(termination());const s=o.snapshot().protocol;o.close();
  const c=context(),n=structuredClone(c);n.identity.run_id='next-external-run';
  const values=[BigInt(c.job.start)&((1n<<64n)-1n),BigInt(c.job.start)>>64n,BigInt(c.job.length),
    ...['positions','tests','gaps','batches'].map(k=>BigInt(s.counter.fields[k]))];
  const checkpoint=Buffer.alloc(64);checkpoint.write('PGNCG001');values.forEach((v,i)=>checkpoint.writeBigUInt64LE(v,8+i*8));
  const j=SourceJournal.open({file:f.journal_file,context:c,budgets:budgets()});
  j.resume(n,checkpoint,{abi:'windows-x64-le-64',provenance:{predecessor_identity:c.identity,next_identity:n.identity,
    checkpoint_evidence_ref:'closed-valid-v2-transition-not-file-mapping',checkpoint_path_reported:s.stopped.fields.checkpoint,
    clock_policy:'CONTINUE_NAMESPACE',observed_utc:null,receipt_monotonic_ns:'9007199254740995',age_at_receipt_ns:null,source_age_ns:null}});
  const retained=j.snapshot().protocol.retained_result_identity_count;j.close();const before=saved(f);
  refusal(()=>open(f),'UNSUPPORTED_GENERATION');assert.ok(saved(f).equals(before));
  assert.equal(SourceJournal.audit({file:f.journal_file,context:c,budgets:budgets()}).protocol.retained_result_identity_count,retained);
});
test('wrong envelope identity and unsupported fields leave journal inputs unchanged',()=>{
  for(const bad of [e=>{e.identity.run_id='wrong';},e=>{e.extra='untrusted';}]){
    const f=fresh(),o=open(f,true),before=saved(f),e=envelope();bad(e);refusal(()=>o.pump(e));assert.ok(saved(f).equals(before));o.close();
    const p=open(f);assert.equal(p.snapshot().observer.cursor,'0');p.close();
  }
});
test('absent observed counter stays null, separately declared fresh baseline stays zero',()=>{
  const f=fresh(),o=open(f,true);assert.equal(o.snapshot().protocol.counter,null);o.pump(envelope());
  assert.equal(o.snapshot().protocol.counter,null);assert.equal(o.snapshot().protocol.baseline.gaps,'0');
  assert.equal(o.snapshot().protocol.baseline_origin,'FRESH_SOURCE_INITIALIZATION_ASSUMPTION');o.close();
});
test('no external generations or unsupported provenance metadata admitted at creation',()=>{
  const f=fresh(),p=provenance();p.extra='scope-promotion';refusal(()=>open(f,true,{provenance:p}),'UNSUPPORTED_METADATA');
  assert.equal(fs.existsSync(f.journal_file),false);
});
test('source hardlink is outside admitted identity domain and is never changed',()=>{
  const f=fresh();fs.linkSync(f.source_file,f.source_file+'.alias');const before=bounded(f.source_file);
  refusal(()=>open(f,true),'FILE_IDENTITY');assert.ok(bounded(f.source_file).equals(before));assert.equal(fs.existsSync(f.journal_file),false);
});
test('foreign WAL overwrite during open observer is refused by extraction integrity checks',()=>{
  const f=fresh(),o=open(f,true);o.pump(envelope());const b=saved(f);b[b.indexOf(Buffer.from('paid-original-run'))]^=1;fs.writeFileSync(f.journal_file,b);
  refusal(()=>o.pump(envelope()),'JOURNAL_CHANGED');o.close();assert.ok(saved(f).equals(b));
});
test('unsupported metadata and encoding policy are refused before artifact creation',()=>{
  const f=fresh();refusal(()=>open(f,true,{extra:true}),'UNSUPPORTED_METADATA');refusal(()=>open(f,true,{policy:{...policy(),encoding:'UTF8'}}),'SOURCE_ENCODING');
  const c=config(f.source_file,f.journal_file,true);Object.defineProperty(c.provenance,'hidden',{value:1});refusal(()=>SourceFileObserver.open(c),'UNSUPPORTED_METADATA');
  assert.equal(fs.existsSync(f.journal_file),false);
});
test('source/journal path collision never modifies source',()=>{
  const f=fresh(),before=bounded(f.source_file);refusal(()=>SourceFileObserver.open(config(f.source_file,f.source_file,true)),'PATH_COLLISION');assert.ok(bounded(f.source_file).equals(before));
});
test('failed binding fsync leaves uncertain binding and refuses creation/recovery',()=>{
  const f=fresh(),original=fs.fsyncSync;try{fs.fsyncSync=()=>{throw new Error('closed binding fsync failure');};refusal(()=>open(f,true),'BINDING_IO_UNKNOWN');}finally{fs.fsyncSync=original;}
  assert.equal(fs.existsSync(f.journal_file),false);assert.ok(fs.existsSync(f.journal_file+'.observer-binding.json'));
  refusal(()=>open(f),'INCOMPLETE_BINDING');refusal(()=>open(f,true),'ARTIFACT_EXISTS');
});
test('journal header fsync failure after durable binding preserves uncertain artifacts and lock',()=>{
  const f=fresh(),original=fs.fsyncSync;let calls=0;
  try{fs.fsyncSync=fd=>{if(++calls===3)throw new Error('closed header fsync failure');return original(fd);};
    refusal(()=>open(f,true),'JOURNAL_IO_UNKNOWN');}finally{fs.fsyncSync=original;}
  assert.equal(calls,3);assert.ok(fs.existsSync(f.journal_file+'.observer-binding.json'));assert.ok(fs.existsSync(f.journal_file+'.lock'));
  const before=saved(f);refusal(()=>open(f),'WRITER_LOCKED');assert.ok(saved(f).equals(before));
});
test('closed tests leave all incumbent files and immutable fixture hashes unchanged',()=>{
  for(const pin of pins)assert.equal(sha(bounded(pin.file)),pin.hash,String(pin.file));
});
