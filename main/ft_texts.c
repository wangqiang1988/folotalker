// FoloTalker 界面字符串清单的实现。
#include "ft_texts.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

// 把所有固定文案拼成一段,用于字形覆盖自检。
// 注意:动态内容(成员昵称、服务器地址)是用户输入,不能靠这个清单覆盖,
// 见 ft_ui.c 里对动态文本的"不支持字符"处理。
const char *ft_ui_text_inventory(void)
{
    static char buf[2048];
    static bool built = false;
    if (built) {
        return buf;
    }

    const char *parts[] = {
        FT_TXT_TITLE_APP, FT_TXT_TITLE_WIFI, FT_TXT_TITLE_PROV,
        FT_TXT_TITLE_HOST, FT_TXT_TITLE_GATE, FT_TXT_TITLE_ROOM,
        FT_TXT_TITLE_SETTINGS,
        FT_TXT_HOLD_TO_TALK, FT_TXT_TALKING, FT_TXT_MIC_MUTED, FT_TXT_MIC_ON,
        FT_TXT_NO_ONE_TALKING, FT_TXT_ROOM_EMPTY, FT_TXT_ONLINE_COUNT,
        FT_TXT_SOMEONE_TALKS, FT_TXT_YOU_TALK, FT_TXT_TAP_SETTINGS,
        FT_TXT_RECONNECTING, FT_TXT_CONNECTING, FT_TXT_OFFLINE,
        FT_TXT_SET_VOLUME, FT_TXT_SET_PROVISION, FT_TXT_SET_SERVER,
        FT_TXT_SET_BACK, FT_TXT_SET_CREDENTIAL,
        FT_TXT_ENTER_NICK, FT_TXT_ENTER_CODE, FT_TXT_ENTER_HOST,
        FT_TXT_PLACEHOLDER_NICK, FT_TXT_PLACEHOLDER_CODE, FT_TXT_PLACEHOLDER_HOST,
        FT_TXT_CODE_TOO_SHORT, FT_TXT_NICK_TOO_LONG, FT_TXT_HOST_INVALID,
        FT_TXT_SAVE_AND_JOIN, FT_TXT_SAVE, FT_TXT_CANCEL,
        FT_TXT_PROV_HINT, FT_TXT_PROV_SCANNING, FT_TXT_PROV_CONNECTED,
        FT_TXT_PROV_FAILED, FT_TXT_WIFI_NEEDED, FT_TXT_BLE_NAME,
        FT_TXT_TRY_AGAIN, FT_TXT_CLEAR,
        FT_TXT_UP, FT_TXT_DOWN, FT_TXT_OK,
        FT_TXT_HINT_EDIT, FT_TXT_HINT_CHARSET, FT_TXT_HINT_ROOM,
        // 字符集名称
        "小写", "大写", "数字", "符号",
        // 协议与状态机的错误文案(ft_protocol.c / ft_state.c)
        "请填写昵称", "家庭口令不正确", "错误次数过多,请稍后再试",
        "房间已满", "加入超时,请重试", "协议错误:需要先加入",
        "麦被占用,请稍候", "发言超时,已自动放麦", "连接异常",
        "未连接", "连接中", "加入中", "在线", "重连中", "已断开",
        // 通用单字
        "确定", "返回", "上一步", "下一步", "完成", "删除",
    };

    size_t w = 0;
    buf[0] = '\0';
    for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); i++) {
        int n = snprintf(buf + w, sizeof(buf) - w, "%s", parts[i]);
        if (n < 0 || (size_t)n >= sizeof(buf) - w) {
            break;
        }
        w += (size_t)n;
    }
    built = true;
    return buf;
}
