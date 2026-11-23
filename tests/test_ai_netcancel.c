/* tests/test_ai_netcancel.c -- the cancel token that closes a blocked
 * request's handle (src/core/ai_netcancel.h), the stream's token, and the
 * web_search / web_fetch honouring of it through their native paths. */
#include "test_framework.h"
#include "ai_netcancel.h"
#include "ai_stream.h"
#include "ai_tool_web_search.h"
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <time.h>
#endif

static volatile int       g_closed;
static volatile uintptr_t g_closed_h;

static void fake_close(uintptr_t h)
{
    __atomic_add_fetch(&g_closed, 1, __ATOMIC_SEQ_CST);
    g_closed_h = h;
}

static void reset_closed(void)
{
    g_closed = 0;
    g_closed_h = 0;
}

int test_ai_nc_fire_before_arm_refuses(void)
{
    TEST_BEGIN();
    reset_closed();
    AiNetCancel c;
    ai_nc_init(&c);
    ASSERT_EQ(ai_nc_cancelled(&c), 0);
    ai_nc_fire(&c);
    ASSERT_EQ(ai_nc_cancelled(&c), 1);
    /* Refused: the worker closes its own handle, fire never saw it. */
    ASSERT_EQ(ai_nc_arm(&c, 0x1234, fake_close), -1);
    ASSERT_EQ(g_closed, 0);
    ASSERT_EQ(ai_nc_disarm(&c), 0);
    TEST_END();
}

int test_ai_nc_fire_closes_armed_handle_once(void)
{
    TEST_BEGIN();
    reset_closed();
    AiNetCancel c;
    ai_nc_init(&c);
    ASSERT_EQ(ai_nc_arm(&c, 0x1234, fake_close), 0);
    ai_nc_fire(&c);
    ASSERT_EQ(g_closed, 1);
    ASSERT_EQ((int)g_closed_h, 0x1234);
    ASSERT_EQ(ai_nc_cancelled(&c), 1);
    TEST_END();
}

int test_ai_nc_disarm_after_fire_returns_zero(void)
{
    TEST_BEGIN();
    reset_closed();
    AiNetCancel c;
    ai_nc_init(&c);
    ASSERT_EQ(ai_nc_arm(&c, 7, fake_close), 0);
    ai_nc_fire(&c);
    ASSERT_EQ(ai_nc_disarm(&c), 0);   /* fire closed it: the worker must not */
    ASSERT_EQ(g_closed, 1);
    TEST_END();
}

int test_ai_nc_disarm_without_fire_returns_one(void)
{
    TEST_BEGIN();
    reset_closed();
    AiNetCancel c;
    ai_nc_init(&c);
    ASSERT_EQ(ai_nc_arm(&c, 7, fake_close), 0);
    ASSERT_EQ(ai_nc_disarm(&c), 1);   /* still the worker's to close */
    ASSERT_EQ(g_closed, 0);
    /* Disarmed: a later fire has nothing to close. */
    ai_nc_fire(&c);
    ASSERT_EQ(g_closed, 0);
    /* Armed again for the next hop, until fired. */
    ai_nc_init(&c);
    ASSERT_EQ(ai_nc_arm(&c, 8, fake_close), 0);
    ASSERT_EQ(ai_nc_disarm(&c), 1);
    ASSERT_EQ(ai_nc_arm(&c, 9, fake_close), 0);
    ASSERT_EQ(ai_nc_disarm(&c), 1);
    TEST_END();
}

int test_ai_nc_fire_twice_closes_once(void)
{
    TEST_BEGIN();
    reset_closed();
    AiNetCancel c;
    ai_nc_init(&c);
    ASSERT_EQ(ai_nc_arm(&c, 5, fake_close), 0);
    ai_nc_fire(&c);
    ai_nc_fire(&c);
    ASSERT_EQ(g_closed, 1);
    TEST_END();
}

int test_ai_nc_null_token_is_harmless(void)
{
    TEST_BEGIN();
    ASSERT_EQ(ai_nc_cancelled(NULL), 0);
    ASSERT_EQ(ai_nc_arm(NULL, 1, fake_close), 0);
    ASSERT_EQ(ai_nc_disarm(NULL), 1);   /* never shared: the caller closes */
    ai_nc_fire(NULL);
    TEST_END();
}

