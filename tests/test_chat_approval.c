/* tests/test_chat_approval.c */
#include "test_framework.h"
#include "chat_approval.h"
#include "ai_prompt.h"
#include <string.h>

int test_approval_init(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    ASSERT_EQ(q.count, 0);
    ASSERT_EQ(q.auto_approve, 0);
    ASSERT_EQ(q.auto_approve_level, (int)AUTO_APPROVE_SAFE);
    TEST_END();
}

int test_approval_add_safe(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    int idx = chat_approval_add(&q, "ls -la", CMD_PLATFORM_LINUX, 0);
    ASSERT_EQ(idx, 0);
    ASSERT_EQ(q.count, 1);
    ASSERT_EQ((int)q.entries[0].safety, (int)CMD_SAFE);
    ASSERT_EQ((int)q.entries[0].status, (int)APPROVE_PENDING);
    TEST_END();
}

int test_approval_add_blocked(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    int idx = chat_approval_add(&q, "mv a b", CMD_PLATFORM_LINUX, 0);
    ASSERT_EQ(idx, 0);
    ASSERT_EQ((int)q.entries[0].status, (int)APPROVE_BLOCKED);
    TEST_END();
}

int test_approval_add_write_permitted(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    int idx = chat_approval_add(&q, "mv a b", CMD_PLATFORM_LINUX, 1);
    ASSERT_EQ(idx, 0);
    ASSERT_EQ((int)q.entries[0].status, (int)APPROVE_PENDING);
    TEST_END();
}

int test_approval_approve(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    chat_approval_add(&q, "ls", CMD_PLATFORM_LINUX, 1);
    ASSERT_EQ(chat_approval_approve(&q, 0), 0);
    ASSERT_EQ((int)q.entries[0].status, (int)APPROVE_APPROVED);
    TEST_END();
}

int test_approval_deny(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    chat_approval_add(&q, "rm -rf /tmp", CMD_PLATFORM_LINUX, 1);
    ASSERT_EQ(chat_approval_deny(&q, 0), 0);
    ASSERT_EQ((int)q.entries[0].status, (int)APPROVE_DENIED);
    TEST_END();
}

int test_approval_approve_all(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    chat_approval_add(&q, "ls", CMD_PLATFORM_LINUX, 1);
    chat_approval_add(&q, "cat /etc/hosts", CMD_PLATFORM_LINUX, 1);
    chat_approval_add(&q, "cp a b", CMD_PLATFORM_LINUX, 1);
    int n = chat_approval_approve_all(&q);
    ASSERT_EQ(n, 3);
    for (int i = 0; i < 3; i++)
        ASSERT_EQ((int)q.entries[i].status, (int)APPROVE_APPROVED);
    TEST_END();
}

int test_approval_auto_approve_flow(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    int r = chat_approval_auto_approve_click(&q, 10.0f, 3.0f);
    ASSERT_EQ(r, 0);
    ASSERT_EQ(q.auto_approve_confirming, 1);
    ASSERT_EQ(q.auto_approve, 0);
    r = chat_approval_auto_approve_click(&q, 11.5f, 3.0f);
    ASSERT_EQ(r, 1);
    ASSERT_EQ(q.auto_approve, 1);
    TEST_END();
}

int test_approval_auto_approve_timeout(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    chat_approval_auto_approve_click(&q, 10.0f, 3.0f);
    int r = chat_approval_auto_approve_click(&q, 14.0f, 3.0f);
    ASSERT_EQ(r, -1);
    ASSERT_EQ(q.auto_approve, 0);
    ASSERT_EQ(q.auto_approve_confirming, 0);
    TEST_END();
}

int test_approval_auto_approve_revoke(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    chat_approval_auto_approve_click(&q, 10.0f, 3.0f);
    chat_approval_auto_approve_click(&q, 11.0f, 3.0f);
    ASSERT_EQ(q.auto_approve, 1);
    chat_approval_revoke_auto(&q);
    ASSERT_EQ(q.auto_approve, 0);
    TEST_END();
}

int test_approval_auto_approve_adds(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    chat_approval_auto_approve_click(&q, 10.0f, 3.0f);
    chat_approval_auto_approve_click(&q, 11.0f, 3.0f);
    chat_approval_add(&q, "ls", CMD_PLATFORM_LINUX, 1);
    ASSERT_EQ((int)q.entries[0].status, (int)APPROVE_APPROVED);
    TEST_END();
}

