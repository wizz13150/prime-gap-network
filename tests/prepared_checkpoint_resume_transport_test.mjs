import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {DatabaseSync} from 'node:sqlite';
import {PreparedCheckpointResumeTransport,resumeAdmissionFromStore} from '../tools/prepared-checkpoint-resume-transport.mjs';
import {ApplicationCloseProtocol} from '../tools/application-close-protocol.mjs';
import {SerializedMutations} from '../tools/serialized-mutations.mjs';
import {legacyNativeDevice as device,finiteNativeDevice} from '../tools/native-device-target.mjs';

// CLOSED: native-format immutable receipts in own temporary SQLite; fake send
// callbacks only. No live-client import, child process, network, runtime or GPU.
const ids={request:'12345678-1234-4234-8234-123456789abc',observation:'11111111-2222-4333-8444-555555555555',booking:'87654321-4321-4321-8321-cba987654321',old:'a'.repeat(32),run:'b'.repeat(32)};
const birth='2026-10-14T00:00:00.1234567Z',predecessor='c'.repeat(32);
const canonical=value=>value&&typeof value==='object'?'{'+Object.keys(value).sort().map(key=>JSON.stringify(key)+':'+canonical(value[key])).join(',')+'}':JSON.stringify(value);
const hash=bytes=>createHash('sha256').update(bytes).digest('hex');
const rangeKey=value=>String(value.length).padStart(3,'0')+value;
const modeKey='mining-session-mode/v1/'+device.toLowerCase(),preferenceKey='mining-auto-after-manual/v1/'+device.toLowerCase();
function checkpoint(old=ids.old,label='C:/PUBLIC-FIXTURE/current-job/checkpoint'){
 const low=100n,high=10n,length=10000n,positions=123n,tests=9007199254740993n,gaps=7n,batches=2n,bytes=Buffer.alloc(64);bytes.write('PGNCG001',0,'ascii');
 [low,high,length,positions,tests,gaps,batches].forEach((word,index)=>bytes.writeBigUInt64LE(word,8+index*8));
 const start=(high<<64n)+low;
 return {schema:'pgn-source-resume-1',device:device.toLowerCase(),resume:{run:old,start:String(start),end:String(start+length),length:String(length),positions:String(positions),nextStart:String(start+positions),remaining:String(length-positions),tests:String(tests),gaps:String(gaps),batches:String(batches),counterTimeNs:'9007199254740995',checkpointLabel:label,checkpointSha256:hash(bytes)},checkpoint_base64:bytes.toString('base64')};
}
function receipt(which=ids,prior=null,label){return {schema:'pgn-checkpoint-resume-epoch-1',origin:'RESUME_CHECKPOINT',request_id:which.request,device,booking_id:which.booking,previous_run:which.old,run:which.run,observation_id:which.observation,predecessor:prior,counter_basis:'PGNCG001_CUMULATIVE_BASELINE',raw_conservation_requirement:'LINEAGE_RAW_CONSERVATION_REQUIRED',checkpoint:checkpoint(which.old,label)};}
function command(r){return {action:'resume_prepared',device,request_id:r.request_id,observation_id:r.observation_id,booking_id:r.booking_id,old_run:r.previous_run,new_run:r.run,base_birth:birth,base_start:r.checkpoint.resume.start,base_end:r.checkpoint.resume.end};}
function fixture(prior=null,mode='manual-reservations'){
 const directory=fs.mkdtempSync(path.join(os.tmpdir(),'pgn-checkpoint-resume-')),file=path.join(directory,'local-bookings.sqlite'),db=new DatabaseSync(file);
 db.exec('CREATE TABLE booking_metadata(key TEXT PRIMARY KEY,value TEXT NOT NULL);CREATE TABLE bookings(id TEXT PRIMARY KEY,start TEXT,end TEXT,start_key TEXT,end_key TEXT,state TEXT,kind TEXT,device TEXT,created_utc TEXT);CREATE TABLE dispatch_requests(id TEXT PRIMARY KEY,start TEXT,end TEXT,device TEXT,state TEXT,predecessor TEXT,run TEXT,created_utc TEXT);CREATE TABLE dispatch_resume_epochs(run TEXT PRIMARY KEY,previous_run TEXT NOT NULL UNIQUE,booking_id TEXT NOT NULL,request_id TEXT NOT NULL UNIQUE,receipt TEXT NOT NULL)');
 db.prepare('INSERT INTO booking_metadata VALUES(?,?)').run(modeKey,mode);db.prepare('INSERT INTO booking_metadata VALUES(?,?)').run(preferenceKey,'0');db.prepare('INSERT INTO booking_metadata VALUES(?,?)').run('fixture/unrelated','KEEP');
 let current;
 function stage(r){
  if(current){db.prepare("UPDATE dispatch_requests SET state='SOURCE_FINISHED' WHERE id=?").run(current.booking_id);db.prepare("UPDATE bookings SET state='SOURCE_FINISHED' WHERE id=?").run(current.booking_id);}
  const s=r.checkpoint.resume;
  db.prepare('INSERT INTO booking_metadata VALUES(?,?)').run('mining-resume/v1/'+device.toLowerCase()+'/'+r.previous_run,canonical(r.checkpoint));
  db.prepare('INSERT INTO dispatch_resume_epochs VALUES(?,?,?,?,?)').run(r.run,r.previous_run,r.booking_id,r.request_id,canonical(r));
  for(const [id,state,kind]of[[r.previous_run,'PAUSED','RUNTIME'],[r.booking_id,'DISPATCHING','LOCAL_INTENT']])db.prepare('INSERT INTO bookings VALUES(?,?,?,?,?,?,?,?,?)').run(id,s.start,s.end,rangeKey(s.start),rangeKey(s.end),state,kind,device,'ORIGINAL-UNCHANGED');
  db.prepare("INSERT INTO dispatch_requests VALUES(?,?,?,?,'PREPARED',?,?,'ORIGINAL-UNCHANGED')").run(r.booking_id,s.start,s.end,device,r.predecessor,r.run);current=r;
 }
 const original=receipt(ids,prior);stage(original);
 const writer=new SerializedMutations(),close=new ApplicationCloseProtocol({mutations:writer});
 return {db,file,receipt:original,stage,writer,close,command:()=>command(original),
  put:r=>db.prepare('UPDATE dispatch_resume_epochs SET receipt=? WHERE run=?').run(canonical(r),original.run),
  saved:r=>db.prepare('UPDATE booking_metadata SET value=? WHERE key=?').run(canonical(r.checkpoint),'mining-resume/v1/'+device.toLowerCase()+'/'+original.previous_run),
  transport:send=>new PreparedCheckpointResumeTransport({db,runMutation:op=>close.run('resume_prepared',op),send}),
  sha:()=>hash(fs.readFileSync(file)),changes:()=>db.prepare('SELECT total_changes() n').get().n,
  dispose(){db.close();const actual=fs.realpathSync(directory),parent=fs.realpathSync(os.tmpdir());assert.equal(path.dirname(actual),parent);assert.ok(path.basename(actual).startsWith('pgn-checkpoint-resume-'));fs.rmSync(actual,{recursive:true,force:true});}};
}
async function rejected(f,mutateCommand=()=>{},send=async()=>{throw Error('must not send')}){
 const c=f.command();mutateCommand(c);let calls=0;const before=f.sha(),changes=f.changes();
 await assert.rejects(f.transport(async(...args)=>{++calls;return send(...args)}).submit(c));assert.equal(calls,0);assert.equal(f.sha(),before);assert.equal(f.changes(),changes);
}

