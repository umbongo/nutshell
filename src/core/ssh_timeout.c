#include "ssh_timeout.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

bool ssh_network_should_timeout(uint32_t now,
                                uint32_t last_socket_tick,
                                uint32_t threshold_ms)
{
    /* Unsigned subtraction wraps correctly across a 32-bit tick rollover. */
    return (uint32_t)(now - last_socket_tick) > threshold_ms;
}

bool ssh_idle_should_timeout(uint32_t now,
                             uint32_t last_input_tick,
                             int timeout_mins)
{
    if (timeout_mins <= 0) return false;
    /* Cast to uint64_t in the multiplication so 35791-minute (~24-day)
     * thresholds don't overflow a 32-bit ms value.  Compare in 32-bit
     * tick space using unsigned wrap. */
    uint64_t threshold_ms = (uint64_t)timeout_mins * 60000ull;
    if (threshold_ms > 0xFFFFFFFFull) threshold_ms = 0xFFFFFFFFull;
    return (uint32_t)(now - last_input_tick) > (uint32_t)threshold_ms;
}

size_t ssh_setup_timeout_text(SshSetupStage stage, const char *host, int port,
                              long timeout_ms, char *buf, size_t cap)
{
    if (!buf || cap == 0) return 0;
    buf[0] = '\0';

    const char *what;
    switch (stage) {
    case SSH_STAGE_HANDSHAKE: what = "during the SSH handshake"; break;
    case SSH_STAGE_AUTH:      what = "during authentication";    break;
    case SSH_STAGE_SESSION:   what = "while opening the session"; break;
    default:                  what = "during connection setup";  break;
    }

    if (!host) host = "";
    /* A colon means an IPv6 literal; bracket it unless it already is. */
    bool bracket = strchr(host, ':') != NULL && host[0] != '[';
    long secs = timeout_ms > 0 ? (timeout_ms + 999) / 1000 : 0;

    int n = snprintf(buf, cap, "Timed out after %ld s waiting for %s%s%s:%d %s.",
                     secs, bracket ? "[" : "", host, bracket ? "]" : "",
                     port, what);
    if (n < 0 || (size_t)n >= cap) {
        buf[0] = '\0';
        return 0;
    }
    return (size_t)n;
}

long ssh_kbd_rearm_ms(long long entry_s, long long now_s, long limit_ms)
{
    const long long max_ms = (long long)LONG_MAX;
    long long elapsed;

    if (now_s <= entry_s)
        elapsed = 0;                                   /* clock went backwards */
    else if (entry_s < 0 && now_s > LLONG_MAX + entry_s)
        return LONG_MAX;                               /* the subtraction would overflow */
    else
        elapsed = now_s - entry_s;

    if (elapsed > max_ms / 1000) return LONG_MAX;
    long long ms = elapsed * 1000;                     /* <= LONG_MAX */
    long long lim = limit_ms < 0 ? 0 : (long long)limit_ms;
    long long room = max_ms - ms;                      /* >= 0 */
    /* ms + lim + 1000 <= LONG_MAX, written so that nothing can overflow even
     * where long is 64 bits wide. */
    if (lim > room || room - lim < 1000) return LONG_MAX;
    return (long)(ms + lim + 1000);
}