int test_approval_all_decided(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    chat_approval_add(&q, "ls", CMD_PLATFORM_LINUX, 1);
    chat_approval_add(&q, "cat f", CMD_PLATFORM_LINUX, 1);
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
    chat_approval_add(&q, "ls", CMD_PLATFORM_LINUX, 1);
    chat_approval_add(&q, "cat f", CMD_PLATFORM_LINUX, 1);
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
    chat_approval_add(&q, "ls", CMD_PLATFORM_LINUX, 1);
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
    int idx = chat_approval_add(&q, "", CMD_PLATFORM_LINUX, 1);
    ASSERT_EQ(idx, -1);
    TEST_END();
}

int test_approval_whitespace_command(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    int idx = chat_approval_add(&q, "   ", CMD_PLATFORM_LINUX, 1);
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
    int idx = chat_approval_add(&q, "echo ok\nrm -rf ~", CMD_PLATFORM_LINUX, 1);
    ASSERT_EQ(idx, -1);
    ASSERT_EQ(q.count, 0);
    TEST_END();
}

int test_approval_queue_full(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    for (int i = 0; i < APPROVAL_MAX_CMDS; i++)
        ASSERT_TRUE(chat_approval_add(&q, "ls", CMD_PLATFORM_LINUX, 1) >= 0);
    ASSERT_EQ(chat_approval_add(&q, "ls", CMD_PLATFORM_LINUX, 1), -1);
    TEST_END();
}

int test_approval_auto_approve_direct_toggle(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    ASSERT_EQ(q.auto_approve, 0);
    /* Simulate the Auto Approve button: direct toggle on */
    q.auto_approve = 1;
    ASSERT_EQ(q.auto_approve, 1);
    /* Commands added while active should auto-approve */
    chat_approval_add(&q, "ls", CMD_PLATFORM_LINUX, 1);
    ASSERT_EQ((int)q.entries[0].status, (int)APPROVE_APPROVED);
    /* Toggle off */
    q.auto_approve = 0;
    ASSERT_EQ(q.auto_approve, 0);
    /* Commands added after toggle off should be pending */
    chat_approval_add(&q, "cat /etc/hosts", CMD_PLATFORM_LINUX, 1);
    ASSERT_EQ((int)q.entries[1].status, (int)APPROVE_PENDING);
    TEST_END();
}

int test_approval_auto_approve_direct_toggle_with_write(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    q.auto_approve = 1;
    /* Write commands with permit_write=0 should still be blocked */
    chat_approval_add(&q, "mv a b", CMD_PLATFORM_LINUX, 0);
    ASSERT_EQ((int)q.entries[0].status, (int)APPROVE_BLOCKED);
    /* Write commands with permit_write=1: with auto_approve_level at the
     * default AUTO_APPROVE_SAFE, Auto Approve does not cover write/critical
     * commands — they stay PENDING for the user to decide. */
    chat_approval_add(&q, "mv a b", CMD_PLATFORM_LINUX, 1);
    ASSERT_EQ((int)q.entries[1].status, (int)APPROVE_PENDING);
    /* With auto_approve_level raised to AUTO_APPROVE_SAFE_WRITE, write
     * commands auto-approve too. */
    q.auto_approve_level = AUTO_APPROVE_SAFE_WRITE;
    chat_approval_add(&q, "mv a b", CMD_PLATFORM_LINUX, 1);
    ASSERT_EQ((int)q.entries[2].status, (int)APPROVE_APPROVED);
    TEST_END();
}

int test_approval_reset(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    chat_approval_add(&q, "ls", CMD_PLATFORM_LINUX, 1);
    chat_approval_add(&q, "cat f", CMD_PLATFORM_LINUX, 1);
    chat_approval_reset(&q);
    ASSERT_EQ(q.count, 0);
    TEST_END();
}

