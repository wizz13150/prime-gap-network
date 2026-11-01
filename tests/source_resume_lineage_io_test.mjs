import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {DatabaseSync} from 'node:sqlite';
import {SourceResumeLineageVerifier} from '../tools/source-resume-lineage-io.mjs';
import {legacyNativeDevice as device} from '../tools/native-device-target.mjs';

// CLOSED: actual readonly DB handles and own temp raw files only. No user DB,
// process control, network, GPU, UI, source repair or service is invoked.
const old='a'.repeat(32),current='b'.repeat(32),booking='87654321-4321-4321-8321-cba987654321';
const first=(10n<<64n)+100n,length=1000n,end=first+length,tests=9007199254740993n,label='C:\\PUBLIC-FIXTURE\\checkpoint';
const canonical=v=>Array.isArray(v)?'['+v.map(canonical).join(',')+']':v&&typeof v==='object'?'{'+Object.keys(v).sort().map(k=>JSON.stringify(k)+':'+canonical(v[k])).join(',')+'}':JSON.stringify(v);
function checkpoint(){
 const bytes=Buffer.alloc(64);bytes.write('PGNCG001');[100n,10n,length,300n,tests,2n,1n].forEach((v,i)=>bytes.writeBigUInt64LE(v,8+8*i));
 const resume={run:old,start:String(first),end:String(end),length:String(length),positions:'300',nextStart:String(first+300n),remaining:'700',tests:String(tests),gaps:'2',batches:'1',counterTimeNs:'111',checkpointLabel:label,checkpointSha256:createHash('sha256').update(bytes).digest('hex')};
 return {schema:'pgn-source-resume-1',device:device.toLowerCase(),resume,checkpoint_base64:bytes.toString('base64')};
}
const header=()=>`[phase0-gpu] engine=GPU_OWNED_CONTINUOUS descriptors=2 original_start=${first} length=${length} proof=GPU_MR12_GAP coverage=SOURCE_PRP_INDEPENDENT_PROOF_PENDING\n`;
const gap=()=>`[phase0-gpu] GAP merit=20.0000 gap=2 lower=${first+10n} upper=${first+12n} verified=1 proof=GPU_MR12_EULER_COMPOSITE record=off table=unknown\n`;
class Fixture {
 constructor(){
  this.dir=fs.mkdtempSync(path.join(os.tmpdir(),'pgn-lineage-io-'));this.sources=path.join(this.dir,'source-journals');fs.mkdirSync(this.sources);
  this.bookingFile=path.join(this.dir,'local-bookings.sqlite');this.historyFile=path.join(this.dir,'scientific-history.sqlite');
  this.booking=new DatabaseSync(this.bookingFile);this.history=new DatabaseSync(this.historyFile);
  this.booking.exec('PRAGMA journal_mode=WAL;CREATE TABLE booking_metadata(key TEXT PRIMARY KEY,value TEXT NOT NULL);CREATE TABLE dispatch_resume_epochs(run TEXT PRIMARY KEY,previous_run TEXT NOT NULL UNIQUE,booking_id TEXT NOT NULL,request_id TEXT NOT NULL UNIQUE,receipt TEXT NOT NULL);');
  this.history.exec('PRAGMA journal_mode=WAL;CREATE TABLE cursors(source TEXT PRIMARY KEY,offset INTEGER NOT NULL,run TEXT NOT NULL);');
  this.checkpoint=checkpoint();this.receipt={schema:'pgn-checkpoint-resume-epoch-1',origin:'RESUME_CHECKPOINT',request_id:'12345678-1234-4234-8234-123456789abc',device,booking_id:booking,previous_run:old,run:current,observation_id:'11111111-2222-4333-8444-555555555555',predecessor:null,counter_basis:'PGNCG001_CUMULATIVE_BASELINE',raw_conservation_requirement:'LINEAGE_RAW_CONSERVATION_REQUIRED',checkpoint:this.checkpoint};
  this.booking.prepare('INSERT INTO dispatch_resume_epochs VALUES(?,?,?,?,?)').run(current,old,booking,this.receipt.request_id,canonical(this.receipt));
  this.manifests=new Map();
  for(const run of [old,current]){
   const m={schema:'pgn-native-process-1',run,uuid:device,queued_intent:booking,start:String(first),end:String(end),stdout:'C:/PUBLIC-FIXTURE/'+run+'.stdout',pid:1234,birth:'2026-10-14T00:00:00.1234567Z'};
   if(run===current){m.resume_previous_run=old;m.checkpoint_input_sha256=this.checkpoint.resume.checkpointSha256;}
   this.manifests.set(run,m);this.booking.prepare('INSERT INTO booking_metadata VALUES(?,?)').run('mining-source-manifest/v1/'+run,canonical(m));
  }
  this.write(old,header()+gap()+gap()+`[pgn-counters] time_ns=111 positions=300 gaps=2 tests=${tests} batches=1 processed_gaps=2\n[phase0-gpu] stopped positions=300 checkpoint=${label}\n`);
  this.write(current,header()+gap()+`[pgn-counters] time_ns=222 positions=1000 gaps=3 tests=${tests+10n} batches=2 processed_gaps=3\n[phase0-gpu] gaps reported=3 verification_failures=0 records_new=0\n[phase0-gpu] --- end ---\n`);
  this.snapshot={state:'OFF',owned:false,manifest:this.manifests.get(current),last_exit:{run:current,exit_code:0},next_offset:String(fs.statSync(this.file(current)).size),pending_bytes:0,observed_utc:'2026-10-14T01:00:00Z',source_completion_observation:{schema:'pgn-source-range-completion-1',run:current,queued_intent:booking,start:String(first),end:String(end),source:this.manifests.get(current).stdout,source_positions:'1000',source_session_gaps:'3',source_tests:String(tests+10n),source_exit_code:0,source_range_complete:true,independent_coverage:false,proof_status:'SOURCE_PRP_INDEPENDENT_PROOF_PENDING',source_tests_complete:true,source_position_evidence:'final_counter'}};
  this.bookingRead=new DatabaseSync(this.bookingFile,{readOnly:true});this.historyRead=new DatabaseSync(this.historyFile,{readOnly:true});
  this.verifier=new SourceResumeLineageVerifier({bookingDb:this.bookingRead,historyDb:this.historyRead,directory:this.sources});
 }
 file(run){return path.join(this.sources,run+'.stdout');}
 write(run,bytes){fs.writeFileSync(this.file(run),bytes);this.history.prepare('INSERT OR REPLACE INTO cursors VALUES(?,?,?)').run('native/'+run+'.stdout',fs.statSync(this.file(run)).size,run);}
 dump(){return canonical({b:this.booking.prepare('SELECT * FROM booking_metadata ORDER BY key').all(),e:this.booking.prepare('SELECT * FROM dispatch_resume_epochs ORDER BY run').all(),h:this.history.prepare('SELECT * FROM cursors ORDER BY source').all()});}
 ready(){const p=this.verifier.verify(this.snapshot);assert.equal(p.ready,true,p.error);assert.equal(p.epochs,2);return p;}
 close(){this.bookingRead.close();this.historyRead.close();this.booking.close();this.history.close();const prefix=path.join(os.tmpdir(),'pgn-lineage-io-');assert.ok(path.resolve(this.dir).startsWith(path.resolve(prefix))&&fs.lstatSync(this.dir).isDirectory()&&!fs.lstatSync(this.dir).isSymbolicLink());fs.rmSync(this.dir,{recursive:true,force:true});}
}
function own(action){const f=new Fixture();try{return action(f);}finally{f.close();}}
function held(p,pattern){assert.equal(p.ready,false);assert.ok(p.error.length>0&&p.error.length<=400);assert.equal(p.chain_sha256,'');if(pattern)assert.match(p.error,pattern);}
function closed(v){const d=v.diagnostics();assert.equal(d.opened,d.closed);return d;}

