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
    /* filesystem / block-device destruction (spec 3.1) */
    "fsck", "e2fsck", "xfs_repair", "resize2fs", "blkdiscard", "sgdisk",
    "mdadm", "cryptsetup", "dmsetup", "losetup",
    /* takes resources away from a running system (spec 3.1) */
    "swapoff", "umount", "chroot", "kexec", "telinit",
    /* kernel module removal (spec 3.1) */
    "insmod", "rmmod",
    /* disables SELinux enforcement (spec 3.1) */
    "setenforce",
    /* wholesale firewall ruleset replacement (spec 3.1) */
    "iptables-restore",
    /* account removal / lock-out (spec 3.1) -- userdel moved here from
     * linux_write_cmds: removing an account is CRITICAL, not WRITE */
    "userdel", "groupdel", "deluser",
    /* whole-system upgrade / interface drop (spec 3.1) */
    "do-release-upgrade", "ifdown",
    NULL
};

static const char *linux_critical_prefixes[] = {
    "mkfs.",   /* mkfs.ext4, mkfs.xfs, etc. */
    NULL
};

static const char *linux_write_cmds[] = {
    "mv", "cp", "mkdir", "chmod", "chown", "ln", "touch", "rsync",
    "apt", "apt-get", "yum", "dnf", "pacman", "pip", "pip3", "npm", "yarn",
    /* userdel moved to linux_critical_cmds (spec 3.1: account removal) */
    "useradd", "usermod", "passwd", "groupadd",
    "make", "gcc", "cmake", "cargo", "go",
    "git", "svn",
    "docker", "kubectl",
    "crontab",
    "tar", "zip", "unzip", "gzip",
    "wget",
    "firewall-cmd",
    "tee",
    "vim", "vi", "nano", "emacs", "ed", "pico",
    "awk",
    /* spec 3.2 WRITE additions */
    "chattr", "setfacl", "visudo", "groupmod", "chgrp", "install", "patch",
    "bunzip2", "xz", "7z", "logrotate",
    "update-grub", "grub2-mkconfig", "dracut", "update-initramfs", "ldconfig",
    "ntpdate", "ansible", "ansible-playbook", "at", "batch", "systemd-run",
    "brctl", "ovs-vsctl", "logger",
    /* additional package managers, now flat WRITE by default like apt/dnf;
     * specific query subcommands stay SAFE via the unmatched-command
     * fallthrough, specific removal subcommands are upgraded to CRITICAL
     * below via linux_subcmd_rules (spec 3.1 F5) */
    "zypper", "apk", "rpm", "dpkg", "snap", "flatpak", "gem", "emerge",
    /* container / IaC / orchestration tooling (spec 3.1/3.2, F10) */
    "podman", "helm", "terraform",
    /* networking tooling (spec 3.1/3.2) */
    "nmcli", "netplan", "tc",
    NULL
};

/* Package managers: a dry-run flag anywhere makes the invocation read-only,
 * whatever the subcommand would otherwise be (apt-get purge --dry-run reports
 * what it would remove and removes nothing). */
static const char *pkg_mgr_cmds[] = {
    "apt", "apt-get", "aptitude", "dnf", "yum", "zypper", "pacman", "apk",
    "rpm", "dpkg", "snap", "flatpak", "emerge", "pip", "pip3", "npm", "yarn",
    NULL
};

static const char *pkg_mgr_dry_run_flags[] = {
    "--simulate", "--dry-run", "--just-print", "--no-act", "--recon",
    "--assume-no", NULL
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
    { "systemctl", "status",  CMD_READ },
    { "systemctl", "is-active", CMD_READ },
    { "systemctl", "is-enabled", CMD_READ },
    { "systemctl", "list-units", CMD_READ },
    /* docker subcommands */
    { "docker", "run",    CMD_WRITE },
    { "docker", "build",  CMD_WRITE },
    { "docker", "exec",   CMD_WRITE },
    { "docker", "rm",     CMD_CRITICAL },
    { "docker", "system", CMD_CRITICAL },
    { "docker", "ps",     CMD_READ },
    { "docker", "images", CMD_READ },
    { "docker", "logs",   CMD_READ },
    { "docker", "inspect", CMD_READ },
    /* kubectl subcommands */
    { "kubectl", "delete", CMD_CRITICAL },
    { "kubectl", "apply",  CMD_WRITE },
    { "kubectl", "create", CMD_WRITE },
    { "kubectl", "edit",   CMD_WRITE },
    { "kubectl", "patch",  CMD_WRITE },
    { "kubectl", "scale",  CMD_WRITE },
    { "kubectl", "get",    CMD_READ },
    { "kubectl", "describe", CMD_READ },
    { "kubectl", "logs",   CMD_READ },
    /* iptables */
    { "iptables", "-F", CMD_CRITICAL },
    { "iptables", "-P", CMD_CRITICAL },
    { "iptables", "-A", CMD_WRITE },
    { "iptables", "-I", CMD_WRITE },
    { "iptables", "-D", CMD_WRITE },
    { "iptables", "-L", CMD_READ },
    { "iptables", "-S", CMD_READ },
    /* ip subcommands */
    { "ip", "addr",  CMD_READ },
    { "ip", "route", CMD_READ },
    { "ip", "link",  CMD_READ },
    /* git subcommands */
    { "git", "push",   CMD_WRITE },
    { "git", "reset",  CMD_WRITE },
    { "git", "rebase", CMD_WRITE },
    { "git", "merge",  CMD_WRITE },
    { "git", "status", CMD_READ },
    { "git", "log",    CMD_READ },
    { "git", "diff",   CMD_READ },
    { "git", "show",   CMD_READ },
    { "git", "branch", CMD_READ },
    /* crontab */
    { "crontab", "-l", CMD_READ },
    { "crontab", "--list", CMD_READ },
    /* sed */
    { "sed", "-i", CMD_WRITE },
    /* curl/wget with output */
    { "curl", "-o", CMD_WRITE },
    { "curl", "-O", CMD_WRITE },
    { "wget", "-O", CMD_WRITE },

    /* --- spec 3.1/3.2 additions below --- */

    /* systemctl: outage-class subcommands (F6) */
    { "systemctl", "poweroff",     CMD_CRITICAL },
    { "systemctl", "reboot",       CMD_CRITICAL },
    { "systemctl", "halt",         CMD_CRITICAL },
    { "systemctl", "isolate",      CMD_CRITICAL },
    { "systemctl", "kill",         CMD_CRITICAL },
    { "systemctl", "set-default",  CMD_CRITICAL },
    { "systemctl", "daemon-reload", CMD_WRITE },
    /* initctl (upstart) stop */
    { "initctl", "stop", CMD_CRITICAL },
    /* modprobe -r: kernel module removal */
    { "modprobe", "-r", CMD_CRITICAL },
    /* nft: wholesale ruleset changes beyond the existing "flush" check */
    { "nft", "delete", CMD_CRITICAL },
    { "nft", "-f",     CMD_CRITICAL },
    /* firewall-cmd: wholesale firewall change */
    { "firewall-cmd", "--list-all",        CMD_READ },
    { "firewall-cmd", "--list-all-zones",  CMD_READ },
    { "firewall-cmd", "--state",           CMD_READ },
    { "firewall-cmd", "--panic-on",        CMD_CRITICAL },
    { "firewall-cmd", "--complete-reload", CMD_CRITICAL },
    /* passwd / usermod: account lock-out */
    { "passwd", "-d", CMD_CRITICAL },
    { "passwd", "-l", CMD_CRITICAL },
    { "usermod", "-L", CMD_CRITICAL },
    /* package manager removal subcommands (F5) */
    { "apt",     "remove",     CMD_CRITICAL },
    { "apt",     "purge",      CMD_CRITICAL },
    { "apt",     "autoremove", CMD_CRITICAL },
    { "apt-get", "remove",       CMD_CRITICAL },
    { "apt-get", "purge",        CMD_CRITICAL },
    { "apt-get", "autoremove",   CMD_CRITICAL },
    { "apt-get", "dist-upgrade", CMD_CRITICAL },
    { "dnf", "remove",       CMD_CRITICAL },
    { "dnf", "erase",        CMD_CRITICAL },
    { "dnf", "autoremove",   CMD_CRITICAL },
    { "dnf", "distro-sync",  CMD_CRITICAL },
    { "yum", "remove",     CMD_CRITICAL },
    { "yum", "erase",      CMD_CRITICAL },
    { "yum", "autoremove", CMD_CRITICAL },
    { "zypper", "rm",  CMD_CRITICAL },
    { "zypper", "dup", CMD_CRITICAL },
    { "pacman", "-R",    CMD_CRITICAL },
    { "pacman", "-Rns",  CMD_CRITICAL },
    { "pacman", "-Syu",  CMD_CRITICAL },
    { "apk", "del", CMD_CRITICAL },
    { "rpm", "-e",  CMD_CRITICAL },
    { "dpkg", "-r",      CMD_CRITICAL },
    { "dpkg", "--purge", CMD_CRITICAL },
    { "dpkg", "-P",      CMD_CRITICAL },
    { "snap", "remove", CMD_CRITICAL },
    { "flatpak", "uninstall", CMD_CRITICAL },
    { "emerge", "--unmerge", CMD_CRITICAL },
    { "emerge", "-C",         CMD_CRITICAL },
    { "pip",  "uninstall", CMD_CRITICAL },
    { "pip3", "uninstall", CMD_CRITICAL },
    /* docker: outage-class subcommands (F10) beyond existing rm/system */
    { "docker", "stop",    CMD_CRITICAL },
    { "docker", "kill",    CMD_CRITICAL },
    { "docker", "rmi",     CMD_CRITICAL },
    { "docker", "prune",   CMD_CRITICAL },
    { "docker", "start",   CMD_WRITE },
    { "docker", "restart", CMD_WRITE },
    { "docker", "pull",    CMD_WRITE },
    { "docker", "tag",     CMD_WRITE },
    { "docker", "commit",  CMD_WRITE },
    /* podman: mirrors docker */
    { "podman", "stop",    CMD_CRITICAL },
    { "podman", "kill",    CMD_CRITICAL },
    { "podman", "rmi",     CMD_CRITICAL },
    { "podman", "prune",   CMD_CRITICAL },
    { "podman", "rm",      CMD_CRITICAL },
    { "podman", "run",     CMD_WRITE },
    { "podman", "build",   CMD_WRITE },
    { "podman", "exec",    CMD_WRITE },
    { "podman", "start",   CMD_WRITE },
    { "podman", "restart", CMD_WRITE },
    { "podman", "pull",    CMD_WRITE },
    { "podman", "tag",     CMD_WRITE },
    { "podman", "commit",  CMD_WRITE },
    /* kubectl: eviction/rollback (F10) beyond existing delete/apply/... */
    { "kubectl", "drain",     CMD_CRITICAL },
    { "kubectl", "cordon",    CMD_CRITICAL },
    { "kubectl", "annotate",  CMD_WRITE },
    { "kubectl", "label",     CMD_WRITE },
    { "kubectl", "uncordon",  CMD_WRITE },
    /* helm */
    { "helm", "uninstall", CMD_CRITICAL },
    { "helm", "rollback",  CMD_CRITICAL },
    { "helm", "install",   CMD_WRITE },
    { "helm", "upgrade",   CMD_WRITE },
    /* terraform */
    { "helm", "list",    CMD_READ },
    { "helm", "status",  CMD_READ },
    { "helm", "get",     CMD_READ },
    { "helm", "history", CMD_READ },
    { "terraform", "destroy", CMD_CRITICAL },
    { "terraform", "apply",   CMD_WRITE },
    { "terraform", "init",    CMD_WRITE },
    { "terraform", "plan",     CMD_READ },
    { "terraform", "show",     CMD_READ },
    { "terraform", "output",   CMD_READ },
    { "terraform", "validate", CMD_READ },
    /* netplan */
    { "netplan", "apply", CMD_CRITICAL },
    /* wg (WireGuard) */
    { "wg", "set", CMD_WRITE },
    /* dmesg -C: clears the kernel ring buffer */
    { "dmesg", "-C", CMD_WRITE },
    /* crontab -e: edit the crontab */
    { "crontab", "-e", CMD_WRITE },
    /* hostnamectl / date: single named-subcommand forms */
    { "hostnamectl", "set-hostname", CMD_WRITE },
    { "date", "-s", CMD_WRITE },
    /* sysctl -w: writes a live kernel parameter */
    { "sysctl", "-w", CMD_WRITE },

    /* --- spec 3.3 additions (narrow: only the package-manager query
     * subcommands and "ufw status" that section 17's corner cases
     * require -- see the report for what was deliberately left out) --- */
    { "apt", "list", CMD_READ },
    { "apt", "show", CMD_READ },
    { "apt", "search", CMD_READ },
    { "apt", "policy", CMD_READ },
    { "dnf", "list", CMD_READ },
    { "dnf", "info", CMD_READ },
    { "dnf", "search", CMD_READ },
    { "dnf", "repolist", CMD_READ },
    { "yum", "list", CMD_READ },
    { "yum", "info", CMD_READ },
    { "rpm", "-qa", CMD_READ },
    { "rpm", "-qi", CMD_READ },
    { "rpm", "-ql", CMD_READ },
    { "dpkg", "-l", CMD_READ },
    { "dpkg", "-L", CMD_READ },
    { "dpkg", "-S", CMD_READ },
    { "pacman", "-Q",  CMD_READ },
    { "pacman", "-Qs", CMD_READ },
    { "pacman", "-Qi", CMD_READ },
    { "zypper", "se", CMD_READ },
    { "zypper", "search", CMD_READ },
    { "zypper", "info", CMD_READ },
    { "zypper", "repos", CMD_READ },
    { "zypper", "if", CMD_READ },
    { "zypper", "lr", CMD_READ },
    { "apk", "info", CMD_READ },
    { "snap", "list", CMD_READ },
    { "flatpak", "list", CMD_READ },
    { "pip",  "list",   CMD_READ },
    { "pip",  "show",   CMD_READ },
    { "pip",  "freeze", CMD_READ },
    { "pip3", "list",   CMD_READ },
    { "pip3", "show",   CMD_READ },
    { "pip3", "freeze", CMD_READ },
    { "npm", "ls",   CMD_READ },
    { "npm", "view", CMD_READ },

    /* --- further spec 3.3 additions: multi-token allow-list entries that
     * don't fit the single-token linux_read_cmds[] table (added while
     * wiring up the C2 allow-list fallthrough -- see the report for what
     * was deliberately left off) --- */
    { "systemctl", "cat",          CMD_READ },
    { "systemctl", "show",         CMD_READ },
    { "systemctl", "list-timers",  CMD_READ },
    { "nft",       "list",         CMD_READ },
    { "ip",        "neigh",        CMD_READ },
    { "route",     "-n",           CMD_READ },
    { "docker",    "stats",        CMD_READ },
    { "docker",    "top",          CMD_READ },
    { "kubectl",   "top",          CMD_READ },
    { "kubectl",   "explain",      CMD_READ },
    { "kubectl",   "api-resources", CMD_READ },
    { "kubectl",   "version",      CMD_READ },

    { NULL, NULL, CMD_READ }
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
    /* spec 3.1/3.2 additions */
    { "ip", "netns", "delete", CMD_CRITICAL },
    { "ip", "netns", "add",    CMD_WRITE },
    { "tc", "qdisc", "del", CMD_CRITICAL },
    { "btrfs", "subvolume", "delete", CMD_CRITICAL },
    { "docker", "volume",  "rm", CMD_CRITICAL },
    { "docker", "network", "rm", CMD_CRITICAL },
    { "docker", "compose", "down", CMD_CRITICAL },
    { "docker", "compose", "up",   CMD_WRITE },
    { "podman", "volume",  "rm", CMD_CRITICAL },
    { "podman", "network", "rm", CMD_CRITICAL },
    { "kubectl", "rollout",  "undo",    CMD_CRITICAL },
    { "kubectl", "replace",  "--force", CMD_CRITICAL },
    { "helm", "repo", "add", CMD_WRITE },
    { "terraform", "apply", "-auto-approve", CMD_CRITICAL },
    { "npm", "uninstall", "-g", CMD_CRITICAL },
    { "nmcli", "con",    "down",       CMD_CRITICAL },
    { "nmcli", "con",    "delete",     CMD_CRITICAL },
    { "nmcli", "device",  "disconnect", CMD_CRITICAL },
    { "nmcli", "con",    "up",     CMD_WRITE },
    { "nmcli", "con",    "modify", CMD_WRITE },
    { NULL, NULL, NULL, CMD_READ }
};

/* kill with -9 flag is critical, plain kill is write */
static const char *kill_critical_flags[] = { "-9", "-KILL", "-SIGKILL", NULL };

/* ----- C2 fix / spec 3.3: Linux SAFE allow-list -----
 * classify_linux_segment() no longer treats "no rule matched" as SAFE --
 * that was audit finding C2, where an unrecognised command classified SAFE
 * and auto-approved at the lowest auto-approve level. The table below is
 * coverage-spec section 3.3 lifted into an explicit allow-list, consulted
 * last -- after every write/critical rule above has had its chance to claim
 * the command, so "tar" (write list) and "find ... -delete" (critical,
 * scanned above) are unaffected by anything in it.
 *
 * Left out of this table, and handled separately, because they are safe
 * only in a bare/no-argument form and a blanket listing would wrongly wave
 * through their write forms too: "mount" (existing bare-check further
 * down, WRITE with any argument), "date" and "dmesg" (bare-check added
 * below; "date -s" / "dmesg -C" are already WRITE via linux_subcmd_rules),
 * "hostname" (bare-check added below).
 *
 * "reset" names no command in coverage spec 3.3, but is included here
 * because a bare "reset" really is the harmless terminfo terminal reset --
 * see test_cmd_classify_unknown_overlay_reset_forms, which relies on
 * classify_linux_segment("reset", ...) staying SAFE when the
 * CMD_PLATFORM_UNKNOWN overlay delegates a bare "reset" to it. */
static const char *linux_read_cmds[] = {
    "ls", "dir", "cat", "tac", "less", "more", "head", "tail",
    "grep", "egrep", "fgrep", "rg", "wc", "sort", "uniq", "cut", "tr",
    "column", "diff", "cmp", "md5sum", "sha1sum", "sha256sum",
    "file", "stat", "readlink", "realpath", "basename", "dirname", "tree",
    "pwd", "echo", "printf", "true", "false", "cal", "uptime",
    "w", "who", "whoami", "id", "groups", "uname",
    "lsb_release", "arch", "nproc",
    "free", "vmstat", "iostat", "mpstat", "sar", "pidstat",
    "ps", "pstree", "top", "htop",
    "df", "du", "lsblk", "blkid", "findmnt",
    "lsof", "lspci", "lsusb", "lscpu", "lsmod", "dmidecode", "sensors",
    "getenforce", "sestatus",
    "env", "printenv", "locale", "which", "whereis", "type", "man", "history",
    "netstat", "ss", "arp",
    "ping", "ping6", "traceroute", "tracepath", "mtr",
    "dig", "host", "nslookup", "whois",
    "last", "lastlog",
    "iptables-save",
    "reset",
    NULL
};

/* ----- Token extraction helpers ----- */

/* Defined after the quoting model below: splits one shell word, quote-aware,
 * skipping redirections. */
static int next_token(const char **p, const char **start, size_t *len);

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

/* Strips a leading path off a token -- POSIX '/' or Windows '\',
 * whichever appears last, so "/usr/bin/sh" and "C:\Windows\System32\
 * cmd.exe" both resolve to their final component -- and a trailing
 * ".exe", matched case-insensitively so "cmd.exe" and "CMD.EXE" both
 * become "cmd". This lets a Windows interpreter invoked by its full path
 * be recognised as a pipe target the same way as its bare name. */
static const char *strip_path(const char *tok, size_t len, size_t *out_len)
{
    const char *base = tok;
    for (size_t i = 0; i < len; i++) {
        if (tok[i] == '/' || tok[i] == '\\') base = tok + i + 1;
    }
    size_t blen = (size_t)((tok + len) - base);
    if (blen >= 4 && ci_memcmp(base + blen - 4, ".exe", 4) == 0)
        blen -= 4;
    *out_len = blen;
    return base;
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

static int tok_in_list(const char *tok, size_t tlen, const char *const *list)
{
    for (int i = 0; list[i]; i++) {
        if (tok_eq(tok, tlen, list[i])) return 1;
    }
    return 0;
}

static int tok_has_prefix(const char *tok, size_t tlen, const char *const *list)
{
    for (int i = 0; list[i]; i++) {
        if (tok_prefix(tok, tlen, list[i])) return 1;
    }
    return 0;
}

/* ----- M2: device-filesystem token helper -----
 * Matches, case-insensitively, a token that starts with one of the
 * network-device filesystem prefixes below. Used by the "delete",
 * "format", "erase", "squeeze" and "copy" rules across the Cisco/Aruba/HP
 * classifiers to scan every remaining token, not just token 2 (spec M2,
 * fixes F3: "delete /force /recursive flash:x" has "/force" as token 2). */
static const char *device_fs_prefixes[] = {
    "flash:", "bootflash:", "disk0:", "disk1:", "slot0:", "slot1:",
    "nvram:", "harddisk:", "usb0:", "usb1:", "system:", "cf:", "unix:",
    "sup-bootflash:", "volatile:", "logflash:",
    NULL
};

static int is_device_fs_token(const char *tok, size_t len)
{
    for (int i = 0; device_fs_prefixes[i]; i++) {
        if (tok_prefix_ci(tok, len, device_fs_prefixes[i])) return 1;
    }
    return 0;
}

/* Scan every remaining token in a segment for a device-filesystem token. */
static int seg_has_device_fs_token(const char *p)
{
    const char *ts;
    size_t tl;
    while (next_token(&p, &ts, &tl)) {
        if (is_device_fs_token(ts, tl)) return 1;
    }
    return 0;
}

/* ----- M4: verb-anywhere token scan -----
 * Case-insensitive search for an exact-match token anywhere in a segment.
 * MikroTik RouterOS puts its verb last ("/ip firewall filter remove
 * numbers=0"), so its classifier scans every token rather than only the
 * first (spec M4). */
static int seg_has_token_ci(const char *seg, size_t len, const char *lit)
{
    const char *p = seg;
    const char *end = seg + len;
    const char *ts;
    size_t tl;
    while (p < end && next_token(&p, &ts, &tl)) {
        if (tok_eq_ci(ts, tl, lit)) return 1;
    }
    return 0;
}

/* ----- Quoting model -----
 * A proposed command may reach a POSIX shell or PowerShell, and the two
 * disagree about escapes: a POSIX shell escapes the next character with a
 * backslash (outside single quotes), PowerShell escapes it with a backtick
 * and treats a backslash as an ordinary character. Inside single quotes
 * neither shell escapes anything; only the closing quote ends the span.
 *
 * A metacharacter (| ; & < >) is "active" when it is outside quotes and not
 * escaped. The segment splitter and the redirect scan read the command under
 * both interpretations. Where the two disagree about which characters are
 * active, or where a quote or escape is left open at the end, the command is
 * ambiguous: classify_core() then takes the worse of the two readings and
 * never reports it better than CMD_UNKNOWN. */
typedef enum { QMODE_POSIX = 0, QMODE_PWSH = 1 } QuoteMode;

/* The quoting reading every word-level scan (next_token, next_shell_word)
 * uses. classify_pass() sets it for the duration of one pass, so the
 * per-command flag checks split words exactly the way that pass's segment
 * splitter did. Thread-local: classification may run on more than one
 * thread. */
static _Thread_local QuoteMode tok_mode = QMODE_POSIX;

/* Recursion depth of classify_core() through command substitutions. */
static _Thread_local int subst_depth = 0;

typedef struct {
    QuoteMode mode;
    int in_sq;
    int in_dq;
    int dangling;   /* an escape character was the last thing in the input */
} QuoteScan;

/* PowerShell also accepts the typographic quotes as string delimiters:
 * U+2018..U+201B as single quotes and U+201C..U+201E as double quotes
 * (UTF-8 E2 80 98..9E). Returns 1 for a single, 2 for a double, else 0. */
static int pwsh_typo_quote(const char *p, const char *end)
{
    if (end - p >= 3 && (unsigned char)p[0] == 0xE2 && (unsigned char)p[1] == 0x80) {
        unsigned char c = (unsigned char)p[2];
        if (c >= 0x98 && c <= 0x9B) return 1;
        if (c >= 0x9C && c <= 0x9E) return 2;
    }
    return 0;
}

static int has_typo_quote(const char *s)
{
    const char *end = s + strlen(s);
    for (const char *p = s; p < end; p++)
        if (pwsh_typo_quote(p, end)) return 1;
    return 0;
}

static void quote_scan_init(QuoteScan *qs, QuoteMode mode)
{
    qs->mode = mode;
    qs->in_sq = 0;
    qs->in_dq = 0;
    qs->dangling = 0;
}

static int quote_scan_balanced(const QuoteScan *qs)
{
    return !qs->in_sq && !qs->in_dq && !qs->dangling;
}

/* Consume the character at *pp (and the character it escapes, if it is an
 * escape). Returns 1 when that character is active -- unquoted and not
 * escaped -- else 0. Always advances *pp by one or two, never past end. */
static int quote_step(QuoteScan *qs, const char **pp, const char *end)
{
    const char *p = *pp;
    char c = *p;
    char esc = (qs->mode == QMODE_POSIX) ? '\\' : '`';

    *pp = p + 1;
    if (qs->mode == QMODE_PWSH) {
        int tq = pwsh_typo_quote(p, end);
        if (tq) {
            *pp = p + 3;
            if (qs->in_sq) { if (tq == 1) qs->in_sq = 0; return 0; }
            if (qs->in_dq) { if (tq == 2) qs->in_dq = 0; return 0; }
            if (tq == 1) qs->in_sq = 1; else qs->in_dq = 1;
            return 0;
        }
    }
    if (qs->in_sq) {
        if (c == '\'') qs->in_sq = 0;
        return 0;
    }
    if (c == esc) {
        if (p + 1 < end) *pp = p + 2;
        else qs->dangling = 1;
        return 0;
    }
    if (qs->in_dq) {
        if (c == '"') qs->in_dq = 0;
        return 0;
    }
    if (c == '\'') { qs->in_sq = 1; return 0; }
    if (c == '"')  { qs->in_dq = 1; return 0; }
    return 1;
}

static int is_shell_meta(char c)
{
    return c == '|' || c == ';' || c == '&' || c == '<' || c == '>';
}

/* Next active metacharacter (and, with blanks set, word-splitting blank) at
 * or after *pp, or NULL at end of input. */
static const char *next_active_sep(QuoteScan *qs, const char **pp, const char *end,
                                   int blanks)
{
    while (*pp < end) {
        const char *here = *pp;
        if (quote_step(qs, pp, end) &&
            (is_shell_meta(*here) || (blanks && (*here == ' ' || *here == '\t'))))
            return here;
    }
    return NULL;
}

/* 1 when the POSIX and PowerShell readings of the command activate exactly
 * the same metacharacters (and, with blanks set, split words at the same
 * blanks), and both close every quote and escape. Blanks matter because the
 * per-command flag checks work word by word: "a\ -o F" is one word to a
 * POSIX shell and two to PowerShell. */
static int quoting_agrees(const char *command, int blanks)
{
    const char *end = command + strlen(command);
    QuoteScan a, b;
    const char *pa = command, *pb = command;
    quote_scan_init(&a, QMODE_POSIX);
    quote_scan_init(&b, QMODE_PWSH);
    for (;;) {
        const char *ma = next_active_sep(&a, &pa, end, blanks);
        const char *mb = next_active_sep(&b, &pb, end, blanks);
        if (ma != mb) return 0;
        if (!ma) break;
    }
    return quote_scan_balanced(&a) && quote_scan_balanced(&b);
}

/* ----- Word splitting -----
 * next_token() returns the next shell word of a segment as a raw span (the
 * quotes are still in it), reading quotes and escapes with tok_mode: a ';',
 * '|', '&', '<' or '>' inside quotes or escaped is part of the word, so a
 * flag after a quoted separator is still seen. It stops, returning 0, at an
 * active '|', ';' or '&' (the end of the segment). A redirection -- an
 * optional fd number, the operator (<, >, >>, >|, <>, <<, <<<, >&, <&, &>,
 * &>>) and its target word -- is skipped as a whole: it is not an argument
 * of the command, and scan_redirects() judges it separately. */

/* If p starts a redirection operator, returns the first character after the
 * operator; else NULL. */
static const char *redirect_op_end(const char *p, const char *end)
{
    const char *r = p;
    if (r + 1 < end && *r == '&' && r[1] == '>') {
        r += 2;
        if (r < end && *r == '>') r++;
        return r;
    }
    while (r < end && *r >= '0' && *r <= '9') r++;
    if (r >= end || (*r != '<' && *r != '>')) return NULL;
    if (*r == '>') {
        r++;
        if (r < end && (*r == '>' || *r == '&' || *r == '|')) r++;
    } else {
        r++;
        if (r < end && *r == '<') {
            r++;
            if (r < end && (*r == '<' || *r == '-')) r++;
        } else if (r < end && (*r == '&' || *r == '>')) {
            r++;
        }
    }
    return r;
}

/* End of the word starting at p (quote-aware, tok_mode). */
static const char *word_end(const char *p, const char *end)
{
    QuoteScan qs;
    quote_scan_init(&qs, tok_mode);
    while (p < end) {
        const char *here = p;
        char c = *here;
        if (quote_step(&qs, &p, end) &&
            (c == ' ' || c == '\t' || is_shell_meta(c)))
            return here;
    }
    return end;
}

static int next_token(const char **p, const char **start, size_t *len)
{
    const char *end = *p + strlen(*p);
    const char *q = *p;
    for (;;) {
        while (q < end && (*q == ' ' || *q == '\t')) q++;
        if (q >= end) { *p = q; return 0; }
        const char *r = redirect_op_end(q, end);
        if (r) {
            while (r < end && (*r == ' ' || *r == '\t')) r++;
            q = (r < end && !is_shell_meta(*r)) ? word_end(r, end) : r;
            continue;
        }
        if (*q == '|' || *q == ';' || *q == '&') { *p = q; return 0; }
        break;
    }
    *start = q;
    q = word_end(q, end);
    *p = q;
    *len = (size_t)(q - *start);
    return *len > 0;
}

/* 1 when a word's raw text hides a leading '-' behind a quote or an escape
 * ("'-o'", "\-o", "\"--output\""): a flag the per-command checks would
 * otherwise mistake for an operand. */
static int tok_is_obscured_flag(const char *tok, size_t len)
{
    size_t i = 0;
    int quoted = 0;
    while (i < len) {
        char c = tok[i];
        if (c == '\'' || c == '"') { quoted = 1; i++; continue; }
        if (c == '\\' || c == '`') { quoted = 1; i++; break; }
        if ((unsigned char)c == 0xE2 && pwsh_typo_quote(tok + i, tok + len)) {
            quoted = 1; i += 3; continue;
        }
        break;
    }
    return quoted && i < len && tok[i] == '-';
}

/* ----- Active shell expansion detection -----
 * Some allow-listed READ commands are safe only while every argument is
 * literal: an unquoted (or double-quoted, where it still expands) shell
 * expansion can produce a flag or path the per-command checks never see, so
 * a word containing one makes the segment at least UNKNOWN. Checked on the
 * word's raw span, quotes and escapes still in it -- callers that already
 * unquote a word (find, sed) must check the raw span before they do. */

static const char *const expansion_exempt_vars[] = {
    "HOME", "PWD", "USER", "LOGNAME", "HOSTNAME", NULL
};

static int is_exempt_var_name(const char *name, size_t len)
{
    return tok_in_list(name, len, expansion_exempt_vars);
}

/* Length of the shell identifier (letter/underscore then alnum/underscore)
 * starting at p, up to end; 0 when p does not start one. */
static size_t var_name_len(const char *p, const char *end)
{
    if (p >= end || !(isalpha((unsigned char)*p) || *p == '_')) return 0;
    size_t n = 1;
    while (p + n < end && (isalnum((unsigned char)p[n]) || p[n] == '_')) n++;
    return n;
}

/* 1 when tok[0..len) contains a shell expansion active under the word's own
 * quoting: brace expansion ("{a,b}" / "{1..3}") and $'...'/$"..." only when
 * fully unquoted (bash never expands either inside single or double
 * quotes); $@ $* $# $? $- $$ $! $0-$9, ${...} and $NAME whenever not inside
 * single quotes (double quotes do not block a variable expansion). A
 * leading '~' is never treated as an expansion here -- tilde expansion is
 * exempt. HOME/PWD/USER/LOGNAME/HOSTNAME are exempt too, bare or spelled
 * "${NAME}". */
static int tok_has_active_expansion(const char *tok, size_t len)
{
    int in_sq = 0, in_dq = 0;
    for (size_t i = 0; i < len; i++) {
        char c = tok[i];
        if (in_sq) {
            if (c == '\'') in_sq = 0;
            continue;
        }
        if (c == '\\') { if (i + 1 < len) i++; continue; }
        if (c == '\'') { in_sq = 1; continue; }
        if (c == '"') { in_dq = !in_dq; continue; }
        if (c == '{' && !in_dq) {
            size_t j = i + 1;
            int has_comma = 0, has_range = 0;
            while (j < len && tok[j] != '}') {
                if (tok[j] == ',') has_comma = 1;
                if (tok[j] == '.' && j + 1 < len && tok[j + 1] == '.') has_range = 1;
                j++;
            }
            if (j < len && (has_comma || has_range)) return 1;
            continue;
        }
        if (c != '$') continue;
        size_t j = i + 1;
        if (j >= len) continue;
        if (!in_dq && (tok[j] == '\'' || tok[j] == '"')) return 1;   /* $'...' $"..." */
        if (strchr("@*#?$!-", tok[j])) return 1;
        if (tok[j] >= '0' && tok[j] <= '9') return 1;
        if (tok[j] == '{') {
            size_t k = j + 1;
            size_t nlen = var_name_len(tok + k, tok + len);
            if (nlen > 0 && k + nlen < len && tok[k + nlen] == '}' &&
                is_exempt_var_name(tok + k, nlen)) {
                i = k + nlen;      /* skip past the exempt "${NAME}" */
                continue;
            }
            return 1;
        }
        {
            size_t nlen = var_name_len(tok + j, tok + len);
            if (nlen > 0) {
                if (is_exempt_var_name(tok + j, nlen)) { i = j + nlen - 1; continue; }
                return 1;
            }
        }
    }
    return 0;
}

/* ----- Command substitution -----
 * "$(" (POSIX and PowerShell), a POSIX backtick, "<(" / ">(", and -- on an
 * unresolved platform, where the command may reach PowerShell -- any
 * unquoted "(" (PowerShell's "(...)" and "@(...)" run the command inside
 * them as part of an argument). What such a segment runs is not visible to
 * the per-segment rules, so it is never READ; the inner command is also
 * classified in full (up to a small nesting depth) and the segment gets its
 * level if that is worse. */

static CmdSafetyLevel classify_core(const char *command, CmdPlatform platform,
                                    char *reason_buf, size_t reason_buf_size,
                                    unsigned *mask_out);

/* Classifies the text between open (just after the opening '(' or '`') and
 * its matching close, read with mode. *resume is set just past the close
 * (or to end), so the caller's scan does not revisit the inner text. */
static CmdSafetyLevel inner_level(const char *open, const char *end, QuoteMode mode,
                                  int backtick, CmdPlatform platform,
                                  const char **resume)
{
    char buf[1024];
    const char *p = open;
    const char *close = end;
    *resume = end;
    if (backtick) {
        while (p < end) {
            if (*p == '\\' && p + 1 < end) { p += 2; continue; }
            if (*p == '`') { close = p; break; }
            p++;
        }
    } else {
        QuoteScan qs;
        int depth = 1;
        quote_scan_init(&qs, mode);
        while (p < end) {
            const char *here = p;
            if (!quote_step(&qs, &p, end)) continue;
            if (*here == '(') depth++;
            else if (*here == ')' && --depth == 0) { close = here; break; }
        }
    }
    if (close < end) *resume = close + 1;
    size_t n = (size_t)(close - open);
    if (n == 0) return CMD_UNKNOWN;
    if (n >= sizeof buf || subst_depth >= 4) return CMD_UNKNOWN;
    memcpy(buf, open, n);
    buf[n] = '\0';
    QuoteMode saved = tok_mode;
    subst_depth++;
    CmdSafetyLevel lvl = classify_core(buf, platform, NULL, 0, NULL);
    subst_depth--;
    tok_mode = saved;
    return lvl < CMD_UNKNOWN ? CMD_UNKNOWN : lvl;
}

/* CMD_READ when the segment has no substitution under mode, else at least
 * CMD_UNKNOWN (the inner command's level when that is worse). */
static CmdSafetyLevel seg_substitution_level_mode(const char *seg, size_t seg_len,
                                                  QuoteMode mode, CmdPlatform platform)
{
    const char *end = seg + seg_len;
    const char *p = seg;
    int in_sq = 0, in_dq = 0;
    char esc = (mode == QMODE_POSIX) ? '\\' : '`';
    CmdSafetyLevel worst = CMD_READ;
    while (p < end) {
        char c = *p;
        if (mode == QMODE_PWSH) {
            int tq = pwsh_typo_quote(p, end);
            if (tq) {
                if (in_sq) { if (tq == 1) in_sq = 0; }
                else if (in_dq) { if (tq == 2) in_dq = 0; }
                else if (tq == 1) in_sq = 1;
                else in_dq = 1;
                p += 3;
                continue;
            }
        }
        if (in_sq) {
            if (c == '\'') in_sq = 0;
            p++;
            continue;
        }
        if (c == esc) { p += (p + 1 < end) ? 2 : 1; continue; }
        if (c == '\'' && !in_dq) { in_sq = 1; p++; continue; }
        if (c == '"') { in_dq = !in_dq; p++; continue; }
        if (c == '`') { /* POSIX command substitution */
            CmdSafetyLevel l = inner_level(p + 1, end, mode, 1, platform, &p);
            if (l > worst) worst = l;
            continue;
        }
        if ((c == '$' || c == '<' || c == '>') && p + 1 < end && p[1] == '(') {
            CmdSafetyLevel l = inner_level(p + 2, end, mode, 0, platform, &p);
            if (l > worst) worst = l;
            continue;
        }
        if (c == '(' && !in_dq && mode == QMODE_PWSH &&
            platform == CMD_PLATFORM_UNKNOWN) {
            CmdSafetyLevel l = inner_level(p + 1, end, mode, 0, platform, &p);
            if (l > worst) worst = l;
            continue;
        }
        p++;
    }
    return worst;
}

static CmdSafetyLevel seg_substitution_level(const char *seg, size_t seg_len,
                                             CmdPlatform platform)
{
    CmdSafetyLevel a = seg_substitution_level_mode(seg, seg_len, QMODE_POSIX, platform);
    CmdSafetyLevel b = seg_substitution_level_mode(seg, seg_len, QMODE_PWSH, platform);
    return a > b ? a : b;
}

/* ----- Redirect scanning ----- */

/* 1 when the redirect target starting at r is exactly lit: the next
 * character ends the word. "/dev/nullF" and "&2F" are other files. */
static int redirect_target_is(const char *r, const char *end, const char *lit)
{
    size_t n = strlen(lit);
    if ((size_t)(end - r) < n || memcmp(r, lit, n) != 0) return 0;
    r += n;
    return r >= end || *r == ' ' || *r == '\t' || *r == ')' || is_shell_meta(*r);
}

static CmdSafetyLevel scan_redirects_mode(const char *seg, size_t seg_len,
                                          QuoteMode mode)
{
    const char *end = seg + seg_len;
    const char *p = seg;
    QuoteScan qs;
    quote_scan_init(&qs, mode);
    while (p < end) {
        const char *here = p;
        if (!quote_step(&qs, &p, end)) continue;
        if (*here == '>' || (*here == '&' && (here + 1) < end && *(here + 1) == '>')) {
            const char *r = here;
            if (*r == '&') r++;
            r++;
            if (r < end && *r == '>') r++;
            while (r < end && (*r == ' ' || *r == '\t')) r++;
            if (redirect_target_is(r, end, "/dev/null"))
                { p = r + 9; continue; }
            if (redirect_target_is(r, end, "&1") || redirect_target_is(r, end, "&2") ||
                redirect_target_is(r, end, "&-"))
                { p = r + 2; continue; }
            return CMD_WRITE;
        }
    }
    return CMD_READ;
}

/* An output redirect active under either quoting reading is a write. */
static CmdSafetyLevel scan_redirects(const char *seg, size_t seg_len)
{
    CmdSafetyLevel a = scan_redirects_mode(seg, seg_len, QMODE_POSIX);
    CmdSafetyLevel b = scan_redirects_mode(seg, seg_len, QMODE_PWSH);
    return a > b ? a : b;
}

/* ----- Pipe-to-dangerous scanning ----- */

/* Matches "iex" / "Invoke-Expression" (PowerShell's call-a-string-as-code
 * cmdlet and its alias), case-insensitively -- PowerShell itself ignores
 * case -- and also the call-operator form glued directly to an opening
 * paren with no space: "IEX(iwr x)" tokenises as one word, "IEX(iwr",
 * since next_token() only stops on whitespace and the shell
 * metacharacters. The 3-letter prefix only matches when the following
 * character is not an identifier character, so "iexplore" (a different,
 * real program) is never caught by it. */
static int tok_is_iex_or_invoke_expression(const char *tok, size_t len)
{
    if (tok_eq_ci(tok, len, "iex") || tok_eq_ci(tok, len, "Invoke-Expression"))
        return 1;
    if (len > 3 && ci_memcmp(tok, "iex", 3) == 0) {
        char c = tok[3];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return 1;
    }
    return 0;
}

/* The interpreters CRITICAL as a pipe target: "curl ... | <this>" runs
 * whatever was downloaded. sh/bash/zsh/dash/ksh/fish/busybox and the
 * scripting-language REPLs are matched case-sensitively (their names are
 * case-sensitive on the platforms that ship them); pwsh/powershell/cmd
 * are matched case-insensitively, like iex/Invoke-Expression above,
 * because PowerShell and cmd.exe both ignore case. python3.x (any minor
 * version) is covered by the "python3." prefix. */
static int is_critical_pipe_interpreter(const char *base, size_t base_len)
{
    static const char *case_sensitive_interpreters[] = {
        "sh", "bash", "zsh", "dash", "ksh", "fish", "busybox",
        "python", "python2", "python3",
        "perl", "ruby", "node", "php",
        NULL
    };
    if (tok_in_list(base, base_len, case_sensitive_interpreters))
        return 1;
    if (tok_prefix(base, base_len, "python3."))
        return 1;
    if (tok_eq_ci(base, base_len, "pwsh") || tok_eq_ci(base, base_len, "powershell")
        || tok_eq_ci(base, base_len, "cmd"))
        return 1;
    if (tok_is_iex_or_invoke_expression(base, base_len))
        return 1;
    return 0;
}

/* Wrappers that never themselves decide a pipe target's severity -- the
 * command they launch does. Peeled off (with their flags and, for "env",
 * VAR=value assignments) so "curl x | sudo bash" and "curl x | env sh"
 * are recognised through them. */
static int tok_is_pipe_wrapper(const char *tok, size_t len)
{
    return tok_eq(tok, len, "sudo") || tok_eq(tok, len, "doas")
        || tok_eq(tok, len, "env") || tok_eq(tok, len, "nice")
        || tok_eq(tok, len, "exec") || tok_eq(tok, len, "command");
}

/* A "-flag" or a VAR=value assignment (env's own syntax): skipped while
 * peeling wrappers off a pipe target, neither is itself the interpreter. */
static int tok_is_flag_or_assignment(const char *tok, size_t len)
{
    if (len == 0) return 0;
    if (tok[0] == '-') return 1;
    for (size_t i = 0; i < len; i++) {
        if (tok[i] == '=') return 1;
    }
    return 0;
}

static CmdSafetyLevel scan_pipe_target(const char *seg)
{
    const char *p = seg;
    const char *tok_start;
    size_t tok_len;

    if (!next_token(&p, &tok_start, &tok_len)) return CMD_READ;

    const char *base;
    size_t base_len;
    base = strip_path(tok_start, tok_len, &base_len);

    if (is_critical_pipe_interpreter(base, base_len))
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

    /* "sudo tee ..." writes as root through what looks like an ordinary
     * pipe target; checked before the generic wrapper peel below because
     * "tee" is not an interpreter and would otherwise read as harmless. */
    if (tok_eq(base, base_len, "sudo")) {
        const char *save_p = p;
        const char *next_start;
        size_t next_len;
        if (next_token(&p, &next_start, &next_len)) {
            if (tok_eq(next_start, next_len, "tee"))
                return CMD_CRITICAL;
        }
        p = save_p;
    }

    /* Peel off wrapper commands and their flags/assignments to find the
     * real interpreter: "curl x | sudo bash", "curl x | env sh". */
    while (tok_is_pipe_wrapper(base, base_len)) {
        if (!next_token(&p, &tok_start, &tok_len)) return CMD_READ;
        while (tok_is_flag_or_assignment(tok_start, tok_len)) {
            if (!next_token(&p, &tok_start, &tok_len)) return CMD_READ;
        }
        base = strip_path(tok_start, tok_len, &base_len);
        if (is_critical_pipe_interpreter(base, base_len))
            return CMD_CRITICAL;
    }

    return CMD_READ;
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
                    return CMD_READ;
                sql++;
            }
            return CMD_WRITE;
        }
        p++;
    }
    return CMD_READ;
}

