// FoloTalker 音频管线实现。
#include "ft_audio.h"

#include <string.h>

#include "bsp_audio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "ft_audio";

// 采集:codec 每读 320 个样本(16kHz 下就是 20ms)就发一帧。
#define FT_CAPTURE_CHUNK_SAMPLES 320
#define FT_CAPTURE_CHUNK_BYTES   (FT_CAPTURE_CHUNK_SAMPLES * 2)

// 播放缓冲:约 200ms。太小会欠载(听出断句),太大会有明显延迟。
#define FT_PLAYBACK_CAPACITY (FT_FRAME_BYTES * 10)

static volatile bool s_capturing;
static volatile bool s_playback_running;
static TaskHandle_t s_capture_task;
static TaskHandle_t s_playback_task;
static ft_audio_send_fn s_sender;

// 播放环形缓冲
static uint8_t s_play_buf[FT_PLAYBACK_CAPACITY];
static size_t s_play_read;
static size_t s_play_write;
static size_t s_play_fill;
static SemaphoreHandle_t s_play_lock;

// 采集用的 PCM 缓冲。放在静态区,避免在任务栈上占空间。
static int16_t s_capture_buf[FT_CAPTURE_CHUNK_SAMPLES];

static void ft_capture_task(void *arg)
{
    (void)arg;
    uint8_t frame[FT_FRAME_BYTES];

    while (s_capturing) {
        memset(frame, 0, sizeof(frame));
        esp_err_t err = bsp_audio_read(s_capture_buf, sizeof(s_capture_buf));
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "采集失败: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        // Int16LE 直接就是协议要求的格式,拷进发送缓冲即可。
        memcpy(frame, s_capture_buf, sizeof(s_capture_buf));
        if (s_sender != NULL) {
            s_sender(frame, sizeof(frame));
        }
    }
    vTaskDelete(NULL);
    s_capture_task = NULL;
}

static void ft_playback_task(void *arg)
{
    (void)arg;
    uint8_t frame[FT_FRAME_BYTES];
    size_t got = 0;

    while (s_playback_running) {
        // 凑满一帧再播。收到的音频不一定是 20ms 对齐的,
        // 所以这里用一个累积缓冲,凑够 640 字节才交给 codec。
        if (s_play_lock != NULL && xSemaphoreTake(s_play_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
            while (got < sizeof(frame) && s_play_fill > 0) {
                size_t chunk = s_play_fill;
                if (chunk > sizeof(frame) - got) {
                    chunk = sizeof(frame) - got;
                }
                for (size_t i = 0; i < chunk; i++) {
                    frame[got + i] = s_play_buf[(s_play_read + i) % FT_PLAYBACK_CAPACITY];
                }
                s_play_read = (s_play_read + chunk) % FT_PLAYBACK_CAPACITY;
                s_play_fill -= chunk;
                got += chunk;
            }
            xSemaphoreGive(s_play_lock);
        }

        if (got == sizeof(frame)) {
            bsp_audio_write(frame, sizeof(frame));
            got = 0;
        }
    }
    vTaskDelete(NULL);
    s_playback_task = NULL;
}

bool ft_audio_init(void)
{
    esp_err_t err = bsp_audio_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "音频初始化失败: %s", esp_err_to_name(err));
        return false;
    }
    // 协议要求 16kHz / 单声道 / 16bit。ES8311 直接支持 16kHz,
    // 不需要软件重采样。
    err = bsp_audio_set_format(FT_SAMPLE_RATE, 16, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "设置采样格式失败: %s", esp_err_to_name(err));
        return false;
    }
    if (s_play_lock == NULL) {
        s_play_lock = xSemaphoreCreateMutex();
    }
    return s_play_lock != NULL;
}

void ft_audio_deinit(void)
{
    ft_audio_stop_capture();
    ft_audio_stop_playback();
    if (s_play_lock != NULL) {
        vSemaphoreDelete(s_play_lock);
        s_play_lock = NULL;
    }
}

void ft_audio_set_volume(int percent)
{
    if (percent < 0) {
        percent = 0;
    }
    if (percent > 100) {
        percent = 100;
    }
    bsp_audio_set_volume((uint8_t)percent);
}

