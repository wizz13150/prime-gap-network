import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {DatabaseSync} from 'node:sqlite';
import {verifySourceResumeLineage,sourceResumeLineageProofMatches} from '../tools/source-resume-lineage.mjs';
import {legacyNativeDevice as device} from '../tools/native-device-target.mjs';

// CLOSED: owned temporary SQLite and public synthetic raw journals only. No
// user files, SSH, process control, live-client, producer/GPU or UI execution.
const runs=['a'.repeat(32),'b'.repeat(32),'c'.repeat(32)],booking='87654321-4321-4321-8321-cba987654321';
const first=(10n<<64n)+100n,length=1000n,end=first+length,label='C:\\PGN\\runtime\\current-job\\checkpoint',testBase=9007199254740993n;
const canonical=v=>Array.isArray(v)?'['+v.map(canonical).join(',')+']':v&&typeof v==='object'?'{'+Object.keys(v).sort().map(k=>JSON.stringify(k)+':'+canonical(v[k])).join(',')+'}':JSON.stringify(v);
function cp(run,positions,gaps,tests,batches,time){
 const bytes=Buffer.alloc(64);bytes.write('PGNCG001');[100n,10n,length,BigInt(positions),BigInt(tests),BigInt(gaps),BigInt(batches)].forEach((v,i)=>bytes.writeBigUInt64LE(v,8+8*i));
 const resume={run,start:String(first),end:String(end),length:String(length),positions:String(positions),nextStart:String(first+BigInt(positions)),remaining:String(length-BigInt(positions)),tests:String(tests),gaps:String(gaps),batches:String(batches),counterTimeNs:String(time),checkpointLabel:label,checkpointSha256:createHash('sha256').update(bytes).digest('hex')};
 return {schema:'pgn-source-resume-1',device:device.toLowerCase(),resume,checkpoint_base64:bytes.toString('base64')};
}
function receipt(run,previous,checkpoint,index){return {schema:'pgn-checkpoint-resume-epoch-1',origin:'RESUME_CHECKPOINT',request_id:index.toString(16).padStart(8,'0')+'-1234-4234-8234-123456789abc',device,booking_id:booking,previous_run:previous,run,observation_id:'11111111-2222-4333-8444-555555555555',predecessor:null,counter_basis:'PGNCG001_CUMULATIVE_BASELINE',raw_conservation_requirement:'LINEAGE_RAW_CONSERVATION_REQUIRED',checkpoint};}
const header=()=>`[phase0-gpu] engine=GPU_OWNED_CONTINUOUS descriptors=2 original_start=${first} length=${length} proof=GPU_MR12_GAP coverage=SOURCE_PRP_INDEPENDENT_PROOF_PENDING\n`;
const gap=(i=0)=>`[phase0-gpu] GAP merit=20.0000 gap=2 lower=${first+10n+BigInt(i%40)*10n} upper=${first+12n+BigInt(i%40)*10n} verified=1 proof=GPU_MR12_EULER_COMPOSITE record=off table=unknown unix_ts=1791936000 time_basis=source_batch_publication\n`;
function raw(count,{positions,gaps,tests,batches,time},complete=false){return header()+Array.from({length:count},(_,i)=>gap(i)).join('')+`[pgn-counters] time_ns=${time} positions=${positions} gaps=${gaps} tests=${tests} batches=${batches} processed_gaps=${gaps}\n`+(complete?`[phase0-gpu] gaps reported=${gaps} verification_failures=0 records_new=0\n[phase0-gpu] --- end ---\n`:`[phase0-gpu] stopped positions=${positions} checkpoint=${label}\n`);}
class Fixture {
 constructor(counts=[2,1,2]){
  this.dir=fs.mkdtempSync(path.join(os.tmpdir(),'pgn-lineage-'));this.db=new DatabaseSync(path.join(this.dir,'local-bookings.sqlite'));
  this.db.exec('CREATE TABLE dispatch_resume_epochs(run TEXT PRIMARY KEY,previous_run TEXT NOT NULL UNIQUE,booking_id TEXT NOT NULL,request_id TEXT NOT NULL UNIQUE,receipt TEXT NOT NULL);CREATE TABLE booking_metadata(key TEXT PRIMARY KEY,value TEXT NOT NULL);CREATE TABLE cursors(source TEXT PRIMARY KEY,offset INTEGER NOT NULL,run TEXT NOT NULL UNIQUE);');
  this.data=new Map();this.stats={reads:0,closed:0,maxRead:0};let total=0;
  for(let i=0;i<3;++i){total+=counts[i];const final={positions:i===2?1000:300*(i+1),gaps:total,tests:testBase+BigInt(i)*10n,batches:i+1,time:111*(i+1)};
   const checkpoint=cp(runs[i],final.positions===1000?999:final.positions,final.gaps,final.tests,final.batches,final.time);
   const m={schema:'pgn-native-process-1',run:runs[i],uuid:device,queued_intent:booking,start:String(first),end:String(end),stdout:`C:/PUBLIC-FIXTURE/${runs[i]}.stdout`,birth:'2026-10-14T00:00:00.1234567Z',pid:1234};
   if(i){const previous=this.data.get(runs[i-1]);const r=receipt(runs[i],runs[i-1],previous.checkpoint,i);this.putReceipt(r);m.resume_previous_run=r.previous_run;m.checkpoint_input_sha256=r.checkpoint.resume.checkpointSha256;}
   this.put(runs[i],m,raw(counts[i],final,i===2),checkpoint,final);
  }
  this.snapshot=this.fullSnapshot(runs[2]);
 }
 putReceipt(r){this.db.prepare('INSERT OR REPLACE INTO dispatch_resume_epochs VALUES(?,?,?,?,?)').run(r.run,r.previous_run,r.booking_id,r.request_id,canonical(r));}
 put(run,manifest,source,checkpoint,final){
  const file=path.join(this.dir,run+'.stdout'),key='mirror/'+run+'.stdout';fs.writeFileSync(file,source);this.data.set(run,{manifest,checkpoint,final,file,key});
  this.db.prepare('INSERT OR REPLACE INTO cursors VALUES(?,?,?)').run(key,fs.statSync(file).size,run);
  this.db.prepare('INSERT OR REPLACE INTO booking_metadata VALUES(?,?)').run('mining-source-manifest/v1/'+run,canonical(manifest));
 }
 rewrite(run,bytes){const d=this.data.get(run);fs.writeFileSync(d.file,bytes);this.db.prepare('UPDATE cursors SET offset=? WHERE run=?').run(fs.statSync(d.file).size,run);}
 fullSnapshot(run){const d=this.data.get(run),f=d.final;return {state:'OFF',owned:false,manifest:d.manifest,last_exit:{run,exit_code:0},source_completion_observation:{schema:'pgn-source-range-completion-1',run,queued_intent:booking,start:String(first),end:String(end),source:String(d.manifest.stdout),source_positions:String(f.positions),source_session_gaps:String(f.gaps),source_tests:String(f.tests),source_exit_code:0,source_range_complete:true,independent_coverage:false,proof_status:'SOURCE_PRP_INDEPENDENT_PROOF_PENDING',source_tests_complete:true,source_position_evidence:'final_counter'}};}
 stoppedSnapshot(run){const d=this.data.get(run);return {state:'OFF',owned:false,manifest:d.manifest,last_exit:{run,exit_code:0},checkpoint_evidence:{schema:'pgn-stop-checkpoint-evidence-1',status:'captured',run,checkpoint_label:label,checkpoint_base64:d.checkpoint.checkpoint_base64,source_tail_utf8:fs.readFileSync(d.file,'utf8')}};}
 cursor(run){const c=this.db.prepare('SELECT * FROM cursors WHERE run=?').get(run),m=this.db.prepare('SELECT typeof(value) kind,value FROM booking_metadata WHERE key=?').get('mining-source-manifest/v1/'+run);return c?{...c,manifest:m?.kind==='text'?JSON.parse(m.value):undefined}:undefined;}
 reader(run,short=65536){
  const d=this.data.get(run);if(!d)return null;const stat=fs.lstatSync(d.file),fd=fs.openSync(d.file,'r'),stats=this.stats;
  return {run,source:d.key,isFile:stat.isFile(),isSymbolicLink:stat.isSymbolicLink(),get size(){return fs.fstatSync(fd,{bigint:true}).size;},read(at,size){assert.equal(typeof at,'bigint');assert.ok(size>0&&size<=65536);stats.reads++;stats.maxRead=Math.max(stats.maxRead,size);const b=Buffer.alloc(Math.min(short,size));const n=fs.readSync(fd,b,0,b.length,Number(at));return b.subarray(0,n);},close(){fs.closeSync(fd);stats.closed++;}};
 }
 verify(snapshot=this.snapshot,readSource=run=>this.reader(run)){this.db.exec('BEGIN');try{return verifySourceResumeLineage({db:this.db,snapshot,readCursor:run=>this.cursor(run),readSource});}finally{this.db.exec('ROLLBACK');}}
 close(){this.db.close();fs.rmSync(this.dir,{recursive:true,force:true});}
}
function own(action,counts){const f=new Fixture(counts);try{return action(f);}finally{f.close();}}
function held(p,pattern){assert.equal(p.ready,false);assert.ok(p.error.length>0&&p.error.length<=400);if(pattern)assert.match(p.error,pattern);assert.equal(p.chain_sha256,'');}

