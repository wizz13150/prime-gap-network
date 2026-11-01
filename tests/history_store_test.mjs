import fs from 'node:fs';import os from 'node:os';import path from 'node:path';
import assert from 'node:assert/strict';
import {HistoryStore} from '../tools/history-store.mjs';
const temporary=fs.mkdtempSync(path.join(os.tmpdir(),'pgn-history-'));
const file=path.join(temporary,'history.sqlite'),run='a'.repeat(32),other='b'.repeat(32);
const row='[phase0-gpu] GAP merit=20.4 gap=954 lower=199999000000000000001 upper=199999000000000000955 verified=1';
let store=new HistoryStore(file);
store.append([row],run,'first',100);
store.append([row+' unix_ts=1791676763'],other,'second',200);
assert.equal(store.count(),1);assert.equal(store.page()[0].unix_ts,1791676763);
assert.equal(store.db.prepare('SELECT COUNT(*) n FROM origins').get().n,2);
assert.throws(()=>store.append([row.replace('gap=954','gap=956')],run,'bad',300),/mismatch/);
assert.equal(store.db.prepare('SELECT offset FROM cursors WHERE source=?').get('bad'),undefined);
store.close();store=new HistoryStore(file);
assert.equal(store.count(),1);assert.equal(store.page()[0].p,'199999000000000000001');
assert.equal(store.page(1).length,0);assert.throws(()=>store.page(0,1000),/bound/);
assert.equal(store.watermark(),1);assert.equal(store.cursorOffset('first'),100);
store.append([row],run,'first',50);assert.equal(store.cursorOffset('first'),100);
assert.equal(store.page(0,256,0).length,0);
store.close();
console.log('PASS durable restart, exact endpoints, dedup, provenance, unknown timestamp repair, rollback and bounded paging');
// Keep this tiny independent receipt directory for recovery, never delete a
// broadly computed path from a test runner.
