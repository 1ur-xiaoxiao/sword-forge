/* 浏览器端到端验收。需要 Playwright 和 Microsoft Edge；不改玩家存档。 */
const { chromium } = require(process.env.FORGE_PLAYWRIGHT_PATH || 'playwright');
const fs = require('node:fs');
const path = require('node:path');
const net = require('node:net');
const { spawn } = require('node:child_process');
const assert = require('node:assert/strict');
const root = path.resolve(__dirname, '..');
const output = path.join(root, 'outputs');
fs.mkdirSync(output, {recursive:true});
fs.mkdirSync(path.join(root, 'work'), {recursive:true});
const temporary = fs.mkdtempSync(path.join(root, 'work', 'browser-test-'));
assert(path.resolve(temporary).startsWith(path.resolve(root, 'work') + path.sep));
fs.mkdirSync(path.join(temporary, 'build'));
const packaged=process.env.FORGE_TEST_EXE;
const executable=path.join(temporary,packaged?'sword_forge.exe':'build/sword_forge.exe');
fs.copyFileSync(packaged||path.join(root,'build/sword_forge.exe'),executable);
if(!packaged)fs.cpSync(path.join(root,'web'),path.join(temporary,'web'),{recursive:true});
let server, browser, passed = 0;
const check = name => { passed++; console.log(`PASS BROWSER ${passed}: ${name}`); };