/* ----- Flag-and-argument hardening helpers -----
 * A command that is only READ because it matched an allow-list entry (the
 * subcommand tables above, or linux_read_cmds below) must not stay READ
 * when a flag or argument on the same line writes a file, sends data,
 * changes state or runs another program. These helpers classify individual
 * flags/tokens for that purpose; classify_linux_segment() calls them before
 * any rule that would otherwise return READ for the command in question. */

static CmdSafetyLevel classify_linux_segment(const char *seg, size_t seg_len,
                                              char *reason_buf, size_t reason_buf_size);

/* ----- Flag allow-lists -----
 * A command that is READ only because it is allow-listed stays READ only
 * while every flag on the line is one known not to write, upload, delete or
 * run another program. Each such command lists those flags in a FlagSpec;
 * any other flag -- including an abbreviated long option, which getopt_long
 * and git accept ("--o=F" for "--output=F") and an attached short value on
 * a letter that is not listed ("-PX", "-oF") -- makes the segment at least
 * UNKNOWN, or WRITE when it is (or abbreviates) one known to write. */

typedef struct {
    const char *name;   /* "--name" */
    int val;            /* LV_NONE, LV_OPT (only as "=value"), LV_REQ */
} LongOpt;

enum { LV_NONE = 0, LV_OPT = 1, LV_REQ = 2 };

typedef struct {
    const char *short_noval;   /* letters that take no value */
    const char *short_val;     /* letters whose value is the rest of the word or the next word */
    const char *short_optval;  /* letters whose optional value is the rest of the word */
    const char *short_write;   /* letters known to write: WRITE */
    int short_digits;          /* "-5" (a count) is allowed */
    const LongOpt *longs;      /* allowed long options, {NULL,0}-terminated */
    const char *const *long_write; /* long options known to write: exact or abbreviated -> WRITE */
    int long_no_separate;      /* LV_REQ values only as "--name=value" (git's revision options) */
} FlagSpec;

static int long_in_write_list(const char *name, size_t nlen, const char *const *list)
{
    if (!list || nlen < 3) return 0;          /* "--" plus at least one letter */
    for (int i = 0; list[i]; i++) {
        size_t l = strlen(list[i]);
        if (nlen <= l && memcmp(name, list[i], nlen) == 0) return 1;
    }
    return 0;
}

/* Classifies one argument word against fs. Returns 1 when the word is an
 * operand (not a flag) and 0 when it was a flag (its level folded into
 * *level). A separate value word is consumed from *scan. "--" sets
 * *operands_only. */
static int flag_word(const FlagSpec *fs, const char *ts, size_t tl, const char **scan,
                     CmdSafetyLevel *level, int *operands_only)
{
    if (*operands_only) return 1;
    if (tok_has_active_expansion(ts, tl)) {
        if (*level < CMD_UNKNOWN) *level = CMD_UNKNOWN;
    }
    if (tok_is_obscured_flag(ts, tl)) {
        if (*level < CMD_UNKNOWN) *level = CMD_UNKNOWN;
        return 0;
    }
    if (tl < 2 || ts[0] != '-') return 1;     /* operand, or "-" (stdin) */
    if (tl == 2 && ts[1] == '-') { *operands_only = 1; return 0; }

    if (ts[1] == '-') {
        size_t nlen = 0;
        while (nlen < tl && ts[nlen] != '=') nlen++;
        int has_eq = nlen < tl;
        if (fs->longs) {
            for (int i = 0; fs->longs[i].name; i++) {
                const LongOpt *lo = &fs->longs[i];
                if (strlen(lo->name) != nlen || memcmp(ts, lo->name, nlen) != 0) continue;
                if (has_eq && lo->val == LV_NONE) break;          /* value on a no-value flag */
                if (!has_eq && lo->val == LV_REQ && !fs->long_no_separate) {
                    const char *v; size_t vl;
                    (void)next_token(scan, &v, &vl);
                }
                return 0;
            }
        }
        if (long_in_write_list(ts, nlen, fs->long_write)) {
            if (*level < CMD_WRITE) *level = CMD_WRITE;
        } else if (*level < CMD_UNKNOWN) {
            *level = CMD_UNKNOWN;
        }
        return 0;
    }

    for (size_t i = 1; i < tl; i++) {
        char c = ts[i];
        if (fs->short_write && strchr(fs->short_write, c)) {
            if (*level < CMD_WRITE) *level = CMD_WRITE;
            return 0;
        }
        if (fs->short_noval && strchr(fs->short_noval, c)) continue;
        if (fs->short_digits && c >= '0' && c <= '9') continue;
        if (fs->short_optval && strchr(fs->short_optval, c)) return 0;
        if (fs->short_val && strchr(fs->short_val, c)) {
            if (i + 1 == tl) {
                const char *v; size_t vl;
                (void)next_token(scan, &v, &vl);
            }
            return 0;
        }
        if (*level < CMD_UNKNOWN) *level = CMD_UNKNOWN;
        return 0;
    }
    return 0;
}

/* Every word from p to the end of the segment against fs. *operands gets
 * the number of operand words (may be NULL). */
static CmdSafetyLevel flag_scan(const FlagSpec *fs, const char *p, int *operands)
{
    CmdSafetyLevel level = CMD_READ;
    int only = 0, n = 0;
    const char *ts;
    size_t tl;
    while (next_token(&p, &ts, &tl)) {
        if (flag_word(fs, ts, tl, &p, &level, &only)) n++;
    }
    if (operands) *operands = n;
    return level;
}

/* ----- sed: -i in any spelling, and a script that writes (w/W command,
 * s///w or s///e) or reads from a file (-f/--file). ----- */

static int sed_is_safe_long_flag(const char *tok, size_t len)
{
    static const char *safe[] = {
        "--quiet", "--silent", "--expression", "--regexp-extended",
        "--separate", "--null-data", "--unbuffered", "--posix",
        "--debug", "--sandbox", NULL
    };
    for (int i = 0; safe[i]; i++) {
        size_t slen = strlen(safe[i]);
        if (len == slen && memcmp(tok, safe[i], slen) == 0) return 1;
    }
    return 0;
}

/* Reads the next shell word at *pp, up to end, with POSIX quoting: '...'
 * is literal, "..." and a bare backslash escape the next character. Stops
 * at unquoted whitespace or an unquoted | ; & < >. Writes the unquoted text
 * to out (NUL-terminated; *truncated set when it did not fit) and the raw
 * span to raw_start and raw_end. Returns 0 when no word remains. Unlike
 * next_token(), an escaped or quoted ';' (find's "\;" terminator) is part
 * of a word, so a scan does not stop there. */
static int next_shell_word(const char **pp, const char *end,
                           char *out, size_t out_size, int *truncated,
                           const char **raw_start, const char **raw_end)
{
    const char *p = *pp;
    size_t n = 0;
    int in_sq = 0, in_dq = 0;
    int pwsh = (tok_mode == QMODE_PWSH);
    char esc = pwsh ? '`' : '\\';

    *truncated = 0;
    for (;;) {
        while (p < end && (*p == ' ' || *p == '\t')) p++;
        if (p >= end) { *pp = p; return 0; }
        const char *r = redirect_op_end(p, end);
        if (!r) break;
        while (r < end && (*r == ' ' || *r == '\t')) r++;
        p = (r < end && !is_shell_meta(*r)) ? word_end(r, end) : r;
    }
    if (*p == '|' || *p == ';' || *p == '&') {
        *pp = p;
        return 0;
    }
    *raw_start = p;
    while (p < end) {
        char c = *p;
        int tq = pwsh ? pwsh_typo_quote(p, end) : 0;
        if (in_sq) {
            if (tq == 1) { p += 3; in_sq = 0; continue; }
            p++;
            if (c == '\'') { in_sq = 0; continue; }
        } else if (in_dq) {
            if (tq == 2) { p += 3; in_dq = 0; continue; }
            p++;
            if (c == '"') { in_dq = 0; continue; }
            if (c == esc && p < end) c = *p++;
        } else {
            if (c == ' ' || c == '\t' || c == '|' || c == ';' || c == '&' ||
                c == '<' || c == '>')
                break;
            if (tq) { p += 3; if (tq == 1) in_sq = 1; else in_dq = 1; continue; }
            p++;
            if (c == '\'') { in_sq = 1; continue; }
            if (c == '"')  { in_dq = 1; continue; }
            if (c == esc && p < end) c = *p++;
        }
        if (n + 1 < out_size) out[n++] = c;
        else *truncated = 1;
    }
    out[n] = '\0';
    *raw_end = p;
    *pp = p;
    return 1;
}

/* Level of one sed script (already unquoted): WRITE for a w/W command or
 * an s///w flag, at least UNKNOWN for an e command or an s///e flag. Every
 * command's address is skipped first, so "1w F", "/x/w F" and "$W F" are
 * seen. Parsing errs towards finding a command: anything that is not an s
 * or y command runs to the next ';' or newline, so a w/e hidden after a
 * ';' in text a real sed would treat as an argument still counts. */
/* Skips a regex body up to (not past) its delimiter d. With brackets set,
 * a bracket expression "[...]" is skipped whole, so a delimiter inside it
 * ("[/]") does not end the regex -- what GNU sed does. Returns NULL when
 * the delimiter never comes. */
static const char *sed_skip_regex(const char *p, char d, int brackets)
{
    while (*p && *p != d) {
        if (*p == '\\' && p[1]) { p += 2; continue; }
        if (brackets && *p == '[') {
            const char *q = p + 1;
            if (*q == '^') q++;
            if (*q == ']') q++;              /* a leading ']' is literal */
            while (*q && *q != ']') {
                if (*q == '[' && (q[1] == ':' || q[1] == '.' || q[1] == '=')) {
                    char k = q[1];
                    q += 2;
                    while (*q && !(*q == k && q[1] == ']')) q++;
                    if (*q) q += 2;
                    continue;
                }
                q++;
            }
            if (!*q) return NULL;
            p = q + 1;
            continue;
        }
        p++;
    }
    return *p ? p : NULL;
}

/* One reading of the script: brackets says whether bracket expressions
 * hide the delimiter. Anything left unparsed -- an address, regex or s/y
 * command that never closes -- is UNKNOWN: a w, W or e may be in it. */
static CmdSafetyLevel sed_script_level_read(const char *s, int brackets)
{
    CmdSafetyLevel level = CMD_READ;
    const char *p = s;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == ';') p++;
        if (!*p) break;

        /* Address: line numbers, $, ranges, steps, negation, /re/ and
         * \cREc with optional I/M modifiers. */
        for (;;) {
            if (*p == '/' || (*p == '\\' && p[1])) {
                char d = '/';
                if (*p == '\\') { d = p[1]; p++; }
                p++;
                p = sed_skip_regex(p, d, brackets);
                if (!p) return CMD_UNKNOWN;
                p++;
                while (*p == 'I' || *p == 'M') p++;
                continue;
            }
            if ((*p >= '0' && *p <= '9') || *p == '$' || *p == ',' ||
                *p == '~' || *p == '!' || *p == '+' || *p == ' ' || *p == '\t') {
                p++;
                continue;
            }
            break;
        }
        if (!*p) break;

        char c = *p++;
        if (c == 'w' || c == 'W') return CMD_WRITE;
        if (c == '{' || c == '}') continue;
        if (c == '#') {
            while (*p && *p != '\n') p++;
            continue;
        }
        if (c == 'e') {
            if (level < CMD_UNKNOWN) level = CMD_UNKNOWN;
        } else if (c == 's' || c == 'y') {
            char d = *p;
            if (!d || d == '\n' || d == '\\') return CMD_UNKNOWN;
            p++;
            /* The pattern (brackets apply only to an s regex), then the
             * replacement or y target, which has no bracket syntax. */
            p = sed_skip_regex(p, d, brackets && c == 's');
            if (!p) return CMD_UNKNOWN;
            p = sed_skip_regex(p + 1, d, 0);
            if (!p) return CMD_UNKNOWN;
            p++;
            if (c == 's') {
                while (*p && *p != ';' && *p != '\n' && *p != '}' &&
                       *p != ' ' && *p != '\t') {
                    if (*p == 'w') return CMD_WRITE;
                    if (*p == 'e' && level < CMD_UNKNOWN) level = CMD_UNKNOWN;
                    p++;
                }
            }
            continue;
        }
        while (*p && *p != ';' && *p != '\n') p++;
    }
    return level;
}

/* Bracket expressions are read both ways -- as GNU sed does (a delimiter
 * inside "[...]" is literal) and as a plain character scan -- and the
 * worse reading wins, so neither parse can hide a w/W/e from the other. */
static CmdSafetyLevel sed_script_level(const char *s)
{
    CmdSafetyLevel a = sed_script_level_read(s, 1);
    CmdSafetyLevel b = sed_script_level_read(s, 0);
    return a > b ? a : b;
}

/* ----- curl: flags that write, send data, or run/leak something, against
 * an explicit known-safe allow-list. ----- */

static int curl_value_is_devnull_or_dash(const char *v, size_t vlen)
{
    return v != NULL && (tok_eq(v, vlen, "-") || tok_eq(v, vlen, "/dev/null"));
}

static int curl_method_is_safe(const char *v, size_t vlen)
{
    return v != NULL && (tok_eq_ci(v, vlen, "GET") || tok_eq_ci(v, vlen, "HEAD"));
}

/* ----- find: the -exec/-execdir/-ok/-okdir sub-command, run through the
 * ordinary Linux classifier recursively. ----- */

static const char *find_takes_value_primaries[] = {
    "-name", "-iname", "-path", "-ipath", "-wholename", "-iwholename",
    "-regex", "-iregex", "-regextype", "-type", "-xtype", "-size", "-perm",
    "-user", "-group", "-uid", "-gid", "-links", "-inum", "-samefile",
    "-newer", "-anewer", "-cnewer", "-mtime", "-mmin", "-atime", "-amin",
    "-ctime", "-cmin", "-used", "-maxdepth", "-mindepth", "-fstype",
    "-lname", "-ilname", "-printf", "-D", NULL
};

static const char *find_novalue_primaries[] = {
    "-empty", "-nouser", "-nogroup", "-daystart", "-depth",
    "-mount", "-xdev", "-noleaf", "-follow", "-readable", "-writable",
    "-executable", "-print", "-print0", "-ls", "-prune", "-quit", "-true",
    "-false", "-not", "-and", "-or", "-a", "-o", "-H", "-L", "-P", NULL
};

static int find_primary_is_allowed(const char *tok, size_t len, int *takes_value)
{
    *takes_value = 0;
    if (tok_prefix(tok, len, "-newer")) { *takes_value = 1; return 1; }
    if (tok_prefix(tok, len, "-O")) return 1; /* -O0.. -O3 */
    if (tok_in_list(tok, len, find_takes_value_primaries)) { *takes_value = 1; return 1; }
    if (tok_in_list(tok, len, find_novalue_primaries)) return 1;
    return 0;
}

/* ----- Wrappers: env/nice/nohup/timeout/command/exec (and cheaply
 * time/stdbuf/ionice/setsid) peel their own options/arguments and hand the
 * rest to classify_linux_segment() recursively. ----- */

static int is_wrapper_cmd(const char *tok, size_t len)
{
    /* "exec" is deliberately not a wrapper here: it replaces the current
     * shell process rather than running the command alongside it, so
     * "exec CMD" is at least UNKNOWN, not whatever CMD alone would be (it
     * still counts as a wrapper for scan_pipe_target()'s narrower purpose
     * via tok_is_pipe_wrapper(), which is unaffected by this list). */
    static const char *wrappers[] = {
        "env", "nice", "nohup", "timeout", "command",
        "time", "stdbuf", "ionice", "setsid", NULL
    };
    return tok_in_list(tok, len, wrappers);
}

/* ----- Flag allow-lists of the READ commands -----
 * The commands below have at least one flag that writes a file, changes
 * system state or runs another program, so they are READ only with the
 * flags listed here. The other entries of linux_read_cmds (ls, cat, grep,
 * head, tail, wc, cut, tr, ps, df, du, ...) take any flag: no documented
 * flag of theirs writes, uploads, deletes or runs another program. */

static const LongOpt sort_longs[] = {
    { "--ignore-leading-blanks", LV_NONE }, { "--dictionary-order", LV_NONE },
    { "--ignore-case", LV_NONE }, { "--general-numeric-sort", LV_NONE },
    { "--ignore-nonprinting", LV_NONE }, { "--month-sort", LV_NONE },
    { "--human-numeric-sort", LV_NONE }, { "--numeric-sort", LV_NONE },
    { "--random-sort", LV_NONE }, { "--reverse", LV_NONE },
    { "--version-sort", LV_NONE }, { "--check", LV_OPT }, { "--merge", LV_NONE },
    { "--stable", LV_NONE }, { "--unique", LV_NONE }, { "--zero-terminated", LV_NONE },
    { "--debug", LV_NONE }, { "--help", LV_NONE }, { "--version", LV_NONE },
    { "--sort", LV_REQ }, { "--key", LV_REQ }, { "--field-separator", LV_REQ },
    { "--buffer-size", LV_REQ }, { "--temporary-directory", LV_REQ },
    { "--parallel", LV_REQ }, { "--batch-size", LV_REQ }, { "--random-source", LV_REQ },
    { "--files0-from", LV_REQ },
    { NULL, 0 }
};
static const char *const sort_long_write[] = { "--output", NULL };
static const FlagSpec sort_spec = {
    "bdfghiMnRrVcCsumz", "ktST", NULL, "o", 0, sort_longs, sort_long_write, 0
};

static const LongOpt uniq_longs[] = {
    { "--count", LV_NONE }, { "--repeated", LV_NONE }, { "--all-repeated", LV_OPT },
    { "--group", LV_OPT }, { "--ignore-case", LV_NONE }, { "--unique", LV_NONE },
    { "--zero-terminated", LV_NONE }, { "--help", LV_NONE }, { "--version", LV_NONE },
    { "--skip-fields", LV_REQ }, { "--skip-chars", LV_REQ }, { "--check-chars", LV_REQ },
    { NULL, 0 }
};
static const FlagSpec uniq_spec = {
    "cdDiuz", "fsw", NULL, NULL, 1, uniq_longs, NULL, 0
};

static const LongOpt man_longs[] = {
    { "--all", LV_NONE }, { "--whatis", LV_NONE }, { "--apropos", LV_NONE },
    { "--where", LV_NONE }, { "--path", LV_NONE }, { "--location", LV_NONE },
    { "--where-cat", LV_NONE }, { "--location-cat", LV_NONE },
    { "--ignore-case", LV_NONE }, { "--match-case", LV_NONE }, { "--regex", LV_NONE },
    { "--wildcard", LV_NONE }, { "--names-only", LV_NONE },
    { "--global-apropos", LV_NONE }, { "--help", LV_NONE }, { "--version", LV_NONE },
    { "--usage", LV_NONE }, { "--no-hyphenation", LV_NONE },
    { "--no-justification", LV_NONE }, { "--no-subpages", LV_NONE },
    { "--sections", LV_REQ }, { "--manpath", LV_REQ }, { "--locale", LV_REQ },
    { "--systems", LV_REQ }, { "--extension", LV_REQ }, { "--encoding", LV_REQ },
    { NULL, 0 }
};
static const FlagSpec man_spec = {
    "afkKwWiIhV", "sSMLmeER", NULL, NULL, 0, man_longs, NULL, 0
};

static const LongOpt less_longs[] = {
    { "--LINE-NUMBERS", LV_NONE }, { "--line-numbers", LV_NONE },
    { "--chop-long-lines", LV_NONE }, { "--RAW-CONTROL-CHARS", LV_NONE },
    { "--raw-control-chars", LV_NONE }, { "--ignore-case", LV_NONE },
    { "--IGNORE-CASE", LV_NONE }, { "--quit-if-one-screen", LV_NONE },
    { "--no-init", LV_NONE }, { "--long-prompt", LV_NONE }, { "--quiet", LV_NONE },
    { "--QUIET", LV_NONE }, { "--silent", LV_NONE }, { "--SILENT", LV_NONE },
    { "--squeeze-blank-lines", LV_NONE }, { "--follow-name", LV_NONE },
    { "--mouse", LV_NONE }, { "--status-column", LV_NONE }, { "--incsearch", LV_NONE },
    { "--use-color", LV_NONE }, { "--no-lessopen", LV_NONE }, { "--quit-at-eof", LV_NONE },
    { "--QUIT-AT-EOF", LV_NONE }, { "--hilite-search", LV_NONE },
    { "--HILITE-SEARCH", LV_NONE }, { "--hilite-unread", LV_NONE },
    { "--HILITE-UNREAD", LV_NONE }, { "--search-skip-screen", LV_NONE },
    { "--tilde", LV_NONE }, { "--no-keypad", LV_NONE }, { "--help", LV_NONE },
    { "--version", LV_NONE }, { "--wordwrap", LV_NONE },
    { "--tabs", LV_REQ }, { "--pattern", LV_REQ }, { "--prompt", LV_REQ },
    { "--jump-target", LV_REQ }, { "--header", LV_REQ }, { "--shift", LV_REQ },
    { "--window", LV_REQ }, { "--max-back-scroll", LV_REQ },
    { "--max-forw-scroll", LV_REQ },
    { NULL, 0 }
};
static const char *const less_long_write[] = { "--log-file", "--LOG-FILE", NULL };
static const FlagSpec less_spec = {
    "NnSRrIiFXMmsqQeEfgGJKLwWacCBuUV~", "xzyhbjpPtT", NULL, "oO", 0,
    less_longs, less_long_write, 0
};

static const LongOpt rg_longs[] = {
    { "--ignore-case", LV_NONE }, { "--smart-case", LV_NONE },
    { "--case-sensitive", LV_NONE }, { "--word-regexp", LV_NONE },
    { "--line-regexp", LV_NONE }, { "--invert-match", LV_NONE },
    { "--line-number", LV_NONE }, { "--no-line-number", LV_NONE },
    { "--files-with-matches", LV_NONE }, { "--files-without-match", LV_NONE },
    { "--count", LV_NONE }, { "--count-matches", LV_NONE },
    { "--fixed-strings", LV_NONE }, { "--hidden", LV_NONE }, { "--no-ignore", LV_NONE },
    { "--no-ignore-vcs", LV_NONE }, { "--files", LV_NONE }, { "--type-list", LV_NONE },
    { "--json", LV_NONE }, { "--vimgrep", LV_NONE }, { "--no-heading", LV_NONE },
    { "--heading", LV_NONE }, { "--column", LV_NONE }, { "--no-column", LV_NONE },
    { "--only-matching", LV_NONE }, { "--multiline", LV_NONE },
    { "--multiline-dotall", LV_NONE }, { "--pcre2", LV_NONE }, { "--follow", LV_NONE },
    { "--trim", LV_NONE }, { "--stats", LV_NONE }, { "--pretty", LV_NONE },
    { "--no-messages", LV_NONE }, { "--quiet", LV_NONE }, { "--text", LV_NONE },
    { "--null", LV_NONE }, { "--no-filename", LV_NONE }, { "--with-filename", LV_NONE },
    { "--search-zip", LV_NONE }, { "--debug", LV_NONE }, { "--help", LV_NONE },
    { "--version", LV_NONE }, { "--unrestricted", LV_NONE }, { "--binary", LV_NONE },
    { "--no-config", LV_NONE }, { "--sort-files", LV_NONE }, { "--byte-offset", LV_NONE },
    { "--passthru", LV_NONE }, { "--no-unicode", LV_NONE }, { "--crlf", LV_NONE },
    { "--color", LV_REQ }, { "--colors", LV_REQ }, { "--sort", LV_REQ },
    { "--sortr", LV_REQ }, { "--max-depth", LV_REQ }, { "--glob", LV_REQ },
    { "--iglob", LV_REQ }, { "--type", LV_REQ }, { "--type-not", LV_REQ },
    { "--context", LV_REQ }, { "--after-context", LV_REQ },
    { "--before-context", LV_REQ }, { "--max-count", LV_REQ },
    { "--max-columns", LV_REQ }, { "--replace", LV_REQ }, { "--regexp", LV_REQ },
    { "--file", LV_REQ }, { "--pre-glob", LV_REQ }, { "--threads", LV_REQ },
    { "--encoding", LV_REQ }, { "--ignore-file", LV_REQ }, { "--type-add", LV_REQ },
    { "--max-filesize", LV_REQ }, { "--path-separator", LV_REQ },
    { NULL, 0 }
};
static const FlagSpec rg_spec = {
    "iSswxvnNlcFLHIopzUPaq0hVu", "CABgtTefmMjrEd", NULL, NULL, 0, rg_longs, NULL, 0
};

