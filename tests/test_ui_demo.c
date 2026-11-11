#include "test_framework.h"
#include "ui_demo.h"
#include "chat_msg.h"
#include "cmd_run_log.h"
#include "chat_copy.h"
#include "text_runs.h"
#include <stdio.h>
#include <stdlib.h>
#include "ai_panel_layout.h"
#include "ns_scale.h"
#include "term.h"
#include <stdio.h>
#include <string.h>

/* ===========================================================================
 * ui_demo tests (Design-System Foundation, task 9; extended for pending
 * command batches, docs/superpowers/specs/
 * 2026-09-09-pending-command-batches.md) -- see
 * docs/superpowers/specs/2026-09-07-design-system-foundation-design.md
 * section 5 ("Verification harness").
 * ===========================================================================
 */

static int count_status(const ApprovalQueue *q, ApprovalStatus status)
{
    int n = 0;
    for (int i = 0; i < q->count; i++)
        if (q->entries[i].status == status) n++;
    return n;
}

/* "all" stays last and still means the union of the panel states. "local"
 * (v1.2.1, spec 2026-09-22-local-shell-design.md section 5) only swaps the
 * terminal transcript; "thinking" is "chat" with a longer reasoning block;
 * "cmddetail", "settings-debug", "policy-auto", "policy-compact" and "ansi"
 * reuse or swap content too, and "shortcuts" (2026-10-20) opens the modal
 * keyboard shortcuts sheet. None of those is part of "all". */
int test_ui_demo_states_list_ends_in_all(void)
{
    TEST_BEGIN();
    int count = 0;
    const char *const *states = ui_demo_states(&count);
    ASSERT_NOT_NULL(states);
    ASSERT_EQ(count, 19);
    ASSERT_STR_EQ(states[count - 1], "all");
    ASSERT_STR_EQ(states[count - 2], "shortcuts");
    ASSERT_STR_EQ(states[count - 3], "policy-compact");
    ASSERT_STR_EQ(states[count - 4], "policy-auto");
    ASSERT_STR_EQ(states[count - 5], "settings-debug");
    ASSERT_STR_EQ(states[count - 6], "cmddetail");
    ASSERT_STR_EQ(states[count - 7], "thinking");
    ASSERT_STR_EQ(states[count - 8], "local");
    ASSERT_STR_EQ(states[count - 9], "ansi");
    ASSERT_STR_EQ(states[count - 10], "runlist");   /* not part of "all" */
    TEST_END();
}

/* "local" is a real state, and it changes the terminal transcript without
 * adding anything to the conversation or the approval queues. */
int test_ui_demo_build_local_swaps_terminal_text_only(void)
{
    TEST_BEGIN();
    AiConversation conv;
    ApprovalQueue approval, approval2;
    char term_buf[8192];

    ASSERT_TRUE(ui_demo_state_valid("local"));
    ASSERT_EQ(ui_demo_build("local", &conv, &approval, &approval2,
                            term_buf, sizeof(term_buf)), 0);
    /* System message only -- no user/assistant turns, no commands. */
    ASSERT_EQ(conv.msg_count, 1);
    ASSERT_EQ(approval.count, 0);
    ASSERT_EQ(approval2.count, 0);
    ASSERT_TRUE(strstr(term_buf, "PS C:\\Users\\thomas>") != NULL);
    ASSERT_TRUE(strstr(term_buf, "web-01") == NULL);

    /* Every other state keeps the SSH transcript. */
    ai_conv_reset(&conv);
    char ssh_buf[8192];
    ASSERT_EQ(ui_demo_build("chat", &conv, &approval, &approval2,
                            ssh_buf, sizeof(ssh_buf)), 0);
    ASSERT_TRUE(strstr(ssh_buf, "web-01") != NULL);
    ai_conv_reset(&conv);
    ASSERT_EQ(ui_demo_build("all", &conv, &approval, &approval2,
                            ssh_buf, sizeof(ssh_buf)), 0);
    ASSERT_TRUE(strstr(ssh_buf, "web-01") != NULL);
    ai_conv_reset(&conv);
    TEST_END();
}

int test_ui_demo_state_valid_accepts_every_listed_state(void)
{
    TEST_BEGIN();
    int count = 0;
    const char *const *states = ui_demo_states(&count);
    for (int i = 0; i < count; i++)
        ASSERT_TRUE(ui_demo_state_valid(states[i]));
    TEST_END();
}

int test_ui_demo_state_valid_rejects_unknown_and_null(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(!ui_demo_state_valid("bogus"));
    ASSERT_TRUE(!ui_demo_state_valid(""));
    ASSERT_TRUE(!ui_demo_state_valid(NULL));
    TEST_END();
}

int test_ui_demo_build_unknown_state_returns_error(void)
{
    TEST_BEGIN();
    AiConversation conv;
    ApprovalQueue approval, approval2;
    char term[64];
    ASSERT_EQ(ui_demo_build("bogus", &conv, &approval, &approval2, term, sizeof(term)), -1);
    TEST_END();
}

int test_ui_demo_build_null_outputs_safe(void)
{
    TEST_BEGIN();
    ASSERT_EQ(ui_demo_build("chat", NULL, NULL, NULL, NULL, 0), 0);
    TEST_END();
}

int test_ui_demo_build_chat_counts(void)
{
    TEST_BEGIN();
    AiConversation conv;
    ApprovalQueue approval, approval2;
    ASSERT_EQ(ui_demo_build("chat", &conv, &approval, &approval2, NULL, 0), 0);
    ASSERT_EQ(conv.msg_count, 3); /* system, user, assistant */
    ASSERT_EQ((int)conv.messages[0].role, (int)AI_ROLE_SYSTEM);
    ASSERT_EQ((int)conv.messages[1].role, (int)AI_ROLE_USER);
    ASSERT_EQ((int)conv.messages[2].role, (int)AI_ROLE_ASSISTANT);
    ASSERT_EQ(approval.count, 0);
    ASSERT_EQ(approval2.count, 0);
    ai_conv_reset(&conv);
    TEST_END();
}

