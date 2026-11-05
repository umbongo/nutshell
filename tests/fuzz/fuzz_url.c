/* libFuzzer target: the URL guard (parse, display form, cleartext check and
 * redirect resolution, with the input once as the base and once as the
 * Location). */
#include "net_guard.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static char out[NET_URL_MAX];
    static NetUrlParts parts;

    char *s = (char *)malloc(size + 1);
    if (!s) return 0;
    memcpy(s, data, size);
    s[size] = '\0';

    (void)net_url_parse(s, &parts);
    (void)net_url_display(s, out, sizeof out);
    (void)net_url_cleartext_remote(s);
    (void)net_url_resolve(s, "z/../w?k=1", out, sizeof out);
    (void)net_url_resolve("https://a.test/x/y?q", s, out, sizeof out);
    free(s);
    return 0;
}
