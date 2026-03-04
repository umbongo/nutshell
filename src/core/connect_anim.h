#ifndef CONGA_CORE_CONNECT_ANIM_H
#define CONGA_CORE_CONNECT_ANIM_H

#include <stddef.h>

/*
 * Pure arithmetic helpers for the "Connecting..." dot animation shown
 * while an SSH connection is being established in the background.
 * No platform types are used so both modules are fully testable on Linux.
 */

/*
 * connect_anim_dots — number of dots to display after elapsed_ms.
 *
 *   elapsed_ms    milliseconds since connection started
 *   interval_ms   how often a new dot appears (e.g. 500); 0 returns max_dots
 *   max_dots      upper bound on dots (clamp)
 *
 * Returns: 0 .. max_dots
 */
int connect_anim_dots(unsigned long elapsed_ms,
                      unsigned long interval_ms,
                      int           max_dots);

/*
 * connect_anim_text — write "Connecting" + N dots into buf.
 *
 *   dots      number of dots to append
 *   buf       output buffer (may be NULL — returns 0)
 *   buf_size  capacity of buf including null terminator (0 — returns 0)
 *
 * Always null-terminates when buf_size > 0.
 * Returns bytes written (excluding null terminator).
 */
int connect_anim_text(int dots, char *buf, size_t buf_size);

#endif
