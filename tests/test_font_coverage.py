#!/usr/bin/env python3
"""校验生成的字体子集覆盖了界面的全部文案。

这是"中文界面检查字体覆盖"的自动化关口:
    - 解析 assets/fonts/ft_font_*.c 里 lv_font_conv 生成的 cmap,
      得到每个字号实际覆盖的码点;
    - 从 main/ft_texts.c 抽出所有界面文案,以及 ASCII 可打印区;
    - 比对两者,任何界面用到的字符缺字都让这个检查失败。

只依赖标准库,不需要 LVGL 或 fontTools —— 直接解析生成的 C 文件里的
unicode_list / range_start / range_length 表。
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TEXTS_C = ROOT / "main" / "ft_texts.c"
PROTOCOL_C = ROOT / "main" / "ft_protocol.c"
STATE_C = ROOT / "main" / "ft_state.c"
FONT_DIR = ROOT / "assets" / "fonts"

SIZES = ["ft_font_16", "ft_font_20", "ft_font_28"]

# LVGL 图标符号区:这些由 Montserrat fallback 提供,不算缺字。
LVGL_SYMBOL_MIN = 0xF000
LVGL_SYMBOL_MAX = 0xF8FF


def parse_font_cmap(path: Path) -> set[int]:
    """从 lv_font_conv 生成的 C 文件里解析出覆盖的码点集合。

    生成的文件用的是指定初始化器格式:

        static const uint16_t unicode_list_1[] = { 0x4e00, 0x4e01, ... };
        static const lv_font_fmt_txt_cmap_t cmaps[] = {
            { .range_start = 32,   .range_length = 95,
              .unicode_list = NULL, ... },
            { .range_start = 19968, .range_length = 20647,
              .unicode_list = unicode_list_1, .list_length = 74, ... },
        };

    区间表(range_start + range_length)覆盖连续码点;稀疏表(unicode_list)
    列出零散的码点。两种都要算进去。
    """
    text = path.read_text(encoding="utf-8")
    codepoints: set[int] = set()

    # 1) 稀疏表:unicode_list_N[] = { 0x...., ... }
    unicode_lists: dict[str, list[int]] = {}
    for m in re.finditer(
        r"unicode_list_(\d+)\[\]\s*=\s*\{(.*?)\}", text, re.S
    ):
        name = f"unicode_list_{m.group(1)}"
        values = [int(t, 16) for t in re.findall(r"0x([0-9a-fA-F]+)", m.group(2))]
        unicode_lists[name] = values
        codepoints.update(values)

    # 2) 区间表:cmaps[] 里每一项的 .range_start / .range_length
    cmap_block = re.search(r"cmaps\[\]\s*=\s*\{(.*?)\n\};", text, re.S)
    if cmap_block is None:
        raise SystemExit(f"FAIL: 在 {path} 里找不到 cmaps 表")
    block = cmap_block.group(1)

    for entry in re.finditer(r"\{([^{}]*)\}", block, re.S):
        body = entry.group(1)
        start_m = re.search(r"\.range_start\s*=\s*(\d+)", body)
        len_m = re.search(r"\.range_length\s*=\s*(\d+)", body)
        if start_m is None or len_m is None:
            continue
        start = int(start_m.group(1))
        length = int(len_m.group(1))
        if length > 0:
            codepoints.update(range(start, start + length))

        # 稀疏项:把 unicode_list 里额外的码点也算进去
        ul_m = re.search(r"\.unicode_list\s*=\s*(unicode_list_\d+)", body)
        if ul_m is not None:
            codepoints.update(unicode_lists.get(ul_m.group(1), []))

    return codepoints


def extract_strings(path: Path) -> set[str]:
    text = path.read_text(encoding="utf-8")
    strings = re.findall(r'"((?:[^"\\]|\\.)*)"', text)
    out: set[str] = set()
    for s in strings:
        out.add(s.replace('\\"', '"').replace("\\\\", "\\"))
    return out


def required_codepoints() -> set[int]:
    cps: set[int] = set()
    for path in (TEXTS_C, PROTOCOL_C, STATE_C):
        if not path.exists():
            continue
        for s in extract_strings(path):
            for ch in s:
                cp = ord(ch)
                if cp < 0x20 or cp == 0x7F:
                    continue
                if LVGL_SYMBOL_MIN <= cp <= LVGL_SYMBOL_MAX:
                    continue
                cps.add(cp)
    for cp in range(0x20, 0x7F):
        cps.add(cp)
    return cps


def main() -> int:
    if not FONT_DIR.is_dir():
        print(f"FAIL: 找不到字体目录 {FONT_DIR}", file=sys.stderr)
        return 1

    fonts: dict[str, set[int]] = {}
    for name in SIZES:
        path = FONT_DIR / f"{name}.c"
        if not path.exists():
            print(f"FAIL: 找不到生成的字体 {path}", file=sys.stderr)
            print("      请运行:python3 tools/gen_fonts.py", file=sys.stderr)
            return 1
        fonts[name] = parse_font_cmap(path)

    required = required_codepoints()
    print(f"界面文案需要 {len(required)} 个码点")

    failed = False
    for name in SIZES:
        covered = fonts[name]
        missing = sorted(required - covered)
        if missing:
            failed = True
            print(f"FAIL: {name} 缺少 {len(missing)} 个字形:")
            for cp in missing:
                print(f"  U+{cp:04X} {chr(cp)!r}")
        else:
            print(f"ok - {name} 覆盖全部界面文案 ({len(covered)} 个码点)")

    # 负例:确认检查本身不是无条件通过。
    # 找一个界面没用到的汉字,确认它确实不在字体里。
    negative = 0x9F98  # 貈
    if all(negative in fonts[n] for n in SIZES):
        print("FAIL: 负例检查失效 —— 界面未使用的字符竟然在字体里", file=sys.stderr)
        failed = True
    else:
        print("ok - 负例检查有效(未使用的字符不在字体子集里)")

    # 三个字号的覆盖范围应当一致(同一份字符清单生成)
    base = fonts[SIZES[0]]
    for name in SIZES[1:]:
        if fonts[name] != base:
            print(
                f"FAIL: {name} 与 {SIZES[0]} 的字符清单不一致,请重新运行 gen_fonts.py",
                file=sys.stderr,
            )
            failed = True

    if failed:
        print("\n字体覆盖检查失败。补字步骤:", file=sys.stderr)
        print("  1. 在 main/ft_texts.c 里补上文案", file=sys.stderr)
        print("  2. python3 tools/gen_fonts.py", file=sys.stderr)
        return 1

    print("\n字体覆盖检查通过")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
