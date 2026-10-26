// FoloTalker 多行输入表单的主机侧单元测试。纯 C,不依赖 ESP-IDF / LVGL。
#include "ft_form.h"

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
    ft_form_t f;
    const ft_form_row_kind_t rows[] = { FT_FORM_ROW_TEXT, FT_FORM_ROW_SAVE };
    ft_form_init(&f, rows, 2);

    CHECK(f.row_count == 2);
    CHECK(ft_form_current(&f) == 0);
    CHECK(!ft_form_on_save_row(&f));
    CHECK(strcmp(ft_form_value(&f, 0), "") == 0);
    return 0;
}

static int test_type_into_text_row(void)
{
    ft_form_t f;
    const ft_form_row_kind_t rows[] = { FT_FORM_ROW_TEXT, FT_FORM_ROW_SAVE };
    ft_form_init(&f, rows, 2);

    CHECK(ft_form_type(&f, 'a'));
    CHECK(ft_form_type(&f, 'b'));
    CHECK(strcmp(ft_form_value(&f, 0), "ab") == 0);
    CHECK(ft_form_current_field(&f) != NULL);
    CHECK(ft_form_current_field(&f)->len == 2);
    return 0;
}

static int test_save_row_rejects_typing(void)
{
    ft_form_t f;
    const ft_form_row_kind_t rows[] = { FT_FORM_ROW_TEXT, FT_FORM_ROW_SAVE };
    ft_form_init(&f, rows, 2);

    ft_form_move(&f, +1);
    CHECK(ft_form_on_save_row(&f));
    CHECK(!ft_form_type(&f, 'x'));
    CHECK(!ft_form_backspace(&f));
    CHECK(strcmp(ft_form_value(&f, 1), "") == 0);
    return 0;
}

static int test_move_keeps_content(void)
{
    ft_form_t f;
    const ft_form_row_kind_t rows[] = {
        FT_FORM_ROW_TEXT, FT_FORM_ROW_TEXT, FT_FORM_ROW_SAVE
    };
    ft_form_init(&f, rows, 3);

    // 第一行输入昵称
    ft_form_type(&f, 'p');
    ft_form_type(&f, 'a');
    // 切到第二行 —— 第一行的内容必须还在
    ft_form_move(&f, +1);
    CHECK(ft_form_current(&f) == 1);
    CHECK(strcmp(ft_form_value(&f, 0), "pa") == 0);

    // 第二行输入口令
    ft_form_type(&f, '1');
    ft_form_type(&f, '2');
    ft_form_type(&f, '3');
    CHECK(strcmp(ft_form_value(&f, 1), "123") == 0);

    // 切到保存行 —— 两行内容都必须保留
    ft_form_move(&f, +1);
    CHECK(ft_form_on_save_row(&f));
    CHECK(strcmp(ft_form_value(&f, 0), "pa") == 0);
    CHECK(strcmp(ft_form_value(&f, 1), "123") == 0);
    return 0;
}

static int test_move_wraps(void)
{
    ft_form_t f;
    const ft_form_row_kind_t rows[] = { FT_FORM_ROW_TEXT, FT_FORM_ROW_SAVE };
    ft_form_init(&f, rows, 2);

    // 往上从第一行绕到最后一行
    CHECK(ft_form_move(&f, -1) == 1);
    CHECK(ft_form_on_save_row(&f));
    // 往下从最后一行绕回第一行
    CHECK(ft_form_move(&f, +1) == 0);
    CHECK(!ft_form_on_save_row(&f));
    return 0;
}

static int test_move_back_keeps_content(void)
{
    ft_form_t f;
    const ft_form_row_kind_t rows[] = {
        FT_FORM_ROW_TEXT, FT_FORM_ROW_TEXT, FT_FORM_ROW_SAVE
    };
    ft_form_init(&f, rows, 3);

    ft_form_type(&f, 'x');
    ft_form_move(&f, +1);
    ft_form_type(&f, 'y');
    // 往回走,第一行的内容仍在
    ft_form_move(&f, -1);
    CHECK(ft_form_current(&f) == 0);
    CHECK(strcmp(ft_form_value(&f, 0), "x") == 0);
    CHECK(strcmp(ft_form_value(&f, 1), "y") == 0);
    return 0;
}

static int test_backspace(void)
{
    ft_form_t f;
    const ft_form_row_kind_t rows[] = { FT_FORM_ROW_TEXT, FT_FORM_ROW_SAVE };
    ft_form_init(&f, rows, 2);

    ft_form_type(&f, 'a');
    ft_form_type(&f, 'b');
    CHECK(ft_form_backspace(&f));
    CHECK(strcmp(ft_form_value(&f, 0), "a") == 0);
    CHECK(ft_form_backspace(&f));
    CHECK(strcmp(ft_form_value(&f, 0), "") == 0);
    // 空文本再退格应失败
    CHECK(!ft_form_backspace(&f));
    return 0;
}

static int test_capacity_limit(void)
{
    ft_form_t f;
    const ft_form_row_kind_t rows[] = { FT_FORM_ROW_TEXT, FT_FORM_ROW_SAVE };
    ft_form_init(&f, rows, 2);

    for (size_t i = 0; i < FT_FIELD_CAPACITY; i++) {
        bool ok = ft_form_type(&f, 'x');
        if (i < FT_FIELD_CAPACITY - 1) {
            CHECK(ok);
        } else {
            CHECK(!ok);
        }
    }
    CHECK(strlen(ft_form_value(&f, 0)) == FT_FIELD_CAPACITY - 1);
    return 0;
}

static int test_null_safety(void)
{
    ft_form_t f;
    const ft_form_row_kind_t rows[] = { FT_FORM_ROW_TEXT };
    ft_form_init(&f, rows, 1);

    CHECK(ft_form_current_field(NULL) == NULL);
    CHECK(!ft_form_type(NULL, 'x'));
    CHECK(!ft_form_backspace(NULL));
    CHECK(strcmp(ft_form_value(NULL, 0), "") == 0);
    CHECK(ft_form_current(NULL) == 0);
    CHECK(!ft_form_on_save_row(NULL));
    return 0;
}

int main(void)
{
    struct { const char *name; int (*fn)(void); } cases[] = {
        { "init",              test_init },
        { "type_text_row",     test_type_into_text_row },
        { "save_row_rejects",  test_save_row_rejects_typing },
        { "move_keeps",        test_move_keeps_content },
        { "move_wraps",        test_move_wraps },
        { "move_back_keeps",   test_move_back_keeps_content },
        { "backspace",         test_backspace },
        { "capacity_limit",    test_capacity_limit },
        { "null_safety",       test_null_safety },
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        if (cases[i].fn() != 0) {
            fprintf(stderr, "test case failed: %s\n", cases[i].name);
            return 1;
        }
        printf("ok - %s\n", cases[i].name);
    }
    printf("ft_form: all tests passed\n");
    return 0;
}
