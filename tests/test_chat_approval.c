/* tests/test_chat_approval.c
 *
 * Translated onto the CmdPolicy model
 * (docs/superpowers/specs/2026-09-11-status-policy-control-design.md).
 * This file tests the QUEUE's behaviour against a policy it is handed --
 * not the policy model itself, which is tests/test_cmd_policy.c's job.
 */
#include "test_framework.h"
#include "chat_approval.h"
#include "cmd_policy.h"
#include "ai_prompt.h"
#include <string.h>

int test_approval_init(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    ASSERT_EQ(q.count, 0);
    /* chat_approval_init() starts at cmd_policy_default(): read-only,
     * nothing unattended -- exactly today's default. */
    ASSERT_EQ(q.policy.allowed, (int)CMD_READ);
    ASSERT_EQ(q.policy.unattended, POLICY_NONE);
    TEST_END();
}

int test_approval_add_read(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    int idx = chat_approval_add(&q, "ls -la", CMD_PLATFORM_LINUX);
    ASSERT_EQ(idx, 0);
    ASSERT_EQ(q.count, 1);
    ASSERT_EQ((int)q.entries[0].safety, (int)CMD_READ);
    ASSERT_EQ((int)q.entries[0].status, (int)APPROVE_PENDING);
    TEST_END();
}

int test_approval_add_blocked(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);   /* default: allowed = CMD_READ */
    int idx = chat_approval_add(&q, "mv a b", CMD_PLATFORM_LINUX);
    ASSERT_EQ(idx, 0);
    ASSERT_EQ((int)q.entries[0].status, (int)APPROVE_BLOCKED);
    TEST_END();
}

int test_approval_add_write_permitted(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    cmd_policy_set_allowed(&q.policy, CMD_WRITE);   /* was: permit_write = 1 */
    int idx = chat_approval_add(&q, "mv a b", CMD_PLATFORM_LINUX);
    ASSERT_EQ(idx, 0);
    ASSERT_EQ((int)q.entries[0].status, (int)APPROVE_PENDING);
    TEST_END();
}

int test_approval_approve(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    chat_approval_add(&q, "ls", CMD_PLATFORM_LINUX);
    ASSERT_EQ(chat_approval_approve(&q, 0), 0);
    ASSERT_EQ((int)q.entries[0].status, (int)APPROVE_APPROVED);
    TEST_END();
}

int test_approval_deny(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    cmd_policy_set_allowed(&q.policy, CMD_CRITICAL);   /* was: permit_write = 1 */
    chat_approval_add(&q, "rm -rf /tmp", CMD_PLATFORM_LINUX);
    ASSERT_EQ(chat_approval_deny(&q, 0), 0);
    ASSERT_EQ((int)q.entries[0].status, (int)APPROVE_DENIED);
    TEST_END();
}

int test_approval_approve_all(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    cmd_policy_set_allowed(&q.policy, CMD_WRITE);   /* was: permit_write = 1 */
    chat_approval_add(&q, "ls", CMD_PLATFORM_LINUX);
    chat_approval_add(&q, "cat /etc/hosts", CMD_PLATFORM_LINUX);
    chat_approval_add(&q, "cp a b", CMD_PLATFORM_LINUX);
    int n = chat_approval_approve_all(&q);
    ASSERT_EQ(n, 3);
    for (int i = 0; i < 3; i++)
        ASSERT_EQ((int)q.entries[i].status, (int)APPROVE_APPROVED);
    TEST_END();
}

int test_approval_all_decided(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    chat_approval_add(&q, "ls", CMD_PLATFORM_LINUX);
    chat_approval_add(&q, "cat f", CMD_PLATFORM_LINUX);
    ASSERT_EQ(chat_approval_all_decided(&q), 0);
    chat_approval_approve(&q, 0);
    ASSERT_EQ(chat_approval_all_decided(&q), 0);
    chat_approval_deny(&q, 1);
    ASSERT_EQ(chat_approval_all_decided(&q), 1);
    TEST_END();
}

int test_approval_next_approved(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    chat_approval_add(&q, "ls", CMD_PLATFORM_LINUX);
    chat_approval_add(&q, "cat f", CMD_PLATFORM_LINUX);
    chat_approval_approve(&q, 0);
    chat_approval_approve(&q, 1);
    ASSERT_EQ(chat_approval_next_approved(&q), 0);
    chat_approval_set_executing(&q, 0);
    ASSERT_EQ(chat_approval_next_approved(&q), 1);
    TEST_END();
}

