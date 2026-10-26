// FoloTalker 应用主控实现。
#include "ft_app.h"

#include <stdio.h>
#include <string.h>

#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "ft_audio.h"
#include "ft_net.h"
#include "ft_prov.h"
#include "ft_state.h"
#include "ft_store.h"
#include "ft_texts.h"
#include "ft_ui.h"

static const char *TAG = "ft_app";

// 应用任务周期
#define FT_APP_TICK_MS 100
#define FT_BATTERY_SAMPLE_MS 10000ULL

// 应用事件队列
#define FT_APP_QUEUE_DEPTH 16

typedef enum {
    FT_APP_EV_NONE = 0,
    FT_APP_EV_KEY,
    FT_APP_EV_NET,
    FT_APP_EV_PROV,
} ft_app_ev_kind_t;

typedef struct {
    ft_app_ev_kind_t kind;
    int key;             // 0=上 1=下 2=确定
    bsp_btn_ev_t btn_ev; // 原始按键事件
    bsp_btn_t btn;
    ft_net_event_t net;
    ft_prov_state_t prov_state;
} ft_app_ev_t;

static ft_state_t s_state;
static ft_store_t *s_store;
static QueueHandle_t s_app_queue;
static TaskHandle_t s_app_task;
static volatile bool s_running;

// 按键事件投递。
// 注意:房间页的"按住说话"依赖 PRESS/RELEASE 的即时性,所以这里不做过滤,
// 全部事件都投递,由应用任务决定如何使用。
static void ft_app_post_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (s_app_queue == NULL) {
        return;
    }
    ft_app_ev_t item = { 0 };
    item.kind = FT_APP_EV_KEY;
    item.btn = btn;
    item.btn_ev = ev;
    switch (btn) {
    case BSP_BTN_UP:   item.key = 0; break;
    case BSP_BTN_DOWN: item.key = 1; break;
    case BSP_BTN_OK:   item.key = 2; break;
    default: return;
    }
    xQueueSend(s_app_queue, &item, 0);
}

// 把一个按键事件投递给应用任务。
void ft_app_dispatch_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    ft_app_post_key(btn, ev);
}

// ---------------------------------------------------------------------------
// 网络事件
// ---------------------------------------------------------------------------

static void ft_app_send_join(void)
{
    char host[FT_MAX_HOST_LEN] = { 0 };
    char nick[FT_MAX_NICK_LEN] = { 0 };
    char code[FT_MAX_CODE_LEN] = { 0 };

    ft_store_get_str(s_store, FT_STORE_KEY_HOST, host, sizeof(host));
    ft_store_get_str(s_store, FT_STORE_KEY_NAME, nick, sizeof(nick));
    ft_store_get_str(s_store, FT_STORE_KEY_CODE, code, sizeof(code));

    if (host[0] == '\0' || nick[0] == '\0' || code[0] == '\0') {
        return;
    }

    char url[FT_MAX_URL_LEN];
    if (ft_build_ws_url(url, sizeof(url), host, 0, "/ws") == 0) {
        return;
    }
    ft_net_set_url(url);

    // 发送 join。服务端要求第一条消息必须是 join,且 5 秒内完成。
    char msg[512];
    size_t n = ft_build_join(msg, sizeof(msg), nick, code);
    if (n == 0) {
        return;
    }
    ft_net_send_text(msg, n);
}

// 发送 speak 开关
static void ft_app_send_speak(bool on)
{
    char msg[64];
    size_t n = ft_build_speak(msg, sizeof(msg), on);
    if (n > 0) {
        ft_net_send_text(msg, n);
    }
}

// 发送 mute
static void ft_app_send_mute(bool on)
{
    char msg[64];
    size_t n = ft_build_mute(msg, sizeof(msg), on);
    if (n > 0) {
        ft_net_send_text(msg, n);
    }
}

// 音频发送回调(运行在采集任务里)
static void ft_app_send_audio(const uint8_t *data, size_t len)
{
    // 只在持有麦权时发送。服务端本身也会过滤,但客户端少发包省电。
    if (!ft_state_has_floor(&s_state)) {
        return;
    }
    ft_net_send_binary(data, len);
}

