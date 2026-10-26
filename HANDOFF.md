<div align="center">

# 交接文档 · FoloTalker

> 写给**下次接着干**的人（或自己）。
> 最后更新：2026-10-26
> 仓库：https://github.com/wangqiang1988/folotalker
> 上游基线：https://gitee.com/FoloToy/ai-passport

</div>

---

## 目录

1. [一句话现状](#一一句话现状)
2. [已经完成并验证过的](#二已经完成并验证过的)
3. [你的环境事实（探测结果，别重做）](#三你的环境事实探测结果别重做)
4. [架构与关键设计决定](#四架构与关键设计决定)
5. [踩过的坑与结论](#五踩过的坑与结论)
6. [下一步](#六下一步)
7. [常用命令速查](#七常用命令速查)
8. [未验证项清单](#八未验证项清单)

---

## 一、一句话现状

**固件功能完整、编译通过、全部主机测试与仓库门禁全绿，但一次真机都没跑过。**

代码在 `feature/folotalker-device` 分支（基于 `ai-passport` 的 `main` @ a06235e），
交付物是经过校验、可从 `0x0` 烧录的合并镜像
`build/FoloToy-AI-Passport-full.bin`。

**下一步是把设备插上电脑、刷机、看屏幕。**

---

## 二、已经完成并验证过的

### 代码

| 模块 | 状态 | 主机测试 |
| --- | --- | --- |
| `main/ft_protocol.c` | 房间协议 JSON 编解码（自写定向扫描器，无 cJSON 依赖） | ✅ `test_ft_protocol.c` |
| `main/ft_state.c` | 连接状态机 + 麦权状态机 + 指数退避 | ✅ `test_ft_state.c` |
| `main/ft_store.c` | 加密 NVS 设置存储 + 主机/密码/URL 校验 | ✅ `test_ft_store.c` |
| `main/ft_textfield.c` | 屏幕键盘（字符集切换、光标、退格） | ✅ `test_ft_textfield.c` |
| `main/ft_form.c` | 多行表单（切行不丢字） | ✅ `test_ft_form.c` |
| `main/ft_glyphs.c` | 字形覆盖检查（UTF-8 解码 + 码点查询） | ✅ `test_ft_glyphs.c` |
| `main/ft_net.c` | WebSocket 传输（esp_websocket_client 1.8.0） | ❌ 需真机 |
| `main/ft_audio.c` | 16kHz 采集/播放双任务 | ❌ 需真机 |
| `main/ft_prov.c` | BLE BLUFI 配网 + Wi-Fi 凭证持久化 | ❌ 需真机 + 手机 |
| `main/ft_ui.c` | 四个页面的界面与导航 | ❌ 需真机 |
| `main/ft_app.c` | 应用主控（唯一持有状态的任务） | ❌ 需真机 |

**新增约 7600 行**（含测试与字体生成脚本）。

### 验证结果（最后一次全绿）

```
仓库检查           PASS   (178 个文本文件)
按键唤醒守卫测试    PASS
BSP 按键故障注入   PASS
BSP LVGL 初始化    PASS
BSP 音频故障/重试   PASS
FoloTalker 主机测试 PASS  (协议/状态机/存储/键盘/表单/字形,共 6 个文件)
中文字体覆盖检查    PASS   (3 个字号 × 209 码点全覆盖 + 负例有效)
仓库检查脚本自测   PASS
Host tests         PASS
Firmware build     PASS   (merged 1,924,608 B, flash at 0x0)
```

> 基线仓库里的 demo 测试界面相关测试（`test_ui_pixel_math`、
> `test_demo_navigation`、`test_demo_*_runtime`、`test_deep_sleep_contract`）
> 随 demo 源码一起移除 —— 那些代码是硬件能力测试界面，本项目不用它们。

### 交付物

> ⚠️ **每次构建的 sha256 都不同** —— 应用镜像里嵌了构建时间戳。
> 所以不要在文档里写死某个 hash，用下面的命令查当前那份固件的身份。

```
build/FoloToy-AI-Passport-full.bin
  sha256 见 manifest.json(每次构建都不同,见下方说明)

build/firmware/<sha256>/
  ├── FoloToy-AI-Passport-full.bin   合并镜像
  ├── FoloToy-AI-Passport.bin        app(0x10000)
  ├── FoloToy-AI-Passport.elf        符号表
  ├── FoloToy-AI-Passport.map        内存分布
  ├── bootloader/bootloader.bin      (0x0)
  ├── partition_table/partition-table.bin  (0x8000)
  ├── flash_args                     esptool 参数
  └── manifest.json                  各文件 sha256
```

验证：`python3 tools/archive_firmware.py verify build/firmware/<sha256>/`

### 体积

```
app        1,859,072 B / 8,323,072 B  factory 分区   (78% 空闲)
DRAM         213,008 B /   321,296 B                  (66%)
Flash Code  1,335,780 B
Flash Data    389,948 B
```

中文字体占约 682 KB Flash（16/20/28px 三档，4bpp，不压缩）。

---

## 三、你的环境事实（探测结果，别重做）

### 开发机

| 项 | 结论 |
| --- | --- |
| ESP-IDF | **v5.5.3**，在 `~/esp/esp-idf-v5.5.3`（不是 `/opt`！） |
| 激活方式 | `source ~/esp/esp-idf-v5.5.3/export.sh` |
| Python | IDF 自带 venv 在 `~/.espressif/python_env/idf5.5_py3.12_env` |
| 工具链 | `riscv32-esp-elf` gcc 14.2.0，`~/.espressif/tools/` |
| `lv_font_conv` | npm 装的，在 `/tmp/opencode/node_modules/.bin/`（不在 PATH 里） |
| `fontTools` | `pip install --break-system-packages fonttools` 装的（系统 Python 有 PEP 668 限制） |
| Noto CJK | `/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc`（子字体下标 2 = SC） |
| USB 设备 | **没有接 ESP32-C3**（`lsusb` 无 303a:xxxx，无 `/dev/ttyACM*`） |

### 五个必需技能

已装在 `~/.config/opencode/skills/`（`passport-develop` / `passport-setup` /
`passport-build` / `passport-device-test` / `passport-debug`），来源是
`ai-passport` 仓库的 `skills/` 目录。**换机器要重新装。**

### HomeTalker 服务端

在 `~/Repositories/hometalker`，跑在 `10.255.1.101:8443`（Caddy 反代 443）。
**本机（`10.255.1.98`）没装 Docker 权限**，`hometalker/HANDOFF.md` 里有完整的
服务器端口占用表和 TLS 排错结论，别重做。

---

## 四、架构与关键设计决定

### 分层

```
main/
  ft_protocol.c   房间协议:JSON 编解码                    [纯逻辑]
  ft_state.c      连接 + 麦权状态机                        [纯逻辑]
  ft_store.c      加密 NVS 设置                            [纯逻辑]
  ft_textfield.c  屏幕键盘                                [纯逻辑]
  ft_form.c       多行表单                                [纯逻辑]
  ft_glyphs.c     字形覆盖检查                            [纯逻辑]
  ft_texts.c      全部界面文案(唯一事实来源)
  ──────────────────────────── 以上无 ESP-IDF/LVGL 依赖 ────────────────────────────
  ft_net.c        WebSocket 传输
  ft_audio.c      16kHz 采集/播放
  ft_prov.c       BLE BLUFI 配网
  ft_ui.c         应用页面
  ft_theme.c      配色与字体绑定
  ft_app.c        应用主控
components/bsp/   未改动一根线
```

**核心原则**：与硬件无关的逻辑必须住在没有 ESP-IDF/LVGL 依赖的模块里，
这样才能被主机测试覆盖。协议解析、麦权时序、退避计算、键盘、字体覆盖
这几块最容易出 bug、又最难在真机上察觉的东西，全在这个层。

### 线程模型

| 线程 | 职责 |
| --- | --- |
| `ft_app` | **应用状态的唯一持有者**。取网络事件、处理按键、推进退避 |
| `ft_ui` | LVGL 对象。渲染状态、投递按键事件 |
| `ft_ws` | WebSocket 传输，回调只把数据拷进队列 |
| `ft_cap` | 音频采集：读 codec，发 20ms 帧 |
| `ft_play` | 音频播放：写 codec |

LVGL 非线程安全 —— 来自 LVGL 任务之外的每次访问都持 `bsp_lvgl_lock()`。

### 关键决定与理由

| 决定 | 理由 |
| --- | --- |
| 自写 JSON 扫描器而不是 cJSON | 协议只需要读固定几种事件的少数字段；给无 PSRAM 的 C3 加依赖不划算 |
| 按键用 PRESS/RELEASE 而不是 CLICK | 「按住说话」要求按下即开始传声，等 CLICK 会有 180ms 延迟 |
| 麦权以服务端为准 | 服务端是唯一事实来源；本地抢着发会发别人正在用的音频 |
| Wi-Fi 凭证存加密 NVS 而非 `WIFI_STORAGE_FLASH` | `esp_wifi_set_storage` 存的是明文；口令类信息必须加密 |
| 字体子集 4bpp 不压缩 | 生成结果可读、避免解码器配置不匹配；当前体积有 78% 余量 |
| 保留基线的 `key_guard.h` 按键守卫 | 深睡唤醒后唤醒键还按着，会立刻误触发长按动作。这是真 bug，不是风格问题 |

### 界面的重新设计

上游 `main` 分支的 demo 菜单 + `ui_pixel` 像素外壳是**硬件能力测试界面**，
不是应用 UI，二次开发禁止沿用。本项目是**全新设计**：

- 深色卡片 + 强调色扁平化（不用像素艺术）
- 四个页面：服务器地址 / 门禁 / 房间 / 设置
- 房间页布局：标题栏（右上电量）→ 状态卡（谁在说话）→ 成员卡 →
  大号按住说话按钮 → 提示行
- 配色见 `main/ft_theme.h`

---

## 五、踩过的坑与结论

### 坑 1：`char c < 0x20` 在 signed char 上会把 UTF-8 打烂

写 JSON 转义时用了 `if (c < 0x20)` 判断控制字符。中文的 UTF-8 字节
在 signed char 下是负数，**每个字节都被当成控制字符转义成 `\u00xx`**，
中文全变乱码。

**结论**：判断字符类别前一律先 `(unsigned char)`。

```c
unsigned char c = (unsigned char)src[i];
if (c < 0x20) { /* 才是控制字符 */ }
```

### 坑 2：`ft_json_get_bool` 用来读字符串字段

`speakMode` 是字符串 `"preempt"` / `"exclusive"`，不是布尔值。我先用
`get_bool` 读它，函数返回 false，整个分支被跳过 → `exclusive` 永远是 false。

**结论**：先确认字段的实际 JSON 类型再选读取函数。

### 坑 3：状态机的退避时间基准被自己覆盖

`ft_state_tick()` 里先写 `st->now_ms = now_ms`，再用 `now_ms - st->now_ms`
算已过时间 —— 差值永远是 0，退避永远不触发。

**结论**：起算点要单独存一个字段（`retry_deadline_ms`），
不能和「当前时间」共用一个变量。

### 坑 4：抢麦后本地还在发音频

服务端把麦给了别人，本地 `speak` 状态还停在 `REQUESTED`，
设备继续推音频流（服务端会丢弃，但白烧电、占带宽）。

**结论**：服务端 SPEAKER 事件是唯一事实来源。麦不归自己时，
`ACTIVE` 和 `REQUESTED` 两种状态都要复位。

### 坑 5：IDF 5.5 的 BLUFI API 和老版本完全不同

我按 `demo/blufi-provisioning` 分支（老 API）写，编译错误一片：

| 我写的（老 API） | IDF 5.5 实际 |
| --- | --- |
| `esp_blufi_svc_init(&callbacks)` | `esp_blufi_register_callbacks()` + `esp_blufi_profile_init()` |
| `callbacks.negotiate` | `callbacks.negotiate_data_handler` |
| `mbedtls_dhm_calc_pubkey()` | `mbedtls_dhm_make_public()` |
| `esp_blufi_report_error()` | `btc_blufi_report_error()`（外部声明） |
| `esp_netif_init()` 返回 esp_err_t | 返回 **void** |
| `nimble_port_run()` 返回 esp_err_t | 返回 **void** |
| `ble_hs_cfg.gap_event_cb` | 本版 NimBLE **没有这个字段** |

**结论**：IDF 5.5 的权威参考是
`~/esp/esp-idf-v5.5.3/examples/bluetooth/blufi/main/`，直接照着抄结构，
不要照抄老分支。

### 坑 6：ESP32-C3 没有 Flash Encryption

开 `CONFIG_NVS_ENCRYPTION=y` 直接编译失败：

```
#error "NVS Encryption (HMAC): Configured eFuse block out of range!"
```

ESP32-C3 没有 Flash Encryption，NVS 加密只能用 **HMAC 外设**方案：

```ini
CONFIG_NVS_ENCRYPTION=y
CONFIG_NVS_SEC_KEY_PROTECT_USING_HMAC=y
CONFIG_NVS_SEC_HMAC_EFUSE_KEY_ID=1
```

**注意**：首次启动会在 eFuse block 1 写入 HMAC 密钥，**这一步不可逆**。
换板子后旧数据解不开（这是预期行为）。eFuse block 1 的用途是
`ESP_EFUSE_KEY_PURPOSE_HMAC_UP`。

### 坑 7：LVGL 9.6 的 API 变化

| 旧 | 新 |
| --- | --- |
| `lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN)` | `lv_obj_set_hidden(obj, true)` |
| `lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN)` | `lv_obj_set_hidden(obj, false)` |
| `lv_timer_create(cb, period)` | `lv_timer_create(cb, period, user_data)` |
| `CONFIG_LV_MEM_SIZE_KILOBYTES=48` | `CONFIG_LV_MEM_SIZE=49152`（字节） |

### 坑 8：`esp_websocket_client_send_text` 需要 timeout 参数

IDF 5.5 的签名是 `send_text(client, data, len, timeout)`，少一个参数编译不过。

### 坑 9：`ft_store` 的 NVS 接口类型

`nvs_get_i32()` 要的是 `int32_t *`，直接传 `int *` 会报
`-Wincompatible-pointer-types`（在 `-Werror=all` 下是硬错误）。

### 坑 10：上游的门禁脚本不适合独立应用仓库

`ai-passport` 的 `tools/check_repo.py` 强制要求每个 `.md` 都有 `.zh_CN.md`
配对、默认 `.md` 里不能有中文、还得有 `.github/ISSUE_TEMPLATE/`。
那是上游基线仓库的约定。

本仓库是独立应用仓库，文档以中文为主，所以换了 `tools/check_repo.py`：
仍然检查链接目标存在、没有提交密钥、没有把固件产物提交进仓库、
应用模块都在 CMakeLists 里注册、字体只注册一次 —— 但不强制中英配对。

---

## 六、下一步

### 立刻能做的（不需要新决策）

1. **插设备、刷固件**
   ```bash
   cd ~/Repositories/folotoytalker
   source ~/esp/esp-idf-v5.5.3/export.sh
   python -m serial.tools.list_ports -v      # 先看端口
   # 确认端口后：
   idf.py -B build -p /dev/ttyACM0 flash
   ```
2. **看串口日志**：`idf.py -B build -p /dev/ttyACM0 monitor`
   重点看有没有 `FoloTalker 启动完成`、有没有崩溃回溯。
3. **看屏幕**：四个页面能不能正常显示，中文是不是方块（不该是）。

### 需要外部配合的

4. **起 HomeTalker 服务端**（或用现成的 `10.255.1.101:8443`），
   然后在设备上填地址 + 昵称 + 口令，看能不能进房间。
5. **BLE 配网实测**：手机装官方 EspBlufi 小程序，给设备配 Wi-Fi。
6. **端到端对讲**：手机端和设备同时在房间里，互相说话，测延迟和音质。

### 可能会需要改的（取决于实测结果）

7. **WSS 证书校验**：HomeTalker 用自签 CA。设备端默认按系统证书包校验，
   **没有「跳过校验」的开关**。如果实测握手失败，需要决定：
   - 把 CA 证书烧进设备（改 `ft_net.c` 的 `cert_pem`）
   - 还是加一个「不校验」设置项（降低安全性，需要用户明确同意）
8. **音频延迟/音质**：16kHz 是协议要求，可能需要调 ES8311 增益、
   I2S 缓冲深度。
9. **成员昵称超长**：目前直接截断，可能需要改成缩略显示。

---

## 七、常用命令速查

```bash
# 环境
source ~/esp/esp-idf-v5.5.3/export.sh

# 完整门禁（仓库检查 + 主机测试 + 固件 + 合并镜像校验）
./tools/validate.sh

# 只跑主机测试（快，秒级）
./tools/validate.sh --static

# 只跑固件构建
./tools/validate.sh --firmware

# 单独跑某个主机测试
cc -std=c11 -Wall -Wextra -Werror -Imain \
   tests/test_ft_state.c main/ft_state.c main/ft_protocol.c \
   -o /tmp/t && /tmp/t

# 重新生成中文字体（改了 ft_texts.c 之后）
pip install --break-system-packages fonttools
npm install lv_font_conv          # 需要时
PATH="/tmp/opencode/node_modules/.bin:$PATH" python3 tools/gen_fonts.py
python3 tests/test_font_coverage.py     # 必须通过

# 固件包校验
python3 tools/archive_firmware.py verify build/firmware/<sha256>/

# 看内存
idf.py -B build size
idf.py -B build size-components
```

---

## 八、未验证项清单

以下都**没有在真机上验证过**，交付时如实标注为 `Unverified`：

| 项 | 为什么没验 |
| --- | --- |
| 屏幕中文渲染 | 无设备 |
| 按键手感与响应延迟 | 无设备 |
| 麦克风采集质量 / 扬声器音质 | 无设备 |
| BLE BLUFI 配网与官方小程序兼容性 | 无设备 + 无手机小程序 |
| WSS 握手（自签 CA 校验） | 无设备 |
| 与 HomeTalker 服务端的端到端互通 | 无设备 |
| NVS 加密首启生成 eFuse 密钥 | 无设备 |
| 掉电重启后自动重连（Wi-Fi + 服务器） | 无设备 |
| 抢占 / 独占两种麦权模式 | 无设备 |
| 音频延迟 | 无设备 |
| 深睡唤醒（本应用未启用深睡） | 无设备 |

**编译通过 ≠ 硬件验证通过。** 上面每一项都需要在真机上跑一遍。

---

## 九、相关链接

| 仓库 | 地址 |
| --- | --- |
| FoloTalker（本项目） | https://github.com/wangqiang1988/folotalker |
| HomeTalker（服务端 + 手机端） | https://github.com/wangqiang1988/hometalker |
| FoloToy AI Passport（固件基线） | https://gitee.com/FoloToy/ai-passport |

英文版本：[HANDOFF.en.md](HANDOFF.en.md)
