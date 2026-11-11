#ifndef NUTSHELL_SEARCH_PROVIDERS_H
#define NUTSHELL_SEARCH_PROVIDERS_H

/*
 * search_providers — the AI web-search providers the Settings dialog offers,
 * as a portable (Win32-free) table: the label the combo box shows and the
 * value stored in Settings.ai_search_provider. Both are ASCII.
 */

/* Number of providers (4). */
int search_provider_count(void);

/* Display label / stored value of provider i, or NULL when i is out of range. */
const char *search_provider_label(int i);
const char *search_provider_value(int i);

/* Index of the provider whose stored value equals `value`, compared ASCII
 * case-insensitively. -1 for NULL, "" or an unknown value. */
int search_provider_index(const char *value);

#endif /* NUTSHELL_SEARCH_PROVIDERS_H */
