// FoloTalker 应用状态机 —— 纯逻辑,可在主机上单测。
//
// 这里集中放"与设备无关"的决策:
//   - 连接状态迁移(何时重连、何时放弃)
//   - 麦权(本地想说话 / 服务端当前谁在说话)
//   - 成员列表维护
//   - 重连退避计算
//
// UI 只读这个状态,网络层只按这个状态发指令。这样把最容易出错的时序逻辑
// 从 LVGL 和网络回调里抽出来,主机测试就能覆盖。
#pragma once

#include "ft_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FT_MAX_NICK_LEN      FT_MAX_NAME_LEN
#define FT_MAX_PATH_LEN      129   // 含结尾 '\0'
#define FT_MAX_URL_LEN       256   // 含结尾 '\0'

// 重连退避
#define FT_BACKOFF_MIN_MS  1000
#define FT_BACKOFF_MAX_MS  10000
#define FT_JOIN_TIMEOUT_MS 5000   // 与服务端 JOIN_TIMEOUT_MS 一致

// 应用页面
typedef enum {
    FT_PAGE_BOOT = 0,     // 启动 / 初始化
    FT_PAGE_WIFI,         // Wi-Fi 未连接,提示去配网
    FT_PAGE_PROVISION,    // BLE 配网中
    FT_PAGE_HOST,         // 输入服务器地址
    FT_PAGE_GATE,         // 输入昵称 + 家庭口令
    FT_PAGE_ROOM,         // 房间主界面
    FT_PAGE_SETTINGS,     // 设置(音量等)
} ft_page_t;

typedef enum {
    FT_SPEAK_IDLE = 0,    // 没在说话
    FT_SPEAK_REQUESTED,   // 本地按住了,还没拿到麦
    FT_SPEAK_ACTIVE,      // 持有麦,正在发送
    FT_SPEAK_DENIED,      // 被拒绝(麦被占 / 超时)
} ft_speak_state_t;

typedef struct {
    ft_conn_state_t conn;
    ft_page_t page;

    // 麦权
    ft_speak_state_t speak;
    bool mic_muted;         // 本地静音(不发音频)
    bool remote_speaking;   // 远端有人正在说话
    char remote_speaker[FT_MAX_NAME_LEN];

    // 成员
    ft_member_t members[FT_MAX_MEMBERS];
    size_t member_count;

    // 重连
    uint32_t backoff_ms;
    uint32_t now_ms;
    uint32_t retry_deadline_ms;  // 到达该时间点才允许重连
    bool retry_pending;     // 需要在 now_ms 之后重连

    // 最近一次错误(用于 UI 横幅)
    ft_error_code_t last_error;
    char last_error_text[96];

    // 服务端下发的房间配置
    ft_room_cfg_t cfg;
    char room_name[FT_MAX_ROOM_NAME_LEN];

    // 本地身份
    char self_id[40];
    char self_name[FT_MAX_NAME_LEN];
} ft_state_t;

// 初始化为干净状态。
void ft_state_init(ft_state_t *st);

// 处理服务端事件,更新状态。
// 返回 true 表示状态发生了需要 UI 关注的变化。
bool ft_state_on_event(ft_state_t *st, const ft_event_t *ev);

// 连接断开(网络层回调)。决定是重连还是放弃。
void ft_state_on_disconnect(ft_state_t *st, bool manual);

// 推进时间,检查是否到达重连时刻。
// 返回 true 表示"现在应该发起重连"。
bool ft_state_tick(ft_state_t *st, uint32_t now_ms);

// 用户按住确定键 —— 请求麦权。
// 返回需要发出的控制消息类型(供网络层发送)。
typedef enum {
    FT_ACTION_NONE = 0,
    FT_ACTION_SEND_SPEAK_ON,
    FT_ACTION_SEND_SPEAK_OFF,
    FT_ACTION_SEND_MUTE,
} ft_action_t;

ft_action_t ft_state_request_speak(ft_state_t *st);
ft_action_t ft_state_release_speak(ft_state_t *st);

// 用户切换本地静音。
ft_action_t ft_state_toggle_mute(ft_state_t *st);

// 查询:当前是否可以开始说话(在线且未静音)。
bool ft_state_can_speak(const ft_state_t *st);

// 查询:当前自己是否持有麦。
bool ft_state_has_floor(const ft_state_t *st);

// 在成员列表里找某人。找不到返回 NULL。
const ft_member_t *ft_state_find_member(const ft_state_t *st, const char *id);

// 在线成员数(不含自己)。
size_t ft_state_peer_count(const ft_state_t *st);

// 页面到 UI 文案(简体中文)。
const char *ft_page_title(ft_page_t page);

#ifdef __cplusplus
}
#endif
