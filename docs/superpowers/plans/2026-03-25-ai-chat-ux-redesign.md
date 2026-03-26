# AI Chat Panel UX Redesign — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the RichEdit-based AI chat display with a custom owner-drawn scrollable message list featuring inline command approval, collapsible thinking regions, and activity monitoring.

**Architecture:** New portable C modules (cmd_classify, chat_msg, chat_thinking, chat_activity, chat_approval) with full test coverage, plus Win32 UI modules (chat_listview, md_render) that integrate into the existing ai_chat.c window. All portable modules are TDD'd on Linux. The migration follows 6 phases: portable modules first, then UI build-out, then integration.

**Tech Stack:** C11, Win32 GDI (owner-draw), MinGW cross-compilation, custom test framework (test_framework.h)

**Spec:** `docs/superpowers/specs/2026-03-25-ai-chat-ux-redesign-design.md`

**Known deferral:** Accessibility (IAccessible / UI Automation) is a known regression from moving away from RichEdit. Deferred to a future phase per spec section 3.5. Do not attempt to add it in scope.

---

## Phase 1: Portable Modules (No UI Changes)

### Task 1: Command Safety Classifier — Types and API

**Files:**
- Create: `src/core/cmd_classify.h`
- Create: `src/core/cmd_classify.c`
- Create: `tests/test_cmd_classify.c`
- Modify: `tests/runner.c` (add forward declarations and calls)

This task sets up the classifier's public API, enums, and a minimal implementation for Linux safe commands only. Subsequent tasks add platform-specific rules incrementally.

- [ ] **Step 1: Write the header file**

```c
/* src/core/cmd_classify.h */
#ifndef NUTSHELL_CMD_CLASSIFY_H
#define NUTSHELL_CMD_CLASSIFY_H

#include <stddef.h>

typedef enum {
    CMD_SAFE,       /* Read-only, always allowed */
    CMD_WRITE,      /* Modifies state, requires permit_write */
    CMD_CRITICAL    /* Can cause outage/data loss, requires permit_write + visual warning */
} CmdSafetyLevel;

typedef enum {
    CMD_PLATFORM_LINUX,
    CMD_PLATFORM_CISCO_IOS,
    CMD_PLATFORM_CISCO_NXOS,
    CMD_PLATFORM_CISCO_ASA,
    CMD_PLATFORM_ARUBA_CX,
    CMD_PLATFORM_ARUBA_OS,
    CMD_PLATFORM_PANOS
} CmdPlatform;

/* Classify a single command string.
 * Returns CMD_SAFE for NULL or empty input.
 * For pipelines/semicolons, returns the highest risk level across all segments. */
CmdSafetyLevel cmd_classify(const char *command, CmdPlatform platform);

/* Classify with detail: fills reason buffer with human-readable explanation.
 * reason_buf may be NULL. Returns safety level. */
CmdSafetyLevel cmd_classify_ex(const char *command, CmdPlatform platform,
                                char *reason_buf, size_t reason_buf_size);

#endif /* NUTSHELL_CMD_CLASSIFY_H */
```

- [ ] **Step 2: Write initial failing tests for Linux safe/write/critical commands**

```c
/* tests/test_cmd_classify.c */
#include "test_framework.h"
#include "cmd_classify.h"
#include <string.h>

/* --- NULL and empty input --- */
int test_cmd_classify_null(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify(NULL, CMD_PLATFORM_LINUX), (int)CMD_SAFE);
    TEST_END();
}

int test_cmd_classify_empty(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("", CMD_PLATFORM_LINUX), (int)CMD_SAFE);
    TEST_END();
}

/* --- Linux safe commands --- */
int test_cmd_classify_linux_ls(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("ls -la /etc", CMD_PLATFORM_LINUX), (int)CMD_SAFE);
    TEST_END();
}

int test_cmd_classify_linux_cat(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("cat /etc/hostname", CMD_PLATFORM_LINUX), (int)CMD_SAFE);
    TEST_END();
}

int test_cmd_classify_linux_grep(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("grep -r 'foo' /var/log", CMD_PLATFORM_LINUX), (int)CMD_SAFE);
    TEST_END();
}

int test_cmd_classify_linux_ping(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("ping -c 4 8.8.8.8", CMD_PLATFORM_LINUX), (int)CMD_SAFE);
    TEST_END();
}

/* --- Linux write commands --- */
int test_cmd_classify_linux_mv(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("mv foo.txt bar.txt", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    TEST_END();
}

int test_cmd_classify_linux_cp(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("cp a.conf a.bak", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    TEST_END();
}

int test_cmd_classify_linux_mkdir(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("mkdir -p /tmp/build", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    TEST_END();
}

int test_cmd_classify_linux_chmod(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("chmod 755 script.sh", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    TEST_END();
}

int test_cmd_classify_linux_apt_install(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("apt install nginx", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    TEST_END();
}

int test_cmd_classify_linux_git_push(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("git push origin main", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    TEST_END();
}

int test_cmd_classify_linux_systemctl_start(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("systemctl start nginx", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    TEST_END();
}

int test_cmd_classify_linux_docker_run(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("docker run -d nginx", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    TEST_END();
}

/* --- Linux critical commands --- */
int test_cmd_classify_linux_rm(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("rm -rf /tmp/build", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_linux_reboot(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("reboot", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_linux_shutdown(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("shutdown -h now", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_linux_kill9(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("kill -9 1234", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_linux_killall(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("killall nginx", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_linux_dd(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("dd if=/dev/zero of=/dev/sda", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_linux_mkfs(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("mkfs.ext4 /dev/sdb1", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_linux_iptables_flush(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("iptables -F", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_linux_systemctl_stop(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("systemctl stop nginx", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_linux_docker_rm_f(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("docker rm -f container1", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_linux_kubectl_delete(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("kubectl delete pod mypod", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}
```

- [ ] **Step 3: Run tests to verify they fail**

Run: `make test 2>&1 | tail -20`
Expected: Linker errors — `cmd_classify` and `cmd_classify_ex` undefined

- [ ] **Step 4: Write the initial cmd_classify.c with Linux rules**