int test_approval_execute_complete(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    chat_approval_add(&q, "ls", CMD_PLATFORM_LINUX);
    chat_approval_approve(&q, 0);
    chat_approval_set_executing(&q, 0);
    ASSERT_EQ((int)q.entries[0].status, (int)APPROVE_EXECUTING);
    chat_approval_set_completed(&q, 0);
    ASSERT_EQ((int)q.entries[0].status, (int)APPROVE_COMPLETED);
    TEST_END();
}

int test_approval_empty_command(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    int idx = chat_approval_add(&q, "", CMD_PLATFORM_LINUX);
    ASSERT_EQ(idx, -1);
    TEST_END();
}

int test_approval_whitespace_command(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    int idx = chat_approval_add(&q, "   ", CMD_PLATFORM_LINUX);
    ASSERT_EQ(idx, -1);
    TEST_END();
}

/* C1 defense in depth: chat_approval_add() refuses a command containing
 * a raw control byte even if some other caller bypasses
 * ai_extract_commands()'s own filtering. */
int test_approval_add_embedded_newline_rejected(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    int idx = chat_approval_add(&q, "echo ok\nrm -rf ~", CMD_PLATFORM_LINUX);
    ASSERT_EQ(idx, -1);
    ASSERT_EQ(q.count, 0);
    TEST_END();
}

int test_approval_queue_full(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    for (int i = 0; i < APPROVAL_MAX_CMDS; i++)
        ASSERT_TRUE(chat_approval_add(&q, "ls", CMD_PLATFORM_LINUX) >= 0);
    ASSERT_EQ(chat_approval_add(&q, "ls", CMD_PLATFORM_LINUX), -1);
    TEST_END();
}

int test_approval_reset(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    chat_approval_add(&q, "ls", CMD_PLATFORM_LINUX);
    chat_approval_add(&q, "cat f", CMD_PLATFORM_LINUX);
    chat_approval_reset(&q);
    ASSERT_EQ(q.count, 0);
    TEST_END();
}

/* Translated from the old direct q.auto_approve field toggle: moving the
 * unattended marker changes the decision for commands added afterward.
 * Entries already decided are untouched by the marker move itself -- that
 * is chat_approval_unblock_all()/block_disallowed()'s job (see
 * test_approval_ceiling_raise_unblocks_and_lower_reblocks below), not
 * add()'s. */
int test_approval_unattended_marker_governs_new_adds(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    ASSERT_EQ(q.policy.unattended, POLICY_NONE);

    chat_approval_add(&q, "ls", CMD_PLATFORM_LINUX);
    ASSERT_EQ((int)q.entries[0].status, (int)APPROVE_PENDING);

    cmd_policy_set_unattended(&q.policy, CMD_READ);
    chat_approval_add(&q, "cat /etc/hosts", CMD_PLATFORM_LINUX);
    ASSERT_EQ((int)q.entries[1].status, (int)APPROVE_APPROVED);
    /* The earlier entry is untouched by the marker move. */
    ASSERT_EQ((int)q.entries[0].status, (int)APPROVE_PENDING);

    cmd_policy_set_unattended(&q.policy, POLICY_NONE);
    chat_approval_add(&q, "pwd", CMD_PLATFORM_LINUX);
    ASSERT_EQ((int)q.entries[2].status, (int)APPROVE_PENDING);
    TEST_END();
}

/* Replaces test_approval_auto_approve_persists_across_reset and
 * test_approval_reset_preserves_level: chat_approval_reset() preserves the
 * policy (both fields), and the next batch decides against that same,
 * preserved policy. */
