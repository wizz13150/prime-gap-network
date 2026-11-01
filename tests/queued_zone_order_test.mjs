import fs from 'node:fs';import os from 'node:os';import path from 'node:path';import assert from 'node:assert/strict';import {randomUUID} from 'node:crypto';
import {DatabaseSync} from 'node:sqlite';import {QueuedDispatch,CONSTANT_DEVICE} from '../tools/queued-dispatch.mjs';
const dir=fs.mkdtempSync(path.join(os.tmpdir(),'pgn-zone-order-'));const file=path.join(dir,'bookings.sqlite');const db=new DatabaseSync(file);const key=s=>String(s.length).padStart(3,'0')+s;
db.exec(`CREATE TABLE bookings(id TEXT PRIMARY KEY,start TEXT,end TEXT,start_key TEXT,end_key TEXT,state TEXT,kind TEXT,device TEXT,created_utc TEXT)`);
const q=new QueuedDispatch(file),zoneWidth=10n**17n,blockWidth=10n**15n;
const a=134000000000000000000n,b=a+zoneWidth,c=b+zoneWidth;
function add(start){const id=randomUUID(),first=String(start),last=String(start+blockWidth);db.prepare("INSERT INTO bookings VALUES(?,?,?,?,?,'QUEUED','LOCAL_INTENT',?,'fixture')").run(id,first,last,key(first),key(last),CONSTANT_DEVICE);return id;}
const a1=add(a),a2=add(a+blockWidth),b1=add(b),c1=add(c),c2=add(c+blockWidth);const old='a'.repeat(32);
assert.equal(q.prepare(old).id,a1);db.prepare("UPDATE bookings SET state='QUEUED' WHERE id=?").run(a1);db.prepare('DELETE FROM dispatch_requests').run();
for(const [rank,start] of [c,a,b].entries())db.prepare('INSERT INTO queue_zone_order VALUES(?,?,?,?,?)').run(String(start),String(start+zoneWidth),key(String(start)),key(String(start+zoneWidth)),rank);
assert.equal(q.prepare(old).id,c1);db.prepare("UPDATE bookings SET state='RELEASED' WHERE id=?").run(c1);db.prepare('DELETE FROM dispatch_requests').run();const restored=add(c);assert.equal(q.prepare(old).id,restored);
db.prepare("UPDATE bookings SET state='SOURCE_FINISHED' WHERE id=?").run(restored);db.prepare('DELETE FROM dispatch_requests').run();assert.equal(q.prepare(old).id,c2);
db.prepare("UPDATE bookings SET state='QUEUED' WHERE id=?").run(c2);db.prepare('DELETE FROM dispatch_requests').run();db.prepare('DELETE FROM queue_zone_order').run();assert.equal(q.prepare(old).id,a1);
assert.equal(db.prepare("SELECT count(*) n FROM bookings WHERE state='QUEUED'").get().n,3);assert.equal(db.prepare('SELECT count(*) n FROM dispatch_requests').get().n,1);
q.close();db.close();console.log('PASS actual boundary dispatcher honors manual zone priority, numeric blocks/re-reservation, numeric reset and claims only current next; fixture only');
