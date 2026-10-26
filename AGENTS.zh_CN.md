<p align="right">
  <strong>简体中文</strong> · <a href="AGENTS.md">English</a>
</p>

# 仓库规范 · AI Agent

本文件是本仓库 AI 辅助工作的唯一必读入口。根据下方路由表读取当前任务所需文档，
不要默认加载全部 README。

## 必需 AI 技能

以下五个技能是本仓库 AI 辅助开发的必需项：`passport-develop`、
`passport-setup`、`passport-build`、`passport-device-test`、`passport-debug`。
它们的权威源文件在上游基线仓库
[ai-passport](https://gitee.com/FoloToy/ai-passport) 的 `skills/` 目录。

开始开发前，AI 必须自行检查这五个技能是否已在当前环境安装并可用。缺少时，
由 AI 根据所用工具和环境，自行选择合适的安装方式、完成安装并验证可用性。
不等待用户另外提出安装要求，也不把选择安装方式或执行安装命令交给用户。
不强制使用特定脚本、安装目录或 AI 客户端。

安装必须遵守环境的权限审批要求，保留已有技能和用户配置。权限、冲突或平台
限制导致无法安装时，说明阻碍，只请求必要的用户配合，不得谎报安装成功。
五个技能必须可用，不代表每次任务都加载或调用全部技能；只使用匹配当前任务的
技能。烧录、Git 写操作和发布仍须分别取得相应授权。

## 项目基线

- 目标平台：ESP32-C3、8MB Flash、无 PSRAM、ESP-IDF 5.5.3。
- 板级支持（`components/bsp`）借自上游基线，是引脚、地址与外设参数的
  **唯一事实来源**。应用代码不得复制这些常量。
- 应用层与整个界面是原创工作。上游基线的 demo 菜单与像素外壳是硬件能力
  测试界面，**不得**用作应用 UI。
- 可复用板级逻辑放入 `components/bsp`；页面、状态机、动画和应用任务放入 `main`。

## 项目结构

```text
main/          应用层:协议、状态、存储、界面、网络、音频、BLE
components/bsp/  板级支持包(借自上游基线)
assets/fonts/  生成的中文位图字体子集
tests/         不依赖硬件的主机测试
tools/         校验、固件归档、字体生成
docs/          架构说明
```

## 文档约定

本仓库以中文为主。主要文档是简体中文，旁边配英文摘要：

| 中文(主) | 英文摘要 |
| --- | --- |
| `README.md` | `README.en.md` |
| `HANDOFF.md` | `HANDOFF.en.md` |
| `使用手册.md` | (见 `README.en.md`) |
| `docs/architecture.md` | (见 `README.en.md`) |

这里**不强制** `.zh_CN.md` 配对 —— 那是上游基线的约定，不是本仓库的约定。
由 `tools/check_repo.py` 强制检查的是：

- 每个相对 Markdown 链接的目标都存在；
- 没有提交凭证、私钥或真实的家庭口令；
- 没有提交构建产物（`*.bin`、`*.elf`、`*.map`）；
- 每个 `main/ft_*.c` 都在 `main/CMakeLists.txt` 里注册；
- 生成的字体只注册一次，且不在别的源文件里 `#include`。

## 中文文本与字体

界面是简体中文，而 LVGL 默认的 Montserrat 字体没有任何中文字形。
UTF-8 正确、编译成功，都证明不了屏幕上能显示出中文。

- `main/ft_texts.c` 是界面文案的唯一事实来源。所有显示给用户的字符串都放在这里。
- `tools/gen_fonts.py` 把 CJK 源字体裁剪成正好只含这些字符，再交给 `lv_font_conv`。
- `tests/test_font_coverage.py` 在界面文案出现字体未覆盖的字符时让门禁失败。

改动界面文案时，要改 `main/ft_texts.c`、重新生成字体、重跑覆盖检查。
绝不能用「关掉占位符渲染」来掩盖缺字。

## 安全与运行时规则

- 保留用户已有修改。先执行 `git status --short --branch`，不得覆盖或清理无关文件。
- LVGL 非线程安全。LVGL 任务之外访问 LVGL 对象时必须持有 `bsp_lvgl_lock()`。
- 按键回调不得阻塞。音频、存储与网络等慢操作必须放入工作任务。
- WebSocket 组件的回调跑在它自己的任务里，只能把数据拷进队列。
- 可测试的状态机、协议、计时和布局计算应与 ESP-IDF / LVGL 解耦，并由主机测试覆盖。
- 禁止提交凭证、设备秘密、私钥、个人数据或未脱敏日志。
  家庭口令与 Wi-Fi 密码绝不能出现在日志里。
- 家庭口令、服务器地址与 Wi-Fi 凭证存放在加密 NVS 分区。不要挪到明文存储。
- 服务端证书按系统证书包校验。固件里**故意**没有「跳过校验」的默认项；
  要加必须取得用户明确同意。

## 按任务加载上下文

| 任务 | 修改前读取 |
| --- | --- |
| 任意代码修改 | `docs/architecture.md`、相关头文件和相邻实现 |
| 协议或状态机 | `main/ft_protocol.c`、`main/ft_state.c` 及其主机测试 |
| 界面、页面或文案 | `main/ft_ui.c`、`main/ft_texts.c`、`assets/fonts/README.md` |
| 网络或音频 | `main/ft_net.c`、`main/ft_audio.c` |
| BLE 配网 | `main/ft_prov.c`、上游 `demo/blufi-provisioning` 参考实现 |
| 构建、测试、依赖、分区 | `tools/validate.sh`、`sdkconfig.defaults`、`partitions.csv` |
| 字体 | `assets/fonts/README.md`、`tools/gen_fonts.py` |
| 文档 | `README.md`、`HANDOFF.md`、本文件 |

硬件事实查阅上游的
[硬件开发指南](https://gitee.com/FoloToy/ai-passport/blob/main/docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md)。
如果任务所需的硬件细节在那里和 `components/bsp/include/bsp_pins.h` 里都没有定义，
直接询问用户，不要猜测。

## 必须执行的验证与交付格式

迭代时运行最小相关检查，交付前运行完整门禁：

```bash
./tools/validate.sh --static    # 仓库检查 + 主机测试
./tools/validate.sh --firmware  # ESP-IDF 构建 + 合并镜像校验
./tools/validate.sh             # 完整门禁
```

完整门禁要求已激活 ESP-IDF 5.5.3 环境。不得把编译成功描述成硬件验证成功。
最终交付必须分别报告：

```text
Build: PASS / FAIL / NOT RUN
Host tests: PASS / FAIL / NOT RUN
Device tests: PASS / FAIL / NOT RUN
Unverified: 仍需板卡、仪器或用户确认的事项
```

每次完整实现用户提出的固件需求后，必须主动询问是否将固件刷写到设备中
进行测试，不能只等到发布时才询问。未检测到设备时，提示用户将设备开机，
再用支持数据传输的数据线连接电脑 USB 接口。烧录前须取得用户同意；
检测到设备本身不代表获得烧录授权。

仅在用户请求或当前工作流明确要求时创建 commit 和 push。