static const LongOpt tree_longs[] = {
    { "--gitignore", LV_NONE }, { "--ignore-case", LV_NONE }, { "--matchdirs", LV_NONE },
    { "--metafirst", LV_NONE }, { "--prune", LV_NONE }, { "--info", LV_NONE },
    { "--noreport", LV_NONE }, { "--dirsfirst", LV_NONE }, { "--filesfirst", LV_NONE },
    { "--si", LV_NONE }, { "--du", LV_NONE }, { "--inodes", LV_NONE },
    { "--device", LV_NONE }, { "--nolinks", LV_NONE }, { "--help", LV_NONE },
    { "--version", LV_NONE },
    { "--charset", LV_REQ }, { "--filelimit", LV_REQ }, { "--timefmt", LV_REQ },
    { "--sort", LV_REQ },
    { NULL, 0 }
};
/* "R" removed from the allowed no-value flags (round-3 review): it was
 * never a reviewed-safe tree option and now falls through to the UNKNOWN
 * default like any other unlisted flag. */
static const FlagSpec tree_spec = {
    "adlfxqNQpugshDFvtcUriASnCXJ", "LPIHT", NULL, "o", 0, tree_longs, NULL, 0
};

static const LongOpt sar_longs[] = {
    { "--human", LV_NONE }, { "--pretty", LV_NONE }, { "--help", LV_NONE },
    { "--dev", LV_REQ }, { "--fs", LV_REQ }, { "--iface", LV_REQ },
    { "--dec", LV_REQ },
    { NULL, 0 }
};
static const FlagSpec sar_spec = {
    "ABbCdFHhpqRrStuvWwyzx", "IijmnPseEf", NULL, "o", 0, sar_longs, NULL, 0
};

static const LongOpt dmidecode_longs[] = {
    { "--quiet", LV_NONE }, { "--dump", LV_NONE }, { "--no-sysfs", LV_NONE },
    { "--no-quirks", LV_NONE }, { "--help", LV_NONE }, { "--version", LV_NONE },
    { "--dev-mem", LV_REQ }, { "--string", LV_REQ }, { "--type", LV_REQ },
    { "--handle", LV_REQ }, { "--from-dump", LV_REQ }, { "--oem-string", LV_REQ },
    { NULL, 0 }
};
static const char *const dmidecode_long_write[] = { "--dump-bin", NULL };
static const FlagSpec dmidecode_spec = {
    "quhV", "dstH", NULL, NULL, 0, dmidecode_longs, dmidecode_long_write, 0
};

static const LongOpt file_longs[] = {
    { "--mime", LV_NONE }, { "--mime-type", LV_NONE }, { "--mime-encoding", LV_NONE },
    { "--brief", LV_NONE }, { "--extension", LV_NONE }, { "--apple", LV_NONE },
    { "--keep-going", LV_NONE }, { "--no-dereference", LV_NONE },
    { "--dereference", LV_NONE }, { "--raw", LV_NONE }, { "--special-files", LV_NONE },
    { "--uncompress", LV_NONE }, { "--uncompress-noreport", LV_NONE },
    { "--no-pad", LV_NONE }, { "--no-buffer", LV_NONE }, { "--print0", LV_NONE },
    { "--preserve-date", LV_NONE }, { "--help", LV_NONE }, { "--version", LV_NONE },
    { "--files-from", LV_REQ }, { "--separator", LV_REQ }, { "--magic-file", LV_REQ },
    { "--exclude", LV_REQ }, { "--exclude-quiet", LV_REQ }, { "--parameter", LV_REQ },
    { NULL, 0 }
};
static const char *const file_long_write[] = { "--compile", NULL };
static const FlagSpec file_spec = {
    "bcdEhiIkLlNnprsSvzZ0", "eFfmP", NULL, "C", 0, file_longs, file_long_write, 0
};

static const LongOpt ss_longs[] = {
    { "--numeric", LV_NONE }, { "--resolve", LV_NONE }, { "--all", LV_NONE },
    { "--listening", LV_NONE }, { "--options", LV_NONE }, { "--extended", LV_NONE },
    { "--memory", LV_NONE }, { "--processes", LV_NONE }, { "--threads", LV_NONE },
    { "--info", LV_NONE }, { "--tos", LV_NONE }, { "--cgroup", LV_NONE },
    { "--tipcinfo", LV_NONE }, { "--summary", LV_NONE }, { "--events", LV_NONE },
    { "--context", LV_NONE }, { "--contexts", LV_NONE }, { "--bpf", LV_NONE },
    { "--tcp", LV_NONE }, { "--udp", LV_NONE }, { "--dccp", LV_NONE },
    { "--raw", LV_NONE }, { "--unix", LV_NONE }, { "--sctp", LV_NONE },
    { "--packet", LV_NONE }, { "--vsock", LV_NONE }, { "--xdp", LV_NONE },
    { "--mptcp", LV_NONE }, { "--tipc", LV_NONE }, { "--oneline", LV_NONE },
    { "--no-header", LV_NONE }, { "--ipv4", LV_NONE }, { "--ipv6", LV_NONE },
    { "--help", LV_NONE }, { "--version", LV_NONE },
    { "--net", LV_REQ }, { "--family", LV_REQ }, { "--query", LV_REQ },
    { "--socket", LV_REQ }, { "--filter", LV_REQ },
    { NULL, 0 }
};
static const char *const ss_long_write[] = { "--kill", "--diag", NULL };
static const FlagSpec ss_spec = {
    "hVHnraloempiTsEZzb460tudwxSMO", "NfAF", NULL, "KD", 0, ss_longs, ss_long_write, 0
};

static const LongOpt arp_longs[] = {
    { "--all", LV_NONE }, { "--numeric", LV_NONE }, { "--verbose", LV_NONE },
    { "--use-device", LV_NONE }, { "--help", LV_NONE }, { "--version", LV_NONE },
    { "--device", LV_REQ }, { "--hw-type", LV_REQ },
    { NULL, 0 }
};
static const char *const arp_long_write[] = { "--delete", "--set", "--file", NULL };
static const FlagSpec arp_spec = {
    "anveDV", "iHtA", NULL, "dsf", 0, arp_longs, arp_long_write, 0
};

static const LongOpt sensors_longs[] = {
    { "--no-adapter", LV_NONE }, { "--fahrenheit", LV_NONE },
    { "--bus-list", LV_NONE }, { "--help", LV_NONE }, { "--version", LV_NONE },
    { "--config-file", LV_REQ },
    { NULL, 0 }
};
static const char *const sensors_long_write[] = { "--set", NULL };
static const FlagSpec sensors_spec = {
    "fAujvhB", "c", NULL, "s", 0, sensors_longs, sensors_long_write, 0
};

static const LongOpt iptables_save_longs[] = {
    { "--counters", LV_NONE }, { "--help", LV_NONE }, { "--version", LV_NONE },
    { "--table", LV_REQ },
    { NULL, 0 }
};
static const char *const iptables_save_long_write[] = { "--file", NULL };
static const FlagSpec iptables_save_spec = {
    "chV", "t", NULL, "f", 0, iptables_save_longs, iptables_save_long_write, 0
};

static const LongOpt lastlog_longs[] = {
    { "--help", LV_NONE },
    { "--before", LV_REQ }, { "--time", LV_REQ }, { "--user", LV_REQ },
    { "--root", LV_REQ },
    { NULL, 0 }
};
static const char *const lastlog_long_write[] = { "--clear", "--set", NULL };
static const FlagSpec lastlog_spec = {
    "h", "btuR", NULL, "CS", 0, lastlog_longs, lastlog_long_write, 0
};

static const LongOpt blkid_longs[] = {
    { "--help", LV_NONE }, { "--version", LV_NONE }, { "--probe", LV_NONE },
    { "--label", LV_REQ }, { "--uuid", LV_REQ }, { "--output", LV_REQ },
    { "--offset", LV_REQ }, { "--match-tag", LV_REQ }, { "--size", LV_REQ },
    { "--match-token", LV_REQ }, { "--usages", LV_REQ }, { "--match-types", LV_REQ },
    { NULL, 0 }
};
static const char *const blkid_long_write[] = { "--cache-file", "--garbage-collect", NULL };
static const FlagSpec blkid_spec = {
    "dhiklpV", "LUnoOsStu", NULL, "cgw", 0, blkid_longs, blkid_long_write, 0
};

static const LongOpt journalctl_longs[] = {
    { "--follow", LV_NONE }, { "--reverse", LV_NONE }, { "--pager-end", LV_NONE },
    { "--catalog", LV_NONE }, { "--quiet", LV_NONE }, { "--all", LV_NONE },
    { "--full", LV_NONE }, { "--no-full", LV_NONE }, { "--no-pager", LV_NONE },
    { "--no-hostname", LV_NONE }, { "--utc", LV_NONE }, { "--merge", LV_NONE },
    { "--system", LV_NONE }, { "--user", LV_NONE }, { "--dmesg", LV_NONE },
    { "--list-boots", LV_NONE }, { "--disk-usage", LV_NONE }, { "--header", LV_NONE },
    { "--list-catalog", LV_NONE }, { "--dump-catalog", LV_NONE }, { "--verify", LV_NONE },
    { "--show-cursor", LV_NONE }, { "--no-tail", LV_NONE }, { "--fields", LV_NONE },
    { "--case-sensitive", LV_OPT }, { "--help", LV_NONE }, { "--version", LV_NONE },
    { "--lines", LV_OPT }, { "--boot", LV_OPT },
    { "--unit", LV_REQ }, { "--user-unit", LV_REQ }, { "--since", LV_REQ },
    { "--until", LV_REQ }, { "--priority", LV_REQ }, { "--identifier", LV_REQ },
    { "--exclude-identifier", LV_REQ }, { "--grep", LV_REQ }, { "--output", LV_REQ },
    { "--output-fields", LV_REQ }, { "--directory", LV_REQ }, { "--file", LV_REQ },
    { "--machine", LV_REQ }, { "--field", LV_REQ }, { "--cursor", LV_REQ },
    { "--after-cursor", LV_REQ }, { "--facility", LV_REQ }, { "--namespace", LV_REQ },
    { "--root", LV_REQ }, { "--image", LV_REQ }, { "--invocation", LV_REQ },
    { NULL, 0 }
};
static const char *const journalctl_long_write[] = {
    "--vacuum-size", "--vacuum-time", "--vacuum-files", "--rotate", "--flush",
    "--sync", "--relinquish-var", "--smart-relinquish-var", "--setup-keys",
    "--update-catalog", "--cursor-file", NULL
};
static const FlagSpec journalctl_spec = {
    /* -n, -b and -I take an optional value, attached only: a separate
     * word after them is never theirs, so it is still checked. */
    "frexqamklNh", "uSUptgoDFMTic", "nbI", NULL, 1, journalctl_longs, journalctl_long_write, 0
};

static const LongOpt dmesg_longs[] = {
    { "--ctime", LV_NONE }, { "--human", LV_NONE }, { "--follow", LV_NONE },
    { "--follow-new", LV_NONE }, { "--decode", LV_NONE }, { "--kernel", LV_NONE },
    { "--userspace", LV_NONE }, { "--raw", LV_NONE }, { "--notime", LV_NONE },
    { "--show-delta", LV_NONE }, { "--nopager", LV_NONE }, { "--json", LV_NONE },
    { "--reltime", LV_NONE }, { "--syslog", LV_NONE }, { "--force-prefix", LV_NONE },
    { "--help", LV_NONE }, { "--version", LV_NONE },
    { "--color", LV_OPT }, { "--time-format", LV_REQ }, { "--level", LV_REQ },
    { "--facility", LV_REQ }, { "--buffer-size", LV_REQ }, { "--file", LV_REQ },
    { "--kmsg-file", LV_REQ }, { "--since", LV_REQ }, { "--until", LV_REQ },
    { NULL, 0 }
};
static const char *const dmesg_long_write[] = {
    "--clear", "--read-clear", "--console-off", "--console-on", "--console-level", NULL
};
static const FlagSpec dmesg_spec = {
    "TtHwWxkurdeSJPhV", "lfsFK", "L", "CcDEn", 0, dmesg_longs, dmesg_long_write, 0
};

static const LongOpt date_longs[] = {
    { "--utc", LV_NONE }, { "--universal", LV_NONE }, { "--rfc-email", LV_NONE },
    { "--rfc-2822", LV_NONE }, { "--debug", LV_NONE }, { "--help", LV_NONE },
    { "--version", LV_NONE }, { "--iso-8601", LV_OPT }, { "--rfc-3339", LV_REQ },
    { "--resolution", LV_NONE },
    { "--date", LV_REQ }, { "--reference", LV_REQ }, { "--file", LV_REQ },
    { NULL, 0 }
};
static const char *const date_long_write[] = { "--set", NULL };
static const FlagSpec date_spec = {
    "uR", "drf", "I", "s", 0, date_longs, date_long_write, 0
};

static const LongOpt hostname_longs[] = {
    { "--short", LV_NONE }, { "--fqdn", LV_NONE }, { "--long", LV_NONE },
    { "--ip-address", LV_NONE }, { "--all-ip-addresses", LV_NONE },
    { "--all-fqdns", LV_NONE }, { "--domain", LV_NONE }, { "--alias", LV_NONE },
    { "--yp", LV_NONE }, { "--nis", LV_NONE }, { "--verbose", LV_NONE },
    { "--help", LV_NONE }, { "--version", LV_NONE },
    { NULL, 0 }
};
static const char *const hostname_long_write[] = { "--file", "--boot", NULL };
static const FlagSpec hostname_spec = {
    "sflIiAdaynvhV", NULL, NULL, "Fb", 0, hostname_longs, hostname_long_write, 0
};

static const LongOpt route_longs[] = {
    { "--numeric", LV_NONE }, { "--extend", LV_NONE }, { "--verbose", LV_NONE },
    { "--fib", LV_NONE }, { "--cache", LV_NONE }, { "--help", LV_NONE },
    { "--version", LV_NONE }, { "--family", LV_REQ },
    { NULL, 0 }
};
static const FlagSpec route_spec = {
    "neveFC46hV", "A", NULL, NULL, 0, route_longs, NULL, 0
};

/* pidstat: "-e program args" starts and monitors a program of its own --
 * left off the allow-list entirely (not in short_noval/short_val/
 * short_optval), so it falls through to flag_word()'s UNKNOWN default like
 * any other unrecognised flag. */
static const LongOpt pidstat_longs[] = {
    { "--human", LV_NONE }, { "--dec", LV_REQ },
    { NULL, 0 }
};
static const FlagSpec pidstat_spec = {
    "dHhIlRrstuVvwx", "CGpT", "U", NULL, 0, pidstat_longs, NULL, 0
};

/* dig: "-f file" batch-reads a list of lookups from a local file and sends
 * each as a query -- left off the allow-list (UNKNOWN, not WRITE: dig
 * itself never writes the file, it only reads and transmits it). */
static const FlagSpec dig_spec = {
    "46hmv", "bckpqtxy", NULL, NULL, 0, NULL, NULL, 0
};

/* git log / show / diff: revision and diff options. git accepts any
 * unambiguous abbreviation of a long option, so an abbreviation of
 * --output is WRITE and any other abbreviation is UNKNOWN. Values of the
 * value-taking long options are only taken as "--name=value": a separate
 * word after them is still checked. --ext-diff (runs an external diff
 * program) is not listed. */
static const LongOpt git_log_longs[] = {
    { "--oneline", LV_NONE }, { "--graph", LV_NONE }, { "--all", LV_NONE },
    { "--decorate", LV_OPT }, { "--no-decorate", LV_NONE }, { "--stat", LV_OPT },
    { "--shortstat", LV_NONE }, { "--numstat", LV_NONE }, { "--name-only", LV_NONE },
    { "--name-status", LV_NONE }, { "--patch", LV_NONE }, { "--no-patch", LV_NONE },
    { "--reverse", LV_NONE }, { "--first-parent", LV_NONE }, { "--no-merges", LV_NONE },
    { "--merges", LV_NONE }, { "--follow", LV_NONE }, { "--color", LV_OPT },
    { "--no-color", LV_NONE }, { "--abbrev-commit", LV_NONE },
    { "--no-abbrev-commit", LV_NONE }, { "--abbrev", LV_OPT }, { "--no-abbrev", LV_NONE },
    { "--summary", LV_NONE }, { "--raw", LV_NONE }, { "--full-diff", LV_NONE },
    { "--word-diff", LV_OPT }, { "--color-words", LV_OPT },
    { "--ignore-all-space", LV_NONE }, { "--ignore-space-change", LV_NONE },
    { "--ignore-blank-lines", LV_NONE }, { "--ignore-cr-at-eol", LV_NONE },
    { "--ignore-space-at-eol", LV_NONE }, { "--check", LV_NONE },
    { "--minimal", LV_NONE }, { "--patience", LV_NONE }, { "--histogram", LV_NONE },
    { "--dirstat", LV_OPT }, { "--date-order", LV_NONE }, { "--topo-order", LV_NONE },
    { "--author-date-order", LV_NONE }, { "--branches", LV_OPT }, { "--tags", LV_OPT },
    { "--remotes", LV_OPT }, { "--left-right", LV_NONE }, { "--cherry-pick", LV_NONE },
    { "--cherry-mark", LV_NONE }, { "--cherry", LV_NONE }, { "--boundary", LV_NONE },
    { "--simplify-by-decoration", LV_NONE }, { "--source", LV_NONE },
    { "--show-signature", LV_NONE }, { "--no-ext-diff", LV_NONE },
    { "--no-textconv", LV_NONE }, { "--textconv", LV_NONE }, { "--cached", LV_NONE },
    { "--staged", LV_NONE }, { "--full-index", LV_NONE }, { "--binary", LV_NONE },
    { "--no-renames", LV_NONE }, { "--find-renames", LV_OPT }, { "--find-copies", LV_OPT },
    { "--relative", LV_OPT }, { "--no-relative", LV_NONE }, { "--exit-code", LV_NONE },
    { "--quiet", LV_NONE }, { "--compact-summary", LV_NONE }, { "--pretty", LV_OPT },
    { "--format", LV_OPT }, { "--no-walk", LV_OPT }, { "--do-walk", LV_NONE },
    { "--walk-reflogs", LV_NONE }, { "--reflog", LV_NONE }, { "--not", LV_NONE },
    { "--regexp-ignore-case", LV_NONE }, { "--extended-regexp", LV_NONE },
    { "--fixed-strings", LV_NONE }, { "--perl-regexp", LV_NONE },
    { "--all-match", LV_NONE }, { "--invert-grep", LV_NONE }, { "--no-notes", LV_NONE },
    { "--notes", LV_OPT }, { "--show-notes", LV_OPT }, { "--children", LV_NONE },
    { "--parents", LV_NONE }, { "--use-mailmap", LV_NONE }, { "--mailmap", LV_NONE },
    { "--no-mailmap", LV_NONE }, { "--log-size", LV_NONE }, { "--full-history", LV_NONE },
    { "--dense", LV_NONE }, { "--sparse", LV_NONE }, { "--simplify-merges", LV_NONE },
    { "--ancestry-path", LV_OPT }, { "--show-pulls", LV_NONE }, { "--merge", LV_NONE },
    { "--no-prefix", LV_NONE }, { "--default-prefix", LV_NONE },
    { "--patch-with-stat", LV_NONE }, { "--patch-with-raw", LV_NONE },
    { "--function-context", LV_NONE }, { "--remerge-diff", LV_NONE }, { "--cc", LV_NONE },
    { "--combined-all-paths", LV_NONE }, { "--no-diff-merges", LV_NONE },
    { "--dd", LV_NONE }, { "--no-index", LV_NONE }, { "--merge-base", LV_NONE },
    { "--submodule", LV_OPT }, { "--expand-tabs", LV_OPT }, { "--no-expand-tabs", LV_NONE },
    { "--show-linear-break", LV_OPT }, { "--irreversible-delete", LV_NONE },
    { "--text", LV_NONE }, { "--no-stat", LV_NONE }, { "--indent-heuristic", LV_NONE },
    { "--no-indent-heuristic", LV_NONE }, { "--help", LV_NONE },
    { "--since", LV_REQ }, { "--until", LV_REQ }, { "--after", LV_REQ },
    { "--before", LV_REQ }, { "--author", LV_REQ }, { "--committer", LV_REQ },
    { "--grep", LV_REQ }, { "--max-count", LV_REQ }, { "--skip", LV_REQ },
    { "--date", LV_REQ }, { "--diff-filter", LV_REQ }, { "--unified", LV_REQ },
    { "--min-parents", LV_REQ }, { "--max-parents", LV_REQ }, { "--glob", LV_REQ },
    { "--exclude", LV_REQ }, { "--encoding", LV_REQ }, { "--diff-algorithm", LV_REQ },
    { "--anchored", LV_REQ }, { "--word-diff-regex", LV_REQ }, { "--color-moved", LV_OPT },
    { "--color-moved-ws", LV_REQ }, { "--ws-error-highlight", LV_REQ },
    { "--src-prefix", LV_REQ }, { "--dst-prefix", LV_REQ }, { "--line-prefix", LV_REQ },
    { "--inter-hunk-context", LV_REQ }, { "--stat-width", LV_REQ },
    { "--stat-name-width", LV_REQ }, { "--stat-count", LV_REQ },
    { "--stat-graph-width", LV_REQ }, { "--ignore-submodules", LV_OPT },
    { "--diff-merges", LV_REQ }, { "--decorate-refs", LV_REQ },
    { "--decorate-refs-exclude", LV_REQ }, { "--since-as-filter", LV_REQ },
    { NULL, 0 }
};
static const char *const git_output_write[] = { "--output", NULL };
static const FlagSpec git_log_spec = {
    "puswbzRiEFWamctrgqPDh", "nSGLI", "BMCUXlO", NULL, 1,
    git_log_longs, git_output_write, 1
};

static const LongOpt git_status_longs[] = {
    { "--short", LV_NONE }, { "--branch", LV_NONE }, { "--long", LV_NONE },
    { "--verbose", LV_NONE }, { "--show-stash", LV_NONE }, { "--ahead-behind", LV_NONE },
    { "--no-ahead-behind", LV_NONE }, { "--renames", LV_NONE }, { "--no-renames", LV_NONE },
    { "--no-column", LV_NONE }, { "--null", LV_NONE }, { "--porcelain", LV_OPT },
    { "--untracked-files", LV_OPT }, { "--ignored", LV_OPT },
    { "--ignore-submodules", LV_OPT }, { "--column", LV_OPT }, { "--find-renames", LV_OPT },
    { "--help", LV_NONE },
    { NULL, 0 }
};
static const FlagSpec git_status_spec = {
    "sbvzh", NULL, "uM", NULL, 0, git_status_longs, NULL, 0
};

/* git branch: the listing options. --contains, --merged and friends
 * switch it to list mode, where operands are patterns; anywhere else an
 * operand names a branch to create. */
static const LongOpt git_branch_longs[] = {
    { "--all", LV_NONE }, { "--remotes", LV_NONE }, { "--verbose", LV_NONE },
    { "--quiet", LV_NONE }, { "--list", LV_NONE }, { "--show-current", LV_NONE },
    { "--ignore-case", LV_NONE }, { "--no-color", LV_NONE }, { "--no-column", LV_NONE },
    { "--no-abbrev", LV_NONE }, { "--omit-empty", LV_NONE }, { "--color", LV_OPT },
    { "--column", LV_OPT }, { "--abbrev", LV_OPT }, { "--sort", LV_REQ },
    { "--format", LV_REQ }, { "--contains", LV_REQ }, { "--no-contains", LV_REQ },
    { "--merged", LV_REQ }, { "--no-merged", LV_REQ }, { "--points-at", LV_REQ },
    { "--help", LV_NONE },
    { NULL, 0 }
};
static const char *const git_branch_long_write[] = {
    "--delete", "--move", "--copy", "--force", "--set-upstream-to",
    "--unset-upstream", "--edit-description", "--track", "--no-track",
    "--create-reflog", "--recurse-submodules", "--set-upstream", NULL
};
static const FlagSpec git_branch_spec = {
    "arvqilh", NULL, NULL, "dDmMcCfut", 0, git_branch_longs, git_branch_long_write, 0
};

static const LongOpt git_rev_parse_longs[] = {
    { "--show-toplevel", LV_NONE }, { "--show-prefix", LV_NONE },
    { "--show-cdup", LV_NONE }, { "--git-dir", LV_NONE }, { "--git-common-dir", LV_NONE },
    { "--absolute-git-dir", LV_NONE }, { "--is-inside-work-tree", LV_NONE },
    { "--is-inside-git-dir", LV_NONE }, { "--is-bare-repository", LV_NONE },
    { "--is-shallow-repository", LV_NONE }, { "--show-superproject-working-tree", LV_NONE },
    { "--show-object-format", LV_OPT }, { "--show-ref-format", LV_NONE },
    { "--verify", LV_NONE }, { "--quiet", LV_NONE }, { "--symbolic", LV_NONE },
    { "--symbolic-full-name", LV_NONE }, { "--abbrev-ref", LV_OPT }, { "--short", LV_OPT },
    { "--all", LV_NONE }, { "--branches", LV_OPT }, { "--tags", LV_OPT },
    { "--remotes", LV_OPT }, { "--revs-only", LV_NONE }, { "--no-revs", LV_NONE },
    { "--flags", LV_NONE }, { "--no-flags", LV_NONE }, { "--sq", LV_NONE },
    { "--not", LV_NONE }, { "--local-env-vars", LV_NONE }, { "--help", LV_NONE },
    { "--git-path", LV_REQ }, { "--since", LV_REQ }, { "--until", LV_REQ },
    { "--after", LV_REQ }, { "--before", LV_REQ }, { "--default", LV_REQ },
    { "--prefix", LV_REQ },
    { NULL, 0 }
};
static const FlagSpec git_rev_parse_spec = {
    "qh", NULL, NULL, NULL, 0, git_rev_parse_longs, NULL, 1
};

static const LongOpt git_ls_files_longs[] = {
    { "--cached", LV_NONE }, { "--deleted", LV_NONE }, { "--modified", LV_NONE },
    { "--others", LV_NONE }, { "--ignored", LV_NONE }, { "--stage", LV_NONE },
    { "--unmerged", LV_NONE }, { "--killed", LV_NONE }, { "--directory", LV_NONE },
    { "--no-empty-directory", LV_NONE }, { "--exclude-standard", LV_NONE },
    { "--error-unmatch", LV_NONE }, { "--full-name", LV_NONE },
    { "--recurse-submodules", LV_NONE }, { "--eol", LV_NONE }, { "--deduplicate", LV_NONE },
    { "--debug", LV_NONE }, { "--sparse", LV_NONE }, { "--resolve-undo", LV_NONE },
    { "--abbrev", LV_OPT }, { "--help", LV_NONE },
    { "--exclude", LV_REQ }, { "--exclude-from", LV_REQ },
    { "--exclude-per-directory", LV_REQ }, { "--with-tree", LV_REQ }, { "--format", LV_REQ },
    { NULL, 0 }
};
static const FlagSpec git_ls_files_spec = {
    "cdmoisukvtfzh", "xX", NULL, NULL, 0, git_ls_files_longs, NULL, 0
};

static const LongOpt git_blame_longs[] = {
    { "--porcelain", LV_NONE }, { "--line-porcelain", LV_NONE },
    { "--incremental", LV_NONE }, { "--root", LV_NONE }, { "--show-stats", LV_NONE },
    { "--show-name", LV_NONE }, { "--show-number", LV_NONE }, { "--show-email", LV_NONE },
    { "--reverse", LV_NONE }, { "--first-parent", LV_NONE }, { "--progress", LV_NONE },
    { "--no-progress", LV_NONE }, { "--color-lines", LV_NONE },
    { "--color-by-age", LV_NONE }, { "--minimal", LV_NONE }, { "--help", LV_NONE },
    { "--date", LV_REQ }, { "--abbrev", LV_OPT }, { "--ignore-rev", LV_REQ },
    { "--ignore-revs-file", LV_REQ }, { "--contents", LV_REQ },
    { NULL, 0 }
};
static const FlagSpec git_blame_spec = {
    "bcflnpstweh", "LS", "MC", NULL, 0, git_blame_longs, NULL, 0
};

typedef struct {
    const char *cmd;
    const FlagSpec *spec;
    int operands;   /* 0: any operand is fine; 1: any operand is WRITE */
} ReadCmdSpec;

/* Commands whose operands are data (files, patterns, sections) and whose
 * flags are checked against the spec. date, hostname and route have their
 * own operand rules below. */
static const ReadCmdSpec read_cmd_specs[] = {
    { "man",           &man_spec,           0 },
    { "rg",            &rg_spec,            0 },
    { "tree",          &tree_spec,          0 },
    { "sar",           &sar_spec,           0 },
    { "dmidecode",     &dmidecode_spec,     0 },
    { "file",          &file_spec,          0 },
    { "ss",            &ss_spec,            0 },
    { "arp",           &arp_spec,           0 },
    { "sensors",       &sensors_spec,       0 },
    { "iptables-save", &iptables_save_spec, 0 },
    { "lastlog",       &lastlog_spec,       0 },
    { "blkid",         &blkid_spec,         0 },
    { "journalctl",    &journalctl_spec,    0 },
    { "dmesg",         &dmesg_spec,         0 },
    { "hostname",      &hostname_spec,      1 },
    { "pidstat",       &pidstat_spec,       0 },
    { "dig",           &dig_spec,           0 },
    { NULL, NULL, 0 }
};

/* Variables an "env NAME=value cmd" may set while the wrapped command keeps
 * its own level. Anything else (LD_PRELOAD, PAGER, LESSOPEN, GIT_*, HOME,
 * PATH, ...) can make a read command load or run another program. */
static int env_var_is_benign(const char *name, size_t len)
{
    static const char *vars[] = {
        "LANG", "LANGUAGE", "TZ", "TERM", "COLUMNS", "LINES", "NO_COLOR",
        "CLICOLOR", "CLICOLOR_FORCE", "POSIXLY_CORRECT", "GREP_COLOR",
        "GREP_COLORS", "LS_COLORS", "TIME_STYLE", "QUOTING_STYLE", NULL
    };
    if (tok_in_list(name, len, vars)) return 1;
    if (len > 3 && memcmp(name, "LC_", 3) == 0) return 1;
    return 0;
}

/* The raw word ts[0..tl) with its quotes and escapes removed (tok_mode). */
static void tok_unquote(const char *ts, size_t tl, char *out, size_t out_size)
{
    const char *pp = ts;
    const char *rs, *re;
    int trunc = 0;
    out[0] = '\0';
    (void)next_shell_word(&pp, ts + tl, out, out_size, &trunc, &rs, &re);
}

/* "-x" (value in the next word: *takes = 1) or "-xVALUE" (attached). */
static int short_opt(const char *ts, size_t tl, char c, int *takes)
{
    if (tl < 2 || ts[0] != '-' || ts[1] != c) return 0;
    *takes = (tl == 2);
    return 1;
}

/* "--name" (value in the next word when want_value: *takes = 1) or
 * "--name=VALUE". */
static int long_opt(const char *ts, size_t tl, const char *name, int want_value, int *takes)
{
    size_t n = strlen(name);
    if (tl == n && memcmp(ts, name, n) == 0) { *takes = want_value; return 1; }
    if (want_value && tl > n && memcmp(ts, name, n) == 0 && ts[n] == '=') {
        *takes = 0;
        return 1;
    }
    return 0;
}

/* One option word of a wrapper command (env, nice, nohup, timeout,
 * command, exec, time, stdbuf, ionice, setsid). Returns 1 when it is one of
 * the wrapper's own known options; *takes is set when its value is the next
 * word and *lvl raised for an option that writes. */
