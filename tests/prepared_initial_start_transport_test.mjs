import test from 'node:test';import assert from 'node:assert/strict';import fs from 'node:fs';import os from 'node:os';import path from 'node:path';import {createHash} from 'node:crypto';import {DatabaseSync} from 'node:sqlite';
import {PreparedInitialStartTransport,initialAdmissionFromStore} from '../tools/prepared-initial-start-transport.mjs';
import {ApplicationCloseProtocol} from '../tools/application-close-protocol.mjs';import {SerializedMutations} from '../tools/serialized-mutations.mjs';import {legacyNativeDevice,finiteNativeDevice} from '../tools/native-device-target.mjs';
// CLOSED: native-format receipt fixture in temporary SQLite, fake remote only.
const request='12345678-1234-4234-8234-123456789abc',observation='11111111-2222-4333-8444-555555555555',booking='87654321-4321-4321-8321-cba987654321',a='199900000000000000000',b='199901000000000000000';
const receiptKey='mining-initial-start/v1/'+request,modeKey='mining-session-mode/v1/'+legacyNativeDevice.toLowerCase();
const serialize=value=>Array.isArray(value)?'['+value.map(serialize).join(',')+']':value&&typeof value==='object'?'{'+Object.keys(value).sort().map(key=>JSON.stringify(key)+':'+serialize(value[key])).join(',')+'}':JSON.stringify(value);
const key=value=>String(value.length).padStart(3,'0')+value;
function fingerprint(receipt){const s=receipt.scope;return createHash('sha256').update(JSON.stringify([receipt.request_id,receipt.device.toLowerCase(),s.execution_start,s.execution_end,s.auto_start,s.auto_end,s.block_anchor,s.block_width,receipt.observation_id,receipt.registry_provenance.semantic_digest])+'\n').digest('hex');}
const command=()=>({action:'start_prepared',request_id:request,device:legacyNativeDevice,observation_id:observation,base_run:null,base_birth:null,base_start:null,base_end:null});
function fixture(mode='automatic'){
 const directory=fs.mkdtempSync(path.join(os.tmpdir(),'pgn-initial-transport-')),db=new DatabaseSync(path.join(directory,'bookings.sqlite'));
 db.exec('CREATE TABLE booking_metadata(key TEXT PRIMARY KEY,value TEXT NOT NULL);CREATE TABLE bookings(id TEXT PRIMARY KEY,start TEXT,end TEXT,start_key TEXT,end_key TEXT,state TEXT,kind TEXT,device TEXT,created_utc TEXT);CREATE TABLE dispatch_requests(id TEXT PRIMARY KEY,start TEXT,end TEXT,device TEXT,state TEXT,predecessor TEXT,run TEXT,created_utc TEXT)');
 const receipt={schema:'pgn-initial-start-receipt-1',origin:'INITIAL_START',request_id:request,device:legacyNativeDevice,observation_id:observation,mode,booking_id:booking,start:a,end:b,
  scope:{execution_start:a,execution_end:'200000000000000000000',auto_start:a,auto_end:'200000000000000000000',block_anchor:'0',block_width:'1000000000000000'},
  registry_provenance:{url:'https://example.invalid/community.csv',sha256:'e'.repeat(64),observed_utc:'2026-10-14T00:00:00.000Z',semantic_digest:'f'.repeat(64)}};receipt.input_fingerprint=fingerprint(receipt);
 const store=value=>db.prepare('INSERT INTO booking_metadata VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value').run(receiptKey,serialize(value));store(receipt);
 db.prepare('INSERT INTO booking_metadata VALUES(?,?)').run(modeKey,mode);
 db.prepare("INSERT INTO bookings VALUES(?,?,?,?,?,'DISPATCHING','LOCAL_INTENT',?,'native-fixture')").run(booking,a,b,key(a),key(b),legacyNativeDevice);
 db.prepare("INSERT INTO dispatch_requests VALUES(?,?,?,?,'PREPARED',NULL,NULL,'native-fixture')").run(booking,a,b,legacyNativeDevice);
 const writer=new SerializedMutations(),close=new ApplicationCloseProtocol({mutations:writer});
 return {db,receipt,store,writer,close,transport:send=>new PreparedInitialStartTransport({db,runMutation:operation=>close.run('start_prepared',operation),send}),
  changes:()=>db.prepare('SELECT total_changes() n').get().n,state:()=>db.prepare('SELECT state,predecessor,run FROM dispatch_requests WHERE id=?').get(booking),
  dispose(){db.close();fs.rmSync(directory,{recursive:true,force:true});}};
}

