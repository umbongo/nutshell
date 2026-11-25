#ifndef NUTSHELL_VOICE_SPEAK_H
#define NUTSHELL_VOICE_SPEAK_H

/*
 * voice_speak -- pure text preparation for spoken replies: turns an AI reply
 * (Markdown) into plain speakable UTF-8, and cuts that text into sentence
 * chunks for the SAPI speech thread. Spec: docs/superpowers/specs/
 * 2026-11-24-voice-live-conversation-design.md section 8.1.
 * Block detection reuses md_classify_line (src/core/markdown.h), the
 * renderer's own line classifier, so fences, tables, headings, lists and
 * quotes are recognised exactly as they are drawn. Output is linear time.
 */

#include <stddef.h>

#define VOICE_SPEAK_FIRST_CHUNK 200
#define VOICE_SPEAK_CHUNK_MAX   1000

/* malloc'd UTF-8 speakable text; NULL only on out of memory; "" when nothing
 * is speakable. The caller frees it (secure_zero first: it is reply content). */
char *voice_speak_text(const char *markdown);

/* Cut text[from..] into chunks that end at a sentence end. The first chunk
 * takes whole sentences up to VOICE_SPEAK_FIRST_CHUNK bytes (at least one
 * sentence), later ones up to VOICE_SPEAK_CHUNK_MAX. A sentence longer than
 * the limit is split at its last space within the limit, or at the limit on a
 * UTF-8 boundary. Writes chunk offsets (into text) and byte lengths; returns
 * the number of chunks (<= max). */
size_t voice_speak_chunks(const char *text, size_t from, size_t *starts,
                          size_t *lens, size_t max);

/* 1 when the text, ignoring trailing whitespace and closing quotes, ends with
 * '?'. */
int voice_speak_ends_with_question(const char *text);

#endif
