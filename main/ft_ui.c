// FoloTalker 应用界面实现 —— 本应用自己的页面、布局与交互。
//
// 【设计说明】
// 基线 main 分支的 demo 菜单与 ui_pixel 像素外壳是"硬件能力测试界面"。
// 本文件不沿用那套外壳:这里是一套为 240x320 对讲场景重新设计的
// "深色卡片 + 强调色"界面,页面结构与按键语义完全由本应用定义。
//
//   房间页布局 (240x320)
//   ┌────────────────────────────┐
//   │ 对讲            [电池 ▮▮▯] │  ← 标题栏 + 电池(右上)
//   │ ┌────────────────────────┐ │
//   │ │ ● 爸爸  正在说话       │ │  ← 状态卡片(说话者)
//   │ └────────────────────────┘ │
//   │ ┌────────────────────────┐ │
//   │ │ 在线 3 人              │ │  ← 成员卡片
//   │ │ 妈妈   爷爷            │ │
//   │ └────────────────────────┘ │
//   │  ┌──────────────────────┐  │
//   │  │    按 住 说 话       │  │  ← 麦权按钮(强调色)
//   │  └──────────────────────┘  │
//   │ 短按确定进入设置            │  ← 提示行
//   └────────────────────────────┘
//
// 【线程模型】
// 本文件的所有 LVGL 操作都在 LVGL 任务里执行(ft_ui_task),
// 或者在持有 bsp_lvgl_lock() 的情况下执行。定时器回调只发事件给 UI 任务,
// 不直接碰 LVGL 对象。
#include "ft_ui.h"
#include "ft_form.h"
#include "ft_fonts.h"
#include "ft_texts.h"

#include <stdio.h>
#include <string.h>

#include "bsp_battery.h"
#include "bsp_display.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

// ---------------------------------------------------------------------------
// UI 内部状态
// ---------------------------------------------------------------------------

typedef enum {
    FT_SETTING_VOLUME = 0,
    FT_SETTING_SERVER,
    FT_SETTING_PROVISION,
    FT_SETTING_CREDENTIAL,
    FT_SETTING_BACK,
    FT_SETTING_COUNT,
} ft_setting_item_t;

static const char *const FT_SETTING_LABELS[FT_SETTING_COUNT] = {
    FT_TXT_SET_VOLUME,
    FT_TXT_SET_SERVER,
    FT_TXT_SET_PROVISION,
    FT_TXT_SET_CREDENTIAL,
    FT_TXT_SET_BACK,
};

typedef struct {
    lv_obj_t *root;
    lv_obj_t *title;
    lv_obj_t *battery;
    lv_obj_t *battery_label;

    // 房间页
    lv_obj_t *room_box;
    lv_obj_t *status_card;
    lv_obj_t *status_text;
    lv_obj_t *status_sub;
    lv_obj_t *members_card;
    lv_obj_t *members_title;
    lv_obj_t *members_list;
    lv_obj_t *talk_btn;
    lv_obj_t *talk_label;
    lv_obj_t *hint_label;

    // 输入页
    lv_obj_t *input_box;
    lv_obj_t *input_title;
    lv_obj_t *input_display;
    lv_obj_t *input_charset;
    lv_obj_t *input_hint;
    lv_obj_t *input_error;

    // 设置页
    lv_obj_t *settings_box;
    lv_obj_t *settings_list;
    lv_obj_t *settings_value;
    lv_obj_t *settings_hint;

    // 配网页
    lv_obj_t *provision_box;
    lv_obj_t *provision_status;
    lv_obj_t *provision_hint;

    // Wi-Fi 提示横幅
    lv_obj_t *wifi_banner;
    lv_obj_t *wifi_text;

    // 错误横幅
    lv_obj_t *error_banner;
    lv_obj_t *error_text;

    // 输入框
    ft_text_field_t field;
    int field_focus;

    // 门禁页与服务器地址页都用同一个多行表单(见 ft_form.h)。
    // 表单的切行与内容保留逻辑由 ft_form.c 负责,并有主机测试覆盖。
    ft_form_t form;
    int form_kind;         // FT_FORM_KIND_HOST / FT_FORM_KIND_GATE

    // 设置页光标
    int setting_index;

    // 定时器
    lv_timer_t *tick_timer;
} ft_ui_t;

static ft_ui_t s_ui;

// UI 任务与事件队列
#define UI_QUEUE_DEPTH 16

typedef enum {
    UI_EV_NONE = 0,
    UI_EV_KEY,
    UI_EV_RENDER,
    UI_EV_BATTERY,
    UI_EV_TICK,
} ui_ev_kind_t;

typedef struct {
    ui_ev_kind_t kind;
    int key;      // 0=上 1=下 2=确定
    bool key_long;
} ui_ev_t;

static QueueHandle_t s_ui_queue;
static TaskHandle_t s_ui_task;
static volatile bool s_ui_running;

