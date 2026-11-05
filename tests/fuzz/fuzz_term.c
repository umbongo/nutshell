/* libFuzzer target: the terminal emulator fed raw bytes, as a remote host
 * (or a ConPTY child) can send them. */
#include "term.h"

#include <stdint.h>
#include <stddef.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    Terminal *t = term_init(24, 80, 100);
    if (!t) return 0;
    term_process(t, (const char *)data, size);
    term_free(t);
    return 0;
}
