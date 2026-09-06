#ifndef NUTSHELL_NS_MOTION_H
#define NUTSHELL_NS_MOTION_H

/*
 * ns_motion -- shared timing tokens and a tiny animation-progress helper,
 * Design-System Foundation task 8. See
 * docs/superpowers/specs/2026-09-07-design-system-foundation-design.md
 * section 4 ("Motion tokens").
 *
 * Every window that animates keeps one NsAnimList and one 16 ms WM_TIMER:
 * it steps the list every tick, invalidates whatever changed, and kills
 * the timer once the list is empty. Reduced-motion (SPI_GETCLIENTAREAANIMATION)
 * is read by the Win32 side and passed into every progress call so
 * animations snap straight to their end state when it is off.
 *
 * Pure C, no windows.h -- fully testable on Linux. Tick values are
 * whatever the caller's monotonic millisecond clock produces (GetTickCount()
 * on Windows); all arithmetic here is unsigned subtraction, so a tick
 * counter wrapping past its max value still produces the correct elapsed
 * time.
 */

enum { MOTION_FAST = 120, MOTION_BASE = 200, MOTION_SLOW = 320 }; /* ms */

/* Ease-out cubic: 1 - (1-t)^3. Clamps t to [0,1] before easing. */
double ns_ease(double t);

typedef struct {
    unsigned long start_tick;
    int           duration_ms;
    int           done_reported; /* internal: has ns_anim_progress already
                                   * reported done=1 for this run? */
} NsAnim;

typedef struct {
    double t;    /* eased progress, clamped to [0,1] */
    int    done; /* 1 exactly once, the first call where t reaches 1 */
} NsAnimStep;

/* Arm (or re-arm) an animation starting now, running for duration_ms. */
void ns_anim_start(NsAnim *a, unsigned long now_tick, int duration_ms);

/* Advance and read an animation's progress at now_tick.
 * reduced_motion: when non-zero, the first call after ns_anim_start
 * returns t = 1 and done = 1 immediately; later calls return t = 1 and
 * done = 0 (already reported). */
NsAnimStep ns_anim_progress(NsAnim *a, unsigned long now_tick, int reduced_motion);

#define NS_ANIM_LIST_MAX 8

typedef struct {
    NsAnim items[NS_ANIM_LIST_MAX];
    int    ids[NS_ANIM_LIST_MAX];
    int    count;
} NsAnimList;

/* Start tracking `id` for duration_ms starting at now_tick. If `id` is
 * already in the list, it is replaced (restarted) in place. Returns the
 * slot index, or -1 if the list is full and `id` is new. */
int ns_anim_list_add(NsAnimList *l, int id, unsigned long now_tick, int duration_ms);

/* Number of animations currently tracked (0..NS_ANIM_LIST_MAX). */
int ns_anim_list_active(const NsAnimList *l);

/* Advance every tracked animation to now_tick and drop any that just
 * finished. Returns how many are still active afterward -- 0 means the
 * caller's WM_TIMER can be killed. */
int ns_anim_list_step(NsAnimList *l, unsigned long now_tick, int reduced_motion);

/* Read `id`'s current progress without mutating the list (safe to call
 * after ns_anim_list_step in the same tick). Returns 1 and fills *out if
 * `id` is still tracked, 0 (out->t = 1, out->done = 0) if it already
 * finished/was never added -- callers should treat "not found" as "use
 * the final value". */
int ns_anim_list_get(const NsAnimList *l, int id, unsigned long now_tick,
                     int reduced_motion, NsAnimStep *out);

#endif /* NUTSHELL_NS_MOTION_H */
