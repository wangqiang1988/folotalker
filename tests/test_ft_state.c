// FoloTalker 应用状态机的主机侧单元测试。纯 C,不依赖 ESP-IDF / LVGL。
#include "ft_state.h"

#include <stdio.h>
#include <string.h>

#define CHECK(cond)                                                           \
    do {                                                                      \
        if (!(cond)) {                                                        \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            return 1;                                                         \
        }                                                                     \
    } while (0)

static ft_event_t make_welcome(const char *self_id, const char *self_name)
{
    ft_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.kind = FT_EV_WELCOME;
    strncpy(ev.self_id, self_id, sizeof(ev.self_id) - 1);
    strncpy(ev.self_name, self_name, sizeof(ev.self_name) - 1);
    strncpy(ev.room_name, "home", sizeof(ev.room_name) - 1);
    ev.cfg.sample_rate = 16000;
    ev.cfg.frame_ms = 20;
    ev.cfg.max_speak_ms = 60000;
    ev.cfg.half_duplex = true;
    ev.cfg.exclusive = false;
    ev.member_count = 2;
    strcpy(ev.members[0].id, "me");
    strcpy(ev.members[0].name, "我");
    strcpy(ev.members[1].id, "peer");
    strcpy(ev.members[1].name, "爸爸");
    return ev;
}

static int test_init(void)
{
    ft_state_t st;
    ft_state_init(&st);
    CHECK(st.conn == FT_CONN_IDLE);
    CHECK(st.page == FT_PAGE_BOOT);
    CHECK(st.speak == FT_SPEAK_IDLE);
    CHECK(st.member_count == 0);
    CHECK(st.backoff_ms == FT_BACKOFF_MIN_MS);
    CHECK(st.cfg.sample_rate == 16000);
    CHECK(st.cfg.frame_ms == 20);
    return 0;
}

static int test_welcome_enters_room(void)
{
    ft_state_t st;
    ft_state_init(&st);
    ft_event_t ev = make_welcome("me", "我");

    CHECK(ft_state_on_event(&st, &ev));
    CHECK(st.conn == FT_CONN_ONLINE);
    CHECK(st.page == FT_PAGE_ROOM);
    CHECK(strcmp(st.self_id, "me") == 0);
    CHECK(strcmp(st.room_name, "home") == 0);
    CHECK(st.member_count == 2);
    CHECK(ft_state_peer_count(&st) == 1);   // 不含自己
    return 0;
}

static int test_speak_lifecycle(void)
{
    ft_state_t st;
    ft_state_init(&st);
    ft_event_t ev = make_welcome("me", "我");
    ft_state_on_event(&st, &ev);

    // 请求说话 -> 应发 speak on
    CHECK(ft_state_request_speak(&st) == FT_ACTION_SEND_SPEAK_ON);
    CHECK(st.speak == FT_SPEAK_REQUESTED);

    // 还没拿到麦,重复请求不应重复发
    CHECK(ft_state_request_speak(&st) == FT_ACTION_NONE);

    // 服务端确认麦归自己 -> ACTIVE
    ft_event_t sp;
    memset(&sp, 0, sizeof(sp));
    sp.kind = FT_EV_SPEAKER;
    sp.has_speaker = true;
    strcpy(sp.speaker_id, "me");
    strcpy(sp.speaker_name, "我");
    ft_state_on_event(&st, &sp);
    CHECK(st.speak == FT_SPEAK_ACTIVE);
    CHECK(ft_state_has_floor(&st));

    // 松手 -> 发 speak off
    CHECK(ft_state_release_speak(&st) == FT_ACTION_SEND_SPEAK_OFF);
    CHECK(st.speak == FT_SPEAK_IDLE);

    // 空闲时松手不应发任何东西
    CHECK(ft_state_release_speak(&st) == FT_ACTION_NONE);
    return 0;
}

static int test_speak_preempted_by_peer(void)
{
    ft_state_t st;
    ft_state_init(&st);
    ft_event_t ev = make_welcome("me", "我");
    ft_state_on_event(&st, &ev);

    ft_state_request_speak(&st);

    // 服务端把麦给了别人 -> 本地必须复位,否则会一直空发音频
    ft_event_t sp;
    memset(&sp, 0, sizeof(sp));
    sp.kind = FT_EV_SPEAKER;
    sp.has_speaker = true;
    strcpy(sp.speaker_id, "peer");
    strcpy(sp.speaker_name, "爸爸");
    ft_state_on_event(&st, &sp);
    CHECK(st.speak == FT_SPEAK_IDLE);
    CHECK(st.remote_speaking);
    CHECK(strcmp(st.remote_speaker, "爸爸") == 0);
    CHECK(!ft_state_has_floor(&st));

    // 再按住仍然可以要麦(preempt 模式)
    CHECK(ft_state_request_speak(&st) == FT_ACTION_SEND_SPEAK_ON);
    return 0;
}

