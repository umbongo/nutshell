/* libFuzzer target: [EXEC] marker extraction from a model reply. */
#include "ai_prompt.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define FUZZ_MAX_CMDS 16

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static char cmds[FUZZ_MAX_CMDS][1024];
    static char notes[FUZZ_MAX_CMDS][CMD_NOTE_MAX];
    int quoted[FUZZ_MAX_CMDS];
    int rejected = 0;
    int rejected_long = 0;

    char *s = (char *)malloc(size + 1);
    if (!s) return 0;
    memcpy(s, data, size);
    s[size] = '\0';
    (void)ai_extract_commands_full(s, cmds, notes, quoted, FUZZ_MAX_CMDS,
                                   &rejected, &rejected_long);
    free(s);
    return 0;
}
