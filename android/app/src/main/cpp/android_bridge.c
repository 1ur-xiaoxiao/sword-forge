#include <jni.h>
#include <stdio.h>
#include <time.h>
#include "game_api.h"

JNIEXPORT jboolean JNICALL Java_cn_swordforge_game_MainActivity_nativeInit(JNIEnv *env, jclass type, jstring directory) {
    (void)type;
    const char *root = (*env)->GetStringUTFChars(env, directory, NULL);
    if (!root) return JNI_FALSE;
    char path[1024];
    int n = snprintf(path, sizeof(path), "%s/game.sav", root);
    (*env)->ReleaseStringUTFChars(env, directory, root);
    return n > 0 && n < (int)sizeof(path) && api_initialize(path, (uint32_t)time(NULL)) ? JNI_TRUE : JNI_FALSE;
}
JNIEXPORT jstring JNICALL Java_cn_swordforge_game_MainActivity_nativeState(JNIEnv *env, jclass type) {
    (void)type;
    return (*env)->NewStringUTF(env, api_state());
}
JNIEXPORT jstring JNICALL Java_cn_swordforge_game_MainActivity_nativeAction(JNIEnv *env, jclass type, jstring input) {
    (void)type;
    const char *body = (*env)->GetStringUTFChars(env, input, NULL);
    if (!body) return NULL;
    jstring result = (*env)->NewStringUTF(env, api_action(body));
    (*env)->ReleaseStringUTFChars(env, input, body);
    return result;
}
JNIEXPORT jboolean JNICALL Java_cn_swordforge_game_MainActivity_nativeSave(JNIEnv *env, jclass type) {
    (void)env; (void)type;
    return api_save() ? JNI_TRUE : JNI_FALSE;
}