/* ---- the race: fire on one thread, disarm on another ------------------- */

typedef struct {
    AiNetCancel *c;
    int          mine;      /* disarm result */
} RaceArg;

#ifdef _WIN32
static DWORD WINAPI race_fire(LPVOID p) { ai_nc_fire(((RaceArg *)p)->c); return 0; }
static DWORD WINAPI race_disarm(LPVOID p)
{
    RaceArg *a = (RaceArg *)p;
    a->mine = ai_nc_disarm(a->c);
    if (a->mine) fake_close(0x77);
    return 0;
}
#else
static void *race_fire(void *p) { ai_nc_fire(((RaceArg *)p)->c); return NULL; }
static void *race_disarm(void *p)
{
    RaceArg *a = (RaceArg *)p;
    a->mine = ai_nc_disarm(a->c);
    if (a->mine) fake_close(0x77);   /* the worker closes it itself */
    return NULL;
}
#endif

int test_ai_nc_concurrent_fire_and_disarm_close_exactly_once(void)
{
    TEST_BEGIN();
    for (int round = 0; round < 3000; round++) {
        reset_closed();
        AiNetCancel c;
        ai_nc_init(&c);
        ASSERT_EQ(ai_nc_arm(&c, 0x77, fake_close), 0);
        RaceArg a = { &c, 0 };
#ifdef _WIN32
        HANDLE t1 = CreateThread(NULL, 0, race_fire, &a, 0, NULL);
        HANDLE t2 = CreateThread(NULL, 0, race_disarm, &a, 0, NULL);
        WaitForSingleObject(t1, INFINITE);
        WaitForSingleObject(t2, INFINITE);
        CloseHandle(t1);
        CloseHandle(t2);
#else
        pthread_t t1, t2;
        pthread_create(&t1, NULL, race_fire, &a);
        pthread_create(&t2, NULL, race_disarm, &a);
        pthread_join(t1, NULL);
        pthread_join(t2, NULL);
#endif
        if (g_closed != 1) {
            printf("  round %d closed %d times\n", round, (int)g_closed);
            ASSERT_EQ(g_closed, 1);
            break;
        }
    }
    TEST_END();
}

/* ---- the stream's token ------------------------------------------------- */

int test_ai_stream_orphan_fires_token(void)
{
    TEST_BEGIN();
    reset_closed();
    AiStream *s = ai_stream_new();
    ASSERT_NOT_NULL(s);
    if (!s) { TEST_END(); }
    ASSERT_EQ(ai_nc_arm(&s->nc, 0x99, fake_close), 0);
    ASSERT_EQ(ai_nc_cancelled(&s->nc), 0);
    ai_stream_orphan(s);          /* the UI's stop: fires before it lets go */
    ASSERT_EQ(g_closed, 1);
    ASSERT_EQ((int)g_closed_h, 0x99);
    /* the worker side: arm now refuses, disarm says it is not ours */
    ASSERT_EQ(ai_nc_disarm(&s->nc), 0);
    ASSERT_EQ(ai_nc_arm(&s->nc, 0x9A, fake_close), -1);
    ai_stream_release(s);
    TEST_END();
}

int test_ai_stream_copy_tools_points_contexts_at_token(void)
{
    TEST_BEGIN();
    AiStream *s = ai_stream_new();
    ASSERT_NOT_NULL(s);
    if (!s) { TEST_END(); }
    WebSearchContext search;
    WebFetchContext  fetch;
    memset(&search, 0, sizeof(search));
    memset(&fetch, 0, sizeof(fetch));
    ai_stream_copy_tools(s, NULL, 1u, &search, &fetch, NULL);
    ASSERT_TRUE(s->search.nc == &s->nc);
    ASSERT_TRUE(s->fetch.nc == &s->nc);
    ai_stream_orphan(s);
    ai_stream_release(s);
    TEST_END();
}

/* ---- web_search: the native path stops on cancel ------------------------ */