test('first actual readonly verification scans bytes; second stats/meta hit reads ZERO bytes, no DB writes',t=>own(f=>{
 const before=f.dump(),a=f.ready(),firstStats=closed(f.verifier);assert.ok(BigInt(firstStats.readBytes)>0n);assert.equal(firstStats.cacheHit,false);
 const b=f.ready(),second=closed(f.verifier);assert.equal(b,a);assert.equal(second.cacheHit,true);assert.equal(second.readBytes,'0');assert.equal(second.readCalls,'0');assert.equal(second.opened,'2');assert.equal(f.dump(),before);
 t.diagnostic(`actual first readBytes=${firstStats.readBytes} readCalls=${firstStats.readCalls}; hit readBytes=${second.readBytes} readCalls=${second.readCalls} opened=${second.opened} closed=${second.closed}`);
}));
test('actual source larger than64KiB is chunk-read, not allocated as one raw corpus',t=>own(f=>{
 const file=f.file(current),original=fs.readFileSync(file,'utf8'),newline=original.indexOf('\n')+1;
 const diagnostic=('diagnostic '+'x'.repeat(1024)+'\n').repeat(130);f.write(current,original.slice(0,newline)+diagnostic+original.slice(newline));f.snapshot.next_offset=String(fs.statSync(file).size);
 f.ready();const d=closed(f.verifier),expected=BigInt(fs.statSync(file).size)+BigInt(fs.statSync(f.file(old)).size);assert.equal(BigInt(d.readBytes),expected);assert.ok(BigInt(d.readCalls)>2n);
 t.diagnostic(`actual chunked readBytes=${d.readBytes} readCalls=${d.readCalls}; each adapter read enforces<=65536 bytes`);
}));
test('fresh observation age/driver telemetry do not rescan; changed transport identity does',()=>own(f=>{
 const p=f.ready();const fresh={...f.snapshot,observed_utc:'2026-10-14T02:00:00Z',raw_driver:{temperature:99,age_ms:5},cpu:{percent:55}};
 assert.equal(f.verifier.verify(fresh),p);assert.equal(closed(f.verifier).readBytes,'0');assert.equal(f.verifier.diagnostics().cacheHit,true);
 assert.equal(f.verifier.verify({...fresh,next_offset:String(BigInt(fresh.next_offset)+1n)}).ready,true);assert.equal(f.verifier.diagnostics().cacheHit,false);assert.ok(BigInt(closed(f.verifier).readBytes)>0n);
}));
test('valid raw change forces new scan/hash, malformed raw or truncated cursor never reuses proof',()=>{
 own(f=>{const before=f.ready(),filename=f.file(old),raw=fs.readFileSync(filename,'utf8');f.write(old,raw.replaceAll('merit=20.0000','merit=21.0000'));fs.utimesSync(filename,new Date(),new Date(Date.now()+2000));const after=f.ready();assert.notEqual(after.chain_sha256,before.chain_sha256);assert.equal(f.verifier.diagnostics().cacheHit,false);assert.ok(BigInt(closed(f.verifier).readBytes)>0n);});
 own(f=>{f.ready();const filename=f.file(old);f.write(old,fs.readFileSync(filename,'utf8').replace(' gap=2 ',' gap=3 '));fs.utimesSync(filename,new Date(),new Date(Date.now()+2000));held(f.verifier.verify(f.snapshot),/endpoint/);closed(f.verifier);});
 own(f=>{f.ready();f.history.prepare('UPDATE cursors SET offset=offset-1 WHERE run=?').run(old);held(f.verifier.verify(f.snapshot),/cursor/);assert.equal(f.verifier.diagnostics().cacheHit,false);closed(f.verifier);});
});
test('cache revalidates deleted/corrupt receipt and manifest, and SQLite types, on every hit',()=>{
 for(const sql of ["DELETE FROM dispatch_resume_epochs;","UPDATE dispatch_resume_epochs SET receipt=receipt||' ';","DELETE FROM booking_metadata WHERE key LIKE '%/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa';","UPDATE booking_metadata SET value=CAST(value AS BLOB);","UPDATE dispatch_resume_epochs SET previous_run='cccccccccccccccccccccccccccccccc';"]){own(f=>{f.ready();f.booking.exec(sql);held(f.verifier.verify(f.snapshot));assert.equal(f.verifier.diagnostics().cacheHit,false);closed(f.verifier);});}
 own(f=>{f.ready();f.history.exec("UPDATE cursors SET source='other/'||run||'.stdout';");held(f.verifier.verify(f.snapshot),/namespace|type/);closed(f.verifier);});
 // INTEGER affinity coerces numeric TEXT+NUL into123 on this real SQLite;
 // use BLOB to preserve the corrupt bytes rather than predict a false type.
 own(f=>{f.ready();f.history.exec("UPDATE cursors SET offset=CAST(x'31323300' AS BLOB);");held(f.verifier.verify(f.snapshot),/type/);closed(f.verifier);});
});
test('path inode replacement is reverified even with identical bytes; invalid UTF8 closes reader',()=>{
 own(f=>{const proof=f.ready(),file=f.file(current),saved=file+'.old';fs.renameSync(file,saved);fs.writeFileSync(file,fs.readFileSync(saved));const replay=f.ready();assert.equal(replay.chain_sha256,proof.chain_sha256);assert.equal(f.verifier.diagnostics().cacheHit,false);assert.ok(BigInt(closed(f.verifier).readBytes)>0n);});
 own(f=>{const file=f.file(current);f.write(current,Buffer.concat([Buffer.from([0xff,10]),fs.readFileSync(file)]));held(f.verifier.verify(f.snapshot));const d=closed(f.verifier);assert.equal(d.opened,'1');assert.ok(BigInt(d.readBytes)>0n);});
});
test('symlink source refuses without following it when platform permits own symlink creation',t=>own(f=>{
 const file=f.file(current),actual=file+'.actual';fs.renameSync(file,actual);
 try{fs.symlinkSync(actual,file,'file');}catch(error){fs.renameSync(actual,file);if(error.code==='EPERM'||error.code==='EACCES'||error.code==='ENOTSUP'){t.skip('own Windows symlink creation unavailable: '+error.code);return;}throw error;}
 held(f.verifier.verify(f.snapshot),/nonsymlink/);assert.equal(closed(f.verifier).opened,'0');
}));
test('unknown/active is Held not busy; foreign transactions preserved; later OFF is rescanned',()=>own(f=>{
 f.ready();held(f.verifier.verify({...f.snapshot,state:'RUNNING',owned:true}),/known OFF/);assert.equal(closed(f.verifier).opened,'0');f.ready();assert.equal(f.verifier.diagnostics().cacheHit,false);
 f.bookingRead.exec('BEGIN');try{held(f.verifier.verify(f.snapshot),/transaction/);assert.equal(f.bookingRead.isTransaction,true);}finally{f.bookingRead.exec('ROLLBACK');}
 f.historyRead.exec('BEGIN');try{held(f.verifier.verify(f.snapshot),/transaction/);assert.equal(f.historyRead.isTransaction,true);assert.equal(f.bookingRead.isTransaction,false);}finally{f.historyRead.exec('ROLLBACK');}
 f.ready();closed(f.verifier);
}));
test('no directory/schema/cursor creation, unsafe offset rejected and malformed snapshot fails closed',()=>own(f=>{
 const before=f.dump(),missing=path.join(f.dir,'absent');const verifier=new SourceResumeLineageVerifier({bookingDb:f.bookingRead,historyDb:f.historyRead,directory:missing});held(verifier.verify(f.snapshot));assert.equal(fs.existsSync(missing),false);assert.equal(f.dump(),before);
 f.history.exec("DELETE FROM cursors;");held(f.verifier.verify(f.snapshot),/cursor/);assert.equal(f.history.prepare('SELECT count(*) n FROM cursors').get().n,0);
 held(f.verifier.verify({state:'OFF',owned:false,manifest:{run:'../outside'}}),/run32/);
}));
