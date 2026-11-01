import fs from 'node:fs';import assert from 'node:assert/strict';
const receipt=JSON.parse(fs.readFileSync('pgn/evidence/conditional-probe-inspection-20261012.json','utf8'));
assert.equal(receipt.probe_running,false);assert.deepEqual(receipt.errors,[]);
assert.equal(receipt.producer.uuid,'GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac');
assert.equal(receipt.producer.birth,receipt.producer.birth_now);
const mask=(1n<<64n)-1n;
const expectedHash=tiles=>{let result=0n;for(let tile=0;tile<tiles;++tile)for(let i=0;i<1024;++i){
 let value=(BigInt(tile*1024+i)*0x9e3779b97f4a7c15n+77n)&mask;
 for(let round=0;round<8;++round){value^=value>>13n;value=(value*0xbf58476d1ce4e5b9n+77n)&mask;}result^=value;
}return result.toString(16).padStart(16,'0');};
const results=receipt.result_lines.map(JSON.parse);assert.deepEqual(results.map(r=>r.tiles),[1,2,128]);
for(const result of results){assert.equal(result.scope,'FINITE_CONTROL_CAPABILITY_NOT_MINING');
 assert.equal(result.host_graph_launches,1);assert.equal(result.completed_tiles,result.tiles);
 assert.equal(result.exact_values,result.tiles*1024);assert.equal(result.mismatches,0);
 assert.equal(result.xor_hash,expectedHash(result.tiles));assert.ok(Number.isFinite(result.event_ms)&&result.event_ms>0);
}
console.log('PASS independent BigInt fixture hashes; exact128 GPU-owned iterations/onehostlaunch. No mining/idle/throughput claim. Wrapper maxBuffer lost exit-code receipt; no rerun to hide it.');