```c
/* src/core/cmd_classify.c */
#include "cmd_classify.h"
#include <string.h>
#include <ctype.h>

/* ----- Linux command tables ----- */

static const char *linux_critical_cmds[] = {
    "rm", "shred", "truncate", "dd", "mkfs", "wipefs",
    "reboot", "shutdown", "poweroff", "halt",
    "killall", "pkill",
    "fdisk", "gdisk", "parted", "lvremove", "vgremove", "pvremove",
    NULL
};

static const char *linux_critical_prefixes[] = {
    "mkfs.",   /* mkfs.ext4, mkfs.xfs, etc. */
    NULL
};

static const char *linux_write_cmds[] = {
    "mv", "cp", "mkdir", "chmod", "chown", "ln", "touch", "rsync",
    "apt", "apt-get", "yum", "dnf", "pacman", "pip", "pip3", "npm", "yarn",
    "useradd", "usermod", "userdel", "passwd", "groupadd",
    "make", "gcc", "cmake", "cargo", "go",
    "git", "svn",
    "docker", "kubectl",
    "crontab",
    "tar", "zip", "unzip", "gzip",
    "wget", "curl",
    "firewall-cmd",
    NULL
};

/* Subcommand-sensitive: cmd + subcommand -> level */
typedef struct {
    const char *cmd;
    const char *subcmd;
    CmdSafetyLevel level;
} SubcmdRule;

static const SubcmdRule linux_subcmd_rules[] = {
    /* systemctl */
    { "systemctl", "stop",    CMD_CRITICAL },
    { "systemctl", "disable", CMD_CRITICAL },
    { "systemctl", "mask",    CMD_CRITICAL },
    { "systemctl", "start",   CMD_WRITE },
    { "systemctl", "restart", CMD_WRITE },
    { "systemctl", "reload",  CMD_WRITE },
    { "systemctl", "enable",  CMD_WRITE },
    { "systemctl", "status",  CMD_SAFE },
    { "systemctl", "is-active", CMD_SAFE },
    { "systemctl", "is-enabled", CMD_SAFE },
    { "systemctl", "list-units", CMD_SAFE },
    /* kill with -9 is critical, but plain kill is write */
    /* docker subcommands */
    { "docker", "run",    CMD_WRITE },
    { "docker", "build",  CMD_WRITE },
    { "docker", "exec",   CMD_WRITE },
    { "docker", "rm",     CMD_CRITICAL },
    { "docker", "system", CMD_CRITICAL },
    { "docker", "ps",     CMD_SAFE },
    { "docker", "images", CMD_SAFE },
    { "docker", "logs",   CMD_SAFE },
    { "docker", "inspect", CMD_SAFE },
    /* kubectl subcommands */
    { "kubectl", "delete", CMD_CRITICAL },
    { "kubectl", "apply",  CMD_WRITE },
    { "kubectl", "create", CMD_WRITE },
    { "kubectl", "edit",   CMD_WRITE },
    { "kubectl", "patch",  CMD_WRITE },
    { "kubectl", "scale",  CMD_WRITE },
    { "kubectl", "get",    CMD_SAFE },
    { "kubectl", "describe", CMD_SAFE },
    { "kubectl", "logs",   CMD_SAFE },
    /* iptables */
    { "iptables", "-F", CMD_CRITICAL },
    { "iptables", "-P", CMD_CRITICAL },
    { "iptables", "-A", CMD_WRITE },
    { "iptables", "-I", CMD_WRITE },
    { "iptables", "-D", CMD_WRITE },
    { "iptables", "-L", CMD_SAFE },
    { "iptables", "-S", CMD_SAFE },
    /* ip subcommands */
    { "ip", "addr",  CMD_SAFE },  /* default safe, "ip addr add" handled below */
    { "ip", "route", CMD_SAFE },  /* default safe, "ip route del" handled below */
    { "ip", "link",  CMD_SAFE },
    /* git subcommands */
    { "git", "push",   CMD_WRITE },
    { "git", "reset",  CMD_WRITE },
    { "git", "rebase", CMD_WRITE },
    { "git", "merge",  CMD_WRITE },
    { "git", "status", CMD_SAFE },
    { "git", "log",    CMD_SAFE },
    { "git", "diff",   CMD_SAFE },
    { "git", "show",   CMD_SAFE },
    { "git", "branch", CMD_SAFE },
    /* sed */
    { "sed", "-i", CMD_WRITE },
    /* curl/wget with output */
    { "curl", "-o", CMD_WRITE },
    { "curl", "-O", CMD_WRITE },
    { "wget", "-O", CMD_WRITE },
    { NULL, NULL, CMD_SAFE }
};

/* ip subcommand + action rules (3-token sensitivity) */
typedef struct {
    const char *cmd;
    const char *sub1;
    const char *sub2;
    CmdSafetyLevel level;
} ThreeTokenRule;

static const ThreeTokenRule linux_3token_rules[] = {
    { "ip", "link",  "set",   CMD_CRITICAL },  /* ip link set * down */
    { "ip", "route", "del",   CMD_CRITICAL },
    { "ip", "route", "flush", CMD_CRITICAL },
    { "ip", "addr",  "add",   CMD_WRITE },
    { "ip", "addr",  "del",   CMD_WRITE },
    { "ip", "route", "add",   CMD_WRITE },
    { NULL, NULL, NULL, CMD_SAFE }
};

/* kill with -9 flag is critical, plain kill is write */
static const char *kill_critical_flags[] = { "-9", "-KILL", "-SIGKILL", NULL };

/* ----- Token extraction helpers ----- */

/* Extract the next whitespace-delimited token starting at *p.
 * Sets *start and *len. Advances *p past the token and trailing whitespace.
 * Returns 1 if a token was found, 0 if end of segment. */
static int next_token(const char **p, const char **start, size_t *len)
{
    while (**p == ' ' || **p == '\t') (*p)++;
    if (!**p || **p == '|' || **p == ';' || **p == '&') return 0;
    *start = *p;
    while (**p && **p != ' ' && **p != '\t' && **p != '|'
           && **p != ';' && **p != '&' && **p != '>' && **p != '<')
        (*p)++;
    *len = (size_t)(*p - *start);
    return *len > 0;
}

/* Strip path prefix: "/usr/bin/rm" -> "rm" */
static const char *strip_path(const char *tok, size_t len, size_t *out_len)
{
    const char *base = tok;
    for (size_t i = 0; i < len; i++) {
        if (tok[i] == '/') base = tok + i + 1;
    }
    *out_len = (size_t)((tok + len) - base);
    return base;
}

/* Portable case-insensitive single-char comparison. */
static int ci_lower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

/* Portable case-insensitive memcmp (avoids strncasecmp portability issues). */
static int ci_memcmp(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        int ca = ci_lower((unsigned char)a[i]);
        int cb = ci_lower((unsigned char)b[i]);
        if (ca != cb) return ca - cb;
    }
    return 0;
}

/* Case-sensitive match of tok (length tlen) against a fixed string. */
static int tok_eq(const char *tok, size_t tlen, const char *lit)
{
    size_t llen = strlen(lit);
    if (tlen != llen) return 0;
    return memcmp(tok, lit, tlen) == 0;
}

/* Case-insensitive match (for network device platforms). */
static int tok_eq_ci(const char *tok, size_t tlen, const char *lit)
{
    size_t llen = strlen(lit);
    if (tlen != llen) return 0;
    return ci_memcmp(tok, lit, tlen) == 0;
}

/* Case-sensitive prefix match. */
static int tok_prefix(const char *tok, size_t tlen, const char *prefix)
{
    size_t plen = strlen(prefix);
    if (tlen < plen) return 0;
    return memcmp(tok, prefix, plen) == 0;
}

/* Case-insensitive prefix match (for network device platforms). */
static int tok_prefix_ci(const char *tok, size_t tlen, const char *prefix)
{
    size_t plen = strlen(prefix);
    if (tlen < plen) return 0;
    return ci_memcmp(tok, prefix, plen) == 0;
}

/* Check if a token matches any string in a NULL-terminated list. */
static int tok_in_list(const char *tok, size_t tlen, const char **list)
{
    for (int i = 0; list[i]; i++) {
        if (tok_eq(tok, tlen, list[i])) return 1;
    }
    return 0;
}

/* Check if a token matches any prefix in a NULL-terminated list. */
static int tok_has_prefix(const char *tok, size_t tlen, const char **list)
{
    for (int i = 0; list[i]; i++) {
        if (tok_prefix(tok, tlen, list[i])) return 1;
    }
    return 0;
}

/* ----- Redirect scanning ----- */

/* Scan for shell redirects in a command segment. Returns CMD_WRITE if a real
 * file redirect is found, CMD_SAFE otherwise. Skips quoted strings. */
static CmdSafetyLevel scan_redirects(const char *seg, size_t seg_len)
{
    const char *end = seg + seg_len;
    for (const char *p = seg; p < end; p++) {
        /* Skip quoted strings */
        if (*p == '\'' || *p == '"') {
            char q = *p++;
            while (p < end && *p != q) p++;
            if (p >= end) break;
            continue;
        }
        if (*p == '>' || (*p == '&' && (p + 1) < end && *(p + 1) == '>')) {
            const char *r = p;
            if (*r == '&') r++;
            /* Check for 2> prefix */
            if (r > seg && *(r - 1) == '2') { /* 2> or 2>> */ }
            r++; /* skip > */
            if (r < end && *r == '>') r++; /* skip >> */
            while (r < end && (*r == ' ' || *r == '\t')) r++;
            /* Allow /dev/null */
            if (r + 9 <= end && memcmp(r, "/dev/null", 9) == 0)
                { p = r + 8; continue; }
            /* Allow &1 and &2 */
            if (r + 2 <= end && *r == '&' && (*(r+1) == '1' || *(r+1) == '2'))
                { p = r + 1; continue; }
            return CMD_WRITE;
        }
    }
    return CMD_SAFE;
}

/* ----- Pipe-to-dangerous scanning ----- */

/* Check if a pipe destination is dangerous: | xargs rm, | sh, | bash,
 * | sudo tee /etc/... */
static CmdSafetyLevel scan_pipe_target(const char *seg)
{
    const char *p = seg;
    const char *tok_start;
    size_t tok_len;

    if (!next_token(&p, &tok_start, &tok_len)) return CMD_SAFE;

    const char *base;
    size_t base_len;
    base = strip_path(tok_start, tok_len, &base_len);

    /* | sh, | bash */
    if (tok_eq(base, base_len, "sh") || tok_eq(base, base_len, "bash"))
        return CMD_CRITICAL;

    /* | xargs rm -> critical */
    if (tok_eq(base, base_len, "xargs")) {
        const char *next_start;
        size_t next_len;
        if (next_token(&p, &next_start, &next_len)) {
            const char *nb;
            size_t nbl;
            nb = strip_path(next_start, next_len, &nbl);
            if (tok_in_list(nb, nbl, linux_critical_cmds))
                return CMD_CRITICAL;
        }
        return CMD_WRITE;
    }

    /* | sudo tee /etc/... -> critical */
    if (tok_eq(base, base_len, "sudo")) {
        const char *next_start;
        size_t next_len;
        if (next_token(&p, &next_start, &next_len)) {
            if (tok_eq(next_start, next_len, "tee"))
                return CMD_CRITICAL;
        }
    }

    return CMD_SAFE;
}

/* ----- SQL in database CLI detection ----- */

static int is_db_cli(const char *tok, size_t len)
{
    return tok_eq(tok, len, "mysql") || tok_eq(tok, len, "psql")
        || tok_eq(tok, len, "mongo") || tok_eq(tok, len, "mongosh")
        || tok_eq(tok, len, "redis-cli") || tok_eq(tok, len, "sqlite3");
}

/* Scan for -e "DROP/DELETE/TRUNCATE" in a database CLI invocation. */
static CmdSafetyLevel scan_db_cli_args(const char *p)
{
    /* Look for -e flag followed by a quoted SQL string */
    while (*p && *p != '|' && *p != ';') {
        if ((*p == '-' && *(p+1) == 'e') || (*p == '-' && *(p+1) == '-')) {
            /* Skip to the SQL content */
            p += 2;
            while (*p == ' ' || *p == '\t') p++;
            /* Check for destructive SQL keywords (case-insensitive, portable) */
            const char *sql = p;
            /* Scan the rest of this segment for keywords */
            while (*sql && *sql != '|' && *sql != ';') {
                if ((sql == p || *(sql-1) == ' ' || *(sql-1) == '"' || *(sql-1) == '\'') &&
                    (ci_memcmp(sql, "DROP", 4) == 0 ||
                     ci_memcmp(sql, "DELETE", 6) == 0 ||
                     ci_memcmp(sql, "TRUNCATE", 8) == 0))
                    return CMD_CRITICAL;
                if (ci_memcmp(sql, "SELECT", 6) == 0 ||
                    ci_memcmp(sql, "SHOW", 4) == 0 ||
                    ci_memcmp(sql, "DESCRIBE", 8) == 0)
                    return CMD_SAFE;
                sql++;
            }
            return CMD_WRITE; /* -e with unknown SQL -> write */
        }
        p++;
    }
    return CMD_SAFE; /* no -e flag, interactive session -> safe to open */
}

/* ----- Per-segment Linux classification ----- */

static CmdSafetyLevel classify_linux_segment(const char *seg, size_t seg_len,
                                              char *reason_buf, size_t reason_buf_size)
{
    const char *p = seg;
    const char *seg_end = seg + seg_len;
    const char *tok1_start, *tok2_start, *tok3_start;
    size_t tok1_len, tok2_len, tok3_len;
    int has_sudo = 0;

    (void)seg_end; /* used implicitly via null check in next_token */

    /* Check redirects first */
    CmdSafetyLevel redir = scan_redirects(seg, seg_len);

    /* Extract first token */
    if (!next_token(&p, &tok1_start, &tok1_len))
        return redir; /* empty segment */

    const char *base1;
    size_t base1_len;
    base1 = strip_path(tok1_start, tok1_len, &base1_len);

    /* Handle sudo/su prefix: escalate the following command */
    if (tok_eq(base1, base1_len, "sudo") || tok_eq(base1, base1_len, "su")
        || tok_eq(base1, base1_len, "doas")) {
        has_sudo = 1;
        /* Skip sudo flags like -u user */
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '-') {
            /* Skip flag and its argument */
            while (*p && *p != ' ' && *p != '\t') p++;
            while (*p == ' ' || *p == '\t') p++;
            /* Skip the argument to -u */
            while (*p && *p != ' ' && *p != '\t') p++;
        }
        if (!next_token(&p, &tok1_start, &tok1_len)) {
            /* "sudo" alone */
            CmdSafetyLevel level = CMD_WRITE;
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size, "sudo/su escalation");
            return level > redir ? level : redir;
        }
        base1 = strip_path(tok1_start, tok1_len, &base1_len);
    }

    /* init 0 or init 6 -> critical */
    if (tok_eq(base1, base1_len, "init")) {
        const char *arg_start;
        size_t arg_len;
        if (next_token(&p, &arg_start, &arg_len)) {
            if (tok_eq(arg_start, arg_len, "0") || tok_eq(arg_start, arg_len, "6")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "init 0/6: system halt/reboot");
                return CMD_CRITICAL;
            }
        }
    }

    /* Check critical commands */
    if (tok_in_list(base1, base1_len, linux_critical_cmds)) {
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "critical command: %.*s",
                     (int)base1_len, base1);
        return CMD_CRITICAL;
    }

    /* Check critical prefixes (e.g., mkfs.ext4) */
    if (tok_has_prefix(base1, base1_len, linux_critical_prefixes)) {
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "critical prefix: %.*s",
                     (int)base1_len, base1);
        return CMD_CRITICAL;
    }

    /* kill with -9 flag -> critical, plain kill -> write */
    if (tok_eq(base1, base1_len, "kill")) {
        /* Scan remaining tokens for -9 / -KILL / -SIGKILL */
        const char *scan = p;
        const char *ts;
        size_t tl;
        while (next_token(&scan, &ts, &tl)) {
            if (tok_in_list(ts, tl, kill_critical_flags)) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "kill with signal 9");
                return CMD_CRITICAL;
            }
        }
        CmdSafetyLevel level = CMD_WRITE;
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "kill (non-critical signal)");
        return level > redir ? level : redir;
    }

    /* Database CLI with embedded SQL */
    if (is_db_cli(base1, base1_len)) {
        CmdSafetyLevel db_level = scan_db_cli_args(p);
        if (db_level > redir) redir = db_level;
        if (reason_buf && reason_buf_size > 0 && db_level > CMD_SAFE)
            snprintf(reason_buf, reason_buf_size, "destructive SQL via %.*s",
                     (int)base1_len, base1);
        return redir;
    }

    /* Three-token rules (e.g., ip link set, ip route del) */
    {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len)) {
            const char *p3 = p2;
            if (next_token(&p3, &tok3_start, &tok3_len)) {
                for (int i = 0; linux_3token_rules[i].cmd; i++) {
                    const ThreeTokenRule *r = &linux_3token_rules[i];
                    if (tok_eq(base1, base1_len, r->cmd) &&
                        tok_eq(tok2_start, tok2_len, r->sub1) &&
                        tok_eq(tok3_start, tok3_len, r->sub2)) {
                        CmdSafetyLevel level = r->level;
                        if (has_sudo && level < CMD_CRITICAL) level = CMD_WRITE;
                        if (reason_buf && reason_buf_size > 0)
                            snprintf(reason_buf, reason_buf_size, "%s %s %s",
                                     r->cmd, r->sub1, r->sub2);
                        return level > redir ? level : redir;
                    }
                }
            }
        }
    }

    /* Two-token subcommand rules */
    {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len)) {
            for (int i = 0; linux_subcmd_rules[i].cmd; i++) {
                const SubcmdRule *r = &linux_subcmd_rules[i];
                if (tok_eq(base1, base1_len, r->cmd) &&
                    tok_eq(tok2_start, tok2_len, r->subcmd)) {
                    CmdSafetyLevel level = r->level;
                    if (has_sudo && level < CMD_CRITICAL) level = CMD_WRITE;
                    if (reason_buf && reason_buf_size > 0)
                        snprintf(reason_buf, reason_buf_size, "%s %s",
                                 r->cmd, r->subcmd);
                    return level > redir ? level : redir;
                }
            }
        }
    }

    /* Check simple write commands */
    if (tok_in_list(base1, base1_len, linux_write_cmds)) {
        CmdSafetyLevel level = CMD_WRITE;
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "write command: %.*s",
                     (int)base1_len, base1);
        return level > redir ? level : redir;
    }

    /* sed -i, perl -pi -e */
    if (tok_eq(base1, base1_len, "sed") || tok_eq(base1, base1_len, "perl")) {
        const char *scan = p;
        const char *ts;
        size_t tl;
        while (next_token(&scan, &ts, &tl)) {
            if (tok_eq(base1, base1_len, "sed") && tok_eq(ts, tl, "-i")) {
                CmdSafetyLevel level = CMD_WRITE;
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "sed -i: in-place edit");
                return level > redir ? level : redir;
            }
            if (tok_eq(base1, base1_len, "perl") &&
                (tok_eq(ts, tl, "-pi") || tok_eq(ts, tl, "-i"))) {
                CmdSafetyLevel level = CMD_WRITE;
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "perl in-place edit");
                return level > redir ? level : redir;
            }
        }
    }

    /* nft flush -> critical */
    if (tok_eq(base1, base1_len, "nft")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len)) {
            if (tok_eq(tok2_start, tok2_len, "flush")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "nft flush");
                return CMD_CRITICAL;
            }
        }
    }

    /* ufw reset -> critical */
    if (tok_eq(base1, base1_len, "ufw")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len)) {
            if (tok_eq(tok2_start, tok2_len, "reset")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "ufw reset");
                return CMD_CRITICAL;
            }
        }
        return CMD_WRITE; /* other ufw commands are write */
    }

    /* docker system prune -> critical */
    /* (handled by subcmd_rules: docker system -> critical) */

    /* zpool destroy, zfs destroy -> critical */
    if (tok_eq(base1, base1_len, "zpool") || tok_eq(base1, base1_len, "zfs")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len)) {
            if (tok_eq(tok2_start, tok2_len, "destroy")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "%.*s destroy",
                             (int)base1_len, base1);
                return CMD_CRITICAL;
            }
        }
    }

    /* Escalate with sudo */
    if (has_sudo) {
        CmdSafetyLevel level = CMD_WRITE;
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "sudo escalation of %.*s",
                     (int)base1_len, base1);
        return level > redir ? level : redir;
    }

    /* Default: safe (plus any redirect level) */
    return redir;
}

/* ----- Top-level command classification ----- */

/* Split command on |, ;, &&, || and classify each segment.
 * Return the highest risk level. */
CmdSafetyLevel cmd_classify_ex(const char *command, CmdPlatform platform,
                                char *reason_buf, size_t reason_buf_size)
{
    if (!command || !command[0]) {
        if (reason_buf && reason_buf_size > 0)
            reason_buf[0] = '\0';
        return CMD_SAFE;
    }

    CmdSafetyLevel worst = CMD_SAFE;
    const char *p = command;
    int is_pipe_target = 0;

    while (*p) {
        /* Skip whitespace */
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;

        /* Find end of this segment */
        const char *seg_start = p;
        int in_sq = 0, in_dq = 0;
        while (*p) {
            if (*p == '\'' && !in_dq) in_sq = !in_sq;
            else if (*p == '"' && !in_sq) in_dq = !in_dq;
            else if (!in_sq && !in_dq) {
                if (*p == '|' || *p == ';') break;
                if (*p == '&' && *(p+1) == '&') break;
            }
            p++;
        }
        size_t seg_len = (size_t)(p - seg_start);

        CmdSafetyLevel seg_level;

        /* Classify the segment via platform-specific rules */
        switch (platform) {
        case CMD_PLATFORM_LINUX:
        default:
            seg_level = classify_linux_segment(seg_start, seg_len,
                            worst == CMD_SAFE ? reason_buf : NULL,
                            worst == CMD_SAFE ? reason_buf_size : 0);
            break;
        /* Network platform classification added in later tasks */
        }

        /* If this segment is the target of a pipe (e.g., "| sh"),
         * also check for dangerous pipe targets and take the worst. */
        if (is_pipe_target) {
            CmdSafetyLevel pipe_level = scan_pipe_target(seg_start);
            if (pipe_level > seg_level) seg_level = pipe_level;
        }

        if (seg_level > worst) worst = seg_level;
        if (worst == CMD_CRITICAL) return worst; /* short-circuit */

        /* Advance past separator.
         * Single | = pipe (next segment is a pipe target).
         * || = logical OR (not a pipe, next segment is independent).
         * && = logical AND (next segment is independent).
         * ; = sequential (next segment is independent). */
        is_pipe_target = (*p == '|' && *(p+1) != '|');
        if (*p == '|' && *(p+1) == '|') p += 2;
        else if (*p == '&' && *(p+1) == '&') p += 2;
        else if (*p) p++;
    }

    return worst;
}

CmdSafetyLevel cmd_classify(const char *command, CmdPlatform platform)
{
    return cmd_classify_ex(command, platform, NULL, 0);
}
```