test('reads exact committed resume epoch before one send, preserves original range/cumulative >2^53 baseline, never claims ownership or STARTED',async()=>{
 for(const prior of [null,predecessor])for(const mode of ['automatic','manual-reservations']){
  const f=fixture(prior,mode);try{
   const before=f.sha(),changes=f.changes();let calls=0;
   const transport=f.transport(async(encoded,admission)=>{
    ++calls;const raw=Buffer.from(encoded,'base64');assert.equal(raw.toString('base64'),encoded);assert.ok(raw.length<4096&&raw.length<=8192);assert.equal(raw.toString('utf8'),canonical(admission));assert.deepEqual(admission.receipt,f.receipt);
    assert.deepEqual(admission.base,{base_run:ids.old,base_birth:birth,base_start:f.receipt.checkpoint.resume.start,base_end:f.receipt.checkpoint.resume.end});
    assert.equal(admission.receipt.checkpoint.resume.tests,'9007199254740993');assert.equal(admission.receipt.checkpoint.resume.counterTimeNs,'9007199254740995');assert.equal(admission.receipt.predecessor,prior);
    assert.equal(f.db.prepare('SELECT receipt FROM dispatch_resume_epochs WHERE run=?').get(ids.run).receipt,canonical(f.receipt));
    assert.deepEqual({...f.db.prepare('SELECT state,predecessor,run FROM dispatch_requests WHERE id=?').get(ids.booking)},{state:'PREPARED',predecessor:prior,run:ids.run});
    assert.equal(f.db.prepare('SELECT state FROM bookings WHERE id=?').get(ids.old).state,'PAUSED');assert.ok(Object.isFrozen(admission)&&Object.isFrozen(admission.receipt.checkpoint.resume));
    return {status:'START_REQUESTED',request_id:ids.request};
   });
   assert.equal((await transport.submit(f.command())).status,'START_REQUESTED');assert.equal(calls,1);assert.equal(f.sha(),before);assert.equal(f.changes(),changes);
   assert.equal(f.db.prepare('SELECT state FROM dispatch_requests WHERE id=?').get(ids.booking).state,'PREPARED');
   assert.equal(f.db.prepare('SELECT value FROM booking_metadata WHERE key=?').get(modeKey).value,mode);assert.equal(f.db.prepare('SELECT value FROM booking_metadata WHERE key=?').get(preferenceKey).value,'0');
  }finally{f.dispose();}
 }
});

