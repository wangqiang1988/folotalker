// FoloTalker 界面字符串清单。
//
// 这个文件是"字形覆盖"的唯一事实来源:界面上的每一句中文都必须在这里
// 出现,字体子集也按这里的内容生成。改动界面文案时,记得同步:
//   1. 本文件
//   2. assets/fonts/README.md 里的字符清单
//   3. 重新生成 assets/fonts/ft_font_*.c
//
// 之所以要集中管理:LVGL 的默认 Montserrat 字体没有任何中文字形,
// 漏一个字符在屏幕上就是一个空心方块,而编译和 UTF-8 校验都不会报错。
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// --- 页面标题 -------------------------------------------------------------
#define FT_TXT_TITLE_APP      "FoloTalker"
#define FT_TXT_TITLE_WIFI     "未连接 Wi-Fi"
#define FT_TXT_TITLE_PROV     "蓝牙配网"
#define FT_TXT_TITLE_HOST     "服务器地址"
#define FT_TXT_TITLE_GATE     "进入房间"
#define FT_TXT_TITLE_ROOM     "对讲"
#define FT_TXT_TITLE_SETTINGS "设置"

// --- 房间页 ---------------------------------------------------------------
#define FT_TXT_HOLD_TO_TALK   "按住说话"
#define FT_TXT_TALKING        "正在说话"
#define FT_TXT_MIC_MUTED      "麦克风已静音"
#define FT_TXT_MIC_ON         "麦克风已开"
#define FT_TXT_NO_ONE_TALKING "当前无人说话"
#define FT_TXT_ROOM_EMPTY     "房间里还没有其他人"
#define FT_TXT_ONLINE_COUNT   "在线 %d 人"
#define FT_TXT_SOMEONE_TALKS  "%s 正在说话"
#define FT_TXT_YOU_TALK       "你正在说话"
#define FT_TXT_TAP_SETTINGS   "短按确定进入设置"
#define FT_TXT_RECONNECTING   "正在重连"
#define FT_TXT_CONNECTING     "正在连接服务器"
#define FT_TXT_OFFLINE        "已离线"

// --- 设置页 ---------------------------------------------------------------
#define FT_TXT_SET_VOLUME     "播放音量"
#define FT_TXT_SET_PROVISION  "重新配网"
#define FT_TXT_SET_SERVER     "修改服务器地址"
#define FT_TXT_SET_BACK       "返回房间"
#define FT_TXT_SET_CREDENTIAL "清除家庭口令"

// --- 门禁 / 输入 ----------------------------------------------------------
#define FT_TXT_ENTER_NICK     "昵称"
#define FT_TXT_ENTER_CODE     "家庭口令"
#define FT_TXT_ENTER_HOST     "地址或主机名"
#define FT_TXT_PLACEHOLDER_NICK "请输入昵称"
#define FT_TXT_PLACEHOLDER_CODE "至少4位"
#define FT_TXT_PLACEHOLDER_HOST "例如 talk.home"
#define FT_TXT_CODE_TOO_SHORT "口令至少 4 位"
#define FT_TXT_NICK_TOO_LONG  "昵称太长"
#define FT_TXT_HOST_INVALID   "地址格式不对"
#define FT_TXT_SAVE_AND_JOIN  "保存并进入"
#define FT_TXT_SAVE           "保存"
#define FT_TXT_CANCEL          "取消"

// --- 配网 / 错误 ----------------------------------------------------------
#define FT_TXT_PROV_HINT      "用手机小程序连接本机 Wi-Fi"
#define FT_TXT_PROV_SCANNING  "正在扫描 Wi-Fi"
#define FT_TXT_PROV_CONNECTED "Wi-Fi 已连接"
#define FT_TXT_PROV_FAILED    "配网失败,请重试"
#define FT_TXT_WIFI_NEEDED    "请先连接 Wi-Fi"
#define FT_TXT_BLE_NAME       "FoloTalker"
#define FT_TXT_TRY_AGAIN      "重试"
#define FT_TXT_CLEAR          "清除"

// --- 按键提示 -------------------------------------------------------------
#define FT_TXT_UP             "上"
#define FT_TXT_DOWN           "下"
#define FT_TXT_OK             "确定"
#define FT_TXT_HINT_EDIT      "上/下选字 确定输入 长按退格"
#define FT_TXT_HINT_CHARSET   "长按上键切换字符集"
#define FT_TXT_HINT_ROOM      "按住确定说话 短按确定设置"

// 把所有界面文案拼成一段文本,供字形覆盖自检使用。
// 返回的字符串是静态的、以 '\0' 结尾。
const char *ft_ui_text_inventory(void);

#ifdef __cplusplus
}
#endif
