// FoloTalker 应用的中文字体子集。
//
// 这里只声明由 lv_font_conv 生成的静态点阵字体的符号。
// 生成脚本与字符清单见 assets/fonts/README.md。
//
// 字体以 4bpp 位图形式编进 Flash,不占用内部 RAM 堆,但 LVGL 在绘制时
// 需要按需从 Flash 解码到行缓冲 —— 8MB Flash / 无 PSRAM 的板子上,
// 字体子集大小直接决定 Flash 余量,所以只收实际用到的字。
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// 16px 正文:覆盖所有界面文字与动态内容可能用到的字符。
LV_FONT_DECLARE(ft_font_16);

// 20px 标题:用于页面标题、按钮文字、成员名。
LV_FONT_DECLARE(ft_font_20);

// 28px 大号数值:用于"按住说话"提示与计时。
LV_FONT_DECLARE(ft_font_28);

#ifdef __cplusplus
}
#endif