test('reads the committed native receipt before send, transports exact bounded envelope and never marks STARTED or writes mode',async()=>{
 for(const mode of ['automatic','manual-reservations']){const f=fixture(mode);try{
  const before=f.changes();let calls=0;const transport=f.transport(async(encoded,admission)=>{
   calls++;assert.equal(f.db.prepare('SELECT value FROM booking_metadata WHERE key=?').get(receiptKey).value,serialize(f.receipt));assert.equal(f.state().state,'PREPARED');
   const raw=Buffer.from(encoded,'base64');assert.ok(raw.length<=12288);assert.equal(raw.toString('base64'),encoded);assert.deepEqual(JSON.parse(raw.toString('utf8')),admission);assert.deepEqual(admission.receipt,f.receipt);assert.deepEqual(admission.base,{base_run:null,base_birth:null,base_start:null,base_end:null});return {status:'START_REQUESTED',request_id:request};
  });const result=await transport.submit(command());assert.equal(result.status,'START_REQUESTED');assert.equal(calls,1);assert.equal(f.changes(),before);assert.deepEqual({...f.state()},{state:'PREPARED',predecessor:null,run:null});
  assert.equal(f.db.prepare('SELECT value FROM booking_metadata WHERE key=?').get(modeKey).value,mode);
 }finally{f.dispose();}}
});

test('command IDs, exact base null/string group, scope and native fingerprint are fail-closed without a remote call',async()=>{
 const poisons=[c=>{delete c.base_birth},c=>{c.base_run='a'.repeat(32)},c=>{c.base_birth=false},c=>{c.observation_id=request},c=>{c.device=finiteNativeDevice},c=>{c.request_id=request.toUpperCase()},c=>{c.observation_id=null},c=>{c.ready=true},c=>{c.action='start'}];
 for(const poison of poisons){const f=fixture();try{let calls=0;const c=command();poison(c);const before=f.changes();await assert.rejects(f.transport(async()=>{calls++;return {status:'START_REQUESTED',request_id:request}}).submit(c));assert.equal(calls,0);assert.equal(f.changes(),before);}finally{f.dispose();}}
 const f=fixture();try{const c={...command(),base_run:'a'.repeat(32),base_birth:'2026-10-14T00:00:00.1234567Z',base_start:'199899000000000000000',base_end:a};const checked=initialAdmissionFromStore(f.db,c);assert.equal(checked.admission.base.base_run,c.base_run);assert.equal(checked.admission.receipt.observation_id,observation);}finally{f.dispose();}
});

test('native receipt schema/types/canonical JSON/provenance/scope corruption never reaches send',async()=>{
 const poisons=[r=>{r.origin='SUCCESSOR'},r=>{r.request_id=observation},r=>{r.observation_id=request},r=>{r.device=finiteNativeDevice},r=>{r.mode=true},r=>{r.start=123},r=>{r.scope.execution_end=r.scope.execution_start},r=>{r.scope.auto_end='200001000000000000000'},r=>{r.scope.block_width='0'},r=>{r.start='199900000000000000001'},r=>{r.input_fingerprint='0'.repeat(64)},r=>{r.registry_provenance.sha256='Z'.repeat(64)},r=>{r.registry_provenance.observed_utc='2026-10-14T00:00:00Z'},r=>{r.registry_provenance.semantic_digest='0'.repeat(64)},r=>{r.registry_provenance.extra=true},r=>{r.extra='not native'}];
 for(const poison of poisons){const f=fixture();try{const value=structuredClone(f.receipt);poison(value);f.store(value);let calls=0;const before=f.changes();await assert.rejects(f.transport(async()=>{calls++;return {status:'START_REQUESTED',request_id:request}}).submit(command()));assert.equal(calls,0);assert.equal(f.changes(),before);}finally{f.dispose();}}
 for(const format of ['pretty','blob']){const f=fixture();try{const raw=format==='pretty'?JSON.stringify(f.receipt,null,2):Buffer.from('not text');f.db.prepare('UPDATE booking_metadata SET value=? WHERE key=?').run(raw,receiptKey);let calls=0;await assert.rejects(f.transport(async()=>{calls++;}).submit(command()));assert.equal(calls,0);}finally{f.dispose();}}
});

