/* src/core/text_runs.h */
#ifndef NUTSHELL_TEXT_RUNS_H
#define NUTSHELL_TEXT_RUNS_H

/*
 * The geometry the chat panel's paint pass records for character selection
 * (spec docs/superpowers/specs/2026-10-18-ai-panel-text-selection-design.md
 * sections 3, 4 and 6). Every word the renderer draws is appended as a run:
 * its rect in list-view client pixels, its byte range into the source text
 * of one part of one message, and the cumulative pixel width at each UTF-8
 * character boundary inside it. The mouse is hit-tested against these by
 * lookup; nothing here measures text. Portable, no windows.h.
 */

/* Parts of a message. Ordered as they are painted (the Thinking body sits
 * above the reply text), so (message id, part, offset) compares in reading
 * order. */
#define TR_PART_THINKING 0
#define TR_PART_MAIN     1

/* Run flags. */
#define TR_FLAG_CODE 1   /* a fenced code line: double-click takes a token */

typedef struct {
    int x, y, w, h;      /* list-local pixels, as painted this frame */
    int start, end;      /* byte range into the part's source text, end
                          * exclusive; a synthetic run spans the source bytes
                          * of its marker ("- ", "12. "), which is what the
                          * copy carries when the range covers them */
    int part;            /* TR_PART_MAIN or TR_PART_THINKING */
    int line;            /* visual line: runs with the same y and part share it */
    int first_cum;       /* index into cum[]/cum_byte[] of the first boundary */
    int n_cum;           /* boundaries: UTF-8 characters + 1; 0 for synthetic */
    int flags;           /* TR_FLAG_* */
    char synthetic[8];   /* "- " or "3. " for a bullet or number, else "" */
} TextRun;

typedef struct {
    TextRun *v; int n, cap;
    int *cum;        /* cumulative pixel width at each boundary (0 first) */
    int *cum_byte;   /* byte offset from the run's start at each boundary */
    int cum_n, cum_cap;
    int line_count;
} TextRuns;

void text_runs_init(TextRuns *r);
/* Forget every run, keep the memory (called at the start of every paint). */
void text_runs_reset(TextRuns *r);
void text_runs_free(TextRuns *r);

/* Append one run. x, y, w, h, start, end, part, flags and synthetic come
 * from *proto; line, first_cum and n_cum are assigned here. `cum` and
 * `cum_byte` hold n_cum boundaries (none for a synthetic run). A run with
 * the same y and part as the previous one continues its visual line; any
 * other run starts a new line. Returns the run's index, or -1 when memory
 * runs out (nothing is appended then). */
int text_runs_append(TextRuns *r, const TextRun *proto,
                     const int *cum, const int *cum_byte, int n_cum);

/* Boundaries of the UTF-8 string s[0, len) from the cumulative extents
 * GetTextExtentExPointW reports for its UTF-16 form (dx16[i] is the width
 * of the first i + 1 UTF-16 units, n16 of them). Writes one boundary per
 * character plus the leading 0: cum[k] is the width and cum_byte[k] the
 * byte offset after k characters. A four-byte sequence is one character and
 * two UTF-16 units (a surrogate pair); an invalid byte is one character and
 * one unit, as MultiByteToWideChar replaces it with U+FFFD. Units past n16
 * take the last width. Returns the number written, or -1 when it would pass
 * `cap` or an argument is NULL. */
int text_runs_cum_from_utf16(const char *s, int len, const int *dx16, int n16,
                             int *cum, int *cum_byte, int cap);

/* Bytes of s[0, len) that make up the first n16 UTF-16 units, whole
 * characters only (a surrogate pair is never split). */
int text_runs_utf8_prefix_for_utf16(const char *s, int len, int n16);

/* The part of a run's pixels that lies inside the selection [sel_start,
 * sel_end): *x0 and *x1 relative to the run's x, at character boundaries.
 * Returns 1 when that part is non-empty, 0 otherwise (also for n_cum < 2). */
int text_runs_span_px(int run_start, int run_end,
                      const int *cum, const int *cum_byte, int n_cum,
                      int sel_start, int sel_end, int *x0, int *x1);

/* Spec section 4. The byte offset in the source of the part the point falls
 * in, the part written to *part_out (may be NULL); -1 when there are no runs.
 *  - The visual line is the first run whose [y, y + h) holds the point, and
 *    every run with that y and part; above every run the first run's start,
 *    below every run the last run's end; in a gap between lines the line
 *    above.
 *  - On the line: left of the first run its start, right of the last run
 *    its end, between two runs the end of the left one, inside a run the
 *    nearest character boundary.
 *  - A synthetic run (bullet, number) gives the start of the next real run
 *    on its line, or its own end when there is none. */
int text_runs_hit(const TextRuns *r, int x, int y, int *part_out);

/* Index of the run whose rect holds the point, or -1. */
int text_runs_run_at(const TextRuns *r, int x, int y);

/* Double-click: the word at byte `off` of `run` in text[0, text_len). A
 * prose run gives its own bytes less trailing spaces and tabs; a code run
 * (TR_FLAG_CODE) gives the whitespace-delimited token holding `off` (or
 * ending at it); a synthetic run gives nothing (*s == *e). */
void text_runs_word(const char *text, int text_len, const TextRun *run,
                    int off, int *s, int *e);

/* Triple-click: the source line holding byte `off` of text[0, len), as
 * [*s, *e) without its '\n'. */
void text_line_at(const char *text, int len, int off, int *s, int *e);

/* Line iterator for the plain (MD_PLAIN) layout: set *pos = 0 and call
 * until it returns 0. Each line is [*ls, *le), its '\n' excluded and also a
 * '\r' right before that '\n' (a CR LF ends a line; a lone '\r' elsewhere
 * is text). A text ending in '\n' has no empty line after it. */
int text_next_line(const char *s, int len, int *pos, int *ls, int *le);

#endif /* NUTSHELL_TEXT_RUNS_H */