int test_ui_demo_build_approval_counts_and_statuses(void)
{
    TEST_BEGIN();
    AiConversation conv;
    ApprovalQueue approval, approval2;
    ASSERT_EQ(ui_demo_build("approval", &conv, &approval, &approval2, NULL, 0), 0);
    ASSERT_EQ(conv.msg_count, 3);
    /* 2026-10-13: a fifth row, the long pending command, was added. */
    ASSERT_EQ(approval.count, 5);
    ASSERT_EQ(count_status(&approval, APPROVE_PENDING), 2);
    ASSERT_EQ(count_status(&approval, APPROVE_APPROVED), 1);
    ASSERT_EQ(count_status(&approval, APPROVE_DENIED), 1);
    ASSERT_EQ(count_status(&approval, APPROVE_BLOCKED), 1);
    ASSERT_EQ(approval2.count, 0);
    ai_conv_reset(&conv);
    TEST_END();
}

int test_ui_demo_build_executing_counts_and_statuses(void)
{
    TEST_BEGIN();
    AiConversation conv;
    ApprovalQueue approval, approval2;
    ASSERT_EQ(ui_demo_build("executing", &conv, &approval, &approval2, NULL, 0), 0);
    ASSERT_EQ(conv.msg_count, 3);
    ASSERT_EQ(approval.count, 2);
    ASSERT_EQ(count_status(&approval, APPROVE_COMPLETED), 1);
    ASSERT_EQ(count_status(&approval, APPROVE_EXECUTING), 1);
    ASSERT_EQ(approval2.count, 0);
    ai_conv_reset(&conv);
    TEST_END();
}

/* The "executing" state replayed the way ai_chat.c's
 * append_batch_command_items() rebuilds a card -- one CHAT_ITEM_COMMAND per
 * queue entry, then chat_msg_batch_sync_run() from that same queue -- must
 * carry the live panel's run states: the COMPLETED command reads "ran", the
 * EXECUTING one "running". Before the 2026-09-23 dispatch-states change a
 * rebuild painted both of them "ran". */
int test_ui_demo_executing_rebuild_carries_run_states(void)
{
    TEST_BEGIN();
    AiConversation conv;
    ApprovalQueue approval, approval2;
    ASSERT_EQ(ui_demo_build("executing", &conv, &approval, &approval2, NULL, 0), 0);
    ASSERT_EQ(approval.count, 2);

    ChatMsgList list;
    chat_msg_list_init(&list);
    for (int i = 0; i < approval.count; i++) {
        ChatMsgItem *it = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
        ASSERT_NOT_NULL(it);
        chat_msg_set_command(it, approval.entries[i].command,
                             approval.entries[i].safety,
                             approval.entries[i].status == APPROVE_BLOCKED);
        chat_msg_set_batch(it, 7);
        /* Both are past the approval card: the replay settles them. */
        it->u.cmd.approved = 1;
        it->u.cmd.settled = 1;
    }
    ASSERT_EQ(chat_msg_batch_sync_run(&list, 7, &approval, 0), 2);

    ChatMsgItem *first = list.head;
    ASSERT_NOT_NULL(first);
    ChatMsgItem *second = first->next;
    ASSERT_NOT_NULL(second);
    ASSERT_EQ(first->u.cmd.run, CHAT_RUN_DONE);
    ASSERT_EQ(second->u.cmd.run, CHAT_RUN_RUNNING);

    ChatCmdIntent intent = CHAT_CMD_INTENT_DIM;
    ASSERT_STR_EQ(chat_cmd_label(first->u.cmd.approved, first->u.cmd.blocked, 0,
                                 first->u.cmd.run, &intent), "ran");
    ASSERT_EQ((int)intent, (int)CHAT_CMD_INTENT_SUCCESS);
    ASSERT_STR_EQ(chat_cmd_label(second->u.cmd.approved, second->u.cmd.blocked, 0,
                                 second->u.cmd.run, &intent), "running");
    ASSERT_EQ((int)intent, (int)CHAT_CMD_INTENT_INFO);

    chat_msg_list_clear(&list);
    ai_conv_reset(&conv);
    TEST_END();
}

int test_ui_demo_build_tool_counts(void)
{
    TEST_BEGIN();
    AiConversation conv;
    ApprovalQueue approval, approval2;
    ASSERT_EQ(ui_demo_build("tool", &conv, &approval, &approval2, NULL, 0), 0);
    /* system, user, assistant(tool_use), tool result, assistant summary */
    ASSERT_EQ(conv.msg_count, 5);
    ASSERT_EQ(approval.count, 0);
    ASSERT_EQ((int)conv.messages[2].role, (int)AI_ROLE_ASSISTANT);
    ASSERT_EQ(conv.messages[2].n_tool_calls, 1);
    ASSERT_STR_EQ(conv.messages[2].tool_calls[0].name, "web_search");
    ASSERT_EQ((int)conv.messages[3].role, (int)AI_ROLE_TOOL);
    ASSERT_EQ(conv.messages[3].n_parts, 1);
    ASSERT_STR_EQ(conv.messages[3].parts[0].tool_name, "web_search");
    ai_conv_reset(&conv);
    TEST_END();
}

int test_ui_demo_build_error_counts(void)
{
    TEST_BEGIN();
    AiConversation conv;
    ApprovalQueue approval, approval2;
    ASSERT_EQ(ui_demo_build("error", &conv, &approval, &approval2, NULL, 0), 0);
    ASSERT_EQ(conv.msg_count, 3); /* system, two user turns, no reply */
    ASSERT_EQ(approval.count, 0);
    ai_conv_reset(&conv);
    TEST_END();
}

