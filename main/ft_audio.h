// FoloTalker 音频管线 —— 采集、16kHz 转换、发送、接收、播放。
//
// 协议要求:16kHz / 单声道 / Int16LE / 20ms 定长帧(640 字节)。
//
// ES8311 codec 的原生采样率由 bsp_audio_set_format() 决定。
// 16kHz 在 ES8311 上是支持的,所以优先直接让 codec 跑 16kHz,
// 避免软件重采样带来的失真与 CPU 开销。
//
// 线程模型:
//   - 采集任务:从 codec 读 PCM -> 转 16kHz 定长帧 -> 交给网络层发送
//   - 播放任务:从网络层收 PCM -> 交给 codec 播放
//   两者都是阻塞式 PCM 读写,必须在自己的工作任务里做。
#pragma once

#include "ft_protocol.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 初始化音频管线(打开 codec,设为 16kHz 单声道 16bit)。
bool ft_audio_init(void);

// 反初始化。
void ft_audio_deinit(void);

// 设置播放音量 0..100。
void ft_audio_set_volume(int percent);

// 开始采集。启动采集任务。
bool ft_audio_start_capture(void);

// 停止采集。停止采集任务并释放 codec 的录音通道。
void ft_audio_stop_capture(void);

// 是否正在采集。
bool ft_audio_is_capturing(void);

// 发送回调:采集任务每凑够一帧就调用它。
typedef void (*ft_audio_send_fn)(const uint8_t *data, size_t len);
void ft_audio_set_sender(ft_audio_send_fn fn);

// 取一个待播放的音频帧(阻塞等待)。返回 false 表示播放侧已停止。
// out 至少要有 FT_FRAME_BYTES 字节的空间。
bool ft_audio_next_playback(uint8_t *out, size_t capacity, size_t *out_len);

// 启动播放任务。
bool ft_audio_start_playback(void);

// 停止播放任务并清空播放缓冲。
void ft_audio_stop_playback(void);

// 把一段收到的音频塞进播放缓冲。
void ft_audio_push_playback(const uint8_t *data, size_t len);

// 播放缓冲里还剩多少字节(用于 UI 显示缓冲深度)。
size_t ft_audio_playback_pending(void);

#ifdef __cplusplus
}
#endif
