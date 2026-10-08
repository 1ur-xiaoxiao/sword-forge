#include "game.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static int groups = 0;
static void passed(const char *name) { printf("PASS %02d: %s\n", ++groups, name); }
static void rich(Game *g) {
    g->gold = RESOURCE_LIMIT;
    for (int i = 0; i < MATERIAL_COUNT; ++i) g->materials[i] = RESOURCE_LIMIT;
    for (int i = 0; i < ITEM_COUNT; ++i) g->items[i] = 1000;
}

static void test_initial_rng(void) {
    Game a, b;
    game_init(&a, 0); assert(game_validate(&a)); assert(a.rng != 0);
    game_init(&a, 42); game_init(&b, 42);
    for (int i = 0; i < 10000; ++i) assert(random_next(&a) == random_next(&b));
    assert(random_below(&a, 0) == 0);
    for (int i = 0; i < 10000; ++i) {
        int n = random_below(&a, 10000); assert(n >= 0 && n < 10000);
    }
    passed("initial state, reproducible random sequence, random bounds");
}

static void test_forge_guards(void) {
    Game g, before;
    int index = -1;
    game_init(&g, 1); before = g;
    assert(forge_weapon(&g, -1, 0, &index) == BAD_CHOICE);
    assert(forge_weapon(&g, 1, 0, &index) == LOCKED);
    assert(memcmp(&g, &before, sizeof(g)) == 0);
    g.gold = 0; before = g;
    assert(forge_weapon(&g, 0, 1, &index) == NO_RESOURCE);
    assert(memcmp(&g, &before, sizeof(g)) == 0 && index == -1);
    game_init(&g, 1); g.materials[WOOD] = 0; before = g;
    assert(forge_weapon(&g, 0, 1, &index) == NO_RESOURCE);
    assert(memcmp(&g, &before, sizeof(g)) == 0);
    game_init(&g, 1); g.items[LUCK] = 0; before = g;
    assert(forge_weapon(&g, 0, 1, &index) == NO_RESOURCE);
    assert(memcmp(&g, &before, sizeof(g)) == 0);
    game_init(&g, 1); g.weapon_count = MAX_WEAPONS; before = g;
    assert(forge_weapon(&g, 0, 0, &index) == BAG_FULL);
    assert(memcmp(&g, &before, sizeof(g)) == 0);
    passed("forge rejects locked recipe, missing resources and full bag without mutation");
}

static void test_probabilities_pity(void) {
    Game g;
    int plain[5], lucky[5], index;
    game_init(&g, 100);
    for (int level = 1; level <= MAX_SMITH; ++level) {
        g.smith_level = level;
        for (int recipe = 0; recipe < RECIPE_COUNT; ++recipe) {
            forge_weights(&g, recipe, 0, plain);
            forge_weights(&g, recipe, 1, lucky);
            int sum = 0, sum_luck = 0;
            for (int i = 0; i < 5; ++i) { assert(plain[i] >= 0 && lucky[i] >= 0); sum += plain[i]; sum_luck += lucky[i]; }
            assert(sum == 10000 && sum_luck == 10000);
            assert(lucky[EPIC] + lucky[LEGENDARY] > plain[EPIC] + plain[LEGENDARY]);
        }
    }
    g.level = MAX_LEVEL;
    for (int i = 0; i < RECIPE_COUNT; ++i) {
        rich(&g); g.pity = 19;
        assert(forge_weapon(&g, i, 0, &index) == OK);
        assert(g.weapons[index].rarity >= EPIC && g.pity == 0);
        assert(game_validate(&g));
    }
    passed("all probabilities total 100%, luck improves odds, twentieth forge guaranteed epic+");
}

static void test_long_forging(void) {
    Game g;
    int index, seen[5] = {0}, previous;
    game_init(&g, 89123); g.level = MAX_LEVEL;
    for (int i = 0; i < 5000; ++i) {
        rich(&g); previous = g.next_id;
        assert(forge_weapon(&g, i % RECIPE_COUNT, i % 2, &index) == OK);
        assert(g.weapons[index].id == previous);
        ++seen[g.weapons[index].rarity];
        assert(game_validate(&g));
        assert(game_salvage(&g, index) == OK);
    }
    for (int i = 0; i < 5; ++i) assert(seen[i] > 0);
    assert(g.smith_level == MAX_SMITH);
    passed("5000 real forging operations, all five rarities reached, bounded valid state");
}

