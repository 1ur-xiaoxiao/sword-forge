#include "game.h"
#include <limits.h>
#include <string.h>

uint32_t random_next(Game *g) {
    /* xorshift32：只有 3 次位运算，测试时给定种子可重现结果。 */
    uint32_t x = g->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g->rng = x;
    return x;
}

int random_below(Game *g, int bound) {
    uint32_t n, limit;
    if (bound <= 1) return 0;
    /* xorshift 的非零状态范围为 1..UINT32_MAX，拒绝尾部以消除取模偏差。 */
    limit = UINT32_MAX - UINT32_MAX % (uint32_t)bound;
    do { n = random_next(g); } while (n > limit);
    return (int)((n - 1u) % (uint32_t)bound);
}

void game_init(Game *g, uint32_t seed) {
    memset(g, 0, sizeof(*g));
    g->rng = seed ? seed : 0xA13F25u;
    g->level = g->smith_level = 1;
    g->hp = 100;
    g->energy = 100;
    g->gold = 180;
    g->day = 1;
    g->materials[IRON] = 12;
    g->materials[WOOD] = 6;
    g->items[POTION] = 3;
    g->items[LUCK] = 1;
    g->items[PROTECTION] = 1;
    g->items[STAMINA] = 1;
    g->weapon_count = 1;
    g->equipped = 0;
    g->next_id = 2;
    g->event_area = g->event_kind = -1;
    g->weapons[0] = (Weapon){1, 0, COMMON, 10, 0};
}

int game_trophies(const Game *g) {
    int n = 0;
    for (int i = 0; i < AREA_COUNT; ++i) if (g->boss_flags & (1 << i)) ++n;
    return n;
}
int game_max_hp(const Game *g) { return 100 + (g->level - 1) * 12 + game_trophies(g) * 8; }
int weapon_attack(const Weapon *w) { return w->base_attack + w->upgrade * (3 + w->rarity); }
int game_attack(const Game *g) {
    int value = 6 + (g->level - 1) * 3 + game_trophies(g) * 2;
    if (g->equipped >= 0 && g->equipped < g->weapon_count)
        value += weapon_attack(&g->weapons[g->equipped]);
    return value;
}
int level_xp_needed(int level) { return 40 + (level - 1) * 25; }
int smith_xp_needed(int level) { return 30 + (level - 1) * 20; }

void game_add_resource(int *value, int amount) {
    /* 所有经济资源饱和到上限，避免长时间游玩产生整数溢出。 */
    if (amount > 0) *value = amount > RESOURCE_LIMIT - *value ? RESOURCE_LIMIT : *value + amount;
}

void game_add_xp(Game *g, int xp, int smith_xp) {
    game_add_resource(&g->xp, xp);
    game_add_resource(&g->smith_xp, smith_xp);
    while (g->level < MAX_LEVEL && g->xp >= level_xp_needed(g->level)) {
        g->xp -= level_xp_needed(g->level);
        ++g->level;
        g->hp = game_max_hp(g);
    }
    while (g->smith_level < MAX_SMITH && g->smith_xp >= smith_xp_needed(g->smith_level)) {
        g->smith_xp -= smith_xp_needed(g->smith_level);
        ++g->smith_level;
    }
    if (g->level == MAX_LEVEL) g->xp = 0;
    if (g->smith_level == MAX_SMITH) g->smith_xp = 0;
}

void game_rest(Game *g) {
    g->hp = game_max_hp(g);
    g->energy = 100;
    g->streak = 0;
    game_add_resource(&g->day, 1);
}

int game_use_potion(Game *g) {
    if (g->hp == game_max_hp(g)) return MAXED;
    if (g->items[POTION] <= 0) return NO_RESOURCE;
    --g->items[POTION];
    g->hp += 55;
    if (g->hp > game_max_hp(g)) g->hp = game_max_hp(g);
    return OK;
}

int game_buy(Game *g, int item, int count) {
    static const int prices[ITEM_COUNT] = {20, 65, 90, 35};
    int cost;
    if (item < 0 || item >= ITEM_COUNT || count < 1 || count > 99) return BAD_CHOICE;
    cost = prices[item] * count;
    if (g->gold < cost) return NO_RESOURCE;
    if (g->items[item] > RESOURCE_LIMIT - count) return MAXED;
    g->gold -= cost;
    g->items[item] += count;
    return OK;
}

int game_use_stamina(Game *g) {
    if (g->energy >= 100) return MAXED;
    if (g->items[STAMINA] <= 0) return NO_RESOURCE;
    --g->items[STAMINA];
    g->energy += 40;
    if (g->energy > 100) g->energy = 100;
    return OK;
}

int game_equip(Game *g, int index) {
    if (index < 0 || index >= g->weapon_count) return BAD_CHOICE;
    g->equipped = index;
    return OK;
}

