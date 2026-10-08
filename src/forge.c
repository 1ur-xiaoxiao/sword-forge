#include "game.h"
#include <limits.h>

void forge_weights(const Game *g, int recipe, int lucky, int out[RARITY_COUNT]) {
    int boost, take;
    if (recipe < 0 || recipe >= RECIPE_COUNT) {
        for (int i = 0; i < RARITY_COUNT; ++i) out[i] = 0;
        return;
    }
    for (int i = 0; i < RARITY_COUNT; ++i) out[i] = recipes[recipe].weights[i];
    /* 锻造师等级与幸运符把低品质概率转移到高品质，总概率保持 100%。 */
    boost = (g->smith_level - 1) * 50 + (lucky ? 1200 : 0);
    take = boost < out[COMMON] ? boost : out[COMMON];
    out[COMMON] -= take;
    boost -= take;
    int from_uncommon = boost < out[UNCOMMON] ? boost : out[UNCOMMON];
    out[UNCOMMON] -= from_uncommon;
    boost -= from_uncommon;
    take += from_uncommon;
    out[RARE] += take * 50 / 100;
    out[EPIC] += take * 35 / 100;
    out[LEGENDARY] += take - take * 50 / 100 - take * 35 / 100;
    /* 高阶配方没有普通/精良时，仍可将稀有概率转到史诗/传说。 */
    int from_rare = boost < out[RARE] ? boost : out[RARE];
    out[RARE] -= from_rare;
    out[EPIC] += from_rare * 70 / 100;
    out[LEGENDARY] += from_rare - from_rare * 70 / 100;
    if (g->pity >= 19) {
        /* 连续 19 次没有史诗/传说，第 20 次必得史诗或传说。 */
        out[EPIC] += out[COMMON] + out[UNCOMMON] + out[RARE];
        out[COMMON] = out[UNCOMMON] = out[RARE] = 0;
    }
}

int forge_weapon(Game *g, int recipe, int lucky, int *new_index) {
    const Recipe *r;
    int weights[RARITY_COUNT], roll, rarity = LEGENDARY;
    if (recipe < 0 || recipe >= RECIPE_COUNT || (lucky != 0 && lucky != 1)) return BAD_CHOICE;
    r = &recipes[recipe];
    if (g->level < r->unlock_level) return LOCKED;
    if (g->weapon_count == MAX_WEAPONS) return BAG_FULL;
    if (g->next_id == INT_MAX) return MAXED;
    if (g->gold < r->gold || (lucky && g->items[LUCK] == 0)) return NO_RESOURCE;
    for (int i = 0; i < MATERIAL_COUNT; ++i)
        if (g->materials[i] < r->materials[i]) return NO_RESOURCE;
    forge_weights(g, recipe, lucky, weights);
    g->gold -= r->gold;
    for (int i = 0; i < MATERIAL_COUNT; ++i) g->materials[i] -= r->materials[i];
    if (lucky) --g->items[LUCK];
    roll = random_below(g, 10000);
    for (int i = 0; i < RARITY_COUNT; ++i) {
        if (roll < weights[i]) { rarity = i; break; }
        roll -= weights[i];
    }
    g->weapons[g->weapon_count] = (Weapon){g->next_id++, recipe, rarity,
                                         r->attack + rarity * 9 + random_below(g, 6), 0};
    if (new_index) *new_index = g->weapon_count;
    ++g->weapon_count;
    game_add_resource(&g->forged, 1);
    g->pity = rarity >= EPIC ? 0 : g->pity + 1;
    game_add_xp(g, 8, 12 + rarity * 4);
    return OK;
}

int upgrade_chance(const Game *g, int index) {
    int chance;
    if (index < 0 || index >= g->weapon_count) return 0;
    chance = 95 - g->weapons[index].upgrade * 7 + (g->smith_level - 1) * 2;
    return chance > 100 ? 100 : chance;
}

int upgrade_cost(const Game *g, int index) {
    if (index < 0 || index >= g->weapon_count) return 0;
    return 20 + g->weapons[index].upgrade * 15 + g->weapons[index].rarity * 10;
}

int upgrade_weapon(Game *g, int index, int protect, int *success) {
    Weapon *w;
    int cost, iron, chance;
    if (index < 0 || index >= g->weapon_count || (protect != 0 && protect != 1)) return BAD_CHOICE;
    w = &g->weapons[index];
    if (w->upgrade == WEAPON_LEVEL_MAX) return MAXED;
    cost = upgrade_cost(g, index);
    iron = 2 + w->upgrade / 2;
    if (g->gold < cost || g->materials[IRON] < iron || (protect && g->items[PROTECTION] == 0)) return NO_RESOURCE;
    chance = upgrade_chance(g, index);
    g->gold -= cost;
    g->materials[IRON] -= iron;
    if (protect) --g->items[PROTECTION];
    if (random_below(g, 100) < chance) {
        ++w->upgrade;
        if (success) *success = 1;
    } else {
        if (!protect && w->upgrade > 0) --w->upgrade;
        if (success) *success = 0;
    }
    game_add_xp(g, 2, 6);
    return OK;
}
