#include <stdlib.h>
#include "test_framework.h"
#include "../src/core/voice_playq.h"

static int16_t *mk(size_t n, int start)
{
    int16_t *s = (int16_t *)malloc(n * sizeof(int16_t));
    size_t i;
    if (!s) return NULL;
    for (i = 0; i < n; i++) s[i] = (int16_t)(start + (int)i);
    return s;
}

int test_vplayq_order_and_pos(void)
{
    TEST_BEGIN();
    VoicePlayQ *q = voice_playq_new();
    int16_t out[120];
    VoicePlayPos pos = {99, 99, 99};
    size_t i, got;
    int ok = 1;
    ASSERT_NOT_NULL(q);
    ASSERT_EQ(voice_playq_push(q, 5, 0, mk(100, 0), 100), 1);
    ASSERT_EQ(voice_playq_push(q, 5, 1, mk(50, 1000), 50), 1);
    got = voice_playq_read(q, out, 120, &pos);
    ASSERT_EQ(got, 120);
    for (i = 0; i < 100; i++) if (out[i] != (int16_t)i) ok = 0;
    for (i = 100; i < 120; i++) if (out[i] != (int16_t)(1000 + (i - 100))) ok = 0;
    ASSERT_TRUE(ok);
    ASSERT_EQ(pos.reply_id, 5);
    ASSERT_EQ(pos.chunk, 1);
    ASSERT_EQ(pos.frames_in_chunk, 20);
    voice_playq_free(q);
    TEST_END();
}

int test_vplayq_flush_drops_late_data(void)
{
    TEST_BEGIN();
    VoicePlayQ *q = voice_playq_new();
    int16_t out[10];
    ASSERT_EQ(voice_playq_push(q, 1, 0, mk(10, 1), 10), 1);
    voice_playq_flush(q, 2);
    ASSERT_EQ(voice_playq_push(q, 1, 1, mk(10, 1), 10), 0);
    ASSERT_EQ(voice_playq_read(q, out, 10, NULL), 0);
    ASSERT_EQ(voice_playq_push(q, 2, 0, mk(10, 7), 10), 1);
    ASSERT_EQ(voice_playq_read(q, out, 10, NULL), 10);
    ASSERT_EQ(out[0], 7);
    /* a stale end marker never fires take_end */
    voice_playq_end(q, 1);
    ASSERT_EQ(voice_playq_read(q, out, 10, NULL), 0);
    ASSERT_EQ(voice_playq_take_end(q, NULL), 0);
    /* flush also discards a reached-but-untaken end */
    voice_playq_end(q, 2);
    voice_playq_read(q, NULL, 0, NULL);
    voice_playq_flush(q, 3);
    ASSERT_EQ(voice_playq_take_end(q, NULL), 0);
    voice_playq_free(q);
    TEST_END();
}

int test_vplayq_underrun_not_done(void)
{
    TEST_BEGIN();
    VoicePlayQ *q = voice_playq_new();
    int16_t out[200];
    unsigned id = 0;
    voice_playq_push(q, 3, 0, mk(100, 0), 100);
    ASSERT_EQ(voice_playq_read(q, out, 200, NULL), 100);
    ASSERT_EQ(voice_playq_take_end(q, &id), 0);
    voice_playq_push(q, 3, 1, mk(50, 0), 50);
    voice_playq_end(q, 3);
    ASSERT_EQ(voice_playq_read(q, out, 200, NULL), 50);
    ASSERT_EQ(voice_playq_take_end(q, &id), 1);
    ASSERT_EQ(id, 3);
    ASSERT_EQ(voice_playq_take_end(q, &id), 0);
    voice_playq_free(q);
    TEST_END();
}

int test_vplayq_gain(void)
{
    TEST_BEGIN();
    VoicePlayQ *q = voice_playq_new();
    int16_t out[3];
    int16_t *s = mk(3, 0);
    s[0] = 10000; s[1] = -32768; s[2] = 32767;
    voice_playq_set_gain(q, 18);   /* Q16 11796; truncating shift: 10000 -> 1799 */
    voice_playq_push(q, 1, 0, s, 3);
    ASSERT_EQ(voice_playq_read(q, out, 3, NULL), 3);
    ASSERT_EQ(out[0], 1799);
    ASSERT_TRUE(out[1] < 0 && out[1] > -6000);
    s = mk(3, 0);
    s[0] = 10000; s[1] = -32768; s[2] = 32767;
    voice_playq_set_gain(q, 100);
    voice_playq_push(q, 1, 1, s, 3);
    ASSERT_EQ(voice_playq_read(q, out, 3, NULL), 3);
    ASSERT_EQ(out[0], 10000);
    ASSERT_EQ(out[1], -32768);
    ASSERT_EQ(out[2], 32767);
    /* clamping */
    s = mk(1, 0); s[0] = 1000;
    voice_playq_set_gain(q, 250);
    voice_playq_push(q, 1, 2, s, 1);
    voice_playq_read(q, out, 1, NULL);
    ASSERT_EQ(out[0], 1000);
    s = mk(1, 0); s[0] = 1000;
    voice_playq_set_gain(q, -5);
    voice_playq_push(q, 1, 3, s, 1);
    voice_playq_read(q, out, 1, NULL);
    ASSERT_EQ(out[0], 0);
    voice_playq_free(q);
    TEST_END();
}

