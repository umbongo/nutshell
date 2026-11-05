/* tests/test_blend.h -- the one blend helper the theme tests share. */
#ifndef NUTSHELL_TEST_BLEND_H
#define NUTSHELL_TEST_BLEND_H

/* rgb_alpha() (src/ui/ns_draw.c) on packed 0x00RRGGBB: per channel
 * fg * a + bg * (1 - a) in float, truncated.  Written out here, not taken
 * from src/core/ui_theme.c, so the tests check the arithmetic and not a
 * helper they share with the code under test. */
static inline unsigned int test_blend(unsigned int fg, unsigned int bg, float a)
{
    unsigned int out = 0;
    for (int sh = 16; sh >= 0; sh -= 8) {
        float f = (float)((fg >> sh) & 0xFFu), b = (float)((bg >> sh) & 0xFFu);
        out |= ((unsigned int)(f * a + b * (1.0f - a)) & 0xFFu) << sh;
    }
    return out;
}

#endif /* NUTSHELL_TEST_BLEND_H */