static int wrapper_flag(const char *w, size_t wl, const char *ts, size_t tl,
                        int *takes, CmdSafetyLevel *lvl)
{
    *takes = 0;
    if (tok_eq(w, wl, "env")) {
        if (tok_eq(ts, tl, "-") || tok_eq(ts, tl, "-i") ||
            tok_eq(ts, tl, "--ignore-environment") || tok_eq(ts, tl, "-0") ||
            tok_eq(ts, tl, "--null") || tok_eq(ts, tl, "-v") || tok_eq(ts, tl, "--debug"))
            return 1;
        return short_opt(ts, tl, 'u', takes) || long_opt(ts, tl, "--unset", 1, takes) ||
               short_opt(ts, tl, 'C', takes) || long_opt(ts, tl, "--chdir", 1, takes) ||
               short_opt(ts, tl, 'S', takes) || long_opt(ts, tl, "--split-string", 1, takes);
    }
    if (tok_eq(w, wl, "nice")) {
        if (tl >= 2 && ts[0] == '-') {
            size_t k = 1;
            while (k < tl && isdigit((unsigned char)ts[k])) k++;
            if (k == tl) return 1;                       /* -10 */
        }
        return short_opt(ts, tl, 'n', takes) || long_opt(ts, tl, "--adjustment", 1, takes);
    }
    if (tok_eq(w, wl, "timeout")) {
        if (tok_eq(ts, tl, "--preserve-status") || tok_eq(ts, tl, "--foreground") ||
            tok_eq(ts, tl, "-v") || tok_eq(ts, tl, "--verbose") ||
            tok_eq(ts, tl, "-p") || tok_eq(ts, tl, "-f"))
            return 1;
        return short_opt(ts, tl, 's', takes) || long_opt(ts, tl, "--signal", 1, takes) ||
               short_opt(ts, tl, 'k', takes) || long_opt(ts, tl, "--kill-after", 1, takes);
    }
    if (tok_eq(w, wl, "command"))
        return tok_eq(ts, tl, "-p");
    if (tok_eq(w, wl, "exec")) {
        if (tok_eq(ts, tl, "-c") || tok_eq(ts, tl, "-l") || tok_eq(ts, tl, "-cl") ||
            tok_eq(ts, tl, "-lc"))
            return 1;
        if (tok_eq(ts, tl, "-a")) { *takes = 1; return 1; }
        return 0;
    }
    if (tok_eq(w, wl, "time")) {
        static const char *const time_write[] = { "--output", "--append", NULL };
        size_t nlen = 0;
        if (tok_eq(ts, tl, "-p") || tok_eq(ts, tl, "--portability") ||
            tok_eq(ts, tl, "-v") || tok_eq(ts, tl, "--verbose") ||
            tok_eq(ts, tl, "-q") || tok_eq(ts, tl, "--quiet"))
            return 1;
        if (short_opt(ts, tl, 'f', takes) || long_opt(ts, tl, "--format", 1, takes))
            return 1;
        if (short_opt(ts, tl, 'o', takes) || long_opt(ts, tl, "--output", 1, takes) ||
            tok_eq(ts, tl, "-a") || tok_eq(ts, tl, "--append")) {
            *lvl = CMD_WRITE;
            return 1;
        }
        while (nlen < tl && ts[nlen] != '=') nlen++;
        if (tl > 2 && ts[1] == '-' && long_in_write_list(ts, nlen, time_write)) {
            *lvl = CMD_WRITE;          /* an abbreviation of --output / --append */
            *takes = (nlen == tl);
            return 1;
        }
        return 0;
    }
    if (tok_eq(w, wl, "stdbuf"))
        return short_opt(ts, tl, 'i', takes) || short_opt(ts, tl, 'o', takes) ||
               short_opt(ts, tl, 'e', takes) || long_opt(ts, tl, "--input", 1, takes) ||
               long_opt(ts, tl, "--output", 1, takes) || long_opt(ts, tl, "--error", 1, takes);
    if (tok_eq(w, wl, "ionice")) {
        if (tok_eq(ts, tl, "-t") || tok_eq(ts, tl, "--ignore")) return 1;
        if (short_opt(ts, tl, 'c', takes) || long_opt(ts, tl, "--class", 1, takes) ||
            short_opt(ts, tl, 'n', takes) || long_opt(ts, tl, "--classdata", 1, takes))
            return 1;
        /* Changes the I/O priority of processes already running. */
        if (short_opt(ts, tl, 'p', takes) || long_opt(ts, tl, "--pid", 1, takes) ||
            short_opt(ts, tl, 'P', takes) || long_opt(ts, tl, "--pgid", 1, takes) ||
            short_opt(ts, tl, 'u', takes) || long_opt(ts, tl, "--uid", 1, takes)) {
            *lvl = CMD_WRITE;
            return 1;
        }
        return 0;
    }
    if (tok_eq(w, wl, "setsid"))
        return tok_eq(ts, tl, "-c") || tok_eq(ts, tl, "--ctty") || tok_eq(ts, tl, "-f") ||
               tok_eq(ts, tl, "--fork") || tok_eq(ts, tl, "-w") || tok_eq(ts, tl, "--wait");
    return 0;   /* nohup: no options of its own */
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

    /* Wrappers: env, nice, nohup, timeout, command, exec, time (and
     * /usr/bin/time), stdbuf, ionice and setsid run another command. Their
     * own options are checked against an allow-list -- anything else is at
     * least UNKNOWN, time -o/-a and ionice -p/-P/-u are WRITE -- and the
     * wrapped command is classified recursively, so the result is never
     * lower than the wrapped command alone. Run before any allow-list rule
     * so "env rm F" cannot ride "env"'s bare-form READ. nohup is at least
     * WRITE: it writes nohup.out. */
    if (is_wrapper_cmd(base1, base1_len)) {
        int is_env     = tok_eq(base1, base1_len, "env");
        int is_timeout = tok_eq(base1, base1_len, "timeout");
        int is_command = tok_eq(base1, base1_len, "command");
        int is_nohup   = tok_eq(base1, base1_len, "nohup");
        int need_duration = is_timeout;
        CmdSafetyLevel wlvl = redir;
        const char *scan = p;
        const char *ts;
        size_t tl;

        /* "command -v NAME" / "command -V NAME" report how a name would be
         * interpreted -- they never run it. */
        if (is_command) {
            const char *look = scan;
            if (next_token(&look, &ts, &tl) &&
                (tok_eq(ts, tl, "-v") || tok_eq(ts, tl, "-V"))) {
                return has_sudo && redir < CMD_WRITE ? CMD_WRITE : redir;
            }
        }

        for (;;) {
            const char *look = scan;
            if (!next_token(&look, &ts, &tl)) break;
            if (tok_is_obscured_flag(ts, tl)) {
                if (wlvl < CMD_UNKNOWN) wlvl = CMD_UNKNOWN;
                scan = look;
                continue;
            }
            if (tl >= 1 && ts[0] == '-' && !(need_duration && tl > 1 &&
                                              isdigit((unsigned char)ts[1]))) {
                int takes = 0;
                CmdSafetyLevel fl = CMD_READ;
                scan = look;
                if (tok_eq(ts, tl, "--")) break;
                if (!wrapper_flag(base1, base1_len, ts, tl, &takes, &fl)) {
                    if (fl < CMD_UNKNOWN) fl = CMD_UNKNOWN;
                }
                if (fl > wlvl) wlvl = fl;
                if (is_env && (tok_eq(ts, tl, "-S") || tok_prefix(ts, tl, "-S") ||
                               tok_eq(ts, tl, "--split-string") ||
                               tok_prefix(ts, tl, "--split-string="))) {
                    /* The value is itself a command line. */
                    char buf[1024];
                    const char *v = NULL; size_t vl = 0;
                    if (takes) {
                        if (next_token(&scan, &v, &vl)) tok_unquote(v, vl, buf, sizeof buf);
                        else buf[0] = '\0';
                        takes = 0;
                    } else {
                        const char *eq = memchr(ts, '=', tl);
                        const char *vs = eq ? eq + 1 : ts + 2;
                        tok_unquote(vs, (size_t)((ts + tl) - vs), buf, sizeof buf);
                    }
                    if (buf[0]) {
                        CmdSafetyLevel sl = classify_linux_segment(buf, strlen(buf), NULL, 0);
                        if (sl > wlvl) wlvl = sl;
                    }
                    if (wlvl < CMD_UNKNOWN) wlvl = CMD_UNKNOWN;
                }
                if (takes) {
                    const char *v; size_t vl;
                    (void)next_token(&scan, &v, &vl);
                }
                continue;
            }
            if (is_env) {
                size_t k = 0;
                while (k < tl && (isalnum((unsigned char)ts[k]) || ts[k] == '_')) k++;
                if (k > 0 && k < tl && ts[k] == '=') {
                    if (!env_var_is_benign(ts, k) && wlvl < CMD_UNKNOWN)
                        wlvl = CMD_UNKNOWN;
                    scan = look;
                    continue;
                }
            }
            if (need_duration) { /* timeout DURATION */
                need_duration = 0;
                scan = look;
                continue;
            }
            break;
        }

        const char *cmd_start = NULL;
        const char *cmd_end = NULL;
        while (next_token(&scan, &ts, &tl)) {
            if (!cmd_start) cmd_start = ts;
            cmd_end = ts + tl;
        }

        if (is_nohup && wlvl < CMD_WRITE) wlvl = CMD_WRITE;
        if (cmd_start) {
            CmdSafetyLevel sub = classify_linux_segment(cmd_start,
                                                          (size_t)(cmd_end - cmd_start),
                                                          NULL, 0);
            CmdSafetyLevel lvl = sub > wlvl ? sub : wlvl;
            if (has_sudo && lvl < CMD_WRITE) lvl = CMD_WRITE;
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size, "%.*s wraps: %.*s",
                         (int)base1_len, base1, (int)(cmd_end - cmd_start), cmd_start);
            return lvl;
        }
        if (wlvl > redir && reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "%.*s: option outside the known-safe set",
                     (int)base1_len, base1);
        if (wlvl > redir) return has_sudo && wlvl < CMD_WRITE ? CMD_WRITE : wlvl;
        /* Nothing left after the wrapper's own options/arguments: fall
         * through unclassified, keeping today's level for the bare
         * wrapper (env/printenv via linux_read_cmds below, everything
         * else via the final CMD_UNKNOWN fallback). */
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

    /* PowerShell's iex / Invoke-Expression executes a string as code --
     * CRITICAL as the command itself, not only as a pipe target. */
    if (tok_is_iex_or_invoke_expression(base1, base1_len)) {
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size,
                     "iex/Invoke-Expression: executes a string as code");
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
        if (reason_buf && reason_buf_size > 0 && db_level > CMD_READ)
            snprintf(reason_buf, reason_buf_size, "destructive SQL via %.*s",
                     (int)base1_len, base1);
        return redir;
    }

    /* Package-manager dry runs report and change nothing (spec 3.3). Checked
     * before the subcommand rules so that "apt-get purge --dry-run" is SAFE
     * rather than CRITICAL. "-s" is apt's short simulate flag only. */
    if (tok_in_list(base1, base1_len, pkg_mgr_cmds)) {
        const char *scan = p;
        const char *ts;
        size_t tl;
        int apt_family = tok_eq(base1, base1_len, "apt")
                      || tok_eq(base1, base1_len, "apt-get")
                      || tok_eq(base1, base1_len, "aptitude");
        while (next_token(&scan, &ts, &tl)) {
            if (tok_in_list(ts, tl, pkg_mgr_dry_run_flags)
                || (apt_family && tok_eq(ts, tl, "-s"))) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size,
                             "%.*s dry run: reports without changing anything",
                             (int)base1_len, base1);
                return redir;
            }
        }
    }

    /* terraform plan: changes no infrastructure, but it runs provider and
     * data-source code, so it is UNKNOWN rather than READ. Checked ahead
     * of linux_subcmd_rules, whose "terraform plan" row says READ. */
    if (tok_eq(base1, base1_len, "terraform")) {
        const char *p2 = p;
        const char *sub_s; size_t sub_l;
        if (next_token(&p2, &sub_s, &sub_l) && tok_eq(sub_s, sub_l, "plan")) {
            CmdSafetyLevel level = CMD_UNKNOWN;
            if (has_sudo && level < CMD_WRITE) level = CMD_WRITE;
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size, "terraform plan: can run arbitrary provider code");
            return level > redir ? level : redir;
        }
    }

    /* curl: every flag against an explicit allow-list. Evaluated before
     * linux_subcmd_rules below, whose "curl"+"-o"/"-O" rows do not know the
     * "-o /dev/null" / "-o -" exception. That exception holds only while
     * nothing else on the line writes output (a second -o, -O, -D to a
     * file, -w with %output{} or an @file format), uploads (-T, data or a
     * form), reads a
     * config (-K) or starts a second transfer (--next). -H/-b values that
     * name a local file ("@F", or a -b value without "=") are WRITE. */
    if (tok_eq(base1, base1_len, "curl")) {
        static const char *const safe_noval[] = {
            "--fail", "--fail-with-body", "--compressed", "--get", "--http1.0",
            "--http1.1", "--http2", "--http3", "--location", "--location-trusted",
            "--progress-bar", "--no-progress-meter", "--help", "--version",
            "--silent", "--show-error", "--insecure", "--head", "--include",
            "--verbose", "--ipv4", "--ipv6", "--retry-connrefused",
            "--retry-all-errors", "--globoff", "--no-buffer", "--disable",
            "--no-keepalive", "--tcp-nodelay", "--tlsv1.2", "--tlsv1.3",
            "--fail-early", "--path-as-is", "--show-headers", NULL
        };
        static const char *const safe_val[] = {
            "--user-agent", "--referer", "--max-time", "--connect-timeout",
            "--proxy", "--user", "--noproxy", "--resolve", "--cacert", "--capath",
            "--cert", "--key", "--max-redirs", "--range", "--continue-at", "--url",
            "--retry", "--retry-delay", "--retry-max-time", "--limit-rate",
            "--interface", "--connect-to", "--proxy-user", NULL
        };
        static const char *const write_prefix[] = {
            "--data", "--form", "--trace", NULL
        };
        static const char *const write_exact[] = {
            "--output-dir", "--create-dirs", "--upload-file", "--json",
            "--cookie-jar", "--libcurl", "--stderr", "--etag-save", "--hsts",
            "--alt-svc", "--remote-name", "--remote-name-all",
            "--remote-header-name", NULL
        };
        const char *scan = p;
        const char *ts;
        size_t tl;
        CmdSafetyLevel level = CMD_READ;
        int outputs = 0, null_outputs = 0, other_io = 0;
        char val[512];
        while (next_token(&scan, &ts, &tl)) {
            if (tok_is_obscured_flag(ts, tl)) {
                if (level < CMD_UNKNOWN) level = CMD_UNKNOWN;
                continue;
            }
            if (tok_has_active_expansion(ts, tl)) {
                if (level < CMD_UNKNOWN) level = CMD_UNKNOWN;
            }
            if (tl < 2 || ts[0] != '-') continue;      /* URL or other operand */
            if (ts[1] == '-') {
                size_t nlen = 0;
                while (nlen < tl && ts[nlen] != '=') nlen++;
                int has_eq = nlen < tl;
                char name[64];
                if (nlen >= sizeof name) { if (level < CMD_UNKNOWN) level = CMD_UNKNOWN; continue; }
                memcpy(name, ts, nlen);
                name[nlen] = '\0';
                int wants = tok_in_list(name, nlen, safe_val) ||
                            tok_eq(name, nlen, "--output") || tok_eq(name, nlen, "--dump-header") ||
                            tok_eq(name, nlen, "--request") || tok_eq(name, nlen, "--config") ||
                            tok_eq(name, nlen, "--header") || tok_eq(name, nlen, "--cookie") ||
                            tok_eq(name, nlen, "--write-out");
                val[0] = '\0';
                if (wants) {
                    const char *v; size_t vl;
                    if (has_eq) tok_unquote(ts + nlen + 1, tl - nlen - 1, val, sizeof val);
                    else if (next_token(&scan, &v, &vl)) tok_unquote(v, vl, val, sizeof val);
                }
                size_t vlen = strlen(val);
                if (tok_in_list(name, nlen, safe_noval) && !has_eq) continue;
                if (tok_in_list(name, nlen, safe_val)) continue;
                if (tok_in_list(name, nlen, write_exact) || tok_has_prefix(name, nlen, write_prefix)) {
                    level = CMD_WRITE;
                    other_io = 1;
                } else if (tok_eq(name, nlen, "--output")) {
                    outputs++;
                    if (curl_value_is_devnull_or_dash(val, vlen)) null_outputs++;
                    else level = CMD_WRITE;
                } else if (tok_eq(name, nlen, "--dump-header")) {
                    if (!curl_value_is_devnull_or_dash(val, vlen)) { level = CMD_WRITE; other_io = 1; }
                } else if (tok_eq(name, nlen, "--request")) {
                    if (!curl_method_is_safe(val, vlen)) level = CMD_WRITE;
                } else if (tok_eq(name, nlen, "--header")) {
                    if (val[0] == '@') level = CMD_WRITE;
                } else if (tok_eq(name, nlen, "--cookie")) {
                    if (val[0] == '@' || !strchr(val, '=')) level = CMD_WRITE;
                } else if (tok_eq(name, nlen, "--write-out")) {
                    if (strstr(val, "%output{")) { level = CMD_WRITE; other_io = 1; }
                    else if (val[0] == '@') {
                        other_io = 1;
                        if (level < CMD_UNKNOWN) level = CMD_UNKNOWN;
                    }
                } else {
                    /* --config, --next and everything not listed */
                    if (tok_eq(name, nlen, "--config") || tok_eq(name, nlen, "--next"))
                        other_io = 1;
                    if (level < CMD_UNKNOWN) level = CMD_UNKNOWN;
                }
                continue;
            }
            for (size_t i = 1; i < tl; i++) {
                char c = ts[i];
                if (strchr("sSLIivkf46G#hVqgN", c)) continue;
                if (c == 'O' || c == 'J') {
                    level = CMD_WRITE;
                    other_io = 1;
                    continue;
                }
                if (!strchr("oTdFcDXHbwAemxuErCUK", c)) {
                    if (c == ':') other_io = 1;                 /* -: is --next */
                    if (level < CMD_UNKNOWN) level = CMD_UNKNOWN;
                    break;
                }
                /* A value-taking letter: the rest of the word, or the next
                 * word. */
                val[0] = '\0';
                if (i + 1 < tl) {
                    tok_unquote(ts + i + 1, tl - i - 1, val, sizeof val);
                } else {
                    const char *v; size_t vl;
                    if (next_token(&scan, &v, &vl)) tok_unquote(v, vl, val, sizeof val);
                }
                size_t vlen = strlen(val);
                switch (c) {
                case 'o':
                    outputs++;
                    if (curl_value_is_devnull_or_dash(val, vlen)) null_outputs++;
                    else level = CMD_WRITE;
                    break;
                case 'T': case 'd': case 'F': case 'c':
                    level = CMD_WRITE;
                    other_io = 1;
                    break;
                case 'D':
                    if (!curl_value_is_devnull_or_dash(val, vlen)) { level = CMD_WRITE; other_io = 1; }
                    break;
                case 'X':
                    if (!curl_method_is_safe(val, vlen)) level = CMD_WRITE;
                    break;
                case 'H':
                    if (val[0] == '@') level = CMD_WRITE;
                    break;
                case 'b':
                    if (val[0] == '@' || !strchr(val, '=')) level = CMD_WRITE;
                    break;
                case 'w':
                    if (strstr(val, "%output{")) { level = CMD_WRITE; other_io = 1; }
                    else if (val[0] == '@') {
                        other_io = 1;
                        if (level < CMD_UNKNOWN) level = CMD_UNKNOWN;
                    }
                    break;
                case 'K':
                    other_io = 1;
                    if (level < CMD_UNKNOWN) level = CMD_UNKNOWN;
                    break;
                default:        /* A e m x u E r C U: values that stay local */
                    break;
                }
                break;
            }
        }
        if (null_outputs && (outputs > 1 || other_io) && level < CMD_WRITE)
            level = CMD_WRITE;
        if (has_sudo && level < CMD_WRITE) level = CMD_WRITE;
        if (reason_buf && reason_buf_size > 0 && level > CMD_READ)
            snprintf(reason_buf, reason_buf_size, "curl: flag outside the known-safe set");
        return level > redir ? level : redir;
    }

    /* git: global options against an allow-list (-c, --git-dir,
     * --work-tree, --exec-path and anything unlisted can point git at
     * another program, config or tree: UNKNOWN), then the subcommand. Only
     * log/show/diff/whatchanged, status, branch (listing), rev-parse,
     * ls-files, blame and a bare or -v "remote" can be READ, each with its
     * own flag allow-list; every other subcommand is WRITE. */
    if (tok_eq(base1, base1_len, "git")) {
        static const char *const git_safe_globals[] = {
            "--no-pager", "-P", "-p", "--paginate", "--no-optional-locks",
            "--literal-pathspecs", "--glob-pathspecs", "--noglob-pathspecs",
            "--icase-pathspecs", "--no-replace-objects", "--no-lazy-fetch",
            "--no-advice", "--version", "--help", NULL
        };
        static const char *const git_valued_globals[] = {
            "-c", "--git-dir", "--work-tree", "--namespace", "--config-env",
            "--super-prefix", NULL
        };
        const char *scan = p;
        const char *ts;
        size_t tl;
        const char *sub_s = NULL;
        size_t sub_l = 0;
        CmdSafetyLevel level = CMD_READ;
        while (next_token(&scan, &ts, &tl)) {
            if (tok_is_obscured_flag(ts, tl)) {
                if (level < CMD_UNKNOWN) level = CMD_UNKNOWN;
                continue;
            }
            if (ts[0] != '-') { sub_s = ts; sub_l = tl; break; }
            if (tok_eq(ts, tl, "-C")) {
                const char *v; size_t vl;
                (void)next_token(&scan, &v, &vl);
                continue;
            }
            if (tok_in_list(ts, tl, git_safe_globals)) continue;
            if (level < CMD_UNKNOWN) level = CMD_UNKNOWN;
            if (tok_in_list(ts, tl, git_valued_globals)) {
                const char *v; size_t vl;
                (void)next_token(&scan, &v, &vl);
            }
        }
        if (sub_s) {
            CmdSafetyLevel sl = CMD_READ;
            const FlagSpec *spec = NULL;
            if (tok_eq(sub_s, sub_l, "log") || tok_eq(sub_s, sub_l, "show") ||
                tok_eq(sub_s, sub_l, "diff") || tok_eq(sub_s, sub_l, "whatchanged"))
                spec = &git_log_spec;
            else if (tok_eq(sub_s, sub_l, "status"))
                spec = &git_status_spec;
            else if (tok_eq(sub_s, sub_l, "rev-parse"))
                spec = &git_rev_parse_spec;
            else if (tok_eq(sub_s, sub_l, "ls-files"))
                spec = &git_ls_files_spec;
            else if (tok_eq(sub_s, sub_l, "blame"))
                spec = &git_blame_spec;

            if (spec) {
                sl = flag_scan(spec, scan, NULL);
            } else if (tok_eq(sub_s, sub_l, "branch")) {
                /* Listing only while every operand is a pattern of a list
                 * mode; an operand anywhere else names a branch to create. */
                static const char *const list_mode_flags[] = {
                    "--list", "--contains", "--no-contains", "--merged",
                    "--no-merged", "--points-at", NULL
                };
                int list_mode = 0, operands = 0, only = 0;
                const char *s2 = scan;
                while (next_token(&s2, &ts, &tl)) {
                    size_t nlen = 0;
                    while (nlen < tl && ts[nlen] != '=') nlen++;
                    if (!only && (tok_eq(ts, tl, "-l") || tok_in_list(ts, nlen, list_mode_flags)))
                        list_mode = 1;
                    if (flag_word(&git_branch_spec, ts, tl, &s2, &sl, &only)) operands++;
                }
                if (operands && !list_mode && sl < CMD_WRITE) sl = CMD_WRITE;
            } else if (tok_eq(sub_s, sub_l, "remote")) {
                const char *s2 = scan;
                while (next_token(&s2, &ts, &tl)) {
                    if (tok_eq(ts, tl, "-v") || tok_eq(ts, tl, "--verbose")) continue;
                    sl = CMD_WRITE;
                    break;
                }
            } else {
                sl = CMD_WRITE;
            }
            if (sl > level) level = sl;
        }
        if (has_sudo && level < CMD_WRITE) level = CMD_WRITE;
        if (reason_buf && reason_buf_size > 0 && level > CMD_READ) {
            if (sub_s)
                snprintf(reason_buf, reason_buf_size,
                         level == CMD_WRITE ? "git %.*s: changes the repository or writes a file"
                                            : "git %.*s: option outside the known-safe set",
                         (int)sub_l, sub_s);
            else
                snprintf(reason_buf, reason_buf_size, "git: option outside the known-safe set");
        }
        return level > redir ? level : redir;
    }

    /* ip: READ only when the verb after the object is absent or exactly
     * show/list/lst/get/help. Any other verb -- including an abbreviation,
     * which ip accepts ("ip a d" is "ip addr delete") -- is WRITE; one that
     * abbreviates flush or delete, and "link set", are CRITICAL. Global
     * options are matched by prefix in iproute2's own order, value-taking
     * ones consume their value, -batch/-force and unknown ones are
     * UNKNOWN. */
    if (tok_eq(base1, base1_len, "ip")) {
        static const struct {
            const char *name;
            unsigned char exact, takes, unknown;
        } ip_globals[] = {
            { "-loops", 0, 1, 0 }, { "-family", 0, 1, 0 }, { "-4", 1, 0, 0 },
            { "-6", 1, 0, 0 }, { "-0", 1, 0, 0 }, { "-M", 1, 0, 0 }, { "-B", 1, 0, 0 },
            { "-human", 0, 0, 0 }, { "-human-readable", 0, 0, 0 }, { "-iec", 0, 0, 0 },
            { "-stats", 0, 0, 0 }, { "-statistics", 0, 0, 0 }, { "-details", 0, 0, 0 },
            { "-resolve", 0, 0, 0 }, { "-oneline", 0, 0, 0 }, { "-timestamp", 0, 0, 0 },
            { "-tshort", 0, 0, 0 }, { "-Version", 0, 0, 0 }, { "-force", 0, 0, 1 },
            { "-batch", 0, 1, 1 }, { "-brief", 0, 0, 0 }, { "-json", 0, 0, 0 },
            { "-pretty", 0, 0, 0 }, { "-rcvbuf", 0, 1, 0 }, { "-color", 0, 0, 0 },
            { "-help", 0, 0, 0 }, { "-netns", 0, 1, 0 }, { "-Numeric", 0, 0, 0 },
            { "-all", 0, 0, 0 }, { "-echo", 1, 0, 0 },
            { NULL, 0, 0, 0 }
        };
        const char *scan = p;
        const char *ts;
        size_t tl;
        CmdSafetyLevel level = CMD_READ;
        int all_ns = 0;
        const char *obj_s = NULL, *verb_s = NULL;
        size_t obj_l = 0, verb_l = 0;
        while (next_token(&scan, &ts, &tl)) {
            if (tok_is_obscured_flag(ts, tl)) {
                if (level < CMD_UNKNOWN) level = CMD_UNKNOWN;
                continue;
            }
            if (ts[0] != '-') { obj_s = ts; obj_l = tl; break; }
            if (tok_eq(ts, tl, "--")) {
                if (next_token(&scan, &ts, &tl)) { obj_s = ts; obj_l = tl; }
                break;
            }
            const char *o = ts;
            size_t ol = tl;
            if (ol > 2 && o[1] == '-') { o++; ol--; }        /* "--json" */
            size_t nl = 0;
            while (nl < ol && o[nl] != '=') nl++;              /* "-color=always" */
            int found = -1;
            for (int i = 0; ip_globals[i].name; i++) {
                size_t gl = strlen(ip_globals[i].name);
                if (ip_globals[i].exact ? (nl == gl && memcmp(o, ip_globals[i].name, gl) == 0)
                                        : (nl <= gl && memcmp(o, ip_globals[i].name, nl) == 0)) {
                    found = i;
                    break;
                }
            }
            if (found < 0 || (nl < ol && strcmp(ip_globals[found].name, "-color") != 0)) {
                if (level < CMD_UNKNOWN) level = CMD_UNKNOWN;
                continue;
            }
            if (ip_globals[found].unknown && level < CMD_UNKNOWN) level = CMD_UNKNOWN;
            if (strcmp(ip_globals[found].name, "-all") == 0) all_ns = 1;
            if (ip_globals[found].takes) {
                const char *v; size_t vl;
                (void)next_token(&scan, &v, &vl);
            }
        }
        if (obj_s && next_token(&scan, &ts, &tl)) { verb_s = ts; verb_l = tl; }
        /* Objects with a second level ("ip xfrm state list", "ip mptcp
         * endpoint show"): the verb follows the sub-object. */
        if (verb_s && (tok_eq(obj_s, obj_l, "xfrm") || tok_eq(obj_s, obj_l, "mptcp") ||
                       tok_eq(obj_s, obj_l, "ioam") || tok_eq(obj_s, obj_l, "sr"))) {
            verb_s = NULL;
            verb_l = 0;
            if (next_token(&scan, &ts, &tl)) { verb_s = ts; verb_l = tl; }
        }

        /* "ip netns exec" / "ip vrf exec", by any unambiguous prefix ip
         * itself would accept ("ip netn e", "ip n e", "ip vrf e"): the
         * object matches when the token is a prefix of the full name and
         * the verb matches when it is a prefix of "exec". */
        if (obj_s && verb_s &&
            ((obj_l >= 1 && obj_l <= 5 && memcmp(obj_s, "netns", obj_l) == 0) ||
             (obj_l >= 1 && obj_l <= 3 && memcmp(obj_s, "vrf", obj_l) == 0)) &&
            verb_l >= 1 && verb_l <= 4 && memcmp(verb_s, "exec", verb_l) == 0) {
            int is_netns = (obj_l <= 5 && memcmp(obj_s, "netns", obj_l) == 0);
            /* ip [-all] netns exec [NAME] CMD... / ip vrf exec NAME CMD...
             * -- classify what runs inside the namespace/VRF recursively,
             * and never counted for less than WRITE regardless of what
             * that turns out to be: this always changes what's reachable
             * from this shell, even when the inner command only reads. */
            const char *name_s; size_t name_l;
            if ((is_netns && all_ns) || next_token(&scan, &name_s, &name_l)) {
                const char *cmd_start = NULL, *cmd_end = NULL;
                while (next_token(&scan, &ts, &tl)) {
                    if (!cmd_start) cmd_start = ts;
                    cmd_end = ts + tl;
                }
                CmdSafetyLevel sub = CMD_WRITE;
                if (cmd_start && cmd_end > cmd_start) {
                    CmdSafetyLevel inner = classify_linux_segment(cmd_start,
                                             (size_t)(cmd_end - cmd_start), NULL, 0);
                    if (inner > sub) sub = inner;
                }
                if (sub > level) level = sub;
                if (has_sudo && level < CMD_WRITE) level = CMD_WRITE;
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size,
                             is_netns ? "ip netns exec: runs a command in another namespace"
                                      : "ip vrf exec: runs a command in another VRF");
                return level > redir ? level : redir;
            }
        }

        /* An object ip does not know ("/ip firewall ..." is RouterOS, not
         * iproute2) makes ip exit with an error: UNKNOWN, not a verb rule. */
        if (obj_s) {
            static const char *const ip_objects[] = {
                "address", "addrlabel", "maddress", "route", "rule", "neighbor",
                "neighbour", "ntable", "ntbl", "link", "l2tp", "fou", "ila",
                "macsec", "tunnel", "tunl", "tuntap", "tap", "token", "tcpmetrics",
                "tcp_metrics", "monitor", "xfrm", "mroute", "mrule", "netns",
                "netconf", "vrf", "sr", "nexthop", "mptcp", "ioam", "stats",
                "help", NULL
            };
            int known = 0;
            for (int i = 0; ip_objects[i]; i++)
                if (obj_l <= strlen(ip_objects[i]) &&
                    memcmp(obj_s, ip_objects[i], obj_l) == 0) { known = 1; break; }
            if (!known) {
                if (level < CMD_UNKNOWN) level = CMD_UNKNOWN;
                verb_s = NULL;
            }
        }

        if (verb_s && !(tok_eq(verb_s, verb_l, "show") || tok_eq(verb_s, verb_l, "list") ||
                        tok_eq(verb_s, verb_l, "lst") || tok_eq(verb_s, verb_l, "get") ||
                        tok_eq(verb_s, verb_l, "help"))) {
            CmdSafetyLevel vl = CMD_WRITE;
            if ((verb_l <= 5 && memcmp(verb_s, "flush", verb_l) == 0) ||
                (verb_l <= 6 && memcmp(verb_s, "delete", verb_l) == 0) ||
                (obj_l <= 4 && memcmp(obj_s, "link", obj_l) == 0 &&
                 verb_l <= 3 && memcmp(verb_s, "set", verb_l) == 0))
                vl = CMD_CRITICAL;
            if (vl > level) level = vl;
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size, "ip %.*s %.*s: changes network state",
                         (int)obj_l, obj_s, (int)verb_l, verb_s);
        }
        if (has_sudo && level < CMD_WRITE) level = CMD_WRITE;
        if (level == CMD_UNKNOWN && reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "ip: option outside the known-safe set");
        if (level == CMD_CRITICAL) return level;
        return level > redir ? level : redir;
    }

    /* route: READ with the display options only. An operand is a change
     * (add, ...); del/delete/flush drop routes and are CRITICAL. */
    if (tok_eq(base1, base1_len, "route")) {
        const char *scan = p;
        const char *ts;
        size_t tl;
        CmdSafetyLevel level = CMD_READ;
        int only = 0;
        while (next_token(&scan, &ts, &tl)) {
            if (!flag_word(&route_spec, ts, tl, &scan, &level, &only)) continue;
            if (tok_eq(ts, tl, "del") || tok_eq(ts, tl, "delete") || tok_eq(ts, tl, "flush"))
                level = CMD_CRITICAL;
            else if (level < CMD_WRITE)
                level = CMD_WRITE;
            break;
        }
        if (has_sudo && level < CMD_WRITE) level = CMD_WRITE;
        if (reason_buf && reason_buf_size > 0 && level > CMD_READ)
            snprintf(reason_buf, reason_buf_size, "route: changes the routing table");
        if (level == CMD_CRITICAL) return level;
        return level > redir ? level : redir;
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

    /* rpm -q* / pacman -Q*: any query subcommand is read-only (spec 3.3
     * writes these as "-q*" / "-Q*" -- the exact -qa/-qi/-ql and
     * -Q/-Qs/-Qi rows above in linux_subcmd_rules only cover the common
     * forms; this prefix check catches the rest, e.g. "-qf", "-Qo"). Must
     * run before the linux_write_cmds check below, since both "rpm" and
     * "pacman" are flat WRITE by default. */
    if (tok_eq(base1, base1_len, "rpm")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            tok_prefix(tok2_start, tok2_len, "-q")) {
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size, "rpm -q*: query, read-only");
            return redir;
        }
    }
    if (tok_eq(base1, base1_len, "pacman")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            tok_prefix(tok2_start, tok2_len, "-Q")) {
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size, "pacman -Q*: query, read-only");
            return redir;
        }
    }

    if (tok_in_list(base1, base1_len, linux_write_cmds)) {
        CmdSafetyLevel level = CMD_WRITE;
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "write command: %.*s",
                     (int)base1_len, base1);
        return level > redir ? level : redir;
    }

    if (tok_eq(base1, base1_len, "sed")) {
        /* Word by word with shell quoting, so a quoted script is one word:
         * -e/--expression values are scripts; otherwise the first operand
         * is the script and the rest are input files (never parsed as
         * script, so a file named "error.log" is not an e command). */
        const char *scan = p;
        char w[1024];
        int trunc = 0;
        const char *rs, *re;
        CmdSafetyLevel level = CMD_READ;
        int script_seen = 0, expect_script = 0, expect_value = 0, operands_only = 0;
        while (next_shell_word(&scan, seg_end, w, sizeof w, &trunc, &rs, &re)) {
            size_t wl = strlen(w);
            CmdSafetyLevel wlv = CMD_READ;
            if (trunc && level < CMD_UNKNOWN) level = CMD_UNKNOWN;
            if (!operands_only && tok_has_active_expansion(rs, (size_t)(re - rs))) {
                if (level < CMD_UNKNOWN) level = CMD_UNKNOWN;
            }
            if (expect_script) {
                wlv = sed_script_level(w);
                expect_script = 0;
            } else if (expect_value) {
                expect_value = 0;
            } else if (!operands_only && wl >= 2 && w[0] == '-' && w[1] == '-') {
                if (wl == 2) operands_only = 1;
                else if (tok_prefix(w, wl, "--expression=")) {
                    wlv = sed_script_level(w + 13);
                    script_seen = 1;
                } else if (tok_eq(w, wl, "--expression")) {
                    expect_script = 1;
                    script_seen = 1;
                } else if (tok_prefix(w, wl, "--in-place")) {
                    wlv = CMD_WRITE;
                } else if (tok_prefix(w, wl, "--file")) {
                    wlv = CMD_UNKNOWN;
                    script_seen = 1;
                    if (tok_eq(w, wl, "--file")) expect_value = 1;
                } else if (tok_eq(w, wl, "--line-length")) {
                    expect_value = 1;
                } else if (!tok_prefix(w, wl, "--line-length=") &&
                           !sed_is_safe_long_flag(w, wl)) {
                    wlv = CMD_UNKNOWN;
                }
            } else if (!operands_only && wl >= 2 && w[0] == '-') {
                for (size_t i = 1; i < wl; i++) {
                    char c = w[i];
                    if (c == 'i') { wlv = CMD_WRITE; break; } /* rest: suffix */
                    if (c == 'e') {
                        script_seen = 1;
                        if (w[i + 1]) wlv = sed_script_level(w + i + 1);
                        else expect_script = 1;
                        break;
                    }
                    if (c == 'f') {
                        wlv = CMD_UNKNOWN;
                        script_seen = 1;
                        if (!w[i + 1]) expect_value = 1;
                        break;
                    }
                    if (c == 'l') {
                        if (!w[i + 1]) expect_value = 1;
                        break;
                    }
                    if (c == 'n' || c == 'E' || c == 'r' || c == 's' ||
                        c == 'z' || c == 'u')
                        continue;
                    wlv = CMD_UNKNOWN;
                    break;
                }
            } else if (!script_seen) {
                wlv = sed_script_level(w);
                script_seen = 1;
            }
            if (wlv > level) level = wlv;
        }
        if (has_sudo && level < CMD_WRITE) level = CMD_WRITE;
        if (reason_buf && reason_buf_size > 0 && level > CMD_READ)
            snprintf(reason_buf, reason_buf_size,
                     level == CMD_WRITE ? "sed: in-place edit or w/W script command"
                                        : "sed: -f/unrecognised flag or an e script command");
        return level > redir ? level : redir;
    }

    if (tok_eq(base1, base1_len, "perl")) {
        /* Not a read command in any form -- at least UNKNOWN, WRITE if
         * "-i" (in-place edit, any spelling incl. combined "-pie", "-0pi")
         * appears anywhere in a short flag cluster. */
        CmdSafetyLevel level = CMD_UNKNOWN;
        const char *scan = p;
        const char *ts;
        size_t tl;
        while (next_token(&scan, &ts, &tl)) {
            if (tl > 1 && ts[0] == '-' && ts[1] != '-') {
                for (size_t i = 1; i < tl; i++) {
                    if (ts[i] == 'i') { level = CMD_WRITE; break; }
                }
            }
        }
        if (has_sudo && level < CMD_WRITE) level = CMD_WRITE;
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size,
                     level == CMD_WRITE ? "perl in-place edit" : "perl: not a read command");
        return level > redir ? level : redir;
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
            if (tok_eq(tok2_start, tok2_len, "disable")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "ufw disable");
                return CMD_CRITICAL;
            }
            /* ufw status: a pure query, not a config change (F8, spec 3.3
             * / 17 corner case) -- narrow SAFE override, doesn't touch any
             * other ufw form */
            if (tok_eq(tok2_start, tok2_len, "status"))
                return CMD_READ;
        }
        return CMD_WRITE;
    }

    if (tok_eq(base1, base1_len, "zpool") || tok_eq(base1, base1_len, "zfs")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len)) {
            if (tok_eq(tok2_start, tok2_len, "destroy") ||
                (tok_eq(base1, base1_len, "zfs") && tok_eq(tok2_start, tok2_len, "rollback"))) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "%.*s %.*s",
                             (int)base1_len, base1, (int)tok2_len, tok2_start);
                return CMD_CRITICAL;
            }
        }
    }

    /* service / init-system stop forms with a middle wildcard target
     * ("service <x> stop", "rc-service <x> stop", "/etc/init.d/<x> stop") --
     * these don't fit the cmd+subcmd table shape because the target name
     * sits between the command and the verb (spec 3.1 F6). */
    if (tok_eq(base1, base1_len, "service") || tok_eq(base1, base1_len, "rc-service")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len)) {
            const char *p3 = p2;
            if (next_token(&p3, &tok3_start, &tok3_len) &&
                tok_eq(tok3_start, tok3_len, "stop")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "%.*s stop: stops a running service",
                             (int)base1_len, base1);
                return CMD_CRITICAL;
            }
        }
        CmdSafetyLevel level = CMD_WRITE;
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "%.*s command", (int)base1_len, base1);
        return level > redir ? level : redir;
    }
    if (tok_prefix(tok1_start, tok1_len, "/etc/init.d/")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            tok_eq(tok2_start, tok2_len, "stop")) {
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size, "init.d stop: stops a running service");
            return CMD_CRITICAL;
        }
        CmdSafetyLevel level = CMD_WRITE;
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "init.d service command");
        return level > redir ? level : redir;
    }

    /* ifconfig <if> down: verb is the last token, target is the middle one */
    if (tok_eq(base1, base1_len, "ifconfig")) {
        const char *scan = p;
        const char *ts;
        size_t tl;
        while (next_token(&scan, &ts, &tl)) {
            if (tok_eq(ts, tl, "down")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "ifconfig down: drops the interface");
                return CMD_CRITICAL;
            }
        }
        return redir;
    }

    /* find: -delete, -exec/-execdir/-ok/-okdir, -fprint, -fprint0,
     * -fprintf, -fls, and any
     * primary outside the known-safe set. Read word by word with shell
     * quoting so an -exec's "\;" or ';' terminator ends only that -exec
     * and the scan carries on to the primaries after it (F9). */
    if (tok_eq(base1, base1_len, "find")) {
        const char *scan = p;
        char w[512];
        int trunc = 0;
        const char *rs, *re;
        CmdSafetyLevel level = CMD_READ;
        while (next_shell_word(&scan, seg_end, w, sizeof w, &trunc, &rs, &re)) {
            size_t wl = strlen(w);
            if (tok_has_active_expansion(rs, (size_t)(re - rs))) {
                if (level < CMD_UNKNOWN) level = CMD_UNKNOWN;
            }
            if (wl == 0 || w[0] != '-') continue; /* path, operand, ( ) ! */

            if (tok_eq(w, wl, "-delete")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "find -delete: removes matched files");
                return CMD_CRITICAL;
            }

            if (tok_eq(w, wl, "-exec") || tok_eq(w, wl, "-execdir")
                || tok_eq(w, wl, "-ok") || tok_eq(w, wl, "-okdir")) {
                const char *cmd_start = NULL, *cmd_end = NULL;
                char first[512];
                char cw[512];
                const char *crs, *cre;
                int ctrunc = 0;
                first[0] = '\0';
                while (next_shell_word(&scan, seg_end, cw, sizeof cw, &ctrunc, &crs, &cre)) {
                    if (strcmp(cw, ";") == 0 || strcmp(cw, "+") == 0) break;
                    if (!cmd_start) {
                        cmd_start = crs;
                        snprintf(first, sizeof first, "%s", cw);
                    }
                    cmd_end = cre;
                }
                if (!cmd_start || !cmd_end) {
                    if (level < CMD_UNKNOWN) level = CMD_UNKNOWN;
                    continue;
                }
                {
                    const char *ebase;
                    size_t ebase_len;
                    ebase = strip_path(first, strlen(first), &ebase_len);
                    if (tok_eq(ebase, ebase_len, "rm") || tok_eq(ebase, ebase_len, "shred")
                        || tok_eq(ebase, ebase_len, "unlink")) {
                        if (reason_buf && reason_buf_size > 0)
                            snprintf(reason_buf, reason_buf_size,
                                     "find %s: removes matched files", w);
                        return CMD_CRITICAL;
                    }
                }
                {
                    CmdSafetyLevel sub = classify_linux_segment(cmd_start,
                                             (size_t)(cmd_end - cmd_start), NULL, 0);
                    if (sub < CMD_UNKNOWN) sub = CMD_UNKNOWN;
                    if (sub > level) level = sub;
                }
                continue;
            }

            if (tok_eq(w, wl, "-fprint") || tok_eq(w, wl, "-fprint0") ||
                tok_eq(w, wl, "-fls") || tok_eq(w, wl, "-fprintf")) {
                if (level < CMD_WRITE) level = CMD_WRITE;
                continue;
            }

            {
                int takes_value = 0;
                if (find_primary_is_allowed(w, wl, &takes_value)) {
                    if (takes_value) {
                        char v[512];
                        const char *vs, *ve;
                        int vtrunc = 0;
                        (void)next_shell_word(&scan, seg_end, v, sizeof v, &vtrunc, &vs, &ve);
                    }
                    continue;
                }
            }
            if (level < CMD_UNKNOWN) level = CMD_UNKNOWN;
        }
        if (has_sudo && level < CMD_WRITE) level = CMD_WRITE;
        if (reason_buf && reason_buf_size > 0 && level > CMD_READ)
            snprintf(reason_buf, reason_buf_size,
                     "find: primary outside the known-safe set, or -exec/-fprint*");
        return level > redir ? level : redir;
    }

    /* sort: -o/--output (and any abbreviation of it) writes a file;
     * --compress-program and every other unlisted flag are UNKNOWN. */
    if (tok_eq(base1, base1_len, "sort")) {
        CmdSafetyLevel level = flag_scan(&sort_spec, p, NULL);
        if (has_sudo && level < CMD_WRITE) level = CMD_WRITE;
        if (reason_buf && reason_buf_size > 0 && level > CMD_READ)
            snprintf(reason_buf, reason_buf_size,
                     level == CMD_WRITE ? "sort -o/--output: writes to a file"
                                        : "sort: flag outside the known-safe set");
        return level > redir ? level : redir;
    }

    /* uniq: a second operand is the output file. */
    if (tok_eq(base1, base1_len, "uniq")) {
        int operands = 0;
        CmdSafetyLevel level = flag_scan(&uniq_spec, p, &operands);
        if (operands >= 2) level = CMD_WRITE;
        if (has_sudo && level < CMD_WRITE) level = CMD_WRITE;
        if (reason_buf && reason_buf_size > 0 && level > CMD_READ)
            snprintf(reason_buf, reason_buf_size,
                     level == CMD_WRITE ? "uniq: a second file argument is the output file"
                                        : "uniq: flag outside the known-safe set");
        return level > redir ? level : redir;
    }

    /* history: bare or "history N" lists. -p only expands (UNKNOWN); every
     * other option (-c -w -d -a -r -n -s, however combined) changes the
     * history list or file. */
    if (tok_eq(base1, base1_len, "history")) {
        const char *scan = p;
        const char *ts;
        size_t tl;
        CmdSafetyLevel level = CMD_READ;
        while (next_token(&scan, &ts, &tl)) {
            if (tok_is_obscured_flag(ts, tl)) {
                if (level < CMD_UNKNOWN) level = CMD_UNKNOWN;
            } else if (tok_eq(ts, tl, "-p")) {
                if (level < CMD_UNKNOWN) level = CMD_UNKNOWN;
            } else if (ts[0] == '-') {
                level = CMD_WRITE;
            } else {
                size_t k = 0;
                while (k < tl && isdigit((unsigned char)ts[k])) k++;
                if (k != tl && level < CMD_UNKNOWN) level = CMD_UNKNOWN;
            }
        }
        if (has_sudo && level < CMD_WRITE) level = CMD_WRITE;
        if (reason_buf && reason_buf_size > 0 && level > CMD_READ)
            snprintf(reason_buf, reason_buf_size, "history: modifies the history list or file");
        return level > redir ? level : redir;
    }

    /* less: flags from less_spec (-o/-O/--log-file write a log file); a
     * "+cmd" operand is a command run on start, READ only for a line
     * number, G, g, F, or a /pattern or ?pattern search -- anything else
     * ("+!cmd" runs a shell, "+|" pipes, "+v" starts an editor, "+s" saves)
     * is UNKNOWN. */
    if (tok_eq(base1, base1_len, "less")) {
        const char *scan = p;
        const char *ts;
        size_t tl;
        CmdSafetyLevel level = CMD_READ;
        int only = 0;
        while (next_token(&scan, &ts, &tl)) {
            if (!flag_word(&less_spec, ts, tl, &scan, &level, &only)) continue;
            char w[512];
            tok_unquote(ts, tl, w, sizeof w);
            if (w[0] != '+') continue;
            const char *c = w + 1;
            if (*c == '+') c++;
            int ok;
            if (*c == '/' || *c == '?') ok = 1;
            else {
                while (isdigit((unsigned char)*c)) c++;
                if (*c && strchr("gGpF%", *c)) c++;
                ok = (*c == '\0');
            }
            if (!ok && level < CMD_UNKNOWN) level = CMD_UNKNOWN;
        }
        if (has_sudo && level < CMD_WRITE) level = CMD_WRITE;
        if (reason_buf && reason_buf_size > 0 && level > CMD_READ)
            snprintf(reason_buf, reason_buf_size,
                     level == CMD_WRITE ? "less: log-file flag writes to a file"
                                        : "less: start-up command or flag outside the known-safe set");
        return level > redir ? level : redir;
    }

    /* date: "+FORMAT" and the display options are READ; any other operand
     * sets the clock (as does -s/--set, also caught above). */
    if (tok_eq(base1, base1_len, "date")) {
        const char *scan = p;
        const char *ts;
        size_t tl;
        CmdSafetyLevel level = CMD_READ;
        int only = 0;
        while (next_token(&scan, &ts, &tl)) {
            if (!flag_word(&date_spec, ts, tl, &scan, &level, &only)) continue;
            char w[8];
            tok_unquote(ts, tl, w, sizeof w);
            if (w[0] != '+') level = CMD_WRITE;
        }
        if (has_sudo && level < CMD_WRITE) level = CMD_WRITE;
        if (reason_buf && reason_buf_size > 0 && level > CMD_READ)
            snprintf(reason_buf, reason_buf_size, "date: sets the clock or uses an unlisted flag");
        return level > redir ? level : redir;
    }

    /* The other READ commands with a flag allow-list (read_cmd_specs). */
    for (int i = 0; read_cmd_specs[i].cmd; i++) {
        if (!tok_eq(base1, base1_len, read_cmd_specs[i].cmd)) continue;
        int operands = 0;
        CmdSafetyLevel level = flag_scan(read_cmd_specs[i].spec, p, &operands);
        if (read_cmd_specs[i].operands && operands > 0) level = CMD_WRITE;
        if (has_sudo && level < CMD_WRITE) level = CMD_WRITE;
        if (reason_buf && reason_buf_size > 0 && level > CMD_READ)
            snprintf(reason_buf, reason_buf_size,
                     level == CMD_WRITE ? "%s: flag or argument that writes or changes state"
                                        : "%s: flag outside the known-safe set",
                     read_cmd_specs[i].cmd);
        return level > redir ? level : redir;
    }

    /* mount: bare (queries current mounts) is SAFE, mount with arguments
     * changes system state (WRITE). */
    if (tok_eq(base1, base1_len, "mount")) {
        const char *p2 = p;
        if (!next_token(&p2, &tok2_start, &tok2_len))
            return redir;
        CmdSafetyLevel level = CMD_WRITE;
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "mount: mounts a filesystem");
        return level > redir ? level : redir;
    }

    /* timedatectl set-*: prefix match on the subcommand */
    if (tok_eq(base1, base1_len, "timedatectl")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            tok_prefix(tok2_start, tok2_len, "set-")) {
            CmdSafetyLevel level = CMD_WRITE;
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size, "timedatectl set-*: changes system clock config");
            return level > redir ? level : redir;
        }
        return redir;
    }

    if (has_sudo) {
        CmdSafetyLevel level = CMD_WRITE;
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "sudo escalation of %.*s",
                     (int)base1_len, base1);
        return level > redir ? level : redir;
    }

    /* C2 fix (spec 3, coverage spec 3.3): no write/critical rule and no
     * sudo escalation claimed this command. This used to fall through to
     * SAFE here -- the audit C2 bug, where an unrecognised command
     * auto-approved at the lowest auto-approve level. It now reaches SAFE
     * only by matching the explicit allow-list above (the subcommand rules)
     * or linux_read_cmds below; anything else is honestly CMD_UNKNOWN,
     * combined with whatever scan_redirects already found -- a redirect
     * still raises UNKNOWN to WRITE ("frobnicate > /etc/passwd" is WRITE,
     * not UNKNOWN, because the redirect itself writes regardless of
     * whether "frobnicate" is recognised). */
    if (tok_in_list(base1, base1_len, linux_read_cmds))
        return redir;

    {
        CmdSafetyLevel level = CMD_UNKNOWN;
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "unrecognised command: %.*s",
                     (int)base1_len, base1);
        return level > redir ? level : redir;
    }
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
        return CMD_READ;

    /* enable secret / enable password: sets the privileged-mode secret --
     * a config change, not the mode change a bare "enable" is. Checked
     * before the bare "enable" -> READ rule just below, which would
     * otherwise swallow it (tok1 alone is "enable" either way). */
    if (tok_eq_ci(tok1_start, tok1_len, "enable")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            (tok_eq_ci(tok2_start, tok2_len, "secret") ||
             tok_eq_ci(tok2_start, tok2_len, "password"))) {
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size,
                         "enable %.*s: sets the privileged-mode secret",
                         (int)tok2_len, tok2_start);
            return CMD_WRITE;
        }
    }

    /* --- Safe commands --- */
    if (tok_prefix_ci(tok1_start, tok1_len, "show"))
        return CMD_READ;
    if (tok_eq_ci(tok1_start, tok1_len, "ping") ||
        tok_eq_ci(tok1_start, tok1_len, "traceroute") ||
        tok_eq_ci(tok1_start, tok1_len, "terminal") ||
        tok_eq_ci(tok1_start, tok1_len, "enable") ||
        tok_eq_ci(tok1_start, tok1_len, "disable") ||
        tok_eq_ci(tok1_start, tok1_len, "exit") ||
        tok_eq_ci(tok1_start, tok1_len, "end") ||
        tok_eq_ci(tok1_start, tok1_len, "dir") ||
        tok_eq_ci(tok1_start, tok1_len, "verify") ||
        tok_eq_ci(tok1_start, tok1_len, "more") ||
        tok_eq_ci(tok1_start, tok1_len, "where") ||
        tok_eq_ci(tok1_start, tok1_len, "who"))
        return CMD_READ;

    /* reload cancel: cancels a pending reload, explicitly SAFE -- must be
     * checked before the general "reload" -> CRITICAL rule below */
    if (tok_eq_ci(tok1_start, tok1_len, "reload")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            tok_eq_ci(tok2_start, tok2_len, "cancel"))
            return CMD_READ;
    }

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
            (tok_eq_ci(tok2_start, tok2_len, "force-switchover") ||
             tok_eq_ci(tok2_start, tok2_len, "reload"))) {
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size, "redundancy %.*s",
                         (int)tok2_len, tok2_start);
            return CMD_CRITICAL;
        }
    }
    /* hw-module reset / reload; microcode reload: hardware/RP restart */
    if (tok_eq_ci(tok1_start, tok1_len, "hw-module")) {
        const char *scan = p;
        const char *ts;
        size_t tl;
        while (next_token(&scan, &ts, &tl)) {
            if (tok_eq_ci(ts, tl, "reset") || tok_eq_ci(ts, tl, "reload")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "hw-module %.*s: hardware restart",
                             (int)tl, ts);
                return CMD_CRITICAL;
            }
        }
    }
    if (tok_eq_ci(tok1_start, tok1_len, "microcode")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            tok_eq_ci(tok2_start, tok2_len, "reload")) {
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size, "microcode reload");
            return CMD_CRITICAL;
        }
    }
    /* default interface <x>: resets an interface to its defaults */
    if (tok_eq_ci(tok1_start, tok1_len, "default")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            tok_eq_ci(tok2_start, tok2_len, "interface")) {
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size, "default interface: resets to defaults");
            return CMD_CRITICAL;
        }
    }
    /* boot system: changes the next-boot image -- must be checked before
     * the generic "boot" -> WRITE rule further down */
    if (tok_eq_ci(tok1_start, tok1_len, "boot")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            tok_eq_ci(tok2_start, tok2_len, "system")) {
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size, "boot system: changes next-boot image");
            return CMD_CRITICAL;
        }
    }
    /* copy <src> running-config / startup-config: replaces the live or
     * boot config. Only the *destination* (the last token) decides this --
     * "copy running-config tftp:" is a harmless export where
     * "running-config" is the source, not the target, so we track the
     * last token seen rather than matching any token (M2's "scan every
     * remaining token" shape, applied to the position that actually
     * varies here: flags/source vary, destination is the last token). */
    if (tok_eq_ci(tok1_start, tok1_len, "copy")) {
        const char *scan = p;
        const char *ts, *last_start = NULL;
        size_t tl, last_len = 0;
        while (next_token(&scan, &ts, &tl)) {
            last_start = ts;
            last_len = tl;
        }
        if (last_start &&
            (tok_eq_ci(last_start, last_len, "running-config") ||
             tok_eq_ci(last_start, last_len, "startup-config"))) {
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size, "copy to %.*s: replaces device config",
                         (int)last_len, last_start);
            return CMD_CRITICAL;
        }
        /* falls through to the generic "copy" -> WRITE rule below */
    }
    /* commit replace / process restart: IOS-XR */
    if (tok_eq_ci(tok1_start, tok1_len, "commit")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            tok_eq_ci(tok2_start, tok2_len, "replace")) {
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size, "commit replace: IOS-XR config replacement");
            return CMD_CRITICAL;
        }
    }
    if (tok_eq_ci(tok1_start, tok1_len, "process")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            tok_eq_ci(tok2_start, tok2_len, "restart")) {
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size, "process restart: IOS-XR daemon restart");
            return CMD_CRITICAL;
        }
    }
    /* test crash / write core: forced crash or core dump */
    if (tok_eq_ci(tok1_start, tok1_len, "test")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            tok_eq_ci(tok2_start, tok2_len, "crash")) {
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size, "test crash: forces a device crash");
            return CMD_CRITICAL;
        }
    }
    /* install add/activate/commit/remove; request platform software
     * package install: IOS-XE image install */
    if (tok_eq_ci(tok1_start, tok1_len, "install")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            (tok_eq_ci(tok2_start, tok2_len, "add") ||
             tok_eq_ci(tok2_start, tok2_len, "activate") ||
             tok_eq_ci(tok2_start, tok2_len, "commit") ||
             tok_eq_ci(tok2_start, tok2_len, "remove"))) {
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size, "install %.*s: IOS-XE image install",
                         (int)tok2_len, tok2_start);
            return CMD_CRITICAL;
        }
    }
    if (tok_eq_ci(tok1_start, tok1_len, "request")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            tok_eq_ci(tok2_start, tok2_len, "platform")) {
            const char *scan = p2;
            const char *ts;
            size_t tl;
            while (next_token(&scan, &ts, &tl)) {
                if (tok_eq_ci(ts, tl, "install")) {
                    if (reason_buf && reason_buf_size > 0)
                        snprintf(reason_buf, reason_buf_size,
                                 "request platform software package install");
                    return CMD_CRITICAL;
                }
            }
        }
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "request command");
        return CMD_WRITE;
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
            /* write core: forces a core dump */
            if (tok_eq_ci(tok1_start, tok1_len, "write") &&
                tok_eq_ci(tok2_start, tok2_len, "core")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "write core: forces a core dump");
                return CMD_CRITICAL;
            }
            /* erase startup-config / erase nvram: / erase with any
             * device-fs token anywhere in the segment (M2, fixes F3) */
            if (tok_eq_ci(tok1_start, tok1_len, "erase") &&
                (tok_prefix_ci(tok2_start, tok2_len, "startup") ||
                 tok_prefix_ci(tok2_start, tok2_len, "nvram") ||
                 seg_has_device_fs_token(p))) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "erase config");
                return CMD_CRITICAL;
            }
            /* delete / format / squeeze with a device-fs token anywhere
             * in the segment (M2, fixes F3: flags before the path, e.g.
             * "delete /force /recursive flash:x") */
            if ((tok_eq_ci(tok1_start, tok1_len, "delete") ||
                 tok_eq_ci(tok1_start, tok1_len, "format") ||
                 tok_eq_ci(tok1_start, tok1_len, "squeeze")) &&
                seg_has_device_fs_token(p)) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "%.*s device filesystem",
                             (int)tok1_len, tok1_start);
                return CMD_CRITICAL;
            }
            /* configure replace / config replace: wholesale config
             * replacement (F4). Prefix match on "conf" per spec M/F4 so
             * both "configure replace" and "config replace" match. */
            if (tok_prefix_ci(tok1_start, tok1_len, "conf") &&
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
            /* clear bgp (without "ip"); clear isis */
            if (tok_eq_ci(tok2_start, tok2_len, "bgp") ||
                tok_eq_ci(tok2_start, tok2_len, "isis")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "clear %.*s: resets adjacencies",
                             (int)tok2_len, tok2_start);
                return CMD_CRITICAL;
            }
            /* clear ip bgp / ospf / eigrp / nat translation / route */
            if (tok_eq_ci(tok2_start, tok2_len, "ip")) {
                const char *p3 = p2;
                if (next_token(&p3, &tok3_start, &tok3_len)) {
                    if (tok_prefix_ci(tok3_start, tok3_len, "bgp") ||
                        tok_prefix_ci(tok3_start, tok3_len, "ospf") ||
                        tok_prefix_ci(tok3_start, tok3_len, "eigrp") ||
                        tok_prefix_ci(tok3_start, tok3_len, "nat") ||
                        tok_prefix_ci(tok3_start, tok3_len, "route")) {
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
            /* no ip routing / no ip access-list / no ip nat (F11) */
            if (tok_eq_ci(tok2_start, tok2_len, "ip")) {
                const char *p3 = p2;
                if (next_token(&p3, &tok3_start, &tok3_len) &&
                    (tok_eq_ci(tok3_start, tok3_len, "routing") ||
                     tok_prefix_ci(tok3_start, tok3_len, "access-list") ||
                     tok_prefix_ci(tok3_start, tok3_len, "nat"))) {
                    if (reason_buf && reason_buf_size > 0)
                        snprintf(reason_buf, reason_buf_size, "no ip %.*s: traffic drop / lock-out",
                                 (int)tok3_len, tok3_start);
                    return CMD_CRITICAL;
                }
            }
            /* no interface / username / aaa / line / enable / access-list /
             * crypto / standby / vrrp / hsrp (F11) */
            if (tok_eq_ci(tok2_start, tok2_len, "interface") ||
                tok_eq_ci(tok2_start, tok2_len, "username") ||
                tok_eq_ci(tok2_start, tok2_len, "aaa") ||
                tok_eq_ci(tok2_start, tok2_len, "line") ||
                tok_eq_ci(tok2_start, tok2_len, "enable") ||
                tok_eq_ci(tok2_start, tok2_len, "access-list") ||
                tok_eq_ci(tok2_start, tok2_len, "crypto") ||
                tok_eq_ci(tok2_start, tok2_len, "standby") ||
                tok_eq_ci(tok2_start, tok2_len, "vrrp") ||
                tok_eq_ci(tok2_start, tok2_len, "hsrp")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "no %.*s: traffic drop / lock-out",
                             (int)tok2_len, tok2_start);
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
        tok_eq_ci(tok1_start, tok1_len, "boot") ||
        /* spec 4 WRITE additions */
        tok_eq_ci(tok1_start, tok1_len, "archive") ||
        tok_eq_ci(tok1_start, tok1_len, "license") ||
        tok_eq_ci(tok1_start, tok1_len, "event") ||
        tok_eq_ci(tok1_start, tok1_len, "policy-map") ||
        tok_eq_ci(tok1_start, tok1_len, "class-map") ||
        tok_eq_ci(tok1_start, tok1_len, "track") ||
        tok_eq_ci(tok1_start, tok1_len, "object-group") ||
        tok_eq_ci(tok1_start, tok1_len, "key") ||
        tok_eq_ci(tok1_start, tok1_len, "crypto") ||
        tok_eq_ci(tok1_start, tok1_len, "tunnel") ||
        tok_eq_ci(tok1_start, tok1_len, "vrf") ||
        tok_eq_ci(tok1_start, tok1_len, "monitor") ||
        tok_eq_ci(tok1_start, tok1_len, "mac") ||
        tok_eq_ci(tok1_start, tok1_len, "errdisable") ||
        tok_eq_ci(tok1_start, tok1_len, "power") ||
        tok_eq_ci(tok1_start, tok1_len, "privilege") ||
        tok_eq_ci(tok1_start, tok1_len, "aaa") ||
        tok_eq_ci(tok1_start, tok1_len, "tacacs-server") ||
        tok_eq_ci(tok1_start, tok1_len, "radius-server") ||
        tok_eq_ci(tok1_start, tok1_len, "clock")) {
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

    /* No write/critical rule claimed this IOS line. It used to guess
     * CMD_WRITE here ("conservative"); that was still a guess, not a
     * finding, so an unrecognised command on the switch is now honestly
     * CMD_UNKNOWN rather than asserted as a write (spec section 3, "Network
     * platforms fall through to UNKNOWN"). NX-OS and ASA inherit this via
     * their delegation to classify_cisco_ios_segment() below. */
    if (reason_buf && reason_buf_size > 0)
        snprintf(reason_buf, reason_buf_size, "unrecognised IOS command: %.*s",
                 (int)tok1_len, tok1_start);
    return CMD_UNKNOWN;
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
        return CMD_READ;

    /* NX-OS specific critical: reload module */
    if (tok_eq_ci(tok1_start, tok1_len, "reload")) {
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "reload: device/module reboot");
        return CMD_CRITICAL;
    }

    /* rollback: replaces the running config from a checkpoint (F12,
     * moved from WRITE -- this is not the reversible "another command
     * undoes it" WRITE case, it wholesale-replaces the running config) */
    if (tok_eq_ci(tok1_start, tok1_len, "rollback")) {
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "rollback: replaces running config");
        return CMD_CRITICAL;
    }

    /* install all / activate / deactivate / remove */
    if (tok_eq_ci(tok1_start, tok1_len, "install")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            (tok_eq_ci(tok2_start, tok2_len, "all") ||
             tok_eq_ci(tok2_start, tok2_len, "activate") ||
             tok_eq_ci(tok2_start, tok2_len, "deactivate") ||
             tok_eq_ci(tok2_start, tok2_len, "remove"))) {
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size, "install %.*s: system upgrade",
                         (int)tok2_len, tok2_start);
            return CMD_CRITICAL;
        }
    }

    /* system switchover; out-of-service module; poweroff module;
     * purge module; attach module; run bash; boot nxos */
    if (tok_eq_ci(tok1_start, tok1_len, "system")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            tok_eq_ci(tok2_start, tok2_len, "switchover")) {
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size, "system switchover: supervisor failover");
            return CMD_CRITICAL;
        }
    }
    if (tok_eq_ci(tok1_start, tok1_len, "out-of-service") ||
        tok_eq_ci(tok1_start, tok1_len, "poweroff") ||
        tok_eq_ci(tok1_start, tok1_len, "purge") ||
        tok_eq_ci(tok1_start, tok1_len, "attach")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            tok_eq_ci(tok2_start, tok2_len, "module")) {
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size, "%.*s module",
                         (int)tok1_len, tok1_start);
            return CMD_CRITICAL;
        }
    }
    if (tok_eq_ci(tok1_start, tok1_len, "run")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            tok_eq_ci(tok2_start, tok2_len, "bash")) {
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size, "run bash: drops to a Linux shell");
            return CMD_CRITICAL;
        }
    }
    if (tok_eq_ci(tok1_start, tok1_len, "boot")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            tok_eq_ci(tok2_start, tok2_len, "nxos")) {
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size, "boot nxos: changes next-boot image");
            return CMD_CRITICAL;
        }
    }
    /* guestshell destroy (critical) / guestshell enable (write) */
    if (tok_eq_ci(tok1_start, tok1_len, "guestshell")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len)) {
            if (tok_eq_ci(tok2_start, tok2_len, "destroy")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "guestshell destroy");
                return CMD_CRITICAL;
            }
            if (tok_eq_ci(tok2_start, tok2_len, "enable")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "guestshell enable");
                return CMD_WRITE;
            }
        }
    }

    /* no vpc / no vpc domain / no feature <any> / no vrf context */
    if (tok_eq_ci(tok1_start, tok1_len, "no")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len)) {
            if (tok_eq_ci(tok2_start, tok2_len, "vpc")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "no vpc: removes vPC config");
                return CMD_CRITICAL;
            }
            /* no feature <any>: disabling any feature discards that
             * feature's entire configuration (F12 -- was nv-overlay-only) */
            if (tok_eq_ci(tok2_start, tok2_len, "feature")) {
                const char *p3 = p2;
                if (next_token(&p3, &tok3_start, &tok3_len)) {
                    if (reason_buf && reason_buf_size > 0)
                        snprintf(reason_buf, reason_buf_size,
                                 "no feature %.*s: discards feature config",
                                 (int)tok3_len, tok3_start);
                    return CMD_CRITICAL;
                }
            }
            if (tok_eq_ci(tok2_start, tok2_len, "vrf")) {
                const char *p3 = p2;
                if (next_token(&p3, &tok3_start, &tok3_len) &&
                    tok_eq_ci(tok3_start, tok3_len, "context")) {
                    if (reason_buf && reason_buf_size > 0)
                        snprintf(reason_buf, reason_buf_size, "no vrf context: removes VRF");
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

    /* NX-OS specific write: feature, checkpoint, vdc */
    if (tok_eq_ci(tok1_start, tok1_len, "feature") ||
        tok_eq_ci(tok1_start, tok1_len, "checkpoint") ||
        tok_eq_ci(tok1_start, tok1_len, "vdc")) {
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "NX-OS config: %.*s",
                     (int)tok1_len, tok1_start);
        return CMD_WRITE;
    }
    if (tok_eq_ci(tok1_start, tok1_len, "switchto")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            tok_eq_ci(tok2_start, tok2_len, "vdc")) {
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size, "switchto vdc");
            return CMD_WRITE;
        }
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
        return CMD_READ;

    /* ASA specific critical: no failover, failover active/reload-standby/
     * reset, no crypto map/access-group/nat/object-group/route/
     * tunnel-group (F11/F13 for ASA) */
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
            if (tok_eq_ci(tok2_start, tok2_len, "access-group") ||
                tok_eq_ci(tok2_start, tok2_len, "nat") ||
                tok_eq_ci(tok2_start, tok2_len, "object-group") ||
                tok_eq_ci(tok2_start, tok2_len, "route") ||
                tok_eq_ci(tok2_start, tok2_len, "tunnel-group")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "no %.*s: removes ASA config object",
                             (int)tok2_len, tok2_start);
                return CMD_CRITICAL;
            }
        }
        /* Fall through to IOS for other "no" commands (covers no crypto,
         * no username, no aaa via the shared IOS handler) */
    }

    if (tok_eq_ci(tok1_start, tok1_len, "failover")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len)) {
            if (tok_eq_ci(tok2_start, tok2_len, "active") ||
                tok_eq_ci(tok2_start, tok2_len, "reload-standby") ||
                tok_eq_ci(tok2_start, tok2_len, "reset")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "failover action");
                return CMD_CRITICAL;
            }
        }
    }

    /* boot system / boot config: next-boot image or config */
    if (tok_eq_ci(tok1_start, tok1_len, "boot")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            (tok_eq_ci(tok2_start, tok2_len, "system") ||
             tok_eq_ci(tok2_start, tok2_len, "config"))) {
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size, "boot %.*s: changes next-boot image/config",
                         (int)tok2_len, tok2_start);
            return CMD_CRITICAL;
        }
    }

    /* upgrade: standalone image upgrade */
    if (tok_eq_ci(tok1_start, tok1_len, "upgrade")) {
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "upgrade: image upgrade");
        return CMD_CRITICAL;
    }

    /* hw-module module ... reset */
    if (tok_eq_ci(tok1_start, tok1_len, "hw-module")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            tok_eq_ci(tok2_start, tok2_len, "module")) {
            const char *scan = p2;
            const char *ts;
            size_t tl;
            while (next_token(&scan, &ts, &tl)) {
                if (tok_eq_ci(ts, tl, "reset")) {
                    if (reason_buf && reason_buf_size > 0)
                        snprintf(reason_buf, reason_buf_size, "hw-module module reset");
                    return CMD_CRITICAL;
                }
            }
        }
    }

    /* crypto key zeroize: destroys the device's crypto keys -- must be
     * checked before the generic "crypto" -> WRITE catch-all below */
    if (tok_eq_ci(tok1_start, tok1_len, "crypto")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            tok_eq_ci(tok2_start, tok2_len, "key")) {
            const char *p3 = p2;
            if (next_token(&p3, &tok3_start, &tok3_len) &&
                tok_eq_ci(tok3_start, tok3_len, "zeroize")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "crypto key zeroize: destroys crypto keys");
                return CMD_CRITICAL;
            }
        }
    }

    /* clear configure <anything> (not just "all" -- F13); clear xlate;
     * clear conn */
    if (tok_eq_ci(tok1_start, tok1_len, "clear")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len)) {
            if (tok_eq_ci(tok2_start, tok2_len, "configure")) {
                const char *p3 = p2;
                if (next_token(&p3, &tok3_start, &tok3_len)) {
                    if (reason_buf && reason_buf_size > 0)
                        snprintf(reason_buf, reason_buf_size,
                                 "clear configure %.*s: wipes config",
                                 (int)tok3_len, tok3_start);
                    return CMD_CRITICAL;
                }
            }
            if (tok_eq_ci(tok2_start, tok2_len, "xlate") ||
                tok_eq_ci(tok2_start, tok2_len, "conn")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "clear %.*s: drops live NAT/connection state",
                             (int)tok2_len, tok2_start);
                return CMD_CRITICAL;
            }
            if (tok_eq_ci(tok2_start, tok2_len, "local-host") ||
                tok_eq_ci(tok2_start, tok2_len, "crashinfo")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "clear %.*s",
                             (int)tok2_len, tok2_start);
                return CMD_WRITE;
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
        tok_eq_ci(tok1_start, tok1_len, "nameif") ||
        /* spec 6 WRITE additions */
        tok_eq_ci(tok1_start, tok1_len, "shun") ||
        tok_eq_ci(tok1_start, tok1_len, "perfmon") ||
        tok_eq_ci(tok1_start, tok1_len, "capture") ||
        tok_eq_ci(tok1_start, tok1_len, "dhcpd") ||
        tok_eq_ci(tok1_start, tok1_len, "webvpn") ||
        tok_eq_ci(tok1_start, tok1_len, "ssl")) {
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
    const char *tok1_start, *tok2_start, *tok3_start, *tok4_start;
    size_t tok1_len, tok2_len, tok3_len, tok4_len;

    (void)seg_len;

    if (!next_token(&p, &tok1_start, &tok1_len))
        return CMD_READ;

    /* enable secret / enable password: sets the privileged-mode secret --
     * a config change, not the mode change a bare "enable" is. Checked
     * before the bare "enable" -> READ rule just below. */
    if (tok_eq_ci(tok1_start, tok1_len, "enable")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            (tok_eq_ci(tok2_start, tok2_len, "secret") ||
             tok_eq_ci(tok2_start, tok2_len, "password"))) {
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size,
                         "enable %.*s: sets the privileged-mode secret",
                         (int)tok2_len, tok2_start);
            return CMD_WRITE;
        }
    }

    /* --- Safe commands --- */
    if (tok_prefix_ci(tok1_start, tok1_len, "show"))
        return CMD_READ;
    if (tok_eq_ci(tok1_start, tok1_len, "ping") ||
        tok_eq_ci(tok1_start, tok1_len, "traceroute") ||
        tok_eq_ci(tok1_start, tok1_len, "enable") ||
        tok_eq_ci(tok1_start, tok1_len, "disable") ||
        tok_eq_ci(tok1_start, tok1_len, "exit") ||
        tok_eq_ci(tok1_start, tok1_len, "end") ||
        tok_eq_ci(tok1_start, tok1_len, "dir") ||
        /* spec 9 SAFE additions */
        tok_eq_ci(tok1_start, tok1_len, "diff") ||
        tok_eq_ci(tok1_start, tok1_len, "less") ||
        tok_eq_ci(tok1_start, tok1_len, "top"))
        return CMD_READ;

    /* --- Critical: standalone --- */
    if (tok_eq_ci(tok1_start, tok1_len, "reload") ||
        tok_eq_ci(tok1_start, tok1_len, "reboot") ||
        tok_eq_ci(tok1_start, tok1_len, "start-shell") ||
        tok_eq_ci(tok1_start, tok1_len, "shutdown")) {
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "%.*s: reboot / shell / interface down",
                     (int)tok1_len, tok1_start);
        return CMD_CRITICAL;
    }
    /* boot / boot system / boot set-default: on AOS-CX "boot" itself is
     * the reboot command (F14), not just "boot set-default" */
    if (tok_eq_ci(tok1_start, tok1_len, "boot")) {
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "boot: reboots the switch");
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
            /* checkpoint rollback (critical) / diff (safe) /
             * auto / post-configuration (write) */
            if (tok_eq_ci(tok1_start, tok1_len, "checkpoint")) {
                if (tok_eq_ci(tok2_start, tok2_len, "rollback")) {
                    if (reason_buf && reason_buf_size > 0)
                        snprintf(reason_buf, reason_buf_size, "checkpoint rollback");
                    return CMD_CRITICAL;
                }
                if (tok_eq_ci(tok2_start, tok2_len, "diff"))
                    return CMD_READ;
                if (tok_eq_ci(tok2_start, tok2_len, "auto") ||
                    tok_eq_ci(tok2_start, tok2_len, "post-configuration")) {
                    if (reason_buf && reason_buf_size > 0)
                        snprintf(reason_buf, reason_buf_size, "checkpoint %.*s",
                                 (int)tok2_len, tok2_start);
                    return CMD_WRITE;
                }
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
            /* vsx update-software */
            if (tok_eq_ci(tok1_start, tok1_len, "vsx") &&
                tok_eq_ci(tok2_start, tok2_len, "update-software")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "vsx update-software");
                return CMD_CRITICAL;
            }
            /* clear arp / clear lldp neighbors -> write */
            if (tok_eq_ci(tok1_start, tok1_len, "clear") &&
                (tok_eq_ci(tok2_start, tok2_len, "arp") ||
                 tok_eq_ci(tok2_start, tok2_len, "lldp"))) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "clear %.*s",
                             (int)tok2_len, tok2_start);
                return CMD_WRITE;
            }
        }
    }

    /* clear ip route all */
    if (tok_eq_ci(tok1_start, tok1_len, "clear")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            tok_eq_ci(tok2_start, tok2_len, "ip")) {
            const char *p3 = p2;
            if (next_token(&p3, &tok3_start, &tok3_len) &&
                tok_eq_ci(tok3_start, tok3_len, "route")) {
                const char *p4 = p3;
                if (next_token(&p4, &tok4_start, &tok4_len) &&
                    tok_eq_ci(tok4_start, tok4_len, "all")) {
                    if (reason_buf && reason_buf_size > 0)
                        snprintf(reason_buf, reason_buf_size, "clear ip route all");
                    return CMD_CRITICAL;
                }
            }
        }
    }

    /* copy <src> running-config / copy checkpoint <x> running-config
     * (critical); copy running-config startup-config (write, F14-adjacent:
     * the destination decides -- scan for the *last* token). */
    if (tok_eq_ci(tok1_start, tok1_len, "copy")) {
        const char *scan = p;
        const char *ts, *last_start = NULL;
        size_t tl, last_len = 0;
        while (next_token(&scan, &ts, &tl)) {
            last_start = ts;
            last_len = tl;
        }
        if (last_start && tok_eq_ci(last_start, last_len, "running-config")) {
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size, "copy to running-config: replaces live config");
            return CMD_CRITICAL;
        }
        if (last_start && tok_eq_ci(last_start, last_len, "startup-config")) {
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size, "copy to startup-config: saves config");
            return CMD_WRITE;
        }
    }

    /* --- "no" prefix critical --- */
    if (tok_eq_ci(tok1_start, tok1_len, "no")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len)) {
            if (tok_eq_ci(tok2_start, tok2_len, "vsx") ||
                tok_eq_ci(tok2_start, tok2_len, "stacking") ||
                tok_eq_ci(tok2_start, tok2_len, "spanning-tree") ||
                tok_eq_ci(tok2_start, tok2_len, "interface") ||
                tok_eq_ci(tok2_start, tok2_len, "vlan") ||
                tok_eq_ci(tok2_start, tok2_len, "vrf") ||
                tok_eq_ci(tok2_start, tok2_len, "user") ||
                tok_eq_ci(tok2_start, tok2_len, "aaa")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "no %.*s: critical config removal",
                             (int)tok2_len, tok2_start);
                return CMD_CRITICAL;
            }
            /* no router bgp/ospf/ospfv3 */
            if (tok_eq_ci(tok2_start, tok2_len, "router")) {
                const char *p3 = p2;
                if (next_token(&p3, &tok3_start, &tok3_len) &&
                    (tok_eq_ci(tok3_start, tok3_len, "bgp") ||
                     tok_eq_ci(tok3_start, tok3_len, "ospf") ||
                     tok_eq_ci(tok3_start, tok3_len, "ospfv3"))) {
                    if (reason_buf && reason_buf_size > 0)
                        snprintf(reason_buf, reason_buf_size, "no router %.*s: removes routing process",
                                 (int)tok3_len, tok3_start);
                    return CMD_CRITICAL;
                }
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
        tok_eq_ci(tok1_start, tok1_len, "mirror") ||
        /* spec 9 WRITE additions */
        tok_eq_ci(tok1_start, tok1_len, "banner") ||
        tok_eq_ci(tok1_start, tok1_len, "ssh") ||
        tok_eq_ci(tok1_start, tok1_len, "https-server") ||
        tok_eq_ci(tok1_start, tok1_len, "snmp-server") ||
        tok_eq_ci(tok1_start, tok1_len, "sflow") ||
        tok_eq_ci(tok1_start, tok1_len, "lag") ||
        tok_eq_ci(tok1_start, tok1_len, "lacp") ||
        tok_eq_ci(tok1_start, tok1_len, "spanning-tree") ||
        tok_eq_ci(tok1_start, tok1_len, "vrf") ||
        tok_eq_ci(tok1_start, tok1_len, "dhcp-server")) {
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "config command: %.*s",
                     (int)tok1_len, tok1_start);
        return CMD_WRITE;
    }

    /* No write/critical rule claimed this AOS-CX line: honestly CMD_UNKNOWN
     * rather than a guessed CMD_WRITE (spec section 3). */
    if (reason_buf && reason_buf_size > 0)
        snprintf(reason_buf, reason_buf_size, "unrecognised AOS-CX command: %.*s",
                 (int)tok1_len, tok1_start);
    return CMD_UNKNOWN;
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
        return CMD_READ;

    /* enable secret / enable password: sets the privileged-mode secret --
     * a config change, not the mode change a bare "enable" is. Checked
     * before the bare "enable" -> READ rule just below. */
    if (tok_eq_ci(tok1_start, tok1_len, "enable")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            (tok_eq_ci(tok2_start, tok2_len, "secret") ||
             tok_eq_ci(tok2_start, tok2_len, "password"))) {
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size,
                         "enable %.*s: sets the privileged-mode secret",
                         (int)tok2_len, tok2_start);
            return CMD_WRITE;
        }
    }

    /* --- Safe commands --- */
    if (tok_prefix_ci(tok1_start, tok1_len, "show"))
        return CMD_READ;
    if (tok_eq_ci(tok1_start, tok1_len, "ping") ||
        tok_eq_ci(tok1_start, tok1_len, "traceroute") ||
        tok_eq_ci(tok1_start, tok1_len, "enable") ||
        tok_eq_ci(tok1_start, tok1_len, "disable") ||
        tok_eq_ci(tok1_start, tok1_len, "exit") ||
        tok_eq_ci(tok1_start, tok1_len, "end"))
        return CMD_READ;

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
    /* spec 10 standalone CRITICAL additions */
    if (tok_eq_ci(tok1_start, tok1_len, "halt") ||
        tok_eq_ci(tok1_start, tok1_len, "disable-ap") ||
        tok_eq_ci(tok1_start, tok1_len, "convert-aos-ap") ||
        tok_eq_ci(tok1_start, tok1_len, "shutdown")) {
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "%.*s: controller/interface outage",
                     (int)tok1_len, tok1_start);
        return CMD_CRITICAL;
    }
    /* crypto-local ... zeroize */
    if (tok_eq_ci(tok1_start, tok1_len, "crypto-local")) {
        const char *scan = p;
        const char *ts;
        size_t tl;
        while (next_token(&scan, &ts, &tl)) {
            if (tok_eq_ci(ts, tl, "zeroize")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "crypto-local zeroize: destroys crypto keys");
                return CMD_CRITICAL;
            }
        }
    }
    /* copy <src> system: partition (image install) */
    if (tok_eq_ci(tok1_start, tok1_len, "copy")) {
        const char *scan = p;
        const char *ts;
        size_t tl;
        while (next_token(&scan, &ts, &tl)) {
            if (tok_prefix_ci(ts, tl, "system:")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "copy to system: partition image install");
                return CMD_CRITICAL;
            }
        }
    }

    /* --- Two-token critical --- */
    {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len)) {
            /* write erase (also covers "write erase all") */
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
            /* clear ap / clear gap-db */
            if (tok_eq_ci(tok1_start, tok1_len, "clear") &&
                (tok_eq_ci(tok2_start, tok2_len, "ap") ||
                 tok_eq_ci(tok2_start, tok2_len, "gap-db"))) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "clear %.*s",
                             (int)tok2_len, tok2_start);
                return CMD_CRITICAL;
            }
            /* clear datapath session */
            if (tok_eq_ci(tok1_start, tok1_len, "clear") &&
                tok_eq_ci(tok2_start, tok2_len, "datapath")) {
                const char *p3 = p2;
                if (next_token(&p3, &tok3_start, &tok3_len) &&
                    tok_eq_ci(tok3_start, tok3_len, "session")) {
                    if (reason_buf && reason_buf_size > 0)
                        snprintf(reason_buf, reason_buf_size, "clear datapath session");
                    return CMD_CRITICAL;
                }
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
            /* local-userdb del (critical) / add (write) */
            if (tok_eq_ci(tok1_start, tok1_len, "local-userdb")) {
                if (tok_eq_ci(tok2_start, tok2_len, "del")) {
                    if (reason_buf && reason_buf_size > 0)
                        snprintf(reason_buf, reason_buf_size, "local-userdb del");
                    return CMD_CRITICAL;
                }
                if (tok_eq_ci(tok2_start, tok2_len, "add")) {
                    if (reason_buf && reason_buf_size > 0)
                        snprintf(reason_buf, reason_buf_size, "local-userdb add");
                    return CMD_WRITE;
                }
            }
            /* license del (critical) / add (write) */
            if (tok_eq_ci(tok1_start, tok1_len, "license")) {
                if (tok_eq_ci(tok2_start, tok2_len, "del")) {
                    if (reason_buf && reason_buf_size > 0)
                        snprintf(reason_buf, reason_buf_size, "license del");
                    return CMD_CRITICAL;
                }
                if (tok_eq_ci(tok2_start, tok2_len, "add")) {
                    if (reason_buf && reason_buf_size > 0)
                        snprintf(reason_buf, reason_buf_size, "license add");
                    return CMD_WRITE;
                }
            }
            /* database synchronize -> write */
            if (tok_eq_ci(tok1_start, tok1_len, "database") &&
                tok_eq_ci(tok2_start, tok2_len, "synchronize")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "database synchronize");
                return CMD_WRITE;
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

    /* no vrrp / no aaa profile / no wlan ssid-profile / no user-role */
    if (tok_eq_ci(tok1_start, tok1_len, "no")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len)) {
            if (tok_eq_ci(tok2_start, tok2_len, "vrrp")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "no vrrp: removes HA");
                return CMD_CRITICAL;
            }
            if (tok_eq_ci(tok2_start, tok2_len, "user-role")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "no user-role: removes role");
                return CMD_CRITICAL;
            }
            if (tok_eq_ci(tok2_start, tok2_len, "aaa")) {
                const char *p3 = p2;
                if (next_token(&p3, &tok3_start, &tok3_len) &&
                    tok_eq_ci(tok3_start, tok3_len, "profile")) {
                    if (reason_buf && reason_buf_size > 0)
                        snprintf(reason_buf, reason_buf_size, "no aaa profile: removes AAA profile");
                    return CMD_CRITICAL;
                }
            }
            if (tok_eq_ci(tok2_start, tok2_len, "wlan")) {
                const char *p3 = p2;
                if (next_token(&p3, &tok3_start, &tok3_len) &&
                    tok_eq_ci(tok3_start, tok3_len, "ssid-profile")) {
                    if (reason_buf && reason_buf_size > 0)
                        snprintf(reason_buf, reason_buf_size, "no wlan ssid-profile: removes SSID");
                    return CMD_CRITICAL;
                }
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
        tok_eq_ci(tok1_start, tok1_len, "restore") ||
        /* spec 10 WRITE additions */
        tok_eq_ci(tok1_start, tok1_len, "ap-rename") ||
        tok_eq_ci(tok1_start, tok1_len, "ap-regroup") ||
        tok_eq_ci(tok1_start, tok1_len, "provision-ap") ||
        tok_eq_ci(tok1_start, tok1_len, "database") ||
        tok_eq_ci(tok1_start, tok1_len, "upgrade-profile") ||
        tok_eq_ci(tok1_start, tok1_len, "master-redundancy") ||
        tok_eq_ci(tok1_start, tok1_len, "papi-security") ||
        tok_eq_ci(tok1_start, tok1_len, "firewall") ||
        tok_eq_ci(tok1_start, tok1_len, "ids") ||
        tok_eq_ci(tok1_start, tok1_len, "wids")) {
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "config command: %.*s",
                     (int)tok1_len, tok1_start);
        return CMD_WRITE;
    }

    /* No write/critical rule claimed this ArubaOS line: honestly
     * CMD_UNKNOWN rather than a guessed CMD_WRITE (spec section 3). */
    if (reason_buf && reason_buf_size > 0)
        snprintf(reason_buf, reason_buf_size, "unrecognised ArubaOS command: %.*s",
                 (int)tok1_len, tok1_start);
    return CMD_UNKNOWN;
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
        return CMD_READ;

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
        tok_eq_ci(tok1_start, tok1_len, "up") ||
        /* spec 11 SAFE additions */
        tok_eq_ci(tok1_start, tok1_len, "check"))
        return CMD_READ;

    /* set cli: output-format preference only, not a config change (F15) --
     * must be checked before the generic "set" -> WRITE rule below */
    if (tok_eq_ci(tok1_start, tok1_len, "set")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len)) {
            if (tok_eq_ci(tok2_start, tok2_len, "cli"))
                return CMD_READ;
            /* set deviceconfig system ip-address/netmask/default-gateway:
             * can strand the management session (F15) */
            if (tok_eq_ci(tok2_start, tok2_len, "deviceconfig")) {
                const char *p3 = p2;
                if (next_token(&p3, &tok3_start, &tok3_len) &&
                    tok_eq_ci(tok3_start, tok3_len, "system")) {
                    const char *scan = p3;
                    const char *ts;
                    size_t tl;
                    while (next_token(&scan, &ts, &tl)) {
                        if (tok_eq_ci(ts, tl, "ip-address") ||
                            tok_eq_ci(ts, tl, "netmask") ||
                            tok_eq_ci(ts, tl, "default-gateway")) {
                            if (reason_buf && reason_buf_size > 0)
                                snprintf(reason_buf, reason_buf_size,
                                         "set deviceconfig system %.*s: can strand management session",
                                         (int)tl, ts);
                            return CMD_CRITICAL;
                        }
                    }
                }
            }
        }
    }

    /* tail follow -> safe */
    if (tok_eq_ci(tok1_start, tok1_len, "tail")) {
        return CMD_READ;
    }

    /* scp export -> safe, scp import -> write */
    if (tok_eq_ci(tok1_start, tok1_len, "scp")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len)) {
            if (tok_eq_ci(tok2_start, tok2_len, "export"))
                return CMD_READ;
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
                return CMD_READ;
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

    /* load config / load named-configuration -> critical */
    if (tok_eq_ci(tok1_start, tok1_len, "load")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            (tok_eq_ci(tok2_start, tok2_len, "config") ||
             tok_eq_ci(tok2_start, tok2_len, "named-configuration"))) {
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size, "load %.*s: replaces running config",
                         (int)tok2_len, tok2_start);
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
                        return CMD_READ;
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
            /* request high-availability / request ha state suspend */
            if (tok_prefix_ci(tok2_start, tok2_len, "high-availability") ||
                tok_eq_ci(tok2_start, tok2_len, "ha")) {
                const char *p3 = p2;
                if (next_token(&p3, &tok3_start, &tok3_len)) {
                    if (tok_eq_ci(tok3_start, tok3_len, "state") ||
                        tok_prefix_ci(tok3_start, tok3_len, "sync-to-remote")) {
                        if (reason_buf && reason_buf_size > 0)
                            snprintf(reason_buf, reason_buf_size, "request %.*s state: HA state change",
                                     (int)tok2_len, tok2_start);
                        return CMD_CRITICAL;
                    }
                }
            }
            /* request plugins <x> install: plugin install restarts services */
            if (tok_eq_ci(tok2_start, tok2_len, "plugins")) {
                const char *scan = p2;
                const char *ts;
                size_t tl;
                while (next_token(&scan, &ts, &tl)) {
                    if (tok_eq_ci(ts, tl, "install")) {
                        if (reason_buf && reason_buf_size > 0)
                            snprintf(reason_buf, reason_buf_size,
                                     "request plugins install: restarts services");
                        return CMD_CRITICAL;
                    }
                }
            }
            /* request global-protect-gateway client-logout: drops user sessions */
            if (tok_prefix_ci(tok2_start, tok2_len, "global-protect-gateway")) {
                const char *p3 = p2;
                if (next_token(&p3, &tok3_start, &tok3_len) &&
                    tok_eq_ci(tok3_start, tok3_len, "client-logout")) {
                    if (reason_buf && reason_buf_size > 0)
                        snprintf(reason_buf, reason_buf_size,
                                 "request global-protect-gateway client-logout");
                    return CMD_CRITICAL;
                }
            }
            /* request vpn ipsec-sa clear / ike-sa clear: drops VPN sessions */
            if (tok_eq_ci(tok2_start, tok2_len, "vpn")) {
                const char *p3 = p2;
                if (next_token(&p3, &tok3_start, &tok3_len) &&
                    (tok_eq_ci(tok3_start, tok3_len, "ipsec-sa") ||
                     tok_eq_ci(tok3_start, tok3_len, "ike-sa"))) {
                    const char *scan = p3;
                    const char *ts;
                    size_t tl;
                    while (next_token(&scan, &ts, &tl)) {
                        if (tok_eq_ci(ts, tl, "clear")) {
                            if (reason_buf && reason_buf_size > 0)
                                snprintf(reason_buf, reason_buf_size,
                                         "request vpn %.*s clear: drops VPN sessions",
                                         (int)tok3_len, tok3_start);
                            return CMD_CRITICAL;
                        }
                    }
                }
            }
            /* request support info -> safe */
            if (tok_eq_ci(tok2_start, tok2_len, "support")) {
                const char *p3 = p2;
                if (next_token(&p3, &tok3_start, &tok3_len) &&
                    tok_eq_ci(tok3_start, tok3_len, "info"))
                    return CMD_READ;
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
            /* clear dos-block-table -> critical */
            if (tok_eq_ci(tok2_start, tok2_len, "dos-block-table")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "clear dos-block-table");
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

    /* --- debug critical / debug show (safe) --- */
    if (tok_eq_ci(tok1_start, tok1_len, "debug")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len)) {
            if (tok_eq_ci(tok2_start, tok2_len, "show"))
                return CMD_READ;
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

    /* No write/critical rule claimed this PAN-OS line: honestly CMD_UNKNOWN
     * rather than a guessed CMD_WRITE (spec section 3). */
    if (reason_buf && reason_buf_size > 0)
        snprintf(reason_buf, reason_buf_size, "unrecognised PAN-OS command: %.*s",
                 (int)tok1_len, tok1_start);
    return CMD_UNKNOWN;
}

/* ----- Per-segment HP ProCurve / ProVision classification ----- */

static CmdSafetyLevel classify_hp_procurve_segment(const char *seg, size_t seg_len,
                                                     char *reason_buf, size_t reason_buf_size)
{
    const char *p = seg;
    const char *tok1_start, *tok2_start, *tok3_start;
    size_t tok1_len, tok2_len, tok3_len;

    (void)seg_len;

    if (!next_token(&p, &tok1_start, &tok1_len))
        return CMD_READ;

    /* --- Safe commands --- */
    if (tok_prefix_ci(tok1_start, tok1_len, "show") ||
        tok_eq_ci(tok1_start, tok1_len, "ping") ||
        tok_eq_ci(tok1_start, tok1_len, "traceroute") ||
        tok_eq_ci(tok1_start, tok1_len, "dir") ||
        tok_eq_ci(tok1_start, tok1_len, "menu") ||
        tok_eq_ci(tok1_start, tok1_len, "getmib") ||
        tok_eq_ci(tok1_start, tok1_len, "walkmib") ||
        tok_eq_ci(tok1_start, tok1_len, "exit") ||
        tok_eq_ci(tok1_start, tok1_len, "end") ||
        tok_eq_ci(tok1_start, tok1_len, "logout") ||
        tok_eq_ci(tok1_start, tok1_len, "page") ||
        tok_eq_ci(tok1_start, tok1_len, "terminal"))
        return CMD_READ;

    /* --- Critical standalone --- */
    if (tok_eq_ci(tok1_start, tok1_len, "reload") ||
        tok_eq_ci(tok1_start, tok1_len, "boot") ||
        tok_eq_ci(tok1_start, tok1_len, "factory-reset")) {
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "%.*s: reboot / image change / full wipe",
                     (int)tok1_len, tok1_start);
        return CMD_CRITICAL;
    }

    /* erase startup-config / erase all zeroize / erase flash.
     * ProCurve spells the flash target without a colon ("erase flash"), so the
     * device-filesystem token test does not apply -- and every erase form on
     * this platform wipes config or flash, so the bare verb is critical. */
    if (tok_eq_ci(tok1_start, tok1_len, "erase")) {
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "erase: config or flash wipe");
        return CMD_CRITICAL;
    }

    /* delete <file>: file removal on flash */
    if (tok_eq_ci(tok1_start, tok1_len, "delete")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len)) {
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size, "delete: file removal on flash");
            return CMD_CRITICAL;
        }
    }

    /* copy tftp|xmodem|usb <dst>: imports a config/image (critical);
     * copy <src> tftp: exports (write, spec WRITE list) */
    if (tok_eq_ci(tok1_start, tok1_len, "copy")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len)) {
            if (tok_eq_ci(tok2_start, tok2_len, "tftp") ||
                tok_eq_ci(tok2_start, tok2_len, "xmodem") ||
                tok_eq_ci(tok2_start, tok2_len, "usb")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size,
                             "copy %.*s: imports config/image", (int)tok2_len, tok2_start);
                return CMD_CRITICAL;
            }
        }
        const char *scan = p;
        const char *ts, *last_start = NULL;
        size_t tl, last_len = 0;
        while (next_token(&scan, &ts, &tl)) {
            last_start = ts;
            last_len = tl;
        }
        if (last_start && tok_eq_ci(last_start, last_len, "tftp")) {
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size, "copy to tftp: export");
            return CMD_WRITE;
        }
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "copy command");
        return CMD_WRITE;
    }

    /* password clear: removes the manager password */
    if (tok_eq_ci(tok1_start, tok1_len, "password")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            tok_eq_ci(tok2_start, tok2_len, "clear")) {
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size, "password clear: removes manager password");
            return CMD_CRITICAL;
        }
    }

    /* no vlan / interface / ip routing / spanning-tree / router /
     * password / aaa / snmp-server */
    if (tok_eq_ci(tok1_start, tok1_len, "no")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len)) {
            if (tok_eq_ci(tok2_start, tok2_len, "vlan") ||
                tok_eq_ci(tok2_start, tok2_len, "interface") ||
                tok_eq_ci(tok2_start, tok2_len, "spanning-tree") ||
                tok_eq_ci(tok2_start, tok2_len, "router") ||
                tok_eq_ci(tok2_start, tok2_len, "password") ||
                tok_eq_ci(tok2_start, tok2_len, "aaa") ||
                tok_eq_ci(tok2_start, tok2_len, "snmp-server")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "no %.*s: traffic drop / lock-out",
                             (int)tok2_len, tok2_start);
                return CMD_CRITICAL;
            }
            if (tok_eq_ci(tok2_start, tok2_len, "ip")) {
                const char *p3 = p2;
                if (next_token(&p3, &tok3_start, &tok3_len) &&
                    tok_eq_ci(tok3_start, tok3_len, "routing")) {
                    if (reason_buf && reason_buf_size > 0)
                        snprintf(reason_buf, reason_buf_size, "no ip routing: traffic drop");
                    return CMD_CRITICAL;
                }
            }
        }
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "no (negation): modifies config");
        return CMD_WRITE;
    }

    /* interface <x> disable: verb-anywhere scan (port down) */
    if (tok_eq_ci(tok1_start, tok1_len, "interface")) {
        if (seg_has_token_ci(p, (size_t)((seg + seg_len) - p), "disable")) {
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size, "interface disable: port down");
            return CMD_CRITICAL;
        }
    }

    /* --- Write commands --- */
    if (tok_eq_ci(tok1_start, tok1_len, "configure") ||
        tok_eq_ci(tok1_start, tok1_len, "write") ||
        tok_eq_ci(tok1_start, tok1_len, "vlan") ||
        tok_eq_ci(tok1_start, tok1_len, "interface") ||
        tok_eq_ci(tok1_start, tok1_len, "trunk") ||
        tok_eq_ci(tok1_start, tok1_len, "ip") ||
        tok_eq_ci(tok1_start, tok1_len, "hostname") ||
        tok_eq_ci(tok1_start, tok1_len, "snmp-server") ||
        tok_eq_ci(tok1_start, tok1_len, "setmib") ||
        tok_eq_ci(tok1_start, tok1_len, "aaa") ||
        tok_eq_ci(tok1_start, tok1_len, "radius-server") ||
        tok_eq_ci(tok1_start, tok1_len, "tacacs-server") ||
        tok_eq_ci(tok1_start, tok1_len, "spanning-tree") ||
        tok_eq_ci(tok1_start, tok1_len, "password") ||
        tok_eq_ci(tok1_start, tok1_len, "time") ||
        tok_eq_ci(tok1_start, tok1_len, "sntp") ||
        tok_eq_ci(tok1_start, tok1_len, "logging") ||
        tok_eq_ci(tok1_start, tok1_len, "qos") ||
        tok_eq_ci(tok1_start, tok1_len, "lldp") ||
        tok_eq_ci(tok1_start, tok1_len, "stack") ||
        tok_eq_ci(tok1_start, tok1_len, "kill")) {
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "config command: %.*s",
                     (int)tok1_len, tok1_start);
        return CMD_WRITE;
    }

    /* No write/critical rule claimed this ProCurve line: honestly
     * CMD_UNKNOWN rather than a guessed CMD_WRITE (spec section 3). */
    if (reason_buf && reason_buf_size > 0)
        snprintf(reason_buf, reason_buf_size, "unrecognised ProCurve command: %.*s",
                 (int)tok1_len, tok1_start);
    return CMD_UNKNOWN;
}

