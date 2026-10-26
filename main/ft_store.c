// FoloTalker 设置与凭证存储实现。
#include "ft_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 主机单元测试不链接 NVS。用 FT_HOST_TEST 把 NVS 后端整个排除,
// 这样"校验 + URL 组装"这些纯逻辑能在主机上跑,不必连硬件。
#ifndef FT_HOST_TEST
#include "nvs.h"
#include "nvs_flash.h"
#define FT_NVS_NAMESPACE "folotalker"
#endif

typedef enum {
    FT_STORE_BACKEND_MEM = 0,
    FT_STORE_BACKEND_NVS,
} ft_store_backend_t;

// 内存后端:一张小表,足够存 host/code/name/volume。
#define FT_MEM_MAX_ENTRIES 8
#define FT_MEM_MAX_VALUE   160

typedef struct {
    char key[48];
    char value[FT_MEM_MAX_VALUE];
    bool used;
    bool is_int;
    int ival;
} ft_mem_entry_t;

struct ft_store {
    ft_store_backend_t backend;
    ft_mem_entry_t mem[FT_MEM_MAX_ENTRIES];
#ifndef FT_HOST_TEST
    nvs_handle_t nvs;
#endif
};

// ---------------------------------------------------------------------------
// 内存后端
// ---------------------------------------------------------------------------

static ft_mem_entry_t *ft_mem_find(ft_store_t *st, const char *key, bool create)
{
    for (size_t i = 0; i < FT_MEM_MAX_ENTRIES; i++) {
        if (st->mem[i].used && strcmp(st->mem[i].key, key) == 0) {
            return &st->mem[i];
        }
    }
    if (!create) {
        return NULL;
    }
    for (size_t i = 0; i < FT_MEM_MAX_ENTRIES; i++) {
        if (!st->mem[i].used) {
            memset(&st->mem[i], 0, sizeof(st->mem[i]));
            st->mem[i].used = true;
            snprintf(st->mem[i].key, sizeof(st->mem[i].key), "%s", key);
            return &st->mem[i];
        }
    }
    return NULL;
}

ft_store_t *ft_store_new_mem(void)
{
    ft_store_t *st = (ft_store_t *)calloc(1, sizeof(ft_store_t));
    if (st == NULL) {
        return NULL;
    }
    st->backend = FT_STORE_BACKEND_MEM;
    return st;
}

void ft_store_free_mem(ft_store_t *st)
{
    free(st);
}

// ---------------------------------------------------------------------------
// NVS 后端
// ---------------------------------------------------------------------------

ft_store_t *ft_store_new_nvs(void)
{
#ifdef FT_HOST_TEST
    return NULL;
#else
    ft_store_t *st = (ft_store_t *)calloc(1, sizeof(ft_store_t));
    if (st == NULL) {
        return NULL;
    }
    st->backend = FT_STORE_BACKEND_NVS;
    esp_err_t err = nvs_open(FT_NVS_NAMESPACE, NVS_READWRITE, &st->nvs);
    if (err != ESP_OK) {
        free(st);
        return NULL;
    }
    return st;
#endif
}

void ft_store_free_nvs(ft_store_t *st)
{
    if (st == NULL) {
        return;
    }
#ifndef FT_HOST_TEST
    if (st->backend == FT_STORE_BACKEND_NVS) {
        nvs_close(st->nvs);
    }
#endif
    free(st);
}

// ---------------------------------------------------------------------------
// 统一读写接口
// ---------------------------------------------------------------------------

bool ft_store_get_str(ft_store_t *st, const char *key, char *out, size_t out_len)
{
    if (st == NULL || key == NULL || out == NULL || out_len == 0) {
        return false;
    }
    if (st->backend == FT_STORE_BACKEND_MEM) {
        ft_mem_entry_t *e = ft_mem_find(st, key, false);
        if (e == NULL || e->is_int) {
            return false;
        }
        snprintf(out, out_len, "%s", e->value);
        return true;
    }
#ifdef FT_HOST_TEST
    return false;
#else
    size_t len = out_len;
    esp_err_t err = nvs_get_str(st->nvs, key, out, &len);
    if (err != ESP_OK) {
        out[0] = '\0';
        return false;
    }
    out[out_len - 1] = '\0';
    return true;
#endif
}

bool ft_store_set_str(ft_store_t *st, const char *key, const char *value)
{
    if (st == NULL || key == NULL || value == NULL) {
        return false;
    }
    if (st->backend == FT_STORE_BACKEND_MEM) {
        ft_mem_entry_t *e = ft_mem_find(st, key, true);
        if (e == NULL) {
            return false;
        }
        e->is_int = false;
        snprintf(e->value, sizeof(e->value), "%s", value);
        return true;
    }
#ifdef FT_HOST_TEST
    return false;
#else
    esp_err_t err = nvs_set_str(st->nvs, key, value);
    if (err != ESP_OK) {
        return false;
    }
    return nvs_commit(st->nvs) == ESP_OK;
#endif
}

