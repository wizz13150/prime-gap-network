import assert from 'node:assert/strict';
for(const tileBits of [1<<18,1<<19]){
 const tileCount=(1<<28)/tileBits,seen=new Set();
 for(let cta=0;cta<92;++cta)for(let tile=cta;tile<tileCount;tile+=92){
  assert(!seen.has(tile));seen.add(tile);
  assert.equal(tileBits/64*64,tileBits);
  const first=BigInt(tile)*BigInt(tileBits)/8n*30n;
  const end=BigInt(tile+1)*BigInt(tileBits)/8n*30n;
  assert.equal(end-first,BigInt(tileBits)/8n*30n);
 }
 assert.equal(seen.size,tileCount);assert.equal(tileCount*tileBits,1<<28);
 assert.equal(BigInt(tileCount)*BigInt(tileBits)/8n*30n,30n<<25n);
}
console.log('PASS 32/64KiB disjoint/full wheel-slot geometry;512 is not the invariant tile count');
