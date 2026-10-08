'use strict';
// 前端仅管理显示和交互；数值、随机结果与存档均来自 C 服务。
const $ = (selector) => document.querySelector(selector);
const icons = ['iron', 'wood', 'crystal', 'star'];
const materialNames = ['铁矿', '硬木', '灵晶', '星陨石'];
const rarityNames = ['普通', '精良', '稀有', '史诗', '传说'];
const colors = ['#a0abb6', '#82c69c', '#82b5ed', '#b496ed', '#f1be73'];
const materialColors = ['#aab9ca', '#c5a17a', '#b49bdd', '#9dbde4'];
const flavors = ['坚实的铁刃，是踏入荒野的第一份勇气。', '以灵晶为心，铭刻月光般清澈的锋芒。', '来自遥远星辰的铁，在炉火中苏醒。', '唯有不灭的炉火，才能铸就王座的荣光。', '双刃交错，将永夜凝结为银色的霜。', '用六域的星光，斩开最后一道黑暗。'];
const pages = {
  forge: ['THE EMBER FORGE', '炉火与星光', '每一次落锤，都有新的可能。', '锻造工坊'],
  explore: ['BEYOND THE TOWN', '循着风，走入荒野', '带回矿石、故事，和更好的自己。', '荒野探索'],
  inventory: ['BLADES OF YOUR JOURNEY', '锋芒收藏', '每一道剑光，都留下了你的印记。', '武器背包'],
  shop: ['THE WANDERING MERCHANT', '旅人的小小行囊', '一点准备，一点好运，再出发。', '旅人商店'],
  journal: ['YOUR STORY SO FAR', '铸剑师的成长手札', '那些微小的进步，终将汇成传奇。', '成长手札']
};
let state = null, recipeIndex = 0, weaponId = null, filter = -1, sort = 'new';
let areaIndex = 0, lastAdventure = '';
let busy = false, closed = false, protectedUpgrade = false, currentPage = 'forge';
let toastTimer, confirmResolve, soundEnabled = false, audioContext;
const history = [];
const icon = (name) => `<svg aria-hidden="true"><use href="#i-${name}"/></svg>`;
const amount = (n) => Number(n).toLocaleString('zh-CN');
const pct = (n, total) => `${Math.min(100, Math.max(0, 100 * n / total))}%`;

// 原创矢量武器图：同一配方与品质有一致的外观。
function swordArt(recipe, rarity, extra = '') {
  const color = colors[rarity];
  const width = [15, 13, 21, 17, 12, 23][recipe];
  return `<svg class="weapon-art ${extra}" viewBox="0 0 100 150" style="--rarity:${color}" aria-label="${rarityNames[rarity]}宝剑" role="img">
  <ellipse cx="50" cy="143" rx="22" ry="4" fill="${color}" opacity=".1"/>
  <path d="M50 3 ${50 + width} 28 ${50 + width - 5} 103 50 112 ${50 - width + 5} 103 ${50 - width} 28Z" fill="${color}"/>
  <path d="M50 3v109l${-width + 5}-9 ${-5}-75Z" fill="#fff" opacity=".18"/>
  <path d="M50 22v68" stroke="#18252f" stroke-width="2" opacity=".35"/>
  ${recipe > 0 ? `<path d="m50 37 5 8-5 9-5-9Z" fill="#f8ecd1" opacity=".8"/><path d="M44 65h12m-12 7h12" stroke="#e9e1d5" opacity=".5"/>` : ''}
  <path d="m25 99 9-5 16 7 16-7 9 5-4 9-21-4-21 4Z" fill="${rarity >= 3 ? '#dcad70' : '#9a886c'}"/>
  <path d="m46 104 8 0 2 31-6 7-6-7Z" fill="#5f4540"/>
  <path d="m45 112 10 3m-10 4 10 3m-10 4 10 3" stroke="#b29777" stroke-width="2"/>
  <path d="m50 133 8 7-8 7-8-7Z" fill="${color}"/>
  <circle cx="50" cy="101" r="4" fill="${color}" stroke="#efd6a2"/>
  </svg>`;
}

function toast(message) {
  clearTimeout(toastTimer);
  $('#toast').textContent = message;
  $('#toast').classList.add('show');
  toastTimer = setTimeout(() => $('#toast').classList.remove('show'), 4300);
}

function log(message) {
  if (!message) return;
  const now = new Date();
  history.unshift({message, time: now.toLocaleTimeString('zh-CN', {hour:'2-digit', minute:'2-digit'})});
  if (history.length > 5) history.pop();
  $('#activity-log').replaceChildren(...history.map(entry => {
    const p = document.createElement('p'); p.className = 'activity-item';
    const dot = document.createElement('span'); dot.className = 'log-dot';
    const time = document.createElement('time'); time.textContent = entry.time;
    const text = document.createElement('span'); text.textContent = entry.message;
    p.append(dot, time, text); return p;
  }));
}