static void test_equipment_salvage(void) {
    Game g;
    int a, b, id;
    game_init(&g, 123); rich(&g);
    assert(forge_weapon(&g, 0, 0, &a) == OK);
    assert(forge_weapon(&g, 0, 0, &b) == OK);
    assert(game_equip(&g, b) == OK); id = g.weapons[b].id;
    assert(game_salvage(&g, b) == LOCKED);
    assert(game_salvage(&g, a) == OK);
    assert(g.equipped == 1 && g.weapons[g.equipped].id == id);
    assert(game_equip(&g, -1) == BAD_CHOICE);
    assert(game_attack(&g) == 6 + weapon_attack(&g.weapons[g.equipped]));
    assert(game_validate(&g));
    passed("equipment bonuses, equipped weapon protected, array index repaired after salvage");
}

static void test_upgrades(void) {
    int success, got_success = 0, got_failure = 0;
    Game g, before;
    for (uint32_t seed = 1; seed < 10000 && (!got_success || !got_failure); ++seed) {
        game_init(&g, seed); rich(&g); g.weapons[0].upgrade = 9;
        assert(upgrade_weapon(&g, 0, 1, &success) == OK);
        assert(g.items[PROTECTION] == 999);
        assert(g.weapons[0].upgrade == (success ? 10 : 9));
        if (success) got_success = 1;
        else {
            got_failure = 1;
            game_init(&g, seed); rich(&g); g.weapons[0].upgrade = 9;
            assert(upgrade_weapon(&g, 0, 0, &success) == OK && !success);
            assert(g.weapons[0].upgrade == 8);
        }
        assert(game_validate(&g));
    }
    assert(got_success && got_failure);
    game_init(&g, 1); g.weapons[0].upgrade = 10; before = g;
    assert(upgrade_weapon(&g, 0, 0, &success) == MAXED);
    assert(memcmp(&g, &before, sizeof(g)) == 0);
    g.weapons[0].upgrade = 0; g.items[PROTECTION] = 0; before = g;
    assert(upgrade_weapon(&g, 0, 1, &success) == NO_RESOURCE);
    assert(memcmp(&g, &before, sizeof(g)) == 0);
    passed("upgrade success, failure downgrade, protection, cap and missing-item guard");
}

static void test_growth_shop_rest(void) {
    Game g, before;
    game_init(&g, 1);
    assert(game_buy(&g, LUCK, 1) == OK && g.gold == 115 && g.items[LUCK] == 2);
    before = g;
    assert(game_buy(&g, -1, 1) == BAD_CHOICE);
    assert(game_buy(&g, POTION, INT_MAX) == BAD_CHOICE);
    assert(game_buy(&g, PROTECTION, 2) == NO_RESOURCE);
    assert(memcmp(&g, &before, sizeof(g)) == 0);
    assert(game_use_potion(&g) == MAXED && g.items[POTION] == 3);
    g.hp = 90;
    assert(game_use_potion(&g) == OK && g.hp == 100 && g.items[POTION] == 2);
    g.hp = 1; g.energy = 0; game_rest(&g);
    assert(g.hp == 100 && g.energy == 100 && g.day == 2);
    game_add_xp(&g, 40, 30);
    assert(g.level == 2 && g.smith_level == 2 && g.hp == 112);
    game_add_xp(&g, RESOURCE_LIMIT, RESOURCE_LIMIT);
    assert(g.level == MAX_LEVEL && g.smith_level == MAX_SMITH && g.xp == 0 && g.smith_xp == 0);
    game_add_resource(&g.gold, INT_MAX);
    assert(g.gold == RESOURCE_LIMIT && game_validate(&g));
    passed("shop costs, potion cap, rest, multi-level growth and overflow prevention");
}

