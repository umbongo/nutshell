/* src/core/cmd_detect.c */
#include "cmd_detect.h"
#include <string.h>

/* ----- Case-insensitive helpers -----
 * Mirrors cmd_classify.c's ci_lower()/tok_eq_ci() in spirit, but operates on
 * length-delimited buffers that are not guaranteed to be NUL-terminated
 * (text/len may be a raw slice of terminal scrollback). Kept local to this
 * translation unit rather than shared, matching the rest of the codebase's
 * per-file tok_*-style helpers. */

static int ci_lower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

static int ci_char_eq(char a, char b)
{
    return ci_lower((unsigned char)a) == ci_lower((unsigned char)b);
}

/* Case-insensitive "does hay[0..hay_len) contain needle" (needle is a plain
 * NUL-terminated C string). Naive O(n*m) scan -- banner text is at most a
 * few KB (term_extract_last_n caps it), so this is plenty fast. */
static int ci_contains(const char *hay, size_t hay_len, const char *needle)
{
    size_t needle_len = strlen(needle);
    if (needle_len == 0 || needle_len > hay_len) return 0;
    for (size_t i = 0; i + needle_len <= hay_len; i++) {
        size_t j = 0;
        while (j < needle_len && ci_char_eq(hay[i + j], needle[j])) j++;
        if (j == needle_len) return 1;
    }
    return 0;
}

/* Does line[0..len) end with suffix? Delimiter characters only (<, >, (, ),
 * [, ], #, $, @, : and space) so plain memcmp is fine -- no case folding
 * needed. */
static int ends_with(const char *line, size_t len, const char *suffix)
{
    size_t slen = strlen(suffix);
    if (len < slen) return 0;
    return memcmp(line + len - slen, suffix, slen) == 0;
}

/* ----- Last non-empty line -----
 * The prompt sits on the last non-empty line of the captured text (spec
 * 3.2). Single forward pass over the buffer, splitting on '\n' and trimming
 * a trailing '\r' plus surrounding spaces/tabs from each line; the last
 * line left with content after trimming wins. Returns NULL (out_len 0) if
 * every line is blank. */
static const char *find_last_nonempty_line(const char *text, size_t len, size_t *out_len)
{
    const char *p = text;
    const char *end = text + len;
    const char *best = NULL;
    size_t best_len = 0;

    while (p <= end) {
        const char *line_start = p;
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        const char *line_end = nl ? nl : end;

        const char *trim_end = line_end;
        while (trim_end > line_start &&
               (*(trim_end - 1) == '\r' || *(trim_end - 1) == ' ' ||
                *(trim_end - 1) == '\t'))
            trim_end--;
        const char *trim_start = line_start;
        while (trim_start < trim_end &&
               (*trim_start == ' ' || *trim_start == '\t'))
            trim_start++;

        if (trim_end > trim_start) {
            best = trim_start;
            best_len = (size_t)(trim_end - trim_start);
        }

        if (!nl) break;
        p = nl + 1;
    }

    *out_len = best_len;
    return best;
}

/* ----- Banner signals (spec 3.1) ----- */

typedef struct {
    const char  *needle;
    CmdPlatform  platform;
} BannerSignal;

static const BannerSignal banner_signals[] = {
    /* ----- Banner: NX-OS ----- */
    { "Cisco Nexus Operating System", CMD_PLATFORM_CISCO_NXOS },
    { "NX-OS",                        CMD_PLATFORM_CISCO_NXOS },
    /* ----- Banner: ASA ----- */
    { "Cisco Adaptive Security Appliance", CMD_PLATFORM_CISCO_ASA },
    /* ----- Banner: IOS ----- */
    { "Cisco IOS Software",                    CMD_PLATFORM_CISCO_IOS },
    { "IOS-XE Software",                       CMD_PLATFORM_CISCO_IOS },
    { "Cisco Internetwork Operating System",   CMD_PLATFORM_CISCO_IOS },
    /* ----- Banner: Aruba OS-CX ----- */
    { "ArubaOS-CX", CMD_PLATFORM_ARUBA_CX },
    { "AOS-CX",     CMD_PLATFORM_ARUBA_CX },
    /* ----- Banner: ArubaOS (controller) ----- */
    { "ArubaOS (MODEL:",       CMD_PLATFORM_ARUBA_OS },
    { "Aruba Operating System", CMD_PLATFORM_ARUBA_OS },
    /* ----- Banner: ProCurve ----- */
    { "ProCurve",  CMD_PLATFORM_HP_PROCURVE },
    { "ProVision", CMD_PLATFORM_HP_PROCURVE },
    { "HP J9",     CMD_PLATFORM_HP_PROCURVE },
    { "Aruba JL",  CMD_PLATFORM_HP_PROCURVE },
    /* ----- Banner: Comware ----- */
    { "Comware Software", CMD_PLATFORM_HP_COMWARE },
    { "H3C Comware",      CMD_PLATFORM_HP_COMWARE },
    { "HPE Comware",      CMD_PLATFORM_HP_COMWARE },
    /* ----- Banner: PAN-OS ----- */
    { "PAN-OS",             CMD_PLATFORM_PANOS },
    { "Palo Alto Networks", CMD_PLATFORM_PANOS },
    /* ----- Banner: Junos ----- */
    { "JUNOS ",           CMD_PLATFORM_JUNOS },
    { "Junos OS",         CMD_PLATFORM_JUNOS },
    { "Juniper Networks", CMD_PLATFORM_JUNOS },
    /* ----- Banner: FortiOS ----- */
    { "FortiGate", CMD_PLATFORM_FORTIOS },
    { "FortiOS",   CMD_PLATFORM_FORTIOS },
    { "Fortinet",  CMD_PLATFORM_FORTIOS },
    /* ----- Banner: VyOS ----- */
    { "Welcome to VyOS", CMD_PLATFORM_VYOS },
    { "VyOS ",           CMD_PLATFORM_VYOS },
    /* ----- Banner: RouterOS ----- */
    { "MikroTik RouterOS", CMD_PLATFORM_MIKROTIK },
    { "RouterOS ",         CMD_PLATFORM_MIKROTIK },
    /* ----- Banner: Linux ----- */
    { "Welcome to Ubuntu",        CMD_PLATFORM_LINUX },
    { "Debian GNU/Linux",         CMD_PLATFORM_LINUX },
    { "Red Hat Enterprise Linux", CMD_PLATFORM_LINUX },
    { "CentOS",                   CMD_PLATFORM_LINUX },
    { "Rocky Linux",              CMD_PLATFORM_LINUX },
    { "AlmaLinux",                CMD_PLATFORM_LINUX },
    { "SUSE Linux",               CMD_PLATFORM_LINUX },
    { "Alpine Linux",             CMD_PLATFORM_LINUX },
    { "Arch Linux",               CMD_PLATFORM_LINUX },
    { "Linux ",                   CMD_PLATFORM_LINUX }, /* uname line of a motd */
};

