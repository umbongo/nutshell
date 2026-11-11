/* tests/test_cmd_run_log.c */
#include "test_framework.h"
#include "cmd_run_log.h"
#include "chat_msg.h"
#include <stdlib.h>
#include <string.h>

/* An allocator that fails its `fail_at`-th call (1-based) and counts calls. */
static int g_alloc_calls;
static int g_alloc_fail_at;

static void *failing_alloc(size_t n)
{
    g_alloc_calls++;
    if (g_alloc_calls == g_alloc_fail_at) return NULL;
    return malloc(n);
}

/* A batch of `n` approved commands (n <= 16) in a fresh set. */
static CmdBatch *make_batch(CmdBatchSet *set, int n, int reply_msg)
{
    cmd_batch_set_init(set);
    CmdBatch *b = cmd_batch_add(set, NULL, NULL);
    if (!b) return NULL;
    b->reply_msg = reply_msg;
    b->q.policy.allowed = CMD_CRITICAL;
    for (int i = 0; i < n; i++) {
        char cmd[32];
        snprintf(cmd, sizeof(cmd), "echo row-%d", i);
        int idx = chat_approval_add(&b->q, cmd, CMD_PLATFORM_LINUX);
        chat_approval_approve(&b->q, idx);
    }
    return b;
}

/* Every ApprovalStatus x batch_ending 0/1, and what chat_cmd_label says of
 * the row it maps to. */
