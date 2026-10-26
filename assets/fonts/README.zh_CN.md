<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# FoloTalker 字体

FoloTalker 应用界面的中文位图字体子集。

## 字体来源

| 项目 | 内容 |
| --- | --- |
| 字体族 | Noto Sans CJK SC(简体中文) |
| 字重 | Regular |
| 许可证 | SIL Open Font License 1.1(允许再分发) |
| 上游地址 | https://github.com/notofonts/noto-cjk |
| 生成时使用的本地路径 | `/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc`(子字体下标 2 = SC) |

上游的 `.ttc` **不入库** —— 它有 19MB,只在生成时需要。
`tools/gen_fonts.py` 会先把它裁剪成只含界面实际用到的字形,再交给
`lv_font_conv`。

## 生成产物

| 文件 | 体积 | 用途 |
| --- | --- | --- |
| `ft_font_16.c` | 约 108 KB | 正文:标签、成员名、状态行 |
| `ft_font_20.c` | 约 146 KB | 标题、按钮、胶囊标签 |
| `ft_font_28.c` | 约 248 KB | 大号数值:"按住说话"提示、计时 |
| `charset.txt` | 约 2 KB | 覆盖码点的机器可读清单 |

三个字号合计约 502 KB Flash。ESP32-C3 有 8MB Flash 且没有 PSRAM,
所以子集被刻意限制在界面上真实出现的字符。

## 生成方法

```bash
pip install fonttools
npm install lv_font_conv          # 来自 github.com/lvgl/lv_font_conv 的官方转换器
python3 tools/gen_fonts.py
```

脚本的处理流程:

1. 从 `main/ft_texts.c` 抽出所有 C 字符串字面量 —— 那个文件是界面文案的
   唯一事实来源。
2. 加上 ASCII `0x20`–`0x7E`。
3. 跳过 `U+F000`–`U+F8FF`(LVGL 内置图标符号由 Montserrat fallback 提供)。
4. 逐个码点核对源字体里是否真的存在,缺失的会明确报告出来,而不是静默丢弃。
5. 用 fontTools 做子集化,再由 `lv_font_conv` 以 4bpp、不压缩的方式
   生成三个字号。

## 字符清单

权威清单是 `charset.txt`(209 个码点)。修改界面文案时:

1. 编辑 `main/ft_texts.c`。
2. 重新运行 `python3 tools/gen_fonts.py`。
3. 重跑主机测试 —— `tests/test_font_coverage.py` 会在
   `ft_texts.c` 里有字符未被生成字体覆盖时失败。

## 为什么用 4bpp 且不压缩

`--bpp 4 --no-compress` 让生成的 C 文件保持可读,同时避免解码器与配置不匹配。
开启 `CONFIG_LV_USE_FONT_COMPRESSED` 能进一步减小体积,但会引入对 LVGL
解压器的运行时依赖,并改变 Flash/ROM 的占用统计;当前体积有充足余量,
因此保留更简单的配置。

## Fallback

`ft_theme.c` 把应用字体描述符的 `fallback` 指向 `lv_font_montserrat_20`,
所以 LVGL 内置图标(`LV_SYMBOL_OK`、`LV_SYMBOL_WIFI` 等)在使用这些字体的
文本里仍然能正常显示。fallback 是单向的:Montserrat 能补上缺失的*图标*,
但它没有任何中文字形,所以缺失的中文字符仍然会显示成空心方块。
