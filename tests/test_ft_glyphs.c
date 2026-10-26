// FoloTalker 字形覆盖检查逻辑的主机侧单元测试。
// 用一个假的字体查表回调验证检查逻辑本身,不依赖 LVGL。
#include "ft_glyphs.h"

#include <stdio.h>
#include <string.h>

#define CHECK(cond)                                                           \
    do {                                                                      \
        if (!(cond)) {                                                        \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            return 1;                                                         \
        }                                                                     \
    } while (0)

// 假的字体:只覆盖 ASCII 与一小批汉字。
static bool fake_query(void *user, uint32_t cp)
{
    (void)user;
    if (cp < 0x80) {
        return true;   // ASCII 全覆盖
    }
    // 只覆盖"中""文""对""讲"
    return cp == 0x4E2D || cp == 0x6587 || cp == 0x5BF9 || cp == 0x8BB2;
}

static int test_utf8_decode(void)
{
    uint32_t cps[16];
    size_t n;

    n = ft_utf8_decode("ABC", cps, 16);
    CHECK(n == 3);
    CHECK(cps[0] == 'A' && cps[1] == 'B' && cps[2] == 'C');

    n = ft_utf8_decode("\xE4\xB8\xAD", cps, 16);   // "中"
    CHECK(n == 1);
    CHECK(cps[0] == 0x4E2D);

    n = ft_utf8_decode("\xE4\xB8\xAD\xE6\x96\x87", cps, 16);  // "中文"
    CHECK(n == 2);
    CHECK(cps[0] == 0x4E2D);
    CHECK(cps[1] == 0x6587);

    // 四字节字符(不在 BMP,但解码器应能正确处理)
    n = ft_utf8_decode("\xF0\x9F\x98\x80", cps, 16);   // U+1F600
    CHECK(n == 1);
    CHECK(cps[0] == 0x1F600);

    // 空串
    CHECK(ft_utf8_decode("", cps, 16) == 0);
    CHECK(ft_utf8_decode(NULL, cps, 16) == 0);

    // 容量不足时返回容量
    uint32_t few[2];
    CHECK(ft_utf8_decode("ABCDE", few, 2) == 2);
    return 0;
}

static int test_coverage_all_present(void)
{
    size_t missing = ft_font_check_coverage("中文对讲", fake_query, NULL, NULL, 0);
    CHECK(missing == 0);

    // ASCII 全部覆盖
    missing = ft_font_check_coverage("Hello, World! 123", fake_query, NULL, NULL, 0);
    CHECK(missing == 0);
    return 0;
}

static int test_coverage_detects_missing(void)
{
    // "中"和"文"有,"语"没有
    size_t missing = ft_font_check_coverage("中文语", fake_query, NULL, NULL, 0);
    CHECK(missing == 1);

    // "语"是 U+8BED
    ft_glyph_check_t results[8];
    missing = ft_font_check_coverage("中文语", fake_query, NULL, results, 8);
    CHECK(missing == 1);
    CHECK(results[0].codepoint == 0x4E2D && results[0].covered);
    CHECK(results[1].codepoint == 0x6587 && results[1].covered);
    CHECK(results[2].codepoint == 0x8BED && !results[2].covered);
    return 0;
}

static int test_coverage_results_capped(void)
{
    // 结果数组容量小于码点数时,只写前 capacity 条,但缺失计数仍要准确。
    ft_glyph_check_t results[2];
    size_t missing = ft_font_check_coverage("中文语中文语", fake_query, NULL, results, 2);
    CHECK(missing == 2);   // 两个"语"
    CHECK(results[0].codepoint == 0x4E2D);
    CHECK(results[1].codepoint == 0x6587);
    return 0;
}

static int test_control_chars_skipped(void)
{
    // 换行与制表符不应被当作缺字
    size_t missing = ft_font_check_coverage("中\n文\t", fake_query, NULL, NULL, 0);
    CHECK(missing == 0);
    return 0;
}

static int test_null_safety(void)
{
    CHECK(ft_font_check_coverage(NULL, fake_query, NULL, NULL, 0) == 0);
    CHECK(ft_font_check_coverage("中", NULL, NULL, NULL, 0) == 0);
    return 0;
}

static int test_invalid_utf8_does_not_hang(void)
{
    // 非法字节序列:解码器必须跳过而不是死循环
    uint32_t cps[8];
    size_t n = ft_utf8_decode("\xFF\xFE中", cps, 8);
    CHECK(n == 1);
    CHECK(cps[0] == 0x4E2D);

    // 截断的三字节序列
    n = ft_utf8_decode("\xE4\xB8", cps, 8);
    CHECK(n <= 1);
    return 0;
}

int main(void)
{
    struct { const char *name; int (*fn)(void); } cases[] = {
        { "utf8_decode",           test_utf8_decode },
        { "coverage_all_present",  test_coverage_all_present },
        { "coverage_detects",      test_coverage_detects_missing },
        { "coverage_results_capped", test_coverage_results_capped },
        { "control_chars_skipped", test_control_chars_skipped },
        { "null_safety",           test_null_safety },
        { "invalid_utf8",          test_invalid_utf8_does_not_hang },
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        if (cases[i].fn() != 0) {
            fprintf(stderr, "test case failed: %s\n", cases[i].name);
            return 1;
        }
        printf("ok - %s\n", cases[i].name);
    }
    printf("ft_glyphs: all tests passed\n");
    return 0;
}
