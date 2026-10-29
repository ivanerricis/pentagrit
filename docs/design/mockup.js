(function(){
const el=(t,c,p)=>{const e=document.createElement(t);if(c)e.className=c;if(p)p.appendChild(e);return e};
const NS='http://www.w3.org/2000/svg';
const sv=(t,a,p)=>{const e=document.createElementNS(NS,t);for(const k in a)e.setAttribute(k,a[k]);if(p)p.appendChild(e);return e};
const clamp=(v,a,b)=>Math.min(b,Math.max(a,v));
const COL=['#FF5A5F','#FFB347','#F9F871','#4CD7A6','#4DA3FF'];
const SATS=['Off','Tanh','Soft Clip','Tube','Wavefold','Hard Clip'];
const OSL=['Off','2x','4x','8x'], LATL=[0,63,94,110], LATM=[0,5,7,8];
const latOf=(os,ph)=>(ph==='min'?LATM:LATL)[os];
const COLV=COL.map((c,i)=>'var(--b'+(i+1)+')');let COLR=COL.slice();const TC={};
const INIT=()=>({inG:0,outG:0,mix:100,auto:false,os:1,n:3,bypass:false,xo:[120,1200,3500,8000],
  phase:'linear',offOS:-1,
  bands:[0,1,2,3,4].map(()=>({gain:0,attack:0,sustain:0,speed:50,sat:0,drive:6,mix:100,mute:false,solo:false,bypass:false}))});
const S=INIT(); // mutated in place, never replaced
const refresh=[];
const P=document.getElementById('plug'), stage=document.getElementById('stage');
const clone=o=>JSON.parse(JSON.stringify(o));
function apply(src){
  ['inG','outG','mix','auto','os','n','bypass'].forEach(k=>S[k]=src[k]);
  S.xo=src.xo.slice();src.bands.forEach((b,i)=>Object.assign(S.bands[i],b));
}
const CFG_DEF={zoom:1,theme:'dark',layout:'full',tips:true,dbl:'reset',drag:'vertical',sens:50,hold:1000,release:20,refresh:60,showCpu:true,folder:'%APPDATA%\\Pentagrit\\Presets',startup:{mode:'last'},last:{name:'Init',user:false},updates:false};
function loadCfg(){try{return JSON.parse(localStorage.getItem('pentagrit-settings')||'{}')}catch(e){return {}}}
function saveCfg(){try{localStorage.setItem('pentagrit-settings',JSON.stringify(CFG))}catch(e){}}
let CFG=Object.assign(clone(CFG_DEF),loadCfg());
if(![.75,1,1.25,1.5,2].includes(CFG.zoom))CFG.zoom=1;
const B=(i,o)=>st=>Object.assign(st.bands[i],o);
const F={
 'Init':['Init',()=>{}],
 'Punchy Kick':['Drums',st=>{B(0,{attack:6})(st);B(1,{attack:9,sat:1,drive:6})(st);st.auto=true}],
 'Snare Snap':['Drums',st=>{B(2,{attack:9})(st);B(3,{attack:6})(st);B(0,{sustain:-4})(st);B(1,{sustain:-4})(st)}],
 'Room Tamer':['Drums',st=>{[2,3,4].forEach(i=>B(i,{sustain:-9,speed:25})(st))}],
 'Overhead Air':['Drums',st=>{B(4,{attack:3,sat:2,drive:4,mix:60})(st)}],
 'Bass Grit':['Bass',st=>{[1,2].forEach(i=>B(i,{sat:3,drive:12,mix:70})(st))}],
 'Sub Tight':['Bass',st=>{B(0,{sustain:-6,sat:1,drive:6})(st)}],
 'Glue Warmth':['Mix Bus',st=>{st.bands.forEach(b=>{b.sat=3;b.drive=3});st.auto=true;st.mix=50}],
 'Gentle Punch':['Mix Bus',st=>{[1,2,3].forEach(i=>B(i,{attack:3,sustain:1})(st))}],
 'Wavefold Shimmer':['Creative',st=>{[3,4].forEach(i=>B(i,{sat:4,drive:18,mix:40})(st))}],
 'Crush Hats':['Creative',st=>{B(4,{sat:5,drive:24,attack:6})(st)}],
 'Sustain Bloom':['Creative',st=>{[1,2,3].forEach(i=>B(i,{sustain:12,sat:1,drive:9,speed:25})(st))}],
 'Full Destroy':['Creative',st=>{st.os=3;st.auto=true;st.bands.forEach((b,i)=>{b.sat=i%2?4:5;b.drive=24})}]
};
const FACT={};Object.keys(F).forEach(k=>{const st=INIT(),ini=INIT();F[k][1](st);
  let hi=2;st.bands.forEach((b,i)=>{if(JSON.stringify(b)!==JSON.stringify(ini.bands[i]))hi=Math.max(hi,i)});st.n=Math.max(st.n,hi+1);FACT[k]={cat:F[k][0],state:st}});
const USER={};
(()=>{const a=INIT();B(2,{sat:3,drive:8,attack:3})(a);a.mix=70;USER['Vocal Bus Crunch']={state:a,cat:'Vocals'};
 const b=INIT();b.bands.forEach(x=>{x.sat=5;x.drive=18});b.mix=30;b.auto=true;USER['Parallel Smash']={state:b,cat:'Parallel'}})();
let cur={name:'Init',user:false}, modified=false, abSel='A', slots={A:null,B:null};
const stateOf=c=>(c.user?USER:FACT)[c.name].state;
function load(name,user){cur={name,user};apply(stateOf(cur));modified=false;CFG.last={name,user};saveCfg();syncAll()}
function mark(){if(!modified){modified=true;updName()}}
function syncAll(){refresh.forEach(f=>f());updName();layout()}

/* ---------- widgets ---------- */
function arcD(cx,cy,r,a0,a1){const p=a=>[cx+r*Math.cos(a*Math.PI/180),cy+r*Math.sin(a*Math.PI/180)];const [x0,y0]=p(a0),[x1,y1]=p(a1);return `M${x0.toFixed(2)} ${y0.toFixed(2)}A${r} ${r} 0 ${a1-a0>180?1:0} 1 ${x1.toFixed(2)} ${y1.toFixed(2)}`}
const scale=()=>Math.min(1,stage.clientWidth/1100)*CFG.zoom;
function knob(parent,obj,key,o){
  const size=o.size||38,w=el('div','knob',parent);w.dataset.hint=o.hint||o.label;
  const l=el('span','lbl',w);l.textContent=o.label;
  const svg=sv('svg',{width:size,height:size,viewBox:'0 0 40 40'},w);
  const trk=sv('path',{d:arcD(20,20,15,135,405),fill:'none','stroke-width':4,'stroke-linecap':'round'},svg);trk.style.stroke='var(--bg3)';
  const va=sv('path',{fill:'none','stroke-width':4,'stroke-linecap':'round'},svg);va.style.stroke=o.color||'var(--ink)';
  const pt=sv('line',{'stroke-width':2,'stroke-linecap':'round'},svg);pt.style.stroke='var(--ink)';
  const vv=el('span','val',w);const min=o.min,max=o.max;
  function draw(){
    const v=obj[key],tt=(v-min)/(max-min),a=135+270*tt,a0=135+270*(o.bipolar?(0-min)/(max-min):0);
    const lo=Math.min(a0,a),hi=Math.max(a0,a);
    va.setAttribute('d',hi-lo<.5?'M0 0':arcD(20,20,15,lo,hi));
    const r=a*Math.PI/180;
    pt.setAttribute('x1',20+6*Math.cos(r));pt.setAttribute('y1',20+6*Math.sin(r));
    pt.setAttribute('x2',20+13*Math.cos(r));pt.setAttribute('y2',20+13*Math.sin(r));
    vv.textContent=o.fmt(v);o.on&&o.on();
  }
  function set(v){v=clamp(o.step?Math.round(v/o.step)*o.step:v,min,max);if(v!==obj[key]){obj[key]=v;mark()}draw()}
  let sy=0,s0=0,fv=0,la=0;
  const kf=()=>Math.pow(4,(CFG.sens-50)/50);
  const ang=e=>{const r=svg.getBoundingClientRect();return Math.atan2(e.clientY-(r.top+r.height/2),e.clientX-(r.left+r.width/2))};
  w.addEventListener('pointerdown',e=>{if(e.target.closest('.vin'))return;w.setPointerCapture(e.pointerId);sy=e.clientY;s0=obj[key];fv=s0;la=ang(e);e.preventDefault()});
  w.addEventListener('pointermove',e=>{if(!w.hasPointerCapture(e.pointerId))return;
    if(CFG.drag==='circular'){const a=ang(e);let d=a-la;if(d>Math.PI)d-=2*Math.PI;if(d<-Math.PI)d+=2*Math.PI;la=a;fv=clamp(fv+d/(1.5*Math.PI)*(max-min)*kf()*(e.shiftKey?.2:1),min,max);set(fv)}
    else set(s0+(sy-e.clientY)/(scale()*(e.shiftKey?700:170)/kf())*(max-min))});
  function typeIn(){vv.textContent='';const inp=el('input','vin',vv);inp.value=o.raw?o.raw(obj[key]):String(+obj[key].toFixed(2));inp.setAttribute('aria-label',o.label);inp.focus();inp.select();
    let done=false;const fin=ok=>{if(done)return;done=true;if(ok){const v=o.parse?o.parse(inp.value):parseFloat(inp.value);if(!isNaN(v))set(v)}inp.remove();draw()};
    inp.onkeydown=e=>{e.stopPropagation();if(e.key==='Enter')fin(true);if(e.key==='Escape')fin(false)};inp.onblur=()=>fin(true)}
  w.addEventListener('dblclick',()=>{if(CFG.dbl==='type')typeIn();else set(o.def)});
  w.addEventListener('wheel',e=>{e.preventDefault();set(obj[key]-Math.sign(e.deltaY)*(max-min)/50)},{passive:false});
  draw();refresh.push(draw);
}
function toggle(parent,text,obj,key,cls,hint,after){
  const b=el('button',cls,parent);b.textContent=text;b.dataset.hint=hint;
  const d=()=>b.classList.toggle('on',!!obj[key]);
  b.addEventListener('click',()=>{obj[key]=!obj[key];mark();d();after&&after()});
  d();refresh.push(d);return b;
}
function seg(parent,items,get,set,hint){
  const s=el('div','seg',parent);s.dataset.hint=hint;
  const bs=items.map((t,i)=>{const b=el('button','',s);b.textContent=t;b.addEventListener('click',()=>{set(i);mark();d()});return b});
  const d=()=>bs.forEach((b,i)=>b.classList.toggle('on',get()===i));d();refresh.push(d);
}
const fdb=v=>(v>0?'+':'')+v.toFixed(1)+' dB', fpc=v=>Math.round(v)+' %';
const fsp=v=>'x'+Math.pow(4,(v-50)/50).toFixed(2);

/* ---------- top bar ---------- */
const top=el('div','top',P);
const br=el('div','brand',top);br.innerHTML=document.getElementById('markT').innerHTML;br.firstElementChild.setAttribute('class','ic');
el('span','wm',br).textContent='PENTAGRIT';
const pr=el('div','preset',top);
const prev=el('button','pbtn',pr);prev.textContent='‹';prev.dataset.hint='Previous preset';
const nameBtn=el('button','pbtn pname',pr);nameBtn.dataset.hint='Preset browser (Factory / User)';
const next=el('button','pbtn',pr);next.textContent='›';next.dataset.hint='Next preset';
const dots=el('button','pbtn',pr);dots.textContent='⋯';dots.dataset.hint='Preset menu: Save, Save As, Rename, Delete, Duplicate, Import, Export';
const abw=el('div','ab',pr);
const abA=el('button','on',abw),abB=el('button','',abw);abA.textContent='A';abB.textContent='B';
abA.dataset.hint=abB.dataset.hint='A/B compare: two states kept in memory';
function updName(){nameBtn.innerHTML='';const s=el('span','',nameBtn);s.textContent=cur.name;const m=el('span','mod',s);m.textContent=modified?' *':'';el('span','',nameBtn).textContent='▾'}
el('div','spacer',top);
let g=el('div','grp',top);el('span','lbl',g).textContent='Bands';
function fixOrder(){for(let i=1;i<4;i++)if(S.xo[i]<S.xo[i-1]*RAT)S.xo[i]=Math.min(19000,Math.round(S.xo[i-1]*RAT))}
seg(g,['2','3','4','5'],()=>S.n-2,i=>{S.n=i+2;fixOrder();layout()},'Number of bands (2 to 5): inactive band strips collapse, the others widen');
g=el('div','grp',top);el('span','lbl',g).textContent='OS';
seg(g,OSL,()=>S.os,i=>{S.os=i;updLat()},'Oversampling: Off / 2x / 4x / 8x (changes latency)');
const lat=el('div','lat',top);lat.dataset.hint='Reported latency, compensated by the host';
const latV=el('span','val',lat),latS=el('span','val',lat);latV.style.color='var(--ink)';
function updLat(){const L=latOf(S.os,S.phase);latV.textContent=(L/48).toFixed(2)+' ms';latS.textContent=L+' smp'}refresh.push(updLat);updLat();
toggle(top,'AUTO-GAIN',S,'auto','tog','Auto-gain: keeps wet loudness equal to dry (RMS, +-24 dB)');
toggle(top,'BYPASS',S,'bypass','tog byp','Global bypass (20 ms crossfade, latency is kept)',()=>P.classList.toggle('bypassed',S.bypass));
refresh.push(()=>P.classList.toggle('bypassed',S.bypass));
const gear=el('button','tog gear',top);gear.dataset.hint='Settings: interface, audio, presets, about';gear.setAttribute('aria-label','Settings');
gear.innerHTML='<svg width="16" height="16" viewBox="0 0 16 16"><circle cx="8" cy="8" r="6.3" fill="none" stroke="currentColor" stroke-width="2" stroke-dasharray="2.47 2.47"/><circle cx="8" cy="8" r="4.6" fill="none" stroke="currentColor" stroke-width="2.4"/></svg>';

/* ---------- crossover display ---------- */
const xo=el('div','xo',P);
const cv=el('canvas','',xo);const CW=1084,CH=168,DPR=2;cv.width=CW*DPR;cv.height=CH*DPR;
const cx=cv.getContext('2d');cx.scale(DPR,DPR);
const PAD=18,PT=26,PB=18,RAT=Math.pow(2,1/3);
const fx=f=>PAD+Math.log(f/20)/Math.log(1000)*(CW-2*PAD);
const xf=x=>20*Math.pow(1000,(x-PAD)/(CW-2*PAD));
const fmtF=f=>f>=1000?(f/1000).toFixed(f>=10000?1:2).replace(/\.?0+$/,'')+' kHz':Math.round(f)+' Hz';
let drag=-1,hov=-1;
const mx=e=>{const r=cv.getBoundingClientRect();return (e.clientX-r.left)/r.width*CW};
cv.addEventListener('pointerdown',e=>{const x=mx(e);let best=-1,bd=14;for(let i=0;i<S.n-1;i++){const d=Math.abs(x-fx(S.xo[i]));if(d<bd){bd=d;best=i}}if(best>=0){drag=best;cv.setPointerCapture(e.pointerId)}});
cv.addEventListener('pointermove',e=>{const x=mx(e);
  if(drag>=0){const lo=drag>0?S.xo[drag-1]*RAT:30,hi=drag<S.n-2?S.xo[drag+1]/RAT:18000;S.xo[drag]=clamp(Math.round(xf(x)),lo,hi);mark();layout();return}
  hov=-1;for(let i=0;i<S.n-1;i++)if(Math.abs(x-fx(S.xo[i]))<14)hov=i;cv.style.cursor=hov>=0?'ew-resize':'default'});
cv.addEventListener('pointerup',()=>{drag=-1});
cv.addEventListener('dblclick',e=>{const x=mx(e);const d=[120,1200,3500,8000];for(let i=0;i<S.n-1;i++)if(Math.abs(x-fx(S.xo[i]))<14){S.xo[i]=d[i];mark();layout()}});
cv.dataset.hint='Crossover: drag the handles (log 20 Hz to 20 kHz, 1/3 octave minimum spacing), double-click resets';
function edges(){const e=[20];for(let i=0;i<S.n-1;i++)e.push(S.xo[i]);e.push(20000);return e}

/* fake program material */
function hits(t){const p0=(t*1.6)%1,p1=(t*1.6+.5)%1,p2=(t*6.4)%1;
  return {k:Math.exp(-p0*7),s:Math.exp(-p1*9),h:Math.exp(-p2*10),kp:p0,sp:p1,hp:p2}}
const WK=[1,.55,.1,0,0],WS=[.15,.6,1,.8,.25],WH=[0,0,.1,.55,1];
function bandEnergy(i,h){return Math.max(.12,WK[i]*h.k,WS[i]*h.s,WH[i]*h.h*.8)}
function spec(f,h,t){const l=Math.log10(f/20)/3;
  let v=.62-.3*l+.06*Math.sin(l*40+t*.7)+.04*Math.sin(l*91-t);
  v+=.3*h.k*Math.exp(-Math.pow((l-.2)/.14,2))+.28*h.s*Math.exp(-Math.pow((l-.55)/.2,2))+.3*h.h*Math.exp(-Math.pow((l-.88)/.14,2));
  return clamp(v,0,1)}
function rr(c,x,y,w,h,r){c.beginPath();c.moveTo(x+r,y);c.arcTo(x+w,y,x+w,y+h,r);c.arcTo(x+w,y+h,x,y+h,r);c.arcTo(x,y+h,x,y,r);c.arcTo(x,y,x+w,y,r);c.closePath()}
function drawX(t){
  const h=hits(t),c=cx;c.clearRect(0,0,CW,CH);
  const y0=PT,y1=CH-PB,ph=y1-y0,anySolo=S.bands.some((b,i)=>i<S.n&&b.solo);
  c.textBaseline='middle';c.font='500 10px "IBM Plex Mono",monospace';
  [20,50,100,200,500,1000,2000,5000,10000,20000].forEach(f=>{const x=fx(f);c.fillStyle=TC.bg2;c.fillRect(Math.round(x),y0,1,ph);c.fillStyle=TC.ink3;c.textAlign='center';c.fillText(f>=1000?(f/1000)+'k':String(f),clamp(x,14,CW-14),CH-8)});
  const E=edges(),N=260;
  for(let b=0;b<S.n;b++){
    const xa=fx(E[b]),xb=fx(E[b+1]),bs=S.bands[b];
    const silent=bs.mute||(anySolo&&!bs.solo);
    c.save();c.beginPath();c.rect(xa,0,xb-xa,CH);c.clip();
    c.globalAlpha=silent?.1:.18;c.fillStyle=COLR[b];c.fillRect(xa,y0,xb-xa,ph);
    c.beginPath();c.moveTo(fx(20),y1);
    for(let i=0;i<=N;i++){const f=20*Math.pow(1000,i/N);c.lineTo(fx(f),y1-spec(f,h,t)*ph*(silent?.5:1))}
    c.lineTo(fx(20000),y1);c.closePath();c.globalAlpha=silent?.25:.7;c.fillStyle=COLR[b];c.fill();
    c.restore();
    c.globalAlpha=silent?.4:1;c.fillStyle=COLR[b];c.textAlign='left';c.font='600 11px "IBM Plex Sans",sans-serif';c.fillText('B'+(b+1),xa+8,y0+10);c.globalAlpha=1;
  }
  c.font='500 10px "IBM Plex Mono",monospace';
  for(let i=0;i<S.n-1;i++){
    const x=Math.round(fx(S.xo[i])),on=drag===i||hov===i;
    c.fillStyle=on?TC.ink:TC.ink2;c.fillRect(x-1,y0-6,2,ph+6);
    const lab=fmtF(S.xo[i]),w=c.measureText(lab).width+14;
    c.fillStyle=on?TC.ink:TC.bg3;rr(c,x-w/2,3,w,18,4);c.fill();
    c.fillStyle=on?TC.bg0:TC.ink;c.textAlign='center';c.fillText(lab,x,12.5);
  }
}

/* ---------- band strips + global ---------- */
const body=el('div','body',P);
const strips=[],meters=[],tgs=[];
const SHF=[u=>u,u=>Math.tanh(u),u=>u<=-1?-1:u>=1?1:1.5*u-.5*u*u*u,
  u=>{const d=.35;return (Math.tanh(u+d)-Math.tanh(d))/(1-Math.tanh(d))*(u<0?.85:1)},
  u=>Math.sin(u*Math.PI/2),u=>clamp(u,-1,1)];
for(let i=0;i<5;i++){
  const b=S.bands[i],c=COLV[i];
  const st=el('div','strip',body);st.style.setProperty('--c',c);el('i','cbar',st);
  const sh=el('div','sh',st);el('span','nm',sh).textContent='B'+(i+1);
  const fr=el('span','fr',sh);
  const msb=el('div','msb',sh);
  toggle(msb,'M',b,'mute','m','Mute band '+(i+1),layout);toggle(msb,'S',b,'solo','s','Solo band '+(i+1),layout);toggle(msb,'B',b,'bypass','b','Bypass processing of band '+(i+1),layout);
  const xr=el('div','xrow',st);
  const xin=[0,1].map(k=>{const w=el('label','',xr);el('span','',w).textContent=k?'To':'From';const inp=el('input','',w);inp.setAttribute('aria-label','Band '+(i+1)+(k?' upper':' lower')+' crossover');inp.dataset.hint='Crossover frequency: type Hz or e.g. 1.2k';return inp});
  xin.forEach((inp,k)=>{const idx=i-1+k;
    inp.onchange=()=>{if(idx<0||idx>S.n-2)return;let s=inp.value.trim().toLowerCase(),v=parseFloat(s);if(isNaN(v)){layout();return}if(/k/.test(s))v*=1000;
      const lo=idx>0?S.xo[idx-1]*RAT:30,hi=idx<S.n-2?S.xo[idx+1]/RAT:18000;S.xo[idx]=clamp(Math.round(v),lo,hi);mark();layout()};
    inp.onkeydown=e=>{if(e.key==='Enter')inp.blur()}});
  const inner=el('div','inner',st);
  let sec=el('div','sec',inner);el('span','lbl',sec).textContent='Transient';
  const tg=el('div','tg',sec);tg.dataset.hint='Live transient gain applied to this band';el('b','',tg);const ti=el('i','',tg);const tv=el('span','',tg);
  let kr=el('div','krow',sec);
  knob(kr,b,'attack',{label:'Attack',min:-24,max:24,def:0,bipolar:true,fmt:v=>(v>0?'+':'')+v.toFixed(1),color:c,step:.1,hint:'Attack gain in dB (boosts or softens the onset)'});
  knob(kr,b,'sustain',{label:'Sustain',min:-24,max:24,def:0,bipolar:true,fmt:v=>(v>0?'+':'')+v.toFixed(1),color:c,step:.1,hint:'Sustain gain in dB (boosts or tightens the tail)'});
  knob(kr,b,'speed',{label:'Speed',min:0,max:100,def:50,fmt:fsp,color:c,step:1,hint:'Detector speed, x0.25 (slow) to x4 (fast)',raw:v=>Math.pow(4,(v-50)/50).toFixed(2),parse:v=>50+50*Math.log(parseFloat(v.replace('x','')))/Math.log(4)});
  sec=el('div','sec grow',inner);el('span','lbl',sec).textContent='Saturation';
  const ss=el('div','satsel',sec);
  const sp=el('button','',ss);sp.textContent='‹';sp.dataset.hint='Previous curve';
  const stt=el('button','t',ss);stt.dataset.hint='Saturation curve (click to step)';
  const sn=el('button','',ss);sn.textContent='›';sn.dataset.hint='Next curve';
  const cvs=sv('svg',{viewBox:'0 0 152 56',class:'curve',preserveAspectRatio:'none'},sec);
  const ax1=sv('path',{d:'M76 2V54M6 28H146','stroke-width':1,fill:'none'},cvs);ax1.style.stroke='var(--bg3)';
  const ax2=sv('path',{d:'M6 52L146 4','stroke-width':1,fill:'none'},cvs);ax2.style.stroke='var(--bg4)';
  const cp=sv('path',{fill:'none','stroke-width':2,'stroke-linejoin':'round'},cvs);cp.style.stroke=c;
  const drawCurve=()=>{
    stt.textContent=SATS[b.sat];
    const gn=1+b.drive/36*6,f=SHF[b.sat];let d='';
    for(let k=0;k<=120;k++){const x=-1+2*k/120,y=clamp(f(x*(b.sat?gn:1)),-1.15,1.15);d+=(k?'L':'M')+(6+(x+1)*70).toFixed(1)+' '+(28-y*24).toFixed(1)}
    cp.setAttribute('d',d);cp.setAttribute('opacity',b.sat?1:.5);
  };
  const setSat=v=>{b.sat=(v+6)%6;mark();drawCurve()};
  sp.onclick=()=>setSat(b.sat-1);sn.onclick=()=>setSat(b.sat+1);stt.onclick=()=>setSat(b.sat+1);
  refresh.push(drawCurve);
  kr=el('div','krow',sec);
  knob(kr,b,'drive',{label:'Drive',min:0,max:36,def:6,fmt:v=>v.toFixed(1)+' dB',color:c,step:.1,hint:'Drive into the saturation curve (0 to 36 dB)',on:drawCurve});
  knob(kr,b,'mix',{label:'Mix',min:0,max:100,def:100,fmt:fpc,color:c,step:1,hint:'Dry/wet of the saturation in this band'});
  sec=el('div','sec',inner);
  const orow=el('div','outrow',sec);
  knob(orow,b,'gain',{label:'Gain',min:-24,max:12,def:0,bipolar:true,fmt:fdb,color:c,step:.1,size:42,hint:'Band output gain'});
  const mt=el('div','meter',orow);mt.dataset.hint='Band output peak';mt.style.setProperty('--c',c);
  const md=[0,1].map(()=>el('div','',mt));const mi=md.map(d=>el('i','',d));const pk=el('u','',md[0]);
  strips.push({st,fr,inner,ti,tv,xin});meters.push({mi,pk,v:0,h:0,t:0});tgs.push(0);
}
const gl=el('div','glob',body);el('h3','',gl).textContent='Master';
const mr=el('div','mrow',gl);
const gcol=(key,lbl,hint)=>{const m=el('div','mc',mr);knob(m,S,key,{label:lbl,min:-24,max:24,def:0,bipolar:true,fmt:fdb,size:44,step:.1,hint});const mt=el('div','meter big g',m);mt.dataset.hint=lbl+' peak level';mt.style.setProperty('--c','var(--ink)');const ds=[0,1].map(()=>el('div','',mt));const a=ds.map(d=>el('i','',d));a.pk=el('u','',ds[0]);a.st={v:0,h:0,t:0};return a};
const inM=gcol('inG','Input','Input gain'),outM=gcol('outG','Output','Output gain');
const mx2=el('div','row',gl);knob(mx2,S,'mix',{label:'Mix',min:0,max:100,def:100,fmt:fpc,size:44,step:1,hint:'Global dry/wet (phase-matched dry)'});
const ag=el('div','ag',gl);ag.dataset.hint='Current auto-gain compensation';el('span','lbl',ag).textContent='Auto-gain';const agv=el('span','val',ag);

/* ---------- layout ---------- */
function layout(){
  const E=edges(),any=S.bands.some((b,i)=>i<S.n&&b.solo);
  strips.forEach((s,i)=>{
    const b=S.bands[i];s.st.style.display=i>=S.n?'none':'';
    s.st.classList.toggle('dim',i<S.n&&(b.mute||(any&&!b.solo)));
    if(i<S.n){const [a0,a1]=s.xin;if(document.activeElement!==a0)a0.value=i?fmtF(E[i]):'20 Hz';if(document.activeElement!==a1)a1.value=i<S.n-1?fmtF(E[i+1]):'20 kHz';a0.disabled=i===0;a1.disabled=i===S.n-1}
    s.fr.innerHTML=i<S.n?fmtF(E[i]).replace(' ','&nbsp;')+'<br>'+fmtF(E[i+1]).replace(' ','&nbsp;'):'';
  });
}

/* ---------- menus + modal (cascading, like juce::PopupMenu) ---------- */
const modal=el('div','modal',P);modal.hidden=true;
const hint=el('div','hint');
let panels=[],menuOwner=null;
function closeMenus(){panels.forEach(p=>p.remove());panels=[];menuOwner=null}
function showPanel(depth,entries,place){
  while(panels.length>depth)panels.pop().remove();
  const m=el('div','menu',P);panels.push(m);
  entries.forEach(e=>{
    if(e.sep){el('hr','',m);return}
    const b=el('button','',m);
    el('span','ck',b).textContent=e.cur?'✓':'';
    el('span','lb',b).textContent=e.label;
    const r=el('span','rt',b);if(e.kbd)r.textContent=e.kbd;if(e.children)r.textContent='›';
    if(e.cur)b.classList.add('cur');
    const open=()=>{[...m.children].forEach(x=>x.classList.remove('open'));b.classList.add('open');
      showPanel(depth+1,e.children,mm=>{const p0=P.getBoundingClientRect(),s=p0.width/1100,br=b.getBoundingClientRect(),pr0=m.getBoundingClientRect();
        let l=(br.right-p0.left)/s-2;if(l+mm.offsetWidth>1096)l=(pr0.left-p0.left)/s-mm.offsetWidth+2;
        mm.style.left=l+'px';mm.style.top=Math.max(4,(br.top-p0.top)/s-6)+'px'})};
    b.onpointerenter=()=>{while(panels.length>depth+1)panels.pop().remove();[...m.children].forEach(x=>x.classList.remove('open'));if(e.children&&!e.dis)open()};
    if(e.dis){b.disabled=true}
    else b.onclick=()=>{if(e.children)open();else{closeMenus();e.fn()}};
  });
  place(m);
}
function openMenu(anchor,entries){
  if(panels.length&&menuOwner===anchor){closeMenus();return}
  closeMenus();menuOwner=anchor;
  showPanel(0,entries,m=>{const p0=P.getBoundingClientRect(),ar=anchor.getBoundingClientRect(),s=p0.width/1100;
    m.style.left=Math.min((ar.left-p0.left)/s,1100-m.offsetWidth-8)+'px';m.style.top=((ar.bottom-p0.top)/s+4)+'px'});
}
document.addEventListener('pointerdown',e=>{if(panels.length&&!panels.some(p=>p.contains(e.target))&&!(menuOwner&&menuOwner.contains(e.target)))closeMenus()});
const uniq=n=>{let r=n,k=2;while(USER[r])r=n+' '+k++;return r};
const clean=s=>s.trim().replace(/[\\/:*?"<>|]/g,'');
function userCats(){const m=new Map();Object.keys(USER).forEach(n=>{const c=USER[n].cat;if(!m.has(c))m.set(c,[]);m.get(c).push(n)});return m}
function saveAs(n,cat){n=clean(n)||'Untitled';cat=clean(cat)||'Unsorted';if(USER[n]&&!(cur.user&&cur.name===n))n=uniq(n);USER[n]={state:clone(S),cat};cur={name:n,user:true};modified=false;updName();toast('Saved to User / '+cat+' / '+n)}
function showModal(title,fields,okText,cb){
  modal.innerHTML='';modal.hidden=false;const c=el('div','card',modal);el('h3','',c).textContent=title;
  const inps=fields.map(f=>{const w=el('label','fld',c);el('span','lbl',w).textContent=f.label;const i=el('input','',w);i.value=f.val;i.maxLength=48;
    if(f.list){const id='dl'+Math.random().toString(36).slice(2);const dl=el('datalist','',w);dl.id=id;f.list.forEach(v=>{el('option','',dl).value=v});i.setAttribute('list',id)}return i});
  if(inps[0])setTimeout(()=>{inps[0].focus();inps[0].select()},0);
  const bt=el('div','btns',c);const cn=el('button','',bt);cn.textContent='Cancel';const ok=el('button','ok',bt);ok.textContent=okText;
  const done=()=>{modal.hidden=true};cn.onclick=done;ok.onclick=()=>{const v=inps.map(i=>i.value);done();cb(...v)};
  inps.forEach(i=>i.onkeydown=e=>{if(e.key==='Enter')ok.click();if(e.key==='Escape')cn.click()});
}
const askSaveAs=()=>showModal('Save preset as',[{label:'Name',val:cur.name},{label:'Category (pick or type a new one)',val:cur.user?USER[cur.name].cat:'',list:[...userCats().keys()]}],'Save',saveAs);
const askName=(t,v,cb)=>showModal(t,[{label:'Name',val:v}],'OK',cb), ask=(t,okT,cb)=>showModal(t,[],okT,cb);
function browserTree(pick=(n,u)=>load(n,u),isCur=(n,u)=>cur.user===u&&cur.name===n){
  const fcats=[];Object.keys(FACT).forEach(n=>{const c=FACT[n].cat;let g=fcats.find(x=>x.label===c);if(!g){g={label:c,children:[]};fcats.push(g)}g.children.push({label:n,cur:isCur(n,false),fn:()=>pick(n,false)})});
  const ucats=[...userCats()].map(([c,ns])=>({label:c,children:ns.map(n=>({label:n,cur:isCur(n,true),fn:()=>pick(n,true)}))}));
  return [{label:'Factory',children:fcats},{label:'User',children:ucats.length?ucats:[{label:'(empty)',dis:true}]}];
}
nameBtn.onclick=()=>openMenu(nameBtn,browserTree());
dots.onclick=()=>{
  const fac=!cur.user;
  openMenu(dots,[
    {label:'Save',kbd:'Ctrl+S',fn:()=>{if(fac){toast('Factory presets are read-only. Use Save As.');askSaveAs()}else{modified=false;updName();toast('Saved "'+cur.name+'"')}}},
    {label:'Save As...',fn:askSaveAs},
    {label:'Rename...',dis:fac,fn:()=>askName('Rename preset',cur.name,n=>{n=clean(n)||cur.name;if(n!==cur.name&&USER[n])n=uniq(n);USER[n]=USER[cur.name];if(n!==cur.name)delete USER[cur.name];cur.name=n;updName();toast('Renamed')})},
    {label:'Delete',dis:fac,fn:()=>ask('Delete "'+cur.name+'"?','Delete',()=>{delete USER[cur.name];load('Init',false);toast('Deleted')})},
    {label:'Duplicate',fn:()=>{const n=uniq(cur.name+' copy'),cat=cur.user?USER[cur.name].cat:'Unsorted';USER[n]={state:clone(S),cat};cur={name:n,user:true};modified=false;updName();toast('Duplicated as "'+n+'"')}},
    {sep:1},
    {label:'Import...',fn:()=>toast('Import: a file chooser for .pgp files opens here')},
    {label:'Export...',fn:()=>toast('Export: saves "'+cur.name+'.pgp" to a chosen folder')}
  ]);
};
/* prev/next follow menu order: Factory categories, then User categories */
const order=()=>[...Object.keys(FACT).map(n=>[n,false]),...[...userCats().values()].flat().map(n=>[n,true])];
function step(d){const o=order();let i=o.findIndex(x=>x[0]===cur.name&&x[1]===cur.user);i=(i+d+o.length)%o.length;load(o[i][0],o[i][1])}
prev.onclick=()=>step(-1);next.onclick=()=>step(1);
function abSwitch(to){
  if(to===abSel)return;slots[abSel]=clone(S);const src=slots[to]||clone(slots[abSel]);apply(src);abSel=to;
  abA.classList.toggle('on',to==='A');abB.classList.toggle('on',to==='B');syncAll();toast('Comparing state '+to);
}
abA.onclick=()=>abSwitch('A');abB.onclick=()=>abSwitch('B');

/* ---------- footer ---------- */
const foot=el('div','foot',P);foot.appendChild(hint);hint.className='hint';
const cpu=el('span','val',foot);cpu.dataset.hint='DSP load of the audio thread';cpu.style.color='var(--ink2)';const ver=el('span','val',foot);ver.textContent='v0.1.0';
let toastT=0;function toast(m){hint.textContent=m;toastT=performance.now()+2600}
const dflt='Drag a knob. Shift for fine, double-click to reset or type a value (see Settings).';hint.textContent=dflt;
P.addEventListener('mouseover',e=>{if(performance.now()<toastT)return;const h=e.target.closest('[data-hint]');hint.textContent=h?h.dataset.hint:dflt});

/* ---------- tooltips ---------- */
const tip=el('div','tip',P);tip.hidden=true;let tipT=0;
P.addEventListener('mouseover',e=>{clearTimeout(tipT);tip.hidden=true;if(!CFG.tips)return;const h=e.target.closest('[data-hint]');if(!h||!P.contains(h))return;
  tipT=setTimeout(()=>{tip.textContent=h.dataset.hint;tip.hidden=false;const p0=P.getBoundingClientRect(),r=h.getBoundingClientRect(),s=p0.width/1100;
    let l=(r.left-p0.left)/s,tp=(r.bottom-p0.top)/s+6;l=Math.min(l,1100-tip.offsetWidth-8);if(tp+tip.offsetHeight>H()-4)tp=(r.top-p0.top)/s-tip.offsetHeight-6;
    tip.style.left=Math.max(4,l)+'px';tip.style.top=tp+'px'},450)});
P.addEventListener('mouseleave',()=>{clearTimeout(tipT);tip.hidden=true});
P.addEventListener('pointerdown',()=>{clearTimeout(tipT);tip.hidden=true});

/* ---------- settings panel ---------- */
const setOv=el('div','settings',P);setOv.hidden=true;
let setTab=0,msgEl=null;
const TABS=['Interface','Audio','Presets','About & Support'];
const say=t=>{if(msgEl)msgEl.textContent=t};
function openSettings(){setOv.hidden=false;renderSet()}
function closeSettings(){setOv.hidden=true;closeMenus()}
gear.onclick=()=>setOv.hidden?openSettings():closeSettings();
setOv.addEventListener('pointerdown',e=>{if(e.target===setOv)closeSettings()});
document.addEventListener('keydown',e=>{if(e.key==='Escape'&&!setOv.hidden&&modal.hidden&&!panels.length)closeSettings()});
function cacheTheme(){const cs=getComputedStyle(P);['bg0','bg1','bg2','bg3','bg4','ink','ink2','ink3'].forEach(n=>TC[n]=cs.getPropertyValue('--'+n).trim());COLR=[1,2,3,4,5].map(i=>cs.getPropertyValue('--b'+i).trim())}
function applyCfg(){
  P.dataset.theme=CFG.theme;P.classList.toggle('compact',CFG.layout==='compact');P.classList.toggle('nohold',CFG.hold===0);
  cpu.style.display=CFG.showCpu?'':'none';
  cacheTheme();fit();updLat();layout();
}
function changed(){saveCfg();applyCfg()}
function renderSet(){
  const old=setOv.querySelector('.sbody'),sc=old?old.scrollTop:0;
  setOv.innerHTML='';const card=el('div','scard',setOv);
  const hd=el('div','shead',card);el('h3','',hd).textContent='Settings';
  const tabs=el('div','seg stabs',hd);
  TABS.forEach((t,i)=>{const b=el('button',i===setTab?'on':'',tabs);b.textContent=t;b.onclick=()=>{setTab=i;renderSet();setOv.querySelector('.sbody').scrollTop=0}});
  const x=el('button','tog',hd);x.textContent='Done';x.onclick=closeSettings;
  const bd=el('div','sbody',card);
  [tabInterface,tabAudio,tabPresets,tabAbout][setTab](bd);
  bd.scrollTop=sc;
  msgEl=el('div','smsg',card);
}
function row(p,label,desc,build,badge){const r=el('div','srow',p);const l=el('div','sl',r);const t=el('span','st',l);t.textContent=label;
  if(badge)el('em','badge',t).textContent='saved with project';if(desc)el('span','sd',l).textContent=desc;build(el('div','sc',r));return r}
function segC(c,items,get,set){const s=el('div','seg',c);items.forEach(([v,t])=>{const b=el('button',get()===v?'on':'',s);b.textContent=t;b.onclick=()=>{set(v);changed();renderSet()}})}
function swC(c,get,set){const b=el('button','sw'+(get()?' on':''),c);b.setAttribute('role','switch');b.setAttribute('aria-checked',String(!!get()));b.onclick=()=>{set(!get());changed();renderSet()}}
function rngC(c,min,max,step,get,set,fmt,l,r){const w=el('div','rng',c);if(l)el('span','val',w).textContent=l;const i=el('input','',w);i.type='range';i.min=min;i.max=max;i.step=step;i.value=get();
  const v=el('span','val',w);v.textContent=fmt(get());i.oninput=()=>{set(+i.value);v.textContent=fmt(+i.value);changed()};if(r)el('span','val',w).textContent=r}
function btnC(c,text,fn){const b=el('button','tog',c);b.textContent=text;b.onclick=()=>fn(b);return b}
const head=(p,t)=>{el('div','shd',p).textContent=t};

function tabInterface(p){
  const Z=[.75,1,1.25,1.5,2].map(z=>[z,Math.round(z*100)+'%']);
  row(p,'Zoom','Scales the whole window; the aspect ratio stays fixed.',c=>segC(c,Z,()=>CFG.zoom,v=>CFG.zoom=v));
  row(p,'Theme','',c=>segC(c,[['dark','Dark'],['light','Light']],()=>CFG.theme,v=>CFG.theme=v));
  row(p,'Layout','Full: crossover display plus strips (1100 x 680). Compact: no display, crossovers typed in each strip (1100 x 540).',c=>segC(c,[['full','Full'],['compact','Compact']],()=>CFG.layout,v=>CFG.layout=v));
  row(p,'Tooltips','Show a description when hovering a control.',c=>swC(c,()=>CFG.tips,v=>CFG.tips=v));
  head(p,'Controls');
  row(p,'Double-click on a knob','',c=>segC(c,[['reset','Reset to default'],['type','Type value']],()=>CFG.dbl,v=>CFG.dbl=v));
  row(p,'Knob drag mode','',c=>segC(c,[['vertical','Vertical'],['circular','Circular']],()=>CFG.drag,v=>CFG.drag=v));
  row(p,'Knob sensitivity','Shift while dragging is always 5x finer.',c=>rngC(c,0,100,1,()=>CFG.sens,v=>CFG.sens=v,()=>'','Fine','Fast'));
  head(p,'Meters');
  row(p,'Peak hold','',c=>segC(c,[[0,'Off'],[500,'0.5 s'],[1000,'1 s'],[2000,'2 s'],[99999,'Infinite']],()=>CFG.hold,v=>CFG.hold=v));
  row(p,'Release speed','How fast the meters fall.',c=>rngC(c,5,60,1,()=>CFG.release,v=>CFG.release=v,v=>v+' dB/s'));
  row(p,'Refresh rate','',c=>segC(c,[[30,'30 Hz'],[60,'60 Hz']],()=>CFG.refresh,v=>CFG.refresh=v));
  head(p,'Footer');
  row(p,'Show CPU readout','',c=>swC(c,()=>CFG.showCpu,v=>CFG.showCpu=v));
}
function tabAudio(p){
  el('p','snote',p).textContent='These settings are stored in the plugin state and travel with the project. They are not per-user.';
  row(p,'Oversampling filter phase','Linear phase: flat phase response, more latency. Minimum phase: low latency, slight phase shift near the filter edge.',c=>segC(c,[['linear','Linear phase'],['min','Minimum phase']],()=>S.phase,v=>S.phase=v),true);
  row(p,'Offline render oversampling','Used automatically when the host renders offline or bounces.',c=>segC(c,[[-1,'Same as live'],[1,'2x'],[2,'4x'],[3,'8x']],()=>S.offOS,v=>S.offOS=v),true);
  const off=S.offOS<0?S.os:S.offOS;
  row(p,'Resulting latency','Reported to the host and compensated.',c=>{const d=el('div','lat2',c);
    const f=(os)=>{const L=latOf(os,S.phase);return (L/48).toFixed(2)+' ms  ('+L+' smp)'};
    d.innerHTML='Live ('+OSL[S.os]+'): '+f(S.os)+'<br>Offline ('+OSL[off]+'): '+f(off)});
}
function startupLabel(){const s=CFG.startup;if(s.mode==='last')return 'Last used';if(s.mode==='init')return 'Init';const e=(s.user?USER:FACT)[s.name];return (s.user?'User':'Factory')+' / '+(e?e.cat:'?')+' / '+s.name}
function tabPresets(p){
  row(p,'User preset folder','Sub-folders become categories in the User menu.',c=>{
    const i=el('input','',c);i.type='text';i.value=CFG.folder;i.style.width='260px';i.setAttribute('aria-label','User preset folder');i.onchange=()=>{CFG.folder=i.value;saveCfg()};
    btnC(c,'Choose...',()=>say('Choose...: a native folder chooser (juce::FileChooser) opens here.'));
    btnC(c,'Open folder',()=>say('Opens "'+CFG.folder+'" in Explorer / Finder.'))});
  row(p,'Startup preset','Which preset is loaded when a new plugin instance is created.',c=>{
    const b=btnC(c,startupLabel()+'  ▾',()=>openMenu(b,[
      {label:'Last used',cur:CFG.startup.mode==='last',fn:()=>{CFG.startup={mode:'last'};changed();renderSet()}},
      {label:'Init',cur:CFG.startup.mode==='init',fn:()=>{CFG.startup={mode:'init'};changed();renderSet()}},
      {label:'Specific preset',children:browserTree((n,u)=>{CFG.startup={mode:'preset',name:n,user:u};changed();renderSet()},(n,u)=>CFG.startup.mode==='preset'&&CFG.startup.name===n&&CFG.startup.user===u)}]))});
}
function debugText(){const ua=navigator.userAgent,os=/Windows/.test(ua)?'Windows 11':/Mac/.test(ua)?'macOS':/Linux/.test(ua)?'Linux':'Unknown';
  const L=latOf(S.os,S.phase);return ['Pentagrit v0.1.0 (VST3)','OS: '+os,'Host: Reaper 7.x (mock)','Sample rate: 48000 Hz','Block size: 512','Oversampling: '+OSL[S.os]+', '+(S.phase==='min'?'minimum':'linear')+' phase',
    'Latency: '+L+' samples ('+(L/48).toFixed(2)+' ms)','Offline oversampling: '+(S.offOS<0?'same as live':OSL[S.offOS])].join('\n')}
function tabAbout(p){
  row(p,'Pentagrit v0.1.0','Multiband transient shaper and saturator.',c=>{});
  row(p,'Licence','',c=>{el('span','val',c).textContent='AGPL-3.0-or-later'});
  row(p,'Source code','',c=>{const a=el('a','',c);a.href='https://github.com/ivanerricis/pentagrit';a.target='_blank';a.rel='noopener';a.textContent='github.com/ivanerricis/pentagrit'});
  head(p,'Support');
  const r=row(p,'Debug information','Include this when reporting a problem.',c=>{
    const b=btnC(c,'Copy debug info',()=>{const txt=debugText();const ok=()=>{b.textContent='Copied';setTimeout(()=>{b.textContent='Copy debug info'},2000)};
      try{navigator.clipboard.writeText(txt).then(ok,()=>{ta.select();say('Select the text and press Ctrl+C.')})}catch(e){ta.select();say('Select the text and press Ctrl+C.')}})});
  r.style.flexWrap='wrap';const ta=el('textarea','',r);ta.readOnly=true;ta.rows=7;ta.value=debugText();ta.setAttribute('aria-label','Debug information');
  head(p,'Updates');
  row(p,'Check for updates on startup','Off by default.',c=>swC(c,()=>CFG.updates,v=>CFG.updates=v));
  row(p,'Check now','Contacts GitHub; no personal data sent.',c=>{const b=btnC(c,'Check now',()=>{say('Checking...');setTimeout(()=>say('You are on the latest version (v0.1.0). Mockup: no request was made.'),700)})});
  head(p,'Reset');
  row(p,'Reset all settings','Resets the per-user settings (interface, controls, meters, preset folder, startup, updates). Presets and project audio settings are not changed.',c=>btnC(c,'Reset all settings...',()=>
    ask('Reset all settings?','Reset',()=>{CFG=clone(CFG_DEF);CFG.last=cur?{name:cur.name,user:cur.user}:CFG.last;saveCfg();applyCfg();renderSet();say('Settings reset to defaults.')})));
}

/* ---------- animation ---------- */
let agS=0,lastDraw=0;
const fall=(p,tg,dt)=>tg>p?tg:Math.max(tg,p-CFG.release/60*dt);
function holdUpd(o,v,ms,dt){const Hd=CFG.hold;if(Hd===0){o.h=0;return}if(v>=o.h){o.h=v;o.t=ms}else if(Hd<99999&&ms-o.t>Hd)o.h=Math.max(v,o.h-CFG.release/60*dt)}
function frame(ms){
  requestAnimationFrame(frame);
  if(ms-lastDraw<1000/CFG.refresh-3)return;
  const dt=Math.min(.1,(ms-lastDraw)/1000);lastDraw=ms;
  const t=ms/1000,h=hits(t),any=S.bands.some((b,i)=>i<S.n&&b.solo);
  if(CFG.layout!=='compact')drawX(t);
  const PH=[h.kp,h.kp,h.sp,h.sp,h.hp];
  for(let i=0;i<5;i++){
    if(i>=S.n)continue;
    const b=S.bands[i],m=meters[i];
    const silent=b.mute||(any&&!b.solo);
    const e=bandEnergy(i,h);
    let db=-46+44*Math.pow(e,.8);
    if(!b.bypass)db+=b.gain+(b.sat?Math.min(b.drive,18)*.25*(b.mix/100):0);
    const target=silent?0:clamp((db+60)/60,0,1);
    m.v=fall(m.v,target,dt);holdUpd(m,m.v,ms,dt);
    m.mi[0].style.height=(m.v*100)+'%';m.mi[1].style.height=clamp(m.v*100-2.5*Math.sin(t*3+i),0,100)+'%';m.pk.style.bottom=(m.h*100)+'%';
    const sp=Math.pow(4,(b.speed-50)/50),ph=PH[i];
    const early=Math.exp(-ph*14*sp),tail=(1-early)*Math.exp(-ph*2.5/sp);
    const tg=(b.bypass||silent)?0:(b.attack*early+b.sustain*tail)*(.35+.65*e);
    tgs[i]+=(tg-tgs[i])*.35;
    const s=strips[i],v=clamp(tgs[i],-24,24)/24*50;
    s.ti.style.left=(v<0?50+v:50)+'%';s.ti.style.width=Math.abs(v)+'%';
    s.tv.textContent=(tgs[i]>=0?'+':'')+tgs[i].toFixed(1);
  }
  const lv=Math.max(.15,h.k*.9,h.s*.8,h.h*.5),l=clamp((-40+40*Math.pow(lv,.7)+S.inG+60)/60,0,1);
  const oo=S.bypass?l:clamp(l+(S.outG+S.bands.reduce((a,b)=>a+(b.sat?b.drive:0),0)/20)/60,0,1);
  [[inM,l],[outM,oo]].forEach(([M,tg])=>{const o=M.st;o.v=fall(o.v,tg,dt);holdUpd(o,o.v,ms,dt);M[0].style.height=o.v*100+'%';M[1].style.height=clamp(o.v-.03,0,1)*100+'%';M.pk.style.bottom=o.h*100+'%'});
  const tgtAg=S.auto&&!S.bypass?-S.bands.reduce((a,b,i)=>a+(i<S.n&&b.sat?b.drive*.18*(b.mix/100):0),0)+Math.sin(t*.5)*.3:0;agS+=(tgtAg-agS)*.05;
  agv.textContent=S.auto?(agS>=0?'+':'')+agS.toFixed(1)+' dB':'off';
  if(CFG.showCpu)cpu.textContent='CPU '+(S.os*1.1+1.9+S.n*.2+Math.sin(t)*.3+Math.sin(t*3.1)*.15).toFixed(1)+'%';
}

/* ---------- fit + brand extras ---------- */
const sizer=document.getElementById('sizer');
const H=()=>CFG.layout==='compact'?540:680;
function fit(){const s=scale();P.style.transform='scale('+s+')';P.style.height=H()+'px';sizer.style.width=(1100*s)+'px';sizer.style.height=(H()*s)+'px'}
addEventListener('resize',fit);
applyCfg();syncAll();
(function(){const s=CFG.startup;let tg=s.mode==='init'?['Init',false]:s.mode==='preset'?[s.name,s.user]:[CFG.last.name,CFG.last.user];if((tg[1]?USER:FACT)[tg[0]])load(tg[0],tg[1])})();
requestAnimationFrame(frame);
const ic=document.getElementById('iconT').innerHTML;
['i16','i32'].forEach((id,k)=>{const h=document.getElementById(id);h.innerHTML=ic;const s=h.querySelector('svg');s.setAttribute('width',k?32:16);s.setAttribute('height',k?32:16)});
const sw=document.getElementById('sw');
[['bg0','#0E1013'],['bg1','#151920'],['bg2','#1D222B'],['bg3','#2A313D'],['ink','#E8ECF2'],['ink2','#98A2B3'],...COL.map((c,i)=>['band'+(i+1),c])].forEach(([n,c])=>{const d=el('div','',sw);el('i','',d).style.background=c;el('span','',d).innerHTML=n+'<br>'+c});
})();
