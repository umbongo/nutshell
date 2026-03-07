#include "test_framework.h"
#include <string.h>
#include <sys/types.h>

/* Mock libssh2_channel_write that simulates EAGAIN then success */
#define LIBSSH2_ERROR_EAGAIN -37
typedef struct _LIBSSH2_CHANNEL LIBSSH2_CHANNEL;

static int g_eagain_count;    /* how many EAGAINs before success */
static int g_call_count;      /* total calls made */
static char g_written[256];   /* data that was "written" */
static size_t g_written_len;

static ssize_t mock_channel_write(LIBSSH2_CHANNEL *ch, const char *data, size_t len)
{
    (void)ch;
    g_call_count++;
    if (g_eagain_count > 0) {
        g_eagain_count--;
        return LIBSSH2_ERROR_EAGAIN;
    }
    if (len > sizeof(g_written) - g_written_len)
        len = sizeof(g_written) - g_written_len;
    memcpy(g_written + g_written_len, data, len);
    g_written_len += len;
    return (ssize_t)len;
}

/* Replicate the retry logic from ssh_channel_write so we can test it
 * without needing real libssh2 linked. */
typedef struct { LIBSSH2_CHANNEL *channel; } TestChannel;

static int channel_write_retry(TestChannel *ch, const char *data, size_t len)
{
    if (!ch || !ch->channel) return -1;
    ssize_t rc;
    int retries = 0;
    do {
        rc = mock_channel_write(ch->channel, data, len);
    } while (rc == LIBSSH2_ERROR_EAGAIN && ++retries < 50);
    return (int)rc;
}

int test_channel_write_immediate_success(void) {
    TEST_BEGIN();
    TestChannel ch;
    ch.channel = (LIBSSH2_CHANNEL *)(void *)1;
    g_eagain_count = 0;
    g_call_count = 0;
    g_written_len = 0;

    int rc = channel_write_retry(&ch, "\r", 1);
    ASSERT_EQ(rc, 1);
    ASSERT_EQ(g_call_count, 1);
    ASSERT_EQ((int)g_written_len, 1);
    ASSERT_EQ(g_written[0], '\r');
    TEST_END();
}

int test_channel_write_eagain_then_success(void) {
    TEST_BEGIN();
    TestChannel ch;
    ch.channel = (LIBSSH2_CHANNEL *)(void *)1;
    g_eagain_count = 3; /* 3 EAGAINs, then success */
    g_call_count = 0;
    g_written_len = 0;

    int rc = channel_write_retry(&ch, "\r", 1);
    ASSERT_EQ(rc, 1);           /* eventually succeeds */
    ASSERT_EQ(g_call_count, 4); /* 3 retries + 1 success */
    ASSERT_EQ((int)g_written_len, 1);
    ASSERT_EQ(g_written[0], '\r');
    TEST_END();
}

int test_channel_write_eagain_exhausted(void) {
    TEST_BEGIN();
    TestChannel ch;
    ch.channel = (LIBSSH2_CHANNEL *)(void *)1;
    g_eagain_count = 100; /* never succeeds within 50 retries */
    g_call_count = 0;
    g_written_len = 0;

    int rc = channel_write_retry(&ch, "\r", 1);
    ASSERT_EQ(rc, LIBSSH2_ERROR_EAGAIN); /* gives up */
    ASSERT_EQ(g_call_count, 50);         /* 1 initial + 49 retries */
    ASSERT_EQ((int)g_written_len, 0);    /* nothing written */
    TEST_END();
}

int test_channel_write_null_channel(void) {
    TEST_BEGIN();
    g_call_count = 0;
    ASSERT_EQ(channel_write_retry(NULL, "\r", 1), -1);
    TestChannel ch;
    ch.channel = NULL;
    ASSERT_EQ(channel_write_retry(&ch, "\r", 1), -1);
    ASSERT_EQ(g_call_count, 0); /* mock never called */
    TEST_END();
}

int test_channel_write_enter_is_cr(void) {
    TEST_BEGIN();
    /* Verify that Enter key must send CR (\r), not LF (\n).
     * The SSH PTY line discipline expects CR to execute commands. */
    TestChannel ch;
    ch.channel = (LIBSSH2_CHANNEL *)(void *)1;
    g_eagain_count = 0;
    g_call_count = 0;
    g_written_len = 0;

    /* Simulate typing "ls" then Enter */
    channel_write_retry(&ch, "l", 1);
    channel_write_retry(&ch, "s", 1);
    channel_write_retry(&ch, "\r", 1); /* CR, not LF */

    ASSERT_EQ((int)g_written_len, 3);
    ASSERT_EQ(g_written[0], 'l');
    ASSERT_EQ(g_written[1], 's');
    ASSERT_EQ(g_written[2], '\r'); /* must be CR */
    TEST_END();
}
