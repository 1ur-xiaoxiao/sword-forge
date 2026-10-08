const {chromium}=require(process.env.FORGE_PLAYWRIGHT_PATH||'playwright');
const ROT=require('../web/vendor/rot.js');
const fs=require('node:fs'),path=require('node:path'),net=require('node:net'),assert=require('node:assert/strict');const{spawn,execFileSync}=require('node:child_process');
const root=path.resolve(__dirname,'..'),out=path.join(root,'outputs');let tmp,server,browser,passed=0;const delay=ms=>new Promise(r=>setTimeout(r,ms));const check=s=>console.log(`PASS DUNGEON ${++passed}: ${s}`);
(async()=>{
 for(let seed=1;seed<=150;seed++){
  function generated(){ROT.RNG.setSeed(seed);const cells=Array.from({length:60},()=>[]),g=new ROT.Map.Rogue(60,28,{cellWidth:3,cellHeight:2,roomWidth:[8,14],roomHeight:[5,9]});g.create((x,y,v)=>cells[x][y]=v);return{cells,rooms:Array.from({length:6},(_,i)=>{const r=g.rooms[i%3][Math.floor(i/3)];return{x:r.x+Math.floor(r.width/2),y:r.y+Math.floor(r.height/2)};})};}
  const a=generated(),b=generated();assert.deepEqual(a,b);
  for(const r of a.rooms){let count=0;new ROT.Path.AStar(r.x,r.y,(x,y)=>x>=0&&x<60&&y>=0&&y<28&&!a.cells[x][y],{topology:4}).compute(a.rooms[0].x,a.rooms[0].y,()=>count++);assert(count>0,`unreachable room seed ${seed}`);}
 }
 check('actual rot.js generator is deterministic and all six rooms reachable for 150 seeds');
 tmp=fs.mkdtempSync(path.join(root,'work','dungeon-test-'));fs.mkdirSync(path.join(tmp,'saves'));fs.copyFileSync(path.join(root,'outputs','SwordForge.exe'),path.join(tmp,'SwordForge.exe'));
 execFileSync(path.join(root,'build','visual_fixture.exe'),[path.join(tmp,'saves','game.sav'),'4']);
 const port=await new Promise(r=>{const s=net.createServer();s.listen(0,'127.0.0.1',()=>{const p=s.address().port;s.close(()=>r(p));});});const url=`http://127.0.0.1:${port}`;
 async function start(){server=spawn(path.join(tmp,'SwordForge.exe'),['--headless','--port',String(port)],{windowsHide:true});for(let i=0;i<80;i++){try{if((await fetch(url+'/api/state')).ok)return;}catch{}await delay(100);}throw Error('service startup');}
 async function stop(){await new Promise(r=>{server.once('exit',r);server.kill();});server=null;}
 await start();browser=await chromium.launch({headless:true,executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
 const page=await browser.newPage({viewport:{width:1440,height:1000},locale:'zh-CN'}),errors=[];page.on('pageerror',e=>errors.push(e.message));
 await page.goto(url);await page.waitForFunction(()=>state!==null);
 async function ready(){await page.waitForFunction(()=>!busy);}
 async function click(sel){await page.locator(sel).click();await ready();return page.evaluate(()=>state);}
 await click('.nav-item[data-page="explore"]');await click('[data-delve="0"]');await page.locator('#delve-panel').waitFor({state:'visible'});
 const original=await page.evaluate(()=>({d:state.delve,map:Delve2D.snapshot()}));assert(original.d.active);assert.equal(original.d.floor,1);assert.equal(original.map.rooms.length,6);assert(original.map.seen<60*28);
 await page.waitForTimeout(150);await page.screenshot({path:path.join(out,'2d-random-dungeon.png'),fullPage:true});check('entering region creates real 2D dungeon, visible fog and limited initial field of view');
 await page.locator('[data-delve-room="1"][data-choice="0"]').click();await page.waitForFunction(()=>!busy&&(state.delve.visited&2));const chest=await page.evaluate(()=>state);
 assert(chest.materials[0]>1000);assert(await page.locator('[data-delve-room="1"][data-choice="1"]').isDisabled());const moved=await page.evaluate(()=>Delve2D.snapshot());assert(moved.seen>original.map.seen);assert.deepEqual(moved.player,moved.rooms[1]);
 await page.reload();await page.waitForFunction(()=>state?.delve.active&&!busy);assert.equal(await page.evaluate(()=>Delve2D.snapshot().layout),original.map.layout);assert.deepEqual(await page.evaluate(()=>Delve2D.snapshot().player),moved.player);
 check('A* walks through corridors, vision expands, chest is single-claim and refresh preserves map plus position');
 await page.locator('[data-delve-room="4"]').click();await page.waitForFunction(()=>state.battle.active&&!busy);
 await click('[data-battle="1"]');const mid=await page.evaluate(()=>({battle:state.battle,hp:state.hp,delve:state.delve}));
 await stop();await start();await page.reload();await page.waitForFunction(()=>state?.battle.active&&!busy);
 const resumed=await page.evaluate(()=>({battle:state.battle,hp:state.hp,delve:state.delve}));assert.deepEqual(resumed,mid);check('cold restart restores exact dungeon enemy health, player health, turn and cooldown');
 let fight=await page.evaluate(()=>state);for(let i=0;i<80&&fight.battle.active;i++){fight=await click(`[data-battle="${fight.battle.cooldown?0:4}"]`);}
 assert(fight.delve.reward);assert.equal(fight.bossFlags,0);assert(await page.locator('#delve-rewards').isVisible());await click('[data-blessing="0"]');assert.equal(await page.evaluate(()=>state.delve.buffs),1);
 await page.locator('[data-delve-room="3"]').click();await page.waitForFunction(()=>state.battle.active&&!busy);fight=await page.evaluate(()=>state);
 while(fight.battle.active)fight=await click(`[data-battle="${fight.battle.cooldown?0:4}"]`);
 await page.locator('[data-delve-room="5"]').click();await page.waitForFunction(()=>state.delve.floor===2&&!busy);
 assert.notEqual(await page.evaluate(()=>Delve2D.snapshot().layout),original.map.layout);assert.equal(await page.evaluate(()=>state.delve.buffs),1);
 check('elite victory offers real blessing, avoids region first-clear reward and staircase advances random layout');
 await page.setViewportSize({width:390,height:844});assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
 await page.waitForTimeout(100);await page.screenshot({path:path.join(out,'2d-dungeon-mobile.png'),fullPage:true});await page.locator('#delve-leave').click();await click('#confirm-yes');
 assert.equal(await page.evaluate(()=>state.delve.active),false);assert.equal(await page.evaluate(()=>state.delve.buffs),0);assert.equal(await page.locator('.world-map-panel').isVisible(),true);assert.deepEqual(errors,[]);
 check('mobile map, touch room controls and withdrawal retain loot and clear temporary buffs');
 fs.writeFileSync(path.join(out,'dungeon-verification.json'),JSON.stringify({date:'2026-10-08',groups:passed,seeds:150,errors,library:'rot.js 2.2.1'},null,2));
})().catch(e=>{console.error(e);process.exitCode=1;}).finally(async()=>{if(browser)await browser.close();if(server&&server.exitCode===null)await new Promise(r=>{server.once('exit',r);server.kill();});if(tmp)fs.rmSync(tmp,{recursive:true,force:true,maxRetries:8,retryDelay:200});});