int test_ui_demo_build_empty_counts(void)
{
    TEST_BEGIN();
    AiConversation conv;
    ApprovalQueue approval, approval2;
    ASSERT_EQ(ui_demo_build("empty", &conv, &approval, &approval2, NULL, 0), 0);
    ASSERT_EQ(conv.msg_count, 1); /* system prompt only -- always skipped */
    ASSERT_EQ(approval.count, 0);
    ai_conv_reset(&conv);
    TEST_END();
}

int test_ui_demo_build_nokey_counts(void)
{
    TEST_BEGIN();
    AiConversation conv;
    ApprovalQueue approval, approval2;
    ASSERT_EQ(ui_demo_build("nokey", &conv, &approval, &approval2, NULL, 0), 0);
    ASSERT_EQ(conv.msg_count, 1); /* system prompt only -- always skipped */
    ASSERT_EQ(approval.count, 0);
    ai_conv_reset(&conv);
    TEST_END();
}

int test_ui_demo_build_nosession_counts(void)
{
    TEST_BEGIN();
    AiConversation conv;
    ApprovalQueue approval, approval2;
    ASSERT_EQ(ui_demo_build("nosession", &conv, &approval, &approval2, NULL, 0), 0);
    ASSERT_EQ(conv.msg_count, 1); /* system prompt only -- always skipped */
    ASSERT_EQ(approval.count, 0);
    ai_conv_reset(&conv);
    TEST_END();
}

/* "batches": batch 1 (user, assistant, pending+blocked pair) then an
 * interleaving user turn then batch 2 (assistant, pending+blocked pair) --
 * see docs/superpowers/specs/2026-09-09-pending-command-batches.md. */
int test_ui_demo_build_batches_counts_and_statuses(void)
{
    TEST_BEGIN();
    AiConversation conv;
    ApprovalQueue approval, approval2;
    ASSERT_EQ(ui_demo_build("batches", &conv, &approval, &approval2, NULL, 0), 0);
    /* system, user, assistant, user, assistant */
    ASSERT_EQ(conv.msg_count, 5);
    ASSERT_EQ((int)conv.messages[1].role, (int)AI_ROLE_USER);
    ASSERT_EQ((int)conv.messages[2].role, (int)AI_ROLE_ASSISTANT);
    ASSERT_EQ((int)conv.messages[3].role, (int)AI_ROLE_USER);
    ASSERT_EQ((int)conv.messages[4].role, (int)AI_ROLE_ASSISTANT);

    ASSERT_EQ(approval.count, 2);
    ASSERT_EQ(count_status(&approval, APPROVE_PENDING), 1);
    ASSERT_EQ(count_status(&approval, APPROVE_BLOCKED), 1);

    ASSERT_EQ(approval2.count, 2);
    ASSERT_EQ(count_status(&approval2, APPROVE_PENDING), 1);
    ASSERT_EQ(count_status(&approval2, APPROVE_BLOCKED), 1);
    ai_conv_reset(&conv);
    TEST_END();
}

int test_ui_demo_build_all_is_union(void)
{
    TEST_BEGIN();
    AiConversation conv;
    ApprovalQueue approval, approval2;
    ASSERT_EQ(ui_demo_build("all", &conv, &approval, &approval2, NULL, 0), 0);
    /* 1 shared system + (chat 2) + (approval 2) + (executing 2) + (tool 4)
     * + (error 2) + (batches 4) = 17 conversation messages. */
    ASSERT_EQ(conv.msg_count, 17);
    /* approval(5) + executing(2) + batches' first batch(2) = 9 entries
     * (approval grew from 4 to 5 on 2026-10-13: the long pending command). */
    ASSERT_EQ(approval.count, 9);
    /* batches' second batch. */
    ASSERT_EQ(approval2.count, 2);
    ai_conv_reset(&conv);
    TEST_END();
}

/* 2026-10-13 (command details popup): every "approval" row carries the
 * assistant's note, and one pending row is long enough to truncate. */
int test_ui_demo_approval_rows_have_notes_and_a_long_command(void)
{
    TEST_BEGIN();
    AiConversation conv;
    ApprovalQueue approval, approval2;
    ASSERT_EQ(ui_demo_build("approval", &conv, &approval, &approval2, NULL, 0), 0);
    int long_idx = -1;
    for (int i = 0; i < approval.count; i++) {
        ASSERT_TRUE(approval.entries[i].note[0] != '\0');
        if (strlen(approval.entries[i].command) >= 100) long_idx = i;
    }
    ASSERT_TRUE(long_idx >= 0);
    ASSERT_TRUE(strlen(approval.entries[long_idx].command) <= 140);
    ASSERT_EQ((int)approval.entries[long_idx].status, (int)APPROVE_PENDING);
    /* Notes sit on the right rows: the note belongs to its own command. */
    for (int i = 0; i < approval.count; i++) {
        if (strcmp(approval.entries[i].command, "ls -la /var/log") == 0)
            ASSERT_NOT_NULL(strstr(approval.entries[i].note, "log directory"));
        if (strcmp(approval.entries[i].command, "rm -rf /var/log/old") == 0)
            ASSERT_NOT_NULL(strstr(approval.entries[i].note, "old rotated logs"));
    }
    ai_conv_reset(&conv);
    TEST_END();
}

/* "cmddetail" is "approval" under another name: the same conversation, the
 * same rows, statuses and notes; "all" does not repeat it. */