// 待执行的动作与参数。UI 设置它们,应用层取走并执行。
static ft_ui_action_t s_pending_action;
static int s_pending_volume;
// UI 请求切换到的页面(应用层读取后同步到状态机)。
static ft_page_t s_requested_page;
static bool s_page_requested;

// ---------------------------------------------------------------------------
// 多行表单(服务器地址页 / 门禁页)
//
// 表单的行结构与"切行不丢字"的规则在 ft_form.c 里,并有主机测试覆盖。
// UI 这里只负责把当前行的标题、占位符和字符集显示出来。
// ---------------------------------------------------------------------------

#define FT_FORM_KIND_HOST 0
#define FT_FORM_KIND_GATE 1

// 每一行的标题与占位符。
static const char *ft_form_row_title(int kind, int row)
{
    if (kind == FT_FORM_KIND_HOST) {
        return (row == 0) ? FT_TXT_ENTER_HOST : FT_TXT_SAVE;
    }
    switch (row) {
    case 0:  return FT_TXT_ENTER_NICK;
    case 1:  return FT_TXT_ENTER_CODE;
    default: return FT_TXT_SAVE_AND_JOIN;
    }
}

static const char *ft_form_row_placeholder(int kind, int row)
{
    if (kind == FT_FORM_KIND_HOST) {
        return (row == 0) ? FT_TXT_PLACEHOLDER_HOST : NULL;
    }
    switch (row) {
    case 0:  return FT_TXT_PLACEHOLDER_NICK;
    case 1:  return FT_TXT_PLACEHOLDER_CODE;
    default: return NULL;
    }
}

// 某一行是不是数字输入(口令行)。
static bool ft_form_row_numeric(int kind, int row)
{
    return (kind == FT_FORM_KIND_GATE && row == 1);
}

// 切到新的一行后,刷新标题、占位符与字符集。
static void ft_form_apply_row(void)
{
    int row = ft_form_current(&s_ui.form);
    const char *title = ft_form_row_title(s_ui.form_kind, row);
    const char *ph = ft_form_row_placeholder(s_ui.form_kind, row);

    lv_label_set_text(s_ui.input_title, title);

    ft_text_field_t *f = ft_form_current_field(&s_ui.form);
    if (f != NULL) {
        f->placeholder = ph;
        // 口令行默认落在数字集,其它行默认小写。
        if (ft_form_row_numeric(s_ui.form_kind, row)) {
            ft_text_field_set_charset(f, FT_CHARSET_DIGITS);
        } else {
            ft_text_field_set_charset(f, FT_CHARSET_LOWER);
        }
    }
}

// 初始化一个表单。
static void ft_form_setup(int kind)
{
    s_ui.form_kind = kind;
    if (kind == FT_FORM_KIND_HOST) {
        const ft_form_row_kind_t rows[] = { FT_FORM_ROW_TEXT, FT_FORM_ROW_SAVE };
        ft_form_init(&s_ui.form, rows, 2);
    } else {
        const ft_form_row_kind_t rows[] = {
            FT_FORM_ROW_TEXT, FT_FORM_ROW_TEXT, FT_FORM_ROW_SAVE
        };
        ft_form_init(&s_ui.form, rows, 3);
    }
    ft_form_apply_row();
}

// ---------------------------------------------------------------------------
// 通用小部件
// ---------------------------------------------------------------------------

static lv_obj_t *ft_make_label(lv_obj_t *parent, const char *text,
                               const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    return label;
}

// 标题栏:左侧标题 + 右侧电池
static void ft_build_header(lv_obj_t *parent)
{
    s_ui.title = ft_make_label(parent, FT_TXT_TITLE_APP, ft_fonts()->font_20, FT_C_TEXT);
    lv_obj_align(s_ui.title, LV_ALIGN_TOP_LEFT, 12, 8);

    // 电池:小方块 + 百分比。读不到时只画图标,不画数字(AI 指南要求优雅降级)。
    s_ui.battery = lv_obj_create(parent);
    lv_obj_remove_style_all(s_ui.battery);
    lv_obj_set_size(s_ui.battery, 26, 14);
    lv_obj_align(s_ui.battery, LV_ALIGN_TOP_RIGHT, -12, 12);
    lv_obj_set_style_bg_color(s_ui.battery, FT_C_TEXT_DIM, 0);
    lv_obj_set_style_bg_opa(s_ui.battery, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_ui.battery, 3, 0);
    lv_obj_set_style_border_width(s_ui.battery, 1, 0);
    lv_obj_set_style_border_color(s_ui.battery, FT_C_TEXT_DIM, 0);

    s_ui.battery_label = ft_make_label(parent, "", ft_fonts()->font_16, FT_C_TEXT_DIM);
    lv_obj_align(s_ui.battery_label, LV_ALIGN_TOP_RIGHT, -44, 8);
}

