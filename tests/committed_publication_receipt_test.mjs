import fs from 'node:fs';import assert from 'node:assert/strict';
const receipt=JSON.parse(fs.readFileSync('pgn/evidence/committed-pub-inspection-20261012.json','utf8'));
const [reference,candidate]=receipt.records;
for(const record of receipt.records){assert.equal(record.exit_code,0);assert.equal(record.closing_totals_marker,true);
 assert.equal(record.stopped,false);assert.equal(record.raw_pair_count,175);assert.equal(record.sorted_pairs.length,175);
 assert.equal(record.final.positions,'1030792151040');assert.equal(record.final.tests,'3437901640');
 assert.equal(record.final.batches,'17');assert.equal(record.final.gaps,'175');assert.equal(record.final.processed_gaps,'175');
 assert.ok(record.raw_counter_count>1);
}
assert.deepEqual(candidate.sorted_pairs,reference.sorted_pairs);assert.equal(reference.commits.length,0);assert.equal(candidate.commits.length,17);
let previous;for(const commit of candidate.commits){
 if(previous){assert.ok(BigInt(commit.time_ns)>BigInt(previous.time_ns));assert.ok(BigInt(commit.positions)>BigInt(previous.positions));
  assert.equal(BigInt(commit.batches),BigInt(previous.batches)+1n);
  for(const name of ['gaps','tests','processed_gaps'])assert.ok(BigInt(commit[name])>=BigInt(previous[name]));}
 previous=commit;
}
for(const name of ['positions','tests','batches','gaps','processed_gaps'])assert.equal(previous[name],candidate.final[name]);
assert.ok(BigInt(previous.time_ns)<BigInt(candidate.final.time_ns));
assert.equal(receipt.producer_now.birth,receipt.producer_now.birth_now);
console.log('PASS175 exactsame sourcepairs/verdicts,17 coherentactualworkercommits,original49/50rawperiodic samples retained,sourceclock/finaltotals. Singlepair10.756745vs11.204615s NOTgain/promoted; not continuousCUDA or independentcoverage proof.');
