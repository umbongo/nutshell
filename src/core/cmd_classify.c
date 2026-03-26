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
    "wget",
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

static int tok_eq_ci(const char *tok, size_t tlen, const char *lit)
{
    size_t llen = strlen(lit);
    if (tlen != llen) return 0;
    return ci_memcmp(tok, lit, tlen) == 0;
}

static int tok_prefix_ci(const char *tok, size_t tlen, const char *prefix)
{
    size_t plen = strlen(prefix);
    if (tlen < plen) return 0;
    return ci_memcmp(tok, prefix, plen) == 0;
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

    if (tok_eq(base1, base1_len, "curl")) {
        const char *scan = p;
        const char *ts;
        size_t tl;
        while (next_token(&scan, &ts, &tl)) {
            if (tok_eq(ts, tl, "-o") || tok_eq(ts, tl, "-O") ||
                tok_eq(ts, tl, "--output")) {
                CmdSafetyLevel level = CMD_WRITE;
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "curl with output flag");
                return level > redir ? level : redir;
            }
        }
        if (has_sudo) {
            CmdSafetyLevel level = CMD_WRITE;
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size, "sudo escalation of curl");
            return level > redir ? level : redir;
        }
        return redir;
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

/* ----- Per-segment Cisco IOS classification ----- */

