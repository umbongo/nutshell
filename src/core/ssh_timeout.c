#include "ssh_timeout.h"

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