int test_approval_block_pending_writes(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    /* Safe command stays pending */
    chat_approval_add(&q, "ls -la", CMD_PLATFORM_LINUX, 1);
    /* Write command stays pending (permit_write=1) */
    chat_approval_add(&q, "mv a b", CMD_PLATFORM_LINUX, 1);
    /* Critical command stays pending (permit_write=1) */
    chat_approval_add(&q, "rm -rf /tmp", CMD_PLATFORM_LINUX, 1);
    ASSERT_EQ(q.count, 3);
    ASSERT_EQ((int)q.entries[0].status, (int)APPROVE_PENDING);
    ASSERT_EQ((int)q.entries[1].status, (int)APPROVE_PENDING);
    ASSERT_EQ((int)q.entries[2].status, (int)APPROVE_PENDING);

    int n = chat_approval_block_pending_writes(&q);
    /* Only write+critical get blocked, safe stays pending */
    ASSERT_EQ(n, 2);
    ASSERT_EQ((int)q.entries[0].status, (int)APPROVE_PENDING);
    ASSERT_EQ((int)q.entries[1].status, (int)APPROVE_BLOCKED);
    ASSERT_EQ((int)q.entries[2].status, (int)APPROVE_BLOCKED);
    TEST_END();
}

int test_approval_auto_approve_persists_across_reset(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    /* Turn on auto-approve */
    q.auto_approve = 1;

    /* First batch: add commands, all auto-approved */
    chat_approval_add(&q, "ls", CMD_PLATFORM_LINUX, 1);
    chat_approval_add(&q, "cat /etc/hosts", CMD_PLATFORM_LINUX, 1);
    ASSERT_EQ((int)q.entries[0].status, (int)APPROVE_APPROVED);
    ASSERT_EQ((int)q.entries[1].status, (int)APPROVE_APPROVED);
    ASSERT_EQ(chat_approval_all_decided(&q), 1);

    /* Simulate next batch arrival: reset then add new commands */
    chat_approval_reset(&q);
    ASSERT_EQ(q.auto_approve, 1);  /* auto_approve preserved */
    chat_approval_add(&q, "pwd", CMD_PLATFORM_LINUX, 1);
    chat_approval_add(&q, "whoami", CMD_PLATFORM_LINUX, 1);

    /* Second batch should also be auto-approved */
    ASSERT_EQ((int)q.entries[0].status, (int)APPROVE_APPROVED);
    ASSERT_EQ((int)q.entries[1].status, (int)APPROVE_APPROVED);
    ASSERT_EQ(chat_approval_all_decided(&q), 1);
    TEST_END();
}

int test_approval_auto_approve_blocked_not_all_decided(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    q.auto_approve = 1;

    /* Safe command: auto-approved. Write command with permit_write=0: blocked */
    chat_approval_add(&q, "ls", CMD_PLATFORM_LINUX, 1);
    chat_approval_add(&q, "mv a b", CMD_PLATFORM_LINUX, 0);

    ASSERT_EQ((int)q.entries[0].status, (int)APPROVE_APPROVED);
    ASSERT_EQ((int)q.entries[1].status, (int)APPROVE_BLOCKED);
    /* BLOCKED is not PENDING, so all_decided should return 1 */
    ASSERT_EQ(chat_approval_all_decided(&q), 1);
    TEST_END();
}

int test_approval_block_pending_writes_skips_decided(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    chat_approval_add(&q, "mv a b", CMD_PLATFORM_LINUX, 1);
    chat_approval_add(&q, "cp x y", CMD_PLATFORM_LINUX, 1);
    /* Approve first command — should not be re-blocked */
    chat_approval_approve(&q, 0);
    int n = chat_approval_block_pending_writes(&q);
    ASSERT_EQ(n, 1);
    ASSERT_EQ((int)q.entries[0].status, (int)APPROVE_APPROVED);
    ASSERT_EQ((int)q.entries[1].status, (int)APPROVE_BLOCKED);
    TEST_END();
}

/* --- Auto Approve levels: SAFE / WRITE / ALL --- */

