<div align="center">

# FoloTalker

**把 FoloToy AI Passport 变成家里的一台对讲机。**

[使用手册](使用手册.md) · [交接文档](HANDOFF.md) · [架构说明](docs/architecture.md) · [HomeTalker](https://github.com/wangqiang1988/hometalker)

</div>

---

## 这是什么

[HomeTalker](https://github.com/wangqiang1988/hometalker) 让手机在家庭 Wi-Fi 里变成
对讲机。**FoloTalker** 是它的设备端客户端 —— FoloToy AI Passport
（ESP32-C3、240×320 屏幕、三个按键）加入同一个房间，和手机互相对讲。

**按住确定键，就能在家里跟家人说话。**

设备端与手机端使用**同一套 WebSocket 协议**（`join` / `speak` / `mute` +
16kHz 单声道 Int16LE 二进制音频帧），所以两者可以同时在房间里。

| | 手机（HomeTalker） | 设备（FoloTalker） |
| --- | --- | --- |
| 怎么说话 | 按住屏幕上的大圆按钮 | **按住确定键** |
| 身份 | 昵称 + 家庭口令 | 昵称 + 家庭口令 |
| 能不能同时在房间里 | 能 | 能 |

---

## 快速上手

```bash
# 1. 编译（ESP-IDF 5.5.3）
source ~/esp/esp-idf-v5.5.3/export.sh
./tools/validate.sh            # 仓库检查 + 主机测试 + 固件 + 合并镜像校验

# 2. 刷机（合并镜像，从 0x0 烧录）
cd build/firmware/<sha256>/
python -m esptool --chip esp32c3 -b 460800 --before default_reset --after hard_reset \
  write_flash "@flash_args"

# 3. 设备上：设置 → 重新配网（手机用官方 EspBlufi 小程序下发 Wi-Fi）
#    → 填服务器地址 → 填昵称 + 家庭口令 → 按住确定键说话
```

详细步骤见 **[使用手册](使用手册.md)**。

---

## 界面

全中文界面，深色卡片 + 强调色扁平化设计（**不是**上游固件里那套
硬件测试用的像素外壳 —— 二次开发必须重新设计 UI）。

```
┌────────────────────────────┐
│ 对讲                  87%  │
│ ┌────────────────────────┐ │
│ │ 爸爸 正在说话          │ │
│ └────────────────────────┘ │
│ ┌────────────────────────┐ │
│ │ 在线 3 人              │ │
│ │ 妈妈  爷爷             │ │
│ └────────────────────────┘ │
│  ┌──────────────────────┐  │
│  │    按 住 说 话       │  │
│  └──────────────────────┘  │
│ 短按确定进入设置            │
└────────────────────────────┘
```

四个页面：**服务器地址** / **门禁** / **房间** / **设置**。

按键语义（房间页）：

| 按键 | 动作 |
| --- | --- |
| 确定 —— 按住 | **说话** |
| 确定 —— 短按 | 打开设置 |
| 确定 —— 长按 | 切换麦克风静音 |
| 下 —— 短按 | 切换麦克风静音 |

---

## 项目结构

```
main/
  ft_protocol.c   房间协议:JSON 编解码      [纯逻辑,主机可测]
  ft_state.c      连接 + 麦权状态机          [纯逻辑,主机可测]
  ft_store.c      加密 NVS 设置              [纯逻辑,主机可测]
  ft_textfield.c  屏幕键盘                  [纯逻辑,主机可测]
  ft_form.c       多行表单                  [纯逻辑,主机可测]
  ft_glyphs.c     字形覆盖检查              [纯逻辑,主机可测]
  ft_texts.c      全部界面文案(唯一事实来源)
  ─────────────── 上面这行以上不依赖 ESP-IDF/LVGL ───────────────
  ft_net.c        WebSocket 传输
  ft_audio.c      16kHz 采集/播放
  ft_prov.c       BLE BLUFI 配网
  ft_ui.c         应用页面
  ft_theme.c      配色与字体绑定
  ft_app.c        应用主控
assets/fonts/     生成的中文位图字体子集
tools/gen_fonts.py  字体子集生成器
```

**设计原则**：与硬件无关的逻辑必须住在没有 ESP-IDF / LVGL 依赖的模块里，
这样协议解析、麦权时序、退避计算、屏幕键盘和字形覆盖这几块
最容易出 bug、又最难在真机上察觉的东西，才能被主机测试覆盖。

---

## 中文字体覆盖

LVGL 默认的 Montserrat 字体**没有任何中文字形**。「UTF-8 编码正确 + 编译通过」
都不代表屏幕上能显示出中文。

本项目的做法：

1. `main/ft_texts.c` 是界面文案的**唯一事实来源**
2. `tools/gen_fonts.py` 把 Noto Sans CJK SC 裁剪成只含这些字符的子集，
   交给 `lv_font_conv` 生成 16/20/28px 三档位图字体
3. `tests/test_font_coverage.py` 在**界面文案出现字体未覆盖的字符**时让门禁失败，
   同时用一个「已知缺失」的负例验证这个检查不会无条件通过

当前覆盖 **209 个码点**，三个字号全部通过。缺字会显示成占位方块，
而不是静默消失。详见 [字体说明](assets/fonts/README.md)。

---

## 安全

| 项 | 做法 |
| --- | --- |
| 家庭口令 / 服务器地址 | 加密 NVS 分区（HMAC 外设方案，eFuse block 1） |
| Wi-Fi 凭证 | 加密 NVS；BLE 侧走 BLUFI 官方安全协商（DHM + AES + MD5） |
| 服务端证书 | 按系统证书包校验，**固件里没有「跳过校验」的默认项** |
| 日志 | 口令与 Wi-Fi 密码都不写日志，也不进仓库 |

---

## 状态

```
Build:        PASS
Host tests:   PASS
Device tests: NOT RUN     ← 还没有任何真机验证
```

完整状态与未验证项见 **[交接文档](HANDOFF.md)**。

---

## 上游与致谢

| 项目 | 地址 |
| --- | --- |
| FoloToy AI Passport（固件基线、BSP、硬件文档） | https://gitee.com/FoloToy/ai-passport |
| HomeTalker（服务端 + 手机网页端） | https://github.com/wangqiang1988/hometalker |

本项目基于 `ai-passport` 的 `main` 分支开发，复用了它的 `components/bsp`
（显示、按键、音频、电池、共享 I2C），但**应用层与界面全部重新实现**。

---

## 文档

| 文档 | 内容 |
| --- | --- |
| [使用手册](使用手册.md) | 配网、填地址、按键操作、常见问题 |
| [交接文档](HANDOFF.md) | 架构、已验证项、踩过的坑、未验证项 |
| [架构说明](docs/architecture.md) | 模块分层、线程模型、协议细节 |
| [字体说明](assets/fonts/README.md) | 中文字体子集的生成与校验 |

英文版本：[README.en.md](README.en.md) · [HANDOFF.en.md](HANDOFF.en.md)

---

## 许可

MIT（与上游基线一致）。中文字体使用 Noto Sans CJK SC，
SIL Open Font License 1.1。

上游项目与第三方组件的来源、许可与使用方式见 [NOTICE](NOTICE)。
