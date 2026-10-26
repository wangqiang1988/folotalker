<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# FoloTalker 文档

FoloTalker 是 [HomeTalker](https://github.com/wangqiang1988/hometalker) 家庭对讲的
设备端客户端，运行在 FoloToy AI Passport 上。

| 文档 | 内容 |
| --- | --- |
| [使用手册](../使用手册.md) | 给使用者：配网、填地址、按键操作、常见问题 |
| [交接文档](../HANDOFF.md) | 给开发者：架构、已验证项、踩过的坑、未验证项 |
| [架构说明](architecture.md) | 模块分层、线程模型、协议细节 |
| [字体说明](../assets/fonts/README.md) | 中文字体子集的生成与校验 |

## 上游基线

本项目基于 [FoloToy AI Passport](https://gitee.com/FoloToy/ai-passport) 的
`main` 分支开发：

- 复用 `components/bsp`（显示、按键、音频、电池、共享 I2C）
- 复用其硬件事实来源 `components/bsp/include/bsp_pins.h`
- **应用层与界面全部重新实现**，未沿用基线的 demo 测试外壳

硬件与 BSP 的权威说明见上游仓库的
[硬件开发指南](https://gitee.com/FoloToy/ai-passport/blob/main/docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md)。
