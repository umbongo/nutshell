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
    "chat", "approval", "executing", "tool", "error", "empty", "all"
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

    /* Approved (write) */
    int approved = chat_approval_add(approval, "systemctl restart nginx",
                                     CMD_PLATFORM_LINUX, 1);
    chat_approval_approve(approval, approved);

    /* Denied (critical) */
    int denied = chat_approval_add(approval, "docker rm -f web_old",
                                   CMD_PLATFORM_LINUX, 1);
    chat_approval_deny(approval, denied);

    /* Pending (safe) -- permit_write doesn't matter for a safe command */
    chat_approval_add(approval, "ls -la /var/log", CMD_PLATFORM_LINUX, 1);

    /* Blocked (critical, permit_write off) */
    chat_approval_add(approval, "rm -rf /var/log/old", CMD_PLATFORM_LINUX, 0);
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

    int completed = chat_approval_add(approval, "apt-get update",
                                      CMD_PLATFORM_LINUX, 1);
    chat_approval_approve(approval, completed);
    chat_approval_set_executing(approval, completed);
    chat_approval_set_completed(approval, completed);

    int executing = chat_approval_add(approval, "systemctl restart nginx",
                                      CMD_PLATFORM_LINUX, 1);
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
 * Entry point
 * --------------------------------------------------------------------- */
int ui_demo_build(const char *state, AiConversation *conv,
                  ApprovalQueue *approval, char *term_text, size_t term_cap)
{
    if (!ui_demo_state_valid(state)) return -1;

    if (conv) {
        ai_conv_init(conv, "demo");
        ai_conv_add(conv, AI_ROLE_SYSTEM, DEMO_SYSTEM_PROMPT);
    }
    if (approval) {
        chat_approval_init(approval);
    }
    if (term_text && term_cap > 0) {
        (void)snprintf(term_text, term_cap, "%s", DEMO_TERM_TEXT);
    }

    int is_all = strcmp(state, "all") == 0;

    if (is_all || strcmp(state, "chat") == 0)
        build_chat(conv);
    if (is_all || strcmp(state, "approval") == 0)
        build_approval(conv, approval);
    if (is_all || strcmp(state, "executing") == 0)
        build_executing(conv, approval);
    if (is_all || strcmp(state, "tool") == 0)
        build_tool(conv);
    if (is_all || strcmp(state, "error") == 0)
        build_error(conv);
    /* "empty" adds nothing beyond the system message. */

    return 0;
}