function playSound(kind = 'click') {
  if (!soundEnabled) return;
  try {
    audioContext ??= new (window.AudioContext || window.webkitAudioContext)();
    audioContext.resume();
    const notes = kind === 'forge' ? [380, 570, 760] : kind === 'win' ? [440, 554, 659] : [260];
    notes.forEach((frequency, i) => {
      const oscillator = audioContext.createOscillator(), gain = audioContext.createGain();
      const t = audioContext.currentTime + i * .1;
      oscillator.type = 'triangle'; oscillator.frequency.value = frequency;
      gain.gain.setValueAtTime(0, t); gain.gain.linearRampToValueAtTime(.08, t + .008);
      gain.gain.exponentialRampToValueAtTime(.001, t + .28);
      oscillator.connect(gain); gain.connect(audioContext.destination);
      oscillator.start(t); oscillator.stop(t + .3);
    });
  } catch { soundEnabled = false; }
}

function switchPage(page) {
  if (!pages[page]) return;
  currentPage = page;
  document.querySelectorAll('.page').forEach(el => el.classList.toggle('active', el.id === `page-${page}`));
  document.querySelectorAll('.nav-item').forEach(el => el.classList.toggle('active', el.dataset.page === page));
  const meta = pages[page];
  $('#page-eyebrow').textContent = meta[0]; $('#page-title').textContent = meta[1];
  $('#page-subtitle').textContent = meta[2]; $('#location-page').textContent = meta[3];
  playSound();
}

function renderCommon() {
  $('#gold-count').textContent = amount(state.gold);
  $('#day-count').textContent = state.day;
  $('#player-level').textContent = `Lv.${state.level}`; $('#level-badge').textContent = state.level;
  $('#xp-label').textContent = state.level === 20 ? '已达满级' : `${state.xp} / ${state.xpNeeded}`;
  $('#xp-bar').style.width = state.level === 20 ? '100%' : pct(state.xp, state.xpNeeded);
  $('#player-rank').textContent = `锻造师 Lv.${state.smithLevel} · ${state.smithLevel === 10 ? '已达满级' : `${state.smithXp} / ${state.smithNeeded} 经验`}`;
  $('#bag-count').textContent = state.weapons.length;
  const w = state.weapons[state.equipped];
  $('#equipped-mini').innerHTML = `<span class="mini-icon">${icon('sword')}</span><div><p>当前装备 · 攻击 ${state.attack}</p><b>${w ? `${state.recipes[w.recipe].name} +${w.upgrade}` : '尚未装备'}</b></div>`;
  $('#rest-button').disabled = busy || state.battle.active || state.event.kind >= 0 || state.corrupted;
  $('#rest-button small').textContent = state.streak ? '回满状态 · 重置连续探索' : '恢复全部生命与体力';
  $('#save-button').disabled = busy || state.battle.active || state.event.kind >= 0 || state.corrupted;
  $('#save-state').innerHTML = `<i style="background:${state.saveOk && !state.corrupted ? '#83bca3' : '#d89179'}"></i>${state.corrupted ? '原存档无法读取' : state.saveOk ? '进度已自动保存' : '保存失败，请检查权限'}`;
}

function renderForge() {
  const r = state.recipes[recipeIndex], lucky = $('#lucky-toggle').checked;
  $('#forge-name').textContent = r.name; $('#forge-flavor').textContent = flavors[recipeIndex];
  $('#forge-cost').textContent = amount(r.gold);
  $('#recipe-count').textContent = `${state.recipes.filter(x => x.level <= state.level).length} / ${state.recipes.length} 已解锁`;
  $('#recipe-list').innerHTML = state.recipes.map((recipe, i) => `<button class="recipe-card ${i === recipeIndex ? 'selected' : ''} ${recipe.level > state.level ? 'locked' : ''}" data-recipe="${i}" aria-pressed="${i === recipeIndex}"><span class="recipe-thumb">${icon('sword')}</span><span><b>${recipe.name}</b><small>${recipe.level > state.level ? `角色 Lv.${recipe.level} 解锁` : `基础攻击 ${recipe.attack} · ${recipe.gold} 金币`}</small></span>${recipe.level > state.level ? `<svg class="lock"><use href="#i-lock"/></svg>` : ''}</button>`).join('');
  $('#ingredients').innerHTML = r.materials.map((cost, i) => `<div class="ingredient ${cost > state.materials[i] ? 'missing' : ''} ${cost === 0 ? 'unused' : ''}"><span class="material-icon">${icon(icons[i])}</span><div><b>${materialNames[i]}</b><p><strong>${cost ? amount(state.materials[i]) : '—'}</strong>${cost ? ` / ${cost}` : ' 不需要'}</p></div></div>`).join('');
  $('#luck-stock').textContent = `持有 ${amount(state.items[1])} 张`;
  const weights = lucky ? r.luckyWeights : r.weights;
  $('#probability-bar').innerHTML = weights.map((weight, i) => `<span style="width:${weight / 100}%;--rarity:${colors[i]}"></span>`).join('');
  $('#probability-list').innerHTML = weights.map((weight, i) => `<div class="prob-row" style="--rarity:${colors[i]}"><i class="rarity-dot"></i><span>${rarityNames[i]}</span><strong>${(weight / 100).toFixed(2)}%</strong></div>`).join('');
  $('#pity-text').textContent = state.pity === 19 ? '下一次锻造，必得史诗或传说！' : `再锻造 ${20 - state.pity} 次，必得史诗或传说`;
  $('#pity-marks').innerHTML = Array.from({length:20}, (_, i) => `<i class="${i < state.pity ? 'filled' : ''}"></i>`).join('');
  let reason = '';
  if (state.corrupted) reason = '原存档无法读取，请在旅程设置里处理';
  else if (state.event.kind >= 0) reason = '先在荒野探索中完成当前事件';
  else if (state.battle.active) reason = '当前正在战斗，请先回到营地';
  else if (state.level < r.level) reason = `角色达到 Lv.${r.level} 后解锁此配方`;
  else if (state.weapons.length >= 80) reason = '武器背包已满，请先回收旧武器';
  else if (state.gold < r.gold) reason = `还缺少 ${r.gold - state.gold} 金币`;
  else if (r.materials.some((n, i) => n > state.materials[i])) reason = '材料不足，前往荒野探索获取';
  else if (lucky && !state.items[1]) reason = '幸运符不足，取消勾选或去商店购买';
  $('#forge-button').disabled = !!reason || busy;
  $('#forge-hint').textContent = reason || '每次锻造必定获得一把武器 · 品质由概率决定';
  $('#resources').innerHTML = state.materials.map((n, i) => `<div class="resource" style="--color:${materialColors[i]}">${icon(icons[i])}<div><p>${materialNames[i]}</p><b>${amount(n)}</b><small>${i > 1 ? '珍贵材料' : '基础材料'}</small></div></div>`).join('');
}


