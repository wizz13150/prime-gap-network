import assert from 'node:assert/strict';import test from 'node:test';import fs from 'node:fs';import os from 'node:os';import path from 'node:path';import {DatabaseSync} from 'node:sqlite';
import {SerializedMutations} from '../tools/serialized-mutations.mjs';
import {ApplicationCloseProtocol,applicationCloseFailure,sourceConservation} from '../tools/application-close-protocol.mjs';
import {legacyNativeDevice,finiteNativeDevice} from '../tools/native-device-target.mjs';

// CLOSED: actual in-process serialized writer and fake callbacks only. No
// live-client import, user DB/source file, process, transport or GPU control.
const id='12345678-1234-4234-8234-123456789abc',other='87654321-4321-4321-8321-cba987654321';
let nonceSequence=0;const inventoryId=()=>'00000000-0000-4000-8000-'+String(++nonceSequence).padStart(12,'0');
const deferred=()=>{let resolve,reject;const promise=new Promise((a,b)=>{resolve=a;reject=b;});return {promise,resolve,reject};};
const constant=()=>({schema:'pgn-runtime-snapshot-1',observed_utc:'2026-10-14T00:00:00.1234567Z',owned:false,driver_valid:true,
 binding:{uuid:legacyNativeDevice,start:'134000000000000000000',end:'134001000000000000000'},manifest:null,
 chunk:Buffer.from('original stream bytes\n').toString('base64'),offset:17,next_offset:39,pending_bytes:0,
 checkpoint_evidence:{schema:'pgn-stop-checkpoint-evidence-1',status:'captured',run:'a'.repeat(32),checkpoint_label:'current-job/checkpoint',checkpoint_base64:Buffer.alloc(64).toString('base64'),source_tail_utf8:'source tail preserved\n'}});
const finite=()=>({schema:'pgn-device-control-observation-1',device:finiteNativeDevice,scope:'BOUNDED_SCIENCE_TEST',observed_utc:'2026-10-14T00:00:00.2345678Z',owned:false,manifest:null});
const create=options=>new ApplicationCloseProtocol({mutations:new SerializedMutations(),readSnapshot:async()=>constant(),readFiniteSnapshot:async()=>finite(),...options});

test('freeze latches synchronously, waits in-flight advance, rejects queued/new work, keeps STOP and conservation paths',async()=>{
 const entered=deferred(),release=deferred(),events=[];let allocations=0,starts=0,registrations=0;
 const protocol=create({readSnapshot:async()=>{events.push('inventory-constant');return constant();},readFiniteSnapshot:async()=>{events.push('inventory-finite');return finite();}});
 const active=protocol.run('advance',async()=>{allocations++;events.push('advance-begin');entered.resolve();await release.promise;events.push('advance-reconciled');return 'preserved receipt';});
 await entered.promise;
 const queuedStart=assert.rejects(protocol.run('start',()=>{starts++;}),/freeze/);
 const queuedAdvance=assert.rejects(protocol.run('advance',()=>{allocations++;}),/freeze/);
 const queuedModule=assert.rejects(protocol.run('module_select',()=>{registrations++;}),/freeze/);
 const prepared=protocol.prepare(id);assert.equal(protocol.frozen,true);assert.equal(protocol.prepare(id),prepared);
 let acknowledged=false;prepared.then(()=>{acknowledged=true;events.push('freeze-ack');});
 const nonce=inventoryId(),inventory=protocol.inventory(id,nonce);assert.equal(protocol.inventory(id,nonce),inventory);assert.equal(acknowledged,false);
 for(const action of ['start','start_prepared','resume_prepared','dispatch','advance','automatic_lookahead','module_register','module_select'])assert.throws(()=>protocol.run(action,()=>{throw Error('must not execute')}),/freeze/);
 const stopped=protocol.run('stop',()=>{events.push('operator-stop');return {status:'STOP_REQUESTED'};});
 // Stream/index ingestion is deliberately not a mutation-admission request.
 events.push('source-bytes-conserved');assert.equal(acknowledged,false);assert.equal(allocations,1);
 release.resolve();assert.equal(await active,'preserved receipt');await Promise.all([queuedStart,queuedAdvance,queuedModule]);
 const ack=await prepared;assert.deepEqual(ack,{type:'application_close_frozen',request_id:id,accepted:true});assert.equal('applied' in ack,false);assert.equal('stopped' in ack,false);
 assert.equal((await stopped).status,'STOP_REQUESTED');const observed=await inventory;
 assert.ok(events.indexOf('advance-reconciled')<events.indexOf('freeze-ack'));assert.ok(events.indexOf('freeze-ack')<events.indexOf('inventory-constant'));
 assert.equal(observed.inventory_known,true);assert.equal(starts,0);assert.equal(registrations,0);assert.equal(allocations,1);
 await protocol.run('module_list',()=>events.push('module-read'));await protocol.run('module_receipt',()=>events.push('receipt-read'));
 assert.equal(protocol.frozen,true);
});

