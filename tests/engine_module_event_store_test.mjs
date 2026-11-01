// Closed SQLite/actual-reader QA only. Fixture timing is harness provenance.
import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createHash,randomUUID} from 'node:crypto';
import {spawn,spawnSync} from 'node:child_process';
import {DatabaseSync} from 'node:sqlite';
import {HistoricalEventStore,EVENT_STORE_DEPENDENCIES} from '../tools/engine-module-event-store.mjs';
import {SourceJournal} from '../tools/engine-module-journal.mjs';
import {readJournalEventPage} from '../tools/engine-module-event-page.mjs';
const sha=x=>createHash('sha256').update(x).digest('hex');
const clone=x=>structuredClone(x);
const self=fileURLToPath(import.meta.url);
const start={sequence:'0',event_offset:'0'};
const query={after:null,kind:null,row_limit:10,byte_limit:65536};

if(process.argv[2]==='--private-child'){
 const file=process.argv[3];assert.ok(fs.statSync(file).size<=262144);
 const command=JSON.parse(fs.readFileSync(file,'utf8'));
 if(['crash','failed-commit','creation-crash','full','before-commit'].includes(command.mode)){
  const exec=DatabaseSync.prototype.exec;let commits=0;
  DatabaseSync.prototype.exec=function(sql){
   if(command.mode==='before-commit'&&sql==='COMMIT'&&commits===1)throw Object.assign(new Error('private pre-COMMIT failure'),{errcode:10});
   const result=exec.call(this,sql);
   if(sql==='COMMIT'&&++commits===(command.mode==='creation-crash'?1:2)){
    if(command.mode==='failed-commit')throw Object.assign(new Error('private injected post-COMMIT failure'),{errcode:10});
    if(command.mode==='full'||command.mode==='before-commit')return result;
    fs.writeFileSync(command.marker,JSON.stringify({pid:process.pid,token:command.token,phase:'COMMITTED_NOT_RETURNED'}));
    Atomics.wait(new Int32Array(new SharedArrayBuffer(4)),0,0,30000);
    throw new Error('parent did not cut verified private child');
   }return result;
  };
 }
 let cache;
 try {
  cache=HistoricalEventStore.open(command.options);
  if(command.mode==='open'){process.stdout.write(JSON.stringify(cache.status()));cache.close();}
  else if(['failed-commit','full','before-commit'].includes(command.mode)){
   try{cache.importPage(command.input);throw new Error('success escaped');}
   catch(e){assert.equal(e.code,'CACHE_IO_UNCERTAIN');assert.equal(e.uncertain,true);
    if(command.mode==='full')assert.equal(e.sqlite_errcode,13);else assert.equal(e.sqlite_errcode,10);
    assert.throws(()=>cache.status(),x=>x.code==='CACHE_POISONED');
    assert.throws(()=>cache.close(),x=>x.code==='POISONED_REQUIRES_PROCESS_EXIT_AND_AUDIT');
    fs.writeFileSync(command.marker,JSON.stringify({pid:process.pid,token:command.token,phase:'POISONED_NO_ACK',code:e.code}));
    Atomics.wait(new Int32Array(new SharedArrayBuffer(4)),0,0,30000);process.exit(7);
   }
  }else if(command.mode==='crash')cache.importPage(command.input);
  else throw new Error('unknown private child mode');
 }catch(e){process.stdout.write(JSON.stringify({code:e.code??'CHILD_FAILURE',uncertain:e.uncertain??false}));process.exitCode=2;}
}else{
 const qaRoot=path.resolve('pgn/private-qa/engine-module-event-store-20261013');
 fs.mkdirSync(qaRoot,{recursive:true});const directory=fs.mkdtempSync(path.join(qaRoot,'run-'));
 const fixtureFile=new URL('../evidence/continuous-native-v1-golden-stdout-20261012.txt',import.meta.url);
 assert.equal(fs.statSync(fixtureFile).size,30797);const golden=fs.readFileSync(fixtureFile);
 assert.equal(sha(golden),'e860081459982dabe55fc0b3e633f7941793903b7e1a5480faeb074ebafc0e8b');
 const context={identity:{job_id:'event-store-job',device_id:'closed-fixture-device',run_id:'original-run',
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
  receipt_monotonic_ns:'9007199254740993',age_at_receipt_ns:'0000',source_age_ns:null,source_clock_ns:'0'};
 const pageLimits={max_wal_bytes:1048576,max_replay_index_bytes:49152,max_page_events:25,max_page_bytes:65536};
 const maxDb=2097152,reserve=32+maxDb/4096*4120;
 const limits={max_db_bytes:maxDb,max_sql_wal_bytes:reserve,max_source_bytes:1048576,max_events:2048,
  max_event_bytes:16384,max_total_event_bytes:4194304,max_import_page_bytes:65536,max_pages:1024,
  max_metadata_bytes:65536,max_query_rows:64,max_query_bytes:65536,max_query_read_bytes:131072,max_recovery_read_bytes:reserve};
 const frozenNames=['engine-module-event-page.mjs','engine-module-observation-model.mjs','engine-module-observer-worker.mjs',
  'engine-module-file-observer.mjs','engine-module-journal.mjs','engine-module-protocol.mjs','engine-module-contract.mjs'];
 const pins=Object.fromEntries(frozenNames.map(n=>{const p=new URL('../tools/'+n,import.meta.url);assert.ok(fs.statSync(p).size<=65536);return [n,sha(fs.readFileSync(p))];}));
 const folder=()=>fs.mkdtempSync(path.join(directory,'case-'));
 function source(finish=false,refuse=false){
  const file=path.join(folder(),'source.jsonl'),j=SourceJournal.open({file,context,budgets,create:true});const original=[];
  if(refuse){const lines=golden.toString().split(/\r?\n/),gap=lines.find(x=>x.startsWith('[phase0-gpu] GAP '));
   try{j.push(lines[0]+'\r\n'+gap+'\r\n'+gap+'\r\n',envelope);assert.fail('duplicate must refuse');}
   catch(e){assert.ok(e.journal_commit);original.push(...e.observed_events);}
  }else{
   original.push(...j.push(golden.subarray(0,17).toString(),envelope).events);
   const second={...envelope};delete second.source_age_ns;second.source_clock_ns=null;
   original.push(...j.push(golden.subarray(17).toString(),second).events);
   if(finish)j.finish({identity:context.identity,exit_code:0,evidence_ref:'closed-fixture-not-hardware'});
  }
  const snapshot=j.snapshot();j.close();const rows=[],pages=[];let cursor=start;
  const began=process.hrtime.bigint();
  do{const page=readJournalEventPage({file,context,budgets,base_sha256:snapshot.journal.last_sha256,cursor,limits:pageLimits});
   pages.push({input_cursor:cursor,page});rows.push(...page.events);cursor=page.next_cursor;
  }while(cursor);
  assert.deepEqual(rows.map(r=>r.event),original);
  return {file,snapshot,pages,rows,original,sha:sha(fs.readFileSync(file)),reader_elapsed_ns:String(process.hrtime.bigint()-began)};
 }
 const f=source(),done=source(true),refused=source(false,true);
 function options(fixture=f,overrides={}){
  return clone({file:path.join(folder(),'derived.sqlite'),source:{file:fixture.file,context,budgets,base_sha256:fixture.snapshot.journal.last_sha256,
   dependencies:EVENT_STORE_DEPENDENCIES},limits,create:true,...overrides});
 }
 function open(fixture=f,overrides={}){const o=options(fixture,overrides);return {o,c:HistoricalEventStore.open(o)};}
 function all(c,kind=null){const out=[];let after=null;do{const p=c.query({...query,kind,after});assert.ok(Buffer.byteLength(JSON.stringify(p))<=query.byte_limit);
  out.push(...p.events);after=p.next_after;assert.equal(p.has_more,after!==null);}while(after);return out;}
 function child(command){const config=path.join(folder(),'child.json');fs.writeFileSync(config,JSON.stringify(command));
  return spawnSync(process.execPath,[self,'--private-child',config],{encoding:'utf8',timeout:10000,maxBuffer:8192});}
 const code=(run,expected)=>assert.throws(run,e=>e.code===expected);
 const measure={schema:'pgn-event-store-closed-measurement-1',fixture_bytes:30797,synthetic_rows:0,source_reader_elapsed_ns:f.reader_elapsed_ns};

 test('actual SQLite runtime and only verified supported methods',()=>{
  assert.equal(process.version,'v24.19.0');const d=new DatabaseSync(':memory:');assert.equal(d.prepare('SELECT sqlite_version() AS v').get().v,'3.53.3');
  for(const n of ['exec','prepare','close'])assert.equal(typeof d[n],'function');
  for(const n of ['run','get','iterate'])assert.equal(typeof d.prepare('SELECT 1')[n],'function');d.close();
 });
 test('actual fixed-digest pages, original175 GAPs/17 counters, transactional cursor and bounded query',()=>{
  const {c}=open();const begin=process.hrtime.bigint();for(const p of f.pages)c.importPage(p);
  measure.import_elapsed_ns=String(process.hrtime.bigint()-begin);const qbegin=process.hrtime.bigint();
  assert.deepEqual(all(c),f.rows);measure.query_elapsed_ns=String(process.hrtime.bigint()-qbegin);
  assert.equal(all(c,'result').length,175);const counters=all(c,'counter');assert.equal(counters.length,17);
  assert.equal(counters.at(-1).event.fields.tests,'3437901640');assert.equal(counters.at(-1).event.fields.positions,'1030792151040');
  assert.equal(counters.at(-1).event.fields.batches,'17');assert.equal(counters.at(-1).event.fields.gaps,'175');
  const s=c.status();assert.equal(s.event_count,f.rows.length);assert.equal(s.complete_at_digest,true);assert.equal(s.source_state,'END_REPORTED');
  assert.equal(s.source_evidence.source_completion_accepted,false);assert.equal(s.authority.network_credit,null);
  measure.storage=s.storage;measure.events=s.event_count;measure.event_bytes=s.event_bytes;measure.pages=s.page_count;c.close();
 });
 test('clean reopen with exact binding restores cursor and historical bytes without reimport',()=>{
  const {c,o}=open();c.importPage(f.pages[0]);const s=c.status();c.close();
  const next=HistoricalEventStore.open({...o,create:false});assert.deepEqual(next.status(),s);assert.deepEqual(all(next),f.pages[0].page.events);next.close();
 });
 test('every exact replay is idempotent, including old page after full import and reopen',()=>{
  const {c,o}=open();for(const p of f.pages)c.importPage(p);const before=c.status();
  for(const p of f.pages){assert.equal(c.importPage(p).replayed,true);assert.deepEqual(c.status(),before);}c.close();
  const reopened=HistoricalEventStore.open({...o,create:false});assert.equal(reopened.importPage(f.pages[0]).replayed,true);assert.deepEqual(all(reopened),f.rows);reopened.close();
 });
 test('derived complete at digest does not infer source termination from END/footer/EOF/cache close',()=>{
  const {c,o}=open();for(const p of f.pages)c.importPage(p);c.close();const n=HistoricalEventStore.open({...o,create:false});
  assert.equal(n.status().complete_at_digest,true);assert.equal(n.status().source_evidence.source_completion_accepted,false);n.close();
 });
 test('explicit original accepted termination stays a separately labeled conformance observation',()=>{
  const {c}=open(done);for(const p of done.pages)c.importPage(p);
  assert.equal(c.status().source_state,'COMPLETION_ACCEPTED');assert.equal(c.status().source_evidence.source_completion_accepted,true);
  assert.equal(c.status().source_evidence.acceptance_boundary,'SUPPLIED_TRANSCRIPT_AND_TERMINATION_CONFORMANCE_ONLY');c.close();
 });
 test('source refusal retains only the actual accepted prefix and historical REFUSED status',()=>{
  const {c}=open(refused);for(const p of refused.pages)c.importPage(p);assert.deepEqual(all(c),refused.rows);
  assert.equal(all(c,'result').length,1);assert.equal(c.status().source_state,'REFUSED');assert.equal(c.status().source_evidence.source_completion_accepted,false);c.close();
 });
 test('original fragmented transport null/absent/leading-zero clocks and decimal values survive',()=>{
  const {c}=open();for(const p of f.pages)c.importPage(p);const header=all(c,'header')[0].event;
  assert.equal(header.transport_fragments.length,2);assert.equal(header.transport_fragments[0].transport.source_age_ns,null);
  assert.equal(header.transport_fragments[0].transport.source_clock_ns,'0');
  assert.equal(Object.hasOwn(header.transport,'source_age_ns'),false);assert.equal(header.transport.source_clock_ns,null);
  assert.equal(header.transport.age_at_receipt_ns,'0000');assert.equal(header.transport.receipt_monotonic_ns,'9007199254740993');
  assert.equal(header.transport.observed_utc,'2026-10-12T21:10:00.1234567Z');assert.deepEqual(all(c),f.rows);c.close();
 });
 test('genuine wire zero remains a decimal string, not unavailable/null',()=>{
  const {c}=open();for(const p of f.pages)c.importPage(p);const s=all(c,'summary')[0].event;
  assert.equal(s.fields.verification_failures,'0');assert.equal(s.fields.records_new,'0');assert.equal(Object.hasOwn(s.fields,'verification_failures'),true);c.close();
 });
 for(const [name,alter,expected] of [
  ['wrong digest',p=>p.page.base_sha256='0'.repeat(64),'PAGE_BINDING'],
  ['wrong context',p=>p.page.context_sha256='0'.repeat(64),'PAGE_BINDING'],
  ['unknown page key',p=>p.page.extra=true,'UNSUPPORTED_METADATA'],
  ['unknown event key',p=>p.page.events[0].event.extra=true,'UNSUPPORTED_METADATA'],
  ['wrong device',p=>p.page.events[0].event.identity.device_id='another','EVENT_BINDING'],
  ['wrong run',p=>p.page.events[0].event.identity.run_id='another','EVENT_BINDING'],
  ['wrong clock',p=>p.page.events[0].event.identity.clock_namespace='another','EVENT_BINDING'],
  ['wrong owner',p=>p.page.events[0].event.identity.owner='another','EVENT_BINDING'],
  ['scope promotion',p=>p.page.source_evidence.execution_scope='PRODUCTION','PAGE_EVIDENCE'],
  ['invented credit',p=>p.page.source_evidence.network_credit='paid','PAGE_EVIDENCE'],
  ['false cache completion',p=>p.page.complete_at_digest=true,'PAGE_COMPLETION'],
  ['false source completion',p=>p.page.source_evidence.source_completion_accepted=true,'PAGE_EVIDENCE'],
  ['reordered events',p=>p.page.events.reverse(),'INITIAL_EVENT'],
  ['duplicated event',p=>p.page.events.splice(1,0,p.page.events[0]),'EVENT_ORDER'],
  ['ordinal omission',p=>p.page.events.splice(1,1),'EVENT_ORDER'],
  ['unknown kind',p=>p.page.events[0].event.kind='resume','EVENT_BINDING'],
  ['bad next cursor',p=>p.page.next_cursor.event_offset='0','CURSOR_PROGRESS'],
  ['noncanonical cursor',p=>p.input_cursor.sequence='00','CURSOR']
 ])test(name+' refuses before state mutation',()=>{
  const {c}=open();const before=c.status(),p=clone(f.pages[0]);alter(p);code(()=>c.importPage(p),expected);assert.deepEqual(c.status(),before);assert.deepEqual(all(c),[]);c.close();
 });
 test('late initial page, skipped page and stale unreceipted cursor refuse atomically',()=>{
  const {c}=open();code(()=>c.importPage(f.pages[1]),'NONCONTIGUOUS_INPUT_CURSOR');c.importPage(f.pages[0]);const before=c.status();
  code(()=>c.importPage(f.pages[2]),'NONCONTIGUOUS_INPUT_CURSOR');const wrong=clone(f.pages[1]);wrong.input_cursor={sequence:'0',event_offset:'1'};
  code(()=>c.importPage(wrong),'NONCONTIGUOUS_INPUT_CURSOR');assert.deepEqual(c.status(),before);c.close();
 });
 test('correct claimed input cursor does not admit a page whose first typed address skips the expected event',()=>{
  const {c}=open();c.importPage(f.pages[0]);const skipped=clone(f.pages[2]);skipped.input_cursor=clone(f.pages[1].input_cursor);
  const before=c.status();code(()=>c.importPage(skipped),'SKIPPED_EVENT');assert.deepEqual(c.status(),before);c.close();
 });
 test('known cursor replay with changed exact JSON data refuses PAGE_CONFLICT',()=>{
  const {c}=open();c.importPage(f.pages[0]);const before=c.status(),changed=clone(f.pages[0]);changed.page.events[1].event.fields.gap='0002';
  code(()=>c.importPage(changed),'PAGE_CONFLICT');assert.deepEqual(c.status(),before);c.close();
 });
 test('source metadata cannot change on the next fixed digest page',()=>{
  const {c}=open();c.importPage(f.pages[0]);const p=clone(f.pages[1]);p.page.source_state='RUNNING';
  code(()=>c.importPage(p),'PAGE_SOURCE_CONFLICT');c.close();
 });
 for(const [name,change,expected] of [
  ['event count',l=>l.max_events=1,'EVENT_COUNT_BUDGET'],
  ['event bytes',l=>l.max_event_bytes=1,'EVENT_BYTE_BUDGET'],
  ['cumulative bytes',l=>l.max_total_event_bytes=1,'EVENT_TOTAL_BYTE_BUDGET'],
  ['page bytes',l=>l.max_import_page_bytes=1024,'JSON_BYTE_BUDGET']
 ])test(name+' admission has no partial rows/cursor',()=>{
  const lim=clone(limits);change(lim);const {c}=open(f,{limits:lim});const before=c.status();code(()=>c.importPage(f.pages[0]),expected);assert.deepEqual(c.status(),before);c.close();
 });
 test('page count budget rejects second delta without cursor change',()=>{
  const {c}=open(f,{limits:{...limits,max_pages:1}});c.importPage(f.pages[0]);const before=c.status();code(()=>c.importPage(f.pages[1]),'PAGE_COUNT_BUDGET');assert.deepEqual(c.status(),before);c.close();
 });
 test('query byte/row/read caps refuse rather than truncate away an oversized first row',()=>{
  const {c}=open();c.importPage(f.pages[0]);const before=c.status();code(()=>c.query({...query,byte_limit:1024}),'QUERY_EVENT_EXCEEDS_RESPONSE');
  code(()=>c.query({...query,row_limit:65}),'QUERY_BUDGET');assert.deepEqual(c.status(),before);c.close();
  const n=open(f,{limits:{...limits,max_query_read_bytes:128}}).c;n.importPage(f.pages[0]);code(()=>n.query(query),'QUERY_READ_BUDGET');n.close();
 });
 test('row/byte-limited continuation returns every exact event, no silently dropped row',()=>{
  const {c}=open();for(const p of f.pages)c.importPage(p);const out=[];let after=null;
  do{const p=c.query({...query,row_limit:1,byte_limit:4096,after});out.push(...p.events);after=p.next_after;}while(after);
  assert.deepEqual(out,f.rows);c.close();
 });
 test('returned observations and status cannot mutate cache or binding',()=>{
  const {c}=open();c.importPage(f.pages[0]);const r=c.query(query);r.events[0].event.raw_line='hostile';const s=c.status();s.source_evidence.network_credit='hostile';
  assert.deepEqual(c.query(query).events,f.pages[0].page.events.slice(0,10));assert.equal(c.status().source_evidence.network_credit,null);c.close();
 });
 test('accessors/cycles/undefined/lone surrogates are rejected before invoking metadata',()=>{
  const {c}=open();const p=clone(f.pages[0]);let invoked=false;Object.defineProperty(p.page,'evil',{enumerable:true,get(){invoked=true;return 1;}});
  code(()=>c.importPage(p),'JSON_DATA');assert.equal(invoked,false);
  for(const poison of [undefined,'\ud800']){const q=clone(f.pages[0]);q.page.events[0].event.fields.poison=poison;assert.throws(()=>c.importPage(q));}
  const cycle=clone(f.pages[0]);cycle.page.self=cycle;code(()=>c.importPage(cycle),'JSON_DATA');c.close();
 });
 test('sparse/extended arrays cannot silently discard unsupported metadata',()=>{
  const {c}=open();const sparse=clone(f.pages[0]);delete sparse.page.events[1];code(()=>c.importPage(sparse),'JSON_DATA');
  const extra=clone(f.pages[0]);extra.page.events.extra='untrusted';code(()=>c.importPage(extra),'JSON_DATA');c.close();
 });
 test('immutable source replacement/truncation/overwrite refuses, without repair',()=>{
  for(const change of ['replace','truncate','overwrite']){
   const copied=path.join(folder(),'copy.jsonl');fs.copyFileSync(f.file,copied);const fixture={...f,file:copied};const {c}=open(fixture);
   if(change==='replace'){fs.renameSync(copied,copied+'.old');fs.copyFileSync(f.file,copied);}
   else if(change==='truncate')fs.truncateSync(copied,1);else{const fd=fs.openSync(copied,'r+');fs.writeSync(fd,Buffer.from('!'),0,1,0);fs.closeSync(fd);}
   const old=sha(fs.readFileSync(copied));code(()=>c.importPage(f.pages[0]),'SOURCE_CHANGED');assert.equal(sha(fs.readFileSync(copied)),old);c.close();
  }
 });
 test('creation never adopts an old/unrelated/partial database or sidecar',()=>{
  for(const suffix of ['','-wal','-shm','-journal']){const o=options();fs.writeFileSync(o.file+suffix,'unknown');code(()=>HistoricalEventStore.open(o),'CACHE_EXISTS');assert.equal(fs.readFileSync(o.file+suffix,'utf8'),'unknown');}
  const o=options();fs.writeFileSync(o.file,'');code(()=>HistoricalEventStore.open({...o,create:false}),'PARTIAL_CACHE_CREATION');
  const u=options();const d=new DatabaseSync(u.file);d.exec('CREATE TABLE unrelated(x TEXT)');d.close();const old=sha(fs.readFileSync(u.file));
  code(()=>HistoricalEventStore.open({...u,create:false}),'SQL_STORAGE_POLICY');assert.equal(sha(fs.readFileSync(u.file)),old);
 });
 test('unrelated schema and changed dependency/context/budgets are never migrated/adopted',()=>{
  const {c,o}=open();c.close();
  for(const modify of [x=>x.source.dependencies.reader='0'.repeat(64),x=>x.source.context.identity.run_id='wrong',x=>x.source.budgets.max_results=2048,x=>x.limits.max_events=1024]){
   const n=clone({...o,create:false});modify(n);assert.throws(()=>HistoricalEventStore.open(n));
  }
  const n=HistoricalEventStore.open({...o,create:false});assert.equal(n.status().event_count,0);n.close();
  const d=new DatabaseSync(o.file);d.exec('CREATE TABLE unknown(x TEXT)');d.close();code(()=>HistoricalEventStore.open({...o,create:false}),'SQL_SCHEMA');
 });
 for(const field of ['job_id','device_id','run_id','module_id','owner','clock_namespace','artifact_sha256'])test('reopen rejects changed '+field+' external binding',()=>{
  const {c,o}=open();c.close();const other=clone({...o,create:false});other.source.context.identity[field]='different';
  assert.throws(()=>HistoricalEventStore.open(other));const valid=HistoricalEventStore.open({...o,create:false});assert.equal(valid.status().event_count,0);valid.close();
 });
 test('accidental valid-JSON metadata corruption is hash-refused; failed child does not repair it',()=>{
  const {c,o}=open();c.close();const d=new DatabaseSync(o.file),s=JSON.parse(d.prepare('SELECT json FROM state').get().json);s.event_count=1;
  d.prepare('UPDATE state SET json=?').run(JSON.stringify(s));d.close();const before=sha(fs.readFileSync(o.file));
  const r=child({mode:'open',options:{...o,create:false}});assert.equal(r.status,2);assert.equal(JSON.parse(r.stdout).code,'CACHE_OPEN_UNCERTAIN');
  assert.equal(sha(fs.readFileSync(o.file)),before);
 });
 test('explicit DB/WAL/recovery reservation and unknown options refuse before file creation',()=>{
  for(const modify of [x=>x.limits.max_sql_wal_bytes=1,x=>x.limits.max_recovery_read_bytes=1,x=>x.limits.max_db_bytes=1,x=>x.resume=true]){
   const o=options();modify(o);assert.throws(()=>HistoricalEventStore.open(o));assert.equal(fs.existsSync(o.file),false);
  }
 });
 test('legacy/reduced module, resume and append bindings have no implicit adoption',()=>{
  for(const change of [x=>x.source.context.capabilities.schema='legacy',x=>x.source.context.capabilities.protocol='other',
   x=>x.source.context.capabilities.whole_job_gpu_owned=false,x=>x.source.context.capabilities.single_low64_window=false,
   x=>x.source.context.capabilities.supported_high_word=9,x=>x.generation=1,x=>x.append=true]){
   const o=options();change(o);assert.throws(()=>HistoricalEventStore.open(o));assert.equal(fs.existsSync(o.file),false);
  }
 });
 test('URI preflight preserves literal spaces/hash/percent in a private cache path',()=>{
  const o=options();o.file=path.join(path.dirname(o.file),'cache # percent%.sqlite');const c=HistoricalEventStore.open(o);c.importPage(f.pages[0]);c.close();
  const n=HistoricalEventStore.open({...o,create:false});assert.equal(n.status().event_count,25);n.close();
 });
 test('hardlink source/cache and parent junction paths are refused before admission',()=>{
  const sourceCopy=path.join(folder(),'original.jsonl');fs.copyFileSync(f.file,sourceCopy);const link=path.join(folder(),'link.jsonl');fs.linkSync(sourceCopy,link);
  const s=options({...f,file:link});code(()=>HistoricalEventStore.open(s),'FILE_IDENTITY');
  const {c,o}=open();c.close();fs.linkSync(o.file,o.file+'.linked');code(()=>HistoricalEventStore.open({...o,create:false}),'FILE_IDENTITY');
  const target=folder(),parent=folder(),junction=path.join(parent,'junction');fs.symlinkSync(target,junction,'junction');
  const j=options();j.file=path.join(junction,'derived.sqlite');code(()=>HistoricalEventStore.open(j),'PARENT_SYMLINK');
 });
 test('normal delta append performs no corpus replay/hydration or event-table SELECT',()=>{
  const {c}=open();c.importPage(f.pages[0]);const prepare=DatabaseSync.prototype.prepare,sql=[];
  DatabaseSync.prototype.prepare=function(text){sql.push(text);return prepare.call(this,text);};
  try{const r=c.importPage(f.pages[1]);assert.equal(r.imported_events,25);
   assert.equal(sql.some(x=>/^SELECT .* FROM events/i.test(x)),false);assert.equal(sql.filter(x=>x.startsWith('INSERT INTO events')).length,1);
   assert.ok(sql.length<=6);measure.second_delta_prepared_statements=sql.length;
  }finally{DatabaseSync.prototype.prepare=prepare;c.close();}
 });
 test('real simultaneous child writer is excluded by native SQLite; owner continues unchanged',()=>{
  const {c,o}=open();c.importPage(f.pages[0]);const before=c.status();const r=child({mode:'open',options:{...o,create:false}});
  assert.equal(r.status,2);assert.equal(JSON.parse(r.stdout).code,'CACHE_BUSY');assert.deepEqual(c.status(),before);c.close();
  const accepted=child({mode:'open',options:{...o,create:false}});assert.equal(accepted.status,0);assert.equal(JSON.parse(accepted.stdout).event_count,25);
 });
 async function cut(mode,overrides={}){
  const o=options(f,overrides);if(mode!=='creation-crash'){const c=HistoricalEventStore.open(o);c.close();}
  const marker=path.join(folder(),'committed.json'),token=randomUUID();
  const config=path.join(folder(),'cut.json');fs.writeFileSync(config,JSON.stringify({mode,options:{...o,create:mode==='creation-crash'},input:f.pages[0],marker,token}));
  const p=spawn(process.execPath,[self,'--private-child',config],{stdio:['ignore','pipe','pipe']});let output='';
  p.stdout.on('data',x=>{output+=x;assert.ok(output.length<=8192);});p.stderr.on('data',()=>{});
  const exit=new Promise(resolve=>p.once('exit',(code,signal)=>resolve({code,signal})));const began=Date.now();
  while(!fs.existsSync(marker)&&p.exitCode===null&&Date.now()-began<5000)await new Promise(r=>setTimeout(r,15));
  try{assert.ok(fs.existsSync(marker),'verified private crash marker: '+output);const proof=JSON.parse(fs.readFileSync(marker,'utf8'));
   assert.equal(proof.pid,p.pid);assert.equal(proof.token,token);assert.equal(proof.phase,['crash','creation-crash'].includes(mode)?'COMMITTED_NOT_RETURNED':'POISONED_NO_ACK');
   const wal=fs.existsSync(o.file+'-wal')?fs.readFileSync(o.file+'-wal'):Buffer.alloc(0);if(['crash','creation-crash','failed-commit'].includes(mode))assert.ok(wal.length>32);assert.equal(output,'');
   p.kill('SIGKILL');const stopped=await exit;assert.notEqual(stopped.code,0);return {o,wal,mode};
  }finally{if(p.exitCode===null)p.kill('SIGKILL');await exit;}
 }
 test('actual child killed after SQL COMMIT before return: reopen keeps rows+cursor exactly once',async()=>{
  const {o,wal}=await cut('crash');const n=HistoricalEventStore.open({...o,create:false});assert.equal(n.status().recovery_wal_read_bytes,wal.length);
  assert.equal(n.status().event_count,25);assert.deepEqual(n.status().next_cursor,f.pages[0].page.next_cursor);assert.equal(n.importPage(f.pages[0]).replayed,true);
  for(const p of f.pages.slice(1))n.importPage(p);assert.deepEqual(all(n),f.rows);n.close();
 });
 test('injected post-COMMIT failure poisons writer, exposes no success, recovery admits only committed evidence',async()=>{
  const {o}=await cut('failed-commit');const n=HistoricalEventStore.open({...o,create:false});assert.equal(n.status().event_count,25);assert.equal(n.importPage(f.pages[0]).replayed,true);n.close();
 });
 test('actual SQLite FULL rolls back the admitted delta; no rows/cursor success escapes',async()=>{
  const {o}=await cut('full',{limits:{...limits,max_db_bytes:32768}});const n=HistoricalEventStore.open({...o,create:false});
  assert.equal(n.status().event_count,0);assert.deepEqual(n.status().next_cursor,start);assert.deepEqual(all(n),[]);n.close();
 });
 test('pre-COMMIT I/O cut leaves uncertainty rather than acknowledging uncommitted rows',async()=>{
  const {o,wal}=await cut('before-commit');const before=fs.readFileSync(o.file);
  if(wal.length){assert.throws(()=>HistoricalEventStore.open({...o,create:false}),e=>['SQL_WAL_UNCOMMITTED_TAIL','SQL_WAL_TRUNCATED'].includes(e.code));
   assert.ok(fs.readFileSync(o.file+'-wal').equals(wal));assert.ok(fs.readFileSync(o.file).equals(before));
  }else{const n=HistoricalEventStore.open({...o,create:false});assert.equal(n.status().event_count,0);assert.deepEqual(n.status().next_cursor,start);n.close();}
 });
 test('unacknowledged partial creation is preserved and refused, never implicitly adopted',async()=>{
  const {o,wal}=await cut('creation-crash');const before=fs.readFileSync(o.file);
  assert.throws(()=>HistoricalEventStore.open({...o,create:false}),e=>['SQL_SCHEMA','PARTIAL_CACHE_CREATION','CACHE_OPEN_REFUSED'].includes(e.code));
  assert.ok(fs.readFileSync(o.file).equals(before));assert.ok(fs.readFileSync(o.file+'-wal').equals(wal));
  code(()=>HistoricalEventStore.open(o),'CACHE_EXISTS');
 });
 test('uncertain partial/corrupt/uncommitted SQLite WAL refuses before native repair, preserves artifacts',async()=>{
  const {o,wal}=await cut('crash');
  const uncommitted=b=>{
   const x=Buffer.from(b),last=x.length-4120;x.writeUInt32BE(0,last+4);
   let a=0,c=0;const word=x.readUInt32BE(0)===0x377f0682?'readUInt32LE':'readUInt32BE';
   const sum=t=>{for(let i=0;i<t.length;i+=8){a=(a+t[word](i)+c)>>>0;c=(c+t[word](i+4)+a)>>>0;}};
   sum(x.subarray(0,24));for(let p=32;p<x.length;p+=4120){sum(x.subarray(p,p+8));sum(x.subarray(p+24,p+4120));x.writeUInt32BE(a,p+16);x.writeUInt32BE(c,p+20);}return x;
  };
  for(const [name,mutate,expected] of [
   ['partial',b=>b.subarray(0,b.length-1),'SQL_WAL_TRUNCATED'],
   ['corrupt',b=>{const x=Buffer.from(b);x[50]^=1;return x;},'SQL_WAL_CHECKSUM'],
   ['unknown',b=>{const x=Buffer.from(b);x[0]=0;return x;},'SQL_WAL_FORMAT'],
   ['uncommitted',uncommitted,'SQL_WAL_UNCOMMITTED_TAIL']
  ]){
   const target=path.join(folder(),name+'.sqlite');fs.copyFileSync(o.file,target);const changed=mutate(wal);fs.writeFileSync(target+'-wal',changed);
   code(()=>HistoricalEventStore.open({...o,file:target,create:false}),expected);assert.ok(fs.readFileSync(target+'-wal').equals(changed));
  }
  // The intact original is still recoverable; refused copies have not stolen locks.
  const n=HistoricalEventStore.open({...o,create:false});n.close();
 });
 test('closed cache rejects commands; reads cannot infer new activity or generations',()=>{
  const {c}=open();c.close();for(const action of [()=>c.query(query),()=>c.status(),()=>c.importPage(f.pages[0])])code(action,'CACHE_CLOSED');
  assert.deepEqual(c.close(),{closed:true});
 });
 test('immutable incumbents and all three actual journal fixtures retain exact byte hashes',()=>{
  for(const [name,digest] of Object.entries(pins))assert.equal(sha(fs.readFileSync(new URL('../tools/'+name,import.meta.url))),digest);
  assert.equal(sha(fs.readFileSync(fixtureFile)),sha(golden));for(const v of [f,done,refused])assert.equal(sha(fs.readFileSync(v.file)),v.sha);
  fs.writeFileSync(path.join(directory,'measurements.json'),JSON.stringify({...measure,limits,pins,fixture_sha256:sha(golden)},null,2));
 });
}
