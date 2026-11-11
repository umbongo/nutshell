#include "search_providers.h"
#include <stddef.h>

typedef struct {
    const char *label;
    const char *value;
} SearchProviderRow;

static const SearchProviderRow k_rows[] = {
    { "None",               "none"            },
    { "DuckDuckGo (API)",   "duckduckgo-api"  },
    { "DuckDuckGo (HTML)",  "duckduckgo-html" },
    { "Custom",             "custom"          },
};
#define ROW_COUNT ((int)(sizeof(k_rows) / sizeof(k_rows[0])))

static char lower_ascii(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

static int equal_nocase(const char *a, const char *b)
{
    while (*a && *b) {
        if (lower_ascii(*a) != lower_ascii(*b)) return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

int search_provider_count(void)
{
    return ROW_COUNT;
}

const char *search_provider_label(int i)
{
    if (i < 0 || i >= ROW_COUNT) return NULL;
    return k_rows[i].label;
}

const char *search_provider_value(int i)
{
    if (i < 0 || i >= ROW_COUNT) return NULL;
    return k_rows[i].value;
}

int search_provider_index(const char *value)
{
    if (!value || !*value) return -1;
    for (int i = 0; i < ROW_COUNT; i++)
        if (equal_nocase(value, k_rows[i].value)) return i;
    return -1;
}