test('canonical IDs, current-request correlation, duplicate prepare and stale requests never thaw',async()=>{
 const protocol=create();
 for(const invalid of [undefined,null,'',id.toUpperCase(),'{'+id+'}',id.replaceAll('-',''),'00000000-0000-0000-0000-000000000000','x'.repeat(100)]){
  assert.throws(()=>protocol.prepare(invalid),/UUID/);assert.equal(protocol.frozen,false);
 }
 assert.throws(()=>protocol.inventory(id,inventoryId()),/current frozen/);assert.equal(protocol.frozen,false);
 const pending=protocol.prepare(id);assert.equal(protocol.prepare(id),pending);await pending;
 assert.throws(()=>protocol.prepare(other),/Stale/);assert.throws(()=>protocol.inventory(other,inventoryId()),/current frozen/);assert.equal(protocol.frozen,true);
 assert.equal((await protocol.prepare(id)).request_id,id);assert.throws(()=>protocol.run('start',()=>{}),/freeze/);
 const failedNonce=inventoryId(),failure=applicationCloseFailure('application_close_inventory',other,Error('stale correlated request'),failedNonce);assert.equal(failure.inventory_id,failedNonce);
 assert.equal(failure.accepted,false);assert.equal(failure.inventory_known,false);assert.equal(failure.snapshot,null);assert.equal(failure.finite_device.status,'unknown');assert.deepEqual(failure.unknown_devices,[legacyNativeDevice,finiteNativeDevice]);
 assert.equal(applicationCloseFailure('application_close_prepare','x'.repeat(100),Error('bad UUID')).request_id,null);
});

test('both inventories are fresh per explicit call, concurrent duplicates share a read, raw checkpoint/stream bytes untouched',async()=>{
 const released=deferred();let constantReads=0,finiteReads=0;const source=constant(),testSource=finite();
 const protocol=create({readSnapshot:async()=>{constantReads++;await released.promise;return source;},readFiniteSnapshot:async()=>{finiteReads++;return testSource;}});
 await protocol.prepare(id);const firstNonce=inventoryId(),first=protocol.inventory(id,firstNonce),duplicate=protocol.inventory(id,firstNonce);assert.equal(first,duplicate);
 released.resolve();const observation=await first;assert.equal(constantReads,1);assert.equal(finiteReads,1);
 assert.equal(observation.snapshot,source);assert.equal(observation.snapshot.checkpoint_evidence,source.checkpoint_evidence);assert.equal(observation.snapshot.chunk,Buffer.from('original stream bytes\n').toString('base64'));assert.equal(observation.snapshot.offset,17);
 assert.equal(observation.finite_device.status,'observed');assert.equal(observation.finite_device.snapshot,testSource);assert.deepEqual(observation.unknown_devices,[]);assert.equal(observation.constant_known,true);assert.equal(observation.inventory_known,true);
 assert.equal('stopped' in observation,false);assert.equal('applied' in observation,false);assert.equal(protocol.inventory(id,firstNonce),first);await protocol.inventory(id,inventoryId());assert.equal(constantReads,2);assert.equal(finiteReads,2);
});

