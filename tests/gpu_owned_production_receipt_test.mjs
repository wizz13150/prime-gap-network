import fs from 'node:fs';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
const read=(path,limit)=>{assert(fs.statSync(path).size<=limit,'Evidence bound');return fs.readFileSync(path,'utf8').replace(/^\uFEFF/,'');};
const prefix='pgn/evidence/gpu-owned-transition-';
const rawPath=prefix+'raw-driver-jsonl-20261012.txt';
const raw=read(rawPath,65536),receipt=JSON.parse(read(prefix+'result-json-20261012.txt',8192));
assert.equal(createHash('sha256').update(raw).digest('hex'),receipt.raw_sha256.toLowerCase());
const after=JSON.parse(read(prefix+'after-json-20261012.txt',131072));
assert.equal(after.owned,true);assert.equal(after.state,'RUNNING');
assert.equal(after.manifest.uuid,'GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac');
assert.equal(after.binding.start,'199910000000000000000');assert.equal(after.binding.end,'199911000000000000000');
assert.equal(after.binding.engine_upgrade.after,'A73A6E383308560EDC36CE351B09EB449FA099275361BE74BE89020B03864B43');
assert.equal(after.binding.engine_upgrade.predecessor,'deb895b4611a47a49ae7d8b358efe1b2');
assert.equal(after.binding.engine_upgrade.policy,'GPU_OWNED_HIGH10_SOURCE_V1');
assert.equal(after.manifest.run,receipt.after.run);assert.equal(after.manifest.birth,receipt.after.birth);
const rows=raw.trim().split(/\r?\n/).map(line=>JSON.parse(line));
assert.equal(rows.length,90);
const samples=[];
for(const [index,row] of rows.entries()){
 assert.equal(row.sequence,index);assert.equal(row.exit_code,0);
 assert.equal(row.owner.run,after.manifest.run);assert.equal(row.owner.pid,after.manifest.pid);assert.equal(row.owner.birth,after.manifest.birth);
 const matching=row.raw.map(line=>line.split(',').map(v=>v.trim())).filter(f=>f[2]===after.manifest.uuid);
 assert.equal(matching.length,1);assert.equal(matching[0].length,11);
 assert.match(matching[0][4],/^\d+$/);const utilization=Number(matching[0][4]);assert(utilization>=0&&utilization<=100);
 samples.push({utc:row.requested_utc,utilization,raw:matching[0]});
 if(index)assert(Date.parse(row.requested_utc)>Date.parse(rows[index-1].requested_utc));
}
const observed={schema:'pgn-gpu-owned-production-observation-1',status:'PASS_NATIVE_PROMOTION_AND_FINITE_RAW_SAMPLES_NOT_FULL_BLOCK',
 run:after.manifest.run,pid:after.manifest.pid,birth:after.manifest.birth,
 samples:samples.length,first_utc:samples[0].utc,last_utc:samples.at(-1).utc,
 span_ms:Date.parse(samples.at(-1).utc)-Date.parse(samples[0].utc),
 minimum_utilization:Math.min(...samples.map(s=>s.utilization)),maximum_utilization:Math.max(...samples.map(s=>s.utilization)),
 zeros:samples.filter(s=>s.utilization===0).length,not100:samples.filter(s=>s.utilization!==100).length,
 raw_sha256:receipt.raw_sha256,full_block_finished:false,all_time_no_idle_proved:false,independent_coverage:false};
const target='pgn/evidence/gpu-owned-production-observed-20261012.json';
if(!fs.existsSync(target))fs.writeFileSync(target,JSON.stringify(observed,null,2)+'\n',{flag:'wx'});
else assert.deepEqual(JSON.parse(read(target,8192)),observed,'Existing receipt changed');
console.log(JSON.stringify(observed));
