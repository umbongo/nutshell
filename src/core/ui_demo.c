/* src/core/ui_demo.c */
#include "ui_demo.h"
#include "ai_agentic.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---------------------------------------------------------------------
 * State names, in the order ui_demo_states() reports them.  "all" is
 * always last and always means "union of the other six".
 * --------------------------------------------------------------------- */
static const char *const STATE_NAMES[] = {
    "chat", "approval", "executing", "tool", "error", "empty",
    "nokey", "nosession", "batches", "local", "thinking", "all"
};
#define N_STATES ((int)(sizeof(STATE_NAMES) / sizeof(STATE_NAMES[0])))

const char *const *ui_demo_states(int *count)
{
    if (count) *count = N_STATES;
    return STATE_NAMES;
}

int ui_demo_state_valid(const char *state)
{
    if (!state) return 0;
    for (int i = 0; i < N_STATES; i++) {
        if (strcmp(state, STATE_NAMES[i]) == 0) return 1;
    }
    return 0;
}

/* States whose canned reply is long enough to push the Thinking disclosure
 * (right above it) off the top of the thread once chat_rebuild_display()
 * lands its usual scroll-to-bottom (src/ui/ai_chat.c's
 * ai_chat_apply_demo_extras()) -- these need to be pulled back to the top
 * afterwards so the gallery capture actually shows the disclosure instead
 * of scrolling past it. "chat"/"thinking" show it collapsed, "all" shows
 * it expanded (ai_chat_apply_demo_extras() forces that). Every other state
 * keeps the bottom scroll, to show its most recent activity.
 *
 * "thinking" was missing from this set until 2026-09-24: left scrolled to
 * the bottom, chat_listview's sticky-user-message repaint (on_paint()'s
 * "last_user" pin) found the last user bubble above the viewport and
 * pinned it at y=0 -- stale paint directly on top of the reply's own
 * heading (see tests/integration/artifacts/gallery/<Theme>-thinking.png
 * before this fix; nothing else about the still-collapsed box was
 * actually wrong). */
int ui_demo_scrolls_to_top(const char *state)
{
    if (!state) return 0;
    return strcmp(state, "chat") == 0 ||
           strcmp(state, "all") == 0 ||
           strcmp(state, "thinking") == 0;
}

/* System prompt shared by every state -- index 0 of every conversation,
 * always skipped by the chat panel's replay (see ai_chat.c
 * chat_rebuild_display). Content doesn't matter for the screenshots; it
 * just has to exist so the real turns line up at index >= 1 like a live
 * conversation. */
static const char DEMO_SYSTEM_PROMPT[] =
    "You are Nutshell's AI assistant. You can see the terminal and, once "
    "the user approves a command, run it on their behalf.";

/* ---------------------------------------------------------------------
 * "chat": user message; AI reply with markdown (headings, list, table,
 * code block). The reply's "thinking" block lives outside AiConversation
 * (see ai_demo_thinking_text()) -- the win32 side attaches it after load.
 * --------------------------------------------------------------------- */
static const char CHAT_USER_MSG[] =
    "Can you check disk usage on this box and tell me what's eating space?";

static const char CHAT_ASSISTANT_MSG[] =
    "## Disk Usage Summary\n"
    "\n"
    "Here's what I found on **web-01**:\n"
    "\n"
    "- Root partition: 42% used\n"
    "- `/var/log` is growing fast\n"
    "- Largest offender: `nginx-access.log` (1.2 GB)\n"
    "\n"
    "| Mount  | Used | Avail |\n"
    "|--------|------|-------|\n"
    "| /      | 42%  | 58G   |\n"
    "| /var   | 71%  | 12G   |\n"
    "| /home  | 15%  | 88G   |\n"
    "\n"
    "```bash\n"
    "$ du -sh /var/log/* | sort -rh | head -3\n"
    "1.2G\tnginx-access.log\n"
    "340M\tnginx-error.log\n"
    "88M\tsyslog\n"
    "```\n"
    "\n"
    "Want me to rotate and compress the old logs?";

