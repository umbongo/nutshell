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
 * `allowed` into 0..3 and `unattended` into POLICY_NONE..allowed, then a
 * split marker (POLICY_NONE < unattended < allowed) rounds DOWN to
 * POLICY_NONE. The invariant this enforces, since 2026-10-18
 * (docs/superpowers/specs/2026-10-18-ai-auto-approve-button-design.md
 * section 2): `unattended` is POLICY_NONE or equal to `allowed` -- the UI
 * shows the policy as one level plus an Auto-approve button. NULL-safe. */
void cmd_policy_clamp(CmdPolicy *p);

/* Move the level (the ceiling) to `stop`, clamped to 0..3. The policy is
 * normalised first, so a split turns Auto-approve off. Then, if
 * Auto-approve is on, the unattended marker moves with the level; if it
 * is off it stays off. NULL-safe. */
void cmd_policy_set_allowed(CmdPolicy *p, int stop);

/* TESTS ONLY: test_chat_approval.c, test_cmd_batch.c and
 * test_cmd_policy.c exercise the gate under split policies. No UI code
 * calls this. Clamps `stop` to POLICY_NONE..allowed and does NOT
 * normalise a split. NULL-safe. */
void cmd_policy_set_unattended(CmdPolicy *p, int stop);

/* Auto-approve on (unattended = allowed) or off (POLICY_NONE). The policy
 * is clamped first. NULL-safe. */
void cmd_policy_set_auto(CmdPolicy *p, int on);

/* 1 when Auto-approve is on: after cmd_policy_clamp(), unattended ==
 * allowed. A split reads as off. */
int cmd_policy_auto_on(CmdPolicy p);

/* 1 when moving from `cur` to `next` turns on "everything runs without
 * asking" -- `next` is Critical with Auto-approve on and `cur` is not. The
 * AI panel and Settings ask for confirmation exactly then. */
int cmd_policy_needs_confirm(CmdPolicy cur, CmdPolicy next);

/* The policy packed for the AI panel's WM_AI_GET_POLICY reply (the
 * integration harness reads it): bits 0-7 the level, bit 8 (0x100) set
 * when Auto-approve is on. */
unsigned cmd_policy_status_word(CmdPolicy p);

/* `p` with Auto-approve forced off: clamped, then unattended = POLICY_NONE.
 * The ceiling is kept, so blocked rows stay blocked. A batch built for a
 * tab that is not displayed is built under this, so nothing in it runs
 * before the user has seen it (spec 2026-10-18 section 6). */
CmdPolicy cmd_policy_attended(CmdPolicy p);

/* Ceiling cycle: Read -> Unknown -> Write -> Critical -> Read. NULL-safe. */
void cmd_policy_cycle_allowed(CmdPolicy *p);

/* Unattended cycle: none -> Read -> ... -> allowed -> none. NULL-safe.
 * No UI code calls this since 2026-10-18; it keeps splits, like
 * cmd_policy_set_unattended(). */
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

/* One-letter label for a stop on the compact pill (a narrow AI panel):
 * "R", "U", "W", "C". Out of range, POLICY_NONE included, returns "". */
const char *cmd_policy_stop_short(int stop);

/* The Auto-approve button's label: "Auto-approve", or "Auto" when
 * `compact` is non-zero. */
const char *cmd_policy_auto_label(int compact);

/* Room for any tooltip below, including the NUL. ai_chat.c's tooltip
 * buffer is this size. */
#define CMD_POLICY_TIP_MAX 512

/* The question asked before everything runs unattended (Critical with
 * Auto-approve on), in the AI panel and on Save in Settings. */
#define CMD_POLICY_CONFIRM_TEXT \
    "Run every command the assistant proposes, including ones that can " \
    "destroy data, without asking?"

/* Tooltip for the cell of `level` (0..3): what the level covers and what
 * a click does. `compact` prefixes the letter ("R = Read: ..."); `auto_on`
 * appends " With Auto-approve on, commands up to this level run without
 * asking." Returns the length in `buf`; out of range gives "" and 0. */
int cmd_policy_cell_tip(int level, int compact, int auto_on, char *buf, size_t cap);

/* Tooltip for the Auto-approve button in state `on`, naming the level
 * `allowed` (clamped to 0..3). Returns the length in `buf`. */
int cmd_policy_auto_tip(int on, int allowed, char *buf, size_t cap);

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

#endif /* NUTSHELL_CMD_POLICY_H */
