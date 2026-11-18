const el=id=>document.getElementById(id);
const grid=el('grid'), viewer=el('viewer'), category=el('category'), collection=el('collection'),search=el('search');
let filtered=[], current=0, lastWheel=0, touchY=0, touchX=0;
function options(select,values){for(const value of [...new Set(values)].sort())select.add(new Option(value,value))}
const roomNames=['Living room','Kitchen & dining','Bedroom','Workspace','Bathroom','Entryway','Exterior'];
const roomFilter=el('roomFilter'), roomTarget=el('roomTarget');
options(roomFilter,roomNames);
options(roomTarget,roomNames);
roomTarget.value='Living room';
const storageKey='loop3d.gallery.room-shortlists.v1';
let shortlist={}, alternate=false;
function readRooms(){
 try {
  const value=JSON.parse(localStorage.getItem(storageKey)||'{}');
  return Object.fromEntries(roomNames.map(room=>[room,Array.isArray(value?.[room])?value[room].filter(id=>typeof id==='string'):[]]));
 }catch{return {}}
}
shortlist=readRooms();
function savedIn(room,id){return (shortlist[room]||[]).includes(id)}
function updateTools(a){
 const file=a.file.split('/').pop();
 el('download').href='files/'+file;
 el('download').download=file;
 el('download').textContent='Download .'+file.split('.').pop();
 el('placement').hidden=!a.placement;
 el('snippet').textContent=a.placement||'';
 const size=dimensions[a.img];
 el('dimensions').textContent=size?'Width × height × depth · '+size.map(m=>Number(m.toFixed(3))).join(' × ')+' m':(a.file.includes('/surfaces/')?'Material swatch · size depends on the object':'');
 el('angle').textContent=alternate?'First angle':'Second angle';
 el('saveRoom').textContent=savedIn(roomTarget.value,a.img)?'Remove from room':'Save to room';
}
el('angle').onclick=()=>{alternate=!alternate;inspect()};
roomTarget.onchange=()=>updateTools(filtered[current]);
roomFilter.onchange=()=>{if(roomFilter.value)roomTarget.value=roomFilter.value;render()};
el('saveRoom').onclick=()=>{
 const a=filtered[current],room=roomTarget.value;
 const latest=readRooms(),list=latest[room]||[],remove=list.includes(a.img);
 latest[room]=remove?list.filter(id=>id!==a.img):[...list,a.img];
 try{localStorage.setItem(storageKey,JSON.stringify(latest))}catch{el('status').textContent='Browser storage is unavailable. This selection could not be saved.';return}
 shortlist=latest;
 if(roomFilter.value===room){render();current=Math.min(current,filtered.length-1);if(current<0){viewer.close();return}alternate=false;inspect()}
 else updateTools(a);
 el('status').textContent=(remove?'Removed from ':'Saved to ')+room+'.';
};
window.addEventListener('storage',e=>{if(e.key!==storageKey&&e.key!==null)return;const id=filtered[current]?.img;shortlist=readRooms();render();if(viewer.open){current=Math.max(0,filtered.findIndex(a=>a.img===id));if(filtered.length)inspect();else viewer.close()}});
el('copy').onclick=async()=>{
 const code=filtered[current].placement;
 try{await navigator.clipboard.writeText(code);el('status').textContent='Placement code copied.'}
 catch{const range=document.createRange();range.selectNodeContents(el('snippet'));const selection=window.getSelection();selection.removeAllRanges();selection.addRange(range);el('status').textContent='Code selected. Use the copy command on your device.'}
};

options(collection,assets.map(a=>a.collection));
function updateCategories(){const prev=category.value;category.length=1;options(category,assets.filter(a=>!collection.value||a.collection===collection.value).map(a=>a.category));if([...category.options].some(o=>o.value===prev))category.value=prev}
function render(){
 const query=search.value.trim().toLowerCase().replaceAll('_',' ');
 filtered=assets.filter(a=>(!roomFilter.value||savedIn(roomFilter.value,a.img))&&(!collection.value||collection.value===a.collection)&&(!category.value||category.value===a.category)&&(a.name+' '+a.collection+' '+a.category).toLowerCase().includes(query));
 el('count').textContent=filtered.length+' items';el('heading').textContent=roomFilter.value||category.value||collection.value||'All assets';
 grid.replaceChildren(...filtered.map((a,i)=>{const btn=document.createElement('button');btn.className='card';btn.innerHTML='<img loading="lazy" alt=""><span class="caption"><small></small><b></b></span>';btn.firstChild.src='img/'+a.img+'.jpg';btn.querySelector('small').textContent=a.category;btn.querySelector('b').textContent=a.name;btn.onclick=()=>{current=i;alternate=false;inspect();viewer.showModal();document.body.style.overflow='hidden'};return btn}));
 if(!filtered.length){const p=document.createElement('p');p.className='empty';p.textContent='No matching assets. Try another name or category.';grid.append(p)}
}
function inspect(){const a=filtered[current];updateTools(a);el('status').textContent='';el('preview').src=(alternate?'alt/':'img/')+a.img+'.jpg';el('preview').alt=a.name;el('meta').textContent=a.collection+' / '+a.category;el('title').textContent=a.name;el('filename').textContent=a.file;el('position').textContent=(current+1)+' of '+filtered.length;el('previous').disabled=current===0;el('next').disabled=current===filtered.length-1}
function move(n){current=Math.max(0,Math.min(filtered.length-1,current+n));inspect()}
el('previous').onclick=()=>move(-1);el('next').onclick=()=>move(1);el('close').onclick=()=>viewer.close();viewer.onclose=()=>{document.body.style.overflow='';const card=grid.children[current];card?.focus({preventScroll:true});card?.scrollIntoView({block:'nearest'})};
viewer.onkeydown=e=>{if(e.target.closest('select,pre'))return;if(['ArrowRight','ArrowDown','ArrowLeft','ArrowUp'].includes(e.key)){e.preventDefault();move(['ArrowRight','ArrowDown'].includes(e.key)?1:-1)}};
el('stage').addEventListener('wheel',e=>{e.preventDefault();if(performance.now()-lastWheel>220&&Math.abs(e.deltaY)>2){lastWheel=performance.now();move(Math.sign(e.deltaY))}},{passive:false});
el('stage').ontouchstart=e=>{touchX=e.touches[0].clientX;touchY=e.touches[0].clientY};el('stage').ontouchend=e=>{const dx=touchX-e.changedTouches[0].clientX,dy=touchY-e.changedTouches[0].clientY;const d=Math.abs(dx)>Math.abs(dy)?dx:dy;if(Math.abs(d)>36)move(Math.sign(d))};
collection.onchange=()=>{updateCategories();render()};category.onchange=render;search.oninput=render;updateCategories();render();
