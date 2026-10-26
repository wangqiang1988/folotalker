// FoloTalker 本地设置与凭证存储 —— 纯逻辑 + NVS 读写。
//
// 存的东西:
//   - 服务器地址(主机名或 IP,不含 scheme)
//   - 家庭口令
//   - 昵称
//   - 播放音量
//
// 安全:口令与地址存在加密 NVS 分区(见 sdkconfig.defaults 的
// CONFIG_NVS_ENCRYPTION=y)。写入 API 与读出 API 分离,便于主机测试替换。
#pragma once

#include "ft_state.h"

#ifdef __cplusplus
extern "C" {
#endif

// 存储后端的抽象。默认实现用 NVS;主机测试用内存实现。
// 这样"存取逻辑"本身可以在主机上跑,不必连硬件。
typedef struct ft_store ft_store_t;

// 内存实现(主机测试用)。
ft_store_t *ft_store_new_mem(void);
void ft_store_free_mem(ft_store_t *st);

// NVS 实现。namespace 固定为 "folotalker"。
// 返回 NULL 表示 NVS 不可用(例如加密分区未启用)。
ft_store_t *ft_store_new_nvs(void);
void ft_store_free_nvs(ft_store_t *st);

// 读一个字符串。key 不存在时返回 false,out 不被修改。
bool ft_store_get_str(ft_store_t *st, const char *key, char *out, size_t out_len);

// 写一个字符串。
bool ft_store_set_str(ft_store_t *st, const char *key, const char *value);

// 读一个整数。
bool ft_store_get_int(ft_store_t *st, const char *key, int *out);

// 写一个整数。
bool ft_store_set_int(ft_store_t *st, const char *key, int value);

// 删除一个 key。
bool ft_store_erase(ft_store_t *st, const char *key);

// ---------------------------------------------------------------------------
// 设置项的语义层:键名、默认值、校验
// ---------------------------------------------------------------------------

#define FT_STORE_KEY_HOST   "host"    // 服务器地址
#define FT_STORE_KEY_CODE   "code"    // 家庭口令
#define FT_STORE_KEY_NAME   "name"    // 昵称
#define FT_STORE_KEY_VOLUME "volume"  // 播放音量 0..100

#define FT_DEFAULT_VOLUME 70

// 校验服务器主机名/IP 是否可用。只检查格式,不检查可达性。
// 允许: 字母数字、点、连字符、冒号(端口)。
bool ft_host_is_valid(const char *host);

// 校验昵称。服务端会截断到 20 字符,这里提前拒绝空串和过长。
bool ft_nick_is_valid(const char *nick);

// 校验口令。服务端要求至少 4 个字符。
bool ft_code_is_valid(const char *code);

// 把 "host" + 可选端口 + 路径 组装成 wss:// URL。
// port <= 0 时不带端口。path 为 NULL 时用 "/ws"。
// 返回写入长度;buf 不足返回 0。
size_t ft_build_ws_url(char *buf, size_t buf_len, const char *host, int port, const char *path);

#ifdef __cplusplus
}
#endif
