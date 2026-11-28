#include "test_framework.h"
#include "paste_write.h"

#include <string.h>

/* A recording transport: every write call is kept (up to 64) so a test can
 * see both what was written and how it was split. */
typedef struct {
    char   data[70000];
    size_t total;
    size_t sizes[64];
    int    calls;
    int    fail_after;   /* writes allowed before returning -1; < 0 = never */
    size_t short_cap;    /* when > 0, accept at most this many bytes per call */
} Rec;

static int rec_write(void *ctx, const char *d, size_t len)
{
    Rec *r = (Rec *)ctx;
    if (r->fail_after >= 0 && r->calls >= r->fail_after) { r->calls++; return -1; }
    size_t take = (r->short_cap > 0 && len > r->short_cap) ? r->short_cap : len;
    if (r->calls < 64) r->sizes[r->calls] = take;
    r->calls++;
    memcpy(r->data + r->total, d, take);
    r->total += take;
    return (int)take;
}

static void rec_init(Rec *r) { memset(r, 0, sizeof *r); r->fail_after = -1; }

int test_paste_write_empty(void)
{
    TEST_BEGIN();
    Rec r; rec_init(&r);
    WriteAgg a; write_agg_init(&a);
    ASSERT_EQ(paste_write_chunked("", 0, 0, rec_write, &r, 16, &a), (size_t)0);
    ASSERT_EQ(r.calls, 0);
    ASSERT_EQ(paste_write_chunked(NULL, 0, 0, rec_write, &r, 16, &a), (size_t)0);
    ASSERT_EQ(paste_write_chunked("x", 1, 0, NULL, &r, 16, &a), (size_t)0);
    ASSERT_EQ(a.bad, 0u);
    TEST_END();
}

int test_paste_write_one_byte(void)
{
    TEST_BEGIN();
    Rec r; rec_init(&r);
    WriteAgg a; write_agg_init(&a);
    ASSERT_EQ(paste_write_chunked("x", 1, 0, rec_write, &r, 16, &a), (size_t)1);
    ASSERT_EQ(r.calls, 1);
    ASSERT_EQ(r.total, (size_t)1);
    ASSERT_EQ(r.data[0], 'x');
    TEST_END();
}

/* A text of exactly one chunk is one write; one byte more is two. */
int test_paste_write_chunk_boundary(void)
{
    TEST_BEGIN();
    Rec r; rec_init(&r);
    WriteAgg a; write_agg_init(&a);
    ASSERT_EQ(paste_write_chunked("0123456789abcdef", 16, 0, rec_write, &r, 16, &a), (size_t)16);
    ASSERT_EQ(r.calls, 1);
    ASSERT_EQ(r.sizes[0], (size_t)16);

    rec_init(&r);
    ASSERT_EQ(paste_write_chunked("0123456789abcdefg", 17, 0, rec_write, &r, 16, &a), (size_t)17);
    ASSERT_EQ(r.calls, 2);
    ASSERT_EQ(r.sizes[0], (size_t)16);
    ASSERT_EQ(r.sizes[1], (size_t)1);
    ASSERT_TRUE(memcmp(r.data, "0123456789abcdefg", 17) == 0);
    TEST_END();
}

/* Line ends: CR dropped, LF kept for SSH, LF becomes CR for a local shell. */
int test_paste_write_line_ends(void)
{
    TEST_BEGIN();
    Rec r; rec_init(&r);
    WriteAgg a; write_agg_init(&a);
    ASSERT_EQ(paste_write_chunked("a\r\nb\n\rc", 7, 0, rec_write, &r, 64, &a), (size_t)5);
    ASSERT_TRUE(memcmp(r.data, "a\nb\nc", 5) == 0);

    rec_init(&r);
    ASSERT_EQ(paste_write_chunked("a\r\nb\n", 5, 1, rec_write, &r, 64, &a), (size_t)4);
    ASSERT_EQ(r.total, (size_t)4);
    ASSERT_TRUE(memcmp(r.data, "a\rb\r", 4) == 0);
    TEST_END();
}

