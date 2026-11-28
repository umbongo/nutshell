/* tests/test_ai_watchdog.c -- ai_nc_expire() and the hop watchdog, native.
 * Time-based, so every wait is generous (a loaded machine runs these). */

#include "test_framework.h"
#include "ai_watchdog.h"
#include <stdint.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#define WD_SLEEP(ms) Sleep(ms)
#else
#include <time.h>
static void wd_sleep(long ms)
{
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}
#define WD_SLEEP(ms) wd_sleep(ms)
#endif

static volatile int g_closes;
static volatile uintptr_t g_closed_h;

static void wd_fake_close(uintptr_t h)
{
    __atomic_fetch_add(&g_closes, 1, __ATOMIC_SEQ_CST);
    g_closed_h = h;
}

static void wd_reset(void)
{
    g_closes = 0;
    g_closed_h = 0;
}

int test_ai_nc_expire_closes_once_and_does_not_cancel(void)
{
    TEST_BEGIN();
    wd_reset();
    AiNetCancel nc; ai_nc_init(&nc);
    ASSERT_EQ(ai_nc_expire(&nc), 0);              /* nothing armed */
    ASSERT_EQ(ai_nc_arm(&nc, 0x1234, wd_fake_close), 0);
    ASSERT_EQ(ai_nc_expire(&nc), 1);
    ASSERT_EQ(ai_nc_expire(&nc), 0);              /* already taken */
    ASSERT_EQ(g_closes, 1);
    ASSERT_TRUE(g_closed_h == 0x1234);
    ASSERT_EQ(ai_nc_cancelled(&nc), 0);           /* not sticky */
    ASSERT_EQ(ai_nc_disarm(&nc), 0);              /* the handle is not ours */
    ASSERT_EQ(ai_nc_arm(&nc, 0x5678, wd_fake_close), 0);   /* next hop works */
    ASSERT_EQ(ai_nc_disarm(&nc), 1);
    ASSERT_EQ(g_closes, 1);
    ASSERT_EQ(ai_nc_expire(NULL), 0);
    TEST_END();
}

int test_ai_nc_expired_state_is_distinct_from_cancelled(void)
{
    TEST_BEGIN();
    wd_reset();
    AiNetCancel nc; ai_nc_init(&nc);
    ASSERT_EQ(ai_nc_expired(&nc), 0);
    ASSERT_EQ(ai_nc_ended(&nc), 0);
    ASSERT_EQ(ai_nc_arm(&nc, 0x21, wd_fake_close), 0);
    ASSERT_EQ(ai_nc_expire(&nc), 1);
    ASSERT_EQ(ai_nc_expired(&nc), 1);             /* the worker can see it */
    ASSERT_EQ(ai_nc_cancelled(&nc), 0);           /* ...and it is not a cancel */
    ASSERT_EQ(ai_nc_ended(&nc), 1);
    ASSERT_EQ(ai_nc_disarm(&nc), 0);              /* already closed */
    ASSERT_EQ(g_closes, 1);
    /* the next hop starts clean */
    ASSERT_EQ(ai_nc_arm(&nc, 0x22, wd_fake_close), 0);
    ASSERT_EQ(ai_nc_expired(&nc), 0);
    ASSERT_EQ(ai_nc_ended(&nc), 0);
    ASSERT_EQ(ai_nc_disarm(&nc), 1);
    /* expire with nothing armed still marks the hop */
    ASSERT_EQ(ai_nc_expire(&nc), 0);
    ASSERT_EQ(ai_nc_expired(&nc), 1);
    ASSERT_EQ(g_closes, 1);
    /* fire: cancelled, ended, not expired */
    AiNetCancel fc; ai_nc_init(&fc);
    ai_nc_fire(&fc);
    ASSERT_EQ(ai_nc_cancelled(&fc), 1);
    ASSERT_EQ(ai_nc_expired(&fc), 0);
    ASSERT_EQ(ai_nc_ended(&fc), 1);
    /* NULL token */
    ASSERT_EQ(ai_nc_expired(NULL), 0);
    ASSERT_EQ(ai_nc_ended(NULL), 0);
    TEST_END();
}

int test_ai_wd_deadline_marks_token_expired(void)
{
    TEST_BEGIN();
    wd_reset();
    AiNetCancel nc; ai_nc_init(&nc);
    ASSERT_EQ(ai_nc_arm(&nc, 0x31, wd_fake_close), 0);
    AiWatchdog *wd = ai_wd_start(&nc, 20, NULL);
    ASSERT_NOT_NULL(wd);
    WD_SLEEP(400);
    ASSERT_EQ(ai_nc_expired(&nc), 1);             /* visible before the join */
    ASSERT_EQ(ai_nc_cancelled(&nc), 0);
    ASSERT_EQ(ai_wd_stop(wd), 1);
    ASSERT_EQ(ai_nc_disarm(&nc), 0);
    ASSERT_EQ(g_closes, 1);
    TEST_END();
}