test('transport failures remain device-specific UNKNOWN, never invented idle; explicit retry refreshes both',async()=>{
 let failConstant=true,failFinite=false,constantReads=0,finiteReads=0;
 const protocol=create({readSnapshot:async()=>{constantReads++;if(failConstant)throw Error('constant transport timeout preserved');return constant();},readFiniteSnapshot:async()=>{finiteReads++;if(failFinite)throw Error('finite controller missing preserved');return finite();}});
 await protocol.prepare(id);let result=await protocol.inventory(id,inventoryId());
 assert.equal(result.accepted,false);assert.equal(result.inventory_known,false);assert.equal(result.constant_known,false);assert.equal(result.snapshot,null);assert.match(result.error,/constant transport timeout/);assert.equal(result.finite_device.status,'observed');assert.deepEqual(result.unknown_devices,[legacyNativeDevice]);
 failConstant=false;failFinite=true;result=await protocol.inventory(id,inventoryId());
 assert.equal(result.accepted,false);assert.equal(result.inventory_known,false);assert.equal(result.constant_known,true);assert.equal(result.snapshot.owned,false);assert.equal(result.finite_device.status,'unknown');assert.match(result.finite_device.error,/finite controller missing/);assert.equal('snapshot' in result.finite_device,false);assert.deepEqual(result.unknown_devices,[finiteNativeDevice]);
 failFinite=false;result=await protocol.inventory(id,inventoryId());assert.equal(result.inventory_known,true);assert.equal(result.accepted,true);assert.equal(constantReads,3);assert.equal(finiteReads,3);
});

test('missing/poisoned ownership, source identity/driver/timestamp and finite fields remain UNKNOWN',async()=>{
 for(const value of [undefined,null,0,'false']){
  const source={...constant(),owned:value},testSource={...finite(),owned:value};if(value===undefined){delete source.owned;delete testSource.owned;}
  const protocol=create({readSnapshot:async()=>source,readFiniteSnapshot:async()=>testSource});await protocol.prepare(id);const result=await protocol.inventory(id,inventoryId());
  assert.equal(result.inventory_known,false);assert.equal(result.constant_known,false);assert.equal(result.finite_device.status,'unknown');assert.deepEqual(result.unknown_devices,[legacyNativeDevice,finiteNativeDevice]);
 }
 for(const source of [{...constant(),driver_valid:false},{...constant(),binding:{uuid:finiteNativeDevice}},{...constant(),observed_utc:'invalid'},{...constant(),owned:true,manifest:null}]){
  const protocol=create({readSnapshot:async()=>source});await protocol.prepare(id);assert.equal((await protocol.inventory(id,inventoryId())).constant_known,false);
 }
 for(const testSource of [{...finite(),device:legacyNativeDevice},{...finite(),scope:'UNKNOWN'},{...finite(),observed_utc:null}]){
  const protocol=create({readFiniteSnapshot:async()=>testSource});await protocol.prepare(id);const result=await protocol.inventory(id,inventoryId());assert.equal(result.inventory_known,false);assert.equal(result.finite_device.status,'unknown');
 }
 const protocol=create({readFiniteSnapshot:undefined});await protocol.prepare(id);assert.match((await protocol.inventory(id,inventoryId())).finite_device.error,/transport unavailable/);
});

