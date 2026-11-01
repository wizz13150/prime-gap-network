import fs from 'node:fs';import assert from 'node:assert/strict';
const result=JSON.parse(fs.readFileSync('pgn/evidence/conditional-euler96-result-20261012.json','utf8'));
assert.equal(result.exit_code,0);assert.equal(result.producer_unchanged,true);assert.deepEqual(result.errors,[]);
assert.equal(result.device,'GPU-33f44d5c-91e4-6ab6-1e45-8c93135fd265');
const rows=result.result_lines.map(JSON.parse);assert.deepEqual(rows.map(row=>row.chunk),[127,4096,12072]);
for(const row of rows){assert.equal(row.scope,'GPU_OWNED_EULER96_JOB_NOT_EXHAUSTIVE_MINING_PROOF');
 assert.equal(row.cases,12072);assert.equal(row.completed,12072);assert.equal(row.host_graph_launches,1);
 assert.equal(row.iterations,Math.ceil(12072/row.chunk));assert.equal(row.mismatches,0);assert.equal(row.coverage_errors,0);
 assert.equal(row.result_hash,rows[0].result_hash);assert.equal(row.positive,4308);assert.ok(row.event_ms>0);
}
console.log('PASS actual Euler96 device-owned fixturejob:12072 paidGMP predicates,onehostlaunch,96/3/1iterations,exactcoverage,no omissions/duplicates. Not exhaustive science mining or new performance claim.');
