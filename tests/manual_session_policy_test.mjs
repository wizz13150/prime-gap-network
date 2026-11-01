import fs from 'node:fs';import os from 'node:os';import path from 'node:path';import assert from 'node:assert/strict';
import {DatabaseSync} from 'node:sqlite';import {QueuedDispatch,CONSTANT_DEVICE} from '../tools/queued-dispatch.mjs';
import {sessionKeys,sessionModes,requestExplicitMiningStart,readSessionMetadata,snapshotCompletionObservation,finishManualSession,commitSnapshotCompletion} from '../tools/mining-session-policy.mjs';
import {CompletionAdvanceGate} from '../tools/completion-advance-gate.mjs';
// CLOSED: temporary SQLite only, fake control callback, no child/GPU/network/UI.
const temp=fs.mkdtempSync(path.join(os.tmpdir(),'pgn-manual-session-')),file=path.join(temp,'bookings.sqlite');let db,q;
const u=10n**15n,a=134000n*u,z=100n*u,old='a'.repeat(32),keys=sessionKeys(CONSTANT_DEVICE),key=s=>String(s.length).padStart(3,'0')+s;
const csv='Start *1e15,End *1e15,Status,Owner\n100000,200000,Open,\n';
const binding={uuid:CONSTANT_DEVICE,start:String(a-u),end:String(a),length:String(u)};
const id=i=>'12345678-1234-1234-1234-'+String(i).padStart(12,'0');
try{
 db=new DatabaseSync(file);db.exec('CREATE TABLE bookings(id TEXT PRIMARY KEY,start TEXT,end TEXT,start_key TEXT,end_key TEXT,state TEXT,kind TEXT,device TEXT,created_utc TEXT)');q=new QueuedDispatch(file);
 const add=(i,start)=>db.prepare("INSERT INTO bookings VALUES(?,?,?,?,?,'QUEUED','LOCAL_INTENT',?,'fixture')").run(id(i),String(start),String(start+u),key(String(start)),key(String(start+u)),CONSTANT_DEVICE);
 const preference=value=>db.prepare('INSERT INTO booking_metadata VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value').run(keys.preference,value);
 const completed=(request,run)=>{
  const manifest={run,uuid:CONSTANT_DEVICE,start:request.start,end:request.end,queued_intent:request.id,birth:'2026-10-14T00:00:00.1234567Z'};
  assert.equal(q.reconcile({owned:true,state:'RUNNING',manifest,binding:manifest}),true);
  assert.equal(q.complete({schema:'pgn-source-range-completion-1',source_range_complete:true,independent_coverage:false,source_exit_code:0,run,queued_intent:request.id,start:request.start,end:request.end,source_positions:String(u)}),true);
 };
 assert.deepEqual(q.sessionMetadata(),{mode:'unselected',automaticAfterManual:false});
 for(let i=0;i<5;++i){q.close();q=new QueuedDispatch(file);assert.deepEqual(q.sessionMetadata(),{mode:'unselected',automaticAfterManual:false});}
 assert.equal(db.prepare('SELECT count(*) n FROM booking_metadata').get().n,0);
 assert.throws(()=>q.prepareAtBoundary(old,binding,csv),/Explicit mining start/);
 assert.equal(db.prepare('SELECT count(*) n FROM bookings').get().n,0);
 // Transport failure and a returned rejection never create an authorization.
 await assert.rejects(requestExplicitMiningStart(q,binding,async()=>{throw Error('simulated start transport failure')}),/transport failure/);
 assert.equal(q.sessionMetadata().mode,'unselected');
 await requestExplicitMiningStart(q,binding,async()=>({status:'OPERATOR_STOP_HELD'}));assert.equal(q.sessionMetadata().mode,'unselected');
 await requestExplicitMiningStart(q,binding,async()=>({status:'START_REQUESTED'}));assert.equal(q.sessionMetadata().mode,'automatic');
 assert.throws(()=>q.arm(id(99),binding),/Only queued/);assert.equal(q.sessionMetadata().mode,'automatic');
 assert.equal(db.prepare('SELECT count(*) n FROM bookings').get().n,0); // start chooses mode, not a look-ahead claim
 // All manual reservations win before Auto and follow configured zone then
 // numeric-block order, not insertion/re-reservation chronology.
 add(3,a+z+u);add(1,a);add(2,a+z);
 db.prepare('INSERT INTO queue_zone_order VALUES(?,?,?,?,?)').run(String(a+z),String(a+2n*z),key(String(a+z)),key(String(a+2n*z)),0);
 db.prepare('INSERT INTO queue_zone_order VALUES(?,?,?,?,?)').run(String(a),String(a+z),key(String(a)),key(String(a+z)),1);
 let request=q.prepareAtBoundary(old,binding,'invalid CSV is not consulted for manual work');assert.equal(request.id,id(2));assert.equal(q.sessionMetadata().mode,'manual-reservations');
 assert.equal(q.prepareAtBoundary(old,binding,csv).id,request.id);assert.throws(()=>q.prepareAtBoundary('f'.repeat(32),binding,csv),/Uncertain/);
 assert.equal(db.prepare('SELECT count(*) n FROM automatic_claims').get().n,0);
 // Manual interrupted provenance survives an explicit accepted resume, even
 // when no other queued reservation remains. Wrong bound identities reject.
 db.prepare("UPDATE bookings SET state='RELEASED' WHERE id IN(?,?)").run(id(1),id(3));
 assert.equal(q.beginExplicitStart({...binding,start:request.start,end:request.end,queued_intent:request.id}),sessionModes.manual);
 assert.throws(()=>q.beginExplicitStart({...binding,queued_intent:request.id}),/binding mismatch/);
 let mismatchedControl=0;await assert.rejects(requestExplicitMiningStart(q,{...binding,queued_intent:request.id},async()=>{++mismatchedControl;return {status:'START_REQUESTED'}}),/binding mismatch/);assert.equal(mismatchedControl,0);
 db.prepare("UPDATE bookings SET state='QUEUED' WHERE id IN(?,?)").run(id(1),id(3));
 q.close();q=new QueuedDispatch(file);assert.equal(q.prepareAtBoundary(old,binding,csv).id,request.id);
 await requestExplicitMiningStart(q,{...binding,start:request.start,end:request.end,queued_intent:request.id},async()=>{completed(request,'b'.repeat(32));return {status:'START_REQUESTED'}});assert.equal(q.sessionMetadata().mode,'manual-reservations');
 let current={...binding,start:request.start,end:request.end};
 request=q.prepareAtBoundary('b'.repeat(32),current,csv);assert.equal(request.id,id(3));completed(request,'c'.repeat(32));current={...binding,start:request.start,end:request.end};
 request=q.prepareAtBoundary('c'.repeat(32),current,csv);assert.equal(request.id,id(1));completed(request,'d'.repeat(32));current={...binding,start:request.start,end:request.end,queued_intent:request.id};
 const before=db.prepare('SELECT count(*) n FROM bookings').get().n;
 for(let i=0;i<5;++i){assert.equal(q.prepareAtBoundary('d'.repeat(32),current,'not CSV').action,'EndSession');q.close();q=new QueuedDispatch(file);}
 assert.equal(q.sessionMetadata().mode,'manual-reservations');assert.equal(db.prepare('SELECT count(*) n FROM bookings').get().n,before);
 // Metadata remains per-device, exact and fail-closed, including malformed
 // TEXT UTF8, blob/numeric-looking encoding and database read failure.
 const another='GPU-00000000-0000-0000-0000-000000000001';
 db.prepare('INSERT INTO booking_metadata VALUES(?,?)').run(sessionKeys(another).preference,'1');assert.equal(q.sessionMetadata().automaticAfterManual,false);
 for(const value of ['true','01','',new Uint8Array([49])]){
  preference(value);assert.equal(q.sessionMetadata(),null);assert.throws(()=>q.prepareAtBoundary('d'.repeat(32),current,csv),/metadata unavailable/);assert.equal(db.prepare('SELECT count(*) n FROM bookings').get().n,before);
 }
 db.prepare('UPDATE booking_metadata SET value=CAST(x\'80\' AS TEXT) WHERE key=?').run(keys.preference);assert.equal(q.sessionMetadata(),null);
 let controlCalls=0;await assert.rejects(requestExplicitMiningStart(q,current,async()=>{++controlCalls;return {status:'START_REQUESTED'}}),/metadata unavailable/);assert.equal(controlCalls,0);
 preference('0');db.prepare('UPDATE booking_metadata SET value=? WHERE key=?').run('AUTO',keys.mode);assert.equal(q.sessionMetadata(),null);
 db.prepare('UPDATE booking_metadata SET value=? WHERE key=?').run('manual-reservations',keys.mode);
 assert.equal(readSessionMetadata({prepare(){throw Error('read failed')}},CONSTANT_DEVICE),null);
 // OFF drain is a successful local policy outcome, not a physical control ACK;
 // settling the existing gate once suppresses later completed-run claims.
 const end=q.prepareAtBoundary('d'.repeat(32),current,csv);assert.equal(end.local_only,true);assert.equal(end.status,'MANUAL_RESERVATIONS_COMPLETE');assert.equal('applied' in end,false);
 const packet={state:'OFF',owned:false,operator_stop:false,pending_bytes:0,observed_utc:'2026-10-14T00:00:01Z',last_exit:{run:'d'.repeat(32),exit_code:0},manifest:{run:'d'.repeat(32),start:request.start,end:request.end},completion:{schema:'pgn-source-range-completion-1',run:'d'.repeat(32),source_range_complete:true,source_exit_code:0,start:request.start,end:request.end,source_positions:String(u)}};
 const gate=new CompletionAdvanceGate(),ticket=gate.claim(packet,'d'.repeat(32));assert.ok(ticket);assert.equal(gate.settle(ticket).held,true);assert.equal(gate.claim({...packet,observed_utc:'2026-10-14T00:00:02Z'},'d'.repeat(32)),null);
 assert.equal(gate.claim(packet,'d'.repeat(32),{paused:true}),null);assert.equal(gate.claim(packet,'d'.repeat(32),{enabled:false}),null);
 // ON allows exactly one JIT Auto claim, but does not erase Manual provenance;
 // a preference change after PREPARED cannot rebind that transition.
 preference('1');request=q.prepareAtBoundary('d'.repeat(32),current,csv);assert.equal(request.automatic,true);assert.equal(q.sessionMetadata().mode,'manual-reservations');preference('0');
 q.close();q=new QueuedDispatch(file);assert.equal(q.prepareAtBoundary('d'.repeat(32),current,csv).id,request.id);assert.equal(db.prepare('SELECT count(*) n FROM automatic_claims').get().n,1);
 completed(request,'e'.repeat(32));current={...binding,start:request.start,end:request.end,queued_intent:request.id};assert.equal(q.prepareAtBoundary('e'.repeat(32),current,csv).action,'EndSession');
 // A true new explicit start after complete drain is Auto even when the old
 // binding still names the completed intent. No claim is made by the start.
 const count=db.prepare('SELECT count(*) n FROM bookings').get().n;
 await requestExplicitMiningStart(q,current,async()=>({status:'SOURCE_FINISHED_ADVANCE_REQUIRED',run:'e'.repeat(32)}));assert.equal(q.sessionMetadata().mode,'automatic');assert.equal(db.prepare('SELECT count(*) n FROM bookings').get().n,count);
 request=q.prepareAtBoundary('e'.repeat(32),current,csv);assert.equal(request.start,String(100001n*u));assert.equal(request.end,String(100002n*u));assert.equal(request.automatic,true);assert.equal(db.prepare('SELECT count(*) n FROM bookings').get().n,count+1);
 // Real OFF drain boundary: source snapshot candidate, not a completion
 // manually inserted in advance, must close the last dispatched intent.
 completed(request,'f'.repeat(32));current={...binding,start:request.start,end:request.end};add(4,a+2n*z);request=q.prepareAtBoundary('f'.repeat(32),current,csv);assert.equal(request.id,id(4));
 const lastRun='1'.repeat(32),manifest={run:lastRun,uuid:CONSTANT_DEVICE,start:request.start,end:request.end,queued_intent:request.id,birth:'2026-10-14T00:00:05Z'};
 q.reconcile({owned:true,state:'RUNNING',manifest,binding:manifest});current={...binding,...manifest};
 const candidate={schema:'pgn-source-range-completion-1',run:lastRun,start:request.start,end:request.end,queued_intent:request.id,source_positions:String(u),source_range_complete:true,source_exit_code:0,independent_coverage:false,proof_status:'SOURCE_PRP_INDEPENDENT_PROOF_PENDING'};
 const sourcePacket={schema:'pgn-runtime-snapshot-1',state:'OFF',owned:false,operator_stop:false,pending_bytes:0,observed_utc:'2026-10-14T00:00:06Z',last_exit:{run:lastRun,exit_code:0},manifest,completion:{...candidate,run:'0'.repeat(32)},completion_candidate:candidate};
 assert.equal(db.prepare('SELECT state FROM dispatch_requests WHERE id=?').get(request.id).state,'STARTED');
 for(const changed of [{owned:true},{operator_stop:true},{pending_bytes:1},{last_exit:{run:lastRun,exit_code:1}},{completion_candidate:{...candidate,source_positions:'1'}},{completion_candidate:{...candidate,queued_intent:id(99)}},{completion_candidate:{...candidate,independent_coverage:true}},{manifest:{...manifest,uuid:'GPU-00000000-0000-0000-0000-000000000001'}}])assert.throws(()=>snapshotCompletionObservation({...sourcePacket,...changed},lastRun,current),/snapshot|binding/);
 for(const field of ['owned','operator_stop'])for(const value of [undefined,null,'false',0]){
  const poison={...sourcePacket,[field]:value};if(value===undefined)delete poison[field];assert.throws(()=>snapshotCompletionObservation(poison,lastRun,current),/snapshot/);
 }
 assert.throws(()=>snapshotCompletionObservation(sourcePacket,lastRun,current,'partial'),/snapshot/);
 assert.throws(()=>commitSnapshotCompletion(q,{run:lastRun,receipt:candidate}),/Validated/);
 const observation=snapshotCompletionObservation(sourcePacket,lastRun,current),ended=q.prepareAtBoundary(lastRun,current,csv),emitted=[];
 assert.equal(ended.action,'EndSession');assert.throws(()=>finishManualSession({complete(){throw Error('simulated SQLite commit failure')}},ended,observation,event=>emitted.push(event)),/commit failure/);assert.equal(emitted.length,0);
 const lastGate=new CompletionAdvanceGate(),lastTicket=lastGate.claim(sourcePacket,lastRun);assert.ok(lastTicket);
 finishManualSession(q,ended,observation,event=>emitted.push(event));lastGate.settle(lastTicket);
 assert.equal(db.prepare('SELECT state FROM dispatch_requests WHERE id=?').get(request.id).state,'SOURCE_FINISHED');assert.equal(lastGate.claim({...sourcePacket,observed_utc:'2026-10-14T00:00:07Z'},lastRun),null);
 assert.deepEqual(emitted.map(event=>event.type),['source_completion','status']);assert.equal(emitted[0].receipt.run,lastRun);assert.equal(emitted[0].receipt.independent_coverage,false);assert.equal(emitted[0].evidence_scope,'VERIFIED_SNAPSHOT_SOURCE_COMPLETION');assert.equal(emitted[0].community_updated,false);
 await requestExplicitMiningStart(q,current,async()=>({status:'SOURCE_FINISHED_ADVANCE_REQUIRED',run:lastRun}));assert.equal(q.sessionMetadata().mode,'automatic');
 // A new explicit completed start can also conserve the observed result
 // before choosing mode, when the passive OFF gate has not run yet.
 add(5,a+3n*z);request=q.prepareAtBoundary(lastRun,current,csv);const finalRun='2'.repeat(32),finalManifest={...manifest,run:finalRun,start:request.start,end:request.end,queued_intent:request.id};q.reconcile({owned:true,state:'RUNNING',manifest:finalManifest,binding:finalManifest});
 current={...binding,...finalManifest};const finalCandidate={...candidate,run:finalRun,start:request.start,end:request.end,queued_intent:request.id},finalPacket={...sourcePacket,manifest:finalManifest,last_exit:{run:finalRun,exit_code:0},completion_candidate:finalCandidate,observed_utc:'2026-10-14T00:00:08Z'};
 await requestExplicitMiningStart(q,current,async()=>({status:'SOURCE_FINISHED_ADVANCE_REQUIRED',run:finalRun}),async observedRun=>snapshotCompletionObservation(finalPacket,observedRun,current));assert.equal(q.sessionMetadata().mode,'automatic');assert.equal(db.prepare('SELECT state FROM dispatch_requests WHERE id=?').get(request.id).state,'SOURCE_FINISHED');
 console.log('PASS CLOSED ROOT549: exact shared v1 metadata; failed/rejected starts; explicit Auto; all manual zone/numeric order; interrupted provenance; PREPARED retry; OFF drain/restart; ON JIT fallback; malformed/read failure hold; no lookahead/control ACK/hot-loop');
}finally{q?.close();db?.close();fs.rmSync(temp,{recursive:true,force:true})}
