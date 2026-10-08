#ifndef FORGE_GAME_API_H
#define FORGE_GAME_API_H
#include <stdint.h>
/* 调用者负责串行访问；返回 JSON 指向内部缓冲区，复制完再进行下一次调用。 */
extern int api_running;
int api_initialize(const char *path, uint32_t seed);
const char *api_state(void);
const char *api_action(const char *body);
int api_save(void);
#endif
