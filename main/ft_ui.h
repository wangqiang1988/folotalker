// FoloTalker 应用界面 —— 本应用自己的页面与交互。
//
// 【与基线 demo 的关系】
// 基线 main 分支上的 demo 菜单、demo_*.c 测试页和 ui_pixel 像素外壳是
// "硬件能力测试界面",不是应用 UI。本模块是重新设计的对讲应用界面:
//   布局、配色、导航结构、按键语义全部独立实现,不沿用那套外壳。
//
// 【页面】
//   FT_PAGE_WIFI       顶部横幅提示未连 Wi-Fi
//   FT_PAGE_HOST       服务器地址输入(带屏幕键盘)
//   FT_PAGE_GATE       昵称 + 家庭口令输入
//   FT_PAGE_ROOM       房间主界面(成员 / 麦权 / 提示)
//   FT_PAGE_SETTINGS   设置(音量 / 重新配网)
//
// 【按键】
//   房间页:确定键【按住】= 说话;确定短按 = 打开设置;
//            确定长按 = 切换麦克风静音
//   输入页:上/下 = 选择字符;确定短按 = 输入字符;
//            确定长按 = 退格;上键长按 = 切换字符集
//   设置页:上/下 = 移动条目;确定 = 执行
#pragma once

#include "ft_state.h"
#include "ft_theme.h"
#include "ft_textfield.h"

#ifdef __cplusplus
extern "C" {
#endif

// UI 向应用层发出的动作意图。UI 只表达意图,不直接操作网络/音频/存储。
typedef enum {
    FT_UI_ACT_NONE = 0,
    FT_UI_ACT_JOIN,             // 用当前输入的 host/nick/code 加入房间
    FT_UI_ACT_SAVE_HOST,        // 保存服务器地址
    FT_UI_ACT_SAVE_CREDENTIAL,  // 保存昵称与口令
    FT_UI_ACT_TALK_START,
    FT_UI_ACT_TALK_STOP,
    FT_UI_ACT_SET_VOLUME,       // 参数见 ft_ui_get_volume()
    FT_UI_ACT_PROVISION,        // 进入 BLE 配网
    FT_UI_ACT_CLEAR_CREDENTIAL,
    FT_UI_ACT_ENTER_SETTINGS,   // 从房间页进入设置页
    FT_UI_ACT_LEAVE_SETTINGS,   // 从设置页回到房间页
} ft_ui_action_t;

// 输入框焦点
#define FT_FIELD_NONE 0
#define FT_FIELD_HOST 1
#define FT_FIELD_NICK 2
#define FT_FIELD_CODE 3

// 初始化界面。在 LVGL 启动之后调用。
void ft_ui_init(void);

// 销毁界面。停掉所有定时器与任务后再删控件。
void ft_ui_deinit(void);

// 渲染当前状态。内部会取 LVGL 锁。
void ft_ui_render(const ft_state_t *st);

// 从 UI 任务投递一个按键事件(不直接操作 LVGL)。
// key: 0=上 1=下 2=确定
void ft_ui_post_key(int key, bool key_long);

// 投递渲染/电池刷新请求。
void ft_ui_post_render(void);
void ft_ui_post_battery(void);

// 设置输入框焦点(FT_FIELD_*)。会重置输入框内容。
void ft_ui_set_field_focus(int focus);

// 设置页光标位置。
void ft_ui_set_setting_index(int index);

// 音量(UI 侧缓存值)。
void ft_ui_set_volume(int volume);
int ft_ui_get_volume(void);

// 取走一次待执行的动作(见 ft_ui_action_t)。没有动作时返回 FT_UI_ACT_NONE。
ft_ui_action_t ft_ui_take_action(void);

// 显示一条错误提示。传 NULL 或空串表示清除。
void ft_ui_show_error(const char *msg);

// 更新配网页的状态文案。
void ft_ui_show_provision(const char *status);

// 当前输入框的内容(UI 侧)。
const char *ft_ui_field_text(void);
void ft_ui_field_set_text(const char *text);
void ft_ui_field_clear(void);

// 门禁页的昵称与家庭口令(提交时读取)。
const char *ft_ui_gate_nick(void);
const char *ft_ui_gate_code(void);

// 服务器地址页输入的地址(提交时读取)。
const char *ft_ui_host_addr(void);

// 页面跳转请求(由 UI 内部使用,应用层据此切状态)。
void ft_ui_request_page(ft_page_t page);

// 取走页面跳转请求。没有请求时返回 false。
bool ft_ui_take_page_request(ft_page_t *out);

// 记录当前页面(应用层在 ft_ui_render() 时同步过来,供按键分发使用)。
void ft_ui_set_current_page(ft_page_t page);

// 应用图标所需的字形覆盖自检。返回缺失的码点个数(0 = 全覆盖)。
int ft_font_missing_glyphs(const lv_font_t *font, const char *text);

#ifdef __cplusplus
}
#endif
