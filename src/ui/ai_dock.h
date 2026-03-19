#ifndef NUTSHELL_AI_DOCK_H
#define NUTSHELL_AI_DOCK_H

/*
 * Pure-logic helpers for the docked AI Assist panel.
 * No Win32 dependency — fully testable on Linux.
 */

#define AI_DOCK_DEFAULT_PCT   30   /* Default panel width as % of main window */
#define AI_DOCK_MIN_PCT       10   /* Minimum panel width % */
#define AI_DOCK_MAX_PCT       75   /* Maximum panel width % */
#define AI_DOCK_SPLITTER_W     6   /* Visual splitter gap width in px */
#define AI_DOCK_SPLITTER_HIT  10  /* Hit zone width for drag detection */
#define AI_DOCK_ANIM_MS      200   /* Slide animation duration in ms */

/* Check if AI Assist can be opened: requires an active SSH session.
 * session_active: non-zero if a session exists.
 * channel_active: non-zero if the session has an open channel.
 * Returns non-zero if AI Assist should be allowed to open. */
static inline int ai_dock_can_open(int session_active, int channel_active)
{
    return session_active && channel_active;
}

/* Calculate panel width in pixels from a percentage of client width.
 * Clamps to [min_pct, max_pct] range. Returns 0 if client_w <= 0. */
static inline int ai_dock_pct_to_px(int client_w, int pct,
                                     int min_pct, int max_pct)
{
    if (client_w <= 0) return 0;
    if (pct < min_pct) pct = min_pct;
    if (pct > max_pct) pct = max_pct;
    return client_w * pct / 100;
}

/* Clamp a requested panel width (px) to the allowed range.
 * Returns clamped width in pixels. */
static inline int ai_dock_clamp_width(int requested_w, int client_w,
                                       int min_pct, int max_pct)
{
    if (client_w <= 0) return 0;
    int min_w = client_w * min_pct / 100;
    int max_w = client_w * max_pct / 100;
    if (requested_w < min_w) return min_w;
    if (requested_w > max_w) return max_w;
    return requested_w;
}

/* Calculate terminal area width given all the chrome.
 * panel_w is 0 when the AI panel is closed or floating. */
static inline int ai_dock_terminal_width(int client_w, int panel_w,
                                          int scrollbar_w, int left_margin)
{
    int w = client_w - panel_w - scrollbar_w - left_margin;
    return w > 0 ? w : 1;
}

/* Ease-in curve (quadratic): slow start, fast finish.
 * t in [0.0, 1.0], returns eased value in [0.0, 1.0]. */
static inline double ai_dock_ease_in(double t)
{
    if (t <= 0.0) return 0.0;
    if (t >= 1.0) return 1.0;
    return t * t;
}

/* Calculate current panel width during animation.
 * t is raw progress [0.0, 1.0], target_w is the final width. */
static inline int ai_dock_anim_width(int target_w, double t)
{
    double ease = ai_dock_ease_in(t);
    return (int)(target_w * ease);
}

/* Eased linear interpolation from from_w to to_w.
 * t is raw progress [0.0, 1.0].  Works for both open (0→target)
 * and close (current→0) animations. */
static inline int ai_dock_anim_lerp(int from_w, int to_w, double t)
{
    double ease = ai_dock_ease_in(t);
    return from_w + (int)((to_w - from_w) * ease);
}

/* Test if mouse x-coordinate hits the splitter zone.
 * splitter_x is the left edge of the docked panel. */
static inline int ai_dock_splitter_hit(int mouse_x, int splitter_x,
                                        int splitter_w, int mouse_y,
                                        int top_y)
{
    if (mouse_y < top_y) return 0;
    int half = splitter_w / 2;
    return (mouse_x >= splitter_x - half && mouse_x <= splitter_x + half);
}

/* Compute display area dimensions for the AI chat panel layout.
 * Returns width and height clamped to a minimum of 1 so that child
 * controls are never created/moved with negative dimensions. */
static inline void ai_dock_chat_layout(int client_w, int client_h,
                                        int top_y, int input_h,
                                        int approve_h, int margin,
                                        int scrollbar_w,
                                        int *out_disp_w, int *out_disp_h)
{
    int dw = client_w - margin * 2 - scrollbar_w;
    int dh = client_h - input_h - approve_h - top_y - margin * 2;
    *out_disp_w = dw > 1 ? dw : 1;
    *out_disp_h = dh > 1 ? dh : 1;
}

/* Calculate initial terminal columns for a new session.
 * panel_visible: non-zero if the docked AI panel is showing.
 * Returns fallback of 80 if char_w is 0. */
static inline int ai_dock_initial_term_cols(int client_w, int char_w,
                                             int panel_visible, int panel_w,
                                             int scrollbar_w, int left_margin)
{
    if (char_w <= 0) return 80;
    int pw = panel_visible ? panel_w : 0;
    int tw = ai_dock_terminal_width(client_w, pw, scrollbar_w, left_margin);
    int cols = tw / char_w;
    return cols > 0 ? cols : 1;
}

#endif /* NUTSHELL_AI_DOCK_H */
