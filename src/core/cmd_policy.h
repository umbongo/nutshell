/* src/core/cmd_policy.h */
#ifndef NUTSHELL_CMD_POLICY_H
#define NUTSHELL_CMD_POLICY_H

/*
 * cmd_policy -- the AI session's command policy: one four-stop scale
 * (Read / Unknown / Write / Critical) carrying two markers.
 *
 *   allowed     the ceiling. A command whose worst category sits above it
 *               is BLOCKED and shows as held on the approval card.
 *   unattended  how far the session runs without asking. A command whose
 *               categories all sit at or below it is approved the moment
 *               it arrives; anything between the two markers asks first.
 *
 * The two markers are the two axes the code has always had -- "may it run
 * at all" and "may it run without asking" -- and they stay distinct: the
 * most common posture there is, "allow writes, but always show me first",
 * needs both. The invariant unattended <= allowed is enforced in the
 * setters, not checked at the gate.
 *
 * Pure, portable, no windows.h. See
 * docs/superpowers/specs/2026-09-11-status-policy-control-design.md.
 */

#include "cmd_classify.h"
#include <stddef.h>

/* The unattended marker's one extra position, below CMD_READ: nothing runs
 * without asking. This is the default, and what the old "Auto approve: off"
 * meant. `allowed` never takes it -- its lowest position is CMD_READ. */
#define POLICY_NONE (-1)

/* Stops on the scale are exactly CmdSafetyLevel 0..3. */
#define POLICY_STOP_COUNT 4

/* Which marker a gesture (or a tooltip) is about. */
enum { POLICY_BAND_ALLOWED = 0, POLICY_BAND_UNATTENDED = 1 };

typedef struct {
    int allowed;     /* CmdSafetyLevel 0..3 */
    int unattended;  /* CmdSafetyLevel 0..3, or POLICY_NONE */
} CmdPolicy;

/* {CMD_READ, POLICY_NONE}: read-only, nothing unattended. */
CmdPolicy cmd_policy_default(void);

/* Normalise anything that arrived from disk or from a sloppy caller:
 * `allowed` into 0..3, `unattended` into POLICY_NONE..allowed. NULL-safe. */
void cmd_policy_clamp(CmdPolicy *p);

/* Move the ceiling to `stop` (clamped to 0..3). Lowering it DRAGS the
 * unattended marker down with it -- tightening must never leave a higher
 * unattended marker behind. NULL-safe. */
void cmd_policy_set_allowed(CmdPolicy *p, int stop);

/* Move the unattended marker to `stop` (clamped to POLICY_NONE..allowed).
 * Never raises the ceiling: you cannot run unattended what you have not
 * first allowed. NULL-safe. */
void cmd_policy_set_unattended(CmdPolicy *p, int stop);

/* Ceiling cycle: Read -> Unknown -> Write -> Critical -> Read. NULL-safe. */
void cmd_policy_cycle_allowed(CmdPolicy *p);

/* Unattended cycle: none -> Read -> ... -> allowed -> none. NULL-safe. */
void cmd_policy_cycle_unattended(CmdPolicy *p);

/* The set of categories permitted to run unattended, as a bitmask of
 * CMD_MASK_OF(level) bits: every stop at or below `unattended`, i.e. always
 * a downward-closed (prefix) set. POLICY_NONE gives 0. */
unsigned cmd_policy_unattended_mask(CmdPolicy p);

/* 1 when a command whose worst category is `worst` is blocked outright. */
int cmd_policy_blocks(CmdPolicy p, CmdSafetyLevel worst);

/* 1 when every category in `safety_mask` (the set across the command's
 * segments, from cmd_classify_mask()) is permitted to run unattended.
 * A set test, not a threshold -- see the spec's section 3 corollary for
 * why it is equivalent to a threshold for today's stops, and why it is
 * still written this way. An empty mask never runs unattended. */
int cmd_policy_runs_unattended(CmdPolicy p, unsigned safety_mask);

/* Stable config token for a stop: "none" (POLICY_NONE), "read", "unknown",
 * "write", "critical". Out of range returns "none". */
const char *cmd_policy_stop_name(int stop);

/* UI label for a stop: "Nothing" (POLICY_NONE), "Read", "Unknown",
 * "Write", "Critical". Out of range returns "Nothing". */
const char *cmd_policy_stop_label(int stop);

/* Parse a stop token into *out. Returns 1 on success, 0 for NULL, empty or
 * an unrecognised token (leaving *out untouched). */
int cmd_policy_stop_from_name(const char *name, int *out);

/* Write the policy as "<allowed>/<unattended>", e.g. "write/read". Returns
 * the length written (0 for a NULL/zero-size buffer). */
int cmd_policy_to_token(CmdPolicy p, char *buf, size_t cap);

/* Parse "<allowed>/<unattended>" into *out, clamped. Returns 1 on success;
 * on failure returns 0 and sets *out to cmd_policy_default(). An `allowed`
 * of "none" is not legal and fails. NULL-safe on both arguments. */
int cmd_policy_from_token(const char *token, CmdPolicy *out);

/* One-line summary for a tooltip or a demo caption, e.g.
 * "allowed to Write, Read runs unattended" or
 * "allowed to Read, nothing runs unattended". Returns the length in `buf`. */
int cmd_policy_caption(CmdPolicy p, char *buf, size_t cap);

/* What clicking band `band` on cell `stop` would do, in one sentence.
 * Covers the no-op and toggle cases (clicking the stop a marker already
 * sits on) and the refusal case (asking for something unattended above the
 * ceiling). Returns the length in `buf`. */
int cmd_policy_tip(CmdPolicy p, int band, int stop, char *buf, size_t cap);

#endif /* NUTSHELL_CMD_POLICY_H */
