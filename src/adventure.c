#include "game.h"
#include <string.h>

int area_unlocked(const Game *g, int area) {
    return area >= 0 && area < AREA_COUNT && g->level >= areas[area].unlock_level &&
        (area == 0 || (g->boss_flags & (1 << (area - 1))));
}

static int begin(Game *g, int area, int boss, Battle *battle) {
    if (area < 0 || area >= AREA_COUNT) return BAD_CHOICE;
    if (!area_unlocked(g, area) || g->event_kind >= 0 || g->delve.active) return LOCKED;
    if (boss && g->area_wins[area] < 2) return LOCKED;
    int cost = areas[area].energy + (boss ? 5 : 0);
    if (g->energy < cost) return NO_RESOURCE;
    memset(battle, 0, sizeof(*battle));
    g->energy -= cost;
    battle->area = area; battle->boss = boss;
    battle->enemy = random_below(g, 3);
    battle->hp = battle->max_hp = (boss ? areas[area].boss_hp : areas[area].hp) + random_below(g, 16);
    battle->attack = (boss ? areas[area].boss_attack : areas[area].attack) + random_below(g, 4);
    battle->active = 1;
    return OK;
}
int battle_begin(Game *g, int area, Battle *battle) { return begin(g, area, 0, battle); }
int boss_begin(Game *g, int area, Battle *battle) { return begin(g, area, 1, battle); }

/* 采集路线不打怪，也不伤血：稳定补材料后遇到一个可选择的事件。 */
int game_gather(Game *g, int area) {
    if (area < 0 || area >= AREA_COUNT) return BAD_CHOICE;
    if (!area_unlocked(g, area) || g->event_kind >= 0) return LOCKED;
    if (g->energy < 10) return NO_RESOURCE;
    g->energy -= 10;
    game_add_resource(&g->materials[IRON], 2 + area);
    game_add_resource(&g->materials[WOOD], 2);
    game_add_resource(&g->materials[areas[area].material], 2 + random_below(g, 3));
    game_add_resource(&g->surveys[area], 1);
    game_add_xp(g, 8 + area * 5, 0);
    g->event_area = area; g->event_kind = random_below(g, 4);
    return OK;
}

/* 事件始终有一个免费选项；失败的交易不扣款、不结束事件。 */
int game_resolve_event(Game *g, int choice) {
    if (g->event_kind < 0 || choice < 0 || choice > 1) return BAD_CHOICE;
    int area = g->event_area;
    switch (g->event_kind) {
    case 0: /* 封存宝箱：材料或金币。 */
        if (choice == 0) game_add_resource(&g->materials[areas[area].material], 4 + area);
        else game_add_resource(&g->gold, 35 + area * 20);
        break;
    case 1: /* 清泉：生命或体力；两种都保留连续胜利。 */
        if (choice == 0) { g->hp += 45; if (g->hp > game_max_hp(g)) g->hp = game_max_hp(g); }
        else { g->energy += 15; if (g->energy > 100) g->energy = 100; }
        break;
    case 2: /* 古代祭坛：角色成长或锻造师成长。 */
        if (choice == 0) game_add_xp(g, 35 + area * 12, 0);
        else game_add_xp(g, 0, 25 + area * 5);
        break;
    case 3: /* 流浪药师。 */
        if (choice == 0) {
            if (g->gold < 25) return NO_RESOURCE;
            if (g->items[STAMINA] == RESOURCE_LIMIT) return MAXED;
            g->gold -= 25; game_add_resource(&g->items[STAMINA], 1);
        } else game_add_resource(&g->materials[WOOD], 3 + area);
        break;
    default: return BAD_CHOICE;
    }
    g->event_area = g->event_kind = -1;
    return OK;
}

