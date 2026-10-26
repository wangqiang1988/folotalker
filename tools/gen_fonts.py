#!/usr/bin/env python3
"""从界面文案生成 FoloTalker 的中文字体子集。

用法:
    python3 tools/gen_fonts.py [--font /path/to/NotoSansCJK-Regular.ttc]

流程:
    1. 从 main/ft_texts.c 抽出所有 C 字符串字面量,得到界面固定文案全集。
    2. 加上 ASCII 可打印字符(0x20-0x7E)。
    3. 用 fontTools 把源字体子集化成只含这些字形的小 OTF(临时文件,不入库),
       再交给 lv_font_conv 生成三个字号的 LVGL 位图字体。
    4. 写出 assets/fonts/charset.txt 作为机器可读的字符清单。

为什么要先做子集化:
    Noto Sans CJK 的 .ttc 有 19MB,直接提交进仓库不可接受;
    lv_font_conv 也不支持直接读 .ttc,必须先拆出 SC 子字体。
    只保留界面真正用到的字形,子集化后的 OTF 只有 ~85KB。

字体源:
    Noto Sans CJK SC (SIL Open Font License 1.1),见 assets/fonts/README.md
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
# 界面文案的来源文件。所有会被显示给用户的字符串都必须出现在这几个文件里。
TEXT_SOURCES = [
    ROOT / "main" / "ft_texts.c",
    ROOT / "main" / "ft_protocol.c",
    ROOT / "main" / "ft_state.c",
]
OUT_DIR = ROOT / "assets" / "fonts"

# 三个字号:正文、标题、大号数值。
SIZES = [
    (16, "ft_font_16"),
    (20, "ft_font_20"),
    (28, "ft_font_28"),
]

DEFAULT_FONT = "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"
# .ttc 里简体中文子字体的下标(Noto Sans CJK 的顺序:JP, KR, SC, TC, HK, Mono...)。
SC_FONT_INDEX = 2

# LVGL 内置图标符号所在的 Unicode 私有区。这些符号由 Montserrat fallback
# 提供,不需要在子集里重复生成。
LVGL_SYMBOL_MIN = 0xF000
LVGL_SYMBOL_MAX = 0xF8FF


def extract_strings() -> set[str]:
    """抽取所有文案来源文件里的 C 字符串字面量。"""
    out: set[str] = set()
    for path in TEXT_SOURCES:
        if not path.exists():
            continue
        text = path.read_text(encoding="utf-8")
        for s in re.findall(r'"((?:[^"\\]|\\.)*)"', text):
            s = s.replace('\\"', '"').replace("\\\\", "\\")
            out.add(s)
    return out


def collect_codepoints(strings: set[str]) -> set[int]:
    """把字符串集合转成码点集合,忽略控制字符。"""
    cps: set[int] = set()
    for s in strings:
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


def build_subset(font_path: str, codepoints: set[int], dest: Path) -> set[int]:
    """把源字体子集化成只含所需字形的 OTF,返回实际存在的码点集合。"""
    try:
        from fontTools.ttLib import TTCollection
        from fontTools import subset
    except ImportError:
        raise SystemExit("需要 fontTools:pip install fonttools")

    if font_path.lower().endswith(".ttc"):
        collection = TTCollection(font_path)
        font = collection.fonts[SC_FONT_INDEX]
    else:
        from fontTools.ttLib import TTFont
        font = TTFont(font_path)

    cmap = font.getBestCmap()
    present = {cp for cp in codepoints if cp in cmap}
    missing = sorted(codepoints - present)
    if missing:
        print("警告:以下码点在源字体中不存在,已跳过:")
        for cp in missing:
            print(f"  U+{cp:04X} {chr(cp)!r}")

    opts = subset.Options()
    opts.layout_features = ["*"]
    opts.name_IDs = ["*"]
    opts.notdef_outline = True
    opts.recalc_bounds = True
    subsetter = subset.Subsetter(options=opts)
    subsetter.populate(unicodes=present)
    subsetter.subset(font)
    font.save(str(dest))
    return present


def run_lv_font_conv(args: list[str]) -> None:
    result = subprocess.run(args, capture_output=True, text=True)
    if result.returncode != 0:
        sys.stderr.write(result.stdout)
        sys.stderr.write(result.stderr)
        raise SystemExit(f"lv_font_conv 失败: {' '.join(args)}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--font", default=DEFAULT_FONT, help="CJK 字体文件(.ttf/.otf/.ttc)")
    parser.add_argument("--conv", default="lv_font_conv", help="lv_font_conv 可执行文件")
    args = parser.parse_args()

    if not Path(args.font).exists():
        raise SystemExit(f"找不到字体文件: {args.font}")

    strings = extract_strings()
    codepoints = collect_codepoints(strings)
    print(f"界面文案共 {len(strings)} 条,需要 {len(codepoints)} 个码点")

    OUT_DIR.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory() as tmp:
        subset_path = Path(tmp) / "subset.otf"
        present = build_subset(args.font, codepoints, subset_path)
        print(f"源字体子集化完成: {subset_path.stat().st_size / 1024:.1f} KB")

        range_arg = ",".join(f"0x{cp:X}" for cp in sorted(present))
        for size, name in SIZES:
            out_file = OUT_DIR / f"{name}.c"
            run_lv_font_conv([
                args.conv,
                "--font", str(subset_path),
                "--range", range_arg,
                "--size", str(size),
                "--bpp", "4",
                "--format", "lvgl",
                "--no-compress",
                "--lv-font-name", name,
                "--lv-include", "lvgl.h",
                "--output", str(out_file),
            ])
            print(f"生成 {out_file.name}  ({size}px, {out_file.stat().st_size / 1024:.1f} KB)")

    manifest = OUT_DIR / "charset.txt"
    manifest.write_text(
        "# FoloTalker 字体子集字符清单(由 tools/gen_fonts.py 生成,勿手工编辑)\n"
        "# 每行一个 U+XXXX 码点\n"
        + "\n".join(f"0x{cp:04X}" for cp in sorted(present))
        + "\n",
        encoding="utf-8",
    )
    print(f"生成 {manifest.name} ({len(present)} 个码点)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
