import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {DatabaseSync} from 'node:sqlite';
import {resumeAdmissionFromStore} from '../tools/prepared-checkpoint-resume-transport.mjs';
import {QueuedDispatch} from '../tools/queued-dispatch.mjs';

// Consume an actual C++ SQLite backup and exact native JSON, not a JS-created
// facsimile. This is explicitly synthetic source/profile evidence, not a run.
const directory=path.resolve(process.argv[2]??'');
assert.match(path.basename(directory),/^root554-native-resume-contract(?:-v[2-9])?$/);
const file=path.join(directory,'local-bookings.sqlite');
const digest=()=>createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const before=digest(),command=JSON.parse(fs.readFileSync(path.join(directory,'command.json'),'utf8'));
const receipt=fs.readFileSync(path.join(directory,'receipt.json'),'utf8');
const db=new DatabaseSync(file,{readOnly:true});
try{
 const result=resumeAdmissionFromStore(db,command);
 assert.deepEqual(result.admission.receipt,JSON.parse(receipt));
 assert.equal(result.admission.receipt.checkpoint.resume.tests,'9007199254741995');
 assert.equal(result.admission.receipt.raw_conservation_requirement,'LINEAGE_RAW_CONSERVATION_REQUIRED');
 assert.equal(digest(),before,'Actual native SQLite is readonly');
 const encodedFile=path.join(directory,'admission.base64');
 if(fs.existsSync(encodedFile))assert.equal(fs.readFileSync(encodedFile,'utf8'),result.encoded,'Existing immutable fixture wire must match exactly');
 else fs.writeFileSync(encodedFile,result.encoded,{flag:'wx'});
 console.log('PASS actual native SQLite -> production resume transport; readonly, >2^53 exact, no source lineage/hardware claim');
}finally{db.close();}
// Real native PREPARED row must not close from its cumulative new-run footer
// alone. This copied DB is exclusively a closed synthetic fixture, not user
// data or a successful hardware SOURCE event.
const temporary=fs.mkdtempSync(path.join(os.tmpdir(),'pgn-resume-completion-'));
try{
 const copied=path.join(temporary,'copied-native.sqlite');fs.copyFileSync(file,copied);
 const dispatch=new QueuedDispatch(copied);
 try{
  const original=dispatch.db.prepare('SELECT state,run FROM dispatch_requests WHERE id=?').get(command.booking_id);
  const tuple=JSON.parse(receipt).checkpoint.resume;
  const completion={schema:'pgn-source-range-completion-1',run:command.new_run,queued_intent:command.booking_id,start:tuple.start,end:tuple.end,source_positions:tuple.length,source_range_complete:true,source_exit_code:0,independent_coverage:false};
  const changes=dispatch.db.prepare('SELECT total_changes() n').get().n;
  assert.equal(dispatch.complete(completion),false);
  assert.throws(()=>dispatch.complete(completion,{requireKnownIntent:true}),/lineage/);
  assert.deepEqual(dispatch.db.prepare('SELECT state,run FROM dispatch_requests WHERE id=?').get(command.booking_id),original);
  assert.equal(dispatch.db.prepare('SELECT state FROM bookings WHERE id=?').get(command.old_run).state,'PAUSED');
  assert.equal(dispatch.db.prepare('SELECT total_changes() n').get().n,changes);
  console.log('PASS actual native PREPARED -> Node completion refuses missing raw lineage, no UPDATE or fake SOURCE closure');
 }finally{dispatch.close();}
}finally{
 const checked=path.resolve(temporary),prefix=path.resolve(os.tmpdir())+path.sep;
 assert.ok(checked.startsWith(prefix)&&path.basename(checked).startsWith('pgn-resume-completion-'));
 fs.rmSync(checked,{recursive:true,force:true});
}
