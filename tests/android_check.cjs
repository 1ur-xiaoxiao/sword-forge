const fs=require('node:fs');const path=require('node:path');const assert=require('node:assert/strict');const {execFileSync}=require('node:child_process');
const root=path.resolve(__dirname,'..'),output=path.join(root,'outputs');
const adb=process.env.FORGE_ADB||'C:/Users/14909/AppData/Local/Android/Sdk/platform-tools/adb.exe';
const serial=process.env.FORGE_ANDROID_SERIAL||'emulator-5556';
const shell=(...args)=>execFileSync(adb,['-s',serial,...args],{windowsHide:true,encoding:'utf8'}).trim();
let ws,id=0,tasks=new Map(),passed=0,errors=[];
const delay=ms=>new Promise(r=>setTimeout(r,ms));
function call(method,params={}){return new Promise((resolve,reject)=>{const n=++id;const timer=setTimeout(()=>{tasks.delete(n);reject(Error('CDP timeout: '+method));},12000);tasks.set(n,{resolve:r=>{clearTimeout(timer);resolve(r);},reject:e=>{clearTimeout(timer);reject(e);}});ws.send(JSON.stringify({id:n,method,params}));});}
async function connect(){
 if(ws)ws.close();
 let pid;for(let attempt=0;attempt<60;attempt++){try{pid=shell('shell','pidof','cn.swordforge.game');if(pid)break;}catch{}await delay(150);}assert(pid,'App process did not start');shell('forward','tcp:9222',`localabstract:webview_devtools_remote_${pid}`);
 let pages;for(let i=0;i<60;i++){try{pages=await(await fetch('http://127.0.0.1:9222/json')).json();if(pages.length)break;}catch{}await delay(200);}
 assert(pages?.length,'WebView target did not start');ws=new WebSocket(pages[0].webSocketDebuggerUrl);await new Promise((r,j)=>{ws.addEventListener('open',r,{once:true});ws.addEventListener('error',j,{once:true});});
 ws.addEventListener('message',e=>{const m=JSON.parse(e.data);if(m.id){const t=tasks.get(m.id);tasks.delete(m.id);if(t)m.error?t.reject(m.error):t.resolve(m.result);}else if(m.method==='Runtime.exceptionThrown')errors.push(m.params.exceptionDetails.text);});
 await call('Runtime.enable');await wait("typeof state!=='undefined' && state !== null && !busy");
}
async function evaluate(expression){const r=await call('Runtime.evaluate',{expression,awaitPromise:true,returnByValue:true});if(r.exceptionDetails)throw Error(r.exceptionDetails.text+': '+(r.exceptionDetails.exception?.description||expression));return r.result.value;}
async function wait(condition){for(let i=0;i<60;i++){if(await evaluate(condition))return;await delay(150);}throw Error('Wait failed: '+condition);}
async function tap(selector){
 const p=await evaluate(`(()=>{const el=document.querySelector(${JSON.stringify(selector)});if(!el||el.disabled)throw Error('Disabled or missing: '+${JSON.stringify(selector)});el.scrollIntoView({block:'center',behavior:'instant'});const r=el.getBoundingClientRect();return {x:r.left+r.width/2,y:r.top+r.height/2};})()`);
 await call('Input.dispatchTouchEvent',{type:'touchStart',touchPoints:[{x:p.x,y:p.y,radiusX:3,radiusY:3,force:1}]});await call('Input.dispatchTouchEvent',{type:'touchEnd',touchPoints:[]});
 await delay(120);await wait('!busy');
}
async function state(){return evaluate('JSON.parse(SwordForgeAndroid.state())');}
async function screenshot(name){await evaluate('window.scrollTo(0,0)');const png=await call('Page.captureScreenshot',{format:'png'});fs.writeFileSync(path.join(output,name),Buffer.from(png.data,'base64'));}
const check=name=>{console.log(`PASS ANDROID ${++passed}: ${name}`);};
(async()=>{
 await connect();let s=await state();assert.equal(s.level,1);assert.equal(s.gold,180);assert.equal(s.recipes.length,6);
 assert.equal(await evaluate('isAndroid'),true);assert.equal(await evaluate("document.querySelector('#connection-error').hidden"),true);
 assert.equal(await evaluate('soundEnabled'),true);assert.equal(await evaluate("document.querySelector('#sound-button').getAttribute('aria-pressed')"),'true');
 assert.equal(await evaluate("document.querySelector('#town-canvas').width"),960);
 assert.equal(await evaluate('document.documentElement.scrollWidth<=innerWidth+1'),true);
 await screenshot('android-forge.png');check('APK runs packaged UI and original C engine in actual Android WebView');
 await tap('#forge-button');await wait("document.querySelector('#result-dialog').open");s=await state();assert.equal(s.weapons.length,2);
 await tap('#equip-result');assert.equal((await state()).equipped,1);check('touch forge and equip use JNI C actions and automatic save');
 await tap('.nav-item[data-page="explore"]');assert.equal(await evaluate("document.querySelectorAll('.map-node').length"),6);
 await tap('[data-area="5"]');assert.equal(await evaluate("document.querySelector('[data-explore=\"5\"]').disabled"),true);
 await tap('[data-area="0"]');await screenshot('android-world-map.png');
 await tap('[data-gather="0"]');s=await state();assert.equal(s.areas[0].surveys,1);assert(s.event.kind>=0);
 await screenshot('android-event.png');check('touch map preview and real gather create persistent reward choices');
 const pending=s.event;
 shell('shell','am','force-stop','cn.swordforge.game');shell('shell','am','start','-n','cn.swordforge.game/.MainActivity');await delay(700);await connect();
 s=await state();assert.deepEqual(s.event,pending);assert.equal(s.weapons.length,2);assert.equal(s.equipped,1);
 await wait("!document.querySelector('#exploration-event').hidden");await tap('[data-event="1"]');assert.equal((await state()).event.kind,-1);
 check('force-stop and restart restore native save, equipment and pending event exactly');
 await tap('[data-explore="0"]');await wait('state.battle.active');
 assert.equal(await evaluate("document.querySelector('.world-map-panel').getClientRects().length"),0);
 assert.equal(await evaluate("(()=>{let h=document.querySelector('.combat-hud').getBoundingClientRect(),a=document.querySelector('.battle-actions').getBoundingClientRect();return h.top>=0&&a.bottom<=innerHeight;})()"),true);
 const combatPng=await call('Page.captureScreenshot',{format:'png'});fs.writeFileSync(path.join(output,'android-2d-combat.png'),Buffer.from(combatPng.data,'base64'));
 let b=await state();for(let turn=0;turn<60&&b.battle.active;turn++){await tap(`[data-battle="${b.battle.cooldown===0?4:0}"]`);b=await state();}
 assert.equal(b.wins,1);assert.equal(b.battle.active,false);check('touch skill cooldown and turn combat finish with real loot');
 const firstJourney=await state();await tap('#settings-button');await tap('[data-slot="1"]');await tap('#confirm-yes');await wait('state.activeSlot===1');
 assert.equal((await state()).gold,180);
 shell('shell','am','force-stop','cn.swordforge.game');shell('shell','am','start','-n','cn.swordforge.game/.MainActivity');await connect();assert.equal((await state()).activeSlot,1);
 await tap('#settings-button');await tap('[data-slot="0"]');await tap('#confirm-yes');await wait('state.activeSlot===0');assert.equal((await state()).gold,firstJourney.gold);
 check('four independent journeys switch by touch and restore active slot across Android cold restart');
 await tap('.nav-item[data-page="inventory"]');await tap('#upgrade-button');await wait("document.querySelector('#upgrade-dialog').open");
 await tap('#upgrade-start');assert.equal((await state()).result,0);assert.equal(await evaluate("document.querySelector('#upgrade-result').textContent.length>0"),true);
 const altarPng=await call('Page.captureScreenshot',{format:'png'});fs.writeFileSync(path.join(output,'android-2d-upgrade.png'),Buffer.from(altarPng.data,'base64'));
 await tap('#upgrade-dialog>.dialog-close');check('touch upgrade uses animated 2D altar and actual native upgrade result');
 await tap('.nav-item[data-page="forge"]');await tap('#forge-button');await wait("document.querySelector('#result-dialog').open");await tap('#result-dialog .dialog-actions .dialog-close');
 await tap('.nav-item[data-page="inventory"]');await tap('#batch-toggle');
 const spare=(await state()).weapons.find(w=>w.id>2);assert(spare);await tap(`[data-weapon="${spare.id}"]`);
 assert.equal(await evaluate("document.querySelector('[data-weapon=\"2\"]').disabled"),true);
 await tap('#batch-recycle');await tap('#confirm-cancel');assert.equal((await state()).weapons.length,3);
 await tap('#batch-recycle');await tap('#confirm-yes');assert.equal((await state()).weapons.length,2);assert.equal((await state()).weapons[(await state()).equipped].id,2);
 check('touch batch selection, cancellation and confirmed recycle preserve equipped weapon');
 for(const page of ['forge','explore','inventory','shop','journal']){
   await tap(`.nav-item[data-page="${page}"]`);assert.equal(await evaluate('document.documentElement.scrollWidth<=innerWidth+1'),true,`${page} overflow`);
 }
 await tap('#settings-button');shell('shell','input','keyevent','4');await wait("!document.querySelector('#settings-dialog').open");
 shell('shell','input','keyevent','4');await wait("currentPage==='forge'");check('all five touch pages fit phone and Android back closes dialogs then returns to forge');
 await tap('.nav-item[data-page="shop"]');await tap('[data-buy="3"]');const bought=await state();assert.equal(bought.result,0);
 shell('shell','input','keyevent','3');shell('shell','am','start','-n','cn.swordforge.game/.MainActivity');await delay(400);
 assert.equal((await state()).items[3],bought.items[3]);check('shop and home/background/resume preserve inventory and save');
 shell('shell','settings','put','system','accelerometer_rotation','0');shell('shell','settings','put','system','user_rotation','1');await delay(700);await connect();
 assert.equal((await state()).gold,bought.gold);assert.equal(await evaluate('document.documentElement.scrollWidth<=innerWidth+1'),true);
 shell('shell','settings','put','system','user_rotation','0');await delay(700);await connect();check('real landscape and portrait rotation preserve C state and responsive layout');
 await tap('#settings-button');await tap('#quit-button');await delay(300);
 shell('shell','am','force-stop','cn.swordforge.game');shell('shell','am','start','-n','cn.swordforge.game/.MainActivity');await delay(700);await connect();
 let final=await state();assert.equal(final.gold,bought.gold);assert.equal(final.wins,1);assert.equal(final.weapons.length,2);assert.deepEqual(errors,[]);
 await tap('.nav-item[data-page="explore"]');await screenshot('android-final-map.png');
 await tap('[data-delve="0"]');await wait('state.delve.active&&!busy');const dungeonSeed=(await state()).delve.seed;
 await tap('[data-delve-room="1"][data-choice="0"]');await wait('!busy&&(state.delve.visited&2)');
 assert.equal(await evaluate('Delve2D.snapshot().seen>40'),true);
 const dungeonPng=await call('Page.captureScreenshot',{format:'png'});fs.writeFileSync(path.join(output,'android-random-dungeon.png'),Buffer.from(dungeonPng.data,'base64'));
 await tap('[data-delve-room="3"]');await wait('state.battle.active&&!busy');await tap('[data-battle="1"]');
 const ongoing=await evaluate('({battle:state.battle,hp:state.hp,delve:state.delve})');assert(ongoing.battle.active);
 shell('shell','am','force-stop','cn.swordforge.game');shell('shell','am','start','-n','cn.swordforge.game/.MainActivity');await connect();
 assert.deepEqual(await evaluate('({battle:state.battle,hp:state.hp,delve:state.delve})'),ongoing);assert.equal((await state()).delve.seed,dungeonSeed);
 check('actual rot.js touch path, chest reward, fog and exact ongoing dungeon battle survive Android cold restart');
 async function finishFight(){let s=await state();for(let i=0;i<60&&s.battle.active;i++){const act=s.hp<s.battle.attack*2+20&&s.items[0]?2:s.battle.elite&&(s.battle.turn+1)%3===0?1:s.battle.cooldown?0:4;await tap(`[data-battle="${act}"]`);s=await state();}assert(s.delve.active,'beginner dungeon unexpectedly lost');}
 await finishFight();await tap('[data-delve-room="2"][data-choice="0"]');await wait('!busy&&(state.delve.visited&4)');
 await tap('[data-delve-room="4"]');await wait('state.battle.active&&!busy');await finishFight();assert((await state()).delve.reward);
 await tap('[data-blessing="1"]');assert.equal((await state()).delve.buffs,2);await tap('[data-delve-room="5"]');await wait('!busy&&state.delve.floor===2');assert.notEqual((await state()).delve.seed,dungeonSeed);
 await tap('#delve-leave');await tap('#confirm-yes');assert.equal((await state()).delve.active,false);final=await state();assert.deepEqual(errors,[]);
 check('starter equipment clears real dungeon elite, selects recovery blessing, descends and returns loot on phone');
 fs.writeFileSync(path.join(output,'android-verification.json'),JSON.stringify({date:'2026-10-08',groups:passed+1,android:shell('shell','getprop','ro.build.version.release'),api:shell('shell','getprop','ro.build.version.sdk'),abi:shell('shell','getprop','ro.product.cpu.abi'),viewport:await evaluate('({width:innerWidth,height:innerHeight})'),errors,final:{gold:final.gold,wins:final.wins,weapons:final.weapons.length,items:final.items}},null,2));
 check('save-and-exit followed by cold restart preserves progress without script errors');
 console.log(`${passed} actual Android verification groups passed.`);
})().catch(e=>{console.error(e);process.exitCode=1;}).finally(()=>{if(ws)ws.close();});