test('row PREPARED/null provenance, same booking identity and session metadata are mandatory without repair/writes',async()=>{
 const mutations=[db=>db.exec("UPDATE dispatch_requests SET state='STARTED'"),db=>db.exec("UPDATE dispatch_requests SET run='aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa'"),db=>db.exec("UPDATE dispatch_requests SET predecessor='aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa'"),db=>db.exec("UPDATE bookings SET kind='RUNTIME'"),db=>db.exec("UPDATE bookings SET state='QUEUED'"),db=>db.exec("UPDATE bookings SET end='199902000000000000000'"),db=>db.exec("UPDATE bookings SET start_key='bad'"),db=>db.prepare('UPDATE bookings SET device=?').run(finiteNativeDevice),db=>db.prepare('UPDATE booking_metadata SET value=? WHERE key=?').run('manual-reservations',modeKey),db=>db.prepare('INSERT INTO booking_metadata VALUES(?,?)').run('mining-auto-after-manual/v1/'+legacyNativeDevice.toLowerCase(),'true'),db=>db.prepare('INSERT INTO booking_metadata VALUES(?,?)').run('queue-order-mode','unknown'),db=>db.prepare('DELETE FROM booking_metadata WHERE key=?').run(receiptKey)];
 for(const mutate of mutations){const f=fixture();try{mutate(f.db);let calls=0;const before=f.changes();await assert.rejects(f.transport(async()=>{calls++;}).submit(command()));assert.equal(calls,0);assert.equal(f.changes(),before);}finally{f.dispose();}}
});

test('same payload replay caches acknowledgement/uncertainty, never sends another worker or mutates its state',async()=>{
 for(const failure of [false,true]){const f=fixture();try{let calls=0;const transport=f.transport(async()=>{calls++;if(failure)throw Error('simulated uncertain transport');return {status:'START_REQUESTED',request_id:request}}),before=f.changes();
  if(failure){await assert.rejects(transport.submit(command()),/uncertain/);await assert.rejects(transport.submit(command()),/uncertain/);}else{assert.equal((await transport.submit(command())).status,'START_REQUESTED');assert.equal((await transport.submit(command())).status,'START_REQUESTED');}
  assert.equal(calls,1);assert.equal(f.changes(),before);assert.equal(f.state().state,'PREPARED');
 }finally{f.dispose();}}
 const f=fixture();try{let calls=0;const transport=f.transport(async()=>{calls++;return {status:'UNKNOWN',request_id:request}});await assert.rejects(transport.submit(command()),/uncorrelated/);await assert.rejects(transport.submit(command()),/uncorrelated/);assert.equal(calls,1);}finally{f.dispose();}
});

test('application close rejects queued and newly received start_prepared before callback; STOP/read remain allowed',async()=>{
 const f=fixture();try{let release,entered;const begun=new Promise(resolve=>entered=resolve),finished=new Promise(resolve=>release=resolve);const active=f.close.run('advance',async()=>{entered();await finished});await begun;
  let calls=0;const transport=f.transport(async()=>{calls++;return {status:'START_REQUESTED',request_id:request}}),queued=assert.rejects(transport.submit(command()),/freeze/),frozen=f.close.prepare(request);assert.throws(()=>transport.submit(command()),/freeze/);release();await Promise.all([active,queued,frozen]);assert.equal(calls,0);assert.equal(f.state().state,'PREPARED');await f.close.run('stop',()=>{});await f.close.run('snapshot',()=>{});
 }finally{f.dispose();}
});

test('live wire uses initial admission only, keeps finite control and blocks productive legacy start without importing relay',()=>{
 const source=fs.readFileSync(new URL('../tools/live-client.mjs',import.meta.url),'utf8');assert.match(source,/remote\('initial',null,admission\.base\.base_run,encoded\)/);assert.match(source,/-InitialAdmission/);assert.match(source,/applicationClose\.run\('start_prepared'/);assert.match(source,/if\(command\.action==='start'\)throw Error\('Productive start requires/);assert.match(source,/target\.kind==='bounded_test'/);assert.match(source,/action:'start_prepared'.*applied:false/);
});