- [ ] **Step 5: Wire tests into runner.c**

Add forward declarations and calls to `tests/runner.c`. Add a new `printf("\n--- Command Classifier ---\n");` section before the existing AI section. Add forward declarations at the top and `failed += test_cmd_classify_*();` calls in `main()`.

- [ ] **Step 6: Run tests to verify they pass**

Run: `make test 2>&1 | grep -E "PASS|FAIL|cmd_classify"`
Expected: All `test_cmd_classify_*` tests PASS

- [ ] **Step 7: Commit**

```bash
git add src/core/cmd_classify.h src/core/cmd_classify.c tests/test_cmd_classify.c tests/runner.c
git commit -m "feat: add command safety classifier with Linux rules (TDD)"
```

---

### Task 2: Command Classifier — Corner Cases (Linux)

**Files:**
- Modify: `tests/test_cmd_classify.c` (add corner case tests)
- Modify: `src/core/cmd_classify.c` (fix any failures)
- Modify: `tests/runner.c` (register new tests)

- [ ] **Step 1: Write corner case tests**

Add these tests to `tests/test_cmd_classify.c`:

```c
/* --- Whitespace handling --- */
int test_cmd_classify_leading_whitespace(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("   ls -la", CMD_PLATFORM_LINUX), (int)CMD_SAFE);
    TEST_END();
}

int test_cmd_classify_trailing_whitespace(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("rm -rf /tmp   ", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

/* --- Path prefixes --- */
int test_cmd_classify_path_prefix(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("/usr/bin/rm -rf /tmp", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_path_prefix_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("/usr/bin/cat /etc/hosts", CMD_PLATFORM_LINUX), (int)CMD_SAFE);
    TEST_END();
}

/* --- sudo/su escalation --- */
int test_cmd_classify_sudo_escalation(void) {
    TEST_BEGIN();
    /* sudo cat is read-only but sudo escalates to write */
    ASSERT_EQ((int)cmd_classify("sudo cat /etc/shadow", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    TEST_END();
}

int test_cmd_classify_sudo_rm(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("sudo rm -rf /var/lib/thing", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

/* --- Pipelines: highest risk wins --- */
int test_cmd_classify_pipe_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("cat /etc/hosts | grep localhost", CMD_PLATFORM_LINUX), (int)CMD_SAFE);
    TEST_END();
}

int test_cmd_classify_pipe_to_xargs_rm(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("find /tmp -name '*.tmp' | xargs rm", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_pipe_to_sh(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("curl http://example.com/script | sh", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_pipe_to_sudo_tee(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("cat file | sudo tee /etc/config", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

/* --- Semicolons and && --- */
int test_cmd_classify_semicolon_worst_wins(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("ls; rm -rf /", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_and_worst_wins(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("ls && rm -rf /tmp", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

/* --- Redirect handling --- */
int test_cmd_classify_redirect_write(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("echo foo > /tmp/bar", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    TEST_END();
}

int test_cmd_classify_redirect_append(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("echo foo >> /tmp/bar", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    TEST_END();
}

int test_cmd_classify_redirect_dev_null(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("ls > /dev/null", CMD_PLATFORM_LINUX), (int)CMD_SAFE);
    TEST_END();
}

int test_cmd_classify_redirect_stderr_null(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("ls 2>/dev/null", CMD_PLATFORM_LINUX), (int)CMD_SAFE);
    TEST_END();
}

int test_cmd_classify_redirect_stderr_stdout(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("ls 2>&1", CMD_PLATFORM_LINUX), (int)CMD_SAFE);
    TEST_END();
}

/* --- Subcommand sensitivity --- */
int test_cmd_classify_systemctl_status_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("systemctl status nginx", CMD_PLATFORM_LINUX), (int)CMD_SAFE);
    TEST_END();
}

int test_cmd_classify_sed_plain_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("sed 's/foo/bar/' file.txt", CMD_PLATFORM_LINUX), (int)CMD_SAFE);
    TEST_END();
}

int test_cmd_classify_sed_i_write(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("sed -i 's/foo/bar/' file.txt", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    TEST_END();
}

int test_cmd_classify_curl_plain_safe(void) {
    TEST_BEGIN();
    /* curl without -o is safe (displays to stdout) */
    ASSERT_EQ((int)cmd_classify("curl http://example.com", CMD_PLATFORM_LINUX), (int)CMD_SAFE);
    TEST_END();
}

int test_cmd_classify_curl_o_write(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("curl -o file.tar.gz http://example.com/file", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    TEST_END();
}

/* --- Database CLI --- */
int test_cmd_classify_mysql_select_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("mysql -e \"SELECT * FROM users\"", CMD_PLATFORM_LINUX), (int)CMD_SAFE);
    TEST_END();
}

int test_cmd_classify_mysql_drop_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("mysql -e \"DROP TABLE users\"", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

/* --- ip commands --- */
int test_cmd_classify_ip_route_del_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("ip route del default", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_ip_addr_add_write(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("ip addr add 10.0.0.1/24 dev eth0", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    TEST_END();
}

/* --- cmd_classify_ex with reason --- */
int test_cmd_classify_ex_reason(void) {
    TEST_BEGIN();
    char reason[128] = {0};
    CmdSafetyLevel level = cmd_classify_ex("rm -rf /tmp", CMD_PLATFORM_LINUX, reason, sizeof(reason));
    ASSERT_EQ((int)level, (int)CMD_CRITICAL);
    ASSERT_TRUE(strlen(reason) > 0);
    TEST_END();
}

int test_cmd_classify_ex_null_reason(void) {
    TEST_BEGIN();
    CmdSafetyLevel level = cmd_classify_ex("rm -rf /tmp", CMD_PLATFORM_LINUX, NULL, 0);
    ASSERT_EQ((int)level, (int)CMD_CRITICAL);
    TEST_END();
}
```

- [ ] **Step 2: Register new tests in runner.c and run**

Run: `make test 2>&1 | grep -E "PASS|FAIL|cmd_classify"`
Expected: All PASS. Fix any failures.

- [ ] **Step 3: Commit**

```bash
git add tests/test_cmd_classify.c tests/runner.c src/core/cmd_classify.c
git commit -m "test: add Linux classifier corner cases (redirects, pipes, sudo, subcommands)"
```

---

### Task 3: Command Classifier — Cisco IOS/NX-OS/ASA Platform Rules

**Files:**
- Modify: `src/core/cmd_classify.c` (add Cisco platform classifiers)
- Modify: `tests/test_cmd_classify.c` (add Cisco tests)
- Modify: `tests/runner.c` (register new tests)

- [ ] **Step 1: Write Cisco IOS tests**

```c
/* --- Cisco IOS --- */
int test_cmd_classify_ios_show_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("show ip route", CMD_PLATFORM_CISCO_IOS), (int)CMD_SAFE);
    TEST_END();
}

int test_cmd_classify_ios_ping_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("ping 10.0.0.1", CMD_PLATFORM_CISCO_IOS), (int)CMD_SAFE);
    TEST_END();
}

int test_cmd_classify_ios_enable_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("enable", CMD_PLATFORM_CISCO_IOS), (int)CMD_SAFE);
    TEST_END();
}

int test_cmd_classify_ios_conf_t_write(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("configure terminal", CMD_PLATFORM_CISCO_IOS), (int)CMD_WRITE);
    TEST_END();
}

int test_cmd_classify_ios_write_mem_write(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("write memory", CMD_PLATFORM_CISCO_IOS), (int)CMD_WRITE);
    TEST_END();
}

int test_cmd_classify_ios_ip_address_write(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("ip address 10.0.0.1 255.255.255.0", CMD_PLATFORM_CISCO_IOS), (int)CMD_WRITE);
    TEST_END();
}

int test_cmd_classify_ios_reload_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("reload", CMD_PLATFORM_CISCO_IOS), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_ios_write_erase_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("write erase", CMD_PLATFORM_CISCO_IOS), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_ios_no_router_bgp_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("no router bgp 65000", CMD_PLATFORM_CISCO_IOS), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_ios_clear_bgp_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("clear ip bgp *", CMD_PLATFORM_CISCO_IOS), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_ios_shutdown_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("shutdown", CMD_PLATFORM_CISCO_IOS), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_ios_case_insensitive(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("SHOW ip route", CMD_PLATFORM_CISCO_IOS), (int)CMD_SAFE);
    TEST_END();
}

/* --- Cisco NX-OS (IOS rules plus NX-OS extras) --- */
int test_cmd_classify_nxos_show_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("show vlan", CMD_PLATFORM_CISCO_NXOS), (int)CMD_SAFE);
    TEST_END();
}

int test_cmd_classify_nxos_feature_write(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("feature nv overlay", CMD_PLATFORM_CISCO_NXOS), (int)CMD_WRITE);
    TEST_END();
}

int test_cmd_classify_nxos_no_vpc_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("no vpc domain 100", CMD_PLATFORM_CISCO_NXOS), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_nxos_reload_module_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("reload module 1", CMD_PLATFORM_CISCO_NXOS), (int)CMD_CRITICAL);
    TEST_END();
}

/* --- Cisco ASA --- */
int test_cmd_classify_asa_show_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("show running-config", CMD_PLATFORM_CISCO_ASA), (int)CMD_SAFE);
    TEST_END();
}

int test_cmd_classify_asa_nat_write(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("nat (inside,outside) dynamic interface", CMD_PLATFORM_CISCO_ASA), (int)CMD_WRITE);
    TEST_END();
}

int test_cmd_classify_asa_no_failover_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("no failover", CMD_PLATFORM_CISCO_ASA), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_asa_clear_configure_all_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("clear configure all", CMD_PLATFORM_CISCO_ASA), (int)CMD_CRITICAL);
    TEST_END();
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `make test 2>&1 | grep "FAIL"`
Expected: All Cisco tests FAIL (only Linux classifier exists)

- [ ] **Step 3: Implement Cisco IOS/NX-OS/ASA classifiers**

Add `classify_cisco_ios_segment()`, `classify_cisco_nxos_segment()`, and `classify_cisco_asa_segment()` functions to `cmd_classify.c`. Wire them into the `switch (platform)` in `cmd_classify_ex()`. Follow the spec tables in sections 7.4-7.6. Key patterns:
- **IMPORTANT:** All network platform classifiers must use `tok_eq_ci()` and `tok_prefix_ci()` instead of `tok_eq()`/`tok_prefix()` — network device commands are case-insensitive (spec section 7.10)
- All `show` commands -> `CMD_SAFE` (case-insensitive)
- `ping`, `traceroute`, `terminal`, `enable`, `disable`, `exit`, `end`, `dir`, `verify` -> `CMD_SAFE`
- `no` prefix -> at minimum `CMD_WRITE`, check if subcommand is critical
- `clear` prefix -> at minimum `CMD_WRITE`, specific clears are critical
- Cross-platform heuristics from spec section 7.10

- [ ] **Step 4: Run tests, verify all pass**

Run: `make test 2>&1 | grep -E "PASS|FAIL|cmd_classify"`
Expected: All PASS

- [ ] **Step 5: Commit**

```bash
git add src/core/cmd_classify.c tests/test_cmd_classify.c tests/runner.c
git commit -m "feat: add Cisco IOS/NX-OS/ASA command classification rules"
```

---

### Task 4: Command Classifier — Aruba and PAN-OS Platform Rules

**Files:**
- Modify: `src/core/cmd_classify.c` (add Aruba and PAN-OS classifiers)
- Modify: `tests/test_cmd_classify.c` (add platform tests)
- Modify: `tests/runner.c` (register new tests)

- [ ] **Step 1: Write Aruba OS-CX, ArubaOS, and PAN-OS tests**

```c
/* --- Aruba OS-CX --- */
int test_cmd_classify_aruba_cx_show_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("show running-config", CMD_PLATFORM_ARUBA_CX), (int)CMD_SAFE);
    TEST_END();
}

int test_cmd_classify_aruba_cx_conf_write(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("configure terminal", CMD_PLATFORM_ARUBA_CX), (int)CMD_WRITE);
    TEST_END();
}

int test_cmd_classify_aruba_cx_erase_startup_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("erase startup-config", CMD_PLATFORM_ARUBA_CX), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_aruba_cx_no_vsx_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("no vsx", CMD_PLATFORM_ARUBA_CX), (int)CMD_CRITICAL);
    TEST_END();
}

/* --- ArubaOS (wireless) --- */
int test_cmd_classify_aruba_os_show_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("show ap database", CMD_PLATFORM_ARUBA_OS), (int)CMD_SAFE);
    TEST_END();
}

int test_cmd_classify_aruba_os_wlan_write(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("wlan ssid-profile corp", CMD_PLATFORM_ARUBA_OS), (int)CMD_WRITE);
    TEST_END();
}

int test_cmd_classify_aruba_os_factory_reset_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("factory-reset", CMD_PLATFORM_ARUBA_OS), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_aruba_os_ap_wipe_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("ap wipe out all", CMD_PLATFORM_ARUBA_OS), (int)CMD_CRITICAL);
    TEST_END();
}

/* --- PAN-OS --- */
int test_cmd_classify_panos_show_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("show system info", CMD_PLATFORM_PANOS), (int)CMD_SAFE);
    TEST_END();
}

int test_cmd_classify_panos_ping_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("ping host 10.0.0.1", CMD_PLATFORM_PANOS), (int)CMD_SAFE);
    TEST_END();
}

int test_cmd_classify_panos_commit_validate_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("commit validate", CMD_PLATFORM_PANOS), (int)CMD_SAFE);
    TEST_END();
}

int test_cmd_classify_panos_test_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("test security-policy-match", CMD_PLATFORM_PANOS), (int)CMD_SAFE);
    TEST_END();
}

int test_cmd_classify_panos_configure_write(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("configure", CMD_PLATFORM_PANOS), (int)CMD_WRITE);
    TEST_END();
}

int test_cmd_classify_panos_set_write(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("set network interface ethernet1/1 ip 10.0.0.1/24", CMD_PLATFORM_PANOS), (int)CMD_WRITE);
    TEST_END();
}

