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
 * without needing real libssh2 linked.  Retry limit must match the
 * real code in ssh_channel.c (currently 20). */
typedef struct { LIBSSH2_CHANNEL *channel; } TestChannel;

#define WRITE_MAX_RETRIES 20

static int channel_write_retry(TestChannel *ch, const char *data, size_t len)
{
    if (!ch || !ch->channel) return -1;
    ssize_t rc;
    int retries = 0;
    do {
        rc = mock_channel_write(ch->channel, data, len);
    } while (rc == LIBSSH2_ERROR_EAGAIN && ++retries < WRITE_MAX_RETRIES);
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
    g_eagain_count = 100; /* never succeeds within retry limit */
    g_call_count = 0;
    g_written_len = 0;

    int rc = channel_write_retry(&ch, "\r", 1);
    ASSERT_EQ(rc, LIBSSH2_ERROR_EAGAIN);          /* gives up */
    ASSERT_EQ(g_call_count, WRITE_MAX_RETRIES);   /* 1 initial + retries */
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

/*
 * Regression test: simulate the pre-drain pattern used in window.c.
 *
 * Bug: in non-blocking mode, libssh2_channel_write returns EAGAIN when
 * the session has pending inbound transport data.  The retry loop in
 * ssh_channel_write blocks the UI thread (via waitsocket/select), which
 * prevents WM_TIMER from firing ssh_io_poll to drain the data — deadlock.
 * After the retry limit, the write fails silently and the keystroke is lost.
 *
 * Fix: call ssh_io_poll (which reads/processes pending data) BEFORE
 * attempting the write.  This clears the transport so the write succeeds
 * on the first attempt.
 *
 * This test simulates: write fails with EAGAIN when pending reads exist,
 * but succeeds immediately after reads are drained.
 */
static int g_pending_reads;  /* simulate pending inbound data */

static ssize_t mock_channel_write_with_transport(LIBSSH2_CHANNEL *ch,
                                                  const char *data, size_t len)
{
    (void)ch;
    g_call_count++;

    /* Simulate EAGAIN when there is pending inbound data */
    if (g_pending_reads > 0) {
        return LIBSSH2_ERROR_EAGAIN;
    }

    if (len > sizeof(g_written) - g_written_len)
        len = sizeof(g_written) - g_written_len;
    memcpy(g_written + g_written_len, data, len);
    g_written_len += len;
    return (ssize_t)len;
}

/* Simulate ssh_io_poll: drains pending reads */
static void mock_io_poll(void) { g_pending_reads = 0; }

int test_channel_write_predrain_fixes_eagain(void) {
    TEST_BEGIN();
    TestChannel ch;
    ch.channel = (LIBSSH2_CHANNEL *)(void *)1;
    g_call_count = 0;
    g_written_len = 0;

    /* Simulate: server sent data, transport has pending reads */
    g_pending_reads = 1;

    /* WITHOUT pre-drain: write returns EAGAIN every time */
    ssize_t rc = mock_channel_write_with_transport(ch.channel, "\r", 1);
    ASSERT_EQ((int)rc, LIBSSH2_ERROR_EAGAIN);
    ASSERT_EQ((int)g_written_len, 0);

    /* Pre-drain the transport (simulates ssh_io_poll before write) */
    mock_io_poll();

    /* NOW write succeeds on first attempt */
    rc = mock_channel_write_with_transport(ch.channel, "\r", 1);
    ASSERT_EQ((int)rc, 1);
    ASSERT_EQ((int)g_written_len, 1);
    ASSERT_EQ(g_written[0], '\r');
    TEST_END();
}

int test_channel_write_no_predrain_loses_keystroke(void) {
    TEST_BEGIN();
    /* Demonstrate the bug: without pre-drain, repeated writes fail
     * when there are pending reads — keystrokes are silently lost. */
    TestChannel ch;
    ch.channel = (LIBSSH2_CHANNEL *)(void *)1;
    g_call_count = 0;
    g_written_len = 0;
    g_pending_reads = 1;

    /* Simulate 3 Enter presses, each going through retry loop.
     * The mock never clears pending_reads (no io_poll runs because
     * the UI thread is blocked), so all writes fail. */
    for (int i = 0; i < 3; i++) {
        ssize_t rc;
        int retries = 0;
        do {
            rc = mock_channel_write_with_transport(ch.channel, "\r", 1);
        } while (rc == LIBSSH2_ERROR_EAGAIN && ++retries < WRITE_MAX_RETRIES);
    }

    /* All 3 keystrokes lost — nothing written */
    ASSERT_EQ((int)g_written_len, 0);
    /* Total calls: 3 × WRITE_MAX_RETRIES */
    ASSERT_EQ(g_call_count, 3 * WRITE_MAX_RETRIES);
    TEST_END();
}

/*
 * Regression: after a command like 'll' outputs a large listing, subsequent
 * Enter presses stop working.  Pre-drain alone isn't sufficient because the
 * transport may have residual protocol-level work (e.g. SSH window adjust
 * messages) that causes non-blocking writes to return EAGAIN even after
 * reads are drained.
 *
 * Fix: ssh_channel_write temporarily switches to blocking mode, which
 * guarantees the transport is fully processed (reads + protocol messages)
 * before the write completes.  For 1-byte keystrokes this is effectively
 * instant.
 *
 * This mock simulates: non-blocking write returns EAGAIN due to residual
 * transport work (g_transport_busy), but in blocking mode the library
 * internally flushes everything and the write succeeds.
 */
static int g_transport_busy;  /* simulate residual SSH protocol work */
static int g_blocking_mode;   /* 1 = blocking (write always succeeds) */

static ssize_t mock_channel_write_transport_busy(LIBSSH2_CHANNEL *ch,
                                                  const char *data, size_t len)
{
    (void)ch;
    g_call_count++;

    /* In blocking mode, transport is fully flushed — write always succeeds */
    if (g_blocking_mode) {
        if (len > sizeof(g_written) - g_written_len)
            len = sizeof(g_written) - g_written_len;
        memcpy(g_written + g_written_len, data, len);
        g_written_len += len;
        return (ssize_t)len;
    }

    /* Non-blocking: EAGAIN when transport has residual work */
    if (g_transport_busy > 0) {
        g_transport_busy--;  /* each call makes partial progress */
        return LIBSSH2_ERROR_EAGAIN;
    }

    if (len > sizeof(g_written) - g_written_len)
        len = sizeof(g_written) - g_written_len;
    memcpy(g_written + g_written_len, data, len);
    g_written_len += len;
    return (ssize_t)len;
}

/* Simulate the blocking write approach: set blocking, write, restore */
static int channel_write_blocking(TestChannel *ch, const char *data, size_t len)
{
    if (!ch || !ch->channel) return -1;
    g_blocking_mode = 1;
    ssize_t rc = mock_channel_write_transport_busy(ch->channel, data, len);
    g_blocking_mode = 0;
    return (int)rc;
}

/* Non-blocking retry loop using the transport-busy mock */
static int channel_write_retry_transport(TestChannel *ch, const char *data, size_t len)
{
    if (!ch || !ch->channel) return -1;
    ssize_t rc;
    int retries = 0;
    do {
        rc = mock_channel_write_transport_busy(ch->channel, data, len);
    } while (rc == LIBSSH2_ERROR_EAGAIN && ++retries < WRITE_MAX_RETRIES);
    return (int)rc;
}

int test_channel_write_nonblocking_fails_after_output(void) {
    TEST_BEGIN();
    /* After 'll' output, transport has ~50 units of residual protocol
     * work.  Non-blocking retry (20 attempts) can't clear it all. */
    TestChannel ch;
    ch.channel = (LIBSSH2_CHANNEL *)(void *)1;
    g_call_count = 0;
    g_written_len = 0;
    g_blocking_mode = 0;
    g_transport_busy = 50;  /* more than WRITE_MAX_RETRIES */

    int rc = channel_write_retry_transport(&ch, "\r", 1);
    /* Non-blocking retry exhausted — keystroke lost */
    ASSERT_EQ(rc, LIBSSH2_ERROR_EAGAIN);
    ASSERT_EQ((int)g_written_len, 0);
    TEST_END();
}

int test_channel_write_blocking_succeeds_after_output(void) {
    TEST_BEGIN();
    /* Same scenario: transport busy, but blocking write succeeds because
     * blocking mode forces full transport flush internally. */
    TestChannel ch;
    ch.channel = (LIBSSH2_CHANNEL *)(void *)1;
    g_call_count = 0;
    g_written_len = 0;
    g_blocking_mode = 0;
    g_transport_busy = 50;

    int rc = channel_write_blocking(&ch, "\r", 1);
    ASSERT_EQ(rc, 1);              /* write succeeds */
    ASSERT_EQ((int)g_written_len, 1);
    ASSERT_EQ(g_written[0], '\r');
    TEST_END();
}

int test_channel_write_blocking_multiple_keystrokes(void) {
    TEST_BEGIN();
    /* Simulate: 'll' + Enter, then 3 Enter presses after output.
     * All should succeed with blocking write. */
    TestChannel ch;
    ch.channel = (LIBSSH2_CHANNEL *)(void *)1;
    g_call_count = 0;
    g_written_len = 0;
    g_blocking_mode = 0;
    g_transport_busy = 100;  /* lots of residual work */

    for (int i = 0; i < 3; i++) {
        int rc = channel_write_blocking(&ch, "\r", 1);
        ASSERT_EQ(rc, 1);
    }
    /* All 3 keystrokes written */
    ASSERT_EQ((int)g_written_len, 3);
    ASSERT_EQ(g_written[0], '\r');
    ASSERT_EQ(g_written[1], '\r');
    ASSERT_EQ(g_written[2], '\r');
    TEST_END();
}
