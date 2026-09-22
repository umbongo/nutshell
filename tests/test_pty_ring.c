#include "test_framework.h"
#include "pty_ring.h"
#include <stdlib.h>
#include <string.h>

/* ===========================================================================
 * pty_ring tests -- see docs/superpowers/specs/2026-09-22-local-shell-design.md
 * section 3 ("Reading") and section 8.
 * ===========================================================================
 */

int test_pty_ring_init_free(void)
{
    TEST_BEGIN();
    PtyRing r;
    ASSERT_EQ(pty_ring_init(&r, 0), 0);
    ASSERT_NOT_NULL(r.buf);
    ASSERT_EQ(r.cap, PTY_RING_CAP);
    ASSERT_EQ(r.head, (size_t)0);
    ASSERT_EQ(r.len, (size_t)0);
    ASSERT_EQ(r.eof, 0);
    ASSERT_EQ(r.full, 0);
    pty_ring_free(&r);
    ASSERT_NULL(r.buf);
    ASSERT_EQ(r.cap, (size_t)0);
    TEST_END();
}

int test_pty_ring_init_explicit_small_cap(void)
{
    TEST_BEGIN();
    PtyRing r;
    ASSERT_EQ(pty_ring_init(&r, 8), 0);
    ASSERT_NOT_NULL(r.buf);
    ASSERT_EQ(r.cap, (size_t)8);
    pty_ring_free(&r);
    TEST_END();
}

int test_pty_ring_double_free(void)
{
    TEST_BEGIN();
    PtyRing r;
    ASSERT_EQ(pty_ring_init(&r, 8), 0);
    pty_ring_free(&r);
    pty_ring_free(&r); /* must not crash */
    ASSERT_NULL(r.buf);
    TEST_END();
}

int test_pty_ring_null_safety(void)
{
    TEST_BEGIN();
    unsigned char buf[4] = {0};

    /* NULL ring pointer on every entry point. */
    ASSERT_EQ(pty_ring_init(NULL, 8), -1);
    pty_ring_free(NULL);
    ASSERT_EQ(pty_ring_push(NULL, buf, 4), (size_t)0);
    ASSERT_EQ(pty_ring_drain(NULL, buf, 4), (size_t)0);
    ASSERT_EQ(pty_ring_available(NULL), (size_t)0);
    ASSERT_EQ(pty_ring_space(NULL), (size_t)0);
    pty_ring_set_eof(NULL);
    ASSERT_EQ(pty_ring_eof(NULL), 0);
    ASSERT_EQ(pty_ring_full(NULL), 0);
    pty_ring_clear_full(NULL);
    pty_ring_reset(NULL);

    /* Un-inited ring (buf == NULL) on every entry point. */
    PtyRing r;
    memset(&r, 0, sizeof(r));
    ASSERT_EQ(pty_ring_push(&r, buf, 4), (size_t)0);
    ASSERT_EQ(pty_ring_drain(&r, buf, 4), (size_t)0);
    ASSERT_EQ(pty_ring_available(&r), (size_t)0);
    ASSERT_EQ(pty_ring_space(&r), (size_t)0);
    ASSERT_EQ(pty_ring_eof(&r), 0);
    ASSERT_EQ(pty_ring_full(&r), 0);
    pty_ring_set_eof(&r); /* eof is not gated on buf; allowed to set */
    ASSERT_EQ(pty_ring_eof(&r), 1);
    pty_ring_clear_full(&r);
    pty_ring_reset(&r); /* no buf: no-op, must not crash */
    ASSERT_NULL(r.buf);

    /* NULL data / NULL out on an inited ring. */
    ASSERT_EQ(pty_ring_init(&r, 16), 0);
    ASSERT_EQ(pty_ring_push(&r, NULL, 4), (size_t)0);
    ASSERT_EQ(pty_ring_drain(&r, NULL, 4), (size_t)0);
    pty_ring_free(&r);
    TEST_END();
}

int test_pty_ring_push_drain_roundtrip(void)
{
    TEST_BEGIN();
    PtyRing r;
    ASSERT_EQ(pty_ring_init(&r, 0), 0);

    const char *msg = "hello, nutshell";
    size_t msg_len = strlen(msg);
    ASSERT_EQ(pty_ring_push(&r, msg, msg_len), msg_len);
    ASSERT_EQ(pty_ring_available(&r), msg_len);

    char out[64];
    size_t got = pty_ring_drain(&r, out, sizeof(out));
    ASSERT_EQ(got, msg_len);
    ASSERT_TRUE(memcmp(out, msg, msg_len) == 0);
    ASSERT_EQ(pty_ring_available(&r), (size_t)0);

    pty_ring_free(&r);
    TEST_END();
}