const regionLore = [
  '溪流蜿蜒穿过密林，废弃矿道里的炉火余温尚未散去。沿着旧矿车轨道，寻找苔岩巨像。',
  '月光在石柱之间化成晶体。沉睡的傀儡守护祭坛，而灵晶祭司仍在等待失落的誓言。',
  '一道坠星撕开大地。紫色裂隙通往地下，吞噬者盘踞在星陨石最密集的地方。',
  '红色岩浆划破黑山。传说巨龙的吐息能点燃永恒炉火，也能淬炼最坚韧的剑刃。',
  '积雪掩埋了古战场，极光照亮冰封的王座。霜月女王守着通向天穹的最后一道山门。',
  '浮空王城高悬云端。铸成黎明的剑，让六域最后一盏灯重新亮起。'
];
const regionColors = ['#8db58d','#a29be0','#a481b9','#d99066','#91bace','#d8bc86'];
const regionSymbols = ['wood','crystal','star','flame','crystal','shield'];
const regionPoints = [[13,73],[29,41],[45,65],[61,29],[76,49],[88,20]];

function mapLandscape() {
  return `<svg class="map-landscape" viewBox="0 0 1100 430" preserveAspectRatio="none" aria-hidden="true">
    <defs><linearGradient id="map-bg" x2="1" y2="1"><stop stop-color="#243735"/><stop offset=".55" stop-color="#282531"/><stop offset="1" stop-color="#25323d"/></linearGradient><radialGradient id="map-glow"><stop stop-color="#e4ba76" stop-opacity=".14"/><stop offset="1" stop-color="#e4ba76" stop-opacity="0"/></radialGradient></defs>
    <rect width="1100" height="430" fill="url(#map-bg)"/><ellipse cx="740" cy="150" rx="360" ry="210" fill="url(#map-glow)"/>
    <path d="M-10 325 84 199 163 260 234 153 313 235 375 181 434 298 520 222 595 323 670 257 769 299 815 200 897 261 972 167 1110 240V440H-10Z" fill="#111c23" opacity=".52"/>
    <path d="m582 203 54-116 32 54 54-96 74 130 31-45 67 107" fill="#5c4b43" opacity=".48"/><path d="m835 180 49-108 29 60 49-119 81 169" fill="#a9bdc3" opacity=".23"/>
    <path d="m939 64 23-51 34 59-33-16Z" fill="#d4dddb" opacity=".48"/><path d="m678 98 44-53 30 53-28-18Z" fill="#cd9875" opacity=".4"/>
    <path d="M323 0q-73 100-8 165t-49 119T278 430" fill="none" stroke="#527775" stroke-width="22" opacity=".3"/><path d="M323 0q-73 100-8 165t-49 119T278 430" fill="none" stroke="#8bb7ac" stroke-width="2" opacity=".27"/>
    ${Array.from({length:23},(_,i)=>{const x=32+(i*79)%360,y=125+(i*53)%240;return `<path d="m${x} ${y} 16-38 16 38h-10l13 24h-38l13-24Z" fill="#61816b" opacity=".25"/>`;}).join('')}
    <path d="m450 270 15-43 19 34 12-18 15 47" fill="#9e7abb" opacity=".27"/>
    <path d="M143 314 319 176 495 280 671 125 836 211 968 86" fill="none" stroke="#d0ac76" stroke-width="2" stroke-dasharray="6 10" opacity=".48"/>
    <g fill="#d5cdb9" opacity=".1"><ellipse cx="1000" cy="35" rx="120" ry="20"/><ellipse cx="825" cy="8" rx="120" ry="24"/><ellipse cx="740" cy="400" rx="220" ry="35"/></g>
    <text x="28" y="34" fill="#bac9bd" opacity=".35" font-size="12" letter-spacing="5">EMBERLAND</text><text x="875" y="405" fill="#baa687" opacity=".45" font-size="11" letter-spacing="3">N ↑ · 六域大陆</text>
  </svg>`;
}

