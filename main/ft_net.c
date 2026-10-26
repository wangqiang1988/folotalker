// FoloTalker 网络层实现 —— 基于 esp_websocket_client。
//
// 关键约束:
//   - 组件的事件回调运行在组件自己的任务里,不能阻塞。
//     所以回调只做一件事:把数据拷进队列。
//   - 音频帧的二进制负载必须立刻拷走(组件的缓冲区会被复用)。
#include "ft_net.h"

#include <stdlib.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

static const char *TAG = "ft_net";

// 队列深度。文本帧与二进制帧分开两个队列:
//   - 文本帧是控制消息,不能被音频挤掉
//   - 二进制帧是音频,允许在极拥塞时丢弃(宁可丢音频也不能拖慢控制)
#define FT_NET_TEXT_QUEUE_DEPTH 8
#define FT_NET_BIN_QUEUE_DEPTH 24

#define FT_NET_MAX_TEXT 1024
#define FT_NET_MAX_BIN  2048

typedef struct {
    size_t len;
    uint8_t data[FT_NET_MAX_TEXT];
} ft_net_text_t;

typedef struct {
    size_t len;
    uint8_t data[FT_NET_MAX_BIN];
} ft_net_bin_t;

static esp_websocket_client_handle_t s_ws;
static QueueHandle_t s_text_queue;
static QueueHandle_t s_bin_queue;
static QueueHandle_t s_event_queue;
static char s_url[FT_MAX_URL_LEN];
static bool s_insecure;
static bool s_connected;

// 事件队列(连接/断开/错误)
typedef struct {
    ft_net_event_kind_t kind;
    int error_code;
} ft_net_simple_t;

static void ft_net_event(ft_net_event_kind_t kind, int error_code)
{
    ft_net_simple_t ev;
    ev.kind = kind;
    ev.error_code = error_code;
    if (s_event_queue != NULL) {
        xQueueSend(s_event_queue, &ev, 0);
    }
}

// ---------------------------------------------------------------------------
// 组件事件回调 —— 只入队,不阻塞
// ---------------------------------------------------------------------------

