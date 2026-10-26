// FoloTalker 视觉系统实现。
#include "ft_theme.h"
#include "ft_fonts.h"

#include <string.h>

// 应用字体集合。ft_ui_init() 在 LVGL 起来之后填充这三项。
static ft_fonts_t s_fonts;

const ft_fonts_t *ft_fonts(void)
{
    return &s_fonts;
}

void ft_fonts_init(void)
{
    s_fonts.font_16 = &ft_font_16;
    s_fonts.font_20 = &ft_font_20;
    s_fonts.font_28 = &ft_font_28;
}

void ft_style_fonts(lv_obj_t *obj, const ft_fonts_t *fonts)
{
    if (obj == NULL || fonts == NULL) {
        return;
    }
    // 正文用 16,标题类元素单独设 20 —— 这里只设正文,避免覆盖调用方的显式设置。
    lv_obj_set_style_text_font(obj, fonts->font_16, LV_PART_MAIN);
}

void ft_style_card(lv_obj_t *obj)
{
    if (obj == NULL) {
        return;
    }
    lv_obj_set_style_bg_color(obj, FT_C_SURFACE, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, 12, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 10, 0);
    lv_obj_set_style_text_color(obj, FT_C_TEXT, 0);
}

void ft_style_button(lv_obj_t *obj, lv_color_t accent)
{
    if (obj == NULL) {
        return;
    }
    lv_obj_set_style_bg_color(obj, accent, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(obj, lv_color_darken(accent, LV_OPA_20), LV_STATE_PRESSED);
    lv_obj_set_style_radius(obj, 10, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_text_color(obj, FT_C_BG, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
}

void ft_style_chip(lv_obj_t *obj, lv_color_t accent)
{
    if (obj == NULL) {
        return;
    }
    lv_obj_set_style_bg_color(obj, lv_color_darken(accent, LV_OPA_70), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, accent, 0);
    lv_obj_set_style_text_color(obj, accent, 0);
    lv_obj_set_style_pad_hor(obj, 10, 0);
    lv_obj_set_style_pad_ver(obj, 4, 0);
}