test('a failed in-flight operation cannot strand the freeze barrier or manufacture a physical ACK',async()=>{
 const entered=deferred(),finish=deferred(),protocol=create();
 const rejected=assert.rejects(protocol.run('advance',async()=>{entered.resolve();await finish.promise;throw Error('uncertain prior advance');}),/uncertain/);
 await entered.promise;const frozen=protocol.prepare(id);finish.resolve();await rejected;const ack=await frozen;
 assert.deepEqual(Object.keys(ack).sort(),['accepted','request_id','type']);assert.equal(protocol.frozen,true);assert.throws(()=>protocol.run('advance',()=>{}),/freeze/);await protocol.run('stop',()=>{});
});

test('live relay uses the freeze for automatic/queued admissions without importing or executing the relay',()=>{
 const source=fs.readFileSync(new URL('../tools/live-client.mjs',import.meta.url),'utf8');
 assert.match(source,/applicationClose\.run\('advance'/);assert.match(source,/applicationClose\.run\('dispatch'/);assert.match(source,/if\(autoAdvance&&!autoPaused&&!applicationClose\.frozen/);
 assert.match(source,/enabled:autoAdvance&&allowAdvance&&!applicationClose\.frozen/);assert.match(source,/readSnapshot:\(\)=>remote\('snapshot'\),readFiniteSnapshot:\(\)=>remoteFiniteDevice\('snapshot'\)/);
 const helper=fs.readFileSync(new URL('../tools/application-close-protocol.mjs',import.meta.url),'utf8');assert.doesNotMatch(helper,/consumeSnapshot|execFile|spawn\(|setInterval|SIGTERM/);
 assert.match(source,/SELECT source,offset,run FROM cursors WHERE source=\?/);assert.match(source,/fs\.lstatSync\(mirror\.file\(sourceRun\)\)/);
});

test('final run conservation reads an exact real temporary SQLite cursor and existing mirror; no missing-row default0',()=>{
 const temp=fs.mkdtempSync(path.join(os.tmpdir(),'pgn-close-conservation-')),sourceRun='a'.repeat(32),source='native/'+sourceRun+'.stdout',file=path.join(temp,sourceRun+'.stdout');let db;
 try{
  db=new DatabaseSync(path.join(temp,'history.sqlite'));db.exec('CREATE TABLE cursors(source TEXT PRIMARY KEY,offset INTEGER NOT NULL,run TEXT NOT NULL)');fs.writeFileSync(file,Buffer.alloc(0));
  const snapshot={...constant(),manifest:{uuid:legacyNativeDevice,run:sourceRun},offset:0,next_offset:0,pending_bytes:0};
  const state={residentViewer:false,currentRun:sourceRun,fragment:'',transportOffset:0,
   readCursor:wanted=>db.prepare('SELECT source,offset,run FROM cursors WHERE source=?').get(wanted),
   readMirrorStat:run=>{assert.equal(run,sourceRun);const stat=fs.lstatSync(file);return {isFile:stat.isFile(),isSymbolicLink:stat.isSymbolicLink(),size:stat.size};}};
  let result=sourceConservation(snapshot,state);assert.equal(result.ready,false);assert.match(result.error,/cursor row unavailable/);assert.equal('committed_offset' in result,false);
  db.prepare('INSERT INTO cursors VALUES(?,?,?)').run(source,0,sourceRun);result=sourceConservation(snapshot,state);assert.equal(result.ready,true);assert.equal(result.required_end,0);
  fs.writeFileSync(file,Buffer.alloc(80));db.prepare('UPDATE cursors SET offset=80 WHERE source=?').run(source);snapshot.next_offset=32;snapshot.pending_bytes=48;state.transportOffset=80;
  result=sourceConservation(snapshot,state);assert.equal(result.ready,true);assert.equal(result.run,sourceRun);assert.equal(result.source,source);assert.equal(result.required_end,80);assert.equal(result.committed_offset,80);assert.equal(result.mirror_size,80);assert.equal(result.transport_offset,80);assert.match(result.scope,/NOT_GLOBAL_POWER_CUT/);
  db.prepare('UPDATE cursors SET run=? WHERE source=?').run('b'.repeat(32),source);assert.equal(sourceConservation(snapshot,state).ready,false);db.prepare('UPDATE cursors SET run=? WHERE source=?').run(sourceRun,source);
  db.prepare('UPDATE cursors SET offset=79 WHERE source=?').run(source);assert.match(sourceConservation(snapshot,state).error,/history cursor/);db.prepare('UPDATE cursors SET offset=80 WHERE source=?').run(source);
  fs.writeFileSync(file,Buffer.alloc(79));assert.match(sourceConservation(snapshot,state).error,/mirror has not reached/);fs.writeFileSync(file,Buffer.alloc(80));
  assert.match(sourceConservation(snapshot,{...state,transportOffset:79}).error,/transport has not drained/);
  fs.unlinkSync(file);assert.equal(sourceConservation(snapshot,state).ready,false);
 }finally{db?.close();fs.rmSync(temp,{recursive:true,force:true});}
});

test('conservation rejects unknown epoch/resources, partial rows, unsafe boundaries and read failures without invented offsets',()=>{
 const sourceRun='a'.repeat(32),source='native/'+sourceRun+'.stdout';
 const snapshot={...constant(),manifest:{uuid:legacyNativeDevice,run:sourceRun},next_offset:32,pending_bytes:48};
 const cursor={source,run:sourceRun,offset:80},mirror={isFile:true,isSymbolicLink:false,size:80};
 const state={residentViewer:false,currentRun:sourceRun,fragment:'',transportOffset:80,readCursor:()=>cursor,readMirrorStat:()=>mirror};
 assert.equal(sourceConservation(snapshot,state).ready,true);
 for(const poison of [{residentViewer:true},{residentViewer:undefined},{currentRun:'b'.repeat(32)},{currentRun:null},{fragment:'partial'},{fragment:null},{fragment:undefined},{readCursor:undefined},{readMirrorStat:undefined},
  {readCursor:()=>undefined},{readCursor:()=>({...cursor,source:'native/'+sourceRun+'.stderr'})},{readCursor:()=>({...cursor,run:'b'.repeat(32)})},
  {readCursor:()=>{throw Error('SQLite read failed preserved')}},{readMirrorStat:()=>{throw Error('mirror absent preserved')}},{readMirrorStat:()=>({...mirror,isFile:false})},{readMirrorStat:()=>({...mirror,isSymbolicLink:true})}])assert.equal(sourceConservation(snapshot,{...state,...poison}).ready,false);
 for(const invalid of [undefined,null,'80',80.5,-1,Number.MAX_SAFE_INTEGER+1]){
  for(const field of ['next_offset','pending_bytes'])assert.equal(sourceConservation({...snapshot,[field]:invalid},state).ready,false);
  assert.equal(sourceConservation(snapshot,{...state,transportOffset:invalid}).ready,false);
  assert.equal(sourceConservation(snapshot,{...state,readCursor:()=>({...cursor,offset:invalid})}).ready,false);
  assert.equal(sourceConservation(snapshot,{...state,readMirrorStat:()=>({...mirror,size:invalid})}).ready,false);
 }
 assert.equal(sourceConservation({...snapshot,next_offset:Number.MAX_SAFE_INTEGER,pending_bytes:1},state).ready,false);assert.equal(sourceConservation({...snapshot,manifest:null},state).ready,false);
 const ahead={...state,transportOffset:100,readCursor:()=>({...cursor,offset:100}),readMirrorStat:()=>({...mirror,size:100})};assert.equal(sourceConservation(snapshot,ahead).ready,true);
 let reads=0;assert.equal(sourceConservation(snapshot,{...state,residentViewer:true,readCursor:()=>{reads++;return cursor;}}).ready,false);assert.equal(reads,0);
});

test('inventory conservation is correlated to its final source run/boundary and stays unknown on unavailable shared owner',async()=>{
 const sourceRun='a'.repeat(32),source='native/'+sourceRun+'.stdout',snapshot={...constant(),manifest:{uuid:legacyNativeDevice,run:sourceRun},next_offset:32,pending_bytes:48};
 let currentRun=sourceRun,fragment='',offset=79,cursorOffset=80,mirrorSize=80;
 const protocol=create({readSnapshot:async()=>snapshot,readSourceState:()=>({residentViewer:false,currentRun,fragment,transportOffset:offset,
  readCursor:wanted=>({source:wanted,run:currentRun,offset:cursorOffset}),readMirrorStat:()=>({isFile:true,isSymbolicLink:false,size:mirrorSize})})});
 await protocol.prepare(id);let result=await protocol.inventory(id,inventoryId());assert.equal(result.inventory_known,true);assert.equal(result.source_conservation.ready,false);assert.equal(result.source_conservation.required_end,80);
 offset=80;result=await protocol.inventory(id,inventoryId());assert.equal(result.source_conservation.ready,true);assert.equal(result.source_conservation.run,sourceRun);assert.equal(result.source_conservation.source,source);assert.equal(result.snapshot,snapshot);assert.equal(result.snapshot.chunk,snapshot.chunk);
 currentRun='b'.repeat(32);result=await protocol.inventory(id,inventoryId());assert.equal(result.source_conservation.ready,false);assert.match(result.source_conservation.error,/epoch/);currentRun=sourceRun;
 fragment='partial';assert.equal((await protocol.inventory(id,inventoryId())).source_conservation.ready,false);fragment='';cursorOffset=79;assert.equal((await protocol.inventory(id,inventoryId())).source_conservation.ready,false);cursorOffset=80;mirrorSize=79;assert.equal((await protocol.inventory(id,inventoryId())).source_conservation.ready,false);
 const absent=create();await absent.prepare(id);assert.equal((await absent.inventory(id,inventoryId())).source_conservation.ready,false);
 const error=create({readSourceState:()=>{throw Error('shared history unavailable preserved')}});await error.prepare(id);assert.match((await error.inventory(id,inventoryId())).source_conservation.error,/shared history unavailable/);
 assert.equal(applicationCloseFailure('application_close_inventory',id,Error('unknown')).source_conservation.ready,false);
});

test('inventory nonce correlates each acquisition; same nonce deduplicates, different inflight nonce is BUSY and never relabels old data',async()=>{
 const entered=deferred(),release=deferred();let reads=0;
 const protocol=create({readSnapshot:async()=>{reads++;entered.resolve();if(reads===1)await release.promise;return constant();}});await protocol.prepare(id);
 for(const malformed of [undefined,null,'',id.toUpperCase(),'00000000-0000-0000-0000-000000000000',id.replaceAll('-','')])assert.throws(()=>protocol.inventory(id,malformed),/UUID/);
 assert.equal(reads,0);const oldNonce=inventoryId(),newNonce=inventoryId(),old=protocol.inventory(id,oldNonce);await entered.promise;
 assert.equal(protocol.inventory(id,oldNonce),old);const busy=await protocol.inventory(id,newNonce);
 assert.equal(busy.inventory_id,newNonce);assert.equal(busy.request_id,id);assert.equal(busy.status,'BUSY');assert.equal(busy.accepted,false);assert.equal(busy.inventory_known,false);assert.equal(busy.snapshot,null);assert.equal(busy.source_conservation.ready,false);assert.equal(reads,1);
 release.resolve();const previous=await old;assert.equal(previous.inventory_id,oldNonce);assert.equal(previous.request_id,id);assert.equal(protocol.inventory(id,oldNonce),old);
 const fresh=await protocol.inventory(id,newNonce);assert.equal(fresh.inventory_id,newNonce);assert.equal(reads,2);assert.throws(()=>protocol.inventory(other,newNonce),/current frozen/);
 const failure=applicationCloseFailure('application_close_inventory',other,Error('stale'),newNonce);assert.equal(failure.inventory_id,newNonce);assert.equal(failure.accepted,false);
});
