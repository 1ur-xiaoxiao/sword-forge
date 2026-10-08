/* 真正启动独立 EXE、读 C 存档、点击 UI；不修改玩家文件。 */
const {chromium}=require(process.env.FORGE_PLAYWRIGHT_PATH||'playwright');
const fs=require('node:fs'),path=require('node:path'),net=require('node:net'),assert=require('node:assert/strict');
const {spawn,execFileSync}=require('node:child_process');
const root=path.resolve(__dirname,'..'),out=path.join(root,'outputs');
const delay=ms=>new Promise(r=>setTimeout(r,ms));let browser,server,tmp,passed=0;
const check=s=>console.log(`PASS 2D ${++passed}: ${s}`);
(async()=>{
 browser=await chromium.launch({headless:true,executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
 const errors=[];
 for(const rarity of [3,4]){
  tmp=fs.mkdtempSync(path.join(root,'work','scene-test-'));fs.mkdirSync(path.join(tmp,'saves'));
  fs.copyFileSync(path.join(root,'outputs','SwordForge.exe'),path.join(tmp,'SwordForge.exe'));
  execFileSync(path.join(root,'build','visual_fixture.exe'),[path.join(tmp,'saves','game.sav'),String(rarity)]);
  const port=await new Promise(r=>{const s=net.createServer();s.listen(0,'127.0.0.1',()=>{const p=s.address().port;s.close(()=>r(p));});});
  server=spawn(path.join(tmp,'SwordForge.exe'),['--headless','--port',String(port)],{windowsHide:true});const url=`http://127.0.0.1:${port}`;
  for(let i=0;i<80;i++){try{if((await fetch(url+'/api/state')).ok)break;}catch{}await delay(100);}
  const page=await browser.newPage({viewport:{width:1440,height:1000},locale:'zh-CN'});page.on('pageerror',e=>errors.push(e.message));
  await page.goto(url);await page.waitForFunction(()=>state!==null);
  assert.equal(await page.locator('#sound-button').getAttribute('aria-pressed'),'true');
  await page.locator('#sound-button').click();assert.equal(await page.locator('#sound-button').getAttribute('aria-pressed'),'false');
  await page.reload();await page.waitForFunction(()=>state!==null);assert.equal(await page.locator('#sound-button').getAttribute('aria-pressed'),'false');
  await page.locator('#sound-button').click();assert.equal(await page.evaluate(()=>audioContext.state),'running');
  check(`default sound, persistent mute and gesture audio unlock (${rarity})`);
  // Clicking the actual 2D station opens the altar, without bypassing the UI.
  const box=await page.locator('#town-canvas').boundingBox();await page.mouse.click(box.x+box.width*590/960,box.y+box.height*241/450);
  await page.locator('#upgrade-dialog').waitFor({state:'visible'});await page.locator('#upgrade-dialog>.dialog-close').click();
  await page.locator('[data-recipe="5"]').click();await page.locator('#lucky-toggle').check();
  const forgedResponse=page.waitForResponse(r=>r.url().endsWith('/api/action'));await page.locator('#forge-button').click();
  const forged=await(await forgedResponse).json();await page.locator('#result-dialog').waitFor({state:'visible'});
  assert.equal(forged.weapons[forged.latest].rarity,rarity);assert.equal(await page.locator('#result-dialog').getAttribute('data-rarity'),String(rarity));
  await page.waitForTimeout(200);await page.screenshot({path:path.join(out,rarity===3?'2d-epic-purple.png':'2d-legend-gold.png')});
  await page.locator('#equip-result').click();await page.waitForFunction(()=>!busy);
  check(`actual C forge result has ${rarity===3?'purple epic':'gold legendary'} 2D reveal and equip`);
  await page.locator('.nav-item[data-page="inventory"]').click();await page.locator('#upgrade-button').click();await page.locator('#altar-protect').check();
  const upgradeResponse=page.waitForResponse(r=>r.url().endsWith('/api/action'));await page.locator('#upgrade-start').click();
  const upgraded=await(await upgradeResponse).json();await page.waitForFunction(()=>!busy);
  assert.equal(upgraded.result,0);assert.equal(upgraded.upgradeSuccess,1);assert.equal(upgraded.items[2],forged.items[2]-1);
  assert.equal(await page.locator('#upgrade-dialog').getAttribute('data-rarity'),String(rarity));assert((await page.locator('#upgrade-result').innerText()).includes('成功'));
  await page.screenshot({path:path.join(out,rarity===3?'2d-upgrade-purple.png':'2d-upgrade-gold.png')});
  await page.locator('#upgrade-dialog>.dialog-close').click();check(`altar upgrades chosen blade, consumes protection and matches rarity glow (${rarity})`);
  await page.locator('#batch-toggle').click();assert(await page.locator(`[data-weapon="${forged.weapons[forged.latest].id}"]`).isDisabled());
  await page.locator('[data-weapon="1"]').click();await page.locator('[data-weapon="2"]').click();assert.equal(await page.evaluate(()=>batchIds.size),2);
  await page.locator('#weapon-sort').selectOption('attack');assert.equal(await page.evaluate(()=>batchIds.size),2);
  await page.locator('#batch-recycle').click();await page.locator('#confirm-cancel').click();assert.equal(await page.evaluate(()=>state.weapons.length),5);
  await page.locator('#batch-recycle').click();const recycledResponse=page.waitForResponse(r=>r.url().endsWith('/api/action'));await page.locator('#confirm-yes').click();
  const recycled=await(await recycledResponse).json();await page.waitForFunction(()=>!busy);assert.equal(recycled.weapons.length,3);assert.equal(recycled.weapons[recycled.equipped].id,forged.weapons[forged.latest].id);
  await page.screenshot({path:path.join(out,'2d-batch-inventory.png'),fullPage:true});check(`multi-selection survives sorting, cancel preserves blades and confirmed batch protects equipment (${rarity})`);
  await page.locator('#settings-button').click();await page.screenshot({path:path.join(out,'2d-save-slots.png')});
  await page.locator('[data-slot="1"]').click();await page.locator('#confirm-yes').click();await page.waitForFunction(()=>!busy&&state.activeSlot===1);assert.equal(await page.locator('#gold-count').innerText(),'180');
  await page.reload();await page.waitForFunction(()=>state?.activeSlot===1);await page.locator('#settings-button').click();await page.locator('[data-slot="0"]').click();await page.locator('#confirm-yes').click();
  await page.waitForFunction(()=>!busy&&state.activeSlot===0);assert.equal(await page.evaluate(()=>state.gold),recycled.gold);check(`four save cards create a new role and restore original role across reload (${rarity})`);
  await page.setViewportSize({width:390,height:844});await page.locator('.nav-item[data-page="explore"]').click();await page.locator('[data-explore="0"]').click();await page.waitForFunction(()=>!busy&&state.battle.active);
  const hud=await page.locator('.combat-hud').boundingBox(),buttons=await page.locator('.battle-actions').boundingBox();
  assert(hud.y>=0&&buttons.y+buttons.height<=844,`HUD/action panel outside phone: ${JSON.stringify({hud,buttons})}`);
  assert.equal(await page.locator('.world-map-panel').isVisible(),false);assert.equal(await page.locator('#combat-canvas').isVisible(),true);
  await page.screenshot({path:path.join(out,'2d-mobile-combat.png')});
  await page.locator('[data-battle="4"]').click();await page.waitForFunction(()=>!busy);
  assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);check(`2D combat shows both health bars and all action buttons together on phone (${rarity})`);
  await page.close();server.kill();await new Promise(r=>server.once('exit',r));server=null;fs.rmSync(tmp,{recursive:true,force:true,maxRetries:5,retryDelay:100});tmp=null;
 }
 assert.deepEqual(errors,[]);fs.writeFileSync(path.join(out,'2d-verification.json'),JSON.stringify({date:'2026-10-08',groups:passed,errors,rarities:[3,4],phone:[390,844]},null,2));
})().catch(e=>{console.error(e);process.exitCode=1;}).finally(async()=>{if(browser)await browser.close();if(server)server.kill();if(tmp)fs.rmSync(tmp,{recursive:true,force:true,maxRetries:8,retryDelay:200});});
