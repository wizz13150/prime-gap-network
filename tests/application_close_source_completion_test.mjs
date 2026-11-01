import assert from 'node:assert/strict';import test from 'node:test';import fs from 'node:fs';import os from 'node:os';import path from 'node:path';import {DatabaseSync} from 'node:sqlite';
import {QueuedDispatch,CONSTANT_DEVICE} from '../tools/queued-dispatch.mjs';
import {SerializedMutations} from '../tools/serialized-mutations.mjs';
import {ApplicationCloseProtocol,sourceConservation} from '../tools/application-close-protocol.mjs';
import {closeSnapshotCompletionObservation,commitSnapshotCompletion,snapshotCompletionObservation} from '../tools/mining-session-policy.mjs';
import {finiteNativeDevice} from '../tools/native-device-target.mjs';
// CLOSED: temporary booking/history SQLite and mirror, fake acquisition only.
// No live-client import, process, SSH, physical controller or user data access.
const closeId='12345678-1234-4234-8234-123456789abc',queueId='87654321-4321-4321-8321-cba987654321',run='b'.repeat(32),predecessor='a'.repeat(32),a='134000000000000000000',b='134001000000000000000',length='1000000000000000';
let nonce=0;const inventoryId=()=>'00000000-0000-4000-8000-'+String(++nonce).padStart(12,'0');const key=s=>String(s.length).padStart(3,'0')+s;
function fixture(){
 const temp=fs.mkdtempSync(path.join(os.tmpdir(),'pgn-close-source-')),bookFile=path.join(temp,'booking.sqlite'),historyFile=path.join(temp,'history.sqlite'),mirrorFile=path.join(temp,run+'.stdout');
 const db=new DatabaseSync(bookFile),history=new DatabaseSync(historyFile);let q;
 try{
  db.exec('CREATE TABLE bookings(id TEXT PRIMARY KEY,start TEXT,end TEXT,start_key TEXT,end_key TEXT,state TEXT,kind TEXT,device TEXT,created_utc TEXT)');
  db.prepare("INSERT INTO bookings VALUES(?,?,?,?,?,'QUEUED','LOCAL_INTENT',?,'fixture')").run(queueId,a,b,key(a),key(b),CONSTANT_DEVICE);
  q=new QueuedDispatch(bookFile);q.arm(queueId,{uuid:CONSTANT_DEVICE,start:a,end:b,length});q.prepare(predecessor);
  const binding={uuid:CONSTANT_DEVICE,start:a,end:b,length,queued_intent:queueId},manifest={...binding,run,birth:'2026-10-14T00:00:00.1234567Z'};
  assert.equal(q.reconcile({owned:true,state:'RUNNING',manifest,binding}),true);
  db.prepare("INSERT INTO bookings VALUES(?,?,?,?,?,'PAUSED','RUNTIME',?,'fixture')").run(run,a,b,key(a),key(b),CONSTANT_DEVICE);
  history.exec('CREATE TABLE cursors(source TEXT PRIMARY KEY,offset INTEGER NOT NULL,run TEXT NOT NULL)');history.prepare('INSERT INTO cursors VALUES(?,?,?)').run('native/'+run+'.stdout',80,run);fs.writeFileSync(mirrorFile,Buffer.alloc(80));
  const receipt={schema:'pgn-source-range-completion-1',run,start:a,end:b,queued_intent:queueId,source_positions:length,source_range_complete:true,source_exit_code:0,independent_coverage:false,proof_status:'SOURCE_PRP_INDEPENDENT_PROOF_PENDING'};
  const packet={schema:'pgn-runtime-snapshot-1',observed_utc:'2026-10-14T00:00:01.1234567Z',state:'OFF',owned:false,operator_stop:true,driver_valid:true,binding,manifest,
   last_exit:{run,exit_code:0},next_offset:32,pending_bytes:48,offset:0,chunk:Buffer.alloc(32).toString('base64'),completion:{...receipt,run:predecessor},completion_candidate:null,
   source_completion_observation:{...receipt,evidence_scope:'VERIFIED_SNAPSHOT_SOURCE_COMPLETION',source_observed_operator_stop:true,source_observed_stop_file:false},
   checkpoint_evidence:{schema:'pgn-stop-checkpoint-evidence-1',status:'unavailable'}};
  const state=()=>({residentViewer:false,currentRun:run,fragment:'',transportOffset:80,readCursor:source=>history.prepare('SELECT source,offset,run FROM cursors WHERE source=?').get(source),readMirrorStat:()=>{const stat=fs.lstatSync(mirrorFile);return {isFile:stat.isFile(),isSymbolicLink:stat.isSymbolicLink(),size:stat.size};}});
  const events=[];let allocationCalls=0;q.prepareAtBoundary=()=>{allocationCalls++;throw Error('Close must never allocate')};let current=packet,protocol;
  protocol=new ApplicationCloseProtocol({mutations:new SerializedMutations(),readSnapshot:async()=>current,
   readFiniteSnapshot:async()=>({schema:'pgn-device-control-observation-1',device:finiteNativeDevice,scope:'BOUNDED_SCIENCE_TEST',observed_utc:'2026-10-14T00:00:01.2345678Z',owned:false}),readSourceState:state,
   finalizeCompletedSource:(snapshot,conserved,id)=>{
    const observation=closeSnapshotCompletionObservation(snapshot,conserved.run,snapshot.binding,{frozenCurrentRequest:protocol.isCurrentRequest(id),requestId:id,sourceConservation:conserved});
    if(!observation)return {status:'not_complete'};
    const exact=commitSnapshotCompletion(q,observation);events.push({type:'source_completion',receipt:exact,evidence_scope:'VERIFIED_SNAPSHOT_SOURCE_COMPLETION',community_updated:false});return {status:'committed',run:exact.run,queued_intent:exact.queued_intent};
   }});
  return {db,history,q,packet,state,protocol,events,setPacket(value){current=value},allocationCalls:()=>allocationCalls,
   status:()=>({request:db.prepare('SELECT state FROM dispatch_requests WHERE id=?').get(queueId).state,intent:db.prepare('SELECT state FROM bookings WHERE id=?').get(queueId).state,physical:db.prepare('SELECT state FROM bookings WHERE id=?').get(run).state}),
   close(){q.close();history.close();db.close();fs.rmSync(temp,{recursive:true,force:true});}};
 }catch(error){q?.close();history.close();db.close();fs.rmSync(temp,{recursive:true,force:true});throw error;}
}

