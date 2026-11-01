import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import assert from 'node:assert/strict';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
import {createHash} from 'node:crypto';
// Only a finite CPU test binary is built/run, never the .cu runtime, CUDA/SDK,
// GPU, driver, miner, live data or an installed image. No network operations.
const directory=path.dirname(fileURLToPath(import.meta.url));
const source=path.resolve(directory,'../tools/continuous-gpu-runtime.cu');
const contents=fs.readFileSync(source,'utf8'),hash=createHash('sha256').update(contents).digest('hex');
assert.ok(fs.statSync(source).size<65536);
assert.ok(contents.includes('#ifndef PGN_SOURCE_BOUNDARY_CLOSED_TEST'));
assert.ok(contents.includes('CHECK(cudaGraphAddNode(&tailBegin,body,&walk,1,&work))'));
assert.ok(contents.includes('CHECK(cudaGraphAddNode(&proofNode,body,&verifyBegin,1,&verifyLoop))'));
assert.ok(contents.includes('w_is_probable_prime<3>(low,high,0)'));
assert.ok(!contents.includes('g.slot+g.gap>=requested.low+length'));
const compiler=process.argv[2]??'C:/msys64/mingw64/bin/g++.exe';
assert.ok(fs.statSync(compiler).isFile(),'Explicit CPU compiler required, not a CUDA compiler');
assert.match(path.basename(compiler),/^(?:g\+\+|clang\+\+)(?:\.exe)?$/);
const environment={...process.env};
const pathKey=Object.keys(environment).find(key=>key.toLowerCase()==='path')??'PATH';
environment[pathKey]=path.dirname(compiler)+path.delimiter+(environment[pathKey]??'');
const temporary=fs.mkdtempSync(path.join(os.tmpdir(),'pgn-source-boundary-'));
try{
 const executable=path.join(temporary,process.platform==='win32'?'closed-boundary-test.exe':'closed-boundary-test');
 const build=spawnSync(compiler,['-std=c++17','-O2','-Wall','-Wextra','-Werror',path.join(directory,'source_terminal_boundary_test.cpp'),'-o',executable],{encoding:'utf8',timeout:30000,windowsHide:true,env:environment});
 assert.ifError(build.error);assert.equal(build.status,0,build.stdout+build.stderr);
 const run=spawnSync(executable,[],{encoding:'utf8',timeout:10000,windowsHide:true,env:environment});
 assert.ifError(run.error);assert.equal(run.status,0,run.stdout+run.stderr);
 assert.match(run.stdout,/source terminal boundary checks=\d+ failures=0/);process.stdout.write(run.stdout);
 assert.equal(createHash('sha256').update(fs.readFileSync(source)).digest('hex'),hash,'Source candidate remains unchanged');
 console.log('PASS graph dependency/source-only ownership guards; runtime CUDA translation and terminal cost NOT qualified');
}finally{
 const resolved=path.resolve(temporary),prefix=path.resolve(os.tmpdir())+path.sep;
 assert.ok(resolved.startsWith(prefix)&&path.basename(resolved).startsWith('pgn-source-boundary-'));
 fs.rmSync(resolved,{recursive:true,force:true});
}