static const size_t banner_signal_count =
    sizeof(banner_signals) / sizeof(banner_signals[0]);

/* ----- Prompt shapes (spec 3.2) -----
 * Matched against the last non-empty line only. Checked in an order where
 * each specific shape is ruled out before the generic ones below it, so a
 * RouterOS "[admin@name] >" line is never mistaken for the bare "user@host>"
 * shape, etc. Everything not claimed here -- including both ambiguous
 * shapes, "user@host>" (Junos/PAN-OS) and bare "hostname#"/"hostname>" (six
 * families) -- falls through to CMD_PLATFORM_UNKNOWN, which is exactly the
 * "resolve to NONE" the spec calls for: the caller sees no difference
 * between an ambiguous shape and no shape at all. */
static CmdPlatform match_prompt_shape(const char *line, size_t len)
{
    if (len == 0) return CMD_PLATFORM_UNKNOWN;

    /* <hostname> -- unique to Comware */
    if (line[0] == '<' && line[len - 1] == '>')
        return CMD_PLATFORM_HP_COMWARE;

    /* (hostname) # or (hostname) *# -- the parenthesised form is unique to
     * ArubaOS */
    if (line[0] == '(' &&
        (ends_with(line, len, ") #") || ends_with(line, len, ") *#")))
        return CMD_PLATFORM_ARUBA_OS;

    /* [admin@name] > -- unique to RouterOS */
    if (line[0] == '[' && ends_with(line, len, "] >"))
        return CMD_PLATFORM_MIKROTIK;

    /* user@host:~$ / user@host:/path# / any line ending '$' or '#' with a
     * ':' before it -- Linux. The ':' is what distinguishes this from the
     * ambiguous Junos/PAN-OS "user@host>" shape and from the ambiguous bare
     * "hostname#"/"hostname>" shape, neither of which contains a ':'. */
    {
        char last = line[len - 1];
        if (last == '$' || last == '#') {
            for (size_t i = 0; i + 1 < len; i++) {
                if (line[i] == ':')
                    return CMD_PLATFORM_LINUX;
            }
        }
    }

    return CMD_PLATFORM_UNKNOWN;
}

/* ----- Top-level detection ----- */

CmdPlatform cmd_detect_platform(const char *text, size_t len,
                                 CmdDetectConfidence *confidence_out)
{
    if (confidence_out) *confidence_out = CMD_DETECT_NONE;

    if (!text || len == 0)
        return CMD_PLATFORM_UNKNOWN;

    for (size_t i = 0; i < banner_signal_count; i++) {
        if (ci_contains(text, len, banner_signals[i].needle)) {
            if (confidence_out) *confidence_out = CMD_DETECT_BANNER;
            return banner_signals[i].platform;
        }
    }

    size_t line_len;
    const char *line = find_last_nonempty_line(text, len, &line_len);
    if (!line)
        return CMD_PLATFORM_UNKNOWN;

    CmdPlatform prompt_platform = match_prompt_shape(line, line_len);
    if (prompt_platform != CMD_PLATFORM_UNKNOWN) {
        if (confidence_out) *confidence_out = CMD_DETECT_PROMPT;
        return prompt_platform;
    }

    return CMD_PLATFORM_UNKNOWN;
}
