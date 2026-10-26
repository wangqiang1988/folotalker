// FoloTalker 屏幕文本输入的纯逻辑 —— 主机可测。
//
// 把"按上下确定怎么改文本"从 LVGL 里抽出来,这样字符集切换、光标移动、
// 退格、超长拒绝这些边界都能在主机上验证。
#include "ft_textfield.h"

#include <string.h>

// 四个字符集。顺序与 ft_charset_t 一致。
static const char FT_CHARS_LOWER[] = "abcdefghijklmnopqrstuvwxyz";
static const char FT_CHARS_UPPER[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
static const char FT_CHARS_DIGITS[] = "0123456789";
// 符号集按实际需要的顺序排,便于在屏幕上按"上/下"线性浏览。
static const char FT_CHARS_SYMBOLS[] = ".:/-_@# !?,";

static const char *const FT_CHARSETS[] = {
    FT_CHARS_LOWER,
    FT_CHARS_UPPER,
    FT_CHARS_DIGITS,
    FT_CHARS_SYMBOLS,
};

const char *ft_charset_chars(ft_charset_t cs)
{
    unsigned idx = (unsigned)cs;
    if (idx >= sizeof(FT_CHARSETS) / sizeof(FT_CHARSETS[0])) {
        return FT_CHARS_LOWER;
    }
    return FT_CHARSETS[idx];
}

const char *ft_charset_name(ft_charset_t cs)
{
    switch (cs) {
    case FT_CHARSET_LOWER:   return "小写";
    case FT_CHARSET_UPPER:   return "大写";
    case FT_CHARSET_DIGITS:  return "数字";
    case FT_CHARSET_SYMBOLS: return "符号";
    default:                 return "小写";
    }
}

void ft_text_field_init(ft_text_field_t *f, const char *title, bool numeric,
                         const char *placeholder)
{
    if (f == NULL) {
        return;
    }
    memset(f, 0, sizeof(*f));
    f->title = title;
    f->numeric = numeric;
    f->placeholder = placeholder;
    f->charset = numeric ? FT_CHARSET_DIGITS : FT_CHARSET_LOWER;
    // 数字输入时,主机名类的地址仍可能带字母与符号,所以集合顺序不能被锁死;
    // 只是默认落在数字集。
}

void ft_text_field_clear(ft_text_field_t *f)
{
    if (f == NULL) {
        return;
    }
    f->text[0] = '\0';
    f->len = 0;
    f->cursor = 0;
}

const char *ft_text_field_get(const ft_text_field_t *f)
{
    return (f != NULL) ? f->text : "";
}

size_t ft_text_field_len(const ft_text_field_t *f)
{
    return (f != NULL) ? f->len : 0;
}

// 在光标处插入一个字符。
// 超出容量时拒绝,返回 false。
static bool ft_text_field_insert(ft_text_field_t *f, char c)
{
    if (f->len + 1 >= sizeof(f->text)) {
        return false;
    }
    // 光标后面的内容整体后移一字节
    memmove(&f->text[f->cursor + 1], &f->text[f->cursor], f->len - f->cursor);
    f->text[f->cursor] = c;
    f->len++;
    f->cursor++;
    f->text[f->len] = '\0';
    return true;
}

// 删除光标前一个字符。
static bool ft_text_field_backspace(ft_text_field_t *f)
{
    if (f->cursor == 0) {
        return false;
    }
    memmove(&f->text[f->cursor - 1], &f->text[f->cursor], f->len - f->cursor + 1);
    f->len--;
    f->cursor--;
    return true;
}

// 删除光标处字符。
static bool ft_text_field_delete(ft_text_field_t *f)
{
    if (f->cursor >= f->len) {
        return false;
    }
    memmove(&f->text[f->cursor], &f->text[f->cursor + 1], f->len - f->cursor);
    f->len--;
    f->text[f->len] = '\0';
    return true;
}

// 光标左移。跨过 UTF-8 多字节序列时,按字节回退会切坏字符 ——
// 这里只做字节级移动,但插入的都是单字节 ASCII,所以不会切坏既有文本。
static bool ft_text_field_move_left(ft_text_field_t *f)
{
    if (f->cursor == 0) {
        return false;
    }
    f->cursor--;
    return true;
}

static bool ft_text_field_move_right(ft_text_field_t *f)
{
    if (f->cursor >= f->len) {
        return false;
    }
    f->cursor++;
    return true;
}

// 在当前字符集内移动"选中的字符"。
static void ft_text_field_cycle_char(ft_text_field_t *f, int delta)
{
    const char *cs = ft_charset_chars(f->charset);
    size_t n = strlen(cs);
    if (n == 0) {
        return;
    }
    // 用一个独立的游标记住当前选中哪个字符
    if (f->char_cursor >= n) {
        f->char_cursor = 0;
    }
    int next = (int)f->char_cursor + delta;
    while (next < 0) {
        next += (int)n;
    }
    f->char_cursor = (size_t)next % n;
}

char ft_text_field_selected_char(const ft_text_field_t *f)
{
    const char *cs = ft_charset_chars(f->charset);
    size_t n = strlen(cs);
    if (n == 0) {
        return '\0';
    }
    return cs[f->char_cursor % n];
}

// 处理一次按键。
//   key: 0 = 上, 1 = 下, 2 = 确定
//   sel: 输入时的"选中字符"指针(可选;为 NULL 时用内部游标)
// 返回 true 表示文本内容发生变化。
bool ft_text_field_key(ft_text_field_t *f, int key, int *sel)
{
    if (f == NULL) {
        return false;
    }

    if (key == 0) {
        // 上:在字符集里往回选一个字符
        ft_text_field_cycle_char(f, -1);
        if (sel != NULL) {
            *sel = (int)f->char_cursor;
        }
        return false;
    }
    if (key == 1) {
        // 下:往前往后选一个字符
        ft_text_field_cycle_char(f, +1);
        if (sel != NULL) {
            *sel = (int)f->char_cursor;
        }
        return false;
    }

    // 确定:把当前选中的字符插到光标处
    char c = ft_text_field_selected_char(f);
    if (c == '\0') {
        return false;
    }
    return ft_text_field_insert(f, c);
}

bool ft_text_field_backspace_key(ft_text_field_t *f)
{
    return ft_text_field_backspace(f);
}

bool ft_text_field_delete_key(ft_text_field_t *f)
{
    return ft_text_field_delete(f);
}

bool ft_text_field_move_left_key(ft_text_field_t *f)
{
    return ft_text_field_move_left(f);
}

bool ft_text_field_move_right_key(ft_text_field_t *f)
{
    return ft_text_field_move_right(f);
}

void ft_text_field_set_charset(ft_text_field_t *f, ft_charset_t cs)
{
    if (f == NULL) {
        return;
    }
    f->charset = cs;
    f->char_cursor = 0;
}

ft_charset_t ft_text_field_charset(const ft_text_field_t *f)
{
    return (f != NULL) ? f->charset : FT_CHARSET_LOWER;
}

bool ft_text_field_is_empty(const ft_text_field_t *f)
{
    return (f == NULL) || f->len == 0;
}
