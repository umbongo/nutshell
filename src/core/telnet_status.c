#include "telnet_status.h"

#include <stdio.h>
#include <string.h>

TnConnOutcome tn_conn_merge(TnConnOutcome acc, TnConnOutcome next)
{
    /* The enum is declared best-first, so the smaller value wins. */
    return next < acc ? next : acc;
}

TnLine tn_line_for_outcome(TnConnOutcome o)
{
    switch (o) {
    case TN_CONN_OK:      return TN_LINE_CONNECTED;
    case TN_CONN_REFUSED: return TN_LINE_REFUSED;
    case TN_CONN_TIMEOUT: return TN_LINE_TIMEOUT;
    case TN_CONN_RESOLVE: return TN_LINE_RESOLVE;
    case TN_CONN_OTHER:
    default:              return TN_LINE_OTHER;
    }
}

size_t tn_status_line(TnLine which, const char *host, int port,
                      char *buf, size_t cap)
{
    if (!buf || cap == 0)
        return 0;
    if (!host)
        host = "?";

    char hp[320];
    snprintf(hp, sizeof hp, strchr(host, ':') ? "[%s]:%d" : "%s:%d", host, port);

    char line[480];
    switch (which) {
    case TN_LINE_CONNECTING:
        snprintf(line, sizeof line, "Connecting to %s", hp);
        break;
    case TN_LINE_CONNECTED:
        snprintf(line, sizeof line,
                 "Connected to %s (telnet \xE2\x80\x94 not encrypted)", hp);
        break;
    case TN_LINE_REFUSED:
        snprintf(line, sizeof line, "Connection refused by %s", hp);
        break;
    case TN_LINE_TIMEOUT:
        snprintf(line, sizeof line, "Connection to %s timed out", hp);
        break;
    case TN_LINE_RESOLVE:
        snprintf(line, sizeof line, "Could not resolve host %s", host);
        break;
    case TN_LINE_CLOSED:
        snprintf(line, sizeof line, "Connection closed by %s", hp);
        break;
    case TN_LINE_OTHER:
    default:
        snprintf(line, sizeof line, "Could not connect to %s", hp);
        break;
    }

    size_t n = strlen(line);
    if (n > cap - 1) {
        n = cap - 1;
        /* Back off over continuation bytes so no sequence is cut in half. */
        if ((unsigned char)line[n] >= 0x80 && (unsigned char)line[n] < 0xC0) {
            while (n > 0 && (unsigned char)line[n] >= 0x80 &&
                   (unsigned char)line[n] < 0xC0)
                n--;
        }
    }
    memcpy(buf, line, n);
    buf[n] = '\0';
    return n;
}
