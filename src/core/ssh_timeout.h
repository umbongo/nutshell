#ifndef NUTSHELL_SSH_TIMEOUT_H
#define NUTSHELL_SSH_TIMEOUT_H

#include <stdbool.h>
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

#define NETWORK_FAILURE_TIMEOUT_MS ((uint32_t)90000u)

#endif
