#ifndef NUTSHELL_ICON_FONT_H
#define NUTSHELL_ICON_FONT_H

/*
 * Icon font creation with glyph validation.
 * Tries Segoe Fluent Icons (Win11), then Segoe MDL2 Assets (Win10).
 * Returns NULL if neither font has the required glyphs — callers
 * should fall back to ASCII text in that case.
 *
 * CreateFont() on Windows never returns NULL; it silently substitutes
 * a default font that can't render Private Use Area codepoints,
 * producing rectangular boxes.  We validate with GetGlyphIndicesW.
 */

#ifdef _WIN32
#include <windows.h>

static inline HFONT create_icon_font(int height)
{
    static const wchar_t test_glyph[] = { 0xEA39 };  /* close icon */
    static const char *font_names[] = {
        "Segoe Fluent Icons",
        "Segoe MDL2 Assets",
    };
    WORD glyph_idx;

    for (int i = 0; i < 2; i++) {
        HFONT f = CreateFont(height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_TT_PRECIS,
                             CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, font_names[i]);
        if (!f) continue;

        HDC tmpDC = CreateCompatibleDC(NULL);
        HFONT oldF = (HFONT)SelectObject(tmpDC, f);
        glyph_idx = 0xFFFF;
        GetGlyphIndicesW(tmpDC, test_glyph, 1, &glyph_idx,
                         GGI_MARK_NONEXISTING_GLYPHS);
        SelectObject(tmpDC, oldF);
        DeleteDC(tmpDC);

        if (glyph_idx != 0xFFFF)
            return f;  /* font has the glyph */
        DeleteObject(f);
    }
    return NULL;  /* neither font available — caller uses ASCII fallback */
}

#endif /* _WIN32 */
#endif /* NUTSHELL_ICON_FONT_H */