int test_approval_reset_preserves_policy(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    cmd_policy_set_allowed(&q.policy, CMD_WRITE);
    cmd_policy_set_unattended(&q.policy, CMD_READ);

    chat_approval_add(&q, "ls", CMD_PLATFORM_LINUX);              /* READ  -> APPROVED */
    chat_approval_add(&q, "cat /etc/hosts", CMD_PLATFORM_LINUX);  /* READ  -> APPROVED */
    ASSERT_EQ((int)q.entries[0].status, (int)APPROVE_APPROVED);
    ASSERT_EQ((int)q.entries[1].status, (int)APPROVE_APPROVED);
    ASSERT_EQ(chat_approval_all_decided(&q), 1);

    chat_approval_reset(&q);
    ASSERT_EQ(q.count, 0);
    ASSERT_EQ(q.policy.allowed, (int)CMD_WRITE);
    ASSERT_EQ(q.policy.unattended, (int)CMD_READ);

    /* The next batch decides against the same, preserved policy. */
    chat_approval_add(&q, "pwd", CMD_PLATFORM_LINUX);       /* READ  -> APPROVED */
    chat_approval_add(&q, "mv a b", CMD_PLATFORM_LINUX);    /* WRITE -> allowed, not unattended -> PENDING */
    ASSERT_EQ((int)q.entries[0].status, (int)APPROVE_APPROVED);
    ASSERT_EQ((int)q.entries[1].status, (int)APPROVE_PENDING);
    TEST_END();
}

/* Replaces test_approval_block_pending_writes and
 * test_approval_block_pending_writes_skips_decided: chat_approval_
 * block_pending_writes() is renamed chat_approval_block_disallowed() and
 * now keys off the ceiling rather than a boolean. This walks both
 * directions -- raising the ceiling unblocks, lowering it re-blocks only
 * the entries now above it -- and exercises every terminal status
 * (DENIED, EXECUTING, COMPLETED) alongside PENDING to confirm decided
 * entries are left alone even when their category is above the new,
 * lowered ceiling.
 *
 * chat_approval_unblock_all() is the exact inverse of block_disallowed():
 * it only unblocks entries the CURRENT policy (set before the call, same
 * as the real caller does) actually allows, so a partial raise -- Read to
 * Write, not all the way to Critical -- must not release a still-too-high
 * CRITICAL row. That partial-raise step is asserted explicitly below,
 * followed by the full raise, then the round trip back down to the
 * ceiling's starting value, which must land the CRITICAL row back where
 * it started: BLOCKED. */
int test_approval_ceiling_raise_unblocks_and_lower_reblocks(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);   /* allowed = Read */

    int i_read  = chat_approval_add(&q, "ls",          CMD_PLATFORM_LINUX); /* READ */
    int i_write = chat_approval_add(&q, "mv a b",      CMD_PLATFORM_LINUX); /* WRITE */
    int i_crit  = chat_approval_add(&q, "rm -rf /tmp", CMD_PLATFORM_LINUX); /* CRITICAL */
    ASSERT_EQ((int)q.entries[i_read].status, (int)APPROVE_PENDING);
    ASSERT_EQ((int)q.entries[i_write].status, (int)APPROVE_BLOCKED);
    ASSERT_EQ((int)q.entries[i_crit].status, (int)APPROVE_BLOCKED);
    ASSERT_EQ(chat_approval_deny(&q, i_read), 0);   /* i_read: DENIED, a decided status */

    /* Partial raise, Read -> Write: unblock_all() must release only the
     * WRITE row now at-or-below the ceiling, and must NOT release the
     * still-too-high CRITICAL row. */
    cmd_policy_set_allowed(&q.policy, CMD_WRITE);
    int n = chat_approval_unblock_all(&q);
    ASSERT_EQ(n, 1);
    ASSERT_EQ((int)q.entries[i_read].status, (int)APPROVE_DENIED);
    ASSERT_EQ((int)q.entries[i_write].status, (int)APPROVE_PENDING);
    ASSERT_EQ((int)q.entries[i_crit].status, (int)APPROVE_BLOCKED);

    /* Full raise, Write -> Critical: now the CRITICAL row releases too. */
    cmd_policy_set_allowed(&q.policy, CMD_CRITICAL);
    n = chat_approval_unblock_all(&q);
    ASSERT_EQ(n, 1);
    ASSERT_EQ((int)q.entries[i_crit].status, (int)APPROVE_PENDING);

    /* Walk the write entry through EXECUTING, and a second write entry
     * through COMPLETED, so the "lower the ceiling" pass below has one of
     * each decided status to leave alone. */
    chat_approval_approve(&q, i_write);
    chat_approval_set_executing(&q, i_write);          /* i_write: EXECUTING */

    int i_write2 = chat_approval_add(&q, "cp x y", CMD_PLATFORM_LINUX);  /* WRITE, ceiling Critical -> PENDING */
    chat_approval_approve(&q, i_write2);
    chat_approval_set_executing(&q, i_write2);
    chat_approval_set_completed(&q, i_write2);         /* i_write2: COMPLETED */

    /* Lower the ceiling all the way back to Read: block_disallowed()
     * blocks exactly the still-PENDING entry above it (i_crit) -- the
     * round trip lands it back where it started, BLOCKED -- and leaves
     * every decided entry -- DENIED, EXECUTING, COMPLETED -- untouched,
     * even though their categories are now above the ceiling too. */
    cmd_policy_set_allowed(&q.policy, CMD_READ);
    n = chat_approval_block_disallowed(&q);
    ASSERT_EQ(n, 1);
    ASSERT_EQ((int)q.entries[i_read].status, (int)APPROVE_DENIED);
    ASSERT_EQ((int)q.entries[i_write].status, (int)APPROVE_EXECUTING);
    ASSERT_EQ((int)q.entries[i_crit].status, (int)APPROVE_BLOCKED);
    ASSERT_EQ((int)q.entries[i_write2].status, (int)APPROVE_COMPLETED);
    TEST_END();
}

