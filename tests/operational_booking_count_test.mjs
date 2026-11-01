import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {DatabaseSync} from 'node:sqlite';
import {CoveredPausedAncestors,operationalActiveCount} from '../tools/operational-booking-count.mjs';
import {QueuedDispatch,CONSTANT_DEVICE as device} from '../tools/queued-dispatch.mjs';
import {initialAdmissionFromStore} from '../tools/prepared-initial-start-transport.mjs';
import {sessionKeys} from '../tools/mining-session-policy.mjs';

// CLOSED TEMPORARY SQLite only. SOURCE_FINISHED is a synthetic, already caller-
// admitted local state, exactly as in the native CoveredPausedAncestors tests.
// This tests consistency/capacity, NOT source authentication, raw conservation,
// checkpoint loading, physical Ready, native selection or a productive launch.
const unit=10n**15n,first=(10n<<64n)+100n,birth='2026-10-14T00:00:01.123456789Z';
const other='GPU-33f44d5c-91e4-6ab6-1e45-8c93135fd265',prior='d'.repeat(32);
const uid=n=>'11111111-2222-4333-8444-'+String(n).padStart(12,'0');
const run=n=>BigInt(n).toString(16).padStart(32,'0');
const key=s=>String(s.length).padStart(3,'0')+s;
const hash=bytes=>createHash('sha256').update(bytes).digest('hex');
const canonical=value=>Array.isArray(value)?'['+value.map(canonical).join(',')+']':value&&typeof value==='object'?'{'+Object.keys(value).sort().map(k=>JSON.stringify(k)+':'+canonical(value[k])).join(',')+'}':JSON.stringify(value);
const csv='Start *1e15,End *1e15,Status,Searcher\n199900,200000,Open,\n';
const ident=value=>'"'+value.replaceAll('"','""')+'"';
function snapshot(db){
 const schema=db.prepare('SELECT type,name,sql FROM sqlite_master ORDER BY type,name').all();
 const tables=db.prepare("SELECT name FROM sqlite_master WHERE type='table' ORDER BY name").all();
 return JSON.stringify([schema,tables.map(({name})=>{
  const columns=db.prepare('PRAGMA table_info('+ident(name)+')').all().map(c=>ident(c.name));
  const select=columns.flatMap(c=>['typeof('+c+')','hex(CAST('+c+' AS BLOB))']).join(',');
  return [name,db.prepare('SELECT '+select+' FROM '+ident(name)+' ORDER BY rowid').all()];
 })]);
}
function checkpoint(previous,length,index=0){
 const bytes=Buffer.alloc(64);bytes.write('PGNCG001');
 const positions=123n+BigInt(index),tests=9007199254740993n+BigInt(index),gaps=7n+BigInt(index),batches=2n+BigInt(index);
 [100n,10n,length,positions,tests,gaps,batches].forEach((word,i)=>bytes.writeBigUInt64LE(word,8+i*8));
 return {schema:'pgn-source-resume-1',device:device.toLowerCase(),resume:{run:previous,start:String(first),end:String(first+length),length:String(length),positions:String(positions),nextStart:String(first+positions),remaining:String(length-positions),tests:String(tests),gaps:String(gaps),batches:String(batches),counterTimeNs:String(9007199254740995n+BigInt(index)),checkpointLabel:'C:/PUBLIC-FIXTURE/current-job/checkpoint',checkpointSha256:hash(bytes)},checkpoint_base64:bytes.toString('base64')};
}
function fixture({epochs=1,length=unit,finished=true,predecessor=prior}={}){
 const directory=fs.mkdtempSync(path.join(os.tmpdir(),'pgn-covered-paused-')),file=path.join(directory,'local-bookings.sqlite');
 const db=new DatabaseSync(file);
 db.exec(`CREATE TABLE bookings(id TEXT PRIMARY KEY,start TEXT,end TEXT,start_key TEXT,end_key TEXT,state TEXT,kind TEXT,device TEXT,created_utc TEXT);
 CREATE TABLE booking_events(id TEXT,state TEXT);
 CREATE TRIGGER history_update AFTER UPDATE ON bookings BEGIN INSERT INTO booking_events VALUES(NEW.id,NEW.state);END;
 CREATE TABLE fixture_source_refs(run TEXT PRIMARY KEY,source BLOB,birth TEXT);
 CREATE TABLE dispatch_resume_epochs(run TEXT PRIMARY KEY,previous_run TEXT NOT NULL UNIQUE,booking_id TEXT NOT NULL,request_id TEXT NOT NULL UNIQUE,receipt TEXT NOT NULL);`);
 let queue=new QueuedDispatch(file);const keys=sessionKeys(device),booking=uid(1),runs=Array.from({length:epochs+1},(_,i)=>run(i+1));
 db.prepare('INSERT INTO booking_metadata VALUES(?,?)').run(keys.mode,'automatic');
 db.prepare('INSERT INTO booking_metadata VALUES(?,?)').run(keys.preference,'0');
 db.prepare('INSERT INTO booking_metadata VALUES(?,?)').run('fixture/unrelated-age','KEEP-ORIGINAL-AGE');
 const add=(id,start,end,state='QUEUED',kind='LOCAL_INTENT',target=device,date=birth)=>db.prepare('INSERT INTO bookings VALUES(?,?,?,?,?,?,?,?,?)').run(id,String(start),String(end),key(String(start)),key(String(end)),state,kind,target,date);
 const receipts=[];
 for(let i=1;i<runs.length;++i){
  const receipt={schema:'pgn-checkpoint-resume-epoch-1',origin:'RESUME_CHECKPOINT',request_id:uid(100+i),device,booking_id:booking,previous_run:runs[i-1],run:runs[i],observation_id:uid(1000+i),predecessor,counter_basis:'PGNCG001_CUMULATIVE_BASELINE',raw_conservation_requirement:'LINEAGE_RAW_CONSERVATION_REQUIRED',checkpoint:checkpoint(runs[i-1],length,i-1)};
  receipts.push(receipt);db.prepare('INSERT INTO dispatch_resume_epochs VALUES(?,?,?,?,?)').run(receipt.run,receipt.previous_run,booking,receipt.request_id,canonical(receipt));
  db.prepare('INSERT INTO booking_metadata VALUES(?,?)').run('mining-resume/v1/'+device.toLowerCase()+'/'+receipt.previous_run,canonical(receipt.checkpoint));
 }
 const manifests=[];
 for(let i=0;i<runs.length;++i){
  const current=runs[i],manifest={schema:'pgn-native-process-1',run:current,uuid:device,pid:1234+i,birth,start:String(first),end:String(first+length),queued_intent:booking,stdout:'C:/PUBLIC-FIXTURE/'+current+'.stdout',stderr:'C:/PUBLIC-FIXTURE/'+current+'.stderr',stop:'C:/PUBLIC-FIXTURE/stop.request',pci:'0000:01:00.0',coverage:'SOURCE_PRP_INDEPENDENT_PROOF_PENDING',proof:{independent_coverage:false}};
  if(i){manifest.resume_previous_run=runs[i-1];manifest.checkpoint_input_sha256=receipts[i-1].checkpoint.resume.checkpointSha256;}
  manifests.push(manifest);db.prepare('INSERT INTO booking_metadata VALUES(?,?)').run('mining-source-manifest/v1/'+current,canonical(manifest));
  add(current,first,first+length,i===runs.length-1&&finished?'SOURCE_FINISHED':'PAUSED','RUNTIME');
  db.prepare('INSERT INTO fixture_source_refs VALUES(?,?,?)').run(current,Buffer.from('TEST_ONLY_IMMUTABLE_SOURCE_REFERENCE/'+current),birth);
 }
 add(booking,first,first+length,finished?'SOURCE_FINISHED':'DISPATCHED');
 db.prepare('INSERT INTO dispatch_requests VALUES(?,?,?,?,?,?,?,?)').run(booking,String(first),String(first+length),device,finished?'SOURCE_FINISHED':'STARTED',predecessor,runs.at(-1),birth);
 function fillers(count){
  db.exec('BEGIN');try{for(let i=0;i<count;++i)add('capacity-'+i,BigInt(i*2),BigInt(i*2+1),'QUEUED','LOCAL_INTENT',other,'ORIGINAL-FILLER-AGE');db.exec('COMMIT');}catch(e){db.exec('ROLLBACK');throw e;}
 }
 return {db,file,directory,booking,runs,receipts,manifests,add,fillers,keys,length,
  get queue(){return queue;},binding:{uuid:device,start:String(first),end:String(first+length),length:String(length),queued_intent:booking},
  count(){return queue.transaction(()=>operationalActiveCount(queue.db));},
  historical(){return snapshot(db);},
  putManifest(index,value){db.prepare('UPDATE booking_metadata SET value=? WHERE key=?').run(typeof value==='string'||value instanceof Uint8Array?value:canonical(value),'mining-source-manifest/v1/'+runs[index]);},
  putEpoch(index,value){db.prepare('UPDATE dispatch_resume_epochs SET receipt=? WHERE run=?').run(typeof value==='string'||value instanceof Uint8Array?value:canonical(value),runs[index+1]);},
  reopen(){queue.close();queue=new QueuedDispatch(file);},
  dispose(){queue.close();db.close();const actual=fs.realpathSync(directory),parent=fs.realpathSync(os.tmpdir());assert.equal(path.dirname(actual),parent);assert.ok(path.basename(actual).startsWith('pgn-covered-paused-'));fs.rmSync(actual,{recursive:true,force:true});}
 };
}