function renderExplore() {
  const b = state.battle, pending = state.event.kind >= 0;
  const blocked = busy || b.active || pending || state.corrupted;
  $('#vitals').innerHTML = `<div class="vital health"><p>生命</p><b>${state.hp}</b><small>/ ${state.maxHp}</small><div class="progress"><i style="width:${pct(state.hp,state.maxHp)}"></i></div></div><div class="vital energy"><p>体力</p><b>${state.energy}</b><small>/ 100</small><div class="progress"><i style="width:${state.energy}%"></i></div></div><div class="vital"><p>总攻击力</p><b>${state.attack}</b><small>战功加成 +${state.trophies*2}</small></div>`;
  $('#world-progress').textContent = `${state.trophies} / ${state.areas.length} 首领已击败`;
  $('#world-map').innerHTML = mapLandscape()+state.areas.map((a,i)=>`<button class="map-node ${a.unlocked?'available':'locked'} ${a.cleared?'cleared':''} ${i===areaIndex?'selected':''}" data-area="${i}" style="left:${regionPoints[i][0]}%;top:${regionPoints[i][1]}%;--region:${regionColors[i]}" aria-pressed="${i===areaIndex}" aria-label="${a.name}，${a.cleared?'首领已击败':a.unlocked?'已解锁':'未解锁'}"><span class="node-mark">${icon(a.cleared?'check':a.unlocked?regionSymbols[i]:'lock')}</span><b>${a.name}</b><small>Lv.${a.level} · ${a.cleared?'已通关':a.unlocked?'可探索':'未解锁'}</small></button>`).join('');
  const a = state.areas[areaIndex], i=areaIndex;
  const lockReason = state.level<a.level ? `角色达到 Lv.${a.level}` : i>0&&!state.areas[i-1].cleared ? `先击败${state.areas[i-1].boss}` : '';
  const bossReady = a.wins >= 2;
  const minimumAttack = Math.ceil(a.bossHp/6);
  $('#area-list').innerHTML = `<article class="area-card region-detail" style="--region:${regionColors[i]}"><div class="area-art"><span class="area-tag">CHAPTER ${String(i+1).padStart(2,'0')}</span>${icon(regionSymbols[i])}<span class="region-biome">${['林地 · 矿脉','遗迹 · 祭坛','裂隙 · 坠星','火山 · 熔炉','雪原 · 极光','王城 · 云海'][i]}</span></div><div class="area-body"><div class="region-title"><h3>${a.name}</h3><span>${a.cleared?'已解放':a.unlocked?'路线已开启':'路线未开启'}</span></div><p>${regionLore[i]}</p><div class="region-facts"><span>常见敌人<br><b>${a.enemies.join(' / ')}</b></span><span>特色材料<br><b>${materialNames[a.material]}</b></span><span>战斗收益<br><b>${a.gold}~${a.gold+20} 金币 · ${a.xp} 经验</b></span></div>${!a.unlocked?`<p class="route-lock">${icon('lock')}${state.level<a.level?`需要 Lv.${a.level}`:''}${i>0&&!state.areas[i-1].cleared?` · 击败${state.areas[i-1].boss}`:''}</p>`:''}<div class="region-actions"><button class="primary-button" data-explore="${i}" ${blocked||!a.unlocked||state.energy<a.energy?'disabled':''}>${icon('sword')}${!a.unlocked?lockReason:`巡逻战斗 · ${a.energy} 体力`}</button><button class="secondary-button" data-gather="${i}" ${blocked||!a.unlocked||state.energy<10?'disabled':''}>${icon('iron')}采集与事件 · 10 体力</button></div><div class="boss-contract"><div><span class="eyebrow">REGION BOSS</span><h4>${a.boss}</h4><p>生命 ${a.bossHp}~${a.bossHp+15} · 攻击 ${a.bossAttack}~${a.bossAttack+7}<br>${a.cleared?'首通奖励已领取 · 重战获得额外金币与特色材料':`首通：${120+i*90} 金币、幸运符、保护符${i>=1?`、${i+1} 星陨石`:''}<br>永久 +2 攻击、+8 生命 · 回满生命 · 打开后续路线`}</p><small>已完成 ${a.wins} 次巡逻 · ${a.surveys} 次采集 · ${state.attack<minimumAttack?`建议攻击 ${minimumAttack}+，先强化武器`:'装备已具备挑战基础，带好治疗药水'}</small></div><button class="boss-button" data-boss="${i}" ${blocked||!a.unlocked||!bossReady||state.energy<a.energy+5?'disabled':''}>${icon('shield')}${!bossReady?`巡逻 ${Math.min(a.wins,2)}/2 后挑战`:`${a.cleared?'再次挑战':'挑战首领'} · ${a.energy+5} 体力`}</button></div></div></article>`;
  $('#stamina-controls').innerHTML = `<div class="combo-note">${icon('flame')}<div><b>连续探索 ${state.streak} 胜 <span>下一场收益 +${state.nextBonus}%</span></b><p>金币与经验最多 +40% · 采集与事件保留连胜 · 休息、战败或逃跑重置</p></div></div><div class="stamina-actions"><button class="secondary-button" id="use-stamina" ${blocked||state.energy===100||!state.items[3]?'disabled':''}>${icon('potion')}补充 40 体力 (${state.items[3]})</button><button class="secondary-button" id="explore-rest" ${blocked?'disabled':''}>${icon('moon')}休息回满</button></div>`;
  renderEvent();
  $('#expedition-result').hidden = !lastAdventure;
  $('#expedition-result').textContent = lastAdventure;
  $('#battle-panel').hidden = !b.active;
  if (b.active) {
    const heavy = b.boss && (b.turn+1)%3===0;
    const enemy = b.boss ? state.areas[b.area].boss : state.areas[b.area].enemies[b.enemy];
    $('#battle-panel').innerHTML = `<div class="battle-head"><div><div class="eyebrow">${b.boss?'BOSS ENCOUNTER':'WILDERNESS ENCOUNTER'}</div><h3>${state.areas[b.area].name} · ${enemy}</h3></div><span class="subtle">第 ${b.turn+1} 回合</span></div><div class="battle-intent ${heavy?'danger':''}">${icon(heavy?'flame':'shield')}${heavy?'首领正在蓄力！下一行动将承受双倍重击，防御可将伤害降至约三分之一。':b.boss?'首领下一行动：普通攻击 · 每第三回合重击':'敌人下一行动：普通攻击 · 破空斩可造成双倍伤害'}</div><div class="section-label"><span>敌人生命 ${b.hp} / ${b.maxHp}</span><span>下一击 ${heavy?b.attack*2:b.attack}~${heavy?(b.attack+4)*2:b.attack+4}</span></div><div class="progress enemy-health"><i style="width:${pct(b.hp,b.maxHp)}"></i></div><div class="battle-actions"><button class="primary-button" data-battle="0" ${busy?'disabled':''}>${icon('sword')}攻击</button><button class="skill-button" data-battle="4" ${busy||b.cooldown?'disabled':''}>${icon('star')}${b.cooldown?`破空斩 · 冷却 ${b.cooldown}`:'破空斩 · 双倍伤害'}</button><button class="secondary-button" data-battle="1" ${busy?'disabled':''}>${icon('shield')}防御反击</button><button class="secondary-button" data-battle="2" ${busy||!state.items[0]||state.hp===state.maxHp?'disabled':''}>${icon('potion')}喝药 (${state.items[0]})</button><button class="secondary-button" data-battle="3" ${busy?'disabled':''}>${icon('arrow')}逃跑 · 75%</button></div>`;
  }
}

