/* libFuzzer target: the command safety classifier.  The first byte picks the
 * platform, the rest is the command.  Besides "does not crash" it checks
 * three invariants and aborts when one breaks:
 *   - the mask of categories contains the category of the single worst level;
 *   - a contradicted session is never looser than the plain classification;
 *   - nor looser than the same command under the UNKNOWN ruleset. */
#include "cmd_classify.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size == 0) return 0;

    CmdPlatform p = (CmdPlatform)(data[0] % (unsigned)(CMD_PLATFORM_UNKNOWN + 1));
    size_t n = size - 1;
    char *c = (char *)malloc(n + 1);
    if (!c) return 0;
    memcpy(c, data + 1, n);
    c[n] = '\0';

    char reason[128];
    (void)cmd_classify_ex(c, p, reason, sizeof reason);

    CmdSafetyLevel plain = cmd_classify(c, p);
    CmdSafetyLevel session = cmd_classify_session(c, p, 1);
    CmdSafetyLevel unknown = cmd_classify(c, CMD_PLATFORM_UNKNOWN);
    unsigned mask = cmd_classify_mask(c, p);
    int ok = (mask & CMD_MASK_OF(plain)) != 0
          && session >= plain
          && session >= unknown;
    free(c);
    if (!ok) abort();
    return 0;
}
