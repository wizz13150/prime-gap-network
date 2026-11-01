import fs from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';
const html=fs.readFileSync(process.argv[2],'utf8');
const source=html.match(/\/\* MODEL_START \*\/([\s\S]*?)\/\* MODEL_END \*\//)?.[1];
assert(source,'Actual displayed model missing');
const c=vm.createContext({Math});
vm.runInContext(source,c);
for(const model of ['linear','quadratic'])for(const reward of [10,200,10000])for(const cadence of [1,10,60]){
 let previous=0,lastReward=reward;
 for(const t of [0,.01,1,5,10,20,50,100,1000]){
  const v=c.emissionAt(t,reward,cadence,model);
  assert(v.allocated>=previous&&v.allocated<=99000000&&v.reward>=0&&v.reward<=lastReward+1e-9);
  previous=v.allocated;lastReward=v.reward;
 }
 for(const f of [.5,.9,.99]){
  const t=c.milestoneYears(f,reward,cadence,model);
  assert(Math.abs(c.emissionAt(t,reward,cadence,model).allocated/99000000-f)<1e-12);
 }
 assert.equal(c.milestoneYears(1,reward,cadence,model),Infinity);
}
assert.equal(c.emissionAt(0,200,10,'linear').reward,200);
assert.equal(c.emissionAt(0,200,10,'quadratic').allocated,0);
assert(c.emissionAt(10,200,10,'linear').allocated>c.emissionAt(10,200,10,'quadratic').allocated);
const tenL=c.emissionAt(10,200,10,'linear'),tenQ=c.emissionAt(10,200,10,'quadratic');
console.log(JSON.stringify({status:'PASS',scope:'Actual displayed continuous approximation, not integer consensus or future launch forecast',ten_years:{linear:tenL,quadratic:tenQ},milestones_years:[.5,.9,.99].map(f=>({fraction:f,linear:c.milestoneYears(f,200,10,'linear'),quadratic:c.milestoneYears(f,200,10,'quadratic')}))}));
