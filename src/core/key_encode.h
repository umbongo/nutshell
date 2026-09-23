/* src/core/key_encode.h -- the bytes a key press sends to the shell.
 *
 * Pure: no Win32, no terminal state. The window maps its virtual-key codes
 * and GetKeyState() onto the identifiers below and asks key_encode() for the
 * bytes; both transports (SSH and the ConPTY local shell) are raw byte
 * pass-throughs, so this table is the whole of the keyboard protocol.
 *
 * The encoding is xterm's (docs/superpowers/specs/2026-09-23-special-keys-
 * design.md, section 1, option A):
 *
 *   modifier parameter m = 1 + (Shift 1) + (Alt 2) + (Ctrl 4), omitted at 1
 *   Up/Down/Right/Left  CSI A-D; SS3 A-D in application cursor mode;
 *                       CSI 1;m A-D with modifiers (either mode)
 *   Home/End            CSI H / CSI F; SS3 H / SS3 F in application mode;
 *                       CSI 1;m H/F with modifiers
 *   Insert/Delete       CSI 2~ / CSI 3~, CSI 2;m~ / CSI 3;m~
 *   PgUp/PgDn           CSI 5~ / CSI 6~, CSI 5;m~ / CSI 6;m~
 *   F1-F4               SS3 P-S, CSI 1;m P-S with modifiers
 *   F5-F12              CSI 15/17/18/19/20/21/23/24 ~, with ;m before the ~
 *   Tab                 0x09; Shift+Tab CSI Z; Alt prefixes ESC; Ctrl ignored
 *   Backspace           0x7F for a local session, 0x08 for SSH (section 7);
 *                       Alt prefixes ESC; Ctrl and Shift are ignored
 *   a character         the byte itself; with Ctrl, X11's control mapping
 *                       (below); with Alt, ESC then the (possibly
 *                       control-mapped) byte -- so Ctrl+Alt+f is ESC 0x06
 *
 * X11's control mapping (XLookupString, which xterm uses) for NSK_CHAR with
 * NSK_MOD_CTRL: '@'..'~' and ' ' -> c & 0x1F; '2' -> 0x00; '3'..'7' ->
 * 0x1B..0x1F; '8' -> 0x7F; '/' -> 0x1F; anything else (including a byte that
 * is already a control character, as WM_CHAR delivers Ctrl+letter) is left
 * unchanged. Shift is never applied to a character: the keyboard layout has
 * already done that by the time a character exists.
 *
 * CSI is ESC [, SS3 is ESC O. Nothing is NUL-terminated: Ctrl+Space is the
 * single byte 0x00, so callers use the returned length.
 */
#ifndef NUTSHELL_KEY_ENCODE_H
#define NUTSHELL_KEY_ENCODE_H

#include <stddef.h>

typedef enum {
    NSK_NONE = 0,
    NSK_CHAR,        /* the character passed in `ch` */
    NSK_UP,
    NSK_DOWN,
    NSK_RIGHT,
    NSK_LEFT,
    NSK_HOME,
    NSK_END,
    NSK_INSERT,
    NSK_DELETE,
    NSK_PGUP,
    NSK_PGDN,
    NSK_F1,
    NSK_F2,
    NSK_F3,
    NSK_F4,
    NSK_F5,
    NSK_F6,
    NSK_F7,
    NSK_F8,
    NSK_F9,
    NSK_F10,
    NSK_F11,
    NSK_F12,
    NSK_TAB,
    NSK_BACKSPACE,
    NSK_COUNT        /* not a key: one past the last */
} NsKey;

/* Modifier bitmask. The values are xterm's, so m = 1 + mods. */
#define NSK_MOD_SHIFT 0x1u
#define NSK_MOD_ALT   0x2u
#define NSK_MOD_CTRL  0x4u

/* Terminal / transport state that changes the bytes. */
#define NSK_FLAG_APP_CURSOR 0x1u  /* DECCKM (?1h) is set */
#define NSK_FLAG_LOCAL      0x2u  /* ConPTY local session, not SSH */

/* Longest sequence key_encode() can produce ("ESC [ 2 4 ; 8 ~" is 7). */
#define KEY_ENCODE_MAX 16

/* Write the bytes for `key` (and `ch` when key is NSK_CHAR; ignored
 * otherwise) under `mods` (NSK_MOD_*) and `flags` (NSK_FLAG_*) into `out`.
 * Returns the number of bytes written. Returns 0 and writes nothing when
 * `key` is NSK_NONE or out of range, `out` is NULL, or the sequence does not
 * fit in `size`. Unknown bits in mods and flags are ignored. */
size_t key_encode(NsKey key, unsigned char ch, unsigned int mods,
                  unsigned int flags, char *out, size_t size);

#endif /* NUTSHELL_KEY_ENCODE_H */