static void battle_reward(Game *g, Battle *battle, BattleReport *report) {
    int area = battle->area;
    if (g->streak < 5) ++g->streak;
    int bonus = (g->streak - 1) * 10;
    int coins = areas[area].gold + random_below(g, 21);
    if (battle->delve && (g->delve.buffs & 4)) coins += coins / 4;
    game_add_resource(&g->gold, coins + coins * bonus / 100);
    game_add_resource(&g->materials[IRON], 3 + area * 2 + random_below(g, 4));
    game_add_resource(&g->materials[WOOD], 2 + area / 2 + random_below(g, 3));
    if (g->streak >= 3) game_add_resource(&g->materials[IRON], 1);
    if (area >= 1) game_add_resource(&g->materials[CRYSTAL], 1 + area / 2 + random_below(g, 3));
    else if (random_below(g, 100) < 20) game_add_resource(&g->materials[CRYSTAL], 1);
    if (area >= 2) game_add_resource(&g->materials[STAR], 1 + area / 2 + random_below(g, 2));
    else if (area == 1 && random_below(g, 100) < 15) game_add_resource(&g->materials[STAR], 1);
    if (random_below(g, 100) < 18) game_add_resource(&g->items[POTION], 1);
    if (random_below(g, 100) < 8) game_add_resource(&g->items[LUCK], 1);
    if (random_below(g, 100) < 8) game_add_resource(&g->items[STAMINA], 1);
    game_add_resource(&g->wins, 1);
    game_add_xp(g, areas[area].xp + areas[area].xp * bonus / 100, 0);
    if (battle->delve) {
        g->delve.visited |= 1 << g->delve.room;
        if (battle->elite) {
            g->delve.reward = g->delve.buffs == 7 ? 0 : 1;
            game_add_resource(&g->materials[areas[area].material], 3 + g->delve.floor);
        }
        if (g->delve.buffs & 2) { g->hp += 25; if(g->hp>game_max_hp(g))g->hp=game_max_hp(g); }
    }
    else if (!battle->boss) game_add_resource(&g->area_wins[area], 1);
    else if (!(g->boss_flags & (1 << area))) {
        g->boss_flags |= 1 << area;
        report->first_clear = 1;
        game_add_resource(&g->gold, 120 + area * 90);
        game_add_resource(&g->items[LUCK], 1);
        game_add_resource(&g->items[PROTECTION], 1);
        if (area >= 1) game_add_resource(&g->materials[STAR], 1 + area);
        game_add_xp(g, 50 + area * 30, 0);
        g->hp = game_max_hp(g); /* 首通庆祝：生命回满，永久 +8 生命 / +2 攻击。 */
    } else { /* 可反复挑战，但不重复领取首通奖励。 */
        game_add_resource(&g->gold, 40 + area * 20);
        game_add_resource(&g->materials[areas[area].material], 2);
    }
}

static int step(Game *g, Battle *battle, int action, BattleReport *report) {
    int status;
    memset(report, 0, sizeof(*report));
    if (!battle->active || battle->area < 0 || battle->area >= AREA_COUNT || action < ATTACK || action > SKILL)
        return BAD_CHOICE;
    if (action == SKILL && battle->skill_cooldown) return LOCKED;
    if (action == HEAL) {
        int old_hp = g->hp;
        status = game_use_potion(g);
        if (status != OK) return status;
        report->healed = g->hp - old_hp;
    }
    ++battle->turn;
    if (action == SKILL) battle->skill_cooldown = 3;
    else if (battle->skill_cooldown > 0) --battle->skill_cooldown;
    if (action == ESCAPE && random_below(g, 100) < 75) {
        g->streak = 0;
        battle->active = 0; report->outcome = 3;
        if (battle->delve) (void)delve_leave(g);
        return OK;
    }
    if (action == ATTACK || action == DEFEND || action == SKILL) {
        report->player_damage = game_attack(g) + random_below(g, 7);
        if (battle->delve && (g->delve.buffs & 1)) report->player_damage += report->player_damage / 4;
        if (action == DEFEND) report->player_damage = (report->player_damage + 1) / 2;
        else if (action == SKILL) report->player_damage *= 2;
        else if (random_below(g, 100) < 12) report->player_damage *= 2;
        battle->hp -= report->player_damage;
    }
    if (battle->hp <= 0) {
        battle->hp = 0; battle->active = 0; report->outcome = 1;
        battle_reward(g, battle, report);
        return OK;
    }
    report->enemy_damage = battle->attack + random_below(g, 5);
    report->heavy = (battle->boss || battle->elite) && battle->turn % 3 == 0;
    if (report->heavy) report->enemy_damage = report->enemy_damage * 2;
    if (action == DEFEND) report->enemy_damage = (report->enemy_damage + 2) / 3;
    g->hp -= report->enemy_damage;
    if (g->hp <= 0) {
        g->streak = 0;
        battle->active = 0; report->outcome = 2;
        g->hp = game_max_hp(g) / 2;
        g->gold -= g->gold / 10;
        if (battle->delve) (void)delve_leave(g);
    }
    return OK;
}

int battle_step(Game *g, Battle *battle, int action, BattleReport *report) {
    int result = step(g, battle, action, report);
    if (result == OK && battle->delve && g->delve.active) g->delve.battle = *battle;
    return result;
}
