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
 * Case-insensitive substring search over the whole text. Banner signals
 * (spec 3.1) are checked first and win; prompt shapes (spec 3.2) are only
 * consulted when no banner matched. Never infers Linux, or anything else,
 * from the absence of a signal -- every platform needs its own positive
 * match, Linux included.
 *
 * Two prompt shapes are shared by several device families and are
 * deliberately NOT guessed apart: "hostname#"/"hostname>" (IOS, NX-OS, ASA,
 * ProCurve, Aruba CX, FortiOS) and "user@host>" (Junos, PAN-OS). Both
 * return CMD_PLATFORM_UNKNOWN with CMD_DETECT_NONE.
 *
 * text/len may describe a partial capture (e.g. a banner split across two
 * poll ticks); calling this again once more text has arrived can only
 * raise confidence, never lower it -- a CMD_DETECT_BANNER result may
 * replace a CMD_DETECT_PROMPT one, but a prompt result never downgrades a
 * banner result already found in an earlier, shorter call. The caller is
 * responsible for keeping the stronger of two results across calls; this
 * function itself is stateless and only looks at what it is given.
 *
 * confidence_out may be NULL. Returns CMD_PLATFORM_UNKNOWN with
 * CMD_DETECT_NONE when text is NULL, len is 0, or nothing matches.
 */
CmdPlatform cmd_detect_platform(const char *text, size_t len,
                                 CmdDetectConfidence *confidence_out);

#endif /* NUTSHELL_CMD_DETECT_H */