/* ----- Per-segment HPE Comware 5/7 (H3C) classification ----- */

static CmdSafetyLevel classify_hp_comware_segment(const char *seg, size_t seg_len,
                                                    char *reason_buf, size_t reason_buf_size)
{
    const char *p = seg;
    const char *tok1_start, *tok2_start, *tok3_start;
    size_t tok1_len, tok2_len, tok3_len;

    (void)seg_len;

    if (!next_token(&p, &tok1_start, &tok1_len))
        return CMD_READ;

    /* --- Safe commands: display, ping, tracert, ... --- */
    if (tok_prefix_ci(tok1_start, tok1_len, "display") ||
        tok_eq_ci(tok1_start, tok1_len, "ping") ||
        tok_eq_ci(tok1_start, tok1_len, "tracert") ||
        tok_eq_ci(tok1_start, tok1_len, "dir") ||
        tok_eq_ci(tok1_start, tok1_len, "more") ||
        tok_eq_ci(tok1_start, tok1_len, "quit") ||
        tok_eq_ci(tok1_start, tok1_len, "return") ||
        tok_eq_ci(tok1_start, tok1_len, "terminal") ||
        tok_eq_ci(tok1_start, tok1_len, "screen-length"))
        return CMD_READ;

    /* --- Critical: reboot / schedule reboot --- */
    if (tok_eq_ci(tok1_start, tok1_len, "reboot")) {
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "reboot: device reboot");
        return CMD_CRITICAL;
    }
    if (tok_eq_ci(tok1_start, tok1_len, "schedule")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            tok_eq_ci(tok2_start, tok2_len, "reboot")) {
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size, "schedule reboot: delayed device reboot");
            return CMD_CRITICAL;
        }
    }
    /* reset saved-configuration */
    if (tok_eq_ci(tok1_start, tok1_len, "reset")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len)) {
            if (tok_eq_ci(tok2_start, tok2_len, "saved-configuration")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "reset saved-configuration: erases startup config");
                return CMD_CRITICAL;
            }
            /* reset bgp / ospf / ip routing-table / session -> critical
             * (adjacency / session reset) */
            if (tok_eq_ci(tok2_start, tok2_len, "bgp") ||
                tok_eq_ci(tok2_start, tok2_len, "ospf") ||
                tok_eq_ci(tok2_start, tok2_len, "session")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "reset %.*s: resets adjacency/session",
                             (int)tok2_len, tok2_start);
                return CMD_CRITICAL;
            }
            if (tok_eq_ci(tok2_start, tok2_len, "ip")) {
                const char *p3 = p2;
                if (next_token(&p3, &tok3_start, &tok3_len) &&
                    tok_prefix_ci(tok3_start, tok3_len, "routing-table")) {
                    if (reason_buf && reason_buf_size > 0)
                        snprintf(reason_buf, reason_buf_size, "reset ip routing-table");
                    return CMD_CRITICAL;
                }
            }
            /* reset counters / reset logbuffer -> write (spec WRITE list) */
            if (tok_eq_ci(tok2_start, tok2_len, "counters") ||
                tok_eq_ci(tok2_start, tok2_len, "logbuffer")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "reset %.*s",
                             (int)tok2_len, tok2_start);
                return CMD_WRITE;
            }
        }
    }
    /* restore factory-default */
    if (tok_eq_ci(tok1_start, tok1_len, "restore")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            tok_prefix_ci(tok2_start, tok2_len, "factory")) {
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size, "restore factory-default: full wipe");
            return CMD_CRITICAL;
        }
    }
    /* delete with /unreserved or a device-fs token; format */
    if (tok_eq_ci(tok1_start, tok1_len, "delete")) {
        const char *scan = p;
        const char *ts;
        size_t tl;
        while (next_token(&scan, &ts, &tl)) {
            if (tok_eq_ci(ts, tl, "/unreserved") || is_device_fs_token(ts, tl)) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "delete: permanent file delete");
                return CMD_CRITICAL;
            }
        }
    }
    if (tok_eq_ci(tok1_start, tok1_len, "format")) {
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "format: filesystem format");
        return CMD_CRITICAL;
    }
    /* boot-loader file; startup saved-configuration */
    if (tok_eq_ci(tok1_start, tok1_len, "boot-loader")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            tok_eq_ci(tok2_start, tok2_len, "file")) {
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size, "boot-loader file: next-boot image");
            return CMD_CRITICAL;
        }
    }
    if (tok_eq_ci(tok1_start, tok1_len, "startup")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            tok_eq_ci(tok2_start, tok2_len, "saved-configuration")) {
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size, "startup saved-configuration: next-boot config");
            return CMD_CRITICAL;
        }
    }
    /* patch install; install activate/commit */
    if (tok_eq_ci(tok1_start, tok1_len, "patch")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            tok_eq_ci(tok2_start, tok2_len, "install")) {
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size, "patch install: image install");
            return CMD_CRITICAL;
        }
    }
    if (tok_eq_ci(tok1_start, tok1_len, "install")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            (tok_eq_ci(tok2_start, tok2_len, "activate") ||
             tok_eq_ci(tok2_start, tok2_len, "commit"))) {
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size, "install %.*s: image install",
                         (int)tok2_len, tok2_start);
            return CMD_CRITICAL;
        }
    }
    /* irf member <n> renumber */
    if (tok_eq_ci(tok1_start, tok1_len, "irf")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            tok_eq_ci(tok2_start, tok2_len, "member")) {
            if (seg_has_token_ci(p2, (size_t)((seg + seg_len) - p2), "renumber")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "irf member renumber: reboots the member");
                return CMD_CRITICAL;
            }
        }
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "irf-port: config command");
        return CMD_WRITE;
    }
    /* shutdown: interface down */
    if (tok_eq_ci(tok1_start, tok1_len, "shutdown")) {
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "shutdown: interface down");
        return CMD_CRITICAL;
    }
    /* _cmdline-mode / xtd-cli-mode: escapes to the hidden shell */
    if (tok_eq_ci(tok1_start, tok1_len, "_cmdline-mode") ||
        tok_eq_ci(tok1_start, tok1_len, "xtd-cli-mode")) {
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "%.*s: escapes to the hidden shell",
                     (int)tok1_len, tok1_start);
        return CMD_CRITICAL;
    }

    /* --- undo: negation prefix --- */
    if (tok_eq_ci(tok1_start, tok1_len, "undo")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len)) {
            if (tok_eq_ci(tok2_start, tok2_len, "interface") ||
                tok_eq_ci(tok2_start, tok2_len, "vlan") ||
                tok_eq_ci(tok2_start, tok2_len, "local-user") ||
                tok_eq_ci(tok2_start, tok2_len, "ospf") ||
                tok_eq_ci(tok2_start, tok2_len, "bgp") ||
                tok_eq_ci(tok2_start, tok2_len, "irf") ||
                tok_eq_ci(tok2_start, tok2_len, "stp") ||
                tok_eq_ci(tok2_start, tok2_len, "acl") ||
                tok_eq_ci(tok2_start, tok2_len, "security-policy") ||
                tok_eq_ci(tok2_start, tok2_len, "nat")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "undo %.*s: removes config block",
                             (int)tok2_len, tok2_start);
                return CMD_CRITICAL;
            }
            if (tok_eq_ci(tok2_start, tok2_len, "ip")) {
                const char *p3 = p2;
                if (next_token(&p3, &tok3_start, &tok3_len) &&
                    tok_prefix_ci(tok3_start, tok3_len, "route-static")) {
                    if (reason_buf && reason_buf_size > 0)
                        snprintf(reason_buf, reason_buf_size, "undo ip route-static: removes static route");
                    return CMD_CRITICAL;
                }
            }
        }
        /* undo <anything not listed above> -> write */
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "undo (negation): modifies config");
        return CMD_WRITE;
    }

    /* --- Write commands --- */
    if (tok_eq_ci(tok1_start, tok1_len, "system-view") ||
        tok_eq_ci(tok1_start, tok1_len, "save") ||
        tok_eq_ci(tok1_start, tok1_len, "interface") ||
        tok_eq_ci(tok1_start, tok1_len, "vlan") ||
        tok_eq_ci(tok1_start, tok1_len, "ip") ||
        tok_eq_ci(tok1_start, tok1_len, "acl") ||
        tok_eq_ci(tok1_start, tok1_len, "ospf") ||
        tok_eq_ci(tok1_start, tok1_len, "bgp") ||
        tok_eq_ci(tok1_start, tok1_len, "stp") ||
        tok_eq_ci(tok1_start, tok1_len, "sysname") ||
        tok_eq_ci(tok1_start, tok1_len, "local-user") ||
        tok_eq_ci(tok1_start, tok1_len, "nat") ||
        tok_eq_ci(tok1_start, tok1_len, "security-zone") ||
        tok_eq_ci(tok1_start, tok1_len, "security-policy") ||
        tok_eq_ci(tok1_start, tok1_len, "radius") ||
        tok_eq_ci(tok1_start, tok1_len, "hwtacacs") ||
        tok_eq_ci(tok1_start, tok1_len, "snmp-agent") ||
        tok_eq_ci(tok1_start, tok1_len, "info-center") ||
        tok_eq_ci(tok1_start, tok1_len, "clock") ||
        tok_eq_ci(tok1_start, tok1_len, "ntp-service") ||
        tok_eq_ci(tok1_start, tok1_len, "irf-port") ||
        tok_eq_ci(tok1_start, tok1_len, "qos") ||
        tok_eq_ci(tok1_start, tok1_len, "port") ||
        tok_eq_ci(tok1_start, tok1_len, "lldp") ||
        tok_eq_ci(tok1_start, tok1_len, "poe") ||
        tok_eq_ci(tok1_start, tok1_len, "debugging")) {
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "config command: %.*s",
                     (int)tok1_len, tok1_start);
        return CMD_WRITE;
    }

    /* No write/critical rule claimed this Comware line: honestly
     * CMD_UNKNOWN rather than a guessed CMD_WRITE (spec section 3). */
    if (reason_buf && reason_buf_size > 0)
        snprintf(reason_buf, reason_buf_size, "unrecognised Comware command: %.*s",
                 (int)tok1_len, tok1_start);
    return CMD_UNKNOWN;
}