static CmdSafetyLevel classify_cisco_ios_segment(const char *seg, size_t seg_len,
                                                   char *reason_buf, size_t reason_buf_size)
{
    const char *p = seg;
    const char *tok1_start, *tok2_start, *tok3_start;
    size_t tok1_len, tok2_len, tok3_len;

    (void)seg_len;

    if (!next_token(&p, &tok1_start, &tok1_len))
        return CMD_SAFE;

    /* --- Safe commands --- */
    if (tok_prefix_ci(tok1_start, tok1_len, "show"))
        return CMD_SAFE;
    if (tok_eq_ci(tok1_start, tok1_len, "ping") ||
        tok_eq_ci(tok1_start, tok1_len, "traceroute") ||
        tok_eq_ci(tok1_start, tok1_len, "terminal") ||
        tok_eq_ci(tok1_start, tok1_len, "enable") ||
        tok_eq_ci(tok1_start, tok1_len, "disable") ||
        tok_eq_ci(tok1_start, tok1_len, "exit") ||
        tok_eq_ci(tok1_start, tok1_len, "end") ||
        tok_eq_ci(tok1_start, tok1_len, "dir") ||
        tok_eq_ci(tok1_start, tok1_len, "verify"))
        return CMD_SAFE;

    /* --- Critical: standalone commands --- */
    if (tok_eq_ci(tok1_start, tok1_len, "reload")) {
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "reload: device reboot");
        return CMD_CRITICAL;
    }
    if (tok_eq_ci(tok1_start, tok1_len, "shutdown")) {
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "shutdown: disables interface");
        return CMD_CRITICAL;
    }
    if (tok_eq_ci(tok1_start, tok1_len, "redundancy")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            tok_eq_ci(tok2_start, tok2_len, "force-switchover")) {
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size, "redundancy force-switchover");
            return CMD_CRITICAL;
        }
    }

    /* --- Two-token critical checks --- */
    {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len)) {
            /* write erase */
            if (tok_eq_ci(tok1_start, tok1_len, "write") &&
                tok_eq_ci(tok2_start, tok2_len, "erase")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "write erase: clears startup config");
                return CMD_CRITICAL;
            }
            /* erase startup-config / erase nvram: */
            if (tok_eq_ci(tok1_start, tok1_len, "erase") &&
                (tok_prefix_ci(tok2_start, tok2_len, "startup") ||
                 tok_prefix_ci(tok2_start, tok2_len, "nvram"))) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "erase config");
                return CMD_CRITICAL;
            }
            /* delete flash: */
            if (tok_eq_ci(tok1_start, tok1_len, "delete") &&
                tok_prefix_ci(tok2_start, tok2_len, "flash")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "delete flash");
                return CMD_CRITICAL;
            }
            /* format flash: */
            if (tok_eq_ci(tok1_start, tok1_len, "format") &&
                tok_prefix_ci(tok2_start, tok2_len, "flash")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "format flash");
                return CMD_CRITICAL;
            }
            /* config replace */
            if (tok_eq_ci(tok1_start, tok1_len, "config") &&
                tok_eq_ci(tok2_start, tok2_len, "replace")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "config replace");
                return CMD_CRITICAL;
            }
        }
    }

    /* --- "clear" critical patterns --- */
    if (tok_eq_ci(tok1_start, tok1_len, "clear")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len)) {
            /* clear crypto sa / clear crypto isakmp */
            if (tok_eq_ci(tok2_start, tok2_len, "crypto")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "clear crypto: drops VPN sessions");
                return CMD_CRITICAL;
            }
            /* clear ip bgp / clear ip ospf */
            if (tok_eq_ci(tok2_start, tok2_len, "ip")) {
                const char *p3 = p2;
                if (next_token(&p3, &tok3_start, &tok3_len)) {
                    if (tok_prefix_ci(tok3_start, tok3_len, "bgp") ||
                        tok_prefix_ci(tok3_start, tok3_len, "ospf")) {
                        if (reason_buf && reason_buf_size > 0)
                            snprintf(reason_buf, reason_buf_size, "clear ip routing: resets adjacencies");
                        return CMD_CRITICAL;
                    }
                }
            }
        }
        /* Other clear commands default to write */
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "clear command");
        return CMD_WRITE;
    }

    /* --- "no" prefix --- */
    if (tok_eq_ci(tok1_start, tok1_len, "no")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len)) {
            /* no router ospf/eigrp/bgp -> critical */
            if (tok_eq_ci(tok2_start, tok2_len, "router")) {
                const char *p3 = p2;
                if (next_token(&p3, &tok3_start, &tok3_len)) {
                    if (tok_eq_ci(tok3_start, tok3_len, "ospf") ||
                        tok_eq_ci(tok3_start, tok3_len, "eigrp") ||
                        tok_eq_ci(tok3_start, tok3_len, "bgp")) {
                        if (reason_buf && reason_buf_size > 0)
                            snprintf(reason_buf, reason_buf_size, "no router: removes routing process");
                        return CMD_CRITICAL;
                    }
                }
            }
            /* no spanning-tree vlan */
            if (tok_eq_ci(tok2_start, tok2_len, "spanning-tree")) {
                const char *p3 = p2;
                if (next_token(&p3, &tok3_start, &tok3_len) &&
                    tok_eq_ci(tok3_start, tok3_len, "vlan")) {
                    if (reason_buf && reason_buf_size > 0)
                        snprintf(reason_buf, reason_buf_size, "no spanning-tree vlan");
                    return CMD_CRITICAL;
                }
            }
            /* no vlan */
            if (tok_eq_ci(tok2_start, tok2_len, "vlan")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "no vlan: removes VLAN");
                return CMD_CRITICAL;
            }
        }
        /* no <anything else> -> write */
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "no (negation): modifies config");
        return CMD_WRITE;
    }

    /* --- Write commands --- */
    if (tok_eq_ci(tok1_start, tok1_len, "configure") ||
        tok_eq_ci(tok1_start, tok1_len, "ip") ||
        tok_eq_ci(tok1_start, tok1_len, "switchport") ||
        tok_eq_ci(tok1_start, tok1_len, "channel-group") ||
        tok_eq_ci(tok1_start, tok1_len, "router") ||
        tok_eq_ci(tok1_start, tok1_len, "network") ||
        tok_eq_ci(tok1_start, tok1_len, "route-map") ||
        tok_eq_ci(tok1_start, tok1_len, "prefix-list") ||
        tok_eq_ci(tok1_start, tok1_len, "access-list") ||
        tok_eq_ci(tok1_start, tok1_len, "username") ||
        tok_eq_ci(tok1_start, tok1_len, "hostname") ||
        tok_eq_ci(tok1_start, tok1_len, "banner") ||
        tok_eq_ci(tok1_start, tok1_len, "logging") ||
        tok_eq_ci(tok1_start, tok1_len, "copy") ||
        tok_eq_ci(tok1_start, tok1_len, "boot")) {
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "config command: %.*s",
                     (int)tok1_len, tok1_start);
        return CMD_WRITE;
    }
    /* write memory / write (without erase, already handled) */
    if (tok_eq_ci(tok1_start, tok1_len, "write")) {
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "write memory: saves config");
        return CMD_WRITE;
    }
    /* enable secret / enable password */
    if (tok_eq_ci(tok1_start, tok1_len, "enable")) {
        /* Already returned SAFE for bare "enable" above; if we get here
         * it means there's a subcommand like "enable secret" */
        return CMD_SAFE;  /* bare enable already handled */
    }
    if (tok_eq_ci(tok1_start, tok1_len, "ntp") ||
        tok_eq_ci(tok1_start, tok1_len, "interface") ||
        tok_eq_ci(tok1_start, tok1_len, "vlan") ||
        tok_eq_ci(tok1_start, tok1_len, "spanning-tree") ||
        tok_eq_ci(tok1_start, tok1_len, "snmp-server") ||
        tok_eq_ci(tok1_start, tok1_len, "line") ||
        tok_eq_ci(tok1_start, tok1_len, "service")) {
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "config command: %.*s",
                     (int)tok1_len, tok1_start);
        return CMD_WRITE;
    }

    /* Default for network devices: conservative -> CMD_WRITE */
    return CMD_WRITE;
}

/* ----- Per-segment Cisco NX-OS classification (inherits IOS + extras) ----- */

