// FoloTalker 应用状态机实现 —— 纯 C,无 ESP-IDF / LVGL 依赖。
#include "ft_state.h"

#include <string.h>

void ft_state_init(ft_state_t *st)
{
    if (st == NULL) {
        return;
    }
    memset(st, 0, sizeof(*st));
    st->conn = FT_CONN_IDLE;
    st->page = FT_PAGE_BOOT;
    st->speak = FT_SPEAK_IDLE;
    st->backoff_ms = FT_BACKOFF_MIN_MS;
    st->cfg.sample_rate = FT_SAMPLE_RATE;
    st->cfg.frame_ms = FT_FRAME_MS;
    st->cfg.max_speak_ms = 60000;
    st->cfg.half_duplex = true;
    st->cfg.exclusive = false;
}

// 把成员列表整体替换成事件里的列表。
static void ft_state_set_members(ft_state_t *st, const ft_event_t *ev)
{
    st->member_count = ev->member_count;
    for (size_t i = 0; i < ev->member_count && i < FT_MAX_MEMBERS; i++) {
        st->members[i] = ev->members[i];
    }
}

// 判断服务端是否还把麦给了自己。
static bool ft_state_speaker_is_self(const ft_state_t *st, const char *speaker_id)
{
    if (!st->cfg.half_duplex) {
        return false;
    }
    // speaker 的 id 与自己的 id 一致 => 自己持有麦
    return st->self_id[0] != '\0' && speaker_id != NULL &&
           strcmp(st->self_id, speaker_id) == 0;
}

bool ft_state_on_event(ft_state_t *st, const ft_event_t *ev)
{
    if (st == NULL || ev == NULL) {
        return false;
    }

    switch (ev->kind) {
    case FT_EV_WELCOME: {
        st->conn = FT_CONN_ONLINE;
        st->page = FT_PAGE_ROOM;
        st->backoff_ms = FT_BACKOFF_MIN_MS;
        st->retry_pending = false;
        st->last_error = FT_ERR_NONE;
        st->last_error_text[0] = '\0';
        st->cfg = ev->cfg;
        if (ev->cfg.sample_rate == 0) {
            st->cfg.sample_rate = FT_SAMPLE_RATE;
        }
        if (ev->cfg.frame_ms == 0) {
            st->cfg.frame_ms = FT_FRAME_MS;
        }
        if (ev->self_id[0] != '\0') {
            strncpy(st->self_id, ev->self_id, sizeof(st->self_id) - 1);
        }
        if (ev->self_name[0] != '\0') {
            strncpy(st->self_name, ev->self_name, sizeof(st->self_name) - 1);
        }
        if (ev->room_name[0] != '\0') {
            strncpy(st->room_name, ev->room_name, sizeof(st->room_name) - 1);
        }
        ft_state_set_members(st, ev);
        st->speak = FT_SPEAK_IDLE;
        st->remote_speaking = false;
        st->remote_speaker[0] = '\0';
        return true;
    }

    case FT_EV_MEMBERS: {
        ft_state_set_members(st, ev);
        return true;
    }

    case FT_EV_SPEAKER: {
        st->remote_speaking = ev->has_speaker;
        if (ev->has_speaker) {
            strncpy(st->remote_speaker, ev->speaker_name, sizeof(st->remote_speaker) - 1);
        } else {
            st->remote_speaker[0] = '\0';
        }
        // 服务端的麦权是唯一事实来源:
        //   - 麦不再归自己 => 本地必须复位(无论是已激活还是还在请求中),
        //     否则设备会继续发送服务端不转发、甚至别人正在用的音频。
        //   - 麦刚归自己   => 请求转为激活。
        bool has_floor = ft_state_speaker_is_self(st, ev->speaker_id);
        if (!has_floor &&
            (st->speak == FT_SPEAK_ACTIVE || st->speak == FT_SPEAK_REQUESTED)) {
            st->speak = FT_SPEAK_IDLE;
        } else if (has_floor && st->speak == FT_SPEAK_REQUESTED) {
            st->speak = FT_SPEAK_ACTIVE;
        }
        return true;
    }

    case FT_EV_ERROR: {
        st->last_error = ev->error_code;
        if (ev->error_message[0] != '\0') {
            strncpy(st->last_error_text, ev->error_message, sizeof(st->last_error_text) - 1);
        } else {
            const char *fallback = ft_error_text(ev->error_code);
            strncpy(st->last_error_text, fallback, sizeof(st->last_error_text) - 1);
        }

        switch (ev->error_code) {
        case FT_ERR_PONG:
            // 心跳响应,不是错误
            break;
        case FT_ERR_BAD_CODE:
        case FT_ERR_LOCKED:
        case FT_ERR_NAME_REQUIRED:
        case FT_ERR_ROOM_FULL:
            // 服务端明确拒绝加入 —— 重连没有意义,停在门禁页等用户改口令。
            st->conn = FT_CONN_FAILED;
            st->page = FT_PAGE_GATE;
            st->speak = FT_SPEAK_IDLE;
            st->retry_pending = false;
            break;
        case FT_ERR_FLOOR_BUSY:
            // 麦被占(仅 exclusive 模式会来)
            st->speak = FT_SPEAK_DENIED;
            break;
        case FT_ERR_SPEAK_TIMEOUT:
            st->speak = FT_SPEAK_IDLE;
            break;
        case FT_ERR_JOIN_TIMEOUT:
        case FT_ERR_JOIN_REQUIRED:
            st->conn = FT_CONN_RECONNECTING;
            st->retry_pending = true;
            break;
        case FT_ERR_NONE:
        case FT_ERR_OTHER:
        default:
            break;
        }
        return true;
    }

    case FT_EV_NONE:
    default:
        return false;
    }
}