static const char CHAT_THINKING[] =
    "The user wants a disk usage summary. I should run df -h for the "
    "mount table and du on /var/log since that's usually the biggest "
    "growth area on a box like this. I'll summarize with a table and "
    "flag the largest files, then offer a concrete next step rather "
    "than just dumping raw output.";

const char *ui_demo_thinking_text(void)
{
    return CHAT_THINKING;
}

/* ---------------------------------------------------------------------
 * "thinking": same user turn as "chat", but with a much longer reasoning
 * block -- well over THINKING_MAX_LINES (50) lines wrapped at a typical
 * panel width, so the expanded Thinking box actually hits its cap and
 * scrolls (maintainer request, 2026-09-24: collapsed by default, but up
 * to 50 lines and smart-scrollable once opened). Not part of "all": like
 * "local", it exists to show one specific thing (the scrollable box) the
 * union state doesn't need a second copy of.
 * --------------------------------------------------------------------- */
static const char LONG_THINKING[] =
    "The user wants a disk usage summary, so let me work through this "
    "methodically rather than just firing off df -h and calling it done. "
    "First, what does 'eating space' actually mean here -- total capacity "
    "used, or growth rate? Both matter, but they point at different "
    "commands, so I should gather both and let the numbers tell me which "
    "one is the actual story.\n\n"
    "Step 1: df -h for the mount table. That gives me per-filesystem "
    "totals and lets me see at a glance whether any single mount is near "
    "full -- 90%+ used is the usual alarm threshold, but on a box that's "
    "been running a while, 70-80% on /var is already worth flagging since "
    "logs and package caches tend to be the fastest-growing thing there.\n\n"
    "Step 2: du on the usual suspects -- /var/log first, since that's "
    "where an unrotated or unusually chatty service log tends to hide. "
    "I'll sort by size and take the top handful rather than dumping the "
    "whole tree; nobody wants to scroll through hundreds of tiny files "
    "when three big ones are the actual problem.\n\n"
    "Step 3: while I'm at it, /var/cache and /tmp are worth a quick check "
    "too -- package manager caches in particular can balloon after a lot "
    "of upgrades and are always safe to clear, unlike log files which "
    "might still be needed for an ongoing investigation.\n\n"
    "Step 4: home directories. Probably not the culprit on a server box, "
    "but a stray core dump or a forgotten database export sitting in "
    "someone's home directory has bitten people before, so a quick du "
    "-sh on /home costs nothing and rules it out.\n\n"
    "Now, how to present this. A raw command dump is the least useful "
    "answer I could give -- the user asked what's eating space, not for "
    "a transcript. I should lead with the headline number (root partition "
    "at whatever percent), then a short table of the mounts so they can "
    "see the shape of things, then name the single biggest offender by "
    "file, since that's the actionable part.\n\n"
    "I also want to think about what happens after this reply. If the "
    "biggest offender is an access log, the natural next step is log "
    "rotation and compression -- I should offer that concretely rather "
    "than leaving it as an exercise, since 'you could rotate your logs' "
    "is not nearly as useful as 'want me to rotate and compress the old "
    "ones for you'. If it turns out to be something like a docker image "
    "layer or an orphaned container volume instead, the advice changes "
    "completely, so I shouldn't commit to the rotate-and-compress framing "
    "until I've actually seen which case this is.\n\n"
    "One more thing worth considering: is there a risk in any of the "
    "commands I'm about to suggest? df and du are both read-only and "
    "safe to run unattended. Rotating logs is a write operation but a "
    "routine, low-risk one as long as I use logrotate or an equivalent "
    "rather than just deleting files out from under a process that might "
    "still have them open -- truncating a log a service is actively "
    "writing to can crash the write, so if I do offer to compress things "
    "I should reach for copytruncate semantics rather than a bare rm.\n\n"
    "Okay, I think I have enough of a plan: gather df -h and the /var/log "
    "breakdown, summarize with a table, name the largest file by name and "
    "size, and end with a concrete offer to rotate and compress rather "
    "than an open-ended question. That gives the user something they can "
    "say yes to in one word instead of having to think about what to ask "
    "for next.";

const char *ui_demo_thinking_text_long(void)
{
    return LONG_THINKING;
}

