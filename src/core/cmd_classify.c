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
    /* crontab */
    { "crontab", "-l", CMD_SAFE },
    { "crontab", "--list", CMD_SAFE },
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
    { "firewall-cmd", "--list-all",        CMD_SAFE },
    { "firewall-cmd", "--list-all-zones",  CMD_SAFE },
    { "firewall-cmd", "--state",           CMD_SAFE },
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
    { "helm", "list",    CMD_SAFE },
    { "helm", "status",  CMD_SAFE },
    { "helm", "get",     CMD_SAFE },
    { "helm", "history", CMD_SAFE },
    { "terraform", "destroy", CMD_CRITICAL },
    { "terraform", "apply",   CMD_WRITE },
    { "terraform", "init",    CMD_WRITE },
    { "terraform", "plan",     CMD_SAFE },
    { "terraform", "show",     CMD_SAFE },
    { "terraform", "output",   CMD_SAFE },
    { "terraform", "validate", CMD_SAFE },
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
    { "apt", "list", CMD_SAFE },
    { "apt", "show", CMD_SAFE },
    { "apt", "search", CMD_SAFE },
    { "apt", "policy", CMD_SAFE },
    { "dnf", "list", CMD_SAFE },
    { "dnf", "info", CMD_SAFE },
    { "dnf", "search", CMD_SAFE },
    { "dnf", "repolist", CMD_SAFE },
    { "yum", "list", CMD_SAFE },
    { "yum", "info", CMD_SAFE },
    { "rpm", "-qa", CMD_SAFE },
    { "rpm", "-qi", CMD_SAFE },
    { "rpm", "-ql", CMD_SAFE },
    { "dpkg", "-l", CMD_SAFE },
    { "dpkg", "-L", CMD_SAFE },
    { "dpkg", "-S", CMD_SAFE },
    { "pacman", "-Q",  CMD_SAFE },
    { "pacman", "-Qs", CMD_SAFE },
    { "pacman", "-Qi", CMD_SAFE },
    { "zypper", "se", CMD_SAFE },
    { "zypper", "search", CMD_SAFE },
    { "zypper", "info", CMD_SAFE },
    { "zypper", "repos", CMD_SAFE },
    { "zypper", "if", CMD_SAFE },
    { "zypper", "lr", CMD_SAFE },
    { "apk", "info", CMD_SAFE },
    { "snap", "list", CMD_SAFE },
    { "flatpak", "list", CMD_SAFE },
    { "pip",  "list",   CMD_SAFE },
    { "pip",  "show",   CMD_SAFE },
    { "pip",  "freeze", CMD_SAFE },
    { "pip3", "list",   CMD_SAFE },
    { "pip3", "show",   CMD_SAFE },
    { "pip3", "freeze", CMD_SAFE },
    { "npm", "ls",   CMD_SAFE },
    { "npm", "view", CMD_SAFE },

    /* --- further spec 3.3 additions: multi-token allow-list entries that
     * don't fit the single-token linux_safe_cmds[] table (added while
     * wiring up the C2 allow-list fallthrough -- see the report for what
     * was deliberately left off) --- */
    { "systemctl", "cat",          CMD_SAFE },
    { "systemctl", "show",         CMD_SAFE },
    { "systemctl", "list-timers",  CMD_SAFE },
    { "nft",       "list",         CMD_SAFE },
    { "ip",        "neigh",        CMD_SAFE },
    { "route",     "-n",           CMD_SAFE },
    { "docker",    "stats",        CMD_SAFE },
    { "docker",    "top",          CMD_SAFE },
    { "kubectl",   "top",          CMD_SAFE },
    { "kubectl",   "explain",      CMD_SAFE },
    { "kubectl",   "api-resources", CMD_SAFE },
    { "kubectl",   "version",      CMD_SAFE },

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
    { NULL, NULL, NULL, CMD_SAFE }
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
static const char *linux_safe_cmds[] = {
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
        /* No in-place flag: sed/perl without "-i"/"-pi" writes to stdout,
         * not to the file -- read-only (C2 fix: explicit now rather than
         * an implicit fall-through to the old SAFE bug). */
        return redir;
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
            if (tok_eq(tok2_start, tok2_len, "disable")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "ufw disable");
                return CMD_CRITICAL;
            }
            /* ufw status: a pure query, not a config change (F8, spec 3.3
             * / 17 corner case) -- narrow SAFE override, doesn't touch any
             * other ufw form */
            if (tok_eq(tok2_start, tok2_len, "status"))
                return CMD_SAFE;
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

    /* find -delete / find -exec rm ...: scan every token (F9) */
    if (tok_eq(base1, base1_len, "find")) {
        const char *scan = p;
        const char *ts;
        size_t tl;
        int saw_exec = 0;
        while (next_token(&scan, &ts, &tl)) {
            if (tok_eq(ts, tl, "-delete")) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "find -delete: removes matched files");
                return CMD_CRITICAL;
            }
            if (saw_exec && (tok_eq(ts, tl, "rm") ||
                              tok_eq(ts, tl, "/bin/rm") || tok_eq(ts, tl, "/usr/bin/rm"))) {
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "find -exec rm: removes matched files");
                return CMD_CRITICAL;
            }
            saw_exec = tok_eq(ts, tl, "-exec");
        }
        return redir;
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

    /* date: bare (queries the current time) is SAFE. "date -s ..." is
     * already WRITE via the linux_subcmd_rules two-token match above; any
     * other argument form is not blanket-SAFE (spec 3.3: "date" *(bare)*)
     * -- falls through unclassified rather than being listed in
     * linux_safe_cmds. */
    if (tok_eq(base1, base1_len, "date")) {
        const char *p2 = p;
        if (!next_token(&p2, &tok2_start, &tok2_len))
            return redir;
    }

    /* hostname: bare (queries the current host name) is SAFE. "hostname
     * newname" changes it, so -- like "date" above -- this is not
     * blanket-SAFE (spec 3.3: "hostname" *(bare)*). */
    if (tok_eq(base1, base1_len, "hostname")) {
        const char *p2 = p;
        if (!next_token(&p2, &tok2_start, &tok2_len))
            return redir;
    }

    /* dmesg: bare (prints the kernel ring buffer) is SAFE. "dmesg -C" is
     * already WRITE via linux_subcmd_rules above; other forms are not
     * blanket-SAFE (spec 3.3: "dmesg" *(bare)*). */
    if (tok_eq(base1, base1_len, "dmesg")) {
        const char *p2 = p;
        if (!next_token(&p2, &tok2_start, &tok2_len))
            return redir;
    }

    /* journalctl --vacuum-*: prefix match on the subcommand */
    if (tok_eq(base1, base1_len, "journalctl")) {
        const char *scan = p;
        const char *ts;
        size_t tl;
        while (next_token(&scan, &ts, &tl)) {
            if (tok_prefix(ts, tl, "--vacuum-")) {
                CmdSafetyLevel level = CMD_WRITE;
                if (reason_buf && reason_buf_size > 0)
                    snprintf(reason_buf, reason_buf_size, "journalctl --vacuum: deletes old logs");
                return level > redir ? level : redir;
            }
        }
        return redir;
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
     * or linux_safe_cmds below; anything else is honestly CMD_UNKNOWN,
     * combined with whatever scan_redirects already found -- a redirect
     * still raises UNKNOWN to WRITE ("frobnicate > /etc/passwd" is WRITE,
     * not UNKNOWN, because the redirect itself writes regardless of
     * whether "frobnicate" is recognised). */
    if (tok_in_list(base1, base1_len, linux_safe_cmds))
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
        tok_eq_ci(tok1_start, tok1_len, "verify") ||
        tok_eq_ci(tok1_start, tok1_len, "more") ||
        tok_eq_ci(tok1_start, tok1_len, "where") ||
        tok_eq_ci(tok1_start, tok1_len, "who"))
        return CMD_SAFE;

    /* reload cancel: cancels a pending reload, explicitly SAFE -- must be
     * checked before the general "reload" -> CRITICAL rule below */
    if (tok_eq_ci(tok1_start, tok1_len, "reload")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            tok_eq_ci(tok2_start, tok2_len, "cancel"))
            return CMD_SAFE;
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
        return CMD_SAFE;

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
        return CMD_SAFE;

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
        tok_eq_ci(tok1_start, tok1_len, "dir") ||
        /* spec 9 SAFE additions */
        tok_eq_ci(tok1_start, tok1_len, "diff") ||
        tok_eq_ci(tok1_start, tok1_len, "less") ||
        tok_eq_ci(tok1_start, tok1_len, "top"))
        return CMD_SAFE;

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
                    return CMD_SAFE;
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
        tok_eq_ci(tok1_start, tok1_len, "up") ||
        /* spec 11 SAFE additions */
        tok_eq_ci(tok1_start, tok1_len, "check"))
        return CMD_SAFE;

    /* set cli: output-format preference only, not a config change (F15) --
     * must be checked before the generic "set" -> WRITE rule below */
    if (tok_eq_ci(tok1_start, tok1_len, "set")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len)) {
            if (tok_eq_ci(tok2_start, tok2_len, "cli"))
                return CMD_SAFE;
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
                    return CMD_SAFE;
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
                return CMD_SAFE;
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
        return CMD_SAFE;

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
        return CMD_SAFE;

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
        return CMD_SAFE;

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
        return CMD_SAFE;

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
        return CMD_SAFE;

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
        return CMD_SAFE;

    /* run show / monitor traffic / monitor interface */
    if (tok_eq_ci(tok1_start, tok1_len, "run")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            tok_prefix_ci(tok2_start, tok2_len, "show"))
            return CMD_SAFE;
    }
    if (tok_eq_ci(tok1_start, tok1_len, "monitor")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            (tok_eq_ci(tok2_start, tok2_len, "traffic") ||
             tok_eq_ci(tok2_start, tok2_len, "interface")))
            return CMD_SAFE;
    }
    /* file show / file list / file compare (safe); file copy / file
     * archive (write); file delete (critical) */
    if (tok_eq_ci(tok1_start, tok1_len, "file")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len)) {
            if (tok_eq_ci(tok2_start, tok2_len, "show") ||
                tok_eq_ci(tok2_start, tok2_len, "list") ||
                tok_eq_ci(tok2_start, tok2_len, "compare"))
                return CMD_SAFE;
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
                return CMD_SAFE;
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
            return CMD_SAFE;
    }
    /* commit: applies the candidate config (critical); commit check
     * stays safe; commit confirmed is the safer auto-reverting form
     * (write) */
    if (tok_eq_ci(tok1_start, tok1_len, "commit")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len)) {
            if (tok_eq_ci(tok2_start, tok2_len, "check"))
                return CMD_SAFE;
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
        return CMD_SAFE;

    /* --- Safe commands --- */
    if (tok_eq_ci(tok1_start, tok1_len, "get") ||
        tok_eq_ci(tok1_start, tok1_len, "show") ||
        tok_eq_ci(tok1_start, tok1_len, "end") ||
        tok_eq_ci(tok1_start, tok1_len, "next") ||
        tok_eq_ci(tok1_start, tok1_len, "abort") ||
        tok_eq_ci(tok1_start, tok1_len, "exit"))
        return CMD_SAFE;

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
                return CMD_SAFE;
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
                    if (tok_eq_ci(ts, tl, "top")) return CMD_SAFE;
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
                return CMD_SAFE;
            if (tok_eq_ci(tok2_start, tok2_len, "sniffer")) {
                if (seg_has_token_ci(p2, (size_t)((seg + seg_len) - p2), "packet"))
                    return CMD_SAFE;
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
        return CMD_SAFE;

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
        return CMD_SAFE;

    /* run show -> safe */
    if (tok_eq_ci(tok1_start, tok1_len, "run")) {
        const char *p2 = p;
        if (next_token(&p2, &tok2_start, &tok2_len) &&
            tok_prefix_ci(tok2_start, tok2_len, "show"))
            return CMD_SAFE;
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
    static const char *safe_verbs[] = {
        "print", "get", "find", "export", "monitor", "monitor-traffic",
        "info", NULL
    };

    if (seg_len == 0)
        return CMD_SAFE;

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

    for (int i = 0; safe_verbs[i]; i++) {
        if (seg_has_token_ci(seg, seg_len, safe_verbs[i]))
            return CMD_SAFE;
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

/* ----- Top-level command classification ----- */

/* The one classification pass. Returns the worst category across the
 * command's segments and, when mask_out is non-NULL, the set of every
 * category present -- see CMD_MASK_OF in the header for why the set
 * matters to the auto-approve gate. */
static CmdSafetyLevel classify_core(const char *command, CmdPlatform platform,
                                    char *reason_buf, size_t reason_buf_size,
                                    unsigned *mask_out)
{
    if (!command || !command[0]) {
        if (reason_buf && reason_buf_size > 0)
            reason_buf[0] = '\0';
        if (mask_out) *mask_out = CMD_MASK_OF(CMD_SAFE);
        return CMD_SAFE;
    }

    CmdSafetyLevel worst = CMD_SAFE;
    unsigned mask = 0;
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
                /* M1: every platform but Linux uses | as a display filter,
                 * not a shell pipe -- only Linux splits on it (fixes F2). */
                if (*p == '|' && platform == CMD_PLATFORM_LINUX) break;
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
        case CMD_PLATFORM_HP_PROCURVE:
            seg_level = classify_hp_procurve_segment(seg_start, seg_len,
                            worst == CMD_SAFE ? reason_buf : NULL,
                            worst == CMD_SAFE ? reason_buf_size : 0);
            break;
        case CMD_PLATFORM_HP_COMWARE:
            seg_level = classify_hp_comware_segment(seg_start, seg_len,
                            worst == CMD_SAFE ? reason_buf : NULL,
                            worst == CMD_SAFE ? reason_buf_size : 0);
            break;
        case CMD_PLATFORM_JUNOS:
            seg_level = classify_junos_segment(seg_start, seg_len,
                            worst == CMD_SAFE ? reason_buf : NULL,
                            worst == CMD_SAFE ? reason_buf_size : 0);
            break;
        case CMD_PLATFORM_FORTIOS:
            seg_level = classify_fortios_segment(seg_start, seg_len,
                            worst == CMD_SAFE ? reason_buf : NULL,
                            worst == CMD_SAFE ? reason_buf_size : 0);
            break;
        case CMD_PLATFORM_VYOS:
            seg_level = classify_vyos_segment(seg_start, seg_len,
                            worst == CMD_SAFE ? reason_buf : NULL,
                            worst == CMD_SAFE ? reason_buf_size : 0);
            break;
        case CMD_PLATFORM_MIKROTIK:
            seg_level = classify_mikrotik_segment(seg_start, seg_len,
                            worst == CMD_SAFE ? reason_buf : NULL,
                            worst == CMD_SAFE ? reason_buf_size : 0);
            break;
        case CMD_PLATFORM_UNKNOWN:
            seg_level = classify_unknown_segment(seg_start, seg_len,
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
        mask |= CMD_MASK_OF(seg_level);
        /* No early-out on CRITICAL: every remaining segment still has
         * to contribute its bit to the mask. */

        is_pipe_target = (*p == '|' && *(p+1) != '|');
        if (*p == '|' && *(p+1) == '|') p += 2;
        else if (*p == '&' && *(p+1) == '&') p += 2;
        else if (*p) p++;
    }

    if (mask_out) *mask_out = mask ? mask : CMD_MASK_OF(CMD_SAFE);
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
    unsigned mask = CMD_MASK_OF(CMD_SAFE);
    classify_core(command, platform, NULL, 0, &mask);
    return mask;
}
