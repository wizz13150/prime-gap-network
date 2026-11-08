// Presentation model only. The native Core reader must authenticate the chain
// anchor before providing a snapshot. This module never validates an audit,
// creates a reward, signs a message, or infers computation from elapsed time.
const HASH=/^[0-9a-f]{64}$/,STATES=new Set(['assigned','committed','passed','failed']);
const ENTITIES=['michael','helel','boshel','kitabel'];
function require(value,message){if(!value)throw Error(message)}
function actorEntity(script){let hash=2166136261;for(const c of script)hash=Math.imul(hash^c.charCodeAt(0),16777619)>>>0;return ENTITIES[hash%4]}
export class AuditActivityModel{
 constructor({maxEvents=24,maxAgeMs=6000,maxGapMs=15000}={}){
  require(Number.isInteger(maxEvents)&&maxEvents>0&&maxEvents<=64,'Bounded event budget required');this.maxEvents=maxEvents;this.maxAgeMs=maxAgeMs;this.maxGapMs=maxGapMs;this.rows=new Map();this.events=[];this.anchor=null;this.visible=false;this.observedAt=null;this.coalesced=0;this.expired=0;
 }
 observe(view,{now,visible=true,capturedAt=now,reset=false,scope='page'}={}){
  require(Number.isFinite(now)&&Number.isFinite(capturedAt)&&capturedAt<=now+100,'Monotonic observation time required');
  const global=view?.schema==='PGN_AUDIT_ACTIVITY_V1';
  require((global||view?.schema==='PGN_WORK_VIEW_V1')&&Array.isArray(view.audits)&&view.audits.length<=100,'Bounded native audit view required');
  const s=view.state;require(s&&HASH.test(s.genesis)&&HASH.test(s.bestblockhash)&&Number.isInteger(s.height)&&s.height>=0,'Native chain identity required');
  if(global){
   scope='global-active';require(view.scope==='global_counts_with_bounded_public_job_rows'&&typeof view.rebase==='boolean'&&typeof view.truncated==='boolean','Explicit global projection contract required');
   require(view.totals&&['active_reports','active_jobs','recent_reports','passed_audits_since_height','selected_active_reports','selected_recent_reports'].every(k=>Number.isSafeInteger(view.totals[k])&&view.totals[k]>=0),'Exact nonnegative global counts required');
  }
  require(scope==='page'||scope==='global-active','Explicit feed coverage required');
  const next=new Map(),requests=new Set();
  for(const report of view.audits){
   require(HASH.test(report.request_id)&&!requests.has(report.request_id)&&typeof report.audited==='boolean'&&Array.isArray(report.jobs)&&report.jobs.length<=2,'Invalid native report identity');requests.add(report.request_id);
   const slots=new Set();
   for(const job of report.jobs){
    require((job.slot===0||job.slot===1)&&!slots.has(job.slot)&&Number.isInteger(job.attempt)&&job.attempt>=0&&STATES.has(job.state)&&typeof job.actor==='string'&&/^[0-9a-f]{2,8192}$/.test(job.actor)&&job.actor.length%2===0,'Invalid native audit job');slots.add(job.slot);
    if(job.passed_height!==undefined)require(Number.isInteger(job.passed_height)&&job.passed_height>0&&job.passed_height<=s.height,'Accepted audit height required');
    const key=report.request_id+':'+job.slot,row={request:report.request_id,slot:job.slot,attempt:job.attempt,actor:job.actor,state:job.state,issued:Number.isInteger(job.issued)?job.issued:null,passedHeight:job.passed_height??null,expired:job.expired===true,reportAudited:report.audited,entity:actorEntity(job.actor),start:report.start??null,end:report.end??null};
    row.revision=[row.attempt,row.actor,row.state,row.expired].join(':');next.set(key,row);
   }
  }
  const stale=now-capturedAt>this.maxGapMs,changedEpoch=this.anchor&&(this.anchor.genesis!==s.genesis||s.height<this.anchor.height||(s.height===this.anchor.height&&s.bestblockhash!==this.anchor.hash)),gap=this.observedAt!==null&&now-this.observedAt>this.maxGapMs;
  const readerKey=[scope,view.kind??'',view.offset??0,view.limit??100].join(':');
  const rebase=reset||view.rebase===true||!this.anchor||changedEpoch||gap||!visible||!this.visible||stale||readerKey!==this.readerKey;
  // Re-entering a hidden tab establishes the present. Historical differences
  // are never replayed as a burst of invented current network activity.
  if(rebase)this.events=[];
  else for(const[key,row]of next){
   const prior=this.rows.get(key);if(prior?.revision===row.revision)continue;
   if(!prior&&Math.max(row.issued??0,row.passedHeight??0)<=this.anchor.height)continue;
   const event={...row,before:prior?.state??null,at:now,kind:row.expired?'lease-expired':row.state};
   // A packet disappears from a page when paging/filtering; absence is not a
   // passed result and never generates a return or reward animation.
   if(this.events.length>=this.maxEvents){this.events.shift();this.coalesced++}this.events.push(event);
  }
  this.rows=next;this.anchor={genesis:s.genesis,hash:s.bestblockhash,height:s.height};this.visible=visible;this.observedAt=now;this.scope=scope;this.readerKey=readerKey;this.stale=stale;this.totals=global?{...view.totals}:null;this.truncated=global?view.truncated:null;this.rebased=rebase;this.prune(now);
  return this.snapshot();
 }
 prune(now){const old=this.events.length;this.events=this.events.filter(e=>now-e.at<=this.maxAgeMs);this.expired+=old-this.events.length;}
 take(now,limit=4){require(Number.isInteger(limit)&&limit>=0&&limit<=4,'At most four simultaneous arrivals');this.prune(now);return this.events.splice(0,limit)}
 suspend(){this.visible=false;this.events=[];}
 snapshot(){return{anchor:this.anchor,scope:this.scope,stale:this.stale,rebased:this.rebased,totals:this.totals,truncated:this.truncated,queued:this.events.length,coalesced:this.coalesced,expired:this.expired,active:[...this.rows.values()].filter(r=>!r.expired&&(r.state==='assigned'||r.state==='committed'))}}
}