int test_approval_level_safe_always_approves_safe(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    q.auto_approve = 1;
    q.auto_approve_level = AUTO_APPROVE_SAFE;
    chat_approval_add(&q, "ls -la", CMD_PLATFORM_LINUX, 1);
    ASSERT_EQ((int)q.entries[0].status, (int)APPROVE_APPROVED);

    chat_approval_reset(&q);
    q.auto_approve_level = AUTO_APPROVE_SAFE_WRITE;
    chat_approval_add(&q, "ls -la", CMD_PLATFORM_LINUX, 1);
    ASSERT_EQ((int)q.entries[0].status, (int)APPROVE_APPROVED);

    chat_approval_reset(&q);
    q.auto_approve_level = AUTO_APPROVE_ALL;
    chat_approval_add(&q, "ls -la", CMD_PLATFORM_LINUX, 1);
    ASSERT_EQ((int)q.entries[0].status, (int)APPROVE_APPROVED);
    TEST_END();
}

int test_approval_level_safe_write_pending(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    q.auto_approve = 1;
    q.auto_approve_level = AUTO_APPROVE_SAFE;
    /* Write command, permit_write on, level SAFE -> PENDING (not covered) */
    chat_approval_add(&q, "mv a b", CMD_PLATFORM_LINUX, 1);
    ASSERT_EQ((int)q.entries[0].status, (int)APPROVE_PENDING);
    TEST_END();
}

int test_approval_level_safe_write_permit_off_blocked(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    q.auto_approve = 1;
    q.auto_approve_level = AUTO_APPROVE_SAFE;
    /* permit_write off -> BLOCKED regardless of level */
    chat_approval_add(&q, "mv a b", CMD_PLATFORM_LINUX, 0);
    ASSERT_EQ((int)q.entries[0].status, (int)APPROVE_BLOCKED);
    TEST_END();
}

int test_approval_level_write_approves_write(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    q.auto_approve = 1;
    q.auto_approve_level = AUTO_APPROVE_SAFE_WRITE;
    /* Write command, permit_write on, level SAFE_WRITE -> APPROVED */
    chat_approval_add(&q, "mv a b", CMD_PLATFORM_LINUX, 1);
    ASSERT_EQ((int)q.entries[0].status, (int)APPROVE_APPROVED);
    TEST_END();
}

int test_approval_level_write_permit_off_blocked(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    q.auto_approve = 1;
    q.auto_approve_level = AUTO_APPROVE_SAFE_WRITE;
    /* permit_write off -> BLOCKED regardless of level */
    chat_approval_add(&q, "mv a b", CMD_PLATFORM_LINUX, 0);
    ASSERT_EQ((int)q.entries[0].status, (int)APPROVE_BLOCKED);
    TEST_END();
}

int test_approval_level_write_critical_pending(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    q.auto_approve = 1;
    q.auto_approve_level = AUTO_APPROVE_SAFE_WRITE;
    /* Critical command, permit_write on, level SAFE_WRITE (not ALL) -> PENDING */
    chat_approval_add(&q, "rm -rf /tmp", CMD_PLATFORM_LINUX, 1);
    ASSERT_EQ((int)q.entries[0].status, (int)APPROVE_PENDING);
    TEST_END();
}

int test_approval_level_all_approves_write_and_critical(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    q.auto_approve = 1;
    q.auto_approve_level = AUTO_APPROVE_ALL;
    chat_approval_add(&q, "mv a b", CMD_PLATFORM_LINUX, 1);
    ASSERT_EQ((int)q.entries[0].status, (int)APPROVE_APPROVED);
    chat_approval_add(&q, "rm -rf /tmp", CMD_PLATFORM_LINUX, 1);
    ASSERT_EQ((int)q.entries[1].status, (int)APPROVE_APPROVED);
    TEST_END();
}

int test_approval_level_all_permit_write_off_still_blocked(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    q.auto_approve = 1;
    q.auto_approve_level = AUTO_APPROVE_ALL;
    /* permit_write off -> BLOCKED regardless of auto_approve_level */
    chat_approval_add(&q, "rm -rf /tmp", CMD_PLATFORM_LINUX, 0);
    ASSERT_EQ((int)q.entries[0].status, (int)APPROVE_BLOCKED);
    TEST_END();
}

int test_approval_level_auto_approve_off_all_pending(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    /* auto_approve off entirely: level is irrelevant, nothing auto-approves */
    q.auto_approve = 0;
    q.auto_approve_level = AUTO_APPROVE_ALL;
    chat_approval_add(&q, "ls -la", CMD_PLATFORM_LINUX, 1);
    ASSERT_EQ((int)q.entries[0].status, (int)APPROVE_PENDING);
    chat_approval_add(&q, "mv a b", CMD_PLATFORM_LINUX, 1);
    ASSERT_EQ((int)q.entries[1].status, (int)APPROVE_PENDING);
    TEST_END();
}

