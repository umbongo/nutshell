/* tests/test_cmd_collapse.c — Tests for command list collapse/expand logic.
 *
 * The helper functions (command_index_of, is_first_command, is_last_command,
 * count_commands) are static in chat_listview.c (Win32-only), so we
 * reimplement them here to test the collapse visibility rules. */

#include "test_framework.h"
#include "chat_msg.h"
#include <string.h>

/* ── Reimplemented helpers (must match chat_listview.c logic) ────────── */

#define BASE_CMD_VISIBLE 4

static void count_commands(const ChatMsgList *list, int *total, int *pending)
{
    *total = 0;
    *pending = 0;
    if (!list) return;
    ChatMsgItem *item = list->head;
    while (item) {
        if (item->type == CHAT_ITEM_COMMAND) {
            (*total)++;
            if (item->u.cmd.approved == -1 && !item->u.cmd.blocked)
                (*pending)++;
        }
        item = item->next;
    }
}

static int is_first_command(const ChatMsgList *list, const ChatMsgItem *target)
{
    ChatMsgItem *item = list->head;
    while (item) {
        if (item->type == CHAT_ITEM_COMMAND)
            return (item == target) ? 1 : 0;
        item = item->next;
    }
    return 0;
}

static int is_last_command(const ChatMsgList *list, const ChatMsgItem *target)
{
    const ChatMsgItem *item = target->next;
    while (item) {
        if (item->type == CHAT_ITEM_COMMAND) return 0;
        item = item->next;
    }
    return (target->type == CHAT_ITEM_COMMAND) ? 1 : 0;
}

static int command_index_of(const ChatMsgList *list, const ChatMsgItem *target)
{
    int idx = 0;
    ChatMsgItem *item = list->head;
    while (item) {
        if (item == target) return idx;
        if (item->type == CHAT_ITEM_COMMAND) idx++;
        item = item->next;
    }
    return -1;
}

/* Determine if a command should be visible given collapse state.
 * Returns 1 if visible, 0 if hidden. Matches measure_item logic. */
static int cmd_is_visible(const ChatMsgList *list, const ChatMsgItem *item,
                           int expanded)
{
    int total_cmds, pending_cmds;
    count_commands(list, &total_cmds, &pending_cmds);
    int idx = command_index_of(list, item);
    if (!expanded && total_cmds > BASE_CMD_VISIBLE && idx >= BASE_CMD_VISIBLE)
        return 0;
    return 1;
}

/* Helper to add N command items to a list. Returns the last one added. */
static ChatMsgItem *add_commands(ChatMsgList *list, int n)
{
    ChatMsgItem *last = NULL;
    for (int i = 0; i < n; i++) {
        ChatMsgItem *item = chat_msg_append(list, CHAT_ITEM_COMMAND, "");
        chat_msg_set_command(item, "ls -la", CMD_SAFE, 0);
        last = item;
    }
    return last;
}

/* ── Tests ──────────────────────────────────────────────────────────── */