void ft_state_on_disconnect(ft_state_t *st, bool manual)
{
    if (st == NULL) {
        return;
    }
    st->speak = FT_SPEAK_IDLE;
    st->remote_speaking = false;
    st->remote_speaker[0] = '\0';
    st->self_id[0] = '\0';

    if (manual) {
        st->conn = FT_CONN_IDLE;
        st->retry_pending = false;
        return;
    }

    // 非主动断开 => 掉线,安排重连。但口令错误这类已被服务端拒绝的情况不重连。
    if (st->conn == FT_CONN_FAILED) {
        st->retry_pending = false;
        return;
    }
    st->conn = FT_CONN_RECONNECTING;
    st->retry_pending = true;
    // 以当前时间基准起算退避窗口。ft_state_tick() 会持续刷新 now_ms,
    // 所以这里必须把起算点单独记在 retry_deadline_ms 上。
    st->retry_deadline_ms = st->now_ms + st->backoff_ms;
}

bool ft_state_tick(ft_state_t *st, uint32_t now_ms)
{
    if (st == NULL) {
        return false;
    }
    st->now_ms = now_ms;
    if (!st->retry_pending) {
        return false;
    }
    // now_ms 单调递增;用回绕安全的时间差比较。
    if ((uint32_t)(now_ms - st->retry_deadline_ms) < 0x80000000u &&
        (int32_t)(now_ms - st->retry_deadline_ms) >= 0) {
        st->retry_pending = false;
        st->backoff_ms = (st->backoff_ms * 2 > FT_BACKOFF_MAX_MS)
                             ? FT_BACKOFF_MAX_MS
                             : st->backoff_ms * 2;
        return true;
    }
    return false;
}

ft_action_t ft_state_request_speak(ft_state_t *st)
{
    if (st == NULL || !ft_state_can_speak(st)) {
        return FT_ACTION_NONE;
    }
    if (st->speak == FT_SPEAK_ACTIVE || st->speak == FT_SPEAK_REQUESTED) {
        return FT_ACTION_NONE;
    }
    st->speak = FT_SPEAK_REQUESTED;
    return FT_ACTION_SEND_SPEAK_ON;
}

ft_action_t ft_state_release_speak(ft_state_t *st)
{
    if (st == NULL) {
        return FT_ACTION_NONE;
    }
    if (st->speak != FT_SPEAK_ACTIVE && st->speak != FT_SPEAK_REQUESTED) {
        return FT_ACTION_NONE;
    }
    st->speak = FT_SPEAK_IDLE;
    return FT_ACTION_SEND_SPEAK_OFF;
}

ft_action_t ft_state_toggle_mute(ft_state_t *st)
{
    if (st == NULL) {
        return FT_ACTION_NONE;
    }
    st->mic_muted = !st->mic_muted;
    return FT_ACTION_SEND_MUTE;
}

bool ft_state_can_speak(const ft_state_t *st)
{
    if (st == NULL) {
        return false;
    }
    if (st->conn != FT_CONN_ONLINE) {
        return false;
    }
    if (st->mic_muted) {
        return false;
    }
    return true;
}

bool ft_state_has_floor(const ft_state_t *st)
{
    if (st == NULL) {
        return false;
    }
    return st->speak == FT_SPEAK_ACTIVE;
}

const ft_member_t *ft_state_find_member(const ft_state_t *st, const char *id)
{
    if (st == NULL || id == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < st->member_count; i++) {
        if (strcmp(st->members[i].id, id) == 0) {
            return &st->members[i];
        }
    }
    return NULL;
}

size_t ft_state_peer_count(const ft_state_t *st)
{
    if (st == NULL) {
        return 0;
    }
    size_t n = 0;
    for (size_t i = 0; i < st->member_count; i++) {
        if (st->self_id[0] == '\0' || strcmp(st->members[i].id, st->self_id) != 0) {
            n++;
        }
    }
    return n;
}

const char *ft_page_title(ft_page_t page)
{
    switch (page) {
    case FT_PAGE_BOOT:      return "FoloTalker";
    case FT_PAGE_WIFI:      return "未连接 Wi-Fi";
    case FT_PAGE_PROVISION: return "蓝牙配网";
    case FT_PAGE_HOST:      return "服务器地址";
    case FT_PAGE_GATE:      return "进入房间";
    case FT_PAGE_ROOM:      return "对讲";
    case FT_PAGE_SETTINGS:  return "设置";
    default:                return "FoloTalker";
    }
}
