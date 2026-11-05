/* libFuzzer target: the web_fetch HTML-to-text pipeline on the raw bytes of
 * a fetched page. */
#include "ai_tool_web_fetch.h"

#include <stdint.h>
#include <stdlib.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    char *out = NULL;
    size_t len = 0;
    int trunc = 0;
    (void)web_fetch_html_to_text((const char *)data, size, &out, &len, &trunc);
    free(out);
    return 0;
}
