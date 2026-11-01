import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {createHash} from 'node:crypto';import {DatabaseSync} from 'node:sqlite';
import {initialAdmissionFromStore} from '../tools/prepared-initial-start-transport.mjs';
// Actual C++ application/BookingStore output, never a user/runtime database.
const root=path.resolve(process.argv[2]??'');
assert.equal(JSON.parse(fs.readFileSync(path.join(root,'fixture.json'),'utf8')).schema,'pgn-closed-native-initial-contract-1');
const file=path.join(root,'native-bookings.sqlite');assert.ok(fs.statSync(file).size<=1048576);
const digest=()=>createHash('sha256').update(fs.readFileSync(file)).digest('hex');const before=digest();
const command=JSON.parse(fs.readFileSync(path.join(root,'command.json'),'utf8'));
const db=new DatabaseSync(file,{readOnly:true});
try{
 const {admission,encoded}=initialAdmissionFromStore(db,command);
 assert.equal(admission.receipt.origin,'INITIAL_START');assert.equal(admission.receipt.mode,'manual-reservations');
 assert.equal(admission.receipt.start,'199907000000000000000');assert.equal(admission.receipt.end,'199908000000000000000');
 assert.equal(db.prepare('SELECT state FROM dispatch_requests WHERE id=?').get(admission.receipt.booking_id).state,'PREPARED');
 fs.writeFileSync(path.join(root,'admission.base64'),encoded,{flag:'wx'});
 assert.equal(digest(),before);
 console.log('PASS actual QtCore application -> committed native SQLite -> production Node admission; no reconstructed receipt, DB unchanged, no remote/controller/GPU');
}finally{db.close();}
