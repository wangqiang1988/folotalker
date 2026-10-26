// FoloTalker 设置存储的主机侧单元测试。
// ft_store.c 里 NVS 相关代码用条件编译隔离:主机测试不编译 NVS 后端。
#include "ft_store.h"

#include <stdio.h>
#include <string.h>

#define CHECK(cond)                                                           \
    do {                                                                      \
        if (!(cond)) {                                                        \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            return 1;                                                         \
        }                                                                     \
    } while (0)

static int test_mem_roundtrip(void)
{
    ft_store_t *st = ft_store_new_mem();
    CHECK(st != NULL);

    // 未写入的 key 读不到
    char buf[64];
    CHECK(!ft_store_get_str(st, FT_STORE_KEY_HOST, buf, sizeof(buf)));

    CHECK(ft_store_set_str(st, FT_STORE_KEY_HOST, "talk.home"));
    CHECK(ft_store_get_str(st, FT_STORE_KEY_HOST, buf, sizeof(buf)));
    CHECK(strcmp(buf, "talk.home") == 0);

    // 覆盖写
    CHECK(ft_store_set_str(st, FT_STORE_KEY_HOST, "192.168.1.50"));
    CHECK(ft_store_get_str(st, FT_STORE_KEY_HOST, buf, sizeof(buf)));
    CHECK(strcmp(buf, "192.168.1.50") == 0);

    // 整数
    int v = -1;
    CHECK(!ft_store_get_int(st, FT_STORE_KEY_VOLUME, &v));
    CHECK(ft_store_set_int(st, FT_STORE_KEY_VOLUME, 55));
    CHECK(ft_store_get_int(st, FT_STORE_KEY_VOLUME, &v));
    CHECK(v == 55);

    // 删除
    CHECK(ft_store_erase(st, FT_STORE_KEY_HOST));
    CHECK(!ft_store_get_str(st, FT_STORE_KEY_HOST, buf, sizeof(buf)));

    ft_store_free_mem(st);
    return 0;
}

static int test_host_validation(void)
{
    CHECK(ft_host_is_valid("talk.home"));
    CHECK(ft_host_is_valid("192.168.1.50"));
    CHECK(ft_host_is_valid("192.168.1.50:8443"));
    CHECK(ft_host_is_valid("my-server"));
    CHECK(ft_host_is_valid("a"));
    CHECK(ft_host_is_valid("[::1]:8443"));

    CHECK(!ft_host_is_valid(""));
    CHECK(!ft_host_is_valid(NULL));
    CHECK(!ft_host_is_valid("talk home"));   // 空格非法
    CHECK(!ft_host_is_valid("http://talk.home"));  // 不该包含 scheme
    CHECK(!ft_host_is_valid("talk.home/x"));  // 路径不放在 host 里

    // 超长
    char longhost[300];
    memset(longhost, 'a', sizeof(longhost) - 1);
    longhost[sizeof(longhost) - 1] = '\0';
    CHECK(!ft_host_is_valid(longhost));
    return 0;
}

static int test_nick_validation(void)
{
    CHECK(ft_nick_is_valid("爸爸"));
    CHECK(ft_nick_is_valid("a"));
    CHECK(!ft_nick_is_valid(""));
    CHECK(!ft_nick_is_valid(NULL));

    char twenty[21];
    memset(twenty, 'x', 20);
    twenty[20] = '\0';
    CHECK(ft_nick_is_valid(twenty));

    char twentyone[22];
    memset(twentyone, 'x', 21);
    twentyone[21] = '\0';
    CHECK(!ft_nick_is_valid(twentyone));
    return 0;
}

static int test_code_validation(void)
{
    CHECK(ft_code_is_valid("1234"));
    CHECK(ft_code_is_valid("family-code"));
    CHECK(!ft_code_is_valid("123"));      // 服务端要求至少 4 个字符
    CHECK(!ft_code_is_valid(""));
    CHECK(!ft_code_is_valid(NULL));
    return 0;
}

static int test_ws_url(void)
{
    char buf[256];
    size_t n;

    n = ft_build_ws_url(buf, sizeof(buf), "talk.home", 0, NULL);
    CHECK(n > 0);
    CHECK(strcmp(buf, "wss://talk.home/ws") == 0);

    n = ft_build_ws_url(buf, sizeof(buf), "talk.home", 8443, NULL);
    CHECK(strcmp(buf, "wss://talk.home:8443/ws") == 0);

    n = ft_build_ws_url(buf, sizeof(buf), "192.168.1.50", 443, "/ws");
    CHECK(strcmp(buf, "wss://192.168.1.50:443/ws") == 0);

    n = ft_build_ws_url(buf, sizeof(buf), "talk.home", -1, NULL);
    CHECK(strcmp(buf, "wss://talk.home/ws") == 0);

    // 缓冲区不足
    char small[8];
    CHECK(ft_build_ws_url(small, sizeof(small), "talk.home", 0, NULL) == 0);
    CHECK(small[0] == '\0');

    // 空 host
    CHECK(ft_build_ws_url(buf, sizeof(buf), "", 0, NULL) == 0);
    return 0;
}

int main(void)
{
    struct { const char *name; int (*fn)(void); } cases[] = {
        { "mem_roundtrip",  test_mem_roundtrip },
        { "host_validation",test_host_validation },
        { "nick_validation",test_nick_validation },
        { "code_validation",test_code_validation },
        { "ws_url",         test_ws_url },
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        if (cases[i].fn() != 0) {
            fprintf(stderr, "test case failed: %s\n", cases[i].name);
            return 1;
        }
        printf("ok - %s\n", cases[i].name);
    }
    printf("ft_store: all tests passed\n");
    return 0;
}
