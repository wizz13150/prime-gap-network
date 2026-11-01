import fs from 'node:fs';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
function bounded(file,limit){assert.ok(fs.statSync(file).size<=limit);return fs.readFileSync(file,'utf8').replace(/^\uFEFF/,'');}
const read=file=>JSON.parse(bounded(file,32768));
const v1=read('pgn/evidence/conditional-walk-v1-result-20261012.json');
const v2=read('pgn/evidence/conditional-walk-v2-trace-known-result-20261012.json');
const summaries=[];
for(const result of [v1,v2]){
 assert.equal(result.exit_code,0);assert.equal(result.producer_unchanged,true);assert.deepEqual(result.errors,[]);
 assert.equal(result.device,'GPU-33f44d5c-91e4-6ab6-1e45-8c93135fd265');
 const rows=result.result_lines.map(JSON.parse),summary=rows.find(row=>row.scope==='FINITE_REAL_SIEVE_WALK_JOB_NOT_PRODUCTION_CONTINUITY');
 assert.ok(summary);summaries.push(summary);
 for(const [field,value] of Object.entries({iterations:3,host_graph_launches:1,completed:3,bitmap_words:25165824,bitmap_errors:0,result_errors:0,visit_errors:0}))assert.equal(summary[field],value);
 assert.ok(summary.tests>0&&summary.jumps>0&&summary.reference_ms>0&&summary.graph_ms>0);
}
assert.equal(v1.image_sha256,'17916A61B111905FABE195FAEBE42AC5B3EBF188F94785A36ED5AB2CAB027153');
assert.equal(v2.image_sha256,'938B601483C4BC6BF6B6752E00A84A94183C7A5F5A4A075007E83E3D7F3256A7');
assert.equal(summaries[0].reported_gaps,0);assert.equal(summaries[1].owned_gaps,1);
assert.equal(summaries[0].tests,23973278);assert.equal(summaries[0].jumps,9432033);
assert.equal(summaries[1].tests,23984350);assert.equal(summaries[1].jumps,9432522);
const gaps=v2.result_lines.map(JSON.parse).filter(row=>row.kind==='owned_source_gap');
const actual=gaps.map(row=>`${(BigInt(row.high)<<64n)+BigInt(row.low)}:${row.gap}`).sort();
assert.deepEqual(actual,['199999990013881082917:892']);
const golden=read('pgn/evidence/committed-pub-inspection-20261012.json');
const low=199999990013746865160n,high=low+4n*(30n<<25n);
const expected=golden.records[0].sorted_pairs.map(pair=>pair.split(':')).filter(([lower])=>BigInt(lower)>=low&&BigInt(lower)<high).map(([lower,,gap,verified])=>{
 assert.equal(verified,'1');return `${lower}:${gap}`;
}).sort();
assert.deepEqual(actual,expected);assert.equal(new Set(actual).size,actual.length);
const traceReport=v2.result_lines.map(JSON.parse).find(row=>row.kind==='concurrent_kernel_trace');
for(const [field,value] of Object.entries({records:16,dropped:0,errors:0,invalid_timestamps:0}))assert.equal(traceReport[field],value);
const traceFile='pgn/evidence/conditional-walk-v2-kernel-activity-20261012.jsonl';
assert.equal(createHash('sha256').update(fs.readFileSync(traceFile)).digest('hex'),'f561cf3396bc839d38672e8f21bf02b2478111c4da8030850b77387643f58a75');
const events=bounded(traceFile,16384).trim().split(/\r?\n/).map(JSON.parse).map(row=>({...row,start:BigInt(row.start_ns),end:BigInt(row.end_ns)}));
assert.equal(events.length,16);for(const row of events){assert.ok(row.start>0n&&row.end>row.start);assert.equal(row.device_id,0);}
const graph=events.filter(row=>row.name.includes('pgn_graph_'));
assert.equal(graph.length,10);assert.ok(graph.every(row=>row.graph_id>0&&BigInt(row.graph_node)>0n));
assert.equal(graph.filter(row=>row.name.includes('pgn_graph_walk')).length,3);
assert.equal(graph.filter(row=>row.name.includes('pgn_graph_advance')).length,3);
const marks=graph.filter(row=>row.name.includes('pgn_graph_mark')).sort((a,b)=>a.start<b.start?-1:1);
assert.equal(marks.length,4);
// The last ahead-mark is the documented out-of-count no-op. Retain it in raw
// trace, but do not count its duration as useful sieve activity.
const useful=graph.filter(row=>row.name.includes('pgn_graph_walk')).concat(marks.slice(0,3));
const ordered=useful.toSorted((a,b)=>a.start<b.start?-1:1);
let coveredEnd=ordered[0].end,largestGap=0n,totalGap=0n;
for(const event of ordered.slice(1)){
 if(event.start>coveredEnd){const gap=event.start-coveredEnd;largestGap=gap>largestGap?gap:largestGap;totalGap+=gap;}
 coveredEnd=event.end>coveredEnd?event.end:coveredEnd;
}
let overlap=0n;for(const mark of marks.slice(1,3))for(const walk of graph.filter(row=>row.name.includes('pgn_graph_walk'))){
 const begin=mark.start>walk.start?mark.start:walk.start,end=mark.end<walk.end?mark.end:walk.end;
 if(end>begin)overlap+=end-begin;
}
assert.ok(overlap>0n); // Independent DAG edges produced actual overlap here.
console.log(JSON.stringify({status:'PASS',exact_owned_gap:actual[0],science_counts_match_reference:true,
 graph_kernel_records:graph.length,raw_trace_dropped:0,overlap_ns:overlap.toString(),
 largest_interval_without_traced_useful_kernel_ns:largestGap.toString(),total_such_intervals_ns:totalGap.toString(),
 scope:'Finite instrumented3060 prototype only; not wholeblock/3090 no-idle or sustained mining gain'}));
