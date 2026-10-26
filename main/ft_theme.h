// FoloTalker 视觉系统 —— 本应用自己的配色、字体与控件外观。
//
// 与基线 demo 的 ui_pixel 像素外壳完全无关:这里是一套"卡片 + 柔和色板"的
// 扁平化设计,专门为 240x320 的小屏对讲场景设计。
//
// 颜色定义遵循 [AI_HARDWARE_DEVELOPMENT_GUIDE] 的显示约束:RGB565,
// 所有颜色都必须在 240x320 的实际面板上可读。
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// --- 语义色板 -------------------------------------------------------------
// 深色主题:房间界面适合暗环境(设备常放在床头/客厅),对比度更高。
#define FT_C_BG          lv_color_hex(0x14181F)  // 页面底色
#define FT_C_SURFACE     lv_color_hex(0x1E2530)  // 卡片底色
#define FT_C_SURFACE_2   lv_color_hex(0x2A3340)  // 次级卡片 / 选中态底色
#define FT_C_ACCENT      lv_color_hex(0x4FC3F7)  // 主强调色(青)
#define FT_C_ACCENT_DIM  lv_color_hex(0x2B6E85)  // 强调色的暗态
#define FT_C_SPEAK       lv_color_hex(0x67E88A)  // 正在说话(绿)
#define FT_C_WARN        lv_color_hex(0xFFB74D)  // 警告(橙)
#define FT_C_DANGER      lv_color_hex(0xEF5350)  // 错误(红)
#define FT_C_TEXT        lv_color_hex(0xF2F5F8)  // 主文字
#define FT_C_TEXT_DIM    lv_color_hex(0x8B97A6)  // 次要文字
#define FT_C_TEXT_FAINT  lv_color_hex(0x5A6675)  // 极弱文字 / 占位符
#define FT_C_DIVIDER     lv_color_hex(0x323C4B)  // 分隔线

// --- 字体 -----------------------------------------------------------------
// app_font_16 / app_font_20 是本应用生成的中文子集字体(见 assets/fonts/)。
// 声明在 ft_fonts.h 里,这里只提供取用入口。

typedef struct {
    const lv_font_t *font_16;   // 正文
    const lv_font_t *font_20;   // 标题 / 关键数值
    const lv_font_t *font_28;   // 大号数值(如麦权计时)
} ft_fonts_t;

// 取得应用字体集合。必须在 LVGL 初始化之后调用。
const ft_fonts_t *ft_fonts(void);

// 初始化字体集合(填充指向生成字体的指针)。
// 必须在 LVGL 初始化之后、创建任何控件之前调用。
void ft_fonts_init(void);

// 把字体集合挂到对象上(设置正文与标题字体)。
// 这是本应用自己的字体绑定方式 —— 不依赖 LVGL 的全局默认主题,
// 因为基线 demo 里的 ui_pixel_label() 会显式设 Montserrat,不能复用。
void ft_style_fonts(lv_obj_t *obj, const ft_fonts_t *fonts);

// 应用统一的圆角卡片样式。
void ft_style_card(lv_obj_t *obj);

// 应用统一的"填充按钮"样式。
void ft_style_button(lv_obj_t *obj, lv_color_t accent);

// 应用统一的"标签胶囊"样式。
void ft_style_chip(lv_obj_t *obj, lv_color_t accent);

#ifdef __cplusplus
}
#endif