static void test_battles(void) {
    Game g, before;
    Battle b;
    BattleReport r;
    game_init(&g, 73); before = g;
    assert(battle_begin(&g, 1, &b) == LOCKED);
    assert(memcmp(&g, &before, sizeof(g)) == 0);
    g.energy = 0; before = g;
    assert(battle_begin(&g, 0, &b) == NO_RESOURCE);
    assert(memcmp(&g, &before, sizeof(g)) == 0);
    game_init(&g, 73); assert(battle_begin(&g, 0, &b) == OK);
    assert(g.energy == 85);
    assert(battle_step(&g, &b, HEAL, &r) == MAXED && b.turn == 0);
    while (b.active) assert(battle_step(&g, &b, ATTACK, &r) == OK);
    assert(r.outcome == 1 && g.wins == 1 && g.gold >= 225 && game_validate(&g));
    assert(battle_step(&g, &b, ATTACK, &r) == BAD_CHOICE);
    game_init(&g, 73); assert(battle_begin(&g, 0, &b) == OK); g.hp = 1;
    assert(battle_step(&g, &b, DEFEND, &r) == OK);
    assert(r.outcome == 2 && g.hp == 50 && g.gold == 162 && g.wins == 0);
    game_init(&g, 73); assert(battle_begin(&g, 0, &b) == OK); g.hp = 20;
    assert(battle_step(&g, &b, HEAL, &r) == OK && r.healed == 55 && g.items[POTION] == 2);
    for (int i = 0; b.active && i < 100; ++i) assert(battle_step(&g, &b, ESCAPE, &r) == OK);
    assert(!b.active && (r.outcome == 3 || r.outcome == 2));
    passed("battle unlocks, stamina, victory loot, defeat recovery, healing and escape");
}

static void test_quests(void) {
    Game g;
    game_init(&g, 5); g.forged = 5; g.wins = 10;
    g.weapons[0].rarity = EPIC; g.weapons[0].base_attack = 37;
    assert(game_claim_quests(&g) == 7 && g.gold == 660 && g.materials[STAR] == 2);
    Game before = g;
    assert(game_claim_quests(&g) == 0 && memcmp(&g, &before, sizeof(g)) == 0);
    assert(game_validate(&g));
    passed("milestone rewards granted exactly once");
}

static void test_save_roundtrip(void) {
    const char *path = "build/test_game.sav";
    Game g, loaded, before;
    int index;
    game_init(&g, 555); rich(&g); assert(forge_weapon(&g, 0, 1, &index) == OK);
    assert(game_equip(&g, index) == OK);
    game_init(&loaded, 1);
    assert(save_game(&g, path)); assert(load_game(&loaded, path));
    assert(memcmp(&g, &loaded, sizeof(g)) == 0);
    assert(random_next(&g) == random_next(&loaded));
    assert(save_game(&g, path)); /* 第二次保存验证替换已有文件。 */
    before = g; loaded = g;
    g.hp = -1; assert(!save_game(&g, path));
    assert(load_game(&loaded, path) && memcmp(&loaded, &before, sizeof(g)) == 0);
    assert(!save_game(&before, "build/no_such_directory/game.sav"));
    remove(path);
    passed("save/load roundtrip, persistent RNG, replacement and failed-save preservation");
}

static void test_bad_saves(void) {
    const char *path = "build/test_bad.sav";
    const char *cases[] = {
        "", "FORGE_SAVE_V9\n", "FORGE_SAVE_V1\n-1\n",
        "FORGE_SAVE_V1\n999999999999999999999999999999999999\n",
        "FORGE_SAVE_V1\n1\n1 0 1 0\n100 100 180 1\n81 0 2\n",
        "FORGE_SAVE_V1\n1\n1 0 1 0\n100 100 180 1\n-1 0 2\n"
    };
    Game g, before;
    game_init(&g, 19); before = g;
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        FILE *f = fopen(path, "w"); assert(f); fputs(cases[i], f); fclose(f);
        assert(!load_game(&g, path) && memcmp(&g, &before, sizeof(g)) == 0);
    }
    assert(save_game(&g, path));
    FILE *f = fopen(path, "a"); assert(f); fputs("UNEXPECTED", f); fclose(f);
    assert(!load_game(&g, path) && memcmp(&g, &before, sizeof(g)) == 0);
    remove(path);
    assert(!load_game(&g, "build/no_such_file.sav"));
    passed("truncated, oversized, unknown-version and trailing-garbage saves rejected without mutation");
}