int game_salvage(Game *g, int index) {
    Weapon old;
    if (index < 0 || index >= g->weapon_count) return BAD_CHOICE;
    if (index == g->equipped) return LOCKED;
    old = g->weapons[index];
    game_add_resource(&g->materials[IRON], 2 + old.rarity);
    game_add_resource(&g->gold, 10 + old.rarity * 12 + old.upgrade * 4);
    /* 删除后挪动数组，并同步修正装备下标。 */
    for (int i = index; i + 1 < g->weapon_count; ++i) g->weapons[i] = g->weapons[i + 1];
    --g->weapon_count;
    memset(&g->weapons[g->weapon_count], 0, sizeof(Weapon));
    if (g->equipped > index) --g->equipped;
    return OK;
}

int game_claim_quests(Game *g) {
    int newly = 0, epic = 0;
    for (int i = 0; i < g->weapon_count; ++i)
        if (g->weapons[i].rarity >= EPIC) epic = 1;
    if (!(g->quest_flags & 1) && g->forged >= 5) {
        g->quest_flags |= 1; newly |= 1;
        game_add_resource(&g->gold, 100); game_add_resource(&g->items[LUCK], 2);
    }
    if (!(g->quest_flags & 2) && g->wins >= 10) {
        g->quest_flags |= 2; newly |= 2;
        game_add_resource(&g->gold, 200); game_add_resource(&g->materials[STAR], 2);
    }
    if (!(g->quest_flags & 4) && epic) {
        g->quest_flags |= 4; newly |= 4;
        game_add_resource(&g->gold, 180); game_add_resource(&g->items[PROTECTION], 2);
    }
    return newly;
}

static int in_range(int n, int low, int high) { return n >= low && n <= high; }

int game_validate(const Game *g) {
    if (!g->rng || !in_range(g->level, 1, MAX_LEVEL) || !in_range(g->smith_level, 1, MAX_SMITH)) return 0;
    if (!in_range(g->xp, 0, level_xp_needed(g->level) - 1) ||
        !in_range(g->smith_xp, 0, smith_xp_needed(g->smith_level) - 1)) return 0;
    if ((g->level == MAX_LEVEL && g->xp) || (g->smith_level == MAX_SMITH && g->smith_xp)) return 0;
    if (!in_range(g->hp, 1, game_max_hp(g)) || !in_range(g->energy, 0, 100) ||
        !in_range(g->gold, 0, RESOURCE_LIMIT) || !in_range(g->day, 1, RESOURCE_LIMIT)) return 0;
    if (!in_range(g->weapon_count, 1, MAX_WEAPONS) || !in_range(g->equipped, -1, g->weapon_count - 1)) return 0;
    if (!in_range(g->forged, 0, RESOURCE_LIMIT) || !in_range(g->wins, 0, RESOURCE_LIMIT) ||
        !in_range(g->pity, 0, 19) || !in_range(g->quest_flags, 0, 7) || g->next_id < 2) return 0;
    if (!in_range(g->streak, 0, 5)) return 0;
    if (!in_range(g->boss_flags, 0, (1 << AREA_COUNT) - 1)) return 0;
    for (int i = 0; i < AREA_COUNT; ++i) {
        if (!in_range(g->area_wins[i], 0, RESOURCE_LIMIT) || !in_range(g->surveys[i], 0, RESOURCE_LIMIT)) return 0;
        if ((g->boss_flags & (1 << i)) && (g->area_wins[i] < 2 || (i && !(g->boss_flags & (1 << (i - 1)))))) return 0;
    }
    if (g->event_kind == -1) { if (g->event_area != -1) return 0; }
    else if (!in_range(g->event_kind, 0, 3) || !area_unlocked(g, g->event_area)) return 0;
    for (int i = 0; i < MATERIAL_COUNT; ++i) if (!in_range(g->materials[i], 0, RESOURCE_LIMIT)) return 0;
    for (int i = 0; i < ITEM_COUNT; ++i) if (!in_range(g->items[i], 0, RESOURCE_LIMIT)) return 0;
    for (int i = 0; i < g->weapon_count; ++i) {
        const Weapon *w = &g->weapons[i];
        if (!in_range(w->id, 1, g->next_id - 1) || !in_range(w->recipe, 0, RECIPE_COUNT - 1) ||
            !in_range(w->rarity, 0, RARITY_COUNT - 1) || !in_range(w->upgrade, 0, WEAPON_LEVEL_MAX)) return 0;
        /* 基础攻击 = 配方攻击 + 品质增量 + 0..5 波动。 */
        if (!in_range(w->base_attack, recipes[w->recipe].attack + w->rarity * 9,
                      recipes[w->recipe].attack + w->rarity * 9 + 5)) return 0;
        for (int j = 0; j < i; ++j) if (g->weapons[j].id == w->id) return 0;
    }
    return 1;
}
