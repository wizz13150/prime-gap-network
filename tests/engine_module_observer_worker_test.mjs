import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import { fileURLToPath } from 'node:url';
import { spawn, spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { SourceJournal } from '../tools/engine-module-journal.mjs';
const sha=x=>createHash('sha256').update(x).digest('hex');
const worker=new URL('../tools/engine-module-observer-worker.mjs',import.meta.url),workerFile=fileURLToPath(worker);
function bounded(file,max=65536){const st=fs.statSync(file);assert.ok(st.isFile()&&st.size<=max);const b=fs.readFileSync(file);assert.equal(b.length,st.size);return b;}
const golden=bounded(new URL('../evidence/continuous-native-v1-golden-stdout-20261012.txt',import.meta.url));
assert.equal(sha(golden),'e860081459982dabe55fc0b3e633f7941793903b7e1a5480faeb074ebafc0e8b');
const lf=golden.indexOf(10)+1,header=golden.subarray(0,lf);
const rows=golden.toString().trimEnd().split(/\r?\n/),result=Buffer.from(rows[1]+'\r\n');
const caps={schema:'pgn-engine-module-1',id:'cuda-continuous-source-v1',protocol:'phase0-source-1',host_refill:false,
  host_verification:false,whole_job_gpu_owned:true,cuda_arch:'sm86',prime_certificate:'MR12 below318665857834031151167461',
  coverage:'SOURCE_PRP_INDEPENDENT_PROOF_PENDING',single_low64_window:true,supported_high_word:10};
const context=()=>({identity:{job_id:'closed-worker-job',device_id:'external-paid-fixture-device',run_id:'paid-source-run',module_id:caps.id,
  owner:'closed-worker-owner',clock_namespace:'external-fixture-clock',artifact_sha256:'ADD6C947E115C477F0DB2DD1C605AA61AECCE838CCECC1D2C2A9FBF65E88C158'},
  job:{start:'199999990000000000000',length:'1030792151040'},capabilities:caps,evidence_scope:'FINITE_TEST'});
const budgets=()=>({max_records:10000,max_bytes:1048576,max_record_bytes:65536,max_results:1024,max_index_bytes:24576,
  max_fragment_envelopes:64,max_generations:8,max_generation_bytes:131072});
const envelope=()=>({identity:context().identity,observed_utc:'harness-original-UTC-string',source_clock_ns:null,
  receipt_monotonic_ns:'9007199254740993',age_at_receipt_ns:'0000',source_age_ns:null});
const termination=()=>({identity:context().identity,exit_code:0,evidence_ref:'explicit-paid-fixture-test-termination-no-physical-worker-proof'});
const limits=()=>({max_command_bytes:16384,max_input_bytes:1048576,max_commands:512,max_id_bytes:64,read_chunk_bytes:257,
  max_response_bytes:2097152,max_output_bytes:16777216});
const root=fs.mkdtempSync(path.join(os.tmpdir(),'pgn-observer-worker-20261013-'));let serial=0;
function fresh(bytes=golden){const dir=path.join(root,String(++serial));fs.mkdirSync(dir);const f={source_file:path.join(dir,'finite.stdout'),journal_file:path.join(dir,'private.wal')};
  fs.writeFileSync(f.source_file,bytes,{flag:'wx'});return f;}
function options(f,create=true){return {...f,context:context(),budgets:budgets(),policy:{encoding:'NATIVE_ASCII',max_source_bytes:65536,max_chunk_bytes:256},
  provenance:{evidence_ref:'closed-fixture-copy-and-harness-envelope',trust_scope:'COOPERATING_APPEND_ONLY_LOCAL_FILE',source_kind:'CLOSED_FIXTURE'},create};}
const cmd=(id,op,extra={})=>({id,op,...extra});
const open=(f,create=true)=>cmd('open','open',{options:options(f,create)});
const pump=id=>cmd(id,'pump',{envelope:envelope()});
const snapshot=id=>cmd(id,'snapshot');
const line=x=>JSON.stringify(x)+'\n';
function decode(b){return b.toString().trimEnd().split('\n').filter(Boolean).map(JSON.parse);}
function sync(input,l=limits(),args=null){const r=spawnSync(process.execPath,args??[workerFile,'--limits-json='+JSON.stringify(l)],
  {input:Buffer.isBuffer(input)?input:Array.isArray(input)?input.map(line).join(''):input,maxBuffer:16777216,timeout:15000});
  assert.equal(r.error,undefined);assert.equal(r.stderr.length,0);return {...r,replies:decode(r.stdout)};}
function audit(f){return SourceJournal.audit({file:f.journal_file,context:context(),budgets:budgets()});}
function refused(r,code,id=undefined){assert.equal(r.status,2,r.stderr.toString());const e=r.replies.at(-1);assert.equal(e.ok,false);assert.equal(e.state,'TERMINAL');
  assert.equal(e.error.code,code);if(id!==undefined)assert.equal(e.id,id);assert.equal(Object.hasOwn(e.error,'stack'),false);assert.equal(Object.hasOwn(e.error,'message'),false);return e;}
const pinNames=['../tools/engine-module-file-observer.mjs','../tests/engine_module_file_observer_test.mjs',
  '../evidence/engine-module-file-observer-head-20261013.json','../evidence/engine-module-file-observer-tests-20261013.json',
  '../tools/engine-module-journal.mjs','../tools/engine-module-protocol.mjs','../tools/engine-module-contract.mjs',
  '../evidence/continuous-native-v1-golden-stdout-20261012.txt'];
const pins=pinNames.map(p=>({file:new URL(p,import.meta.url),hash:sha(bounded(new URL(p,import.meta.url)))}));
function asyncChild(l=limits(),args=null){
  const p=spawn(process.execPath,args??[workerFile,'--limits-json='+JSON.stringify(l)],{stdio:['pipe','pipe','pipe']});
  let stdout=[],stderr=[],bytes=0;p.stdin.on('error',()=>{});
  const record=b=>{bytes+=b.length;assert.ok(bytes<=16777216);stdout.push(b);};
  p.stdout.on('data',record);p.stderr.on('data',b=>stderr.push(b));
  const done=new Promise((resolve,reject)=>{p.once('error',reject);p.once('close',(status,signal)=>resolve({status,signal,stdout:Buffer.concat(stdout),stderr:Buffer.concat(stderr)}));});
  return {p,done,record};
}
test('real child conserves all 175 events/counters then explicit finish/close',()=>{
  const f=fresh(),commands=[open(f)];for(let i=0;i<Math.ceil(golden.length/256);i++)commands.push(pump('p'+i));
  commands.push(snapshot('before'),cmd('finish','finish',{termination:termination()}),snapshot('after'),cmd('close','close'));
  const r=sync(commands);assert.equal(r.status,0);assert.equal(r.replies.length,commands.length);
  assert.deepEqual(r.replies.map(x=>x.id),commands.map(x=>x.id));assert.ok(r.replies.every(x=>x.ok));
  const events=r.replies.filter(x=>x.id.startsWith('p')&&/^p\d+$/.test(x.id)).flatMap(x=>x.result.events);
  const gaps=events.filter(x=>x.kind==='result');assert.equal(gaps.length,175);
  assert.equal(new Set(gaps.map(x=>x.fields.lower+':'+x.fields.upper)).size,175);
  const nativeGaps=rows.filter(x=>x.includes('] GAP '));assert.deepEqual(gaps.map(x=>x.raw_line.replace(/\r$/,'')),nativeGaps);
  const before=r.replies.find(x=>x.id==='before').result,after=r.replies.find(x=>x.id==='after').result;
  assert.equal(before.protocol.state,'END_REPORTED');assert.equal(before.protocol.evidence.source_completion_accepted,false);
  assert.equal(after.protocol.state,'COMPLETION_ACCEPTED');assert.equal(after.protocol.retained_result_identity_count,'175');
  for(const [k,v] of Object.entries({positions:'1030792151040',tests:'3437901640',gaps:'175',batches:'17'}))assert.equal(after.protocol.counter.fields[k],v);
  assert.equal(after.protocol.evidence.independent_coverage,null);assert.equal(after.protocol.evidence.network_credit,null);
  assert.equal(after.protocol.counter.transport.age_at_receipt_ns,'0000');assert.equal(after.protocol.counter.transport.source_age_ns,null);
  assert.equal(after.protocol.counter.transport.receipt_monotonic_ns,'9007199254740993');
  assert.equal(fs.existsSync(f.journal_file+'.lock'),false);assert.equal(audit(f).protocol.state,'COMPLETION_ACCEPTED');
});
test('stdin EOF closes ownership without inventing source termination',()=>{
  const f=fresh(header),r=sync([open(f),pump('p')]);assert.equal(r.status,0);assert.equal(r.replies.length,2);
  assert.equal(audit(f).protocol.state,'RUNNING');assert.equal(audit(f).protocol.termination,null);assert.equal(fs.existsSync(f.journal_file+'.lock'),false);
});
test('fixture footer plus EOF is not completion',()=>{
  const f=fresh(),commands=[open(f)];for(let i=0;i<121;i++)commands.push(pump('p'+i));const r=sync(commands);assert.equal(r.status,0);
  assert.equal(audit(f).protocol.state,'END_REPORTED');assert.equal(audit(f).protocol.termination,null);
});
test('new worker clean restart rebinds actual journal and keeps original bytes/index',()=>{
  const f=fresh(),a=sync([open(f),pump('first')]);assert.equal(a.status,0);
  const b=sync([open(f,false),snapshot('old'),pump('second'),snapshot('new')]);assert.equal(b.status,0);
  assert.equal(b.replies[0].result.cursor,'256');assert.equal(b.replies[1].result.observer.cursor,'256');assert.equal(b.replies[3].result.observer.cursor,'512');
  assert.equal(b.replies[1].result.protocol.header.transport.age_at_receipt_ns,'0000');
  assert.equal(b.replies[3].result.protocol.context.identity.run_id,context().identity.run_id);
});
test('absent timing stays absent and no observed counter is manufactured',()=>{
  const f=fresh(header),p=pump('p');p.envelope={identity:context().identity};const r=sync([open(f),p,snapshot('s')]);assert.equal(r.status,0);
  const s=r.replies[2].result;assert.deepEqual(s.protocol.last_transport,{});assert.equal(s.protocol.counter,null);
  assert.equal(Object.hasOwn(s.protocol.header.transport,'source_age_ns'),false);assert.equal(s.protocol.baseline_origin,'FRESH_SOURCE_INITIALIZATION_ASSUMPTION');
});
test('stdin chunks split UTF8 metadata and lines without premature decoding',async()=>{
  const f=fresh(header),p=pump('p');p.envelope.observed_utc='original-é-UTC';const bytes=Buffer.from([open(f),p,snapshot('s')].map(line).join(''));
  const child=asyncChild();const at=bytes.indexOf(Buffer.from('é'));
  child.p.stdin.write(bytes.subarray(0,at+1));await new Promise(r=>setTimeout(r,20));child.p.stdin.end(bytes.subarray(at+1));
  const r=await child.done;assert.equal(r.status,0,r.stderr.toString());assert.equal(decode(r.stdout)[2].result.protocol.header.transport.observed_utc,'original-é-UTC');
});
test('one-byte stdin fragmentation preserves exact correlation order',async()=>{
  const f=fresh(header),child=asyncChild(),commands=[open(f),pump('p'),snapshot('s'),cmd('c','close')];
  for(const b of Buffer.from(commands.map(line).join('')))child.p.stdin.write(Buffer.from([b]));child.p.stdin.end();
  const r=await child.done;assert.equal(r.status,0,r.stderr.toString());assert.deepEqual(decode(r.stdout).map(x=>x.id),commands.map(x=>x.id));
});
test('pipe backpressure blocks later close admission until preceding replies drain',async()=>{
  const f=fresh(header),child=asyncChild();child.p.stdout.off('data',child.record);child.p.stdout.pause();
  const commands=[open(f),pump('p')];for(let i=0;i<160;i++)commands.push(snapshot('s'+i));commands.push(cmd('close','close'));
  child.p.stdin.end(commands.map(line).join(''));await new Promise(r=>setTimeout(r,150));
  assert.equal(child.p.exitCode,null);assert.equal(fs.existsSync(f.journal_file+'.lock'),true);
  child.p.stdout.on('data',child.record);child.p.stdout.resume();const r=await child.done;assert.equal(r.status,0,r.stderr.toString());
  const replies=decode(r.stdout);assert.equal(replies.length,commands.length);assert.deepEqual(replies.map(x=>x.id),commands.map(x=>x.id));
  assert.equal(fs.existsSync(f.journal_file+'.lock'),false);
});
test('actual worker death retains uncertain lock; another process refuses stealing',async()=>{
  const f=fresh(header),child=asyncChild();let seen='';
  const response=new Promise(resolve=>child.p.stdout.on('data',b=>{seen+=b.toString();if(seen.split('\n').length>=3)resolve();}));
  child.p.stdin.write(line(open(f))+line(pump('p')));await response;child.p.kill('SIGKILL');const killed=await child.done;
  assert.notEqual(killed.status,0);assert.equal(fs.existsSync(f.journal_file+'.lock'),true);
  assert.equal(audit(f).protocol.state,'RUNNING');assert.equal(audit(f).protocol.termination,null);
  const r=sync([open(f,false)]);const e=refused(r,'WRITER_LOCKED','open');assert.equal(e.error.domain,'JOURNAL');assert.ok(fs.existsSync(f.journal_file+'.lock'));
});
test('ordinary source refusal returns committed accepted-prefix events without active snapshot',()=>{
  const f=fresh(Buffer.concat([header,result,result])),o=open(f);o.options.policy.max_chunk_bytes=1024;
  const r=sync([o,pump('p'),snapshot('stale')],{...limits(),max_response_bytes:4194304,max_output_bytes:16777216});
  const e=refused(r,'DUPLICATE_RESULT','p');assert.equal(e.error.domain,'SOURCE_PROTOCOL');assert.equal(e.error.uncertainty,'REFUSAL_COMMITTED_AFTER_FSYNC_RETURN');
  assert.ok(e.commit);assert.equal(e.observed_events.filter(x=>x.kind==='result').length,1);assert.equal(r.replies.length,2);
  assert.equal(e.remaining_input_not_admitted,true);assert.equal(audit(f).protocol.state,'REFUSED');
});
test('real injected fsync error remains uncertain IO, never ordinary refusal/success',()=>{
  const f=fresh(header),script=`import fs from 'node:fs';import {runObserverWorker} from ${JSON.stringify(worker.href)};
    const original=fs.fsyncSync;let calls=0;fs.fsyncSync=fd=>{if(++calls===4)throw new Error('closed private fsync injection');return original(fd);};
    const r=await runObserverWorker({input:process.stdin,output:process.stdout,limits:${JSON.stringify(limits())}});process.exitCode=r.exit_code;`;
  const r=sync([open(f),pump('p'),snapshot('stale')],limits(),['--input-type=module','-e',script]);
  const e=refused(r,'JOURNAL_IO_UNKNOWN','p');assert.equal(e.error.domain,'JOURNAL');assert.equal(e.error.uncertainty,'JOURNAL_BYTES_AND_OWNERSHIP_UNCERTAIN');
  assert.equal(e.commit,null);assert.deepEqual(e.observed_events,[]);assert.equal(e.ownership.lock_retained,true);assert.equal(e.ownership.writer_poisoned,true);
  assert.ok(fs.existsSync(f.journal_file+'.lock'));assert.equal(r.replies.length,2);
});
test('broken output pipe does not launch next command or claim delivery',async()=>{
  const f=fresh(),child=asyncChild();child.p.stdout.destroy();child.p.stdin.end([open(f),pump('p'),cmd('close','close')].map(line).join(''));
  const r=await child.done;assert.equal(r.status,3,r.stderr.toString());
  assert.equal(audit(f).protocol.state,'AWAITING_HEADER');assert.equal(fs.existsSync(f.journal_file+'.lock'),false);
});
test('pump response reservation refuses before any source push',()=>{
  const f=fresh(header),r=sync([open(f),pump('p')],{...limits(),max_response_bytes:2048});const e=refused(r,'RESPONSE_LIMIT','p');
  assert.equal(e.error.domain,'TRANSPORT');assert.equal(audit(f).protocol.state,'AWAITING_HEADER');assert.equal(audit(f).records,'1');
});
test('snapshot response overflow refuses full response, does not silently truncate it',()=>{
  const f=fresh(header),r=sync([open(f),snapshot('big')],{...limits(),max_response_bytes:2048});refused(r,'RESPONSE_LIMIT','big');
  assert.equal(r.replies.length,2);assert.equal(Object.hasOwn(r.replies[1],'result'),false);assert.equal(audit(f).protocol.state,'AWAITING_HEADER');
});
test('output aggregate budget exhaustion is explicit and closes ownership',()=>{
  const f=fresh(header),r=sync([open(f),snapshot('big')],{...limits(),max_response_bytes:8192,max_output_bytes:10240});assert.equal(r.status,0);
  // One full snapshot fits the per-reply cap but not the remaining reserved
  // output budget after the open reply when multiple snapshots are requested.
  const many=sync([open(f,false),...Array.from({length:10},(_,i)=>snapshot('s'+i))],{...limits(),max_response_bytes:8192,max_output_bytes:10240});
  refused(many,'OUTPUT_BUDGET');assert.ok(many.stdout.length<=10240);assert.equal(fs.existsSync(f.journal_file+'.lock'),false);
});
test('duplicate IDs are not evicted or reused',()=>{const f=fresh(header),r=sync([open(f),snapshot('same'),snapshot('same')]);refused(r,'DUPLICATE_ID','same');assert.equal(r.replies.length,3);});
test('ID admission bound refuses before new command',()=>{const f=fresh(header),r=sync([open(f),snapshot('s')],{...limits(),max_commands:1});refused(r,'ID_BUDGET','s');});
for(const [name,c,code] of [['pump before open',pump('p'),'NOT_OPEN'],['snapshot before open',snapshot('s'),'NOT_OPEN'],
  ['close before open',cmd('c','close'),'NOT_OPEN'],['resume',cmd('r','resume'),'UNSUPPORTED_OPERATION'],['executor',cmd('x','launch'),'UNSUPPORTED_OPERATION'],
  ['arbitrary file action',cmd('x','unlink',{path:'anything'}),'UNSUPPORTED_OPERATION']])
  test(name+' refused',()=>refused(sync([c]),code,c.id));
test('second open refused without opening another observer',()=>{const f=fresh(header),o=open(f);o.id='second';refused(sync([open(f),o]),'OPEN_ORDER','second');});
test('commands after explicit close refuse terminal state',()=>{const f=fresh(header);refused(sync([open(f),cmd('close','close'),snapshot('late')]),'TERMINAL_STATE','late');});
test('pump after explicit finish refuses instead of stale active observation',()=>{
  const f=fresh(),cs=[open(f)];for(let i=0;i<121;i++)cs.push(pump('p'+i));cs.push(cmd('finish','finish',{termination:termination()}),pump('late'));
  refused(sync(cs),'FINISHED_STATE','late');assert.equal(audit(f).protocol.state,'COMPLETION_ACCEPTED');
});
for(const [name,c,code] of [['unsupported command key',{id:'s',op:'snapshot',extra:true},'UNSUPPORTED_KEYS'],
  ['missing correlation',{op:'snapshot'},'CORRELATION_ID'],['numeric correlation',{id:1,op:'snapshot'},'CORRELATION_ID'],
  ['bad correlation',{id:'a b',op:'snapshot'},'CORRELATION_ID'],['unsupported open key',null,'UNSUPPORTED_KEYS']])
  test(name+' refused',()=>{const f=fresh(header),v=c??{...open(f),options:{...options(f),test_fault:{at:'after_commit_before_cursor'}}};refused(sync([v]),code);});
test('nonfinite/live source scope refused without file actions',()=>{
  const f=fresh(header),o=open(f);o.options.context.evidence_scope='SOURCE_TRANSCRIPT';refused(sync([o]),'FINITE_FILE_SCOPE');assert.equal(fs.existsSync(f.journal_file),false);
});
test('external native provenance refused in finite-only worker',()=>{
  const f=fresh(header),o=open(f);o.options.provenance.source_kind='EXTERNAL_NATIVE_STDOUT';refused(sync([o]),'FINITE_FILE_SCOPE');assert.equal(fs.existsSync(f.journal_file),false);
});
test('UNC/network source paths are refused before any observer open',()=>{
  const f=fresh(header),o=open(f);o.options.source_file='\\\\not-a-contacted-server\\share\\file.stdout';
  refused(sync([o]),'LOCAL_PATH_SCOPE');assert.equal(fs.existsSync(f.journal_file),false);
});
test('close I/O refusal preserves actual journal code and uncertain ownership',()=>{
  const f=fresh(header),script=`import fs from 'node:fs';import {runObserverWorker} from ${JSON.stringify(worker.href)};
    fs.unlinkSync=()=>{throw new Error('closed private lock release injection');};
    const r=await runObserverWorker({input:process.stdin,output:process.stdout,limits:${JSON.stringify(limits())}});process.exitCode=r.exit_code;`;
  const r=sync([open(f),cmd('close','close')],limits(),['--input-type=module','-e',script]);
  const e=refused(r,'JOURNAL_IO_UNKNOWN','close');assert.equal(e.error.domain,'JOURNAL');assert.equal(e.ownership.ownership_uncertain,true);
  assert.equal(e.ownership.close_error_code,'JOURNAL_IO_UNKNOWN');assert.equal(fs.existsSync(f.journal_file+'.lock'),true);
});
test('journal record capacity refusal is not converted into successful pump',()=>{
  const f=fresh(header),o=open(f);o.options.budgets.max_records=1;const r=sync([o,pump('p')]);
  refused(r,'JOURNAL_RECORD_BUDGET','p');const s=SourceJournal.audit({file:f.journal_file,context:context(),budgets:o.options.budgets});
  assert.equal(s.protocol.state,'AWAITING_HEADER');assert.equal(s.records,'1');
});
test('EOF partial command closes acquired ownership but does not invent termination',()=>{
  const f=fresh(header),r=sync(line(open(f))+line(pump('p'))+'{"id":"incomplete"');refused(r,'PARTIAL_COMMAND',null);
  assert.equal(audit(f).protocol.state,'RUNNING');assert.equal(audit(f).protocol.termination,null);assert.equal(fs.existsSync(f.journal_file+'.lock'),false);
});
test('depth and node admission refuse JSON growth before file actions',()=>{
  const deep='{"id":"x","op":"snapshot","extra":'+'['.repeat(26)+'0'+']'.repeat(26)+'}\n';
  refused(sync(deep),'JSON_STRUCTURE_BOUND');
  const wide='{"id":"x","op":"snapshot","extra":['+Array(4096).fill('0').join(',')+']}\n';refused(sync(wide),'JSON_STRUCTURE_BOUND');
});
test('ID byte bound refuses without echoing oversized correlation data',()=>{
  refused(sync([snapshot('long-id')],{...limits(),max_id_bytes:2}),'CORRELATION_ID',null);
});
for(const [name,input,code] of [['malformed JSON','{oops}\n','MALFORMED_JSON'],['duplicate JSON key','{"id":"x","id":"y","op":"snapshot"}\n','DUPLICATE_KEY'],
  ['unsafe integer','{"id":"x","op":"snapshot","n":9007199254740993}\n','UNSAFE_JSON_NUMBER'],['negative zero','{"id":"x","op":"snapshot","n":-0}\n','UNSAFE_JSON_NUMBER'],
  ['partial tail','{"id":"x"','PARTIAL_COMMAND'],['raw NUL',Buffer.from([0,10]),'WIRE_CONTROL_ENCODING'],
  ['raw control',Buffer.from([1,10]),'WIRE_CONTROL_ENCODING'],['malformed UTF8',Buffer.from([0xff,10]),'WIRE_UTF8'],
  ['split incomplete UTF8',Buffer.from([0xc3,10]),'WIRE_UTF8'],['UTF16 BOM',Buffer.from([0xff,0xfe,10]),'WIRE_UTF8'],
  ['UTF8 BOM',Buffer.concat([Buffer.from([0xef,0xbb,0xbf]),Buffer.from('{}\n')]),'MALFORMED_JSON'],
  ['unpaired escaped surrogate','{"id":"x","op":"snapshot","s":"\\ud800"}\n','WIRE_UNICODE']])
  test(name+' refuses bounded wire decoding',()=>refused(sync(input),code));
test('oversized command refused without waiting for newline or accumulating rest',()=>{
  const r=sync(Buffer.alloc(1000,32),{...limits(),max_command_bytes:128});refused(r,'COMMAND_BYTE_LIMIT');assert.ok(r.stdout.length<2048);
});
test('total input budget exhaustion after valid command is explicit',()=>{
  const f=fresh(header),o=line(open(f));
  const bytes=o+' '.repeat(4097);refused(sync(bytes,{...limits(),max_command_bytes:4096,max_input_bytes:4096}),'INPUT_BUDGET');
});
test('missing/unsupported/unsafe transport limits reject before reading stdin',()=>{
  refused(sync('',limits(),[workerFile]),'LIMITS_REQUIRED');
  refused(sync('',{...limits(),extra:1}),'UNSUPPORTED_KEYS');
  refused(sync('',{...limits(),max_commands:4097}),'TRANSPORT_LIMIT');
});
test('dependency version change in private code copy refuses the frozen response-bound contract',()=>{
  const f=fresh(header),dir=path.join(path.dirname(f.source_file),'private-code');fs.mkdirSync(dir);
  for(const name of ['engine-module-observer-worker.mjs','engine-module-file-observer.mjs','engine-module-journal.mjs',
    'engine-module-protocol.mjs','engine-module-contract.mjs']){
    fs.writeFileSync(path.join(dir,name),bounded(new URL('../tools/'+name,import.meta.url)),{flag:'wx'});
  }
  fs.appendFileSync(path.join(dir,'engine-module-file-observer.mjs'),'\n// closed copied version change\n');
  const r=sync([open(f)],limits(),[path.join(dir,'engine-module-observer-worker.mjs'),'--limits-json='+JSON.stringify(limits())]);
  refused(r,'DEPENDENCY_PIN',null);assert.equal(fs.existsSync(f.journal_file),false);
});
test('wrong source envelope identity yields refusal, never success rename',()=>{
  const f=fresh(header),p=pump('p');p.envelope.identity.run_id='wrong';const r=sync([open(f),p]);const e=refused(r,'IDENTITY_MISMATCH','p');
  assert.equal(e.error.domain,'JOURNAL');assert.equal(audit(f).protocol.state,'AWAITING_HEADER');
});
test('wrong termination is not inferred from worker child exit',()=>{
  const f=fresh(header),r=sync([open(f),pump('p'),cmd('finish','finish',{termination:termination()})]);refused(r,'MISSING_TERMINAL','finish');
  assert.equal(audit(f).protocol.state,'REFUSED');
});
test('native byte encoding refusal is observer-layer, no fake parser events',()=>{
  const f=fresh(Buffer.from([0xff])),r=sync([open(f),pump('p')]);const e=refused(r,'SOURCE_ENCODING','p');
  assert.equal(e.error.domain,'OBSERVER');assert.deepEqual(e.observed_events,[]);assert.equal(audit(f).protocol.state,'AWAITING_HEADER');
});
test('incumbent observer/journal/protocol/contract/fixture source and receipts remain unchanged',()=>{
  for(const p of pins)assert.equal(sha(bounded(p.file)),p.hash,String(p.file));
});
