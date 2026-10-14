#ifndef NUTSHELL_CLIPBOARD_H
#define NUTSHELL_CLIPBOARD_H

/*
 * clipboard -- put UTF-8 text on the Windows clipboard as CF_UNICODETEXT.
 * Windows synthesises CF_TEXT from it for programs that only ask for the old
 * format, so nothing is lost; CF_TEXT written from UTF-8 bytes would be read
 * back in the ANSI codepage and garble anything outside ASCII.
 */

#ifdef _WIN32
#include <windows.h>
#include <stddef.h>

/* Set the one CF_UNICODETEXT format from `len` bytes of UTF-8. The caller
 * must already have the clipboard open (and should EmptyClipboard() first).
 * Returns 1 when the text was placed, 0 otherwise; NULL or empty text, and
 * any failure, leave the clipboard as it was. */
int clip_set_utf8(const char *utf8, size_t len);

/* Convert the text first, then open the clipboard for `owner`, replace its
 * contents and close it again. The existing contents are only emptied once
 * the new text is ready, so a failed conversion never wipes the clipboard.
 * Returns 1 when the text was placed, 0 otherwise (empty text, conversion or
 * allocation failure, or the clipboard was busy). */
int clip_copy_utf8(HWND owner, const char *utf8, size_t len);

#endif /* _WIN32 */
#endif /* NUTSHELL_CLIPBOARD_H */