bool ft_audio_start_capture(void)
{
    if (s_capturing) {
        return true;
    }
    if (s_capture_task != NULL) {
        return false;
    }
    s_capturing = true;
    if (xTaskCreate(ft_capture_task, "ft_cap", 4096, NULL, 5, &s_capture_task) != pdPASS) {
        s_capturing = false;
        s_capture_task = NULL;
        return false;
    }
    return true;
}

void ft_audio_stop_capture(void)
{
    s_capturing = false;
    if (s_capture_task != NULL) {
        // 任务自己会检查标志并退出,这里等一小会儿确认。
        for (int i = 0; i < 50 && s_capture_task != NULL; i++) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (s_capture_task != NULL) {
            vTaskDelete(s_capture_task);
            s_capture_task = NULL;
        }
    }
}

bool ft_audio_is_capturing(void)
{
    return s_capturing;
}

void ft_audio_set_sender(ft_audio_send_fn fn)
{
    s_sender = fn;
}

bool ft_audio_start_playback(void)
{
    if (s_playback_running) {
        return true;
    }
    if (s_playback_task != NULL) {
        return false;
    }
    s_playback_running = true;
    if (xTaskCreate(ft_playback_task, "ft_play", 4096, NULL, 5, &s_playback_task) != pdPASS) {
        s_playback_running = false;
        s_playback_task = NULL;
        return false;
    }
    return true;
}

void ft_audio_stop_playback(void)
{
    s_playback_running = false;
    if (s_playback_task != NULL) {
        for (int i = 0; i < 50 && s_playback_task != NULL; i++) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (s_playback_task != NULL) {
            vTaskDelete(s_playback_task);
            s_playback_task = NULL;
        }
    }
    if (s_play_lock != NULL && xSemaphoreTake(s_play_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
        s_play_read = 0;
        s_play_write = 0;
        s_play_fill = 0;
        xSemaphoreGive(s_play_lock);
    }
}

void ft_audio_push_playback(const uint8_t *data, size_t len)
{
    if (data == NULL || len == 0 || s_play_lock == NULL) {
        return;
    }
    if (xSemaphoreTake(s_play_lock, pdMS_TO_TICKS(20)) != pdTRUE) {
        return;
    }
    for (size_t i = 0; i < len; i++) {
        if (s_play_fill >= FT_PLAYBACK_CAPACITY) {
            // 缓冲满:丢掉最老的一帧,保持低延迟。
            s_play_read = (s_play_read + FT_FRAME_BYTES) % FT_PLAYBACK_CAPACITY;
            if (s_play_fill >= FT_FRAME_BYTES) {
                s_play_fill -= FT_FRAME_BYTES;
            } else {
                s_play_fill = 0;
            }
        }
        s_play_buf[s_play_write] = data[i];
        s_play_write = (s_play_write + 1) % FT_PLAYBACK_CAPACITY;
        s_play_fill++;
    }
    xSemaphoreGive(s_play_lock);
}

size_t ft_audio_playback_pending(void)
{
    if (s_play_lock == NULL) {
        return 0;
    }
    size_t fill = 0;
    if (xSemaphoreTake(s_play_lock, pdMS_TO_TICKS(10)) == pdTRUE) {
        fill = s_play_fill;
        xSemaphoreGive(s_play_lock);
    }
    return fill;
}

bool ft_audio_next_playback(uint8_t *out, size_t capacity, size_t *out_len)
{
    // 播放由独立任务完成,这个接口保留给不需要独立任务的场景。
    if (out == NULL || out_len == NULL || capacity < FT_FRAME_BYTES) {
        return false;
    }
    if (s_play_lock == NULL) {
        return false;
    }
    if (xSemaphoreTake(s_play_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }
    size_t n = 0;
    while (n < FT_FRAME_BYTES && s_play_fill > 0) {
        out[n++] = s_play_buf[s_play_read];
        s_play_read = (s_play_read + 1) % FT_PLAYBACK_CAPACITY;
        s_play_fill--;
    }
    xSemaphoreGive(s_play_lock);
    *out_len = n;
    return n > 0;
}