static void build_chat(AiConversation *conv)
{
    if (!conv) return;
    ai_conv_add(conv, AI_ROLE_USER, CHAT_USER_MSG);
    ai_conv_add(conv, AI_ROLE_ASSISTANT, CHAT_ASSISTANT_MSG);
}

/* ---------------------------------------------------------------------
 * "approval": safe / write / critical commands, one each in approved,
 * denied, pending and blocked. Settled entries (approved/denied) are
 * added first so the still-active ones (pending/blocked) land adjacent
 * in the message list and group into a single active card together.
 * --------------------------------------------------------------------- */
static const char APPROVAL_USER_MSG[] =
    "Clean up the old nginx logs and get rid of that dead container.";

static const char APPROVAL_ASSISTANT_MSG[] =
    "I'll need to run a few commands for that -- here's what I'd like to do:";

static void build_approval(AiConversation *conv, ApprovalQueue *approval)
{
    if (conv) {
        ai_conv_add(conv, AI_ROLE_USER, APPROVAL_USER_MSG);
        ai_conv_add(conv, AI_ROLE_ASSISTANT, APPROVAL_ASSISTANT_MSG);
    }
    if (!approval) return;

    /* Raise the ceiling to Critical so the write and critical rows below
     * land PENDING (approve/deny only act on PENDING), then settle them
     * by hand exactly like a live approval card. */
    cmd_policy_set_allowed(&approval->policy, CMD_CRITICAL);

    /* Approved (write) */
    int approved = chat_approval_add(approval, "systemctl restart nginx",
                                     CMD_PLATFORM_LINUX);
    chat_approval_approve(approval, approved);

    /* Denied (critical) */
    int denied = chat_approval_add(approval, "docker rm -f web_old",
                                   CMD_PLATFORM_LINUX);
    chat_approval_deny(approval, denied);

    /* Pending (read) -- the ceiling doesn't matter for a read command */
    chat_approval_add(approval, "ls -la /var/log", CMD_PLATFORM_LINUX);

    /* Blocked (critical) -- lower the ceiling so this row is held */
    cmd_policy_set_allowed(&approval->policy, CMD_READ);
    chat_approval_add(approval, "rm -rf /var/log/old", CMD_PLATFORM_LINUX);
}

/* ---------------------------------------------------------------------
 * "executing": one COMPLETED command, one EXECUTING command. Both are
 * approved first (matching how a live batch reaches either state) so
 * only the *_add / approve / set_* API is used.
 * --------------------------------------------------------------------- */
static const char EXECUTING_USER_MSG[] =
    "Update the package list and then restart nginx.";

static const char EXECUTING_ASSISTANT_MSG[] =
    "Running those now -- one moment.";

static void build_executing(AiConversation *conv, ApprovalQueue *approval)
{
    if (conv) {
        ai_conv_add(conv, AI_ROLE_USER, EXECUTING_USER_MSG);
        ai_conv_add(conv, AI_ROLE_ASSISTANT, EXECUTING_ASSISTANT_MSG);
    }
    if (!approval) return;

    /* Raise the ceiling to Critical so both rows land PENDING and can be
     * approved by hand, regardless of where an earlier build_* call left
     * the queue's policy. */
    cmd_policy_set_allowed(&approval->policy, CMD_CRITICAL);

    int completed = chat_approval_add(approval, "apt-get update",
                                      CMD_PLATFORM_LINUX);
    chat_approval_approve(approval, completed);
    chat_approval_set_executing(approval, completed);
    chat_approval_set_completed(approval, completed);

    int executing = chat_approval_add(approval, "systemctl restart nginx",
                                      CMD_PLATFORM_LINUX);
    chat_approval_approve(approval, executing);
    chat_approval_set_executing(approval, executing);
}

/* ---------------------------------------------------------------------
 * "tool": a web_search tool call, and a truncated tool result. The
 * win32 replay (ai_chat.c chat_rebuild_display) shows the "using tool:"
 * line for the call and a preview + "truncated to 1MB" note for a result
 * whose content is long enough that the 200-char preview actually cuts
 * it off.
 * --------------------------------------------------------------------- */
