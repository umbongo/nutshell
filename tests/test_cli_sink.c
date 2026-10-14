#include "test_framework.h"
#include "cli_sink.h"
#include <string.h>

/* cli_sink_for: nutshell.exe is a GUI program, so its --help/--version/
 * --list text goes to the parent console unless the shell redirected the
 * stream to a pipe, a file or a non-console device, in which case it must
 * go to that handle so a waiting reader receives it in order, "> file" is
 * not empty and "> NUL" is silent. */

int test_cli_sink_invalid_handle_is_console(void)
{
    TEST_BEGIN();
    ASSERT_EQ(cli_sink_for(0, CLI_FT_PIPE, 0), CLI_SINK_CONSOLE);
    ASSERT_EQ(cli_sink_for(0, CLI_FT_DISK, 0), CLI_SINK_CONSOLE);
    ASSERT_EQ(cli_sink_for(0, CLI_FT_CHAR, 0), CLI_SINK_CONSOLE);
    ASSERT_EQ(cli_sink_for(0, CLI_FT_UNKNOWN, 0), CLI_SINK_CONSOLE);
    TEST_END();
}

int test_cli_sink_pipe_is_handle(void)
{
    TEST_BEGIN();
    ASSERT_EQ(cli_sink_for(1, CLI_FT_PIPE, 0), CLI_SINK_HANDLE);
    /* is_console is only meaningful for a character device */
    ASSERT_EQ(cli_sink_for(1, CLI_FT_PIPE, 1), CLI_SINK_HANDLE);
    TEST_END();
}

int test_cli_sink_disk_is_handle(void)
{
    TEST_BEGIN();
    ASSERT_EQ(cli_sink_for(1, CLI_FT_DISK, 0), CLI_SINK_HANDLE);
    ASSERT_EQ(cli_sink_for(1, CLI_FT_DISK, 1), CLI_SINK_HANDLE);
    TEST_END();
}

int test_cli_sink_console_device_is_console(void)
{
    TEST_BEGIN();
    /* The console itself: keep the AttachConsole path and its leading
     * newline off the prompt line. */
    ASSERT_EQ(cli_sink_for(1, CLI_FT_CHAR, 1), CLI_SINK_CONSOLE);
    TEST_END();
}

int test_cli_sink_char_device_not_console_is_handle(void)
{
    TEST_BEGIN();
    /* "> NUL", a COM port: GetConsoleMode fails, write it like a file so
     * nothing reaches the console and no MessageBox blocks a headless
     * parent. */
    ASSERT_EQ(cli_sink_for(1, CLI_FT_CHAR, 0), CLI_SINK_HANDLE);
    TEST_END();
}

int test_cli_sink_unknown_type_is_console(void)
{
    TEST_BEGIN();
    ASSERT_EQ(cli_sink_for(1, CLI_FT_UNKNOWN, 0), CLI_SINK_CONSOLE);
    ASSERT_EQ(cli_sink_for(1, CLI_FT_UNKNOWN, 1), CLI_SINK_CONSOLE);
    /* Any value Win32 does not define today falls back the same way. */
    ASSERT_EQ(cli_sink_for(1, 0x8000u, 0), CLI_SINK_CONSOLE);
    ASSERT_EQ(cli_sink_for(1, 4u, 0), CLI_SINK_CONSOLE);
    TEST_END();
}

/* cli_write_all: a fake writer that takes at most `max_chunk` bytes per
 * call and can fail once `fail_at` bytes have been accepted. */

typedef struct {
    char out[256];
    size_t len;
    size_t max_chunk;     /* 0 = unlimited */
    long fail_at;         /* -1 = never; else fail when len >= fail_at */
    CliIoStatus fail_with;
    int zero_once;        /* report OK with 0 bytes on the first call */
    int calls;
} FakeWriter;

static CliIoStatus fake_write(void *ctx, const char *buf, size_t len,
                              size_t *written)
{
    FakeWriter *w = (FakeWriter *)ctx;
    w->calls++;
    *written = 0;
    if (w->zero_once) {
        w->zero_once = 0;
        return CLI_IO_OK;
    }
    if (w->fail_at >= 0 && w->len >= (size_t)w->fail_at) {
        return w->fail_with;
    }
    size_t take = len;
    if (w->max_chunk > 0 && take > w->max_chunk) {
        take = w->max_chunk;
    }
    if (w->fail_at >= 0 && w->len + take > (size_t)w->fail_at) {
        take = (size_t)w->fail_at - w->len;
    }
    if (w->len + take >= sizeof(w->out)) {
        take = sizeof(w->out) - 1 - w->len;
    }
    memcpy(w->out + w->len, buf, take);
    w->len += take;
    w->out[w->len] = '\0';
    *written = take;
    return CLI_IO_OK;
}

