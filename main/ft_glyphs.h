// FoloTalker 字形覆盖自检 —— 纯逻辑,可主机测试。
//
// 为什么需要它:LVGL 的 Montserrat 字体没有任何中文字形,而
// "UTF-8 编码正确 + 编译通过"都不代表屏幕上能显示出中文。
// 这里按码点检查生成字体里是否真的有对应字形,把缺字变成一个可测的失败。
//
// 检查方式与 lvgl-chinese-fonts.md 的建议一致:
//   lv_font_get_glyph_dsc() 返回成功 且 is_placeholder 为假,才算真正覆盖。
//
// 本模块不依赖 LVGL:字体的 cmap 覆盖信息由"外部提供的查表回调"注入,
// 这样主机测试可以用一个假的字体描述来验证检查逻辑本身。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 一个码点的覆盖情况。
typedef struct {
    uint32_t codepoint;   // Unicode 码点
    bool covered;         // 字体里真的有这个字形
} ft_glyph_check_t;

// 字体覆盖查询后端。真实实现走 LVGL;主机测试注入假的。
typedef bool (*ft_glyph_query_fn)(void *user, uint32_t codepoint);

// 检查一段 UTF-8 文本里的所有码点是否都被覆盖。
//
// out_results: 可选。每覆盖/缺失一个码点就往里写一条,capacity 为容量上限。
//              传 NULL 表示只数个数。
//
// 返回缺失的码点数量(0 = 全部覆盖)。
size_t ft_font_check_coverage(const char *text,
                              ft_glyph_query_fn query, void *user,
                              ft_glyph_check_t *out_results, size_t capacity);

// 把一个 UTF-8 文本解码成码点数组。返回解码出的码点个数。
// out 可为 NULL(只数个数)。
size_t ft_utf8_decode(const char *text, uint32_t *out, size_t capacity);

#ifdef __cplusplus
}
#endif