int test_cmd_classify_panos_commit_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("commit", CMD_PLATFORM_PANOS), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_panos_commit_force_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("commit force", CMD_PLATFORM_PANOS), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_panos_commit_all_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("commit-all", CMD_PLATFORM_PANOS), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_panos_delete_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("delete network interface ethernet1/1", CMD_PLATFORM_PANOS), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_panos_request_restart_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("request restart system", CMD_PLATFORM_PANOS), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_panos_request_license_info_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("request license info", CMD_PLATFORM_PANOS), (int)CMD_SAFE);
    TEST_END();
}

int test_cmd_classify_panos_request_license_deactivate_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("request license deactivate", CMD_PLATFORM_PANOS), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_panos_pipe_modifier(void) {
    TEST_BEGIN();
    /* Pipe modifiers (| match, | except) should not change safety level */
    ASSERT_EQ((int)cmd_classify("show running-config | match ssl", CMD_PLATFORM_PANOS), (int)CMD_SAFE);
    TEST_END();
}

int test_cmd_classify_panos_clear_session_all_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("clear session all", CMD_PLATFORM_PANOS), (int)CMD_CRITICAL);
    TEST_END();
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `make test 2>&1 | grep "FAIL"`
Expected: All new tests FAIL

- [ ] **Step 3: Implement Aruba and PAN-OS classifiers**

Add `classify_aruba_cx_segment()`, `classify_aruba_os_segment()`, and `classify_panos_segment()` to `cmd_classify.c`. Wire into the platform switch. Follow spec sections 7.7-7.9. **All network classifiers use `tok_eq_ci()`/`tok_prefix_ci()` for case-insensitive matching.**

Key PAN-OS patterns:
- `commit validate` is safe, but `commit`, `commit force`, `commit-all` are critical
- `request license info` is safe, `request license deactivate` is critical
- `set` and `delete` in config mode are write/critical respectively
- Pipe modifiers (`| match`, `| except`) don't change safety

- [ ] **Step 4: Run tests, verify all pass**

Run: `make test 2>&1 | grep -E "PASS|FAIL"`
Expected: All PASS

- [ ] **Step 5: Commit**

```bash
git add src/core/cmd_classify.c tests/test_cmd_classify.c tests/runner.c
git commit -m "feat: add Aruba OS-CX/ArubaOS and PAN-OS command classification rules"
```

---

### Task 5: Command Classifier — Cross-Platform Heuristics and Integration

**Files:**
- Modify: `src/core/cmd_classify.c` (add fallback heuristics)
- Modify: `tests/test_cmd_classify.c` (heuristic tests)
- Modify: `src/core/ai_prompt.c` (refactor `ai_command_is_readonly()` to use `cmd_classify()`)
- Modify: `src/core/ai_prompt.h` (add `CmdPlatform` to session awareness)
- Modify: `tests/runner.c`

- [ ] **Step 1: Write cross-platform heuristic tests**

```c
/* --- Cross-platform heuristics (spec section 7.10) --- */
int test_cmd_classify_heuristic_show_always_safe(void) {
    TEST_BEGIN();
    /* show/display should be safe on any platform */
    ASSERT_EQ((int)cmd_classify("show version", CMD_PLATFORM_CISCO_IOS), (int)CMD_SAFE);
    ASSERT_EQ((int)cmd_classify("show version", CMD_PLATFORM_ARUBA_CX), (int)CMD_SAFE);
    ASSERT_EQ((int)cmd_classify("show version", CMD_PLATFORM_PANOS), (int)CMD_SAFE);
    TEST_END();
}

int test_cmd_classify_heuristic_negation_write(void) {
    TEST_BEGIN();
    /* "no <anything>" is at minimum write on network devices */
    ASSERT_EQ((int)cmd_classify("no logging host 10.0.0.1", CMD_PLATFORM_CISCO_IOS) >= CMD_WRITE, 1);
    TEST_END();
}

int test_cmd_classify_heuristic_reload_always_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("reload", CMD_PLATFORM_CISCO_IOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("reload", CMD_PLATFORM_ARUBA_CX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("reload", CMD_PLATFORM_ARUBA_OS), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_heuristic_clear_write(void) {
    TEST_BEGIN();
    /* "clear" prefix -> at minimum write */
    ASSERT_EQ((int)cmd_classify("clear counters", CMD_PLATFORM_CISCO_IOS) >= CMD_WRITE, 1);
    TEST_END();
}
```

- [ ] **Step 2: Run tests, fix any failures in heuristic logic**

- [ ] **Step 3: Refactor ai_command_is_readonly() to delegate to cmd_classify()**

In `src/core/ai_prompt.c`, replace the body of `ai_command_is_readonly()` with:

```c
int ai_command_is_readonly(const char *cmd)
{
    return cmd_classify(cmd, CMD_PLATFORM_LINUX) == CMD_SAFE;
}
```

Keep the function signature unchanged so callers (ai_chat.c) are unaffected. Add `#include "cmd_classify.h"` at the top of `ai_prompt.c`.

- [ ] **Step 4: Run all tests to verify existing ai_prompt tests still pass**

Run: `make test 2>&1 | tail -10`
Expected: 0 failures. The existing `ai_command_is_readonly` tests should still pass since the new classifier is a superset of the old logic.

- [ ] **Step 5: Commit**

```bash
git add src/core/cmd_classify.c src/core/ai_prompt.c tests/test_cmd_classify.c tests/runner.c
git commit -m "feat: add cross-platform heuristics and integrate cmd_classify into ai_prompt"
```

---

### Task 6: Message Item Model

**Files:**
- Create: `src/core/chat_msg.h`
- Create: `src/core/chat_msg.c`
- Create: `tests/test_chat_msg.c`
- Modify: `tests/runner.c`

- [ ] **Step 1: Write the header**

```c
/* src/core/chat_msg.h */
#ifndef NUTSHELL_CHAT_MSG_H
#define NUTSHELL_CHAT_MSG_H

#include <stddef.h>
#include "cmd_classify.h"

typedef enum {
    CHAT_ITEM_USER,
    CHAT_ITEM_AI_TEXT,
    CHAT_ITEM_COMMAND,
    CHAT_ITEM_STATUS
} ChatItemType;

typedef struct ChatMsgItem {
    ChatItemType type;
    int id;
    int measured_height;
    int dirty;
    char *text;
    size_t text_len;

    union {
        struct {
            char *thinking_text;
            int thinking_collapsed;
            float thinking_elapsed;
            int thinking_complete;
        } ai;
        struct {
            char *command;
            CmdSafetyLevel safety;
            int approved;       /* -1=pending, 0=denied, 1=approved */
            int blocked;
        } cmd;
    } u;

    struct ChatMsgItem *next;
    struct ChatMsgItem *prev;
} ChatMsgItem;

typedef struct {
    ChatMsgItem *head;
    ChatMsgItem *tail;
    int count;
    int next_id;
} ChatMsgList;

/* Initialize a message list. */
void chat_msg_list_init(ChatMsgList *list);

/* Create and append an item. Returns the new item, or NULL on alloc failure.
 * text is copied (heap-allocated). */
ChatMsgItem *chat_msg_append(ChatMsgList *list, ChatItemType type, const char *text);

/* Remove an item from the list and free it. */
void chat_msg_remove(ChatMsgList *list, ChatMsgItem *item);

/* Free all items in the list. */
void chat_msg_list_clear(ChatMsgList *list);

/* Update item text (re-allocates). Marks item dirty. Returns 0 on success. */
int chat_msg_set_text(ChatMsgItem *item, const char *text);

/* Set command fields on a CHAT_ITEM_COMMAND item. command string is copied. */
int chat_msg_set_command(ChatMsgItem *item, const char *command,
                         CmdSafetyLevel safety, int blocked);

/* Set thinking text on a CHAT_ITEM_AI_TEXT item. Copied to heap. */
int chat_msg_set_thinking(ChatMsgItem *item, const char *thinking_text);

/* Get item count. */
int chat_msg_count(const ChatMsgList *list);

#endif /* NUTSHELL_CHAT_MSG_H */
```

- [ ] **Step 2: Write failing tests**

```c
/* tests/test_chat_msg.c */
#include "test_framework.h"
#include "chat_msg.h"
#include <string.h>
#include <stdlib.h>

int test_chat_msg_list_init(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ASSERT_NULL(list.head);
    ASSERT_NULL(list.tail);
    ASSERT_EQ(list.count, 0);
    TEST_END();
}

int test_chat_msg_append_user(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *item = chat_msg_append(&list, CHAT_ITEM_USER, "hello");
    ASSERT_NOT_NULL(item);
    ASSERT_EQ((int)item->type, (int)CHAT_ITEM_USER);
    ASSERT_STR_EQ(item->text, "hello");
    ASSERT_EQ(item->dirty, 1);
    ASSERT_EQ(list.count, 1);
    ASSERT_TRUE(list.head == item);
    ASSERT_TRUE(list.tail == item);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_chat_msg_append_order(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *a = chat_msg_append(&list, CHAT_ITEM_USER, "first");
    ChatMsgItem *b = chat_msg_append(&list, CHAT_ITEM_AI_TEXT, "second");
    ChatMsgItem *c = chat_msg_append(&list, CHAT_ITEM_STATUS, "third");
    ASSERT_EQ(list.count, 3);
    ASSERT_TRUE(list.head == a);
    ASSERT_TRUE(list.tail == c);
    ASSERT_TRUE(a->next == b);
    ASSERT_TRUE(b->next == c);
    ASSERT_TRUE(c->prev == b);
    ASSERT_TRUE(b->prev == a);
    ASSERT_NULL(a->prev);
    ASSERT_NULL(c->next);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_chat_msg_remove_middle(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *a = chat_msg_append(&list, CHAT_ITEM_USER, "a");
    ChatMsgItem *b = chat_msg_append(&list, CHAT_ITEM_USER, "b");
    ChatMsgItem *c = chat_msg_append(&list, CHAT_ITEM_USER, "c");
    chat_msg_remove(&list, b);
    ASSERT_EQ(list.count, 2);
    ASSERT_TRUE(a->next == c);
    ASSERT_TRUE(c->prev == a);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_chat_msg_remove_head(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *a = chat_msg_append(&list, CHAT_ITEM_USER, "a");
    ChatMsgItem *b = chat_msg_append(&list, CHAT_ITEM_USER, "b");
    chat_msg_remove(&list, a);
    ASSERT_EQ(list.count, 1);
    ASSERT_TRUE(list.head == b);
    ASSERT_NULL(b->prev);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_chat_msg_remove_tail(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *a = chat_msg_append(&list, CHAT_ITEM_USER, "a");
    ChatMsgItem *b = chat_msg_append(&list, CHAT_ITEM_USER, "b");
    chat_msg_remove(&list, b);
    ASSERT_EQ(list.count, 1);
    ASSERT_TRUE(list.tail == a);
    ASSERT_NULL(a->next);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_chat_msg_remove_only(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *a = chat_msg_append(&list, CHAT_ITEM_USER, "a");
    chat_msg_remove(&list, a);
    ASSERT_EQ(list.count, 0);
    ASSERT_NULL(list.head);
    ASSERT_NULL(list.tail);
    TEST_END();
}

int test_chat_msg_set_text(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *item = chat_msg_append(&list, CHAT_ITEM_USER, "old");
    item->dirty = 0;
    ASSERT_EQ(chat_msg_set_text(item, "new text"), 0);
    ASSERT_STR_EQ(item->text, "new text");
    ASSERT_EQ(item->dirty, 1);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_chat_msg_set_command(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *item = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    ASSERT_EQ(chat_msg_set_command(item, "ls -la", CMD_SAFE, 0), 0);
    ASSERT_STR_EQ(item->u.cmd.command, "ls -la");
    ASSERT_EQ((int)item->u.cmd.safety, (int)CMD_SAFE);
    ASSERT_EQ(item->u.cmd.approved, -1); /* pending */
    ASSERT_EQ(item->u.cmd.blocked, 0);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_chat_msg_set_thinking(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *item = chat_msg_append(&list, CHAT_ITEM_AI_TEXT, "response");
    ASSERT_EQ(chat_msg_set_thinking(item, "I think..."), 0);
    ASSERT_STR_EQ(item->u.ai.thinking_text, "I think...");
    ASSERT_EQ(item->u.ai.thinking_collapsed, 1); /* default collapsed */
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_chat_msg_empty_text(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *item = chat_msg_append(&list, CHAT_ITEM_USER, "");
    ASSERT_NOT_NULL(item);
    ASSERT_STR_EQ(item->text, "");
    ASSERT_EQ(item->text_len, (size_t)0);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_chat_msg_null_text(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *item = chat_msg_append(&list, CHAT_ITEM_STATUS, NULL);
    ASSERT_NOT_NULL(item);
    ASSERT_NOT_NULL(item->text); /* should be "" */
    ASSERT_EQ(item->text_len, (size_t)0);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_chat_msg_unique_ids(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *a = chat_msg_append(&list, CHAT_ITEM_USER, "a");
    ChatMsgItem *b = chat_msg_append(&list, CHAT_ITEM_USER, "b");
    ASSERT_TRUE(a->id != b->id);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_chat_msg_command_too_long(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *item = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    /* Build a command > 1023 bytes */
    char long_cmd[2048];
    memset(long_cmd, 'x', sizeof(long_cmd) - 1);
    long_cmd[sizeof(long_cmd) - 1] = '\0';
    /* Should reject (return non-zero) */
    ASSERT_TRUE(chat_msg_set_command(item, long_cmd, CMD_SAFE, 0) != 0);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_chat_msg_list_clear(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    for (int i = 0; i < 50; i++)
        chat_msg_append(&list, CHAT_ITEM_USER, "msg");
    ASSERT_EQ(list.count, 50);
    chat_msg_list_clear(&list);
    ASSERT_EQ(list.count, 0);
    ASSERT_NULL(list.head);
    ASSERT_NULL(list.tail);
    TEST_END();
}
```

- [ ] **Step 3: Run tests to verify they fail**

Run: `make test 2>&1 | tail -5`
Expected: Linker errors for chat_msg functions

- [ ] **Step 4: Implement chat_msg.c**

