#include "ssh_io.h"
#include "string_utils.h"
#include <libssh2.h>
#include <stdio.h>

/* Write data to the log file with ANSI escapes stripped. */
static void log_chunk(FILE *f, const char *data, size_t len)
{
    if (!f || len == 0u) return;
    char strip_buf[4096];
    while (len > 0u) {
        size_t slice = (len < sizeof(strip_buf) - 1u)
                         ? len : (sizeof(strip_buf) - 1u);
        size_t stripped = ansi_strip(strip_buf, sizeof(strip_buf), data, slice);
        if (stripped > 0u) {
            fwrite(strip_buf, 1u, stripped, f);
        }
        data += slice;
        len  -= slice;
    }
    fflush(f);
}

/* Write raw bytes to the debug log in a readable format.
 * ESC (0x1B) → "ESC", printable ASCII → as-is, others → \xHH */
static void debug_log_chunk(FILE *f, const char *data, size_t len)
{
    if (!f || len == 0u) return;
    for (size_t i = 0u; i < len; i++) {
        unsigned char ch = (unsigned char)data[i];
        if (ch == 0x1Bu) {
            fputs("ESC", f);
        } else if (ch >= 0x20u && ch < 0x7Fu) {
            fputc((int)ch, f);
        } else if (ch == '\r') {
            fputs("\\r", f);
        } else if (ch == '\n') {
            fputs("\\n\n", f);
        } else if (ch == '\t') {
            fputs("\\t", f);
        } else {
            fprintf(f, "\\x%02X", (unsigned int)ch);
        }
    }
    fflush(f);
}

int ssh_io_poll(SSHChannel *channel, Terminal *term, FILE *log_file,
                FILE *debug_log) {
    if (!channel || !term) return -1;

    char buf[4096];
    size_t total_read = 0;  /* L-5: use size_t to avoid int overflow */
    int loops = 0;
    const int MAX_LOOPS = 10;

    while (loops < MAX_LOOPS) {
        int work_done = 0;

        /* Read stdout */
        ssize_t rc = libssh2_channel_read(channel->channel, buf, sizeof(buf));
        if (rc > 0) {
            term_process(term, buf, (size_t)rc);
            log_chunk(log_file, buf, (size_t)rc);
            debug_log_chunk(debug_log, buf, (size_t)rc);
            total_read += (size_t)rc;
            work_done = 1;
        } else if (rc == LIBSSH2_ERROR_EAGAIN || rc == 0) {
            if (libssh2_channel_eof(channel->channel)) return -2; /* EOF */
        } else {
            return -1; /* Error */
        }

        /* Read stderr */
        rc = libssh2_channel_read_stderr(channel->channel, buf, sizeof(buf));
        if (rc > 0) {
            term_process(term, buf, (size_t)rc);
            log_chunk(log_file, buf, (size_t)rc);
            debug_log_chunk(debug_log, buf, (size_t)rc);
            total_read += (size_t)rc;
            work_done = 1;
        }

        if (!work_done) break;
        loops++;
    }

    return (total_read > 0) ? 1 : 0;
}