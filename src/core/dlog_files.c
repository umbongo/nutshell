/* src/core/dlog_files.c -- see dlog_files.h */
#include "dlog_files.h"
#include "dlog_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Time ------------------------------------------------------------------ */

/* Howard Hinnant's days-from-civil / civil-from-days, proleptic Gregorian. */
static long long days_from_civil(long long y, int m, int d)
{
    y -= (m <= 2) ? 1 : 0;
    long long era = (y >= 0 ? y : y - 399) / 400;
    long long yoe = y - era * 400;
    long long mp = (m > 2) ? (long long)m - 3 : (long long)m + 9;
    long long doy = (153 * mp + 2) / 5 + (long long)d - 1;
    long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static void civil_from_days(long long z, int *y_out, int *m_out, int *d_out)
{
    z += 719468;
    long long era = (z >= 0 ? z : z - 146096) / 146097;
    long long doe = z - era * 146097;
    long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long long y = yoe + era * 400;
    long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    long long mp = (5 * doy + 2) / 153;
    long long d = doy - (153 * mp + 2) / 5 + 1;
    long long m = (mp < 10) ? mp + 3 : mp - 9;
    if (m <= 2) y++;
    *y_out = (int)y;
    *m_out = (int)m;
    *d_out = (int)d;
}

void dlog_civil_from_unix_ms(long long unix_ms, int offset_min, DlogTm *tm)
{
    if (!tm) return;
    long long local = unix_ms + (long long)offset_min * 60000LL;
    long long days = local / 86400000LL;
    long long rem = local % 86400000LL;
    if (rem < 0) { rem += 86400000LL; days--; }
    civil_from_days(days, &tm->year, &tm->mon, &tm->day);
    tm->hour = (int)(rem / 3600000LL);
    tm->min  = (int)((rem / 60000LL) % 60);
    tm->sec  = (int)((rem / 1000LL) % 60);
    tm->ms   = (int)(rem % 1000LL);
}

long long dlog_unix_ms_from_civil(const DlogTm *tm, int offset_min)
{
    if (!tm) return 0;
    long long days = days_from_civil(tm->year, tm->mon, tm->day);
    long long ms = days * 86400000LL + (long long)tm->hour * 3600000LL
                 + (long long)tm->min * 60000LL + (long long)tm->sec * 1000LL + tm->ms;
    return ms - (long long)offset_min * 60000LL;
}

void dlog_format_timestamp(long long unix_ms, int offset_min, char *out)
{
    if (!out) return;
    DlogTm tm;
    dlog_civil_from_unix_ms(unix_ms, offset_min, &tm);
    if (tm.year < 0) tm.year = 0;
    if (tm.year > 9999) tm.year = 9999;
    char sign = (offset_min < 0) ? '-' : '+';
    int off = (offset_min < 0) ? -offset_min : offset_min;
    if (off > 99 * 60 + 59) off = 99 * 60 + 59;
    (void)snprintf(out, (size_t)DLOG_TS_LEN + 1u, "%04d-%02d-%02dT%02d:%02d:%02d.%03d%c%02d:%02d",
                   tm.year, tm.mon, tm.day, tm.hour, tm.min, tm.sec, tm.ms,
                   sign, off / 60, off % 60);
}

static int dig(const char *s, int n, int *out)
{
    int v = 0;
    for (int i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') return 0;
        v = v * 10 + (s[i] - '0');
    }
    *out = v;
    return 1;
}

int dlog_parse_timestamp(const char *s, long long *unix_ms)
{
    if (!s) return 0;
    for (int i = 0; i < DLOG_TS_LEN; i++)
        if (s[i] == '\0') return 0;
    DlogTm tm;
    int oh, om;
    if (!dig(s, 4, &tm.year) || s[4] != '-' || !dig(s + 5, 2, &tm.mon) || s[7] != '-' ||
        !dig(s + 8, 2, &tm.day) || s[10] != 'T' || !dig(s + 11, 2, &tm.hour) || s[13] != ':' ||
        !dig(s + 14, 2, &tm.min) || s[16] != ':' || !dig(s + 17, 2, &tm.sec) || s[19] != '.' ||
        !dig(s + 20, 3, &tm.ms) || (s[23] != '+' && s[23] != '-') ||
        !dig(s + 24, 2, &oh) || s[26] != ':' || !dig(s + 27, 2, &om))
        return 0;
    if (tm.mon < 1 || tm.mon > 12 || tm.day < 1 || tm.day > 31 || tm.hour > 23 ||
        tm.min > 59 || tm.sec > 60 || om > 59)
        return 0;
    int off = oh * 60 + om;
    if (s[23] == '-') off = -off;
    if (unix_ms) *unix_ms = dlog_unix_ms_from_civil(&tm, off);
    return 1;
}

/* ---- Names ----------------------------------------------------------------- */

size_t dlog_stem_name(char *out, size_t cap, long long unix_ms, int offset_min, unsigned pid)
{
    if (!out || cap == 0) return 0;
    DlogTm tm;
    dlog_civil_from_unix_ms(unix_ms, offset_min, &tm);
    int n = snprintf(out, cap, "nutshell-%04d%02d%02d-%02d%02d%02d-p%u",
                     tm.year, tm.mon, tm.day, tm.hour, tm.min, tm.sec, pid);
    if (n < 0 || (size_t)n >= cap) { out[0] = '\0'; return 0; }
    return (size_t)n;
}

size_t dlog_file_name(char *out, size_t cap, const char *stem, unsigned part)
{
    if (!out || cap == 0 || !stem) return 0;
    int n = (part <= 1u) ? snprintf(out, cap, "%s.log", stem)
                         : snprintf(out, cap, "%s.%u.log", stem, part);
    if (n < 0 || (size_t)n >= cap) { out[0] = '\0'; return 0; }
    return (size_t)n;
}

size_t dlog_issue_file_name(char *out, size_t cap, const char *stem, unsigned n, const char *ext)
{
    if (!out || cap == 0 || !stem || !ext) return 0;
    int w = snprintf(out, cap, "%s.issue%u.%s", stem, n, ext);
    if (w < 0 || (size_t)w >= cap) { out[0] = '\0'; return 0; }
    return (size_t)w;
}

/* Parse a run of decimal digits (no sign, no leading zero requirement). */
static int parse_uint(const char **pp, unsigned *out)
{
    const char *p = *pp;
    if (*p < '0' || *p > '9') return 0;
    unsigned long long v = 0;
    while (*p >= '0' && *p <= '9') {
        v = v * 10u + (unsigned long long)(*p - '0');
        if (v > 0xFFFFFFFFULL) return 0;
        p++;
    }
    *out = (unsigned)v;
    *pp = p;
    return 1;
}

int dlog_name_parse(const char *name, DlogNameInfo *info)
{
    if (!name) return 0;
    if (strncmp(name, "nutshell-", 9) != 0) return 0;
    const char *p = name + 9;
    /* YYYYMMDD-HHMMSS */
    char stamp[16];
    for (int i = 0; i < 15; i++) {
        char c = p[i];
        if (i == 8) { if (c != '-') return 0; }
        else if (c < '0' || c > '9') return 0;
        stamp[i] = c;
    }
    stamp[15] = '\0';
    p += 15;
    if (p[0] != '-' || p[1] != 'p') return 0;
    p += 2;
    unsigned pid;
    if (!parse_uint(&p, &pid)) return 0;
    unsigned part = 0, issue = 0;
    int is_png = 0;
    if (strcmp(p, ".log") == 0) {
        part = 1;
    } else if (p[0] == '.' && p[1] >= '0' && p[1] <= '9') {
        const char *q = p + 1;
        if (!parse_uint(&q, &part) || part < 2u || strcmp(q, ".log") != 0) return 0;
    } else if (strncmp(p, ".issue", 6) == 0) {
        const char *q = p + 6;
        if (!parse_uint(&q, &issue) || issue < 1u) return 0;
        if (strcmp(q, ".log") == 0) is_png = 0;
        else if (strcmp(q, ".png") == 0) is_png = 1;
        else return 0;
    } else {
        return 0;
    }
    if (info) {
        memcpy(info->stamp, stamp, sizeof(info->stamp));
        info->pid = pid;
        info->part = part;
        info->issue = issue;
        info->is_png = is_png;
    }
    return 1;
}

int dlog_name_compare(const DlogNameInfo *a, const DlogNameInfo *b)
{
    int c = strcmp(a->stamp, b->stamp);
    if (c != 0) return c;
    if (a->pid != b->pid) return (a->pid < b->pid) ? -1 : 1;
    int ai = (a->issue != 0u), bi = (b->issue != 0u);
    if (ai != bi) return ai - bi;
    unsigned ak = ai ? a->issue : a->part;
    unsigned bk = bi ? b->issue : b->part;
    if (ak != bk) return (ak < bk) ? -1 : 1;
    if (a->is_png != b->is_png) return a->is_png - b->is_png;
    return 0;
}

/* ---- Folder ---------------------------------------------------------------- */

static int is_sep(char c) { return c == '\\' || c == '/'; }

static int is_absolute(const char *p)
{
    if (!p[0]) return 0;
    if (is_sep(p[0])) return 1;   /* rooted and UNC */
    if (((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) && p[1] == ':' && is_sep(p[2]))
        return 1;
    return 0;
}

static const char *default_lookup(const char *name, void *ctx)
{
    (void)ctx;
    return getenv(name);
}

/* Expand %NAME% into out. Returns 1, or 0 on overflow. */
static int expand_env(const char *in, DlogEnvLookup lookup, void *ctx, char *out, size_t cap)
{
    size_t o = 0;
    for (const char *p = in; *p; ) {
        if (*p == '%') {
            const char *q = p + 1;
            while ((*q >= 'A' && *q <= 'Z') || (*q >= 'a' && *q <= 'z') ||
                   (*q >= '0' && *q <= '9') || *q == '_')
                q++;
            if (q > p + 1 && *q == '%') {
                char name[64];
                size_t nl = (size_t)(q - (p + 1));
                if (nl < sizeof(name)) {
                    memcpy(name, p + 1, nl);
                    name[nl] = '\0';
                    const char *val = lookup(name, ctx);
                    if (val) {
                        size_t vl = strlen(val);
                        if (o + vl >= cap) return 0;
                        memcpy(out + o, val, vl);
                        o += vl;
                        p = q + 1;
                        continue;
                    }
                }
            }
        }
        if (o + 1 >= cap) return 0;
        out[o++] = *p++;
    }
    out[o] = '\0';
    return 1;
}

/* The separator a directory string already uses: '/' only when it has one
 * and no backslash. */
static char dir_sep(const char *dir)
{
    int fwd = 0, back = 0;
    for (const char *p = dir; *p; p++) {
        if (*p == '/') fwd = 1;
        else if (*p == '\\') back = 1;
    }
    return (fwd && !back) ? '/' : '\\';
}

static void trim_trailing_seps(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && is_sep(s[n - 1])) {
        /* keep the one after a drive letter ("D:\") and a lone root */
        if (n == 1) break;
        if (n == 3 && s[1] == ':') break;
        s[--n] = '\0';
    }
}

int dlog_join(char *out, size_t cap, const char *dir, const char *name)
{
    if (!out || cap == 0 || !dir || !name) return 0;
    char sep = dir_sep(dir);
    size_t dl = strlen(dir);
    int need_sep = (dl > 0 && !is_sep(dir[dl - 1]));
    int n = snprintf(out, cap, "%s%s%s", dir, need_sep ? (sep == '/' ? "/" : "\\") : "", name);
    if (n < 0 || (size_t)n >= cap) { out[0] = '\0'; return 0; }
    return 1;
}

int dlog_resolve_dir_ex(const char *setting, const char *exe_dir, DlogEnvLookup lookup,
                        void *ctx, char *out, size_t cap)
{
    if (!out || cap == 0) return 0;
    out[0] = '\0';
    if (!lookup) lookup = default_lookup;
    if (!setting || !setting[0]) setting = "logs";

    char expanded[1024];
    if (!expand_env(setting, lookup, ctx, expanded, sizeof(expanded))) return 0;
    trim_trailing_seps(expanded);
    if (!expanded[0]) (void)snprintf(expanded, sizeof(expanded), "logs");

    if (is_absolute(expanded)) {
        size_t l = strlen(expanded);
        if (l >= cap) return 0;
        memcpy(out, expanded, l + 1);
        return 1;
    }
    if (!exe_dir || !exe_dir[0]) return 0;
    char base[1024];
    if (snprintf(base, sizeof(base), "%s", exe_dir) >= (int)sizeof(base)) return 0;
    trim_trailing_seps(base);
    char tmp[2048];
    char sep = dir_sep(base);
    int n = snprintf(tmp, sizeof(tmp), "%s%c%s", base, sep, expanded);
    if (n < 0 || (size_t)n >= sizeof(tmp) || (size_t)n >= cap) return 0;
    memcpy(out, tmp, (size_t)n + 1u);
    return 1;
}

int dlog_resolve_dir(const char *setting, const char *exe_dir, char *out, size_t cap)
{
    return dlog_resolve_dir_ex(setting, exe_dir, NULL, NULL, out, cap);
}

int dlog_fallback_dir(const char *local_appdata, char *out, size_t cap)
{
    if (!out || cap == 0 || !local_appdata || !local_appdata[0]) return 0;
    char base[1024];
    if (snprintf(base, sizeof(base), "%s", local_appdata) >= (int)sizeof(base)) return 0;
    trim_trailing_seps(base);
    char sep = dir_sep(base);
    int n = snprintf(out, cap, "%s%cNutshell%clogs", base, sep, sep);
    if (n < 0 || (size_t)n >= cap) { out[0] = '\0'; return 0; }
    return 1;
}

static unsigned char lower_ch(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') ? (unsigned char)(c - 'A' + 'a') : c;
}

static int path_char_eq(char a, char b)
{
    if (is_sep(a) && is_sep(b)) return 1;
    return lower_ch((unsigned char)a) == lower_ch((unsigned char)b);
}

int dlog_relativize_dir(const char *abs_dir, const char *exe_dir, char *out, size_t cap)
{
    if (!abs_dir || !out || cap == 0) return 0;
    out[0] = '\0';
    char a[1024], e[1024];
    if (snprintf(a, sizeof(a), "%s", abs_dir) >= (int)sizeof(a)) return 0;
    trim_trailing_seps(a);
    const char *rest = a;
    if (exe_dir && exe_dir[0]) {
        if (snprintf(e, sizeof(e), "%s", exe_dir) >= (int)sizeof(e)) return 0;
        trim_trailing_seps(e);
        size_t el = strlen(e), al = strlen(a);
        int prefix = el > 0 && al >= el;
        for (size_t i = 0; prefix && i < el; i++)
            if (!path_char_eq(a[i], e[i])) prefix = 0;
        if (prefix && (al == el || is_sep(a[el]))) {
            size_t k = el;
            while (k < al && is_sep(a[k])) k++;
            rest = (k >= al) ? "." : a + k;
        }
    }
    size_t rl = strlen(rest);
    if (rl + 1 > cap) return 0;
    memcpy(out, rest, rl + 1);
    return 1;
}

/* ---- Rotation and retention ------------------------------------------------- */

unsigned long long dlog_file_cap_bytes(int enabled, int max_file_mb)
{
    if (!enabled) return DLOG_OFF_FILE_BYTES;
    if (max_file_mb < 1) max_file_mb = 1;
    if (max_file_mb > 100) max_file_mb = 100;
    return (unsigned long long)max_file_mb * 1024ULL * 1024ULL;
}

unsigned long long dlog_total_cap_bytes(int max_total_mb)
{
    if (max_total_mb < 5) max_total_mb = 5;
    if (max_total_mb > 5000) max_total_mb = 5000;
    return (unsigned long long)max_total_mb * 1024ULL * 1024ULL;
}

int dlog_should_rotate(unsigned long long cur_size, unsigned long long add, unsigned long long cap)
{
    if (cur_size == 0) return 0;
    return (cur_size + add > cap) ? 1 : 0;
}

unsigned long long dlog_files_total(const DlogFileInfo *files, size_t n, size_t *count)
{
    unsigned long long total = 0;
    size_t c = 0;
    for (size_t i = 0; files && i < n; i++) {
        if (!dlog_name_parse(files[i].name, NULL)) continue;
        total += files[i].size;
        c++;
    }
    if (count) *count = c;
    return total;
}

int dlog_retention_next(const DlogFileInfo *files, size_t n, const char *current,
                        unsigned long long total_cap, const unsigned char *state)
{
    if (!files) return -1;
    unsigned long long total = 0;
    for (size_t i = 0; i < n; i++) {
        if (state && state[i] == 1) continue;
        if (!dlog_name_parse(files[i].name, NULL)) continue;
        total += files[i].size;
    }
    if (total <= total_cap) return -1;

    int best = -1;
    DlogNameInfo best_info;
    memset(&best_info, 0, sizeof(best_info));
    for (size_t i = 0; i < n; i++) {
        if (state && state[i] != 0) continue;
        DlogNameInfo info;
        if (!dlog_name_parse(files[i].name, &info)) continue;
        if (current && strcmp(files[i].name, current) == 0) continue;
        if (best < 0 || dlog_name_compare(&info, &best_info) < 0) {
            best = (int)i;
            best_info = info;
        }
    }
    return best;
}

size_t dlog_retention_pick(const DlogFileInfo *files, size_t n, unsigned long long total_cap,
                           const char *current, size_t *victims, size_t victims_cap)
{
    if (!files || !victims || n == 0) return 0;
    unsigned char *state = (unsigned char *)calloc(n, 1);
    if (!state) return 0;
    size_t count = 0;
    while (count < victims_cap) {
        int i = dlog_retention_next(files, n, current, total_cap, state);
        if (i < 0) break;
        state[i] = 1;
        victims[count++] = (size_t)i;
    }
    free(state);
    return count;
}

/* ---- FORMAT.txt ---------------------------------------------------------------- */

static const char k_format_body[] =
"\n"
"This folder holds Nutshell's debug log. Each run writes its own file(s); the\n"
"names sort chronologically. Read this file first, then the newest\n"
"nutshell-*.issue*.log (an issue mark: the state and the last minutes of detail\n"
"around a moment the user flagged), then the main nutshell-*.log if you need\n"
"more.\n"
"\n"
"FILES\n"
"  nutshell-YYYYMMDD-HHMMSS-p<pid>.log           the run started at that local time, process <pid>\n"
"  nutshell-YYYYMMDD-HHMMSS-p<pid>.<n>.log       the same run after a rotation (n >= 2); it starts\n"
"                                                with a log.continue line naming the previous file\n"
"  nutshell-YYYYMMDD-HHMMSS-p<pid>.issue<n>.log  issue mark n: the in-memory recorder, oldest first,\n"
"                                                ending with the issue.* state snapshot\n"
"  nutshell-YYYYMMDD-HHMMSS-p<pid>.issue<n>.png  a screenshot of the window, only if enabled\n"
"The oldest files are deleted when the folder exceeds its size cap.\n"
"\n"
"LINE FORMAT (one event per line, LF terminated, UTF-8)\n"
"  <timestamp> <LEVEL> <category> <sid> <thread> <event> key=value key=\"quoted value\" ...\n"
"  timestamp  local time, ISO 8601, milliseconds, UTC offset: 2026-10-17T14:03:22.418+01:00\n"
"  LEVEL      ERROR, WARN, INFO, DEBUG, TRACE (padded to 5), or ISSUE (an issue mark or its snapshot)\n"
"  category   render, term, ai, ssh, shell, general\n"
"  sid        the session (tab) id such as s3, or - when the event is not about one session\n"
"  thread     ui, ssh, ai, pty, set (settings model fetch), log (the writer), ? (unregistered)\n"
"  event      a stable dotted name: paint, poll.cooldown_skip, ssh.auth.result ...\n"
"Values containing a space, a double quote, '=' or a backslash are quoted, with \\\" and \\\\\n"
"escapes inside. Bytes below 0x20 and 0x7F, and invalid UTF-8, appear as \\xNN. A line is cut\n"
"at 4 KB and then ends with truncated=1.\n"
"\n"
"REPEATS\n"
"  A line identical to the previous line of the same category and session is not written\n"
"  again; the next different line (or 5 seconds) emits\n"
"  repeat n=<count> last=<timestamp of the last one> of=<event>\n"
"  so 22,000 identical paints are one line plus a repeat line.\n"
"\n"
"LEVELS\n"
"  Each category has its own setting: Off, Errors (ERROR and WARN), Info, Debug, Trace.\n"
"  The header and every dlog.config line state what the file can contain, so a missing\n"
"  event may simply be below the level. The in-memory recorder usually holds Trace for\n"
"  everything, which is why an issue file is more detailed than the main log.\n"
"\n"
"REDACTION\n"
"  Passwords, key passphrases, private keys, the AI API key, keyboard input and what\n"
"  was pasted are never logged. A field whose name suggests a secret is written\n"
"  =[REDACTED]. Terminal text, AI commands and chat appear only when the user ticked\n"
"  'Include terminal content' (see content= in app.start); treat such a log as private.\n"
"\n";

static const char k_format_body2[] =
"CATEGORIES AND EVENTS (not exhaustive)\n"
"  general  app.start (version, pid, exe, levels, content, recorder), dlog.config, log.continue,\n"
"           log.moved, dlog.dropped (lines lost because the writer lagged), issue.mark,\n"
"           issue.window, issue.term, issue.render, issue.session, issue.ai, app.crash\n"
"  render   paint (rect, dirty_rows, full_redraw, alt, sb, tick), render.draw (rows, rows_in_rect,\n"
"           rows_covered, rows_left_dirty, cells_drawn, charH, us), invalidate (rect, why),\n"
"           poll.invalidate, poll.cooldown_skip (poll_rc, dirty_rows, critical, since_paint_ms),\n"
"           dispbuf.invalidate, scrollbar, grid.sync\n"
"  term     mode changes, resize, alt screen, term.rx (received bytes, only with content)\n"
"  ai       requests, streams, tools, approvals, the command dispatcher\n"
"  ssh      connect stages, host key, auth method and result, channel, keepalive, timeouts\n"
"  shell    local shell resolver choice, ConPTY create/close, reader thread, child exit\n"
"\n"
"HOW TO READ AN ISSUE\n"
"  1. The last issue.mark line is the moment the user pressed Ctrl+Shift+M.\n"
"  2. issue.window / issue.term / issue.render / issue.session / issue.ai are the state at that\n"
"     instant; dirty=0-72 means rows 0 to 72 were marked dirty.\n"
"  3. The lines above the mark, newest last, are the recorder: what the app did in the minutes\n"
"     before. Look for the last paint, the last poll.*, and any WARN or ERROR.\n";

size_t dlog_format_text(char *out, size_t cap, const char *version)
{
    if (!out || cap == 0) return 0;
    int n = snprintf(out, cap, "Nutshell debug log format (nutshell %s)\n%s%s",
                     (version && version[0]) ? version : "unknown", k_format_body, k_format_body2);
    if (n < 0) { out[0] = '\0'; return 0; }
    if ((size_t)n >= cap) return cap - 1;
    return (size_t)n;
}