// ---------------------------------------------------------------------------
// 应用状态迁移
// ---------------------------------------------------------------------------

static void ft_app_goto_page(ft_page_t page)
{
    s_state.page = page;
    ft_ui_set_current_page(page);
}

// Wi-Fi 是否已连上。读失败时按"未连接"处理 —— 保守比乐观安全。
static bool ft_app_wifi_connected(void)
{
    wifi_ap_record_t ap;
    return esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
}

// 根据当前配置决定进入哪个页面
static void ft_app_decide_start_page(void)
{
    char host[FT_MAX_HOST_LEN] = { 0 };
    char nick[FT_MAX_NICK_LEN] = { 0 };
    char code[FT_MAX_CODE_LEN] = { 0 };
    ft_store_get_str(s_store, FT_STORE_KEY_HOST, host, sizeof(host));
    ft_store_get_str(s_store, FT_STORE_KEY_NAME, nick, sizeof(nick));
    ft_store_get_str(s_store, FT_STORE_KEY_CODE, code, sizeof(code));

    if (host[0] == '\0') {
        // 没有服务器地址:先进输入页
        ft_app_goto_page(FT_PAGE_HOST);
        ft_ui_set_field_focus(FT_FIELD_HOST);
        ft_ui_field_clear();
        return;
    }
    if (nick[0] == '\0' || code[0] == '\0') {
        // 缺昵称或口令:进门禁页
        ft_app_goto_page(FT_PAGE_GATE);
        ft_ui_set_field_focus(FT_FIELD_NICK);
        ft_ui_field_clear();
        return;
    }
    // 配置齐全:直接连服务器
    ft_app_goto_page(FT_PAGE_ROOM);
    if (!ft_app_wifi_connected()) {
        // 没连 Wi-Fi 就连服务器只会一直失败,先把原因说清楚。
        ft_ui_show_error(FT_TXT_WIFI_NEEDED);
        return;
    }
    ft_ui_show_error(NULL);
    ft_net_connect();
}

// ---------------------------------------------------------------------------
// 处理 UI 动作
// ---------------------------------------------------------------------------