int test_approval_reset_preserves_level(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    q.auto_approve = 1;
    q.auto_approve_level = AUTO_APPROVE_SAFE_WRITE;
    chat_approval_add(&q, "ls", CMD_PLATFORM_LINUX, 1);
    chat_approval_reset(&q);
    ASSERT_EQ(q.auto_approve, 1);
    ASSERT_EQ((int)q.auto_approve_level, (int)AUTO_APPROVE_SAFE_WRITE);
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
    chat_approval_add(&q, "ls", CMD_PLATFORM_LINUX, 1);
    chat_approval_add(&q, "cat f", CMD_PLATFORM_LINUX, 1);
    ASSERT_EQ(chat_approval_needs_user(&q), 1);
    TEST_END();
}

int test_needs_user_pending_and_blocked(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    chat_approval_add(&q, "ls", CMD_PLATFORM_LINUX, 1);       /* pending */
    chat_approval_add(&q, "mv a b", CMD_PLATFORM_LINUX, 0);   /* blocked */
    ASSERT_EQ(chat_approval_needs_user(&q), 1);
    TEST_END();
}

int test_needs_user_blocked_only(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    /* Every command is a write and permit_write is off: nothing pending,
     * nothing approved, but nothing will run either -- the card must
     * stay up so the user can switch to Read + write and run, or deny. */
    chat_approval_add(&q, "mv a b", CMD_PLATFORM_LINUX, 0);
    chat_approval_add(&q, "rm f", CMD_PLATFORM_LINUX, 0);
    ASSERT_EQ(chat_approval_needs_user(&q), 1);
    TEST_END();
}

int test_needs_user_blocked_and_approved_auto_approve(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    q.auto_approve = 1;
    /* Safe command auto-approves; write command with permit_write=0
     * is blocked. Since something WILL run, the card doesn't need to
     * stay up for the blocked one -- it's just noise on the side. */
    chat_approval_add(&q, "ls", CMD_PLATFORM_LINUX, 1);
    chat_approval_add(&q, "mv a b", CMD_PLATFORM_LINUX, 0);
    ASSERT_EQ((int)q.entries[0].status, (int)APPROVE_APPROVED);
    ASSERT_EQ((int)q.entries[1].status, (int)APPROVE_BLOCKED);
    ASSERT_EQ(chat_approval_needs_user(&q), 0);
    TEST_END();
}

int test_needs_user_approved_only(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    chat_approval_add(&q, "ls", CMD_PLATFORM_LINUX, 1);
    chat_approval_approve(&q, 0);
    ASSERT_EQ(chat_approval_needs_user(&q), 0);
    TEST_END();
}

int test_needs_user_denied_only(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    chat_approval_add(&q, "ls", CMD_PLATFORM_LINUX, 1);
    chat_approval_deny(&q, 0);
    ASSERT_EQ(chat_approval_needs_user(&q), 0);
    TEST_END();
}

int test_needs_user_after_unblock_all(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    /* Blocked-only batch needs the user... */
    chat_approval_add(&q, "mv a b", CMD_PLATFORM_LINUX, 0);
    ASSERT_EQ(chat_approval_needs_user(&q), 1);
    /* ...and after Permit Write turns on and unblocks it, it's now
     * PENDING, which still needs the user (to actually run it). */
    int n = chat_approval_unblock_all(&q);
    ASSERT_EQ(n, 1);
    ASSERT_EQ((int)q.entries[0].status, (int)APPROVE_PENDING);
    ASSERT_EQ(chat_approval_needs_user(&q), 1);
    TEST_END();
}

/* --- Permit-write toggle conversation injection tests --- */

/* When permit_write is toggled ON and there is an active conversation,
 * a corrective "write is now allowed" note must be injected as the last
 * user message so the AI drops its stale "blocked" context. */