test('A PAUSED -> B resumed SOURCE is operationally covered, not rewritten/released/deleted; reopen and exact typed history retained',()=>{
 for(const predecessor of [null,prior]){
  const f=fixture({predecessor});try{
   f.reopen();const before=f.historical(),changes=f.db.prepare('SELECT total_changes() n').get().n;
   f.queue.transaction(()=>{const coverage=new CoveredPausedAncestors(f.queue.db);assert.equal(coverage.contains(f.runs[0],device),true);assert.equal(coverage.contains(f.runs[0],device),true);});
   assert.equal(f.count(),0);assert.equal(f.historical(),before);assert.equal(f.db.prepare('SELECT total_changes() n').get().n,changes);
   assert.equal(f.db.prepare('SELECT state FROM bookings WHERE id=?').get(f.runs[0]).state,'PAUSED');
   assert.equal(f.db.prepare('SELECT state FROM bookings WHERE id=?').get(f.runs[1]).state,'SOURCE_FINISHED');
   assert.throws(()=>f.queue.arm(f.booking,f.binding),/Only queued/);
  }finally{f.dispose();}
 }
});

test('iterative all-manifest chain has no arbitrary depth quota; completed predecessor is not resume ancestry',()=>{
 const f=fixture({epochs:257});try{
  const before=f.historical();assert.equal(f.count(),0);assert.equal(f.historical(),before);
  f.queue.transaction(()=>{const coverage=new CoveredPausedAncestors(f.queue.db);for(const current of f.runs.slice(0,-1))assert.equal(coverage.contains(current,device),true);assert.equal(coverage.contains(prior,device),false);});
 }finally{f.dispose();}
});

