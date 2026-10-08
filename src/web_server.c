/* 图形版入口：Windows 无控制台程序 + 仅供本机使用的 HTTP 接口。
   HTML/CSS/JS 负责画面，所有游戏决策仍调用原来的 C 核心函数。 */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <shellapi.h>
#include <direct.h>
#include "game.h"
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static Game game;
static Battle encounter;
static int running = 1, save_ok = 1, corrupted_save = 0;
static const char *save_path = "saves/game.sav";
static char json[65536];
static size_t json_length;

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
    append("],\"battle\":{\"active\":%s,\"area\":%d,\"hp\":%d,\"maxHp\":%d,\"attack\":%d,\"turn\":%d,\"boss\":%s,\"enemy\":%d,\"cooldown\":%d}}",
           encounter.active ? "true" : "false", encounter.area, encounter.hp,
           encounter.max_hp, encounter.attack, encounter.turn, encounter.boss ? "true" : "false", encounter.enemy, encounter.skill_cooldown);
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

static const char *action_json(const char *body) {
    char action[32], message[512] = "操作完成。";
    int result = BAD_CHOICE, latest = -1, success = 0;
    int p = number_parameter(body, "p"), q = number_parameter(body, "q");
    static const char *const errors[] = {"操作完成。", "操作参数无效。", "尚未解锁，或武器正在装备。",
        "资源不足，先去探索收集材料吧。", "背包已满，请先回收武器。", "已经达到上限。", "操作失败。"};
    if (!parameter(body, "action", action, sizeof(action))) return state_json(BAD_CHOICE, errors[BAD_CHOICE], -1);
    if (corrupted_save && strcmp(action, "new") && strcmp(action, "quit"))
        return state_json(FAILED, "存档无法读取，请先备份旧文件，再在设置里确认开始新旅程。", -1);
    if (encounter.active && strcmp(action, "battle") && strcmp(action, "quit"))
        return state_json(LOCKED, "请先结束当前战斗。", -1);
    if (game.event_kind >= 0 && strcmp(action, "event") && strcmp(action, "save") && strcmp(action, "quit") && strcmp(action, "new"))
        return state_json(LOCKED, "请先在地图上选择当前探索事件的奖励。", -1);
    if (!strcmp(action, "forge")) {
        result = forge_weapon(&game, p, q, &latest);
        if (result == OK) snprintf(message, sizeof(message), "锻造完成！获得%s品质的%s。", rarity_names[game.weapons[latest].rarity], recipes[p].name);
    } else if (!strcmp(action, "upgrade")) {
        result = upgrade_weapon(&game, p, q, &success);
        if (result == OK) snprintf(message, sizeof(message), "%s", success ? "强化成功，武器攻击提升！" : (q ? "强化失败，保护符保住了等级。" : "强化失败，武器下降一级（最低 +0）。"));
    } else if (!strcmp(action, "equip")) result = game_equip(&game, p);
    else if (!strcmp(action, "salvage")) result = game_salvage(&game, p);
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
            }
            else if (report.outcome == 2) strcpy(message, "战败后回到营地：损失 10% 金币，恢复半血。" );
            else if (report.outcome == 3) strcpy(message, "成功逃脱，安全回到营地。" );
            else snprintf(message, sizeof(message), "第 %d 回合：造成 %d 伤害，恢复 %d 生命，承受 %d 伤害。%s", encounter.turn, report.player_damage, report.healed, report.enemy_damage, report.heavy ? " 首领重击！" : "");
        }
    } else if (!strcmp(action, "load")) {
        result = load_game(&game, save_path) ? OK : FAILED;
        if (result == OK) strcpy(message, "存档读取成功，欢迎回到剑炉。" );
    } else if (!strcmp(action, "save")) { result = OK; strcpy(message, "旅程已保存。" ); }
    else if (!strcmp(action, "new") && p == 1) {
        /* 用户在前端确认之后才发送 p=1，保留覆盖前的备份。 */
        if (GetFileAttributesA(save_path) != INVALID_FILE_ATTRIBUTES &&
            !CopyFileA(save_path, "saves/game.before-new.sav", FALSE))
            return state_json(FAILED, "备份旧存档失败，已取消新游戏。", -1);
        game_init(&game, (uint32_t)time(NULL)); corrupted_save = 0; result = OK;
        strcpy(message, "新的剑炉旅程开始，新手礼包已经放入背包。" );
    } else if (!strcmp(action, "quit")) {
        result = OK; running = 0;
        strcpy(message, corrupted_save ? "旧存档已保留，可以关闭游戏窗口。" : "旅程已保存，可以关闭游戏窗口。" );
    }
    if (result == OK && !corrupted_save) {
        int rewards = game_claim_quests(&game);
        save_ok = save_game(&game, save_path);
        if (!save_ok) {
            strcpy(message, "操作已完成，但存档写入失败，请检查文件夹权限。" );
            if (!strcmp(action, "quit")) { running = 1; result = FAILED; }
        }
        else if (rewards) {
            size_t n = strlen(message);
            (void)snprintf(message + n, sizeof(message) - n, " 达成成长里程碑，奖励已领取！");
        }
    } else if (result != OK) strcpy(message, errors[result]);
    return state_json(result, message, latest);
}