```c
/* src/core/chat_msg.c */
#include "chat_msg.h"
#include <stdlib.h>
#include <string.h>

#define CMD_MAX_LEN 1023

void chat_msg_list_init(ChatMsgList *list)
{
    list->head = NULL;
    list->tail = NULL;
    list->count = 0;
    list->next_id = 1;
}

ChatMsgItem *chat_msg_append(ChatMsgList *list, ChatItemType type, const char *text)
{
    ChatMsgItem *item = calloc(1, sizeof(*item));
    if (!item) return NULL;

    item->type = type;
    item->id = list->next_id++;
    item->dirty = 1;
    item->measured_height = 0;

    /* Copy text (treat NULL as empty) */
    const char *src = text ? text : "";
    size_t len = strlen(src);
    item->text = malloc(len + 1);
    if (!item->text) { free(item); return NULL; }
    memcpy(item->text, src, len + 1);
    item->text_len = len;

    /* Type-specific defaults */
    if (type == CHAT_ITEM_AI_TEXT) {
        item->u.ai.thinking_text = NULL;
        item->u.ai.thinking_collapsed = 1;
        item->u.ai.thinking_elapsed = 0.0f;
        item->u.ai.thinking_complete = 0;
    } else if (type == CHAT_ITEM_COMMAND) {
        item->u.cmd.command = NULL;
        item->u.cmd.safety = CMD_SAFE;
        item->u.cmd.approved = -1;
        item->u.cmd.blocked = 0;
    }

    /* Link into list */
    item->prev = list->tail;
    item->next = NULL;
    if (list->tail)
        list->tail->next = item;
    else
        list->head = item;
    list->tail = item;
    list->count++;

    return item;
}

/* Secure zero: use volatile pointer to prevent compiler from optimizing out
 * the memset. On Windows production builds, use SecureZeroMemory instead. */
static void secure_zero(void *ptr, size_t len)
{
    volatile unsigned char *p = (volatile unsigned char *)ptr;
    while (len--) *p++ = 0;
}

static void free_item(ChatMsgItem *item)
{
    if (!item) return;
    /* Zero sensitive content before free (may contain API reasoning, passwords) */
    if (item->text) {
        secure_zero(item->text, item->text_len);
        free(item->text);
    }
    if (item->type == CHAT_ITEM_AI_TEXT && item->u.ai.thinking_text) {
        size_t tlen = strlen(item->u.ai.thinking_text);
        secure_zero(item->u.ai.thinking_text, tlen);
        free(item->u.ai.thinking_text);
    }
    if (item->type == CHAT_ITEM_COMMAND && item->u.cmd.command) {
        size_t clen = strlen(item->u.cmd.command);
        secure_zero(item->u.cmd.command, clen);
        free(item->u.cmd.command);
    }
    free(item);
}

void chat_msg_remove(ChatMsgList *list, ChatMsgItem *item)
{
    if (!list || !item) return;
    if (item->prev)
        item->prev->next = item->next;
    else
        list->head = item->next;
    if (item->next)
        item->next->prev = item->prev;
    else
        list->tail = item->prev;
    list->count--;
    free_item(item);
}

void chat_msg_list_clear(ChatMsgList *list)
{
    ChatMsgItem *cur = list->head;
    while (cur) {
        ChatMsgItem *next = cur->next;
        free_item(cur);
        cur = next;
    }
    list->head = NULL;
    list->tail = NULL;
    list->count = 0;
}

int chat_msg_set_text(ChatMsgItem *item, const char *text)
{
    if (!item) return -1;
    const char *src = text ? text : "";
    size_t len = strlen(src);
    char *new_text = malloc(len + 1);
    if (!new_text) return -1;
    memcpy(new_text, src, len + 1);
    if (item->text) {
        memset(item->text, 0, item->text_len);
        free(item->text);
    }
    item->text = new_text;
    item->text_len = len;
    item->dirty = 1;
    return 0;
}

int chat_msg_set_command(ChatMsgItem *item, const char *command,
                         CmdSafetyLevel safety, int blocked)
{
    if (!item || item->type != CHAT_ITEM_COMMAND) return -1;
    if (!command) return -1;
    size_t len = strlen(command);
    if (len > CMD_MAX_LEN) return -1; /* reject, don't truncate */

    char *new_cmd = malloc(len + 1);
    if (!new_cmd) return -1;
    memcpy(new_cmd, command, len + 1);

    if (item->u.cmd.command) {
        size_t old_len = strlen(item->u.cmd.command);
        memset(item->u.cmd.command, 0, old_len);
        free(item->u.cmd.command);
    }
    item->u.cmd.command = new_cmd;
    item->u.cmd.safety = safety;
    item->u.cmd.approved = -1;
    item->u.cmd.blocked = blocked;
    item->dirty = 1;
    return 0;
}

int chat_msg_set_thinking(ChatMsgItem *item, const char *thinking_text)
{
    if (!item || item->type != CHAT_ITEM_AI_TEXT) return -1;
    const char *src = thinking_text ? thinking_text : "";
    size_t len = strlen(src);
    char *new_text = malloc(len + 1);
    if (!new_text) return -1;
    memcpy(new_text, src, len + 1);

    if (item->u.ai.thinking_text) {
        size_t old_len = strlen(item->u.ai.thinking_text);
        memset(item->u.ai.thinking_text, 0, old_len);
        free(item->u.ai.thinking_text);
    }
    item->u.ai.thinking_text = new_text;
    item->dirty = 1;
    return 0;
}

int chat_msg_count(const ChatMsgList *list)
{
    return list ? list->count : 0;
}
```

- [ ] **Step 5: Register tests in runner.c and run**

Run: `make test 2>&1 | grep -E "PASS|FAIL|chat_msg"`
Expected: All PASS

- [ ] **Step 6: Commit**

```bash
git add src/core/chat_msg.h src/core/chat_msg.c tests/test_chat_msg.c tests/runner.c
git commit -m "feat: add ChatMsgItem linked list model with TDD"
```

---

### Task 7: Thinking Controller

**Files:**
- Create: `src/core/chat_thinking.h`
- Create: `src/core/chat_thinking.c`
- Create: `tests/test_chat_thinking.c`
- Modify: `tests/runner.c`

- [ ] **Step 1: Write the header**

```c
/* src/core/chat_thinking.h */
#ifndef NUTSHELL_CHAT_THINKING_H
#define NUTSHELL_CHAT_THINKING_H

#include "chat_msg.h"

typedef enum {
    THINK_IDLE,       /* No active thinking */
    THINK_STREAMING,  /* Receiving thinking tokens */
    THINK_COMPLETE    /* Thinking finished */
} ThinkingPhase;

typedef struct {
    ThinkingPhase phase;
    float elapsed_sec;      /* Seconds since first thinking token */
    float start_time;       /* Timestamp (seconds) of first token */
    int collapsed;          /* Current collapse state */
} ThinkingState;

/* Initialize thinking state. */
void chat_thinking_init(ThinkingState *state);

/* Called when a thinking token arrives. current_time in seconds (monotonic).
 * Updates elapsed time and phase. */
void chat_thinking_token(ThinkingState *state, float current_time);

/* Called when thinking is complete. Finalizes elapsed time. */
void chat_thinking_complete(ThinkingState *state, float current_time);

/* Toggle collapsed state. Returns the new collapsed value (0 or 1). */
int chat_thinking_toggle(ThinkingState *state);

/* Update elapsed time display (call periodically during streaming).
 * current_time in seconds. */
void chat_thinking_tick(ThinkingState *state, float current_time);

/* Reset to idle state. */
void chat_thinking_reset(ThinkingState *state);

#endif /* NUTSHELL_CHAT_THINKING_H */
```

- [ ] **Step 2: Write failing tests**

```c
/* tests/test_chat_thinking.c */
#include "test_framework.h"
#include "chat_thinking.h"

int test_thinking_init(void) {
    TEST_BEGIN();
    ThinkingState s;
    chat_thinking_init(&s);
    ASSERT_EQ((int)s.phase, (int)THINK_IDLE);
    ASSERT_EQ(s.collapsed, 1); /* default collapsed */
    TEST_END();
}

int test_thinking_first_token(void) {
    TEST_BEGIN();
    ThinkingState s;
    chat_thinking_init(&s);
    chat_thinking_token(&s, 10.0f);
    ASSERT_EQ((int)s.phase, (int)THINK_STREAMING);
    ASSERT_TRUE(s.start_time >= 9.9f && s.start_time <= 10.1f);
    ASSERT_TRUE(s.elapsed_sec < 0.1f);
    TEST_END();
}

int test_thinking_elapsed_updates(void) {
    TEST_BEGIN();
    ThinkingState s;
    chat_thinking_init(&s);
    chat_thinking_token(&s, 10.0f);
    chat_thinking_tick(&s, 12.5f);
    ASSERT_TRUE(s.elapsed_sec >= 2.4f && s.elapsed_sec <= 2.6f);
    TEST_END();
}

int test_thinking_complete(void) {
    TEST_BEGIN();
    ThinkingState s;
    chat_thinking_init(&s);
    chat_thinking_token(&s, 10.0f);
    chat_thinking_complete(&s, 17.8f);
    ASSERT_EQ((int)s.phase, (int)THINK_COMPLETE);
    ASSERT_TRUE(s.elapsed_sec >= 7.7f && s.elapsed_sec <= 7.9f);
    TEST_END();
}

int test_thinking_toggle(void) {
    TEST_BEGIN();
    ThinkingState s;
    chat_thinking_init(&s);
    ASSERT_EQ(s.collapsed, 1);
    int result = chat_thinking_toggle(&s);
    ASSERT_EQ(result, 0);  /* now expanded */
    ASSERT_EQ(s.collapsed, 0);
    result = chat_thinking_toggle(&s);
    ASSERT_EQ(result, 1);  /* collapsed again */
    TEST_END();
}

int test_thinking_toggle_during_streaming(void) {
    TEST_BEGIN();
    ThinkingState s;
    chat_thinking_init(&s);
    chat_thinking_token(&s, 10.0f);
    chat_thinking_toggle(&s); /* expand during streaming */
    ASSERT_EQ(s.collapsed, 0);
    ASSERT_EQ((int)s.phase, (int)THINK_STREAMING); /* still streaming */
    TEST_END();
}

int test_thinking_tick_idle_noop(void) {
    TEST_BEGIN();
    ThinkingState s;
    chat_thinking_init(&s);
    chat_thinking_tick(&s, 100.0f); /* should not crash */
    ASSERT_EQ((int)s.phase, (int)THINK_IDLE);
    ASSERT_TRUE(s.elapsed_sec < 0.01f);
    TEST_END();
}

int test_thinking_tick_complete_noop(void) {
    TEST_BEGIN();
    ThinkingState s;
    chat_thinking_init(&s);
    chat_thinking_token(&s, 10.0f);
    chat_thinking_complete(&s, 15.0f);
    float final_elapsed = s.elapsed_sec;
    chat_thinking_tick(&s, 100.0f); /* should not update elapsed after complete */
    ASSERT_TRUE(s.elapsed_sec >= final_elapsed - 0.01f && s.elapsed_sec <= final_elapsed + 0.01f);
    TEST_END();
}

int test_thinking_reset(void) {
    TEST_BEGIN();
    ThinkingState s;
    chat_thinking_init(&s);
    chat_thinking_token(&s, 10.0f);
    chat_thinking_reset(&s);
    ASSERT_EQ((int)s.phase, (int)THINK_IDLE);
    ASSERT_TRUE(s.elapsed_sec < 0.01f);
    TEST_END();
}
```

- [ ] **Step 3: Run tests to verify they fail**

- [ ] **Step 4: Implement chat_thinking.c**

```c
/* src/core/chat_thinking.c */
#include "chat_thinking.h"

void chat_thinking_init(ThinkingState *state)
{
    state->phase = THINK_IDLE;
    state->elapsed_sec = 0.0f;
    state->start_time = 0.0f;
    state->collapsed = 1;
}

void chat_thinking_token(ThinkingState *state, float current_time)
{
    if (state->phase == THINK_IDLE) {
        state->start_time = current_time;
        state->phase = THINK_STREAMING;
    }
    state->elapsed_sec = current_time - state->start_time;
}

void chat_thinking_complete(ThinkingState *state, float current_time)
{
    if (state->phase == THINK_STREAMING) {
        state->elapsed_sec = current_time - state->start_time;
    }
    state->phase = THINK_COMPLETE;
}

int chat_thinking_toggle(ThinkingState *state)
{
    state->collapsed = !state->collapsed;
    return state->collapsed;
}

void chat_thinking_tick(ThinkingState *state, float current_time)
{
    if (state->phase == THINK_STREAMING) {
        state->elapsed_sec = current_time - state->start_time;
    }
}

void chat_thinking_reset(ThinkingState *state)
{
    chat_thinking_init(state);
}
```

- [ ] **Step 5: Register tests in runner.c and run**

Run: `make test 2>&1 | grep -E "PASS|FAIL|thinking"`
Expected: All PASS

- [ ] **Step 6: Commit**

```bash
git add src/core/chat_thinking.h src/core/chat_thinking.c tests/test_chat_thinking.c tests/runner.c
git commit -m "feat: add thinking state machine with TDD"
```

---

### Task 8: Activity Monitor

**Files:**
- Create: `src/core/chat_activity.h`
- Create: `src/core/chat_activity.c`
- Create: `tests/test_chat_activity.c`
- Modify: `tests/runner.c`

- [ ] **Step 1: Write the header**

```c
/* src/core/chat_activity.h */
#ifndef NUTSHELL_CHAT_ACTIVITY_H
#define NUTSHELL_CHAT_ACTIVITY_H

typedef enum {
    ACTIVITY_IDLE,
    ACTIVITY_PROCESSING,
    ACTIVITY_THINKING,
    ACTIVITY_RESPONDING,
    ACTIVITY_EXECUTING,
    ACTIVITY_WAITING
} ActivityPhase;

typedef enum {
    HEALTH_GREEN,    /* 0-10s since last token */
    HEALTH_YELLOW,   /* 10-30s */
    HEALTH_RED,      /* 30s+ or connection lost */
} HealthStatus;

typedef struct {
    ActivityPhase phase;
    HealthStatus health;
    float last_token_time;   /* timestamp of last received token */
    float phase_start_time;  /* when current phase started */
    int exec_current;        /* current command index (1-based) */
    int exec_total;          /* total commands to execute */
    int connection_lost;     /* 1 if connection detected lost */
} ActivityState;

/* Initialize activity state to idle. */
void chat_activity_init(ActivityState *state);

/* Set phase. current_time is monotonic seconds. */
void chat_activity_set_phase(ActivityState *state, ActivityPhase phase, float current_time);

/* Record a token arrival. Resets health timer. */
void chat_activity_token(ActivityState *state, float current_time);

/* Heartbeat tick: update health status based on elapsed time since last token.
 * Call every ~1 second. */
void chat_activity_tick(ActivityState *state, float current_time);

/* Set execution progress (for ACTIVITY_EXECUTING phase). */
void chat_activity_set_exec(ActivityState *state, int current, int total);

/* Mark connection as lost. */
void chat_activity_connection_lost(ActivityState *state);

/* Reset to idle. */
void chat_activity_reset(ActivityState *state);

/* Format inline status text into buf. Returns bytes written. */
int chat_activity_format(const ActivityState *state, char *buf, size_t buf_size);

#endif /* NUTSHELL_CHAT_ACTIVITY_H */
```