int test_ui_demo_cmddetail_matches_approval(void)
{
    TEST_BEGIN();
    AiConversation ca, cb;
    ApprovalQueue a, a2, b, b2;
    ASSERT_TRUE(ui_demo_state_valid("cmddetail"));
    ASSERT_EQ(ui_demo_build("approval", &ca, &a, &a2, NULL, 0), 0);
    ASSERT_EQ(ui_demo_build("cmddetail", &cb, &b, &b2, NULL, 0), 0);
    ASSERT_EQ(ca.msg_count, cb.msg_count);
    ASSERT_EQ(a.count, b.count);
    ASSERT_EQ(b2.count, 0);
    for (int i = 0; i < a.count; i++) {
        ASSERT_STR_EQ(a.entries[i].command, b.entries[i].command);
        ASSERT_STR_EQ(a.entries[i].note, b.entries[i].note);
        ASSERT_EQ((int)a.entries[i].status, (int)b.entries[i].status);
    }
    ASSERT_TRUE(!ui_demo_scrolls_to_top("cmddetail"));
    ai_conv_reset(&ca);
    ai_conv_reset(&cb);
    TEST_END();
}

int test_ui_demo_term_text_ends_with_prompt(void)
{
    TEST_BEGIN();
    AiConversation conv;
    ApprovalQueue approval, approval2;
    char term[4096];
    ASSERT_EQ(ui_demo_build("chat", &conv, &approval, &approval2, term, sizeof(term)), 0);
    size_t len = strlen(term);
    ASSERT_TRUE(len > 2);
    ASSERT_STR_EQ(term + len - 2, "$ ");
    ai_conv_reset(&conv);
    TEST_END();
}

int test_ui_demo_thinking_text_non_empty(void)
{
    TEST_BEGIN();
    const char *t = ui_demo_thinking_text();
    ASSERT_NOT_NULL(t);
    ASSERT_TRUE(t[0] != '\0');
    TEST_END();
}

/* "thinking" state: the 50-line scrollable Thinking box demo. Its
 * conversation is exactly "chat"'s (same user/assistant turns); only the
 * attached thinking text differs (ai_chat.c's demo wiring, not this
 * module's job to prove). */
int test_ui_demo_thinking_text_long_non_empty_and_much_longer(void)
{
    TEST_BEGIN();
    const char *t = ui_demo_thinking_text_long();
    ASSERT_NOT_NULL(t);
    ASSERT_TRUE(t[0] != '\0');
    /* Comfortably over THINKING_MAX_LINES (50) lines' worth of prose at
     * any plausible panel width -- see ai_thinking_max_body_h() in
     * ai_panel_layout.h. */
    ASSERT_TRUE(strlen(t) > 2000);
    TEST_END();
}

int test_ui_demo_build_thinking_matches_chat_conversation(void)
{
    TEST_BEGIN();
    AiConversation chat_conv, thinking_conv;
    ApprovalQueue approval, approval2;

    ASSERT_TRUE(ui_demo_state_valid("thinking"));
    ASSERT_EQ(ui_demo_build("chat", &chat_conv, &approval, &approval2, NULL, 0), 0);
    ASSERT_EQ(ui_demo_build("thinking", &thinking_conv, &approval, &approval2, NULL, 0), 0);

    ASSERT_EQ(thinking_conv.msg_count, chat_conv.msg_count);
    for (int i = 0; i < chat_conv.msg_count; i++) {
        ASSERT_EQ((int)thinking_conv.messages[i].role, (int)chat_conv.messages[i].role);
        ASSERT_STR_EQ(thinking_conv.messages[i].content, chat_conv.messages[i].content);
    }
    ai_conv_reset(&chat_conv);
    ai_conv_reset(&thinking_conv);
    TEST_END();
}

/* "thinking" is deliberately not in the is_all chain (see ui_demo_build())
 * -- test_ui_demo_build_all_is_union() above pins "all"'s exact message
 * count at 17, unchanged by adding "thinking", so that test failing would
 * be the signal if this ever regresses. */

/* ui_demo_scrolls_to_top(): "chat"/"all"/"thinking" have a canned reply
 * long enough to push the Thinking disclosure off the top of the thread
 * once ai_chat_apply_demo_extras() (src/ui/ai_chat.c) finishes its usual
 * scroll-to-bottom, so those three need the panel pulled back to the top
 * afterwards. "thinking" was missing from this set until 2026-09-24: left
 * scrolled to the bottom with the disclosure off-screen, chat_listview's
 * sticky-user-message repaint found the last user bubble above the
 * viewport and pinned it at y=0, stale paint directly on top of the
 * reply's own heading (tests/integration/artifacts/gallery/
 * <Theme>-thinking.png showed it before this fix). Every other state
 * (including "local", and unrecognised/NULL) keeps the bottom scroll. */
int test_ui_demo_scrolls_to_top_chat_all_and_thinking(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(ui_demo_scrolls_to_top("chat"));
    ASSERT_TRUE(ui_demo_scrolls_to_top("all"));
    ASSERT_TRUE(ui_demo_scrolls_to_top("thinking"));
    TEST_END();
}

int test_ui_demo_scrolls_to_top_false_for_other_states(void)
{
    TEST_BEGIN();
    int count = 0;
    const char *const *states = ui_demo_states(&count);
    for (int i = 0; i < count; i++) {
        const char *s = states[i];
        if (strcmp(s, "chat") == 0 || strcmp(s, "all") == 0 ||
            strcmp(s, "thinking") == 0)
            continue;
        ASSERT_TRUE(!ui_demo_scrolls_to_top(s));
    }
    ASSERT_TRUE(!ui_demo_scrolls_to_top("bogus-state"));
    ASSERT_TRUE(!ui_demo_scrolls_to_top(NULL));
    TEST_END();
}

/* "settings-debug" is a real state that builds an empty conversation (the
 * Settings window is opened by the win32 side) and is not part of "all". */
int test_ui_demo_settings_debug_is_an_empty_state(void)
{
    TEST_BEGIN();
    AiConversation conv;
    ApprovalQueue approval, approval2;
    char term_buf[8192];
    ASSERT_TRUE(ui_demo_state_valid("settings-debug"));
    ASSERT_EQ(ui_demo_build("settings-debug", &conv, &approval, &approval2,
                            term_buf, sizeof(term_buf)), 0);
    ASSERT_EQ(conv.msg_count, 1);
    ASSERT_EQ(approval.count, 0);
    ASSERT_EQ(approval2.count, 0);
    ASSERT_TRUE(!ui_demo_scrolls_to_top("settings-debug"));
    ASSERT_TRUE(!ui_demo_state_valid("settings"));
    ASSERT_TRUE(!ui_demo_state_valid("settings-debug "));
    ai_conv_reset(&conv);
    TEST_END();
}

