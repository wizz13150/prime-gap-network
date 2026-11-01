import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';import os from 'node:os';import path from 'node:path';
import {DatabaseSync} from 'node:sqlite';
import {NativePlotIndex} from '../tools/native-plot-index.mjs';
import {buildNativeDisplayCache} from '../tools/build-native-display-cache.mjs';
test('exact immutable coordinate prefix cache, additive suffix and raw rows survive refresh',()=>{
 const root=fs.mkdtempSync(path.join(os.tmpdir(),'pgn-display-cache-')),file=path.join(root,'native.sqlite');
 const db=new DatabaseSync(file);
 try{
  db.exec('CREATE TABLE gaps(id INTEGER PRIMARY KEY,merit TEXT,gap TEXT,unix_ts INTEGER);CREATE TABLE origins(gap_id INTEGER,run TEXT);');
  const index=new NativePlotIndex(db),add=(id,merit,gap,time)=>{
   db.prepare('INSERT INTO gaps VALUES(?,?,?,?)').run(id,merit,gap,time);
   db.prepare('INSERT INTO origins VALUES(?,?)').run(id,'a'.repeat(32));index.admitId(id);
  };
  add(1,'20','800',1790000000);add(2,'20','800',1790000000);add(3,'21','802',null);
  const raw=()=>db.prepare('SELECT * FROM gaps ORDER BY id').all(),before=raw();
  const first=buildNativeDisplayCache(file,path.join(root,'stage'));
  assert.deepEqual(raw(),before);assert.equal(first.raw_source_rows_changed,0);assert.equal(first.original_dates_changed,0);
  assert.deepEqual(first.receipts.map(x=>[x.axis,x.points,x.cells]),[['length',3,2],['time',2,1]]);
  assert.equal(db.prepare('SELECT SUM(multiplicity) n FROM native_plot_multiplicity WHERE axis=?').get('length').n,3);
  const stage=new DatabaseSync(first.staging,{readOnly:true});assert.equal(stage.prepare('SELECT SUM(multiplicity) n FROM cells WHERE axis=?').get('length').n,3);stage.close();
  add(4,'20','800',null);
  assert.equal(db.prepare('SELECT base_revision FROM native_plot_multiplicity_manifest WHERE axis=?').get('length').base_revision,3);
  assert.equal(db.prepare('SELECT COUNT(*) n FROM native_plot_coordinates WHERE scope=? AND axis=? AND admission_revision>?').get('','length',3).n,1);
  const second=buildNativeDisplayCache(file,path.join(root,'stage'));
  assert.deepEqual(second.receipts.map(x=>[x.axis,x.points,x.cells]),[['length',4,2]]);
  assert.equal(second.receipts[0].source_rows_read,1);assert.equal(second.receipts[0].prefix_cells_read,2);
  assert.deepEqual(second.reused.map(x=>[x.axis,x.points,x.cells,x.source_rows_read]),[['time',2,1,0]]);
  assert.deepEqual(raw().slice(0,3),before);assert.equal(raw().length,4);
  assert.equal(db.prepare('SELECT SUM(multiplicity) n FROM native_plot_multiplicity WHERE axis=?').get('length').n,4);
 }finally{db.close();fs.rmSync(root,{recursive:true,force:true});}
});