test('three exact conserved journals count cumulative DELTAs; replay identical, SQLite unchanged',()=>own(f=>{
 const before=f.db.prepare('SELECT receipt FROM dispatch_resume_epochs ORDER BY run').all();const p=f.verify();assert.equal(p.ready,true,p.error);assert.equal(p.epochs,3);assert.equal(p.terminal,'complete');assert.equal(p.scope,'LOCAL_RAW_LINEAGE_COUNTER_DELTAS_NOT_INDEPENDENT_PRIME_COVERAGE');assert.match(p.chain_sha256,/^[0-9a-f]{64}$/);assert.deepEqual(f.verify(),p);assert.deepEqual(f.db.prepare('SELECT receipt FROM dispatch_resume_epochs ORDER BY run').all(),before);assert.equal(f.stats.closed,6);
}));
test('short reads and split multibyte UTF8 preserve exact framing/hash',()=>own(f=>{
 const d=f.data.get(runs[1]);f.rewrite(runs[1],fs.readFileSync(d.file,'utf8').replace('[pgn-counters]','diagnostic € source\n[pgn-counters]'));
 const normal=f.verify(),split=f.verify(f.snapshot,r=>f.reader(r,2));assert.equal(normal.ready,true,normal.error);assert.deepEqual(split,normal);
}));
test('final stopped capture remains explicitly stopped, root needs no epoch table but needs provenance',()=>own(f=>{
 const partial=f.verify(f.stoppedSnapshot(runs[1]));assert.equal(partial.ready,true,partial.error);assert.equal(partial.terminal,'stopped');assert.equal(partial.epochs,2);
 f.db.exec('DROP TABLE dispatch_resume_epochs');const root=f.verify(f.stoppedSnapshot(runs[0]));assert.equal(root.ready,true,root.error);assert.equal(root.epochs,1);
 f.db.prepare('DELETE FROM booking_metadata WHERE key=?').run('mining-source-manifest/v1/'+runs[0]);held(f.verify(f.stoppedSnapshot(runs[0])),/manifest/);
}));
test('zero GAPs is known zero, not permission to omit old journal or intermediate receipt',()=>own(f=>{
 assert.equal(f.verify().ready,true);f.db.prepare('DELETE FROM dispatch_resume_epochs WHERE run=?').run(runs[1]);held(f.verify(),/unknown origin/);
},[0,0,0]));
test('cached foreign completion never blocks current stopped or complete proof, snapshots unchanged',()=>own(f=>{
 for(const key of ['completion','completion_candidate','source_completion_observation']){
  for(const snapshot of [f.stoppedSnapshot(runs[1]),f.fullSnapshot(runs[2])]){
   // Preserve the genuine current receipt when the cache occupies its field.
   if(key==='source_completion_observation'&&snapshot.source_completion_observation)snapshot.completion_candidate=snapshot.source_completion_observation;
   snapshot[key]=f.fullSnapshot(runs[0]).source_completion_observation;
   const before=canonical(snapshot),p=f.verify(snapshot);assert.equal(p.ready,true,p.error);
   assert.equal(p.terminal,snapshot.manifest.run===runs[1]?'stopped':'complete');assert.equal(canonical(snapshot),before);
  }
 }
}));
test('malformed completion objects or foreign run identifiers hold, never look like absent caches',()=>own(f=>{
 for(const candidate of [false,7,'receipt',[],{}, {run:'A'.repeat(32)}, {run:runs[0]+'\n'}, {run:7}, {run:null}]){
  const s=f.stoppedSnapshot(runs[1]);s.completion=candidate;const before=canonical(s);held(f.verify(s));assert.equal(canonical(s),before);
 }
}));
test('every current completion remains mandatory and contradictory receipts hold',()=>own(f=>{
 const good=f.snapshot.source_completion_observation;
 for(const key of ['completion','completion_candidate']){
  for(const delta of [{end:String(end+1n)},{source_session_gaps:'999'}, {source_range_complete:false}, {schema:'other'}]){
   const s={...f.snapshot,[key]:{...good,...delta}},before=canonical(s);held(f.verify(s));assert.equal(canonical(s),before);
  }
  const s={...f.snapshot,[key]:{...good}};assert.equal(f.verify(s).ready,true);
 }
}));
test('missing raw, false cursor, suffix truncation and duplicate replay rows hold',()=>{
 own(f=>{const saved=f.data.get(runs[0]);f.data.delete(runs[0]);held(f.verify(),/reader/);f.data.set(runs[0],saved);});
 own(f=>{f.db.prepare('UPDATE cursors SET offset=offset-1 WHERE run=?').run(runs[0]);held(f.verify(),/cursor/);});
 own(f=>{const d=f.data.get(runs[0]);f.rewrite(runs[0],fs.readFileSync(d.file).subarray(0,-1));held(f.verify(),/incomplete/);});
 own(f=>{const d=f.data.get(runs[2]);f.rewrite(runs[2],fs.readFileSync(d.file,'utf8').replace('[pgn-counters]',gap()+'[pgn-counters]'));held(f.verify(),/DELTA/);});
});
test('swapped valid child checkpoint tuple fails against actual old journal, not just JSON',()=>own(f=>{
 const d=f.data.get(runs[1]),r=receipt(runs[2],runs[1],cp(runs[1],d.final.positions,d.final.gaps,d.final.tests+1n,d.final.batches,d.final.time),2);f.putReceipt(r);
 const c=f.data.get(runs[2]);c.manifest.checkpoint_input_sha256=r.checkpoint.resume.checkpointSha256;f.db.prepare('UPDATE booking_metadata SET value=? WHERE key=?').run(canonical(c.manifest),'mining-source-manifest/v1/'+runs[2]);held(f.verify(),/counter\/checkpoint\/receipt/);
}));
test('malformed UTF8, oversized row, endpoint width, incomplete publication and wrong stop tuple hold',()=>{
 own(f=>{const d=f.data.get(runs[0]);f.rewrite(runs[0],Buffer.concat([Buffer.from('bad '),Buffer.from([0xff]),Buffer.from('\n'),fs.readFileSync(d.file)]));held(f.verify());});
 own(f=>{const d=f.data.get(runs[0]);f.rewrite(runs[0],Buffer.from('x'.repeat(32769)+'\n'+fs.readFileSync(d.file,'utf8')));held(f.verify(),/32768/);});
 own(f=>{const d=f.data.get(runs[0]);f.rewrite(runs[0],fs.readFileSync(d.file,'utf8').replace(' gap=2 ',' gap=3 '));held(f.verify(),/endpoint/);});
 own(f=>{const d=f.data.get(runs[0]);f.rewrite(runs[0],fs.readFileSync(d.file,'utf8').replace('[phase0-gpu] stopped positions=300','[phase0-gpu] stopped positions=301'));held(f.verify(),/footer/);});
 own(f=>{const s=f.stoppedSnapshot(runs[1]);s.checkpoint_evidence.source_tail_utf8+='\n';held(f.verify(s),/suffix/);});
 own(f=>{const d=f.data.get(runs[2]),bad={...d.final,tests:testBase-1n};f.rewrite(runs[2],raw(2,bad,true));f.snapshot.source_completion_observation.source_tests=String(bad.tests);held(f.verify(),/regressed/);});
});
test('source handle checks close on failure and no whole-journal size cap',()=>{
 own(f=>{const n=f.stats.closed;held(f.verify(f.snapshot,r=>{const x=f.reader(r);x.isSymbolicLink=true;return x;}),/regular-file/);assert.equal(f.stats.closed,n+1);});
 own(f=>{
  f.db.exec('DROP TABLE dispatch_resume_epochs');const count=5001,final={positions:1000,gaps:count,tests:testBase,batches:1,time:111};
  const m=f.data.get(runs[0]).manifest;f.put(runs[0],m,raw(count,final,true),cp(runs[0],999,count,testBase,1,111),final);const p=f.verify(f.fullSnapshot(runs[0]));assert.equal(p.ready,true,p.error);assert.ok(f.stats.reads>10);assert.ok(f.stats.maxRead<=65536);assert.equal(p.epochs,1);
 });
});
test('cycle of individually well-formed epoch receipts is refused',()=>own(f=>{
 f.db.exec('DELETE FROM dispatch_resume_epochs');
 for(let i=0;i<2;++i){const current=runs[i],previous=runs[1-i],checkpoint=cp(previous,300,0,testBase,1,111),r=receipt(current,previous,checkpoint,i+10);f.putReceipt(r);const m={...f.data.get(current).manifest,resume_previous_run:previous,checkpoint_input_sha256:checkpoint.resume.checkpointSha256};const final={positions:300,gaps:0,tests:testBase,batches:1,time:111};f.put(current,m,raw(0,final),cp(current,300,0,testBase,1,111),final);}
 held(f.verify(f.stoppedSnapshot(runs[0])),/cycle/);
}));
test('gate ABI strictly matches identity, safe count/hash and complete vs stopped (not authenticity)',()=>own(f=>{
 const p=f.verify(),identity={run:runs[2],booking_id:booking,start:String(first),end:String(end)};assert.equal(sourceResumeLineageProofMatches(p,identity),true);assert.ok(p.epochs>=2);
 for(const change of [{schema:'other'},{ready:'true'},{scope:'coverage'},{terminal:'stopped'},{run:runs[0]},{booking_id:'unknown'},{start:'0'+String(first)},{end:String(end+1n)},{epochs:0},{epochs:Number.MAX_SAFE_INTEGER+1},{chain_sha256:p.chain_sha256.toUpperCase()},{error:'held'}])assert.equal(sourceResumeLineageProofMatches({...p,...change},identity),false);
 const s=f.verify(f.stoppedSnapshot(runs[1])),sid={...identity,run:runs[1]};assert.equal(sourceResumeLineageProofMatches(s,sid),false);assert.equal(sourceResumeLineageProofMatches(s,sid,'stopped'),true);
 assert.equal(sourceResumeLineageProofMatches(p,{...identity,start:'0'+String(first)}),false);assert.equal(sourceResumeLineageProofMatches(p,identity,'unknown'),false);
}));