/* "shortcuts" is a real state that builds an empty conversation (the win32
 * side opens Help > Keyboard Shortcuts over it) and is not part of "all". */
int test_ui_demo_shortcuts_is_an_empty_state(void)
{
    TEST_BEGIN();
    AiConversation conv;
    ApprovalQueue approval, approval2;
    char term_buf[8192];
    ASSERT_TRUE(ui_demo_state_valid("shortcuts"));
    ASSERT_EQ(ui_demo_build("shortcuts", &conv, &approval, &approval2,
                            term_buf, sizeof(term_buf)), 0);
    ASSERT_EQ(conv.msg_count, 1);
    ASSERT_EQ(approval.count, 0);
    ASSERT_EQ(approval2.count, 0);
    ASSERT_TRUE(!ui_demo_scrolls_to_top("shortcuts"));
    ASSERT_TRUE(!ui_demo_state_valid("shortcut"));
    ASSERT_TRUE(!ui_demo_state_valid("Shortcuts"));
    ai_conv_reset(&conv);
    TEST_END();
}

/* The "chat" state's selection (spec 2026-10-18 section 6), copied the way
 * the panel copies it: the user turn and the reply replayed as items like
 * ai_chat_rebuild_display() does, the Thinking body expanded as
 * ai_chat_apply_demo_extras() leaves it. */
static void build_chat_items(ChatMsgList *l, AiConversation *conv)
{
    chat_msg_list_init(l);
    chat_msg_append(l, CHAT_ITEM_USER, ai_msg_content(&conv->messages[1]));
    ChatMsgItem *ai = chat_msg_append(l, CHAT_ITEM_AI_TEXT,
                                      ai_msg_content(&conv->messages[2]));
    chat_msg_set_thinking(ai, ui_demo_thinking_text());
    ai->u.ai.thinking_collapsed = 0;
}

int test_ui_demo_chat_selection_spans_every_kind(void)
{
    TEST_BEGIN();
    AiConversation *conv = (AiConversation *)calloc(1, sizeof(*conv));
    ASSERT_NOT_NULL(conv);
    if (!conv) { TEST_END(); }
    int built = ui_demo_build("chat", conv, NULL, NULL, NULL, 0);
    ASSERT_EQ(built, 0);
    ChatMsgList l;
    build_chat_items(&l, conv);
    int uo = -1, ro = -1;
    ui_demo_chat_selection(&uo, &ro);
    ChatSelPos a = { l.head->id, TR_PART_MAIN, uo };
    ChatSelPos b = { l.tail->id, TR_PART_MAIN, ro };
    char *out = NULL;
    size_t n = chat_copy_selection(&l, a, b, 1, &out);
    ASSERT_TRUE(n > 0);
    ASSERT_NOT_NULL(out);
    if (out) {
        ASSERT_TRUE(strncmp(out, "disk usage on this box", 22) == 0);
        ASSERT_TRUE(strstr(out, ui_demo_thinking_text()) != NULL);
        ASSERT_TRUE(strstr(out, "I checked the mounts first:\ndf -h /\n") != NULL);
        ASSERT_TRUE(strstr(out, "- Root partition: 42% used") != NULL);
        ASSERT_TRUE(strstr(out, "Mount\tUsed\tAvail") != NULL);
        ASSERT_TRUE(strstr(out, "$ du -sh /var/log/*") != NULL);
        ASSERT_TRUE(strstr(out, "[EXEC]") == NULL);
        ASSERT_TRUE(strstr(out, "**") == NULL);
        ASSERT_TRUE(strstr(out, "```") == NULL);
        ASSERT_TRUE(n >= 7 && strcmp(out + n - 7, "Want me") == 0);
    }
    free(out);
    chat_msg_list_clear(&l);
    ai_conv_reset(conv);
    free(conv);
    TEST_END();
}

/* What the integration case ai_panel_select_across_items
 * (tests/integration/cases/95-ai-select.ps1) expects on the clipboard after
 * dragging from "disk" in the user bubble to the end of "found" in the
 * reply. Keep the two strings identical. */
int test_ui_demo_chat_drag_across_items_golden(void)
{
    TEST_BEGIN();
    AiConversation *conv = (AiConversation *)calloc(1, sizeof(*conv));
    ASSERT_NOT_NULL(conv);
    if (!conv) { TEST_END(); }
    int built = ui_demo_build("chat", conv, NULL, NULL, NULL, 0);
    ASSERT_EQ(built, 0);
    ChatMsgList l;
    build_chat_items(&l, conv);
    const char *u = l.head->text, *r = l.tail->text;
    ChatSelPos a = { l.head->id, TR_PART_MAIN, (int)(strstr(u, "disk") - u) };
    ChatSelPos b = { l.tail->id, TR_PART_MAIN,
                     (int)(strstr(r, "found") - r) + 5 };
    char *out = NULL;
    chat_copy_selection(&l, a, b, 1, &out);
    char want[1024];
    snprintf(want, sizeof(want),
             "disk usage on this box and tell me what's eating space?\n\n"
             "%s\n\n"
             "Disk Usage Summary\n\nI checked the mounts first:\ndf -h /\n\n"
             "Here's what I found", ui_demo_thinking_text());
    ASSERT_NOT_NULL(out);
    if (out) ASSERT_STR_EQ(out, want);
    free(out);
    chat_msg_list_clear(&l);
    ai_conv_reset(conv);
    free(conv);
    TEST_END();
}

