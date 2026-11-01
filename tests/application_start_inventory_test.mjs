import test from 'node:test';import assert from 'node:assert/strict';import fs from 'node:fs';
import {applicationStartInventory} from '../tools/application-start-inventory.mjs';import {SerializedMutations} from '../tools/serialized-mutations.mjs';import {ApplicationCloseProtocol} from '../tools/application-close-protocol.mjs';
// CLOSED: fake observations and the real process-local writer; no relay import.
const id='12345678-1234-4234-8234-123456789abc',one='11111111-2222-4333-8444-555555555555',two='99999999-2222-4333-8444-555555555555';
const command=inventory=>({action:'application_start_inventory',request_id:id,inventory_id:inventory});
const raw=()=>({schema:'pgn-runtime-snapshot-1',owned:false,has_checkpoint:false,manifest:null,chunk:'b3JpZ2luYWwK',offset:10,next_offset:19,pending_bytes:0});
test('fresh snapshot waits existing writer barrier and conserves raw bytes/checkpoint facts without changing shared cursor',async()=>{
 const writer=new SerializedMutations(),close=new ApplicationCloseProtocol({mutations:writer});let release,enter,reads=0,sharedOffset=10;const entered=new Promise(resolve=>enter=resolve),finish=new Promise(resolve=>release=resolve);
 const active=close.run('advance',async()=>{enter();await finish});await entered;const snapshot=raw();const result=applicationStartInventory(command(one),{runMutation:operation=>close.run('snapshot',operation),readSnapshot:async()=>{reads++;return snapshot}});assert.equal(reads,0);release();await active;
 const observed=await result;assert.equal(observed.accepted,true);assert.equal(observed.request_id,id);assert.equal(observed.inventory_id,one);assert.equal(observed.snapshot,snapshot);assert.equal(sharedOffset,10);assert.equal(snapshot.has_checkpoint,false);assert.equal(snapshot.chunk,'b3JpZ2luYWwK');
});
test('each new nonce acquires fresh data, unknown facts stay unknown, and close freeze permits readonly inspection only',async()=>{
 const close=new ApplicationCloseProtocol({mutations:new SerializedMutations()});await close.prepare(id);let reads=0;const options={runMutation:operation=>close.run('snapshot',operation),readSnapshot:async()=>{reads++;const snapshot=raw();delete snapshot.has_checkpoint;return snapshot}};
 const first=await applicationStartInventory(command(one),options),second=await applicationStartInventory(command(two),options);assert.equal(reads,2);assert.equal(first.inventory_id,one);assert.equal(second.inventory_id,two);assert.equal('has_checkpoint'in second.snapshot,false);assert.throws(()=>close.run('start_prepared',()=>{}),/freeze/);assert.equal(close.frozen,true);
});
test('malformed correlation, read failures and schema mismatch return correlated unknown with no cached/Ready result',async()=>{
 let reads=0;const options={runMutation:operation=>new SerializedMutations().run(operation),readSnapshot:async()=>{reads++;throw Error('fresh transport unavailable')}};
 for(const changed of [{request_id:null},{inventory_id:undefined},{inventory_id:'00000000-0000-0000-0000-000000000000'},{inventory_id:id.toUpperCase()},{action:'start'}]){const result=await applicationStartInventory({...command(one),...changed},options);assert.equal(result.accepted,false);assert.equal(result.snapshot,null);}assert.equal(reads,0);
 let result=await applicationStartInventory(command(one),options);assert.equal(result.accepted,false);assert.equal(result.request_id,id);assert.equal(result.inventory_id,one);assert.match(result.error,/transport unavailable/);
 result=await applicationStartInventory(command(two),{...options,readSnapshot:async()=>({schema:'wrong',owned:false})});assert.equal(result.accepted,false);assert.equal(result.snapshot,null);
});
test('actual relay handler transports inventory without consumeSnapshot and before frozen mutation rejection',()=>{
 const source=fs.readFileSync(new URL('../tools/live-client.mjs',import.meta.url),'utf8'),start=source.indexOf("if(command.action==='application_start_inventory')"),guard=source.indexOf('applicationClose.assertAllowed(command.action)');assert.ok(start>0&&start<guard);assert.match(source.slice(start,guard),/readSnapshot:\(\)=>remote\('snapshot'\)/);assert.doesNotMatch(source.slice(start,guard),/consumeSnapshot/);
});
test('start inventory exposes exact logical conservation but never invokes a completion finalizer or mutates a cursor',async()=>{
 const writer=new SerializedMutations(),run='a'.repeat(32),snapshot={...raw(),manifest:{run},next_offset:15,pending_bytes:5};let writes=0,cursor=20;
 const options={runMutation:operation=>writer.run(operation),readSnapshot:async()=>snapshot,readSourceState:()=>({residentViewer:false,currentRun:run,fragment:'',transportOffset:20,readCursor:source=>({source,run,offset:cursor}),readMirrorStat:()=>({isFile:true,isSymbolicLink:false,size:20})}),finalizeCompletedSource:()=>{writes++;}};
 let result=await applicationStartInventory(command(one),options);assert.equal(result.source_conservation.ready,true);assert.equal(result.source_conservation.required_end,20);assert.equal(result.snapshot,snapshot);assert.equal(writes,0);assert.equal(cursor,20);
 cursor=19;result=await applicationStartInventory(command(two),options);assert.equal(result.source_conservation.ready,false);assert.equal(writes,0);assert.equal(cursor,19);
});