test('stopped full SOURCE commits last manual intent and same RUNTIME atomically before correlated inventory, never advances',async()=>{
 const f=fixture();try{
  assert.deepEqual(f.status(),{request:'STARTED',intent:'DISPATCHED',physical:'PAUSED'});await f.protocol.prepare(closeId);const nonce=inventoryId(),result=await f.protocol.inventory(closeId,nonce);f.events.push(result);
  assert.equal(result.accepted,true);assert.equal(result.inventory_id,nonce);assert.equal(result.source_conservation.ready,true);assert.equal(result.source_finalization.status,'committed');
  assert.deepEqual(f.status(),{request:'SOURCE_FINISHED',intent:'SOURCE_FINISHED',physical:'SOURCE_FINISHED'});assert.deepEqual(f.events.map(e=>e.type),['source_completion','application_close_inventory']);
  const event=f.events[0];assert.equal(event.receipt.run,run);assert.equal(event.receipt.queued_intent,queueId);assert.equal(event.receipt.independent_coverage,false);assert.equal(event.receipt.proof_status,'SOURCE_PRP_INDEPENDENT_PROOF_PENDING');assert.equal(event.community_updated,false);assert.equal(event.evidence_scope,'VERIFIED_SNAPSHOT_SOURCE_COMPLETION');
  assert.equal(f.packet.operator_stop,true);assert.equal(f.q.sessionMetadata().mode,'manual-reservations');assert.equal(f.allocationCalls(),0);assert.throws(()=>f.protocol.run('advance',()=>{}),/freeze/);
  assert.equal((await f.protocol.inventory(closeId,inventoryId())).source_finalization.status,'committed');assert.equal(f.db.prepare('SELECT count(*) n FROM dispatch_requests').get().n,1);assert.equal(f.allocationCalls(),0);
 }finally{f.close();}
});

