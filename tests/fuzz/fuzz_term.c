/* libFuzzer target: the terminal emulator fed raw bytes, as a remote host
 * (or a ConPTY child) can send them, plus two command bytes taken out of the
 * data stream so the buffer's resize and allocation-failure paths are
 * reachable (spec 2026-11-05-settings-help-design.md, item C):
 *
 *   0xFF r c   resize to 1 + r % 8 rows by 1 + c % 16 columns
 *   0xFE k     make allocation number 1 + k % 4, counted from now, fail once
 *
 * 0xFE and 0xFF are never valid UTF-8, so the parser loses little. A command
 * byte with too few bytes after it is data. After every run of data and every
 * resize the ring invariant is checked: exactly the live part holds rows. */
#include "term.h"
#include "term_extract.h"

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static int g_fail_in;   /* allocator calls left up to and including the failing one; 0 = disarmed */

static void *fuzz_calloc(size_t nmemb, size_t size)
{
    if (g_fail_in > 0 && --g_fail_in == 0) return NULL;
    return calloc(nmemb, size);
}

/* Exactly the live part [start, start + count) mod cap holds rows. */
static int seg_ok(TermRow *const *lines, int cap, int start, int count)
{
    for (int i = 0; i < cap; i++) {
        int in = ((i - start + cap) % cap) < count;
        if (in != (lines[i] != NULL)) return 0;
    }
    return 1;
}

static void check_invariant(const Terminal *t)
{
    if (!seg_ok(t->lines, t->lines_capacity, t->lines_start, term_logical_rows(t)))
        __builtin_trap();
    if (t->alt_screen_active &&
        !seg_ok(t->primary_lines, t->primary_lines_capacity,
                t->primary_lines_start,
                t->primary_lines_count > t->rows ? t->primary_lines_count : t->rows))
        __builtin_trap();
}

/* The absolute-line reader, under whatever state the allocation failures
 * left: it allocates only its result, and reads only the live ring. */
static void read_abs(const Terminal *t)
{
    free(term_extract_abs_range_dup(t, 0, term_abs_cursor_line(t) + 1,
                                    NULL, NULL, NULL));
}

static void feed_run(Terminal *t, const uint8_t *data, size_t from, size_t to)
{
    if (to > from) {
        uint32_t erase = t->erase_seq;
        term_reset_cursor_low(t);   /* a live mark: the parser has something to lower */
        term_process(t, (const char *)data + from, to - from);
        check_invariant(t);
        /* The capture counters: erase_seq never goes back; the low-water mark
         * never sits above the cursor's line on the primary screen. */
        if (t->erase_seq < erase) __builtin_trap();
        if (!t->alt_screen_active && t->cursor_low_line > term_abs_cursor_line(t))
            __builtin_trap();
        read_abs(t);
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    Terminal *t = term_init(4, 10, 6);
    if (!t) return 0;
    g_fail_in = 0;
    term_set_calloc(fuzz_calloc);

    size_t run = 0;   /* start of the pending run of data */
    size_t i = 0;
    while (i < size) {
        if (data[i] == 0xFF && i + 2 < size) {
            feed_run(t, data, run, i);
            int new_rows = 1 + data[i + 1] % 8, new_cols = 1 + data[i + 2] % 16;
            uint32_t seq = t->reflow_seq, erase = t->erase_seq;
            uint64_t low = t->cursor_low_line;
            int changed = new_rows != t->rows || new_cols != t->cols;
            bool ok = term_resize(t, new_rows, new_cols);
            /* One reflow for a resize that committed, none for one that did
             * not or that had nothing to do. */
            if (t->reflow_seq != seq + (ok && changed ? 1u : 0u)) __builtin_trap();
            /* A resize never touches erase_seq; a failed or empty one leaves
             * the low-water mark too (a committed one resets it to 0). */
            if (t->erase_seq != erase) __builtin_trap();
            if (!(ok && changed) && t->cursor_low_line != low) __builtin_trap();
            if (ok && changed && t->cursor_low_line != 0) __builtin_trap();
            check_invariant(t);
            term_reset_cursor_low(t);
            i += 3;
            run = i;
        } else if (data[i] == 0xFE && i + 1 < size) {
            feed_run(t, data, run, i);
            g_fail_in = 1 + data[i + 1] % 4;
            i += 2;
            run = i;
        } else {
            i++;
        }
    }
    feed_run(t, data, run, size);

    term_set_calloc(NULL);
    term_free(t);
    return 0;
}