- [ ] **Step 2: Write failing tests**

```c
/* tests/test_chat_activity.c */
#include "test_framework.h"
#include "chat_activity.h"
#include <string.h>

int test_activity_init(void) {
    TEST_BEGIN();
    ActivityState s;
    chat_activity_init(&s);
    ASSERT_EQ((int)s.phase, (int)ACTIVITY_IDLE);
    ASSERT_EQ((int)s.health, (int)HEALTH_GREEN);
    ASSERT_EQ(s.connection_lost, 0);
    TEST_END();
}

int test_activity_phase_transition(void) {
    TEST_BEGIN();
    ActivityState s;
    chat_activity_init(&s);
    chat_activity_set_phase(&s, ACTIVITY_PROCESSING, 1.0f);
    ASSERT_EQ((int)s.phase, (int)ACTIVITY_PROCESSING);
    chat_activity_set_phase(&s, ACTIVITY_THINKING, 2.0f);
    ASSERT_EQ((int)s.phase, (int)ACTIVITY_THINKING);
    chat_activity_set_phase(&s, ACTIVITY_RESPONDING, 5.0f);
    ASSERT_EQ((int)s.phase, (int)ACTIVITY_RESPONDING);
    TEST_END();
}

int test_activity_skip_thinking(void) {
    TEST_BEGIN();
    ActivityState s;
    chat_activity_init(&s);
    chat_activity_set_phase(&s, ACTIVITY_PROCESSING, 1.0f);
    chat_activity_set_phase(&s, ACTIVITY_RESPONDING, 3.0f); /* skip thinking */
    ASSERT_EQ((int)s.phase, (int)ACTIVITY_RESPONDING);
    TEST_END();
}

int test_activity_health_green(void) {
    TEST_BEGIN();
    ActivityState s;
    chat_activity_init(&s);
    chat_activity_set_phase(&s, ACTIVITY_PROCESSING, 0.0f);
    chat_activity_token(&s, 1.0f);
    chat_activity_tick(&s, 5.0f); /* 4s since token */
    ASSERT_EQ((int)s.health, (int)HEALTH_GREEN);
    TEST_END();
}

int test_activity_health_yellow(void) {
    TEST_BEGIN();
    ActivityState s;
    chat_activity_init(&s);
    chat_activity_set_phase(&s, ACTIVITY_PROCESSING, 0.0f);
    chat_activity_token(&s, 1.0f);
    chat_activity_tick(&s, 15.0f); /* 14s since token */
    ASSERT_EQ((int)s.health, (int)HEALTH_YELLOW);
    TEST_END();
}

int test_activity_health_red(void) {
    TEST_BEGIN();
    ActivityState s;
    chat_activity_init(&s);
    chat_activity_set_phase(&s, ACTIVITY_PROCESSING, 0.0f);
    chat_activity_token(&s, 1.0f);
    chat_activity_tick(&s, 35.0f); /* 34s since token */
    ASSERT_EQ((int)s.health, (int)HEALTH_RED);
    TEST_END();
}

int test_activity_health_boundary_10s(void) {
    TEST_BEGIN();
    ActivityState s;
    chat_activity_init(&s);
    chat_activity_set_phase(&s, ACTIVITY_PROCESSING, 0.0f);
    chat_activity_token(&s, 0.0f);
    chat_activity_tick(&s, 10.0f); /* exactly 10s */
    ASSERT_EQ((int)s.health, (int)HEALTH_YELLOW);
    TEST_END();
}

int test_activity_health_boundary_30s(void) {
    TEST_BEGIN();
    ActivityState s;
    chat_activity_init(&s);
    chat_activity_set_phase(&s, ACTIVITY_PROCESSING, 0.0f);
    chat_activity_token(&s, 0.0f);
    chat_activity_tick(&s, 30.0f); /* exactly 30s */
    ASSERT_EQ((int)s.health, (int)HEALTH_RED);
    TEST_END();
}

int test_activity_token_resets_health(void) {
    TEST_BEGIN();
    ActivityState s;
    chat_activity_init(&s);
    chat_activity_set_phase(&s, ACTIVITY_PROCESSING, 0.0f);
    chat_activity_token(&s, 0.0f);
    chat_activity_tick(&s, 20.0f); /* yellow */
    ASSERT_EQ((int)s.health, (int)HEALTH_YELLOW);
    chat_activity_token(&s, 20.0f); /* new token */
    chat_activity_tick(&s, 22.0f); /* 2s since new token */
    ASSERT_EQ((int)s.health, (int)HEALTH_GREEN);
    TEST_END();
}

int test_activity_connection_lost(void) {
    TEST_BEGIN();
    ActivityState s;
    chat_activity_init(&s);
    chat_activity_set_phase(&s, ACTIVITY_PROCESSING, 0.0f);
    chat_activity_connection_lost(&s);
    ASSERT_EQ(s.connection_lost, 1);
    ASSERT_EQ((int)s.health, (int)HEALTH_RED);
    TEST_END();
}

int test_activity_exec_progress(void) {
    TEST_BEGIN();
    ActivityState s;
    chat_activity_init(&s);
    chat_activity_set_phase(&s, ACTIVITY_EXECUTING, 0.0f);
    chat_activity_set_exec(&s, 2, 5);
    ASSERT_EQ(s.exec_current, 2);
    ASSERT_EQ(s.exec_total, 5);
    TEST_END();
}

int test_activity_format_processing(void) {
    TEST_BEGIN();
    ActivityState s;
    chat_activity_init(&s);
    chat_activity_set_phase(&s, ACTIVITY_PROCESSING, 0.0f);
    char buf[128];
    int n = chat_activity_format(&s, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(strstr(buf, "Processing") != NULL);
    TEST_END();
}

int test_activity_format_executing(void) {
    TEST_BEGIN();
    ActivityState s;
    chat_activity_init(&s);
    chat_activity_set_phase(&s, ACTIVITY_EXECUTING, 0.0f);
    chat_activity_set_exec(&s, 2, 5);
    char buf[128];
    chat_activity_format(&s, buf, sizeof(buf));
    ASSERT_TRUE(strstr(buf, "2") != NULL);
    ASSERT_TRUE(strstr(buf, "5") != NULL);
    TEST_END();
}

int test_activity_format_stalled(void) {
    TEST_BEGIN();
    ActivityState s;
    chat_activity_init(&s);
    chat_activity_set_phase(&s, ACTIVITY_RESPONDING, 0.0f);
    chat_activity_token(&s, 0.0f);
    chat_activity_tick(&s, 45.0f);
    char buf[128];
    chat_activity_format(&s, buf, sizeof(buf));
    ASSERT_TRUE(strstr(buf, "Stalled") != NULL || strstr(buf, "stalled") != NULL);
    TEST_END();
}

int test_activity_reset(void) {
    TEST_BEGIN();
    ActivityState s;
    chat_activity_init(&s);
    chat_activity_set_phase(&s, ACTIVITY_RESPONDING, 0.0f);
    chat_activity_token(&s, 5.0f);
    chat_activity_reset(&s);
    ASSERT_EQ((int)s.phase, (int)ACTIVITY_IDLE);
    ASSERT_EQ((int)s.health, (int)HEALTH_GREEN);
    ASSERT_EQ(s.connection_lost, 0);
    TEST_END();
}
```

- [ ] **Step 3: Run tests to verify they fail**

- [ ] **Step 4: Implement chat_activity.c**

```c
/* src/core/chat_activity.c */
#include "chat_activity.h"
#include <stdio.h>
#include <string.h>

void chat_activity_init(ActivityState *state)
{
    memset(state, 0, sizeof(*state));
    state->phase = ACTIVITY_IDLE;
    state->health = HEALTH_GREEN;
}

void chat_activity_set_phase(ActivityState *state, ActivityPhase phase, float current_time)
{
    state->phase = phase;
    state->phase_start_time = current_time;
    if (phase == ACTIVITY_PROCESSING) {
        state->last_token_time = current_time;
        state->health = HEALTH_GREEN;
        state->connection_lost = 0;
    }
}

void chat_activity_token(ActivityState *state, float current_time)
{
    state->last_token_time = current_time;
    state->health = HEALTH_GREEN;
}

void chat_activity_tick(ActivityState *state, float current_time)
{
    if (state->phase == ACTIVITY_IDLE) return;
    if (state->connection_lost) { state->health = HEALTH_RED; return; }

    float elapsed = current_time - state->last_token_time;
    if (elapsed >= 30.0f)
        state->health = HEALTH_RED;
    else if (elapsed >= 10.0f)
        state->health = HEALTH_YELLOW;
    else
        state->health = HEALTH_GREEN;
}

void chat_activity_set_exec(ActivityState *state, int current, int total)
{
    state->exec_current = current;
    state->exec_total = total;
}

void chat_activity_connection_lost(ActivityState *state)
{
    state->connection_lost = 1;
    state->health = HEALTH_RED;
}

void chat_activity_reset(ActivityState *state)
{
    chat_activity_init(state);
}

int chat_activity_format(const ActivityState *state, char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) return 0;

    if (state->connection_lost)
        return (int)snprintf(buf, buf_size, "Connection lost");

    if (state->health == HEALTH_RED && state->phase != ACTIVITY_IDLE) {
        float elapsed = 0; /* caller should provide current_time for dynamic text */
        (void)elapsed;
        return (int)snprintf(buf, buf_size, "Stalled - no response");
    }

    const char *slow = (state->health == HEALTH_YELLOW) ? " (slow)" : "";

    switch (state->phase) {
    case ACTIVITY_IDLE:
        return (int)snprintf(buf, buf_size, "Idle");
    case ACTIVITY_PROCESSING:
        return (int)snprintf(buf, buf_size, "Processing...%s", slow);
    case ACTIVITY_THINKING:
        return (int)snprintf(buf, buf_size, "Thinking...%s", slow);
    case ACTIVITY_RESPONDING:
        return (int)snprintf(buf, buf_size, "Responding...%s", slow);
    case ACTIVITY_EXECUTING:
        return (int)snprintf(buf, buf_size, "Executing %d/%d...%s",
                             state->exec_current, state->exec_total, slow);
    case ACTIVITY_WAITING:
        return (int)snprintf(buf, buf_size, "Waiting for output...%s", slow);
    }
    return 0;
}
```

- [ ] **Step 5: Register tests in runner.c and run**

Run: `make test 2>&1 | grep -E "PASS|FAIL|activity"`
Expected: All PASS

- [ ] **Step 6: Commit**

```bash
git add src/core/chat_activity.h src/core/chat_activity.c tests/test_chat_activity.c tests/runner.c
git commit -m "feat: add activity monitor with health detection (TDD)"
```

---

### Task 9: Command Approval State Machine

**Files:**
- Create: `src/core/chat_approval.h`
- Create: `src/core/chat_approval.c`
- Create: `tests/test_chat_approval.c`
- Modify: `tests/runner.c`

- [ ] **Step 1: Write the header**

```c
/* src/core/chat_approval.h */
#ifndef NUTSHELL_CHAT_APPROVAL_H
#define NUTSHELL_CHAT_APPROVAL_H

#include "cmd_classify.h"

#define APPROVAL_MAX_CMDS 16

typedef enum {
    APPROVE_PENDING,
    APPROVE_APPROVED,
    APPROVE_DENIED,
    APPROVE_BLOCKED,
    APPROVE_EXECUTING,
    APPROVE_COMPLETED
} ApprovalStatus;

typedef struct {
    char command[1024];
    CmdSafetyLevel safety;
    ApprovalStatus status;
} ApprovalEntry;

typedef struct {
    ApprovalEntry entries[APPROVAL_MAX_CMDS];
    int count;
    int auto_approve;           /* 1 = session-level auto-approve active */
    int auto_approve_confirming; /* 1 = waiting for double-click confirm */
    float confirm_start_time;    /* when "are you sure?" was shown */
} ApprovalQueue;

/* Initialize approval queue. */
void chat_approval_init(ApprovalQueue *q);

/* Add a command to the approval queue. Classifies it against the given platform.
 * If permit_write is 0 and the command is write/critical, it's auto-blocked.
 * If auto_approve is active, it's auto-approved.
 * Returns the entry index, or -1 if queue is full. */
int chat_approval_add(ApprovalQueue *q, const char *command,
                      CmdPlatform platform, int permit_write);

/* Approve a specific command by index. Returns 0 on success. */
int chat_approval_approve(ApprovalQueue *q, int index);

/* Deny a specific command by index. Returns 0 on success. */
int chat_approval_deny(ApprovalQueue *q, int index);

/* Approve all pending commands. Returns number approved. */
int chat_approval_approve_all(ApprovalQueue *q);

/* Start the "allow all session" flow. First call shows confirm prompt.
 * Second call within timeout activates auto-approve.
 * current_time: monotonic seconds. confirm_timeout: seconds (3.0).
 * Returns: 0 = confirming (show "are you sure?"), 1 = activated, -1 = timed out (reset). */
int chat_approval_auto_approve_click(ApprovalQueue *q, float current_time,
                                      float confirm_timeout);

/* Revoke session auto-approve. */
void chat_approval_revoke_auto(ApprovalQueue *q);

/* Check if all commands have been decided (no PENDING). */
int chat_approval_all_decided(const ApprovalQueue *q);

/* Get next approved command that hasn't started executing.
 * Returns entry index, or -1 if none. */
int chat_approval_next_approved(const ApprovalQueue *q);

/* Mark a command as executing. */
void chat_approval_set_executing(ApprovalQueue *q, int index);

/* Mark a command as completed. */
void chat_approval_set_completed(ApprovalQueue *q, int index);

/* Reset the queue (e.g., for new AI response). */
void chat_approval_reset(ApprovalQueue *q);

#endif /* NUTSHELL_CHAT_APPROVAL_H */
```

- [ ] **Step 2: Write failing tests**