static void ft_net_ws_event_handler(void *arg, esp_event_base_t base,
                                    int32_t id, void *data)
{
    (void)arg;
    (void)base;
    esp_websocket_event_data_t *ev = (esp_websocket_event_data_t *)data;

    switch (id) {
    case WEBSOCKET_EVENT_CONNECTED:
        s_connected = true;
        ft_net_event(FT_NET_EV_CONNECTED, 0);
        break;

    case WEBSOCKET_EVENT_DISCONNECTED:
    case WEBSOCKET_EVENT_CLOSED:
        s_connected = false;
        ft_net_event(FT_NET_EV_DISCONNECTED, ev ? ev->close_status_code : 0);
        break;

    case WEBSOCKET_EVENT_ERROR:
        ft_net_event(FT_NET_EV_ERROR,
                     ev ? (int)ev->error_handle.error_type : 0);
        break;

    case WEBSOCKET_EVENT_DATA: {
        if (ev == NULL || ev->data_ptr == NULL || ev->data_len <= 0) {
            break;
        }
        // 只处理完整的一帧。fin=false 说明还有后续分片,
        // 对本应用(服务端不会分片)可以忽略。
        if (!ev->fin) {
            break;
        }
        if (ev->op_code == 0x08) {   // close 帧
            break;
        }
        if (ev->op_code == 0x09) {   // ping 帧,组件会自动回 pong
            break;
        }
        if (ev->op_code == 0x0A) {   // pong 帧
            break;
        }

        if (ev->op_code == 0x01 || ev->op_code == 0x00) {
            // 文本帧
            if (ev->data_len > FT_NET_MAX_TEXT) {
                ESP_LOGW(TAG, "文本帧过长(%d),已丢弃", ev->data_len);
                break;
            }
            ft_net_text_t item;
            item.len = (size_t)ev->data_len;
            memcpy(item.data, ev->data_ptr, item.len);
            if (s_text_queue != NULL) {
                // 队列满时丢弃最旧的一条,保证最新的控制消息能进队。
                if (uxQueueMessagesWaiting(s_text_queue) > 0) {
                    ft_net_text_t drop;
                    xQueueReceive(s_text_queue, &drop, 0);
                }
                xQueueSend(s_text_queue, &item, 0);
            }
        } else if (ev->op_code == 0x02) {
            // 二进制帧(音频)
            if (ev->data_len > FT_NET_MAX_BIN) {
                ESP_LOGW(TAG, "二进制帧过长(%d),已丢弃", ev->data_len);
                break;
            }
            ft_net_bin_t item;
            item.len = (size_t)ev->data_len;
            memcpy(item.data, ev->data_ptr, item.len);
            if (s_bin_queue != NULL) {
                // 音频允许丢:队列满时直接丢新的这一帧。
                xQueueSend(s_bin_queue, &item, 0);
            }
        }
        break;
    }

    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// 对外接口
// ---------------------------------------------------------------------------

bool ft_net_init(void)
{
    if (s_ws != NULL) {
        return true;
    }
    s_text_queue = xQueueCreate(FT_NET_TEXT_QUEUE_DEPTH, sizeof(ft_net_text_t));
    s_bin_queue = xQueueCreate(FT_NET_BIN_QUEUE_DEPTH, sizeof(ft_net_bin_t));
    s_event_queue = xQueueCreate(8, sizeof(ft_net_simple_t));
    if (s_text_queue == NULL || s_bin_queue == NULL || s_event_queue == NULL) {
        ESP_LOGE(TAG, "创建队列失败");
        ft_net_deinit();
        return false;
    }

    esp_websocket_client_config_t cfg = {
        .uri = s_url[0] ? s_url : NULL,
        .task_name = "ft_ws",
        .task_stack = 4096,
        .task_prio = 5,
        .buffer_size = 2048,
        .disable_auto_reconnect = true,   // 重连策略由应用层统一管理(指数退避)
        .enable_close_reconnect = false,
        .network_timeout_ms = 10000,
        .reconnect_timeout_ms = 10000,
        .ping_interval_sec = 20,
        .pingpong_timeout_sec = 30,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .transport = WEBSOCKET_TRANSPORT_OVER_SSL,
    };

    s_ws = esp_websocket_client_init(&cfg);
    if (s_ws == NULL) {
        ESP_LOGE(TAG, "创建 WebSocket 客户端失败");
        ft_net_deinit();
        return false;
    }
    esp_websocket_register_events(s_ws, WEBSOCKET_EVENT_ANY, ft_net_ws_event_handler, NULL);
    return true;
}

void ft_net_deinit(void)
{
    if (s_ws != NULL) {
        esp_websocket_client_destroy(s_ws);
        s_ws = NULL;
    }
    if (s_text_queue != NULL) {
        vQueueDelete(s_text_queue);
        s_text_queue = NULL;
    }
    if (s_bin_queue != NULL) {
        vQueueDelete(s_bin_queue);
        s_bin_queue = NULL;
    }
    if (s_event_queue != NULL) {
        vQueueDelete(s_event_queue);
        s_event_queue = NULL;
    }
    s_connected = false;
}

bool ft_net_set_url(const char *url)
{
    if (url == NULL) {
        return false;
    }
    size_t n = strlen(url);
    if (n == 0 || n >= sizeof(s_url)) {
        return false;
    }
    memcpy(s_url, url, n + 1);
    if (s_ws != NULL) {
        esp_websocket_client_set_uri(s_ws, s_url);
    }
    return true;
}

bool ft_net_connect(void)
{
    if (s_ws == NULL) {
        if (!ft_net_init()) {
            return false;
        }
    }
    if (s_url[0] == '\0') {
        ESP_LOGE(TAG, "尚未设置 URL");
        return false;
    }
    esp_err_t err = esp_websocket_client_set_uri(s_ws, s_url);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "设置 URI 失败: %s", esp_err_to_name(err));
        return false;
    }
    err = esp_websocket_client_start(s_ws);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "启动连接失败: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

void ft_net_disconnect(void)
{
    if (s_ws != NULL) {
        esp_websocket_client_stop(s_ws);
    }
    s_connected = false;
}

bool ft_net_is_connected(void)
{
    return s_connected && s_ws != NULL;
}

bool ft_net_send_text(const char *text, size_t len)
{
    if (s_ws == NULL || !s_connected || text == NULL || len == 0) {
        return false;
    }
    // 超时给 1 秒:控制消息必须发出去,否则服务端会因 join 超时断开。
    return esp_websocket_client_send_text(s_ws, text, (int)len, pdMS_TO_TICKS(1000)) >= 0;
}

bool ft_net_send_binary(const uint8_t *data, size_t len)
{
    if (s_ws == NULL || !s_connected || data == NULL || len == 0) {
        return false;
    }
    // 音频帧允许等一小会儿,但不能无限等 —— 宁可丢一帧也不能卡住采集任务。
    return esp_websocket_client_send_bin(s_ws, (const char *)data, (int)len,
                                         pdMS_TO_TICKS(200)) >= 0;
}

bool ft_net_next_event(ft_net_event_t *out, uint32_t timeout_ms)
{
    if (out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));

    // 优先处理连接类事件,它们决定状态机的走向。
    if (s_event_queue != NULL) {
        ft_net_simple_t simple;
        if (xQueueReceive(s_event_queue, &simple,
                           (timeout_ms > 0) ? pdMS_TO_TICKS(timeout_ms) : 0) == pdTRUE) {
            out->kind = simple.kind;
            out->error_code = simple.error_code;
            return true;
        }
        if (timeout_ms == 0) {
            return false;
        }
    }

    // 文本帧
    if (s_text_queue != NULL) {
        ft_net_text_t item;
        if (xQueueReceive(s_text_queue, &item, 0) == pdTRUE) {
            out->kind = FT_NET_EV_TEXT;
            out->text_len = item.len < sizeof(out->text) ? item.len
                                                        : sizeof(out->text) - 1;
            memcpy(out->text, item.data, out->text_len);
            out->text[out->text_len] = '\0';
            return true;
        }
    }

    // 二进制帧
    if (s_bin_queue != NULL) {
        ft_net_bin_t item;
        if (xQueueReceive(s_bin_queue, &item, 0) == pdTRUE) {
            out->kind = FT_NET_EV_BINARY;
            out->data = item.data;
            out->data_len = item.len;
            return true;
        }
    }

    return false;
}

void ft_net_flush_events(void)
{
    if (s_text_queue != NULL) {
        xQueueReset(s_text_queue);
    }
    if (s_bin_queue != NULL) {
        xQueueReset(s_bin_queue);
    }
    if (s_event_queue != NULL) {
        xQueueReset(s_event_queue);
    }
}

void ft_net_set_insecure(bool insecure)
{
    s_insecure = insecure;
}
