// FoloTalker Wi-Fi 配网 —— 通过 BLE BLUFI 接收 Wi-Fi 凭证。
//
// 复用 demo/blufi-provisioning 分支的实现思路(BLUFI 官方安全协商 +
// 手机小程序下发 SSID/密码),但为本应用重写了生命周期管理与状态通知。
//
// 安全协商的代码来自 ESP-IDF 5.5 的 BLUFI 安全示例(Unlicense / CC0-1.0),
// 见 assets/ 下的 NOTICE 说明。
#pragma once

#include <stdbool.h>

#include "esp_wifi_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// 配网状态
typedef enum {
    FT_PROV_IDLE = 0,
    FT_PROV_ADVERTISING,   // 正在广播,等待手机连接
    FT_PROV_CONNECTED,    // 手机已连上
    FT_PROV_CONNECTING,   // 正在连接 Wi-Fi
    FT_PROV_SUCCESS,      // 已连上 Wi-Fi
    FT_PROV_FAILED,       // 失败
} ft_prov_state_t;

// 状态变化回调。运行在 BLE 栈的回调上下文,只能做有界操作。
typedef void (*ft_prov_cb_t)(ft_prov_state_t state, const char *message);

// 初始化 BLE + BLUFI。返回 false 表示失败。
bool ft_prov_init(ft_prov_cb_t cb);

// 反初始化,释放 NimBLE 主机与 BLUFI 资源。
void ft_prov_deinit(void);

// 开始广播,等待手机配网。
bool ft_prov_start(void);

// 停止广播。
void ft_prov_stop(void);

// 当前状态。
ft_prov_state_t ft_prov_state(void);

// 状态到中文文案的映射。
const char *ft_prov_state_text(ft_prov_state_t state);

// 从加密 NVS 读回已保存的 Wi-Fi 凭证。
// 成功返回 true 并把凭证填进 cfg;没有保存过则返回 false。
bool ft_prov_load_stored_credentials(wifi_config_t *cfg);

// 用已保存的凭证连接 Wi-Fi。返回 true 表示凭证存在且连接已发起。
bool ft_prov_reconnect_stored(void);

#ifdef __cplusplus
}
#endif