(async()=>{
  const port = await new Promise(resolve => {
    const probe = net.createServer();
    probe.listen(0,'127.0.0.1',()=>{const port=probe.address().port;probe.close(()=>resolve(port));});
  });
  const url = `http://127.0.0.1:${port}`;
  server = spawn(executable,['--headless','--port',String(port)],{windowsHide:true});
  for(let i=0;i<80;i++) {
    try {const response=await fetch(url+'/api/state');if(response.ok)break;} catch{}
    if(i===79)throw new Error('C service did not start');
    await new Promise(resolve=>setTimeout(resolve,100));
  }
  browser = await chromium.launch({headless:true,executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
  const page = await browser.newPage({viewport:{width:1440,height:1000},locale:'zh-CN'});
  const errors = [];
  page.on('pageerror', error => errors.push(error.message));
  page.on('console', message => {if(message.type()==='error')errors.push(message.text());});
  await page.goto(url,{waitUntil:'networkidle'});
  await page.locator('#forge-button:enabled').waitFor();
  assert.equal(await page.locator('#gold-count').innerText(),'180');
  assert.equal(await page.locator('#recipe-list .recipe-card').count(),6);
  await page.screenshot({path:path.join(output,'ui-desktop.png'),fullPage:true});
  check('actual graphical page loads all local assets and displays C state');

  await page.locator('[data-recipe="3"]').click();
  assert(await page.locator('#forge-button').isDisabled());
  assert((await page.locator('#forge-hint').innerText()).includes('Lv.10'));
  await page.locator('[data-recipe="0"]').click();
  await page.locator('#lucky-toggle').check();
  assert((await page.locator('#probability-list').innerText()).includes('43.00%'));
  check('locked recipe preview, material checks and lucky probability update');

  async function clickAction(selector) {
    const responsePromise=page.waitForResponse(response=>response.url().endsWith('/api/action')&&response.request().method()==='POST');
    await page.locator(selector).click();
    const response=await responsePromise;
    assert.equal(response.status(),200);
    return response.json();
  }
  const forged = await clickAction('#forge-button');
  await page.locator('#result-dialog').waitFor({state:'visible'});
  assert.equal(forged.result,0);
  assert.equal(forged.weapons.length,2);
  assert((await page.locator('#result-content').innerText()).includes('旅人铁剑'));
  await page.screenshot({path:path.join(output,'ui-forge-result.png')});
  const equipped = await clickAction('#equip-result');
  assert.equal(equipped.equipped,1);
  await page.locator('#result-dialog').waitFor({state:'hidden'});
  check('animated forge reveals actual weapon, equip dialog updates C equipment');

  await page.locator('.nav-item[data-page="inventory"]').click();
  await page.locator('#protect-toggle').check();
  await page.locator('#upgrade-button').click();
  await page.locator('#upgrade-dialog').waitFor({state:'visible'});
  const upgraded=await clickAction('#upgrade-start');
  assert.equal(upgraded.result,0);
  assert.equal(upgraded.items[2],equipped.items[2]-1);
  await page.waitForFunction(gold=>document.querySelector('#gold-count').textContent===gold,String(upgraded.gold));
  await page.waitForFunction(()=>!busy);
  await page.locator('#upgrade-dialog .dialog-close').first().click();
  await page.screenshot({path:path.join(output,'ui-inventory.png'),fullPage:true});
  await page.locator('.filter[data-filter="3"]').click();
  if(!upgraded.weapons.some(w=>w.rarity>=3)) assert(await page.locator('#weapon-grid .empty-state').isVisible());
  await page.locator('.filter[data-filter="-1"]').click();
  await page.locator('[data-weapon="1"]').click();
  await page.locator('#salvage-button').click();
  await page.locator('#confirm-dialog').waitFor({state:'visible'});
  await page.locator('#confirm-cancel').click();
  assert.equal((await (await fetch(url+'/api/state')).json()).weapons.length,2);
  check('inventory detail, protected upgrade, quality filters and canceled salvage');

  await page.locator('.nav-item[data-page="explore"]').click();
  const encounter=await clickAction('[data-explore="0"]');
  assert(encounter.battle.active);
  await page.locator('#battle-panel').waitFor({state:'visible'});
  assert(await page.locator('#rest-button').isDisabled());
  let battle=encounter;
  for(let i=0;i<50&&battle.battle.active;i++) {
    battle=await clickAction('[data-battle="0"]');
    if(battle.battle.active) await page.waitForFunction(turn=>document.querySelector('#battle-panel').textContent.includes(`第 ${turn+1} 回合`),battle.battle.turn);
  }
  assert(!battle.battle.active);assert.equal(battle.wins,1);
  await page.locator('#battle-panel').waitFor({state:'hidden'});
  const usedStamina=await clickAction('#use-stamina');
  assert.equal(usedStamina.result,0);assert.equal(usedStamina.streak,1);assert.equal(usedStamina.energy,100);
  await page.locator('#rest-button').click();
  await page.locator('#confirm-dialog').waitFor({state:'visible'});
  const rested=await clickAction('#confirm-yes');
  assert.equal(rested.streak,0);
  assert.equal(rested.hp,rested.maxHp);assert.equal(rested.energy,100);
  check('real combat, stamina potion retains streak and confirmed rest resets it');

  await page.locator('.nav-item[data-page="shop"]').click();
  await page.locator('#quantity-0').fill('1');
  const bought=await clickAction('[data-buy="0"]');
  assert.equal(bought.result,0);
  assert.equal(await page.locator('.shop-card').count(),4);
  await page.locator('#quantity-3').fill('1');
  const energyBought=await clickAction('[data-buy="3"]');
  assert.equal(energyBought.result,0);assert.equal(energyBought.items[3],usedStamina.items[3]+1);
  await page.locator('#quantity-0').fill('0');
  assert(await page.locator('[data-buy="0"]').isDisabled());
  await page.locator('#quantity-0').fill('99');
  assert(await page.locator('[data-buy="0"]').isDisabled());
  await page.locator('.nav-item[data-page="journal"]').click();
  assert.equal(await page.locator('.quest-card').count(),3);
  check('shop validates quantity and gold, journal renders persistent milestones');

  await page.reload({waitUntil:'networkidle'});
  await page.waitForFunction(gold=>document.querySelector('#gold-count').textContent===gold,String(energyBought.gold));
  assert.equal(await page.locator('#bag-count').innerText(),'2');
  await page.locator('#settings-button').click();
  await page.locator('#new-button').click();
  await page.locator('#confirm-dialog').waitFor({state:'visible'});
  await page.keyboard.press('Escape');
  assert.equal((await (await fetch(url+'/api/state')).json()).weapons.length,2);
  check('reload restores C progress and new-game cancellation preserves it');


  await page.locator('.nav-item[data-page="explore"]').click();
  assert.equal(await page.locator('.map-node').count(),6);
  await page.locator('[data-area="5"]').click();
  assert(await page.locator('[data-explore="5"]').isDisabled());
  assert((await page.locator('#area-list').innerText()).includes('无光之王'));
  await page.locator('[data-area="0"]').click();
  assert(await page.locator('[data-boss="0"]').isDisabled());
  await page.screenshot({path:path.join(output,'ui-world-map.png'),fullPage:true});
  check('six interactive map landmarks, locked-region preview and local boss objective');

  const gathered=await clickAction('[data-gather="0"]');
  assert.equal(gathered.result,0);assert.equal(gathered.energy,90);assert.equal(gathered.areas[0].surveys,1);
  await page.locator('#exploration-event').waitFor({state:'visible'});
  assert.equal(await page.locator('[data-event]').count(),2);
  assert(await page.locator('[data-explore="0"]').isDisabled());
  await page.screenshot({path:path.join(output,'ui-exploration-event.png'),fullPage:true});
  await page.reload({waitUntil:'networkidle'});
  await page.locator('#exploration-event').waitFor({state:'visible'});
  const resolved=await clickAction('[data-event="1"]');
  assert.equal(resolved.result,0);assert.equal(resolved.event.kind,-1);
  await page.locator('#exploration-event').waitFor({state:'hidden'});
  check('real gathering, two event choices, refresh persistence and event action locking');

  const secondEncounter=await clickAction('[data-explore="0"]');
  let worldBattle=secondEncounter;
  for(let turn=0;turn<50&&worldBattle.battle.active;turn++) {
    const skill=worldBattle.battle.cooldown===0;
    worldBattle=await clickAction(`[data-battle="${skill?4:0}"]`);
    if(worldBattle.battle.active) await page.waitForFunction(turn=>document.querySelector('#battle-panel').textContent.includes(`第 ${turn+1} 回合`),worldBattle.battle.turn);
  }
  assert.equal(worldBattle.areas[0].wins,2);
  await page.locator('#battle-panel').waitFor({state:'hidden'});
  await page.locator('#explore-rest').click();
  await page.locator('#confirm-dialog').waitFor({state:'visible'});
  await clickAction('#confirm-yes');
  const bossEncounter=await clickAction('[data-boss="0"]');
  assert(bossEncounter.battle.boss);
  let bossBattle=bossEncounter;
  for(let turn=0;turn<50&&bossBattle.battle.active;turn++) {
    const b=bossBattle.battle;
    const action=b.boss&&(b.turn+1)%3===0?1:bossBattle.hp<b.attack+25&&bossBattle.items[0]?2:b.cooldown===0?4:0;
    bossBattle=await clickAction(`[data-battle="${action}"]`);
    if(bossBattle.battle.active) await page.waitForFunction(turn=>document.querySelector('#battle-panel').textContent.includes(`第 ${turn+1} 回合`),bossBattle.battle.turn);
  }
  assert.equal(bossBattle.trophies,1);
  assert(bossBattle.message.includes('首通'));
  await page.locator('#battle-panel').waitFor({state:'hidden'});
  assert((await page.locator('#world-progress').innerText()).includes('1 / 6'));
  await page.locator('[data-area="1"]').click();
  assert.equal(bossBattle.areas[1].unlocked,bossBattle.level>=3);
  await page.locator('.nav-item[data-page="journal"]').click();
  assert.equal(await page.locator('.trophy-card.earned').count(),1);
  check('real skill combat and first boss clear update permanent stats, route and trophy journal');

  for(const width of [1440,1024,768,390]) {
    await page.setViewportSize({width,height:width===390?844:1000});
    for(const tab of ['forge','explore','inventory','shop','journal']) {
      await page.locator(`.nav-item[data-page="${tab}"]`).click();
      const dimensions=await page.evaluate(()=>({width:innerWidth,scroll:document.documentElement.scrollWidth}));
      assert(dimensions.scroll<=dimensions.width+1,`${tab} overflow at ${width}: ${JSON.stringify(dimensions)}`);
    }
  }
  await page.locator('.nav-item[data-page="explore"]').click();
  await page.waitForFunction(()=>!document.querySelector('#toast').classList.contains('show'));
  await page.evaluate(()=>window.scrollTo(0,0));
  await page.screenshot({path:path.join(output,'ui-map-mobile.png'),fullPage:true});
  await page.locator('.nav-item[data-page="forge"]').click();
  await page.screenshot({path:path.join(output,'ui-mobile.png'),fullPage:true});
  await page.setViewportSize({width:1440,height:1000});
  check('all five pages fit desktop, laptop, tablet and phone widths without horizontal overflow');
  assert.deepEqual(errors,[]);
  check('no browser script errors or failed asset requests');
  fs.writeFileSync(path.join(output,'browser-verification.json'),JSON.stringify({date:'2026-10-08',groups:passed,errors,viewports:[1440,1024,768,390]},null,2));
  console.log(`${passed} browser verification groups passed.`);
})().catch(error=>{console.error(error);process.exitCode=1;}).finally(async()=>{
  if(browser)await browser.close();
  if(server&&server.exitCode===null) {
    await new Promise(resolve=>{server.once('exit',resolve);server.kill();});
  }
  // 只删除本次自行创建、已验证在 work 内的临时项目。
  fs.rmSync(temporary,{recursive:true,force:true,maxRetries:8,retryDelay:200});
});
