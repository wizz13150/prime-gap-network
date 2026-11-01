import fs from 'node:fs';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
const bounded=(file,max)=>{assert(fs.statSync(file).size<=max);return fs.readFileSync(file,'utf8')};
const file=name=>'pgn/evidence/continuous-native-v1-'+name+'-20261012.txt';
const receipt=JSON.parse(bounded(file('compact-result-json'),8192).replace(/^\uFEFF/,''));
assert.equal(receipt.image_sha256,'A73A6E383308560EDC36CE351B09EB449FA099275361BE74BE89020B03864B43');
assert.equal(receipt.cases.length,7);
for(const row of receipt.cases)assert.equal(row.exit_code,row.expected_exit);
const stdout=name=>{const text=bounded(file(name+'-stdout'),65536),row=receipt.cases.find(r=>r.case===name);assert.equal(createHash('sha256').update(text).digest('hex'),row.stdout_sha256.toLowerCase());return text};
const golden=stdout('golden'),stopped=stdout('stopped'),resumed=stdout('resumed');
const pairs=text=>text.split(/\r?\n/).filter(row=>row.startsWith('[phase0-gpu] GAP ')).map(row=>{
 const fields=Object.fromEntries([...row.matchAll(/([a-z_]+)=([^ ]+)/g)].map(m=>[m[1],m[2]]));
 assert.equal(fields.verified,'1');assert.equal(fields.proof,'GPU_MR12_EULER_COMPOSITE');
 assert.equal(BigInt(fields.upper)-BigInt(fields.lower),BigInt(fields.gap));
 return `${fields.lower}:${fields.upper}:${fields.gap}:1`;
}).sort();
const baseline=JSON.parse(bounded('pgn/evidence/committed-pub-inspection-20261012.json',32768)).records.find(r=>r.variant==='reference').sorted_pairs;
assert.deepEqual(pairs(golden),baseline);
assert.deepEqual([...pairs(stopped),...pairs(resumed)].sort(),baseline);
assert.equal(new Set([...pairs(stopped),...pairs(resumed)]).size,175);
const counters=text=>text.split(/\r?\n/).filter(row=>row.startsWith('[pgn-counters] ')).map(row=>Object.fromEntries([...row.matchAll(/([a-z_]+)=([0-9]+)/g)].map(m=>[m[1],BigInt(m[2])])));
for(const text of [golden,resumed]){
 assert(text.endsWith('[phase0-gpu] --- end ---\n')||text.endsWith('[phase0-gpu] --- end ---\r\n'));
 const rows=counters(text),last=rows.at(-1);assert.equal(last.positions,1030792151040n);assert.equal(last.tests,3437901640n);assert.equal(last.gaps,175n);
 for(let i=1;i<rows.length;i++){assert(rows[i].time_ns>rows[i-1].time_ns);assert(rows[i].positions>rows[i-1].positions);assert(rows[i].tests>=rows[i-1].tests)}
}
assert(!stopped.includes('[phase0-gpu] --- end ---'));
assert(counters(stopped).at(-1).positions>0n&&counters(stopped).at(-1).positions<1030792151040n);
const joined=bounded(file('resume-gaps'),65536);assert.deepEqual(pairs(joined),baseline);
console.log(JSON.stringify({status:'PASS',native_pairs:175,resumed_pairs:175,omissions:0,duplicates:0,abi_cases:7,source_positions:'1030792151040',tests:'3437901640',scope:'Finite actual native source ABI/lifecycle parity; no production/no-idle or independent coverage claim'}));
