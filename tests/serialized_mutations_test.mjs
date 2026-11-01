import assert from 'node:assert/strict';import {SerializedMutations} from '../tools/serialized-mutations.mjs';
const writer=new SerializedMutations(),events=[];let finish;
const advance=writer.run(async()=>{events.push('advance-begin');await new Promise(resolve=>finish=resolve);events.push('advance-end');});
const stop=writer.run(async()=>events.push('stop'));
await Promise.resolve();assert.deepEqual(events,['advance-begin']);finish();await Promise.all([advance,stop]);
assert.deepEqual(events,['advance-begin','advance-end','stop']);
await assert.rejects(writer.run(()=>{throw Error('rejected mutation');}),/rejected/);
await writer.run(()=>events.push('resume'));
assert.equal(events.at(-1),'resume');console.log('PASS ordered STOP after in-flight advance, failure recovery, one mutation writer');
