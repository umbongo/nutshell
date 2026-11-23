/* src/ui/thread_join.h */
#ifndef NUTSHELL_THREAD_JOIN_H
#define NUTSHELL_THREAD_JOIN_H

#include <windows.h>

/*
 * thread_join -- a small list of worker-thread handles that are kept, not
 * closed at launch, so the UI can wait for them (bounded) and join them.
 *
 * UI thread only. The AI stream threads use one list (ai_chat_send.c).
 */

#define THREAD_JOIN_MAX 32   /* MsgWaitForMultipleObjects takes at most 63 */

typedef struct {
    HANDLE h[THREAD_JOIN_MAX];
    int    n;
} ThreadJoinList;

void thread_join_init(ThreadJoinList *l);

/* Close the handles of threads that have already finished and drop them. */
void thread_join_reap(ThreadJoinList *l);

/* Keep `h` for a later join (ownership passes to the list). Finished
 * threads are reaped first. When the list is still full the handle is
 * closed instead (the thread then simply cannot be joined). Returns 1 when
 * kept, 0 when closed. */
int thread_join_add(ThreadJoinList *l, HANDLE h);

/* Close every listed handle (threads running or not) and empty the list. */
void thread_join_detach_all(ThreadJoinList *l);

/* Wait until every listed thread has finished or `ms` have passed,
 * servicing this thread's sent messages meanwhile (a worker may be blocked
 * on a cross-thread SendMessage to the UI). Closes the finished handles and
 * returns how many threads are still running; those stay listed. */
int thread_join_wait(ThreadJoinList *l, DWORD ms);

#endif /* NUTSHELL_THREAD_JOIN_H */
