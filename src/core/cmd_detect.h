/* src/core/cmd_detect.h */
#ifndef NUTSHELL_CMD_DETECT_H
#define NUTSHELL_CMD_DETECT_H

#include "cmd_classify.h"
#include <stddef.h>

typedef enum {
    CMD_DETECT_NONE = 0,   /* nothing recognised; caller keeps CMD_PLATFORM_UNKNOWN */
    CMD_DETECT_PROMPT,     /* prompt shape only -- weaker */
    CMD_DETECT_BANNER      /* an unambiguous product string */
} CmdDetectConfidence;

/*
 * Detect a device platform from terminal text: a login banner and/or the
 * shell/CLI prompt on its last non-empty line.
 *
 * Banner signals (spec 3.1) are anchored: a needle only counts when it is a
 * case-insensitive prefix of some line (after trimming that line's leading
 * whitespace) -- a real banner or motd line, not a vendor's name mentioned
 * mid-sentence, in a cat'ed file, or anywhere else "elsewhere" in the
 * capture. Prompt shapes (spec 3.2) are matched against the last non-empty
 * line only. Never infers Linux, or anything else, from the absence of a
 * signal -- every platform needs its own positive match, Linux included.
 *
 * When both an anchored banner and a resolving last-line prompt are
 * present, they usually agree and the banner's higher confidence wins. If
 * they name two *different* platforms, that is a genuine conflict -- e.g.
 * the session hopped to another device and back, and an earlier banner is
 * still sitting in the scan window behind the live prompt -- and the last
 * line wins: it is by construction the most recent evidence, closer to
 * "what is this session talking to right now" than a banner that may have
 * scrolled out of relevance. This is also what makes a vendor's name
 * showing up after a Linux prompt harmless even when it does anchor a
 * line (e.g. it opens a line inside a cat'ed file): as long as the
 * capture's last line is back at a Linux prompt, Linux wins the conflict.
 * VyOS is the one platform whose own default prompt has the Linux shape
 * (Debian underneath); its banner and a Linux-shaped last line are treated
 * as agreeing, not conflicting (see platform_shares_linux_prompt_shape()).
 *
 * Two prompt shapes are shared by several device families and are
 * deliberately NOT guessed apart: "hostname#"/"hostname>" (IOS, NX-OS, ASA,
 * ProCurve, Aruba CX) and "user@host>" (Junos, PAN-OS). Both return
 * CMD_PLATFORM_UNKNOWN with CMD_DETECT_NONE. FortiOS is carved out of the
 * first group: its default prompt pads a space before the '#'.
 *
 * text/len may describe a partial capture (e.g. a banner split across two
 * poll ticks); calling this again once more text has arrived can raise
 * confidence (a CMD_DETECT_BANNER result replacing an earlier
 * CMD_DETECT_PROMPT one), and can also change the resolved platform outright
 * when a later call's last line disagrees with an earlier one -- see the
 * conflict paragraph above. A confidence of CMD_DETECT_NONE from a later,
 * shorter-lived call (nothing resolves any more) is not itself evidence of
 * anything; a caller polling this function tick by tick should not let that
 * downgrade a platform it already resolved. This function itself is
 * stateless and only looks at what it is given.
 *
 * confidence_out may be NULL. Returns CMD_PLATFORM_UNKNOWN with
 * CMD_DETECT_NONE when text is NULL, len is 0, or nothing matches.
 */
CmdPlatform cmd_detect_platform(const char *text, size_t len,
                                 CmdDetectConfidence *confidence_out);

#endif /* NUTSHELL_CMD_DETECT_H */