test('covered history permits the existing finite split at4096, preserves manual order, derivation/history and PREPARED replay identity',()=>{
 const f=fixture();try{
  f.fillers(4094);const a=199900n*unit,id=uid(20),later=uid(21);f.add(id,a,a+2n*unit);f.add(later,a+3n*unit,a+4n*unit);
  assert.equal(f.count(),4096);const old=f.db.prepare('SELECT * FROM bookings WHERE id=?').get(f.runs[0]);
  const prepared=f.queue.prepareAtBoundary(f.runs.at(-1),f.binding,csv);
  assert.equal(prepared.start,String(a));assert.equal(prepared.end,String(a+unit));assert.equal(prepared.state,'PREPARED');
  assert.equal(f.count(),4097);assert.equal(f.db.prepare('SELECT state FROM bookings WHERE id=?').get(id).state,'RELEASED');
  assert.equal(f.db.prepare('SELECT count(*) n FROM booking_derivations WHERE parent_id=?').get(id).n,2);
  assert.equal(f.db.prepare('SELECT state FROM bookings WHERE id=?').get(later).state,'QUEUED');
  assert.deepEqual(f.db.prepare('SELECT * FROM bookings WHERE id=?').get(f.runs[0]),old);
  const after=f.historical();assert.equal(f.queue.prepareAtBoundary(f.runs.at(-1),f.binding,'not CSV').id,prepared.id);assert.equal(f.historical(),after);
  f.reopen();assert.equal(f.queue.prepareAtBoundary(f.runs.at(-1),f.binding,'not CSV').id,prepared.id);assert.equal(f.historical(),after);
 }finally{f.dispose();}
});