int test_cmd_index_single(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *cmd = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    chat_msg_set_command(cmd, "ls", CMD_SAFE, 0);
    ASSERT_EQ(command_index_of(&list, cmd), 0);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_cmd_index_with_mixed_items(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    chat_msg_append(&list, CHAT_ITEM_AI_TEXT, "thinking...");
    ChatMsgItem *c0 = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    chat_msg_set_command(c0, "ls", CMD_SAFE, 0);
    chat_msg_append(&list, CHAT_ITEM_STATUS, "running");
    ChatMsgItem *c1 = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    chat_msg_set_command(c1, "pwd", CMD_SAFE, 0);
    ASSERT_EQ(command_index_of(&list, c0), 0);
    ASSERT_EQ(command_index_of(&list, c1), 1);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_cmd_index_not_found(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    /* A non-existent item (not in list) returns -1 */
    ChatMsgItem dummy;
    memset(&dummy, 0, sizeof(dummy));
    dummy.type = CHAT_ITEM_COMMAND;
    chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    ASSERT_EQ(command_index_of(&list, &dummy), -1);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_cmd_first_last_single(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *cmd = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    chat_msg_set_command(cmd, "ls", CMD_SAFE, 0);
    ASSERT_EQ(is_first_command(&list, cmd), 1);
    ASSERT_EQ(is_last_command(&list, cmd), 1);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_cmd_first_last_multiple(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *c0 = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    chat_msg_set_command(c0, "ls", CMD_SAFE, 0);
    ChatMsgItem *c1 = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    chat_msg_set_command(c1, "pwd", CMD_SAFE, 0);
    ChatMsgItem *c2 = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    chat_msg_set_command(c2, "cat", CMD_SAFE, 0);
    ASSERT_EQ(is_first_command(&list, c0), 1);
    ASSERT_EQ(is_first_command(&list, c1), 0);
    ASSERT_EQ(is_last_command(&list, c0), 0);
    ASSERT_EQ(is_last_command(&list, c2), 1);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_collapse_few_commands_all_visible(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    /* With 3 commands (< BASE_CMD_VISIBLE), all should be visible even collapsed */
    ChatMsgItem *cmds[3];
    for (int i = 0; i < 3; i++) {
        cmds[i] = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
        chat_msg_set_command(cmds[i], "ls", CMD_SAFE, 0);
    }
    for (int i = 0; i < 3; i++)
        ASSERT_EQ(cmd_is_visible(&list, cmds[i], 0), 1);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_collapse_exact_threshold_all_visible(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    /* With exactly 4 commands (== BASE_CMD_VISIBLE), all visible */
    ChatMsgItem *cmds[4];
    for (int i = 0; i < 4; i++) {
        cmds[i] = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
        chat_msg_set_command(cmds[i], "ls", CMD_SAFE, 0);
    }
    for (int i = 0; i < 4; i++)
        ASSERT_EQ(cmd_is_visible(&list, cmds[i], 0), 1);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_collapse_hides_beyond_threshold(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    /* With 6 commands (> BASE_CMD_VISIBLE), collapsed hides 5th and 6th */
    ChatMsgItem *cmds[6];
    for (int i = 0; i < 6; i++) {
        cmds[i] = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
        chat_msg_set_command(cmds[i], "ls", CMD_SAFE, 0);
    }
    /* First 4 visible when collapsed */
    for (int i = 0; i < 4; i++)
        ASSERT_EQ(cmd_is_visible(&list, cmds[i], 0), 1);
    /* 5th and 6th hidden when collapsed */
    ASSERT_EQ(cmd_is_visible(&list, cmds[4], 0), 0);
    ASSERT_EQ(cmd_is_visible(&list, cmds[5], 0), 0);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_expand_shows_all(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    /* With 8 commands, expanded shows all */
    ChatMsgItem *cmds[8];
    for (int i = 0; i < 8; i++) {
        cmds[i] = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
        chat_msg_set_command(cmds[i], "ls", CMD_SAFE, 0);
    }
    for (int i = 0; i < 8; i++)
        ASSERT_EQ(cmd_is_visible(&list, cmds[i], 1), 1);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_collapse_max_commands(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    /* With 16 commands (APPROVAL_MAX_CMDS), only first 4 visible */
    ChatMsgItem *cmds[16];
    for (int i = 0; i < 16; i++) {
        cmds[i] = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
        chat_msg_set_command(cmds[i], "ls", CMD_SAFE, 0);
    }
    int visible = 0, hidden = 0;
    for (int i = 0; i < 16; i++) {
        if (cmd_is_visible(&list, cmds[i], 0))
            visible++;
        else
            hidden++;
    }
    ASSERT_EQ(visible, 4);
    ASSERT_EQ(hidden, 12);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_count_commands_with_pending(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *c0 = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    chat_msg_set_command(c0, "ls", CMD_SAFE, 0);  /* pending */
    ChatMsgItem *c1 = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    chat_msg_set_command(c1, "rm x", CMD_WRITE, 1);  /* blocked */
    ChatMsgItem *c2 = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    chat_msg_set_command(c2, "pwd", CMD_SAFE, 0);  /* pending */
    c0->u.cmd.approved = 1;  /* approve first */
    int total, pending;
    count_commands(&list, &total, &pending);
    ASSERT_EQ(total, 3);
    ASSERT_EQ(pending, 1);  /* only c2 is pending (c0 approved, c1 blocked) */
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_allow_all_button_visibility(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    /* Action buttons row should show on last command when pending > 0 */
    ChatMsgItem *c0 = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    chat_msg_set_command(c0, "ls", CMD_SAFE, 0);
    ChatMsgItem *c1 = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    chat_msg_set_command(c1, "pwd", CMD_SAFE, 0);
    int total, pending;
    count_commands(&list, &total, &pending);
    int show_actions = (pending > 0 && is_last_command(&list, c1));
    ASSERT_EQ(show_actions, 1);

    /* After approving all, action buttons should hide */
    c0->u.cmd.approved = 1;
    c1->u.cmd.approved = 1;
    count_commands(&list, &total, &pending);
    show_actions = (pending > 0 && is_last_command(&list, c1));
    ASSERT_EQ(show_actions, 0);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_expand_button_visibility(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    /* "Expand All" button should show when collapsed and total > 4 */
    ChatMsgItem *cmds[6];
    for (int i = 0; i < 6; i++) {
        cmds[i] = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
        chat_msg_set_command(cmds[i], "ls", CMD_SAFE, 0);
    }
    int total, pending;
    count_commands(&list, &total, &pending);
    int expanded = 0;
    /* Expand button shows on cmd_index == BASE_CMD_VISIBLE - 1 when collapsed */
    int show_expand = (!expanded && total > BASE_CMD_VISIBLE
                       && command_index_of(&list, cmds[3]) == BASE_CMD_VISIBLE - 1);
    ASSERT_EQ(show_expand, 1);

    /* When expanded, no expand button */
    expanded = 1;
    show_expand = (!expanded && total > BASE_CMD_VISIBLE
                   && command_index_of(&list, cmds[3]) == BASE_CMD_VISIBLE - 1);
    ASSERT_EQ(show_expand, 0);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_single_command_no_allow_all(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    /* With only 1 command, action buttons still show (pending > 0) */
    ChatMsgItem *c0 = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    chat_msg_set_command(c0, "ls", CMD_SAFE, 0);
    int total, pending;
    count_commands(&list, &total, &pending);
    int show = (pending > 0 && is_last_command(&list, c0));
    ASSERT_EQ(show, 1);
    chat_msg_list_clear(&list);
    TEST_END();
}

/* ── Tickbox / selected tests ──────────────────────────────────────── */

static int count_selected(const ChatMsgList *list)
{
    int n = 0;
    if (!list) return 0;
    ChatMsgItem *item = list->head;
    while (item) {
        if (item->type == CHAT_ITEM_COMMAND &&
            item->u.cmd.approved == -1 && !item->u.cmd.blocked &&
            item->u.cmd.selected)
            n++;
        item = item->next;
    }
    return n;
}

int test_cmd_selected_default_zero(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *c0 = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    chat_msg_set_command(c0, "ls", CMD_SAFE, 0);
    ASSERT_EQ(c0->u.cmd.selected, 0);
    ASSERT_EQ(count_selected(&list), 0);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_cmd_selected_toggle(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *c0 = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    chat_msg_set_command(c0, "ls", CMD_SAFE, 0);
    ChatMsgItem *c1 = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    chat_msg_set_command(c1, "pwd", CMD_SAFE, 0);
    ChatMsgItem *c2 = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    chat_msg_set_command(c2, "cat f", CMD_SAFE, 0);

    /* Select first and third */
    c0->u.cmd.selected = 1;
    c2->u.cmd.selected = 1;
    ASSERT_EQ(count_selected(&list), 2);

    /* Deselect first */
    c0->u.cmd.selected = 0;
    ASSERT_EQ(count_selected(&list), 1);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_cmd_selected_blocked_not_counted(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *c0 = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    chat_msg_set_command(c0, "rm -rf /", CMD_CRITICAL, 1); /* blocked */
    c0->u.cmd.selected = 1;  /* selected but blocked */
    ASSERT_EQ(count_selected(&list), 0);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_cmd_selected_approved_not_counted(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *c0 = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    chat_msg_set_command(c0, "ls", CMD_SAFE, 0);
    c0->u.cmd.selected = 1;
    c0->u.cmd.approved = 1; /* already approved */
    ASSERT_EQ(count_selected(&list), 0);
    chat_msg_list_clear(&list);
    TEST_END();
}
