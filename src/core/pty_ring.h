#ifndef NUTSHELL_PTY_RING_H
#define NUTSHELL_PTY_RING_H

/* A byte ring buffer between a blocking reader thread and the UI thread.
 *
 * Pure C, no Win32 and no locking of its own: the owner (src/ui/local_pty.c)
 * guards every call with one critical section, which is also what makes the
 * "full" flag meaningful -- it is set by the producer and read/cleared by the
 * consumer under the same lock.
 *
 * See docs/superpowers/specs/2026-09-22-local-shell-design.md section 3
 * ("Reading") and section 8.
 */

#include <stddef.h>

/* Default capacity: 64 KB, heap-allocated (spec section 3). */
#define PTY_RING_CAP ((size_t)(64u * 1024u))

typedef struct PtyRing {
    unsigned char *buf;   /* cap bytes, heap; NULL until pty_ring_init()      */
    size_t         cap;   /* allocated capacity                               */
    size_t         head;  /* index of the oldest unread byte                  */
    size_t         len;   /* bytes currently held                             */
    int            eof;   /* producer saw end of stream                       */
    int            full;  /* sticky: at least one push was (partly) dropped   */
} PtyRing;

/* Allocate the backing buffer. cap == 0 means PTY_RING_CAP.
 * Returns 0 on success, -1 on allocation failure (the ring stays unusable
 * but safe: every other call is a no-op on it). */
int pty_ring_init(PtyRing *r, size_t cap);

/* Release the backing buffer and zero the struct. Safe on NULL and on an
 * already-freed ring. */
void pty_ring_free(PtyRing *r);

/* Append up to len bytes. Returns how many were actually taken: a short
 * return means the ring was (or became) full, and sets the sticky `full`
 * flag -- the producer drops the remainder rather than blocking the reader
 * thread on a UI thread that is not draining. */
size_t pty_ring_push(PtyRing *r, const void *data, size_t len);

/* Copy out up to max bytes, oldest first, and remove them.
 * Returns how many were copied (0 when empty). */
size_t pty_ring_drain(PtyRing *r, void *out, size_t max);

/* Bytes held / bytes that would be accepted by the next push. */
size_t pty_ring_available(const PtyRing *r);
size_t pty_ring_space(const PtyRing *r);

/* End-of-stream marker. Set once by the producer; the consumer reports EOF
 * to its caller only after the ring has also been drained empty. */
void pty_ring_set_eof(PtyRing *r);
int  pty_ring_eof(const PtyRing *r);

/* The sticky overflow flag, and its reset. */
int  pty_ring_full(const PtyRing *r);
void pty_ring_clear_full(PtyRing *r);

/* Drop all buffered bytes and both flags; keeps the allocation. */
void pty_ring_reset(PtyRing *r);

#endif /* NUTSHELL_PTY_RING_H */
