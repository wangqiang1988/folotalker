// FoloTalker 字形覆盖自检的实现。
#include "ft_glyphs.h"

#include <string.h>

size_t ft_utf8_decode(const char *text, uint32_t *out, size_t capacity)
{
    if (text == NULL) {
        return 0;
    }
    const unsigned char *p = (const unsigned char *)text;
    size_t n = 0;

    while (*p != '\0') {
        uint32_t cp;
        size_t extra;

        if (*p < 0x80) {
            cp = *p;
            extra = 0;
        } else if ((*p & 0xE0) == 0xC0) {
            cp = *p & 0x1Fu;
            extra = 1;
        } else if ((*p & 0xF0) == 0xE0) {
            cp = *p & 0x0Fu;
            extra = 2;
        } else if ((*p & 0xF8) == 0xF0) {
            cp = *p & 0x07u;
            extra = 3;
        } else {
            // 非法起始字节 —— 跳过一字节继续,避免死循环。
            p++;
            continue;
        }

        // 检查后续字节是否都是合法的 10xxxxxx
        bool valid = true;
        for (size_t i = 1; i <= extra; i++) {
            if ((p[i] & 0xC0) != 0x80) {
                valid = false;
                break;
            }
        }
        if (!valid) {
            p++;
            continue;
        }

        for (size_t i = 1; i <= extra; i++) {
            cp = (cp << 6) | (uint32_t)(p[i] & 0x3Fu);
        }
        p += extra + 1;

        if (out != NULL && n < capacity) {
            out[n] = cp;
        }
        n++;
    }

    if (out != NULL && n > capacity) {
        return capacity;
    }
    return n;
}

size_t ft_font_check_coverage(const char *text,
                              ft_glyph_query_fn query, void *user,
                              ft_glyph_check_t *out_results, size_t capacity)
{
    if (text == NULL || query == NULL) {
        return 0;
    }

    // 先解码到临时缓冲。界面文案最长几百字节,128 个码点足够;
    // 超出部分会被忽略,但仍然会继续检查(防止漏检)。
    uint32_t cps[256];
    size_t count = ft_utf8_decode(text, cps, sizeof(cps) / sizeof(cps[0]));

    size_t missing = 0;
    size_t written = 0;

    for (size_t i = 0; i < count; i++) {
        uint32_t cp = cps[i];
        // 跳过控制字符:它们不该有可见字形。
        if (cp < 0x20 || cp == 0x7F) {
            continue;
        }
        bool covered = query(user, cp);
        if (!covered) {
            missing++;
        }
        if (out_results != NULL && written < capacity) {
            out_results[written].codepoint = cp;
            out_results[written].covered = covered;
            written++;
        }
    }

    return missing;
}