int test_cmd_run_row_every_state(void)
{
    TEST_BEGIN();
    struct {
        ApprovalStatus status;
        int ending;
        int approved, run;
        const char *label;
    } cases[] = {
        { APPROVE_COMPLETED, 0,  1, CHAT_RUN_DONE,       "ran" },
        { APPROVE_COMPLETED, 1,  1, CHAT_RUN_DONE,       "ran" },
        { APPROVE_EXECUTING, 0,  1, CHAT_RUN_RUNNING,    "running" },
        { APPROVE_EXECUTING, 1,  1, CHAT_RUN_UNFINISHED, "unfinished" },
        { APPROVE_APPROVED,  0,  1, CHAT_RUN_NONE,       "queued" },
        { APPROVE_APPROVED,  1,  1, CHAT_RUN_ABORTED,    "not run" },
        { APPROVE_DENIED,    0,  0, CHAT_RUN_NONE,       "denied" },
        { APPROVE_DENIED,    1,  0, CHAT_RUN_NONE,       "denied" },
        { APPROVE_PENDING,   0, -1, CHAT_RUN_NONE,       "skipped" },
        { APPROVE_PENDING,   1, -1, CHAT_RUN_NONE,       "skipped" },
        { APPROVE_BLOCKED,   0, -1, CHAT_RUN_NONE,       "held" },
        { APPROVE_BLOCKED,   1, -1, CHAT_RUN_NONE,       "held" },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        ApprovalEntry e;
        memset(&e, 0, sizeof(e));
        snprintf(e.command, sizeof(e.command), "ls");
        e.safety = CMD_WRITE;
        e.status = cases[i].status;
        CmdRunRow row;
        memset(&row, 0x5A, sizeof(row));
        cmd_run_row_from_entry(&e, cases[i].ending, &row);
        ASSERT_EQ(row.approved, cases[i].approved);
        /* A held row stays held: the live row says "held" (blocked wins in
         * chat_cmd_label), so the replay must too. */
        ASSERT_EQ(row.blocked, cases[i].status == APPROVE_BLOCKED ? 1 : 0);
        ASSERT_EQ(row.run, cases[i].run);
        ASSERT_EQ(row.quoted, 0);
        ASSERT_EQ((int)row.safety, (int)CMD_WRITE);
        ASSERT_NULL(row.command);   /* the command is not copied */
        ASSERT_STR_EQ(chat_cmd_label(row.approved, row.blocked, row.quoted,
                                     row.run, NULL), cases[i].label);
    }

    /* The intent of the new chip. */
    ChatCmdIntent intent = CHAT_CMD_INTENT_DIM;
    ASSERT_STR_EQ(chat_cmd_label(1, 0, 0, CHAT_RUN_UNFINISHED, &intent),
                  "unfinished");
    ASSERT_EQ((int)intent, (int)CHAT_CMD_INTENT_WARNING);

    /* quoted is copied and wins, with its reason. */
    ApprovalEntry q;
    memset(&q, 0, sizeof(q));
    q.status = APPROVE_BLOCKED;
    q.quoted = 1;
    snprintf(q.reason, sizeof(q.reason), "reads credentials");
    CmdRunRow qr;
    cmd_run_row_from_entry(&q, 1, &qr);
    ASSERT_EQ(qr.quoted, 1);
    ASSERT_STR_EQ(qr.reason, "reads credentials");
    ASSERT_STR_EQ(chat_cmd_label(qr.approved, qr.blocked, qr.quoted, qr.run,
                                 NULL), "quoted, not run");

    /* NULL arguments are ignored. */
    cmd_run_row_from_entry(NULL, 0, &qr);
    cmd_run_row_from_entry(&q, 0, NULL);

    /* Replayed as rows (the rebuild's fields), a held and a skipped entry
     * count "held" and "skipped" in the batch summary, as they did live. */
    ApprovalEntry he, se;
    memset(&he, 0, sizeof(he));
    memset(&se, 0, sizeof(se));
    he.status = APPROVE_BLOCKED;
    se.status = APPROVE_PENDING;
    ApprovalEntry *ents[2] = { &he, &se };
    ChatMsgList list;
    chat_msg_list_init(&list);
    for (int i = 0; i < 2; i++) {
        CmdRunRow rr;
        cmd_run_row_from_entry(ents[i], 1, &rr);
        ChatMsgItem *it = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
        ASSERT_NOT_NULL(it);
        chat_msg_set_command(it, "ls", rr.safety, rr.blocked);
        chat_msg_set_batch(it, -1);
        it->u.cmd.approved = rr.approved;
        it->u.cmd.run = rr.run;
        it->u.cmd.settled = 1;
    }
    ChatBatchCounts counts;
    ASSERT_EQ(chat_msg_batch_counts(&list, -1, &counts), 2);
    ASSERT_EQ(counts.held, 1);
    ASSERT_EQ(counts.skipped, 1);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_cmd_run_log_record_copies(void)
{
    TEST_BEGIN();
    CmdBatchSet set;
    CmdBatch *b = make_batch(&set, 3, 4);
    ASSERT_NOT_NULL(b);
    ASSERT_EQ(b->q.count, 3);
    snprintf(b->q.entries[1].reason, sizeof(b->q.entries[1].reason),
             "touches keys");
    chat_approval_set_executing(&b->q, 0);
    chat_approval_set_completed(&b->q, 0);
    chat_approval_set_executing(&b->q, 1);

    CmdRunLog log;
    cmd_run_log_init(&log);
    ASSERT_EQ(log.count, 0);
    ASSERT_EQ(cmd_run_log_record(&log, b, 0xABCDu, 1), 0);
    ASSERT_EQ(log.count, 1);
    const CmdRunRecord *r = log.rec[0];
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->n, 3);
    ASSERT_EQ(r->reply_msg, 4);
    ASSERT_EQ((int)r->turn_hash, 0xABCD);
    ASSERT_STR_EQ(r->rows[0].command, "echo row-0");
    ASSERT_STR_EQ(r->rows[1].command, "echo row-1");
    ASSERT_STR_EQ(r->rows[2].command, "echo row-2");
    ASSERT_EQ(r->rows[0].run, CHAT_RUN_DONE);
    ASSERT_EQ(r->rows[1].run, CHAT_RUN_UNFINISHED);   /* batch_ending 1 */
    ASSERT_EQ(r->rows[2].run, CHAT_RUN_ABORTED);
    ASSERT_STR_EQ(r->rows[1].reason, "touches keys");

    /* The copies are the log's own: change the source afterwards. */
    ASSERT_TRUE(r->rows[0].command != b->q.entries[0].command);
    snprintf(b->q.entries[0].command, sizeof(b->q.entries[0].command),
             "changed");
    ASSERT_STR_EQ(r->rows[0].command, "echo row-0");

    cmd_run_log_free(&log);
    cmd_batch_set_free(&set);
    TEST_END();
}