static int test_server_releases_floor(void)
{
    ft_state_t st;
    ft_state_init(&st);
    ft_event_t ev = make_welcome("me", "我");
    ft_state_on_event(&st, &ev);

    // 真实协议顺序:先请求,服务端再把麦指派回来
    ft_state_request_speak(&st);
    ft_event_t sp;
    memset(&sp, 0, sizeof(sp));
    sp.kind = FT_EV_SPEAKER;
    sp.has_speaker = true;
    strcpy(sp.speaker_id, "me");
    ft_state_on_event(&st, &sp);
    CHECK(st.speak == FT_SPEAK_ACTIVE);

    // 服务端超时放麦
    sp.has_speaker = false;
    sp.speaker_id[0] = '\0';
    ft_state_on_event(&st, &sp);
    CHECK(st.speak == FT_SPEAK_IDLE);
    CHECK(!st.remote_speaking);
    return 0;
}

static int test_mute_blocks_speak(void)
{
    ft_state_t st;
    ft_state_init(&st);
    ft_event_t ev = make_welcome("me", "我");
    ft_state_on_event(&st, &ev);

    CHECK(ft_state_toggle_mute(&st) == FT_ACTION_SEND_MUTE);
    CHECK(st.mic_muted);
    CHECK(!ft_state_can_speak(&st));
    CHECK(ft_state_request_speak(&st) == FT_ACTION_NONE);

    CHECK(ft_state_toggle_mute(&st) == FT_ACTION_SEND_MUTE);
    CHECK(!st.mic_muted);
    CHECK(ft_state_can_speak(&st));
    return 0;
}

static int test_bad_code_stops_reconnect(void)
{
    ft_state_t st;
    ft_state_init(&st);
    ft_event_t ev = make_welcome("me", "我");
    ft_state_on_event(&st, &ev);

    ft_event_t err;
    memset(&err, 0, sizeof(err));
    err.kind = FT_EV_ERROR;
    err.error_code = FT_ERR_BAD_CODE;
    strcpy(err.error_message, "家庭口令不正确");
    ft_state_on_event(&st, &err);

    // 口令错误 => 停在门禁页,不自动重连
    CHECK(st.conn == FT_CONN_FAILED);
    CHECK(st.page == FT_PAGE_GATE);
    CHECK(!st.retry_pending);
    CHECK(st.last_error == FT_ERR_BAD_CODE);
    CHECK(strcmp(st.last_error_text, "家庭口令不正确") == 0);

    // 掉线也不应该触发重连
    ft_state_on_disconnect(&st, false);
    CHECK(!st.retry_pending);
    CHECK(st.conn == FT_CONN_FAILED);
    return 0;
}

static int test_floor_busy_denied(void)
{
    ft_state_t st;
    ft_state_init(&st);
    ft_event_t ev = make_welcome("me", "我");
    ft_state_on_event(&st, &ev);

    ft_event_t err;
    memset(&err, 0, sizeof(err));
    err.kind = FT_EV_ERROR;
    err.error_code = FT_ERR_FLOOR_BUSY;
    ft_state_on_event(&st, &err);
    CHECK(st.speak == FT_SPEAK_DENIED);
    // 不能因为被拒就断开
    CHECK(st.conn == FT_CONN_ONLINE);
    return 0;
}

static int test_reconnect_backoff(void)
{
    ft_state_t st;
    ft_state_init(&st);
    ft_event_t ev = make_welcome("me", "我");
    ft_state_on_event(&st, &ev);

    // 先把时间基准推进到 1000
    ft_state_tick(&st, 1000);
    ft_state_on_disconnect(&st, false);
    CHECK(st.conn == FT_CONN_RECONNECTING);
    CHECK(st.retry_pending);
    // 退避窗口从 1000 起算 1000ms => 2000 时可重连
    CHECK(st.retry_deadline_ms == 2000);

    // 还没到退避时间,不应重连
    CHECK(!ft_state_tick(&st, 1000));
    CHECK(!ft_state_tick(&st, 1500));

    // 到达退避时间 => 触发重连,退避翻倍
    CHECK(ft_state_tick(&st, 2000));
    CHECK(!st.retry_pending);
    CHECK(st.backoff_ms == 2000);

    // 退避依次翻倍并封顶 10s
    ft_state_on_disconnect(&st, false);
    CHECK(st.retry_deadline_ms == 2000 + 2000);
    CHECK(ft_state_tick(&st, 4000));
    CHECK(st.backoff_ms == 4000);

    ft_state_on_disconnect(&st, false);
    CHECK(ft_state_tick(&st, 8000));
    CHECK(st.backoff_ms == 8000);

    ft_state_on_disconnect(&st, false);
    CHECK(ft_state_tick(&st, 16000));
    CHECK(st.backoff_ms == 10000);   // 封顶,不再翻倍

    ft_state_on_disconnect(&st, false);
    CHECK(ft_state_tick(&st, 26000));
    CHECK(st.backoff_ms == 10000);
    return 0;
}