function renderEvent() {
  const e = state.event, panel=$('#exploration-event');
  panel.hidden = e.kind<0;
  if(e.kind<0)return;
  const a=state.areas[e.area],i=e.area;
  const events = [
    {title:'封存的宝箱',text:'倒下的石柱后，一只宝箱还保留着最后的封印。你只能带走其中一份收获。',symbol:'star',choices:[`${materialNames[a.material]} +${4+i}`,`${35+i*20} 金币`]},
    {title:'林间清泉',text:'清澈的水流穿过石缝。喝下泉水疗伤，或装进行囊补充体力？两种选择都保留连胜。',symbol:'potion',choices:['恢复 45 生命（至上限）','恢复 15 体力（至上限）']},
    {title:'古代铸剑祭坛',text:'石碑讲述着一位古代铸剑师的旅程。领悟剑术，或记下失传的淬火技巧？',symbol:'book',choices:[`角色经验 +${35+i*12}`,`锻造经验 +${25+i*5}`]},
    {title:'流浪药师',text:'路边的旅人愿意优惠出售一瓶体力药水，也愿意赠送一些沿路收集的硬木。',symbol:'bag',choices:['花 25 金币，获得 1 体力药水',`免费领取 ${3+i} 硬木`]}
  ];
  const v=events[e.kind];
  panel.innerHTML = `<div class="event-symbol">${icon(v.symbol)}</div><div class="event-story"><div class="eyebrow">A CHANCE ENCOUNTER · ${a.name}</div><h3>${v.title}</h3><p>${v.text}</p><div class="event-choices">${v.choices.map((c,j)=>`<button class="secondary-button" data-event="${j}" ${busy||state.corrupted||e.kind===3&&j===0&&(state.gold<25||state.items[3]>=1000000)?'disabled':''}>${c}${icon('arrow')}</button>`).join('')}</div><small>事件进度已自动保存 · 先完成选择，再继续冒险</small></div>`;
}

function selectedWeapon() {
  let index = state.weapons.findIndex(w => w.id === weaponId);
  if (index < 0) { index = state.equipped >= 0 ? state.equipped : 0; weaponId = state.weapons[index]?.id; }
  return {weapon: state.weapons[index], index};
}

