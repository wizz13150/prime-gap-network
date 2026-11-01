import assert from 'node:assert/strict';
const jobs=164*512;
let checks=0;
for(const threads of [128,256,512]){
 const blocks=jobs/threads,seen=new Uint8Array(jobs);
 for(let b=0;b<blocks;b++)for(let t=0;t<threads;t++){
  const id=b*threads+t;assert.equal(seen[id],0);seen[id]=1;checks++;
 }
 assert.equal(seen.every(x=>x===1),true);
 for(const slots of [0n,1n,65535n,83967n,83968n,83969n,1n<<34n,65n*(1n<<28n)]){
  const length=slots/BigInt(jobs);let end=0n;
  for(let id=0;id<jobs;id++){
   const lo=BigInt(id)*length,hi=id+1===jobs?slots:lo+length;
   assert.equal(lo,end);assert.ok(hi>=lo);end=hi;checks++;
  }
  assert.equal(end,slots);
 }
}
console.log(JSON.stringify({status:'CONSERVED_PHYSICAL_LOGICAL_MAPPING_PASS',jobs,checks,geometries:['656x128','328x256','164x512'],prime_search:false}));
