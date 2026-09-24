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
 * (0x00-0x1F) except TAB, LF and CR, DEL (0x7F), every C1 control
 * (U+0080-U+009F, which arrives UTF-8-encoded as 0xC2 0x80..0x9F), and
 * every bidi override/isolate character (U+202A-U+202E, U+2066-U+2069,
 * UTF-8-encoded as E2 80 AA..AE / E2 81 A6..A9) from a UTF-8 byte buffer.
 * The bidi controls are stripped for the same reason as the others: a
 * hostile paste could use them to make text read, in the confirm dialog or
 * a shell prompt, in a different order than the bytes actually sent
 * ("Trojan Source", CVE-2021-42574). `out` may alias `in` -- filtering only
 * ever removes bytes, so in-place compaction is always safe. Returns the
 * filtered length (<= in_len); *removed, if non-NULL, receives the count of
 * characters removed (a multi-byte control's encoding counts once, not once
 * per byte). A NULL `in` is treated as a zero-length input. */
size_t paste_filter_controls(const char *in, size_t in_len, char *out, size_t *removed);

/* Build a display copy of raw text for the paste-confirm dialog: every
 * character paste_filter_controls() would remove is replaced, in place, by
 * a visible marker -- a C0/C1/DEL control becomes its Unicode "control
 * picture" (U+2400 + code for a C0 control or a C1 control's low 7 bits,
 * U+2421 for DEL), UTF-8 encoded; a bidi override/isolate becomes a short
 * bracketed tag ("[LRO]", "[RLI]", ...) naming it, since Unicode has no
 * single-codepoint picture for those and inserting the raw character would
 * just make the *preview* reorder too -- so the dialog can show exactly
 * what a hostile paste contained without ever sending, interpreting, or
 * visually reordering around it. TAB, LF and CR pass through unchanged,
 * matching paste_filter_controls(). Returns a malloc'd, NUL-terminated
 * buffer (the caller frees it), or NULL on allocation failure or a NULL
 * `raw`; *out_removed, if non-NULL, receives the same count
 * paste_filter_controls() would report for the same input. */
char *paste_visualize_controls(const char *raw, size_t raw_len, size_t *out_removed);

/* Shared by the AI command-execution paths (src/core/chat_approval.c,
 * src/core/ai_prompt.c, src/ui/ai_chat.c): true if `s` (NUL-terminated
 * UTF-8) contains a byte or sequence paste_filter_controls() would strip --
 * any C0 control (0x00-0x1F, including TAB/LF/CR: a command sent to
 * execute_command() must be exactly one line, so none of them are
 * legitimate here, unlike in a paste), DEL (0x7F), a UTF-8-encoded C1
 * control (U+0080-U+009F), or a bidi override/isolate character
 * (U+202A-U+202E, U+2066-U+2069) that could make the command shown to the
 * user in an approval card read differently from the command actually
 * sent. A NULL `s` is not unsafe (returns 0). */
int text_has_unsafe_command_char(const char *s);

#endif
