import fs from 'node:fs';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
import {createRequire} from 'node:module';
import assert from 'node:assert/strict';
const require=createRequire('C:/Users/toto/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/');
const {chromium}=require('playwright');
const source=path.resolve(process.argv[2]),image=path.resolve(process.argv[3]);
const browser=await chromium.launch({headless:true,executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
const page=await browser.newPage({viewport:{width:760,height:1200}});
const errors=[];page.on('pageerror',e=>errors.push(e.message));
try {
 await page.goto(pathToFileURL(source).href,{waitUntil:'networkidle',timeout:30000});
 const frame=page.frameLocator('iframe');
 await frame.locator('path.pgn-model').first().waitFor({timeout:15000});
 assert.equal(await frame.locator('path.pgn-model').count(),4);
 const old=await frame.locator('[data-calendar]').textContent();
 await frame.locator('[data-input="cadence"]').fill('20');
 await frame.locator('[data-input="cadence"]').dispatchEvent('change');
 await page.waitForTimeout(250);
 assert.notEqual(await frame.locator('[data-calendar]').textContent(),old);
 await frame.locator('[data-input="cadence"]').fill('10');
 await frame.locator('[data-input="cadence"]').dispatchEvent('change');
 await page.waitForTimeout(250);
 await frame.locator('[data-series="quadratic"]').click();
 assert.equal(await frame.locator('[data-series="quadratic"]').getAttribute('aria-pressed'),'false');
 await frame.locator('[data-series="quadratic"]').click();
 await page.waitForTimeout(250);
 const widths=[];
 for(const width of [760,360]){
  await page.setViewportSize({width,height:1300});await page.waitForTimeout(200);
  const actual=page.frames().find(f=>f.parentFrame());
  const result=await actual.evaluate(()=>{
   const r=document.getElementById('pgn-emission-20261012');
   const charts=[...r.querySelectorAll('[data-chart]')].map(s=>({width:s.clientWidth,viewbox:s.getAttribute('viewBox'),paths:[...s.querySelectorAll('path.pgn-model')].every(p=>p.getAttribute('d')?.length>100)}));
   return {root:r.clientWidth,body:document.body.clientWidth,overflow:document.documentElement.scrollWidth>document.documentElement.clientWidth,charts};
  });
  assert(!result.overflow);assert(result.charts.every(c=>c.paths));widths.push({width,...result});
  await frame.locator('#pgn-emission-20261012').screenshot({path:image.replace('.png','-'+width+'.png')});
 }
 assert.deepEqual(errors,[]);
 console.log(JSON.stringify({status:'PASS',controls:'Cadence changes actual curves/calendar; both series toggles; original assumptions restored',widths,script_errors:errors}));
} finally {await browser.close()}