/* ----- Per-segment Juniper Junos classification ----- */

static CmdSafetyLevel classify_junos_segment(const char *seg, size_t seg_len,
                                               char *reason_buf, size_t reason_buf_size)
{
    const char *p = seg;
    const char *tok1_start, *tok2_start, *tok3_start;
    size_t tok1_len, tok2_len, tok3_len;

    (void)seg_len;

    if (!next_token(&p, &tok1_start, &tok1_len))
        return CMD_READ;

    /* --- Safe commands --- */
    if (tok_prefix_ci(tok1_start, tok1_len, "show") ||
        tok_eq_ci(tok1_start, tok1_len, "ping") ||
        tok_eq_ci(tok1_start, tok1_len, "traceroute") ||
        tok_eq_ci(tok1_start, tok1_len, "help") ||
        tok_eq_ci(tok1_start, tok1_len, "exit") ||
        tok_eq_ci(tok1_start, tok1_len, "quit") ||
        tok_eq_ci(tok1_start, tok1_len, "top") ||
        tok_eq_ci(tok1_start, tok1_len, "up") ||
        tok_eq_ci(tok1_start, tok1_len, "compare"))
        return CMD_READ;

    /* run show / monitor traffic / monitor interface */
    if (tok_eq_ci(tok1_start, tok1_len, "run")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            tok_prefix_ci(tok2_start, tok2_len, "show"))
            return CMD_READ;
    }
    if (tok_eq_ci(tok1_start, tok1_len, "monitor")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            (tok_eq_ci(tok2_start, tok2_len, "traffic") ||
             tok_eq_ci(tok2_start, tok2_len, "interface")))
            return CMD_READ;
    }
    /* file show / file list / file compare (safe); file copy / file
     * archive (write); file delete (critical) */
    if (tok_eq_ci(tok1_start, tok1_len, "file")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len)) {
            if (tok_eq_ci(tok2_start, tok2_len, "show") ||
                tok_eq_ci(tok2_start, tok2_len, "list") ||
                tok_eq_ci(tok2_start, tok2_len, "compare"))
                return CMD_READ;
            if (tok_eq_ci(tok2_start, tok2_len, "delete")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "file delete: file removal");
                return CMD_CRITICAL;
            }
            if (tok_eq_ci(tok2_start, tok2_len, "copy") ||
                tok_eq_ci(tok2_start, tok2_len, "archive")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "file %.*s", (int)tok2_len, tok2_start);
                return CMD_WRITE;
            }
        }
    }
    /* request support information -> safe */
    if (tok_eq_ci(tok1_start, tok1_len, "request")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            tok_eq_ci(tok2_start, tok2_len, "support")) {
            const char *p3 = p2;
            if (next_token(&p3, &tok3_start, &tok3_len) &&
                tok_prefix_ci(tok3_start, tok3_len, "information"))
                return CMD_READ;
        }
        /* request system reboot / halt / power-off / zeroize /
         * software add / partition; request chassis cluster failover.
         * Rescan from the segment start (p2 above may already have
         * advanced past the first token). */
        const char *p2b = p;
        if (next_token(&p2b, &tok2_start, &tok2_len)) {
            if (tok_eq_ci(tok2_start, tok2_len, "system")) {
                const char *p3 = p2b;
                if (next_token(&p3, &tok3_start, &tok3_len) &&
                    (tok_eq_ci(tok3_start, tok3_len, "reboot") ||
                     tok_eq_ci(tok3_start, tok3_len, "halt") ||
                     tok_eq_ci(tok3_start, tok3_len, "power-off") ||
                     tok_eq_ci(tok3_start, tok3_len, "zeroize") ||
                     tok_prefix_ci(tok3_start, tok3_len, "software") ||
                     tok_eq_ci(tok3_start, tok3_len, "partition"))) {
                    if (reason_buf && reason_buf_size > 0)
                        snprintf(reason_buf, reason_buf_size, "request system %.*s",
                                 (int)tok3_len, tok3_start);
                    return CMD_CRITICAL;
                }
                if (tok_eq_ci(tok3_start, tok3_len, "snapshot") ||
                    tok_prefix_ci(tok3_start, tok3_len, "license")) {
                    if (reason_buf && reason_buf_size > 0)
                        snprintf(reason_buf, reason_buf_size, "request system %.*s",
                                 (int)tok3_len, tok3_start);
                    return CMD_WRITE;
                }
            }
            if (tok_eq_ci(tok2_start, tok2_len, "chassis")) {
                const char *p3 = p2b;
                if (next_token(&p3, &tok3_start, &tok3_len) &&
                    tok_eq_ci(tok3_start, tok3_len, "cluster")) {
                    if (seg_has_token_ci(p3, (size_t)((seg + seg_len) - p3), "failover")) {
                        if (reason_buf && reason_buf_size > 0)
                            snprintf(reason_buf, reason_buf_size,
                                     "request chassis cluster failover: HA failover");
                        return CMD_CRITICAL;
                    }
                }
            }
        }
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "request command");
        return CMD_WRITE;
    }
    /* restart routing / restart <daemon> */
    if (tok_eq_ci(tok1_start, tok1_len, "restart")) {
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "restart: daemon restart");
        return CMD_CRITICAL;
    }
    /* clear bgp neighbor / clear ospf neighbor / clear isis adjacency /
     * clear security flow session all / clear system commit; clear
     * interfaces statistics / clear log / clear arp (write, unlisted) */
    if (tok_eq_ci(tok1_start, tok1_len, "clear")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len)) {
            if (tok_eq_ci(tok2_start, tok2_len, "bgp") ||
                tok_eq_ci(tok2_start, tok2_len, "ospf") ||
                tok_eq_ci(tok2_start, tok2_len, "isis")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "clear %.*s: resets adjacency",
                             (int)tok2_len, tok2_start);
                return CMD_CRITICAL;
            }
            if (tok_eq_ci(tok2_start, tok2_len, "security")) {
                if (seg_has_token_ci(p2, (size_t)((seg + seg_len) - p2), "session")) {
                    if (reason_buf && reason_buf_size > 0)
                        snprintf(reason_buf, reason_buf_size, "clear security flow session: drops sessions");
                    return CMD_CRITICAL;
                }
            }
            if (tok_eq_ci(tok2_start, tok2_len, "system")) {
                const char *p3 = p2;
                if (next_token(&p3, &tok3_start, &tok3_len) &&
                    tok_eq_ci(tok3_start, tok3_len, "commit")) {
                    if (reason_buf && reason_buf_size > 0)
                        snprintf(reason_buf, reason_buf_size, "clear system commit");
                    return CMD_CRITICAL;
                }
            }
            if (tok_eq_ci(tok2_start, tok2_len, "interfaces") ||
                tok_eq_ci(tok2_start, tok2_len, "log") ||
                tok_eq_ci(tok2_start, tok2_len, "arp")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "clear %.*s",
                             (int)tok2_len, tok2_start);
                return CMD_WRITE;
            }
        }
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "clear command");
        return CMD_WRITE;
    }
    /* start shell: escapes to the FreeBSD shell */
    if (tok_eq_ci(tok1_start, tok1_len, "start")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            tok_eq_ci(tok2_start, tok2_len, "shell")) {
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size, "start shell: escapes to the FreeBSD shell");
            return CMD_CRITICAL;
        }
    }
    /* test policy -> safe */
    if (tok_eq_ci(tok1_start, tok1_len, "test")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            tok_eq_ci(tok2_start, tok2_len, "policy"))
            return CMD_READ;
    }
    /* commit: applies the candidate config (critical); commit check
     * stays safe; commit confirmed is the safer auto-reverting form
     * (write) */
    if (tok_eq_ci(tok1_start, tok1_len, "commit")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len)) {
            if (tok_eq_ci(tok2_start, tok2_len, "check"))
                return CMD_READ;
            if (tok_eq_ci(tok2_start, tok2_len, "confirmed")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "commit confirmed: auto-reverting apply");
                return CMD_WRITE;
            }
            /* commit and-quit / synchronize / force / at -> critical */
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size, "commit %.*s: applies candidate config",
                         (int)tok2_len, tok2_start);
            return CMD_CRITICAL;
        }
        /* bare commit -> critical */
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "commit: applies candidate config");
        return CMD_CRITICAL;
    }
    /* load override / replace / factory-default -> critical;
     * load merge / set / patch -> write */
    if (tok_eq_ci(tok1_start, tok1_len, "load")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len)) {
            if (tok_eq_ci(tok2_start, tok2_len, "override") ||
                tok_eq_ci(tok2_start, tok2_len, "replace") ||
                tok_prefix_ci(tok2_start, tok2_len, "factory-default")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "load %.*s: replaces the whole config",
                             (int)tok2_len, tok2_start);
                return CMD_CRITICAL;
            }
            if (tok_eq_ci(tok2_start, tok2_len, "merge") ||
                tok_eq_ci(tok2_start, tok2_len, "set") ||
                tok_eq_ci(tok2_start, tok2_len, "patch")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "load %.*s", (int)tok2_len, tok2_start);
                return CMD_WRITE;
            }
        }
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "load command");
        return CMD_WRITE;
    }
    /* delete (config mode -- removes a hierarchy) */
    if (tok_eq_ci(tok1_start, tok1_len, "delete")) {
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "delete: removes a config hierarchy");
        return CMD_CRITICAL;
    }

    /* --- Write commands --- */
    if (tok_eq_ci(tok1_start, tok1_len, "configure") ||
        tok_eq_ci(tok1_start, tok1_len, "edit") ||
        tok_eq_ci(tok1_start, tok1_len, "set") ||
        tok_eq_ci(tok1_start, tok1_len, "deactivate") ||
        tok_eq_ci(tok1_start, tok1_len, "activate") ||
        tok_eq_ci(tok1_start, tok1_len, "rename") ||
        tok_eq_ci(tok1_start, tok1_len, "copy") ||
        tok_eq_ci(tok1_start, tok1_len, "insert") ||
        tok_eq_ci(tok1_start, tok1_len, "annotate") ||
        tok_eq_ci(tok1_start, tok1_len, "replace") ||
        tok_eq_ci(tok1_start, tok1_len, "save") ||
        tok_eq_ci(tok1_start, tok1_len, "rollback") ||
        tok_eq_ci(tok1_start, tok1_len, "op")) {
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "config command: %.*s",
                     (int)tok1_len, tok1_start);
        return CMD_WRITE;
    }

    /* No write/critical rule claimed this Junos line: honestly CMD_UNKNOWN
     * rather than a guessed CMD_WRITE (spec section 3). */
    if (reason_buf && reason_buf_size > 0)
        snprintf(reason_buf, reason_buf_size, "unrecognised Junos command: %.*s",
                 (int)tok1_len, tok1_start);
    return CMD_UNKNOWN;
}