static void test_campaign(void) {
    Game g;
    Battle b;
    BattleReport r;
    int index, success;
    game_init(&g, 20261007);
    for (int round = 0; round < 500 && game_trophies(&g) < AREA_COUNT; ++round) {
        game_rest(&g);
        int area = 0;
        for (int i = 0; i < AREA_COUNT; ++i) if (area_unlocked(&g, i)) area = i;
        if (g.items[POTION] < 6 && g.gold >= 120) assert(game_buy(&g, POTION, 6 - g.items[POTION]) == OK);
        int boss = g.area_wins[area] >= 2 && !(g.boss_flags & (1 << area)) && game_attack(&g) >= areas[area].boss_hp / 6;
        assert((boss ? boss_begin(&g, area, &b) : battle_begin(&g, area, &b)) == OK);
        while (b.active) {
            int action = b.boss && (b.turn + 1) % 3 == 0 ? DEFEND :
                g.hp < b.attack + 50 && g.items[POTION] ? HEAL : !b.skill_cooldown ? SKILL : ATTACK;
            assert(battle_step(&g, &b, action, &r) == OK);
        }
        int recipe = 0;
        for (int i = 0; i < RECIPE_COUNT; ++i) if (g.level >= recipes[i].unlock_level) recipe = i;
        if (forge_weapon(&g, recipe, 0, &index) == OK) {
            if (weapon_attack(&g.weapons[index]) > weapon_attack(&g.weapons[g.equipped])) game_equip(&g, index);
        }
        if (g.weapon_count > 8) {
            int salvage = g.equipped == 0 ? 1 : 0;
            assert(game_salvage(&g, salvage) == OK);
        }
        (void)upgrade_weapon(&g, g.equipped, g.items[PROTECTION] > 0, &success);
        (void)game_claim_quests(&g);
        assert(game_validate(&g));
    }
    assert(g.level >= 16 && g.wins >= 18 && g.forged >= 5 && game_trophies(&g) == AREA_COUNT);
    assert(save_game(&g, "build/test_campaign.sav"));
    printf("  Campaign reached Lv.%d, %d wins, %d forged, smith Lv.%d\n", g.level, g.wins, g.forged, g.smith_level);
    remove("build/test_campaign.sav");
    passed("real campaign clears all six bosses from starter gear without injected resources");
}

static void test_stamina_streak(void) {
    Game g, before;
    Battle b;
    BattleReport r;
    game_init(&g, 89);
    before = g;
    assert(game_use_stamina(&g) == MAXED && memcmp(&g, &before, sizeof(g)) == 0);
    assert(game_buy(&g, STAMINA, 1) == OK && g.gold == 145 && g.items[STAMINA] == 2);
    g.energy = 20; g.streak = 3;
    assert(game_use_stamina(&g) == OK && g.energy == 60 && g.streak == 3);
    g.energy = 90;
    assert(game_use_stamina(&g) == OK && g.energy == 100 && g.items[STAMINA] == 0);
    g.energy = 10; before = g;
    assert(game_use_stamina(&g) == NO_RESOURCE && memcmp(&g, &before, sizeof(g)) == 0);
    game_rest(&g); assert(g.streak == 0 && g.energy == 100);
    g.level = 20;
    for (int i = 1; i <= 5; ++i) {
        int coins = g.gold, iron = g.materials[IRON];
        assert(battle_begin(&g, 0, &b) == OK);
        while (b.active) assert(battle_step(&g, &b, ATTACK, &r) == OK);
        assert(r.outcome == 1 && g.streak == i);
        assert(g.gold - coins >= 45 + 45 * (i - 1) * 10 / 100);
        assert(g.materials[IRON] - iron >= 3 + (i >= 3 ? 1 : 0));
        assert(game_validate(&g));
    }
    g.energy = 100;
    assert(battle_begin(&g, 0, &b) == OK); g.hp = 1;
    assert(battle_step(&g, &b, DEFEND, &r) == OK && r.outcome == 2 && g.streak == 0);
    passed("stamina shop/use/cap, potion retains streak, combo rewards and rest/defeat reset");
}