static const char TOOL_USER_MSG[] =
    "Why are we hitting API rate limits again? Look it up.";

static const char TOOL_RESULT_CONTENT[] =
    "Rate limit dashboard (api.example.internal) -- 3 keys tracked.\n"
    "\n"
    "key-prod-1: 12,400 / 15,000 requests this hour (82%)\n"
    "key-prod-2: 4,102 / 15,000 (27%)\n"
    "key-batch:  15,000 / 15,000 (100%) -- THROTTLED since 08:52 UTC\n"
    "\n"
    "Recommend rotating key-batch or raising its quota before the "
    "nightly batch job at 02:00 UTC. Full response payload continues "
    "with per-endpoint breakdowns and a 30-day trend chart.";

static const char TOOL_SUMMARY_MSG[] =
    "Your `key-batch` API key hit its hourly limit at **08:52 UTC** and "
    "is currently throttled; `key-prod-1` is close behind at 82%.\n"
    "\n"
    "Want me to raise the quota or spread `key-batch` traffic across a "
    "second key?";

static void build_tool(AiConversation *conv)
{
    if (!conv) return;
    ai_conv_add(conv, AI_ROLE_USER, TOOL_USER_MSG);

    AiToolCall call;
    memset(&call, 0, sizeof(call));
    snprintf(call.id, sizeof(call.id), "toolu_01demo");
    snprintf(call.name, sizeof(call.name), "web_search");
    snprintf(call.input_json, sizeof(call.input_json),
             "{\"query\":\"api rate limit dashboard\"}");
    agentic_add_assistant_tool_msg(conv, "", &call, 1);

    AiToolResult result;
    memset(&result, 0, sizeof(result));
    snprintf(result.tool_use_id, sizeof(result.tool_use_id), "%s", call.id);
    result.content_len = sizeof(TOOL_RESULT_CONTENT) - 1;
    result.content = malloc(result.content_len + 1);
    if (result.content) {
        memcpy(result.content, TOOL_RESULT_CONTENT, result.content_len + 1);
        result.was_truncated = 1;
        agentic_add_tool_results(conv, &call, &result, 1);
        free(result.content);
    }

    ai_conv_add(conv, AI_ROLE_ASSISTANT, TOOL_SUMMARY_MSG);
}

/* ---------------------------------------------------------------------
 * "error": a request that failed (HTTP error, shown with a Retry link by
 * the win32 side via ActivityState) and a second one the user cancelled
 * mid-stream. Both are user turns with no assistant reply, since neither
 * attempt produced one.
 * --------------------------------------------------------------------- */
static const char ERROR_USER_MSG_1[] =
    "Check the deploy webhook status.";

static const char ERROR_USER_MSG_2[] =
    "Actually never mind, I'll check it myself -- stop.";

static void build_error(AiConversation *conv)
{
    if (!conv) return;
    ai_conv_add(conv, AI_ROLE_USER, ERROR_USER_MSG_1);
    ai_conv_add(conv, AI_ROLE_USER, ERROR_USER_MSG_2);
}

/* ---------------------------------------------------------------------
 * "batches": two independent pending command cards from two separate
 * assistant replies, with a user turn between them so the transcript
 * shows the interleaving (docs/superpowers/specs/
 * 2026-09-09-pending-command-batches.md). Batch 1 reuses "approval"'s
 * pending/blocked pair (ls -la /var/log, rm -rf /var/log/old); batch 2 is
 * a fresh pending/blocked pair from the second reply.
 * --------------------------------------------------------------------- */
static const char BATCH1_USER_MSG[] =
    "Check the log directory and clear out anything stale.";

static const char BATCH1_ASSISTANT_MSG[] =
    "Here's what I'd like to run for that:";

static const char BATCH_INTERLEAVE_USER_MSG[] =
    "Also -- while that's pending, can you check nginx and update packages?";

static const char BATCH2_ASSISTANT_MSG[] =
    "Sure, here's a second batch for that:";