/* 2026-10-18 auto-approve: "policy-auto" is "executing" (a running card)
 * with the demo tab's policy at Write and Auto-approve on. */
int test_ui_demo_policy_auto_is_executing_with_auto_on(void)
{
    TEST_BEGIN();
    AiConversation conv, conv2;
    ApprovalQueue approval, approval2, ea, ea2;
    ASSERT_TRUE(ui_demo_state_valid("policy-auto"));
    ASSERT_EQ(ui_demo_build("policy-auto", &conv, &approval, &approval2, NULL, 0), 0);
    ASSERT_EQ(ui_demo_build("executing", &conv2, &ea, &ea2, NULL, 0), 0);
    ASSERT_EQ(conv.msg_count, conv2.msg_count);
    ASSERT_EQ(approval.count, ea.count);
    ASSERT_EQ(count_status(&approval, APPROVE_EXECUTING), 1);
    ASSERT_EQ(approval2.count, 0);
    CmdPolicy p = ui_demo_policy("policy-auto");
    ASSERT_EQ(p.allowed, (int)CMD_WRITE);
    ASSERT_EQ(p.unattended, (int)CMD_WRITE);
    ASSERT_TRUE(!ui_demo_scrolls_to_top("policy-auto"));
    ai_conv_reset(&conv);
    ai_conv_reset(&conv2);
    TEST_END();
}

/* "policy-compact" is "approval" in a panel narrow enough for R U W C. */
int test_ui_demo_policy_compact_matches_approval(void)
{
    TEST_BEGIN();
    AiConversation conv, conv2;
    ApprovalQueue approval, approval2, aa, aa2;
    ASSERT_TRUE(ui_demo_state_valid("policy-compact"));
    ASSERT_EQ(ui_demo_build("policy-compact", &conv, &approval, &approval2, NULL, 0), 0);
    ASSERT_EQ(ui_demo_build("approval", &conv2, &aa, &aa2, NULL, 0), 0);
    ASSERT_EQ(conv.msg_count, conv2.msg_count);
    ASSERT_EQ(approval.count, aa.count);
    ASSERT_TRUE(!ui_demo_scrolls_to_top("policy-compact"));
    ai_conv_reset(&conv);
    ai_conv_reset(&conv2);
    TEST_END();
}

int test_ui_demo_policy_for_other_states_is_write_off(void)
{
    TEST_BEGIN();
    static const char *const others[4] = { "chat", "approval", "all", "policy-compact" };
    for (int i = 0; i < 4; i++) {
        CmdPolicy p = ui_demo_policy(others[i]);
        ASSERT_EQ(p.allowed, (int)CMD_WRITE);
        ASSERT_EQ(p.unattended, POLICY_NONE);
    }
    CmdPolicy n = ui_demo_policy(NULL);
    ASSERT_EQ(n.allowed, (int)CMD_WRITE);
    ASSERT_EQ(n.unattended, POLICY_NONE);
    TEST_END();
}

int test_ui_demo_panel_width_only_for_narrow_states(void)
{
    TEST_BEGIN();
    ASSERT_EQ(ui_demo_panel_width_px("policy-compact", 96), 260);
    ASSERT_EQ(ui_demo_panel_width_px("policy-compact", 192), 520);
    ASSERT_EQ(ui_demo_panel_width_px("ansi", 96), 260);
    ASSERT_EQ(ui_demo_panel_width_px("policy-auto", 96), 0);
    ASSERT_EQ(ui_demo_panel_width_px("all", 96), 0);
    ASSERT_EQ(ui_demo_panel_width_px(NULL, 96), 0);
    /* With caption-font widths measured near enough at 96 DPI (full pill
     * 200, compact 96, "Auto-approve" 68, "Auto" 24, no meter), that width
     * lands on the compact tier, not the clipped one. */
    AiStatusMeasure m = { 200, 96, 68, 24, 0 };
    NsRect status = { 0, 0, ui_demo_panel_width_px("policy-compact", 96), 28 };
    AiStatusLayout l;
    ai_status_layout(status, 96, &m, &l);
    ASSERT_EQ(l.compact, 1);
    ASSERT_EQ(l.tier, AI_STATUS_TIER_COMPACT);
    TEST_END();
}

/* "ansi" (spec 2026-10-19-light-theme-ansi-contrast-design.md, section 6):
 * an empty conversation, 17 swatch lines in index order (0-15, then dim
 * bright white), none wider than 24 columns, no trailing newline so the
 * cursor ends on line 16 and nothing scrolls, and the narrow dock. */
