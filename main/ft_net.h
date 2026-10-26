// FoloTalker 网络层 —— WebSocket 客户端。
//
// 协议与 HomeTalker 服务端对齐:
//   - 控制消息走 JSON 文本帧
//   - 音频走二进制帧(16kHz / 单声道 / Int16LE / 20ms)
//
// 线程模型:
//   网络回调(esp_websocket_client 的任务)只做"把数据放进队列",
//   解析与状态更新在应用任务里做。这样回调永远不阻塞。
#pragma once

#include "ft_protocol.h"
#include "ft_state.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 网络层事件类型
typedef enum {
    FT_NET_EV_NONE = 0,
    FT_NET_EV_CONNECTED,     // WebSocket 已连上
    FT_NET_EV_DISCONNECTED,  // 断开了
    FT_NET_EV_TEXT,          // 收到文本帧(完整 JSON)
    FT_NET_EV_BINARY,        // 收到二进制帧(音频)
    FT_NET_EV_ERROR,         // 连接/传输层错误
} ft_net_event_kind_t;

// 网络层事件(投递给应用层)
typedef struct {
    ft_net_event_kind_t kind;
    // TEXT 事件:文本内容(不含结尾 '\0')
    char text[1024];
    size_t text_len;
    // BINARY 事件:音频负载指针(在回调返回后即失效,需立刻拷走)
    const uint8_t *data;
    size_t data_len;
    // ERROR 事件
    int error_code;
} ft_net_event_t;

// 初始化网络层。返回 false 表示组件不可用。
bool ft_net_init(void);

// 反初始化,释放所有资源。
void ft_net_deinit(void);

// 设置服务端 URL(wss://host[:port]/ws)。
bool ft_net_set_url(const char *url);

// 连接。url 为 NULL 时用已设置的 URL。
bool ft_net_connect(void);

// 主动断开。不触发自动重连。
void ft_net_disconnect(void);

// 是否已连接。
bool ft_net_is_connected(void);

// 发送文本(控制消息)。返回 false 表示未连接。
bool ft_net_send_text(const char *text, size_t len);

// 发送二进制(音频帧)。
bool ft_net_send_binary(const uint8_t *data, size_t len);

// 取一个网络事件。超时 timeout_ms 后返回 false。
// 返回的事件里 BINARY 的 data 指针在下一次调用前有效。
bool ft_net_next_event(ft_net_event_t *out, uint32_t timeout_ms);

// 从事件队列中丢弃所有待处理事件(重新连接前用)。
void ft_net_flush_events(void);

// TLS 证书校验开关。
// HomeTalker 用的是自签 CA(局域网自建),手机端由用户手动安装根证书。
// 设备端默认【开启】校验;用户可以在设置里改成"不校验"以支持自签证书。
// 生产环境应保持开启。
void ft_net_set_insecure(bool insecure);

#ifdef __cplusplus
}
#endif