static void build_batches(AiConversation *conv, ApprovalQueue *approval,
                          ApprovalQueue *approval2)
{
    if (conv) {
        ai_conv_add(conv, AI_ROLE_USER, BATCH1_USER_MSG);
        ai_conv_add(conv, AI_ROLE_ASSISTANT, BATCH1_ASSISTANT_MSG);
    }
    if (approval) {
        /* Pending (read) -- the ceiling doesn't matter for a read command */
        chat_approval_add(approval, "ls -la /var/log", CMD_PLATFORM_LINUX);
        /* Blocked (critical) -- lower the ceiling so this row is held,
         * regardless of where an earlier build_* call left it. */
        cmd_policy_set_allowed(&approval->policy, CMD_READ);
        chat_approval_add(approval, "rm -rf /var/log/old", CMD_PLATFORM_LINUX);
    }

    if (conv) {
        ai_conv_add(conv, AI_ROLE_USER, BATCH_INTERLEAVE_USER_MSG);
        ai_conv_add(conv, AI_ROLE_ASSISTANT, BATCH2_ASSISTANT_MSG);
    }
    if (approval2) {
        /* approval2 is freshly initialized (chat_approval_init) with the
         * default policy (allowed = Read, unattended = none), which is
         * already the ceiling both rows below need. */

        /* Pending (read) */
        chat_approval_add(approval2, "systemctl status nginx", CMD_PLATFORM_LINUX);
        /* Blocked (write) -- above the default Read ceiling */
        chat_approval_add(approval2, "sudo apt update", CMD_PLATFORM_LINUX);
    }
}

/* ---------------------------------------------------------------------
 * Terminal transcript: ~30 lines of plausible shell output ending at a
 * bare prompt, fed through term_process() by the win32 side.
 * --------------------------------------------------------------------- */
static const char DEMO_TERM_TEXT[] =
    "Last login: Sun Sep  6 09:12:44 2026 from 10.0.4.18\r\n"
    "web-01:~$ uname -a\r\n"
    "Linux web-01 6.8.0-45-generic #45-Ubuntu SMP x86_64 GNU/Linux\r\n"
    "web-01:~$ uptime\r\n"
    " 09:14:02 up 42 days,  3:11,  2 users,  load average: 0.18, 0.22, 0.19\r\n"
    "web-01:~$ df -h\r\n"
    "Filesystem      Size  Used Avail Use% Mounted on\r\n"
    "/dev/sda1        80G   34G   46G  43% /\r\n"
    "/dev/sdb1       120G   85G   35G  71% /var\r\n"
    "web-01:~$ systemctl status nginx\r\n"
    "\xe2\x97\x8f nginx.service - A high performance web server\r\n"
    "     Loaded: loaded (/lib/systemd/system/nginx.service; enabled)\r\n"
    "     Active: active (running) since Mon 2026-08-25 04:02:11 UTC\r\n"
    "   Main PID: 1142 (nginx)\r\n"
    "      Tasks: 5 (limit: 4915)\r\n"
    "     Memory: 12.4M\r\n"
    "web-01:~$ tail -n 5 /var/log/nginx/access.log\r\n"
    "10.0.4.9 - - [06/Sep/2026:09:10:02] \"GET /health HTTP/1.1\" 200 12\r\n"
    "10.0.4.9 - - [06/Sep/2026:09:10:12] \"GET /health HTTP/1.1\" 200 12\r\n"
    "10.0.4.22 - - [06/Sep/2026:09:11:45] \"POST /api/v1/orders HTTP/1.1\" 201 340\r\n"
    "10.0.4.9 - - [06/Sep/2026:09:12:02] \"GET /health HTTP/1.1\" 200 12\r\n"
    "10.0.4.31 - - [06/Sep/2026:09:12:58] \"GET /api/v1/orders?limit=20 HTTP/1.1\" 200 5120\r\n"
    "web-01:~$ free -h\r\n"
    "              total        used        free      shared  buff/cache   available\r\n"
    "Mem:           7.8G        2.1G        1.2G        128M        4.5G        5.3G\r\n"
    "Swap:          2.0G          0B        2.0G\r\n"
    "web-01:~$ ps aux --sort=-%cpu | head -3\r\n"
    "USER       PID %CPU %MEM    VSZ   RSS TTY      STAT START   TIME COMMAND\r\n"
    "www-data  1142  2.1  1.4 231232 54212 ?        Ssl  Aug25   3:14 nginx: master\r\n"
    "postgres  2044  1.3  3.2 512004 98120 ?        Ss   Aug25   9:02 postgres\r\n"
    "web-01:~$ ";

