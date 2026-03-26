/* src/core/cmd_classify.c */
#include "cmd_classify.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>

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
    { "ip", "addr",  CMD_SAFE },
    { "ip", "route", CMD_SAFE },
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
    { "ip", "link",  "set",   CMD_CRITICAL },
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

static const char *strip_path(const char *tok, size_t len, size_t *out_len)
{
    const char *base = tok;
    for (size_t i = 0; i < len; i++) {
        if (tok[i] == '/') base = tok + i + 1;
    }
    *out_len = (size_t)((tok + len) - base);
    return base;
}

static int ci_lower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

static int ci_memcmp(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        int ca = ci_lower((unsigned char)a[i]);
        int cb = ci_lower((unsigned char)b[i]);
        if (ca != cb) return ca - cb;
    }
    return 0;
}

static int tok_eq(const char *tok, size_t tlen, const char *lit)
{
    size_t llen = strlen(lit);
    if (tlen != llen) return 0;
    return memcmp(tok, lit, tlen) == 0;
}

static int tok_prefix(const char *tok, size_t tlen, const char *prefix)
{
    size_t plen = strlen(prefix);
    if (tlen < plen) return 0;
    return memcmp(tok, prefix, plen) == 0;
}

static int tok_in_list(const char *tok, size_t tlen, const char **list)
{
    for (int i = 0; list[i]; i++) {
        if (tok_eq(tok, tlen, list[i])) return 1;
    }
    return 0;
}

static int tok_has_prefix(const char *tok, size_t tlen, const char **list)
{
    for (int i = 0; list[i]; i++) {
        if (tok_prefix(tok, tlen, list[i])) return 1;
    }
    return 0;
}

/* ----- Redirect scanning ----- */

static CmdSafetyLevel scan_redirects(const char *seg, size_t seg_len)
{
    const char *end = seg + seg_len;
    for (const char *p = seg; p < end; p++) {
        if (*p == '\'' || *p == '"') {
            char q = *p++;
            while (p < end && *p != q) p++;
            if (p >= end) break;
            continue;
        }
        if (*p == '>' || (*p == '&' && (p + 1) < end && *(p + 1) == '>')) {
            const char *r = p;
            if (*r == '&') r++;
            if (r > seg && *(r - 1) == '2') { /* 2> or 2>> */ }
            r++;
            if (r < end && *r == '>') r++;
            while (r < end && (*r == ' ' || *r == '\t')) r++;
            if (r + 9 <= end && memcmp(r, "/dev/null", 9) == 0)
                { p = r + 8; continue; }
            if (r + 2 <= end && *r == '&' && (*(r+1) == '1' || *(r+1) == '2'))
                { p = r + 1; continue; }
            return CMD_WRITE;
        }
    }
    return CMD_SAFE;
}

/* ----- Pipe-to-dangerous scanning ----- */

static CmdSafetyLevel scan_pipe_target(const char *seg)
{
    const char *p = seg;
    const char *tok_start;
    size_t tok_len;

    if (!next_token(&p, &tok_start, &tok_len)) return CMD_SAFE;

    const char *base;
    size_t base_len;
    base = strip_path(tok_start, tok_len, &base_len);

    if (tok_eq(base, base_len, "sh") || tok_eq(base, base_len, "bash"))
        return CMD_CRITICAL;

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

static CmdSafetyLevel scan_db_cli_args(const char *p)
{
    while (*p && *p != '|' && *p != ';') {
        if ((*p == '-' && *(p+1) == 'e') || (*p == '-' && *(p+1) == '-')) {
            p += 2;
            while (*p == ' ' || *p == '\t') p++;
            const char *sql = p;
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
            return CMD_WRITE;
        }
        p++;
    }
    return CMD_SAFE;
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

    (void)seg_end;

    CmdSafetyLevel redir = scan_redirects(seg, seg_len);

    if (!next_token(&p, &tok1_start, &tok1_len))
        return redir;

    const char *base1;
    size_t base1_len;
    base1 = strip_path(tok1_start, tok1_len, &base1_len);

    if (tok_eq(base1, base1_len, "sudo") || tok_eq(base1, base1_len, "su")
        || tok_eq(base1, base1_len, "doas")) {
        has_sudo = 1;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '-') {
            while (*p && *p != ' ' && *p != '\t') p++;
            while (*p == ' ' || *p == '\t') p++;
            while (*p && *p != ' ' && *p != '\t') p++;
        }
        if (!next_token(&p, &tok1_start, &tok1_len)) {
            CmdSafetyLevel level = CMD_WRITE;
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size, "sudo/su escalation");
            return level > redir ? level : redir;
        }
        base1 = strip_path(tok1_start, tok1_len, &base1_len);
    }

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

    if (tok_in_list(base1, base1_len, linux_critical_cmds)) {
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "critical command: %.*s",
                     (int)base1_len, base1);
        return CMD_CRITICAL;
    }

    if (tok_has_prefix(base1, base1_len, linux_critical_prefixes)) {
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "critical prefix: %.*s",
                     (int)base1_len, base1);
        return CMD_CRITICAL;
    }

    if (tok_eq(base1, base1_len, "kill")) {
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

    if (is_db_cli(base1, base1_len)) {
        CmdSafetyLevel db_level = scan_db_cli_args(p);
        if (db_level > redir) redir = db_level;
        if (reason_buf && reason_buf_size > 0 && db_level > CMD_SAFE)
            snprintf(reason_buf, reason_buf_size, "destructive SQL via %.*s",
                     (int)base1_len, base1);
        return redir;
    }

    /* Three-token rules */
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

    if (tok_in_list(base1, base1_len, linux_write_cmds)) {
        CmdSafetyLevel level = CMD_WRITE;
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "write command: %.*s",
                     (int)base1_len, base1);
        return level > redir ? level : redir;
    }

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

    if (tok_eq(base1, base1_len, "ufw")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len)) {
            if (tok_eq(tok2_start, tok2_len, "reset")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "ufw reset");
                return CMD_CRITICAL;
            }
        }
        return CMD_WRITE;
    }

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

    if (has_sudo) {
        CmdSafetyLevel level = CMD_WRITE;
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "sudo escalation of %.*s",
                     (int)base1_len, base1);
        return level > redir ? level : redir;
    }

    return redir;
}

/* ----- Top-level command classification ----- */

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
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;

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

        switch (platform) {
        case CMD_PLATFORM_LINUX:
        default:
            seg_level = classify_linux_segment(seg_start, seg_len,
                            worst == CMD_SAFE ? reason_buf : NULL,
                            worst == CMD_SAFE ? reason_buf_size : 0);
            break;
        /* Network platform classification added in later tasks */
        }

        if (is_pipe_target) {
            CmdSafetyLevel pipe_level = scan_pipe_target(seg_start);
            if (pipe_level > seg_level) seg_level = pipe_level;
        }

        if (seg_level > worst) worst = seg_level;
        if (worst == CMD_CRITICAL) return worst;

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
