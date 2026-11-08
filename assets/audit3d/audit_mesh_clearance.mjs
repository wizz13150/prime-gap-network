// Closed-frame QA only: exact distance to the currently skinned triangles.
// This is independent of the runtime's approximate skeleton capsules.
function segmentDistance(p,a,b){const d=b.clone().sub(a),den=d.lengthSq(),t=den?Math.max(0,Math.min(1,p.clone().sub(a).dot(d)/den)):0;return p.distanceTo(a.clone().addScaledVector(d,t))}
function triangleDistance(p,a,b,c){const x=b.clone().sub(a),y=c.clone().sub(a),z=p.clone().sub(a),xx=x.dot(x),xy=x.dot(y),yy=y.dot(y),zx=z.dot(x),zy=z.dot(y),den=xx*yy-xy*xy;
 if(den>1e-18){const u=(yy*zx-xy*zy)/den,v=(xx*zy-xy*zx)/den;if(u>=0&&v>=0&&u+v<=1){const normal=x.clone().cross(y);return Math.abs(z.dot(normal))/normal.length()}}
 return Math.min(segmentDistance(p,a,b),segmentDistance(p,b,c),segmentDistance(p,c,a));
}
export function auditMeshClearance(scene,controllers){
 scene.updateMatrixWorld(true);const meshes=[];scene.traverse(mesh=>{if(!mesh.isSkinnedMesh)return;const count=mesh.geometry.attributes.position.count,vertices=new Float32Array(count*3),point=mesh.position.clone(),low=[Infinity,Infinity,Infinity],high=[-Infinity,-Infinity,-Infinity];for(let i=0;i<count;i++){mesh.getVertexPosition(i,point);mesh.localToWorld(point);vertices[i*3]=point.x;vertices[i*3+1]=point.y;vertices[i*3+2]=point.z;for(let axis=0;axis<3;axis++){const v=point.getComponent(axis);low[axis]=Math.min(low[axis],v);high[axis]=Math.max(high[axis],v)}}meshes.push({mesh,vertices,low,high,index:mesh.geometry.index?.array})});
 const result=[];
 for(const controller of controllers.values()){
  const state=controller.inspect();if(!state.visible)continue;const point=scene.position.clone().fromArray(state.packet);let distance=Infinity,nearest='',nearestBones=[],tested=0;
  const ordered=[...meshes].sort((a,b)=>Number(b.mesh.name.startsWith(state.entity))-Number(a.mesh.name.startsWith(state.entity)));
  for(const{mesh,vertices,index,low,high}of ordered){let whole=0;for(let axis=0;axis<3;axis++)whole+=Math.max(low[axis]-point.getComponent(axis),0,point.getComponent(axis)-high[axis])**2;if(whole>=distance*distance)continue;const length=index?index.length:vertices.length/3;
   for(let triangle=0;triangle<length;triangle+=3){const ia=(index?index[triangle]:triangle)*3,ib=(index?index[triangle+1]:triangle+1)*3,ic=(index?index[triangle+2]:triangle+2)*3;let box=0;
    for(let axis=0;axis<3;axis++){const lo=Math.min(vertices[ia+axis],vertices[ib+axis],vertices[ic+axis]),hi=Math.max(vertices[ia+axis],vertices[ib+axis],vertices[ic+axis]),value=point.getComponent(axis);box+=Math.max(lo-value,0,value-hi)**2}
    if(box>=distance*distance)continue;tested++;const ids=[ia,ib,ic],[a,b,c]=ids.map(i=>point.clone().set(vertices[i],vertices[i+1],vertices[i+2]));const candidate=triangleDistance(point,a,b,c);if(candidate<distance){distance=candidate;nearest=mesh.name;const weighted=new Map();for(const vertex of ids.map(i=>i/3))for(const part of ['getX','getY','getZ','getW']){const bone=mesh.skeleton.bones[mesh.geometry.attributes.skinIndex[part](vertex)].name,weight=mesh.geometry.attributes.skinWeight[part](vertex)/3;weighted.set(bone,(weighted.get(bone)||0)+weight)}nearestBones=[...weighted].sort((a,b)=>b[1]-a[1]).slice(0,4)}
   }
  }result.push({entity:state.entity,phase:state.phase,packet_radius:state.packetRadius,triangle_clearance:distance-state.packetRadius,nearest_mesh:nearest,nearest_bones:nearestBones,triangles_narrow_phase:tested});
 }return result;
}