int test_permit_toggle_corrective_msg_appended(void) {
    TEST_BEGIN();
    AiConversation conv;
    ai_conv_init(&conv, "test");

    /* Simulate: user asked something, AI replied, then blocked note was
     * injected because permit_write was off at the time. */
    ASSERT_EQ(ai_conv_add(&conv, AI_ROLE_USER, "move config.bak to config"), 0);
    ASSERT_EQ(ai_conv_add(&conv, AI_ROLE_ASSISTANT,
        "I'll run: [EXEC]mv config.bak config[/EXEC]"), 0);
    ASSERT_EQ(ai_conv_add(&conv, AI_ROLE_USER,
        "NOTE: The following commands were BLOCKED by the user's read-only "
        "security policy and were NOT executed:\n  - mv config.bak config\n"
        "Do NOT claim these commands were executed. If the user needs these "
        "actions, tell them to enable 'Permit Write' and try again."), 0);

    int count_before = conv.msg_count;

    /* Simulate the IDC_CHAT_PERMIT toggle-ON path: inject corrective note */
    const char *corrective =
        "NOTE: The user has enabled 'Permit Write'. "
        "Write commands are now allowed and will no longer be blocked. "
        "Do not reference any previous security policy blocks.";
    ASSERT_EQ(ai_conv_add(&conv, AI_ROLE_USER, corrective), 0);

    /* One message was added */
    ASSERT_EQ(conv.msg_count, count_before + 1);

    /* Last message is a user message with the corrective text */
    int last = conv.msg_count - 1;
    ASSERT_EQ((int)conv.messages[last].role, (int)AI_ROLE_USER);
    ASSERT_TRUE(strstr(conv.messages[last].content, "Permit Write") != NULL);
    ASSERT_TRUE(strstr(conv.messages[last].content,
        "no longer be blocked") != NULL);
    TEST_END();
}

int test_permit_toggle_no_inject_when_conv_empty(void) {
    TEST_BEGIN();
    AiConversation conv;
    ai_conv_init(&conv, "test");

    /* Empty conversation — the toggle handler should NOT inject
     * (guarded by msg_count > 0 in the real code). Nothing to test
     * in the conversation API itself; just verify msg_count stays 0
     * when we add nothing. */
    ASSERT_EQ(conv.msg_count, 0);
    TEST_END();
}

int test_permit_toggle_corrective_msg_is_last(void) {
    TEST_BEGIN();
    AiConversation conv;
    ai_conv_init(&conv, "test");

    /* Several exchanges before the blocked note */
    ai_conv_add(&conv, AI_ROLE_USER, "hello");
    ai_conv_add(&conv, AI_ROLE_ASSISTANT, "hi there");
    ai_conv_add(&conv, AI_ROLE_USER, "run mv a b");
    ai_conv_add(&conv, AI_ROLE_ASSISTANT, "[EXEC]mv a b[/EXEC]");
    ai_conv_add(&conv, AI_ROLE_USER,
        "NOTE: The following commands were BLOCKED by the user's read-only "
        "security policy and were NOT executed:\n  - mv a b");

    /* Inject corrective note */
    ai_conv_add(&conv, AI_ROLE_USER,
        "NOTE: The user has enabled 'Permit Write'. "
        "Write commands are now allowed and will no longer be blocked. "
        "Do not reference any previous security policy blocks.");

    /* Corrective note must be the last message in the conversation */
    int last = conv.msg_count - 1;
    ASSERT_EQ((int)conv.messages[last].role, (int)AI_ROLE_USER);
    ASSERT_TRUE(strstr(conv.messages[last].content,
        "enabled 'Permit Write'") != NULL);

    /* The message before it is the stale blocked note — not removed */
    int prev = conv.msg_count - 2;
    ASSERT_EQ((int)conv.messages[prev].role, (int)AI_ROLE_USER);
    ASSERT_TRUE(strstr(conv.messages[prev].content, "BLOCKED") != NULL);
    TEST_END();
}

/* --- The five auto-approve modes, tested against all four safety
 * categories (2026-09-11-unknown-safety-category-design.md §8): 20 cases. --- */