```c
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
    /* permit_write=0 + write command -> auto-blocked */
    int idx = chat_approval_add(&q, "mv a b", CMD_PLATFORM_LINUX, 0);
    ASSERT_EQ(idx, 0);
    ASSERT_EQ((int)q.entries[0].status, (int)APPROVE_BLOCKED);
    TEST_END();
}

int test_approval_add_write_permitted(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    /* permit_write=1 + write command -> pending */
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
    /* First click: confirming */
    int r = chat_approval_auto_approve_click(&q, 10.0f, 3.0f);
    ASSERT_EQ(r, 0);
    ASSERT_EQ(q.auto_approve_confirming, 1);
    ASSERT_EQ(q.auto_approve, 0);
    /* Second click within 3s: activated */
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
    /* Second click after 3s: timed out */
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
    /* Activate auto-approve */
    chat_approval_auto_approve_click(&q, 10.0f, 3.0f);
    chat_approval_auto_approve_click(&q, 11.0f, 3.0f);
    /* New commands should auto-approve */
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
    ASSERT_EQ(idx, -1); /* reject empty */
    TEST_END();
}

int test_approval_whitespace_command(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    int idx = chat_approval_add(&q, "   ", CMD_PLATFORM_LINUX, 1);
    ASSERT_EQ(idx, -1); /* reject whitespace-only */
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

int test_approval_reset(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    chat_approval_add(&q, "ls", CMD_PLATFORM_LINUX, 1);
    chat_approval_add(&q, "cat f", CMD_PLATFORM_LINUX, 1);
    chat_approval_reset(&q);
    ASSERT_EQ(q.count, 0);
    /* auto_approve should persist across resets (per-session) */
    TEST_END();
}
```

- [ ] **Step 3: Run tests to verify they fail**

- [ ] **Step 4: Implement chat_approval.c**

```c
/* src/core/chat_approval.c */
#include "chat_approval.h"
#include <string.h>
#include <ctype.h>

void chat_approval_init(ApprovalQueue *q)
{
    memset(q, 0, sizeof(*q));
}

static int is_whitespace_only(const char *s)
{
    if (!s) return 1;
    while (*s) {
        if (!isspace((unsigned char)*s)) return 0;
        s++;
    }
    return 1;
}

int chat_approval_add(ApprovalQueue *q, const char *command,
                      CmdPlatform platform, int permit_write)
{
    if (!command || is_whitespace_only(command)) return -1;
    if (q->count >= APPROVAL_MAX_CMDS) return -1;

    int idx = q->count;
    ApprovalEntry *e = &q->entries[idx];
    size_t len = strlen(command);
    if (len >= sizeof(e->command)) len = sizeof(e->command) - 1;
    memcpy(e->command, command, len);
    e->command[len] = '\0';
    e->safety = cmd_classify(command, platform);

    /* Determine initial status */
    if (e->safety > CMD_SAFE && !permit_write) {
        e->status = APPROVE_BLOCKED;
    } else if (q->auto_approve) {
        e->status = APPROVE_APPROVED;
    } else {
        e->status = APPROVE_PENDING;
    }

    q->count++;
    return idx;
}

int chat_approval_approve(ApprovalQueue *q, int index)
{
    if (index < 0 || index >= q->count) return -1;
    if (q->entries[index].status != APPROVE_PENDING) return -1;
    q->entries[index].status = APPROVE_APPROVED;
    return 0;
}

int chat_approval_deny(ApprovalQueue *q, int index)
{
    if (index < 0 || index >= q->count) return -1;
    if (q->entries[index].status != APPROVE_PENDING) return -1;
    q->entries[index].status = APPROVE_DENIED;
    return 0;
}

int chat_approval_approve_all(ApprovalQueue *q)
{
    int n = 0;
    for (int i = 0; i < q->count; i++) {
        if (q->entries[i].status == APPROVE_PENDING) {
            q->entries[i].status = APPROVE_APPROVED;
            n++;
        }
    }
    return n;
}

int chat_approval_auto_approve_click(ApprovalQueue *q, float current_time,
                                      float confirm_timeout)
{
    if (!q->auto_approve_confirming) {
        /* First click: start confirmation */
        q->auto_approve_confirming = 1;
        q->confirm_start_time = current_time;
        return 0;
    }

    /* Second click: check timeout */
    float elapsed = current_time - q->confirm_start_time;
    if (elapsed > confirm_timeout) {
        /* Timed out: reset */
        q->auto_approve_confirming = 0;
        return -1;
    }

    /* Within timeout: activate */
    q->auto_approve = 1;
    q->auto_approve_confirming = 0;
    return 1;
}

void chat_approval_revoke_auto(ApprovalQueue *q)
{
    q->auto_approve = 0;
    q->auto_approve_confirming = 0;
}

int chat_approval_all_decided(const ApprovalQueue *q)
{
    for (int i = 0; i < q->count; i++) {
        if (q->entries[i].status == APPROVE_PENDING)
            return 0;
    }
    return 1;
}

int chat_approval_next_approved(const ApprovalQueue *q)
{
    for (int i = 0; i < q->count; i++) {
        if (q->entries[i].status == APPROVE_APPROVED)
            return i;
    }
    return -1;
}

void chat_approval_set_executing(ApprovalQueue *q, int index)
{
    if (index >= 0 && index < q->count)
        q->entries[index].status = APPROVE_EXECUTING;
}

void chat_approval_set_completed(ApprovalQueue *q, int index)
{
    if (index >= 0 && index < q->count)
        q->entries[index].status = APPROVE_COMPLETED;
}

void chat_approval_reset(ApprovalQueue *q)
{
    int saved_auto = q->auto_approve;
    memset(q->entries, 0, sizeof(q->entries));
    q->count = 0;
    q->auto_approve = saved_auto; /* preserve session flag */
    q->auto_approve_confirming = 0;
}
```

- [ ] **Step 5: Register tests in runner.c and run**

Run: `make test 2>&1 | grep -E "PASS|FAIL|approval"`
Expected: All PASS

- [ ] **Step 6: Commit**

```bash
git add src/core/chat_approval.h src/core/chat_approval.c tests/test_chat_approval.c tests/runner.c
git commit -m "feat: add command approval state machine with auto-approve flow (TDD)"
```

---

### Task 10: Theme Chat Colors

**Files:**
- Modify: `src/core/ui_theme.h` (add `ThemeChatColors` sub-struct)
- Modify: `src/core/ui_theme.c` (add color values for all 4 themes)

- [ ] **Step 1: Add ThemeChatColors to ui_theme.h**

Add the following after the existing `ThemeColors` struct definition (before the closing `};`):

```c
typedef struct {
    unsigned int user_bubble;        /* User bubble background */
    unsigned int user_text;          /* User bubble text */
    unsigned int ai_accent;          /* AI avatar and name color */
    unsigned int cmd_bg;             /* Command block background */
    unsigned int cmd_border;         /* Command block border */
    unsigned int cmd_text;           /* Command text (monospace) */
    unsigned int thinking_border;    /* Thinking region left border */
    unsigned int thinking_text;      /* Thinking content text */
    unsigned int status_text;        /* Status message text */
    unsigned int indicator_green;    /* Healthy activity dot */
    unsigned int indicator_yellow;   /* Slow activity dot */
    unsigned int indicator_red;      /* Stalled activity dot */
} ThemeChatColors;
```

Add `ThemeChatColors chat;` as a new field in the `ThemeColors` struct.

- [ ] **Step 2: Add chat color values to all 4 themes in ui_theme.c**

For each theme in `k_themes[]`, add a `.chat = { ... }` initializer with appropriate colors. Design choices:

**Onyx Synapse (dark):**
```c
.chat = {
    0x007AFF, /* user_bubble — accent blue */
    0xFFFFFF, /* user_text — white on blue */
    0x007AFF, /* ai_accent — blue */
    0x1A1A2E, /* cmd_bg — darker than bg */
    0x2A2A3E, /* cmd_border */
    0xC0C0C0, /* cmd_text — monospace light */
    0x007AFF, /* thinking_border — accent */
    0x888888, /* thinking_text — dim */
    0x666666, /* status_text — dimmer */
    0x34C759, /* indicator_green */
    0xFFCC00, /* indicator_yellow */
    0xFF3B30, /* indicator_red */
}
```

**Onyx Light:**
```c
.chat = {
    0x007AFF, 0xFFFFFF, 0x007AFF,
    0xEEEEF2, 0xDCDCE0, 0x333333,
    0x007AFF, 0x86868B, 0xAAAAAA,
    0x34C759, 0xFFCC00, 0xFF3B30,
}
```

**Sage & Sand:**
```c
.chat = {
    0xA3B18A, 0x1A1A14, 0xA3B18A,
    0x232520, 0x3F4138, 0xD4D1C4,
    0xA3B18A, 0xA09E93, 0x7A7868,
    0x8FBC6A, 0xD4AA4A, 0xC75A3A,
}
```

**Moss & Mist:**
```c
.chat = {
    0x84A98C, 0xFFFFFF, 0x84A98C,
    0xE8EBE6, 0xD5D8D3, 0x354F52,
    0x84A98C, 0x6B8A8D, 0x9AAFB1,
    0x52B788, 0xE9C46A, 0xE76F51,
}
```

- [ ] **Step 3: Build and verify no compile errors**

Run: `make test 2>&1 | tail -5`
Expected: 0 failures (existing tests unaffected)

- [ ] **Step 4: Commit**

```bash
git add src/core/ui_theme.h src/core/ui_theme.c
git commit -m "feat: add ThemeChatColors to all 4 themes for chat panel redesign"
```

---

## Phase 2: Owner-Drawn Message List View (Win32)

### Task 11: Chat List View — Basic Scaffolding

**Files:**
- Create: `src/ui/chat_listview.h`
- Create: `src/ui/chat_listview.c`

This is the Win32 owner-drawn panel. It can't be unit-tested on Linux — testing happens via manual verification and integration in Phase 3.

- [ ] **Step 1: Write the header**

```c
/* src/ui/chat_listview.h */
#ifndef NUTSHELL_CHAT_LISTVIEW_H
#define NUTSHELL_CHAT_LISTVIEW_H

#include <windows.h>
#include "chat_msg.h"
#include "ui_theme.h"

typedef struct {
    HWND hwnd;              /* The owner-drawn panel window */
    ChatMsgList *msg_list;  /* Pointer to message list (not owned) */
    const ThemeColors *theme;

    /* Scroll state */
    int scroll_y;           /* Current scroll offset in pixels */
    int total_height;       /* Total content height in pixels */
    int viewport_height;    /* Visible area height */

    /* Fonts (not owned, set by parent) */
    HFONT hFont;
    HFONT hMonoFont;
    HFONT hBoldFont;
    HFONT hSmallFont;
    HFONT hIconFont;

    /* DPI scaling factor (1.0 = 96 DPI) */
    float dpi_scale;

    /* Layout constants (scaled) */
    int msg_gap;            /* Inter-message gap (12px base) */
    int user_pad_h;         /* User bubble horizontal padding (10px) */
    int user_pad_v;         /* User bubble vertical padding (8px) */
    int ai_indent;          /* AI content left indent (30px) */
    int code_pad;           /* Code block padding (6px) */

} ChatListView;

/* Register the window class. Call once at startup. */
void chat_listview_register(HINSTANCE hInstance);

/* Create the list view as a child window. */
HWND chat_listview_create(HWND parent, int x, int y, int w, int h,
                          ChatMsgList *msg_list, const ThemeColors *theme);

/* Set fonts (called after creation or font change). */
void chat_listview_set_fonts(HWND hwnd, HFONT font, HFONT mono,
                             HFONT bold, HFONT small_font, HFONT icon);

/* Set theme (triggers full repaint). */
void chat_listview_set_theme(HWND hwnd, const ThemeColors *theme);

/* Notify that the message list has changed. Triggers remeasure + repaint. */
void chat_listview_invalidate(HWND hwnd);

/* Scroll to bottom (e.g., after new message). */
void chat_listview_scroll_to_bottom(HWND hwnd);

/* Recalculate layout after resize. */
void chat_listview_relayout(HWND hwnd);

#endif /* NUTSHELL_CHAT_LISTVIEW_H */
```

- [ ] **Step 2: Implement chat_listview.c with WndProc, paint, and scroll**

The implementation should:
1. Register a custom window class `"NutshellChatList"` with its own WndProc
2. Handle `WM_CREATE`: store `ChatListView*` in `GWLP_USERDATA`
3. Handle `WM_PAINT`: iterate visible items using virtual scroll, call per-type paint functions
4. Handle `WM_VSCROLL` and `WM_MOUSEWHEEL`: update `scroll_y`, clamp, repaint
5. Handle `WM_SIZE`: update `viewport_height`, recalculate scrollbar range
6. Implement `measure_item()`: calculate pixel height for each item type using `DrawText` with `DT_CALCRECT`
7. Implement `paint_user_item()`, `paint_ai_item()`, `paint_command_item()`, `paint_status_item()`
8. Use `ScrollWindowEx` or `InvalidateRect` for smooth scrolling

This is a large file (~400-500 lines). Write it incrementally: first scaffolding with solid scroll math, then the paint routines.

- [ ] **Step 3: Verify it compiles with MinGW**

Run: `make 2>&1 | tail -10`
Expected: Clean compile (no errors, warnings acceptable for now)

- [ ] **Step 4: Commit**

```bash
git add src/ui/chat_listview.h src/ui/chat_listview.c
git commit -m "feat: add owner-drawn chat list view with virtual scrolling"
```

---

### Task 12: Markdown-to-GDI Renderer

**Files:**
- Create: `src/ui/md_render.h`
- Create: `src/ui/md_render.c`

Uses the existing `markdown.h` inline parser (`md_classify_line`, `md_parse_inline`) but renders to GDI HDC instead of RichEdit.

- [ ] **Step 1: Write the header**

```c
/* src/ui/md_render.h */
#ifndef NUTSHELL_MD_RENDER_H
#define NUTSHELL_MD_RENDER_H

#include <windows.h>
#include "ui_theme.h"

/* Render markdown text into a GDI device context.
 * x, y: top-left position. max_width: available width for wrapping.
 * Returns the total height consumed (in pixels). */
int md_render_text(HDC hdc, const char *text, int x, int y, int max_width,
                   HFONT hFont, HFONT hMonoFont, HFONT hBoldFont,
                   const ThemeColors *theme);

/* Measure the height that markdown text would consume without painting.
 * Same parameters as md_render_text. */
int md_measure_text(HDC hdc, const char *text, int max_width,
                    HFONT hFont, HFONT hMonoFont, HFONT hBoldFont,
                    const ThemeColors *theme);

#endif /* NUTSHELL_MD_RENDER_H */
```

- [ ] **Step 2: Implement md_render.c**

