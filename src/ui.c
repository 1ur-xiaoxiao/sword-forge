#include "ui.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

static int end_of_input = 0;
int ui_eof(void) { return end_of_input; }

void ui_init(void) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
}

int ui_number(const char *prompt, int low, int high) {
    char line[128], *end;
    long value;
    for (;;) {
        printf("%s", prompt);
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) { end_of_input = 1; return -1; }
        if (!strchr(line, '\n') && !feof(stdin)) {
            int ch;
            while ((ch = getchar()) != '\n' && ch != EOF) { }
            puts("输入太长，请只输入菜单数字。");
            continue;
        }
        errno = 0;
        value = strtol(line, &end, 10);
        if (end == line || errno) { puts("请输入有效的整数。"); continue; }
        while (isspace((unsigned char)*end)) ++end;
        if (*end || value < low || value > high) {
            printf("请输入 %d 到 %d 之间的整数。\n", low, high);
            continue;
        }
        return (int)value;
    }
}

void ui_result(int result) {
    static const char *const messages[] = {
        "操作完成。", "编号或参数无效。", "尚未解锁，或这把武器正在装备中。",
        "金币、材料、道具或体力不足。", "武器背包已满，请先回收。",
        "已达到上限。", "操作失败。"
    };
    if (result >= OK && result <= FAILED) puts(messages[result]);
}

static void show_weapon(const Game *g, int i) {
    const Weapon *w = &g->weapons[i];
    printf("%2d. [%s] %s +%d  攻击:%d  编号:#%d%s\n", i + 1,
           rarity_names[w->rarity], recipes[w->recipe].name, w->upgrade,
           weapon_attack(w), w->id, g->equipped == i ? "  <已装备>" : "");
}

void ui_status(const Game *g) {
    printf("\n================ 剑炉物语 · 第 %d 天 ================\n", g->day);
    printf("角色 Lv.%d (%d/%d)  锻造师 Lv.%d (%d/%d)\n", g->level, g->xp,
           g->level == MAX_LEVEL ? 0 : level_xp_needed(g->level), g->smith_level,
           g->smith_xp, g->smith_level == MAX_SMITH ? 0 : smith_xp_needed(g->smith_level));
    printf("生命 %d/%d | 体力 %d/100 | 攻击 %d | 金币 %d\n",
           g->hp, game_max_hp(g), g->energy, game_attack(g), g->gold);
    printf("武器 %d/%d | 已锻造 %d 次 | 探索胜利 %d 次 | 高品质保底 %d/20\n",
           g->weapon_count, MAX_WEAPONS, g->forged, g->wins, g->pity);
    printf("连续探索 %d 胜 | 下一场收益 +%d%%（休息会重置）\n", g->streak, (g->streak < 4 ? g->streak : 4) * 10);
    if (g->equipped >= 0) show_weapon(g, g->equipped);
    for (int i = 0; i < g->weapon_count; ++i)
        if (g->weapons[i].recipe == 3 && g->weapons[i].rarity == LEGENDARY && g->weapons[i].upgrade == 10) {
            puts("★ 终极目标达成：你打造了传说级王座圣刃 +10！可以继续自由冒险。");
            break;
        }
}

void ui_inventory(const Game *g) {
    puts("\n--- 材料仓库 ---");
    for (int i = 0; i < MATERIAL_COUNT; ++i) printf("%s: %d%s", material_names[i], g->materials[i], i + 1 == MATERIAL_COUNT ? "\n" : " | ");
    for (int i = 0; i < ITEM_COUNT; ++i) printf("%s: %d%s", item_names[i], g->items[i], i + 1 == ITEM_COUNT ? "\n" : " | ");
    puts("--- 武器背包（菜单使用前面的序号）---");
    for (int i = 0; i < g->weapon_count; ++i) show_weapon(g, i);
}

void ui_forge(Game *g) {
    int choice, lucky, weights[RARITY_COUNT], index = -1, result;
    ui_inventory(g);
    puts("\n--- 锻造配方 ---");
    for (int i = 0; i < RECIPE_COUNT; ++i) {
        const Recipe *r = &recipes[i];
        printf("%d. %s | 角色 Lv.%d 解锁 | %d 金币", i + 1, r->name, r->unlock_level, r->gold);
        for (int j = 0; j < MATERIAL_COUNT; ++j) if (r->materials[j]) printf(" | %s x%d", material_names[j], r->materials[j]);
        puts(g->level < r->unlock_level ? " [未解锁]" : "");
    }
    choice = ui_number("选配方（0 返回）：", 0, RECIPE_COUNT);
    if (choice <= 0) return;
    --choice;
    if (g->level < recipes[choice].unlock_level) { ui_result(LOCKED); return; }
    lucky = ui_number("使用幸运符？1 使用 / 0 不用：", 0, 1);
    if (lucky < 0) return;
    forge_weights(g, choice, lucky, weights);
    puts("本次实际品质概率：");
    for (int i = 0; i < RARITY_COUNT; ++i) printf("%s %.2f%%%s", rarity_names[i], weights[i] / 100.0, i + 1 == RARITY_COUNT ? "\n" : " | ");
    if (g->pity >= 19) puts("本次触发保底：至少史诗！");
    if (ui_number("1 确认锻造 / 0 取消：", 0, 1) != 1) return;
    result = forge_weapon(g, choice, lucky, &index);
    if (result != OK) { ui_result(result); return; }
    puts("锻造成功！新武器：");
    show_weapon(g, index);
    if (ui_number("1 立即装备 / 0 放进背包：", 0, 1) == 1) game_equip(g, index);
}