int test_ui_demo_ansi_swatches(void)
{
    TEST_BEGIN();
    AiConversation conv;
    ApprovalQueue approval, approval2;
    char text[8192];
    text[0] = '\0';
    ASSERT_TRUE(ui_demo_state_valid("ansi"));
    int built = ui_demo_build("ansi", &conv, &approval, &approval2,
                              text, sizeof(text));
    ASSERT_EQ(built, 0);
    if (built != 0) { TEST_END(); }
    ASSERT_EQ(conv.msg_count, 1);
    ASSERT_EQ(approval.count, 0);
    ASSERT_EQ(approval2.count, 0);
    ASSERT_TRUE(strstr(text, "web-01") == NULL);
    size_t len = strlen(text);
    ASSERT_TRUE(len > 2u);
    ASSERT_TRUE(!(text[len - 2] == '\r' && text[len - 1] == '\n'));
    int crlf = 0;
    for (const char *p = strstr(text, "\r\n"); p; p = strstr(p + 2, "\r\n")) crlf++;
    ASSERT_EQ(crlf, 16);

    Terminal *t = term_init(30, 40, 100);
    ASSERT_NOT_NULL(t);
    if (!t) { ai_conv_reset(&conv); TEST_END(); }
    term_process(t, text, len);
    ASSERT_EQ(t->cursor.row, 16);
    ASSERT_EQ(t->cursor.col, 11);
    for (int r = 0; r < 17; r++) {
        const TermRow *row = t->lines[term_screen_to_phys(t, r)];
        ASSERT_TRUE(row->len <= 24);
        char label[4] = { (char)row->cells[0].codepoint, (char)row->cells[1].codepoint,
                          (char)row->cells[2].codepoint, '\0' };
        char want[8];
        if (r < 16) (void)snprintf(want, sizeof(want), "%03d", r);
        else        (void)snprintf(want, sizeof(want), "dim");
        ASSERT_STR_EQ(label, want);
        for (int c = 4; c < 8; c++) {
            const TermAttr *a = &row->cells[c].attr;
            ASSERT_EQ(row->cells[c].codepoint, 0x2588u);
            ASSERT_EQ(a->fg_mode, COLOR_ANSI16);
            ASSERT_EQ(a->fg_index, (r < 16) ? r : 15);
            ASSERT_EQ((a->flags & TERM_ATTR_DIM) != 0, r == 16);
            ASSERT_EQ(a->bg_mode, COLOR_DEFAULT);
        }
    }
    ASSERT_EQ(t->lines[term_screen_to_phys(t, 17)]->len, 0);
    term_free(t);

    ASSERT_EQ(ui_demo_panel_width_px("ansi", 96), 260);
    ASSERT_EQ(ui_demo_panel_width_px("ansi", 192), 520);
    ASSERT_TRUE(!ui_demo_scrolls_to_top("ansi"));
    ai_conv_reset(&conv);
    TEST_END();
}

/* ---- "runlist" (spec 2026-11-05 run-command tool, decision 21) ---- */

/* Append `n` settled rows of `batch` the way the panel's replay does: the
 * label inputs come from the row, which came from the entry. */
static void append_run_rows(ChatMsgList *list, int batch, const CmdRunRow *rows,
                            const char *const *cmds, int n)
{
    for (int i = 0; i < n; i++) {
        ChatMsgItem *it = chat_msg_append(list, CHAT_ITEM_COMMAND, "");
        if (!it) return;
        chat_msg_set_command(it, cmds[i], rows[i].safety, rows[i].blocked);
        chat_msg_set_batch(it, batch);
        chat_msg_set_quoted(it, rows[i].quoted);
        it->u.cmd.approved = rows[i].approved;
        it->u.cmd.run = rows[i].run;
        it->u.cmd.settled = 1;
    }
}

static const char *row_label(const ChatMsgItem *it)
{
    return chat_cmd_label(it->u.cmd.approved, it->u.cmd.blocked,
                          it->u.cmd.quoted, it->u.cmd.run, NULL);
}

int test_ui_demo_runlist_rows(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(ui_demo_state_valid("runlist"));

    AiConversation conv;
    ApprovalQueue approval, approval2;
    ASSERT_EQ(ui_demo_build("runlist", &conv, &approval, &approval2, NULL, 0), 0);
    /* system, user, reply, user, reply */
    ASSERT_EQ(conv.msg_count, 5);
    ASSERT_EQ(approval.count, 4);
    ASSERT_EQ(approval2.count, 0);
    ASSERT_EQ(count_status(&approval, APPROVE_COMPLETED), 1);
    ASSERT_EQ(count_status(&approval, APPROVE_EXECUTING), 1);
    ASSERT_EQ(count_status(&approval, APPROVE_APPROVED), 2);

    /* The live batch (id 2), rows as the live panel holds them. */
    ChatMsgList list;
    chat_msg_list_init(&list);
    CmdRunRow live[APPROVAL_MAX_CMDS];
    const char *live_cmds[APPROVAL_MAX_CMDS];
    for (int i = 0; i < approval.count; i++) {
        cmd_run_row_from_entry(&approval.entries[i], 0, &live[i]);
        live_cmds[i] = approval.entries[i].command;
    }
    append_run_rows(&list, 2, live, live_cmds, approval.count);

    /* The recorded batch (id -1), replayed from the log. */
    CmdRunLog log;
    cmd_run_log_init(&log);
    ui_demo_run_log("runlist", &log, &conv);
    ASSERT_EQ(log.count, 1);
    const CmdRunRecord *rec = log.rec[0];
    ASSERT_NOT_NULL(rec);
    ASSERT_EQ(rec->n, 3);
    CmdRunRow recd[APPROVAL_MAX_CMDS];
    const char *rec_cmds[APPROVAL_MAX_CMDS];
    for (int i = 0; i < rec->n; i++) {
        recd[i] = rec->rows[i];
        rec_cmds[i] = rec->rows[i].command;
    }
    append_run_rows(&list, -1, recd, rec_cmds, rec->n);

    /* Labels in list order: the live batch, then the recorded one. */
    const char *want[] = { "ran", "running", "queued", "queued",
                           "unfinished", "not run", "denied" };
    int i = 0;
    for (ChatMsgItem *it = list.head; it; it = it->next, i++) {
        ASSERT_TRUE(i < 7);
        if (i >= 7) break;
        ASSERT_STR_EQ(row_label(it), want[i]);
    }
    ASSERT_EQ(i, 7);

    /* Both summary lines, verbatim, from the functions the painter uses. */
    ChatBatchCounts c;
    char line[128];
    ASSERT_EQ(chat_msg_batch_counts(&list, 2, &c), 4);
    ASSERT_TRUE(chat_batch_summary_text(&c, line, sizeof(line)) > 0);
    ASSERT_STR_EQ(line, "4 commands: 1 ran, 1 running, 2 queued");
    ASSERT_EQ(chat_msg_batch_counts(&list, -1, &c), 3);
    ASSERT_TRUE(chat_batch_summary_text(&c, line, sizeof(line)) > 0);
    ASSERT_STR_EQ(line, "3 commands: 1 unfinished, 1 not run, 1 denied");

    chat_msg_list_clear(&list);
    cmd_run_log_free(&log);
    ai_conv_reset(&conv);
    TEST_END();
}