/* ----- Per-segment Fortinet FortiOS classification ----- */

static CmdSafetyLevel classify_fortios_segment(const char *seg, size_t seg_len,
                                                 char *reason_buf, size_t reason_buf_size)
{
    const char *p = seg;
    const char *tok1_start, *tok2_start, *tok3_start;
    size_t tok1_len, tok2_len, tok3_len;

    (void)seg_len;
    (void)tok3_start;
    (void)tok3_len;

    if (!next_token(&p, &tok1_start, &tok1_len))
        return CMD_READ;

    /* --- Safe commands --- */
    if (tok_eq_ci(tok1_start, tok1_len, "get") ||
        tok_eq_ci(tok1_start, tok1_len, "show") ||
        tok_eq_ci(tok1_start, tok1_len, "end") ||
        tok_eq_ci(tok1_start, tok1_len, "next") ||
        tok_eq_ci(tok1_start, tok1_len, "abort") ||
        tok_eq_ci(tok1_start, tok1_len, "exit"))
        return CMD_READ;

    /* execute: mostly safe (ping/traceroute/telnet/ssh/time) or write
     * (backup config / update-now / date <value> / ha manage / cli),
     * with several critical destructive subcommands */
    if (tok_eq_ci(tok1_start, tok1_len, "execute")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len)) {
            if (tok_eq_ci(tok2_start, tok2_len, "ping") ||
                tok_eq_ci(tok2_start, tok2_len, "traceroute") ||
                tok_eq_ci(tok2_start, tok2_len, "telnet") ||
                tok_eq_ci(tok2_start, tok2_len, "ssh") ||
                tok_eq_ci(tok2_start, tok2_len, "time"))
                return CMD_READ;
            if (tok_eq_ci(tok2_start, tok2_len, "factoryreset") ||
                tok_eq_ci(tok2_start, tok2_len, "factoryreset2") ||
                tok_eq_ci(tok2_start, tok2_len, "erase-disk") ||
                tok_eq_ci(tok2_start, tok2_len, "formatlogdisk") ||
                tok_eq_ci(tok2_start, tok2_len, "reboot") ||
                tok_eq_ci(tok2_start, tok2_len, "shutdown") ||
                tok_eq_ci(tok2_start, tok2_len, "disconnect-admin-session")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "execute %.*s",
                             (int)tok2_len, tok2_start);
                return CMD_CRITICAL;
            }
            if (tok_eq_ci(tok2_start, tok2_len, "restore")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size,
                             "execute restore: config/image replacement");
                return CMD_CRITICAL;
            }
            if (tok_eq_ci(tok2_start, tok2_len, "ha")) {
                const char *scan = p2;
                const char *ts;
                size_t tl;
                while (next_token(&scan, &ts, &tl)) {
                    if (tok_eq_ci(ts, tl, "failover") || tok_eq_ci(ts, tl, "synchronize")) {
                        if (reason_buf && reason_buf_size > 0)
                            snprintf(reason_buf, reason_buf_size, "execute ha: HA state change");
                        return CMD_CRITICAL;
                    }
                    if (tok_eq_ci(ts, tl, "manage")) {
                        if (reason_buf && reason_buf_size > 0)
                            snprintf(reason_buf, reason_buf_size, "execute ha manage");
                        return CMD_WRITE;
                    }
                }
            }
            if (tok_eq_ci(tok2_start, tok2_len, "log")) {
                if (seg_has_token_ci(p2, (size_t)((seg + seg_len) - p2), "delete-all")) {
                    if (reason_buf && reason_buf_size > 0)
                        snprintf(reason_buf, reason_buf_size, "execute log delete-all: log destruction");
                    return CMD_CRITICAL;
                }
            }
            if (tok_eq_ci(tok2_start, tok2_len, "router")) {
                if (seg_has_token_ci(p2, (size_t)((seg + seg_len) - p2), "clear")) {
                    if (reason_buf && reason_buf_size > 0)
                        snprintf(reason_buf, reason_buf_size,
                                 "execute router clear: resets routing adjacency");
                    return CMD_CRITICAL;
                }
            }
            if (tok_eq_ci(tok2_start, tok2_len, "backup") ||
                tok_eq_ci(tok2_start, tok2_len, "update-now") ||
                tok_eq_ci(tok2_start, tok2_len, "date") ||
                tok_eq_ci(tok2_start, tok2_len, "cli")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "execute %.*s",
                             (int)tok2_len, tok2_start);
                return CMD_WRITE;
            }
        }
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "execute command");
        return CMD_WRITE;
    }

    /* diagnose: mostly write, several critical/safe subcommands */
    if (tok_eq_ci(tok1_start, tok1_len, "diagnose")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len)) {
            if (tok_eq_ci(tok2_start, tok2_len, "sys")) {
                const char *scan = p2;
                const char *ts;
                size_t tl;
                while (next_token(&scan, &ts, &tl)) {
                    if (tok_eq_ci(ts, tl, "top")) return CMD_READ;
                    if (tok_eq_ci(ts, tl, "kill")) {
                        if (reason_buf && reason_buf_size > 0)
                            snprintf(reason_buf, reason_buf_size, "diagnose sys kill: process kill");
                        return CMD_CRITICAL;
                    }
                    if (tok_eq_ci(ts, tl, "flash")) {
                        if (seg_has_token_ci(scan, (size_t)((seg + seg_len) - scan), "format")) {
                            if (reason_buf && reason_buf_size > 0)
                                snprintf(reason_buf, reason_buf_size, "diagnose sys flash format");
                            return CMD_CRITICAL;
                        }
                    }
                }
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "diagnose sys");
                return CMD_WRITE;
            }
            if (tok_eq_ci(tok2_start, tok2_len, "debug"))
                return CMD_READ;
            if (tok_eq_ci(tok2_start, tok2_len, "sniffer")) {
                if (seg_has_token_ci(p2, (size_t)((seg + seg_len) - p2), "packet"))
                    return CMD_READ;
            }
            if (tok_eq_ci(tok2_start, tok2_len, "hardware")) {
                if (seg_has_token_ci(p2, (size_t)((seg + seg_len) - p2), "test")) {
                    if (reason_buf && reason_buf_size > 0)
                        snprintf(reason_buf, reason_buf_size, "diagnose hardware test");
                    return CMD_CRITICAL;
                }
            }
            if (tok_eq_ci(tok2_start, tok2_len, "vpn")) {
                if (seg_has_token_ci(p2, (size_t)((seg + seg_len) - p2), "down")) {
                    if (reason_buf && reason_buf_size > 0)
                        snprintf(reason_buf, reason_buf_size, "diagnose vpn tunnel down: VPN drop");
                    return CMD_CRITICAL;
                }
            }
        }
        /* diagnose (default) -> write */
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "diagnose command");
        return CMD_WRITE;
    }

    /* purge: deletes every entry in the current table */
    if (tok_eq_ci(tok1_start, tok1_len, "purge")) {
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "purge: deletes every entry in the table");
        return CMD_CRITICAL;
    }
    /* delete: removes an object inside a config table */
    if (tok_eq_ci(tok1_start, tok1_len, "delete")) {
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "delete: removes a config object");
        return CMD_CRITICAL;
    }

    /* --- Write commands --- */
    if (tok_eq_ci(tok1_start, tok1_len, "config") ||
        tok_eq_ci(tok1_start, tok1_len, "edit") ||
        tok_eq_ci(tok1_start, tok1_len, "set") ||
        tok_eq_ci(tok1_start, tok1_len, "unset") ||
        tok_eq_ci(tok1_start, tok1_len, "append") ||
        tok_eq_ci(tok1_start, tok1_len, "clone") ||
        tok_eq_ci(tok1_start, tok1_len, "move") ||
        tok_eq_ci(tok1_start, tok1_len, "rename")) {
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "config command: %.*s",
                     (int)tok1_len, tok1_start);
        return CMD_WRITE;
    }

    /* No write/critical rule claimed this FortiOS line: honestly
     * CMD_UNKNOWN rather than a guessed CMD_WRITE (spec section 3). */
    if (reason_buf && reason_buf_size > 0)
        snprintf(reason_buf, reason_buf_size, "unrecognised FortiOS command: %.*s",
                 (int)tok1_len, tok1_start);
    return CMD_UNKNOWN;
}

/* ----- Per-segment VyOS classification ----- */

static CmdSafetyLevel classify_vyos_segment(const char *seg, size_t seg_len,
                                              char *reason_buf, size_t reason_buf_size)
{
    const char *p = seg;
    const char *tok1_start, *tok2_start, *tok3_start;
    size_t tok1_len, tok2_len, tok3_len;

    (void)seg_len;
    (void)tok3_start;
    (void)tok3_len;

    if (!next_token(&p, &tok1_start, &tok1_len))
        return CMD_READ;

    /* --- Safe commands --- */
    if (tok_prefix_ci(tok1_start, tok1_len, "show") ||
        tok_eq_ci(tok1_start, tok1_len, "compare") ||
        tok_eq_ci(tok1_start, tok1_len, "ping") ||
        tok_eq_ci(tok1_start, tok1_len, "traceroute") ||
        tok_eq_ci(tok1_start, tok1_len, "monitor") ||
        tok_eq_ci(tok1_start, tok1_len, "exit") ||
        tok_eq_ci(tok1_start, tok1_len, "top") ||
        tok_eq_ci(tok1_start, tok1_len, "up") ||
        tok_eq_ci(tok1_start, tok1_len, "commit-check"))
        return CMD_READ;

    /* run show -> safe */
    if (tok_eq_ci(tok1_start, tok1_len, "run")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            tok_prefix_ci(tok2_start, tok2_len, "show"))
            return CMD_READ;
    }

    /* --- Critical standalone --- */
    if (tok_eq_ci(tok1_start, tok1_len, "commit") ||
        tok_eq_ci(tok1_start, tok1_len, "delete") ||
        tok_eq_ci(tok1_start, tok1_len, "load") ||
        tok_eq_ci(tok1_start, tok1_len, "merge") ||
        tok_eq_ci(tok1_start, tok1_len, "reboot") ||
        tok_eq_ci(tok1_start, tok1_len, "poweroff") ||
        tok_eq_ci(tok1_start, tok1_len, "shutdown")) {
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "%.*s: applies/replaces config or halts the system",
                     (int)tok1_len, tok1_start);
        return CMD_CRITICAL;
    }

    /* reset vpn ipsec-peer / reset ip bgp / reset ospf process */
    if (tok_eq_ci(tok1_start, tok1_len, "reset")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len)) {
            if (tok_eq_ci(tok2_start, tok2_len, "vpn") ||
                tok_eq_ci(tok2_start, tok2_len, "ip") ||
                tok_eq_ci(tok2_start, tok2_len, "ospf")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "reset %.*s: resets a session/process",
                             (int)tok2_len, tok2_start);
                return CMD_CRITICAL;
            }
        }
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "reset command");
        return CMD_CRITICAL;
    }

    /* delete system image / set system image default-boot */
    if (tok_eq_ci(tok1_start, tok1_len, "set")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            tok_eq_ci(tok2_start, tok2_len, "system")) {
            if (seg_has_token_ci(p2, (size_t)((seg + seg_len) - p2), "default-boot")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "set system image default-boot");
                return CMD_CRITICAL;
            }
        }
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "set: config command");
        return CMD_WRITE;
    }

    /* --- Write commands --- */
    if (tok_eq_ci(tok1_start, tok1_len, "configure") ||
        tok_eq_ci(tok1_start, tok1_len, "save") ||
        tok_eq_ci(tok1_start, tok1_len, "discard") ||
        tok_eq_ci(tok1_start, tok1_len, "comment") ||
        tok_eq_ci(tok1_start, tok1_len, "rename") ||
        tok_eq_ci(tok1_start, tok1_len, "copy") ||
        tok_eq_ci(tok1_start, tok1_len, "commit-confirm") ||
        tok_eq_ci(tok1_start, tok1_len, "generate")) {
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "config command: %.*s",
                     (int)tok1_len, tok1_start);
        return CMD_WRITE;
    }
    if (tok_eq_ci(tok1_start, tok1_len, "add")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            tok_eq_ci(tok2_start, tok2_len, "system")) {
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size, "add system image");
            return CMD_WRITE;
        }
    }
    if (tok_eq_ci(tok1_start, tok1_len, "clear")) {
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "clear interfaces counters");
        return CMD_WRITE;
    }

    /* No write/critical rule claimed this VyOS line: honestly CMD_UNKNOWN
     * rather than a guessed CMD_WRITE (spec section 3). */
    if (reason_buf && reason_buf_size > 0)
        snprintf(reason_buf, reason_buf_size, "unrecognised VyOS command: %.*s",
                 (int)tok1_len, tok1_start);
    return CMD_UNKNOWN;
}

/* ----- Per-segment MikroTik RouterOS classification -----
 * RouterOS puts the verb last ("/ip firewall filter remove numbers=0"),
 * so this classifier scans every token (helper M4, seg_has_token_ci)
 * rather than only the first. Order matters: CRITICAL first, then
 * WRITE, then SAFE (spec section 15). */

static CmdSafetyLevel classify_mikrotik_segment(const char *seg, size_t seg_len,
                                                  char *reason_buf, size_t reason_buf_size)
{
    static const char *critical_verbs[] = {
        "remove", "reset-configuration", "reset", "reboot", "shutdown",
        "disable", "downgrade", "upgrade", "format-drive", NULL
    };
    static const char *write_verbs[] = {
        "set", "add", "edit", "comment", "move", "enable", "upload",
        "fetch", "import", NULL
    };
    static const char *read_verbs[] = {
        "print", "get", "find", "export", "monitor", "monitor-traffic",
        "info", NULL
    };

    if (seg_len == 0)
        return CMD_READ;

    for (int i = 0; critical_verbs[i]; i++) {
        if (seg_has_token_ci(seg, seg_len, critical_verbs[i])) {
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size, "%s: destructive RouterOS verb",
                         critical_verbs[i]);
            return CMD_CRITICAL;
        }
    }
    if (seg_has_token_ci(seg, seg_len, "backup") &&
        seg_has_token_ci(seg, seg_len, "load")) {
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "backup load: restores a backup");
        return CMD_CRITICAL;
    }
    if (seg_has_token_ci(seg, seg_len, "routerboard") &&
        seg_has_token_ci(seg, seg_len, "upgrade")) {
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "routerboard upgrade");
        return CMD_CRITICAL;
    }

    for (int i = 0; write_verbs[i]; i++) {
        if (seg_has_token_ci(seg, seg_len, write_verbs[i])) {
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size, "%s: RouterOS write verb",
                         write_verbs[i]);
            return CMD_WRITE;
        }
    }
    if (seg_has_token_ci(seg, seg_len, "backup") &&
        seg_has_token_ci(seg, seg_len, "save")) {
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "backup save");
        return CMD_WRITE;
    }

    for (int i = 0; read_verbs[i]; i++) {
        if (seg_has_token_ci(seg, seg_len, read_verbs[i]))
            return CMD_READ;
    }

    /* No critical/write/safe verb found anywhere in the segment: honestly
     * CMD_UNKNOWN rather than a guessed CMD_WRITE, as with the other
     * network platforms (spec section 3). */
    if (reason_buf && reason_buf_size > 0)
        snprintf(reason_buf, reason_buf_size, "unrecognised RouterOS command");
    return CMD_UNKNOWN;
}