int test_cmd_run_log_refusals(void)
{
    TEST_BEGIN();
    CmdBatchSet set;
    CmdBatch *b = make_batch(&set, 2, 1);
    ASSERT_NOT_NULL(b);
    CmdRunLog log;
    cmd_run_log_init(&log);

    ASSERT_EQ(cmd_run_log_record(NULL, b, 1, 0), -1);
    ASSERT_EQ(cmd_run_log_record(&log, NULL, 1, 0), -1);
    ASSERT_EQ(log.count, 0);

    b->tool_stream_id = 7;                       /* a tool batch */
    ASSERT_EQ(cmd_run_log_record(&log, b, 1, 0), -1);
    ASSERT_EQ(log.count, 0);
    b->tool_stream_id = 0;

    b->q.count = 0;                              /* an empty queue */
    ASSERT_EQ(cmd_run_log_record(&log, b, 1, 0), -1);
    ASSERT_EQ(log.count, 0);

    cmd_run_log_free(&log);
    cmd_batch_set_free(&set);
    TEST_END();
}

int test_cmd_run_log_drops_oldest(void)
{
    TEST_BEGIN();
    CmdBatchSet set;
    CmdBatch *b = make_batch(&set, 2, 0);
    ASSERT_NOT_NULL(b);
    CmdRunLog log;
    cmd_run_log_init(&log);

    for (int i = 1; i <= CMD_RUN_LOG_MAX + 1; i++) {
        b->reply_msg = i;
        ASSERT_EQ(cmd_run_log_record(&log, b, (uint32_t)i, 0), 0);
        ASSERT_EQ(log.count, i <= CMD_RUN_LOG_MAX ? i : CMD_RUN_LOG_MAX);
    }
    ASSERT_EQ(log.count, 16);
    ASSERT_EQ(log.rec[0]->reply_msg, 2);                 /* the 1st was dropped */
    ASSERT_EQ(log.rec[15]->reply_msg, 17);               /* newest last */
    for (int i = 0; i < log.count; i++) {
        ASSERT_NOT_NULL(log.rec[i]);
        ASSERT_EQ(log.rec[i]->reply_msg, i + 2);
    }

    cmd_run_log_free(&log);
    cmd_batch_set_free(&set);
    TEST_END();
}

int test_cmd_run_log_free(void)
{
    TEST_BEGIN();
    CmdBatchSet set;
    CmdBatch *b = make_batch(&set, 2, 1);
    ASSERT_NOT_NULL(b);
    CmdRunLog log;
    cmd_run_log_init(&log);
    ASSERT_EQ(cmd_run_log_record(&log, b, 1, 0), 0);
    ASSERT_EQ(cmd_run_log_record(&log, b, 2, 0), 0);
    ASSERT_EQ(log.count, 2);

    cmd_run_log_free(&log);
    ASSERT_EQ(log.count, 0);
    ASSERT_NULL(log.rec[0]);
    cmd_run_log_free(&log);          /* twice is safe */
    ASSERT_EQ(log.count, 0);
    cmd_run_log_free(NULL);          /* NULL is safe */
    cmd_run_log_init(NULL);

    cmd_batch_set_free(&set);
    TEST_END();
}

int test_cmd_run_log_turn_hash(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(cmd_run_log_turn_hash("", 0) == 0x811C9DC5u);   /* FNV-1a 32 offset basis */
    ASSERT_TRUE(cmd_run_log_turn_hash("abc", 3) == 0x1A47E90Bu);
    ASSERT_TRUE(cmd_run_log_turn_hash(NULL, 0) == 0x811C9DC5u);
    /* Only len bytes are read. */
    ASSERT_TRUE(cmd_run_log_turn_hash("abcdef", 3) == 0x1A47E90Bu);
    ASSERT_TRUE(cmd_run_log_turn_hash("abd", 3) != cmd_run_log_turn_hash("abc", 3));
    ASSERT_TRUE(cmd_run_log_turn_hash("Abc", 3) != cmd_run_log_turn_hash("abc", 3));
    TEST_END();
}