static FakeWriter fake_writer(size_t max_chunk, long fail_at,
                              CliIoStatus fail_with)
{
    FakeWriter w;
    memset(&w, 0, sizeof(w));
    w.max_chunk = max_chunk;
    w.fail_at = fail_at;
    w.fail_with = fail_with;
    return w;
}

int test_cli_write_all_single_call_crlf(void)
{
    TEST_BEGIN();
    FakeWriter w = fake_writer(0, -1, CLI_IO_ERROR);
    ASSERT_EQ(cli_write_all(fake_write, &w, "Nutshell 1.2.32\n"),
              CLI_WRITE_DONE);
    ASSERT_STR_EQ(w.out, "Nutshell 1.2.32\r\n");
    ASSERT_EQ(w.calls, 1);
    /* existing CRLF is not doubled */
    w = fake_writer(0, -1, CLI_IO_ERROR);
    ASSERT_EQ(cli_write_all(fake_write, &w, "a\r\nb\n"), CLI_WRITE_DONE);
    ASSERT_STR_EQ(w.out, "a\r\nb\r\n");
    TEST_END();
}

int test_cli_write_all_partial_writes_resume(void)
{
    TEST_BEGIN();
    FakeWriter w = fake_writer(3, -1, CLI_IO_ERROR);
    ASSERT_EQ(cli_write_all(fake_write, &w, "Usage:\n  -v\n"),
              CLI_WRITE_DONE);
    ASSERT_STR_EQ(w.out, "Usage:\r\n  -v\r\n");
    ASSERT_EQ(w.calls, 5); /* 14 bytes in chunks of 3 */
    TEST_END();
}

int test_cli_write_all_zero_bytes_is_none(void)
{
    TEST_BEGIN();
    /* Nothing accepted and an ordinary error: the caller may fall back to
     * the console. A 0-byte "success" counts as that error, not a spin. */
    FakeWriter w = fake_writer(0, 0, CLI_IO_ERROR);
    ASSERT_EQ(cli_write_all(fake_write, &w, "text\n"), CLI_WRITE_NONE);
    ASSERT_EQ(w.len, 0);
    w = fake_writer(0, -1, CLI_IO_ERROR);
    w.zero_once = 1;
    ASSERT_EQ(cli_write_all(fake_write, &w, "text\n"), CLI_WRITE_NONE);
    ASSERT_EQ(w.calls, 1);
    TEST_END();
}

int test_cli_write_all_error_after_partial_is_stopped(void)
{
    TEST_BEGIN();
    /* Disk full after 4 bytes: the file has part of the text; it must not
     * also be dumped on the console. */
    FakeWriter w = fake_writer(0, 4, CLI_IO_ERROR);
    ASSERT_EQ(cli_write_all(fake_write, &w, "Saved sessions:\n"),
              CLI_WRITE_STOPPED);
    ASSERT_STR_EQ(w.out, "Save");
    TEST_END();
}

int test_cli_write_all_closed_is_stopped(void)
{
    TEST_BEGIN();
    /* The reader went away ("| findstr /m"): even before any byte, the
     * text was piped away on purpose. */
    FakeWriter w = fake_writer(0, 0, CLI_IO_CLOSED);
    ASSERT_EQ(cli_write_all(fake_write, &w, "text\n"), CLI_WRITE_STOPPED);
    w = fake_writer(0, 2, CLI_IO_CLOSED);
    ASSERT_EQ(cli_write_all(fake_write, &w, "text\n"), CLI_WRITE_STOPPED);
    ASSERT_STR_EQ(w.out, "te");
    TEST_END();
}

int test_cli_write_all_empty_text_is_done(void)
{
    TEST_BEGIN();
    FakeWriter w = fake_writer(0, -1, CLI_IO_ERROR);
    ASSERT_EQ(cli_write_all(fake_write, &w, ""), CLI_WRITE_DONE);
    ASSERT_EQ(cli_write_all(fake_write, &w, NULL), CLI_WRITE_DONE);
    ASSERT_EQ(w.calls, 0);
    TEST_END();
}
