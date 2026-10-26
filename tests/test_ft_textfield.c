// FoloTalker 屏幕文本输入的主机侧单元测试。纯 C,不依赖 ESP-IDF / LVGL。
#include "ft_textfield.h"

#include <stdio.h>
#include <string.h>

#define CHECK(cond)                                                           \
    do {                                                                      \
        if (!(cond)) {                                                        \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            return 1;                                                         \
        }                                                                     \
    } while (0)

static int test_init(void)
{
    ft_text_field_t f;
    ft_text_field_init(&f, "昵称", false, "请输入昵称");
    CHECK(f.len == 0);
    CHECK(f.cursor == 0);
    CHECK(f.text[0] == '\0');
    CHECK(ft_text_field_is_empty(&f));
    CHECK(f.charset == FT_CHARSET_LOWER);
    CHECK(strcmp(f.title, "昵称") == 0);

    // 数字输入框默认落在数字集
    ft_text_field_init(&f, "口令", true, "至少4位");
    CHECK(f.charset == FT_CHARSET_DIGITS);
    return 0;
}

static int test_type_characters(void)
{
    ft_text_field_t f;
    ft_text_field_init(&f, "昵称", false, NULL);

    // 默认选中字符集的第一个字符 'a'
    CHECK(ft_text_field_selected_char(&f) == 'a');

    // 按确定插入
    CHECK(ft_text_field_key(&f, 2, NULL));
    CHECK(strcmp(f.text, "a") == 0);
    CHECK(f.len == 1);
    CHECK(f.cursor == 1);

    // 下键选下一个字符 'b'
    ft_text_field_key(&f, 1, NULL);
    CHECK(ft_text_field_selected_char(&f) == 'b');
    CHECK(ft_text_field_key(&f, 2, NULL));
    CHECK(strcmp(f.text, "ab") == 0);

    // 上键回到 'a'
    ft_text_field_key(&f, 0, NULL);
    CHECK(ft_text_field_selected_char(&f) == 'a');
    CHECK(ft_text_field_key(&f, 2, NULL));
    CHECK(strcmp(f.text, "aba") == 0);
    return 0;
}

static int test_charset_wraparound(void)
{
    ft_text_field_t f;
    ft_text_field_init(&f, "x", false, NULL);

    // 向上从 'a' 回绕到 'z'
    ft_text_field_key(&f, 0, NULL);
    CHECK(ft_text_field_selected_char(&f) == 'z');

    // 向下从 'z' 回绕到 'a'
    ft_text_field_key(&f, 1, NULL);
    CHECK(ft_text_field_selected_char(&f) == 'a');
    return 0;
}

static int test_switch_charset(void)
{
    ft_text_field_t f;
    ft_text_field_init(&f, "x", false, NULL);

    ft_text_field_set_charset(&f, FT_CHARSET_DIGITS);
    CHECK(ft_text_field_charset(&f) == FT_CHARSET_DIGITS);
    CHECK(ft_text_field_selected_char(&f) == '0');
    CHECK(ft_text_field_key(&f, 2, NULL));
    CHECK(strcmp(f.text, "0") == 0);

    ft_text_field_set_charset(&f, FT_CHARSET_UPPER);
    CHECK(ft_text_field_selected_char(&f) == 'A');
    CHECK(ft_text_field_key(&f, 2, NULL));
    CHECK(strcmp(f.text, "0A") == 0);

    ft_text_field_set_charset(&f, FT_CHARSET_SYMBOLS);
    CHECK(ft_text_field_selected_char(&f) == '.');
    CHECK(ft_text_field_key(&f, 2, NULL));
    CHECK(strcmp(f.text, "0A.") == 0);
    return 0;
}

static int test_backspace(void)
{
    ft_text_field_t f;
    ft_text_field_init(&f, "x", false, NULL);
    ft_text_field_set_charset(&f, FT_CHARSET_DIGITS);

    // 空文本退格应失败
    CHECK(!ft_text_field_backspace_key(&f));

    ft_text_field_key(&f, 2, NULL);   // '0'
    ft_text_field_key(&f, 1, NULL);   // 选 '1'
    ft_text_field_key(&f, 2, NULL);   // 插入 '1'
    CHECK(strcmp(f.text, "01") == 0);

    CHECK(ft_text_field_backspace_key(&f));
    CHECK(strcmp(f.text, "0") == 0);
    CHECK(f.len == 1);
    CHECK(f.cursor == 1);

    CHECK(ft_text_field_backspace_key(&f));
    CHECK(strcmp(f.text, "") == 0);
    CHECK(f.len == 0);
    return 0;
}