/* The rebuild's match rule (spec 21 (c)): reply_msg, an assistant turn, and
 * the content's hash. */
int test_cmd_run_record_matches_rule(void)
{
    TEST_BEGIN();
    const char *text = "I will run two commands.";
    size_t len = strlen(text);
    CmdRunRecord rec;
    memset(&rec, 0, sizeof(rec));
    rec.reply_msg = 5;
    rec.turn_hash = cmd_run_log_turn_hash(text, len);

    ASSERT_EQ(cmd_run_record_matches(&rec, 5, 1, text, len), 1);
    ASSERT_EQ(cmd_run_record_matches(&rec, 4, 1, text, len), 0);   /* other index */
    ASSERT_EQ(cmd_run_record_matches(&rec, 5, 0, text, len), 0);   /* not an assistant turn */
    ASSERT_EQ(cmd_run_record_matches(&rec, 5, 1, "I will run two commands!", len), 0);
    ASSERT_EQ(cmd_run_record_matches(&rec, 5, 1, text, len - 1), 0);
    ASSERT_EQ(cmd_run_record_matches(NULL, 5, 1, text, len), 0);
    ASSERT_EQ(cmd_run_record_matches(&rec, 5, 1, NULL, 0), 0);     /* hash of "" differs */
    TEST_END();
}

/* Spec 25: a copy that cannot be allocated records nothing, whichever
 * allocation fails, with the log full or not, and nothing leaks. */
int test_cmd_run_log_oom_records_nothing(void)
{
    TEST_BEGIN();
    CmdBatchSet set;
    CmdBatch *b = make_batch(&set, 3, 2);
    ASSERT_NOT_NULL(b);
    CmdRunLog log;
    cmd_run_log_init(&log);
    ASSERT_EQ(cmd_run_log_record(&log, b, 11, 0), 0);
    ASSERT_EQ(log.count, 1);
    const CmdRunRecord *kept = log.rec[0];

    cmd_run_log_set_alloc_for_test(failing_alloc);

    /* One allocation for the record, one per command copy: 4 in all. Fail
     * each in turn, so the failure is part-way through the rows. */
    for (int at = 1; at <= 4; at++) {
        g_alloc_calls = 0;
        g_alloc_fail_at = at;
        ASSERT_EQ(cmd_run_log_record(&log, b, 22, 0), -1);
        ASSERT_EQ(log.count, 1);
        ASSERT_TRUE(log.rec[0] == kept);
    }

    /* A full log keeps its oldest record when the new one cannot be made. */
    for (int i = 0; i < CMD_RUN_LOG_MAX - 1; i++) {
        g_alloc_calls = 0;
        g_alloc_fail_at = 0;
        ASSERT_EQ(cmd_run_log_record(&log, b, 33, 0), 0);
    }
    ASSERT_EQ(log.count, CMD_RUN_LOG_MAX);
    g_alloc_calls = 0;
    g_alloc_fail_at = 3;
    ASSERT_EQ(cmd_run_log_record(&log, b, 44, 0), -1);
    ASSERT_EQ(log.count, CMD_RUN_LOG_MAX);
    ASSERT_TRUE(log.rec[0] == kept);

    /* With no failure the same call goes through. */
    g_alloc_calls = 0;
    g_alloc_fail_at = 0;
    ASSERT_EQ(cmd_run_log_record(&log, b, 44, 0), 0);
    ASSERT_EQ(log.count, CMD_RUN_LOG_MAX);
    ASSERT_EQ(g_alloc_calls, 4);

    cmd_run_log_set_alloc_for_test(NULL);      /* restore the default */
    cmd_run_log_free(&log);
    cmd_batch_set_free(&set);
    TEST_END();
}