int test_pty_ring_push_wraps_buffer_end(void)
{
    TEST_BEGIN();
    PtyRing r;
    ASSERT_EQ(pty_ring_init(&r, 8), 0);

    /* Fill to 6 of 8, drain 4, so head sits at 4. */
    unsigned char fill[6] = {1, 2, 3, 4, 5, 6};
    ASSERT_EQ(pty_ring_push(&r, fill, 6), (size_t)6);

    unsigned char tmp[4];
    ASSERT_EQ(pty_ring_drain(&r, tmp, 4), (size_t)4);
    ASSERT_EQ(r.head, (size_t)4);
    ASSERT_EQ(r.len, (size_t)2); /* bytes 5, 6 remain */

    /* Push 6 more: tail starts at (4+2)%8 == 6, so this wraps around. */
    unsigned char more[6] = {7, 8, 9, 10, 11, 12};
    ASSERT_EQ(pty_ring_push(&r, more, 6), (size_t)6);
    ASSERT_EQ(r.len, (size_t)8);

    unsigned char out[8];
    ASSERT_EQ(pty_ring_drain(&r, out, sizeof(out)), (size_t)8);
    unsigned char expected[8] = {5, 6, 7, 8, 9, 10, 11, 12};
    ASSERT_TRUE(memcmp(out, expected, 8) == 0);
    ASSERT_EQ(pty_ring_available(&r), (size_t)0);

    pty_ring_free(&r);
    TEST_END();
}

int test_pty_ring_fill_exactly_to_cap(void)
{
    TEST_BEGIN();
    PtyRing r;
    ASSERT_EQ(pty_ring_init(&r, 8), 0);

    unsigned char data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    ASSERT_EQ(pty_ring_push(&r, data, 8), (size_t)8);
    ASSERT_EQ(pty_ring_full(&r), 0);
    ASSERT_EQ(pty_ring_space(&r), (size_t)0);
    ASSERT_EQ(pty_ring_available(&r), (size_t)8);

    pty_ring_free(&r);
    TEST_END();
}

int test_pty_ring_overflow_sets_full_and_keeps_prefix(void)
{
    TEST_BEGIN();
    PtyRing r;
    ASSERT_EQ(pty_ring_init(&r, 8), 0);

    unsigned char data[12];
    for (int i = 0; i < 12; i++) {
        data[i] = (unsigned char)(i + 1);
    }

    size_t took = pty_ring_push(&r, data, 12);
    ASSERT_EQ(took, (size_t)8);
    ASSERT_EQ(pty_ring_full(&r), 1);
    ASSERT_EQ(pty_ring_space(&r), (size_t)0);

    unsigned char out[8];
    ASSERT_EQ(pty_ring_drain(&r, out, sizeof(out)), (size_t)8);
    ASSERT_TRUE(memcmp(out, data, 8) == 0); /* the accepted prefix is intact */

    pty_ring_clear_full(&r);
    ASSERT_EQ(pty_ring_full(&r), 0);

    pty_ring_free(&r);
    TEST_END();
}

int test_pty_ring_push_zero_len_and_null_data(void)
{
    TEST_BEGIN();
    PtyRing r;
    ASSERT_EQ(pty_ring_init(&r, 8), 0);

    unsigned char data[4] = {1, 2, 3, 4};
    ASSERT_EQ(pty_ring_push(&r, data, 0), (size_t)0);
    ASSERT_EQ(pty_ring_full(&r), 0); /* a zero-length push is not a drop */
    ASSERT_EQ(pty_ring_push(&r, NULL, 4), (size_t)0);
    ASSERT_EQ(pty_ring_full(&r), 0);
    ASSERT_EQ(pty_ring_available(&r), (size_t)0);

    pty_ring_free(&r);
    TEST_END();
}

