import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import assert from 'node:assert/strict';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
import {createHash} from 'node:crypto';
// Builds/runs ONE finite CPU formatter test. No CUDA/SDK/driver/miner/remote.
const directory=path.dirname(fileURLToPath(import.meta.url));
const source=path.resolve(directory,'../tools/continuous-gpu-runtime.cu');
const digest=file=>createHash('sha256').update(fs.readFileSync(file)).digest('hex');
assert.ok(fs.statSync(source).size<65536);
const original=digest(source),text=fs.readFileSync(source,'utf8');
assert.ok(text.includes('const uint64_t ownedFirst=resumeLow>d.low?resumeLow:d.low;'));
assert.ok(text.includes('const uint64_t clippedEnd=requestedEnd<ownedEnd?requestedEnd:ownedEnd;'));
assert.ok(text.includes('formatTerminalBoundaryV2(boundary,sizeof(boundary),next,ownedFirst,clippedEnd,requested.high,tail)'));
assert.ok(text.includes('std::fputs(boundary,stdout);'));
assert.ok(text.includes('coverage=SOURCE_PRP_INDEPENDENT_PROOF_PENDING boundary_protocol=pgn-source-boundary-v2\\n'));
const compiler=process.argv[2]??'C:/msys64/mingw64/bin/g++.exe';
assert.ok(fs.statSync(compiler).isFile());assert.match(path.basename(compiler),/^(?:g\+\+|clang\+\+)(?:\.exe)?$/);
const environment={...process.env},pathKey=Object.keys(environment).find(key=>key.toLowerCase()==='path')??'PATH';
environment[pathKey]=path.dirname(compiler)+path.delimiter+(environment[pathKey]??'');
const temporary=fs.mkdtempSync(path.join(os.tmpdir(),'pgn-source-publication-v2-'));
try{
 const executable=path.join(temporary,process.platform==='win32'?'closed-publication-v2-test.exe':'closed-publication-v2-test');
 const build=spawnSync(compiler,['-std=c++17','-O2','-Wall','-Wextra','-Werror',path.join(directory,'source_terminal_publication_v2_test.cpp'),'-o',executable],{encoding:'utf8',timeout:30000,windowsHide:true,env:environment});
 assert.ifError(build.error);assert.equal(build.status,0,build.stdout+build.stderr);
 const run=spawnSync(executable,[],{encoding:'utf8',timeout:10000,windowsHide:true,env:environment});
 assert.ifError(run.error);assert.equal(run.status,0,run.stdout+run.stderr);process.stdout.write(run.stdout);
 assert.match(run.stdout,/source terminal publication v2 checks=\d+ failures=0/);
 assert.equal(digest(source),original,'CPU test never changes the candidate source');
}finally{
 const resolved=path.resolve(temporary),prefix=path.resolve(os.tmpdir())+path.sep;
 assert.ok(resolved.startsWith(prefix)&&path.basename(resolved).startsWith('pgn-source-publication-v2-'));
 fs.rmSync(resolved,{recursive:true,force:true});
}
