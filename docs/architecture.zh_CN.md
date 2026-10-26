<p align="right">
  <strong>简体中文</strong> · <a href="architecture.md">English</a>
</p>

# FoloTalker 架构

设备端客户端是怎么搭起来的，以及为什么这么搭。

## 分层

贯穿整个代码库的那条规则是：**不需要硬件的逻辑，住在不依赖 ESP-IDF、
不依赖 LVGL 的模块里。** 正是这条规则，让协议编解码、麦权状态机、
重连退避、屏幕键盘和字形覆盖这几块能在主机上测。

```text
main/
  ft_protocol.c   房间协议:JSON 编解码        [纯逻辑]
  ft_state.c      连接 + 麦权状态机            [纯逻辑]
  ft_store.c      加密 NVS 设置                [纯逻辑]
  ft_textfield.c  屏幕键盘逻辑                  [纯逻辑]
  ft_form.c       多行表单逻辑                  [纯逻辑]
  ft_glyphs.c     字形覆盖检查                  [纯逻辑]
  ft_texts.c      全部界面文案,集中一处
  ─────────────── 上面这行以上不依赖 ESP-IDF / LVGL ───────────────
  ft_net.c        WebSocket 传输
  ft_audio.c      16kHz 采集与播放
  ft_prov.c       BLE BLUFI Wi-Fi 配网
  ft_ui.c         应用页面
  ft_theme.c      配色与字体绑定
  ft_app.c        应用主控
components/bsp/   原样借用上游基线,未改动
```

## 线程模型

| 线程 | 职责 |
| --- | --- |
| `ft_app` | **应用状态的唯一持有者**。取网络事件、处理按键、推进重连退避 |
| `ft_ui` | LVGL 对象。渲染状态、投递按键事件 |
| `ft_ws` | WebSocket 传输。回调只把数据拷进队列 |
| `ft_cap` | 音频采集:读 codec,发 20ms 帧 |
| `ft_play` | 音频播放:写 codec |

LVGL 不是线程安全的,所以来自 LVGL 任务之外的每一次访问都持 `bsp_lvgl_lock()`。

这个划分是有原因的:WebSocket 回调跑在组件自己的任务里,绝不能阻塞,
所以它只负责把字节拷进队列;按键回调跑在共享的 `esp_timer` 任务里,同理。

## 协议

与 HomeTalker 服务端完全一致
([`server/src/index.ts`](https://github.com/wangqiang1988/hometalker/blob/main/server/src/index.ts))。

### 控制消息 —— JSON 文本帧

```json
{"t":"join","name":"爸爸","code":"1234"}
{"t":"speak","on":true}
{"t":"mute","on":true}
{"t":"ping"}
```

### 服务端事件 —— JSON 文本帧

```json
{"t":"welcome","you":{"id":"...","name":"..."},"room":{"name":"home"},
 "members":[{"id":"...","name":"...","muted":false,"speaking":false}],
 "speaker":{"id":"...","name":"..."},
 "cfg":{"sampleRate":16000,"frameMs":20,"maxSpeakMs":60000,
        "halfDuplex":true,"speakMode":"preempt"}}
{"t":"members","members":[...]}
{"t":"speaker","id":"...","name":"..."}
{"t":"error","code":"BAD_CODE","message":"..."}
```

### 音频 —— 二进制帧

16kHz、单声道、Int16 小端,每帧正好 20ms(320 采样,640 字节)。
设备把 ES8311 直接配成原生 16kHz,所以没有软件重采样。

### 为什么手写 JSON 扫描器

协议只需要从几种固定形状的事件里读少数字段。`cJSON` / `json-c` 会给一块
8MB Flash、无 PSRAM 的板子增加依赖和代码体积。`ft_protocol.c` 用了一个
小的定向扫描器 —— 约 250 行,而且被主机测试完整覆盖。

有一个值得记住的坑:**判断控制字符边界之前,一定要先转成 `unsigned char`。**
signed char 装下的 UTF-8 后续字节是负数,`c < 0x20` 会成立,于是那个字节被
转义成 `\u00xx`,每一个中文字符都被打烂。

## 麦权控制

服务端是"谁持有麦"的唯一事实来源。

```text
用户按下确定键
        │
        ├─► 发 {"t":"speak","on":true}
        │         │
        │         ├─ 服务端授予 ──► speaker 事件说"是你"
        │         │                    └─► 推流
        │         │
        │         └─ 服务端拒绝(独占模式)
        │                  └─► 错误 FLOOR_BUSY,显示提示
        │
        └─ 用户松开
                └─► 发 {"t":"speak","on":false},停止采集
```

如果按住期间服务端把麦给了别人,设备会**立刻停止发送**。
`speaker` 事件是权威的:只要麦不归自己,`ACTIVE` 和 `REQUESTED`
两种状态都复位。继续推流只会白费电和白占带宽 —— 服务端本来也会丢掉。

## 重连

`ft_state.c` 负责指数退避:1s → 2s → 4s → 8s → 10s(封顶)。

- 加入被拒(口令错、房间满、被锁定)会**停掉重连循环**并退回门禁页。
  再重试只会让情况更糟。
- 其它断开都会安排重连。
- 退避计时器触发时如果 Wi-Fi 还没连上,重连会被推迟,而不是反复硬试。

## 存储

| 键 | 内容 |
| --- | --- |
| `host` | HomeTalker 主机名或 IP |
| `name` | 昵称 |
| `code` | 家庭口令 |
| `volume` | 播放音量 0–100 |
| `wifi_ssid` / `wifi_pass` | Wi-Fi 凭证(由配网模块写入) |

全部在**加密 NVS 分区**里(`CONFIG_NVS_ENCRYPTION`,HMAC 外设方案,
eFuse block 1)。eFuse 密钥在首次启动时生成,之后不可更改,
所以同一份数据换到另一块板子上解不开 —— 这是预期行为。

Wi-Fi 密码从不写日志,也从不进仓库。

## 中文字体覆盖

LVGL 默认的 Montserrat 字体没有任何中文字形。UTF-8 正确 + 编译成功,
都证明不了屏幕上能显示出中文。

流水线:

1. `main/ft_texts.c` —— 界面文案的唯一事实来源
2. `tools/gen_fonts.py` —— 把 Noto Sans CJK SC 裁剪成正好只含这些字符,
   逐个核对请求的码点在源字体里确实存在,再交给 `lv_font_conv`
   以 16/20/28px、4bpp 生成字体
3. `tests/test_font_coverage.py` —— 界面文案里有字符没被生成字体覆盖时
   让构建失败;并包含一个「已知缺失」的负例,保证这个检查不会无条件通过

当前 209 个码点,三个字号全覆盖。缺字显示成占位方块而不是静默消失,
Montserrat 保留为 fallback 用来提供 LVGL 内置图标。

## 安全立场

| 关注点 | 处理 |
| --- | --- |
| 家庭口令、服务器地址 | 加密 NVS |
| Wi-Fi 凭证 | 加密 NVS;BLE 侧走 BLUFI 官方安全协商(DHM + AES + MD5) |
| 服务端证书 | 按系统证书包校验,**固件里没有「跳过校验」的默认项** |
| 日志 | 家庭口令与 Wi-Fi 密码都从不写日志 |

## 硬件复用

`components/bsp` 原样借自上游基线,未改动。应用层从不复制引脚常量、
I2C 地址或寄存器值 —— 它们都在 `components/bsp/include/bsp_pins.h`,
那是这块板子的唯一事实来源。

应用层没有新增任何硬件接口,也没有新增任何接线。
