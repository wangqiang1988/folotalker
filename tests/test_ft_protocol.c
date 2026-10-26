// FoloTalker 协议的主机侧单元测试。纯 C,不依赖 ESP-IDF / LVGL。
#include "ft_protocol.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define CHECK(cond)                                                           \
    do {                                                                      \
        if (!(cond)) {                                                        \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            return 1;                                                         \
        }                                                                     \
    } while (0)

static int test_build_messages(void)
{
    char buf[512];
    size_t n;

    n = ft_build_join(buf, sizeof(buf), "妈妈", "1234");
    CHECK(n > 0);
    CHECK(strcmp(buf, "{\"t\":\"join\",\"name\":\"妈妈\",\"code\":\"1234\"}") == 0);

    n = ft_build_speak(buf, sizeof(buf), true);
    CHECK(n > 0);
    CHECK(strcmp(buf, "{\"t\":\"speak\",\"on\":true}") == 0);

    n = ft_build_speak(buf, sizeof(buf), false);
    CHECK(strcmp(buf, "{\"t\":\"speak\",\"on\":false}") == 0);

    n = ft_build_mute(buf, sizeof(buf), true);
    CHECK(strcmp(buf, "{\"t\":\"mute\",\"on\":true}") == 0);

    n = ft_build_ping(buf, sizeof(buf));
    CHECK(strcmp(buf, "{\"t\":\"ping\"}") == 0);

    // 名称里带引号/反斜杠必须被转义,否则会破坏 JSON 结构
    n = ft_build_join(buf, sizeof(buf), "a\"b\\c", "x");
    CHECK(n > 0);
    CHECK(strstr(buf, "a\\\"b\\\\c") != NULL);

    // 缓冲区不足时返回 0,不产生半截消息
    char small[8];
    CHECK(ft_build_join(small, sizeof(small), "昵称很长很长", "1234") == 0);
    CHECK(small[0] == '\0');

    return 0;
}

static int test_parse_welcome(void)
{
    const char *json =
        "{\"t\":\"welcome\",\"you\":{\"id\":\"abc-123\",\"name\":\"爸爸\"},"
        "\"room\":{\"name\":\"home\"},"
        "\"members\":[{\"id\":\"abc-123\",\"name\":\"爸爸\",\"muted\":false,\"speaking\":false},"
        "{\"id\":\"def-456\",\"name\":\"妈妈\",\"muted\":true,\"speaking\":true}],"
        "\"speaker\":{\"id\":\"def-456\",\"name\":\"妈妈\"},"
        "\"cfg\":{\"sampleRate\":16000,\"frameMs\":20,\"maxSpeakMs\":60000,"
        "\"halfDuplex\":true,\"speakMode\":\"preempt\"}}";

    ft_event_t ev;
    CHECK(ft_parse_event(json, strlen(json), &ev));
    CHECK(ev.kind == FT_EV_WELCOME);
    CHECK(strcmp(ev.self_id, "abc-123") == 0);
    CHECK(strcmp(ev.self_name, "爸爸") == 0);
    CHECK(strcmp(ev.room_name, "home") == 0);
    CHECK(ev.member_count == 2);
    CHECK(strcmp(ev.members[0].name, "爸爸") == 0);
    CHECK(ev.members[0].speaking == false);
    CHECK(ev.members[1].muted == true);
    CHECK(ev.members[1].speaking == true);
    CHECK(ev.has_speaker);
    CHECK(strcmp(ev.speaker_name, "妈妈") == 0);
    CHECK(ev.cfg.sample_rate == 16000);
    CHECK(ev.cfg.frame_ms == 20);
    CHECK(ev.cfg.max_speak_ms == 60000);
    CHECK(ev.cfg.half_duplex == true);
    CHECK(ev.cfg.exclusive == false);

    // speaker 为 null
    const char *json2 =
        "{\"t\":\"welcome\",\"you\":{\"id\":\"a\",\"name\":\"x\"},"
        "\"room\":{\"name\":\"home\"},\"members\":[],\"speaker\":null,"
        "\"cfg\":{\"sampleRate\":16000,\"frameMs\":20,\"maxSpeakMs\":60000,"
        "\"halfDuplex\":true,\"speakMode\":\"exclusive\"}}";
    CHECK(ft_parse_event(json2, strlen(json2), &ev));
    CHECK(ev.has_speaker == false);
    CHECK(ev.member_count == 0);
    CHECK(ev.cfg.exclusive == true);

    return 0;
}

static int test_parse_members(void)
{
    const char *json =
        "{\"t\":\"members\",\"members\":["
        "{\"id\":\"1\",\"name\":\"奶奶\",\"muted\":false,\"speaking\":false},"
        "{\"id\":\"2\",\"name\":\"爷爷\",\"muted\":false,\"speaking\":false}]}";
    ft_event_t ev;
    CHECK(ft_parse_event(json, strlen(json), &ev));
    CHECK(ev.kind == FT_EV_MEMBERS);
    CHECK(ev.member_count == 2);
    CHECK(strcmp(ev.members[1].name, "爷爷") == 0);

    // 空数组
    const char *empty = "{\"t\":\"members\",\"members\":[]}";
    CHECK(ft_parse_event(empty, strlen(empty), &ev));
    CHECK(ev.kind == FT_EV_MEMBERS);
    CHECK(ev.member_count == 0);

    return 0;
}

