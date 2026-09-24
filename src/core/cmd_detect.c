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

/* ----- Banner signals (spec 3.1), anchored -----
 * A real login banner or motd puts its identifying product string at the
 * very start of a line -- "Cisco IOS Software, ...", "Welcome to Ubuntu
 * ...", "Last login: ...". A vendor's name showing up mid-sentence --
 * someone's motd blurb ("reaches the Cisco IOS core switches"), a file the
 * user cats, ordinary scrollback -- never does. Anchoring the search to
 * "case-insensitive prefix of some (leading-whitespace-trimmed) line"
 * throws out that whole class of false positive without needing to name
 * every possible sentence shape: it is what "the vendor's actual banner
 * format" cashes out to mechanically. */

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
    { "Last login:",              CMD_PLATFORM_LINUX }, /* sshd/login's own line */
    { "Linux ",                   CMD_PLATFORM_LINUX }, /* uname line of a motd */
};

static const size_t banner_signal_count =
    sizeof(banner_signals) / sizeof(banner_signals[0]);

/* True if `needle` is a case-insensitive prefix of some line in
 * hay[0..hay_len), once that line's leading whitespace is trimmed. This is
 * the anchoring check described above the table: it accepts a genuine
 * banner/motd line and rejects the same words sitting mid-sentence. */
static int banner_anchor_match(const char *hay, size_t hay_len, const char *needle)
{
    size_t needle_len = strlen(needle);
    if (needle_len == 0 || needle_len > hay_len) return 0;

    const char *p = hay;
    const char *end = hay + hay_len;

    while (p <= end) {
        const char *line_start = p;
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        const char *line_end = nl ? nl : end;

        const char *trim_start = line_start;
        while (trim_start < line_end &&
               (*trim_start == ' ' || *trim_start == '\t' || *trim_start == '\r'))
            trim_start++;

        size_t avail = (size_t)(line_end - trim_start);
        if (avail >= needle_len) {
            size_t j = 0;
            while (j < needle_len && ci_char_eq(trim_start[j], needle[j])) j++;
            if (j == needle_len) return 1;
        }

        if (!nl) break;
        p = nl + 1;
    }
    return 0;
}

/* ----- Prompt shapes (spec 3.2) -----
 * Matched against the last non-empty line only. Checked in an order where
 * each specific shape is ruled out before the generic ones below it, so a
 * RouterOS "[admin@name] >" line is never mistaken for the bare "user@host>"
 * shape, etc. Everything not claimed here -- including both ambiguous
 * shapes, "user@host>" (Junos/PAN-OS) and bare "hostname#"/"hostname>" (six
 * families) -- falls through to CMD_PLATFORM_UNKNOWN, which is exactly the
 * "resolve to NONE" the spec calls for: the caller sees no difference
 * between an ambiguous shape and no shape at all.
 *
 * "hostname#"/"hostname>" and "user@host>" stay deliberately unresolved:
 * IOS/NX-OS/ASA/ProCurve/Aruba-CX/FortiOS genuinely share the first shape,
 * and Junos/PAN-OS genuinely share the second, so picking one by shape
 * alone would just be a guess (see the ambiguous-shape tests). FortiOS is
 * the one exception worth carving out of the first group: its default
 * prompt pads a space before the '#' ("hostname # "), which the other five
 * families do not conventionally do -- that survives trailing-whitespace
 * trimming as a real, if narrow, distinguishing feature. */
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

        /* "hostname # " (trimmed to "hostname #") -- FortiOS's padded-hash
         * convention, checked only once the colon/Linux case above has had
         * its shot, so a hypothetical "user@host: #" (colon present) still
         * reads as Linux, never FortiOS. */
        if (last == '#' && len >= 2 && line[len - 2] == ' ')
            return CMD_PLATFORM_FORTIOS;
    }

    return CMD_PLATFORM_UNKNOWN;
}

/* Platforms whose default interactive prompt is the same
 * "user@host:path$/#" shape Linux uses, so a match_prompt_shape() result of
 * CMD_PLATFORM_LINUX does not, by itself, contradict an anchored banner
 * naming one of these. VyOS is Debian underneath and keeps Debian's default
 * bash prompt; nothing else in the table shares that convention. */
static int platform_shares_linux_prompt_shape(CmdPlatform platform)
{
    return platform == CMD_PLATFORM_VYOS;
}

/* ----- Top-level detection ----- */

CmdPlatform cmd_detect_platform(const char *text, size_t len,
                                 CmdDetectConfidence *confidence_out)
{
    if (confidence_out) *confidence_out = CMD_DETECT_NONE;

    if (!text || len == 0)
        return CMD_PLATFORM_UNKNOWN;

    CmdPlatform banner_platform = CMD_PLATFORM_UNKNOWN;
    int banner_found = 0;
    for (size_t i = 0; i < banner_signal_count; i++) {
        if (banner_anchor_match(text, len, banner_signals[i].needle)) {
            banner_platform = banner_signals[i].platform;
            banner_found = 1;
            break;
        }
    }

    size_t line_len;
    const char *line = find_last_nonempty_line(text, len, &line_len);
    CmdPlatform prompt_platform = line ? match_prompt_shape(line, line_len)
                                        : CMD_PLATFORM_UNKNOWN;

    if (banner_found) {
        if (prompt_platform == CMD_PLATFORM_UNKNOWN ||
            prompt_platform == banner_platform ||
            (prompt_platform == CMD_PLATFORM_LINUX &&
             platform_shares_linux_prompt_shape(banner_platform))) {
            /* No prompt evidence to disagree with (ambiguous or absent),
             * or the two sources agree: the banner is the stronger of the
             * two and wins, as before. */
            if (confidence_out) *confidence_out = CMD_DETECT_BANNER;
            return banner_platform;
        }

        /* Genuine conflict: an anchored banner names one platform, but the
         * *last* line of the capture -- the live prompt, right now --
         * unambiguously names a different one. This is the reconciliation
         * spec item 3 calls for: rather than trusting a banner that may
         * have scrolled out of relevance (the session hopped to another
         * device and back, or an earlier device's banner is still sitting
         * in the scan window behind it), prefer whichever evidence is
         * closest to the end of the capture. The last line is by
         * definition the most recent, so this always resolves to it. */
        if (confidence_out) *confidence_out = CMD_DETECT_PROMPT;
        return prompt_platform;
    }

    if (prompt_platform != CMD_PLATFORM_UNKNOWN) {
        if (confidence_out) *confidence_out = CMD_DETECT_PROMPT;
        return prompt_platform;
    }

    return CMD_PLATFORM_UNKNOWN;
}