test('strict command correlation, original base range and canonical UTC reject unknown/malformed inputs before send',async()=>{
 const poisons=[c=>{c.action='start_prepared'},c=>{c.action='resume'},c=>{c.request_id=ids.observation},c=>{c.request_id=ids.request.toUpperCase()},c=>{c.observation_id=ids.request},c=>{c.booking_id=ids.request},c=>{c.old_run=ids.run},c=>{c.new_run=ids.old},c=>{c.old_run=null},c=>{c.new_run=true},c=>{c.device=finiteNativeDevice},c=>{c.base_start=Number(c.base_start)},c=>{c.base_end=c.base_start},c=>{c.base_start='01'},c=>{c.base_end='184467440737095526261'},c=>{delete c.base_birth},c=>{c.base_birth=null},c=>{c.base_birth=birth+'\n'},c=>{c.base_birth='2026-02-30T00:00:00Z'},c=>{c.base_birth='0000-01-01T00:00:00Z'},c=>{c.base_birth='2026-10-14T00:00:00+00:00'},c=>{c.ready=true}];
 for(const poison of poisons){const f=fixture();try{await rejected(f,poison);}finally{f.dispose();}}
});

test('native receipt keys/schema/origin/new epoch/typed strings and predecessor are exact, never repaired',async()=>{
 const poisons=[r=>{r.extra='unknown'},r=>{delete r.predecessor},r=>{r.schema='pgn-initial-start-receipt-1'},r=>{r.origin='INITIAL_START'},r=>{r.counter_basis='NEW_DELTA'},r=>{r.raw_conservation_requirement='ALREADY_CONSERVED'},r=>{r.request_id=ids.observation},r=>{r.observation_id=ids.request},r=>{r.device=device.toLowerCase()},r=>{r.device=finiteNativeDevice},r=>{r.booking_id=ids.request},r=>{r.previous_run=ids.run},r=>{r.run=ids.old},r=>{r.predecessor=false},r=>{r.predecessor=ids.old+'\n'},r=>{r.checkpoint=null}];
 for(const poison of poisons){const f=fixture();try{const r=structuredClone(f.receipt);poison(r);f.put(r);await rejected(f);}finally{f.dispose();}}
 for(const form of ['pretty','duplicate','trailing','blob','invalid-utf8']){
  const f=fixture();try{
   let raw=canonical(f.receipt);if(form==='pretty')raw=JSON.stringify(f.receipt,null,2);if(form==='duplicate')raw='{"schema":"wrong",'+raw.slice(1);if(form==='trailing')raw+='\n';if(form==='blob')raw=Buffer.from(raw);
   if(form==='invalid-utf8')f.db.exec("UPDATE dispatch_resume_epochs SET receipt=CAST(x'ff' AS TEXT)");else f.db.prepare('UPDATE dispatch_resume_epochs SET receipt=?').run(raw);await rejected(f);
  }finally{f.dispose();}
 }
});

