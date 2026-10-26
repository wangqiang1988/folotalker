// FoloTalker 屏幕文本输入的纯逻辑接口 —— 主机可测。
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FT_FIELD_CAPACITY 32   // 含结尾 '\0'

typedef enum {
    FT_CHARSET_LOWER = 0,
    FT_CHARSET_UPPER,
    FT_CHARSET_DIGITS,
    FT_CHARSET_SYMBOLS,
} ft_charset_t;

typedef struct {
    char text[FT_FIELD_CAPACITY];
    size_t len;
    size_t cursor;             // 光标位置(字节索引)
    size_t char_cursor;        // 当前字符集内选中的字符下标
    bool numeric;              // 默认落在数字集
    ft_charset_t charset;
    const char *title;
    const char *placeholder;
} ft_text_field_t;

// 初始化一个输入框。
void ft_text_field_init(ft_text_field_t *f, const char *title, bool numeric,
                         const char *placeholder);

void ft_text_field_clear(ft_text_field_t *f);

const char *ft_text_field_get(const ft_text_field_t *f);
size_t ft_text_field_len(const ft_text_field_t *f);
bool ft_text_field_is_empty(const ft_text_field_t *f);

// 处理"上/下/确定"。key: 0=上, 1=下, 2=确定。
// 返回 true 表示文本内容发生变化。
bool ft_text_field_key(ft_text_field_t *f, int key, int *sel);

// 当前字符集内选中的字符。
char ft_text_field_selected_char(const ft_text_field_t *f);

// 退格 / 删除 / 左右移动光标。
bool ft_text_field_backspace_key(ft_text_field_t *f);
bool ft_text_field_delete_key(ft_text_field_t *f);
bool ft_text_field_move_left_key(ft_text_field_t *f);
bool ft_text_field_move_right_key(ft_text_field_t *f);

// 切换字符集。
void ft_text_field_set_charset(ft_text_field_t *f, ft_charset_t cs);
ft_charset_t ft_text_field_charset(const ft_text_field_t *f);

// 字符集查询。
const char *ft_charset_chars(ft_charset_t cs);
const char *ft_charset_name(ft_charset_t cs);

#ifdef __cplusplus
}
#endif