static int send_all(SOCKET socket, const char *data, int length) {
    while (length > 0) {
        int n = send(socket, data, length, 0);
        if (n <= 0) return 0;
        data += n; length -= n;
    }
    return 1;
}

static void response(SOCKET socket, int status, const char *type, const char *data, int length) {
    char header[1024];
    int n = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %d\r\nConnection: close\r\nCache-Control: no-store\r\n"
        "X-Content-Type-Options: nosniff\r\nContent-Security-Policy: default-src 'self'; img-src 'self' data:; style-src 'self' 'unsafe-inline'; script-src 'self'; connect-src 'self'; object-src 'none'; frame-ancestors 'none'\r\n\r\n",
        status, status == 200 ? "OK" : "Error", type, length);
    if (n > 0 && n < (int)sizeof(header) && send_all(socket, header, n)) (void)send_all(socket, data, length);
}

/* 区分大小写只限字段值；HTTP 头名字不区分大小写。 */
static int header_value(const char *request, const char *name, char *out, size_t size) {
    const char *line = strstr(request, "\r\n");
    size_t name_length = strlen(name);
    if (!line) return 0;
    line += 2;
    while (*line && strncmp(line, "\r\n", 2)) {
        const char *end = strstr(line, "\r\n");
        if (!end) return 0;
        if ((size_t)(end - line) > name_length && !_strnicmp(line, name, name_length) && line[name_length] == ':') {
            const char *start = line + name_length + 1;
            while (start < end && (*start == ' ' || *start == '\t')) ++start;
            size_t n = (size_t)(end - start);
            if (n >= size) return 0;
            memcpy(out, start, n); out[n] = '\0'; return 1;
        }
        line = end + 2;
    }
    return 0;
}

