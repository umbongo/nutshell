#ifndef NUTSHELL_CORE_VOICE_TRANSCRIPT_H
#define NUTSHELL_CORE_VOICE_TRANSCRIPT_H

#include <stddef.h>

/* Live-transcript buffer for dictation.  It holds what a live recording has
 * put into the AI panel's input box after the user's existing draft: the
 * committed (final) text of finished segments plus a provisional tail per
 * open segment, and says which range of the box (UTF-16 units, for EM_SETSEL)
 * to replace with what text after each update.
 *
 * Invariant of vt_render(): after the glue replaces [from, end) with the
 * returned text, the box from draft_u16 to the end equals the whole rendered
 * live text (separator + committed + open segments, joined).  `from` is the
 * first UTF-16 offset that may differ from what the previous render left.
 *
 * Text is user content: every heap buffer is wiped with secure_zero before it
 * is freed or reallocated away, and nothing here logs. */

#define VT_SEG_MAX 32
#define VT_ID_MAX 64
#define VT_FIN_MAX 64   /* finished ids remembered; later events for them are ignored */

typedef struct {
    char   id[VT_ID_MAX];
    char  *text;
    size_t tlen, tcap;
    int    done;
} VtSeg;

typedef struct {
    size_t draft_u16;         /* box length (UTF-16 units) at start */
    int    need_sep;          /* draft non-empty and not ending in whitespace */
    char  *committed;  size_t clen, ccap;   /* final text of finished segments, in order */
    VtSeg  seg[VT_SEG_MAX];
    int    nseg;
    char   fin[VT_FIN_MAX][VT_ID_MAX];   /* ring of finished ids */
    int    nfin, fin_next;
    char  *shown;      size_t slen, scap;   /* what the box holds after draft_u16 */
    size_t shown_u16;         /* UTF-16 length, after draft_u16, of what the box holds now */
    size_t stable_u16;        /* UTF-16 length, after draft_u16, left unchanged by the last render */
} VoiceTranscript;

void   vt_init(VoiceTranscript *t, size_t draft_u16, int need_sep);
/* 0 on success, -1 on out of memory or bad arguments. */
int    vt_delta(VoiceTranscript *t, const char *seg, const char *text, int append_only);
/* Register a segment in commit order before any text for it arrives (OpenAI's
 * input_audio_buffer.committed), so completions that come out of order still
 * land in commit order. A known or finished id is left alone. */
int    vt_register(VoiceTranscript *t, const char *seg);
int    vt_complete(VoiceTranscript *t, const char *seg, const char *final_text); /* NULL = keep the deltas */
int    vt_replace_all(VoiceTranscript *t, const char *text);  /* Mistral done, batch fallback */
/* Heap UTF-8 for [from, end); caller frees (secure_zero then free). NULL on OOM. */
char  *vt_render(VoiceTranscript *t, size_t *from_u16);
/* The whole text the last vt_render left after the draft (separator, committed text,
 * open segments); "" before any render or for NULL. Valid until the next render. A caller
 * that cannot trust UTF-16 offsets on its control replaces its own tail with this. */
const char *vt_rendered(const VoiceTranscript *t);
/* Heap: the recording's whole text, joined, no separator. */
char  *vt_text(const VoiceTranscript *t);
int    vt_all_done(const VoiceTranscript *t);
void   vt_free(VoiceTranscript *t);
size_t vt_utf16_len(const char *utf8, size_t n);

#endif