test('covered ancestor permits Auto allocation at4096; first free block, occupancy and at4097 replay are unchanged',()=>{
 const f=fixture();try{
  f.fillers(4096);assert.equal(f.count(),4096);const old=f.db.prepare('SELECT * FROM bookings WHERE id=?').get(f.runs[0]);
  const prepared=f.queue.prepareAtBoundary(f.runs.at(-1),f.binding,csv);
  assert.equal(prepared.start,String(199900n*unit));assert.equal(prepared.end,String(199901n*unit));assert.equal(prepared.automatic,true);assert.equal(f.count(),4097);
  const after=f.historical();assert.equal(f.queue.prepareAtBoundary(f.runs.at(-1),f.binding,csv).id,prepared.id);assert.equal(f.historical(),after);
  assert.throws(()=>f.queue.prepareAtBoundary(run(9999),f.binding,csv),/Uncertain/);assert.equal(f.historical(),after);
  assert.deepEqual(f.db.prepare('SELECT * FROM bookings WHERE id=?').get(f.runs[0]),old);
 }finally{f.dispose();}
 const f2=fixture();try{
  // Ancestors remain range-occupying even after they leave the quota.
  const id=uid(30);f2.add(id,first,first+unit);const before=f2.historical();
  assert.throws(()=>f2.queue.arm(id,f2.binding),/overlaps/);assert.equal(f2.historical(),before);
  f2.db.prepare("UPDATE bookings SET state='RELEASED' WHERE id=?").run(id);
  const occupiedCsv='Start *1e15,End *1e15,Status,Searcher\n184467,184472,Open,\n';
  const next=f2.queue.prepareAtBoundary(f2.runs.at(-1),f2.binding,occupiedCsv);
  assert.equal(next.start,String(184469n*unit));assert.equal(next.end,String(184470n*unit));
  assert.equal(f2.db.prepare('SELECT state FROM bookings WHERE id=?').get(f2.runs[0]).state,'PAUSED');
 }finally{f2.dispose();}
});

test('both allocation consumers refuse a full4097 operational file atomically, including rollback of legacy lookahead retirement',()=>{
 for(const manual of [false,true]){
  const f=fixture();try{
   f.fillers(manual?4096:4097);if(manual)f.add(uid(40),199900n*unit,199902n*unit);
   const before=f.historical();assert.equal(f.count(),4097);
   assert.throws(()=>f.queue.prepareAtBoundary(f.runs.at(-1),f.binding,csv),/active-entry bound/);assert.equal(f.historical(),before);
  }finally{f.dispose();}
 }
 const f=fixture();try{
  f.fillers(4097);const legacy=uid(41);f.add(legacy,199950n*unit,199951n*unit,'ARMED');
  f.db.prepare('INSERT INTO automatic_next VALUES(?,?,?)').run(run(7000),legacy,birth);
  f.db.prepare("INSERT INTO dispatch_requests VALUES(?,?,?,?,'ARMED',NULL,NULL,?)").run(legacy,String(199950n*unit),String(199951n*unit),device,birth);
  const before=f.historical();assert.throws(()=>f.queue.prepareAtBoundary(f.runs.at(-1),f.binding,csv),/active-entry bound/);
  assert.equal(f.historical(),before);assert.equal(f.db.prepare('SELECT state FROM bookings WHERE id=?').get(legacy).state,'ARMED');
 }finally{f.dispose();}
});

test('unfinished resume ancestors remain counted and visible; they cannot buy capacity for a new allocation',()=>{
 const f=fixture({finished:false});try{
  assert.equal(f.count(),3);f.fillers(4094);const before=f.historical();
  f.queue.transaction(()=>assert.equal(new CoveredPausedAncestors(f.queue.db).contains(f.runs[0],device),false));
  assert.throws(()=>f.queue.prepareAtBoundary(run(8000),f.binding,csv),/active-entry bound/);assert.equal(f.historical(),before);
  assert.deepEqual(f.db.prepare("SELECT state FROM bookings WHERE kind='RUNTIME' ORDER BY id").all().map(r=>r.state),['PAUSED','PAUSED']);
 }finally{f.dispose();}
});