static void test_legacy_migration(void) {
    const char *path = "build/test_legacy.sav";
    FILE *f = fopen(path, "w"); assert(f);
    fputs("FORGE_SAVE_V1\n123\n1 0 1 0\n100 100 180 1\n1 0 2\n0 0 0 0\n12 6 0 0\n3 1 1\n1 0 0 10 0\nEND\n", f);
    fclose(f);
    Game g;
    assert(load_game(&g, path));
    assert(g.gold == 180 && g.items[POTION] == 3 && g.items[STAMINA] == 0 && g.streak == 0);
    assert(save_game(&g, path));
    char version[32]; f = fopen(path, "r"); assert(f);
    assert(fscanf(f, "%31s", version) == 1 && !strcmp(version, "FORGE_SAVE_V4"));
    fclose(f); remove(path);
    passed("V1 player saves migrate safely to V4 with map/event defaults");
}

static void test_world_events(void) {
    Game g, before, loaded;
    game_init(&g, 34); g.level = 20; g.hp = game_max_hp(&g);
    assert(!area_unlocked(&g, 1)); before=g;
    assert(game_gather(&g, 1)==LOCKED && memcmp(&g,&before,sizeof(g))==0);
    assert(boss_begin(&g, 0, &(Battle){0})==LOCKED);
    g.energy=9; before=g;
    assert(game_gather(&g, 0)==NO_RESOURCE && memcmp(&g,&before,sizeof(g))==0);
    g.energy=100; g.streak=3;
    assert(game_gather(&g,0)==OK && g.energy==90 && g.surveys[0]==1 && g.streak==3);
    assert(game_validate(&g)); assert(save_game(&g,"build/test_event.sav"));
    assert(load_game(&loaded,"build/test_event.sav") && memcmp(&g,&loaded,sizeof(g))==0);
    assert(battle_begin(&g,0,&(Battle){0})==LOCKED);
    before=g; assert(game_resolve_event(&g,2)==BAD_CHOICE && memcmp(&g,&before,sizeof(g))==0);
    for(int kind=0;kind<4;++kind) for(int choice=0;choice<2;++choice) {
        Game event=loaded; event.event_kind=kind; event.hp=50; event.energy=30;
        int coins=event.gold, material=event.materials[IRON];
        assert(game_resolve_event(&event,choice)==OK && event.event_kind==-1 && event.event_area==-1);
        assert(event.streak==3 && game_validate(&event));
        if(kind==0&&choice==0) assert(event.materials[IRON]==material+4);
        if(kind==0&&choice==1) assert(event.gold==coins+35);
        if(kind==1) assert(choice==0 ? event.hp==95 : event.energy==45);
        if(kind==3&&choice==0) assert(event.gold==coins-25 && event.items[STAMINA]==loaded.items[STAMINA]+1);
        before=event; assert(game_resolve_event(&event,choice)==BAD_CHOICE && memcmp(&event,&before,sizeof(event))==0);
    }
    g=loaded; g.event_kind=3; g.gold=0; before=g;
    assert(game_resolve_event(&g,0)==NO_RESOURCE && memcmp(&g,&before,sizeof(g))==0);
    assert(game_resolve_event(&g,1)==OK); remove("build/test_event.sav");
    passed("route gates, gathering, eight event choices, pending-event persistence and atomic failures");
}