int test_approval_mask_five_modes_by_four_categories(void) {
    TEST_BEGIN();

    /* One representative command per CmdSafetyLevel. "frobnicate" matches
     * no rule on any platform, so it classifies CMD_UNKNOWN. */
    static const char *const k_cmd_by_category[4] = {
        "ls -la",       /* CMD_SAFE */
        "frobnicate",   /* CMD_UNKNOWN */
        "mv a b",       /* CMD_WRITE */
        "rm -rf /tmp",  /* CMD_CRITICAL */
    };
    static const AutoApproveLevel k_levels[5] = {
        AUTO_APPROVE_SAFE, AUTO_APPROVE_SAFE_UNKNOWN, AUTO_APPROVE_SAFE_WRITE,
        AUTO_APPROVE_SAFE_UNKNOWN_WRITE, AUTO_APPROVE_ALL
    };
    /* expect[level][category] = 1 if that category auto-approves at that level. */
    static const int k_expect[5][4] = {
        /*              SAFE  UNKNOWN  WRITE  CRITICAL */
        /* SAFE               */ { 1, 0, 0, 0 },
        /* SAFE_UNKNOWN       */ { 1, 1, 0, 0 },
        /* SAFE_WRITE         */ { 1, 0, 1, 0 },
        /* SAFE_UNKNOWN_WRITE */ { 1, 1, 1, 0 },
        /* ALL                */ { 1, 1, 1, 1 },
    };

    for (int lvl = 0; lvl < 5; lvl++) {
        for (int cat = 0; cat < 4; cat++) {
            ApprovalQueue q;
            chat_approval_init(&q);
            q.auto_approve = 1;
            q.auto_approve_level = (int)k_levels[lvl];
            chat_approval_add(&q, k_cmd_by_category[cat], CMD_PLATFORM_LINUX, 1);
            int want_approved = k_expect[lvl][cat];
            int got_approved = (q.entries[0].status == APPROVE_APPROVED);
            if (got_approved != want_approved) {
                printf("  level=%d category=%d cmd=\"%s\": expected approved=%d, got status=%d\n",
                       lvl, cat, k_cmd_by_category[cat], want_approved,
                       (int)q.entries[0].status);
                _tf_local_fail = 1;
            }
        }
    }
    TEST_END();
}

/* The mask, not the maximum, gates approval: a {UNKNOWN, WRITE} pipeline
 * must not auto-approve under SAFE_WRITE even though its `safety` maximum
 * (what the chip shows) is CMD_WRITE. */
int test_approval_mixed_pipeline_unknown_and_write_not_approved_under_safe_write(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    q.auto_approve = 1;
    q.auto_approve_level = AUTO_APPROVE_SAFE_WRITE;
    chat_approval_add(&q, "frobnicate | tee /etc/f", CMD_PLATFORM_LINUX, 1);
    ASSERT_EQ((int)q.entries[0].safety, (int)CMD_WRITE);
    ASSERT_TRUE((q.entries[0].safety_mask & CMD_MASK_OF(CMD_UNKNOWN)) != 0);
    ASSERT_TRUE((q.entries[0].safety_mask & CMD_MASK_OF(CMD_WRITE)) != 0);
    ASSERT_EQ((int)q.entries[0].status, (int)APPROVE_PENDING);

    /* The same pipeline DOES auto-approve once UNKNOWN is also permitted. */
    chat_approval_reset(&q);
    q.auto_approve_level = AUTO_APPROVE_SAFE_UNKNOWN_WRITE;
    chat_approval_add(&q, "frobnicate | tee /etc/f", CMD_PLATFORM_LINUX, 1);
    ASSERT_EQ((int)q.entries[0].status, (int)APPROVE_APPROVED);
    TEST_END();
}

int test_approval_unknown_blocked_when_permit_write_off(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    /* UNKNOWN is gated like WRITE/CRITICAL: safety (1) > CMD_SAFE (0). */
    int idx = chat_approval_add(&q, "frobnicate", CMD_PLATFORM_LINUX, 0);
    ASSERT_EQ(idx, 0);
    ASSERT_EQ((int)q.entries[0].safety, (int)CMD_UNKNOWN);
    ASSERT_EQ((int)q.entries[0].status, (int)APPROVE_BLOCKED);
    TEST_END();
}