test('all13 checkpoint strings, canonical64 bytes/SHA, current high10 layout and cumulative arithmetic are fail-closed',async()=>{
 const poisons=[c=>{c.extra='unknown'},c=>{c.device=device},c=>{c.schema='unknown'},c=>{c.resume.extra='unknown'},c=>{c.resume.run=ids.run},c=>{c.resume.start='01'},c=>{c.resume.end=c.resume.start},c=>{c.resume.length='0'},c=>{c.resume.positions=c.resume.length},c=>{c.resume.nextStart=c.resume.start},c=>{c.resume.remaining=c.resume.length},c=>{c.resume.tests='9007199254740992'},c=>{c.resume.gaps='8'},c=>{c.resume.batches='3'},c=>{c.resume.counterTimeNs='18446744073709551616'},c=>{c.resume.checkpointLabel=''},c=>{c.resume.checkpointLabel+='\n'},c=>{c.resume.checkpointLabel+='\0'},c=>{c.resume.checkpointLabel='\ud800'},c=>{c.resume.checkpointSha256='0'.repeat(64)},c=>{c.checkpoint_base64+='\n'},c=>{c.checkpoint_base64=c.checkpoint_base64.slice(0,-1)},c=>{c.checkpoint_base64=Buffer.alloc(63).toString('base64')},c=>{const b=Buffer.from(c.checkpoint_base64,'base64');b[0]=88;c.checkpoint_base64=b.toString('base64');c.resume.checkpointSha256=hash(b)},c=>{const b=Buffer.from(c.checkpoint_base64,'base64');b.writeBigUInt64LE(11n,16);c.checkpoint_base64=b.toString('base64');c.resume.checkpointSha256=hash(b)}];
 for(const name of Object.keys(checkpoint().resume))poisons.push(c=>{c.resume[name]=false});
 for(const poison of poisons){const f=fixture();try{const r=structuredClone(f.receipt);poison(r.checkpoint);f.put(r);await rejected(f);}finally{f.dispose();}}
 const f=fixture();try{const r=structuredClone(f.receipt),s=r.checkpoint.resume,b=Buffer.from(r.checkpoint.checkpoint_base64,'base64');
  // Self-consistent tuple/bytes still cannot extend beyond the native low64
  // profile; this poison must not pass merely because its SHA was recomputed.
  const low=(1n<<64n)-2n,start=(10n<<64n)+low;b.writeBigUInt64LE(low,8);b.writeBigUInt64LE(5n,24);b.writeBigUInt64LE(0n,32);
  Object.assign(s,{start:String(start),end:String(start+5n),length:'5',positions:'0',nextStart:String(start),remaining:'5',checkpointSha256:hash(b)});r.checkpoint.checkpoint_base64=b.toString('base64');f.put(r);await rejected(f);
 }finally{f.dispose();}
});

