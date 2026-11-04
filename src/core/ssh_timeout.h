#ifndef NUTSHELL_SSH_TIMEOUT_H
#define NUTSHELL_SSH_TIMEOUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Network-failure rail: declares the link dead when no socket bytes have
 * arrived for `threshold_ms`.  Both timestamps are 32-bit tick counts
 * (e.g. GetTickCount); subtraction wraps modulo 2^32, which is the
 * intended behaviour. */
bool ssh_network_should_timeout(uint32_t now,
                                uint32_t last_socket_tick,
                                uint32_t threshold_ms);

/* User-idle rail: declares the session idle when the user has not
 * interacted for `timeout_mins` minutes.  Returns false when
 * `timeout_mins <= 0` (disabled). */
bool ssh_idle_should_timeout(uint32_t now,
                             uint32_t last_input_tick,
                             int timeout_mins);

/* Connection-setup stages that share the 15 s limit (the limit covers setup
 * only, never an established session). */
typedef enum {
    SSH_STAGE_HANDSHAKE,
    SSH_STAGE_AUTH,
    SSH_STAGE_SESSION
} SshSetupStage;

/* Writes "Timed out after N s waiting for host:port <stage phrase>." into
 * `buf`, N being timeout_ms rounded up to whole seconds and an IPv6 host
 * bracketed.  Returns the length written, or 0 (with `buf` left an empty
 * string when cap > 0) when the text does not fit. */
size_t ssh_setup_timeout_text(SshSetupStage stage, const char *host, int port,
                              long timeout_ms, char *buf, size_t cap);

#define NETWORK_FAILURE_TIMEOUT_MS ((uint32_t)90000u)

#endif