/* ----- Per-segment CMD_PLATFORM_UNKNOWN classification (spec section 2) -----
 * A session whose platform has not been chosen and whose banner has not
 * resolved it yet. The overlay below claims a short list of first-token
 * verbs that are destructive on some network CLI and are not commands on
 * any Linux system, so claiming them costs a Linux session nothing; a bare
 * "commit" or "delete" typed at a Linux shell is a command-not-found. Three
 * verbs need a second token to disambiguate ("write erase", "request
 * system/restart/shutdown", "execute factoryreset/reboot/restore"); the
 * rest are unconditional on the first token. Anything the overlay does not
 * claim delegates to classify_linux_segment() unchanged, so Linux behaviour
 * is preserved exactly for everything but this list. */

static const char *unknown_critical_verbs[] = {
    "reload", "reboot", "erase", "factory-reset", "restore",
    "factory-default", "commit", "rollback", "delete", "purge", "undo",
    "boot",
    NULL
};

static const char *unknown_write_verbs[] = {
    "no", "shutdown", "configure", "system-view", "set", "config",
    NULL
};

static CmdSafetyLevel classify_unknown_segment(const char *seg, size_t seg_len,
                                                char *reason_buf, size_t reason_buf_size)
{
    const char *p = seg;
    const char *tok1_start, *tok2_start;
    size_t tok1_len, tok2_len;

    if (!next_token(&p, &tok1_start, &tok1_len))
        return classify_linux_segment(seg, seg_len, reason_buf, reason_buf_size);

    /* "write erase" (IOS, ArubaOS) -- only the two-word form is claimed; a
     * bare "write" falls through to Linux. */
    if (tok_eq_ci(tok1_start, tok1_len, "write")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            tok_eq_ci(tok2_start, tok2_len, "erase")) {
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size,
                         "write erase: destructive on IOS/ArubaOS");
            return CMD_CRITICAL;
        }
        return classify_linux_segment(seg, seg_len, reason_buf, reason_buf_size);
    }

    /* "request system/restart/shutdown" (PAN-OS, Junos) */
    if (tok_eq_ci(tok1_start, tok1_len, "request")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            (tok_eq_ci(tok2_start, tok2_len, "system") ||
             tok_eq_ci(tok2_start, tok2_len, "restart") ||
             tok_eq_ci(tok2_start, tok2_len, "shutdown"))) {
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size,
                         "request %.*s: destructive on PAN-OS/Junos",
                         (int)tok2_len, tok2_start);
            return CMD_CRITICAL;
        }
        return classify_linux_segment(seg, seg_len, reason_buf, reason_buf_size);
    }

    /* "execute factoryreset/reboot/restore" (FortiOS) */
    if (tok_eq_ci(tok1_start, tok1_len, "execute")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            (tok_eq_ci(tok2_start, tok2_len, "factoryreset") ||
             tok_eq_ci(tok2_start, tok2_len, "reboot") ||
             tok_eq_ci(tok2_start, tok2_len, "restore"))) {
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size,
                         "execute %.*s: destructive on FortiOS",
                         (int)tok2_len, tok2_start);
            return CMD_CRITICAL;
        }
        return classify_linux_segment(seg, seg_len, reason_buf, reason_buf_size);
    }

    /* "reset saved-configuration" (Comware), "reset bgp|ospf|isis|session"
     * (Comware adjacency resets). A bare "reset" on Linux is the harmless
     * terminfo terminal reset, so only the two-token network forms are
     * claimed here. */
    if (tok_eq_ci(tok1_start, tok1_len, "reset")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            (tok_prefix_ci(tok2_start, tok2_len, "saved-config") ||
             tok_eq_ci(tok2_start, tok2_len, "configuration") ||
             tok_eq_ci(tok2_start, tok2_len, "bgp") ||
             tok_eq_ci(tok2_start, tok2_len, "ospf") ||
             tok_eq_ci(tok2_start, tok2_len, "isis") ||
             tok_eq_ci(tok2_start, tok2_len, "session"))) {
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size,
                         "reset %.*s: destructive on Comware",
                         (int)tok2_len, tok2_start);
            return CMD_CRITICAL;
        }
        return classify_linux_segment(seg, seg_len, reason_buf, reason_buf_size);
    }

    /* RouterOS puts its verb last behind a "/path" head ("/system
     * reset-configuration", "/ip firewall filter remove numbers=0"), so the
     * first token alone never identifies it. A Linux absolute-path
     * invocation ("/etc/init.d/nginx stop") shares none of these verbs. */
    if (tok1_len > 0 && tok1_start[0] == '/') {
        static const char *ros_verbs[] = {
            "reset-configuration", "remove", "reboot", "shutdown",
            "format-drive", "downgrade", "disable", NULL
        };
        for (int i = 0; ros_verbs[i]; i++) {
            if (seg_has_token_ci(seg, seg_len, ros_verbs[i])) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size,
                             "%s: destructive RouterOS verb", ros_verbs[i]);
                return CMD_CRITICAL;
            }
        }
    }

    for (int i = 0; unknown_critical_verbs[i]; i++) {
        if (tok_eq_ci(tok1_start, tok1_len, unknown_critical_verbs[i])) {
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size,
                         "%s: destructive on some network CLI",
                         unknown_critical_verbs[i]);
            return CMD_CRITICAL;
        }
    }

    for (int i = 0; unknown_write_verbs[i]; i++) {
        if (tok_eq_ci(tok1_start, tok1_len, unknown_write_verbs[i])) {
            /* Never downgrade. Some of these verbs are worse than WRITE on a
             * real Linux host -- "shutdown" is CRITICAL there -- and an
             * unresolved session must never classify below what the Linux
             * ruleset alone would have said. The overlay may only raise. */
            char linux_reason[128];
            CmdSafetyLevel linux_level;
            linux_reason[0] = 0;
            linux_level = classify_linux_segment(seg, seg_len, linux_reason,
                                                 sizeof linux_reason);
            if (linux_level > CMD_WRITE) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "%s", linux_reason);
                return linux_level;
            }
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size,
                         "%s: config-changing verb on every network CLI",
                         unknown_write_verbs[i]);
            return CMD_WRITE;
        }
    }

    return classify_linux_segment(seg, seg_len, reason_buf, reason_buf_size);
}


/* ----- Platform names, labels and the dropdown order ----- */

/* "auto" is deliberately first: it is the default for a new profile and the
 * value every profile in an older config inherits. It is not a device family,
 * so it maps to CMD_PLATFORM_UNKNOWN -- a session that has not resolved what
 * it is talking to. */
typedef struct {
    const char   *name;   /* stable config token */
    const char   *label;  /* UI label */
    CmdPlatform   platform;
} PlatformChoice;

static const PlatformChoice platform_choices[] = {
    { "auto",        "Auto-detect",             CMD_PLATFORM_UNKNOWN },
    { "linux",       "Linux / Unix",            CMD_PLATFORM_LINUX },
    { "cisco-ios",   "Cisco IOS / IOS-XE",      CMD_PLATFORM_CISCO_IOS },
    { "cisco-nxos",  "Cisco NX-OS",             CMD_PLATFORM_CISCO_NXOS },
    { "cisco-asa",   "Cisco ASA",               CMD_PLATFORM_CISCO_ASA },
    { "hp-procurve", "HP ProCurve / ProVision", CMD_PLATFORM_HP_PROCURVE },
    { "hp-comware",  "HPE Comware / H3C",       CMD_PLATFORM_HP_COMWARE },
    { "aruba-cx",    "Aruba OS-CX",             CMD_PLATFORM_ARUBA_CX },
    { "aruba-os",    "ArubaOS (controller)",    CMD_PLATFORM_ARUBA_OS },
    { "panos",       "Palo Alto PAN-OS",        CMD_PLATFORM_PANOS },
    { "junos",       "Juniper Junos",           CMD_PLATFORM_JUNOS },
    { "fortios",     "Fortinet FortiOS",        CMD_PLATFORM_FORTIOS },
    { "vyos",        "VyOS",                    CMD_PLATFORM_VYOS },
    { "routeros",    "MikroTik RouterOS",       CMD_PLATFORM_MIKROTIK }
};

static const int platform_choice_n =
    (int)(sizeof(platform_choices) / sizeof(platform_choices[0]));

CmdPlatform cmd_platform_from_name(const char *name)
{
    if (!name || !name[0]) return CMD_PLATFORM_UNKNOWN;
    for (int i = 0; i < platform_choice_n; i++) {
        const char *n = platform_choices[i].name;
        size_t len = strlen(n);
        if (strlen(name) == len && ci_memcmp(name, n, len) == 0)
            return platform_choices[i].platform;
    }
    return CMD_PLATFORM_UNKNOWN;
}

const char *cmd_platform_name(CmdPlatform platform)
{
    /* Skip row 0 ("auto"): it shares CMD_PLATFORM_UNKNOWN with the unresolved
     * state, and "auto" is what an unresolved session should persist as. */
    if (platform == CMD_PLATFORM_UNKNOWN) return "auto";
    for (int i = 1; i < platform_choice_n; i++) {
        if (platform_choices[i].platform == platform)
            return platform_choices[i].name;
    }
    return "auto";
}

const char *cmd_platform_label(CmdPlatform platform)
{
    if (platform == CMD_PLATFORM_UNKNOWN) return "Auto-detect";
    for (int i = 1; i < platform_choice_n; i++) {
        if (platform_choices[i].platform == platform)
            return platform_choices[i].label;
    }
    return "Auto-detect";
}

int cmd_platform_choice_count(void)
{
    return platform_choice_n;
}

const char *cmd_platform_choice_name(int index)
{
    if (index < 0 || index >= platform_choice_n) return NULL;
    return platform_choices[index].name;
}

const char *cmd_platform_choice_label(int index)
{
    if (index < 0 || index >= platform_choice_n) return NULL;
    return platform_choices[index].label;
}

/* ----- Device-platform shell-metacharacter floor -----
 *
 * On a network-device platform "|" is a display filter, not a shell pipe,
 * so classify_pass() never splits a device segment on it (M1, above) --
 * "show run | include x" reaches the per-platform classifier as one
 * string, and every device classifier's first-token check sees "show" and
 * stops looking. That is correct for the ordinary filter case, but it
 * also means a segment that carries real shell danger past that first
 * token -- "show version || rm -rf /", "show run | sudo bash", "show run
 * > bootflash:evil" -- classifies only as whatever "show" means, which is
 * always at best READ. device_shell_floor() is the safety net under that:
 * a minimum level for the segment, independent of what the platform's own
 * classifier decided, that these dangers can never fall below. It never
 * lowers a level the platform classifier already found (classify_pass()
 * takes the max of the two).
 *
 * Session platform detection can also simply be wrong -- a session
 * labelled as a device may really be a Linux shell -- so what "||"
 * actually runs is read the same way a real Linux shell would read it
 * (classify_linux_segment()), not judged by device syntax.
 *
 * Like scan_redirects(), every scan here reads the segment under both
 * QMODE_POSIX and QMODE_PWSH and takes the worse of the two: which
 * characters are "active" (unquoted, unescaped) depends on which shell
 * ends up interpreting the text, and the classifier does not get to
 * assume the friendlier one. */

/* Display-filter keywords after a device "|", per CLI family. The CLI
 * takes a keyword case-insensitively and abbreviated to any prefix that is
 * unique among its keywords, so on IOS "red", "REDIRECT" and "appe" are
 * redirect and append, and "t" is tee. A word writes (exports the output
 * to the device filesystem, or sends it away) when it is, or abbreviates, a
 * write keyword and is not also a prefix of a read keyword -- such a
 * prefix is ambiguous and the device rejects it. An unrecognised word adds
 * nothing. */
typedef struct {
    const char *const *read;
    const char *const *write;
} FilterWords;

static const char *const filter_write_common[] = {
    "append", "redirect", "save", "tee", NULL
};
static const char *const ios_filter_read[] = {
    "begin", "count", "exclude", "format", "include", "section", NULL
};
static const char *const asa_filter_read[] = {
    "begin", "count", "exclude", "format", "grep", "include", "section", NULL
};
static const char *const nxos_filter_read[] = {
    "begin", "count", "cut", "diff", "egrep", "exclude", "grep", "head", "human",
    "include", "json", "json-pretty", "last", "less", "no-more", "section", "sed",
    "sort", "tr", "uniq", "wc", "xml", NULL
};
static const char *const nxos_filter_write[] = {
    "append", "email", "redirect", "save", "tee", NULL
};
static const char *const junos_filter_read[] = {
    "compare", "count", "display", "except", "find", "hold", "last", "match",
    "no-more", "refresh", "request", "resolve", "trim", NULL
};
static const char *const generic_filter_read[] = {
    "begin", "compare", "count", "cut", "details", "diff", "display", "egrep",
    "except", "exclude", "find", "format", "grep", "head", "hold", "include",
    "json", "last", "less", "match", "more", "no-more", "refresh", "request",
    "resolve", "section", "sort", "trim", "uniq", "wc", "xml", NULL
};

static FilterWords device_filter_words(CmdPlatform platform)
{
    FilterWords fw;
    fw.write = filter_write_common;
    switch (platform) {
    case CMD_PLATFORM_CISCO_IOS:  fw.read = ios_filter_read; break;
    case CMD_PLATFORM_CISCO_ASA:  fw.read = asa_filter_read; break;
    case CMD_PLATFORM_CISCO_NXOS: fw.read = nxos_filter_read; fw.write = nxos_filter_write; break;
    case CMD_PLATFORM_JUNOS:      fw.read = junos_filter_read; break;
    default:                      fw.read = generic_filter_read; break;
    }
    return fw;
}

static int word_prefix_of_any_ci(const char *w, size_t wl, const char *const *list)
{
    for (int i = 0; list[i]; i++)
        if (wl <= strlen(list[i]) && ci_memcmp(w, list[i], wl) == 0) return 1;
    return 0;
}

static int word_in_list_ci(const char *w, size_t wl, const char *const *list)
{
    for (int i = 0; list[i]; i++)
        if (tok_eq_ci(w, wl, list[i])) return 1;
    return 0;
}

static int filter_word_writes(CmdPlatform platform, const char *w, size_t wl)
{
    FilterWords fw = device_filter_words(platform);
    if (wl == 0) return 0;
    if (word_in_list_ci(w, wl, fw.write)) return 1;
    if (word_in_list_ci(w, wl, fw.read)) return 0;
    return word_prefix_of_any_ci(w, wl, fw.write) && !word_prefix_of_any_ci(w, wl, fw.read);
}

/* An output redirect ("show run > bootflash:x", "show run >> flash:y") is
 * a write only on a CLI that has shell-style redirection (NX-OS), only in
 * the command before the first display-filter "|" (after it, '>' is part
 * of a filter pattern: "| include *>i"), and only when a real target word
 * follows the '>' -- a trailing '>' is a display artifact. Built on
 * quote_step() directly rather than reusing scan_redirects(), which treats
 * any '>' as a write -- correct for a real shell, wrong here. */
static CmdSafetyLevel scan_device_redirect_mode(const char *seg, size_t seg_len,
                                                 QuoteMode mode, CmdPlatform platform)
{
    const char *end = seg + seg_len;
    const char *p = seg;
    QuoteScan qs;
    /* NX-OS has genuine shell-style redirection; VyOS's operational mode is
     * bash underneath and HP Comware's CLI also writes a file on a bare
     * '>' before any display filter -- everywhere else (IOS, ASA, Junos,
     * ...) '>' is never special outside a filter pattern. */
    if (platform != CMD_PLATFORM_CISCO_NXOS && platform != CMD_PLATFORM_VYOS &&
        platform != CMD_PLATFORM_HP_COMWARE)
        return CMD_READ;
    quote_scan_init(&qs, mode);

    while (p < end) {
        const char *here = p;
        if (!quote_step(&qs, &p, end)) continue;
        if (*here == '|') break;                    /* display filter from here */
        if (*here != '>') continue;

        const char *r = here + 1;
        if (r < end && *r == '>') r++;             /* ">>" append */
        while (r < end && (*r == ' ' || *r == '\t')) r++;
        if (r >= end) continue;                     /* nothing follows: not a write */
        return CMD_WRITE;
    }
    return CMD_READ;
}

/* One quoting-mode reading of the floor: scans the segment's active
 * characters for "||", a single "|", and an output redirect, and returns
 * the worst level any of them implies. reason_buf/reason_buf_size follow
 * the same "only the first UNKNOWN-or-worse finding gets a reason"
 * convention classify_pass() itself uses -- the caller passes NULL/0 once
 * a reason has already been captured. */
static CmdSafetyLevel device_shell_floor_mode(const char *seg, size_t seg_len,
                                               QuoteMode mode, CmdPlatform platform,
                                               char *reason_buf, size_t reason_buf_size)
{
    CmdSafetyLevel floor = CMD_READ;
    const char *end = seg + seg_len;
    const char *p = seg;
    int in_filter = 0;   /* past the first display-filter "|" */
    QuoteScan qs;
    quote_scan_init(&qs, mode);

    CmdSafetyLevel redir = scan_device_redirect_mode(seg, seg_len, mode, platform);
    if (redir > floor) {
        floor = redir;
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size,
                     "output redirection writes to the device filesystem");
    }

    while (p < end) {
        const char *here = p;
        if (!quote_step(&qs, &p, end)) continue;

        /* A lone '&' backgrounds what precedes it and starts a new shell
         * command with whatever follows it -- device platforms don't split
         * on it at the top level the way Linux and an unresolved platform
         * do (classify_pass() gates that split on platform), so this floor
         * has to catch it itself, the same way it already catches "||":
         * read the way a real Linux shell would read it, never counted for
         * less than UNKNOWN outside a filter pattern (or, inside one, only
         * when the Linux reading is WRITE or worse). Never a redirect's own
         * '&' ("2>&1", "&>file"), never "&&" (already a top-level split on
         * every platform), and never a trailing '&' with nothing after it
         * (plain backgrounding, nothing further to classify). */
        if (*here == '&' && !(here + 1 < end && here[1] == '&') &&
            !(here + 1 < end && here[1] == '>') &&
            !(here > seg && here[-1] == '>')) {
            const char *rem = here + 1;
            while (rem < end && (*rem == ' ' || *rem == '\t')) rem++;
            if (rem >= end) continue;
            CmdSafetyLevel rl = classify_linux_segment(rem, (size_t)(end - rem), NULL, 0);
            if (!in_filter) {
                if (rl < CMD_UNKNOWN) rl = CMD_UNKNOWN;
            } else if (rl < CMD_WRITE) {
                rl = CMD_READ;
            }
            if (rl > floor) {
                floor = rl;
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size,
                             "&: backgrounds and runs a shell command");
            }
            continue;
        }

        if (*here != '|') continue;

        if ((here + 1) < end && here[1] == '|') {
            /* Rule 1: "||" in the command itself is never a display
             * filter. What comes after it runs as a shell command if the
             * first part fails, read the way a real Linux shell would read
             * it, and never counts for less than UNKNOWN. Inside a filter
             * pattern ("| include a||b") it is regex alternation, so there
             * it only raises the floor when the Linux reading of the rest
             * is WRITE or worse. classify_linux_segment() reads only the
             * first command of the remainder, so scanning continues after
             * the "||" (its second '|' skipped, so it is not re-read as a
             * single pipe): a later "| sh" in the remainder still meets
             * rule 2. */
            const char *rem = here + 2;
            CmdSafetyLevel rl = classify_linux_segment(rem, (size_t)(end - rem),
                                                         NULL, 0);
            if (!in_filter) {
                if (rl < CMD_UNKNOWN) rl = CMD_UNKNOWN;
            } else if (rl < CMD_WRITE) {
                rl = CMD_READ;
            }
            if (rl > floor) {
                floor = rl;
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size,
                             "||: runs a shell command when the first part fails");
            }
            p = here + 2;
            continue;
        }

        /* Rule 2: a single active '|', or '|&' (a bash-ism that also
         * redirects stderr into the pipe) -- look at what it feeds. */
        int amp = (here + 1) < end && here[1] == '&';
        in_filter = 1;
        const char *tgt_start = here + (amp ? 2 : 1);
        CmdSafetyLevel pipe_level = scan_pipe_target(tgt_start);
        if (pipe_level > floor) {
            floor = pipe_level;
            if (reason_buf && reason_buf_size > 0)
                snprintf(reason_buf, reason_buf_size,
                         "pipe target runs a shell or interpreter");
        }
        if (pipe_level < CMD_WRITE) {
            const char *tp = tgt_start;
            const char *ts;
            size_t tl;
            if (next_token(&tp, &ts, &tl) && filter_word_writes(platform, ts, tl)) {
                if (CMD_WRITE > floor) {
                    floor = CMD_WRITE;
                    if (reason_buf && reason_buf_size > 0)
                        snprintf(reason_buf, reason_buf_size,
                                 "pipe to %.*s: writes to the device filesystem",
                                 (int)tl, ts);
                }
            }
        }
        if (amp) p = here + 2;
    }
    return floor;
}

/* The floor applied in classify_pass() to every platform except Linux and
 * Unknown (which already get the full shell reading on their own terms).
 * Reads the segment under both quoting modes and takes the worse. */
static CmdSafetyLevel device_shell_floor(const char *seg, size_t seg_len, CmdPlatform platform,
                                          char *reason_buf, size_t reason_buf_size)
{
    char reason_a[128];
    char reason_b[128];
    reason_a[0] = '\0';
    reason_b[0] = '\0';

    QuoteMode saved = tok_mode;
    tok_mode = QMODE_POSIX;
    CmdSafetyLevel a = device_shell_floor_mode(seg, seg_len, QMODE_POSIX, platform,
                            reason_buf ? reason_a : NULL,
                            reason_buf ? sizeof reason_a : 0);
    tok_mode = QMODE_PWSH;
    CmdSafetyLevel b = device_shell_floor_mode(seg, seg_len, QMODE_PWSH, platform,
                            reason_buf ? reason_b : NULL,
                            reason_buf ? sizeof reason_b : 0);
    tok_mode = saved;

    if (a >= b) {
        if (reason_buf && reason_buf_size > 0 && reason_a[0])
            snprintf(reason_buf, reason_buf_size, "%s", reason_a);
        return a;
    }
    if (reason_buf && reason_buf_size > 0 && reason_b[0])
        snprintf(reason_buf, reason_buf_size, "%s", reason_b);
    return b;
}

/* ----- Top-level command classification ----- */

/* The one classification pass. Returns the worst category across the
 * command's segments and, when mask_out is non-NULL, the set of every
 * category present -- see CMD_MASK_OF in the header for why the set
 * matters to the auto-approve gate. */
static CmdSafetyLevel classify_pass(const char *command, CmdPlatform platform,
                                    QuoteMode mode,
                                    char *reason_buf, size_t reason_buf_size,
                                    unsigned *mask_out)
{
    CmdSafetyLevel worst = CMD_READ;
    unsigned mask = 0;
    const char *p = command;
    const char *cmd_end = command + strlen(command);
    int is_pipe_target = 0;
    QuoteScan qs;
    quote_scan_init(&qs, mode);
    tok_mode = mode;

    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;

        const char *seg_start = p;
        while (*p) {
            const char *here = p;
            /* Quoted or escaped characters never split; quote_step has
             * already moved p past them. */
            if (!quote_step(&qs, &p, cmd_end)) continue;
            p = here;
            {
                /* M1: every device platform uses | as a display filter,
                 * not a shell pipe -- only Linux splits on it (fixes F2).
                 * So does an unresolved platform, which is Linux plus an
                 * overlay: without the split, everything after the first
                 * '|' went unclassified ("cat x | sh" came out READ). */
                if (*p == '|' && (platform == CMD_PLATFORM_LINUX ||
                                  platform == CMD_PLATFORM_UNKNOWN)) break;
                if (*p == ';') break;
                if (*p == '&' && *(p+1) == '&') break;
                /* A lone '&' -- the background operator, and PowerShell's
                 * call operator, as in "& Remove-Item ..." -- is also a
                 * separator on Linux and an unresolved platform, but
                 * never when it's part of a redirect (">&2",
                 * "&>/dev/null", "2>&1"): those never split. Before this,
                 * next_token() treated a bare '&' as end-of-input, so an
                 * unsplit segment starting with '&' never reached a first
                 * token and classified READ: a call-operator launch of
                 * evil.exe, and a background "cat x" ahead of a
                 * destructive "rm -rf", both did (classifier holes v1.2.9
                 * audit). */
                if (*p == '&' &&
                    (platform == CMD_PLATFORM_LINUX ||
                     platform == CMD_PLATFORM_UNKNOWN) &&
                    *(p + 1) != '>' &&
                    !(p > command && *(p - 1) == '>'))
                    break;
            }
            p++;
        }
        size_t seg_len = (size_t)(p - seg_start);

        CmdSafetyLevel seg_level;

        switch (platform) {
        case CMD_PLATFORM_CISCO_IOS:
            seg_level = classify_cisco_ios_segment(seg_start, seg_len,
                            worst == CMD_READ ? reason_buf : NULL,
                            worst == CMD_READ ? reason_buf_size : 0);
            break;
        case CMD_PLATFORM_CISCO_NXOS:
            seg_level = classify_cisco_nxos_segment(seg_start, seg_len,
                            worst == CMD_READ ? reason_buf : NULL,
                            worst == CMD_READ ? reason_buf_size : 0);
            break;
        case CMD_PLATFORM_CISCO_ASA:
            seg_level = classify_cisco_asa_segment(seg_start, seg_len,
                            worst == CMD_READ ? reason_buf : NULL,
                            worst == CMD_READ ? reason_buf_size : 0);
            break;
        case CMD_PLATFORM_ARUBA_CX:
            seg_level = classify_aruba_cx_segment(seg_start, seg_len,
                            worst == CMD_READ ? reason_buf : NULL,
                            worst == CMD_READ ? reason_buf_size : 0);
            break;
        case CMD_PLATFORM_ARUBA_OS:
            seg_level = classify_aruba_os_segment(seg_start, seg_len,
                            worst == CMD_READ ? reason_buf : NULL,
                            worst == CMD_READ ? reason_buf_size : 0);
            break;
        case CMD_PLATFORM_PANOS:
            seg_level = classify_panos_segment(seg_start, seg_len,
                            worst == CMD_READ ? reason_buf : NULL,
                            worst == CMD_READ ? reason_buf_size : 0);
            break;
        case CMD_PLATFORM_HP_PROCURVE:
            seg_level = classify_hp_procurve_segment(seg_start, seg_len,
                            worst == CMD_READ ? reason_buf : NULL,
                            worst == CMD_READ ? reason_buf_size : 0);
            break;
        case CMD_PLATFORM_HP_COMWARE:
            seg_level = classify_hp_comware_segment(seg_start, seg_len,
                            worst == CMD_READ ? reason_buf : NULL,
                            worst == CMD_READ ? reason_buf_size : 0);
            break;
        case CMD_PLATFORM_JUNOS:
            seg_level = classify_junos_segment(seg_start, seg_len,
                            worst == CMD_READ ? reason_buf : NULL,
                            worst == CMD_READ ? reason_buf_size : 0);
            break;
        case CMD_PLATFORM_FORTIOS:
            seg_level = classify_fortios_segment(seg_start, seg_len,
                            worst == CMD_READ ? reason_buf : NULL,
                            worst == CMD_READ ? reason_buf_size : 0);
            break;
        case CMD_PLATFORM_VYOS:
            seg_level = classify_vyos_segment(seg_start, seg_len,
                            worst == CMD_READ ? reason_buf : NULL,
                            worst == CMD_READ ? reason_buf_size : 0);
            break;
        case CMD_PLATFORM_MIKROTIK:
            seg_level = classify_mikrotik_segment(seg_start, seg_len,
                            worst == CMD_READ ? reason_buf : NULL,
                            worst == CMD_READ ? reason_buf_size : 0);
            break;
        case CMD_PLATFORM_UNKNOWN:
            seg_level = classify_unknown_segment(seg_start, seg_len,
                            worst == CMD_READ ? reason_buf : NULL,
                            worst == CMD_READ ? reason_buf_size : 0);
            break;
        case CMD_PLATFORM_LINUX:
        default:
            seg_level = classify_linux_segment(seg_start, seg_len,
                            worst == CMD_READ ? reason_buf : NULL,
                            worst == CMD_READ ? reason_buf_size : 0);
            break;
        }

        /* Device-platform shell-metacharacter floor (see the comment
         * above device_shell_floor()): Linux and an unresolved platform
         * already get the full shell reading above, everything else gets
         * this minimum on top of whatever its own classifier decided.
         * Never lowers seg_level, and only supplies a reason when it is
         * the one raising the level and a reason has not already been
         * captured for this command (the same worst == CMD_READ gate the
         * per-platform calls above use). */
        if (platform != CMD_PLATFORM_LINUX && platform != CMD_PLATFORM_UNKNOWN) {
            char floor_reason[256];
            floor_reason[0] = '\0';
            CmdSafetyLevel floor = device_shell_floor(seg_start, seg_len, platform,
                            worst == CMD_READ ? floor_reason : NULL,
                            worst == CMD_READ ? sizeof floor_reason : 0);
            if (floor > seg_level) {
                seg_level = floor;
                if (worst == CMD_READ && reason_buf && reason_buf_size > 0 &&
                    floor_reason[0])
                    snprintf(reason_buf, reason_buf_size, "%s", floor_reason);
            }
        }

        if (is_pipe_target) {
            CmdSafetyLevel pipe_level = scan_pipe_target(seg_start);
            if (pipe_level > seg_level) seg_level = pipe_level;
        }

        {
            CmdSafetyLevel sub = seg_substitution_level(seg_start, seg_len, platform);
            tok_mode = mode; /* the inner classification ran its own passes */
            if (sub > seg_level) {
                if (worst == CMD_READ && reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size,
                             "command substitution: runs a command the rules cannot see");
                seg_level = sub;
            }
        }

        if (seg_level > worst) worst = seg_level;
        mask |= CMD_MASK_OF(seg_level);
        /* No early-out on CRITICAL: every remaining segment still has
         * to contribute its bit to the mask. */

        is_pipe_target = (*p == '|' && *(p+1) != '|');
        if (*p == '|' && *(p+1) == '|') p += 2;
        else if (*p == '|' && *(p+1) == '&') p += 2;   /* "|&": stderr+stdout pipe */
        else if (*p == '&' && *(p+1) == '&') p += 2;
        else if (*p) p++;
    }

    if (mask_out) *mask_out = mask ? mask : CMD_MASK_OF(CMD_READ);
    return worst;
}

/* The one classification entry point. Reads the command under the POSIX
 * quoting rules; if the PowerShell reading would activate different
 * metacharacters, or a quote or escape is left open, the command is
 * ambiguous -- see "Quoting model" above -- and the result is the worse of
 * both readings, never better than CMD_UNKNOWN. */
static CmdSafetyLevel classify_core(const char *command, CmdPlatform platform,
                                    char *reason_buf, size_t reason_buf_size,
                                    unsigned *mask_out)
{
    if (!command || !command[0]) {
        if (reason_buf && reason_buf_size > 0)
            reason_buf[0] = '\0';
        if (mask_out) *mask_out = CMD_MASK_OF(CMD_READ);
        return CMD_READ;
    }

    QuoteMode saved_mode = tok_mode;
    unsigned mask_a = 0;
    CmdSafetyLevel level_a = classify_pass(command, platform, QMODE_POSIX,
                                           reason_buf, reason_buf_size, &mask_a);
    /* On an unresolved platform the command may reach PowerShell, which
     * also takes the typographic quotes as string delimiters: any of them
     * makes the quoting shell-dependent. When the two readings differ only
     * in where words split (an escaped blank, "My\ File"), both readings
     * are classified and the worse taken, without the UNKNOWN floor: the
     * segments are the same, only a flag could hide in one reading. */
    int typo = (platform == CMD_PLATFORM_UNKNOWN && has_typo_quote(command));
    int metas_agree = !typo && quoting_agrees(command, 0);
    if (metas_agree && quoting_agrees(command, 1)) {
        tok_mode = saved_mode;
        if (mask_out) *mask_out = mask_a;
        return level_a;
    }

    char reason_b[256];
    unsigned mask_b = 0;
    reason_b[0] = '\0';
    CmdSafetyLevel level_b = classify_pass(command, platform, QMODE_PWSH,
                                           reason_b, sizeof reason_b, &mask_b);
    tok_mode = saved_mode;
    CmdSafetyLevel worst = level_a;
    if (level_b > worst) {
        worst = level_b;
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size, "%s", reason_b);
    }
    if (metas_agree) {
        if (mask_out) *mask_out = mask_a | mask_b;
        return worst;
    }
    if (worst < CMD_UNKNOWN) {
        worst = CMD_UNKNOWN;
        if (reason_buf && reason_buf_size > 0)
            snprintf(reason_buf, reason_buf_size,
                     "unbalanced or shell-dependent quoting");
    }
    if (mask_out) *mask_out = mask_a | mask_b | CMD_MASK_OF(CMD_UNKNOWN);
    return worst;
}

CmdSafetyLevel cmd_classify_ex(const char *command, CmdPlatform platform,
                                char *reason_buf, size_t reason_buf_size)
{
    return classify_core(command, platform, reason_buf, reason_buf_size, NULL);
}

CmdSafetyLevel cmd_classify(const char *command, CmdPlatform platform)
{
    return classify_core(command, platform, NULL, 0, NULL);
}

unsigned cmd_classify_mask(const char *command, CmdPlatform platform)
{
    unsigned mask = CMD_MASK_OF(CMD_READ);
    classify_core(command, platform, NULL, 0, &mask);
    return mask;
}

/* ----- A session whose resolved platform host output has contradicted -----
 * CMD_PLATFORM_UNKNOWN is not a strictest ruleset: it is never looser than
 * Linux, but it is looser than every device ruleset somewhere (IOS's "copy
 * running-config startup-config" is CRITICAL under IOS and only UNKNOWN
 * under UNKNOWN; Junos's "file delete" CRITICAL against READ). So a
 * contradicted session keeps its resolved platform and is judged under BOTH
 * rulesets, taking the worse of the two -- never looser than either. */

static int session_needs_second_ruleset(CmdPlatform platform, int contradicted)
{
    return contradicted && platform != CMD_PLATFORM_UNKNOWN;
}

CmdSafetyLevel cmd_classify_session(const char *command, CmdPlatform platform,
                                    int contradicted)
{
    CmdSafetyLevel level = cmd_classify(command, platform);
    if (session_needs_second_ruleset(platform, contradicted)) {
        CmdSafetyLevel other = cmd_classify(command, CMD_PLATFORM_UNKNOWN);
        if (other > level) level = other;
    }
    return level;
}

unsigned cmd_classify_mask_session(const char *command, CmdPlatform platform,
                                   int contradicted)
{
    unsigned mask = cmd_classify_mask(command, platform);
    if (session_needs_second_ruleset(platform, contradicted))
        mask |= cmd_classify_mask(command, CMD_PLATFORM_UNKNOWN);
    return mask;
}
