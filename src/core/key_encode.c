/* src/core/key_encode.c -- xterm-style key encoding. See key_encode.h for
 * the full contract; this file is table-driven off that comment block. */
#include "key_encode.h"
#include <stdio.h>
#include <string.h>

/* Letters for Up/Down/Right/Left, indexed by (key - NSK_UP). */
static const char ARROW_LETTER[4] = { 'A', 'B', 'C', 'D' };
/* SS3 letters for F1-F4, indexed by (key - NSK_F1). */
static const char F1_F4_LETTER[4] = { 'P', 'Q', 'R', 'S' };
/* CSI ... ~ codes for Insert/Delete/PgUp/PgDn, indexed by (key - NSK_INSERT). */
static const int INS_DEL_PG_CODE[4] = { 2, 3, 5, 6 };
/* CSI ... ~ codes for F5-F12, indexed by (key - NSK_F5). */
static const int F5_F12_CODE[8] = { 15, 17, 18, 19, 20, 21, 23, 24 };

/* Up/Down/Right/Left/Home/End share this shape: an unmodified press is
 * "ESC [ letter", or "ESC O letter" when app_cursor is set; a modified
 * press is always "ESC [ 1 ; m letter" regardless of app_cursor. Passing
 * app_cursor as "always true" gives F1-F4's shape instead, since their
 * unmodified form is always SS3. `mods` is already masked to 0..7. */
static size_t cursor_key(char letter, unsigned int mods, int app_cursor,
                          char *buf, size_t bufsize)
{
    if (mods == 0) {
        if (bufsize < 3) return 0;
        buf[0] = 0x1B;
        buf[1] = app_cursor ? 'O' : '[';
        buf[2] = letter;
        return 3;
    }
    {
        int w = snprintf(buf, bufsize, "\x1b[1;%u%c", 1u + mods, letter);
        return (w > 0) ? (size_t)w : 0;
    }
}

/* Insert/Delete/PgUp/PgDn and F5-F12 share this shape: "ESC [ code ~", or
 * "ESC [ code ; m ~" when modified. `mods` is already masked to 0..7. */
static size_t csi_tilde(int code, unsigned int mods, char *buf, size_t bufsize)
{
    int w = (mods == 0)
        ? snprintf(buf, bufsize, "\x1b[%d~", code)
        : snprintf(buf, bufsize, "\x1b[%d;%u~", code, 1u + mods);
    return (w > 0) ? (size_t)w : 0;
}

size_t key_encode(NsKey key, unsigned char ch, unsigned int mods,
                   unsigned int flags, char *out, size_t size)
{
    char buf[KEY_ENCODE_MAX];
    size_t len;

    if (out == NULL || key == NSK_NONE || key >= NSK_COUNT) return 0;

    mods &= (NSK_MOD_SHIFT | NSK_MOD_ALT | NSK_MOD_CTRL);
    flags &= (NSK_FLAG_APP_CURSOR | NSK_FLAG_LOCAL);

    switch (key) {
    case NSK_UP:
    case NSK_DOWN:
    case NSK_RIGHT:
    case NSK_LEFT:
        len = cursor_key(ARROW_LETTER[key - NSK_UP], mods,
                          (flags & NSK_FLAG_APP_CURSOR) != 0, buf, sizeof(buf));
        break;

    case NSK_HOME:
        len = cursor_key('H', mods, (flags & NSK_FLAG_APP_CURSOR) != 0, buf, sizeof(buf));
        break;

    case NSK_END:
        len = cursor_key('F', mods, (flags & NSK_FLAG_APP_CURSOR) != 0, buf, sizeof(buf));
        break;

    case NSK_INSERT:
    case NSK_DELETE:
    case NSK_PGUP:
    case NSK_PGDN:
        len = csi_tilde(INS_DEL_PG_CODE[key - NSK_INSERT], mods, buf, sizeof(buf));
        break;

    case NSK_F1:
    case NSK_F2:
    case NSK_F3:
    case NSK_F4:
        /* F1-F4's unmodified form is always SS3, independent of DECCKM. */
        len = cursor_key(F1_F4_LETTER[key - NSK_F1], mods, 1, buf, sizeof(buf));
        break;

    case NSK_F5:
    case NSK_F6:
    case NSK_F7:
    case NSK_F8:
    case NSK_F9:
    case NSK_F10:
    case NSK_F11:
    case NSK_F12:
        len = csi_tilde(F5_F12_CODE[key - NSK_F5], mods, buf, sizeof(buf));
        break;

    case NSK_TAB: {
        size_t p = 0;
        if (mods & NSK_MOD_ALT) buf[p++] = 0x1B;
        if (mods & NSK_MOD_SHIFT) {
            buf[p++] = 0x1B;
            buf[p++] = '[';
            buf[p++] = 'Z';
        } else {
            buf[p++] = 0x09;
        }
        len = p;
        break;
    }

    case NSK_BACKSPACE: {
        size_t p = 0;
        unsigned char base = (flags & NSK_FLAG_LOCAL) ? 0x7Fu : 0x08u;
        if (mods & NSK_MOD_ALT) buf[p++] = 0x1B;
        buf[p++] = (char)base;
        len = p;
        break;
    }

    case NSK_CHAR: {
        unsigned char c = ch;
        size_t p = 0;
        if (mods & NSK_MOD_CTRL) {
            if ((c >= '@' && c <= '~') || c == ' ')
                c = (unsigned char)(c & 0x1Fu);
            else if (c == '2')
                c = 0x00u;
            else if (c >= '3' && c <= '7')
                c = (unsigned char)(c - '3' + 0x1B);
            else if (c == '8')
                c = 0x7Fu;
            else if (c == '/')
                c = 0x1Fu;
            /* else: left unchanged, including an already-control byte. */
        }
        if (mods & NSK_MOD_ALT) buf[p++] = 0x1B;
        buf[p++] = (char)c;
        len = p;
        break;
    }

    default:
        return 0;
    }

    if (len == 0 || len > size) return 0;
    memcpy(out, buf, len);
    return len;
}
