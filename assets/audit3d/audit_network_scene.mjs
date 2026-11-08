import{AuditActivityModel,canCompleteLanding}from'./audit_activity_model.mjs';
const entities=['michael','helel','boshel','kitabel'],model=new AuditActivityModel(),actor=document.querySelector('#actor');
let scene,controllers=new Map(),ready=false,shown=false,last=0,lastRender=0,preview=false,latest=null,projection={};
const targetHeights={michael:3.2,helel:3.17,boshel:1.84,kitabel:2.90},nameAnchors=new Map();let homeCamera,homeRotation;const restLayouts=new Map();let layoutQueued=false;
let revealGeneration=0;
async function revealTogether(){
 const generation=++revealGeneration;
 if(!ready||!shown||!window.pgnAuditRailReady||document.body.classList.contains('audit-ready'))return;
 await actor.updateComplete;actor.jumpCameraToGoal();scene.queueRender();
 await new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)));
 if(generation!==revealGeneration||!shown||!window.pgnAuditRailReady)return;
 names();document.body.classList.add('audit-ready');native({event:'presented'});
}
function meshBounds(root){scene.updateMatrixWorld(true);let box=null;root.traverse(n=>{if(!n.isMesh)return;if(n.isSkinnedMesh)n.computeBoundingBox();else n.geometry.computeBoundingBox();const local=n.isSkinnedMesh?n.boundingBox:n.geometry.boundingBox;if(local){const bounds=local.clone().applyMatrix4(n.matrixWorld);box=box?box.union(bounds):bounds}});return box}
function normalizeActors(){const heights={},floor=-Math.max(...Object.values(targetHeights))/2+1.10;for(const entity of entities){const root=scene.getObjectByName('Actor'+entity[0].toUpperCase()+entity.slice(1));let bounds=meshBounds(root);if(!bounds)throw Error('Actor geometry missing '+entity);const original=bounds.max.y-bounds.min.y;root.scale.multiplyScalar(targetHeights[entity]/original);root.updateWorldMatrix(false,true);bounds=meshBounds(root);const position=root.getWorldPosition(root.position.clone());position.y+=floor-bounds.min.y;root.position.copy(root.parent.worldToLocal(position));root.updateWorldMatrix(false,true);bounds=meshBounds(root);heights[entity]={original_height:original,display_height:bounds.max.y-bounds.min.y};}window.__auditNetwork.worldHeights=heights}
function native(value){window.chrome?.webview?.postMessage(JSON.stringify(value))}
function names(){if(!scene)return;const camera=scene.getCamera();camera.updateWorldMatrix(true,false);const visible=[];
 for(const label of document.querySelectorAll('[data-entity-name]')){const entity=label.dataset.entityName,root=scene.getObjectByName('Actor'+entity[0].toUpperCase()+entity.slice(1)),anchor=nameAnchors.get(entity);if(!root||!anchor)continue;const point=root.localToWorld(anchor.clone()),v=point.clone().project(camera),x=(v.x+1)/2*actor.clientWidth,y=(1-v.y)/2*actor.clientHeight+1;label.style.visibility='hidden';if(v.z<-1||v.z>1||x<0||x>actor.clientWidth||y<0||y>document.body.clientHeight-20)continue;visible.push({label,x,y,distance:point.distanceTo(camera.getWorldPosition(point.clone()))})}
 visible.sort((a,b)=>a.x-b.x);for(let i=0;i<visible.length;i++){const entry=visible[i],gap=Math.min(i?entry.x-visible[i-1].x:Infinity,i+1<visible.length?visible[i+1].x-entry.x:Infinity),width=Math.min(120,gap*.88);if(width<64)continue;entry.label.style.width=width+'px';entry.label.style.left=entry.x+'px';entry.label.style.top=Math.min(entry.y,document.body.clientHeight-88)+'px';entry.label.style.visibility='visible';const state=controllers.get(entry.label.dataset.entityName)?.inspect(),working=String(!!state?.job&&!state.metrics.complete);if(entry.label.dataset.working!==working)entry.label.dataset.working=working}
}
function staging(){if(!scene)return;const camera=scene.getCamera();camera.updateWorldMatrix(true,false);homeCamera??=camera.getWorldPosition(camera.position.clone());homeRotation??=camera.getWorldQuaternion(camera.quaternion.clone());
 const v=(x=0,y=0,z=0)=>camera.position.clone().set(x,y,z),right=v(1,0,0).applyQuaternion(homeRotation),up=v(0,1,0).applyQuaternion(homeRotation),forward=v(0,0,-1).applyQuaternion(homeRotation),depth=13;
 // Decode the rest shape once. A pane resize must not CPU-skin every vertex.
 for(const n of entities){if(restLayouts.has(n))continue;const root=scene.getObjectByName('Actor'+n[0].toUpperCase()+n.slice(1));let head,face;root.traverse(node=>{if(node.isBone&&node.name.endsWith('Head'))head=node;if(node.isBone&&node.name.endsWith('headfront'))face=node});if(!head||!face)throw Error('Facing calibration missing '+n);const front=face.getWorldPosition(v()).sub(head.getWorldPosition(v()));front.y=0;front.normalize();const localFront=front.clone().applyQuaternion(root.getWorldQuaternion(root.quaternion.clone()).invert()),toward=forward.clone().negate();toward.y=0;const yaw=Math.atan2(toward.x,toward.z)-Math.atan2(front.x,front.z),turn=root.quaternion.clone().setFromAxisAngle(v(0,1,0),yaw),world=root.getWorldQuaternion(root.quaternion.clone());root.quaternion.copy(root.parent.getWorldQuaternion(root.quaternion.clone()).invert().multiply(turn.multiply(world)));root.updateWorldMatrix(false,true);const bounds=meshBounds(root),foot=bounds.min.clone().add(bounds.max).multiplyScalar(.5);foot.y=bounds.min.y;nameAnchors.set(n,root.worldToLocal(foot));restLayouts.set(n,{width:bounds.max.x-bounds.min.x,front:localFront});window.__auditNetwork.layoutMeshScans=(window.__auditNetwork.layoutMeshScans??0)+1;}
 const width=actor.clientWidth,height=actor.clientHeight,bodyHeight=document.body.clientHeight,aspect=width/Math.max(1,height),footPixel=Math.max(70,bodyHeight-96),desiredHeight=Math.max(50,footPixel-18),totalWidth=[...restLayouts.values()].reduce((n,r)=>n+r.width,0),gap=.24;
 const tanFov=Math.max(3.2*1.06*height/(2*depth*desiredHeight),(totalWidth+gap*5)/(2*depth*aspect)),fov=2*Math.atan(tanFov)*180/Math.PI;
 actor.minFieldOfView='5deg';actor.maxFieldOfView='60deg';actor.fieldOfView=fov+'deg';const viewWidth=2*depth*tanFov*aspect,space=Math.max(gap,(viewWidth-totalWidth)/5);let cursor=-viewWidth/2+space;
 const plane=homeCamera.clone().addScaledVector(forward,depth),vertical=(1-2*footPixel/height)*depth*tanFov,alignment={};
 for(const n of entities){const root=scene.getObjectByName('Actor'+n[0].toUpperCase()+n.slice(1)),rest=restLayouts.get(n),old=root.getWorldPosition(v()),oldQ=root.getWorldQuaternion(root.quaternion.clone()),goal=plane.clone().addScaledVector(right,cursor+rest.width/2).addScaledVector(up,vertical);cursor+=rest.width+space;
  const current=rest.front.clone().applyQuaternion(oldQ),toward=homeCamera.clone().sub(goal);current.y=0;toward.y=0;const yaw=Math.atan2(toward.x,toward.z)-Math.atan2(current.x,current.z),turn=root.quaternion.clone().setFromAxisAngle(v(0,1,0),yaw);root.quaternion.copy(root.parent.getWorldQuaternion(root.quaternion.clone()).invert().multiply(turn.clone().multiply(oldQ)));root.updateWorldMatrix(false,true);const anchor=root.localToWorld(nameAnchors.get(n).clone()),next=old.clone().add(goal.clone().sub(anchor)),delta=next.clone().sub(old);root.position.copy(root.parent.worldToLocal(next));root.updateWorldMatrix(false,true);scene.updateMatrixWorld(true);controllers.get(n)?.relocate(delta.toArray(),turn.toArray(),old.toArray());alignment[n]=rest.front.clone().applyQuaternion(root.getWorldQuaternion(root.quaternion.clone())).normalize().dot(toward.normalize());}
 window.__auditNetwork.facingAlignment=alignment;window.__auditNetwork.layout={width,height,bodyHeight,fov,desiredHeight,depth};actor.updateComplete.then(()=>{actor.jumpCameraToGoal();scene.queueRender();names()});
}
function requestLayout(){if(layoutQueued)return;layoutQueued=true;requestAnimationFrame(()=>{layoutQueued=false;staging()})}

