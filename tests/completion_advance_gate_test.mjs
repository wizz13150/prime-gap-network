import assert from 'node:assert/strict';
import {CompletionAdvanceGate,isUncertainTransport,completeSourceMatches} from '../tools/completion-advance-gate.mjs';
let now=0;const run='a'.repeat(32),next='b'.repeat(32),gate=new CompletionAdvanceGate({now:()=>now,retryDelayMs:1000});
const packet=(epoch=run,utc='2026-10-11T06:00:00.0000001Z')=>({manifest:{run:epoch,start:'100000000000000000000',end:'100001000000000000000'},state:'OFF',owned:false,operator_stop:false,last_exit:{run:epoch,exit_code:0},pending_bytes:0,observed_utc:utc,completion:{schema:'pgn-source-range-completion-1',run:epoch,start:'100000000000000000000',end:'100001000000000000000',source_positions:'1000000000000000',source_range_complete:true,source_exit_code:0}});
for(const c of [null,{...packet().completion,run:next},{...packet().completion,source_range_complete:false},{...packet().completion,source_positions:'1'},{...packet().completion,start:'1'},{...packet().completion,source_exit_code:1}])assert.equal(gate.claim({...packet(),completion:c},run),null);
for(const changed of [{state:'RUNNING'},{owned:true},{pending_bytes:1},{last_exit:{run:next}},{observed_utc:'invalid'}])assert.equal(gate.claim({...packet(),...changed},run),null);
assert.equal(gate.claim({...packet(),operator_stop:true},run),null);
// The real controller publishes last-completion during advance, not worker
// exit. A read-only candidate closes that cycle without accepting exit0 alone.
const candidatePacket={...packet(),completion:{...packet().completion,run:next},completion_candidate:packet().completion};
const candidateGate=new CompletionAdvanceGate();assert.equal(candidateGate.claim(candidatePacket,run).run,run);
for(const candidate of [null,{...packet().completion,run:next},{...packet().completion,source_positions:'1'},{...packet().completion,source_range_complete:false}])assert.equal(new CompletionAdvanceGate().claim({...candidatePacket,completion_candidate:candidate},run),null);
assert.equal(gate.claim(packet(),run,{paused:true}),null);
assert.equal(gate.claim(packet(),run,{enabled:false}),null);
assert.equal(gate.claim(packet(),run,{fragment:'partial'}),null);
let ticket=gate.claim(packet(),run);assert.equal(ticket.attempt,1);assert.equal(gate.claim(packet(),run),null);
assert.equal(gate.settle(ticket,{killed:true}).retryable,true);
now=1000;assert.equal(gate.claim(packet(),run),null); // unchanged source snapshot
ticket=gate.claim(packet(run,'2026-10-11T06:00:00.0000002Z'),run);assert.equal(ticket.attempt,2); // do not collapse sub-ms source dates
gate.settle(ticket,{code:'ECONNRESET'});now=2000;
ticket=gate.claim(packet(run,'2026-10-11T06:00:01.0000000Z'),run);assert.equal(ticket.attempt,3);
assert.equal(gate.settle(ticket,{code:'ETIMEDOUT'}).held,true);now=5000;
assert.equal(gate.claim(packet(run,'2026-10-11T06:00:02.0000000Z'),run),null);
gate.reset();ticket=gate.claim(packet(),run);assert.equal(gate.settle(ticket,Error('Completed source identity/range mismatch')).held,true);
assert.equal(gate.claim(packet(run,'2026-10-11T06:00:03.0000000Z'),run),null); // never loop a rejected guard
gate.reset();ticket=gate.claim(packet(),run);const newer=gate.claim(packet(next),next);
assert.equal(gate.settle(ticket,{killed:true}).stale,true);assert.equal(gate.settle(newer).held,true);
gate.reset();ticket=gate.claim(packet(),run);gate.reset();assert.equal(gate.settle(ticket).stale,true);
ticket=gate.claim(packet(run,'2026-10-11T06:00:00.000Z'),run);gate.settle(ticket,{killed:true});now+=1000;
assert.equal(gate.claim(packet(run,'2026-10-11T06:00:00.000000000Z'),run),null); // same instant, different precision
assert.equal(gate.claim(packet(run,'2026-10-11T06:00:00.000000001Z'),run).attempt,2);
assert.equal(isUncertainTransport({code:255,stderr:'Connection reset by peer'}),true);
assert.equal(isUncertainTransport({code:255,stderr:'Permission denied (publickey)'}),false);
assert.equal(isUncertainTransport({code:1,stderr:'Operator STOP holds automatic succession'}),false);
assert.equal(isUncertainTransport(Error('Existing completion conflicts')),false);
assert.throws(()=>new CompletionAdvanceGate({maxAttempts:4}),/Invalid/);
const resumed=packet();Object.assign(resumed.manifest,{resume_previous_run:next,checkpoint_input_sha256:'c'.repeat(64),queued_intent:'11111111-2222-4333-8444-555555555555'});
assert.equal(completeSourceMatches(resumed,run),false);
resumed.resume_lineage={schema:'pgn-source-resume-lineage-1',ready:true,error:'',terminal:'complete',run,booking_id:resumed.manifest.queued_intent,start:resumed.manifest.start,end:resumed.manifest.end,epochs:2,chain_sha256:'d'.repeat(64),scope:'LOCAL_RAW_LINEAGE_COUNTER_DELTAS_NOT_INDEPENDENT_PRIME_COVERAGE'};
assert.equal(completeSourceMatches(resumed,run),true);
for(const change of [{terminal:'stopped'},{ready:'true'},{epochs:1},{epochs:'2'},{run:next},{booking_id:'unknown'},{chain_sha256:'G'.repeat(64)},{error:'not paid'}])assert.equal(completeSourceMatches({...resumed,resume_lineage:{...resumed.resume_lineage,...change}},run),false);
assert.equal(new CompletionAdvanceGate().claim(packet(run,'2026-10-11T06:00:00Z\n'),run),null);
for(const change of [{owned:null},{operator_stop:null},{owned:undefined},{operator_stop:undefined}])assert.equal(new CompletionAdvanceGate().claim({...packet(),...change},run),null);
console.log('PASS fresh-source gated transport-only bounded retries, strict raw UTC/STOP, exact resumed-source lineage ABI and changed-run isolation; invariant/auth failures held');
