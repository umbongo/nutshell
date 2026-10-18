/* src/core/cmd_policy.c */
#include "cmd_policy.h"
#include <stdio.h>
#include <string.h>

/* Stop tables, indexed by stop + 1 so POLICY_NONE lands on entry 0. */
static const char *const k_stop_names[POLICY_STOP_COUNT + 1] = {
    "none", "read", "unknown", "write", "critical"
};
static const char *const k_stop_labels[POLICY_STOP_COUNT + 1] = {
    "Nothing", "Read", "Unknown", "Write", "Critical"
};

static int stop_in_range(int stop)
{
    return stop >= POLICY_NONE && stop < POLICY_STOP_COUNT;
}

/* Range only: `allowed` into 0..3, `unattended` into POLICY_NONE..allowed.
 * Keeps a split. The gate and the test-only setters use this so a split
 * policy a test hands them is judged exactly as given. */
static void clamp_range(CmdPolicy *p)
{
    if (p->allowed < CMD_READ) p->allowed = CMD_READ;
    if (p->allowed > POLICY_STOP_COUNT - 1) p->allowed = POLICY_STOP_COUNT - 1;
    if (p->unattended < POLICY_NONE) p->unattended = POLICY_NONE;
    if (p->unattended > p->allowed) p->unattended = p->allowed;
}

CmdPolicy cmd_policy_default(void)
{
    CmdPolicy p;
    p.allowed = CMD_READ;
    p.unattended = POLICY_NONE;
    return p;
}

void cmd_policy_clamp(CmdPolicy *p)
{
    if (!p) return;
    clamp_range(p);
    if (p->unattended != POLICY_NONE && p->unattended != p->allowed)
        p->unattended = POLICY_NONE;     /* a split rounds down: button off */
}

void cmd_policy_set_allowed(CmdPolicy *p, int stop)
{
    if (!p) return;
    cmd_policy_clamp(p);
    int on = (p->unattended != POLICY_NONE);
    p->allowed = stop;
    p->unattended = POLICY_NONE;
    clamp_range(p);                      /* clamps the level into 0..3 */
    if (on) p->unattended = p->allowed;
}

void cmd_policy_set_unattended(CmdPolicy *p, int stop)
{
    if (!p) return;
    p->unattended = stop;
    clamp_range(p);
}

void cmd_policy_set_auto(CmdPolicy *p, int on)
{
    if (!p) return;
    cmd_policy_clamp(p);
    p->unattended = on ? p->allowed : POLICY_NONE;
}

int cmd_policy_auto_on(CmdPolicy p)
{
    cmd_policy_clamp(&p);
    return p.unattended != POLICY_NONE;
}

int cmd_policy_needs_confirm(CmdPolicy cur, CmdPolicy next)
{
    cmd_policy_clamp(&cur);
    cmd_policy_clamp(&next);
    int next_all = next.allowed == CMD_CRITICAL && next.unattended == CMD_CRITICAL;
    int cur_all  = cur.allowed == CMD_CRITICAL && cur.unattended == CMD_CRITICAL;
    return next_all && !cur_all;
}

unsigned cmd_policy_status_word(CmdPolicy p)
{
    cmd_policy_clamp(&p);
    return (unsigned)p.allowed | (p.unattended != POLICY_NONE ? 0x100u : 0u);
}

CmdPolicy cmd_policy_attended(CmdPolicy p)
{
    cmd_policy_clamp(&p);
    p.unattended = POLICY_NONE;
    return p;
}

void cmd_policy_cycle_allowed(CmdPolicy *p)
{
    if (!p) return;
    cmd_policy_clamp(p);
    int next = p->allowed + 1;
    if (next > POLICY_STOP_COUNT - 1) next = CMD_READ;
    cmd_policy_set_allowed(p, next);
}

void cmd_policy_cycle_unattended(CmdPolicy *p)
{
    if (!p) return;
    clamp_range(p);
    int next = p->unattended + 1;
    if (next > p->allowed) next = POLICY_NONE;
    cmd_policy_set_unattended(p, next);
}

unsigned cmd_policy_unattended_mask(CmdPolicy p)
{
    clamp_range(&p);
    if (p.unattended < CMD_READ) return 0u;
    return (1u << (unsigned)(p.unattended + 1)) - 1u;
}