test('same SQLite typed epoch mapping, PREPARED/new run, preserved predecessor, LOCAL intent and old RUNTIME PAUSED are mandatory',async()=>{
 const changes=[
  f=>f.db.exec("UPDATE dispatch_resume_epochs SET previous_run='dddddddddddddddddddddddddddddddd'"),
  f=>f.db.exec("UPDATE dispatch_resume_epochs SET booking_id='unknown'"),f=>f.db.exec("UPDATE dispatch_resume_epochs SET request_id='unknown'"),
  f=>f.db.exec("UPDATE dispatch_requests SET state='STARTED'"),f=>f.db.exec("UPDATE dispatch_requests SET state='UNKNOWN'"),f=>f.db.exec('UPDATE dispatch_requests SET run=NULL'),f=>f.db.prepare('UPDATE dispatch_requests SET run=?').run(ids.old),f=>f.db.prepare('UPDATE dispatch_requests SET predecessor=?').run(ids.old),
  f=>f.db.exec("UPDATE dispatch_requests SET end='200000000000000000000'"),f=>f.db.prepare('UPDATE dispatch_requests SET device=?').run(finiteNativeDevice),
  f=>f.db.prepare("UPDATE bookings SET state='DISPATCHED' WHERE id=?").run(ids.booking),f=>f.db.prepare("UPDATE bookings SET kind='RUNTIME' WHERE id=?").run(ids.booking),f=>f.db.prepare("UPDATE bookings SET start_key='bad' WHERE id=?").run(ids.booking),
  f=>f.db.prepare("UPDATE bookings SET state='SOURCE_FINISHED' WHERE id=?").run(ids.old),f=>f.db.prepare("UPDATE bookings SET kind='LOCAL_INTENT' WHERE id=?").run(ids.old),f=>f.db.prepare("UPDATE bookings SET device=? WHERE id=?").run(finiteNativeDevice,ids.old),f=>f.db.prepare("UPDATE bookings SET end_key='bad' WHERE id=?").run(ids.old),
  f=>f.db.prepare("UPDATE bookings SET start=CAST(x'ff' AS TEXT) WHERE id=?").run(ids.old),f=>f.db.prepare('DELETE FROM bookings WHERE id=?').run(ids.old),
  f=>f.db.prepare("INSERT INTO bookings SELECT ?,start,end,start_key,end_key,'ACTIVE','RUNTIME',device,created_utc FROM bookings WHERE id=?").run(ids.run,ids.old),
  f=>f.db.prepare("INSERT INTO bookings SELECT 'eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee',start,end,start_key,end_key,'ACTIVE','RUNTIME',device,created_utc FROM bookings WHERE id=?").run(ids.old),
  f=>f.db.prepare("INSERT INTO dispatch_requests SELECT 'second-pending',start,end,device,'PREPARED',predecessor,'dddddddddddddddddddddddddddddddd',created_utc FROM dispatch_requests").run(),
  f=>f.db.prepare('DELETE FROM booking_metadata WHERE key=?').run('mining-resume/v1/'+device.toLowerCase()+'/'+ids.old),
  f=>f.db.prepare('UPDATE booking_metadata SET value=? WHERE key=?').run('unknown',modeKey),f=>f.db.prepare('UPDATE booking_metadata SET value=? WHERE key=?').run('false',preferenceKey),
  f=>f.db.prepare('INSERT INTO booking_metadata VALUES(?,?)').run('mining-initial-start/v1/'+ids.request,'foreign initial identity'),
  f=>f.db.exec('DROP TABLE dispatch_resume_epochs'),f=>f.db.exec('ALTER TABLE dispatch_resume_epochs RENAME TO old_epochs;CREATE VIEW dispatch_resume_epochs AS SELECT * FROM old_epochs')
 ];
 for(const change of changes){const f=fixture();try{change(f);await rejected(f);}finally{f.dispose();}}
 for(const form of ['different','blob','noncanonical']){const f=fixture();try{let raw=canonical(f.receipt.checkpoint);if(form==='different'){const c=structuredClone(f.receipt.checkpoint);c.resume.counterTimeNs='9007199254740996';raw=canonical(c)}if(form==='blob')raw=Buffer.from(raw);if(form==='noncanonical')raw+=' ';
  f.db.prepare('UPDATE booking_metadata SET value=? WHERE key=?').run(raw,'mining-resume/v1/'+device.toLowerCase()+'/'+ids.old);await rejected(f);
 }finally{f.dispose();}}
 const f=fixture(predecessor);try{f.db.prepare('UPDATE dispatch_requests SET predecessor=?').run(ids.old);await rejected(f);}finally{f.dispose();}
});

test('ACK, HELD, uncertain transport and malformed replies are cached, never a second send or physical ownership',async()=>{
 for(const status of ['START_REQUESTED','ALREADY_RUNNING','CHECKPOINT_RESUME_HELD','uncertain','UNKNOWN','wrong-request','unbounded-error']){
  const f=fixture();try{let calls=0;const transport=f.transport(async()=>{++calls;if(status==='uncertain')throw Error('simulated uncertain transport');if(status==='wrong-request')return {status:'START_REQUESTED',request_id:ids.observation};if(status==='unbounded-error')return {status:'CHECKPOINT_RESUME_HELD',request_id:ids.request,error:'x'.repeat(401)};return {status,request_id:ids.request,...(status==='CHECKPOINT_RESUME_HELD'?{error:'SELF held, no launch'}:{})}}),before=f.sha();
   const accepted=['START_REQUESTED','ALREADY_RUNNING','CHECKPOINT_RESUME_HELD'].includes(status);
   for(let replay=0;replay<2;++replay){if(accepted)assert.equal((await transport.submit(f.command())).status,status);else await assert.rejects(transport.submit(f.command()));}
   assert.equal(calls,1);assert.equal(f.sha(),before);assert.equal(f.db.prepare('SELECT state FROM dispatch_requests').get().state,'PREPARED');
   await assert.rejects(transport.submit({...f.command(),base_birth:'2026-10-14T00:00:01Z'}),/rebound/);assert.equal(calls,1);
  }finally{f.dispose();}
 }
});