// 错误横幅(顶部标题下方)
static void ft_build_error_banner(lv_obj_t *parent)
{
    s_ui.error_banner = lv_obj_create(parent);
    lv_obj_remove_style_all(s_ui.error_banner);
    lv_obj_set_size(s_ui.error_banner, 216, 26);
    lv_obj_align(s_ui.error_banner, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_set_style_bg_color(s_ui.error_banner, FT_C_DANGER, 0);
    lv_obj_set_style_bg_opa(s_ui.error_banner, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_ui.error_banner, 6, 0);
    lv_obj_set_style_pad_all(s_ui.error_banner, 3, 0);

    s_ui.error_text = ft_make_label(s_ui.error_banner, "", ft_fonts()->font_16, FT_C_TEXT);
    lv_obj_center(s_ui.error_text);
    lv_obj_set_hidden(s_ui.error_banner, true);
}

// Wi-Fi 未连接的顶部提示
static void ft_build_wifi_banner(lv_obj_t *parent)
{
    s_ui.wifi_banner = lv_obj_create(parent);
    lv_obj_remove_style_all(s_ui.wifi_banner);
    lv_obj_set_size(s_ui.wifi_banner, 216, 24);
    lv_obj_align(s_ui.wifi_banner, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_set_style_bg_color(s_ui.wifi_banner, FT_C_WARN, 0);
    lv_obj_set_style_bg_opa(s_ui.wifi_banner, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_ui.wifi_banner, 6, 0);
    lv_obj_set_style_pad_all(s_ui.wifi_banner, 2, 0);

    s_ui.wifi_text = ft_make_label(s_ui.wifi_banner, FT_TXT_WIFI_NEEDED,
                                   ft_fonts()->font_16, FT_C_BG);
    lv_obj_center(s_ui.wifi_text);
    lv_obj_set_hidden(s_ui.wifi_banner, true);
}

// ---------------------------------------------------------------------------
// 房间页
// ---------------------------------------------------------------------------

static void ft_build_room_page(lv_obj_t *parent)
{
    s_ui.room_box = lv_obj_create(parent);
    lv_obj_remove_style_all(s_ui.room_box);
    lv_obj_set_size(s_ui.room_box, 240, 320);
    lv_obj_center(s_ui.room_box);

    // 状态卡片:谁在说话
    s_ui.status_card = lv_obj_create(s_ui.room_box);
    ft_style_card(s_ui.status_card);
    lv_obj_set_size(s_ui.status_card, 216, 46);
    lv_obj_align(s_ui.status_card, LV_ALIGN_TOP_MID, 0, 44);

    s_ui.status_text = ft_make_label(s_ui.status_card, FT_TXT_NO_ONE_TALKING,
                                     ft_fonts()->font_20, FT_C_TEXT);
    lv_obj_align(s_ui.status_text, LV_ALIGN_TOP_LEFT, 0, 0);

    s_ui.status_sub = ft_make_label(s_ui.status_card, "", ft_fonts()->font_16,
                                     FT_C_TEXT_DIM);
    lv_obj_align(s_ui.status_sub, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    // 成员卡片
    s_ui.members_card = lv_obj_create(s_ui.room_box);
    ft_style_card(s_ui.members_card);
    lv_obj_set_size(s_ui.members_card, 216, 76);
    lv_obj_align(s_ui.members_card, LV_ALIGN_TOP_MID, 0, 98);

    s_ui.members_title = ft_make_label(s_ui.members_card, FT_TXT_ROOM_EMPTY,
                                       ft_fonts()->font_16, FT_C_TEXT_DIM);
    lv_obj_align(s_ui.members_title, LV_ALIGN_TOP_LEFT, 0, 0);

    s_ui.members_list = ft_make_label(s_ui.members_card, "", ft_fonts()->font_16, FT_C_TEXT);
    lv_obj_align(s_ui.members_list, LV_ALIGN_TOP_LEFT, 0, 22);

    // 麦权按钮
    s_ui.talk_btn = lv_obj_create(s_ui.room_box);
    lv_obj_remove_style_all(s_ui.talk_btn);
    lv_obj_set_size(s_ui.talk_btn, 216, 66);
    lv_obj_align(s_ui.talk_btn, LV_ALIGN_BOTTOM_MID, 0, -30);
    lv_obj_set_style_bg_color(s_ui.talk_btn, FT_C_ACCENT, 0);
    lv_obj_set_style_bg_opa(s_ui.talk_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_ui.talk_btn, FT_C_ACCENT_DIM, LV_STATE_PRESSED);
    lv_obj_set_style_radius(s_ui.talk_btn, 14, 0);

    s_ui.talk_label = ft_make_label(s_ui.talk_btn, FT_TXT_HOLD_TO_TALK,
                                    ft_fonts()->font_28, FT_C_BG);
    lv_obj_center(s_ui.talk_label);

    // 底部提示
    s_ui.hint_label = ft_make_label(s_ui.room_box, FT_TXT_HINT_ROOM,
                                    ft_fonts()->font_16, FT_C_TEXT_FAINT);
    lv_obj_align(s_ui.hint_label, LV_ALIGN_BOTTOM_MID, 0, -6);
}

// ---------------------------------------------------------------------------
// 输入页
// ---------------------------------------------------------------------------

static void ft_build_input_page(lv_obj_t *parent)
{
    s_ui.input_box = lv_obj_create(parent);
    lv_obj_remove_style_all(s_ui.input_box);
    lv_obj_set_size(s_ui.input_box, 240, 320);
    lv_obj_center(s_ui.input_box);

    s_ui.input_title = ft_make_label(s_ui.input_box, "", ft_fonts()->font_20, FT_C_TEXT);
    lv_obj_align(s_ui.input_title, LV_ALIGN_TOP_LEFT, 12, 50);

    // 输入显示框
    s_ui.input_display = lv_obj_create(s_ui.input_box);
    ft_style_card(s_ui.input_display);
    lv_obj_set_size(s_ui.input_display, 216, 52);
    lv_obj_align(s_ui.input_display, LV_ALIGN_TOP_MID, 0, 84);
    lv_obj_set_style_bg_color(s_ui.input_display, FT_C_SURFACE_2, 0);

    s_ui.input_charset = ft_make_label(s_ui.input_box, "", ft_fonts()->font_16, FT_C_ACCENT);
    lv_obj_align(s_ui.input_charset, LV_ALIGN_TOP_LEFT, 12, 148);

    s_ui.input_hint = ft_make_label(s_ui.input_box, FT_TXT_HINT_EDIT,
                                     ft_fonts()->font_16, FT_C_TEXT_FAINT);
    lv_obj_align(s_ui.input_hint, LV_ALIGN_TOP_LEFT, 12, 174);

    s_ui.input_error = ft_make_label(s_ui.input_box, "", ft_fonts()->font_16, FT_C_DANGER);
    lv_obj_align(s_ui.input_error, LV_ALIGN_BOTTOM_MID, 0, -14);
}

// ---------------------------------------------------------------------------
// 设置页
// ---------------------------------------------------------------------------

static void ft_build_settings_page(lv_obj_t *parent)
{
    s_ui.settings_box = lv_obj_create(parent);
    lv_obj_remove_style_all(s_ui.settings_box);
    lv_obj_set_size(s_ui.settings_box, 240, 320);
    lv_obj_center(s_ui.settings_box);

    s_ui.settings_list = ft_make_label(s_ui.settings_box, "", ft_fonts()->font_20, FT_C_TEXT);
    lv_obj_align(s_ui.settings_list, LV_ALIGN_TOP_LEFT, 14, 50);

    s_ui.settings_value = ft_make_label(s_ui.settings_box, "", ft_fonts()->font_20, FT_C_ACCENT);
    lv_obj_align(s_ui.settings_value, LV_ALIGN_BOTTOM_LEFT, 14, -50);

    s_ui.settings_hint = ft_make_label(s_ui.settings_box, "", ft_fonts()->font_16, FT_C_TEXT_FAINT);
    lv_obj_align(s_ui.settings_hint, LV_ALIGN_BOTTOM_MID, 0, -12);
}

// ---------------------------------------------------------------------------
// 配网页
// ---------------------------------------------------------------------------

static void ft_build_provision_page(lv_obj_t *parent)
{
    s_ui.provision_box = lv_obj_create(parent);
    lv_obj_remove_style_all(s_ui.provision_box);
    lv_obj_set_size(s_ui.provision_box, 240, 320);
    lv_obj_center(s_ui.provision_box);

    s_ui.provision_status = ft_make_label(s_ui.provision_box, FT_TXT_PROV_SCANNING,
                                          ft_fonts()->font_20, FT_C_TEXT);
    lv_obj_align(s_ui.provision_status, LV_ALIGN_CENTER, 0, -10);

    s_ui.provision_hint = ft_make_label(s_ui.provision_box, FT_TXT_PROV_HINT,
                                        ft_fonts()->font_16, FT_C_TEXT_DIM);
    lv_obj_align(s_ui.provision_hint, LV_ALIGN_CENTER, 0, 18);
}

// ---------------------------------------------------------------------------
// 页面切换
// ---------------------------------------------------------------------------

static void ft_show_only(lv_obj_t *obj)
{
    if (obj == NULL) {
        return;
    }
    lv_obj_set_hidden(obj, true);
}

static void ft_show(lv_obj_t *obj)
{
    if (obj == NULL) {
        return;
    }
    lv_obj_set_hidden(obj, false);
}

// 根据状态切换可见页面。
static void ft_apply_page(const ft_state_t *st)
{
    ft_show_only(s_ui.room_box);
    ft_show_only(s_ui.input_box);
    ft_show_only(s_ui.settings_box);
    ft_show_only(s_ui.provision_box);

    switch (st->page) {
    case FT_PAGE_ROOM:
        ft_show(s_ui.room_box);
        lv_label_set_text(s_ui.title, FT_TXT_TITLE_ROOM);
        break;
    case FT_PAGE_HOST:
        ft_show(s_ui.input_box);
        lv_label_set_text(s_ui.title, FT_TXT_TITLE_HOST);
        break;
    case FT_PAGE_GATE:
        ft_show(s_ui.input_box);
        lv_label_set_text(s_ui.title, FT_TXT_TITLE_GATE);
        break;
    case FT_PAGE_SETTINGS:
        ft_show(s_ui.settings_box);
        lv_label_set_text(s_ui.title, FT_TXT_TITLE_SETTINGS);
        break;
    case FT_PAGE_PROVISION:
        ft_show(s_ui.provision_box);
        lv_label_set_text(s_ui.title, FT_TXT_TITLE_PROV);
        break;
    case FT_PAGE_WIFI:
        ft_show(s_ui.input_box);
        lv_label_set_text(s_ui.title, FT_TXT_TITLE_WIFI);
        break;
    case FT_PAGE_BOOT:
    default:
        lv_label_set_text(s_ui.title, FT_TXT_TITLE_APP);
        break;
    }
}

// ---------------------------------------------------------------------------
// 各页面的渲染
// ---------------------------------------------------------------------------

static void ft_render_room(const ft_state_t *st)
{
    // 状态卡片:优先显示"谁在说话"
    if (ft_state_has_floor(st)) {
        lv_label_set_text(s_ui.status_text, FT_TXT_YOU_TALK);
        lv_obj_set_style_text_color(s_ui.status_text, FT_C_SPEAK, LV_PART_MAIN);
        lv_label_set_text(s_ui.status_sub, FT_TXT_TALKING);
    } else if (st->remote_speaking) {
        char buf[64];
        snprintf(buf, sizeof(buf), FT_TXT_SOMEONE_TALKS,
                 st->remote_speaker[0] ? st->remote_speaker : "");
        lv_label_set_text(s_ui.status_text, buf);
        lv_obj_set_style_text_color(s_ui.status_text, FT_C_ACCENT, LV_PART_MAIN);
        lv_label_set_text(s_ui.status_sub, FT_TXT_TALKING);
    } else if (st->conn != FT_CONN_ONLINE) {
        lv_label_set_text(s_ui.status_text, ft_conn_state_text(st->conn));
        lv_obj_set_style_text_color(s_ui.status_text, FT_C_WARN, LV_PART_MAIN);
        lv_label_set_text(s_ui.status_sub, "");
    } else {
        lv_label_set_text(s_ui.status_text, FT_TXT_NO_ONE_TALKING);
        lv_obj_set_style_text_color(s_ui.status_text, FT_C_TEXT, LV_PART_MAIN);
        lv_label_set_text(s_ui.status_sub, "");
    }

    // 麦权按钮
    if (ft_state_has_floor(st)) {
        lv_label_set_text(s_ui.talk_label, FT_TXT_TALKING);
        lv_obj_set_style_bg_color(s_ui.talk_btn, FT_C_SPEAK, 0);
    } else if (st->mic_muted) {
        lv_label_set_text(s_ui.talk_label, FT_TXT_MIC_MUTED);
        lv_obj_set_style_bg_color(s_ui.talk_btn, FT_C_ACCENT_DIM, 0);
    } else {
        lv_label_set_text(s_ui.talk_label, FT_TXT_HOLD_TO_TALK);
        lv_obj_set_style_bg_color(s_ui.talk_btn, FT_C_ACCENT, 0);
    }

    // 成员列表
    size_t peers = ft_state_peer_count(st);
    if (peers == 0) {
        lv_label_set_text(s_ui.members_title, FT_TXT_ROOM_EMPTY);
        lv_label_set_text(s_ui.members_list, "");
    } else {
        char title[32];
        snprintf(title, sizeof(title), FT_TXT_ONLINE_COUNT, (int)(peers + 1));
        lv_label_set_text(s_ui.members_title, title);

        // 成员名横向排列,超过一行时用逗号分隔
        char names[220];
        size_t w = 0;
        names[0] = '\0';
        for (size_t i = 0; i < st->member_count; i++) {
            if (st->self_id[0] && strcmp(st->members[i].id, st->self_id) == 0) {
                continue;
            }
            const char *nm = st->members[i].name[0] ? st->members[i].name : "?";
            int n = snprintf(names + w, sizeof(names) - w, "%s%s",
                             (w > 0) ? "  " : "", nm);
            if (n < 0 || (size_t)n >= sizeof(names) - w) {
                break;
            }
            w += (size_t)n;
        }
        lv_label_set_text(s_ui.members_list, names);
    }
}

static void ft_render_input(void)
{
    char buf[FT_FIELD_CAPACITY + 4];
    ft_text_field_t *f = ft_form_current_field(&s_ui.form);
    if (f == NULL) {
        return;
    }
    const char *text = ft_text_field_get(f);
    if (ft_text_field_is_empty(f)) {
        // 空输入框:显示占位提示,而不是空白。
        const char *ph = f->placeholder;
        lv_label_set_text(s_ui.input_display, ph ? ph : "");
        lv_obj_set_style_text_color(s_ui.input_display, FT_C_TEXT_FAINT, LV_PART_MAIN);
        lv_label_set_text(s_ui.input_error, "");
    } else {
        snprintf(buf, sizeof(buf), "%s", text);
        lv_label_set_text(s_ui.input_display, buf);
        lv_obj_set_style_text_color(s_ui.input_display, FT_C_TEXT, LV_PART_MAIN);
    }
    lv_label_set_text(s_ui.input_charset, ft_charset_name(ft_text_field_charset(f)));
}

static void ft_render_settings(const ft_state_t *st, int volume)
{
    char buf[256];
    size_t w = 0;
    buf[0] = '\0';
    for (int i = 0; i < FT_SETTING_COUNT; i++) {
        int n = snprintf(buf + w, sizeof(buf) - w, "%s%s%s\n",
                         (i == s_ui.setting_index) ? "> " : "  ",
                         FT_SETTING_LABELS[i],
                         (i == FT_SETTING_COUNT - 1) ? "" : "");
        if (n < 0 || (size_t)n >= sizeof(buf) - w) {
            break;
        }
        w += (size_t)n;
    }
    lv_label_set_text(s_ui.settings_list, buf);

    if (s_ui.setting_index == FT_SETTING_VOLUME) {
        snprintf(buf, sizeof(buf), "%d%%", volume);
        lv_label_set_text(s_ui.settings_value, buf);
        lv_label_set_text(s_ui.settings_hint, "上下键调整");
    } else {
        lv_label_set_text(s_ui.settings_value, "");
        lv_label_set_text(s_ui.settings_hint, "确定键执行");
    }
    (void)st;
}

static void ft_render_battery(void)
{
    int soc = bsp_battery_soc();
    if (soc < 0) {
        // 读不到就只显示图标,不显示数字(AI 指南:优雅降级)
        lv_label_set_text(s_ui.battery_label, "");
        return;
    }
    // 电量是 0..100 的整数,"100%%" 最长 5 字节。留 8 字节足够,
    // 并把 soc 限制到 0..100,避免异常读数撑爆缓冲区。
    if (soc > 100) {
        soc = 100;
    }
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", soc);
    lv_label_set_text(s_ui.battery_label, buf);
    lv_color_t color = (soc <= 15) ? FT_C_DANGER : FT_C_TEXT_DIM;
    lv_obj_set_style_text_color(s_ui.battery_label, color, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ui.battery, color, 0);
    lv_obj_set_style_border_color(s_ui.battery, color, 0);
}

static void ft_render_error(const ft_state_t *st)
{
    if (st->last_error_text[0] != '\0') {
        lv_label_set_text(s_ui.error_text, st->last_error_text);
        ft_show(s_ui.error_banner);
    } else {
        ft_show_only(s_ui.error_banner);
    }
}

// ---------------------------------------------------------------------------
// 对外渲染入口
// ---------------------------------------------------------------------------

void ft_ui_render(const ft_state_t *st)
{
    if (st == NULL || s_ui.root == NULL) {
        return;
    }
    if (!bsp_lvgl_lock(200)) {
        return;
    }
    ft_apply_page(st);
    switch (st->page) {
    case FT_PAGE_ROOM:
        ft_render_room(st);
        break;
    case FT_PAGE_SETTINGS:
        ft_render_settings(st, s_pending_volume);
        break;
    case FT_PAGE_HOST:
    case FT_PAGE_GATE:
        ft_render_input();
        break;
    default:
        break;
    }
    ft_render_error(st);
    ft_render_battery();
    bsp_lvgl_unlock();
}

// ---------------------------------------------------------------------------
// 按键处理
// ---------------------------------------------------------------------------

static void ft_ui_set_field(int focus)
{
    s_ui.field_focus = focus;
    switch (focus) {
    case FT_FIELD_HOST:
        ft_form_setup(FT_FORM_KIND_HOST);
        break;
    case FT_FIELD_NICK:
    case FT_FIELD_CODE:
        // 门禁页:总是从第一行(昵称)开始。
        ft_form_setup(FT_FORM_KIND_GATE);
        break;
    default:
        break;
    }
    lv_label_set_text(s_ui.input_error, "");
}

// 按键事件入口(在 UI 任务里执行)。
// 页面由应用层通过 ft_ui_render() 同步过来,这里保存一份最近的页面副本。
static ft_page_t s_current_page = FT_PAGE_BOOT;

void ft_ui_set_current_page(ft_page_t page)
{
    s_current_page = page;
}

static void ft_handle_room_key(int key, bool key_long)
{
    // 房间页的"按住说话"由应用层(ft_app.c)直接处理,因为它需要
    // PRESS/RELEASE 的即时性。UI 这里只处理"进入设置"这个动作。
    if (key != 2 || key_long) {
        return;
    }
    s_pending_action = FT_UI_ACT_ENTER_SETTINGS;
    s_ui.setting_index = 0;
    ft_ui_request_page(FT_PAGE_SETTINGS);
}

// 设置页的按键
static void ft_handle_settings_key(int key)
{
    switch (key) {
    case 0:
        s_ui.setting_index = (s_ui.setting_index + FT_SETTING_COUNT - 1) % FT_SETTING_COUNT;
        break;
    case 1:
        s_ui.setting_index = (s_ui.setting_index + 1) % FT_SETTING_COUNT;
        break;
    case 2:
        switch ((ft_setting_item_t)s_ui.setting_index) {
        case FT_SETTING_VOLUME:
            s_pending_volume = (s_pending_volume + 10) % 110;
            s_pending_action = FT_UI_ACT_SET_VOLUME;
            break;
        case FT_SETTING_SERVER:
            ft_ui_set_field(FT_FIELD_HOST);
            s_pending_action = FT_UI_ACT_SAVE_HOST;
            break;
        case FT_SETTING_PROVISION:
            s_pending_action = FT_UI_ACT_PROVISION;
            break;
        case FT_SETTING_CREDENTIAL:
            s_pending_action = FT_UI_ACT_CLEAR_CREDENTIAL;
            break;
        case FT_SETTING_BACK:
        default:
            s_pending_action = FT_UI_ACT_LEAVE_SETTINGS;
            ft_ui_request_page(FT_PAGE_ROOM);
            break;
        }
        break;
    default:
        break;
    }
}

void ft_ui_handle_key(int key, bool key_long)
{
    switch (s_current_page) {
    case FT_PAGE_ROOM:
        ft_handle_room_key(key, key_long);
        break;

    case FT_PAGE_SETTINGS:
        if (key == 2 && key_long) {
            s_pending_action = FT_UI_ACT_LEAVE_SETTINGS;
            ft_ui_request_page(FT_PAGE_ROOM);
            break;
        }
        ft_handle_settings_key(key);
        break;

    case FT_PAGE_HOST:
    case FT_PAGE_GATE: {
        ft_text_field_t *f = ft_form_current_field(&s_ui.form);
        if (f == NULL) {
            break;
        }
        if (key == 0 && key_long) {
            // 上键长按:切换字符集
            ft_charset_t cs = ft_text_field_charset(f);
            cs = (ft_charset_t)((cs + 1) % 4);
            ft_text_field_set_charset(f, cs);
            break;
        }
        if (key == 2 && key_long) {
            // 确定长按:退格
            ft_form_backspace(&s_ui.form);
            break;
        }
        if (key == 0 || key == 1) {
            // 上/下键:在行之间移动
            ft_form_move(&s_ui.form, (key == 0) ? -1 : 1);
            ft_form_apply_row();
            break;
        }
        // 确定键:
        if (ft_form_on_save_row(&s_ui.form)) {
            // 在"保存"行上按确定 = 提交
            s_pending_action = (s_ui.form_kind == FT_FORM_KIND_HOST)
                                   ? FT_UI_ACT_SAVE_HOST
                                   : FT_UI_ACT_SAVE_CREDENTIAL;
            break;
        }
        // 文本行:输入当前选中的字符
        char c = ft_text_field_selected_char(f);
        if (c != '\0') {
            ft_form_type(&s_ui.form, c);
        }
        break;
    }

    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// 队列与 UI 任务
// ---------------------------------------------------------------------------

static void ft_ui_queue_event(ui_ev_kind_t kind, int key, bool key_long)
{
    if (s_ui_queue == NULL) {
        return;
    }
    ui_ev_t ev = { .kind = kind, .key = key, .key_long = key_long };
    // 队列满时丢弃最旧的一条,保证最新按键不会卡住。
    xQueueSend(s_ui_queue, &ev, 0);
}

// 定时器回调:只发事件,不碰 LVGL。
static void ft_ui_tick_cb(lv_timer_t *timer)
{
    (void)timer;
    ft_ui_queue_event(UI_EV_TICK, 0, false);
}

static void ft_ui_process(ui_ev_t *ev)
{
    switch (ev->kind) {
    case UI_EV_KEY:
        ft_ui_handle_key(ev->key, ev->key_long);
        break;
    case UI_EV_BATTERY:
        if (bsp_lvgl_lock(100)) {
            ft_render_battery();
            bsp_lvgl_unlock();
        }
        break;
    case UI_EV_TICK:
    case UI_EV_RENDER:
    default:
        break;
    }
}

static void ft_ui_task(void *arg)
{
    (void)arg;
    while (s_ui_running) {
        ui_ev_t ev;
        if (xQueueReceive(s_ui_queue, &ev, pdMS_TO_TICKS(200)) == pdTRUE) {
            ft_ui_process(&ev);
        }
    }
    vTaskDelete(NULL);
}

// ---------------------------------------------------------------------------
// 对外接口
// ---------------------------------------------------------------------------

void ft_ui_init(void)
{
    if (s_ui.root != NULL) {
        return;
    }

    ft_fonts_init();

    s_ui.root = lv_screen_active();
    lv_obj_remove_style_all(s_ui.root);
    lv_obj_set_style_bg_color(s_ui.root, FT_C_BG, 0);
    lv_obj_set_style_bg_opa(s_ui.root, LV_OPA_COVER, 0);
    lv_obj_set_style_text_font(s_ui.root, ft_fonts()->font_16, LV_PART_MAIN);

    ft_build_header(s_ui.root);
    ft_build_error_banner(s_ui.root);
    ft_build_wifi_banner(s_ui.root);
    ft_build_room_page(s_ui.root);
    ft_build_input_page(s_ui.root);
    ft_build_settings_page(s_ui.root);
    ft_build_provision_page(s_ui.root);

    s_ui_queue = xQueueCreate(UI_QUEUE_DEPTH, sizeof(ui_ev_t));
    if (s_ui_queue != NULL) {
        s_ui_running = true;
        xTaskCreate(ft_ui_task, "ft_ui", 4096, NULL, 4, &s_ui_task);
    }

    // 定时器在 LVGL 任务上下文中运行,回调只发事件。
    s_ui.tick_timer = lv_timer_create(ft_ui_tick_cb, 2000, NULL);
}

void ft_ui_deinit(void)
{
    if (s_ui.root == NULL) {
        return;
    }
    // 先停定时器,再停任务,最后删控件 —— 顺序不能反,
    // 否则定时器可能在一个已经被删掉的控件上触发。
    if (s_ui.tick_timer != NULL) {
        lv_timer_delete(s_ui.tick_timer);
        s_ui.tick_timer = NULL;
    }
    s_ui_running = false;
    if (s_ui_queue != NULL) {
        // 唤醒任务让它退出循环
        ui_ev_t ev = { .kind = UI_EV_NONE };
        xQueueSend(s_ui_queue, &ev, 0);
    }
    if (s_ui_task != NULL) {
        vTaskDelay(pdMS_TO_TICKS(250));
        vTaskDelete(s_ui_task);
        s_ui_task = NULL;
    }
    if (s_ui_queue != NULL) {
        vQueueDelete(s_ui_queue);
        s_ui_queue = NULL;
    }
    s_ui.root = NULL;
}

void ft_ui_post_key(int key, bool key_long)
{
    ft_ui_queue_event(UI_EV_KEY, key, key_long);
}

void ft_ui_post_render(void)
{
    ft_ui_queue_event(UI_EV_RENDER, 0, false);
}

void ft_ui_post_battery(void)
{
    ft_ui_queue_event(UI_EV_BATTERY, 0, false);
}

void ft_ui_set_field_focus(int focus)
{
    ft_ui_set_field(focus);
}

void ft_ui_set_setting_index(int index)
{
    s_ui.setting_index = index;
}

void ft_ui_set_volume(int volume)
{
    s_pending_volume = volume;
}

int ft_ui_get_volume(void)
{
    return s_pending_volume;
}

ft_ui_action_t ft_ui_take_action(void)
{
    ft_ui_action_t a = s_pending_action;
    s_pending_action = FT_UI_ACT_NONE;
    return a;
}

void ft_ui_show_error(const char *msg)
{
    if (!bsp_lvgl_lock(100)) {
        return;
    }
    if (msg == NULL || msg[0] == '\0') {
        ft_show_only(s_ui.error_banner);
    } else {
        lv_label_set_text(s_ui.error_text, msg);
        ft_show(s_ui.error_banner);
    }
    bsp_lvgl_unlock();
}

void ft_ui_show_provision(const char *status)
{
    if (!bsp_lvgl_lock(100)) {
        return;
    }
    if (status != NULL) {
        lv_label_set_text(s_ui.provision_status, status);
    }
    bsp_lvgl_unlock();
}

const char *ft_ui_field_text(void)
{
    ft_text_field_t *f = ft_form_current_field(&s_ui.form);
    return (f != NULL) ? ft_text_field_get(f) : "";
}

// 门禁页的昵称与家庭口令。
const char *ft_ui_gate_nick(void)
{
    return ft_form_value(&s_ui.form, 0);
}

const char *ft_ui_gate_code(void)
{
    return ft_form_value(&s_ui.form, 1);
}

// 服务器地址页输入的地址。
const char *ft_ui_host_addr(void)
{
    return ft_form_value(&s_ui.form, 0);
}

void ft_ui_field_set_text(const char *text)
{
    ft_text_field_t *f = ft_form_current_field(&s_ui.form);
    if (f == NULL) {
        return;
    }
    ft_text_field_clear(f);
    if (text == NULL) {
        return;
    }
    // 逐字符插入,保持与键盘输入一致的路径(容量上限由 ft_text_field 控制)。
    for (size_t i = 0; text[i] != '\0'; i++) {
        if (!ft_form_type(&s_ui.form, text[i])) {
            break;
        }
    }
}

void ft_ui_field_clear(void)
{
    ft_text_field_t *f = ft_form_current_field(&s_ui.form);
    if (f != NULL) {
        ft_text_field_clear(f);
    }
}

void ft_ui_request_page(ft_page_t page)
{
    s_requested_page = page;
    s_page_requested = true;
}

// 取走页面跳转请求。没有请求时返回 false。
bool ft_ui_take_page_request(ft_page_t *out)
{
    if (!s_page_requested) {
        return false;
    }
    s_page_requested = false;
    if (out != NULL) {
        *out = s_requested_page;
    }
    return true;
}
