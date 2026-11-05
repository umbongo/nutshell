/* libFuzzer target: json_parse() on arbitrary bytes (a config file, an AI
 * reply body).  Build and run with `make fuzz fuzz-run`. */
#include "json_parser.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    char *s = (char *)malloc(size + 1);
    if (!s) return 0;
    memcpy(s, data, size);
    s[size] = '\0';
    json_free(json_parse(s));
    free(s);
    return 0;
}
