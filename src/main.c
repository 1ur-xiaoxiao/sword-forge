#include "ui.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

static const char *const save_path = "saves/game.sav";

static void ensure_save_directory(void) {
#ifdef _WIN32
    (void)_mkdir("saves");
#else
    (void)mkdir("saves", 0755);
#endif
}

static void autosave(const Game *g) {
    if (!save_game(g, save_path)) puts("存档写入失败！请确认项目文件夹可写；当前内存进度仍可继续玩。");
}

static void demo(void) {
    Game g;
    Battle b;
    BattleReport r;
    int index;
    game_init(&g, 20261007u);
    puts("=== 固定种子演示（不写入玩家存档）===");
    ui_status(&g);
    if (forge_weapon(&g, 0, 1, &index) == OK) {
        game_equip(&g, index);
        puts("已使用幸运符锻造并装备新武器。");
    }
    if (battle_begin(&g, 0, &b) == OK) {
        while (b.active) {
            battle_step(&g, &b, ATTACK, &r);
            printf("战斗回合 %d：造成 %d / 承受 %d，结局 %d\n", b.turn, r.player_damage, r.enemy_damage, r.outcome);
        }
    }
    ui_status(&g);
    ui_inventory(&g);
    puts(game_validate(&g) ? "演示状态检查通过。" : "演示状态异常。" );
}

int main(int argc, char **argv) {
    Game g, loaded;
    int choice;
    ui_init();
    if (argc == 2 && strcmp(argv[1], "--demo") == 0) { demo(); return 0; }
    if (argc != 1) { puts("用法：forge_game.exe [--demo]"); return 1; }
    ensure_save_directory();
    puts("剑炉物语 — 离线概率锻造养成\n目标：探索、升级锻造师，打造传说级王座圣刃 +10！");
    if (load_game(&loaded, save_path)) {
        choice = ui_number("发现存档：1 继续游戏 / 2 新游戏 / 0 退出：", 0, 2);
        if (choice <= 0) return 0;
        if (choice == 1) g = loaded;
        else {
            if (ui_number("新游戏将覆盖现有存档：1 确认 / 0 取消：", 0, 1) != 1) return 0;
            game_init(&g, (uint32_t)time(NULL));
        }
    } else {
        FILE *existing = fopen(save_path, "r");
        if (existing) {
            fclose(existing);
            puts("原存档损坏或版本不兼容。开始新游戏会覆盖它，建议先复制备份。");
            if (ui_number("1 开始新游戏 / 0 退出保留文件：", 0, 1) != 1) return 0;
        }
        game_init(&g, (uint32_t)time(NULL));
        puts("领取新手礼包：180 金币、12 铁矿、6 硬木、3 药水、1 幸运符、1 保护符、1 体力药水。");
    }
    autosave(&g);
    do {
        ui_status(&g);
        puts("\n1 探索冒险   2 概率锻造   3 材料与背包   4 装备/强化/回收\n5 营地商店   6 休息一天   7 使用药水     8 成长里程碑\n9 手动保存   10 读取存档  11 体力药水   0 保存并退出");
        choice = ui_number("你的选择：", 0, 11);
        switch (choice) {
            case 1: ui_adventure(&g); break;
            case 2: ui_forge(&g); break;
            case 3: ui_inventory(&g); break;
            case 4: ui_equipment(&g); break;
            case 5: ui_shop(&g); break;
            case 6: game_rest(&g); puts("休息完成：生命与体力回满。"); break;
            case 7: ui_result(game_use_potion(&g)); break;
            case 8: ui_quests(&g); break;
            case 9: puts(save_game(&g, save_path) ? "保存成功：saves/game.sav" : "保存失败，请检查写入权限。"); break;
            case 11: ui_result(game_use_stamina(&g)); break;
            case 10:
                if (ui_number("1 确认读取 / 0 返回：", 0, 1) == 1)
                    puts(load_game(&g, save_path) ? "读档成功。" : "读档失败，当前进度保留。");
                break;
            default: break;
        }
        if (game_claim_quests(&g)) puts("达成新里程碑！奖励已领取，可在成长里程碑查看。");
        autosave(&g);
    } while (choice != 0 && !ui_eof());
    puts("游戏结束，进度已尝试保存。下次启动可继续冒险。");
    return 0;
}
