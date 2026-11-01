import test from 'node:test';import assert from 'node:assert/strict';
import {hypotheticalReward,emissionCalendar} from '../tools/emission-scenarios.mjs';
for(const exponent of [1,2])for(const decimals of [8,10])test(`integer cap/reward ${exponent}/${decimals}`,()=>{
 const scale=10n**BigInt(decimals),capAtoms=99000000n*scale,initialRewardAtoms=200n*scale;
 const reward=remainingAtoms=>hypotheticalReward({capAtoms,initialRewardAtoms,remainingAtoms,exponent});
 assert.equal(reward(0n),0n);assert.equal(reward(capAtoms),initialRewardAtoms);
 for(const fraction of [1n,2n,10n,100n]){const remaining=capAtoms/fraction;
  assert.equal(reward(remaining),exponent===1?initialRewardAtoms/fraction:initialRewardAtoms/(fraction*fraction));}
 assert.ok(reward(1n)<=1n);assert.ok(reward(2n)<=2n);
 assert.throws(()=>reward(capAtoms+1n));
});
test('calendar monotonicity and continuous accounting',()=>{
 const c=emissionCalendar();assert.equal(c.approved,false);assert.equal(c.deployment,false);assert.equal(c.inputs.exceptionalCalendar,null);
 for(const series of ['linear','quadratic']){let prior=0;for(const row of c.rows){const v=row[series];
  assert.ok(v.allocated>=prior&&v.allocated<=99000000);assert.ok(v.remaining>=0);prior=v.allocated;
  assert.ok(Math.abs(v.allocated+v.remaining-99000000)<1e-6);assert.ok(v.nextYearEmission>=0&&v.nextYearEmission<=v.remaining);}}
 assert.ok(c.rows[1].linear.allocated>c.rows[1].quadratic.allocated);
 assert.equal(c.rows[0].linear.reward,200);assert.equal(c.rows[0].quadratic.reward,200);
});
test('cadence is hypothetical and actually affects calendar',()=>{
 const fast=emissionCalendar({blockSeconds:60}),slow=emissionCalendar({blockSeconds:120});
 assert.equal(slow.linearHalfYears,fast.linearHalfYears*2);
 assert.equal(slow.quadraticHalfYears,fast.quadraticHalfYears*2);
});
test('integer exhaustion needs a separately approved tail policy',()=>{
 const scale=100000000n,capAtoms=99000000n*scale;
 assert.equal(hypotheticalReward({capAtoms,remainingAtoms:1n,initialRewardAtoms:200n*scale,exponent:1}),0n);
 assert.equal(hypotheticalReward({capAtoms,remainingAtoms:1n,initialRewardAtoms:200n*scale,exponent:2}),0n);
});
test('invalid inputs cannot masquerade as an emission schedule',()=>{
 assert.throws(()=>emissionCalendar({blockSeconds:0}));assert.throws(()=>emissionCalendar({initialReward:NaN}));
 assert.throws(()=>hypotheticalReward({capAtoms:1n,remainingAtoms:1n,initialRewardAtoms:1n,exponent:3}));
});