/* --- chat_approval_needs_user: does the approval card need to stay up? --- */

int test_needs_user_empty_queue(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    ASSERT_EQ(chat_approval_needs_user(&q), 0);
    TEST_END();
}

int test_needs_user_all_pending(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    chat_approval_add(&q, "ls", CMD_PLATFORM_LINUX);
    chat_approval_add(&q, "cat f", CMD_PLATFORM_LINUX);
    ASSERT_EQ(chat_approval_needs_user(&q), 1);
    TEST_END();
}

int test_needs_user_pending_and_blocked(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    chat_approval_add(&q, "ls", CMD_PLATFORM_LINUX);       /* pending */
    chat_approval_add(&q, "mv a b", CMD_PLATFORM_LINUX);   /* blocked: ceiling is Read */
    ASSERT_EQ(chat_approval_needs_user(&q), 1);
    TEST_END();
}

int test_needs_user_blocked_only(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    /* Every command is above the default Read ceiling: nothing pending,
     * nothing approved, but nothing will run either -- the card must
     * stay up so the user can raise the ceiling and run, or deny. */
    chat_approval_add(&q, "mv a b", CMD_PLATFORM_LINUX);
    chat_approval_add(&q, "rm f", CMD_PLATFORM_LINUX);
    ASSERT_EQ(chat_approval_needs_user(&q), 1);
    TEST_END();
}

int test_needs_user_blocked_and_approved_auto_approve(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    cmd_policy_set_unattended(&q.policy, CMD_READ);   /* was: q.auto_approve = 1 */

    /* Read command auto-approves; write command is above the (still Read)
     * ceiling and is blocked. Since something WILL run, the card doesn't
     * need to stay up for the blocked one -- it's just noise on the side. */
    chat_approval_add(&q, "ls", CMD_PLATFORM_LINUX);
    chat_approval_add(&q, "mv a b", CMD_PLATFORM_LINUX);
    ASSERT_EQ((int)q.entries[0].status, (int)APPROVE_APPROVED);
    ASSERT_EQ((int)q.entries[1].status, (int)APPROVE_BLOCKED);
    ASSERT_EQ(chat_approval_needs_user(&q), 0);
    TEST_END();
}

int test_needs_user_approved_only(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    chat_approval_add(&q, "ls", CMD_PLATFORM_LINUX);
    chat_approval_approve(&q, 0);
    ASSERT_EQ(chat_approval_needs_user(&q), 0);
    TEST_END();
}

int test_needs_user_denied_only(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    chat_approval_add(&q, "ls", CMD_PLATFORM_LINUX);
    chat_approval_deny(&q, 0);
    ASSERT_EQ(chat_approval_needs_user(&q), 0);
    TEST_END();
}

