#include "cli_sink.h"
#include "text_lines.h"
#include <stdlib.h>

CliSink cli_sink_for(int handle_valid, unsigned file_type, int is_console)
{
    if (!handle_valid) {
        return CLI_SINK_CONSOLE;
    }
    switch (file_type) {
    case CLI_FT_DISK:
    case CLI_FT_PIPE:
        return CLI_SINK_HANDLE;
    case CLI_FT_CHAR:
        return is_console ? CLI_SINK_CONSOLE : CLI_SINK_HANDLE;
    default:
        return CLI_SINK_CONSOLE;
    }
}

CliWriteResult cli_write_all(CliWriteFn fn, void *ctx, const char *text)
{
    size_t need = text_lf_to_crlf(text, NULL, 0);
    if (need == 0) {
        return CLI_WRITE_DONE;
    }
    char *buf = malloc(need + 1);
    if (!buf) {
        return CLI_WRITE_NONE;
    }
    (void)text_lf_to_crlf(text, buf, need + 1);

    size_t done = 0;
    CliWriteResult result = CLI_WRITE_DONE;
    while (done < need) {
        size_t written = 0;
        CliIoStatus st = fn(ctx, buf + done, need - done, &written);
        if (st == CLI_IO_OK && written > 0) {
            done += written;
            continue;
        }
        if (st == CLI_IO_CLOSED) {
            result = CLI_WRITE_STOPPED;
        } else {
            result = (done == 0) ? CLI_WRITE_NONE : CLI_WRITE_STOPPED;
        }
        break;
    }
    free(buf);
    return result;
}