function renderInventory() {
  const {weapon: w, index} = selectedWeapon();
  let weapons = state.weapons.map((weapon, index) => ({...weapon, index})).filter(weapon => weapon.rarity >= filter);
  weapons.sort(sort === 'attack' ? (a,b) => b.attack-a.attack : (a,b) => b.id-a.id);
  $('#weapon-grid').innerHTML = weapons.length ? weapons.map(weapon => `<button class="weapon-card ${weapon.id === weaponId ? 'selected' : ''}" style="--rarity:${colors[weapon.rarity]}" data-weapon="${weapon.id}">${weapon.index === state.equipped ? '<small>已装备</small>':''}${swordArt(weapon.recipe,weapon.rarity)}<b>${state.recipes[weapon.recipe].name} +${weapon.upgrade}</b><p>${rarityNames[weapon.rarity]} · 攻击 ${weapon.attack}</p></button>`).join('') : '<div class="empty-state">尚未收集到这个品质的武器。<br>继续锻造，下一把或许就是传奇。</div>';
  if (!w) return;
  const locked = busy || state.battle.active || state.event.kind >= 0 || state.corrupted;
  const cannotUpgrade = locked || w.upgrade >= 10 || state.gold < w.cost || state.materials[0] < w.iron || (protectedUpgrade && !state.items[2]);
  $('#weapon-detail').style.setProperty('--rarity',colors[w.rarity]);
  $('#weapon-detail').innerHTML = `<div class="eyebrow">BLADE NO. ${String(w.id).padStart(3,'0')}</div>${swordArt(w.recipe,w.rarity)}<h2>${state.recipes[w.recipe].name}</h2><p class="rarity-label">${rarityNames[w.rarity]}品质 · ${w.upgrade ? `强化 +${w.upgrade}` : '未经强化'}</p><div class="weapon-facts"><div><p>武器攻击</p><b>${w.attack}</b></div><div><p>强化等级</p><b>+${w.upgrade}<small style="font-size:11px;color:#888"> / 10</small></b></div></div><button class="primary-button" id="equip-button" ${locked || index === state.equipped ? 'disabled':''}>${icon('sword')}${index === state.equipped ? '已装备此武器' : '装备武器'}</button><button class="secondary-button" id="upgrade-button" ${cannotUpgrade ? 'disabled':''}>${icon('forge')}${w.upgrade === 10 ? '已达到最高强化' : `强化 · ${w.cost} 金币 / ${w.iron} 铁矿`}</button><p class="upgrade-info">${w.upgrade === 10 ? '炉火淬炼，锋芒已至巅峰。' : `成功率 ${w.chance}% · 失败降 1 级（最低 +0）<br>资源不足时强化按钮会暂时关闭`}</p><label class="protect-toggle"><input type="checkbox" id="protect-toggle" ${protectedUpgrade ? 'checked':''} ${!state.items[2] || locked ? 'disabled':''}>使用保护符（持有 ${state.items[2]} 张）</label><button class="salvage-button" id="salvage-button" ${locked || index === state.equipped ? 'disabled':''}>回收武器 · 获得 ${2+w.rarity} 铁矿与 ${10+w.rarity*12+w.upgrade*4} 金币</button>`;
}

function renderShop() {
  const names = ['治疗药水','幸运符','保护符','体力药水'], prices = [20,65,90,35];
  const descriptions = ['恢复 55 点生命，不超过生命上限。<br>也可在战斗中使用。','单次锻造提升高品质概率。<br>让史诗与传说离你更近。','单次强化失败时保住等级。<br>无论成功失败都消耗一张。','恢复 40 体力，最高 100。<br>保留连续探索奖励，继续冒险。'];
  const previous = [0,1,2,3].map(i => $(`#quantity-${i}`)?.value || '1');
  $('#shop-list').innerHTML = names.map((name,i) => `<article class="panel shop-card" style="--color:${['#d4968a','#dfc18b','#a8bfd8','#83bca3'][i]}"><div class="shop-art">${icon(['potion','star','shield','potion'][i])}</div><h3>${name}</h3><p>${descriptions[i]}</p><div class="owned">当前持有 ${amount(state.items[i])} ${i === 0 || i === 3 ? '瓶':'张'}</div><label>购买数量 <input type="number" id="quantity-${i}" data-quantity="${i}" min="1" max="99" step="1" value="${previous[i]}"></label><button class="primary-button" data-buy="${i}" ${busy || state.battle.active || state.event.kind >= 0 || state.corrupted || state.gold < prices[i]*Number(previous[i]) ? 'disabled':''}>${icon('coin')}购入 · ${prices[i]*Number(previous[i])} 金币</button></article>`).join('');
}

