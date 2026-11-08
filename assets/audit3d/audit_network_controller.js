// Actual display controllers: one independent skeleton/packet pair per entity.
// Presentation capsules are conservative approximations, not exact mesh collision proofs.
window.pgnCreateAuditController=function(scene,entity,prototype){
 const clamp=(x,a,b)=>Math.max(a,Math.min(b,x)),smooth=x=>{x=clamp(x,0,1);return x*x*(3-2*x)};
 const packet=prototype.clone(true);packet.name='LiveAuditPacket-'+entity;prototype.parent.add(packet);packet.visible=false;packet.scale.setScalar(1);packet.updateWorldMatrix(true,true);
 let packetRadius=0;const packetCenter=packet.getWorldPosition(packet.position.clone()),orbitalBound=Math.hypot(.026,.021,.024);
 packet.traverse(n=>{if(!n.isMesh)return;n.geometry.computeBoundingSphere();const bound=n.geometry.boundingSphere,scale=n.getWorldScale(n.position.clone()),maximum=Math.max(scale.x,scale.y,scale.z),offset=n.getWorldPosition(n.position.clone()).distanceTo(packetCenter);packetRadius=Math.max(packetRadius,offset+(bound.center.length()+bound.radius+(n.name.startsWith('ObservedGap')?orbitalBound:0))*maximum)});
 if(!Number.isFinite(packetRadius)||packetRadius<=0||packetRadius>.5)throw Error('Invalid constellation geometry bound');
 let rig,obstacles=[],head,face,hips,spine,arms=[],base=[],dots=[],arcs=[],up,front,side,hold,selection=.5,endpoints={},torsoProfile,paletteMode='live';
 let state={phase:'idle',age:0,elapsed:0,paused:true,u:0,opening:0,blocked:0,pathStart:null},metrics={transitions:[],complete:false,maxLinkError:0},networkMode=true,terminal=false,job=null;

 function palmCenter(a){return world(a.hand).addScaledVector(a.finger.clone().applyQuaternion(worldQ(a.hand)),a.palm)}
 function captureTorso(){
  const points=[],origin=world(hips);rig.traverse(mesh=>{if(!mesh.isSkinnedMesh)return;const attributes=mesh.geometry.attributes,indices=attributes.skinIndex,weights=attributes.skinWeight;
   for(let i=0;i<indices.count;i++){let weight=0;for(const part of ['getX','getY','getZ','getW'])if(/(?:Hips|Spine[0-9]*)$/.test(mesh.skeleton.bones[indices[part](i)].name))weight+=weights[part](i);if(weight<.5)continue;
    const p=mesh.getVertexPosition(i,v());mesh.localToWorld(p);p.sub(origin);points.push([p.dot(side),p.y,p.dot(front)]);
   }
  });if(!points.length)throw Error('Torso envelope unavailable '+entity);
  let low=Infinity,high=-Infinity;for(const p of points){low=Math.min(low,p[1]);high=Math.max(high,p[1])}
  const bands=Array.from({length:6},()=>({min:[Infinity,Infinity,Infinity],max:[-Infinity,-Infinity,-Infinity]}));
  for(const p of points){const box=bands[Math.min(5,Math.floor((p[1]-low)/(high-low)*6))];for(let axis=0;axis<3;axis++){box.min[axis]=Math.min(box.min[axis],p[axis]);box.max[axis]=Math.max(box.max[axis],p[axis])}}
  torsoProfile={hips,side:side.clone(),front:front.clone(),bands:bands.filter(b=>Number.isFinite(b.min[0])),margin:.07,vertices:points.length};
  if(!scene.userData.auditTorsoProfiles)scene.userData.auditTorsoProfiles=new Map();scene.userData.auditTorsoProfiles.set(entity,torsoProfile);
 }
 function torsoSupport(y){let maximum=-Infinity;for(const b of torsoProfile.bands)if(b.min[1]-.07<=y+packetRadius+.07&&b.max[1]+.07>=y-packetRadius-.07)maximum=Math.max(maximum,b.max[2]);return Number.isFinite(maximum)?maximum:-Infinity}
 function torsoClearance(p,profile){const relative=p.clone().sub(world(profile.hips)),coords=[relative.dot(profile.side),relative.y,relative.dot(profile.front)];let best=Infinity;
  for(const box of profile.bands){let squared=0,inside=Infinity;for(let axis=0;axis<3;axis++){const lo=box.min[axis]-profile.margin,hi=box.max[axis]+profile.margin,outside=Math.max(lo-coords[axis],0,coords[axis]-hi);squared+=outside*outside;inside=Math.min(inside,coords[axis]-lo,hi-coords[axis])}best=Math.min(best,squared?Math.sqrt(squared)-packetRadius:-inside-packetRadius)}return best;
 }
 function v(x=0,y=0,z=0){return packet.position.clone().set(x,y,z)}
 function world(node){return node.getWorldPosition(v())}
 function worldQ(node){return node.getWorldQuaternion(node.quaternion.clone())}
 function writeWorldRotation(node,q,dt,speed=3){const parent=worldQ(node.parent);const local=parent.invert().multiply(q);node.quaternion.rotateTowards(local,speed*dt);node.updateWorldMatrix(false,true)}
 function aimSegment(node,child,target,dt){const origin=world(node),from=world(child).sub(origin).normalize(),to=target.clone().sub(origin).normalize();const delta=node.quaternion.clone().setFromUnitVectors(from,to);writeWorldRotation(node,delta.multiply(worldQ(node)),dt)}
 function transition(phase){state.phase=phase;state.age=0;state.u=0;state.blocked=0;state.pathStart=packet?world(packet):null;metrics?.transitions.push(phase);if(phase==='open')state.releaseBaseline=arms.map(a=>({hand:world(a.hand),radius:palmCenter(a).distanceTo(hold)}))}
 function layout(){}
 function anchor(id){
  const camera=scene.getCamera();camera.updateWorldMatrix(true,false);
  const position=endpoints[id==='#originReport'?'source':'target']||{x:selection,y:1.08};
  const a=camera.getWorldPosition(v()),d=v(position.x*2-1,1-position.y*2,.5).unproject(camera).sub(a).normalize(),normal=camera.getWorldDirection(v());
  return a.addScaledVector(d,hold.clone().sub(a).dot(normal)/d.dot(normal));
 }
 function place(p){packet.position.copy(packet.parent.worldToLocal(p.clone()));packet.updateWorldMatrix(true,true)}
 function distance(p,a,b){const d=b.clone().sub(a),den=d.lengthSq();return p.distanceTo(a.clone().addScaledVector(d,den?clamp(p.clone().sub(a).dot(d)/den,0,1):0))}
 function volumes(){
  const out=[];if(hips&&head)out.push({a:world(hips),b:world(head).addScaledVector(up,-.2),r:side.scaleWidth*.28,label:'torso'});
  for(const a of arms){const shoulder=world(a.upper),elbow=world(a.lower),wrist=world(a.hand),f=a.finger.clone().applyQuaternion(worldQ(a.hand));
   out.push({a:shoulder,b:elbow,r:a.l1*.13,label:a.name+' upper arm'},{a:elbow,b:wrist,r:a.l2*.15,label:a.name+' forearm'},{a:wrist.clone(),b:wrist.clone().addScaledVector(f,a.palm*.2+a.handLength),r:a.handRadius,label:a.name+' hand'});
  }
  for(const rig of obstacles){
   const hip=world(rig.hips),top=world(rig.head).addScaledVector(up,-.2);out.push({a:hip,b:top,r:rig.width*.28,label:rig.name+' torso'});
   for(const limb of rig.limbs){const a=world(limb.a),b=world(limb.b);out.push({a,b,r:limb.radius,label:rig.name+' '+limb.label})}
  }return out;
 }
 function clearance(p,items=volumes()){let value=Infinity;for(const c of items)value=Math.min(value,distance(p,c.a,c.b)-c.r-packetRadius);for(const profile of scene.userData.auditTorsoProfiles?.values()||[])value=Math.min(value,torsoClearance(p,profile));return value}
 function sweepClear(from,to){const items=volumes(),length=from.distanceTo(to),steps=Math.max(2,Math.ceil(length/.025));let result=Infinity;for(let i=0;i<=steps;i++)result=Math.min(result,clearance(from.clone().lerp(to,i/steps),items));return result-length/steps/2}
 function curve(from,to,u,bulge){
  // Keep transit in the free plane in front of all actors. The terminal
  // segment still follows the receiving actor's own hand-facing normal.
  const outward=scene.getCamera().getWorldDirection(v()).negate(),reverse=state.phase==='depart';
  const margin=packetRadius+.12;let plane=Math.max(from.dot(outward),to.dot(outward));
  for(const c of volumes())plane=Math.max(plane,c.a.dot(outward)+c.r+margin,c.b.dot(outward)+c.r+margin);
  // A depth-only leg must follow the endpoint's perspective ray. A parallel
  // camera-forward leg changes its screen position and produces a visible
  // overshoot/return before the packet reaches its Atlas point.
  const cameraPosition=scene.getCamera().getWorldPosition(v());
  const firstDirection=reverse?front:cameraPosition.clone().sub(from).normalize();
  const lastDirection=reverse?cameraPosition.clone().sub(to).normalize():front;
  const a=from.clone().addScaledVector(firstDirection,Math.max(0,plane-from.dot(outward))/Math.max(.2,firstDirection.dot(outward)));
  const b=to.clone().addScaledVector(lastDirection,Math.max(0,plane-to.dot(outward))/Math.max(.2,lastDirection.dot(outward)));
  const knots=[from,a,b,to],lengths=[from.distanceTo(a),a.distanceTo(b),b.distanceTo(to)],total=lengths.reduce((x,y)=>x+y,0);state.routeLength=total;
  let distance=clamp(u,0,1)*total;
  for(let i=0;i<3;i++){if(distance<=lengths[i]||i===2){const t=lengths[i]>1e-7?clamp(distance/lengths[i],0,1):1,e=t*t*t*(10+t*(-15+6*t));return knots[i].clone().lerp(knots[i+1],e)}distance-=lengths[i]}
  return to.clone();
 }
 function solveArm(a,palm,dt,opening){
  const wantedPalm=hold.clone().sub(palm).normalize().lerp(front,opening*.7).normalize();let finger=up.clone().multiplyScalar(.85).addScaledVector(front,.35);finger.addScaledVector(wantedPalm,-finger.dot(wantedPalm));if(finger.lengthSq()<.001)finger.copy(front).addScaledVector(wantedPalm,-front.dot(wantedPalm));finger.normalize();
  const wrist=palm.clone().addScaledVector(finger,-a.palm),s=world(a.upper),delta=wrist.clone().sub(s),length=clamp(delta.length(),Math.abs(a.l1-a.l2)+.01,a.l1+a.l2-.015),axis=delta.normalize();
  wrist.copy(s).addScaledVector(axis,length);const along=(a.l1*a.l1-a.l2*a.l2+length*length)/(2*length),preferred=side.clone().multiplyScalar(a.sign*.5).addScaledVector(up,-.8).addScaledVector(front,.1);preferred.addScaledVector(axis,-preferred.dot(axis)).normalize();
  const elbow=s.clone().addScaledVector(axis,along).addScaledVector(preferred,Math.sqrt(Math.max(0,a.l1*a.l1-along*along)));
  aimSegment(a.upper,a.lower,elbow,dt);aimSegment(a.lower,a.hand,wrist,dt);
  const wantedFinger=finger.clone();
  const reference=worldQ(a.hand),actualFinger=a.finger.clone().applyQuaternion(reference),q=a.hand.quaternion.clone().setFromUnitVectors(actualFinger,wantedFinger);let desired=q.multiply(reference);
  const actualPalm=a.palmNormal.clone().applyQuaternion(desired),cross=actualPalm.clone().cross(wantedPalm),angle=Math.atan2(cross.dot(wantedFinger),clamp(actualPalm.dot(wantedPalm),-1,1));desired.premultiply(a.hand.quaternion.clone().setFromAxisAngle(wantedFinger,angle));writeWorldRotation(a.hand,desired,dt,3.5);
  const currentPalm=world(a.hand).addScaledVector(a.finger.clone().applyQuaternion(worldQ(a.hand)),a.palm);
  const linkError=Math.max(Math.abs(world(a.upper).distanceTo(world(a.lower))/a.l1-1),Math.abs(world(a.lower).distanceTo(world(a.hand))/a.l2-1));metrics.maxLinkError=Math.max(metrics.maxLinkError,linkError);return currentPalm.distanceTo(palm);
 }
 function gaze(target,dt){if(!head||!face)return;const parent=worldQ(head.parent),reference=parent.clone().multiply(head.userData.rest),from=head.userData.forward.clone().applyQuaternion(reference),wanted=target.clone().sub(world(head)).normalize(),turn=head.quaternion.clone().setFromUnitVectors(from,wanted),angle=turn.angleTo(turn.clone().identity());if(angle>.68)turn.slerp(turn.clone().identity(),1-.68/angle);writeWorldRotation(head,turn.multiply(reference),dt,({michael:1.7,helel:3.5,boshel:3,kitabel:2.7}[entity]))}
 function reset(position=selection){
  if(!packet)return;selection=position;layout();for(const b of base){b.node.quaternion.copy(b.q);b.node.position.copy(b.p);b.node.scale.copy(b.s)}scene.updateMatrixWorld(true);
  metrics={maxLinkError:0,minFlightClearance:Infinity,maxCaptureError:0,deniedSteps:0,transitions:['summon'],complete:false};state={phase:'summon',age:0,elapsed:0,paused:false,u:0,opening:0,blocked:0,bulge:.3,pathStart:null};place(anchor('#originReport'));packet.scale.setScalar(0);scene.queueRender();
 }
 function step(dt){
  if(!packet)return;if(state.paused){if(!job)idle(dt);return;}state.age+=dt;state.elapsed+=dt;
  const phase=state.phase,breath=Math.sin(state.elapsed*1.1)*.012;
  for(const b of [hips,spine])if(b){const target=b.userData.rest.clone().multiply(b.quaternion.clone().setFromAxisAngle(v(0,0,1),breath*(b===hips?.5:1)));b.quaternion.rotateTowards(target,dt*.3);b.updateWorldMatrix(false,true)}
  if(spine){const engaged=['capture','examine','open'].includes(phase)?1:phase==='approach'?smooth((.9-world(packet).distanceTo(hold))/.7):0,lean=({michael:.018,helel:.025,boshel:.055,kitabel:.035}[entity])*engaged;
   const desired=worldQ(spine.parent).multiply(spine.userData.rest),delta=spine.quaternion.clone().setFromAxisAngle(side,lean);writeWorldRotation(spine,delta.multiply(desired),dt,.6);
  }
  const shoulders=world(arms[0].upper).add(world(arms[1].upper)).multiplyScalar(.5),reach=Math.min(...arms.map(a=>a.l1+a.l2)),drop=entity==='boshel'?-Math.min(.20,reach*.40):Math.min(.30,reach*.30);
  const bodyLow=world(hips),bodyHigh=world(head).addScaledVector(up,-.2),body=bodyLow.clone().lerp(bodyHigh,clamp((shoulders.y-drop-bodyLow.y)/(bodyHigh.y-bodyLow.y),0,1));
  const bodyClearance=Math.max(body.sub(shoulders).dot(front)+side.scaleWidth*.28+packetRadius+.055,torsoSupport(shoulders.y-drop-world(hips).y)+packetRadius+.11-shoulders.clone().sub(world(hips)).dot(front));
  hold.copy(shoulders).addScaledVector(front,Math.max(bodyClearance,Math.min(.48,reach*.5))).addScaledVector(up,-drop);
  if(phase==='examine'){
   // A closed, eased inspection path: the hands guide the same centre the
   // gaze examines. No independent object loop or snap at the release.
   const t=networkMode?((state.age%6)/6):clamp(state.age/({michael:3.4,helel:2.4,boshel:2.6,kitabel:2.8}[entity]),0,1),envelope=Math.sin(Math.PI*t)**2,turn=selection<.5?-1:1;
   hold.addScaledVector(side,turn*.065*Math.sin(t*Math.PI*2)*envelope).addScaledVector(up,.04*Math.sin(t*Math.PI)*envelope).addScaledVector(front,.025*envelope);
  }
  let center=world(packet),distanceToHold=center.distanceTo(hold),approach=phase==='summon'?0:phase==='approach'?smooth((.85-distanceToHold)/.65):phase==='depart'?1-smooth((distanceToHold-.2)/.8):phase==='complete'?0:1;
  const opening=phase==='approach'?1:phase==='capture'?1-smooth(state.age/.55):phase==='open'?smooth(state.age/.8):phase==='depart'?1:0;state.opening=opening;
  let maxError=0;
  const rotations=arms.map(a=>[a.upper.quaternion.clone(),a.lower.quaternion.clone(),a.hand.quaternion.clone()]);
  for(const a of arms){
   if(approach<.001){for(const bone of [a.upper,a.lower,a.hand])bone.quaternion.rotateTowards(bone.userData.rest,dt*2);a.upper.updateWorldMatrix(false,true);continue}
   const spacing=packetRadius+a.handRadius+.055+opening*Math.min(.10,reach*.10);
   const below=entity==='michael'?(a.sign>0?-.52:-.62):entity==='helel'?(a.sign>0?.08:-.40):entity==='boshel'?-1.25:0;
   const direction=side.clone().multiplyScalar(a.sign).addScaledVector(up,below*(1-opening*.65)).normalize(),palm=hold.clone().addScaledVector(direction,spacing),desired=a.restPalm.clone().lerp(palm,approach);maxError=Math.max(maxError,solveArm(a,desired,dt,opening));
  }
  // Moving collision volumes must also respect the waiting report.
  if(packet.scale.x>.01&&clearance(center)<.018){for(let i=0;i<arms.length;i++){const a=arms[i];[a.upper,a.lower,a.hand].forEach((bone,j)=>bone.quaternion.copy(rotations[i][j]));a.upper.updateWorldMatrix(false,true)}metrics.deniedArmSteps=(metrics.deniedArmSteps||0)+1}
  if(phase==='summon'&&state.age>.35){transition('approach');state.pathStart=center.clone()}
  else if(phase==='approach'||phase==='depart'){
   const target=phase==='approach'?hold:anchor('#returnedReport'),start=state.pathStart||center,nextU=Math.min(1,state.u+dt/Math.max(.9,(state.routeLength||start.distanceTo(target))/3)),candidate=curve(start,target,nextU,state.bulge||.3),clear=sweepClear(center,candidate);
   metrics.lastClearance=clear;
   if(clear>=.018){place(candidate);state.u=nextU;state.blocked=0;metrics.minFlightClearance=Math.min(metrics.minFlightClearance,clear)}
   else{state.blocked+=dt;metrics.deniedSteps++;if(state.blocked>.5){state.pathStart=center.clone();state.u=0;state.bulge=Math.min(1.8,(state.bulge||.3)+.1);state.blocked=0}}
   if(phase==='approach'&&world(packet).distanceTo(hold)<.035){transition('capture')}
   if(phase==='depart'&&state.u>=1){transition('complete');packet.scale.setScalar(0);metrics.complete=true;}
  }else if(phase==='capture'){
   if(maxError<.045&&sweepClear(center,hold)>.018){place(hold);metrics.maxCaptureError=maxError;transition('examine')}
  }else if(phase==='examine'){
   if(clearance(hold)>.018)place(hold);
   if(networkMode?(terminal&&state.age>.35):state.age>({michael:3.4,helel:2.4,boshel:2.6,kitabel:2.8}[entity]))transition('open');
  }else if(phase==='open'){
   if(clearance(hold)>.018)place(hold);
   // Report departure is forbidden until the actual opened hands clear it.
   if(opening>.98&&state.releaseBaseline?.some((before,i)=>world(arms[i].hand).distanceTo(before.hand)>.03)&&clearance(hold)>=.04-1e-6){transition('depart');state.pathStart=world(packet);state.bulge=.35}
  }
  metrics.currentPalmError=maxError;
  center=world(packet);let look=state.phase==='summon'?anchor('#originReport'):center;
  if(state.phase==='examine'&&dots.length){const choice=Math.min(dots.length-1,Math.floor((state.age%6)/({michael:.9,helel:.48,boshel:.5,kitabel:.65}[entity])));look=world(dots[choice].node)}gaze(look,dt);
  for(let i=0;i<dots.length;i++){const d=dots[i],t=state.elapsed;d.node.position.copy(d.rest).add(v(Math.sin(t*(.75+i*.11)+i*2.4)*.026,Math.cos(t*(.61+i*.09)+i)*.021,Math.sin(t*(.89+i*.07)+i*.7)*.024))}
  for(let i=0;i<arcs.length;i++){arcs[i].rotation.x=state.elapsed*.12+i*.4;arcs[i].rotation.y=state.elapsed*.08}
  const arriving=state.phase==='approach'||['capture','examine','open','depart'].includes(state.phase);packet.visible=arriving;if(arriving)packet.scale.setScalar(Math.min(1,packet.scale.x+dt*3));

  scene.queueRender();
 }
 function find(name){let match=rig?.getObjectByName(entity+"::"+name)||rig?.getObjectByName(name);if(!match&&rig)rig.traverse(n=>{if(n.isBone&&n.name.endsWith(name))match=n});return match||scene.getObjectByName(name)}
   rig=scene.getObjectByName('Actor'+entity[0].toUpperCase()+entity.slice(1));arms=[];dots=[];arcs=[];head=find('Head');face=find('headfront');hips=find('Hips');spine=find('Spine');up=v(0,1,0);scene.updateMatrixWorld(true);
   const all=[];rig.traverse(n=>{if(n.isBone)all.push(n)});base=all.map(node=>({node,q:node.quaternion.clone(),p:node.position.clone(),s:node.scale.clone()}));for(const b of base)b.node.userData.rest=b.q.clone();scene.updateMatrixWorld(true);
   front=world(face).sub(world(head));front.y=0;front.normalize();side=world(find('LeftArm')).sub(world(find('RightArm')));const width=side.length();side.normalize();side.scaleWidth=width;
   arms=['Left','Right'].map((name,i)=>{const upper=find(name+'Arm'),lower=find(name+'ForeArm'),hand=find(name+'Hand'),s=world(upper),e=world(lower),w=world(hand),l1=s.distanceTo(e),l2=e.distanceTo(w),inverse=worldQ(hand).invert(),finger=w.clone().sub(e).normalize().applyQuaternion(inverse);return {name,upper,lower,hand,l1,l2,finger,palmNormal:front.clone().applyQuaternion(inverse),palm:l2*.24,handLength:l2*.45,handRadius:l2*.18,sign:i===0?1:-1,restPalm:w.clone().addScaledVector(w.clone().sub(e).normalize(),l2*.24)}});
   head.userData.forward=world(face).sub(world(head)).normalize().applyQuaternion(worldQ(head).invert());hold=world(arms[0].upper).add(world(arms[1].upper)).multiplyScalar(.5).addScaledVector(front,.48).addScaledVector(up,-.36);
   obstacles=[];
   for(const name of ['michael','helel','boshel','kitabel']){
    if(name===entity)continue;const root=scene.getObjectByName('Actor'+name[0].toUpperCase()+name.slice(1));if(!root)continue;
    const bone=label=>{let result;root.traverse(n=>{if(n.isBone&&n.name.endsWith(label))result=n});return result},hips=bone('Hips'),head=bone('Head'),left=bone('LeftArm'),right=bone('RightArm');
    if(!hips||!head||!left||!right)continue;const obstacle={name,hips,head,width:world(left).distanceTo(world(right)),limbs:[]};
    for(const prefix of ['Left','Right'])for(const [from,to,label,factor]of [['Arm','ForeArm','upper arm',.13],['ForeArm','Hand','forearm',.15],['UpLeg','Leg','thigh',.13],['Leg','Foot','shin',.11]]){const a=bone(prefix+from),b=bone(prefix+to);if(a&&b)obstacle.limbs.push({a,b,label:prefix+' '+label,radius:world(a).distanceTo(world(b))*factor})}
    obstacles.push(obstacle);
   }
   packet.traverse(n=>{if(n.name.startsWith('ObservedGap'))dots.push({node:n,rest:n.position.clone()});if(n.name.startsWith('ConstellationOrbit'))arcs.push(n)});

 captureTorso();
 let idleTime=0;
 function idle(dt){idleTime+=dt;const phase={michael:.2,helel:1.4,boshel:2.7,kitabel:4}[entity],rate={michael:.65,helel:.9,boshel:1.2,kitabel:.8}[entity];for(const bone of [hips,spine,head])if(bone){const rest=bone.userData.rest,angle=Math.sin(idleTime*rate+phase)*(bone===head?.009:.004);bone.quaternion.copy(rest).multiply(bone.quaternion.clone().setFromAxisAngle(v(0,0,1),angle));bone.updateWorldMatrix(false,true)}scene.queueRender()}
 function clear(){job=null;terminal=false;state.paused=true;state.phase='idle';packet.visible=false;packet.scale.setScalar(0);for(const b of base){b.node.quaternion.copy(b.q);b.node.position.copy(b.p);b.node.scale.copy(b.s)}scene.updateMatrixWorld(true);scene.queueRender()}
 const pigments=[];packet.traverse(n=>{if(!n.isMesh||!n.name.startsWith('ObservedGap'))return;const source=Array.isArray(n.material)?n.material:[n.material],materials=source.map(m=>{const copy=m.clone();pigments.push({material:copy,color:copy.color?.clone(),emissive:copy.emissive?.clone()});return copy});n.material=Array.isArray(n.material)?materials:materials[0]});
 function palette(mode){paletteMode=mode;for(const p of pigments){if(mode==='preview'){if(p.color)p.material.color.copy(p.color);if(p.emissive)p.material.emissive.copy(p.emissive)}else{const color=job?.state==='passed'?'#62d9bb':job?.state==='failed'||job?.expired?'#e88388':'#7bbdd0';p.material.color?.set(color);p.material.emissive?.set(color)}}scene.queueRender()}
 function apply(value,points={},rebase=false){
  endpoints=points;const identity=value.request+':'+value.slot+':'+value.attempt+':'+value.actor,changed=!job||job.identity!==identity;
  if(changed){clear();terminal=false;reset(points.source?.x??.5);job={...value,identity};if(rebase){for(let n=0;n<1200&&state.phase!=='examine';n++)step(1/60);if(state.phase!=='examine')clear()}}
  if(job){job={...value,identity};terminal=value.expired||value.state==='passed'||value.state==='failed';palette(paletteMode)}
 }
 function relocate(delta){const d=v(...delta);for(const a of arms)a.restPalm.add(d);hold.add(d);if(['capture','examine','open'].includes(state.phase))place(world(packet).add(d));else if(['approach','depart'].includes(state.phase)){state.pathStart=world(packet);state.u=0}}
 return {entity,apply,clear,step,relocate,palette,pause:()=>{state.paused=true},resume:()=>{if(job)state.paused=false},setEndpoints:value=>{endpoints=value},inspect:()=>({entity,job,endpoints,packetRadius,phase:state.phase,age:state.age,elapsed:state.elapsed,metrics:JSON.parse(JSON.stringify(metrics)),hold:hold.toArray(),packet:world(packet).toArray(),clearance:clearance(world(packet)),nearest:volumes().map(c=>({label:c.label,clearance:distance(world(packet),c.a,c.b)-c.r-packetRadius})).sort((a,b)=>a.clearance-b.clearance).slice(0,2),visible:packet.visible})};
};
