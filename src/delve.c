/* 秘境规则是本项目的原创 C 实现；地图、寻路和视野复用 BSD rot.js。 */
#include "game.h"
#include <string.h>

int delve_leave(Game *g) {
    if (!g->delve.active) return BAD_CHOICE;
    int clears = g->delve.clears;
    memset(&g->delve, 0, sizeof(g->delve)); g->delve.clears = clears;
    return OK;
}
int delve_begin(Game *g, int area) {
    if (area < 0 || area >= AREA_COUNT) return BAD_CHOICE;
    if (g->delve.active || g->event_kind >= 0 || !area_unlocked(g, area)) return LOCKED;
    if (g->energy < 20) return NO_RESOURCE;
    int clears = g->delve.clears;
    memset(&g->delve, 0, sizeof(g->delve));
    g->delve.clears = clears; g->delve.active = 1; g->delve.area = area;
    g->delve.seed = 1 + random_below(g, 1000000000);
    g->delve.floor = 1; g->delve.visited = 1; g->energy -= 20;
    return OK;
}
int delve_blessing(Game *g, int choice) {
    if (choice < 0 || choice > 2) return BAD_CHOICE;
    if (!g->delve.active || !g->delve.reward || (g->delve.buffs & (1 << choice))) return LOCKED;
    g->delve.buffs |= 1 << choice; g->delve.reward = 0;
    return OK;
}
int delve_room(Game *g, int room, int choice, Battle *battle) {
    if (room < 1 || room > 5 || choice < 0 || choice > 1) return BAD_CHOICE;
    Delve *d = &g->delve;
    if (!d->active || d->reward || d->battle.active || (d->visited & (1 << room))) return LOCKED;
    int area = d->area;
    if (room == 5) {
        if ((d->visited & 24) != 24) return LOCKED; /* 击败本层普通敌人和精英。 */
        game_add_resource(&g->gold, 60 + area * 30 + d->floor * 20);
        game_add_resource(&g->materials[areas[area].material], 4 + d->floor + area);
        if (d->floor == 3) {
            game_add_resource(&g->gold, 200 + area * 100);
            game_add_resource(&g->items[LUCK], 2); game_add_resource(&g->items[PROTECTION], 1);
            game_add_resource(&g->materials[STAR], 2 + area);
            game_add_resource(&d->clears, 1); return delve_leave(g);
        }
        ++d->floor; d->seed = 1 + random_below(g, 1000000000); d->visited = 1;
        d->room = 0; memset(&d->battle, 0, sizeof(d->battle));
        g->hp += 20; if (g->hp > game_max_hp(g)) g->hp = game_max_hp(g);
    } else if (room == 1) {
        if (choice == 0) {
            game_add_resource(&g->materials[areas[area].material], 5 + area * 3 + d->floor * 2);
            game_add_resource(&g->materials[WOOD], 3);
        } else game_add_resource(&g->gold, 45 + area * 20 + d->floor * 15);
        d->visited |= 1 << room;
    } else if (room == 2) {
        if (choice == 0) { g->hp += 40; if (g->hp > game_max_hp(g)) g->hp = game_max_hp(g); }
        else { g->energy += 15; if (g->energy > 100) g->energy = 100; }
        d->visited |= 1 << room;
    } else {
        memset(battle, 0, sizeof(*battle));
        battle->area = area; battle->delve = 1; battle->elite = room == 4;
        battle->enemy = random_below(g, 3);
        battle->max_hp = areas[area].hp + random_below(g, 16) + (d->floor - 1) * 18;
        battle->attack = areas[area].attack + (d->floor - 1) * 2 + random_below(g, 3);
        if (battle->elite) { battle->max_hp = battle->max_hp * 3 / 2; battle->attack += 3; }
        battle->hp = battle->max_hp; battle->active = 1;
        d->room = room; d->battle = *battle;
    }
    return OK;
}