bool ft_store_get_int(ft_store_t *st, const char *key, int *out)
{
    if (st == NULL || key == NULL || out == NULL) {
        return false;
    }
    if (st->backend == FT_STORE_BACKEND_MEM) {
        ft_mem_entry_t *e = ft_mem_find(st, key, false);
        if (e == NULL || !e->is_int) {
            return false;
        }
        *out = e->ival;
        return true;
    }
#ifdef FT_HOST_TEST
    return false;
#else
    // nvs_get_i32 要的是 int32_t*,不能直接传 int*。
    int32_t v = 0;
    esp_err_t err = nvs_get_i32(st->nvs, key, &v);
    if (err != ESP_OK) {
        return false;
    }
    *out = (int)v;
    return true;
#endif
}

bool ft_store_set_int(ft_store_t *st, const char *key, int value)
{
    if (st == NULL || key == NULL) {
        return false;
    }
    if (st->backend == FT_STORE_BACKEND_MEM) {
        ft_mem_entry_t *e = ft_mem_find(st, key, true);
        if (e == NULL) {
            return false;
        }
        e->is_int = true;
        e->ival = value;
        return true;
    }
#ifdef FT_HOST_TEST
    return false;
#else
    esp_err_t err = nvs_set_i32(st->nvs, key, value);
    if (err != ESP_OK) {
        return false;
    }
    return nvs_commit(st->nvs) == ESP_OK;
#endif
}

bool ft_store_erase(ft_store_t *st, const char *key)
{
    if (st == NULL || key == NULL) {
        return false;
    }
    if (st->backend == FT_STORE_BACKEND_MEM) {
        ft_mem_entry_t *e = ft_mem_find(st, key, false);
        if (e != NULL) {
            memset(e, 0, sizeof(*e));
        }
        return true;
    }
#ifdef FT_HOST_TEST
    return false;
#else
    esp_err_t err = nvs_erase_key(st->nvs, key);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        return false;
    }
    return nvs_commit(st->nvs) == ESP_OK;
#endif
}

// ---------------------------------------------------------------------------
// 校验
// ---------------------------------------------------------------------------

bool ft_host_is_valid(const char *host)
{
    if (host == NULL) {
        return false;
    }
    size_t len = strlen(host);
    if (len == 0 || len >= FT_MAX_HOST_LEN) {
        return false;
    }
    // 允许 "host"、"host.domain"、"192.168.1.10"、"talk.home:8443"、"[::1]:8443"
    bool has_digit = false;
    bool has_alpha = false;
    for (size_t i = 0; i < len; i++) {
        char c = host[i];
        if (c >= '0' && c <= '9') {
            has_digit = true;
            continue;
        }
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
            has_alpha = true;
            continue;
        }
        if (c == '.' || c == '-' || c == '_' || c == ':' || c == '[' || c == ']') {
            continue;
        }
        return false;
    }
    // 纯数字串(1-5 位)当端口看,纯数字且更长当 IP 看;但不能只有端口没有主机
    if (!has_digit && !has_alpha) {
        return false;
    }
    // 至少要有一个字母或数字作为主机名的实质内容
    return true;
}

bool ft_nick_is_valid(const char *nick)
{
    if (nick == NULL) {
        return false;
    }
    size_t len = strlen(nick);
    // 服务端按 slice(0, 20) 截断,所以这里限制在 20 个字节内,避免用户以为没保存上。
    if (len == 0 || len > 20) {
        return false;
    }
    return true;
}

bool ft_code_is_valid(const char *code)
{
    if (code == NULL) {
        return false;
    }
    size_t len = strlen(code);
    // 服务端要求 FAMILY_CODE 至少 4 个字符
    if (len < 4) {
        return false;
    }
    if (len >= FT_MAX_CODE_LEN) {
        return false;
    }
    return true;
}

size_t ft_build_ws_url(char *buf, size_t buf_len, const char *host, int port, const char *path)
{
    if (buf == NULL || buf_len == 0 || host == NULL || host[0] == '\0') {
        return 0;
    }
    if (path == NULL || path[0] == '\0') {
        path = "/ws";
    }
    // 服务端对外只暴露 HTTPS,WebSocket 走 wss。
    int n;
    if (port > 0 && port < 65536) {
        n = snprintf(buf, buf_len, "wss://%s:%d%s", host, port, path);
    } else {
        n = snprintf(buf, buf_len, "wss://%s%s", host, path);
    }
    if (n < 0 || (size_t)n >= buf_len) {
        buf[0] = '\0';
        return 0;
    }
    return (size_t)n;
}
