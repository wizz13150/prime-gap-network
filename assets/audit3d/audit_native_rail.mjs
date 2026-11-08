// The verified native Atlas owns ranges, zoom and selection. This renderer
// shares its coordinate space with the articulated scene; no mock table/data.
const rail=document.querySelector('#atlas-rail'),actor=document.querySelector('#actor'),NS='http://www.w3.org/2000/svg';
const states=['assigned','pending_audits','audits_assigned','audits_passed','audit_failed'];
const colors=['#70badb','#e6bd64','#ba9aee','#63dfb3','#f37b7e'];
const titles=['Assigned','Awaiting audits','Audits assigned','Audits passed','Failed'];
let atlas=null,jobs=[],previewGeneration=0;
const send=value=>window.chrome?.webview?.postMessage(JSON.stringify({event:'atlas',...value}));
const U=1n<<40n;
function bit(n){let k=BigInt(n.toString(2).length-1),base=1n<<k;return k*U+(n-base)*U/base}
function unit(n){const a=BigInt(atlas.start),b=BigInt(atlas.end);return Number((atlas.bit_scale?(bit(n)-bit(a))*1000000n/(bit(b)-bit(a)):(n-a)*1000000n/(b-a)))/1000000}
function lanes(){return states.filter(s=>atlas.totals[s]?.tickets>0||s==='audits_assigned'||s==='audits_passed')}
function y(state){const list=lanes();return 4+(Math.max(0,list.indexOf(state))+.5)*27/list.length}
function make(name,attrs,parent){const n=document.createElementNS(NS,name);for(const[k,v]of Object.entries(attrs))n.setAttribute(k,String(v));parent?.append(n);return n}
function label(n){const s=n.toString();return '≈ '+s[0]+'.'+s.slice(1,5)+' × 10'+s.length.toString()}
function redraw(){
 rail.replaceChildren();if(!atlas||!atlas.start||!atlas.end)return;
 const width=rail.clientWidth,svg=make('svg',{width:'100%',height:'100%',viewBox:`0 0 ${width} 58`},rail);
 for(const state of lanes())make('path',{d:`M0 ${y(state)}H${width}`,stroke:'#23424a','stroke-width':1},svg);
 // Bins describe coverage, not unique ticket identities. Hollow glyphs are
 // aggregate cells; solid glyphs below are exact detailed ticket identities.
 if(atlas.details_truncated)for(let i=0;i<atlas.bins.length;i++){
  const bin=atlas.bins[i],x=unit((BigInt(bin.start)+BigInt(bin.end))/2n)*width;
  for(let s=0;s<states.length;s++)if(BigInt(bin.stages?.[states[s]]?.positions??0)>0n){
   make('circle',{cx:x,cy:y(states[s]),r:2.2,fill:'none',stroke:colors[s],opacity:.45,'data-bin':i},svg);
  }
 }
 for(const item of atlas.details??[]){const x=unit((BigInt(item.start)+BigInt(item.end))/2n)*width;if(x<0||x>width)continue;
  const s=states.indexOf(item.stage),selected=item.request===atlas.selected;
  const node=make('circle',{cx:x,cy:y(item.stage),r:selected?4.1:3.2,fill:colors[s]??colors[0],stroke:selected?'#f4fff9':'none','stroke-width':1.4,'data-request':item.request},svg);
  node.style.cursor='pointer';
 }
 const a=BigInt(atlas.start),b=BigInt(atlas.end);
 if(atlas.bit_scale){const lo=a.toString(2).length-1,hi=(b-1n).toString(2).length-1;
  for(let k=lo;k<=hi;k++){const n=(1n<<BigInt(k))<a?a:1n<<BigInt(k),x=unit(n)*width;
   make('path',{d:`M${x} 0V31`,stroke:'#27505a','stroke-width':.6},svg);
   const text=make('text',{x,y:k%2?54:42,'text-anchor':'start',class:'atlas-axis'},svg);text.textContent=(k+1)+'b';
   const exp=(1n<<BigInt(k)).toString().length-1;make('tspan',{x,dy:10,'font-size':8},text).textContent='≈ '+(2**k/10**exp).toFixed(1)+'e'+exp;
  }
 }else for(let i=0;i<5;i++){const x=width*i/4,n=a+(b-a)*BigInt(i)/4n,s=n.toString();
  make('path',{d:`M${x} 0V32`,stroke:'#27505a','stroke-width':.6},svg);
  const text=make('text',{x,y:45,'text-anchor':i===0?'start':i===4?'end':'middle',class:'atlas-axis'},svg);
  text.textContent='≈ '+s[0]+'.'+s.slice(1,5)+' × 10';make('tspan',{'baseline-shift':'super','font-size':8},text).textContent=String(s.length-1);
 }
 rail.dataset.detailCount=String(atlas.details?.length??0);rail.dataset.totalVisible=String(atlas.visible_assignments??0);
}
function endpoints(value){
 if(value?.__atlas){atlas=value.__atlas;redraw()}
 const result={...value};delete result.__atlas;if(!atlas)return result;
 const a=rail.getBoundingClientRect(),b=actor.getBoundingClientRect();
 for(const row of [...(atlas.details??[]).map(r=>({...r,request_id:r.request})),...jobs]){
  if(!row.start||!row.end)continue;
  const u=unit((BigInt(row.start)+BigInt(row.end))/2n),x=(a.left-b.left+Math.max(0,Math.min(1,u))*a.width)/b.width;
  const point=state=>({x,y:(a.top-b.top+y(state))/b.height});
  result[row.request_id]={source:point('pending_audits'),target:point('audits_passed'),outside:u<0||u>1};
 }
 return result;
}
const apply=window.pgnAuditApply,project=window.pgnAuditProject,oldPreview=window.pgnAuditPreview;
window.pgnAuditApply=value=>{jobs=value.view?.audits??[];apply({...value,projection:endpoints(value.projection??{})})};
window.pgnAuditProject=value=>project(endpoints(value));
window.pgnAuditPreview=value=>{
 endpoints(value);const details=atlas?.details??[];if(!details.length){oldPreview(endpoints(value));return}
 const generation=++previewGeneration,picked=[details[Math.floor(details.length*.25)],details[Math.floor(details.length*.72)]];
 const id=n=>n.toString(16).padStart(64,'0');let height=1;
 const actorScript=index=>{for(let n=1;n<200;n++){const s='0014'+n.toString(16).padStart(2,'0');let h=2166136261;for(const c of s)h=Math.imul(h^c.charCodeAt(0),16777619)>>>0;if(h%4===index)return s}return '001401'};
 const snapshot=state=>({schema:'PGN_AUDIT_ACTIVITY_V1',state:{genesis:atlas.genesis,bestblockhash:id(++height),height},scope:'global_counts_with_bounded_public_job_rows',rebase:false,truncated:false,
  totals:{active_reports:2,active_jobs:4,recent_reports:0,passed_audits_since_height:state==='passed'?4:0,selected_active_reports:2,selected_recent_reports:0},
  audits:picked.map((p,i)=>({request_id:p.request,start:p.start,end:p.end,audited:state==='passed',jobs:[0,1].map(slot=>({slot,attempt:0,state,actor:actorScript(i*2+slot),issued:3,...(state==='passed'?{passed_height:height}:{})}))}))});
 const empty=snapshot('assigned');empty.audits=[];window.pgnAuditApply({view:empty,projection:value,reset:true,preview:true});
 window.pgnAuditApply({view:snapshot('assigned'),projection:value,reset:false,preview:true});window.__auditNetwork.previewStarted=performance.now();
 setTimeout(()=>{if(generation!==previewGeneration)return;window.pgnAuditApply({view:snapshot('passed'),projection:value,reset:false,preview:true});window.__auditNetwork.previewStarted=performance.now()},14000);
};
const suspend=window.pgnAuditSuspend;window.pgnAuditSuspend=()=>{previewGeneration++;suspend()};
new ResizeObserver(()=>{redraw();if(atlas)project(endpoints({}))}).observe(rail);
let pressed=null;rail.onpointerdown=e=>{pressed={x:e.clientX,request:e.target.dataset?.request,bin:e.target.dataset?.bin};rail.setPointerCapture(e.pointerId)};
rail.onpointerup=e=>{
 if(!pressed)return;
 if(Math.abs(e.clientX-pressed.x)>6)send({action:'shift',delta_ppm:Math.round((pressed.x-e.clientX)/rail.clientWidth*1000000)});
 else if(pressed.request)send({action:'inspect',request:pressed.request});
 else if(pressed.bin!==undefined)send({action:'bin',index:Number(pressed.bin)});
 pressed=null;
};
rail.onpointercancel=()=>{pressed=null};rail.onwheel=e=>{e.preventDefault();const r=rail.getBoundingClientRect();send({action:'zoom',steps:e.deltaY<0?1:-1,x_ppm:Math.round((e.clientX-r.left)/r.width*1000000)})};
for(const button of document.querySelectorAll('.atlas-tools button'))button.onclick=()=>send({action:button.dataset.action,steps:Number(button.dataset.steps??0)});
