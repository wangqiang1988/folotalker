// main/main.c —— FoloTalker 固件入口。
//
// 本文件是【FoloTalker 对讲应用】的入口,不是基线的 BSP 驱动示例。
// 基线 main 分支上的 demo 菜单与 demo_*.c 测试页在这里被完全取代 ——
// 二次开发必须重新设计应用界面,不得沿用硬件测试外壳。
//
// 启动流程:
//   1. bsp_display_init()  —— SPI 面板 + 背光
//   2. bsp_lvgl_init()     —— LVGL 与渲染任务
//   3. ft_ui_init()        —— 本应用的界面
//   4. ft_app_start()      —— 应用主控(网络/音频/存储/按键)
//
// 按键语义由 FoloTalker 自己定义(见 main/ft_app.c):
//   房间页  确定键按住 = 说话;确定短按 = 设置;确定长按 = 静音
//   输入页  上/下 = 选字符;确定 = 输入;确定长按 = 退格;上键长按 = 换字符集
#include <stdio.h>

#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_pins.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "nvs_flash.h"

#include "ft_app.h"
#include "ft_ui.h"
#include "key_guard.h"

static const char *TAG = "main";

// 深睡按键唤醒后的守卫(见 main/key_guard.h)。
// 按键唤醒会让芯片在"唤醒键还按着"的状态下重启,按键组件晚几百毫秒才启动,
// 于是几乎立刻触发一个意料外的长按动作。守卫在唤醒键松手前吞掉所有按键动作。
#define KEY_GUARD_MAX_US (10LL * 1000 * 1000)
static key_guard_t s_key_guard;

// 输入分发是否已就绪。
static volatile bool s_input_ready;

// 按键回调 —— 运行在 button 组件的共享 esp_timer 任务里,只能入队。
static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user) {
    (void)user;
    // Consume the guard before the readiness check: a release can arrive after the
    // post-init voltage sample but before input dispatch is ready, and dropping it
    // here would leave the guard armed to swallow the next real press.
    const bool was_armed = s_key_guard.armed;
    if (key_guard_consume(&s_key_guard, ev == BSP_BTN_RELEASE, esp_timer_get_time())) {
        // The release and the deadline both arrive on this task; log the disarm
        // transition once it happens.
        if (was_armed && !s_key_guard.armed) {
            ESP_LOGI(TAG, "唤醒键已松手,按键恢复响应");
        }
        return;
    }
    if (!s_input_ready) return;
    ft_app_dispatch_key(btn, ev);
}

// 应用主控在另一个任务里跑,这里等它就绪后就可以返回 app_main。
// ESP-IDF 要求 app_main 返回后事件循环继续运行,所以这里不阻塞。
static void ft_app_main_task(void *arg)
{
    (void)arg;
    if (!ft_app_start()) {
        ESP_LOGE(TAG, "FoloTalker 应用启动失败");
        // 启动失败也要让用户看得见,不能黑屏。
        if (bsp_lvgl_lock(200)) {
            lv_obj_t *label = lv_label_create(lv_screen_active());
            lv_obj_set_style_text_color(label, lv_color_hex(0xEF5350), LV_PART_MAIN);
            lv_label_set_text(label, "FoloTalker 启动失败");
            lv_obj_center(label);
            bsp_lvgl_unlock();
        }
        vTaskDelete(NULL);
        return;
    }
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "FoloToy AI Passport — FoloTalker");

    // NVS 需要在其它模块之前初始化(加密分区要在这里生成密钥)。
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS 初始化失败: %s", esp_err_to_name(err));
    }

    // 按键唤醒时,那颗键在整个启动期间还按着。先挡住它的动作(见 on_key)。
    const uint32_t wake_causes = esp_sleep_get_wakeup_causes();
    if (wake_causes & (1u << ESP_SLEEP_WAKEUP_GPIO)) {
        key_guard_arm(&s_key_guard, esp_timer_get_time(), KEY_GUARD_MAX_US);
        ESP_LOGI(TAG, "按键唤醒:先屏蔽按键动作,直到那颗键松手");
    }

    // 按键
    if (bsp_button_init(on_key, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "按键初始化失败");
    } else if (s_key_guard.armed) {
        // 若唤醒键在按键驱动启动前就已松手,组件不会产生 release 事件,守卫会一直
        // 拦到兜底。初始化完成后按真实电压同步一次:已松手立即撤防。
        const bool released = bsp_button_read_mv() >= BSP_BTN_MV_RELEASED_MIN;
        key_guard_sync_released(&s_key_guard, released);
    }

    // 显示 + LVGL
    ESP_ERROR_CHECK(bsp_display_init());
    bsp_display_backlight(90);

    if (bsp_lvgl_init() == NULL) {
        ESP_LOGE(TAG, "LVGL 初始化失败");
        return;
    }

    // 本应用的界面
    ft_ui_init();

    // 应用主控放到独立任务里:它会阻塞等待网络/按键事件。
    xTaskCreate(ft_app_main_task, "ft_boot", 4096, NULL, 5, NULL);

    // 应用层的事件队列建好之后,按键才允许分发。
    s_input_ready = true;
}
