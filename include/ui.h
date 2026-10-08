#ifndef FORGE_UI_H
#define FORGE_UI_H
#include "game.h"
void ui_init(void);
int ui_number(const char *prompt, int low, int high);
void ui_result(int result);
void ui_status(const Game *g);
void ui_inventory(const Game *g);
void ui_forge(Game *g);
void ui_equipment(Game *g);
void ui_adventure(Game *g);
void ui_shop(Game *g);
void ui_quests(Game *g);
int ui_eof(void);
#endif