int test_approval_unknown_unblock_and_reblock(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    chat_approval_add(&q, "frobnicate", CMD_PLATFORM_LINUX, 0);
    ASSERT_EQ((int)q.entries[0].status, (int)APPROVE_BLOCKED);

    /* Permit Write turns on: unblock_all() moves it to PENDING. */
    int n = chat_approval_unblock_all(&q);
    ASSERT_EQ(n, 1);
    ASSERT_EQ((int)q.entries[0].status, (int)APPROVE_PENDING);

    /* Permit Write turns back off: block_pending_writes() re-blocks it,
     * exactly like a WRITE or CRITICAL entry would (safety > CMD_SAFE). */
    n = chat_approval_block_pending_writes(&q);
    ASSERT_EQ(n, 1);
    ASSERT_EQ((int)q.entries[0].status, (int)APPROVE_BLOCKED);
    TEST_END();
}

/* --- auto_approve_mask() --- */

int test_auto_approve_mask_per_level(void) {
    TEST_BEGIN();
    ASSERT_EQ(auto_approve_mask(AUTO_APPROVE_SAFE), CMD_MASK_OF(CMD_SAFE));
    ASSERT_EQ(auto_approve_mask(AUTO_APPROVE_SAFE_UNKNOWN),
              CMD_MASK_OF(CMD_SAFE) | CMD_MASK_OF(CMD_UNKNOWN));
    ASSERT_EQ(auto_approve_mask(AUTO_APPROVE_SAFE_WRITE),
              CMD_MASK_OF(CMD_SAFE) | CMD_MASK_OF(CMD_WRITE));
    ASSERT_EQ(auto_approve_mask(AUTO_APPROVE_SAFE_UNKNOWN_WRITE),
              CMD_MASK_OF(CMD_SAFE) | CMD_MASK_OF(CMD_UNKNOWN) | CMD_MASK_OF(CMD_WRITE));
    ASSERT_EQ(auto_approve_mask(AUTO_APPROVE_ALL),
              CMD_MASK_OF(CMD_SAFE) | CMD_MASK_OF(CMD_UNKNOWN) |
              CMD_MASK_OF(CMD_WRITE) | CMD_MASK_OF(CMD_CRITICAL));
    TEST_END();
}

/* --- auto_approve_mode_from_name() / _name() / _label() --- */

int test_auto_approve_mode_name_round_trip(void) {
    TEST_BEGIN();
    static const char *const k_names[6] = {
        "off", "safe", "safe+unknown", "safe+write", "safe+unknown+write", "all"
    };
    for (int i = 0; i < 6; i++) {
        ASSERT_STR_EQ(auto_approve_mode_name(i), k_names[i]);
        ASSERT_EQ(auto_approve_mode_from_name(k_names[i]), i);
    }
    TEST_END();
}

int test_auto_approve_mode_label_per_mode(void) {
    TEST_BEGIN();
    ASSERT_STR_EQ(auto_approve_mode_label(0), "Off");
    ASSERT_STR_EQ(auto_approve_mode_label(1), "Safe only");
    ASSERT_STR_EQ(auto_approve_mode_label(2), "Safe + unknown");
    ASSERT_STR_EQ(auto_approve_mode_label(3), "Safe + write");
    ASSERT_STR_EQ(auto_approve_mode_label(4), "Safe + unknown + write");
    ASSERT_STR_EQ(auto_approve_mode_label(5), "All");
    TEST_END();
}

int test_auto_approve_mode_from_name_garbage_is_off(void) {
    TEST_BEGIN();
    ASSERT_EQ(auto_approve_mode_from_name(NULL), 0);
    ASSERT_EQ(auto_approve_mode_from_name(""), 0);
    ASSERT_EQ(auto_approve_mode_from_name("not-a-real-mode"), 0);
    ASSERT_EQ(auto_approve_mode_from_name("SAFE"), 0); /* case-sensitive */
    TEST_END();
}

int test_auto_approve_mode_name_and_label_clamp_out_of_range(void) {
    TEST_BEGIN();
    ASSERT_STR_EQ(auto_approve_mode_name(-1), "off");
    ASSERT_STR_EQ(auto_approve_mode_name(6), "off");
    ASSERT_STR_EQ(auto_approve_mode_label(-1), "Off");
    ASSERT_STR_EQ(auto_approve_mode_label(6), "Off");
    TEST_END();
}
