'use strict';
// 原创 Canvas 2D 场景。只画画面；武器品质、伤害与强化结果始终来自 C。
window.Forge2D = (() => {
  const scenes = new Map(), palette = ['#b6c6d4','#80e5ae','#75b9ff','#bd6bff','#ffd35e'];
  const hero = {x:620,y:340,tx:620,ty:340}, keys = new Set();
  let data = null, forging = 0, upgrading = 0, upgradeWeapon = null, upgraded = null, hit = null, last = performance.now();
  function poly(c, points, fill, stroke) {
    c.beginPath(); points.forEach(([x,y],i)=>i?c.lineTo(x,y):c.moveTo(x,y)); c.closePath();
    c.fillStyle=fill; c.fill(); if(stroke){c.strokeStyle=stroke;c.lineWidth=2;c.stroke();}
  }
  function rect(c,x,y,w,h,color){c.fillStyle=color;c.fillRect(x,y,w,h);}
  function oval(c,x,y,rx,ry,color){c.beginPath();c.ellipse(x,y,rx,ry,0,0,Math.PI*2);c.fillStyle=color;c.fill();}
  function glow(c,x,y,r,color,alpha=1) {
    c.save();c.globalAlpha=alpha;const g=c.createRadialGradient(x,y,0,x,y,r);g.addColorStop(0,color);g.addColorStop(1,color+'00');
    c.fillStyle=g;c.fillRect(x-r,y-r,r*2,r*2);c.restore();
  }
  function label(c,text,x,y,size=16,color='#f5dfb7') {c.fillStyle=color;c.font=`${size}px "Microsoft YaHei",sans-serif`;c.textAlign='center';c.fillText(text,x,y);}
  function blade(c,x,y,w,scale=1,angle=0) {
    const rarity=w?.rarity??0, type=w?.recipe??0, bw=[12,10,18,14,9,20][type];
    c.save();c.translate(x,y);c.rotate(angle);c.scale(scale,scale);
    c.shadowColor=palette[rarity];c.shadowBlur=rarity>=3?15:3;
    poly(c,[[0,-95],[bw,-65],[bw-3,35],[0,45],[-bw+3,35],[-bw,-65]],palette[rarity]);
    poly(c,[[0,-95],[0,45],[-bw+3,35],[-bw,-65]],'#ffffff66');
    rect(c,-1,-66,2,90,'#ffffff99');c.shadowBlur=0;
    poly(c,[[-28,31],[-22,25],[0,36],[22,25],[28,31],[23,42],[0,39],[-23,42]],'#dfac60');
    rect(c,-5,39,10,30,'#53333c');for(let i=0;i<4;i++)rect(c,-5,41+i*7,10,2,'#b1936b');
    poly(c,[[0,67],[8,75],[0,83],[-8,75]],palette[rarity]);oval(c,0,35,4,4,palette[rarity]);c.restore();
  }
  function aura(c,x,y,rarity,t,power=1) {
    const col=palette[rarity];glow(c,x,y,135*power,col,.25);
    if(rarity<3)return;
    c.save();c.translate(x,y);c.rotate(t*(rarity===4?.13:-.17));
    for(let i=0;i<(rarity===4?20:10);i++) {
      c.rotate(Math.PI*2/(rarity===4?20:10));
      const len=(rarity===4?190:120)*(1+.12*Math.sin(t*3+i));
      const g=c.createLinearGradient(0,0,0,-len);g.addColorStop(0,col+'90');g.addColorStop(1,col+'00');
      poly(c,[[0,-20],[-7,-len],[7,-len]],g);
    }
    c.restore();
    c.save();c.strokeStyle=col;c.shadowColor=col;c.shadowBlur=18;c.lineWidth=2;
    for(let i=0;i<3;i++) {c.beginPath();c.ellipse(x,y+55,95+i*18,22+i*6,0,t+i,t+i+4.8);c.stroke();}
    for(let i=0;i<26;i++) {
      const a=i*2.399+t*.3, r=45+(i*37)%115, px=x+Math.cos(a)*r,py=y+Math.sin(a)*r*.7;
      c.globalAlpha=.45+.5*Math.sin(t*3+i)**2;rect(c,px,py,rarity===4?3:2,rarity===4?3:2,col);
    }
    c.restore();
  }
  function pedestal(c,x,y,col='#d2a069') {
    oval(c,x,y+55,112,26,'#00000066');
    poly(c,[[x-70,y],[x,y-25],[x+70,y],[x,y+26]],'#515665');
    poly(c,[[x-70,y],[x,y+26],[x,y+64],[x-70,y+33]],'#252938');
    poly(c,[[x,y+26],[x+70,y],[x+70,y+33],[x,y+64]],'#363c50');
    c.strokeStyle=col;c.lineWidth=3;c.beginPath();c.ellipse(x,y,58,15,0,0,Math.PI*2);c.stroke();
    for(let i=0;i<6;i++){const a=i*Math.PI/3;rect(c,x+Math.cos(a)*43-3,y+Math.sin(a)*11-2,6,4,col);}
  }
  function person(c,x,y,t,walking=false,facing=1) {
    c.save();c.translate(x,y);c.scale(facing,1);oval(c,0,7,24,8,'#00000055');
    const stride=walking?Math.sin(t*12)*8:0,bob=walking?Math.sin(t*24)*2:Math.sin(t*2)*1.2;c.translate(0,bob);
    rect(c,-12,-12,10,19+stride,'#302d3d');rect(c,3,-12,10,19-stride,'#302d3d');
    rect(c,-14,4+stride,13,6,'#211f2b');rect(c,3,4-stride,14,6,'#211f2b');
    poly(c,[[-17,-49],[14,-49],[22,-10],[-20,-10]],'#428b8c');rect(c,-14,-42,28,7,'#83b8aa');rect(c,-17,-16,36,5,'#b78d5f');
    rect(c,-24,-42,9,28,'#c39a78');rect(c,17,-42,9,28,'#c39a78');
    rect(c,-14,-72,29,26,'#dbb48d');rect(c,-17,-77,34,12,'#453339');rect(c,-17,-66,7,14,'#453339');
    rect(c,7,-60,3,4,'#262432');rect(c,-2,-50,14,3,'#c39776');
    blade(c,28,-35,{rarity:data?.weapons[data.equipped]?.rarity??0},.28,-.35);c.restore();
  }
  function hammer(c,x,y,t,on) {
    c.save();c.translate(x,y);c.rotate(on?-.8+Math.max(0,Math.sin(t*12))*1.8:-.5);
    rect(c,-4,-68,8,85,'#a07650');poly(c,[[-23,-90],[20,-90],[26,-67],[-23,-67]],'#7b8392');rect(c,-22,-89,43,7,'#bac2ca');c.restore();
  }
  function sparks(c,x,y,t,age,col='#ffb850') {
    if(age<0||age>2)return;
    for(let i=0;i<42;i++){const a=i*2.399, v=30+(i*37)%135, life=(age+(i%4)*.07)%1;
      c.globalAlpha=1-life;rect(c,x+Math.cos(a)*v*life,y-Math.abs(Math.sin(a))*v*life+life*life*80,2+i%3,2,col);
    }c.globalAlpha=1;
  }
  function town(c,t) {
    const working=forging>performance.now();
    const g=c.createLinearGradient(0,0,0,450);g.addColorStop(0,'#172432');g.addColorStop(1,'#514037');rect(c,0,0,960,450,g);
    // 背景、石墙、屋梁与斜铺地砖形成明确的前后层次。
    for(let row=0;row<8;row++)for(let i=0;i<19;i++)rect(c,i*56-(row%2)*28,row*27,53,24,row%2?'#2d3036':'#30343a');
    rect(c,0,0,960,18,'#151b23');rect(c,0,190,960,14,'#1b2027');for(let x=15;x<960;x+=235)rect(c,x,0,12,205,'#191e25');
    poly(c,[[0,205],[960,205],[960,450],[0,450]],'#373232');
    c.strokeStyle='#bfa08118';c.lineWidth=2;for(let y=220;y<460;y+=36){c.beginPath();c.moveTo(0,y);c.lineTo(960,y);c.stroke();}
    for(let x=-300;x<1250;x+=90){c.beginPath();c.moveTo(480+(x-480)*.5,205);c.lineTo(x,450);c.stroke();}
    // 熔炉：火舌、热晕、烟与炉体。
    rect(c,62,64,145,174,'#1b232d');poly(c,[[49,84],[133,44],[217,84],[196,94],[70,94]],'#53606b');
    rect(c,77,112,115,113,'#0d1018');glow(c,134,175,140,'#ff862d',.25+.05*Math.sin(t*4));
    for(let i=0;i<9;i++){const x=88+i*11,ht=34+18*Math.sin(t*5+i);poly(c,[[x-8,218],[x+Math.sin(t*4+i)*8,151-ht],[x+10,218]],i%2?'#ffce63':'#f47c3f');}
    rect(c,77,222,118,9,'#877569');for(let i=0;i<8;i++){const p=(t*.22+i/8)%1;oval(c,133+Math.sin(i+t)*20,107-p*80,9+p*18,7+p*7,`rgba(143,139,133,${.12*(1-p)})`);}
    label(c,'熔炉',136,260,15);
    // 锻造砧的顶面、侧面与底座。
    oval(c,360,300,96,22,'#00000060');rect(c,320,238,76,60,'#59443b');
    poly(c,[[290,225],[388,207],[438,218],[408,240],[295,246]],'#a6afb5');
    poly(c,[[295,246],[408,240],[406,265],[329,270],[329,246]],'#586471');
    blade(c,362,229,{recipe:window.recipeIndex??0,rarity:working?1:0},.35,Math.PI/2);
    hammer(c,386,182,t,working);if(working){sparks(c,364,225,t,(t*1.8)%1);glow(c,365,225,65,'#ffb962',.35);}
    label(c,'锻造工坊',360,322,16);
    pedestal(c,590,241,'#bd78ef');aura(c,590,202,3,t,.6);label(c,'升级台',590,305,16,'#d8b4ff');
    rect(c,780,85,119,126,'#1c2833');poly(c,[[768,87],[841,38],[912,87]],'#738080');
    glow(c,840,136,52,'#c6dddb',.16);poly(c,[[786,208],[806,142],[826,192],[851,106],[891,208]],'#607582');
    label(c,'荒野之门',839,251,16,'#b4d1c5');
    const dt=Math.min(.04,(performance.now()-last)/1000);
    if(keys.size){hero.tx=Math.max(50,Math.min(910,hero.x+((keys.has('d')||keys.has('arrowright')?1:0)-(keys.has('a')||keys.has('arrowleft')?1:0))*220*dt));hero.ty=Math.max(320,Math.min(408,hero.y+((keys.has('s')||keys.has('arrowdown')?1:0)-(keys.has('w')||keys.has('arrowup')?1:0))*180*dt));}
    const dx=hero.tx-hero.x,dy=hero.ty-hero.y,dist=Math.hypot(dx,dy),step=Math.min(dist,200*dt);
    if(dist>1){hero.x+=dx/dist*step;hero.y+=dy/dist*step;}
    person(c,hero.x,hero.y,t,dist>2,dx<0?-1:1);
    // 前景花篮、矿石与灯光，遮挡关系丰富空间。
    for(let i=0;i<4;i++)poly(c,[[42+i*18,385],[48+i*18,369-i*3],[63+i*18,380],[59+i*18,396]],'#697982');
    rect(c,856,360,63,48,'#665047');rect(c,852,373,71,7,'#af8d62');rect(c,852,397,71,7,'#af8d62');
    label(c,'点击地面行走 · WASD / 方向键移动',480,433,13,'#c5b59e');
  }
  function upgradeScene(c,t) {
    rect(c,0,0,960,450,'#141b2b');const w=upgradeWeapon??data?.weapons[data.equipped],col=palette[w?.rarity??0];
    for(let i=0;i<7;i++)poly(c,[[i*160-80,280],[i*160,140],[i*160+90,280]],'#263149');
    for(let x=90;x<960;x+=195){rect(c,x,40,20,290,'#2c3347');rect(c,x-7,65,34,7,'#4a4d67');glow(c,x+10,140,70,col,.12);}
    poly(c,[[0,290],[960,290],[960,450],[0,450]],'#232538');
    pedestal(c,480,320,col);aura(c,480,220,w?.rarity??0,t);
    const active=upgrading>performance.now();
    blade(c,480,220,w,.95+Math.sin(t*2)*.02,Math.sin(t)*.06);
    if(active){c.save();c.strokeStyle=col;c.lineWidth=3;c.shadowColor=col;c.shadowBlur=15;
      for(let i=0;i<3;i++){c.beginPath();c.ellipse(480,225,80+i*32,110-i*10,t*2+i,0,Math.PI*2);c.stroke();}c.restore();sparks(c,480,250,t,(t*1.5)%1,col);}
    if(upgraded&&performance.now()-upgraded.time<4500){const age=(performance.now()-upgraded.time)/1000;
      if(upgraded.success){glow(c,480,215,190,col,Math.max(0,.65-age*.12));sparks(c,480,230,t,age,col);label(c,'强化成功 · 锋芒进阶',480,67,24,col);}
      else {label(c,upgraded.protected?'保护符生效 · 等级保留':'强化失败 · 调整后再试',480,67,22,'#e7a395');if(upgraded.protected){c.strokeStyle='#a6e5ec';c.lineWidth=4;c.beginPath();c.arc(480,220,125,0,Math.PI*2);c.stroke();}}
    } else label(c,'星纹升级台',480,65,23,'#e2d4f4');
    label(c,active?'符文正在重塑剑身…':'同一把剑，同一份品质光辉',480,420,15,'#b5adc9');
  }
  function enemy(c,x,y,t,b) {
    const theme=['#788e62','#9b8ed2','#9671b0','#d06c47','#94c4dc','#d4ba77'][b.area];
    c.save();c.translate(x,y);const scale=b.boss||b.elite?1.4:1;c.scale(scale,scale);
    oval(c,0,4,38,11,'#00000055');const bob=Math.sin(t*2.5)*3;
    if(b.boss||b.elite||b.enemy===1){poly(c,[[-30,-78],[-14,-107],[18,-103],[38,-75],[29,-11],[-27,-12]],theme);rect(c,-25,-14,19,21,'#3f4250');rect(c,9,-14,19,21,'#3f4250');rect(c,-42,-71,14,45,theme);rect(c,31,-71,15,45,theme);
      rect(c,-13,-79+bob,8,5,'#ffdf80');rect(c,9,-79+bob,8,5,'#ffdf80');poly(c,[[-12,-47],[0,-61],[13,-47],[0,-28]],'#f8d979');
      if(b.boss){poly(c,[[-30,-98],[-40,-133],[-8,-111],[4,-128],[20,-107],[42,-134],[34,-94]],'#c5ae87');glow(c,0,-45,60,theme,.25);}}
    else if(b.enemy===0){poly(c,[[-45,-20],[-34,-53],[-13,-71],[9,-65],[30,-47],[50,-25],[23,-5],[-22,-6]],theme);poly(c,[[20,-51],[25,-78],[39,-49]],theme);rect(c,23,-39,7,4,'#ffd180');rect(c,-32,-7,13,16,'#343a42');rect(c,22,-7,13,16,'#343a42');}
    else {oval(c,0,-37+bob,41,34,theme);poly(c,[[-37,-30],[0,-60],[37,-30],[23,-8],[-23,-8]],theme);rect(c,-13,-41+bob,6,6,'#162336');rect(c,9,-41+bob,6,6,'#162336');}
    c.restore();
  }
  function bar(c,x,y,value,max,col,name) {
    label(c,name,x,y-13,16,'#eee7da');rect(c,x-115,y,230,13,'#111928');rect(c,x-113,y+2,226*Math.max(0,value/max),9,col);label(c,`${value} / ${max}`,x,y+35,16);
  }
  function battle(c,t) {
    const b=data?.battle;if(!b)return;const themes=['#375044','#41405e','#392e4a','#583c36','#415a6b','#514b42'];
    const g=c.createLinearGradient(0,0,0,450);g.addColorStop(0,'#111d2a');g.addColorStop(1,themes[b.area]);rect(c,0,0,960,450,g);
    oval(c,780,85,43,43,'#d9e1de24');
    for(let i=0;i<11;i++){let x=i*110;poly(c,[[x-50,300],[x+30,90+(i*53)%140],[x+110,300]],themes[b.area]);if(b.area===0){rect(c,x+20,170,12,146,'#263a33');poly(c,[[x-10,210],[x+25,100],[x+70,210]],'#335446');}}
    poly(c,[[0,295],[960,295],[960,450],[0,450]],'#242a31');c.strokeStyle='#678a6d44';for(let i=0;i<24;i++){const x=(i*139)%960,y=324+(i*37)%112;c.beginPath();c.moveTo(x,y);c.lineTo(x+4,y-9);c.lineTo(x+9,y);c.stroke();}
    let shift=0,eshift=0;const elapsed=hit?(performance.now()-hit.time)/1000:9;
    if(elapsed<.55){const swing=Math.sin(elapsed/.55*Math.PI);shift=hit.action===0||hit.action===4?swing*110:0;eshift=elapsed>.22?-Math.sin((elapsed-.22)*9)*25:0;}
    person(c,250+shift,335,t,shift>0);enemy(c,704+eshift,337,t,b);
    if(elapsed<.9){const alpha=1-elapsed/.9;c.save();c.globalAlpha=alpha;
      if(hit.dealt){c.strokeStyle=hit.action===4?'#f9dd86':'#cfecff';c.lineWidth=hit.action===4?9:4;c.shadowColor=c.strokeStyle;c.shadowBlur=20;c.beginPath();c.arc(690,270,70,-1.5,1.2);c.stroke();label(c,`-${hit.dealt}`,706,205-elapsed*35,26,'#ffe093');sparks(c,700,282,t,elapsed,'#dcedff');}
      if(hit.taken)label(c,`-${hit.taken}`,250,225-elapsed*35,24,'#ff999a');
      if(hit.healed){glow(c,250,286,85,'#6efca8',.5);label(c,`+${hit.healed}`,250,215-elapsed*35,24,'#9cffbc');}
      if(hit.action===1){c.strokeStyle='#9acfed';c.lineWidth=5;c.beginPath();c.arc(255,291,53,-1.5,1.5);c.stroke();}c.restore();}
    const name=b.elite?'秘境精英':b.boss?data.areas[b.area].boss:data.areas[b.area].enemies[b.enemy];
    bar(c,250,65,data.hp,data.maxHp,'#79d3b1','铸剑师');bar(c,705,65,b.hp,b.maxHp,b.boss?'#c383eb':'#e29387',name);
    label(c,`第 ${b.turn+1} 回合 · ${data.areas[b.area].name}`,480,425,15,'#c7b9a5');
  }
  function reveal(c,t,w){rect(c,0,0,960,450,'#141b2b');aura(c,480,207,w.rarity,t,1.5);pedestal(c,480,349,palette[w.rarity]);blade(c,480,215+Math.sin(t*2)*7,w,1.2,Math.sin(t*.8)*.04);}
  function frame(now) {
    const t=now/1000;
    for(const [canvas,s] of scenes) {
      if(!canvas.isConnected){scenes.delete(canvas);continue;}
      if(!canvas.getClientRects().length||document.hidden)continue;
      const c=s.ctx;c.save();c.clearRect(0,0,960,450);
      if(s.type==='town')town(c,t);if(s.type==='upgrade')upgradeScene(c,t);if(s.type==='battle'&&data)battle(c,t);if(s.type==='reveal')reveal(c,t,s.weapon);
      c.restore();
    }
    last=now;requestAnimationFrame(frame);
  }
  function attach(canvas,type,weapon) {
    if(!canvas)return;canvas.width=960;canvas.height=450;scenes.set(canvas,{ctx:canvas.getContext('2d'),type,weapon});
    if(type==='town'&&!canvas.dataset.walkable){canvas.dataset.walkable='true';
      canvas.addEventListener('pointerdown',e=>{const r=canvas.getBoundingClientRect(),x=(e.clientX-r.left)/r.width*960,y=(e.clientY-r.top)/r.height*450;hero.tx=Math.max(50,Math.min(910,x));hero.ty=Math.max(320,Math.min(408,y));
        if(y<305){const target=x>740?'explore':x>490?'upgrade':x>255?'forge':null;if(target)window.dispatchEvent(new CustomEvent('station',{detail:target}));}});
    }
  }
  document.addEventListener('keydown',e=>{if(['INPUT','SELECT','TEXTAREA'].includes(e.target.tagName)||document.querySelector('dialog[open]'))return;const k=e.key.toLowerCase();if(['w','a','s','d','arrowup','arrowleft','arrowdown','arrowright'].includes(k)){keys.add(k);if(k.startsWith('arrow'))e.preventDefault();}});
  document.addEventListener('keyup',e=>keys.delete(e.key.toLowerCase()));window.addEventListener('blur',()=>keys.clear());
  requestAnimationFrame(frame);
  return {attach,update:s=>{data=s;},forge:()=>{forging=performance.now()+1800;},
    upgrade:(w)=>{upgradeWeapon={...w};upgraded=null;},
    upgrading:()=>{upgrading=performance.now()+1800;},
    upgraded:(w,success,protectedUsed)=>{upgradeWeapon={...w};upgraded={success,protected:protectedUsed,time:performance.now()};},
    hit:(report,action)=>{hit={...report,action,time:performance.now()};}};
})();
