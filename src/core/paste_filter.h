#ifndef NUTSHELL_PASTE_FILTER_H
#define NUTSHELL_PASTE_FILTER_H

#include <stddef.h>

/* Hardening: a pasted clipboard payload is untrusted input from the user's
 * point of view -- if the remote (or a local shell) is reading it with
 * bracketed paste mode on, a hostile clipboard could still embed the
 * bracket's own close sequence (ESC [ 201 ~) or another control sequence to
 * make the terminal treat part of the paste as typed keystrokes instead of
 * literal text. xterm and Windows Terminal both strip this class of byte
 * from every paste regardless of bracketed-paste state; Nutshell does the
 * same here.
 *
 * paste_filter_controls() removes ESC (0x1B), every other C0 control
 * (0x00-0x1F) except TAB, LF and CR, DEL (0x7F), and every C1 control
 * (U+0080-U+009F, which arrives UTF-8-encoded as 0xC2 0x80..0x9F) from a
 * UTF-8 byte buffer. `out` may alias `in` -- filtering only ever removes
 * bytes, so in-place compaction is always safe. Returns the filtered
 * length (<= in_len); *removed, if non-NULL, receives the count of
 * characters removed (a C1 control's two-byte encoding counts once, not
 * twice). A NULL `in` is treated as a zero-length input. */
size_t paste_filter_controls(const char *in, size_t in_len, char *out, size_t *removed);

/* Build a display copy of raw text for the paste-confirm dialog: every
 * character paste_filter_controls() would remove is replaced, in place, by
 * its Unicode "control picture" (U+2400 + code for a C0 control or a C1
 * control's low 7 bits, U+2421 for DEL), UTF-8 encoded, so the dialog can
 * show exactly what a hostile paste contained without ever sending or
 * interpreting it. TAB, LF and CR pass through unchanged, matching
 * paste_filter_controls(). Returns a malloc'd, NUL-terminated buffer (the
 * caller frees it), or NULL on allocation failure or a NULL `raw`;
 * *out_removed, if non-NULL, receives the same count
 * paste_filter_controls() would report for the same input. */
char *paste_visualize_controls(const char *raw, size_t raw_len, size_t *out_removed);

#endif
