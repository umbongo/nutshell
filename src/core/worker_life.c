/* src/core/worker_life.c -- see worker_life.h */
#include "worker_life.h"

void worker_life_init(WorkerLife *l)
{
    if (!l) return;
    __atomic_store_n(&l->refs, 2L, __ATOMIC_SEQ_CST);
    __atomic_store_n(&l->cancelled, 0, __ATOMIC_SEQ_CST);
}

void worker_life_cancel(WorkerLife *l)
{
    if (!l) return;
    __atomic_store_n(&l->cancelled, 1, __ATOMIC_SEQ_CST);
}

int worker_life_cancelled(const WorkerLife *l)
{
    if (!l) return 1;
    return __atomic_load_n(&l->cancelled, __ATOMIC_SEQ_CST) ? 1 : 0;
}

int worker_life_release(WorkerLife *l)
{
    if (!l) return 0;
    /* ACQ_REL: everything this side wrote to the object happens-before the
     * other side's free of it. */
    return __atomic_sub_fetch(&l->refs, 1L, __ATOMIC_ACQ_REL) == 0 ? 1 : 0;
}

int worker_life_refs(const WorkerLife *l)
{
    if (!l) return 0;
    return (int)__atomic_load_n(&l->refs, __ATOMIC_SEQ_CST);
}

unsigned worker_life_next_id(void)
{
    static volatile unsigned counter = 0u;
    unsigned id;
    do {
        id = __atomic_add_fetch(&counter, 1u, __ATOMIC_SEQ_CST);
    } while (id == 0u);   /* wrapped: 0 means "no stream" to callers */
    return id;
}
