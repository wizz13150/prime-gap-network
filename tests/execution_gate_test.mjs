import test from 'node:test';import assert from 'node:assert/strict';import fs from 'node:fs';import os from 'node:os';import path from 'node:path';import {createHash} from 'node:crypto';
import {evaluateAction,admitRemoteScript} from '../tools/execution-gate.mjs';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const root=fs.mkdtempSync(path.join(os.tmpdir(),'pgn-action-gate-'));
function fixture(name,value){const bytes=Buffer.from(JSON.stringify(value));fs.writeFileSync(path.join(root,name),bytes,{flag:'wx'});return {path:name,sha256:createHash('sha256').update(bytes).digest('hex')};}
const closed=fixture('closed.json',{state:'CLOSED_EXACT_NO_NET_GAIN'}),witness=fixture('witness.json',{status:'PASS'});
const state={schema:'pgn-execution-control-1',writer:'root',revision:1,max_trials_without_delivery:2,
 branches:[{id:'gpu',requests:['ROOT463'],ready:true,trials_without_delivery:7,delivery_layers:['end-to-end-performance']},{id:'ui',requests:['ROOT451'],ready:true,trials_without_delivery:0,delivery_layers:['user-path']}],
 closed_trials:[{fingerprint:'same-kernel',receipt:closed}]};
const action={base_revision:1,kind:'trial',branch:'gpu',requests:['ROOT463'],fingerprint:'new-kernel'};
test('non-delivery trial streak requires an independent ready delivery, not a global stop',()=>{
 assert.throws(()=>evaluateAction(root,state,action),/PIVOT_TO_READY_DELIVERY/);
 assert.equal(evaluateAction(root,state,{...action,kind:'integrate',branch:'ui',requests:['ROOT451']}).status,'ADMITTED');
 assert.equal(evaluateAction(root,state,{...action,kind:'observe'}).status,'ADMITTED');
});
test('a renamed variant does not escape the parent branch delivery check',()=>assert.throws(()=>evaluateAction(root,state,{...action,fingerprint:'totally-renamed'}),/PIVOT_TO_READY_DELIVERY/));
test('closed fingerprint needs a verified changed relation, not a prose assertion',()=>{
 const s={...state,branches:state.branches.map(x=>({...x,trials_without_delivery:0}))};
 assert.throws(()=>evaluateAction(root,s,{...action,fingerprint:'same-kernel'}),/CLOSED_FINGERPRINT/);
 const fake=fixture('fake-premise.json',{status:'PASS'});
 assert.throws(()=>evaluateAction(root,s,{...action,fingerprint:'same-kernel',changed_premise:fake}),/UNVERIFIED_PREMISE/);
 const verified=fixture('premise.json',{schema:'pgn-causal-premise-receipt-1',writer:'root',status:'VERIFIED',fingerprint:'same-kernel',changed_relation:'New measured data boundary'});
 assert.equal(evaluateAction(root,s,{...action,fingerprint:'same-kernel',changed_premise:verified}).status,'ADMITTED');
});
test('component PASS cannot fulfill a UI user-path obligation',()=>{
 const ref=fixture('component.json',{schema:'pgn-user-delivery-receipt-1',writer:'root',status:'PASS',layer:'component',request_ids:['ROOT451'],checked_paths:['unit test'],accepted_evidence:[witness]});
 assert.throws(()=>evaluateAction(root,state,{...action,kind:'deliver',branch:'ui',requests:['ROOT451'],receipt:ref}),/COMPONENT_IS_NOT_DELIVERY/);
});
test('delivery needs actual pinned evidence and an identified tested user path',()=>{
 const ref=fixture('delivery.json',{schema:'pgn-user-delivery-receipt-1',writer:'root',status:'PASS',layer:'user-path',request_ids:['ROOT451'],checked_paths:['3060 ON/OFF with live observer'],accepted_evidence:[witness]});
 assert.equal(evaluateAction(root,state,{...action,kind:'deliver',branch:'ui',requests:['ROOT451'],receipt:ref}).status,'ADMITTED');
 fs.appendFileSync(path.join(root,'witness.json'),' ');
 assert.throws(()=>evaluateAction(root,state,{...action,kind:'deliver',branch:'ui',requests:['ROOT451'],receipt:ref}),/EVIDENCE_CHANGED/);
});
test('unknown request, stale control and scope escape are refused',()=>{
 assert.throws(()=>evaluateAction(root,state,{...action,base_revision:0}),/STALE_ACTION/);
 assert.throws(()=>evaluateAction(root,state,{...action,requests:['ROOT9999']}),/REQUEST_BINDING/);
 assert.throws(()=>evaluateAction(root,state,{...action,kind:'deliver',branch:'ui',requests:['ROOT451'],receipt:{path:'../outside.json',sha256:'0'.repeat(64)}}),/EVIDENCE_SCOPE/);
});
test('remote launch requires pinned intent; script substitution and tunneling are refused',()=>{
 const control=fixture('control.json',state),source=path.join(root,'readonly.ps1');fs.writeFileSync(source,'# exact private test source');
 const script_sha256=createHash('sha256').update(fs.readFileSync(source)).digest('hex');
 const intent=fixture('intent.json',{control,action:{...action,kind:'observe',script_sha256}});
 assert.equal(admitRemoteScript(root,source,path.join(root,intent.path)).status,'ADMITTED');
 assert.throws(()=>admitRemoteScript(root,source,null),/EXECUTION_INTENT_REQUIRED/);
 fs.appendFileSync(source,'\n# changed');assert.throws(()=>admitRemoteScript(root,source,path.join(root,intent.path)),/SCRIPT_CHANGED/);
});
test('actual launcher refuses before config access or SSH when intent is missing',()=>{
 const script=new URL('../tools/run-owned-remote-script.mjs',import.meta.url);
 const result=spawnSync(process.execPath,[fileURLToPath(script),'MISSING_PRIVATE_CONFIG','MISSING_SOURCE','C:/PGN/tools/refused-test.ps1'],{encoding:'utf8',windowsHide:true});
 assert.notEqual(result.status,0);assert.match(result.stderr,/EXECUTION_INTENT_REQUIRED/);assert.doesNotMatch(result.stderr,/ENOENT|ssh|scp/);
});
