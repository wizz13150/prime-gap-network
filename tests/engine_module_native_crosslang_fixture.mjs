// TEST ONLY: real old Node registry, own fresh explicit directory. No executor.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';
import {DatabaseSync} from 'node:sqlite';
import {EngineModuleRegistry} from '../tools/engine-module-registry.mjs';
import {observeEngineModule,createEngineModuleRequest} from '../tools/engine-module-contract.mjs';
const [mode,arg]=process.argv.slice(2);assert(arg&&path.isAbsolute(arg),'Explicit absolute fixture directory');const root=path.resolve(arg);
const json=value=>JSON.stringify(value);let checks=0;const check=(a,b)=>{++checks;assert.deepEqual(a,b);};
const base={schema:'pgn-engine-module-1',id:'fixture-module',protocol:'phase0-source-1',host_refill:false,host_verification:false,whole_job_gpu_owned:true,single_low64_window:true,supported_high_word:'10'};
if(mode==='--prepare'){
 assert(!fs.existsSync(root),'Never overwrite/reinitialize fixture');fs.mkdirSync(root);
 const inputs=[base,{...base,id:'future',protocol:'future-protocol',extension:{zero:0,off:false,nil:null,literal:'$(never-execute)'}}];
 const unicode={...base,id:'unicode-\ud800',extension:{'\ue000':'private','\ud800\udc00':'supplementary','\ud800':'lonehigh','\udc00':'lonelow','0':0,'10':10,'2':2,strings:['\u0000\b\f\n\r\t"\\/','\u2028\u2029','\ud800','\udc00','a\ud800b']}};
 inputs.push({schema:'pgn-engine-package-1',capabilities:unicode,artifact_sha256:'AB'.repeat(32),artifact_ref:'C:/opaque/\ud800/$(never-execute).exe'});
 const numbers=[-0,0,1e-7,1e-6,1e20,1e21,1.0000000000000001e18,Number.MIN_VALUE,Number.MAX_VALUE,2.2250738585072014e-308,9007199254740992,1.2345678901234567];
 let seed=0x123456789abcdef0n;const bytes=Buffer.alloc(8);
 for(let i=0;i<4096;i++){seed=(seed*6364136223846793005n+1442695040888963407n)&((1n<<64n)-1n);bytes.writeBigUInt64LE(seed);const d=bytes.readDoubleLE();if(Number.isFinite(d))numbers.push(d);}
 for(let i=0;i<numbers.length;i+=64)inputs.push({...base,id:'numbers-'+i,extension:{numbers:numbers.slice(i,i+64)}});
 const registry=new EngineModuleRegistry(path.join(root,'node.sqlite')),db=new DatabaseSync(path.join(root,'node.sqlite'));
 const cases=[];
 for(const [i,input] of inputs.entries()){
  const source='original-source-'+i+'\u0000\ud800',registration=registry.register(input,source);
  const intent={request_id:'cross-request-'+i,module_key:registration.module_key,device_id:'opaque-device',owner:'explicit-user',expected_run:'opaque-original-run',boundary:'NEXT_JOB_ONLY',extension:{n:1e-7,empty:null}};
  if(i===0)intent.job={start:String(10n<<64n),length:'30'};
  const receipt=registry.request(intent),row=registry.descriptor(registration.module_key);
  const canonical=db.prepare('SELECT capabilities FROM modules WHERE module_key=?').get(registration.module_key).capabilities;
  cases.push({input,source,registration,intent,receipt,row,canonical});
 }
 const direct=['0','-0','1e-7','1e-6','1e20','1e21','9007199254740993','9223372036854775807','1000000000000000128','5e-324','1.7976931348623157e308'].map(raw=>({raw,canonical:json(JSON.parse(raw))}));
 const observation=observeEngineModule(base,{source:'actual fixture source',observed_utc:'2026-10-15T00:00:00.1234567Z',artifact_sha256:null,unknown:{n:1e21}});
 const pureRequest=createEngineModuleRequest(observation,{request_id:'pure-request',device_id:'opaque-device',owner:'explicit-user',start:String(10n<<64n),length:'30'});
 fs.writeFileSync(path.join(root,'goldens.json'),json({schema:'pgn-engine-module-native-crosslang-test-1',fixture_test_only:true,node:process.version,cases,direct,observation,pureRequest}));
 registry.close();db.close();console.log(`NODE_MODULE_GOLDENS ${cases.length} real registry declarations; ${numbers.length} finite binary64 values; no engine/GPU/process`);
}else if(mode==='--prepare-deep'){
 assert(fs.existsSync(path.join(root,'goldens.json'))&&!fs.existsSync(path.join(root,'deep-golden.json')),'Existing TESTONLY fixture, one new deep case');
 let nested=0;for(let i=0;i<1025;i++)nested={child:nested};
 const descriptor={...base,id:'actual-node-depth1025',extension:nested},registry=new EngineModuleRegistry(path.join(root,'node.sqlite'));
 const registration=registry.register(descriptor,'original deep source'),row=registry.descriptor(registration.module_key),db=new DatabaseSync(path.join(root,'node.sqlite'));
 const canonical=db.prepare('SELECT capabilities FROM modules WHERE module_key=?').get(registration.module_key).capabilities;
 const intent={request_id:'deep-node-request',module_key:registration.module_key,device_id:'opaque-device',owner:'explicit-user',expected_run:'opaque-run',boundary:'NEXT_JOB_ONLY'};
 const receipt=registry.request(intent),metadata={...row};delete metadata.capabilities;
 fs.writeFileSync(path.join(root,'deep-golden.json'),json({schema:'pgn-engine-module-depth-test-1',fixture_test_only:true,depth:1025,registration,metadata,canonical,intent,receipt}));
 registry.close();db.close();console.log('NODE_MODULE_DEEP1025 real oldregistry accepted/persisted; one metadata-only fixture, no reinitialize');
}else if(mode==='--verify-deep'){
 const expected=JSON.parse(fs.readFileSync(path.join(root,'deep-golden.json'),'utf8')),native=JSON.parse(fs.readFileSync(path.join(root,'deep-native-receipt.json'),'utf8'));
 assert(expected.fixture_test_only===true);const registry=new EngineModuleRegistry(path.join(root,'node.sqlite'));
 const db=new DatabaseSync(path.join(root,'node.sqlite'));
 check(registry.receipt('deep-native-request'),native);check(db.prepare('SELECT capabilities FROM modules WHERE module_key=?').get(expected.registration.module_key).capabilities,expected.canonical);
 const row=registry.descriptor(expected.registration.module_key);delete row.capabilities;check(row,expected.metadata);
 registry.close();db.close();console.log(`NODE_MODULE_DEEP_NATIVE_REOPEN ${checks} checks / 0 failures; metadata intent NOT engine admission`);
}else if(mode==='--verify'){
 const golden=JSON.parse(fs.readFileSync(path.join(root,'goldens.json'),'utf8')),native=JSON.parse(fs.readFileSync(path.join(root,'native-results.json'),'utf8'));
 assert(golden.fixture_test_only===true&&native.fixture_test_only===true);
 const registry=new EngineModuleRegistry(path.join(root,'native.sqlite')),db=new DatabaseSync(path.join(root,'native.sqlite'));
 check(native.cases.length,golden.cases.length);
 for(let i=0;i<golden.cases.length;i++){
  const expected=golden.cases[i],observed=native.cases[i];
  check(registry.descriptor(expected.registration.module_key),observed.row);
  check(db.prepare('SELECT capabilities FROM modules WHERE module_key=?').get(expected.registration.module_key).capabilities,expected.canonical);
  check(registry.receipt(expected.intent.request_id),observed.receipt);
  check(registry.request(expected.intent),observed.receipt); // same exact replay, original date
  const again=registry.register(expected.input,'MUST-NOT-REPLACE-ORIGINAL');check(again,{...expected.registration,created:false});
  check(registry.descriptor(again.module_key),observed.row); // source/age not rewritten
 }
 check(registry.page('',128).items.length,golden.cases.length);
 registry.close();db.close();console.log(`NODE_MODULE_NATIVE_REOPEN ${checks} checks / 0 failures; actual old registry reads native SQLite`);
}else throw Error('Only explicit --prepare / --verify fixture actions');
