import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import { fileURLToPath } from 'node:url';
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { SourceJournal, JournalRefusal } from '../tools/engine-module-journal.mjs';
import { SourceProtocolSession, SourceProtocolRefusal } from '../tools/engine-module-protocol.mjs';
const sha=x=>createHash('sha256').update(x).digest('hex');
function bounded(relative,max=65536){
  const file=new URL(relative,import.meta.url),st=fs.statSync(file);assert.ok(st.isFile()&&st.size<=max);
  const bytes=fs.readFileSync(file);assert.equal(bytes.length,st.size);return bytes;
}
const source=bounded('../tools/continuous-gpu-runtime.cu');
const literal=source.toString().match(/std::puts\(("(?:\\.|[^"\\])*")\);return 0;/);assert.ok(literal);
const capabilities=JSON.parse(JSON.parse(literal[1]));
// These paid fixtures belong to the original artifact, not whichever CUDA
// candidate root is currently editing. A source hash change is not permission
// to reattribute old results to a new executable/source artifact.
const fixtureArtifact='ADD6C947E115C477F0DB2DD1C605AA61AECCE838CCECC1D2C2A9FBF65E88C158';
const stopped=bounded('../evidence/continuous-native-v1-stopped-stdout-20261012.txt').toString();
const resumed=bounded('../evidence/continuous-native-v1-resumed-stdout-20261012.txt').toString();
const golden=bounded('../evidence/continuous-native-v1-golden-stdout-20261012.txt').toString();
const lines=golden.trimEnd().split(/\r?\n/),counter=lines.findIndex(x=>x.startsWith('[pgn-counters]'));
const firstBatch=lines.slice(0,counter+1).join('\n')+'\n',header=lines[0]+'\n',result=lines[1]+'\n';
const ctx=()=>({identity:{job_id:'journal-finite-job',device_id:'external-fixture-device',run_id:'one-original-run',
  module_id:capabilities.id,owner:'closed-test-owner',clock_namespace:'original-qpc-fixture',artifact_sha256:fixtureArtifact},
  job:{start:'199999990000000000000',length:'1030792151040'},capabilities,evidence_scope:'FINITE_TEST'});
const budget=()=>({max_records:10000,max_bytes:1048576,max_record_bytes:65536,max_results:1024,
  max_index_bytes:24576,max_fragment_envelopes:64,max_generations:8,max_generation_bytes:131072});
const envelope=(c=ctx())=>({identity:{...c.identity},observed_utc:'2026-10-12T21:10:00.1234567Z',
  receipt_monotonic_ns:'9007199254740993',age_at_receipt_ns:'0',source_age_ns:null});
const termination=(c=ctx())=>({identity:{...c.identity},exit_code:0,evidence_ref:'already-paid-finite-fixture-only'});
function checkpointFor(observation){
  const start=BigInt(observation.context.job.start),counter=observation.counter.fields;
  const values=[start&((1n<<64n)-1n),start>>64n,BigInt(observation.context.job.length),
    ...['positions','tests','gaps','batches'].map(k=>BigInt(counter[k]))];
  const bytes=Buffer.alloc(64);bytes.write('PGNCG001',0,'ascii');values.forEach((v,i)=>bytes.writeBigUInt64LE(v,8+i*8));return bytes;
}
function nextContext(old=ctx(),run='second-original-run',clock=old.identity.clock_namespace){
  const next=structuredClone(old);next.identity.run_id=run;next.identity.clock_namespace=clock;return next;
}
function resumeOptions(observation,next,policy='CONTINUE_NAMESPACE'){
  return {abi:'windows-x64-le-64',provenance:{predecessor_identity:{...observation.context.identity},next_identity:{...next.identity},
    checkpoint_evidence_ref:'closed-native-layout-from-paid-stop-counter-not-an-executor-action',
    checkpoint_path_reported:observation.stopped.fields.checkpoint,clock_policy:policy,
    observed_utc:'2026-10-13T00:30:00.1234567Z',receipt_monotonic_ns:'9007199254740995',age_at_receipt_ns:'0000',source_age_ns:null}};
}
function stoppedJournal(b=budget(),fault=null){
  const file=fresh(),writer=open(file,{create:true,budgets:b,test_fault:fault});
  const old=envelope();old.age_at_receipt_ns='0007';old.source_age_ns='0';
  writer.push(stopped,old);writer.finish(termination());return {file,writer};
}
const root=fs.mkdtempSync(path.join(os.tmpdir(),'pgn-engine-module-journal-20261013-'));
let serial=0;const fresh=()=>path.join(root,String(++serial)+'.journal');
function open(file,options={}){return SourceJournal.open({file,context:ctx(),budgets:budget(),...options});}
function audit(file,options={}){return SourceJournal.audit({file,context:ctx(),budgets:budget(),...options});}
function refusal(fn,code){assert.throws(fn,e=>(e instanceof JournalRefusal||e instanceof SourceProtocolRefusal)&&(!code||e.code===code));}
function closed(text=firstBatch,b=budget()){
  const file=fresh(),writer=open(file,{create:true,budgets:b});writer.push(text,envelope());writer.close();return file;
}
function child(action,file){return JSON.parse(execFileSync(process.execPath,[fileURLToPath(import.meta.url),'--journal-child',action,file],
  {encoding:'utf8',maxBuffer:8192,timeout:15000}));}
function canon(x){return x!==null&&typeof x==='object'?(Array.isArray(x)?'['+x.map(canon).join(',')+']':
  '{'+Object.keys(x).sort().map(k=>JSON.stringify(k)+':'+canon(x[k])).join(',')+'}'):JSON.stringify(x);}
function records(file){return fs.readFileSync(file,'utf8').trimEnd().split('\n').map(JSON.parse);}
function fixture(bytes){const file=fresh();fs.writeFileSync(file,bytes,{flag:'wx'});return file;}
function rehash(record){record.hash=sha(canon({seq:record.seq,previous:record.previous,payload:record.payload}));return record;}
function encode(records){return records.map(x=>canon(x)+'\n').join('');}
const pins=new Map(['../tools/engine-module-protocol.mjs','../tools/engine-module-contract.mjs','engine_module_protocol_test.mjs',
  '../src/engine/cuda/euler68_fusedbit.cuh','euler68_fusedbit_test.cpp','../evidence/euler68-fusedbit-head-20261013.json',
  '../evidence/euler68-fusedbit-tests-20261013.json'].map(p=>[p,sha(bounded(p))]));
const archive='../evidence/engine-module-journal-v1-closed-20261013/';
for(const name of ['engine-module-journal.mjs','engine_module_journal_test.mjs','engine-module-journal-head-20261013.json',
  'engine-module-journal-tests-20261013.json','engine-module-journal-root-acceptance-20261013.json'])pins.set(archive+name,sha(bounded(archive+name)));

if(process.argv[2]==='--journal-child'){
  const action=process.argv[3],file=process.argv[4];let writer;
  if(action==='create-batch'||action==='crash-batch'){
    writer=open(file,{create:true});const ack=writer.push(firstBatch,envelope());
    const report={state:writer.snapshot().protocol.state,count:writer.snapshot().protocol.retained_result_identity_count,commit:ack.commit};
    if(action==='create-batch')writer.close();console.log(JSON.stringify(report));
  }else if(action==='restart-duplicate'){
    writer=open(file);const before=writer.snapshot().protocol;let code,commit;
    try{writer.push(result,envelope());}catch(e){code=e.code;commit=e.journal_commit;}
    const after=writer.snapshot().protocol;writer.close();
    const select=s=>({state:s.state,retained_result_identity_count:s.retained_result_identity_count});
    console.log(JSON.stringify({before:select(before),after:select(after),code,commit}));
  }else if(action==='try-open'){
    let code;try{writer=open(file);writer.close();}catch(e){code=e.code;}console.log(JSON.stringify({code}));
  }else if(action==='restart-tail'){
    writer=open(file);const before=writer.snapshot().protocol;
    writer.push(lines[1].slice(13)+'\n',envelope());const after=writer.snapshot().protocol;
    writer.close();console.log(JSON.stringify({before:{partial_tail:before.partial_tail,partial_transport_fragments:before.partial_transport_fragments},
      after:{partial_tail:after.partial_tail,retained_result_identity_count:after.retained_result_identity_count}}));
  }else if(action==='create-stop'){
    writer=open(file,{create:true});const old=envelope();old.age_at_receipt_ns='0007';old.source_age_ns='0';
    writer.push(stopped,old);writer.finish(termination());const s=writer.snapshot().protocol;writer.close();
    console.log(JSON.stringify({state:s.state,retained:s.retained_result_identity_count,counter:s.counter.fields}));
  }else if(action==='resume-complete'||action==='resume-duplicate'){
    writer=open(file);const old=writer.snapshot().protocol,next=nextContext(old.context);
    const ack=writer.resume(next,checkpointFor(old),resumeOptions(old,next));
    const admitted=writer.snapshot().protocol;
    if(action==='resume-complete'){
      writer.push(resumed,envelope(next));writer.finish(termination(next));
      const final=writer.snapshot(),protocol=final.protocol;writer.close();
      console.log(JSON.stringify({state:protocol.state,retained:protocol.retained_result_identity_count,counter:protocol.counter.fields,
        baseline:admitted.baseline,new_counter_before_source:admitted.counter,commit:ack.commit,
        generations:final.journal.generation_lineage.length,old_counter:final.journal.generation_lineage[1].predecessor.counter}));
    }else{
      writer.push(resumed.split(/\r?\n/)[0]+'\n',envelope(next));let error;
      try{writer.push(result,envelope(next));}catch(e){error=e;}
      const after=writer.snapshot().protocol;writer.close();console.log(JSON.stringify({code:error.code,commit:error.journal_commit,
        retained:after.retained_result_identity_count,baseline:after.baseline}));
    }
  }else if(action==='resume-third-complete'){
    writer=open(file);const old=writer.snapshot().protocol,next=nextContext(old.context,'third-original-run');
    writer.resume(next,checkpointFor(old),resumeOptions(old,next));
    const paid=resumed.trimEnd().split(/\r?\n/),first=paid.findIndex(x=>x.startsWith('[pgn-counters]'));
    writer.push(paid[0].replace('descriptors=16','descriptors=15')+'\n'+paid.slice(first+1).join('\n')+'\n',envelope(next));
    writer.finish(termination(next));const state=writer.snapshot();writer.close();
    console.log(JSON.stringify({state:state.protocol.state,retained:state.protocol.retained_result_identity_count,
      generations:state.journal.generation_lineage.length,counter:state.protocol.counter.fields}));
  }else throw new Error('Unsupported test child action');
}else{
test('closed evidence and parser pins are exact; no B12 replay or artifact reattribution',()=>{
  assert.deepEqual(capabilities,{schema:'pgn-engine-module-1',id:'cuda-continuous-source-v1',protocol:'phase0-source-1',
    host_refill:false,host_verification:false,whole_job_gpu_owned:true,cuda_arch:'sm86',
    prime_certificate:'MR12 below318665857834031151167461',coverage:'SOURCE_PRP_INDEPENDENT_PROOF_PENDING',
    single_low64_window:true,supported_high_word:10});
  assert.equal(sha(Buffer.from(stopped)).toUpperCase(),'6029D3DB6176701421406220B631863C2740F4A0914690B2A330A5DB8E3F4EF2');
  assert.equal(sha(Buffer.from(golden)).toUpperCase(),'E860081459982DABE55FC0B3E633F7941793903B7E1A5480FAEB074EBAFC0E8B');
  assert.equal(sha(Buffer.from(resumed)).toUpperCase(),'45484FBD27963E761C06B86D6A681F38683DCAF57F9B7BD8C38E07FE54640571');
  assert.equal(pins.get('../tools/engine-module-protocol.mjs').toUpperCase(),'5759E409C6B7C0A590870D7A0D1740D14D8518746AD244144EE257AA7543FB1B');
});
test('real child process restart reconstructs index and rejects exact duplicate after a batch',()=>{
  const file=fresh(),first=child('create-batch',file);assert.equal(first.count,'8');
  const report=child('restart-duplicate',file);assert.equal(report.before.retained_result_identity_count,'8');
  assert.equal(report.code,'DUPLICATE_RESULT');assert.ok(report.commit);assert.equal(report.after.state,'REFUSED');
  assert.equal(report.after.retained_result_identity_count,'8');assert.equal(audit(file).protocol.state,'REFUSED');
});
test('independent child writer excluded; orderly release permits reopening',()=>{
  const file=fresh(),writer=open(file,{create:true});assert.equal(child('try-open',file).code,'WRITER_LOCKED');
  writer.close();assert.equal(child('try-open',file).code,undefined);
});
test('real exited process lock is not stolen; read-only audit is explicitly uncertain',()=>{
  const file=fresh();child('crash-batch',file);const before=sha(fs.readFileSync(file));
  refusal(()=>open(file),'WRITER_LOCKED');const observation=audit(file);
  assert.equal(observation.writer_lock_present,true);assert.match(observation.recovery,/PRIOR_ACK_AND_PAST_FSYNC_UNKNOWN/);
  assert.equal(observation.protocol.retained_result_identity_count,'8');assert.equal(sha(fs.readFileSync(file)),before);
});
test('split fragment provenance and ages survive actual process restart',()=>{
  const file=fresh(),writer=open(file,{create:true}),first=envelope();first.age_at_receipt_ns='0007';
  first.source_age_ns='0';writer.push(header,first);writer.push(lines[1].slice(0,13),first);writer.close();
  const report=child('restart-tail',file);assert.equal(report.before.partial_tail,lines[1].slice(0,13));
  assert.equal(report.before.partial_transport_fragments[0].transport.age_at_receipt_ns,'0007');
  assert.equal(report.before.partial_transport_fragments[0].transport.source_age_ns,'0');
  assert.equal(report.after.partial_tail,'');assert.equal(report.after.retained_result_identity_count,'1');
  const record=records(file).at(-1);assert.equal(record.payload.operation.envelope.source_age_ns,null);
});
test('fragment identity mismatch refuses before touching bytes or prior tail',()=>{
  const file=fresh(),writer=open(file,{create:true});writer.push(header.slice(0,20),envelope());
  const before=writer.snapshot(),bytes=sha(fs.readFileSync(file)),wrong=envelope();wrong.identity.clock_namespace='other-clock';
  refusal(()=>writer.push(header.slice(20),wrong),'IDENTITY_MISMATCH');assert.deepEqual(writer.snapshot(),before);
  assert.equal(sha(fs.readFileSync(file)),bytes);writer.close();
});
test('golden termination replay equals public parser; producer declaration remains separate',()=>{
  const file=fresh(),writer=open(file,{create:true}),direct=new SourceProtocolSession(ctx(),{max_results:1024,max_index_bytes:24576,max_fragment_envelopes:64});
  for(let offset=0;offset<golden.length;offset+=1024){const text=golden.slice(offset,offset+1024);
    assert.deepEqual(writer.push(text,envelope()).events,direct.push(text,envelope()));}
  const expected=direct.finish(termination());assert.deepEqual(writer.finish(termination()).observation,expected);writer.close();
  const restored=open(file);assert.deepEqual(restored.snapshot().protocol,expected);
  assert.equal(expected.state,'COMPLETION_ACCEPTED');assert.equal(expected.counter.fields.gaps,'175');
  assert.equal(expected.evidence.independent_coverage,null);assert.equal(expected.evidence.network_credit,null);
  assert.equal(expected.result_index.persistence,'PROCESS_LOCAL_VOLATILE');restored.close();
});
test('counter source time and genuine zero/null/absent ages are not fabricated',()=>{
  const file=fresh(),writer=open(file,{create:true}),env=envelope();delete env.source_age_ns;
  writer.push(firstBatch,env);const expected=writer.snapshot().protocol;writer.close();
  const restored=audit(file).protocol;assert.deepEqual(restored,expected);
  assert.equal(restored.counter.fields.time_ns,lines[counter].match(/time_ns=([0-9]+)/)[1]);
  assert.equal(restored.counter.transport.age_at_receipt_ns,'0');
  assert.equal(restored.counter.transport.receipt_monotonic_ns,'9007199254740993');
  assert.equal(Object.hasOwn(restored.counter.transport,'source_age_ns'),false);
});
test('empty source state preserves unavailable counter/timing rather than manufacturing observations',()=>{
  const file=fresh(),writer=open(file,{create:true}),s=writer.snapshot().protocol;
  assert.equal(s.counter,null);assert.equal(s.last_transport,null);assert.equal(s.header,null);
  assert.equal(s.evidence.producer_declaration,null);assert.equal(s.baseline_origin,'FRESH_SOURCE_INITIALIZATION_ASSUMPTION');writer.close();
  assert.deepEqual(audit(file).protocol,s);
});
test('stopped termination replays exactly; unsupported resume metadata leaves old state unchanged',()=>{
  const file=fresh(),writer=open(file,{create:true});writer.push(stopped,envelope());writer.finish(termination());writer.close();
  const restored=open(file),before=restored.snapshot(),bytes=sha(fs.readFileSync(file));assert.equal(before.protocol.state,'STOPPED_ACCEPTED');
  refusal(()=>restored.resume(ctx(),Buffer.alloc(64),{abi:'windows-x64-le-64'}),'UNSUPPORTED_METADATA');
  assert.deepEqual(restored.snapshot(),before);assert.equal(sha(fs.readFileSync(file)),bytes);restored.close();
});
test('missing termination is not completion; partial source tail is retained and refused on finish',()=>{
  const file=fresh(),writer=open(file,{create:true});writer.push(header.slice(0,-1),envelope());writer.close();
  const restored=open(file);assert.equal(restored.snapshot().protocol.state,'AWAITING_HEADER');
  let error;try{restored.finish(termination());}catch(e){error=e;}assert.equal(error.code,'PARTIAL_TAIL');assert.ok(error.journal_commit);
  restored.close();assert.equal(audit(file).protocol.refusal.code,'PARTIAL_TAIL');
});
test('source diagnostics survive; unsupported module record is durably refused, not sanitized',()=>{
  const file=fresh(),writer=open(file,{create:true});const diagnostic='native diagnostic exact zero=0 age=N/A\r\n';
  assert.equal(writer.push(diagnostic,envelope()).events[0].raw_line,diagnostic.slice(0,-1));
  let error;try{writer.push('[phase0-gpu] invented=success\n',envelope());}catch(e){error=e;}
  assert.equal(error.code,'UNSUPPORTED_PROTOCOL_RECORD');assert.match(error.rejected_record.raw_line,/invented/);assert.ok(error.journal_commit);
  writer.close();const observed=audit(file);assert.equal(observed.protocol.diagnostics_seen,1);assert.equal(observed.protocol.refusal.code,error.code);
  assert.equal(records(file).at(-1).payload.operation.text,'[phase0-gpu] invented=success\n');
});
test('refused source attempt is terminal; accepted events cannot be appended after it',()=>{
  const file=fresh(),writer=open(file,{create:true});writer.push(firstBatch,envelope());
  refusal(()=>writer.push(result,envelope()),'DUPLICATE_RESULT');const before=sha(fs.readFileSync(file));
  refusal(()=>writer.push('diagnostic\n',envelope()),'JOURNAL_PROTOCOL_REFUSED');writer.close();
  const restored=open(file);assert.equal(restored.snapshot().protocol.state,'REFUSED');refusal(()=>restored.push(header,envelope()),'JOURNAL_PROTOCOL_REFUSED');
  restored.close();assert.equal(sha(fs.readFileSync(file)),before);
});
for(const field of ['job_id','device_id','run_id','module_id','owner','clock_namespace','artifact_sha256'])test('exact reopen identity bound: '+field,()=>{
  const file=closed(),wrong=ctx();wrong.identity[field]+='-mismatch';const before=sha(fs.readFileSync(file));
  refusal(()=>open(file,{context:wrong}),'BINDING_MISMATCH');assert.equal(sha(fs.readFileSync(file)),before);assert.equal(fs.existsSync(file+'.lock'),false);
});
test('job span, evidence scope, capability and immutable budget mismatch refuse',()=>{
  const file=closed();for(const change of [c=>c.job.length='1030792151041',c=>c.evidence_scope='SOURCE_TRANSCRIPT',c=>c.capabilities.coverage='OTHER']){
    const c=structuredClone(ctx());change(c);refusal(()=>open(file,{context:c}),'BINDING_MISMATCH');}
  refusal(()=>open(file,{budgets:{...budget(),max_results:1025}}),'BINDING_MISMATCH');
});
test('unsupported metadata and non-JSON values fail closed without journal mutation',()=>{
  const file=fresh(),writer=open(file,{create:true});const before=sha(fs.readFileSync(file));
  for(const value of [undefined,NaN,Infinity,1n,()=>{},new Date(),-0]){
    const env=envelope();env.observed_utc=value;refusal(()=>writer.push(header,env));}
  const unknown=envelope();unknown.unknown_extension={zero:0};refusal(()=>writer.push(header,unknown),'UNSUPPORTED_METADATA');
  const accessor=envelope();Object.defineProperty(accessor,'observed_utc',{enumerable:true,get(){throw new Error('Getter must not run');}});
  refusal(()=>writer.push(header,accessor),'UNSUPPORTED_METADATA');assert.equal(sha(fs.readFileSync(file)),before);writer.close();
  refusal(()=>SourceJournal.open({file:fresh(),context:ctx(),budgets:budget(),invented:true}),'UNSUPPORTED_METADATA');
  const options={file:fresh(),context:ctx(),budgets:budget()};
  Object.defineProperty(options,'create',{enumerable:true,get(){throw new Error('Open getter must not run');}});
  refusal(()=>SourceJournal.open(options),'UNSUPPORTED_METADATA');
});
test('explicit index budgets refused before creating journal; no persistent index fiction',()=>{
  const file=fresh();refusal(()=>open(file,{create:true,budgets:{...budget(),max_index_bytes:1}}),'INDEX_MEMORY_ADMISSION');
  assert.equal(fs.existsSync(file),false);assert.equal(fs.existsSync(file+'.lock'),false);
  refusal(()=>open(file,{create:true,budgets:{max_records:1}}),'UNSUPPORTED_METADATA');
});
test('record admission exhaustion leaves semantic duplicate evidence unchanged',()=>{
  const file=fresh(),b={...budget(),max_records:2},writer=open(file,{create:true,budgets:b});writer.push(firstBatch,envelope());
  const before=writer.snapshot();refusal(()=>writer.push(result,envelope()),'JOURNAL_RECORD_BUDGET');assert.deepEqual(writer.snapshot(),before);writer.close();
  assert.equal(audit(file,{budgets:b}).protocol.retained_result_identity_count,'8');
});
test('byte admission exhaustion occurs before semantic mutation; oversized append also refused',()=>{
  const file=fresh(),b={...budget(),max_bytes:5000,max_record_bytes:4096},writer=open(file,{create:true,budgets:b});
  let refused=false;for(let i=0;i<8;++i){const before=writer.snapshot();try{writer.push('d'.repeat(900)+'\n',envelope());}
    catch(e){assert.equal(e.code,'JOURNAL_BYTE_BUDGET');assert.deepEqual(writer.snapshot(),before);refused=true;break;}}
  assert.ok(refused);refusal(()=>writer.push('x'.repeat(4097),envelope()),'JOURNAL_BYTE_BUDGET');writer.close();
  assert.ok(Number(audit(file,{budgets:b}).bytes)<=b.max_bytes);
});
test('packed index exhaustion preserves offending source and retained identities in durable refusal',()=>{
  const file=fresh(),b={...budget(),max_results:8,max_index_bytes:192},writer=open(file,{create:true,budgets:b});writer.push(firstBatch,envelope());
  const next=lines.slice(counter+1).find(x=>x.startsWith('[phase0-gpu] GAP '));let error;
  try{writer.push(next+'\n',envelope());}catch(e){error=e;}
  assert.equal(error.code,'RESULT_CAPACITY');assert.equal(error.rejected_record.raw_line,next);assert.ok(error.journal_commit);writer.close();
  assert.equal(audit(file,{budgets:b}).protocol.retained_result_identity_count,'8');assert.equal(audit(file,{budgets:b}).protocol.refusal.code,'RESULT_CAPACITY');
});
test('normal append performs no corpus reads or replay; fsync happens before returned commit',()=>{
  const file=fresh(),writer=open(file,{create:true});let reads=0,syncs=0;
  const read=fs.readSync,sync=fs.fsyncSync;fs.readSync=(...args)=>{reads++;return read(...args);};
  fs.fsyncSync=(...args)=>{assert.throws(()=>writer.snapshot(),e=>e.code==='JOURNAL_BUSY');syncs++;return sync(...args);};
  try{for(let i=0;i<128;++i){const ack=writer.push('closed diagnostic '+i+'\n',envelope());assert.equal(syncs,i+1);assert.ok(ack.commit);}
    assert.equal(reads,0);}finally{fs.readSync=read;fs.fsyncSync=sync;writer.close();}
});
test('partial append I/O failure poisons writer, retains bytes/lock and never acknowledges',()=>{
  const file=fresh(),writer=open(file,{create:true,test_fault:{at:'append_write',bytes:19}}),before=fs.statSync(file).size;
  refusal(()=>writer.push(header,envelope()),'JOURNAL_IO_UNKNOWN');assert.equal(fs.statSync(file).size,before+19);
  refusal(()=>writer.snapshot(),'JOURNAL_POISONED');refusal(()=>writer.push(header,envelope()),'JOURNAL_POISONED');
  assert.equal(writer.close().lock_retained,true);const bytes=sha(fs.readFileSync(file));
  refusal(()=>open(file),'WRITER_LOCKED');refusal(()=>audit(file),'JOURNAL_TRUNCATED');assert.equal(sha(fs.readFileSync(file)),bytes);
});
test('failed fsync leaves complete readable frame with unknown prior commit, not invented success',()=>{
  const file=fresh(),writer=open(file,{create:true,test_fault:{at:'append_fsync'}});
  refusal(()=>writer.push(firstBatch,envelope()),'JOURNAL_IO_UNKNOWN');refusal(()=>writer.snapshot(),'JOURNAL_POISONED');writer.close();
  refusal(()=>open(file),'WRITER_LOCKED');const report=audit(file);assert.equal(report.protocol.retained_result_identity_count,'8');
  assert.equal(report.writer_lock_present,true);assert.equal(report.recovery,'COMPLETE_READABLE_CHAIN_PRIOR_ACK_AND_PAST_FSYNC_UNKNOWN');
});
test('create never replaces an old journal',()=>{
  const file=closed(),before=sha(fs.readFileSync(file));refusal(()=>open(file,{create:true}),'JOURNAL_EXISTS');
  assert.equal(sha(fs.readFileSync(file)),before);assert.equal(fs.existsSync(file+'.lock'),false);
});
test('real read syscall failure during reopening retains exclusive lock and unchanged evidence',()=>{
  const file=closed(),before=sha(fs.readFileSync(file)),read=fs.readSync;
  fs.readSync=()=>{const error=new Error('Injected closed replay read failure');error.code='EIO';throw error;};
  try{refusal(()=>open(file),'JOURNAL_IO_UNKNOWN');}finally{fs.readSync=read;}
  assert.equal(fs.existsSync(file+'.lock'),true);assert.equal(sha(fs.readFileSync(file)),before);
  refusal(()=>open(file),'WRITER_LOCKED');assert.equal(audit(file).protocol.retained_result_identity_count,'8');
});
test('failed append fsync syscall also poisons writer without exposing observation',()=>{
  const file=fresh(),writer=open(file,{create:true}),sync=fs.fsyncSync;
  fs.fsyncSync=()=>{const e=new Error('Injected closed syscall fsync failure');e.code='EIO';throw e;};
  try{refusal(()=>writer.push(firstBatch,envelope()),'JOURNAL_IO_UNKNOWN');}finally{fs.fsyncSync=sync;}
  refusal(()=>writer.snapshot(),'JOURNAL_POISONED');writer.close();assert.equal(audit(file).writer_lock_present,true);
});
test('lost lock ownership cannot cause another lock to be unlinked',()=>{
  const file=fresh(),writer=open(file,{create:true});
  fs.writeFileSync(file+'.lock',JSON.stringify({token:'different-closed-test-token'}));
  refusal(()=>writer.close(),'JOURNAL_IO_UNKNOWN');assert.equal(JSON.parse(fs.readFileSync(file+'.lock','utf8')).token,'different-closed-test-token');
  refusal(()=>open(file),'WRITER_LOCKED');
});
test('lost lock before append poisons writer without acknowledging or changing source bytes',()=>{
  const file=fresh(),writer=open(file,{create:true}),before=sha(fs.readFileSync(file));
  fs.unlinkSync(file+'.lock');refusal(()=>writer.push(header,envelope()),'JOURNAL_IO_UNKNOWN');
  refusal(()=>writer.snapshot(),'JOURNAL_POISONED');const closed=writer.close();
  assert.equal(closed.writer_poisoned,true);assert.equal(closed.lock_retained,false);
  assert.equal(sha(fs.readFileSync(file)),before);
});
test('truncated journal has no silent tail discard or destructive repair',()=>{
  const base=fs.readFileSync(closed());for(const cut of [1,19,Math.floor(base.length/2)]){
    const file=fixture(base.subarray(0,base.length-cut)),before=sha(fs.readFileSync(file));refusal(()=>audit(file),'JOURNAL_TRUNCATED');
    refusal(()=>open(file),'JOURNAL_TRUNCATED');assert.equal(sha(fs.readFileSync(file)),before);}
});
test('corrupt hash and malformed/duplicate-key/noncanonical/UTF8 records fail closed',()=>{
  const original=records(closed()),changed=structuredClone(original);changed[1].payload.operation.text+='corrupt';
  refusal(()=>audit(fixture(encode(changed))),'JOURNAL_CORRUPTION');
  for(const suffix of ['{bad}\n','{"seq":"0","seq":"1"}\n',' {}\n'])refusal(()=>audit(fixture(encode(original)+suffix)));
  refusal(()=>audit(fixture(Buffer.concat([Buffer.from(encode(original)),Buffer.from([0xff,10])]))),'JOURNAL_CORRUPTION');
});
test('duplicated and reordered exact frames refused by sequence/chain',()=>{
  const file=fresh(),writer=open(file,{create:true});writer.push(header,envelope());writer.push('diagnostic\n',envelope());writer.close();
  const rs=records(file);refusal(()=>audit(fixture(encode([rs[0],rs[1],rs[1]]))),'JOURNAL_ORDER');
  refusal(()=>audit(fixture(encode([rs[0],rs[2],rs[1]]))),'JOURNAL_ORDER');
});
test('oversized journal stat and frame refused with explicit bounds',()=>{
  refusal(()=>audit(fixture(Buffer.alloc(budget().max_bytes+1))),'JOURNAL_BYTE_BUDGET');
  refusal(()=>audit(fixture(encode(records(closed()))+'x'.repeat(budget().max_record_bytes)+'\n')),'JOURNAL_RECORD_BUDGET');
});
test('rehashed unsupported metadata and inconsistent parser outcomes cannot be accepted',()=>{
  const rs=records(closed()),changed=structuredClone(rs);changed[1].payload.operation.envelope.public_lease=true;rehash(changed[1]);
  refusal(()=>audit(fixture(encode(changed))),'UNSUPPORTED_METADATA');
  const other=structuredClone(rs);other[1].payload.outcome.state_sha256='0'.repeat(64);rehash(other[1]);
  refusal(()=>audit(fixture(encode(other))),'JOURNAL_OUTCOME');
});
test('unsupported generation operation and parser pin mismatch refuse replay',()=>{
  const rs=records(closed()),changed=structuredClone(rs);changed[1].payload.operation={kind:'unsupported-generation',checkpoint:'fake'};rehash(changed[1]);
  refusal(()=>audit(fixture(encode(changed))),'UNSUPPORTED_OPERATION');
  const other=structuredClone(rs);other[0].payload.binding.parser.protocol_sha256='0'.repeat(64);rehash(other[0]);
  refusal(()=>audit(fixture(encode(other))),'BINDING_MISMATCH');
});
test('source counter conservation/clock refusal is retained instead of becoming a fabricated zero',()=>{
  const file=fresh(),writer=open(file,{create:true});writer.push(firstBatch,envelope());
  let error;try{writer.push(lines[counter]+'\n',envelope());}catch(e){error=e;}
  assert.equal(error.code,'CLOCK_REORDER_OR_DUPLICATE');assert.ok(error.journal_commit);writer.close();
  const report=audit(file);assert.equal(report.protocol.counter.fields.gaps,'8');assert.equal(report.protocol.refusal.code,error.code);
});
test('hardlink journal alias is refused',()=>{
  const file=closed(),alias=fresh();fs.linkSync(file,alias);
  // Path regularity alone cannot establish global writer ownership for aliases;
  // this implementation therefore refuses multiply linked files as well.
  refusal(()=>open(alias),'FILE_IDENTITY');
});
test('real STOP child exits; fresh process resumes exact8+167 unique results and completion',()=>{
  const file=fresh(),first=child('create-stop',file);assert.equal(first.state,'STOPPED_ACCEPTED');assert.equal(first.retained,'8');
  const completed=child('resume-complete',file);assert.equal(completed.state,'COMPLETION_ACCEPTED');assert.equal(completed.retained,'175');
  assert.deepEqual(Object.fromEntries(['positions','tests','gaps','batches'].map(k=>[k,completed.counter[k]])),
    {positions:'1030792151040',tests:'3437901640',gaps:'175',batches:'17'});
  assert.equal(completed.generations,2);assert.equal(completed.new_counter_before_source,null);assert.ok(completed.commit);
  assert.equal(completed.old_counter.identity.run_id,ctx().identity.run_id);
  assert.equal(completed.old_counter.transport.age_at_receipt_ns,'0007');assert.equal(completed.old_counter.transport.source_age_ns,'0');
  const restored=open(file),observed=restored.snapshot();assert.equal(observed.protocol.context.identity.run_id,'second-original-run');
  assert.equal(observed.protocol.retained_result_identity_count,'175');assert.equal(observed.journal.generation_lineage.length,2);restored.close();
});
test('historical result rejected in independent restart after actual public resume transfer',()=>{
  const file=fresh();child('create-stop',file);const report=child('resume-duplicate',file);
  assert.equal(report.code,'DUPLICATE_RESULT');assert.ok(report.commit);assert.equal(report.retained,'8');assert.equal(report.baseline.gaps,'8');
  const observed=audit(file);assert.equal(observed.protocol.refusal.code,'DUPLICATE_RESULT');assert.equal(observed.generation_lineage.length,2);
});
test('journal successor observation equals direct public parser stopped/resume/finish path',()=>{
  const {file,writer}=stoppedJournal(),old=writer.snapshot().protocol,next=nextContext(),bytes=checkpointFor(old);
  const direct=new SourceProtocolSession(ctx(),{max_results:1024,max_index_bytes:24576,max_fragment_envelopes:64});
  const oldEnvelope=envelope();oldEnvelope.age_at_receipt_ns='0007';oldEnvelope.source_age_ns='0';
  direct.push(stopped,oldEnvelope);direct.finish(termination());const successor=direct.resume(next,bytes,{abi:'windows-x64-le-64'});
  writer.resume(next,bytes,resumeOptions(old,next));assert.deepEqual(writer.snapshot().protocol,successor.snapshot());
  writer.push(resumed,envelope(next));successor.push(resumed,envelope(next));
  assert.deepEqual(writer.finish(termination(next)).observation,successor.finish(termination(next)));
  const expected=writer.snapshot();writer.close();assert.deepEqual(audit(file).generation_lineage,expected.journal.generation_lineage);
  assert.equal(expected.protocol.evidence.independent_coverage,null);assert.equal(expected.protocol.evidence.network_credit,null);
});
test('exact checkpoint hex/ABI/provenance is reversible; old ages and new unavailable state stay distinct',()=>{
  const {file,writer}=stoppedJournal(),old=writer.snapshot().protocol,next=nextContext(),bytes=checkpointFor(old),options=resumeOptions(old,next);
  writer.resume(next,bytes,options);const state=writer.snapshot(),op=records(file).at(-1).payload.operation;
  assert.equal(op.checkpoint_hex.length,128);assert.deepEqual(Buffer.from(op.checkpoint_hex,'hex'),bytes);assert.equal(op.abi,options.abi);
  assert.deepEqual(op.provenance,options.provenance);assert.equal(state.protocol.counter,null);assert.equal(state.protocol.last_transport,null);
  assert.deepEqual(state.protocol.baseline,Object.fromEntries(['positions','tests','gaps','batches'].map(k=>[k,old.counter.fields[k]])));
  const retained=state.journal.generation_lineage[1].predecessor;assert.deepEqual(retained.counter,old.counter);
  assert.deepEqual(retained.stopped,old.stopped);assert.deepEqual(retained.termination,old.termination);
  assert.equal(retained.counter.identity.clock_namespace,ctx().identity.clock_namespace);writer.close();
  const restored=open(file);assert.deepEqual(restored.snapshot(),state);restored.close();
});
test('public resume allocates no fresh packed result backing and never resets old identities',()=>{
  const {writer}=stoppedJournal(),old=writer.snapshot().protocol,next=nextContext(),ArrayType=globalThis.Uint32Array;
  let allocations=0;globalThis.Uint32Array=new Proxy(ArrayType,{construct(target,args){allocations++;return Reflect.construct(target,args);}});
  try{writer.resume(next,checkpointFor(old),resumeOptions(old,next));assert.equal(allocations,0);}
  finally{globalThis.Uint32Array=ArrayType;}
  const now=writer.snapshot().protocol;assert.equal(now.retained_result_identity_count,'8');
  assert.equal(now.result_index.backing_bytes,old.result_index.backing_bytes);assert.equal(now.result_index.owner_run.run_id,next.identity.run_id);writer.close();
});
test('multiple fresh generations replay with explicit synthetic second stop/header, not an executor experiment',()=>{
  const {file,writer}=stoppedJournal(),old=writer.snapshot().protocol,next=nextContext();
  writer.resume(next,checkpointFor(old),resumeOptions(old,next));
  const paid=resumed.trimEnd().split(/\r?\n/),first=paid.findIndex(x=>x.startsWith('[pgn-counters]'));
  writer.push(paid.slice(0,first+1).join('\n')+'\n',envelope(next));
  const counterState=writer.snapshot().protocol,positions=counterState.counter.fields.positions;
  // This closed cut is synthetic; it makes no claim that the paid worker
  // really stopped a second time. Original paid counters/result rows survive.
  writer.push('[phase0-gpu] stopped positions='+positions+' checkpoint='+old.stopped.fields.checkpoint+'\n',envelope(next));
  writer.finish({...termination(next),evidence_ref:'synthetic-second-stop-from-paid-counter-only'});writer.close();
  const second=open(file),before=second.snapshot().protocol,third=nextContext(next,'third-original-run');
  const originalRun=nextContext(next,ctx().identity.run_id),beforeBytes=sha(fs.readFileSync(file));
  refusal(()=>second.resume(originalRun,checkpointFor(before),resumeOptions(before,originalRun)),'GENERATION_RUN_REUSE');
  assert.equal(sha(fs.readFileSync(file)),beforeBytes);
  second.close();const childResult=child('resume-third-complete',file);assert.equal(childResult.state,'COMPLETION_ACCEPTED');
  assert.equal(childResult.retained,'175');assert.equal(childResult.generations,3);
  const thirdReader=open(file),expected=thirdReader.snapshot();thirdReader.close();
  const replayed=open(file);assert.deepEqual(replayed.snapshot(),expected);assert.equal(expected.journal.generation_lineage.length,3);
  assert.equal(expected.protocol.retained_result_identity_count,'175');assert.equal(expected.protocol.counter.fields.batches,'17');replayed.close();
});
test('v2 refuses legacy v1 without reinterpretation, writable migration or byte changes',()=>{
  const rs=records(closed());rs[0].payload.binding.schema='pgn-source-journal-1';rs[0].payload.binding.resume_policy='UNSUPPORTED_NATIVE_RESUME';
  delete rs[0].payload.binding.budgets.max_generations;delete rs[0].payload.binding.budgets.max_generation_bytes;
  let previous='0'.repeat(64);for(const r of rs){r.previous=previous;if(r.payload.outcome)delete r.payload.outcome.lineage_sha256;rehash(r);previous=r.hash;}
  const file=fixture(encode(rs)),before=sha(fs.readFileSync(file));refusal(()=>open(file),'JOURNAL_VERSION');refusal(()=>audit(file),'JOURNAL_VERSION');
  assert.equal(sha(fs.readFileSync(file)),before);assert.equal(fs.existsSync(file+'.lock'),false);
  assert.equal(pins.get(archive+'engine-module-journal.mjs').toUpperCase(),'1A128B11BE1A1D54C1AC1A289209EBADA1B9388989F3466FD488744793C4C4E6');
  assert.equal(pins.get(archive+'engine_module_journal_test.mjs').toUpperCase(),'EE3DF0F0250ABF20429E5F46D0464B7E7825AF4E5B5A5409FF9D3093D4FB7234');
});
for(const name of ['job_id','device_id','module_id','owner','artifact_sha256'])test('transition preserves exact persistent identity: '+name,()=>{
  const {file,writer}=stoppedJournal(),old=writer.snapshot().protocol,next=nextContext();next.identity[name]+='-wrong';
  const before=writer.snapshot(),bytes=sha(fs.readFileSync(file));
  refusal(()=>writer.resume(next,checkpointFor(old),resumeOptions(old,next)),'GENERATION_IDENTITY');
  assert.deepEqual(writer.snapshot(),before);assert.equal(sha(fs.readFileSync(file)),bytes);writer.close();
});
test('run reuse, job/span, capability/scope promotion and budget/options mutation reject without clearing old index',()=>{
  for(const mutate of [c=>c.identity.run_id=ctx().identity.run_id,c=>c.job.length='1030792151041',
    c=>c.job.start='199999990000000000001',c=>c.evidence_scope='SOURCE_TRANSCRIPT',c=>c.capabilities.coverage='PROMOTED']){
    const {writer}=stoppedJournal(),old=writer.snapshot().protocol,next=nextContext();mutate(next);const before=writer.snapshot();
    refusal(()=>writer.resume(next,checkpointFor(old),resumeOptions(old,next)));assert.deepEqual(writer.snapshot(),before);writer.close();
  }
  const {writer}=stoppedJournal(),old=writer.snapshot().protocol,next=nextContext(),before=writer.snapshot();
  for(const field of ['max_results','max_index_bytes','result_index','max_fragment_envelopes','max_generations'])
    refusal(()=>writer.resume(next,checkpointFor(old),{...resumeOptions(old,next),[field]:1}),'UNSUPPORTED_METADATA');
  assert.deepEqual(writer.snapshot(),before);writer.close();
});
test('wrong clock declarations/identities/provenance reject; explicit new namespace retains old source ages',()=>{
  const {writer}=stoppedJournal(),old=writer.snapshot().protocol,next=nextContext(old.context,'new-clock-run','explicit-qpc-new');
  const before=writer.snapshot();refusal(()=>writer.resume(next,checkpointFor(old),resumeOptions(old,next)),'GENERATION_CLOCK');
  const wrong=resumeOptions(old,next,'NEW_NAMESPACE');wrong.provenance.predecessor_identity.clock_namespace='wrong-old';
  refusal(()=>writer.resume(next,checkpointFor(old),wrong),'IDENTITY_MISMATCH');
  const wrongNext=resumeOptions(old,next,'NEW_NAMESPACE');wrongNext.provenance.next_identity.clock_namespace='wrong-new';
  refusal(()=>writer.resume(next,checkpointFor(old),wrongNext),'IDENTITY_MISMATCH');assert.deepEqual(writer.snapshot(),before);
  writer.resume(next,checkpointFor(old),resumeOptions(old,next,'NEW_NAMESPACE'));
  writer.push(resumed,envelope(next));writer.finish(termination(next));const s=writer.snapshot();
  assert.equal(s.protocol.counter.identity.clock_namespace,'explicit-qpc-new');
  assert.equal(s.journal.generation_lineage[1].predecessor.counter.identity.clock_namespace,old.context.identity.clock_namespace);
  assert.equal(s.journal.generation_lineage[1].predecessor.counter.transport.age_at_receipt_ns,'0007');writer.close();
});
test('continued namespace cannot reset source clock after transition/restart',()=>{
  const {file,writer}=stoppedJournal(),old=writer.snapshot().protocol,next=nextContext();
  writer.resume(next,checkpointFor(old),resumeOptions(old,next));writer.close();const restored=open(file);
  const text=resumed.replace(/time_ns=[0-9]+/,'time_ns=0');let error;
  try{restored.push(text,envelope(next));}catch(e){error=e;}assert.equal(error.code,'RESUME_CLOCK_REGRESSION');assert.ok(error.journal_commit);
  assert.ok(Number(restored.snapshot().protocol.retained_result_identity_count)>=8);restored.close();
  assert.equal(audit(file).generation_lineage[1].predecessor.counter.fields.time_ns,old.counter.fields.time_ns);
});
test('an old namespace cannot be rebound as new after another confirmed generation stop',()=>{
  const {writer}=stoppedJournal(),old=writer.snapshot().protocol,next=nextContext(old.context,'changed-clock-run','explicit-second-clock');
  writer.resume(next,checkpointFor(old),resumeOptions(old,next,'NEW_NAMESPACE'));
  const paid=resumed.trimEnd().split(/\r?\n/),first=paid.findIndex(x=>x.startsWith('[pgn-counters]'));
  writer.push(paid.slice(0,first+1).join('\n')+'\n',envelope(next));const running=writer.snapshot().protocol;
  writer.push('[phase0-gpu] stopped positions='+running.counter.fields.positions+' checkpoint='+old.stopped.fields.checkpoint+'\n',envelope(next));
  writer.finish({...termination(next),evidence_ref:'synthetic-clock-history-stop-not-an-executor-action'});
  const before=writer.snapshot(),nextOldClock=nextContext(next,'third-clock-run',old.context.identity.clock_namespace);
  refusal(()=>writer.resume(nextOldClock,checkpointFor(before.protocol),resumeOptions(before.protocol,nextOldClock,'NEW_NAMESPACE')),'GENERATION_CLOCK');
  assert.deepEqual(writer.snapshot(),before);writer.close();
});
test('confirmed stop with actual source counter required; no generation on unconfirmed/empty stop',()=>{
  for(const text of [firstBatch,stopped]){
    const file=fresh(),writer=open(file,{create:true});writer.push(text,envelope());const before=writer.snapshot();
    const bytes=checkpointFor({context:ctx(),counter:{fields:{positions:'0',tests:'0',gaps:'0',batches:'0'}}});
    const options={...resumeOptions({context:ctx(),stopped:{fields:{checkpoint:'unsupported-unconfirmed'}}},nextContext())};
    refusal(()=>writer.resume(nextContext(),bytes,options),'CONFIRMED_COUNTER_STOP_REQUIRED');assert.deepEqual(writer.snapshot(),before);writer.close();
  }
  const file=fresh(),writer=open(file,{create:true});writer.push(header+'[phase0-gpu] stopped positions=0 checkpoint=closed-no-counter\n',envelope());
  writer.finish(termination());const old=writer.snapshot().protocol,next=nextContext();
  const bytes=Buffer.alloc(64);bytes.write('PGNCG001');refusal(()=>writer.resume(next,bytes,resumeOptions(old,next)),'CONFIRMED_COUNTER_STOP_REQUIRED');writer.close();
});
test('checkpoint encoding/ABI and provenance admission refuse before mutation',()=>{
  const {writer}=stoppedJournal(),old=writer.snapshot().protocol,next=nextContext(),before=writer.snapshot();
  for(const bytes of [undefined,Buffer.alloc(63),Buffer.alloc(65),'fake'])
    refusal(()=>writer.resume(next,bytes,resumeOptions(old,next)),'CHECKPOINT_ENCODING');
  refusal(()=>writer.resume(next,checkpointFor(old),{...resumeOptions(old,next),abi:'linux-guessed'}),'CHECKPOINT_ABI_REQUIRED');
  for(const mutate of [o=>delete o.provenance.source_age_ns,o=>o.provenance.checkpoint_path_reported='other-path',
    o=>o.provenance.checkpoint_evidence_ref='',o=>o.provenance.age_at_receipt_ns='-1',o=>o.provenance.unsupported=true]){
    const options=resumeOptions(old,next);mutate(options);refusal(()=>writer.resume(next,checkpointFor(old),options));
  }
  assert.deepEqual(writer.snapshot(),before);writer.close();
});
for(const [name,offset] of [['low',8],['high',16],['length',24],['positions',32],['tests',40],['gaps',48],['batches',56]])
test('native checkpoint mismatch durably refused without transferring or clearing index: '+name,()=>{
  const {file,writer}=stoppedJournal(),old=writer.snapshot().protocol,next=nextContext(),bytes=checkpointFor(old);
  bytes.writeBigUInt64LE(bytes.readBigUInt64LE(offset)+1n,offset);let error;
  try{writer.resume(next,bytes,resumeOptions(old,next));}catch(e){error=e;}
  assert.ok(error instanceof SourceProtocolRefusal);assert.ok(error.journal_commit);const s=writer.snapshot();
  assert.equal(s.protocol.retained_result_identity_count,'8');assert.equal(s.protocol.result_index.owner_run.run_id,old.context.identity.run_id);
  assert.equal(s.journal.generation_lineage.length,1);assert.equal(s.journal.source_attempt_refused,true);writer.close();
  const replayed=audit(file);assert.equal(replayed.source_attempt_refused,true);assert.equal(replayed.generation_lineage.length,1);
  assert.equal(replayed.protocol.retained_result_identity_count,'8');
});
test('bad checkpoint magic also preserves old stop and exact refused bytes',()=>{
  const {file,writer}=stoppedJournal(),old=writer.snapshot().protocol,next=nextContext(),bytes=checkpointFor(old);bytes[0]^=1;
  refusal(()=>writer.resume(next,bytes,resumeOptions(old,next)),'CHECKPOINT_MAGIC');writer.close();
  assert.equal(records(file).at(-1).payload.operation.checkpoint_hex,bytes.toString('hex'));
  assert.equal(audit(file).protocol.retained_result_identity_count,'8');
});
test('generation count/lineage bytes and transition record budgets admit before public transfer',()=>{
  for(const b of [{...budget(),max_generations:1},{...budget(),max_generation_bytes:1000},{...budget(),max_records:3}]){
    const {file,writer}=stoppedJournal(b),old=writer.snapshot().protocol,next=nextContext(),before=writer.snapshot(),bytes=sha(fs.readFileSync(file));
    refusal(()=>writer.resume(next,checkpointFor(old),resumeOptions(old,next)));assert.deepEqual(writer.snapshot(),before);
    assert.equal(sha(fs.readFileSync(file)),bytes);writer.close();
  }
});
test('partial transition after public ownership transfer never acknowledges; audit refuses uncertainty',()=>{
  const {file,writer}=stoppedJournal(budget(),{at:'resume_write',bytes:19}),old=writer.snapshot().protocol,next=nextContext(),size=fs.statSync(file).size;
  let error;try{writer.resume(next,checkpointFor(old),resumeOptions(old,next));}catch(e){error=e;}
  assert.equal(error.code,'JOURNAL_IO_UNKNOWN');assert.equal(error.journal_commit,undefined);assert.equal(fs.statSync(file).size,size+19);
  refusal(()=>writer.snapshot(),'JOURNAL_POISONED');writer.close();refusal(()=>open(file),'WRITER_LOCKED');refusal(()=>audit(file),'JOURNAL_TRUNCATED');
});
test('failed transition fsync leaves readable but unacknowledged next generation with old identities',()=>{
  const {file,writer}=stoppedJournal(budget(),{at:'resume_fsync'}),old=writer.snapshot().protocol,next=nextContext();let error;
  try{writer.resume(next,checkpointFor(old),resumeOptions(old,next));}catch(e){error=e;}
  assert.equal(error.code,'JOURNAL_IO_UNKNOWN');assert.equal(error.journal_commit,undefined);refusal(()=>writer.snapshot(),'JOURNAL_POISONED');writer.close();
  const s=audit(file);assert.equal(s.writer_lock_present,true);assert.match(s.recovery,/PRIOR_ACK_AND_PAST_FSYNC_UNKNOWN/);
  assert.equal(s.generation_lineage.length,2);assert.equal(s.protocol.context.identity.run_id,next.identity.run_id);
  assert.equal(s.protocol.retained_result_identity_count,'8');assert.equal(s.protocol.counter,null);assert.deepEqual(s.generation_lineage[1].predecessor.counter,old.counter);
});
test('no transition result/snapshot escapes before fsync and no corpus replay on transition',()=>{
  const {writer}=stoppedJournal(),old=writer.snapshot().protocol,next=nextContext(),sync=fs.fsyncSync,read=fs.readSync;let reads=0,syncs=0;
  fs.readSync=(...args)=>{reads++;return read(...args);};fs.fsyncSync=(...args)=>{
    refusal(()=>writer.snapshot(),'JOURNAL_BUSY');syncs++;return sync(...args);
  };
  try{const ack=writer.resume(next,checkpointFor(old),resumeOptions(old,next));assert.ok(ack.commit);assert.equal(syncs,1);assert.equal(reads,0);}
  finally{fs.fsyncSync=sync;fs.readSync=read;writer.close();}
});
test('corrupt/reordered/duplicated generation records never reset old index on replay',()=>{
  const {file,writer}=stoppedJournal(),old=writer.snapshot().protocol,next=nextContext();writer.resume(next,checkpointFor(old),resumeOptions(old,next));writer.close();
  const original=records(file),tampered=structuredClone(original);tampered.at(-1).payload.operation.checkpoint_hex='00'.repeat(64);rehash(tampered.at(-1));
  refusal(()=>audit(fixture(encode(tampered))),'JOURNAL_OUTCOME');
  const changed=structuredClone(original);changed.at(-1).payload.operation.next_context.identity.owner='other-owner';
  changed.at(-1).payload.operation.provenance.next_identity.owner='other-owner';rehash(changed.at(-1));
  refusal(()=>audit(fixture(encode(changed))),'GENERATION_IDENTITY');
  refusal(()=>audit(fixture(encode(original.concat([original.at(-1)])))),'JOURNAL_ORDER');
  const swapped=structuredClone(original);[swapped[2],swapped[3]]=[swapped[3],swapped[2]];
  refusal(()=>audit(fixture(encode(swapped))),'JOURNAL_ORDER');
});
test('incumbent, archive v1 and prior four-file fusedbit hashes unchanged after all closed tests',()=>{
  for(const [p,hash] of pins)assert.equal(sha(bounded(p)),hash,p);
  console.log('JOURNAL_TEST_ARTIFACT_DIRECTORY '+root);
});
}