void ui_equipment(Game *g) {
    int action, index, protect, success = 0, result;
    ui_inventory(g);
    action = ui_number("1 装备 / 2 强化 / 3 回收 / 0 返回：", 0, 3);
    if (action <= 0) return;
    index = ui_number("输入武器序号（0 返回）：", 0, g->weapon_count);
    if (index <= 0) return;
    --index;
    if (action == 1) { ui_result(game_equip(g, index)); return; }
    if (action == 3) {
        const Weapon *w = &g->weapons[index];
        printf("回收获得铁矿 %d、金币 %d。\n", 2 + w->rarity, 10 + w->rarity * 12 + w->upgrade * 4);
        if (ui_number("1 确认回收 / 0 取消：", 0, 1) == 1) ui_result(game_salvage(g, index));
        return;
    }
    if (g->weapons[index].upgrade >= WEAPON_LEVEL_MAX) { ui_result(MAXED); return; }
    printf("成功率 %d%%，成本 %d 金币 + %d 铁矿。最高 +10。\n",
           upgrade_chance(g, index), upgrade_cost(g, index), 2 + g->weapons[index].upgrade / 2);
    puts("失败时降低 1 级（最低 +0）；保护符可阻止降级，无论成功失败都消耗。");
    protect = ui_number("1 使用保护符 / 0 不用：", 0, 1);
    if (protect < 0 || ui_number("1 确认强化 / 0 取消：", 0, 1) != 1) return;
    result = upgrade_weapon(g, index, protect, &success);
    if (result != OK) ui_result(result);
    else {
        puts(success ? "强化成功！" : (protect ? "强化失败，保护符保住了等级。" : "强化失败。"));
        show_weapon(g, index);
    }
}

void ui_adventure(Game *g) {
    Battle battle;
    BattleReport report;
    int area, action, result, old_level;
    Game before = *g;
    puts("\n--- 探索区域 ---");
    for (int i = 0; i < AREA_COUNT; ++i)
        printf("%d. %s | Lv.%d 解锁 | 体力 %d | 敌人生命 %d~%d / 攻击 %d~%d\n",
               i + 1, areas[i].name, areas[i].unlock_level, areas[i].energy,
               areas[i].hp, areas[i].hp + 15, areas[i].attack, areas[i].attack + 3);
    area = ui_number("选择区域（0 返回）：", 0, AREA_COUNT);
    if (area <= 0) return;
    result = battle_begin(g, area - 1, &battle);
    if (result != OK) { ui_result(result); return; }
    old_level = g->level;
    while (battle.active) {
        printf("\n[%s · 守卫] HP %d/%d | 你 HP %d/%d | 药水 %d\n",
               areas[battle.area].name, battle.hp, battle.max_hp, g->hp, game_max_hp(g), g->items[POTION]);
        action = ui_number("1 攻击 / 2 防御反击 / 3 喝药 / 4 逃跑：", 1, 4);
        if (action < 0) { /* 输入关闭时放弃遭遇，体力已消耗，返回后保存。 */
            puts("输入已关闭，离开当前遭遇。");
            return;
        }
        result = battle_step(g, &battle, action - 1, &report);
        if (result != OK) { ui_result(result); continue; }
        if (report.healed) printf("恢复 %d 生命。\n", report.healed);
        if (report.player_damage) printf("你造成 %d 点伤害。\n", report.player_damage);
        if (report.enemy_damage) printf("守卫造成 %d 点伤害。\n", report.enemy_damage);
        if (action == 4 && report.outcome != 3) puts("逃跑失败！");
        if (report.outcome == 1) {
            int gained_xp = g->xp - before.xp;
            for (int i = before.level; i < g->level; ++i) gained_xp += level_xp_needed(i);
            printf("胜利！获得 %d 金币、%d 角色经验。连续 %d 胜。\n", g->gold - before.gold, gained_xp, g->streak);
            for (int i = 0; i < MATERIAL_COUNT; ++i)
                if (g->materials[i] > before.materials[i]) printf("  %s +%d\n", material_names[i], g->materials[i] - before.materials[i]);
            for (int i = 0; i < ITEM_COUNT; ++i)
                if (g->items[i] > before.items[i]) printf("  %s +%d\n", item_names[i], g->items[i] - before.items[i]);
            if (g->level > old_level) printf("角色升级到 Lv.%d！生命回满。\n", g->level);
        } else if (report.outcome == 2) puts("战败，被送回营地：损失当前金币的 10%，恢复一半生命。");
        else if (report.outcome == 3) puts("成功逃跑，未获得战利品。");
    }
}

void ui_shop(Game *g) {
    int item, count;
    puts("\n--- 营地商店 ---\n1 治疗药水 20 金币（恢复 55 HP）\n2 幸运符 65 金币（提升品质概率）\n3 保护符 90 金币（强化失败不降级）\n4 体力药水 35 金币（恢复 40 体力，保留连胜）");
    printf("当前金币：%d\n", g->gold);
    item = ui_number("选商品（0 返回）：", 0, ITEM_COUNT);
    if (item <= 0) return;
    count = ui_number("买多少个（0 返回，最多 99）：", 0, 99);
    if (count > 0) ui_result(game_buy(g, item - 1, count));
}

void ui_quests(Game *g) {
    int flags = game_claim_quests(g);
    puts("\n--- 成长里程碑（满足条件自动领取，每项仅一次）---");
    printf("%s 锻造 5 次：100 金币 + 2 幸运符（当前 %d）\n", g->quest_flags & 1 ? "[已领]" : "[进行中]", g->forged);
    printf("%s 探索胜利 10 次：200 金币 + 2 星陨石（当前 %d）\n", g->quest_flags & 2 ? "[已领]" : "[进行中]", g->wins);
    printf("%s 获得史诗或传说武器：180 金币 + 2 保护符\n", g->quest_flags & 4 ? "[已领]" : "[进行中]");
    if (flags) puts("新里程碑奖励已经放入背包！");
}
