/* tests/test_chat_approval.c */
#include "test_framework.h"
#include "chat_approval.h"
#include <string.h>

int test_approval_init(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    ASSERT_EQ(q.count, 0);
    ASSERT_EQ(q.auto_approve, 0);
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
    /* Write commands with permit_write=1 should auto-approve */
    chat_approval_add(&q, "mv a b", CMD_PLATFORM_LINUX, 1);
    ASSERT_EQ((int)q.entries[1].status, (int)APPROVE_APPROVED);
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