test('close freeze blocks both queued and newly arrived resume commands before send; STOP/read remain permitted',async()=>{
 const f=fixture();try{
  let entered,release,calls=0;const begun=new Promise(resolve=>entered=resolve),held=new Promise(resolve=>release=resolve),active=f.close.run('advance',async()=>{entered();await held});await begun;
  const before=f.sha(),transport=f.transport(async()=>{++calls;return {status:'START_REQUESTED',request_id:ids.request}});
  const queued=assert.rejects(transport.submit(f.command()),/freeze/),frozen=f.close.prepare(ids.request);assert.throws(()=>transport.submit(f.command()),/freeze/);release();await Promise.all([active,queued,frozen]);
  assert.equal(calls,0);assert.equal(f.sha(),before);await f.close.run('stop',()=>{});await f.close.run('snapshot',()=>{});
 }finally{f.dispose();}
});

test('an already entered resume send settles before freeze ACK, with no later hidden send or physical acknowledgement',async()=>{
 const f=fixture();try{
  let entered,release,calls=0;const begun=new Promise(resolve=>entered=resolve),held=new Promise(resolve=>release=resolve),before=f.sha();
  const transport=f.transport(async()=>{++calls;entered();await held;return {status:'START_REQUESTED',request_id:ids.request}}),active=transport.submit(f.command());await begun;
  let frozen=false;const barrier=f.close.prepare(ids.request).then(()=>{frozen=true});await Promise.resolve();assert.equal(frozen,false);
  assert.throws(()=>transport.submit(f.command()),/freeze/);release();assert.equal((await active).status,'START_REQUESTED');await barrier;
  assert.equal(frozen,true);assert.equal(calls,1);assert.equal(f.sha(),before);assert.equal(f.db.prepare('SELECT state FROM dispatch_requests').get().state,'PREPARED');
 }finally{f.dispose();}
});

test('DB/checkpoint bounds are distinct from conservative8192B wire limit; overbudget receipts remain Held',async()=>{
 for(const label of ['x'.repeat(8000),'λ'.repeat(4000),'\x01'.repeat(2000),'x'.repeat(17000)]){
  const f=fixture();try{const r=structuredClone(f.receipt);r.checkpoint.resume.checkpointLabel=label;f.put(r);f.saved(r);await rejected(f);}finally{f.dispose();}
 }
 const f=fixture();try{f.db.prepare('UPDATE dispatch_resume_epochs SET receipt=?').run('x'.repeat(24577));await rejected(f);}finally{f.dispose();}
});

test('64-entry no-eviction cache refuses a65th send and retains the first uncertain identity',async()=>{
 const f=fixture();try{
  let calls=0;const transport=f.transport(async(_encoded,admission)=>{++calls;if(admission.receipt.request_id===ids.request)throw Error('first outcome uncertain');return {status:'START_REQUESTED',request_id:admission.receipt.request_id}});
  await assert.rejects(transport.submit(f.command()),/uncertain/);
  for(let i=1;i<=64;++i){const suffix=i.toString(16).padStart(12,'0'),r=receipt({request:'20000000-0000-4000-8000-'+suffix,observation:'30000000-0000-4000-8000-'+suffix,booking:'40000000-0000-4000-8000-'+suffix,old:i.toString(16).padStart(32,'0'),run:(10000+i).toString(16).padStart(32,'0')});f.stage(r);const before=f.sha();if(i<64)await transport.submit(command(r));else await assert.rejects(transport.submit(command(r)),/64-request/);assert.equal(f.sha(),before);}
  assert.equal(calls,64);
  // Only the test rewinds its synthetic current pointer to discriminate cache
  // eviction. The transport itself never rewinds or writes any row.
  f.db.exec("UPDATE dispatch_requests SET state='SOURCE_FINISHED';UPDATE bookings SET state='SOURCE_FINISHED' WHERE kind='LOCAL_INTENT'");f.db.prepare("UPDATE dispatch_requests SET state='PREPARED' WHERE id=?").run(ids.booking);f.db.prepare("UPDATE bookings SET state='DISPATCHING' WHERE id=?").run(ids.booking);
  const before=f.sha();await assert.rejects(transport.submit(f.command()),/first outcome uncertain/);assert.equal(calls,64);assert.equal(f.sha(),before);
 }finally{f.dispose();}
});