int test_needs_user_after_unblock_all(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    /* Blocked-only batch needs the user... */
    chat_approval_add(&q, "mv a b", CMD_PLATFORM_LINUX);
    ASSERT_EQ(chat_approval_needs_user(&q), 1);
    /* ...and after the ceiling is raised to Write and unblocks it, it's
     * now PENDING, which still needs the user (to actually run it). */
    cmd_policy_set_allowed(&q.policy, CMD_WRITE);
    int n = chat_approval_unblock_all(&q);
    ASSERT_EQ(n, 1);
    ASSERT_EQ((int)q.entries[0].status, (int)APPROVE_PENDING);
    ASSERT_EQ(chat_approval_needs_user(&q), 1);
    TEST_END();
}

/* --- The conversation notes a policy change injects ---------------------
 *
 * When a batch is held back, and again when the user raises the ceiling,
 * ai_chat.c appends a note to the conversation so the model does not keep
 * citing a policy block the user has already lifted. Both notes are built
 * by ai_build_policy_blocked_note() / ai_build_policy_raised_note() in
 * src/core/ai_prompt.c precisely so the wording the model receives is
 * testable here rather than asserted against a copy of it. */

int test_policy_blocked_note_names_the_commands_and_the_ceiling(void) {
    TEST_BEGIN();
    char buf[512];
    size_t n = ai_build_policy_blocked_note("  - mv config.bak config\n",
                                            buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_EQ((int)n, (int)strlen(buf));
    /* It must say they did NOT run, list them, and point at the policy --
     * not at either of the two controls the policy replaced. */
    ASSERT_TRUE(strstr(buf, "NOT executed") != NULL);
    ASSERT_TRUE(strstr(buf, "mv config.bak config") != NULL);
    ASSERT_TRUE(strstr(buf, "command policy") != NULL);
    ASSERT_NULL(strstr(buf, "Permit Write"));
    ASSERT_NULL(strstr(buf, "Auto approve"));

    /* An empty list still produces a well-formed note. */
    ASSERT_TRUE(ai_build_policy_blocked_note("", buf, sizeof(buf)) > 0);
    ASSERT_TRUE(ai_build_policy_blocked_note(NULL, buf, sizeof(buf)) > 0);

    /* A buffer that cannot hold the note reports failure rather than
     * feeding the model a truncated instruction. */
    char tiny[16];
    ASSERT_EQ((int)ai_build_policy_blocked_note("  - ls\n", tiny, sizeof(tiny)), 0);
    ASSERT_EQ((int)ai_build_policy_blocked_note("  - ls\n", NULL, 64), 0);
    ASSERT_EQ((int)ai_build_policy_blocked_note("  - ls\n", buf, 0), 0);
    TEST_END();
}

int test_policy_raised_note_names_the_new_ceiling(void) {
    TEST_BEGIN();
    char buf[512];

    /* The note has to name the stop the ceiling moved to -- "no longer
     * blocked" alone would be a lie at any ceiling below Critical. */
    ASSERT_TRUE(ai_build_policy_raised_note(CMD_WRITE, buf, sizeof(buf)) > 0);
    ASSERT_TRUE(strstr(buf, "Write") != NULL);
    ASSERT_TRUE(strstr(buf, "no longer blocked") != NULL);
    ASSERT_NULL(strstr(buf, "Permit Write"));

    ASSERT_TRUE(ai_build_policy_raised_note(CMD_UNKNOWN, buf, sizeof(buf)) > 0);
    ASSERT_TRUE(strstr(buf, "Unknown") != NULL);
    ASSERT_TRUE(ai_build_policy_raised_note(CMD_CRITICAL, buf, sizeof(buf)) > 0);
    ASSERT_TRUE(strstr(buf, "Critical") != NULL);

    /* An out-of-range stop falls back through cmd_policy_stop_label()
     * rather than reading off the end of its table. */
    ASSERT_TRUE(ai_build_policy_raised_note(99, buf, sizeof(buf)) > 0);
    ASSERT_TRUE(strstr(buf, "Nothing") != NULL);

    char tiny[16];
    ASSERT_EQ((int)ai_build_policy_raised_note(CMD_WRITE, tiny, sizeof(tiny)), 0);
    ASSERT_EQ((int)ai_build_policy_raised_note(CMD_WRITE, NULL, 64), 0);
    ASSERT_EQ((int)ai_build_policy_raised_note(CMD_WRITE, buf, 0), 0);
    TEST_END();
}

/* The note is appended, as the last USER message, on top of the stale
 * blocked note rather than replacing it -- that ordering is what makes the
 * model act on the newer one. */
int test_policy_raised_note_lands_last_in_the_conversation(void) {
    TEST_BEGIN();
    AiConversation conv;
    ai_conv_init(&conv, "test");

    ASSERT_EQ(ai_conv_add(&conv, AI_ROLE_USER, "move config.bak to config"), 0);
    ASSERT_EQ(ai_conv_add(&conv, AI_ROLE_ASSISTANT,
        "I'll run: [EXEC]mv config.bak config[/EXEC]"), 0);

    char blocked[512];
    ASSERT_TRUE(ai_build_policy_blocked_note("  - mv config.bak config\n",
                                             blocked, sizeof(blocked)) > 0);
    ASSERT_EQ(ai_conv_add(&conv, AI_ROLE_USER, blocked), 0);
    int count_before = conv.msg_count;

    char raised[512];
    ASSERT_TRUE(ai_build_policy_raised_note(CMD_WRITE, raised, sizeof(raised)) > 0);
    ASSERT_EQ(ai_conv_add(&conv, AI_ROLE_USER, raised), 0);

    ASSERT_EQ(conv.msg_count, count_before + 1);
    int last = conv.msg_count - 1;
    ASSERT_EQ((int)conv.messages[last].role, (int)AI_ROLE_USER);
    ASSERT_TRUE(strstr(conv.messages[last].content, "no longer blocked") != NULL);

    /* The stale blocked note is still there, one above it -- the real code
     * appends rather than rewriting history. */
    ASSERT_TRUE(strstr(conv.messages[last - 1].content, "NOT executed") != NULL);
    TEST_END();
}

/* ------------------------------------------------------------------------
 * The policy gate: the decision matrix, the segment-set rule, and UNKNOWN's
 * behaviour against a moving ceiling. Replaces the five-AutoApproveLevel
 * tests that lived here before -- the stop tokens and labels those also
 * covered now belong to tests/test_cmd_policy.c and are not repeated.
 * ------------------------------------------------------------------------ */

/* The full 14-policy x 4-category decision matrix from the design doc
 * (section 8): BLOCKED above the ceiling, APPROVED at or below the
 * unattended marker, PENDING in between. */
int test_approval_decision_matrix_every_policy_by_category(void) {
    TEST_BEGIN();

    /* One representative command per CmdSafetyLevel. "frobnicate" matches
     * no rule on any platform, so it classifies CMD_UNKNOWN. Asserted
     * against cmd_classify() directly (not assumed) so this test cannot
     * silently rot if a classifier rule moves the command to a different
     * category. */
    static const char *const k_cmd_by_category[POLICY_STOP_COUNT] = {
        "ls -la",       /* CMD_READ */
        "frobnicate",   /* CMD_UNKNOWN */
        "mv a b",       /* CMD_WRITE */
        "rm -rf /tmp",  /* CMD_CRITICAL */
    };
    for (int cat = 0; cat < POLICY_STOP_COUNT; cat++) {
        CmdSafetyLevel got = cmd_classify(k_cmd_by_category[cat], CMD_PLATFORM_LINUX);
        if ((int)got != cat) {
            printf("  \"%s\" classified as %d, expected category %d\n",
                   k_cmd_by_category[cat], (int)got, cat);
            _tf_local_fail = 1;
        }
    }

    for (int allowed = 0; allowed < POLICY_STOP_COUNT; allowed++) {
        for (int unattended = POLICY_NONE; unattended <= allowed; unattended++) {
            for (int cat = 0; cat < POLICY_STOP_COUNT; cat++) {
                ApprovalQueue q;
                chat_approval_init(&q);
                q.policy.allowed = allowed;
                q.policy.unattended = unattended;

                chat_approval_add(&q, k_cmd_by_category[cat], CMD_PLATFORM_LINUX);

                ApprovalStatus want;
                if (cat > allowed) want = APPROVE_BLOCKED;
                else if (cat <= unattended) want = APPROVE_APPROVED;
                else want = APPROVE_PENDING;

                if (q.entries[0].status != want) {
                    printf("  policy={allowed=%d,unattended=%d} category=%d "
                           "cmd=\"%s\": expected status %d, got %d\n",
                           allowed, unattended, cat, k_cmd_by_category[cat],
                           (int)want, (int)q.entries[0].status);
                    _tf_local_fail = 1;
                }
            }
        }
    }
    TEST_END();
}

/* Was: the mask, not the maximum, gates approval under AUTO_APPROVE_SAFE_
 * WRITE. That mode has no equivalent policy (design doc section 3 -- {READ,
 * WRITE} unattended skips the UNKNOWN stop). The underlying claim survives
 * intact and is what this now tests directly: a command whose segments
 * span {CMD_UNKNOWN, CMD_WRITE} (its `safety` maximum is CMD_WRITE, its
 * `safety_mask` has both bits) must NOT run unattended just because its
 * maximum is at or below the marker -- the gate is a set test, not a
 * threshold on `safety`. */
int test_approval_mixed_pipeline_needs_every_segment_unattended(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    cmd_policy_set_allowed(&q.policy, CMD_CRITICAL);     /* nothing blocked */
    cmd_policy_set_unattended(&q.policy, CMD_UNKNOWN);   /* covers UNKNOWN, not WRITE */

    chat_approval_add(&q, "frobnicate | tee /etc/f", CMD_PLATFORM_LINUX);
    ASSERT_EQ((int)q.entries[0].safety, (int)CMD_WRITE);
    ASSERT_TRUE((q.entries[0].safety_mask & CMD_MASK_OF(CMD_UNKNOWN)) != 0);
    ASSERT_TRUE((q.entries[0].safety_mask & CMD_MASK_OF(CMD_WRITE)) != 0);
    ASSERT_EQ((int)q.entries[0].status, (int)APPROVE_PENDING);

    /* The same pipeline DOES run unattended once the marker covers the
     * whole set, at CMD_WRITE. */
    chat_approval_reset(&q);
    cmd_policy_set_unattended(&q.policy, CMD_WRITE);
    chat_approval_add(&q, "frobnicate | tee /etc/f", CMD_PLATFORM_LINUX);
    ASSERT_EQ((int)q.entries[0].status, (int)APPROVE_APPROVED);
    TEST_END();
}

int test_approval_unknown_blocked_at_a_read_ceiling(void) {
    TEST_BEGIN();
    /* "permit_write off" is now simply the default policy: allowed = Read.
     * UNKNOWN is gated like WRITE/CRITICAL: category (1) > ceiling (0). */
    ApprovalQueue q;
    chat_approval_init(&q);
    int idx = chat_approval_add(&q, "frobnicate", CMD_PLATFORM_LINUX);
    ASSERT_EQ(idx, 0);
    ASSERT_EQ((int)q.entries[0].safety, (int)CMD_UNKNOWN);
    ASSERT_EQ((int)q.entries[0].status, (int)APPROVE_BLOCKED);
    TEST_END();
}

int test_approval_unknown_unblocks_and_reblocks_with_the_ceiling(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    chat_approval_add(&q, "frobnicate", CMD_PLATFORM_LINUX);
    ASSERT_EQ((int)q.entries[0].status, (int)APPROVE_BLOCKED);

    /* Ceiling raised to Unknown: unblock_all() moves it to PENDING. */
    cmd_policy_set_allowed(&q.policy, CMD_UNKNOWN);
    int n = chat_approval_unblock_all(&q);
    ASSERT_EQ(n, 1);
    ASSERT_EQ((int)q.entries[0].status, (int)APPROVE_PENDING);

    /* Ceiling lowered back to Read: block_disallowed() re-blocks it,
     * exactly like a WRITE or CRITICAL entry would (category > ceiling). */
    cmd_policy_set_allowed(&q.policy, CMD_READ);
    n = chat_approval_block_disallowed(&q);
    ASSERT_EQ(n, 1);
    ASSERT_EQ((int)q.entries[0].status, (int)APPROVE_BLOCKED);
    TEST_END();
}

/* Was: auto_approve_mask() per AutoApproveLevel. That function is gone;
 * its replacement is cmd_policy_unattended_mask(), already exhaustively
 * covered by test_policy_unattended_mask_is_always_a_prefix_set() in
 * tests/test_cmd_policy.c. This pins only the old-mode -> new-stop
 * migration mapping from the design doc's table (section 2): each
 * reachable old mode's mask equals the mask of the unattended stop it maps
 * to. AUTO_APPROVE_SAFE_WRITE has no equivalent (the documented loss) and
 * is not exercised here -- see the mixed-pipeline test above. */