static CmdSafetyLevel classify_cisco_nxos_segment(const char *seg, size_t seg_len,
                                                    char *reason_buf, size_t reason_buf_size)
{
    const char *p = seg;
    const char *tok1_start, *tok2_start, *tok3_start;
    size_t tok1_len, tok2_len, tok3_len;

    (void)seg_len;

    if (!next_token(&p, &tok1_start, &tok1_len))
        return CMD_SAFE;

    /* NX-OS specific critical: reload module */
    if (tok_eq_ci(tok1_start, tok1_len, "reload")) {
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "reload: device/module reboot");
        return CMD_CRITICAL;
    }

    /* install all */
    if (tok_eq_ci(tok1_start, tok1_len, "install")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            tok_eq_ci(tok2_start, tok2_len, "all")) {
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size, "install all: system upgrade");
            return CMD_CRITICAL;
        }
    }

    /* no vpc / no vpc domain / no feature nv overlay / vpc role preempt */
    if (tok_eq_ci(tok1_start, tok1_len, "no")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len)) {
            if (tok_eq_ci(tok2_start, tok2_len, "vpc")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "no vpc: removes vPC config");
                return CMD_CRITICAL;
            }
            if (tok_eq_ci(tok2_start, tok2_len, "feature")) {
                const char *p3 = p2;
                if (next_token(&p3, &tok3_start, &tok3_len) &&
                    tok_prefix_ci(tok3_start, tok3_len, "nv")) {
                    if (reason_buf && reason_buf_size > 0)
                        snprintf(reason_buf, reason_buf_size, "no feature nv overlay: disables VXLAN");
                    return CMD_CRITICAL;
                }
            }
        }
        /* Fall through to IOS handler for other "no" commands */
    }

    if (tok_eq_ci(tok1_start, tok1_len, "vpc")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            tok_eq_ci(tok2_start, tok2_len, "role")) {
            const char *p3 = p2;
            if (next_token(&p3, &tok3_start, &tok3_len) &&
                tok_eq_ci(tok3_start, tok3_len, "preempt")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "vpc role preempt");
                return CMD_CRITICAL;
            }
        }
    }

    /* NX-OS specific write: feature, checkpoint, rollback */
    if (tok_eq_ci(tok1_start, tok1_len, "feature") ||
        tok_eq_ci(tok1_start, tok1_len, "checkpoint") ||
        tok_eq_ci(tok1_start, tok1_len, "rollback")) {
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "NX-OS config: %.*s",
                     (int)tok1_len, tok1_start);
        return CMD_WRITE;
    }

    /* Delegate to IOS classifier for everything else */
    return classify_cisco_ios_segment(seg, seg_len, reason_buf, reason_buf_size);
}

/* ----- Per-segment Cisco ASA classification ----- */

static CmdSafetyLevel classify_cisco_asa_segment(const char *seg, size_t seg_len,
                                                   char *reason_buf, size_t reason_buf_size)
{
    const char *p = seg;
    const char *tok1_start, *tok2_start, *tok3_start;
    size_t tok1_len, tok2_len, tok3_len;

    (void)seg_len;

    if (!next_token(&p, &tok1_start, &tok1_len))
        return CMD_SAFE;

    /* ASA specific critical: no failover, failover active/reload-standby */
    if (tok_eq_ci(tok1_start, tok1_len, "no")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len)) {
            if (tok_eq_ci(tok2_start, tok2_len, "failover")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "no failover: disables HA");
                return CMD_CRITICAL;
            }
            if (tok_eq_ci(tok2_start, tok2_len, "nameif")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "no nameif: removes interface name");
                return CMD_CRITICAL;
            }
            if (tok_eq_ci(tok2_start, tok2_len, "context")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "no context: removes security context");
                return CMD_CRITICAL;
            }
        }
        /* Fall through to IOS for other "no" commands */
    }

    if (tok_eq_ci(tok1_start, tok1_len, "failover")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len)) {
            if (tok_eq_ci(tok2_start, tok2_len, "active") ||
                tok_eq_ci(tok2_start, tok2_len, "reload-standby")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "failover action");
                return CMD_CRITICAL;
            }
        }
    }

    /* clear configure all */
    if (tok_eq_ci(tok1_start, tok1_len, "clear")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            tok_eq_ci(tok2_start, tok2_len, "configure")) {
            const char *p3 = p2;
            if (next_token(&p3, &tok3_start, &tok3_len) &&
                tok_eq_ci(tok3_start, tok3_len, "all")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "clear configure all: wipes config");
                return CMD_CRITICAL;
            }
        }
        /* Delegate other clear to IOS */
    }

    /* configure factory-default */
    if (tok_eq_ci(tok1_start, tok1_len, "configure")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            tok_prefix_ci(tok2_start, tok2_len, "factory")) {
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size, "configure factory-default");
            return CMD_CRITICAL;
        }
    }

    /* vpn-sessiondb logoff */
    if (tok_eq_ci(tok1_start, tok1_len, "vpn-sessiondb")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            tok_eq_ci(tok2_start, tok2_len, "logoff")) {
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size, "vpn-sessiondb logoff: disconnects VPN users");
            return CMD_CRITICAL;
        }
    }

    /* ASA specific write commands */
    if (tok_eq_ci(tok1_start, tok1_len, "nat") ||
        tok_eq_ci(tok1_start, tok1_len, "access-group") ||
        tok_eq_ci(tok1_start, tok1_len, "object-group") ||
        tok_eq_ci(tok1_start, tok1_len, "route") ||
        tok_eq_ci(tok1_start, tok1_len, "tunnel-group") ||
        tok_eq_ci(tok1_start, tok1_len, "group-policy") ||
        tok_eq_ci(tok1_start, tok1_len, "crypto") ||
        tok_eq_ci(tok1_start, tok1_len, "aaa") ||
        tok_eq_ci(tok1_start, tok1_len, "policy-map") ||
        tok_eq_ci(tok1_start, tok1_len, "service-policy") ||
        tok_eq_ci(tok1_start, tok1_len, "security-level") ||
        tok_eq_ci(tok1_start, tok1_len, "nameif")) {
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "ASA config: %.*s",
                     (int)tok1_len, tok1_start);
        return CMD_WRITE;
    }

    /* Delegate to IOS classifier for shared commands */
    return classify_cisco_ios_segment(seg, seg_len, reason_buf, reason_buf_size);
}

