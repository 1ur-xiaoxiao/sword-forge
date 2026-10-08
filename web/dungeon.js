'use strict';
// 地图生成、A* 寻路和精确阴影视野直接使用 rot.js；经济与房间奖励由 C 验证。
window.Delve2D=(()=>{
  const W=60,H=28,T=40,names=['入口','封存宝箱','疗愈清泉','矿道守卫','秘境精英','下层阶梯'];
  let canvas,ctx,state=null,map=null,rooms=[],seed=0,player={x:0,y:0},path=[],queued=null,visible=new Set(),seen=new Set(),lastStep=0,paused=true,storageKey='';
  let fov,mapSlot=-1;
  function light(){visible.clear();fov.compute(player.x,player.y,8,(x,y)=>{visible.add(`${x},${y}`);seen.add(`${x},${y}`);});}
  function persist(){if(!storageKey)return;try{localStorage.setItem(storageKey,JSON.stringify({x:player.x,y:player.y,seen:[...seen]}));}catch{}}
  function generate(d,slot){
    const old=ROT.RNG.getState();ROT.RNG.setSeed(d.seed);
    map=Array.from({length:W},()=>Array(H).fill(1));
    const gen=new ROT.Map.Rogue(W,H,{cellWidth:3,cellHeight:2,roomWidth:[8,14],roomHeight:[5,9]});
    gen.create((x,y,v)=>map[x][y]=v);ROT.RNG.setState(old);
    // Rogue 的房间数组在锁定的发行代码中公开可读；更新依赖时做连通性验收。
    rooms=Array.from({length:6},(_,i)=>{const r=gen.rooms[i%3][Math.floor(i/3)];return {x:r.x+Math.floor(r.width/2),y:r.y+Math.floor(r.height/2)};});
    seed=d.seed;mapSlot=slot;storageKey=`swordforge-delve-${slot}-${seed}`;seen.clear();path=[];queued=null;
    player={...rooms[d.room||0]};
    try{const p=JSON.parse(localStorage.getItem(storageKey));if(p&&Number.isInteger(p.x)&&Number.isInteger(p.y)&&p.x>=0&&p.x<W&&p.y>=0&&p.y<H&&!map[p.x][p.y]){player={x:p.x,y:p.y};if(Array.isArray(p.seen))seen=new Set(p.seen.filter(v=>typeof v==='string').slice(0,W*H));}}catch{}
    fov=new ROT.FOV.PreciseShadowcasting((x,y)=>x>=0&&x<W&&y>=0&&y<H&&!map[x][y],{topology:4});light();
  }
  function walk(x,y,action=null){
    if(paused||!map||x<0||x>=W||y<0||y>=H||map[x][y])return;
    const points=[];new ROT.Path.AStar(x,y,(a,b)=>a>=0&&a<W&&b>=0&&b<H&&!map[a][b],{topology:4}).compute(player.x,player.y,(a,b)=>points.push({x:a,y:b}));
    if(!points.length)return;path=points.slice(1);queued=action;
    if(!path.length&&queued){const q=queued;queued=null;window.dispatchEvent(new CustomEvent('delve-arrival',{detail:q}));}
  }
  function camera(){return {x:Math.max(0,Math.min(W*T-960,player.x*T-460)),y:Math.max(0,Math.min(H*T-450,player.y*T-205))};}
  function text(s,x,y,size=16,color='#eee3cd'){ctx.fillStyle=color;ctx.font=`${size}px "Microsoft YaHei",sans-serif`;ctx.textAlign='center';ctx.fillText(s,x,y);}
  function draw(now){
    if(canvas&&canvas.isConnected&&canvas.getClientRects().length&&!document.hidden&&map){
      if(path.length&&!paused&&now-lastStep>55){player=path.shift();lastStep=now;light();if(!path.length){persist();if(queued){const q=queued;queued=null;window.dispatchEvent(new CustomEvent('delve-arrival',{detail:q}));}}}
      ctx.clearRect(0,0,960,450);ctx.fillStyle='#111722';ctx.fillRect(0,0,960,450);const cam=camera(),theme=['#566458','#6d597a','#625072','#795b4a','#5d7583','#7c7056'][state.delve.area];
      for(let x=Math.floor(cam.x/T);x<Math.min(W,Math.ceil((cam.x+960)/T));x++)for(let y=Math.floor(cam.y/T);y<Math.min(H,Math.ceil((cam.y+450)/T));y++){
        const k=`${x},${y}`;if(!seen.has(k))continue;const sx=x*T-cam.x,sy=y*T-cam.y,lit=visible.has(k);ctx.globalAlpha=lit?1:.28;
        if(map[x][y]){ctx.fillStyle=theme;ctx.fillRect(sx,sy,T-1,T-1);ctx.fillStyle='#aab6b522';ctx.fillRect(sx,sy,T-1,5);ctx.fillStyle='#00000044';ctx.fillRect(sx,sy+T-5,T-1,5);
          if(lit&&(x*7+y*3)%19===0){ctx.fillStyle='#603f2d';ctx.fillRect(sx+17,sy+20,5,13);ctx.fillStyle='#ffc47c';ctx.shadowColor='#ed933f';ctx.shadowBlur=22;ctx.fillRect(sx+14,sy+12+Math.sin(now/130+x)*2,11,12);ctx.shadowBlur=0;}
        }else{ctx.fillStyle=(x+y)%2?'#303139':'#35363b';ctx.fillRect(sx,sy,T,T);ctx.fillStyle='#a9a69618';ctx.fillRect(sx+3,sy+3,2,2);}
      }
      ctx.globalAlpha=1;
      for(let i=0;i<6;i++){const r=rooms[i],k=`${r.x},${r.y}`;if(!seen.has(k))continue;const x=r.x*T+T/2-cam.x,y=r.y*T+T/2-cam.y,done=!!(state.delve.visited&(1<<i));ctx.globalAlpha=done?.45:1;
        if(i===1){ctx.fillStyle='#aa7e4b';ctx.fillRect(x-15,y-10,30,22);ctx.fillStyle='#efd198';ctx.fillRect(x-17,y-12,34,6);ctx.fillRect(x-3,y-4,6,9);}
        else if(i===2){ctx.fillStyle='#79bbcf';ctx.beginPath();ctx.ellipse(x,y+3,20,11,0,0,Math.PI*2);ctx.fill();ctx.strokeStyle='#b2e9e5';ctx.beginPath();ctx.ellipse(x,y+Math.sin(now/350)*3,13,5,0,0,Math.PI*2);ctx.stroke();}
        else if(i===3||i===4){ctx.fillStyle=i===4?'#ae79dc':'#8c9b70';ctx.fillRect(x-12,y-20,24,28);ctx.fillStyle='#ffda88';ctx.fillRect(x-7,y-13,4,4);ctx.fillRect(x+4,y-13,4,4);if(i===4){ctx.fillStyle='#d9a5ff';ctx.fillRect(x-15,y-26,6,9);ctx.fillRect(x+9,y-26,6,9);}}
        else if(i===5){ctx.fillStyle='#aeafa6';for(let a=0;a<4;a++)ctx.fillRect(x-17+a*3,y-12+a*6,34-a*6,4);}
        else {ctx.strokeStyle='#83c4a5';ctx.lineWidth=3;ctx.strokeRect(x-16,y-16,32,32);}
        text(done?'已探索':names[i],x,y+35,13);ctx.globalAlpha=1;
      }
      const px=player.x*T+T/2-cam.x,py=player.y*T+T/2-cam.y,bob=path.length?Math.sin(now/60)*2:0;
      ctx.fillStyle='#00000066';ctx.beginPath();ctx.ellipse(px,py+13,13,5,0,0,Math.PI*2);ctx.fill();ctx.fillStyle='#387f85';ctx.fillRect(px-10,py-7+bob,20,20);ctx.fillStyle='#d4aa80';ctx.fillRect(px-8,py-22+bob,16,16);ctx.fillStyle='#4c3642';ctx.fillRect(px-10,py-25+bob,20,8);ctx.fillStyle='#b9d4e6';ctx.fillRect(px+13,py-13+bob,3,24);
      // 右上角小地图提供方向，未见过的房间保留黑色。
      ctx.fillStyle='#0e1526e6';ctx.fillRect(746,12,200,104);
      for(let x=0;x<W;x++)for(let y=0;y<H;y++)if(seen.has(`${x},${y}`)){ctx.fillStyle=map[x][y]?'#586276':'#adac96';ctx.fillRect(756+x*3,21+y*3,3,3);}
      ctx.fillStyle='#f4d083';ctx.fillRect(756+player.x*3,21+player.y*3,4,4);text(path.length?'正在沿走廊前进…':'点击地面移动 · 点选房间寻找奖励',480,433,15,'#ead4b7');
    }
    requestAnimationFrame(draw);
  }
  function attach(el){canvas=el;canvas.width=960;canvas.height=450;ctx=canvas.getContext('2d');canvas.addEventListener('pointerdown',e=>{if(!map)return;const r=canvas.getBoundingClientRect(),cam=camera();walk(Math.floor(((e.clientX-r.left)/r.width*960+cam.x)/T),Math.floor(((e.clientY-r.top)/r.height*450+cam.y)/T));});}
  function room(i,q=0){if(!rooms[i])return;walk(rooms[i].x,rooms[i].y,{room:i,choice:q,seed});}
  function move(dx,dy){if(paused)return;queued=null;path=[];walk(player.x+dx,player.y+dy);}
  document.addEventListener('keydown',e=>{if(paused||['INPUT','SELECT'].includes(e.target.tagName)||document.querySelector('dialog[open]'))return;const dirs={w:[0,-1],a:[-1,0],s:[0,1],d:[1,0],ArrowUp:[0,-1],ArrowDown:[0,1],ArrowLeft:[-1,0],ArrowRight:[1,0]};if(dirs[e.key]){e.preventDefault();move(...dirs[e.key]);}});
  document.addEventListener('visibilitychange',()=>{if(document.hidden)persist();});requestAnimationFrame(draw);
  return {attach,room,move,persist,update:s=>{state=s;paused=!s.delve.active||s.battle.active||s.delve.reward||window.gameBusyForDelve||!document.querySelector('#page-explore.active');if(s.delve.active&&(s.delve.seed!==seed||mapSlot!==s.activeSlot))generate(s.delve,s.activeSlot);},
    snapshot:()=>({seed,player:{...player},rooms:rooms.map(r=>({...r})),seen:seen.size,walking:path.length>0,layout:map?map.map(c=>c.join('')).join('|'):''})};
})();
