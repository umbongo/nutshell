/* src/core/ai_watchdog.c -- see ai_watchdog.h */
#include "ai_watchdog.h"
#include <stdlib.h>

#define WD_SLICE_MS 25u   /* how often the cancel flag is looked at */

#ifdef _WIN32
#include <windows.h>

struct AiWatchdog {
    AiNetCancel   *nc;
    volatile int  *cancel;
    ULONGLONG      deadline;
    HANDLE         stop_ev;     /* manual reset: set by ai_wd_stop */
    HANDLE         thread;
    volatile LONG  fired;
};

static unsigned long long wd_now(void) { return GetTickCount64(); }

static int wd_wait(AiWatchdog *wd, unsigned long ms)   /* 1: stop requested */
{
    return WaitForSingleObject(wd->stop_ev, ms) == WAIT_OBJECT_0;
}

static DWORD WINAPI wd_main(LPVOID arg);
#else
#include <pthread.h>
#include <time.h>

struct AiWatchdog {
    AiNetCancel   *nc;
    volatile int  *cancel;
    unsigned long long deadline;
    pthread_mutex_t mu;
    pthread_cond_t  cv;
    int             stop;       /* under mu */
    pthread_t       thread;
    int             fired;      /* written by the thread, read after join */
};

static unsigned long long wd_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000ULL + (unsigned long long)ts.tv_nsec / 1000000ULL;
}

static int wd_wait(AiWatchdog *wd, unsigned long ms)   /* 1: stop requested */
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    unsigned long long ns = (unsigned long long)ts.tv_nsec + (unsigned long long)ms * 1000000ULL;
    ts.tv_sec += (time_t)(ns / 1000000000ULL);
    ts.tv_nsec = (long)(ns % 1000000000ULL);
    pthread_mutex_lock(&wd->mu);
    while (!wd->stop) {
        if (pthread_cond_timedwait(&wd->cv, &wd->mu, &ts) != 0) break;
    }
    int stop = wd->stop;
    pthread_mutex_unlock(&wd->mu);
    return stop;
}

static void *wd_main(void *arg);
#endif

/* The loop both builds share. */
static void wd_loop(AiWatchdog *wd, int *fired)
{
    for (;;) {
        unsigned long long now = wd_now();
        int cancelled = wd->cancel && *wd->cancel;
        if (cancelled || now >= wd->deadline) {
            /* Closing through the token: exactly once, whoever is first. */
            *fired = 1;
            (void)ai_nc_expire(wd->nc);
            return;
        }
        unsigned long long left = wd->deadline - now;
        unsigned long ms = left < WD_SLICE_MS ? (unsigned long)left : WD_SLICE_MS;
        if (ms < 1) ms = 1;
        if (wd_wait(wd, ms)) return;
    }
}

#ifdef _WIN32
static DWORD WINAPI wd_main(LPVOID arg)
{
    AiWatchdog *wd = (AiWatchdog *)arg;
    int fired = 0;
    wd_loop(wd, &fired);
    if (fired) InterlockedExchange(&wd->fired, 1);
    return 0;
}

AiWatchdog *ai_wd_start(AiNetCancel *nc, unsigned long ms, volatile int *cancel)
{
    AiWatchdog *wd = calloc(1, sizeof(*wd));
    if (!wd) return NULL;
    wd->nc = nc;
    wd->cancel = cancel;
    wd->deadline = GetTickCount64() + (ms < 1 ? 1 : ms);
    wd->stop_ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!wd->stop_ev) { free(wd); return NULL; }
    wd->thread = CreateThread(NULL, 0, wd_main, wd, 0, NULL);
    if (!wd->thread) { CloseHandle(wd->stop_ev); free(wd); return NULL; }
    return wd;
}

int ai_wd_stop(AiWatchdog *wd)
{
    if (!wd) return 0;
    SetEvent(wd->stop_ev);
    WaitForSingleObject(wd->thread, INFINITE);
    int fired = wd->fired ? 1 : 0;
    CloseHandle(wd->thread);
    CloseHandle(wd->stop_ev);
    free(wd);
    return fired;
}
#else
static void *wd_main(void *arg)
{
    AiWatchdog *wd = (AiWatchdog *)arg;
    int fired = 0;
    wd_loop(wd, &fired);
    wd->fired = fired;
    return NULL;
}

AiWatchdog *ai_wd_start(AiNetCancel *nc, unsigned long ms, volatile int *cancel)
{
    AiWatchdog *wd = calloc(1, sizeof(*wd));
    if (!wd) return NULL;
    wd->nc = nc;
    wd->cancel = cancel;
    wd->deadline = wd_now() + (ms < 1 ? 1 : ms);
    pthread_condattr_t at;
    pthread_condattr_init(&at);
    pthread_condattr_setclock(&at, CLOCK_MONOTONIC);
    int ok = pthread_mutex_init(&wd->mu, NULL) == 0 &&
             pthread_cond_init(&wd->cv, &at) == 0;
    pthread_condattr_destroy(&at);
    if (!ok || pthread_create(&wd->thread, NULL, wd_main, wd) != 0) {
        free(wd);
        return NULL;
    }
    return wd;
}

int ai_wd_stop(AiWatchdog *wd)
{
    if (!wd) return 0;
    pthread_mutex_lock(&wd->mu);
    wd->stop = 1;
    pthread_cond_signal(&wd->cv);
    pthread_mutex_unlock(&wd->mu);
    pthread_join(wd->thread, NULL);
    int fired = wd->fired;
    pthread_cond_destroy(&wd->cv);
    pthread_mutex_destroy(&wd->mu);
    free(wd);
    return fired;
}
#endif
