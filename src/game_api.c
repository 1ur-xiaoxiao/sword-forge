/* Windows 与 Android 共用的操作/JSON 层；玩法仍由各个 C 核心模块负责。 */
#include "game_api.h"
#include "game.h"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#endif

static int file_exists(const char *path) {
    struct stat info;
    return stat(path, &info) == 0;
}
static int copy_save(const char *from, const char *to) {
    FILE *source = fopen(from, "rb"), *target;
    char buffer[4096]; size_t n;
    int good = 1;
    if (!source) return 0;
    target = fopen(to, "wb");
    if (!target) { fclose(source); return 0; }
    while ((n = fread(buffer, 1, sizeof(buffer), source)) != 0)
        if (fwrite(buffer, 1, n, target) != n) { good = 0; break; }
    if (ferror(source) || fflush(target)) good = 0;
    if (fclose(source)) good = 0;
    if (fclose(target)) good = 0;
    return good;
}
static Game game;
static Battle encounter;
static int save_ok = 1, corrupted_save = 0;
int api_running = 1;
static char save_path[1024], backup_path[1050];
static char slot_paths[4][1024], marker_path[1024];
static int active_slot, upgrade_result = -1;
static BattleReport last_report;
static char json[65536];
static size_t json_length;

static void set_slot(int slot) {
    active_slot = slot;
    strcpy(save_path, slot_paths[slot]);
    snprintf(backup_path, sizeof(backup_path), "%s.before-new.sav", save_path);
    char *name = strrchr(backup_path, '/');
    name = name ? name + 1 : backup_path;
    if (!strcmp(name, "game.sav.before-new.sav")) strcpy(name, "game.before-new.sav");
}
static int write_marker(int slot) {
    char temporary[1050];
    snprintf(temporary, sizeof(temporary), "%s.tmp", marker_path);
    FILE *f = fopen(temporary, "w");
    if (!f) return 0;
    int good = fprintf(f, "%d\n", slot + 1) > 0;
    if (fflush(f)) good = 0;
    if (fclose(f)) good = 0;
    if (good) {
#ifdef _WIN32
        good = MoveFileExA(temporary, marker_path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
        good = rename(temporary, marker_path) == 0;
#endif
    }
    if (!good) remove(temporary);
    return good;
}

#if defined(__GNUC__) || defined(__clang__)
static void append(const char *format, ...) __attribute__((format(printf, 1, 2)));
#endif
static void append(const char *format, ...) {
    va_list args;
    va_start(args, format);
    int count = vsnprintf(json + json_length, sizeof(json) - json_length, format, args);
    va_end(args);
    if (count > 0 && (size_t)count < sizeof(json) - json_length) json_length += (size_t)count;
}

static void numbers(const int *values, int count) {
    append("[");
    for (int i = 0; i < count; ++i) append("%s%d", i ? "," : "", values[i]);
    append("]");
}

static const char *state_json(int result, const char *message, int latest) {
    json_length = 0;
    append("{\"result\":%d,\"message\":\"%s\",\"latest\":%d,\"saveOk\":%s,\"corrupted\":%s,",
           result, message, latest, save_ok ? "true" : "false", corrupted_save ? "true" : "false");
    append("\"activeSlot\":%d,\"upgradeSuccess\":%d,\"combat\":{\"dealt\":%d,\"taken\":%d,\"healed\":%d,\"outcome\":%d},\"slots\":[",
           active_slot, upgrade_result, last_report.player_damage, last_report.enemy_damage, last_report.healed, last_report.outcome);
    for (int i = 0; i < 4; ++i) {
        Game candidate;
        int exists = file_exists(slot_paths[i]), valid = load_game(&candidate, slot_paths[i]);
        append("%s{\"slot\":%d,\"exists\":%s,\"valid\":%s,\"level\":%d,\"day\":%d,\"gold\":%d,\"weapons\":%d}",
               i ? "," : "", i, exists ? "true" : "false", valid ? "true" : "false",
               valid ? candidate.level : 0, valid ? candidate.day : 0, valid ? candidate.gold : 0, valid ? candidate.weapon_count : 0);
    }
    append("],");
    const Delve *d=&game.delve;
    append("\"delve\":{\"active\":%s,\"area\":%d,\"seed\":%d,\"floor\":%d,\"visited\":%d,\"room\":%d,\"reward\":%s,\"buffs\":%d,\"clears\":%d},",
           d->active?"true":"false",d->area,d->seed,d->floor,d->visited,d->room,d->reward?"true":"false",d->buffs,d->clears);
    append("\"level\":%d,\"xp\":%d,\"xpNeeded\":%d,\"smithLevel\":%d,\"smithXp\":%d,\"smithNeeded\":%d,",
           game.level, game.xp, level_xp_needed(game.level), game.smith_level, game.smith_xp, smith_xp_needed(game.smith_level));
    append("\"hp\":%d,\"maxHp\":%d,\"energy\":%d,\"gold\":%d,\"day\":%d,\"attack\":%d,",
           game.hp, game_max_hp(&game), game.energy, game.gold, game.day, game_attack(&game));
    append("\"equipped\":%d,\"forged\":%d,\"wins\":%d,\"pity\":%d,\"quests\":%d,\"materials\":",
           game.equipped, game.forged, game.wins, game.pity, game.quest_flags);
    numbers(game.materials, MATERIAL_COUNT);
    append(",\"trophies\":%d,\"bossFlags\":%d,\"event\":{\"area\":%d,\"kind\":%d}",
           game_trophies(&game), game.boss_flags, game.event_area, game.event_kind);
    append(",\"streak\":%d,\"nextBonus\":%d", game.streak, (game.streak < 4 ? game.streak : 4) * 10);
    append(",\"items\":"); numbers(game.items, ITEM_COUNT);
    append(",\"weapons\":[");
    for (int i = 0; i < game.weapon_count; ++i) {
        const Weapon *w = &game.weapons[i];
        append("%s{\"id\":%d,\"recipe\":%d,\"rarity\":%d,\"upgrade\":%d,\"attack\":%d,\"chance\":%d,\"cost\":%d,\"iron\":%d}",
               i ? "," : "", w->id, w->recipe, w->rarity, w->upgrade, weapon_attack(w),
               upgrade_chance(&game, i), upgrade_cost(&game, i), 2 + w->upgrade / 2);
    }
    append("],\"recipes\":[");
    for (int i = 0; i < RECIPE_COUNT; ++i) {
        int weights[RARITY_COUNT];
        append("%s{\"name\":\"%s\",\"level\":%d,\"gold\":%d,\"attack\":%d,\"materials\":",
               i ? "," : "", recipes[i].name, recipes[i].unlock_level, recipes[i].gold, recipes[i].attack);
        numbers(recipes[i].materials, MATERIAL_COUNT);
        append(",\"weights\":"); forge_weights(&game, i, 0, weights); numbers(weights, RARITY_COUNT);
        append(",\"luckyWeights\":"); forge_weights(&game, i, 1, weights); numbers(weights, RARITY_COUNT); append("}");
    }
    append("],\"areas\":[");
    for (int i = 0; i < AREA_COUNT; ++i) {
        append("%s{\"name\":\"%s\",\"level\":%d,\"energy\":%d,\"hp\":%d,\"attack\":%d,\"gold\":%d,\"xp\":%d,",
               i ? "," : "", areas[i].name, areas[i].unlock_level, areas[i].energy,
               areas[i].hp, areas[i].attack, areas[i].gold, areas[i].xp);
        append("\"unlocked\":%s,\"wins\":%d,\"surveys\":%d,\"cleared\":%s,\"material\":%d,\"boss\":\"%s\",\"bossHp\":%d,\"bossAttack\":%d,\"enemies\":[\"%s\",\"%s\",\"%s\"]}",
               area_unlocked(&game, i) ? "true" : "false", game.area_wins[i], game.surveys[i],
               game.boss_flags & (1 << i) ? "true" : "false", areas[i].material, areas[i].boss,
               areas[i].boss_hp, areas[i].boss_attack, areas[i].enemies[0], areas[i].enemies[1], areas[i].enemies[2]);
    }
    append("],\"battle\":{\"active\":%s,\"area\":%d,\"hp\":%d,\"maxHp\":%d,\"attack\":%d,\"turn\":%d,\"boss\":%s,\"enemy\":%d,\"cooldown\":%d,\"delve\":%s,\"elite\":%s}}",
           encounter.active ? "true" : "false", encounter.area, encounter.hp,
           encounter.max_hp, encounter.attack, encounter.turn, encounter.boss ? "true" : "false", encounter.enemy, encounter.skill_cooldown,
           encounter.delve?"true":"false",encounter.elite?"true":"false");
    return json;
}

static int parameter(const char *body, const char *name, char *out, size_t size) {
    size_t length = strlen(name);
    const char *part = body;
    int found = 0;
    while (*part) {
        const char *end = strchr(part, '&');
        if (!end) end = part + strlen(part);
        if ((size_t)(end - part) > length && !strncmp(part, name, length) && part[length] == '=') {
            size_t n = (size_t)(end - part) - length - 1;
            if (found || n >= size) return 0;
            memcpy(out, part + length + 1, n); out[n] = '\0'; found = 1;
        }
        part = *end ? end + 1 : end;
    }
    return found;
}

static int number_parameter(const char *body, const char *name) {
    char value[32], *end;
    long n;
    if (!parameter(body, name, value, sizeof(value))) return -1;
    errno = 0;
    n = strtol(value, &end, 10);
    if (errno || end == value || *end || n < 0 || n > 10000) return -1;
    return (int)n;
}

const char *api_action(const char *body) {
    char action[32], message[512] = "操作完成。";
    int result = BAD_CHOICE, latest = -1, success = 0;
    int p = number_parameter(body, "p"), q = number_parameter(body, "q");
    upgrade_result = -1; memset(&last_report, 0, sizeof(last_report));
    static const char *const errors[] = {"操作完成。", "操作参数无效。", "尚未解锁，或武器正在装备。",
        "资源不足，先去探索收集材料吧。", "背包已满，请先回收武器。", "已经达到上限。", "操作失败。"};
    if (!parameter(body, "action", action, sizeof(action))) return state_json(BAD_CHOICE, errors[BAD_CHOICE], -1);
    if (corrupted_save && strcmp(action, "new") && strcmp(action, "quit") && strcmp(action, "slot"))
        return state_json(FAILED, "存档无法读取，请先备份旧文件，再在设置里确认开始新旅程。", -1);
    if (encounter.active && strcmp(action, "battle") && strcmp(action, "quit"))
        return state_json(LOCKED, "请先结束当前战斗。", -1);
    if (game.event_kind >= 0 && strcmp(action, "event") && strcmp(action, "save") && strcmp(action, "quit") && strcmp(action, "new"))
        return state_json(LOCKED, "请先在地图上选择当前探索事件的奖励。", -1);
    if (game.delve.active && strcmp(action,"delve_room") && strcmp(action,"blessing") && strcmp(action,"delve_leave") &&
        strcmp(action,"battle") && strcmp(action,"slot") && strcmp(action,"load") && strcmp(action,"save") && strcmp(action,"quit") &&
        strcmp(action,"potion") && strcmp(action,"stamina") && strcmp(action,"new"))
        return state_json(LOCKED,"当前正在秘境探险，回到营地后可以锻造、购物和休息。",-1);
    if (!strcmp(action, "forge")) {
        result = forge_weapon(&game, p, q, &latest);
        if (result == OK) snprintf(message, sizeof(message), "锻造完成！获得%s品质的%s。", rarity_names[game.weapons[latest].rarity], recipes[p].name);
    } else if (!strcmp(action,"delve")) {
        result=delve_begin(&game,p);
        if(result==OK)strcpy(message,"进入三层随机秘境！点击房间自动寻路，宝箱和泉水只能领取一次；击败本层敌人与精英才能下楼。");
    } else if (!strcmp(action,"delve_room")) {
        int gold=game.gold;
        result=delve_room(&game,p,q,&encounter);
        if(result==OK) {
            if(p==1)strcpy(message,q?"打开宝箱，金币收入行囊。":"打开宝箱，特色材料和硬木收入行囊。");
            else if(p==2)strcpy(message,q?"清泉补充 15 体力，最高 100。":"清泉恢复 40 生命，最高至生命上限。");
            else if(p==3||p==4)strcpy(message,p==4?"遭遇秘境精英！每第三回合发动重击，击败后选择祝福。":"守卫拦住去路，准备战斗！");
            else snprintf(message,sizeof(message),"%s金币 +%d。%s",game.delve.active?"通过本层！":"三层秘境通关！",game.gold-gold,
                game.delve.active?"进入新地图，生命恢复 20；本次祝福继续生效。":"额外获得 2 幸运符、1 保护符与星陨石，带回工坊锻造吧！");
        }
    } else if (!strcmp(action,"blessing")) {
        static const char *const blessings[]={"锋锐祝福：本次秘境攻击 +25%。","复苏祝福：本次秘境每次战斗胜利回复 25 生命。","丰收祝福：本次秘境战斗金币 +25%。"};
        result=delve_blessing(&game,p);if(result==OK)strcpy(message,blessings[p]);
    } else if (!strcmp(action,"delve_leave")) {
        result=delve_leave(&game);if(result==OK)strcpy(message,"已离开秘境，已获得的材料、金币和道具保留，本次祝福结束。");
    } else if (!strcmp(action, "upgrade")) {
        result = upgrade_weapon(&game, p, q, &success);
        if (result == OK) upgrade_result = success;
        if (result == OK) snprintf(message, sizeof(message), "%s", success ? "强化成功，武器攻击提升！" : (q ? "强化失败，保护符保住了等级。" : "强化失败，武器下降一级（最低 +0）。"));
    } else if (!strcmp(action, "equip")) result = game_equip(&game, p);
    else if (!strcmp(action, "salvage")) result = game_salvage(&game, p);
    else if (!strcmp(action, "salvage_many")) {
        char list[900], *at, *end; int ids[MAX_WEAPONS], count = 0;
        if (parameter(body, "ids", list, sizeof(list))) {
            at = list;
            while (*at && count < MAX_WEAPONS) {
                errno = 0; long id = strtol(at, &end, 10);
                if (errno || end == at || id < 1 || id > 100000000 || (*end && *end != '-')) { count = 0; break; }
                ids[count++] = (int)id;
                if (*end == '-' && !end[1]) { count = 0; break; }
                at = *end ? end + 1 : end;
            }
            if (!*at && count) {
                int gold = game.gold, iron = game.materials[IRON];
                result = game_salvage_many(&game, ids, count);
                if (result == OK) snprintf(message, sizeof(message), "批量回收 %d 把武器：铁矿 +%d，金币 +%d。", count, game.materials[IRON] - iron, game.gold - gold);
            }
        }
    }
    else if (!strcmp(action, "buy")) result = game_buy(&game, p, q);
    else if (!strcmp(action, "potion")) result = game_use_potion(&game);
    else if (!strcmp(action, "stamina")) {
        result = game_use_stamina(&game);
        if (result == OK) strcpy(message, "恢复 40 体力（最高 100），连续探索奖励保留。" );
    }
    else if (!strcmp(action, "rest")) { game_rest(&game); result = OK; strcpy(message, "炉火渐息，休息一天后生命与体力恢复。" ); }
    else if (!strcmp(action, "gather")) {
        int before[MATERIAL_COUNT];
        memcpy(before, game.materials, sizeof(before));
        result = game_gather(&game, p);
        if (result == OK) snprintf(message, sizeof(message), "采集完成：铁矿 +%d，硬木 +%d，灵晶 +%d，星陨石 +%d。途中发现了一个事件，选择你的收获。",
            game.materials[IRON] - before[IRON], game.materials[WOOD] - before[WOOD], game.materials[CRYSTAL] - before[CRYSTAL], game.materials[STAR] - before[STAR]);
    } else if (!strcmp(action, "event")) {
        int kind = game.event_kind, area = game.event_area;
        int hp = game.hp, energy = game.energy;
        result = game_resolve_event(&game, p);
        if (result == OK) {
            if (kind == 0) snprintf(message, sizeof(message), "打开宝箱！获得%s %d（资源达到上限时不再增加）。", p ? "金币" : material_names[areas[area].material], p ? 35 + area * 20 : 4 + area);
            else if (kind == 1) snprintf(message, sizeof(message), "清泉滋养：恢复 %d %s，连续探索奖励保留。", p ? game.energy - energy : game.hp - hp, p ? "体力" : "生命");
            else if (kind == 2) snprintf(message, sizeof(message), "祭坛领悟：获得 %d %s经验，达到等级上限后不再积累。", p ? 25 + area * 5 : 35 + area * 12, p ? "锻造师" : "角色");
            else strcpy(message, p ? "药师赠礼：获得沿途收集的硬木。" : "药师交易：花费 25 金币，获得 1 瓶体力药水。" );
        }
    } else if (!strcmp(action, "explore") || !strcmp(action, "boss")) {
        result = !strcmp(action, "boss") ? boss_begin(&game, p, &encounter) : battle_begin(&game, p, &encounter);
        if (result == OK) snprintf(message, sizeof(message), "遭遇%s！%s", encounter.boss ? areas[p].boss : areas[p].enemies[encounter.enemy],
            encounter.boss ? "首领每第三回合会发动重击，观察预警并防御。" : "破空斩造成双倍伤害，使用后冷却三个行动。" );
    } else if (!strcmp(action, "battle")) {
        BattleReport report;
        int gold = game.gold, xp = game.xp, level = game.level;
        int materials[MATERIAL_COUNT];
        memcpy(materials, game.materials, sizeof(materials));
        result = battle_step(&game, &encounter, p, &report);
        if (result == OK) last_report = report;
        if (result == OK) {
            if (report.outcome == 1) {
                int gained = game.xp - xp;
                for (int i = level; i < game.level; ++i) gained += level_xp_needed(i);
                snprintf(message, sizeof(message), "探索胜利！%d 金币、%d 经验；铁矿 +%d、硬木 +%d、灵晶 +%d、星陨石 +%d。连续 %d 胜，收益 +%d%%。", game.gold - gold, gained,
                    game.materials[IRON] - materials[IRON], game.materials[WOOD] - materials[WOOD], game.materials[CRYSTAL] - materials[CRYSTAL], game.materials[STAR] - materials[STAR], game.streak, (game.streak - 1) * 10);
                if (report.first_clear) {
                    size_t n = strlen(message);
                    snprintf(message + n, sizeof(message) - n, " 首领首通！永久 +2 攻击、+8 生命，获得幸运符与保护符，生命回满。%s", encounter.area == AREA_COUNT - 1 ? "六域已解放！" : "后续路线已开启，达到等级即可前往。");
                }
                if (game.delve.reward) {
                    size_t n=strlen(message);
                    snprintf(message+n,sizeof(message)-n," 秘境精英已击败，请在地图中选择本次探险的祝福。");
                }
            }
            else if (report.outcome == 2) strcpy(message, "战败后回到营地：损失 10% 金币，恢复半血。" );
            else if (report.outcome == 3) strcpy(message, "成功逃脱，安全回到营地。" );
            else snprintf(message, sizeof(message), "第 %d 回合：造成 %d 伤害，恢复 %d 生命，承受 %d 伤害。%s", encounter.turn, report.player_damage, report.healed, report.enemy_damage, report.heavy ? " 首领重击！" : "");
        }
    } else if (!strcmp(action, "slot") && p >= 0 && p < 4) {
        if (p == active_slot) return state_json(OK, "已经在这段旅程中。", -1);
        Game candidate;
        if (file_exists(slot_paths[p])) {
            if (!load_game(&candidate, slot_paths[p])) return state_json(FAILED, "这个存档损坏，文件已保留，请选择其他旅程。", -1);
        } else {
            game_init(&candidate, (uint32_t)time(NULL) ^ (uint32_t)(p * 7919));
            if (!save_game(&candidate, slot_paths[p])) return state_json(FAILED, "无法创建新存档，当前旅程已保留。", -1);
        }
        if (!corrupted_save && !save_game(&game, save_path)) return state_json(FAILED, "当前旅程保存失败，已取消切换。", -1);
        if (!write_marker(p)) return state_json(FAILED, "存档选择记录写入失败，已取消切换。", -1);
        set_slot(p); game = candidate; corrupted_save = 0;
        encounter = game.delve.battle; result = OK;
        snprintf(message, sizeof(message), "已进入旅程 %d，其他旅程的进度已保留。", p + 1);
    } else if (!strcmp(action, "load")) {
        result = load_game(&game, save_path) ? OK : FAILED;
        if (result == OK) encounter = game.delve.battle;
        if (result == OK) strcpy(message, "存档读取成功，欢迎回到剑炉。" );
    } else if (!strcmp(action, "save")) { result = OK; strcpy(message, "旅程已保存。" ); }
    else if (!strcmp(action, "new") && p == 1) {
        /* 用户在前端确认之后才发送 p=1，保留覆盖前的备份。 */
        if (file_exists(save_path) && !copy_save(save_path, backup_path))
            return state_json(FAILED, "备份旧存档失败，已取消新游戏。", -1);
        game_init(&game, (uint32_t)time(NULL)); corrupted_save = 0; result = OK;
        strcpy(message, "新的剑炉旅程开始，新手礼包已经放入背包。" );
    } else if (!strcmp(action, "quit")) {
        result = OK; api_running = 0;
        strcpy(message, corrupted_save ? "旧存档已保留，可以关闭游戏窗口。" : "旅程已保存，可以关闭游戏窗口。" );
    }
    if (result == OK && !corrupted_save) {
        int rewards = game_claim_quests(&game);
        save_ok = save_game(&game, save_path);
        if (!save_ok) {
            strcpy(message, "操作已完成，但存档写入失败，请检查文件夹权限。" );
            if (!strcmp(action, "quit")) { api_running = 1; result = FAILED; }
        }
        else if (rewards) {
            size_t n = strlen(message);
            (void)snprintf(message + n, sizeof(message) - n, " 达成成长里程碑，奖励已领取！");
        }
    } else if (result != OK) strcpy(message, errors[result]);
    return state_json(result, message, latest);
}


int api_initialize(const char *path, uint32_t seed) {
    int n = snprintf(save_path, sizeof(save_path), "%s", path);
    if (n < 0 || n >= (int)sizeof(save_path)) return 0;
    strcpy(slot_paths[0], path);
    const char *slash = strrchr(path, '/'), *backslash = strrchr(path, '\\');
    if (!slash || (backslash && backslash > slash)) slash = backslash;
    size_t prefix = slash ? (size_t)(slash - path + 1) : 0;
    if (prefix > 950) return 0;
    for (int i = 1; i < 4; ++i) snprintf(slot_paths[i], sizeof(slot_paths[i]), "%.*sgame.slot%d.sav", (int)prefix, path, i + 1);
    snprintf(marker_path, sizeof(marker_path), "%.*sactive-slot.txt", (int)prefix, path);
    int selected = 1; FILE *marker = fopen(marker_path, "r");
    if (marker) { if (fscanf(marker, "%d", &selected) != 1) selected = 1; fclose(marker); }
    if (selected < 1 || selected > 4 || (selected != 1 && !file_exists(slot_paths[selected - 1]))) selected = 1;
    set_slot(selected - 1);
    n = snprintf(backup_path, sizeof(backup_path), "%s.before-new.sav", save_path);
    if (n < 0 || n >= (int)sizeof(backup_path)) return 0;
    /* 保留 Windows 原有备份文件名称。 */
    char *extension = strrchr(backup_path, '/');
    extension = extension ? extension + 1 : backup_path;
    if (!strcmp(extension, "game.sav.before-new.sav")) strcpy(extension, "game.before-new.sav");
    api_running = 1; save_ok = 1; corrupted_save = 0;
    memset(&encounter, 0, sizeof(encounter));
    if (!load_game(&game, save_path)) {
        corrupted_save = file_exists(save_path);
        game_init(&game, seed);
        if (!corrupted_save) save_ok = save_game(&game, save_path);
    }
    if (!corrupted_save) encounter = game.delve.battle;
    return 1;
}
const char *api_state(void) { return state_json(OK, "", -1); }
int api_save(void) { return corrupted_save ? 0 : (save_ok = save_game(&game, save_path)); }