int test_pty_ring_drain_partial_and_empty(void)
{
    TEST_BEGIN();
    PtyRing r;
    ASSERT_EQ(pty_ring_init(&r, 16), 0);

    unsigned char out[4];
    ASSERT_EQ(pty_ring_drain(&r, out, sizeof(out)), (size_t)0); /* empty ring */

    unsigned char data[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    ASSERT_EQ(pty_ring_push(&r, data, 10), (size_t)10);

    unsigned char part[3];
    ASSERT_EQ(pty_ring_drain(&r, part, sizeof(part)), (size_t)3);
    ASSERT_TRUE(memcmp(part, data, 3) == 0);
    ASSERT_EQ(pty_ring_available(&r), (size_t)7);

    unsigned char rest[16];
    ASSERT_EQ(pty_ring_drain(&r, rest, sizeof(rest)), (size_t)7);
    ASSERT_TRUE(memcmp(rest, data + 3, 7) == 0);
    ASSERT_EQ(pty_ring_available(&r), (size_t)0);

    ASSERT_EQ(pty_ring_drain(&r, out, sizeof(out)), (size_t)0); /* empty again */

    pty_ring_free(&r);
    TEST_END();
}

int test_pty_ring_interleaved_push_drain_pattern(void)
{
    TEST_BEGIN();
    PtyRing r;
    ASSERT_EQ(pty_ring_init(&r, 32), 0);

    /* A byte pattern (counter mod 251) pushed and drained in irregular
     * chunks across ~5x the capacity, verified end to end. */
    size_t total = 32 * 5 + 7;
    unsigned char *sent = malloc(total);
    unsigned char *received = malloc(total);
    ASSERT_NOT_NULL(sent);
    ASSERT_NOT_NULL(received);

    for (size_t i = 0; i < total; i++) {
        sent[i] = (unsigned char)(i % 251);
    }

    size_t sent_pos = 0;
    size_t recv_pos = 0;
    size_t chunk_sizes[] = {5, 3, 11, 1, 9, 2, 17};
    size_t chunk_idx = 0;

    while (recv_pos < total) {
        if (sent_pos < total) {
            size_t chunk = chunk_sizes[chunk_idx % (sizeof(chunk_sizes) / sizeof(chunk_sizes[0]))];
            chunk_idx++;
            if (chunk > total - sent_pos) {
                chunk = total - sent_pos;
            }
            size_t took = pty_ring_push(&r, sent + sent_pos, chunk);
            sent_pos += took;
        }

        size_t drain_chunk = chunk_sizes[(chunk_idx + 1) % (sizeof(chunk_sizes) / sizeof(chunk_sizes[0]))];
        size_t got = pty_ring_drain(&r, received + recv_pos, drain_chunk);
        recv_pos += got;

        /* Invariant holds at every step. */
        ASSERT_EQ(pty_ring_available(&r) + pty_ring_space(&r), r.cap);

        if (got == 0 && sent_pos >= total) {
            break; /* nothing left to push or drain */
        }
    }

    ASSERT_EQ(sent_pos, total);
    ASSERT_EQ(recv_pos, total);
    ASSERT_TRUE(memcmp(sent, received, total) == 0);
    ASSERT_EQ(pty_ring_full(&r), 0); /* never overran with this pacing */

    free(sent);
    free(received);
    pty_ring_free(&r);
    TEST_END();
}

int test_pty_ring_eof_flag(void)
{
    TEST_BEGIN();
    PtyRing r;
    ASSERT_EQ(pty_ring_init(&r, 8), 0);

    ASSERT_EQ(pty_ring_eof(&r), 0);
    pty_ring_set_eof(&r);
    ASSERT_EQ(pty_ring_eof(&r), 1);

    /* Draining does not clear eof. */
    unsigned char data[4] = {1, 2, 3, 4};
    ASSERT_EQ(pty_ring_push(&r, data, 4), (size_t)4);
    unsigned char out[4];
    ASSERT_EQ(pty_ring_drain(&r, out, 4), (size_t)4);
    ASSERT_EQ(pty_ring_eof(&r), 1);

    pty_ring_free(&r);
    TEST_END();
}

int test_pty_ring_reset_clears_flags_free_zeroes_all(void)
{
    TEST_BEGIN();
    PtyRing r;
    ASSERT_EQ(pty_ring_init(&r, 8), 0);

    unsigned char data[12];
    for (int i = 0; i < 12; i++) {
        data[i] = (unsigned char)(i + 1);
    }
    pty_ring_push(&r, data, 12); /* overflows: sets full */
    pty_ring_set_eof(&r);
    ASSERT_EQ(pty_ring_full(&r), 1);
    ASSERT_EQ(pty_ring_eof(&r), 1);
    ASSERT_TRUE(r.len > 0);

    unsigned char *buf_before = r.buf;
    size_t cap_before = r.cap;
    pty_ring_reset(&r);
    ASSERT_EQ(r.head, (size_t)0);
    ASSERT_EQ(r.len, (size_t)0);
    ASSERT_EQ(r.eof, 0);
    ASSERT_EQ(r.full, 0);
    ASSERT_TRUE(r.buf == buf_before); /* allocation kept */
    ASSERT_EQ(r.cap, cap_before);

    pty_ring_free(&r);
    ASSERT_NULL(r.buf);
    ASSERT_EQ(r.cap, (size_t)0);
    ASSERT_EQ(r.head, (size_t)0);
    ASSERT_EQ(r.len, (size_t)0);
    ASSERT_EQ(r.eof, 0);
    ASSERT_EQ(r.full, 0);
    TEST_END();
}

int test_pty_ring_available_space_invariant(void)
{
    TEST_BEGIN();
    PtyRing r;
    ASSERT_EQ(pty_ring_init(&r, 16), 0);
    ASSERT_EQ(pty_ring_available(&r) + pty_ring_space(&r), r.cap);

    unsigned char data[6] = {1, 2, 3, 4, 5, 6};
    pty_ring_push(&r, data, 6);
    ASSERT_EQ(pty_ring_available(&r) + pty_ring_space(&r), r.cap);

    unsigned char out[3];
    pty_ring_drain(&r, out, 3);
    ASSERT_EQ(pty_ring_available(&r) + pty_ring_space(&r), r.cap);

    pty_ring_push(&r, data, 6); /* wraps */
    ASSERT_EQ(pty_ring_available(&r) + pty_ring_space(&r), r.cap);

    pty_ring_push(&r, data, 6); /* overflows */
    ASSERT_EQ(pty_ring_available(&r) + pty_ring_space(&r), r.cap);

    pty_ring_free(&r);
    TEST_END();
}
