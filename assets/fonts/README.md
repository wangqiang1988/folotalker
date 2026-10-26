<p align="right">
  <a href="README.zh_CN.md">Simplified Chinese</a> · <strong>English</strong>
</p>

# FoloTalker Fonts

Chinese bitmap-font subsets for the FoloTalker application UI.

## Source

| Item | Value |
| --- | --- |
| Family | Noto Sans CJK SC (Simplified Chinese) |
| Style | Regular |
| License | SIL Open Font License 1.1 (redistribution permitted) |
| Upstream | https://github.com/notofonts/noto-cjk |
| Local path used for generation | `/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc` (sub-font index 2 = SC) |

The upstream `.ttc` is **not** committed — it is 19 MB and only needed at
generation time. `tools/gen_fonts.py` subsets it down to the glyphs the UI
actually uses before handing it to `lv_font_conv`.

## Generated files

| File | Size | Purpose |
| --- | --- | --- |
| `ft_font_16.c` | ~108 KB | Body text: labels, member names, status lines |
| `ft_font_20.c` | ~146 KB | Titles, buttons, chips |
| `ft_font_28.c` | ~248 KB | Large values: the hold-to-talk prompt, timers |
| `charset.txt` | ~2 KB | Machine-readable list of every covered code point |

Total ≈ 502 KB of Flash. The ESP32-C3 has 8 MB of Flash and no PSRAM, so the
subset is deliberately limited to characters that appear in the UI.

## Generation

```bash
pip install fonttools
npm install lv_font_conv          # official converter from github.com/lvgl/lv_font_conv
python3 tools/gen_fonts.py
```

The script:

1. Extracts every C string literal from `main/ft_texts.c` — that file is the
   single source of truth for UI copy.
2. Adds ASCII `0x20`–`0x7E`.
3. Skips `U+F000`–`U+F8FF` (LVGL's built-in icon symbols, supplied by the
   Montserrat fallback).
4. Verifies each requested code point actually exists in the source font, and
   reports the missing ones instead of silently dropping them.
5. Subsets the font with fontTools, then calls `lv_font_conv` at 4 bpp,
   uncompressed, for each of the three sizes.

## Character inventory

The authoritative list is `charset.txt` (169 code points). To change the UI
copy:

1. Edit `main/ft_texts.c`.
2. Re-run `python3 tools/gen_fonts.py`.
3. Re-run the host tests — `tests/test_font_coverage.py` fails if a string in
   `ft_texts.c` has a character that the generated font does not cover.

## Why 4 bpp and no compression

`--bpp 4 --no-compress` keeps the generated C readable and avoids a
decoder/configuration mismatch. Enabling `CONFIG_LV_USE_FONT_COMPRESSED` would
cut the size further but adds a runtime dependency on the LVGL decompressor
and changes Flash/ROM accounting; the current size fits comfortably, so the
simpler configuration is kept.

## Fallback

`ft_theme.c` sets each application's font descriptor's `fallback` to
`lv_font_montserrat_20`, so LVGL's built-in icons (`LV_SYMBOL_OK`,
`LV_SYMBOL_WIFI`, …) still render inside text that uses these fonts. The
fallback is directional: Montserrat can supply a missing *icon*, but it has no
Chinese glyphs, so a missing Chinese character stays a placeholder box.