static int test_parse_speaker(void)
{
    const char *json = "{\"t\":\"speaker\",\"id\":\"u1\",\"name\":\"哥哥\"}";
    ft_event_t ev;
    CHECK(ft_parse_event(json, strlen(json), &ev));
    CHECK(ev.kind == FT_EV_SPEAKER);
    CHECK(ev.has_speaker);
    CHECK(strcmp(ev.speaker_name, "哥哥") == 0);

    // 放麦: id 为 null
    const char *json2 = "{\"t\":\"speaker\",\"id\":null,\"name\":null}";
    CHECK(ft_parse_event(json2, strlen(json2), &ev));
    CHECK(ev.kind == FT_EV_SPEAKER);
    CHECK(ev.has_speaker == false);

    return 0;
}

static int test_parse_error(void)
{
    const char *json = "{\"t\":\"error\",\"code\":\"BAD_CODE\",\"message\":\"家庭口令不正确\"}";
    ft_event_t ev;
    CHECK(ft_parse_event(json, strlen(json), &ev));
    CHECK(ev.kind == FT_EV_ERROR);
    CHECK(ev.error_code == FT_ERR_BAD_CODE);
    CHECK(strcmp(ev.error_message, "家庭口令不正确") == 0);

    const char *busy = "{\"t\":\"error\",\"code\":\"FLOOR_BUSY\",\"message\":\"爸爸 正在说话\"}";
    CHECK(ft_parse_event(busy, strlen(busy), &ev));
    CHECK(ev.error_code == FT_ERR_FLOOR_BUSY);

    const char *pong = "{\"t\":\"error\",\"code\":\"PONG\",\"message\":\"\"}";
    CHECK(ft_parse_event(pong, strlen(pong), &ev));
    CHECK(ev.error_code == FT_ERR_PONG);

    // 未知错误码映射到 OTHER
    const char *other = "{\"t\":\"error\",\"code\":\"WHAT_IS_THIS\",\"message\":\"?\"}";
    CHECK(ft_parse_event(other, strlen(other), &ev));
    CHECK(ev.error_code == FT_ERR_OTHER);

    return 0;
}

static int test_parse_rejects_garbage(void)
{
    ft_event_t ev;
    CHECK(ft_parse_event("not json", 8, &ev) == false);
    CHECK(ft_parse_event("", 0, &ev) == false);
    CHECK(ft_parse_event("{\"foo\":1}", 9, &ev) == false);
    CHECK(ft_parse_event("{\"t\":\"unknown_type\"}", 20, &ev) == false);
    CHECK(ft_parse_event(NULL, 4, &ev) == false);

    return 0;
}

static int test_escaped_names(void)
{
    // 成员名里含引号与反斜杠,解析后应还原
    const char *json =
        "{\"t\":\"members\",\"members\":[{\"id\":\"1\",\"name\":\"a\\\"b\",\"muted\":false,"
        "\"speaking\":false}]}";
    ft_event_t ev;
    CHECK(ft_parse_event(json, strlen(json), &ev));
    CHECK(strcmp(ev.members[0].name, "a\"b") == 0);

    return 0;
}

static int test_frame_constants(void)
{
    // 音频帧参数必须与服务端 web/src/audio/audio.ts 一致
    CHECK(FT_SAMPLE_RATE == 16000);
    CHECK(FT_FRAME_SAMPLES == 320);
    CHECK(FT_FRAME_BYTES == 640);
    return 0;
}

static int test_error_text(void)
{
    CHECK(strcmp(ft_error_text(FT_ERR_BAD_CODE), "家庭口令不正确") == 0);
    CHECK(strcmp(ft_error_text(FT_ERR_FLOOR_BUSY), "麦被占用,请稍候") == 0);
    CHECK(strcmp(ft_conn_state_text(FT_CONN_ONLINE), "在线") == 0);
    return 0;
}

int main(void)
{
    struct { const char *name; int (*fn)(void); } cases[] = {
        { "build_messages",   test_build_messages },
        { "parse_welcome",    test_parse_welcome },
        { "parse_members",    test_parse_members },
        { "parse_speaker",    test_parse_speaker },
        { "parse_error",      test_parse_error },
        { "parse_rejects",    test_parse_rejects_garbage },
        { "escaped_names",    test_escaped_names },
        { "frame_constants",  test_frame_constants },
        { "error_text",       test_error_text },
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        if (cases[i].fn() != 0) {
            fprintf(stderr, "test case failed: %s\n", cases[i].name);
            return 1;
        }
        printf("ok - %s\n", cases[i].name);
    }
    printf("ft_protocol: all tests passed\n");
    return 0;
}
