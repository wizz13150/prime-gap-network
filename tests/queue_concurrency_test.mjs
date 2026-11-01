import test from 'node:test';import assert from 'node:assert/strict';import fs from 'node:fs';import path from 'node:path';import os from 'node:os';import {Worker,isMainThread,parentPort,workerData} from 'node:worker_threads';import{DatabaseSync}from'node:sqlite';
import {QueuedDispatch,CONSTANT_DEVICE} from '../tools/queued-dispatch.mjs';
if(!isMainThread){
 let queue;try{queue=new QueuedDispatch(workerData.file);parentPort.postMessage({ready:true});parentPort.once('message',()=>{
  try{parentPort.postMessage({claim:queue.prepare(workerData.predecessor)});}catch(error){parentPort.postMessage({error:error.message});}finally{queue.close();}
 });}catch(error){queue?.close();parentPort.postMessage({error:error.message});}
}else{
test('two simultaneous dispatch writers share one prepared intent and one state transition',async()=>{
 const temp=fs.mkdtempSync(path.join(os.tmpdir(),'pgn-queue-race-')),file=path.join(temp,'bookings.sqlite'),workers=[];let db,queue;
 try{
  db=new DatabaseSync(file);db.exec("PRAGMA journal_mode=WAL;CREATE TABLE bookings(id TEXT PRIMARY KEY,start TEXT,end TEXT,start_key TEXT,end_key TEXT,state TEXT,kind TEXT,device TEXT,created_utc TEXT);CREATE TABLE events(id TEXT,state TEXT);CREATE TRIGGER state_changed AFTER UPDATE OF state ON bookings WHEN OLD.state<>NEW.state BEGIN INSERT INTO events VALUES(NEW.id,NEW.state);END;");
  const start='199900000000000000000',end='199901000000000000000',id='12345678-1234-1234-1234-123456789abc',predecessor='a'.repeat(32),key=s=>String(s.length).padStart(3,'0')+s;
  db.prepare("INSERT INTO bookings VALUES(?,?,?,?,?,'QUEUED','LOCAL_INTENT',?,?)").run(id,start,end,key(start),key(end),CONSTANT_DEVICE,'original-time');queue=new QueuedDispatch(file);queue.close();queue=null;
  const ready=[],results=[];
  for(let i=0;i<2;i++){
   const worker=new Worker(new URL(import.meta.url),{workerData:{file,predecessor}});workers.push(worker);
   ready.push(new Promise((resolve,reject)=>{worker.once('error',reject);worker.once('message',message=>message.ready?resolve():reject(Error(message.error)));}));
  }
  await Promise.all(ready);
  for(const worker of workers)results.push(new Promise((resolve,reject)=>{worker.once('error',reject);worker.once('message',message=>message.claim?resolve(message.claim):reject(Error(message.error)));}));
  for(const worker of workers)worker.postMessage('claim');const claims=await Promise.all(results);
  assert.equal(claims[0].id,id);assert.equal(claims[1].id,id);assert.equal(claims[0].predecessor,predecessor);assert.equal(claims[1].predecessor,predecessor);
  assert.equal(db.prepare('SELECT count(*) n FROM dispatch_requests').get().n,1);
  assert.deepEqual(db.prepare('SELECT state FROM events ORDER BY rowid').all().map(x=>x.state),['DISPATCHING']);
  queue=new QueuedDispatch(file);assert.throws(()=>queue.prepare('c'.repeat(32)),/Uncertain/);
  assert.equal(db.prepare('SELECT state FROM bookings').get().state,'DISPATCHING');
 }finally{await Promise.all(workers.map(worker=>worker.terminate()));queue?.close();db?.close();fs.rmSync(temp,{recursive:true,force:true});}
});
}