function renderJournal() {
  $('#journey-stats').innerHTML = `<div><b>${state.forged}</b><p>次落锤锻造</p></div><div><b>${state.wins}</b><p>次探索胜利</p></div><div><b>Lv.${state.smithLevel}</b><p>锻造师等级</p></div>`;
  const quests = [{bit:1,name:'第一簇炉火',desc:'累计锻造 5 把武器',reward:'100 金币 + 2 幸运符',n:state.forged,total:5,icon:'forge'}, {bit:2,name:'荒野的回响',desc:'累计取得 10 次探索胜利',reward:'200 金币 + 2 星陨石',n:state.wins,total:10,icon:'map'}, {bit:4,name:'传奇的前奏',desc:'获得一把史诗或传说武器',reward:'180 金币 + 2 保护符',n:state.weapons.some(w=>w.rarity>=3)?1:0,total:1,icon:'star'}];
  $('#quest-list').innerHTML = quests.map(q => { const done = state.quests & q.bit; return `<article class="panel quest-card">${icon(done?'check':q.icon)}<h3>${q.name}</h3><p>${q.desc}<br>奖励：${q.reward}</p><div class="progress"><i style="width:${done?'100%':pct(q.n,q.total)}"></i></div><div class="quest-footer"><span>${done?'已完成 · 已自动领取':`${Math.min(q.n,q.total)} / ${q.total}`}</span><span class="${done?'quest-done':''}">${done?'奖励已入背包':'进行中'}</span></div></article>`; }).join('');
  $('#trophy-list').innerHTML = state.areas.map((a,i)=>`<article class="panel trophy-card ${a.cleared?'earned':''}">${icon(a.cleared?'shield':'lock')}<div><b>${a.boss}</b><p>${a.name} · ${a.cleared?'战功已获：+2 攻击 / +8 生命':'首次击败可获得永久战功'}</p></div></article>`).join('');
  const goal = state.trophies === 6 && state.weapons.some(w=>w.recipe===5 && w.rarity===4 && w.upgrade===10);
  $('#page-journal .journey-banner>p').textContent = goal ? '六域已解放！传说品质的黎明神剑 +10，因你而生。' : '终极目标：解放六域，打造传说品质的「黎明神剑 +10」。';
}

function render() {
  if (!state) return;
  if (!state.items[2]) protectedUpgrade = false;
  renderCommon(); renderForge(); renderExplore(); renderInventory(); renderShop(); renderJournal();
}

async function connect() {
  if (closed) return;
  try {
    const response = await fetch('/api/state', {cache:'no-store', signal:AbortSignal.timeout(4000)});
    if (!response.ok) throw new Error('游戏连接未就绪');
    state = await response.json(); $('#connection-error').hidden = true; render();
    if (state.event.kind >= 0) {areaIndex=state.event.area; switchPage('explore'); renderExplore();}
    if (state.corrupted) { toast('原存档无法读取，旧文件已保留。请在旅程设置里处理。'); $('#settings-dialog').showModal(); }
  } catch { $('#connection-error').hidden = false; }
}

async function act(action, p = 0, q = 0) {
  if (busy || closed || !state) return null;
  busy = true; render();
  if (action === 'forge') { $('#forge-scene').classList.add('forging'); playSound('forge'); }
  try {
    const response = await fetch('/api/action', {method:'POST', body:new URLSearchParams({action,p,q}), signal:AbortSignal.timeout(6000)});
    if (!response.ok) throw new Error('连接异常，请重新启动游戏。');
    const next = await response.json();
    if (action === 'forge') await new Promise(resolve=>setTimeout(resolve,750));
    state = next;
    if (['explore','boss','gather','event','battle'].includes(action)) lastAdventure = state.message;
    log(state.message); toast(state.message);
    if (state.result === 0) {
      playSound(action === 'battle' && !state.battle.active ? 'win' : 'click');
      if (action === 'forge') showResult(state.latest);
      if (action === 'gather') {switchPage('explore'); requestAnimationFrame(()=>$('#exploration-event').scrollIntoView({behavior:'smooth',block:'center'}));}
      if (action === 'explore' || action === 'boss') { switchPage('explore'); requestAnimationFrame(()=>$('#battle-panel').scrollIntoView({behavior:'smooth',block:'center'})); }
      if (action === 'new') { recipeIndex = 0; weaponId = null; areaIndex=0; lastAdventure=''; switchPage('forge'); }
      if (action === 'quit') {
        closed = true;
        $('#connection-error').hidden = false;
        $('#connection-error h2').textContent = '炉火暂歇，明日再会';
        $('#connection-error p').textContent = state.corrupted ? '原存档文件已保留。现在可以关闭游戏窗口。' : '进度已保存。现在可以关闭游戏窗口，下次双击启动游戏继续。';
        $('#reconnect-button').hidden = true;
      }
    }
    return state;
  } catch (error) {
    // 不自动重发动作，避免网络错误造成重复扣费；只重新获取当前状态。
    toast(error.message || '连接失败，请重新启动游戏。');
    await connect(); return null;
  } finally { busy = false; $('#forge-scene').classList.remove('forging'); render(); }
}

function showResult(index) {
  const w = state.weapons[index];
  if (!w) return;
  weaponId = w.id;
  const dialog = $('#result-dialog');
  dialog.style.borderColor = colors[w.rarity];
  $('#result-content').innerHTML = `${swordArt(w.recipe,w.rarity)}<p class="rarity-label" style="--rarity:${colors[w.rarity]}">${rarityNames[w.rarity]}品质</p><h3>${state.recipes[w.recipe].name}</h3><div class="result-detail">武器攻击 <b>${w.attack}</b><br>${w.rarity >= 3 ? '炉火间闪过一抹不同寻常的光。' : '新的锋芒，将与你一同踏入荒野。'}</div>`;
  dialog.showModal();
}

