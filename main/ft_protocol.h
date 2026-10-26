// FoloTalker 房间协议 —— 纯逻辑,不含 ESP-IDF / LVGL 依赖,可在主机上单测。
//
// 协议与 HomeTalker 服务端 (server/src/index.ts, server/src/room.ts) 对齐:
//   控制消息:JSON 文本帧,形如 {"t":"join","name":"...","code":"..."}
//   音频    :二进制帧,负载是 16kHz / 单声道 / Int16LE 的 20ms 定长帧
//
// 本文件只做"生成待发送的字节流"和"把服务端事件归约成状态",不碰网络与 UI。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FT_MAX_NAME_LEN      21   // 含结尾 '\0';服务端会截断到 20 字符
#define FT_MAX_CODE_LEN      65   // 含结尾 '\0'
#define FT_MAX_HOST_LEN      129  // 含结尾 '\0';服务器主机名或 IP
#define FT_MAX_ROOM_NAME_LEN 33   // 含结尾 '\0'

// 服务端 welcome 事件里的音频参数。本客户端只接受这一组,避免与服务端协商错位。
#define FT_SAMPLE_RATE   16000
#define FT_FRAME_MS      20
#define FT_FRAME_SAMPLES (FT_SAMPLE_RATE * FT_FRAME_MS / 1000)  // 320
#define FT_FRAME_BYTES   (FT_FRAME_SAMPLES * 2)                // 640 = Int16LE 单声道

// 成员列表上限。与服务端 MAX_MEMBERS(默认 12)对齐,留一点余量。
#define FT_MAX_MEMBERS 16

// 服务端事件类型
typedef enum {
    FT_EV_NONE = 0,
    FT_EV_WELCOME,
    FT_EV_MEMBERS,
    FT_EV_SPEAKER,
    FT_EV_ERROR,
} ft_event_kind_t;

// 服务端错误码。决定 UI 上的提示文案与是否需要重连。
typedef enum {
    FT_ERR_NONE = 0,
    FT_ERR_NAME_REQUIRED,
    FT_ERR_BAD_CODE,
    FT_ERR_LOCKED,
    FT_ERR_ROOM_FULL,
    FT_ERR_JOIN_TIMEOUT,
    FT_ERR_JOIN_REQUIRED,
    FT_ERR_FLOOR_BUSY,
    FT_ERR_SPEAK_TIMEOUT,
    FT_ERR_PONG,
    FT_ERR_OTHER,
} ft_error_code_t;

typedef struct {
    char id[40];
    char name[FT_MAX_NAME_LEN];
    bool muted;
    bool speaking;
} ft_member_t;

// 服务器下发的房间配置
typedef struct {
    uint32_t sample_rate;
    uint32_t frame_ms;
    uint32_t max_speak_ms;
    bool half_duplex;
    bool exclusive;      // true = exclusive 模式(麦被占时拒绝),false = preempt
} ft_room_cfg_t;

// 一次服务端事件
typedef struct {
    ft_event_kind_t kind;
    // WELCOME
    char self_id[40];
    char self_name[FT_MAX_NAME_LEN];
    char room_name[FT_MAX_ROOM_NAME_LEN];
    ft_room_cfg_t cfg;
    // MEMBERS
    ft_member_t members[FT_MAX_MEMBERS];
    size_t member_count;
    // SPEAKER
    char speaker_id[40];
    char speaker_name[FT_MAX_NAME_LEN];
    bool has_speaker;
    // ERROR
    ft_error_code_t error_code;
    char error_message[96];
} ft_event_t;

// 连接状态机
typedef enum {
    FT_CONN_IDLE = 0,
    FT_CONN_CONNECTING,
    FT_CONN_JOINING,     // 已连上 WebSocket,等待 welcome
    FT_CONN_ONLINE,      // 已加入房间
    FT_CONN_RECONNECTING,
    FT_CONN_FAILED,      // 被服务端拒绝(口令错误等),不再自动重连
} ft_conn_state_t;

// ---------------------------------------------------------------------------
// JSON 生成:把待发送消息编码成待发出的字节。
// 返回写入长度;buf 不足时返回 0。
// ---------------------------------------------------------------------------

// 生成 join 消息。name/code 为 UTF-8 字符串。
size_t ft_build_join(char *buf, size_t buf_len, const char *name, const char *code);

// 生成 {"t":"speak","on":true|false}
size_t ft_build_speak(char *buf, size_t buf_len, bool on);

// 生成 {"t":"mute","on":true|false}
size_t ft_build_mute(char *buf, size_t buf_len, bool on);

// 生成 {"t":"ping"}
size_t ft_build_ping(char *buf, size_t buf_len);

// ---------------------------------------------------------------------------
// JSON 解析:把服务端文本帧归约成 ft_event_t。
// 这是本文件里最容易出错的部分,故与生成侧分开实现,并由主机单测覆盖。
//
// 返回 true 表示成功解析出事件;false 表示这段 JSON 无法识别(应忽略而不是断连)。
// ---------------------------------------------------------------------------
bool ft_parse_event(const char *json, size_t json_len, ft_event_t *out);

// 把错误码映射到可用于 UI 的提示文案(简体中文)。
const char *ft_error_text(ft_error_code_t code);

// 连接状态到 UI 文案(简体中文)。
const char *ft_conn_state_text(ft_conn_state_t state);

#ifdef __cplusplus
}
#endif
