import fs from 'node:fs';import assert from 'node:assert/strict';
const read=file=>{assert.ok(fs.statSync(file).size<=16384);return JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));};
const v3=read('pgn/evidence/conditional-walk-v3-drain-stop-fixed-result-20261012.json');
const v4=read('pgn/evidence/conditional-walk-v4-explicit-stream-result-20261012.json');
for(const result of [v3,v4]){assert.equal(result.exit_code,16);assert.equal(result.producer_unchanged,true);assert.deepEqual(result.errors,[]);}
assert.deepEqual(v3.result_lines,[]);
const rows=v4.result_lines.map(JSON.parse),gate=rows.find(row=>row.kind==='drain_boundary_failed'),report=rows.find(row=>row.kind==='concurrent_kernel_trace');
assert.deepEqual(gate,{kind:'drain_boundary_failed',variant:1,completed:8,drained:8,drains_before_end:0,stop_requested_at:0});
assert.equal(report.records,42);assert.equal(report.dropped,0);assert.equal(report.errors,0);assert.equal(report.invalid_timestamps,0);
const file='pgn/evidence/conditional-walk-v4-kernel-activity-20261012.jsonl';assert.ok(fs.statSync(file).size<=16384);
const events=fs.readFileSync(file,'utf8').trim().split(/\r?\n/).map(JSON.parse);
assert.equal(events.length,42);const graph=events.filter(row=>row.graph_id),snapshots=events.filter(row=>row.name.includes('pgn_graph_ready_snapshot'));
assert.equal(graph.length,25);assert.equal(snapshots.length,1);
const graphEnd=graph.reduce((end,row)=>BigInt(row.end_ns)>end?BigInt(row.end_ns):end,0n);
assert.ok(BigInt(snapshots[0].start_ns)>=graphEnd);
console.log(JSON.stringify({status:'EXPECTED_FAILURE_PRESERVED',completed:8,drained_only_after_graph:true,
 source_graph_kernel_records:25,external_snapshot_kernel_records:1,
 first_snapshot_after_graph_ns:(BigInt(snapshots[0].start_ns)-graphEnd).toString(),
 scope:'Profiled conditional graph external control path unavailable during job; cause not attributed from this trace alone. No productive mutation.'}));
