import * as C from './vendor/cannon.js';
import * as T from './vendor/three.js';

const v=(x=0,y=0,z=0)=>new C.Vec3(x,y,z);
const rotation=(x=0,y=0,z=0)=>new C.Quaternion().setFromEuler(x,y,z);
export const clips=[['Walk cycle','Locomotion',1.6],['Arm flex','Articulation',2.4],['Run cycle','Locomotion',1.1],['Idle breath','Idle',3.6],['Overhead reach','Gesture',2.8],['Deep squat','Exercise',3],['Hand wave','Gesture',2],['Side step','Locomotion',1.8]];
const floorY=0;
// Approximate 75 kg adult. Forearms include hands; trunk includes pelvis.
// Each drive specifies stiffness (N·m/rad), damping (N·m·s/rad), and peak torque (N·m).
const drives={
 trunk:   {mass:37.5, stiffness:740, damping:54, peak:300},
 head:    {mass:6,    stiffness:60,  damping:3,  peak:30},
 thigh:   {mass:7.5,  stiffness:1250, damping:36, peak:220},
 shin:    {mass:3.5,  stiffness:330, damping:13, peak:180},
 foot:    {mass:1.1,  stiffness:155, damping:5,  peak:125},
 arm:     {mass:2.1,  stiffness:190, damping:8,  peak:90},
 forearm: {mass:1.6,  stiffness:110, damping:4,  peak:65}
};
export function buildPhysics(){
 const world=new C.World({gravity:v(0,-9.81,0)});
 world.solver.iterations=22;
 world.defaultContactMaterial.friction=.8;
 world.defaultContactMaterial.restitution=0;
 const ground=new C.Body({mass:0,shape:new C.Plane(),position:v(0,floorY,0)});
 ground.quaternion.setFromEuler(-Math.PI/2,0,0);world.addBody(ground);
 const parts=[],byName={};
 function part(name,parent,anchor,length,radius,box,hinge=false){
  const drive=drives[name.replace(/-?\d+$/,'')];
  const body=new C.Body({mass:drive.mass,linearDamping:.06,angularDamping:.13});
  const offset=name==='head'?v(0,-length/2,0):box?v(0,box[1],-.065):v(0,length/2,0);
  if(box)body.addShape(new C.Box(v(...box)));
  else{
   const span=Math.max(.005,length-2*radius);
   body.addShape(new C.Cylinder(radius,radius,span,12));
   for(const sign of [-1,1])body.addShape(new C.Sphere(radius),v(0,sign*span/2,0));
  }
  world.addBody(body);
  const p={name,drive,body,parent:byName[parent],anchor,offset,length,radius,box,target:rotation(),targetPos:v()};
  if(p.parent){
   const options={pivotA:anchor,pivotB:offset,collideConnected:false,maxForce:2e4};
   const Joint=hinge?C.HingeConstraint:C.ConeTwistConstraint;
   Object.assign(options,hinge?{axisA:v(1),axisB:v(1)}:{axisA:v(0,-1),axisB:v(0,-1),angle:1.65,twistAngle:drive===drives.thigh?1.7:.85});
   const joint=drive===drives.arm?new C.PointToPointConstraint(p.parent.body,anchor,body,offset,2e4):new Joint(p.parent.body,body,options);
   joint.collideConnected=false;world.addConstraint(joint);
  }
  parts.push(p);byName[name]=p;return p;
 }
 part('trunk',null,v(),.58,.145);
 part('head','trunk',v(0,.29,0),.24,.105);
 for(const side of [-1,1]){
  part('thigh'+side,'trunk',v(side*.115,-.24,0),.425,.094);
  part('shin'+side,'thigh'+side,v(0,-.2125,0),.415,.071,null,true);
  part('foot'+side,'shin'+side,v(0,-.2075,0),.12,.06,[.067,.053,.132]);
  part('arm'+side,'trunk',v(side*.205,.21,0),.31,.065);
  part('forearm'+side,'arm'+side,v(0,-.155,0),.30,.051,null,true);
 }

 function sample(k,t){
  const p=t*2*Math.PI,s=Math.sin(p),u=(1-Math.cos(p))/2,walk=k===0||k===2,run=k===2,squat=k===5?u:0;
  const root=parts[0];
  root.targetPos.set(k===7?.15*s:0,(walk ? 1.105 + (run ? .018 : .021)*Math.cos(2*p) : 1.18) - .26*squat + (walk ? 0 : .005*s),0);
  root.target=rotation(.09*squat+(run?.09:0),walk?.075*s:0,walk?.025*Math.cos(p):0);
  for(const b of parts.slice(1)){
   const side=b.name.endsWith('-1')?-1:1,sw=s*side,raised=side===1;
   let x=0,z=0;
   // Solve the sagittal leg against a continuous ankle path. The swing
   // foot clears the floor, while the stance ankle travels back at floor height.
   if(walk && (b.name.startsWith('thigh') || b.name.startsWith('shin') || b.box)){
    const swing=Math.max(0,Math.cos(p)*side);
    const ankleZ=(run ? .26 : .20)*sw;
    const ankleY=.106+(run ? .14 : .09)*swing*swing;
    const drop=root.targetPos.y-.24-ankleY;
    const upper=.425,lower=.415;
    const cosine=(drop*drop+ankleZ*ankleZ-upper*upper-lower*lower)/(2*upper*lower);
    const knee=Math.acos(Math.max(-1,Math.min(1,cosine)));
    const hip=-Math.atan2(ankleZ,drop)-Math.atan2(lower*Math.sin(knee),upper+lower*Math.cos(knee));
    if(b.name.startsWith('thigh')) x=hip-(run ? .09 : 0);
    else if(b.name.startsWith('shin')) x=knee;
    else x=-hip-knee-.12*swing;
   }else if(b.name.startsWith('thigh')){
    x=-.82*squat;
    if(k===7){z=-.13*s;x-=.19*Math.max(0,side*Math.cos(p))}
   }else if(b.name.startsWith('shin')){
    x=1.55*squat+.035+(k===7 ? .44*Math.max(0,side*Math.cos(p))**2 : 0);
   }else if(b.box)x=-.72*squat;
   if(b.name.startsWith('arm')){
    x=walk ? (run ? .64 : .38)*sw : -.15-.70*squat;
    z=side*(.09+(raised&&k===4 ? 2.70*u : raised&&k===6 ? 1.66+.13*Math.sin(2*p) : 0));
   }
   if(b.name.startsWith('forearm'))x=k===1 ? -.12-2.2*u : k===6&&raised ? -1.32-.38*Math.sin(2*p-.35) : run ? -1.18-.18*sw : -.28;
   if(b.name==='head')x=run ? -.07 : k===4 ? -.13*u : -.04*squat;
   b.target=b.parent.target.mult(rotation(x,0,z));
   b.targetPos=b.parent.targetPos.vadd(b.parent.target.vmult(b.anchor)).vsub(b.target.vmult(b.offset));
  }
 }
 // Keep every contributing clip moving through an interrupted crossfade.
 let layers=[],incoming,selected=-1,elapsed=0;
 function updateTargets(k,t,dt){
  if(k!==selected){
   for(const layer of layers)layer.start=layer.weight;
   layers=layers.filter(layer=>layer.weight>.00001);
   incoming={clip:k,weight:0,start:0,phase:t};layers.push(incoming);
   selected=k;elapsed=0;
  }
  elapsed=Math.min(.38,elapsed+dt);
  const u=elapsed/.38,fade=u*u*(3-2*u);
  if(elapsed===.38)layers=[incoming];
  let accumulated=0;
  const rootPosition=v();
  for(const layer of layers){
   layer.phase=layer===incoming?t:(layer.phase+dt/clips[layer.clip][2])%1;
   layer.weight=layer.start*(1-fade)+(layer===incoming?fade:0);
   if(!layer.weight)continue;
   sample(layer.clip,layer.phase);
   accumulated+=layer.weight;
   rootPosition.addScaledVector(layer.weight,parts[0].targetPos,rootPosition);
   for(const b of parts){
    const local=b.parent?b.parent.target.inverse().mult(b.target):b.target;
    if(accumulated===layer.weight)b.mixed=local.clone();
    else b.mixed.slerp(local,layer.weight/accumulated,b.mixed);
   }
  }
  parts[0].targetPos.copy(rootPosition);
  for(const b of parts){
   b.target=b.parent?b.parent.target.mult(b.mixed):b.mixed.clone();
   if(b.parent)b.targetPos=b.parent.targetPos.vadd(b.parent.target.vmult(b.anchor)).vsub(b.target.vmult(b.offset));
   const delta=b.mixed.mult(b.previousGoal.inverse());
   b.rate=v(delta.x,delta.y,delta.z).scale(2*Math.sign(delta.w)/dt);
   b.previousGoal.copy(b.mixed);
  }
 }
 function pose(k,t){
  sample(k,t);selected=k;elapsed=.38;incoming={clip:k,start:1,weight:1,phase:t};layers=[incoming];
  for(const b of parts){
   b.previousGoal=b.parent?b.parent.target.inverse().mult(b.target):b.target.clone();
   b.body.position.copy(b.targetPos);b.body.quaternion.copy(b.target);
   b.body.velocity.setZero();b.body.angularVelocity.setZero();b.body.aabbNeedsUpdate=true;
  }
 }
 function integrate(powered,dt){
  if(powered){
   const root=parts[0],rb=root.body;
   const touching=parts.filter(p=>p.box && world.contacts.some(c=>
    (c.bi===ground && c.bj===p.body)||(c.bj===ground && c.bi===p.body)));
   const swing=layers.reduce((sum,l)=>sum+((l.clip===0||l.clip===2)?l.weight*Math.cos(l.phase*2*Math.PI):0),0);
   const feet=touching.length===2&&Math.abs(swing)>.25
    ?touching.filter(p=>p.name===(swing>0?'foot-1':'foot1')):touching;
   for(const p of parts.filter(p=>p.box)){
    const planted=feet.includes(p);
    if(planted!==p.body.fixedRotation){
     p.body.fixedRotation=planted;p.body.updateMassProperties();
     if(planted){p.body.quaternion.set(0,0,0,1);p.body.angularVelocity.setZero()}
    }
   }
   const mass=parts.reduce((sum,p)=>sum+p.body.mass,0);
   const center=v(),velocity=v(),stance=v();
   for(const p of parts){
    center.addScaledVector(p.body.mass/mass,p.body.position,center);
    velocity.addScaledVector(p.body.mass/mass,p.body.velocity,velocity);
   }
   for(const foot of feet)stance.addScaledVector(1/feet.length,foot.body.position,stance);
   for(const foot of feet){
    // Virtual leg actuator. The sole receives the opposite force and reaction moment.
    const shift=root.targetPos.x/3;
    const push=v((stance.x+shift-center.x)*1400-velocity.x*500,
     mass*9.81+3800*(root.targetPos.y-rb.position.y)-440*rb.velocity.y,
     (stance.z-.025-center.z)*1400-velocity.z*500).scale(1/feet.length);
    push.y=Math.max(0,Math.min(1050,push.y));
    const shear=Math.hypot(push.x,push.z),limit=push.y*.65;
    if(shear>limit){push.x*=limit/shear;push.z*=limit/shear}
    rb.force.vadd(push,rb.force);foot.body.force.vsub(push,foot.body.force);
    // Balance the force couple at the planted sole.
    const moment=rb.position.vsub(foot.body.position).cross(push);
    foot.body.torque.vsub(moment,foot.body.torque);
   }
   for(const b of parts){
    if(!b.parent&&!feet.length)continue;
    const parent=b.parent,goal=parent?parent.body.quaternion.mult(parent.target.inverse().mult(b.target)):b.target;
    const error=goal.mult(b.body.quaternion.inverse()),sgn=error.w<0?-1:1;
    const {stiffness,damping,peak}=b.drive;
    const relative=b.body.angularVelocity.vsub(parent?parent.body.angularVelocity:v());
    const wanted=parent?parent.body.quaternion.vmult(b.rate):b.rate;
    const torque=v(error.x,error.y,error.z).scale(2*stiffness*sgn).vadd(wanted.vsub(relative).scale(damping));
    if(torque.length()>peak)torque.scale(peak/torque.length(),torque);
    b.body.torque.vadd(torque,b.body.torque);
    if(parent)parent.body.torque.vsub(torque,parent.body.torque);
    else for(const foot of feet)foot.body.torque.addScaledVector(-1/feet.length,torque,foot.body.torque);
   }
  }
  if(!powered)for(const p of parts)if(p.box&&p.body.fixedRotation){p.body.fixedRotation=false;p.body.updateMassProperties()}
  world.step(dt);
 }
 function step(k,t,powered,dt){
  updateTargets(k,t,dt);
  integrate(powered,dt/2);
  integrate(powered,dt/2);
 }
 pose(3,.12);return {parts,pose,step,world};
}
export function createRig(canvas){
 const rig=buildPhysics(),scene=new T.Scene(),camera=new T.OrthographicCamera();
 const renderer=new T.WebGLRenderer({canvas,antialias:true});renderer.setPixelRatio(Math.min(devicePixelRatio,2));
 scene.background=new T.Color(0xe6e5dd);scene.fog=new T.FogExp2(0xe6e5dd,.067);
 renderer.shadowMap.enabled=true;renderer.shadowMap.type=T.PCFSoftShadowMap;
 scene.add(new T.HemisphereLight(0xfff8e9,0xb5a59a,2));
 const light=new T.DirectionalLight(0xfff5e8,2.8);light.position.set(-3,6,4);light.castShadow=true;light.shadow.mapSize.set(1024,1024);light.shadow.normalBias=.009;scene.add(light);
 const floor=new T.Mesh(new T.PlaneGeometry(120,120),new T.MeshStandardMaterial({color:0xe6e5dd,polygonOffset:true,polygonOffsetFactor:1,polygonOffsetUnits:1}));
 floor.rotation.x=-Math.PI/2;floor.position.y=floorY;floor.receiveShadow=true;scene.add(floor);
 const grid=new T.GridHelper(60,160,0xa8a79e,0xbdbdb2);grid.material.transparent=true;grid.material.opacity=.30;grid.position.y=floorY;scene.add(grid);
 const skin=new T.MeshStandardMaterial({color:0xce784e,roughness:.8}),sole=new T.MeshStandardMaterial({color:0x995b40});
 const boneMat=new T.MeshBasicMaterial({color:0x394a40,depthTest:false}),jointMat=new T.MeshBasicMaterial({color:0xffdf9a,depthTest:false});
 for(const b of rig.parts){
  const geometry=b.box?new T.BoxGeometry(...b.box.map(n=>2*n)):new T.CapsuleGeometry(b.radius,Math.max(.005,b.length-2*b.radius),6,18);
  b.mesh=new T.Mesh(geometry,b.box?sole:skin);if(b.name==='trunk'){
   const profile=[[.055,-.29],[.137,-.25],[.153,-.17],[.128,-.075],[.15,.045],[.194,.17],[.175,.215],[.071,.269],[.055,.29]];
   b.mesh.geometry.dispose();
   b.mesh.geometry=new T.LatheGeometry(profile.map(([r,y])=>new T.Vector2(r,y)),32);
   b.mesh.scale.z=.72;
  }else if(b.name==='head'){b.mesh.scale.set(.89,1,.98)}
  b.mesh.castShadow=true;scene.add(b.mesh);
  b.bone=new T.Group();const rod=new T.Mesh(new T.CylinderGeometry(.012,.019,b.length,8),boneMat);rod.renderOrder=3;b.bone.add(rod);
  const joint=new T.Mesh(new T.SphereGeometry(.032,10,8),jointMat);joint.position.copy(b.offset);joint.renderOrder=4;b.bone.add(joint);scene.add(b.bone)
 }
 let width,height;
 function render(yaw,mode,w=canvas.clientWidth,h=canvas.clientHeight){
  if(width!==w||height!==h){width=w;height=h;renderer.setSize(width,height,false);
   camera.left=-1.22*width/height;camera.right=-camera.left;camera.top=1.22;camera.bottom=-1.22;camera.near=.05;camera.far=70;camera.updateProjectionMatrix()}
  const {x,z}=rig.parts[0].body.position;
  camera.position.set(x+4*Math.sin(yaw),1.94,z+4*Math.cos(yaw));camera.lookAt(x,.89,z);
  for(const b of rig.parts){for(const obj of [b.mesh,b.bone]){obj.position.copy(b.body.position);obj.quaternion.copy(b.body.quaternion)}b.mesh.visible=mode!==1;b.bone.visible=mode!==0}
  renderer.render(scene,camera)
 }
 return {...rig,render}
}