function points(job){return projection[job.request]||{source:{x:entities.indexOf(job.entity)/4+.125,y:1.08},target:{x:entities.indexOf(job.entity)/4+.125,y:1.08},outside:true}}
function display(snapshot){
 if(!ready)return;latest=snapshot;preview=snapshot.preview===true;shown=true;projection=snapshot.projection??{};
 const observed=model.observe(snapshot.view,{now:performance.now(),reset:snapshot.reset===true,visible:true,capturedAt:performance.now()-(snapshot.age_ms??0)}),events=model.take(performance.now());
 if(observed.rebased)for(const controller of controllers.values())controller.clear();
 for(const controller of controllers.values())controller.palette(snapshot.preview===true?'preview':'live');
 // Keep identities attached while represented. A busy network is sampled,
 // never expanded into an unbounded delayed replay queue.
 const assigned=new Set(),candidates=[...observed.active,...events.filter(e=>!observed.active.some(a=>a.request===e.request&&a.slot===e.slot))];
 for(const job of candidates){
  const key=job.request+':'+job.slot,existing=[...controllers.values()].find(c=>{const j=c.inspect().job;return j&&j.request+':'+j.slot===key});
  let controller=existing;
  if(!controller){const first=entities.indexOf(job.entity);for(let n=0;n<4;n++){const candidate=controllers.get(entities[(first+n)%4]);if(!candidate.inspect().job&&!assigned.has(candidate.entity)){controller=candidate;break}}}
  if(!controller)continue;controller.apply(job,points(job),observed.rebased||!events.some(e=>e.request===job.request&&e.slot===job.slot&&e.attempt===job.attempt));assigned.add(controller.entity);
 }
 for(const controller of controllers.values()){
  const j=controller.inspect().job;if(!j)continue;const current=model.rows.get(j.request+':'+j.slot);
  if(current)controller.apply(current,points(current));
  // Absence can be sampling or a rollback. It is never a success verdict.
  else if(observed.rebased||snapshot.view.absent_watched_requests?.includes(j.request))controller.clear();
 }
 if(!preview)native({event:'watched',requests:[...new Set([...controllers.values()].map(c=>c.inspect().job?.request).filter(Boolean))]});
 window.__auditNetwork.observation=observed;
 revealTogether();
}
async function initialize(){
 actor.pause();await actor.updateComplete;
 for(let p=actor;p;p=Object.getPrototypeOf(p))for(const symbol of Object.getOwnPropertySymbols(p))if(symbol.description==='scene'&&actor[symbol]?.getObjectByName)scene=actor[symbol];
 if(!scene)throw Error('3D scene unavailable');scene.mixer.stopAllAction();actor.minimumRenderScale=1;normalizeActors();
 staging();await actor.updateComplete;actor.jumpCameraToGoal();
 const prototype=scene.getObjectByName('ReportFromMining');if(!prototype)throw Error('Report geometry unavailable');prototype.visible=false;prototype.scale.setScalar(1);
 for(const entity of entities)controllers.set(entity,window.pgnCreateAuditController(scene,entity,prototype));
 ready=true;scene.queueRender();names();new ResizeObserver(()=>requestLayout()).observe(actor);native({event:'ready'});if(latest)display(latest);
}
window.pgnAuditApply=value=>{latest=value;if(ready)display(value)};
window.pgnAuditProject=value=>{projection=value;for(const c of controllers.values()){const job=c.inspect().job;if(job)c.setEndpoints(points(job))}revealTogether()};
window.pgnAuditSuspend=()=>{shown=false;preview=false;model.suspend();for(const controller of controllers.values())controller.clear();scene?.queueRender()};
const id=n=>n.toString(16).padStart(64,'0');
function fixture(height,jobs,rebase=false){return{schema:'PGN_AUDIT_ACTIVITY_V1',state:{genesis:id(1),bestblockhash:id(height+100),height},scope:'global_counts_with_bounded_public_job_rows',rebase,truncated:false,totals:{active_reports:4,active_jobs:4,recent_reports:0,passed_audits_since_height:0,selected_active_reports:4,selected_recent_reports:0},audits:jobs.map((job,i)=>({request_id:id(i+1),audited:false,jobs:[job]}))}}
function previewStart(nativePoints={}){model.suspend();for(const controller of controllers.values())controller.clear();shown=true;preview=true;const projections={},points=Object.values(nativePoints);const rows=entities.map((entity,i)=>{projections[id(i+1)]=points[i]||{source:{x:[.08,.43,.65,.83][i],y:1.08},target:{x:[.08,.43,.65,.83][i],y:1.08}};return{slot:0,attempt:0,state:'assigned',actor:'0014'+(i+1).toString(16).padStart(2,'0'),issued:2}});display({view:fixture(1,[],true),projection:projections,reset:true,preview:true});display({view:fixture(2,rows),projection:projections,preview:true});preview=true;return rows}
window.pgnAuditPreview=points=>{previewStart(points);window.__auditNetwork.previewStarted=performance.now()};
window.__auditNetwork={get ready(){return ready},controllers,model,apply:display,suspend:window.pgnAuditSuspend,observation:null,inspect:()=>[...controllers.values()].map(c=>c.inspect())};
window.pgnAuditProbe=async()=>{
 const rows=previewStart();for(let n=0;n<1800;n++)for(const c of controllers.values())c.step(1/60);
 const held=[...controllers.values()].map(c=>c.inspect()),{auditMeshClearance}=await import('./audit_mesh_clearance.mjs'),meshClearance=auditMeshClearance(scene,controllers);
 const passed=rows.map(j=>({...j,state:'passed',passed_height:3}));display({view:fixture(3,passed),projection,reset:false});
 for(let n=0;n<1500;n++)for(const c of controllers.values())c.step(1/60);
 const released=[...controllers.values()].map(c=>c.inspect());
 window.pgnAuditSuspend();display({view:fixture(80,passed),projection,reset:true});
 const rebased=[...controllers.values()].map(c=>c.inspect());
 const context=document.createElement('canvas').getContext('webgl2'),extension=context?.getExtension('WEBGL_debug_renderer_info');
 const rigChecks=[];for(const entity of entities){const root=scene.getObjectByName('Actor'+entity[0].toUpperCase()+entity.slice(1));root.traverse(mesh=>{if(!mesh.isSkinnedMesh)return;for(const suffix of ['Head','LeftHand','RightHand']){const bone=mesh.skeleton.bones.find(b=>b.name.endsWith(suffix));if(!bone)continue;const index=mesh.skeleton.bones.indexOf(bone),weights=mesh.geometry.attributes.skinWeight,indices=mesh.geometry.attributes.skinIndex,distances=[],center=bone.getWorldPosition(mesh.position.clone());for(let i=0;i<indices.count;i++){let weight=0;for(const part of ['getX','getY','getZ','getW'])if(indices[part](i)===index)weight+=weights[part](i);if(weight>.4){const point=mesh.getVertexPosition(i,mesh.position.clone());mesh.localToWorld(point);distances.push(point.distanceTo(center))}}distances.sort((a,b)=>a-b);rigChecks.push({entity,bone:suffix,world:center.toArray(),vertices:distances.length,median_distance:distances[Math.floor(distances.length/2)],maximum_distance:distances[distances.length-1]})}})}
 native({event:'probe',PASS:held.every(s=>s.phase==='examine'&&s.visible)&&released.every(s=>s.metrics.complete)&&rebased.every(s=>!s.visible)&&meshClearance.every(s=>s.triangle_clearance>=0),mesh_clearance:meshClearance,rig_checks:rigChecks,world_heights:window.__auditNetwork.worldHeights,held,released,return_replays:model.snapshot().queued,renderer:extension?context.getParameter(extension.UNMASKED_RENDERER_WEBGL):null});shown=false;
};
document.addEventListener('visibilitychange',()=>{if(document.hidden)window.pgnAuditSuspend()});
window.pgnAuditTrajectoryProbe=async()=>{
 const{auditMeshClearance}=await import('./audit_mesh_clearance.mjs'),cases=[];shown=false;preview=false;
 for(const origins of [[.08,.43,.65,.83],[.83,.65,.43,.08]]){
  for(const c of controllers.values())c.clear();const jobs=[];
  for(let i=0;i<4;i++){const entity=entities[i],job={request:id(i+1),slot:0,attempt:0,state:'assigned',actor:'0014'+(i+1).toString(16).padStart(2,'0'),entity};jobs.push(job);controllers.get(entity).apply(job,{source:{x:origins[i],y:1.08},target:{x:origins[i],y:1.08}})}
  let checkedApproach=false;for(let n=0;n<1800;n++){for(const c of controllers.values())c.step(1/60);if(!checkedApproach&&[...controllers.values()].some(c=>c.inspect().phase==='capture')){cases.push({origins,stage:'capture',clearance:auditMeshClearance(scene,controllers)});checkedApproach=true}if([...controllers.values()].every(c=>c.inspect().phase==='examine'))break}
  cases.push({origins,stage:'held',clearance:auditMeshClearance(scene,controllers),phases:[...controllers.values()].map(c=>c.inspect().phase)});
  for(let i=0;i<4;i++)controllers.get(entities[i]).apply({...jobs[i],state:'passed'},{source:{x:origins[i],y:1.08},target:{x:origins[i],y:1.08}});
  for(let n=0;n<30;n++)for(const c of controllers.values())c.step(1/60);cases.push({origins,stage:'opening',clearance:auditMeshClearance(scene,controllers)});
  for(let n=0;n<60;n++)for(const c of controllers.values())c.step(1/60);cases.push({origins,stage:'departure',clearance:auditMeshClearance(scene,controllers)});
  for(let n=0;n<1800;n++){for(const c of controllers.values())c.step(1/60);if([...controllers.values()].every(c=>c.inspect().metrics.complete))break}
  cases.push({origins,stage:'complete',complete:[...controllers.values()].every(c=>c.inspect().metrics.complete)});
 }
 const minimum=Math.min(...cases.flatMap(c=>(c.clearance||[]).map(m=>m.triangle_clearance))),context=document.createElement('canvas').getContext('webgl2'),extension=context?.getExtension('WEBGL_debug_renderer_info');
 native({event:'probe',PASS:minimum>=0&&cases.filter(c=>c.stage==='held').every(c=>c.phases.every(p=>p==='examine'))&&cases.filter(c=>c.stage==='complete').every(c=>c.complete),trajectory_cases:cases,minimum_exact_mesh_clearance:minimum,scope:'Eight actual rig/origin cases,32 sampled posed-mesh states; not a universal continuous surface proof',renderer:extension?context.getParameter(extension.UNMASKED_RENDERER_WEBGL):null});window.pgnAuditSuspend();
};
let dragging=false,home={theta:0,phi:84*Math.PI/180,radius:13};actor.addEventListener('pointerdown',()=>{dragging=true});for(const e of['pointerup','pointercancel','lostpointercapture'])actor.addEventListener(e,()=>{dragging=false});
actor.addEventListener('camera-change',()=>names());
function tick(now){let dt=Math.min(.06,(now-last)/1000||0);last=now;
 if(ready&&shown&&!document.hidden){dt=Math.min(.1,(now-lastRender)/1000);lastRender=now;
  if(preview&&now-window.__auditNetwork.previewStarted>14000){for(const c of controllers.values()){const j=c.inspect().job;if(j&&!['passed','failed'].includes(j.state))c.apply({...j,state:'passed'},points(j))}}
  for(const c of controllers.values()){let rest=dt;while(rest>1e-6){const step=Math.min(rest,1/60);c.step(step);rest-=step}const state=c.inspect();if(state.metrics.complete&&canCompleteLanding(state.job,window.pgnAuditLandingMarker?.(state.job),state.age,preview))c.clear()}
  if(!dragging){const orbit=actor.getCameraOrbit(),k=1-Math.exp(-dt*4),theta=orbit.theta+(home.theta-orbit.theta)*k,phi=orbit.phi+(home.phi-orbit.phi)*k;actor.cameraOrbit=`${theta}rad ${phi}rad ${home.radius}m`}
  names();
 }requestAnimationFrame(tick)}requestAnimationFrame(tick);
actor.addEventListener('load',()=>initialize().catch(e=>native({event:'error',message:String(e)})));if(actor.loaded)initialize().catch(e=>native({event:'error',message:String(e)}));

window.pgnProjectedActorBounds=()=>{const camera=scene.getCamera();return entities.map(entity=>{const root=scene.getObjectByName("Actor"+entity[0].toUpperCase()+entity.slice(1)),b=meshBounds(root),p=root.position.clone(),xy=[];for(const x of[b.min.x,b.max.x])for(const y of[b.min.y,b.max.y])for(const z of[b.min.z,b.max.z]){const v=p.clone().set(x,y,z).project(camera);xy.push({x:(v.x+1)*actor.clientWidth/2,y:(1-v.y)*actor.clientHeight/2})}return{entity,left:Math.min(...xy.map(v=>v.x)),right:Math.max(...xy.map(v=>v.x)),top:Math.min(...xy.map(v=>v.y)),bottom:Math.max(...xy.map(v=>v.y))}})};
