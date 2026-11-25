#ifndef VOICE_PLAYQ_H
#define VOICE_PLAYQ_H

#include <stddef.h>
#include <stdint.h>

/* Play queue: a FIFO of mono PCM blocks for AI speech, drained by the render
 * client on the audio thread. Single-threaded by contract: whoever owns the
 * queue is the only caller (no locking here). Reply ids compare wrap-safe:
 * (int)(a - b) < 0 means a is before b. */

typedef struct {
    unsigned reply_id;
    unsigned chunk;
    uint64_t frames_in_chunk;
} VoicePlayPos;

typedef struct VoicePlayQ VoicePlayQ;

VoicePlayQ *voice_playq_new(void);
void   voice_playq_free(VoicePlayQ *q);                  /* wipes every block */

/* Takes s (malloc'd, n frames). Returns 1 when queued. Returns 0 when dropped
 * (stale reply after a flush, n == 0, NULL s or out of memory); s is then
 * wiped and freed. */
int    voice_playq_push(VoicePlayQ *q, unsigned reply_id, unsigned chunk,
                        int16_t *s, size_t n);
void   voice_playq_end(VoicePlayQ *q, unsigned reply_id);   /* the reply's last block is in */

/* Drop everything queued, pending end markers included. Later pushes and ends
 * for a reply before min_reply are dropped. */
void   voice_playq_flush(VoicePlayQ *q, unsigned min_reply);

/* 0..100 percent (clamped), Q16, applied at read time with saturation. */
void   voice_playq_set_gain(VoicePlayQ *q, int percent);

/* Copies up to frames samples into out, with gain. Fewer on an underrun. pos
 * (optional) is set to the position after the last frame written, and left
 * alone when nothing was written. out NULL or frames 0 only walks past end
 * markers at the head of the queue. */
size_t voice_playq_read(VoicePlayQ *q, int16_t *out, size_t frames, VoicePlayPos *pos);

/* 1 once per reply whose end marker read has reached; sets *reply_id. */
int    voice_playq_take_end(VoicePlayQ *q, unsigned *reply_id);

uint64_t voice_playq_queued_frames(const VoicePlayQ *q);

#endif