static void test_boss_skill(void) {
    Game g; Battle b, before; BattleReport r;
    game_init(&g, 981); g.level=20; g.hp=game_max_hp(&g);
    g.weapons[0]=(Weapon){1,5,LEGENDARY,106,10};
    for(int area=0;area<AREA_COUNT;++area) {
        assert(area_unlocked(&g,area)); g.area_wins[area]=2; game_rest(&g);
        int hp=game_max_hp(&g), attack=game_attack(&g);
        assert(boss_begin(&g,area,&b)==OK);
        assert(battle_step(&g,&b,SKILL,&r)==OK && r.player_damage>=attack*2);
        if(b.active) {
            before=b; Game old=g;
            assert(battle_step(&g,&b,SKILL,&r)==LOCKED && memcmp(&b,&before,sizeof(b))==0 && memcmp(&g,&old,sizeof(g))==0);
        }
        while(b.active) assert(battle_step(&g,&b,ATTACK,&r)==OK);
        assert(r.outcome==1 && r.first_clear && game_max_hp(&g)==hp+8 && game_attack(&g)==attack+2);
        assert(game_validate(&g));
        game_rest(&g); assert(boss_begin(&g,area,&b)==OK);
        while(b.active) assert(battle_step(&g,&b,ATTACK,&r)==OK);
        assert(r.outcome==1 && !r.first_clear && game_max_hp(&g)==hp+8);
    }
    game_rest(&g); assert(boss_begin(&g,5,&b)==OK);
    b.hp=b.max_hp=100000; g.items[POTION]=99;
    assert(battle_step(&g,&b,SKILL,&r)==OK);
    assert(battle_step(&g,&b,DEFEND,&r)==OK);
    assert(battle_step(&g,&b,DEFEND,&r)==OK && r.heavy && r.enemy_damage <= (b.attack+4)*2/3+1);
    assert(battle_step(&g,&b,HEAL,&r)==OK && b.skill_cooldown==0);
    assert(battle_step(&g,&b,SKILL,&r)==OK && b.skill_cooldown==3);
    passed("six bosses, one-time permanent trophies, skill cooldown and telegraphed heavy defense");
}

static void test_v2_migration(void) {
    FILE *f=fopen("build/test_v2.sav","w"); assert(f);
    fputs("FORGE_SAVE_V2\n123\n3 0 2 0\n124 70 300 4\n1 0 2\n5 8 4 1\n3\n12 6 2 1\n3 1 1 2\n1 0 0 10 0\nEND\n",f); fclose(f);
    Game g; assert(load_game(&g,"build/test_v2.sav"));
    assert(g.level==3 && g.gold==300 && g.streak==3 && g.items[STAMINA]==2);
    assert(g.boss_flags==0 && g.event_kind==-1 && g.area_wins[0]==0);
    assert(save_game(&g,"build/test_v2.sav")); remove("build/test_v2.sav");
    passed("V2 progression and inventory preserved while new map fields initialize safely");
}

static void test_batch_salvage(void) {
    Game g, before;
    game_init(&g, 47);
    g.weapon_count = 4; g.next_id = 5; g.equipped = 2;
    for (int i=0;i<4;++i) g.weapons[i]=(Weapon){i+1,0,i,10+i*9,i};
    before=g;
    int invalid[]={1,99}, duplicate[]={1,1}, equipped[]={1,3};
    assert(game_salvage_many(&g,invalid,2)==BAD_CHOICE && !memcmp(&g,&before,sizeof(g)));
    assert(game_salvage_many(&g,duplicate,2)==BAD_CHOICE && !memcmp(&g,&before,sizeof(g)));
    assert(game_salvage_many(&g,equipped,2)==LOCKED && !memcmp(&g,&before,sizeof(g)));
    int selected[]={4,1,2};
    assert(game_salvage_many(&g,selected,3)==OK);
    assert(g.weapon_count==1 && g.equipped==0 && g.weapons[0].id==3);
    assert(g.gold==before.gold+10+26+58 && g.materials[IRON]==before.materials[IRON]+2+3+5);
    assert(game_validate(&g));
    passed("batch recycling uses stable ids, rejects entire invalid selection and protects equipped blade");
}