static void ft_app_handle_ui_action(ft_ui_action_t action)
{
    switch (action) {
    case FT_UI_ACT_ENTER_SETTINGS:
        ft_app_goto_page(FT_PAGE_SETTINGS);
        break;

    case FT_UI_ACT_LEAVE_SETTINGS:
        ft_app_goto_page(FT_PAGE_ROOM);
        break;

    case FT_UI_ACT_SAVE_HOST: {
        const char *host = ft_ui_host_addr();
        if (!ft_host_is_valid(host)) {
            ft_ui_show_error(FT_TXT_HOST_INVALID);
            return;
        }
        ft_store_set_str(s_store, FT_STORE_KEY_HOST, host);
        ft_ui_show_error(NULL);
        ft_app_decide_start_page();
        break;
    }

    case FT_UI_ACT_SAVE_CREDENTIAL: {
        const char *nick = ft_ui_gate_nick();
        const char *code = ft_ui_gate_code();
        if (!ft_nick_is_valid(nick)) {
            ft_ui_show_error(FT_TXT_NICK_TOO_LONG);
            return;
        }
        if (!ft_code_is_valid(code)) {
            ft_ui_show_error(FT_TXT_CODE_TOO_SHORT);
            return;
        }
        ft_store_set_str(s_store, FT_STORE_KEY_NAME, nick);
        ft_store_set_str(s_store, FT_STORE_KEY_CODE, code);
        ft_ui_show_error(NULL);
        ft_app_decide_start_page();
        break;
    }
    case FT_UI_ACT_CLEAR_CREDENTIAL:
        ft_store_erase(s_store, FT_STORE_KEY_NAME);
        ft_store_erase(s_store, FT_STORE_KEY_CODE);
        ft_app_goto_page(FT_PAGE_GATE);
        ft_ui_set_field_focus(FT_FIELD_NICK);
        ft_ui_field_clear();
        break;

    case FT_UI_ACT_TALK_START: {
        ft_action_t a = ft_state_request_speak(&s_state);
        if (a == FT_ACTION_SEND_SPEAK_ON) {
            ft_app_send_speak(true);
            ft_audio_start_capture();
        }
        break;
    }

    case FT_UI_ACT_TALK_STOP: {
        ft_action_t a = ft_state_release_speak(&s_state);
        if (a == FT_ACTION_SEND_SPEAK_OFF) {
            ft_app_send_speak(false);
        }
        ft_audio_stop_capture();
        break;
    }

    case FT_UI_ACT_SET_VOLUME: {
        int vol = ft_ui_get_volume();
        ft_store_set_int(s_store, FT_STORE_KEY_VOLUME, vol);
        ft_audio_set_volume(vol);
        break;
    }

    case FT_UI_ACT_PROVISION:
        ft_app_goto_page(FT_PAGE_PROVISION);
        if (!ft_prov_start()) {
            ft_ui_show_error(FT_TXT_PROV_FAILED);
        }
        break;

    case FT_UI_ACT_JOIN:
        ft_net_connect();
        break;

    case FT_UI_ACT_NONE:
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// 网络事件处理
// ---------------------------------------------------------------------------

static void ft_app_handle_net_event(const ft_net_event_t *ev)
{
    switch (ev->kind) {
    case FT_NET_EV_CONNECTED:
        s_state.conn = FT_CONN_JOINING;
        // WebSocket 刚连上,服务端要求 5 秒内发 join。
        ft_app_send_join();
        break;

    case FT_NET_EV_DISCONNECTED:
        ft_state_on_disconnect(&s_state, false);
        ft_audio_stop_capture();
        break;

    case FT_NET_EV_TEXT: {
        ft_event_t parsed;
        if (!ft_parse_event(ev->text, ev->text_len, &parsed)) {
            break;
        }
        ft_state_on_event(&s_state, &parsed);
        // welcome 之后启动播放
        if (parsed.kind == FT_EV_WELCOME) {
            ft_audio_start_playback();
        }
        break;
    }

    case FT_NET_EV_BINARY:
        // 收到的音频直接进播放缓冲
        ft_audio_push_playback(ev->data, ev->data_len);
        break;

    case FT_NET_EV_ERROR:
        ESP_LOGW(TAG, "网络错误: %d", ev->error_code);
        ft_state_on_disconnect(&s_state, false);
        break;

    case FT_NET_EV_NONE:
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// 配网回调 —— 运行在 BLE 栈上下文,只投递事件。
// ---------------------------------------------------------------------------

static void ft_app_prov_cb(ft_prov_state_t prov_state, const char *message)
{
    ft_app_ev_t item = { 0 };
    item.kind = FT_APP_EV_PROV;
    item.prov_state = prov_state;
    if (s_app_queue != NULL) {
        xQueueSend(s_app_queue, &item, 0);
    }
    (void)message;
}

// ---------------------------------------------------------------------------
// 应用任务
// ---------------------------------------------------------------------------

static void ft_app_process_key(const ft_app_ev_t *ev)
{
    // 房间页的"按住说话"由应用层直接处理(需要 PRESS/RELEASE 的即时性),
    // 其余页面交给 UI 处理。
    //
    // 按键组件的事件顺序(见 iot_button 的 iot_button_process):
    //   PRESS_DOWN  按下瞬间
    //   [LONG_PRESS_START]  按住超过 long_press_time(本板 500ms)
    //   PRESS_UP    松开瞬间
    //   [SINGLE_CLICK]      在 short_press_time(本板 180ms)内松开才会发
    //
    // 因此 CLICK 与 LONG 互斥:短按不会同时产生 LONG,长按也不会产生 CLICK。
    // 但 PRESS_UP(RELEASE)两种情况都会发,所以放麦逻辑放在 RELEASE 上,
    // "进入设置"放在 CLICK 上 —— 两者不会同时发生。
    //
    // 房间页的按键语义:
    //   确定键按住(PRESS..RELEASE) = 说话
    //   确定键短按(CLICK)          = 进入设置
    //   确定键长按(LONG)           = 切换麦克风静音
    //   下键短按(CLICK)            = 切换麦克风静音
    if (s_state.page == FT_PAGE_ROOM) {
        switch (ev->btn) {
        case BSP_BTN_OK:
            switch (ev->btn_ev) {
            case BSP_BTN_PRESS:
                // 按下:立刻要麦权并开始采集。低延迟比"长按才生效"更重要。
                if (ft_state_can_speak(&s_state)) {
                    if (ft_state_request_speak(&s_state) == FT_ACTION_SEND_SPEAK_ON) {
                        ft_app_send_speak(true);
                        ft_audio_start_capture();
                    }
                }
                return;

            case BSP_BTN_RELEASE:
                // 松开:放麦,并停止采集。
                if (ft_state_release_speak(&s_state) == FT_ACTION_SEND_SPEAK_OFF) {
                    ft_app_send_speak(false);
                }
                ft_audio_stop_capture();
                return;

            case BSP_BTN_LONG:
                // 按住超过 500ms:切换麦克风静音。
                ft_state_toggle_mute(&s_state);
                ft_app_send_mute(s_state.mic_muted);
                return;

            case BSP_BTN_CLICK:
                // 短按(180ms 内松开):进入设置页。
                ft_ui_set_setting_index(0);
                ft_app_goto_page(FT_PAGE_SETTINGS);
                return;

            default:
                return;
            }

        case BSP_BTN_DOWN:
            if (ev->btn_ev == BSP_BTN_CLICK) {
                // 下键短按:切换麦克风静音。
                ft_state_toggle_mute(&s_state);
                ft_app_send_mute(s_state.mic_muted);
            }
            return;

        default:
            return;
        }
    }

    // 交给 UI 处理(输入页、设置页)
    ft_ui_post_key(ev->key, ev->btn_ev == BSP_BTN_LONG);
}

// 应用任务主循环
static void ft_app_task(void *arg)
{
    (void)arg;
    uint32_t last_tick = 0;
    uint64_t last_battery = 0;

    while (s_running) {
        // 先把网络层的事件取出来。ft_net_next_event() 是非阻塞的,
        // 超时给 0 表示"有多少取多少"。
        ft_net_event_t net_ev;
        while (ft_net_next_event(&net_ev, 0)) {
            ft_app_handle_net_event(&net_ev);
            if (!s_running) {
                break;
            }
        }

        ft_app_ev_t ev;
        // 最多等 100ms,然后推进一次状态机
        if (xQueueReceive(s_app_queue, &ev, pdMS_TO_TICKS(FT_APP_TICK_MS)) == pdTRUE) {
            switch (ev.kind) {
            case FT_APP_EV_KEY:
                ft_app_process_key(&ev);
                break;
            case FT_APP_EV_NET:
                ft_app_handle_net_event(&ev.net);
                break;
            case FT_APP_EV_PROV:
                ft_ui_show_provision(ft_prov_state_text(ev.prov_state));
                if (ev.prov_state == FT_PROV_SUCCESS) {
                    // Wi-Fi 好了,重新决定下一步
                    ft_app_decide_start_page();
                } else if (ev.prov_state == FT_PROV_FAILED) {
                    ft_ui_show_error(FT_TXT_PROV_FAILED);
                }
                break;
            case FT_APP_EV_NONE:
            default:
                break;
            }
        }

        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);

        // 重连退避
        if (ft_state_tick(&s_state, now)) {
            if (!ft_app_wifi_connected()) {
                // Wi-Fi 还没连上,重连没有意义。等 Wi-Fi 事件到了再重试。
                // 这里把退避窗口重置,避免疯狂重试打满日志。
                ESP_LOGW(TAG, "Wi-Fi 未连接,暂缓重连");
                s_state.backoff_ms = FT_BACKOFF_MIN_MS;
                continue;
            }
            ESP_LOGI(TAG, "重连中");
            ft_net_flush_events();
            ft_net_connect();
        }

        // 电池采样
        uint64_t now_us = esp_timer_get_time();
        if (last_battery == 0 || (now_us - last_battery) >= FT_BATTERY_SAMPLE_MS * 1000ULL) {
            last_battery = now_us;
            ft_ui_post_battery();
        }

        // 状态变化时刷新界面
        if (now != last_tick) {
            last_tick = now;
            ft_ui_render(&s_state);
            // 处理 UI 发来的动作
            ft_ui_action_t action = ft_ui_take_action();
            if (action != FT_UI_ACT_NONE) {
                ft_app_handle_ui_action(action);
            }
            // 处理 UI 的页面跳转请求
            ft_page_t want;
            if (ft_ui_take_page_request(&want)) {
                ft_app_goto_page(want);
            }
        }
    }
    vTaskDelete(NULL);
    s_app_task = NULL;
}

// ---------------------------------------------------------------------------
// 对外接口
// ---------------------------------------------------------------------------

bool ft_app_start(void)
{
    ft_state_init(&s_state);

    // NVS。加密分区需要在第一次启动时生成密钥。
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS 初始化失败: %s", esp_err_to_name(err));
        return false;
    }

    s_store = ft_store_new_nvs();
    if (s_store == NULL) {
        ESP_LOGE(TAG, "打开设置存储失败");
        return false;
    }

    // 网络栈
    if (esp_netif_init() != ESP_OK) {
        ESP_LOGE(TAG, "netif 初始化失败");
        return false;
    }
    esp_event_loop_create_default();

    if (esp_wifi_init(&(wifi_init_config_t)WIFI_INIT_CONFIG_DEFAULT()) != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi 初始化失败");
        return false;
    }
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(WIFI_MODE_STA);

    // 尝试用已保存的凭证连接(如果有)
    esp_wifi_start();
    // 配网模块负责 Wi-Fi 凭证的持久化(加密 NVS),这里用它重连。
    ft_prov_reconnect_stored();

    // 音频
    if (!ft_audio_init()) {
        ESP_LOGE(TAG, "音频初始化失败");
        return false;
    }
    int volume = FT_DEFAULT_VOLUME;
    ft_store_get_int(s_store, FT_STORE_KEY_VOLUME, &volume);
    ft_ui_set_volume(volume);
    ft_audio_set_volume(volume);
    ft_audio_set_sender(ft_app_send_audio);

    // 网络
    if (!ft_net_init()) {
        ESP_LOGE(TAG, "网络层初始化失败");
        return false;
    }

    // 配网
    ft_prov_init(ft_app_prov_cb);

    // 按键由 main.c 的 on_key() 回调接入;深睡唤醒的按键守卫也在 main.c 里
    // (见 key_guard.h 与 main.c 的 on_key)。

    // 事件队列与任务
    s_app_queue = xQueueCreate(FT_APP_QUEUE_DEPTH, sizeof(ft_app_ev_t));
    if (s_app_queue == NULL) {
        ESP_LOGE(TAG, "创建事件队列失败");
        return false;
    }
    s_running = true;
    if (xTaskCreate(ft_app_task, "ft_app", 6144, NULL, 5, &s_app_task) != pdPASS) {
        s_running = false;
        ESP_LOGE(TAG, "创建应用任务失败");
        return false;
    }

    // 决定起始页面
    ft_app_decide_start_page();

    ESP_LOGI(TAG, "FoloTalker 启动完成");
    return true;
}

void ft_app_stop(void)
{
    s_running = false;
    if (s_app_task != NULL) {
        vTaskDelay(pdMS_TO_TICKS(200));
        vTaskDelete(s_app_task);
        s_app_task = NULL;
    }
    if (s_app_queue != NULL) {
        vQueueDelete(s_app_queue);
        s_app_queue = NULL;
    }
    ft_net_deinit();
    ft_audio_deinit();
    ft_prov_deinit();
    if (s_store != NULL) {
        ft_store_free_nvs(s_store);
        s_store = NULL;
    }
}
