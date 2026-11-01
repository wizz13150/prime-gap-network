import assert from 'node:assert/strict';
import fs from 'node:fs';import os from 'node:os';import path from 'node:path';
import {HistoryStore} from '../tools/history-store.mjs';
const directory=fs.mkdtempSync(path.join(os.tmpdir(),'pgn-native-index-'));
const store=new HistoryStore(path.join(directory,'history.sqlite'),{indexed:true});
const a='a'.repeat(32),b='b'.repeat(32);
const line=(p,time='')=>`[phase0-gpu] GAP merit=24.123 gap=1000 lower=${p} upper=${BigInt(p)+1000n} verified=1${time}`;
const rows=Array.from({length:2048},(_,i)=>line(String(200000000000000000000n+2000n*BigInt(i)),i%2?' unix_ts=1791682500':''));
store.append(rows,a,'native/a.stdout',1234);store.append(rows.slice(0,12),b,'native/b.stdout',1200);
await store.plot.backfill();
assert.equal(store.plot.boundsFor('','length').points,2048);
assert.equal(store.plot.boundsFor('','time').points,1024);
assert.equal(store.plot.boundsFor(a,'length').points,2048);
assert.equal(store.plot.boundsFor(b,'length').points,12);
assert.equal(store.plot.boundsFor(b,'time').points,6);
assert.equal(store.latest().length,256);
store.append([rows[0]+' unix_ts=1791682501'],a,'native/a.stdout',1235);
assert.equal(store.plot.boundsFor('','time').points,1025);
assert.equal(store.plot.boundsFor(b,'time').points,7); // original date repair reaches every exact origin
assert.equal(store.db.prepare("SELECT COUNT(*) n FROM native_plot_coordinates WHERE scope=? AND axis='time' AND admission_revision<=?").get('',1024).n,1024);
await store.plot.backfill();
assert.equal(store.plot.boundsFor('','length').points,2048);
const query=store.db.prepare(`EXPLAIN QUERY PLAN SELECT x,y FROM native_plot_coordinates
  WHERE scope=? AND axis=? AND point_key>=? AND point_key<=? ORDER BY point_key`).all('','length','0','f'.repeat(32));
assert.match(query.map(row=>row.detail).join(' '),/PRIMARY KEY/);
store.close();
const reopened=new HistoryStore(path.join(directory,'history.sqlite'),{indexed:true});await reopened.plot.backfill();
assert.equal(reopened.plot.boundsFor('','time').points,1025);assert.equal(reopened.cursorOffset('native/a.stdout'),1235);
reopened.close();console.log('PASS: exact ALL/run index, unknown time, all-origin date repair, restart, duplicate-X keyset and bounded latest records');
