#ifndef FORGE_GAME_H
#define FORGE_GAME_H

#include <stdint.h>

/* 所有上限都集中在这里；背包使用固定数组，便于初学者理解。 */
#define MAX_WEAPONS 80
#define MATERIAL_COUNT 4
#define ITEM_COUNT 4
#define RARITY_COUNT 5
#define RECIPE_COUNT 6
#define AREA_COUNT 6
#define MAX_LEVEL 20
#define MAX_SMITH 10
#define RESOURCE_LIMIT 1000000
#define WEAPON_LEVEL_MAX 10

enum Material { IRON, WOOD, CRYSTAL, STAR };
enum Item { POTION, LUCK, PROTECTION, STAMINA };
enum Rarity { COMMON, UNCOMMON, RARE, EPIC, LEGENDARY };
enum Result { OK, BAD_CHOICE, LOCKED, NO_RESOURCE, BAG_FULL, MAXED, FAILED };
enum BattleAction { ATTACK, DEFEND, HEAL, ESCAPE, SKILL };

typedef struct {
    int id, recipe, rarity, base_attack, upgrade;
} Weapon;

typedef struct {
    uint32_t rng; /* 保存随机数状态，读档后继续同一随机序列。 */
    int level, xp, smith_level, smith_xp;
    int hp, energy, gold, day;
    int materials[MATERIAL_COUNT], items[ITEM_COUNT];
    Weapon weapons[MAX_WEAPONS];
    int weapon_count, equipped, next_id;
    int forged, wins, pity, quest_flags;
    int streak; /* 同一次营地出发的连胜，最多 5；休息/战败/逃跑重置。 */
    int area_wins[AREA_COUNT], surveys[AREA_COUNT], boss_flags;
    int event_area, event_kind; /* -1 表示无待处理事件；事件选择也随存档保存。 */
} Game;

typedef struct {
    const char *name;
    int unlock_level, gold, materials[MATERIAL_COUNT], attack;
    int weights[RARITY_COUNT]; /* 总和为 10000，1 对应 0.01%。 */
} Recipe;

typedef struct {
    const char *name;
    int unlock_level, energy, hp, attack, gold, xp;
    const char *boss, *enemies[3];
    int boss_hp, boss_attack, material;
} Area;

typedef struct {
    int area, hp, max_hp, attack, turn, active;
    int boss, enemy, skill_cooldown;
} Battle;

typedef struct {
    int player_damage, enemy_damage, healed;
    int outcome; /* 0 继续，1 胜利，2 战败，3 逃跑。 */
    int first_clear, heavy;
} BattleReport;

extern const char *const material_names[MATERIAL_COUNT];
extern const char *const item_names[ITEM_COUNT];
extern const char *const rarity_names[RARITY_COUNT];
extern const Recipe recipes[RECIPE_COUNT];
extern const Area areas[AREA_COUNT];

void game_init(Game *g, uint32_t seed);
uint32_t random_next(Game *g);
int random_below(Game *g, int bound);
int game_max_hp(const Game *g);
int weapon_attack(const Weapon *w);
int game_attack(const Game *g);
int level_xp_needed(int level);
int smith_xp_needed(int level);
void game_add_xp(Game *g, int xp, int smith_xp);
void game_add_resource(int *value, int amount);
void game_rest(Game *g);
int game_use_potion(Game *g);
int game_use_stamina(Game *g);
int game_buy(Game *g, int item, int count);
int game_equip(Game *g, int index);
int game_salvage(Game *g, int index);
int game_claim_quests(Game *g);
int game_validate(const Game *g);

void forge_weights(const Game *g, int recipe, int lucky, int out[RARITY_COUNT]);
int forge_weapon(Game *g, int recipe, int lucky, int *new_index);
int upgrade_chance(const Game *g, int index);
int upgrade_cost(const Game *g, int index);
int upgrade_weapon(Game *g, int index, int protect, int *success);

int battle_begin(Game *g, int area, Battle *battle);
int area_unlocked(const Game *g, int area);
int game_trophies(const Game *g);
int boss_begin(Game *g, int area, Battle *battle);
int game_gather(Game *g, int area);
int game_resolve_event(Game *g, int choice);
int battle_step(Game *g, Battle *battle, int action, BattleReport *report);

int save_game(const Game *g, const char *path);
int load_game(Game *g, const char *path);

#endif
