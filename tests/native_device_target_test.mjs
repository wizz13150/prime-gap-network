import assert from 'node:assert/strict';
import {requireControllerTarget,legacyNativeDevice,finiteNativeDevice,resolveNativeController} from '../tools/native-device-target.mjs';
const other='GPU-33f44d5c-91e4-6ab6-1e45-8c93135fd265';
for(const action of ['start','stop']){
 assert.equal(requireControllerTarget({action,device:legacyNativeDevice}),legacyNativeDevice);
 assert.equal(requireControllerTarget({action,device:legacyNativeDevice.toUpperCase()}),legacyNativeDevice);
 for(const device of [other,'CPU',undefined,null,{},'GPU-unknown'])
  assert.throws(()=>requireControllerTarget({action,device}),/no command sent/);
 assert.equal(requireControllerTarget({action,device:other},other),other);
 assert.throws(()=>requireControllerTarget({action,device:legacyNativeDevice},other),/no command sent/);
 assert.deepEqual(resolveNativeController({action,device:legacyNativeDevice}),{device:legacyNativeDevice,kind:'productive'});
 assert.deepEqual(resolveNativeController({action,device:finiteNativeDevice}),{device:finiteNativeDevice,kind:'bounded_test'});
 for(const device of ['CPU',undefined,null,{},'GPU-unknown'])assert.throws(()=>resolveNativeController({action,device}));
}
assert.throws(()=>resolveNativeController({action:'dispatch',device:finiteNativeDevice}));
console.log(JSON.stringify({status:'PASS',selected_device_isolation:true,
 scope:'Transport target validation only;3060 physical controller and live button delivery remain unpaid'}));