Use `md_classify_line()` from `markdown.h` to identify block-level elements (headings, code fences, lists, paragraphs). For each line, use `md_parse_inline()` to get bold/italic/code spans. Render each span with appropriate font selection and `DrawTextW` (UTF-8 -> UTF-16 conversion).

Key rendering logic:
- **Headings**: Increase font size, bold, extra vertical space
- **Code blocks**: Monospace font, fill background rect with `theme->chat.cmd_bg`
- **Inline code**: Monospace font, subtle background
- **Bold/italic**: Select appropriate font via `SelectObject`
- **Lists**: Indent + bullet/number prefix
- **Paragraphs**: Word-wrap with `DT_WORDBREAK`

- [ ] **Step 3: Verify it compiles**

Run: `make 2>&1 | tail -5`
Expected: Clean compile

- [ ] **Step 4: Commit**

```bash
git add src/ui/md_render.h src/ui/md_render.c
git commit -m "feat: add markdown-to-GDI renderer using existing markdown parser"
```

---

## Phase 3: Replace RichEdit with Chat List View

### Task 13: Integrate Chat List View into ai_chat.c

**Files:**
- Modify: `src/ui/ai_chat.c` (major changes)

This is the largest integration task. Key changes:

- [ ] **Step 1: Add new fields to AiChatData struct**

Add to the `AiChatData` struct (around line 82):
```c
/* New chat list view fields */
ChatMsgList msg_list;           /* Message item linked list */
HWND hChatList;                 /* Owner-drawn chat list view */
ThinkingState thinking_state;   /* Current thinking state machine */
ActivityState activity_state;   /* Activity monitor */
ApprovalQueue approval_queue;   /* Command approval queue */
```

Add includes at the top:
```c
#include "chat_msg.h"
#include "chat_thinking.h"
#include "chat_activity.h"
#include "chat_approval.h"
#include "chat_listview.h"
```

- [ ] **Step 2: Replace RichEdit creation with ChatListView**

In the `WM_CREATE` handler, replace `CreateWindowExW(... "RichEdit50W" ...)` for `hDisplay` with `chat_listview_create(...)`. Initialize `msg_list` with `chat_msg_list_init()`. Initialize thinking/activity/approval state.

Keep `hDisplay` name pointing to the new list view HWND for compatibility with layout code.

- [ ] **Step 3: Replace chat_rebuild_display() to populate ChatMsgList**

Instead of appending styled text to RichEdit, iterate the conversation and create `ChatMsgItem` entries:
```c
static void chat_rebuild_display(AiChatData *d) {
    chat_msg_list_clear(&d->msg_list);
    for (int i = 1; i < d->conv.msg_count; i++) { /* skip system prompt */
        AiMessage *m = &d->conv.messages[i];
        if (m->role == AI_ROLE_USER) {
            chat_msg_append(&d->msg_list, CHAT_ITEM_USER, m->content);
        } else if (m->role == AI_ROLE_ASSISTANT) {
            ChatMsgItem *item = chat_msg_append(&d->msg_list, CHAT_ITEM_AI_TEXT, m->content);
            if (item && d->thinking_history[i])
                chat_msg_set_thinking(item, d->thinking_history[i]);
        }
    }
    chat_listview_invalidate(d->hChatList);
    chat_listview_scroll_to_bottom(d->hChatList);
}
```

- [ ] **Step 4: Update WM_AI_STREAM handler**

Replace RichEdit text manipulation with ChatMsgItem updates:
- On first thinking token: create/update AI item's thinking text
- On content tokens: update AI item's text
- Mark items dirty, call `chat_listview_invalidate()`

- [ ] **Step 5: Update WM_AI_RESPONSE handler**

After stream completes:
- Finalize the AI item text with full markdown content
- Extract commands, create `CHAT_ITEM_COMMAND` items
- Populate the `ApprovalQueue`
- Remove old Allow/Deny button logic (will be inline in list view)

- [ ] **Step 6: Remove old RichEdit helper functions**

Remove or deprecate: `chat_append_styled()`, `chat_append_markdown()`, `chat_append_styled_ex()`, `start_indicator()`, `remove_indicator()` and other RichEdit-specific code.

- [ ] **Step 7: Build and manually test**

Run: `make 2>&1 | tail -10`
Expected: Clean compile. Manual test by running the application.

- [ ] **Step 8: Commit**

```bash
git add src/ui/ai_chat.c
git commit -m "feat: replace RichEdit display with owner-drawn chat list view"
```

---

## Phase 4: Command Blocks with Inline Buttons

### Task 14: Inline Command Approval UI

**Files:**
- Modify: `src/ui/chat_listview.c` (command block rendering + button hit testing)
- Modify: `src/ui/ai_chat.c` (wire approval actions)

- [ ] **Step 1: Implement command block painting in chat_listview.c**

In `paint_command_item()`:
- Draw bordered rectangle with safety tag in top-right corner
- Render command text in monospace (`hMonoFont`)
- Draw Allow/Deny buttons when `status == APPROVE_PENDING`
- **Blocked state** (when `blocked == 1`): grey out command text, render lock icon via `hIconFont` (Fluent UI icon font, not emoji — GDI emoji rendering is unreliable), show help text "Enable \"Permit Write\" to approve", no Allow/Deny buttons
- Color safety tag: grey for `CMD_SAFE`, orange for `CMD_WRITE`, red for `CMD_CRITICAL`
- **Approved/executing/completed states**: Show status text instead of buttons ("Approved", "Running...", "Done")

When multiple commands exist, render an "Allow All (N commands)" ghost/outline button above the first command block. This is a separate clickable region with a lighter/outline style to make it harder to accidentally click than the per-command buttons (spec section 5.3). Below all command blocks, render "Allow all commands this session" as small text link.

- [ ] **Step 2: Implement hit testing for Allow/Deny buttons**

In the WndProc `WM_LBUTTONDOWN` handler:
- Convert mouse coordinates to item + button
- Send `WM_COMMAND` to parent with command index
- Handle "Allow All" ghost button above command group
- Handle "Allow all commands this session" text link

- [ ] **Step 3: Wire button actions in ai_chat.c**

Handle new button messages:
- `IDC_CMD_APPROVE`: call `chat_approval_approve()`, start execution
- `IDC_CMD_DENY`: call `chat_approval_deny()`, send denial message
- `IDC_CMD_APPROVE_ALL`: call `chat_approval_approve_all()`
- `IDC_AUTO_APPROVE`: call `chat_approval_auto_approve_click()`

Remove old floating `hAllowBtn`/`hDenyBtn` windows.

- [ ] **Step 4: Build and test**

Run: `make 2>&1 | tail -5`
Expected: Clean compile

- [ ] **Step 5: Commit**

```bash
git add src/ui/chat_listview.c src/ui/ai_chat.c
git commit -m "feat: add inline command approval buttons with safety classification"
```

---

## Phase 5: Activity Indicator Integration

### Task 15: Dual-Placement Activity Indicator

**Files:**
- Modify: `src/ui/ai_chat.c` (header bar indicator + inline indicator)
- Modify: `src/ui/chat_listview.c` (inline indicator rendering)

- [ ] **Step 1: Add header bar indicator to ai_chat.c**

Add a small status area in the chat panel header (next to session label):
- Pulsing dot (two pre-rendered bitmaps toggled by timer)
- One-word status text
- Use `TIMER_HEARTBEAT` (1-second interval) to call `chat_activity_tick()`

- [ ] **Step 2: Add inline indicator to chat_listview.c**

When activity is not idle, render a status line below the last message:
- Full status text from `chat_activity_format()`
- Pulsing dot with health color
- `[Retry]` link when stalled

- [ ] **Step 3: Wire activity state transitions**

In `ai_chat.c`:
- `launch_stream_thread()`: set `ACTIVITY_PROCESSING`
- `WM_AI_STREAM` wParam==0: set `ACTIVITY_THINKING`
- `WM_AI_STREAM` wParam==1: set `ACTIVITY_RESPONDING`
- Command execution: set `ACTIVITY_EXECUTING` with progress
- After commands: set `ACTIVITY_WAITING`
- `WM_AI_RESPONSE`: reset to `ACTIVITY_IDLE`
- Each stream chunk: call `chat_activity_token()`

- [ ] **Step 4: Implement pulsing animation**

Create two small bitmaps per color (full-opacity and half-opacity 8px circles). Toggle with a 750ms timer. Use `BitBlt` to draw.

- [ ] **Step 5: Implement Retry action**

When user clicks `[Retry]`:
- Cancel current HTTP request
- Re-send last user message
- Reset activity to Processing
- Append status message "[retrying request]"

- [ ] **Step 6: Build and test**

Run: `make 2>&1 | tail -5`

- [ ] **Step 7: Commit**

```bash
git add src/ui/ai_chat.c src/ui/chat_listview.c
git commit -m "feat: add dual-placement activity indicator with health monitoring"
```

---

## Phase 6: Polish and Integration

### Task 16: Collapsible Thinking UI

**Files:**
- Modify: `src/ui/chat_listview.c` (thinking region rendering + toggle)
- Modify: `src/ui/ai_chat.c` (thinking timer integration)

- [ ] **Step 1: Implement thinking region rendering**

In `paint_ai_item()`, after the AI header and before the text content:
- If thinking text exists, render the collapsible thinking region
- Collapsed: single line `> Thinking (2.1s)` with right arrow
- Expanded: `v Thinking (2.1s)` + scrollable content area with left border (3px solid, theme `thinking_border` color)
- Max height 300px (DPI-scaled), min height 30px (DPI-scaled), internal scroll if content exceeds

- [ ] **Step 2: Implement thinking toggle click**

In WM_LBUTTONDOWN hit testing:
- Detect clicks on the thinking toggle line
- Call `chat_thinking_toggle()` on the item's thinking state
- Mark item dirty, repaint

- [ ] **Step 3: Implement nested scroll for thinking region**

In WM_MOUSEWHEEL:
- If cursor is over an expanded thinking region with overflow
- Scroll the thinking region internally
- When thinking scroll reaches top/bottom, bubble to parent list

- [ ] **Step 4: Wire thinking timer**

In ai_chat.c, update the heartbeat timer to also call `chat_thinking_tick()` for live elapsed time updates during streaming.

- [ ] **Step 5: Build and test**

- [ ] **Step 6: Commit**

```bash
git add src/ui/chat_listview.c src/ui/ai_chat.c
git commit -m "feat: add collapsible thinking regions with live timer"
```

---

### Task 17: Session Context Isolation

**Files:**
- Modify: `src/ui/ai_chat.c` (`do_session_switch()` updates)
- Modify: `src/core/ai_prompt.h` (add `CmdPlatform` to `AiSessionState`)

- [ ] **Step 1: Add platform and approval state to AiSessionState**

In `ai_prompt.h`, add to `AiSessionState`:
```c
int platform;              /* CmdPlatform for this session */
int auto_approve;          /* Session-level auto-approve flag */
```

- [ ] **Step 2: Update do_session_switch() to save/restore new state**

Save before switch:
- Serialize `ChatMsgList` (or just mark for rebuild)
- Save `approval_queue.auto_approve` to old session
- Save activity state phase

Restore on switch:
- Rebuild `ChatMsgList` from new session's conversation
- Restore `auto_approve` from new session
- Update activity indicator for new session
- Set classifier platform from new session's `platform` field

- [ ] **Step 3: Handle concurrent streaming during switch**

If old session is busy (streaming), chunks continue accumulating in its `AiSessionState` buffers. When user switches back, rebuild display with accumulated content.

- [ ] **Step 4: Build and verify existing tests still pass**

Run: `make test 2>&1 | tail -5`
Expected: 0 failures

- [ ] **Step 5: Commit**

```bash
git add src/ui/ai_chat.c src/core/ai_prompt.h
git commit -m "feat: add per-session context isolation for chat list view"
```

---

### Task 18: DPI Scaling and Keyboard Navigation

**Files:**
- Modify: `src/ui/chat_listview.c` (DPI awareness + keyboard)

- [ ] **Step 1: Apply DPI scaling to all layout constants**

Multiply all pixel constants by `dpi_scale`:
- `msg_gap = (int)(12 * dpi_scale)`
- `user_pad_h = (int)(10 * dpi_scale)`
- `user_pad_v = (int)(8 * dpi_scale)`
- `ai_indent = (int)(30 * dpi_scale)`
- `code_pad = (int)(6 * dpi_scale)`
- Thinking max height: `(int)(300 * dpi_scale)`

Calculate `dpi_scale` from `GetDpiForWindow()` (Win10+) or `GetDeviceCaps(hdc, LOGPIXELSX) / 96.0f`.

- [ ] **Step 2: Handle WM_DPICHANGED**

Recalculate all scaled constants and trigger full relayout.

- [ ] **Step 3: Add keyboard navigation**

Handle `WM_KEYDOWN`:
- `Page Up / Page Down`: scroll by viewport height
- `Home / End`: scroll to top / bottom
- `Up / Down arrows`: scroll by one message height

- [ ] **Step 4: Build and test**

- [ ] **Step 5: Commit**

```bash
git add src/ui/chat_listview.c
git commit -m "feat: add DPI scaling and keyboard navigation to chat list view"
```

---

### Task 19: Final Integration and Cleanup

**Files:**
- Modify: `src/ui/ai_chat.c` (remove dead code)
- Modify: `src/ui/resource.h` (add new control IDs if needed)
- Run: full test suite

- [ ] **Step 1: Remove dead RichEdit code from ai_chat.c**

Remove functions that are no longer called:
- `chat_append_styled()` and `chat_append_styled_ex()`
- `chat_append_markdown()` (replaced by md_render)
- `start_indicator()` / `remove_indicator()` (replaced by activity monitor)
- Old `chat_append_ops()` welcome header logic (rebuild in ChatMsgList style)
- Old floating button positioning code

- [ ] **Step 2: Add any missing control IDs to resource.h**

If the inline buttons need unique IDs, define them:
```c
#define IDC_CMD_APPROVE_BASE 2020  /* 2020..2035 for up to 16 commands */
#define IDC_CMD_DENY_BASE    2040
#define IDC_CMD_APPROVE_ALL  2060
#define IDC_AUTO_APPROVE     2061
```

- [ ] **Step 3: Run full test suite**

Run: `make test 2>&1 | tail -10`
Expected: 0 failures. All 865+ existing tests pass, plus all new tests.

- [ ] **Step 4: Build release binary**

Run: `make clean && make 2>&1 | tail -5`
Expected: Clean compile with zero warnings.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "chore: remove dead RichEdit code and finalize chat UX redesign"
```