static int test_cursor_movement(void)
{
    ft_text_field_t f;
    ft_text_field_init(&f, "x", false, NULL);
    ft_text_field_set_charset(&f, FT_CHARSET_DIGITS);

    ft_text_field_key(&f, 2, NULL);   // '0'
    ft_text_field_key(&f, 1, NULL);   // '1'
    ft_text_field_key(&f, 2, NULL);   // '1'  => "01"

    CHECK(ft_text_field_move_left_key(&f));
    CHECK(f.cursor == 1);
    CHECK(ft_text_field_move_left_key(&f));
    CHECK(f.cursor == 0);
    CHECK(!ft_text_field_move_left_key(&f));   // 已在最左

    CHECK(ft_text_field_move_right_key(&f));
    CHECK(f.cursor == 1);
    CHECK(ft_text_field_move_right_key(&f));
    CHECK(f.cursor == 2);
    CHECK(!ft_text_field_move_right_key(&f));  // 已在最右
    return 0;
}

static int test_insert_at_cursor(void)
{
    ft_text_field_t f;
    ft_text_field_init(&f, "x", false, NULL);
    ft_text_field_set_charset(&f, FT_CHARSET_DIGITS);

    ft_text_field_key(&f, 2, NULL);   // '0'
    ft_text_field_key(&f, 1, NULL);   // '1'
    ft_text_field_key(&f, 2, NULL);   // '1' => "01"

    // 把光标移到中间再插入
    ft_text_field_move_left_key(&f);
    // 切换字符集会把选中游标重置到集首('0'),往上一次到 '9'
    ft_text_field_set_charset(&f, FT_CHARSET_DIGITS);
    CHECK(ft_text_field_selected_char(&f) == '0');
    ft_text_field_key(&f, 0, NULL);
    CHECK(ft_text_field_selected_char(&f) == '9');
    CHECK(ft_text_field_key(&f, 2, NULL));
    CHECK(strcmp(f.text, "091") == 0);
    CHECK(f.len == 3);
    return 0;
}

static int test_capacity_limit(void)
{
    ft_text_field_t f;
    ft_text_field_init(&f, "x", false, NULL);
    ft_text_field_set_charset(&f, FT_CHARSET_DIGITS);

    // 填到容量上限
    for (int i = 0; i < FT_FIELD_CAPACITY; i++) {
        bool ok = ft_text_field_key(&f, 2, NULL);
        if (i < FT_FIELD_CAPACITY - 1) {
            CHECK(ok);
        } else {
            // 超过容量的那一次必须被拒绝
            CHECK(!ok);
        }
    }
    CHECK(f.len == FT_FIELD_CAPACITY - 1);
    CHECK(f.text[FT_FIELD_CAPACITY - 1] == '\0');
    return 0;
}

static int test_clear(void)
{
    ft_text_field_t f;
    ft_text_field_init(&f, "x", false, NULL);
    ft_text_field_key(&f, 2, NULL);
    CHECK(!ft_text_field_is_empty(&f));
    ft_text_field_clear(&f);
    CHECK(ft_text_field_is_empty(&f));
    CHECK(f.cursor == 0);
    CHECK(f.text[0] == '\0');
    return 0;
}

static int test_charsets(void)
{
    CHECK(strchr(ft_charset_chars(FT_CHARSET_LOWER), 'a') != NULL);
    CHECK(strchr(ft_charset_chars(FT_CHARSET_UPPER), 'Z') != NULL);
    CHECK(strchr(ft_charset_chars(FT_CHARSET_DIGITS), '7') != NULL);
    CHECK(strchr(ft_charset_chars(FT_CHARSET_SYMBOLS), '.') != NULL);
    CHECK(strcmp(ft_charset_name(FT_CHARSET_LOWER), "小写") == 0);
    CHECK(strcmp(ft_charset_name(FT_CHARSET_SYMBOLS), "符号") == 0);
    return 0;
}

static int test_delete_key(void)
{
    ft_text_field_t f;
    ft_text_field_init(&f, "x", false, NULL);
    ft_text_field_set_charset(&f, FT_CHARSET_DIGITS);
    ft_text_field_key(&f, 2, NULL);   // '0'
    ft_text_field_key(&f, 1, NULL);   // '1'
    ft_text_field_key(&f, 2, NULL);   // '1' => "01"

    ft_text_field_move_left_key(&f);  // 光标在 index 1,指向 '1'
    CHECK(f.cursor == 1);
    CHECK(ft_text_field_delete_key(&f));
    CHECK(strcmp(f.text, "0") == 0);   // 删掉了 '1'
    CHECK(f.len == 1);
    CHECK(f.cursor == 1);
    return 0;
}

int main(void)
{
    struct { const char *name; int (*fn)(void); } cases[] = {
        { "init",             test_init },
        { "type_characters",  test_type_characters },
        { "charset_wraparound", test_charset_wraparound },
        { "switch_charset",   test_switch_charset },
        { "backspace",        test_backspace },
        { "cursor_movement",  test_cursor_movement },
        { "insert_at_cursor", test_insert_at_cursor },
        { "capacity_limit",   test_capacity_limit },
        { "clear",            test_clear },
        { "charsets",         test_charsets },
        { "delete_key",       test_delete_key },
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        if (cases[i].fn() != 0) {
            fprintf(stderr, "test case failed: %s\n", cases[i].name);
            return 1;
        }
        printf("ok - %s\n", cases[i].name);
    }
    printf("ft_textfield: all tests passed\n");
    return 0;
}