test('an unrelated PAUSED run stays operational; an ACTIVE intermediate ancestor cannot be covered; SQL failure rolls back the whole split',()=>{
 const unrelated=fixture();try{
  unrelated.add(run(9000),first,first+unit,'PAUSED','RUNTIME');assert.equal(unrelated.count(),1);unrelated.fillers(4096);
  const before=unrelated.historical();assert.throws(()=>unrelated.queue.prepareAtBoundary(unrelated.runs.at(-1),unrelated.binding,csv),/active-entry bound/);
  assert.equal(unrelated.historical(),before);assert.equal(unrelated.db.prepare('SELECT state FROM bookings WHERE id=?').get(run(9000)).state,'PAUSED');
 }finally{unrelated.dispose();}
 const chain=fixture({epochs:2});try{
  chain.db.prepare("UPDATE bookings SET state='ACTIVE' WHERE id=?").run(chain.runs[1]);const before=chain.historical();
  assert.throws(()=>chain.count(),/exactly PAUSED/);assert.equal(chain.historical(),before);
 }finally{chain.dispose();}
 const rollback=fixture();try{
  const parent=uid(44);rollback.add(parent,199900n*unit,199902n*unit);
  rollback.db.exec("CREATE TRIGGER deny_new_intent AFTER INSERT ON bookings BEGIN SELECT RAISE(FAIL,'fixture commit refusal');END;");
  const before=rollback.historical();assert.throws(()=>rollback.queue.prepareAtBoundary(rollback.runs.at(-1),rollback.binding,csv),/fixture commit refusal/);
  assert.equal(rollback.historical(),before);assert.equal(rollback.db.prepare('SELECT state FROM bookings WHERE id=?').get(parent).state,'QUEUED');
 }finally{rollback.dispose();}
});

test('absent epochs/manifests, current physical/local mismatch, cycles and immutable receipt corruption never hide historical PAUSED rows',()=>{
 const poisons=[
  f=>f.db.exec('DROP TABLE dispatch_resume_epochs'),
  f=>f.db.prepare('DELETE FROM booking_metadata WHERE key=?').run('mining-source-manifest/v1/'+f.runs[0]),
  f=>f.db.prepare('DELETE FROM booking_metadata WHERE key=?').run('mining-source-manifest/v1/'+f.runs[1]),
  f=>f.putManifest(0,canonical(f.manifests[0])+' '),
  f=>f.putManifest(1,Buffer.from(canonical(f.manifests[1]))),
  f=>f.putManifest(1,{...f.manifests[1],birth:birth+'\n'}),
  f=>f.putManifest(1,{...f.manifests[1],pid:0}),
  f=>f.putManifest(1,{...f.manifests[1],pid:true}),
  f=>f.putManifest(1,{...f.manifests[1],schema:'foreign-process'}),
  f=>f.putManifest(1,{...f.manifests[1],uuid:other}),
  f=>f.putManifest(1,{...f.manifests[1],checkpoint_input_sha256:'0'.repeat(64)}),
  f=>{const m={...f.manifests[1]};delete m.resume_previous_run;f.putManifest(1,m);},
  f=>f.putEpoch(0,canonical(f.receipts[0])+'\n'),
  f=>f.putEpoch(0,Buffer.from(canonical(f.receipts[0]))),
  f=>f.putEpoch(0,{...f.receipts[0],predecessor:null}),
  f=>{const e=structuredClone(f.receipts[0]);e.checkpoint.resume.checkpointSha256='0'.repeat(64);f.putEpoch(0,e);},
  f=>f.db.exec('UPDATE dispatch_resume_epochs SET run=CAST(run AS BLOB)'),
  f=>f.db.exec('ALTER TABLE dispatch_resume_epochs RENAME TO hidden_epochs;CREATE VIEW dispatch_resume_epochs AS SELECT * FROM hidden_epochs;'),
  f=>f.db.prepare('DELETE FROM bookings WHERE id=?').run(f.runs[1]),
  f=>f.db.prepare("UPDATE bookings SET device=? WHERE id=?").run(other,f.runs[1]),
  f=>f.db.prepare("UPDATE bookings SET start_key='BAD' WHERE id=?").run(f.runs[1]),
  f=>f.db.prepare("UPDATE bookings SET state='DISPATCHED' WHERE id=?").run(f.booking),
  f=>f.db.prepare("UPDATE bookings SET kind='LOCAL_INTENT' WHERE id=?").run(f.runs[1]),
  f=>f.db.prepare("UPDATE dispatch_requests SET run=? WHERE id=?").run(run(999),f.booking),
  f=>f.db.prepare("UPDATE bookings SET state=CAST('PAUSED' AS BLOB) WHERE id=?").run(f.runs[0]),
  f=>f.db.prepare("UPDATE bookings SET state='UNKNOWN' WHERE id=?").run(f.runs[0]),
  f=>f.db.prepare('INSERT INTO booking_metadata VALUES(?,?)').run(Buffer.from('mining-source-manifest/v1/'+run(909)),'{}'),
  f=>{
   const e=structuredClone(f.receipts[0]);e.run=f.runs[0];e.previous_run=f.runs[1];e.request_id=uid(999);e.checkpoint.resume.run=f.runs[1];
   f.db.prepare('INSERT INTO dispatch_resume_epochs VALUES(?,?,?,?,?)').run(e.run,e.previous_run,e.booking_id,e.request_id,canonical(e));
   f.putManifest(0,{...f.manifests[0],resume_previous_run:f.runs[1],checkpoint_input_sha256:e.checkpoint.resume.checkpointSha256});
  }
 ];
 for(const poison of poisons){const f=fixture();try{
  poison(f);f.fillers(4096);const before=f.historical();
  assert.throws(()=>f.queue.prepareAtBoundary(f.runs.at(-1),f.binding,csv));assert.equal(f.historical(),before);
  assert.equal(f.db.prepare('SELECT count(*) n FROM bookings WHERE id=?').get(f.runs[0]).n,1);
 }finally{f.dispose();}}
});

