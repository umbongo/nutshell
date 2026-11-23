/* src/ui/thread_join.c -- see thread_join.h */
#include "thread_join.h"

void thread_join_init(ThreadJoinList *l)
{
    if (!l) return;
    l->n = 0;
    for (int i = 0; i < THREAD_JOIN_MAX; i++) l->h[i] = NULL;
}

void thread_join_reap(ThreadJoinList *l)
{
    if (!l) return;
    int keep = 0;
    for (int i = 0; i < l->n; i++) {
        if (WaitForSingleObject(l->h[i], 0) == WAIT_OBJECT_0) {
            CloseHandle(l->h[i]);
        } else {
            l->h[keep++] = l->h[i];
        }
    }
    for (int i = keep; i < l->n; i++) l->h[i] = NULL;
    l->n = keep;
}

int thread_join_add(ThreadJoinList *l, HANDLE h)
{
    if (!l || !h) return 0;
    thread_join_reap(l);
    if (l->n >= THREAD_JOIN_MAX) {
        CloseHandle(h);
        return 0;
    }
    l->h[l->n++] = h;
    return 1;
}

int thread_join_wait(ThreadJoinList *l, DWORD ms)
{
    if (!l) return 0;
    DWORD start = GetTickCount();
    for (;;) {
        thread_join_reap(l);
        if (l->n == 0) return 0;
        DWORD elapsed = GetTickCount() - start;
        if (elapsed >= ms) return l->n;

        DWORD rc = MsgWaitForMultipleObjects((DWORD)l->n, l->h, FALSE,
                                             ms - elapsed, QS_SENDMESSAGE);
        if (rc == WAIT_FAILED) return l->n;
        if (rc == WAIT_OBJECT_0 + (DWORD)l->n) {
            /* A sent message is waiting: deliver it (a worker blocked on a
             * cross-thread SendMessage to this thread is released by it). */
            MSG msg;
            while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE | PM_QS_SENDMESSAGE)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
        /* otherwise one handle signalled: reaped at the top of the loop */
    }
}
