/* 图形版入口：Windows 无控制台程序 + 仅供本机使用的 HTTP 接口。
   HTML/CSS/JS 负责画面，所有游戏决策仍调用原来的 C 核心函数。 */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <shellapi.h>
#include <direct.h>
#include "game_api.h"
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
        const char *data = api_action(body);
        response(socket, 200, "application/json; charset=utf-8", data, (int)strlen(data));
        return;
    }
    if (strcmp(method, "GET")) { response(socket, 405, "text/plain", "Method not allowed", 18); return; }
    if (!strcmp(path, "/api/state")) {
        const char *data = api_state();
        response(socket, 200, "application/json; charset=utf-8", data, (int)strlen(data)); return;
    }
    /* 只提供这几份静态文件，任何其他路径均不能读取本地文件。 */
    const char *file = NULL, *type = NULL;
    if (!strcmp(path, "/")) { file = "web/index.html"; type = "text/html; charset=utf-8"; }
    if (!strcmp(path, "/styles.css")) { file = "web/styles.css"; type = "text/css; charset=utf-8"; }
    if (!strcmp(path, "/app.js")) { file = "web/app.js"; type = "text/javascript; charset=utf-8"; }
    if (!strcmp(path, "/scene.js")) { file = "web/scene.js"; type = "text/javascript; charset=utf-8"; }
    if (!strcmp(path, "/assets/forge.svg")) { file = "web/assets/forge.svg"; type = "image/svg+xml"; }
    if (!strcmp(path,"/vendor/rot.js")) { file="web/vendor/rot.js"; type="text/javascript; charset=utf-8"; }
    if (!strcmp(path,"/dungeon.js")) { file="web/dungeon.js"; type="text/javascript; charset=utf-8"; }
    if (!strcmp(path,"/vendor/ROT-LICENSE.txt")) { file="web/vendor/ROT-LICENSE.txt"; type="text/plain; charset=utf-8"; }
    if (!file) { response(socket, 404, "text/plain", "Not found", 9); return; }
#ifdef FORGE_EMBEDDED_ASSETS
    int resource_id = !strcmp(file, "web/index.html") ? 101 : !strcmp(file, "web/styles.css") ? 102 : !strcmp(file, "web/app.js") ? 103 : !strcmp(file, "web/scene.js") ? 105 : !strcmp(file,"web/vendor/rot.js") ? 106 : !strcmp(file,"web/dungeon.js") ? 107 : !strcmp(file,"web/vendor/ROT-LICENSE.txt") ? 108 : 104;
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
    if (!api_initialize("saves/game.sav", (uint32_t)time(NULL))) { closesocket(listener); WSACleanup(); CloseHandle(mutex); return 1; }
    if (!headless) open_game_window(port);
    ULONGLONG last_request = GetTickCount64();
    while (api_running && GetTickCount64() - last_request < 120000) {
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