/* ----- Per-segment Aruba OS-CX classification ----- */

static CmdSafetyLevel classify_aruba_cx_segment(const char *seg, size_t seg_len,
                                                  char *reason_buf, size_t reason_buf_size)
{
    const char *p = seg;
    const char *tok1_start, *tok2_start, *tok3_start;
    size_t tok1_len, tok2_len, tok3_len;

    (void)seg_len;

    if (!next_token(&p, &tok1_start, &tok1_len))
        return CMD_SAFE;

    /* --- Safe commands --- */
    if (tok_prefix_ci(tok1_start, tok1_len, "show"))
        return CMD_SAFE;
    if (tok_eq_ci(tok1_start, tok1_len, "ping") ||
        tok_eq_ci(tok1_start, tok1_len, "traceroute") ||
        tok_eq_ci(tok1_start, tok1_len, "enable") ||
        tok_eq_ci(tok1_start, tok1_len, "disable") ||
        tok_eq_ci(tok1_start, tok1_len, "exit") ||
        tok_eq_ci(tok1_start, tok1_len, "end") ||
        tok_eq_ci(tok1_start, tok1_len, "dir"))
        return CMD_SAFE;

    /* --- Critical: standalone --- */
    if (tok_eq_ci(tok1_start, tok1_len, "reload")) {
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "reload: device reboot");
        return CMD_CRITICAL;
    }

    /* --- Two-token critical --- */
    {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len)) {
            /* erase startup-config */
            if (tok_eq_ci(tok1_start, tok1_len, "erase") &&
                tok_prefix_ci(tok2_start, tok2_len, "startup")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "erase startup-config");
                return CMD_CRITICAL;
            }
            /* erase all zeroize */
            if (tok_eq_ci(tok1_start, tok1_len, "erase") &&
                tok_eq_ci(tok2_start, tok2_len, "all")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "erase all");
                return CMD_CRITICAL;
            }
            /* checkpoint rollback */
            if (tok_eq_ci(tok1_start, tok1_len, "checkpoint") &&
                tok_eq_ci(tok2_start, tok2_len, "rollback")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "checkpoint rollback");
                return CMD_CRITICAL;
            }
            /* boot set-default */
            if (tok_eq_ci(tok1_start, tok1_len, "boot") &&
                tok_prefix_ci(tok2_start, tok2_len, "set-default")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "boot set-default");
                return CMD_CRITICAL;
            }
            /* delete flash */
            if (tok_eq_ci(tok1_start, tok1_len, "delete") &&
                tok_prefix_ci(tok2_start, tok2_len, "flash")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "delete flash");
                return CMD_CRITICAL;
            }
            /* redundancy switchover */
            if (tok_eq_ci(tok1_start, tok1_len, "redundancy") &&
                tok_eq_ci(tok2_start, tok2_len, "switchover")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "redundancy switchover");
                return CMD_CRITICAL;
            }
        }
    }

    /* --- "no" prefix critical --- */
    if (tok_eq_ci(tok1_start, tok1_len, "no")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len)) {
            if (tok_eq_ci(tok2_start, tok2_len, "vsx") ||
                tok_eq_ci(tok2_start, tok2_len, "stacking") ||
                tok_eq_ci(tok2_start, tok2_len, "spanning-tree")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "no %.*s: critical config removal",
                             (int)tok2_len, tok2_start);
                return CMD_CRITICAL;
            }
        }
        /* other "no" -> write */
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "no (negation): modifies config");
        return CMD_WRITE;
    }

    /* --- Write commands --- */
    if (tok_eq_ci(tok1_start, tok1_len, "configure") ||
        tok_eq_ci(tok1_start, tok1_len, "write") ||
        tok_eq_ci(tok1_start, tok1_len, "interface") ||
        tok_eq_ci(tok1_start, tok1_len, "vlan") ||
        tok_eq_ci(tok1_start, tok1_len, "router") ||
        tok_eq_ci(tok1_start, tok1_len, "ip") ||
        tok_eq_ci(tok1_start, tok1_len, "user") ||
        tok_eq_ci(tok1_start, tok1_len, "radius-server") ||
        tok_eq_ci(tok1_start, tok1_len, "aaa") ||
        tok_eq_ci(tok1_start, tok1_len, "hostname") ||
        tok_eq_ci(tok1_start, tok1_len, "ntp") ||
        tok_eq_ci(tok1_start, tok1_len, "mirror")) {
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "config command: %.*s",
                     (int)tok1_len, tok1_start);
        return CMD_WRITE;
    }

    /* Default for network devices: conservative -> CMD_WRITE */
    return CMD_WRITE;
}