int cmd_policy_blocks(CmdPolicy p, CmdSafetyLevel worst)
{
    clamp_range(&p);
    return (int)worst > p.allowed;
}

int cmd_policy_runs_unattended(CmdPolicy p, unsigned safety_mask)
{
    if (safety_mask == 0u) return 0;
    return (safety_mask & ~cmd_policy_unattended_mask(p)) == 0u;
}

const char *cmd_policy_stop_name(int stop)
{
    if (!stop_in_range(stop)) stop = POLICY_NONE;
    return k_stop_names[stop + 1];
}

const char *cmd_policy_stop_label(int stop)
{
    if (!stop_in_range(stop)) stop = POLICY_NONE;
    return k_stop_labels[stop + 1];
}

int cmd_policy_stop_from_name(const char *name, int *out)
{
    if (!name || !*name) return 0;
    for (int i = 0; i <= POLICY_STOP_COUNT; i++) {
        if (strcmp(name, k_stop_names[i]) == 0) {
            if (out) *out = i - 1;
            return 1;
        }
    }
    return 0;
}

int cmd_policy_to_token(CmdPolicy p, char *buf, size_t cap)
{
    if (!buf || cap == 0) return 0;
    cmd_policy_clamp(&p);
    snprintf(buf, cap, "%s/%s", cmd_policy_stop_name(p.allowed),
             cmd_policy_stop_name(p.unattended));
    return (int)strlen(buf);
}

int cmd_policy_from_token(const char *token, CmdPolicy *out)
{
    CmdPolicy p = cmd_policy_default();
    if (!token) { if (out) *out = p; return 0; }

    const char *slash = strchr(token, '/');
    if (!slash) { if (out) *out = p; return 0; }

    char left[16];
    size_t left_len = (size_t)(slash - token);
    if (left_len == 0 || left_len >= sizeof(left)) { if (out) *out = p; return 0; }
    memcpy(left, token, left_len);
    left[left_len] = '\0';

    int allowed = 0, unattended = 0;
    if (!cmd_policy_stop_from_name(left, &allowed) ||
        !cmd_policy_stop_from_name(slash + 1, &unattended) ||
        allowed == POLICY_NONE) {          /* the ceiling has no "none" position */
        if (out) *out = p;
        return 0;
    }

    p.allowed = allowed;
    p.unattended = unattended;
    cmd_policy_clamp(&p);
    if (out) *out = p;
    return 1;
}

int cmd_policy_caption(CmdPolicy p, char *buf, size_t cap)
{
    if (!buf || cap == 0) return 0;
    clamp_range(&p);
    if (p.unattended == POLICY_NONE) {
        snprintf(buf, cap, "allowed to %s, nothing runs unattended",
                 cmd_policy_stop_label(p.allowed));
    } else {
        snprintf(buf, cap, "allowed to %s, %s runs unattended",
                 cmd_policy_stop_label(p.allowed),
                 cmd_policy_stop_label(p.unattended));
    }
    return (int)strlen(buf);
}

int cmd_policy_tip(CmdPolicy p, int band, int stop, char *buf, size_t cap)
{
    if (!buf || cap == 0) return 0;
    cmd_policy_clamp(&p);
    if (stop < CMD_READ || stop > POLICY_STOP_COUNT - 1) {
        buf[0] = '\0';
        return 0;
    }

    const char *label = cmd_policy_stop_label(stop);

    if (band == POLICY_BAND_ALLOWED) {
        if (stop > p.allowed)
            snprintf(buf, cap, "Allow %s commands - they still ask first.", label);
        else if (stop < p.allowed)
            snprintf(buf, cap, "Block anything above %s.", label);
        else
            snprintf(buf, cap, "Commands above %s are blocked.", label);
    } else {
        if (stop > p.allowed)
            snprintf(buf, cap, "Allow %s first to run it unattended.", label);
        else if (stop == p.unattended)
            snprintf(buf, cap, "Stop running anything unattended.");
        else
            snprintf(buf, cap, "Run %s and below without asking.", label);
    }
    return (int)strlen(buf);
}