int test_ai_wd_closes_at_deadline_once(void)
{
    TEST_BEGIN();
    wd_reset();
    AiNetCancel nc; ai_nc_init(&nc);
    ASSERT_EQ(ai_nc_arm(&nc, 0x10, wd_fake_close), 0);
    AiWatchdog *wd = ai_wd_start(&nc, 80, NULL);
    ASSERT_NOT_NULL(wd);
    WD_SLEEP(600);
    ASSERT_EQ(g_closes, 1);                       /* closed with no stop yet */
    ASSERT_EQ(ai_wd_stop(wd), 1);
    ASSERT_EQ(ai_nc_disarm(&nc), 0);              /* the worker must not close */
    ASSERT_EQ(g_closes, 1);
    ASSERT_EQ(ai_nc_cancelled(&nc), 0);
    TEST_END();
}

int test_ai_wd_stop_before_deadline_closes_nothing(void)
{
    TEST_BEGIN();
    wd_reset();
    AiNetCancel nc; ai_nc_init(&nc);
    ASSERT_EQ(ai_nc_arm(&nc, 0x10, wd_fake_close), 0);
    AiWatchdog *wd = ai_wd_start(&nc, 60000, NULL);
    ASSERT_NOT_NULL(wd);
    WD_SLEEP(50);
    ASSERT_EQ(ai_wd_stop(wd), 0);                 /* joins promptly, not after 60 s */
    ASSERT_EQ(g_closes, 0);
    ASSERT_EQ(ai_nc_disarm(&nc), 1);              /* still the worker's */
    /* the thread is gone: a later hop's handle is never touched */
    ASSERT_EQ(ai_nc_arm(&nc, 0x20, wd_fake_close), 0);
    WD_SLEEP(100);
    ASSERT_EQ(g_closes, 0);
    ASSERT_EQ(ai_nc_disarm(&nc), 1);
    TEST_END();
}

int test_ai_wd_cancel_flag_closes_once(void)
{
    TEST_BEGIN();
    wd_reset();
    AiNetCancel nc; ai_nc_init(&nc);
    volatile int cancel = 0;
    ASSERT_EQ(ai_nc_arm(&nc, 0x10, wd_fake_close), 0);
    AiWatchdog *wd = ai_wd_start(&nc, 60000, &cancel);
    ASSERT_NOT_NULL(wd);
    cancel = 1;
    WD_SLEEP(600);
    ASSERT_EQ(g_closes, 1);
    ASSERT_EQ(ai_wd_stop(wd), 1);
    ASSERT_EQ(ai_nc_disarm(&nc), 0);
    ASSERT_EQ(g_closes, 1);
    TEST_END();
}

int test_ai_wd_fire_and_deadline_close_once(void)
{
    TEST_BEGIN();
    for (int i = 0; i < 20; i++) {
        wd_reset();
        AiNetCancel nc; ai_nc_init(&nc);
        ASSERT_EQ(ai_nc_arm(&nc, 0x10, wd_fake_close), 0);
        AiWatchdog *wd = ai_wd_start(&nc, 30, NULL);
        ASSERT_NOT_NULL(wd);
        WD_SLEEP(25 + i);                          /* straddle the deadline */
        ai_nc_fire(&nc);
        (void)ai_wd_stop(wd);
        ASSERT_EQ(ai_nc_disarm(&nc), 0);
        ASSERT_EQ(g_closes, 1);
    }
    TEST_END();
}

int test_ai_wd_handles_null_and_zero(void)
{
    TEST_BEGIN();
    wd_reset();
    ASSERT_EQ(ai_wd_stop(NULL), 0);
    AiNetCancel nc; ai_nc_init(&nc);
    ASSERT_EQ(ai_nc_arm(&nc, 0x10, wd_fake_close), 0);
    AiWatchdog *wd = ai_wd_start(&nc, 0, NULL);    /* 0 counts as 1 ms */
    ASSERT_NOT_NULL(wd);
    WD_SLEEP(400);
    ASSERT_EQ(ai_wd_stop(wd), 1);
    ASSERT_EQ(g_closes, 1);
    ASSERT_EQ(ai_nc_disarm(&nc), 0);
    /* no token: nothing to close, still starts and stops */
    wd = ai_wd_start(NULL, 10, NULL);
    ASSERT_NOT_NULL(wd);
    WD_SLEEP(100);
    (void)ai_wd_stop(wd);
    TEST_END();
}