int main(void) {
    test_initial_rng(); test_forge_guards(); test_probabilities_pity(); test_long_forging();
    test_equipment_salvage(); test_upgrades(); test_growth_shop_rest(); test_battles();
    test_quests(); test_save_roundtrip(); test_bad_saves(); test_campaign(); test_stamina_streak(); test_legacy_migration(); test_world_events(); test_boss_skill(); test_v2_migration();
    test_batch_salvage();
    /* 秘境测试也走真实数值，只把角色设为后期装备以覆盖三层流程。 */
    Game g, copy, old; Battle b; BattleReport r;
    game_init(&g, 88); old=g;
    assert(delve_begin(&g,5)==LOCKED && !memcmp(&g,&old,sizeof(g)));
    g.energy=19;old=g;assert(delve_begin(&g,0)==NO_RESOURCE && !memcmp(&g,&old,sizeof(g)));
    g.energy=100;assert(delve_begin(&g,0)==OK && g.energy==80);
    old=g;assert(delve_room(&g,5,0,&b)==LOCKED && !memcmp(&g,&old,sizeof(g)));
    assert(delve_room(&g,1,0,&b)==OK);old=g;
    assert(delve_room(&g,1,1,&b)==LOCKED && !memcmp(&g,&old,sizeof(g)));
    assert(delve_room(&g,2,1,&b)==OK && g.energy==95);
    passed("delve route/cost checks, single-claim chests/springs and locked staircase are atomic");
    assert(delve_room(&g,3,0,&b)==OK);
    assert(battle_step(&g,&b,SKILL,&r)==OK && b.active);
    assert(save_game(&g,"build/test_delve.sav") && load_game(&copy,"build/test_delve.sav"));
    assert(!memcmp(&g,&copy,sizeof(g)) && copy.delve.battle.hp==b.hp && copy.delve.battle.skill_cooldown==3);
    old=g;copy.delve.battle.hp=copy.delve.battle.max_hp+1;
    assert(!game_validate(&copy));copy=g;copy.delve.visited|=8;assert(!game_validate(&copy));
    remove("build/test_delve.sav");
    passed("V4 saves dungeon seed, floors and complete ongoing battle and rejects impossible state");
    assert(delve_leave(&g)==OK);g.level=20;g.hp=game_max_hp(&g);g.weapons[0]=(Weapon){1,5,LEGENDARY,106,10};
    assert(delve_begin(&g,0)==OK);
    for(int floor=1;floor<=3;++floor){
        assert(g.delve.floor==floor);
        for(int room=3;room<=4;++room){assert(delve_room(&g,room,0,&b)==OK);
            while(b.active)assert(battle_step(&g,&b,b.skill_cooldown?ATTACK:SKILL,&r)==OK);
            assert(game_validate(&g));assert(g.delve.visited&(1<<room));
        }
        assert(g.delve.reward);old=g;assert(delve_room(&g,5,0,&b)==LOCKED && !memcmp(&g,&old,sizeof(g)));
        assert(delve_blessing(&g,floor-1)==OK && !g.delve.reward);
        assert(delve_room(&g,5,0,&b)==OK && game_validate(&g));
    }
    assert(!g.delve.active && g.delve.clears==1 && g.boss_flags==0 && g.area_wins[0]==0 && g.wins==6);
    passed("three-floor delve combines all blessings and pays completion rewards without granting region boss trophies");
    game_init(&g,56);assert(delve_begin(&g,0)==OK);assert(delve_room(&g,4,0,&b)==OK);g.hp=1;
    assert(battle_step(&g,&b,DEFEND,&r)==OK && r.outcome==2 && !g.delve.active && game_validate(&g));
    passed("delve defeat returns to camp, clears temporary buffs and retains validated permanent inventory");
    FILE *v3=fopen("build/test_v3.sav","w");assert(v3);
    fputs("FORGE_SAVE_V3\n123\n3 0 2 0\n124 70 300 4\n1 0 2\n5 8 4 1\n3\n0 -1 -1\n0 0\n0 0\n0 0\n0 0\n0 0\n0 0\n12 6 2 1\n3 1 1 2\n1 0 0 10 0\nEND\n",v3);fclose(v3);
    assert(load_game(&g,"build/test_v3.sav") && g.level==3 && g.gold==300 && !g.delve.active && g.delve.clears==0);
    remove("build/test_v3.sav");passed("V3 saves keep levels, resources and old map progression when upgrading to V4");
    printf("\n%d test groups passed.\n", groups);
    return 0;
}