static int test_manual_disconnect_no_retry(void)
{
    ft_state_t st;
    ft_state_init(&st);
    ft_event_t ev = make_welcome("me", "我");
    ft_state_on_event(&st, &ev);

    ft_state_on_disconnect(&st, true);
    CHECK(st.conn == FT_CONN_IDLE);
    CHECK(!st.retry_pending);
    CHECK(st.speak == FT_SPEAK_IDLE);
    return 0;
}

static int test_member_lookup(void)
{
    ft_state_t st;
    ft_state_init(&st);
    ft_event_t ev = make_welcome("me", "我");
    ft_state_on_event(&st, &ev);

    const ft_member_t *m = ft_state_find_member(&st, "peer");
    CHECK(m != NULL);
    CHECK(strcmp(m->name, "爸爸") == 0);
    CHECK(ft_state_find_member(&st, "nobody") == NULL);
    CHECK(ft_state_find_member(&st, NULL) == NULL);
    return 0;
}

static int test_members_update(void)
{
    ft_state_t st;
    ft_state_init(&st);
    ft_event_t ev = make_welcome("me", "我");
    ft_state_on_event(&st, &ev);
    CHECK(st.member_count == 2);

    ft_event_t mem;
    memset(&mem, 0, sizeof(mem));
    mem.kind = FT_EV_MEMBERS;
    mem.member_count = 3;
    strcpy(mem.members[0].id, "me");
    strcpy(mem.members[0].name, "我");
    strcpy(mem.members[1].id, "a");
    strcpy(mem.members[1].name, "奶奶");
    strcpy(mem.members[2].id, "b");
    strcpy(mem.members[2].name, "爷爷");
    ft_state_on_event(&st, &mem);
    CHECK(st.member_count == 3);
    CHECK(ft_state_peer_count(&st) == 2);
    return 0;
}

static int test_member_overflow_capped(void)
{
    ft_state_t st;
    ft_state_init(&st);

    // 服务端返回的成员数超过本地上限时,解析层必须把数量截到 FT_MAX_MEMBERS。
    // 这里构造一个真实的长 JSON 文本,走完整的 ft_parse_event 路径。
    char json[2048];
    size_t n = (size_t)snprintf(json, sizeof(json), "{\"t\":\"members\",\"members\":[");
    for (size_t i = 0; i < FT_MAX_MEMBERS + 5; i++) {
        n += (size_t)snprintf(json + n, sizeof(json) - n,
                              "%s{\"id\":\"m%d\",\"name\":\"成员%d\",\"muted\":false,\"speaking\":false}",
                              (i == 0) ? "" : ",", (int)i, (int)i);
    }
    snprintf(json + n, sizeof(json) - n, "]}");

    ft_event_t ev;
    CHECK(ft_parse_event(json, strlen(json), &ev));
    CHECK(ev.kind == FT_EV_MEMBERS);
    // ft_event_t 自身的 members 数组容量就是 FT_MAX_MEMBERS,解析不能越界写
    CHECK(ev.member_count == FT_MAX_MEMBERS);

    ft_state_on_event(&st, &ev);
    CHECK(st.member_count <= FT_MAX_MEMBERS);
    return 0;
}

static int test_speak_requires_online(void)
{
    ft_state_t st;
    ft_state_init(&st);
    // 未在线时不能要麦
    CHECK(ft_state_request_speak(&st) == FT_ACTION_NONE);
    CHECK(st.speak == FT_SPEAK_IDLE);
    return 0;
}

int main(void)
{
    struct { const char *name; int (*fn)(void); } cases[] = {
        { "init",                  test_init },
        { "welcome_enters_room",   test_welcome_enters_room },
        { "speak_lifecycle",       test_speak_lifecycle },
        { "speak_preempted",       test_speak_preempted_by_peer },
        { "server_releases_floor", test_server_releases_floor },
        { "mute_blocks_speak",     test_mute_blocks_speak },
        { "bad_code_stops_retry",  test_bad_code_stops_reconnect },
        { "floor_busy_denied",     test_floor_busy_denied },
        { "reconnect_backoff",     test_reconnect_backoff },
        { "manual_disconnect",     test_manual_disconnect_no_retry },
        { "member_lookup",         test_member_lookup },
        { "members_update",        test_members_update },
        { "member_overflow",       test_member_overflow_capped },
        { "speak_requires_online", test_speak_requires_online },
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        if (cases[i].fn() != 0) {
            fprintf(stderr, "test case failed: %s\n", cases[i].name);
            return 1;
        }
        printf("ok - %s\n", cases[i].name);
    }
    printf("ft_state: all tests passed\n");
    return 0;
}