int test_vplayq_end_without_data(void)
{
    TEST_BEGIN();
    VoicePlayQ *q = voice_playq_new();
    unsigned id = 0;
    voice_playq_end(q, 4);
    ASSERT_EQ(voice_playq_read(q, NULL, 0, NULL), 0);
    ASSERT_EQ(voice_playq_take_end(q, &id), 1);
    ASSERT_EQ(id, 4);
    ASSERT_EQ(voice_playq_take_end(q, &id), 0);
    voice_playq_free(q);
    TEST_END();
}

int test_vplayq_wrap_ids(void)
{
    TEST_BEGIN();
    VoicePlayQ *q = voice_playq_new();
    int16_t out[4];
    voice_playq_flush(q, 1);
    ASSERT_EQ(voice_playq_push(q, 0xFFFFFFFEu, 0, mk(4, 0), 4), 0);
    ASSERT_EQ(voice_playq_push(q, 1, 0, mk(4, 0), 4), 1);
    ASSERT_EQ(voice_playq_read(q, out, 4, NULL), 4);
    voice_playq_free(q);
    TEST_END();
}

int test_vplayq_queued_frames(void)
{
    TEST_BEGIN();
    VoicePlayQ *q = voice_playq_new();
    int16_t out[30];
    ASSERT_EQ(voice_playq_queued_frames(q), 0);
    voice_playq_push(q, 1, 0, mk(20, 0), 20);
    voice_playq_push(q, 1, 1, mk(30, 0), 30);
    ASSERT_EQ(voice_playq_queued_frames(q), 50);
    voice_playq_read(q, out, 30, NULL);
    ASSERT_EQ(voice_playq_queued_frames(q), 20);
    voice_playq_flush(q, 2);
    ASSERT_EQ(voice_playq_queued_frames(q), 0);
    voice_playq_free(q);
    TEST_END();
}

int test_vplayq_empty_push(void)
{
    TEST_BEGIN();
    VoicePlayQ *q = voice_playq_new();
    int16_t out[4];
    ASSERT_EQ(voice_playq_push(q, 1, 0, mk(4, 0), 0), 0);   /* n == 0: freed */
    ASSERT_EQ(voice_playq_push(q, 1, 0, NULL, 4), 0);
    ASSERT_EQ(voice_playq_queued_frames(q), 0);
    ASSERT_EQ(voice_playq_read(q, out, 4, NULL), 0);
    voice_playq_free(q);
    TEST_END();
}

int test_vplayq_pos_accumulates_across_blocks(void)
{
    TEST_BEGIN();
    VoicePlayQ *q = voice_playq_new();
    int16_t out[100];
    VoicePlayPos pos = {99, 99, 99};
    ASSERT_NOT_NULL(q);
    /* one chunk arrives as three blocks, then the next chunk's first block */
    ASSERT_EQ(voice_playq_push(q, 4, 0, mk(30, 1), 30), 1);
    ASSERT_EQ(voice_playq_push(q, 4, 0, mk(30, 1), 30), 1);
    ASSERT_EQ(voice_playq_push(q, 4, 0, mk(30, 1), 30), 1);
    ASSERT_EQ(voice_playq_push(q, 4, 1, mk(20, 1), 20), 1);
    /* a read ending in the third block, then a read across the block boundary */
    ASSERT_EQ(voice_playq_read(q, out, 70, &pos), 70);
    ASSERT_EQ(pos.chunk, 0);
    ASSERT_EQ(pos.frames_in_chunk, 70);
    ASSERT_EQ(voice_playq_read(q, out, 20, &pos), 20);
    ASSERT_EQ(pos.chunk, 0);
    ASSERT_EQ(pos.frames_in_chunk, 90);
    ASSERT_EQ(voice_playq_read(q, out, 5, &pos), 5);
    ASSERT_EQ(pos.chunk, 1);
    ASSERT_EQ(pos.frames_in_chunk, 5);
    /* after a flush the count starts over, even for the same reply and chunk */
    voice_playq_flush(q, 4);
    ASSERT_EQ(voice_playq_push(q, 4, 1, mk(10, 1), 10), 1);
    ASSERT_EQ(voice_playq_read(q, out, 10, &pos), 10);
    ASSERT_EQ(pos.frames_in_chunk, 10);
    voice_playq_free(q);
    TEST_END();
}