int test_web_search_cancelled_flag_stops_before_request(void)
{
    TEST_BEGIN();
    volatile int cancel = 1;
    char *buf = NULL;
    size_t len = 0;
    int trunc = 0;
    int rc = tool_web_search_execute("{\"query\":\"cats\"}", NULL, &cancel,
                                     &buf, &len, &trunc);
    ASSERT_EQ(rc, -1);
    ASSERT_NOT_NULL(buf);
    if (buf) ASSERT_TRUE(strstr(buf, "cancelled") != NULL);
    free(buf);
    TEST_END();
}

int test_web_search_fired_token_stops_before_request(void)
{
    TEST_BEGIN();
    AiNetCancel nc;
    ai_nc_init(&nc);
    ai_nc_fire(&nc);
    WebSearchContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.nc = &nc;
    char *buf = NULL;
    size_t len = 0;
    int trunc = 0;
    int rc = tool_web_search_execute("{\"query\":\"cats\"}", &ctx, NULL,
                                     &buf, &len, &trunc);
    ASSERT_EQ(rc, -1);
    ASSERT_NOT_NULL(buf);
    if (buf) ASSERT_TRUE(strstr(buf, "cancelled") != NULL);
    free(buf);
    TEST_END();
}

/* ---- disarm waits for a fire whose close is still running -------------- */

static volatile int g_cb_entered, g_cb_release, g_disarm_done;

static void blocking_close(uintptr_t h)
{
    (void)h;
    __atomic_store_n(&g_cb_entered, 1, __ATOMIC_SEQ_CST);
    while (!__atomic_load_n(&g_cb_release, __ATOMIC_SEQ_CST)) { /* hold the close open */ }
}

static void nap_ms(int ms)
{
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    struct timespec ts = { 0, ms * 1000000L };
    nanosleep(&ts, NULL);
#endif
}

#ifdef _WIN32
static DWORD WINAPI bc_fire(LPVOID p) { ai_nc_fire((AiNetCancel *)p); return 0; }
static DWORD WINAPI bc_disarm(LPVOID p)
{
    int mine = ai_nc_disarm((AiNetCancel *)p);
    (void)mine;
    __atomic_store_n(&g_disarm_done, 1, __ATOMIC_SEQ_CST);
    return 0;
}
#else
static void *bc_fire(void *p) { ai_nc_fire((AiNetCancel *)p); return NULL; }
static void *bc_disarm(void *p)
{
    (void)ai_nc_disarm((AiNetCancel *)p);
    __atomic_store_n(&g_disarm_done, 1, __ATOMIC_SEQ_CST);
    return NULL;
}
#endif

int test_ai_nc_disarm_waits_for_running_close(void)
{
    TEST_BEGIN();
    g_cb_entered = g_cb_release = g_disarm_done = 0;
    AiNetCancel c;
    ai_nc_init(&c);
    ASSERT_EQ(ai_nc_arm(&c, 0x55, blocking_close), 0);
#ifdef _WIN32
    HANDLE tf = CreateThread(NULL, 0, bc_fire, &c, 0, NULL);
#else
    pthread_t tf, td;
    pthread_create(&tf, NULL, bc_fire, &c);
#endif
    for (int i = 0; i < 2000 && !g_cb_entered; i++) nap_ms(1);
    ASSERT_EQ((int)g_cb_entered, 1);
#ifdef _WIN32
    HANDLE td = CreateThread(NULL, 0, bc_disarm, &c, 0, NULL);
#else
    pthread_create(&td, NULL, bc_disarm, &c);
#endif
    nap_ms(100);
    ASSERT_EQ((int)g_disarm_done, 0);   /* the close has not returned: disarm waits */
    __atomic_store_n(&g_cb_release, 1, __ATOMIC_SEQ_CST);
#ifdef _WIN32
    WaitForSingleObject(tf, INFINITE); WaitForSingleObject(td, INFINITE);
    CloseHandle(tf); CloseHandle(td);
#else
    pthread_join(tf, NULL);
    pthread_join(td, NULL);
#endif
    ASSERT_EQ((int)g_disarm_done, 1);
    TEST_END();
}
