import test from 'node:test';import assert from 'node:assert/strict';
import {parsePhaseTimingReceipt as parse} from '../tools/phase-timing-receipt.mjs';
const line=(i,t)=>'[pgn-phase] descriptor='+i+' '+['mark_begin','mark_end','walk_begin','walk_end','verify_begin','verify_end','publish_begin','publish_end'].map((n,j)=>n+'='+t[j]).join(' ');
const header='[pgn-phase] calibration globaltimer_span=3120 cuda_event_ms=0.003120 count=2 scope=NATIVE_SM86_DIAGNOSTIC_NOT_PRODUCTION';
const a=line(0,[1000,1100,1110,2500,2510,2600,2610,2620]),b=line(1,[1050,1800,2630,4000,4010,4100,4110,4120]);
const raw=[header,a,b].join('\n'),opts={expected_count:2};
test('synthetic exact phase wire retains clocks, overlap and negative ready offset, not hardware proof',()=>{
 const r=parse(raw,opts);assert.equal(r.raw_rows[1].mark_begin,'1050');assert.equal(r.derived_intervals[0].signed_next_mark_end_minus_verify_end_ns,'-800');assert.equal(r.derived_intervals[0].walk_ns,'1390');assert.equal(r.calibration.cuda_event_ms,'0.003120');assert.match(r.evidence_scope,/NOT_HARDWARE/);assert.ok(!Object.hasOwn(r,'observed_utc'));
});
test('CRLF and unrelated source records survive filtering, without accepting unsupported phase records',()=>{
 assert.deepEqual(parse('[pgn-counters] time_ns=17\r\n'+raw.replaceAll('\n','\r\n')+'\r\n',opts),parse(raw,opts));assert.throws(()=>parse(raw+'\n[pgn-phase] whatever=3',opts));
});
test('precise uint64 beyond Number safe integer stays exact',()=>{
 const offset=9007199254740993n;const large=[header,line(0,[1000,1100,1110,2500,2510,2600,2610,2620].map(n=>BigInt(n)+offset)),line(1,[1050,1800,2630,4000,4010,4100,4110,4120].map(n=>BigInt(n)+offset))].join('\n');assert.equal(parse(large,opts).raw_rows[0].mark_begin,'9007199254741993');
});
test('missing/zero/duplicate/reordered phases and fields refuse',()=>{
 for(const bad of [header+'\n'+a,raw.replace('mark_begin=1000','mark_begin=0'),raw+'\n'+b,header+'\n'+b+'\n'+a,raw.replace('mark_end=1100','mark_end=1100 mark_end=1100'),raw.replace('walk_end=2500','walk_end=1090')])assert.throws(()=>parse(bad,opts));
});
test('cross-iteration dependencies refuse even when each row is locally monotone',()=>{
 assert.throws(()=>parse(raw.replace('mark_end=1800','mark_end=2700').replace('walk_begin=2630','walk_begin=2701'),opts));assert.throws(()=>parse(raw.replace('walk_begin=2630','walk_begin=2619'),opts));
});
test('clock calibration, scope, uint64 and resource admissions refuse without invented zero',()=>{
 for(const bad of [raw.replace('globaltimer_span=3120','globaltimer_span=3121'),raw.replace('0.003120','999.003120'),raw.replace('NATIVE_SM86','OTHER_GPU'),raw.replace('mark_begin=1000','mark_begin=18446744073709551616'),raw.replace('mark_begin=1000','mark_begin=-1')])assert.throws(()=>parse(bad,opts));
 for(const options of [{},{expected_count:0},{expected_count:257},{expected_count:2,max_bytes:8},{expected_count:1}])assert.throws(()=>parse(raw,options));
});
