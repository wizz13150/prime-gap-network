import test from 'node:test';import assert from 'node:assert/strict';import fs from 'node:fs';import os from 'node:os';import path from 'node:path';
import {DatabaseSync} from 'node:sqlite';
import {EngineModuleRegistry,handleModuleRegistryCommand} from '../tools/engine-module-registry.mjs';
const descriptor={schema:'pgn-engine-module-1',id:'user-source',protocol:'phase0-source-1',host_refill:false,host_verification:false,whole_job_gpu_owned:true,single_low64_window:true,supported_high_word:'10',extension:{zero:0,unknown:null,literal:'$(do-not-execute)'}};
function fixture(fn){const root=fs.mkdtempSync(path.join(os.tmpdir(),'pgn-module-registry-')),file=path.join(root,'registry.sqlite'),registry=new EngineModuleRegistry(file);try{fn(registry,file)}finally{registry.close();fs.rmSync(root,{recursive:true,force:true});}}
test('read of absent registry is not a write; descriptor registration exact and idempotent',()=>fixture((r,file)=>{
 assert.deepEqual(r.page(),{items:[],next_cursor:null,latest_request:null});assert.equal(fs.existsSync(file),false);
 const one=r.register(descriptor),two=r.register(JSON.parse(JSON.stringify(descriptor)));
 assert.equal(one.created,true);assert.equal(two.created,false);assert.equal(one.module_key,two.module_key);
 assert.deepEqual(r.descriptor(one.module_key).capabilities,descriptor);assert.equal(one.artifact_sha256,null);assert.equal(one.gpu_control_calls,0);
 const artifactRef='C:/user engine/$(do-not-execute)/source.exe';
 const packageEntry=r.register({schema:'pgn-engine-package-1',capabilities:descriptor,artifact_sha256:'a'.repeat(64),artifact_ref:artifactRef});
 assert.notEqual(packageEntry.module_key,one.module_key);
 assert.equal(r.descriptor(packageEntry.module_key).artifact_ref,artifactRef);assert.equal(packageEntry.artifact_ref,artifactRef);
}));
test('multiple versions, unknown protocols and complete indexed pages remain inspectable',()=>fixture(r=>{
 for(let i=0;i<40;i++)r.register({...descriptor,id:'engine-'+i,protocol:i===3?'future-protocol':'phase0-source-1'});
 let after='',all=[];do{const page=r.page(after,7);all.push(...page.items);after=page.next_cursor;}while(after);
 assert.equal(all.length,40);assert.equal(new Set(all.map(x=>x.module_key)).size,40);assert(all.some(x=>x.protocol==='future-protocol'));
 assert.throws(()=>r.page('not-a-key'));assert.throws(()=>r.page('',0));
}));
test('request persistence/correlation never implies controller effect, lease or source mutation',()=>fixture((r,file)=>{
 const entry=r.register(descriptor),intent={request_id:'request-1',module_key:entry.module_key,device_id:'gpu-external-binding',owner:'explicit-user',expected_run:'original-run',boundary:'NEXT_JOB_ONLY',job:{start:'199999990000000000000',length:'1030792151040'}};
 const first=r.request(intent);assert.equal(first.state,'REQUESTED_NOT_ADMITTED');assert.equal(first.applied,false);assert.equal(first.reservation_changes,0);assert.equal(first.gpu_control_calls,0);
 assert.deepEqual(r.request(intent),first);assert.throws(()=>r.request({...intent,owner:'different'}));assert.deepEqual(r.receipt(intent.request_id),first);
 assert.throws(()=>r.request({...intent,request_id:'hot',boundary:'NOW'}));
 assert.throws(()=>r.request({...intent,request_id:'wrong-span',job:{start:'0',length:'1'}}));
 const other=new EngineModuleRegistry(file);try{assert.deepEqual(other.receipt(intent.request_id),first);assert.deepEqual(other.page().latest_request,first)}finally{other.close();}
}));
test('bad descriptors/artifact pins refused without silent normalization or execution',()=>fixture(r=>{
 assert.throws(()=>r.register({...descriptor,host_refill:0}));
 assert.throws(()=>r.register({schema:'pgn-engine-package-1',capabilities:descriptor,artifact_sha256:'not-a-digest'}));
 assert.throws(()=>r.register({schema:'pgn-engine-package-1',capabilities:descriptor,artifact_ref:'bad\0path'}));
 assert.throws(()=>r.register({schema:'pgn-engine-package-1',capabilities:descriptor,artifact_ref:'x'.repeat(2049)}));
 assert.throws(()=>r.register({...descriptor,extension:'x'.repeat(40000)}));assert.equal(r.page().items.length,0);
}));
test('command dispatch is correlated metadata only, with no executor verbs',()=>fixture(r=>{
 const add=handleModuleRegistryCommand(r,{action:'module_register',request_id:'import-1',package:descriptor,source_ref:'fixture'});
 assert.equal(add.request_id,'import-1');assert.equal(add.applied,false);assert.equal(add.page.items.length,1);
 const select=handleModuleRegistryCommand(r,{action:'module_select',request_id:'select-1',module_key:add.registration.module_key,device_id:'device',owner:'user',expected_run:'actual-run'});
 assert.equal(select.selection.state,'REQUESTED_NOT_ADMITTED');assert.equal(select.selection.intent.expected_run,'actual-run');
 assert.equal(select.gpu_control_calls,0);assert.equal(select.selection.applied,false);
 const receipt=handleModuleRegistryCommand(r,{action:'module_receipt',request_id:'inspect-1',selection_id:'select-1'});
 assert.equal(receipt.request_id,'inspect-1');assert.deepEqual(receipt.selection,select.selection);
 assert.throws(()=>handleModuleRegistryCommand(r,{action:'start',request_id:'not-executor'}));
 assert.throws(()=>handleModuleRegistryCommand(r,{action:'module_list'}));
}));
test('legacy catalogue schema migration preserves declarations and their stable keys',()=>fixture((r,file)=>{
 const legacy=new DatabaseSync(file);
 legacy.exec('CREATE TABLE modules(module_key TEXT PRIMARY KEY,module_id TEXT NOT NULL,descriptor_sha256 TEXT NOT NULL,artifact_sha256 TEXT,capabilities TEXT NOT NULL,source_ref TEXT NOT NULL,added_utc TEXT NOT NULL)');
 legacy.prepare('INSERT INTO modules VALUES(?,?,?,?,?,?,?)').run('f'.repeat(64),'legacy','e'.repeat(64),null,JSON.stringify(descriptor),'legacy-source','original-age');legacy.close();
 assert.equal(r.page().items[0].module_key,'f'.repeat(64));assert.equal(r.descriptor('f'.repeat(64)).added_utc,'original-age');
 assert.equal(r.descriptor('f'.repeat(64)).artifact_ref,null);assert.equal(r.register(descriptor).created,true);
 const same=r.register(descriptor);assert.equal(same.created,false);
}));