/* A 3-byte UTF-8 character that would straddle the limit moves to the next
 * chunk whole. */
int test_paste_write_utf8_not_split(void)
{
    TEST_BEGIN();
    Rec r; rec_init(&r);
    WriteAgg a; write_agg_init(&a);
    /* "abc" + U+20AC (E2 82 AC) + "d" with a limit of 5: abc+2 bytes of the
     * euro would fit, so the euro must start the second chunk. */
    const char *s = "abc\xE2\x82\xAC" "d";
    ASSERT_EQ(paste_write_chunked(s, 7, 0, rec_write, &r, 5, &a), (size_t)7);
    ASSERT_EQ(r.calls, 2);
    ASSERT_EQ(r.sizes[0], (size_t)3);
    ASSERT_EQ(r.sizes[1], (size_t)4);
    ASSERT_TRUE(memcmp(r.data, s, 7) == 0);

    /* 4-byte sequences too, and a tiny limit is raised to 4. */
    rec_init(&r);
    const char *e = "\xF0\x9F\x98\x80\xF0\x9F\x98\x80";
    ASSERT_EQ(paste_write_chunked(e, 8, 0, rec_write, &r, 1, &a), (size_t)8);
    ASSERT_EQ(r.calls, 2);
    ASSERT_EQ(r.sizes[0], (size_t)4);
    TEST_END();
}

/* Malformed input (stray continuation, truncated lead) is passed through and
 * never loses a byte. */
int test_paste_write_malformed_utf8_passthrough(void)
{
    TEST_BEGIN();
    Rec r; rec_init(&r);
    WriteAgg a; write_agg_init(&a);
    const char *s = "\x80\x80" "a\xE2\x82";
    ASSERT_EQ(paste_write_chunked(s, 5, 0, rec_write, &r, 4, &a), (size_t)5);
    ASSERT_TRUE(memcmp(r.data, s, 5) == 0);
    TEST_END();
}

/* A large paste is split into chunks of at most the limit, in order. */
int test_paste_write_large(void)
{
    TEST_BEGIN();
    static char in[60000];
    for (size_t i = 0; i < sizeof in; i++) in[i] = (char)('a' + (i % 26));
    static Rec r;
    rec_init(&r);
    WriteAgg a; write_agg_init(&a);
    ASSERT_EQ(paste_write_chunked(in, sizeof in, 0, rec_write, &r,
                                  PASTE_WRITE_CHUNK, &a), sizeof in);
    ASSERT_EQ(r.total, sizeof in);
    ASSERT_TRUE(memcmp(r.data, in, sizeof in) == 0);
    ASSERT_EQ(r.calls, 15);   /* 60000 / 4096 rounded up */
    ASSERT_EQ(a.bad, 0u);
    TEST_END();
}

/* A failing transport stops the paste after the first failed write and the
 * aggregate reports it. */
int test_paste_write_failure_stops(void)
{
    TEST_BEGIN();
    Rec r; rec_init(&r);
    r.fail_after = 1;
    WriteAgg a; write_agg_init(&a);
    ASSERT_EQ(paste_write_chunked("0123456789abcdefghij", 20, 0, rec_write, &r, 8, &a), (size_t)8);
    ASSERT_EQ(r.calls, 2);          /* one good write, one failed, then stop */
    ASSERT_EQ(a.bad, 1u);
    ASSERT_EQ(a.rc, -1);
    TEST_END();
}

/* A short write continues with the rest and is flagged. */
int test_paste_write_short_write_continues(void)
{
    TEST_BEGIN();
    Rec r; rec_init(&r);
    r.short_cap = 3;
    WriteAgg a; write_agg_init(&a);
    ASSERT_EQ(paste_write_chunked("0123456789", 10, 0, rec_write, &r, 16, &a), (size_t)10);
    ASSERT_TRUE(memcmp(r.data, "0123456789", 10) == 0);
    ASSERT_EQ(r.calls, 4);
    ASSERT_TRUE(a.bad > 0u);
    TEST_END();
}
