// FoloTalker 应用主控 —— 把网络、音频、UI、存储串成一个应用。
//
// 这是本应用的入口。它取代基线 main 分支的 demo 菜单:
// 启动后直接进入对讲应用的流程(配网 -> 服务器地址 -> 门禁 -> 房间),
// 界面上不会出现任何 demo 测试项。
//
// 线程模型:
//   - app_task:唯一的状态机所有者。处理网络事件、按键事件、定时推进。
//   - button 组件回调:只投递按键事件,不碰状态机和 LVGL。
//   - 网络回调:只往队列里塞数据(见 ft_net.c)。
//   - 音频采集/播放:各自的任务(见 ft_audio.c)。
#pragma once

#include "ft_state.h"

#include <stdbool.h>

#include "bsp_button.h"

#ifdef __cplusplus
extern "C" {
#endif

// 启动应用。返回 false 表示初始化失败。
bool ft_app_start(void);

// 停止应用,释放所有资源。
void ft_app_stop(void);

// 按键事件投递给应用任务(由 main.c 的 on_key() 回调调用)。
void ft_app_dispatch_key(bsp_btn_t btn, bsp_btn_ev_t ev);

#ifdef __cplusplus
}
#endif
