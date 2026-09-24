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
 * line, and only consulted when no banner matched -- a banner decides
 * outright once found, never mind what the last line happens to say. Never
 * infers Linux, or anything else, from the absence of a signal -- every
 * platform needs its own positive match, Linux included.
 *
 * Two prompt shapes are shared by several device families and are
 * deliberately NOT guessed apart: "hostname#"/"hostname>" (IOS, NX-OS, ASA,
 * ProCurve, Aruba CX, FortiOS) and "user@host>" (Junos, PAN-OS). Both return
 * CMD_PLATFORM_UNKNOWN with CMD_DETECT_NONE (2026-09-24: FortiOS's padded
 * "hostname # " form was briefly carved out of the first group, but that
 * also matched ordinary Linux root prompts such as "/ #" and "host ~ #" --
 * removed again, FortiOS is back in the ambiguous group).
 *
 * text/len may describe a partial capture (e.g. a banner split across two
 * poll ticks); calling this again once more text has arrived can only raise
 * confidence, never lower it -- a CMD_DETECT_BANNER result may replace a
 * CMD_DETECT_PROMPT one, but a prompt result never downgrades a banner
 * result already found in an earlier, shorter call. The caller is
 * responsible for keeping the stronger of two results across calls; this
 * function itself is stateless and only looks at what it is given.
 *
 * This function alone only ever *resolves* a platform (spec 3's initial
 * scan window); it never reconsiders one it already returned. A session
 * that keeps watching after it has resolved -- CLAUDE.md's invariant that
 * host output must never make a session's ruleset looser than it was --
 * needs the two functions below instead, not another call to this one.
 *
 * confidence_out may be NULL. Returns CMD_PLATFORM_UNKNOWN with
 * CMD_DETECT_NONE when text is NULL, len is 0, or nothing matches.
 */
CmdPlatform cmd_detect_platform(const char *text, size_t len,
                                 CmdDetectConfidence *confidence_out);

/*
 * Cheap, post-resolution contradiction check for a session that has already
 * resolved `resolved` (via cmd_detect_platform() above, or a locked local
 * shell). Examines only the last non-empty line of `text` -- no banner scan,
 * unlike cmd_detect_platform() -- so it is safe to run every poll tick.
 *
 * Returns 1 only when the last line's prompt shape is unambiguous (see
 * cmd_detect_platform()'s prompt-shape doc) and names a platform other than
 * `resolved`. VyOS's own default prompt shares Linux's shape (Debian
 * underneath), so a Linux-shaped last line against a `resolved` of
 * CMD_PLATFORM_VYOS is agreement, not a contradiction, and returns 0 -- same
 * carve-out cmd_detect_platform() would apply within one call.
 *
 * Returns 0 (no contradiction) when text is NULL/empty, every line is
 * blank, the last line's shape is ambiguous, or it agrees with `resolved`.
 * Deciding what to *do* about a contradiction is the caller's job -- see
 * cmd_detect_transition_allowed().
 */
int cmd_detect_last_line_contradicts(const char *text, size_t len,
                                      CmdPlatform resolved);

/*
 * Is it allowed to move a session's resolved platform from `from` to `to`,
 * based on output the host printed after the session already resolved?
 *
 * The one invariant continued detection must never violate: host output can
 * make a session's ruleset stricter, never looser. `to` is allowed only when
 * it equals `from` (a no-op) or is CMD_PLATFORM_UNKNOWN -- the unresolved
 * ruleset, which classify_unknown_segment() (cmd_classify.c) builds as the
 * Linux ruleset plus an overlay that only ever *raises* the result, so it is
 * always at least as strict as any resolved platform's ruleset, Linux
 * included. Any other `to` is refused: once resolved, a contradiction may
 * only demote a session to CMD_PLATFORM_UNKNOWN, never sideways to a
 * different specific platform: and once at CMD_PLATFORM_UNKNOWN, no further
 * move is ever allowed again for that session.
 *
 * Pure and stateless -- the caller (window.c's poll-tick handler) owns the
 * session state this decides about.
 */
int cmd_detect_transition_allowed(CmdPlatform from, CmdPlatform to);

#endif /* NUTSHELL_CMD_DETECT_H */
