/* src/core/voice_lang.h -- the speech-to-text language list, the "Other" box
 * parser and the ISO 639-1 name table. Pure: no I/O. */
#ifndef NUTSHELL_VOICE_LANG_H
#define NUTSHELL_VOICE_LANG_H

#include <stddef.h>

typedef struct {
    const char *id;      /* stored in Settings: "" "en-GB" "en-US" "fr" ... */
    const char *label;   /* the dropdown text */
    const char *iso;     /* the ISO 639-1 code sent to the provider, "" for auto */
} VoiceLang;

#define VOICE_LANG_DEFAULT "en-GB"
/* The Other box's cue banner (Settings uses it verbatim). */
#define VOICE_LANG_OTHER_CUE "2-letter code or name, e.g. cy or Welsh"

/* The listed languages; the dropdown appends "Other..." itself. */
const VoiceLang *voice_lang_table(size_t *n);

/* Table index of id, or -1 (Settings shows that as Other, keeping the value). */
int voice_lang_find(const char *id);

/* The Other box: 1 and the lower-case code in code when text (spaces trimmed) is
 * a two-letter ISO 639-1 code in any case or its English name; else 0 and "". */
int voice_lang_parse_other(const char *text, char *code, size_t cap);

/* The English name of an ISO 639-1 code, or NULL. */
const char *voice_lang_name(const char *code);

/* Inline hint for the Other box. Returns the tone: 0 neutral, 1 error. */
int voice_lang_other_hint(const char *text, char *out, size_t cap);

/* The ISO code to send for a stored id: the table's iso for a listed id, else the
 * part before the first '-' or '_', lower-cased. */
void voice_lang_iso(const char *id, char *out, size_t cap);

/* A prompt asking for the variant's spelling, or NULL ("en-GB" only). */
const char *voice_lang_spelling_hint(const char *id);

#endif