/* ----- Per-segment ArubaOS (wireless) classification ----- */

static CmdSafetyLevel classify_aruba_os_segment(const char *seg, size_t seg_len,
                                                  char *reason_buf, size_t reason_buf_size)
{
    const char *p = seg;
    const char *tok1_start, *tok2_start, *tok3_start;
    size_t tok1_len, tok2_len, tok3_len;

    (void)seg_len;
    (void)tok3_start;
    (void)tok3_len;

    if (!next_token(&p, &tok1_start, &tok1_len))
        return CMD_SAFE;

    /* --- Safe commands --- */
    if (tok_prefix_ci(tok1_start, tok1_len, "show"))
        return CMD_SAFE;
    if (tok_eq_ci(tok1_start, tok1_len, "ping") ||
        tok_eq_ci(tok1_start, tok1_len, "traceroute") ||
        tok_eq_ci(tok1_start, tok1_len, "enable") ||
        tok_eq_ci(tok1_start, tok1_len, "disable") ||
        tok_eq_ci(tok1_start, tok1_len, "exit") ||
        tok_eq_ci(tok1_start, tok1_len, "end"))
        return CMD_SAFE;

    /* --- Critical: standalone --- */
    if (tok_eq_ci(tok1_start, tok1_len, "reload")) {
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "reload: device reboot");
        return CMD_CRITICAL;
    }
    if (tok_eq_ci(tok1_start, tok1_len, "factory-reset")) {
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "factory-reset: wipes device");
        return CMD_CRITICAL;
    }

    /* --- Two-token critical --- */
    {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len)) {
            /* write erase */
            if (tok_eq_ci(tok1_start, tok1_len, "write") &&
                tok_eq_ci(tok2_start, tok2_len, "erase")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "write erase: clears config");
                return CMD_CRITICAL;
            }
            /* cluster reset */
            if (tok_eq_ci(tok1_start, tok1_len, "cluster") &&
                tok_eq_ci(tok2_start, tok2_len, "reset")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "cluster reset");
                return CMD_CRITICAL;
            }
            /* clear ap */
            if (tok_eq_ci(tok1_start, tok1_len, "clear") &&
                tok_eq_ci(tok2_start, tok2_len, "ap")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "clear ap");
                return CMD_CRITICAL;
            }
            /* delete flash: */
            if (tok_eq_ci(tok1_start, tok1_len, "delete") &&
                tok_prefix_ci(tok2_start, tok2_len, "flash")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "delete flash");
                return CMD_CRITICAL;
            }
            /* whitelist-db del */
            if (tok_eq_ci(tok1_start, tok1_len, "whitelist-db") &&
                tok_eq_ci(tok2_start, tok2_len, "del")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "whitelist-db del");
                return CMD_CRITICAL;
            }
        }
    }

    /* ap wipe out */
    if (tok_eq_ci(tok1_start, tok1_len, "ap")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            tok_eq_ci(tok2_start, tok2_len, "wipe")) {
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size, "ap wipe: wipes access points");
            return CMD_CRITICAL;
        }
    }

    /* apboot */
    if (tok_eq_ci(tok1_start, tok1_len, "apboot")) {
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "apboot: reboots access points");
        return CMD_CRITICAL;
    }

    /* no vrrp */
    if (tok_eq_ci(tok1_start, tok1_len, "no")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len)) {
            if (tok_eq_ci(tok2_start, tok2_len, "vrrp")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "no vrrp: removes HA");
                return CMD_CRITICAL;
            }
        }
        /* other "no" -> write */
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "no (negation): modifies config");
        return CMD_WRITE;
    }

    /* --- Write commands --- */
    if (tok_eq_ci(tok1_start, tok1_len, "configure") ||
        tok_eq_ci(tok1_start, tok1_len, "write") ||
        tok_eq_ci(tok1_start, tok1_len, "ap-group") ||
        tok_eq_ci(tok1_start, tok1_len, "wlan") ||
        tok_eq_ci(tok1_start, tok1_len, "virtual-ap") ||
        tok_eq_ci(tok1_start, tok1_len, "ap") ||
        tok_eq_ci(tok1_start, tok1_len, "interface") ||
        tok_eq_ci(tok1_start, tok1_len, "ip") ||
        tok_eq_ci(tok1_start, tok1_len, "vlan") ||
        tok_eq_ci(tok1_start, tok1_len, "aaa") ||
        tok_eq_ci(tok1_start, tok1_len, "user-role") ||
        tok_eq_ci(tok1_start, tok1_len, "hostname") ||
        tok_eq_ci(tok1_start, tok1_len, "ntp") ||
        tok_eq_ci(tok1_start, tok1_len, "snmp-server") ||
        tok_eq_ci(tok1_start, tok1_len, "backup") ||
        tok_eq_ci(tok1_start, tok1_len, "restore")) {
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "config command: %.*s",
                     (int)tok1_len, tok1_start);
        return CMD_WRITE;
    }

    /* Default for network devices: conservative -> CMD_WRITE */
    return CMD_WRITE;
}