test('Native-style committed INITIAL receipt remains transport-admissible after covered history; Node never invents native Ready/selection',()=>{
 const f=fixture();try{
  assert.equal(f.count(),0);const old=f.db.prepare('SELECT * FROM bookings WHERE id=?').get(f.runs[0]);
  // Synthetic native writer DTO, not invoking or qualifying native selection.
  const request=uid(800),observation=uid(801),booking=uid(802),a=String(199900n*unit),b=String(199901n*unit);
  const scope={execution_start:a,execution_end:String(200000n*unit),auto_start:a,auto_end:String(200000n*unit),block_anchor:'0',block_width:String(unit)};
  const registry={url:'https://PUBLIC-FIXTURE.invalid/ranges',sha256:'e'.repeat(64),observed_utc:'2026-10-14T00:00:00.000Z',semantic_digest:'f'.repeat(64)};
  const fields=['execution_start','execution_end','auto_start','auto_end','block_anchor','block_width'];
  const receipt={schema:'pgn-initial-start-receipt-1',origin:'INITIAL_START',request_id:request,device,observation_id:observation,mode:'automatic',booking_id:booking,start:a,end:b,scope,registry_provenance:registry};
  receipt.input_fingerprint=hash(JSON.stringify([request,device.toLowerCase(),...fields.map(k=>scope[k]),observation,registry.semantic_digest])+'\n');
  f.add(booking,BigInt(a),BigInt(b),'DISPATCHING');f.db.prepare("INSERT INTO dispatch_requests VALUES(?,?,?,?,'PREPARED',NULL,NULL,?)").run(booking,a,b,device,birth);
  f.db.prepare('INSERT INTO booking_metadata VALUES(?,?)').run('mining-initial-start/v1/'+request,canonical(receipt));
  const before=f.historical(),command={action:'start_prepared',request_id:request,device,observation_id:observation,base_run:f.runs.at(-1),base_birth:birth,base_start:f.binding.start,base_end:f.binding.end};
  const result=initialAdmissionFromStore(f.queue.db,command);assert.deepEqual(result.admission.receipt,receipt);assert.equal(f.historical(),before);
  assert.equal(f.count(),1);assert.deepEqual(f.db.prepare('SELECT * FROM bookings WHERE id=?').get(f.runs[0]),old);
  assert.equal(f.db.prepare('SELECT state FROM dispatch_requests WHERE id=?').get(booking).state,'PREPARED');
 }finally{f.dispose();}
});