function confirm(title, text) {
  $('#confirm-title').textContent = title; $('#confirm-body').textContent = text;
  $('#confirm-dialog').showModal();
  return new Promise(resolve=>{confirmResolve = resolve;});
}
function finishConfirm(answer) {
  const resolve = confirmResolve; confirmResolve = null;
  $('#confirm-dialog').close(); resolve?.(answer);
}

document.addEventListener('click', async event => {
  const button = event.target.closest('button');
  if (!button || button.disabled) return;
  if (button.dataset.page) return switchPage(button.dataset.page);
  if (button.classList.contains('dialog-close')) return button.closest('dialog').close();
  if (button.dataset.recipe !== undefined) { recipeIndex = Number(button.dataset.recipe); renderForge(); playSound(); return; }
  if (button.dataset.weapon !== undefined) { weaponId = Number(button.dataset.weapon); renderInventory(); return; }
  if (button.dataset.filter !== undefined) {
    filter = Number(button.dataset.filter);
    document.querySelectorAll('.filter').forEach(el=>el.classList.toggle('active',el===button));
    renderInventory(); return;
  }
  if (button.dataset.area !== undefined) {areaIndex=Number(button.dataset.area); renderExplore(); return;}
  if (button.dataset.gather !== undefined) return act('gather',Number(button.dataset.gather));
  if (button.dataset.boss !== undefined) return act('boss',Number(button.dataset.boss));
  if (button.dataset.event !== undefined) return act('event',Number(button.dataset.event));
  if (button.dataset.explore !== undefined) return act('explore',Number(button.dataset.explore));
  if (button.dataset.battle !== undefined) return act('battle',Number(button.dataset.battle));
  if (button.dataset.buy !== undefined) {
    const item = Number(button.dataset.buy), n = Number($(`#quantity-${item}`).value);
    if (!Number.isInteger(n) || n < 1 || n > 99) return toast('购买数量请输入 1～99 的整数。');
    return act('buy', item, n);
  }
  const {weapon:w,index} = state ? selectedWeapon() : {weapon:null,index:-1};
  switch(button.id) {
    case 'forge-button': return act('forge',recipeIndex,Number($('#lucky-toggle').checked));
    case 'rest-button':
    case 'explore-rest':
      if (state.streak && !await confirm('回到营地休息？',`生命和体力会免费回满，但连续 ${state.streak} 胜会重置，下一场收益加成回到 0%。使用体力药水可以保留连续奖励。`)) return;
      return act('rest');
    case 'use-stamina': return act('stamina');
    case 'equip-button': return act('equip',index);
    case 'upgrade-button': return act('upgrade',index,Number(protectedUpgrade));
    case 'salvage-button':
      if (await confirm('让旧剑化作新的可能？',`回收「${state.recipes[w.recipe].name} +${w.upgrade}」将获得 ${2+w.rarity} 铁矿和 ${10+w.rarity*12+w.upgrade*4} 金币。这把武器会从背包中移除。`)) return act('salvage',index);
      return;
    case 'save-button': return act('save');
    case 'settings-button': return $('#settings-dialog').showModal();
    case 'equip-result': $('#result-dialog').close(); return act('equip',index);
    case 'confirm-yes': return finishConfirm(true);
    case 'confirm-cancel': return finishConfirm(false);
    case 'reconnect-button': return connect();
    case 'sound-button':
      soundEnabled = !soundEnabled; button.setAttribute('aria-pressed',String(soundEnabled));
      button.title = soundEnabled ? '关闭音效' : '开启音效'; button.setAttribute('aria-label',button.title);
      playSound('forge'); toast(soundEnabled?'已开启音效':'音效已关闭'); return;
    case 'load-button':
      $('#settings-dialog').close();
      if (await confirm('读取最近的存档？','游戏会恢复到最近一次保存的进度。自动保存会在每次有效操作后进行。')) return act('load');
      return;
    case 'new-button':
      $('#settings-dialog').close();
      if (await confirm('开启一段全新的旅程？','当前存档会先备份到 saves/game.before-new.sav，然后重置角色、装备和背包。')) return act('new',1);
      return;
    case 'quit-button': $('#settings-dialog').close(); return act('quit');
  }
});

$('#lucky-toggle').addEventListener('change',renderForge);
$('#weapon-sort').addEventListener('change',event=>{sort=event.target.value;renderInventory();});
$('#weapon-detail').addEventListener('change',event=>{if(event.target.id==='protect-toggle'){protectedUpgrade=event.target.checked;renderInventory();}});
$('#confirm-dialog').addEventListener('cancel',()=>finishConfirm(false));
$('#shop-list').addEventListener('input',event=>{
  if (event.target.dataset.quantity === undefined || !state) return;
  const i=Number(event.target.dataset.quantity),n=Number(event.target.value),price=[20,65,90,35][i];
  const button=$(`[data-buy="${i}"]`),valid=Number.isInteger(n)&&n>=1&&n<=99;
  button.innerHTML=`${icon('coin')}购入 · ${valid?price*n:'—'} 金币`;
  button.disabled=!valid||busy||state.battle.active||state.corrupted||state.gold<price*n;
});
setInterval(()=>{if(!busy&&!closed)connect();},20000);
connect();