test('partial checkpoint and pre-STOP inventory do not fabricate source completion',async()=>{
 for(const change of [{completion_candidate:null,source_completion_observation:null,checkpoint_evidence:{schema:'pgn-stop-checkpoint-evidence-1',status:'captured'}},{operator_stop:false},{state:'RUNNING',owned:true}]){
  const f=fixture();try{f.setPacket({...f.packet,...change});await f.protocol.prepare(closeId);const result=await f.protocol.inventory(closeId,inventoryId());assert.equal(result.accepted,true);assert.equal(result.source_finalization.status,'not_complete');assert.equal(f.events.length,0);assert.equal(f.status().request,'STARTED');assert.equal(f.status().physical,'PAUSED');assert.equal(f.allocationCalls(),0);}finally{f.close();}
 }
});

test('unknown booleans, changed run/foreign binding and inconsistent complete evidence hold without completion',async()=>{
 const changes=[];for(const field of ['owned','operator_stop'])for(const value of [undefined,null,0,'false'])changes.push({[field]:value});
 for(const field of [{state:null},{manifestRun:'c'.repeat(32)},{bindingUuid:finiteNativeDevice},{bindingStart:'134000000000000000001'},{receiptRun:'c'.repeat(32)},{receiptPositions:'1'},{receiptQueued:'11111111-1111-4111-8111-111111111111'},{exitCode:1},{receiptIndependent:true},{receiptProof:'CERTIFIED'},{receiptScope:'AUTO'},{receiptObservedStop:false},{receiptStopFile:'false'}])changes.push(field);
 for(const change of changes){
  const f=fixture();try{
   let packet={...f.packet,...change};
   if('manifestRun'in change)packet.manifest={...f.packet.manifest,run:change.manifestRun};if('bindingUuid'in change)packet.binding={...f.packet.binding,uuid:change.bindingUuid};if('bindingStart'in change)packet.binding={...f.packet.binding,start:change.bindingStart};
   const receipt={...f.packet.source_completion_observation};if('receiptRun'in change)receipt.run=change.receiptRun;if('receiptPositions'in change)receipt.source_positions=change.receiptPositions;if('receiptQueued'in change)receipt.queued_intent=change.receiptQueued;if('receiptIndependent'in change)receipt.independent_coverage=change.receiptIndependent;if('receiptProof'in change)receipt.proof_status=change.receiptProof;if('receiptScope'in change)receipt.evidence_scope=change.receiptScope;if('receiptObservedStop'in change)receipt.source_observed_operator_stop=change.receiptObservedStop;if('receiptStopFile'in change)receipt.source_observed_stop_file=change.receiptStopFile;packet.source_completion_observation=receipt;
   if('exitCode'in change)packet.last_exit={run,exit_code:change.exitCode};f.setPacket(packet);await f.protocol.prepare(closeId);const result=await f.protocol.inventory(closeId,inventoryId());
   assert.ok(result.accepted===false||result.source_conservation.ready===false);assert.equal(f.events.length,0);assert.equal(f.status().request,'STARTED');assert.equal(f.status().physical,'PAUSED');assert.equal(f.allocationCalls(),0);
  }finally{f.close();}
 }
});

