#ifndef NUTSHELL_HTML_UTIL_H
#define NUTSHELL_HTML_UTIL_H

#include <stddef.h>

/* Strip all HTML tags from input, writing plain text to out_buf.
 * Removes <script> and <style> blocks entirely (including content).
 * Preserves text content between tags.
 * Returns bytes written (excluding NUL). */
size_t html_strip_tags(const char *html, size_t html_len,
                       char *out_buf, size_t out_max);

/* Decode HTML entities in-place: &amp; &quot; &#x27; &lt; &gt; &#NNN; &#xHHHH;
 * Returns new length after decoding. */
size_t html_decode_entities(char *buf, size_t len);

/* URL-decode a percent-encoded string in-place: %20 -> space, %2F -> /, etc.
 * Applies a second decode pass if the result still contains %-sequences.
 * Returns new length. */
size_t html_url_decode(char *buf, size_t len);

/* Strip specific tags by name while keeping their text content.
 * Modifies buf in-place. Returns new length. */
size_t html_strip_tag_by_name(char *buf, size_t len, const char *tag_name);

/* Extract text content between the first occurrence of a tag with the given
 * class attribute value. Returns pointer into html (not a copy), sets *out_len.
 * Returns NULL if not found. Class matching checks if the class string CONTAINS
 * the target (substring match, not exact -- supports multi-class elements). */
const char *html_find_by_class(const char *html, size_t html_len,
                               const char *class_value, size_t *out_len);

#endif /* NUTSHELL_HTML_UTIL_H */