int test_ui_demo_runlist_record_matches_turn(void)
{
    TEST_BEGIN();
    AiConversation conv;
    ApprovalQueue approval, approval2;
    ASSERT_EQ(ui_demo_build("runlist", &conv, &approval, &approval2, NULL, 0), 0);

    CmdRunLog log;
    cmd_run_log_init(&log);
    ui_demo_run_log("runlist", &log, &conv);
    ASSERT_EQ(log.count, 1);
    const CmdRunRecord *rec = log.rec[0];
    ASSERT_NOT_NULL(rec);

    /* Under the first reply: an assistant turn whose hash is the record's. */
    int r = rec->reply_msg;
    ASSERT_TRUE(r > 0 && r < conv.msg_count);
    const AiMessage *m = &conv.messages[r];
    ASSERT_EQ((int)m->role, (int)AI_ROLE_ASSISTANT);
    ASSERT_EQ(cmd_run_record_matches(rec, r, 1, ai_msg_content(m), m->content_len), 1);
    /* ... and the first one of the two. */
    for (int k = 1; k < r; k++)
        ASSERT_TRUE(conv.messages[k].role != AI_ROLE_ASSISTANT);
    /* Not the second reply, the other turns, or an edited turn. */
    for (int k = 0; k < conv.msg_count; k++) {
        if (k == r) continue;
        const AiMessage *o = &conv.messages[k];
        ASSERT_EQ(cmd_run_record_matches(rec, k, o->role == AI_ROLE_ASSISTANT,
                                         ai_msg_content(o), o->content_len), 0);
    }
    ASSERT_EQ(cmd_run_record_matches(rec, r, 1, "something else", 14), 0);

    /* The recorded rows ended with the batch. */
    ASSERT_EQ(rec->rows[0].run, CHAT_RUN_UNFINISHED);
    ASSERT_EQ(rec->rows[1].run, CHAT_RUN_ABORTED);
    ASSERT_EQ(rec->rows[2].approved, 0);

    /* No other state has a record; a NULL argument is harmless. */
    CmdRunLog none;
    cmd_run_log_init(&none);
    ui_demo_run_log("approval", &none, &conv);
    ui_demo_run_log("all", &none, &conv);
    ui_demo_run_log(NULL, &none, &conv);
    ui_demo_run_log("runlist", NULL, &conv);
    ASSERT_EQ(none.count, 0);

    cmd_run_log_free(&log);
    ai_conv_reset(&conv);
    TEST_END();
}

/* The displayed [EXEC] branch (ai_chat.c) adds a NOTE (a USER turn) after the
 * reply when a marker is rejected, and only then takes conv_mark: so
 * conv_mark - 1 is the NOTE, and the batch carries the reply's own index. */
int test_cmd_run_log_exec_note_before_mark(void)
{
    TEST_BEGIN();
    AiConversation conv;
    ai_conv_init(&conv, "demo");
    ai_conv_add(&conv, AI_ROLE_SYSTEM, "system");
    ai_conv_add(&conv, AI_ROLE_USER, "tidy the logs");
    const char *reply = "Doing it:\n[EXEC]ls /var/log[/EXEC]\n[EXEC]pwd[/EXEC]";
    ai_conv_add(&conv, AI_ROLE_ASSISTANT, reply);
    int reply_msg = conv.msg_count - 1;
    const char *note = "[NOTE] one command marker sat inside a code block";
    ai_conv_add(&conv, AI_ROLE_USER, note);
    int conv_mark = conv.msg_count;
    ASSERT_EQ(reply_msg, 2);
    ASSERT_EQ(conv_mark, 4);

    CmdBatchSet set;
    cmd_batch_set_init(&set);
    CmdBatch *b = cmd_batch_add(&set, NULL, NULL);
    ASSERT_NOT_NULL(b);
    b->conv_mark = conv_mark;
    b->reply_msg = reply_msg;               /* not conv_mark - 1 */
    {
        const AiMessage *pm = &conv.messages[reply_msg];
        b->reply_hash = cmd_run_log_turn_hash(ai_msg_content(pm), pm->content_len);
    }
    b->q.policy.allowed = CMD_CRITICAL;
    int i0 = chat_approval_add(&b->q, "ls /var/log", CMD_PLATFORM_LINUX);
    int i1 = chat_approval_add(&b->q, "pwd", CMD_PLATFORM_LINUX);
    chat_approval_approve(&b->q, i0);
    chat_approval_approve(&b->q, i1);

    CmdRunLog log;
    cmd_run_log_init(&log);
    const AiMessage *rm = &conv.messages[b->reply_msg];
    /* The hash the batch carries from creation, not one taken at removal. */
    ASSERT_EQ(cmd_run_log_record(&log, b, b->reply_hash, 1), 0);
    const CmdRunRecord *rec = log.rec[0];
    ASSERT_EQ(rec->reply_msg, 2);

    /* The replay lands under the reply ... */
    ASSERT_EQ(cmd_run_record_matches(rec, 2, 1, ai_msg_content(rm),
                                     rm->content_len), 1);
    /* ... and not under the NOTE that conv_mark - 1 names, whether or not
     * the caller takes it for an assistant turn. */
    const AiMessage *nm = &conv.messages[conv_mark - 1];
    ASSERT_STR_EQ(ai_msg_content(nm), note);
    ASSERT_EQ(cmd_run_record_matches(rec, conv_mark - 1, 0, ai_msg_content(nm),
                                     nm->content_len), 0);
    ASSERT_EQ(cmd_run_record_matches(rec, conv_mark - 1, 1, ai_msg_content(nm),
                                     nm->content_len), 0);

    cmd_run_log_free(&log);
    cmd_batch_set_free(&set);
    ai_conv_reset(&conv);
    TEST_END();
}
