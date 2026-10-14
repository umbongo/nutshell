#ifndef NUTSHELL_TEXT_LINES_H
#define NUTSHELL_TEXT_LINES_H

/*
 * text_lines -- line-end normalisation. A multiline Win32 EDIT only breaks
 * lines on CR LF, so text that reaches it with bare LF (the AI transcript
 * copy joins lines with '\n') runs on as one line. Portable, no windows.h.
 */

#include <stddef.h>

/* Copies `in` to `out`, turning every bare '\n' into "\r\n". An existing
 * "\r\n" is left alone, and a lone '\r' is copied as is. NUL-terminates
 * when out_sz > 0. Returns the full length the converted text needs,
 * excluding the NUL, whether or not it fitted, so a caller can size a
 * buffer with text_lf_to_crlf(in, NULL, 0). When out_sz is too small the
 * output is truncated, never in the middle of a "\r\n" pair. `in == NULL`
 * is empty text. */
size_t text_lf_to_crlf(const char *in, char *out, size_t out_sz);

#endif /* NUTSHELL_TEXT_LINES_H */