/* ---------------------------------------------------------------------
 * "local": the same window with no remote host at all -- PowerShell on
 * this PC, the automatic local-shell search's first pick since busybox was
 * removed as an option (2026-09-24; spec 2026-09-22-local-shell-design.md
 * sections 5 and 9). It changes only the terminal transcript; the tab
 * chrome and status line are the win32 side's doing (create_demo_session in
 * window.c) and there is no process behind either. The conversation is left
 * empty on purpose: this state is about the terminal and the tab, not the
 * panel.
 * --------------------------------------------------------------------- */
static const char LOCAL_TERM_TEXT[] =
    "Nutshell local shell -- PowerShell 7, no SSH session.\r\n"
    "\r\n"
    "PS C:\\Users\\thomas> Get-ChildItem\r\n"
    "\r\n"
    "    Directory: C:\\Users\\thomas\r\n"
    "\r\n"
    "Mode                 LastWriteTime         Length Name\r\n"
    "----                 -------------         ------ ----\r\n"
    "d-----         9/21/2026   6:02 PM                Desktop\r\n"
    "d-----         9/19/2026   9:44 AM                Documents\r\n"
    "d-----         9/22/2026   7:15 AM                Downloads\r\n"
    "-a----         9/22/2026   7:51 AM           4096 notes.md\r\n"
    "\r\n"
    "PS C:\\Users\\thomas> (Select-String notes.md -Pattern TODO).Count\r\n"
    "7\r\n"
    "PS C:\\Users\\thomas> ";

/* ---------------------------------------------------------------------
 * Entry point
 * --------------------------------------------------------------------- */
int ui_demo_build(const char *state, AiConversation *conv,
                  ApprovalQueue *approval, ApprovalQueue *approval2,
                  char *term_text, size_t term_cap)
{
    if (!ui_demo_state_valid(state)) return -1;

    if (conv) {
        ai_conv_init(conv, "demo");
        ai_conv_add(conv, AI_ROLE_SYSTEM, DEMO_SYSTEM_PROMPT);
    }
    if (approval) {
        chat_approval_init(approval);
    }
    if (approval2) {
        chat_approval_init(approval2);
    }
    /* "local" swaps the transcript for a local-shell one; "all" keeps the
     * SSH transcript, since it is the union of the panel states and the
     * local state adds nothing to the conversation. */
    if (term_text && term_cap > 0) {
        (void)snprintf(term_text, term_cap, "%s",
                       (strcmp(state, "local") == 0) ? LOCAL_TERM_TEXT
                                                     : DEMO_TERM_TEXT);
    }

    int is_all = strcmp(state, "all") == 0;

    if (is_all || strcmp(state, "chat") == 0)
        build_chat(conv);
    /* "thinking" reuses "chat"'s own turns -- the only difference is which
     * thinking text ai_chat_apply_demo_extras() (src/ui/ai_chat.c) attaches
     * to the reply afterwards. Deliberately not part of "all": it exists
     * only to demo the 50-line scrollable box, which "all" doesn't need a
     * second copy of. */
    if (strcmp(state, "thinking") == 0)
        build_chat(conv);
    if (is_all || strcmp(state, "approval") == 0)
        build_approval(conv, approval);
    if (is_all || strcmp(state, "executing") == 0)
        build_executing(conv, approval);
    if (is_all || strcmp(state, "tool") == 0)
        build_tool(conv);
    if (is_all || strcmp(state, "error") == 0)
        build_error(conv);
    if (is_all || strcmp(state, "batches") == 0)
        build_batches(conv, approval, approval2);
    /* "empty", "nokey" and "nosession" add nothing beyond the system
     * message -- their distinct look comes entirely from the forced
     * AiPanelStateId the win32 side applies via ai_chat_force_state()
     * (see ai_chat_apply_demo_extras() in ai_chat.c). */

    return 0;
}