/* ----- Per-segment PAN-OS classification ----- */

static CmdSafetyLevel classify_panos_segment(const char *seg, size_t seg_len,
                                               char *reason_buf, size_t reason_buf_size)
{
    const char *p = seg;
    const char *tok1_start, *tok2_start, *tok3_start;
    size_t tok1_len, tok2_len, tok3_len;

    (void)seg_len;

    if (!next_token(&p, &tok1_start, &tok1_len))
        return CMD_SAFE;

    /* --- Safe commands --- */
    if (tok_prefix_ci(tok1_start, tok1_len, "show") ||
        tok_eq_ci(tok1_start, tok1_len, "less") ||
        tok_eq_ci(tok1_start, tok1_len, "diff") ||
        tok_eq_ci(tok1_start, tok1_len, "find") ||
        tok_eq_ci(tok1_start, tok1_len, "ping") ||
        tok_eq_ci(tok1_start, tok1_len, "traceroute") ||
        tok_eq_ci(tok1_start, tok1_len, "nslookup") ||
        tok_eq_ci(tok1_start, tok1_len, "test") ||
        tok_eq_ci(tok1_start, tok1_len, "exit") ||
        tok_eq_ci(tok1_start, tok1_len, "quit") ||
        tok_eq_ci(tok1_start, tok1_len, "top") ||
        tok_eq_ci(tok1_start, tok1_len, "up"))
        return CMD_SAFE;

    /* tail follow -> safe */
    if (tok_eq_ci(tok1_start, tok1_len, "tail")) {
        return CMD_SAFE;
    }

    /* scp export -> safe, scp import -> write */
    if (tok_eq_ci(tok1_start, tok1_len, "scp")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len)) {
            if (tok_eq_ci(tok2_start, tok2_len, "export"))
                return CMD_SAFE;
            if (tok_eq_ci(tok2_start, tok2_len, "import")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "scp import");
                return CMD_WRITE;
            }
        }
        return CMD_WRITE;
    }

    /* --- commit handling (most nuanced) --- */
    if (tok_eq_ci(tok1_start, tok1_len, "commit")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len)) {
            /* commit validate -> safe */
            if (tok_eq_ci(tok2_start, tok2_len, "validate"))
                return CMD_SAFE;
            /* commit force / commit partial -> critical */
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size, "commit: applies config changes");
            return CMD_CRITICAL;
        }
        /* bare "commit" -> critical */
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "commit: applies config changes");
        return CMD_CRITICAL;
    }
    /* commit-all -> critical */
    if (tok_prefix_ci(tok1_start, tok1_len, "commit-")) {
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "commit-all: pushes config to devices");
        return CMD_CRITICAL;
    }

    /* --- Critical standalone --- */
    if (tok_eq_ci(tok1_start, tok1_len, "delete")) {
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "delete: removes config objects");
        return CMD_CRITICAL;
    }
    if (tok_eq_ci(tok1_start, tok1_len, "rollback")) {
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "rollback: reverts config");
        return CMD_CRITICAL;
    }

    /* load config -> critical */
    if (tok_eq_ci(tok1_start, tok1_len, "load")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            tok_eq_ci(tok2_start, tok2_len, "config")) {
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size, "load config: replaces running config");
            return CMD_CRITICAL;
        }
    }

    /* --- request subcommands --- */
    if (tok_eq_ci(tok1_start, tok1_len, "request")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len)) {
            /* request restart / request shutdown -> critical */
            if (tok_eq_ci(tok2_start, tok2_len, "restart") ||
                tok_eq_ci(tok2_start, tok2_len, "shutdown")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "request %.*s: system action",
                             (int)tok2_len, tok2_start);
                return CMD_CRITICAL;
            }
            /* request system -> check subcommand */
            if (tok_eq_ci(tok2_start, tok2_len, "system")) {
                const char *p3 = p2;
                if (next_token(&p3, &tok3_start, &tok3_len)) {
                    if (tok_prefix_ci(tok3_start, tok3_len, "private-data-reset")) {
                        if (reason_buf && reason_buf_size > 0)
                            snprintf(reason_buf, reason_buf_size, "request system private-data-reset");
                        return CMD_CRITICAL;
                    }
                    if (tok_eq_ci(tok3_start, tok3_len, "software")) {
                        /* request system software install -> critical, download -> write */
                        const char *p4 = p3;
                        const char *tok4_start;
                        size_t tok4_len;
                        if (next_token(&p4, &tok4_start, &tok4_len)) {
                            if (tok_eq_ci(tok4_start, tok4_len, "install")) {
                                if (reason_buf && reason_buf_size > 0)
                                    snprintf(reason_buf, reason_buf_size, "request system software install");
                                return CMD_CRITICAL;
                            }
                            if (tok_eq_ci(tok4_start, tok4_len, "download")) {
                                if (reason_buf && reason_buf_size > 0)
                                    snprintf(reason_buf, reason_buf_size, "request system software download");
                                return CMD_WRITE;
                            }
                        }
                    }
                }
            }
            /* request certificate delete -> critical, generate/import -> write */
            if (tok_eq_ci(tok2_start, tok2_len, "certificate")) {
                const char *p3 = p2;
                if (next_token(&p3, &tok3_start, &tok3_len)) {
                    if (tok_eq_ci(tok3_start, tok3_len, "delete")) {
                        if (reason_buf && reason_buf_size > 0)
                            snprintf(reason_buf, reason_buf_size, "request certificate delete");
                        return CMD_CRITICAL;
                    }
                    if (tok_eq_ci(tok3_start, tok3_len, "generate") ||
                        tok_eq_ci(tok3_start, tok3_len, "import")) {
                        if (reason_buf && reason_buf_size > 0)
                            snprintf(reason_buf, reason_buf_size, "request certificate %.*s",
                                     (int)tok3_len, tok3_start);
                        return CMD_WRITE;
                    }
                }
            }
            /* request license deactivate -> critical, info -> safe, activate/fetch -> write */
            if (tok_eq_ci(tok2_start, tok2_len, "license")) {
                const char *p3 = p2;
                if (next_token(&p3, &tok3_start, &tok3_len)) {
                    if (tok_eq_ci(tok3_start, tok3_len, "deactivate")) {
                        if (reason_buf && reason_buf_size > 0)
                            snprintf(reason_buf, reason_buf_size, "request license deactivate");
                        return CMD_CRITICAL;
                    }
                    if (tok_eq_ci(tok3_start, tok3_len, "info"))
                        return CMD_SAFE;
                    if (tok_eq_ci(tok3_start, tok3_len, "activate") ||
                        tok_eq_ci(tok3_start, tok3_len, "fetch")) {
                        if (reason_buf && reason_buf_size > 0)
                            snprintf(reason_buf, reason_buf_size, "request license %.*s",
                                     (int)tok3_len, tok3_start);
                        return CMD_WRITE;
                    }
                }
            }
            /* request content/anti-virus/wildfire upgrade install -> critical */
            if (tok_eq_ci(tok2_start, tok2_len, "content") ||
                tok_eq_ci(tok2_start, tok2_len, "anti-virus") ||
                tok_eq_ci(tok2_start, tok2_len, "wildfire")) {
                /* Check for "upgrade install" in remaining tokens */
                const char *scan = p2;
                const char *ts;
                size_t tl;
                while (next_token(&scan, &ts, &tl)) {
                    if (tok_eq_ci(ts, tl, "install")) {
                        if (reason_buf && reason_buf_size > 0)
                            snprintf(reason_buf, reason_buf_size, "request %.*s upgrade install",
                                     (int)tok2_len, tok2_start);
                        return CMD_CRITICAL;
                    }
                }
            }
            /* request high-availability */
            if (tok_prefix_ci(tok2_start, tok2_len, "high-availability")) {
                const char *p3 = p2;
                if (next_token(&p3, &tok3_start, &tok3_len)) {
                    if (tok_eq_ci(tok3_start, tok3_len, "state") ||
                        tok_prefix_ci(tok3_start, tok3_len, "sync-to-remote")) {
                        if (reason_buf && reason_buf_size > 0)
                            snprintf(reason_buf, reason_buf_size, "request high-availability action");
                        return CMD_CRITICAL;
                    }
                }
            }
        }
        /* Default request -> write */
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "request command");
        return CMD_WRITE;
    }

    /* --- clear subcommands --- */
    if (tok_eq_ci(tok1_start, tok1_len, "clear")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len)) {
            /* clear session all -> critical, clear session id/filter -> write */
            if (tok_eq_ci(tok2_start, tok2_len, "session")) {
                const char *p3 = p2;
                if (next_token(&p3, &tok3_start, &tok3_len)) {
                    if (tok_eq_ci(tok3_start, tok3_len, "all")) {
                        if (reason_buf && reason_buf_size > 0)
                            snprintf(reason_buf, reason_buf_size, "clear session all: drops all sessions");
                        return CMD_CRITICAL;
                    }
                }
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "clear session");
                return CMD_WRITE;
            }
            /* clear log -> critical */
            if (tok_eq_ci(tok2_start, tok2_len, "log")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "clear log: deletes logs");
                return CMD_CRITICAL;
            }
            /* clear counter / clear arp / clear mac / clear ndp -> write */
            if (tok_eq_ci(tok2_start, tok2_len, "counter") ||
                tok_eq_ci(tok2_start, tok2_len, "arp") ||
                tok_eq_ci(tok2_start, tok2_len, "mac") ||
                tok_eq_ci(tok2_start, tok2_len, "ndp")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "clear %.*s",
                             (int)tok2_len, tok2_start);
                return CMD_WRITE;
            }
        }
        /* Default clear -> write */
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "clear command");
        return CMD_WRITE;
    }

    /* --- debug critical --- */
    if (tok_eq_ci(tok1_start, tok1_len, "debug")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len)) {
            if (tok_eq_ci(tok2_start, tok2_len, "software")) {
                const char *p3 = p2;
                if (next_token(&p3, &tok3_start, &tok3_len) &&
                    tok_eq_ci(tok3_start, tok3_len, "restart")) {
                    if (reason_buf && reason_buf_size > 0)
                        snprintf(reason_buf, reason_buf_size, "debug software restart");
                    return CMD_CRITICAL;
                }
            }
            if (tok_eq_ci(tok2_start, tok2_len, "dataplane")) {
                const char *p3 = p2;
                if (next_token(&p3, &tok3_start, &tok3_len) &&
                    tok_eq_ci(tok3_start, tok3_len, "reset")) {
                    if (reason_buf && reason_buf_size > 0)
                        snprintf(reason_buf, reason_buf_size, "debug dataplane reset");
                    return CMD_CRITICAL;
                }
            }
        }
    }

    /* --- Write commands --- */
    if (tok_eq_ci(tok1_start, tok1_len, "configure") ||
        tok_eq_ci(tok1_start, tok1_len, "set") ||
        tok_eq_ci(tok1_start, tok1_len, "edit") ||
        tok_eq_ci(tok1_start, tok1_len, "rename") ||
        tok_eq_ci(tok1_start, tok1_len, "copy") ||
        tok_eq_ci(tok1_start, tok1_len, "move") ||
        tok_eq_ci(tok1_start, tok1_len, "override") ||
        tok_eq_ci(tok1_start, tok1_len, "revert") ||
        tok_eq_ci(tok1_start, tok1_len, "import")) {
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "config command: %.*s",
                     (int)tok1_len, tok1_start);
        return CMD_WRITE;
    }

    /* save config -> write */
    if (tok_eq_ci(tok1_start, tok1_len, "save")) {
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "save config");
        return CMD_WRITE;
    }

    /* Default for PAN-OS: conservative -> CMD_WRITE */
    return CMD_WRITE;
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
                /* PAN-OS: | is a display filter, not a shell pipe */
                if (*p == '|' && platform != CMD_PLATFORM_PANOS) break;
                if (*p == ';') break;
                if (*p == '&' && *(p+1) == '&') break;
            }
            p++;
        }
        size_t seg_len = (size_t)(p - seg_start);

        CmdSafetyLevel seg_level;

        switch (platform) {
        case CMD_PLATFORM_CISCO_IOS:
            seg_level = classify_cisco_ios_segment(seg_start, seg_len,
                            worst == CMD_SAFE ? reason_buf : NULL,
                            worst == CMD_SAFE ? reason_buf_size : 0);
            break;
        case CMD_PLATFORM_CISCO_NXOS:
            seg_level = classify_cisco_nxos_segment(seg_start, seg_len,
                            worst == CMD_SAFE ? reason_buf : NULL,
                            worst == CMD_SAFE ? reason_buf_size : 0);
            break;
        case CMD_PLATFORM_CISCO_ASA:
            seg_level = classify_cisco_asa_segment(seg_start, seg_len,
                            worst == CMD_SAFE ? reason_buf : NULL,
                            worst == CMD_SAFE ? reason_buf_size : 0);
            break;
        case CMD_PLATFORM_ARUBA_CX:
            seg_level = classify_aruba_cx_segment(seg_start, seg_len,
                            worst == CMD_SAFE ? reason_buf : NULL,
                            worst == CMD_SAFE ? reason_buf_size : 0);
            break;
        case CMD_PLATFORM_ARUBA_OS:
            seg_level = classify_aruba_os_segment(seg_start, seg_len,
                            worst == CMD_SAFE ? reason_buf : NULL,
                            worst == CMD_SAFE ? reason_buf_size : 0);
            break;
        case CMD_PLATFORM_PANOS:
            seg_level = classify_panos_segment(seg_start, seg_len,
                            worst == CMD_SAFE ? reason_buf : NULL,
                            worst == CMD_SAFE ? reason_buf_size : 0);
            break;
        case CMD_PLATFORM_LINUX:
        default:
            seg_level = classify_linux_segment(seg_start, seg_len,
                            worst == CMD_SAFE ? reason_buf : NULL,
                            worst == CMD_SAFE ? reason_buf_size : 0);
            break;
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
