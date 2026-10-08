#include "game.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

/* 文本存档先写临时文件，写完后再替换；读档先验证临时 Game。 */
int save_game(const Game *g, const char *path) {
    char temporary[1024];
    FILE *f;
    int good, written;
    if (!game_validate(g)) return 0;
    written = snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    if (written < 0 || written >= (int)sizeof(temporary)) return 0;
    f = fopen(temporary, "w");
    if (!f) return 0;
    good = fprintf(f, "FORGE_SAVE_V3\n%lu\n%d %d %d %d\n%d %d %d %d\n%d %d %d\n%d %d %d %d\n%d\n",
                   (unsigned long)g->rng, g->level, g->xp, g->smith_level, g->smith_xp,
                   g->hp, g->energy, g->gold, g->day, g->weapon_count, g->equipped, g->next_id,
                   g->forged, g->wins, g->pity, g->quest_flags, g->streak) >= 0;
    if (fprintf(f, "%d %d %d\n", g->boss_flags, g->event_area, g->event_kind) < 0) good = 0;
    for (int i = 0; i < AREA_COUNT; ++i)
        if (fprintf(f, "%d %d\n", g->area_wins[i], g->surveys[i]) < 0) good = 0;
    for (int i = 0; i < MATERIAL_COUNT; ++i)
        if (fprintf(f, "%d%c", g->materials[i], i + 1 == MATERIAL_COUNT ? '\n' : ' ') < 0) good = 0;
    for (int i = 0; i < ITEM_COUNT; ++i)
        if (fprintf(f, "%d%c", g->items[i], i + 1 == ITEM_COUNT ? '\n' : ' ') < 0) good = 0;
    for (int i = 0; i < g->weapon_count; ++i) {
        const Weapon *w = &g->weapons[i];
        if (fprintf(f, "%d %d %d %d %d\n", w->id, w->recipe, w->rarity, w->base_attack, w->upgrade) < 0) good = 0;
    }
    if (fprintf(f, "END\n") < 0 || fflush(f) != 0) good = 0;
    if (fclose(f) != 0) good = 0;
    if (!good) { remove(temporary); return 0; }
#ifdef _WIN32
    good = MoveFileExA(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    good = rename(temporary, path) == 0;
#endif
    if (!good) remove(temporary);
    return good;
}

static int read_number(FILE *f, long long low, long long high, long long *out) {
    char token[64], *end;
    long long value;
    if (fscanf(f, "%63s", token) != 1) return 0;
    errno = 0;
    value = strtoll(token, &end, 10);
    if (errno || end == token || *end || value < low || value > high) return 0;
    *out = value;
    return 1;
}

static int read_int(FILE *f, int *out) {
    long long value;
    if (!read_number(f, INT_MIN, INT_MAX, &value)) return 0;
    *out = (int)value;
    return 1;
}

static int read_body(FILE *f, Game *candidate) {
    char token[64];
    long long state;
    if (fscanf(f, "%63s", token) != 1) return 0;
    int version = !strcmp(token, "FORGE_SAVE_V1") ? 1 : (!strcmp(token, "FORGE_SAVE_V2") ? 2 : (!strcmp(token, "FORGE_SAVE_V3") ? 3 : 0));
    if (!version) return 0;
    if (!read_number(f, 1, UINT32_MAX, &state)) return 0;
    candidate->rng = (uint32_t)state;
#define READ_FIELD(field) do { if (!read_int(f, &candidate->field)) return 0; } while (0)
    READ_FIELD(level); READ_FIELD(xp); READ_FIELD(smith_level); READ_FIELD(smith_xp);
    READ_FIELD(hp); READ_FIELD(energy); READ_FIELD(gold); READ_FIELD(day);
    READ_FIELD(weapon_count); READ_FIELD(equipped); READ_FIELD(next_id);
    READ_FIELD(forged); READ_FIELD(wins); READ_FIELD(pity); READ_FIELD(quest_flags);
    if (version >= 2) READ_FIELD(streak);
    candidate->event_area = candidate->event_kind = -1;
    if (version >= 3) {
        READ_FIELD(boss_flags); READ_FIELD(event_area); READ_FIELD(event_kind);
        for (int i = 0; i < AREA_COUNT; ++i) { READ_FIELD(area_wins[i]); READ_FIELD(surveys[i]); }
    }
    if (candidate->weapon_count < 1 || candidate->weapon_count > MAX_WEAPONS) return 0;
    for (int i = 0; i < MATERIAL_COUNT; ++i) READ_FIELD(materials[i]);
    for (int i = 0; i < (version == 1 ? 3 : ITEM_COUNT); ++i) READ_FIELD(items[i]);
    for (int i = 0; i < candidate->weapon_count; ++i) {
        READ_FIELD(weapons[i].id); READ_FIELD(weapons[i].recipe); READ_FIELD(weapons[i].rarity);
        READ_FIELD(weapons[i].base_attack); READ_FIELD(weapons[i].upgrade);
    }
#undef READ_FIELD
    if (fscanf(f, "%63s", token) != 1 || strcmp(token, "END") != 0) return 0;
    int ch;
    while ((ch = fgetc(f)) != EOF) if (!isspace((unsigned char)ch)) return 0;
    return !ferror(f) && game_validate(candidate);
}

int load_game(Game *g, const char *path) {
    Game candidate;
    FILE *f = fopen(path, "r");
    int good;
    if (!f) return 0;
    memset(&candidate, 0, sizeof(candidate));
    good = read_body(f, &candidate);
    if (fclose(f) != 0) good = 0;
    if (good) *g = candidate;
    return good;
}
