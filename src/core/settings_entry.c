#include "settings_entry.h"

int settings_entry_or_keep(int parsed_ok, unsigned v, int lo, int hi,
                           int zero_ok, int current)
{
    if (!parsed_ok) return current;
    if (v == 0u && zero_ok) return 0;
    /* A negative bound is a caller bug; refuse it rather than let the
     * unsigned compare wrap. Comparing in unsigned keeps every cast safe. */
    if (lo < 0 || hi < lo) return current;
    if (v >= (unsigned)lo && v <= (unsigned)hi) return (int)v;
    return current;
}
