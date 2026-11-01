import fs from 'node:fs';import os from 'node:os';import path from 'node:path';import assert from 'node:assert/strict';
import {ObservationCatalog,importObservations,pointKey} from '../tools/observation-catalog.mjs';
const directory=fs.mkdtempSync(path.join(os.tmpdir(),'pgn-observation-qa-'));
const database=path.join(directory,'observations.sqlite'),source=path.join(directory,'fixture.ndjson');
const rows=Array.from({length:700},(_,i)=>({p:String(200000000000000000000n+BigInt(i)*2000n),q:String(200000000000000000800n+BigInt(i)*2000n),gap:'800',merit_visual:17+i/1000,observed_utc:i%2?'2026-10-10T20:00:00Z':null,proof_status:'SOURCE_OBSERVATION_PROOF_NOT_PROMOTED',complete_interval:false}));
// Generated QA data, no scientific output or real wallet/network activity.
fs.writeFileSync(source,rows.map(r=>JSON.stringify(r)+'\n').join(''));
let catalog=new ObservationCatalog(database);
const receipt=await importObservations(catalog,source);assert.equal(receipt.observations,700);
let afterX=null,afterId=0,seen=0;
for(;;){const points=catalog.viewportPage({axis:'length',afterX,afterId,size:37});if(!points.length)break;
 seen+=points.length;afterX=points.at(-1).x;afterId=points.at(-1).id;}
assert.equal(seen,700);assert.equal(catalog.viewportPage({axis:'time',size:512}).length,350);
const values=[-1e100,-10,-0,0,0.001,800,1e100];
assert.deepEqual(values.map((x,i)=>({x,key:pointKey(x,i+1)})).sort((a,b)=>a.key.localeCompare(b.key)).map(r=>r.x),values);
assert.equal(pointKey(-0,1),pointKey(0,1));
assert.equal(catalog.viewportPage({axis:'length',lo:800,hi:800,size:512}).length,512);
const plan=catalog.db.prepare('EXPLAIN QUERY PLAN SELECT observation_id FROM plot_coordinates WHERE axis=? AND point_key>? AND point_key<=? ORDER BY point_key LIMIT ?').all('length',pointKey(800),pointKey(1000),256);
assert.ok(plan.some(r=>r.detail.includes('axis=? AND point_key>?')));
assert.equal(catalog.lookup(rows[0].p,rows[0].q).unix_ms,null);
assert.equal(catalog.lookup(rows[0].p,rows[0].q).proof_status,rows[0].proof_status);
catalog.append([{offset:0,raw:JSON.stringify({...rows[0],observed_utc:'2026-10-10T20:00:01Z'})}],'known-time-repair',{size:200,mtimeMs:1},200);
assert.equal(catalog.viewportPage({axis:'time',size:512}).length,351);
assert.throws(()=>catalog.append([{offset:0,raw:JSON.stringify({...rows[0],q:'2'})}],'bad-source',{size:100,mtimeMs:1},100));
assert.equal(catalog.state('bad-source'),undefined);assert.equal(catalog.count(),700);
assert.throws(()=>catalog.viewportPage({axis:'unknown'}));assert.throws(()=>catalog.viewportPage({axis:'length',size:513}));
catalog.close();catalog=new ObservationCatalog(database);
assert.equal((await importObservations(catalog,source)).observations,700);assert.equal(catalog.state(path.resolve(source)).next_offset,fs.statSync(source).size);
catalog.close();console.log('PASS exact BigInt identity, raw proof metadata, unknown dates, all-point indexed paging, rollback and restart cursor; no averaging');
