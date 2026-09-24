/* src/core/cmd_detect.h */
#ifndef NUTSHELL_CMD_DETECT_H
#define NUTSHELL_CMD_DETECT_H

#include "cmd_classify.h"
#include <stddef.h>

typedef enum {
    CMD_DETECT_NONE = 0,   /* nothing recognised; caller keeps CMD_PLATFORM_UNKNOWN */
    CMD_DETECT_PROMPT,     /* prompt shape only -- weaker */
    CMD_DETECT_BANNER,     /* an unambiguous product string */
    CMD_DETECT_CONFLICT    /* an anchored banner and an unambiguous last-line
                            * prompt shape name two different platforms: the
                            * capture resolves nothing (CMD_PLATFORM_UNKNOWN),
                            * and is itself evidence that something in the
                            * session is not what it looks like. */
} CmdDetectConfidence;

/*
 * The invariant every function below serves (CLAUDE.md): host output must
 * never make a session's classification looser than it was. A session holds
 * two pieces of state: its platform (CMD_PLATFORM_UNKNOWN while unresolved)
 * and a sticky `contradicted` flag. Host output may move an unresolved
 * session to a platform, and may set `contradicted`; nothing host output
 * does ever replaces a platform once set, and nothing ever clears the flag.
 * A contradicted session is classified with cmd_classify_session(), the
 * worse of its platform's ruleset and CMD_PLATFORM_UNKNOWN's -- never
 * demoted to CMD_PLATFORM_UNKNOWN alone, which is looser than every device
 * ruleset somewhere.
 */

/*
 * Detect a device platform from terminal text: a login banner and/or the
 * shell/CLI prompt on its last non-empty line.
 *
 * Banner signals (spec 3.1) are anchored: a needle only counts when it is a
 * case-insensitive prefix of some line (after trimming that line's leading
 * whitespace) -- a real banner or motd line, not a vendor's name mentioned
 * mid-sentence, in a cat'ed file, or anywhere else "elsewhere" in the
 * capture. Prompt shapes (spec 3.2) are matched against the last non-empty
 * line. A banner decides unless the last line's prompt shape is unambiguous
 * and names a different platform: then the two disagree, and the result is
 * CMD_PLATFORM_UNKNOWN with CMD_DETECT_CONFLICT -- neither is trusted (a
 * cat'ed file line can look like a banner; a hop to another host changes
 * the prompt). An ambiguous or absent last-line shape, or an agreeing one
 * (VyOS's Linux-shaped prompt included), leaves the banner's result. Never
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
 * Stateless: it looks only at what it is given. What a result does to a
 * session is cmd_detect_scan_step()'s job.
 *
 * confidence_out may be NULL. Returns CMD_PLATFORM_UNKNOWN with
 * CMD_DETECT_NONE when text is NULL, len is 0, or nothing matches.
 */
CmdPlatform cmd_detect_platform(const char *text, size_t len,
                                 CmdDetectConfidence *confidence_out);

/*
 * Cheap, post-resolution contradiction check for a session that has already
 * resolved `resolved`. Examines only the last non-empty line of `text` -- no
 * banner scan, unlike cmd_detect_platform() -- so it is safe to run every
 * poll tick.
 *
 * Returns 1 only when the last line's prompt shape is unambiguous (see
 * cmd_detect_platform()'s prompt-shape doc) and names a platform other than
 * `resolved`. VyOS's own default prompt shares Linux's shape (Debian
 * underneath), so a Linux-shaped last line against a `resolved` of
 * CMD_PLATFORM_VYOS is agreement, not a contradiction, and returns 0.
 *
 * Returns 0 (no contradiction) when text is NULL/empty, every line is
 * blank, the last line's shape is ambiguous, or it agrees with `resolved`.
 */
int cmd_detect_last_line_contradicts(const char *text, size_t len,
                                      CmdPlatform resolved);

/*
 * One read of the initial scan window (window.c: up to
 * PLATFORM_DETECT_MAX_TICKS data-bearing reads of the last rows). Runs
 * cmd_detect_platform() on text/len and applies the result to the session
 * state *platform / *contradicted:
 *
 *   CMD_DETECT_NONE                 nothing changes;
 *   CMD_DETECT_CONFLICT             *contradicted = 1, platform unchanged;
 *   a platform, session unresolved  *platform = it (the only way a platform
 *                                   is ever set from host output);
 *   the session's own platform      nothing changes (VyOS's Linux-shaped
 *                                   prompt counts as its own);
 *   a different platform            *contradicted = 1, platform unchanged
 *                                   -- a resolved session never moves
 *                                   sideways, however strong the evidence.
 *
 * *contradicted is never cleared. Returns the detection confidence, so the
 * caller can end the scan window on CMD_DETECT_BANNER. Does nothing (and
 * returns CMD_DETECT_NONE) if either pointer is NULL.
 */
CmdDetectConfidence cmd_detect_scan_step(const char *text, size_t len,
                                         CmdPlatform *platform,
                                         int *contradicted);

/*
 * One poll tick after the scan window closed: text/len should be just the
 * last non-empty terminal row. Sets *contradicted = 1 when
 * cmd_detect_last_line_contradicts() says the row disagrees with a resolved
 * `platform`; never touches the platform, never clears the flag. An
 * unresolved session (CMD_PLATFORM_UNKNOWN) is left alone -- there is
 * nothing to contradict. NULL `contradicted` is a no-op.
 */
void cmd_detect_watch_step(const char *text, size_t len,
                           CmdPlatform platform, int *contradicted);

#endif /* NUTSHELL_CMD_DETECT_H */
