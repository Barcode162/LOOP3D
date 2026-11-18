import { createRig, clips } from './rig.js';
let clip=0,phase=.12,yaw=.65,active=true,mode=0,last=0;
const rig=createRig(view);
let cards=clips.map(([name,type,d],i)=>{let b=document.createElement('button');b.className='card';b.innerHTML='<div class="art"><canvas></canvas><small>A / '+String(i+1).padStart(3,'0')+'</small></div><div class="info"><b>'+name+'</b><small>'+type+' · '+d.toFixed(1)+' s</small></div>';document.querySelector('.cards').append(b);b.onclick=()=>select(i);return b});
function select(i){if(clip!==i){const gait=(k)=>k===0||k===2;if(!(gait(clip)&&gait(i)))phase=0;clip=i;}if(!active)rig.pose(i,phase);current.textContent=clips[i][0];cards.forEach((b,j)=>b.classList.toggle('active',j===i))}select(3);
const previewCanvas=document.createElement('canvas');
const preview=createRig(previewCanvas);
const previewPhases=[.21,.44,.21,.15,.45,.43,.2,.3];
function thumbnails(){
 cards.forEach((card,i)=>{
  const canvas=card.querySelector('canvas');
  if(!canvas.clientWidth||!canvas.clientHeight)return;
  preview.pose(i,previewPhases[i]);
  preview.render(.63,0,canvas.clientWidth,canvas.clientHeight);
  canvas.width=previewCanvas.width;
  canvas.height=previewCanvas.height;
  canvas.getContext('2d').drawImage(previewCanvas,0,0);
 });
}
new ResizeObserver(thumbnails).observe(document.querySelector('.cards'));
play.onclick=()=>{active=!active;play.textContent=active?'Ⅱ':'▶';play.ariaLabel=active?'Pause':'Play'};seek.oninput=()=>phase=seek.value/1000;
document.querySelectorAll('.modes button').forEach((b,i)=>b.onclick=()=>{mode=i;document.querySelector('.modes .on').classList.remove('on');b.classList.add('on')});
let px;
view.onpointerdown=e=>{px=e.clientX;view.setPointerCapture(e.pointerId)};
view.onpointermove=e=>{if(px!==undefined){yaw+=(e.clientX-px)*.012;px=e.clientX}};
view.onlostpointercapture=()=>px=undefined;
view.onkeydown=e=>{if(['ArrowLeft','ArrowRight'].includes(e.key)){e.preventDefault();yaw+=e.key==='ArrowLeft'?-.15:.15}};
let powered=true,carry=0;
release.onclick=()=>{powered=!powered;release.textContent=powered?'Release ragdoll':'Engage muscles';release.ariaPressed=String(!powered)};
resetStance.onclick=()=>{powered=true;phase=0;carry=0;select(3);rig.pose(3,phase);release.textContent="Release ragdoll";release.ariaPressed="false"};
seek.oninput=()=>{phase=seek.value/1000;rig.pose(clip,phase);carry=0};
function frame(now){
 const duration=clips[clip][2], step=1/120;
 if(active){carry+=Math.min(.1,(now-last)/1000)*parseFloat(speed.value);
 while(carry>=step){phase=(phase+step/duration)%1;rig.step(clip,phase,powered,step);carry-=step}}
 last=now;seek.value=phase*1000;time.textContent=(phase*duration).toFixed(1)+' / '+duration.toFixed(1)+'s';
 rig.render(yaw,mode);requestAnimationFrame(frame)
}
requestAnimationFrame(frame);