static void handle_request(SOCKET socket, int port) {
    char request[8192], method[16], path[256], host[128], expected[128], value[128];
    int used = 0, content_length = 0;
    char *body = NULL;
    DWORD timeout = 2000;
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout));
    setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeout, sizeof(timeout));
    while (used < (int)sizeof(request) - 1) {
        int n = recv(socket, request + used, (int)sizeof(request) - 1 - used, 0);
        if (n <= 0) return;
        used += n; request[used] = '\0';
        body = strstr(request, "\r\n\r\n");
        if (body) break;
    }
    if (!body || sscanf(request, "%15s %255s", method, path) != 2) { response(socket, 400, "text/plain", "Bad request", 11); return; }
    body += 4;
    (void)snprintf(expected, sizeof(expected), "127.0.0.1:%d", port);
    if (!header_value(request, "Host", host, sizeof(host)) || strcmp(host, expected)) { response(socket, 403, "text/plain", "Invalid host", 12); return; }
    if (!strcmp(method, "POST") && !strcmp(path, "/api/action")) {
        (void)snprintf(expected, sizeof(expected), "http://127.0.0.1:%d", port);
        if (header_value(request, "Transfer-Encoding", value, sizeof(value))) { response(socket, 400, "text/plain", "Invalid body", 12); return; }
        if (!header_value(request, "Content-Length", value, sizeof(value))) { response(socket, 400, "text/plain", "Missing length", 14); return; }
        char *end;
        errno = 0;
        long parsed = strtol(value, &end, 10);
        if (errno || *end || end == value || parsed < 0 || parsed > 1024) { response(socket, 400, "text/plain", "Invalid length", 14); return; }
        content_length = (int)parsed;
        int offset = (int)(body - request);
        while (used - offset < content_length && used < (int)sizeof(request) - 1) {
            int n = recv(socket, request + used, (int)sizeof(request) - 1 - used, 0);
            if (n <= 0) return;
            used += n;
        }
        if (used - offset < content_length) return;
        body[content_length] = '\0';
        if (!header_value(request, "Origin", value, sizeof(value)) || strcmp(value, expected)) { response(socket, 403, "text/plain", "Invalid origin", 14); return; }
        const char *data = action_json(body);
        response(socket, 200, "application/json; charset=utf-8", data, (int)strlen(data));
        return;
    }
    if (strcmp(method, "GET")) { response(socket, 405, "text/plain", "Method not allowed", 18); return; }
    if (!strcmp(path, "/api/state")) {
        const char *data = state_json(OK, "", -1);
        response(socket, 200, "application/json; charset=utf-8", data, (int)strlen(data)); return;
    }
    /* 只提供这几份静态文件，任何其他路径均不能读取本地文件。 */
    const char *file = NULL, *type = NULL;
    if (!strcmp(path, "/")) { file = "web/index.html"; type = "text/html; charset=utf-8"; }
    if (!strcmp(path, "/styles.css")) { file = "web/styles.css"; type = "text/css; charset=utf-8"; }
    if (!strcmp(path, "/app.js")) { file = "web/app.js"; type = "text/javascript; charset=utf-8"; }
    if (!strcmp(path, "/assets/forge.svg")) { file = "web/assets/forge.svg"; type = "image/svg+xml"; }
    if (!file) { response(socket, 404, "text/plain", "Not found", 9); return; }
#ifdef FORGE_EMBEDDED_ASSETS
    int resource_id = !strcmp(file, "web/index.html") ? 101 : !strcmp(file, "web/styles.css") ? 102 : !strcmp(file, "web/app.js") ? 103 : 104;
    HMODULE module = GetModuleHandleW(NULL);
    HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(resource_id), (LPCWSTR)RT_RCDATA);
    HGLOBAL loaded = resource ? LoadResource(module, resource) : NULL;
    const char *data = loaded ? LockResource(loaded) : NULL;
    DWORD length = resource ? SizeofResource(module, resource) : 0;
    if (!data || !length || length > 2000000) { response(socket, 500, "text/plain", "Missing resource", 16); return; }
    response(socket, 200, type, data, (int)length);
#else
    FILE *f = fopen(file, "rb");
    if (!f) { response(socket, 404, "text/plain", "Not found", 9); return; }
    if (fseek(f, 0, SEEK_END)) { fclose(f); return; }
    long length = ftell(f);
    if (length < 0 || length > 2000000 || fseek(f, 0, SEEK_SET)) { fclose(f); return; }
    char *data = malloc((size_t)length + 1);
    if (!data) { fclose(f); return; }
    size_t n = fread(data, 1, (size_t)length, f);
    fclose(f);
    if (n == (size_t)length) response(socket, 200, type, data, (int)length);
    free(data);
#endif
}

