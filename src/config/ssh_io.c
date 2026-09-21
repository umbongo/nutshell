#include "ssh_io.h"
#include "string_utils.h"
#include "../term/ssh_pty.h"
#include <libssh2.h>
#include <stdio.h>
#include <string.h>

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

/* ---- SessionIo vtable (spec section 2) ---------------------------------- *
 * The context is the SSHChannel: it carries a back-pointer to the SshSession
 * it was opened on (ssh_channel_open() sets ch->ssh), so one void * is enough
 * to poll, write, resize and tear the whole transport down.
 * Each wrapper is a straight forward to the function the call sites used
 * before the seam existed, so behaviour is unchanged. */

static int ssh_io_vt_poll(void *ctx, Terminal *term, FILE *log_file,
                          FILE *debug_log)
{
    return ssh_io_poll((SSHChannel *)ctx, term, log_file, debug_log);
}

static int ssh_io_vt_write(void *ctx, const char *data, size_t len)
{
    return ssh_channel_write((SSHChannel *)ctx, data, len);
}

static int ssh_io_vt_resize(void *ctx, int cols, int rows)
{
    return ssh_pty_resize((SSHChannel *)ctx, cols, rows);
}

/* Releases the channel and then the session it was opened on -- the two
 * halves the close paths in window.c used to free one after the other. */
static void ssh_io_vt_close(void *ctx)
{
    SSHChannel *ch = (SSHChannel *)ctx;
    if (!ch) return;
    SshSession *ssh = ch->ssh;
    ssh_channel_free(ch);
    ssh_session_free(ssh);   /* tolerates NULL */
}

SessionIo session_io_ssh(SshSession *ssh, SSHChannel *channel)
{
    SessionIo io;
    memset(&io, 0, sizeof(io));
    io.kind = SESSION_SSH;
    if (!channel) return io;          /* ctx stays NULL: no transport */
    /* ssh is the session the channel was opened on; the channel's own
     * back-pointer is what close() uses, so keep the two in agreement. */
    if (ssh && channel->ssh != ssh) channel->ssh = ssh;
    io.poll   = ssh_io_vt_poll;
    io.write  = ssh_io_vt_write;
    io.resize = ssh_io_vt_resize;
    io.close  = ssh_io_vt_close;
    io.ctx    = channel;
    return io;
}