test('SQLite trigger failure rolls back intent/RUNTIME and returns held inventory before any source event; explicit fresh retry repairs',async()=>{
 const f=fixture();try{
  f.db.exec("CREATE TRIGGER reject_finish BEFORE UPDATE OF state ON bookings WHEN NEW.state='SOURCE_FINISHED' BEGIN SELECT RAISE(ABORT,'simulated close completion commit failure');END;");await f.protocol.prepare(closeId);let result=await f.protocol.inventory(closeId,inventoryId());
  assert.equal(result.accepted,false);assert.equal(result.source_conservation.ready,true);assert.equal(result.source_finalization.status,'held');assert.match(result.error,/commit failure/);assert.equal(f.events.length,0);assert.deepEqual(f.status(),{request:'STARTED',intent:'DISPATCHED',physical:'PAUSED'});
  f.db.exec('DROP TRIGGER reject_finish');result=await f.protocol.inventory(closeId,inventoryId());assert.equal(result.source_finalization.status,'committed');assert.equal(f.status().physical,'SOURCE_FINISHED');assert.equal(f.allocationCalls(),0);
 }finally{f.close();}
});

test('close SOURCE verifier requires its real frozen/conserved epoch and never reuses the automatic gate with STOP cleared',()=>{
 const f=fixture();try{
  const conserved=sourceConservation(f.packet,f.state()),context={frozenCurrentRequest:true,requestId:closeId,sourceConservation:conserved};
  assert.equal(conserved.ready,true);assert.throws(()=>snapshotCompletionObservation(f.packet,run,f.packet.binding),/snapshot/);
  const autoPacket={...f.packet,operator_stop:false,pending_bytes:0,completion:null,source_completion_observation:{...f.packet.source_completion_observation,source_observed_operator_stop:false}};
  assert.throws(()=>snapshotCompletionObservation(autoPacket,run,autoPacket.binding),/snapshot/);assert.equal(autoPacket.operator_stop,false);assert.equal(autoPacket.completion_candidate,null);
  assert.throws(()=>closeSnapshotCompletionObservation(f.packet,run,f.packet.binding,{...context,frozenCurrentRequest:false}),/freeze/);
  assert.throws(()=>closeSnapshotCompletionObservation(f.packet,run,f.packet.binding,{...context,requestId:'bad'}),/freeze/);
  assert.throws(()=>closeSnapshotCompletionObservation(f.packet,run,f.packet.binding,{...context,sourceConservation:{...conserved,ready:false}}),/conserved/);
  assert.throws(()=>closeSnapshotCompletionObservation(f.packet,predecessor,f.packet.binding,context),/conserved/);
  const observed=closeSnapshotCompletionObservation(f.packet,run,f.packet.binding,context);assert.equal(observed.receipt.run,run);assert.equal(f.packet.operator_stop,true);assert.equal(f.status().request,'STARTED');assert.equal(f.allocationCalls(),0);
 }finally{f.close();}
});

test('close SOURCE selection prefers current committed receipt, then dedicated stopped observation, then legacy candidate only',()=>{
 const f=fixture();try{
  const conserved=sourceConservation(f.packet,f.state()),context={frozenCurrentRequest:true,requestId:closeId,sourceConservation:conserved};
  const dedicated=f.packet.source_completion_observation,committed={...dedicated,source_position_evidence:'committed-current'},candidate={...dedicated,source_position_evidence:'legacy-candidate'};
  let observation=closeSnapshotCompletionObservation({...f.packet,completion:committed,completion_candidate:{...candidate,source_positions:'1'}},run,f.packet.binding,context);assert.equal(observation.receipt.source_position_evidence,'committed-current');
  observation=closeSnapshotCompletionObservation({...f.packet,completion_candidate:{...candidate,source_positions:'1'}},run,f.packet.binding,context);assert.equal(observation.receipt.evidence_scope,'VERIFIED_SNAPSHOT_SOURCE_COMPLETION');
  observation=closeSnapshotCompletionObservation({...f.packet,source_completion_observation:null,completion_candidate:candidate},run,f.packet.binding,context);assert.equal(observation.receipt.source_position_evidence,'legacy-candidate');assert.equal(f.status().request,'STARTED');
 }finally{f.close();}
});
