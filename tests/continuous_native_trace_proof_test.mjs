import fs from 'node:fs';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
const file=name=>'pgn/evidence/continuous-native-v2-trace-proofs-'+name+'-20261012.txt';
const bounded=(name,limit=65536)=>{const path=file(name);assert(fs.statSync(path).size<=limit);return fs.readFileSync(path,'utf8')};
const receipt=JSON.parse(bounded('run-result-json',8192).replace(/^\uFEFF/,''));
assert.equal(receipt.cases.length,2);
const stdout=name=>{const text=bounded(name+'-stdout');const row=receipt.cases.find(r=>r.case===name);assert.equal(row.exit_code,0);assert.equal(createHash('sha256').update(text).digest('hex'),row.stdout_sha256.toLowerCase());assert.equal(row.producer_same_epoch,true);return text};
const poison=JSON.parse(stdout('poisons').trim());
assert.equal(poison.errors,0);assert.equal(poison.point_cases,9);assert.equal(poison.gap_cases,3);
assert.equal(poison.valid_gap,1);assert.equal(poison.even_endpoint,0);assert.equal(poison.inner_prime,0);
assert.equal(poison.done,3);assert.equal(poison.published,0);assert.equal(poison.completed,0);
const source=stdout('traced');const summary=source.trim().split(/\r?\n/).filter(s=>s.startsWith('{')).map(s=>JSON.parse(s)).find(r=>r.kind==='concurrent_kernel_trace');
assert(summary);for(const name of ['dropped','errors','invalid_timestamps'])assert.equal(summary[name],0);
const pairs=source.split(/\r?\n/).filter(s=>s.startsWith('[phase0-gpu] GAP ')).map(row=>{const fields=Object.fromEntries([...row.matchAll(/([a-z_]+)=([^ ]+)/g)].map(m=>[m[1],m[2]]));return `${fields.lower}:${fields.upper}:${fields.gap}:1`}).sort();
const original='pgn/evidence/committed-pub-inspection-20261012.json';assert(fs.statSync(original).size<=32768);
assert.deepEqual(pairs,JSON.parse(fs.readFileSync(original,'utf8')).records.find(r=>r.variant==='reference').sorted_pairs);
const raw=bounded('kernel-activity-jsonl');assert.equal(createHash('sha256').update(raw).digest('hex'),receipt.trace_sha256.toLowerCase());
const rows=raw.trim().split(/\r?\n/).map(s=>JSON.parse(s));assert.equal(rows.length,summary.records);
const kernels=rows.filter(r=>r.name.includes('pgn_graph_')).map(r=>({...r,start:BigInt(r.start_ns),end:BigInt(r.end_ns)})).sort((a,b)=>a.start<b.start?-1:a.start>b.start?1:0);
assert.equal(kernels.length,69);
for(const name of ['walk','verify','advance'])assert.equal(kernels.filter(k=>k.name.includes('pgn_graph_'+name)).length,17);
assert.equal(kernels.filter(k=>k.name.includes('pgn_graph_mark')).length,18);
let end=kernels[0].end;const gaps=[];
for(const kernel of kernels.slice(1)){
 assert(kernel.end>kernel.start);
 if(kernel.start>end)gaps.push({start_ns:String(end),end_ns:String(kernel.start),duration_ns:String(kernel.start-end)});
 if(kernel.end>end)end=kernel.end;
}
const maxGap=gaps.reduce((m,g)=>BigInt(g.duration_ns)>m?BigInt(g.duration_ns):m,0n);
const runtime='pgn/tools/continuous-gpu-runtime.cu';assert(fs.statSync(runtime).size<=65536);const text=fs.readFileSync(runtime,'utf8');
assert.equal((text.match(/cudaGraphLaunch\(/g)??[]).length,1);
// SOURCE boundary helpers now contain their own device loops before main.
// Locate the actual host publication loop, not the first lexical for-loop.
const hostLoop='for(;;){\n  MemoryBarrier();while(next<Count&&flags[next]){';
const liveStart=text.indexOf(hostLoop),liveEnd=text.indexOf('CHECK(cudaEventSynchronize(ended))',liveStart);
assert(liveStart>=0&&text.indexOf(hostLoop,liveStart+1)===-1&&liveEnd>liveStart);
const live=text.slice(liveStart,liveEnd);
assert(!/cuda[A-Z]\w*\s*\(/.test(live));assert(!live.includes('prime12('));assert(!live.includes('pgn_euler96::test'));
console.log(JSON.stringify({status:'PASS',point_cases:9,gap_poisons:3,invalid_job_published:0,exact_pairs:175,useful_graph_kernels:69,gpu_owned_iterations:17,max_observed_uncovered_ns:String(maxGap),uncovered_intervals:gaps,scope:'Finite process-local instrumented kernel timeline, not production/NVML100% or no-idle certification'}));