static void open_game_window(int port) {
    wchar_t url[128], browser[1024], args[256];
    swprintf(url, 128, L"http://127.0.0.1:%d/", port);
    const wchar_t *folders[] = {L"ProgramFiles(x86)", L"ProgramFiles", L"LOCALAPPDATA"};
    for (int i = 0; i < 3; ++i) {
        DWORD n = GetEnvironmentVariableW(folders[i], browser, 800);
        if (!n || n >= 800) continue;
        wcscat(browser, L"\\Microsoft\\Edge\\Application\\msedge.exe");
        if (GetFileAttributesW(browser) == INVALID_FILE_ATTRIBUTES) continue;
        swprintf(args, 256, L"--app=%ls --window-size=1440,960", url);
        if ((INT_PTR)ShellExecuteW(NULL, L"open", browser, args, NULL, SW_SHOWNORMAL) > 32) return;
    }
    (void)ShellExecuteW(NULL, L"open", url, NULL, NULL, SW_SHOWNORMAL);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command, int show) {
    WSADATA winsock;
    struct sockaddr_in address;
    int port = 18973, headless = strstr(command, "--headless") != NULL;
    wchar_t module[2048], mutex_name[64];
    (void)instance; (void)previous; (void)show;
    char *port_arg = strstr(command, "--port ");
    if (port_arg && (sscanf(port_arg + 7, "%d", &port) != 1 || port < 1024 || port > 65535)) return 1;
    /* 向上寻找 web/index.html，兼容普通 build 与 CMake 的嵌套目录。 */
    DWORD path_length = GetModuleFileNameW(NULL, module, 2048);
    if (!path_length || path_length >= 2048) return 1;
    wchar_t *slash = wcsrchr(module, L'\\');
    if (!slash) return 1;
    *slash = L'\0';
#ifndef FORGE_EMBEDDED_ASSETS
    int found_root = 0;
    for (int i = 0; i < 5; ++i) {
        wchar_t candidate[2100];
        swprintf(candidate, 2100, L"%ls\\web\\index.html", module);
        if (GetFileAttributesW(candidate) != INVALID_FILE_ATTRIBUTES) { found_root = 1; break; }
        slash = wcsrchr(module, L'\\');
        if (!slash) break;
        *slash = L'\0';
    }
    if (!found_root) {
        if (!headless) MessageBoxW(NULL, L"找不到界面文件。请将 web 文件夹与 build 文件夹保留在同一项目目录中。", L"剑炉物语", MB_OK | MB_ICONERROR);
        return 1;
    }
#endif
    if (_wchdir(module)) return 1;
    swprintf(mutex_name, 64, L"Local\\SwordForge_%d", port);
    HANDLE mutex = CreateMutexW(NULL, FALSE, mutex_name);
    if (!mutex) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (!headless) open_game_window(port);
        CloseHandle(mutex); return 0;
    }
    if (WSAStartup(MAKEWORD(2, 2), &winsock)) { CloseHandle(mutex); return 1; }
    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET; address.sin_port = htons((u_short)port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (listener == INVALID_SOCKET || bind(listener, (struct sockaddr *)&address, sizeof(address)) || listen(listener, 16)) {
        if (!headless) MessageBoxW(NULL, L"游戏启动失败：本机端口被占用。请关闭其他游戏窗口后重试。", L"剑炉物语", MB_OK | MB_ICONERROR);
        if (listener != INVALID_SOCKET) closesocket(listener);
        WSACleanup(); CloseHandle(mutex); return 1;
    }
    (void)_mkdir("saves");
    if (!load_game(&game, save_path)) {
        corrupted_save = GetFileAttributesA(save_path) != INVALID_FILE_ATTRIBUTES;
        game_init(&game, (uint32_t)time(NULL));
        if (!corrupted_save) save_ok = save_game(&game, save_path);
    }
    if (!headless) open_game_window(port);
    ULONGLONG last_request = GetTickCount64();
    while (running && GetTickCount64() - last_request < 120000) {
        fd_set reads;
        FD_ZERO(&reads); FD_SET(listener, &reads);
        struct timeval wait = {1, 0};
        int selected = select(0, &reads, NULL, NULL, &wait);
        if (selected < 0) break;
        if (!selected) continue;
        SOCKET client = accept(listener, NULL, NULL);
        if (client == INVALID_SOCKET) continue;
        last_request = GetTickCount64();
        handle_request(client, port); closesocket(client);
    }
    closesocket(listener); WSACleanup(); CloseHandle(mutex);
    return 0;
}
