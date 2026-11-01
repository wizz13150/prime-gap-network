import fs from 'node:fs';import os from 'node:os';import path from 'node:path';import assert from 'node:assert/strict';
import {DatabaseSync} from 'node:sqlite';import {QueuedDispatch,CONSTANT_DEVICE} from '../tools/queued-dispatch.mjs';
const temporary=fs.mkdtempSync(path.join(os.tmpdir(),'pgn-numeric-queue-'));let q,db;
try{
 const file=path.join(temporary,'queue.sqlite');db=new DatabaseSync(file);
 db.exec("CREATE TABLE bookings(id TEXT PRIMARY KEY,start TEXT,end TEXT,start_key TEXT,end_key TEXT,state TEXT,kind TEXT,device TEXT,created_utc TEXT)");
 q=new QueuedDispatch(file);const unit=10n**15n,start=199900n*unit,predecessor='a'.repeat(32),run='b'.repeat(32);
 const id=i=>'12345678-1234-1234-1234-'+String(i).padStart(12,'0'),key=s=>String(s.length).padStart(3,'0')+s;
 const add=(i,block,date,device=CONSTANT_DEVICE)=>{const a=String(start+BigInt(block-1)*unit),b=String(start+BigInt(block)*unit);db.prepare("INSERT INTO bookings VALUES(?,?,?,?,?,'QUEUED','LOCAL_INTENT',?,?)").run(id(i),a,b,key(a),key(b),device,date);return {a,b}};
 // Reverse arrival order: numeric1 must precede2 and3 without any arm() call.
 add(3,3,'2026-10-11');add(2,2,'2026-10-12');const one=add(1,1,'2026-10-13');
 const claimed=q.prepare(predecessor);assert.equal(claimed.id,id(1));assert.equal(claimed.state,'PREPARED');
 assert.equal(db.prepare('SELECT state FROM bookings WHERE id=?').get(id(1)).state,'DISPATCHING');
 q.close();q=new QueuedDispatch(file);assert.equal(q.prepare(predecessor).id,id(1));
 assert.throws(()=>q.prepare('c'.repeat(32)),/Uncertain/);
 const manifest={run,uuid:CONSTANT_DEVICE,start:one.a,end:one.b,queued_intent:id(1),birth:'2026-10-12T01:00:00.0000000Z'};
 assert.equal(q.reconcile({owned:true,state:'RUNNING',manifest,binding:{...manifest}}),true);
 assert.equal(q.complete({schema:'pgn-source-range-completion-1',run,queued_intent:id(1),start:one.a,end:one.b,source_positions:String(unit),source_range_complete:true,independent_coverage:false,source_exit_code:0}),true);
 // Release2 and re-reserve as a fresh ID/time AFTER3. It is still next.
 db.prepare("UPDATE bookings SET state='RELEASED' WHERE id=?").run(id(2));add(22,2,'2099-01-01');
 assert.equal(q.prepare(run).id,id(22));
 assert.equal(db.prepare('SELECT state FROM bookings WHERE id=?').get(id(3)).state,'QUEUED');
 assert.equal(db.prepare('SELECT state FROM bookings WHERE id=?').get(id(2)).state,'RELEASED');
 // A wrong physical GPU is never admitted through automatic reservation.
 add(30,1,'1900-01-01','3060');assert.equal(q.prepare(run).id,id(22));
 console.log('PASS automatic reservation-only dispatch; exact numeric order despite reverse chronology/re-release/re-reserve, restart, pending-claim protection, device gate, completed/released preservation');
}finally{q?.close();db?.close();fs.rmSync(temporary,{recursive:true,force:true})}
