#ifndef NUTSHELL_SETTINGS_LAYOUT_H
#define NUTSHELL_SETTINGS_LAYOUT_H

/*
 * settings_layout — portable (Win32-free) layout math for the Settings
 * window redesign. Pure C, no windows.h, no HWND, no MulDiv: this module
 * compiles into both the MinGW cross-build and the native Linux test
 * build (see tests/test_settings_layout.c).
 *
 * It owns:
 *   - the two-level category nav model (headers + pages),
 *   - DPI-scaled metrics,
 *   - splitting the client area into nav / content / button-bar panes,
 *   - placing one label/control row inside the content pane,
 *   - scroll-offset maths for an overflowing page.
 *
 * The Win32 shell (src/ui/settings.c) is written against this API and
 * supplies only the HWNDs and drawing.
 */

/* Page identifiers. SETTINGS_PAGE_NONE means "no page" / header row. */
enum {
    SETTINGS_PAGE_NONE = -1,
    SETTINGS_PAGE_APPEARANCE = 0,
    SETTINGS_PAGE_TERMINAL,
    SETTINGS_PAGE_LOGGING,
    SETTINGS_PAGE_SSH,
    SETTINGS_PAGE_STARTUP,
    SETTINGS_PAGE_AI_PROVIDER,
    SETTINGS_PAGE_AI_BEHAVIOUR,
    SETTINGS_PAGE_VOICE,
    SETTINGS_PAGE_AI_WEB,
    SETTINGS_PAGE_DEBUG,
    SETTINGS_PAGE_ABOUT,
    SETTINGS_PAGE_COUNT
};

typedef struct {
    int         page_id;   /* SETTINGS_PAGE_* , or SETTINGS_PAGE_NONE for a header */
    int         depth;     /* 0 = header or standalone page, 1 = child page */
    int         is_header; /* 1 = non-selectable group label */
    const char *label;     /* display text */
} SettingsNavEntry;

typedef struct { int x, y, w, h; } SettingsRect;

typedef struct {
    int dpi;
    int nav_w;      /* width of the left category pane */
    int pad;        /* outer padding inside panes */
    int label_w;    /* width of the right-aligned label column */
    int gap;        /* horizontal gap between label and control */
    int row_h;      /* vertical pitch between rows */
    int ctrl_h;     /* height of a single-line edit/combo */
    int nav_item_h; /* height of one nav list row */
    int btnbar_h;   /* height of the bottom Save/Cancel bar */
    int btn_w;      /* button width */
    int btn_h;      /* button height */
    int min_w;      /* minimum window client width */
    int min_h;      /* minimum window client height */
} SettingsMetrics;

/* Fill m with DPI-scaled constants. dpi <= 0 is treated as 96. */
void settings_metrics_init(SettingsMetrics *m, int dpi);

/* Flattened navigation list (headers + pages), in display order. */
int  settings_nav_count(void);
const SettingsNavEntry *settings_nav_at(int index);   /* NULL if out of range */

/* Display title for a page, e.g. "General > Appearance". NULL if out of range. */
const char *settings_page_title(int page_id);

/* Index into the nav list for a page id, or -1. */
int settings_nav_index_of_page(int page_id);

/* First selectable (non-header) nav index, or -1 if none. */
int settings_nav_first_page(void);

/* Split the client area into the three panes. Any out pointer may be NULL.
 * Panes tile the client area exactly: nav on the left full height above the
 * button bar, content to its right, button bar across the bottom. */
void settings_layout_regions(int client_w, int client_h,
                             const SettingsMetrics *m,
                             SettingsRect *nav,
                             SettingsRect *content,
                             SettingsRect *buttons);

/* Place one label/control pair for row `row` inside a content pane of width
 * content_w. ctrl_w <= 0 means "stretch to the remaining width".
 * y_offset lets the caller apply a scroll offset (pass 0 when unscrolled). */
void settings_row_rects(int row, int content_w, int ctrl_w, int y_offset,
                        const SettingsMetrics *m,
                        SettingsRect *label, SettingsRect *ctrl);

/* How one row is built from its controls (96-dpi widths, scaled by the row
 * layout). ctrl_w > 0 is the row's explicit total width (the control and any
 * companions beside it), 0 stretches to the pane; full_w drops the label
 * column; extra_w / extra2_w are the companion controls to the right of the
 * main one (extra2 the rightmost); extra_off keeps extra on the row at no
 * width. */
typedef struct {
    int ctrl_w;
    int extra_w;
    int extra2_w;
    int full_w;
    int extra_off;
} SettingsRowSpec;

typedef struct {
    SettingsRect label;   /* the label column's box (meaningful unless full_w) */
    SettingsRect ctrl;
    SettingsRect extra;   /* w == 0 when the row has no extra */
    SettingsRect extra2;  /* w == 0 when the row has no extra2 */
} SettingsRowGeom;

/* Place every control of row `row` in a page `usable_w` wide (the scrollbar
 * already taken off). Whatever the width, nothing extends past
 * usable_w - m->pad: an explicit total (ctrl_w) is cut to the room left, and
 * the companion `extra` gives way first, the main control keeping the width
 * it was asked for (ctrl_w minus its companions) for as long as there is room.
 * A stretched row's control takes what the companions leave, as always. */
void settings_row_layout(const SettingsMetrics *m, int row, int usable_w,
                         int y_offset, const SettingsRowSpec *spec,
                         SettingsRowGeom *g);

/* Total pixel height a page of `rows` rows occupies (including top/bottom pad). */
int settings_page_height(int rows, const SettingsMetrics *m);

/* Initial client size and window position for the Settings dialog. The window
 * opens just tall enough that the page of target_rows rows (title included;
 * AI Assistant > Behaviour) needs no scrolling, with its button bar:
 *   target_h = settings_page_height(target_rows) + btnbar_h
 *   client_h = max(min_h, min(target_h, work_h - nc_h - 2 * margin))
 *   client_w = max(min_w, min(760 scaled, work_w - nc_w - 2 * margin))
 * where margin is SP_XL scaled to the dpi. work_* is the monitor work area,
 * nc_* the window's non-client size at its own monitor's DPI (measured from
 * the hidden window, not AdjustWindowRect). The window (client plus nc)
 * is centred in the work area, then moved so its far edge is inside it, then
 * so its near edge is inside it: top-left wins when it cannot fit. Writes the
 * client size and the window's top-left, in the work area's coordinates. */
void settings_initial_placement(const SettingsMetrics *m, int target_rows,
                                int work_x, int work_y, int work_w, int work_h,
                                int nc_w, int nc_h,
                                int *client_w, int *client_h,
                                int *win_x, int *win_y);

/* The smallest outer window size the dialog may be dragged to on the monitor
 * whose metrics are `m`: the minimum client size plus the window frame
 * (nc_w, nc_h) measured at that monitor's DPI. Writes nothing for a NULL
 * `m`; either out pointer may be NULL. (WM_GETMINMAXINFO.) */
void settings_min_window_size(const SettingsMetrics *m, int nc_w, int nc_h,
                              int *w, int *h);

/* A scroll offset in pixels at old_dpi, expressed at new_dpi (the page is
 * laid out again at the new scale after a monitor move, so the same rows stay
 * in view). Never negative; an offset is returned unchanged for a dpi <= 0. */
int settings_scroll_rescale(int pos, int old_dpi, int new_dpi);

/* Maximum scroll offset. 0 when the content fits. Never negative. */
int settings_scroll_max(int view_h, int content_h);

/* Clamp pos into [0, max]. */
int settings_scroll_clamp(int pos, int max);

/* The scroll offset that shows the span y..y+h (page coordinates, before
 * scrolling) in a viewport view_h tall, given the current offset `scroll` and
 * the page's content height. Already in view: unchanged. Above: scrolls up to
 * y. Below: scrolls down just enough. Taller than the viewport: top aligned.
 * The result is clamped to 0..settings_scroll_max(view_h, content_h). */
int settings_scroll_to_show(int scroll, int y, int h, int view_h, int content_h);

/* ---- The Debugging page (spec 2026-10-17-debug-logging-design.md, section 9)
 *
 * Every control on the page below the "Enable debugging" tickbox appears only
 * while the tickbox is ticked. The page is the usual stack of one-row slots;
 * settings_debug_layout() says which slot is on which row for either state,
 * so the Win32 page (src/ui/settings.c) and the tests share one table.
 * Row 0 is the page title; the slots start at row 1. */
typedef enum {
    SDBG_ENABLE = 0,       /* "Enable debugging" tickbox                          */
    SDBG_HINT_ON,          /* "Records what Nutshell does ..."  (enabled only)     */
    SDBG_HINT_OFF,         /* "Errors are still written ..."    (disabled only)    */
    SDBG_LEVEL_RENDER,     /* six level rows, in DlogCat order                      */
    SDBG_LEVEL_TERM,
    SDBG_LEVEL_AI,
    SDBG_LEVEL_SSH,
    SDBG_LEVEL_SHELL,
    SDBG_LEVEL_GENERAL,
    SDBG_RECORDER,         /* "Keep recent detail for issue marks" + level combo   */
    SDBG_SCREENSHOT,       /* "Save a screenshot with each issue mark"             */
    SDBG_CONTENT,          /* "Include terminal content"                           */
    SDBG_CONTENT_WARN,     /* its warning (two rows: it wraps)                      */
    SDBG_FOLDER,           /* Log folder + Browse + Open                           */
    SDBG_MAX_FILE,         /* Max file size (MB)                                   */
    SDBG_MAX_TOTAL,        /* Keep at most (MB) + "using N MB in K files"          */
    SDBG_CURRENT,          /* Current log file (read-only; shown in both states)   */
    SDBG_NOTE,             /* "Give this folder to ... Mark issue: Ctrl+Shift+M"   */
    SDBG_COUNT
} SettingsDebugSlot;

typedef struct {
    int visible;   /* 1 = shown in this state                                      */
    int row;       /* first row of the slot (page title is row 0); -1 if hidden    */
    int rows;      /* rows the slot spans (0 if hidden)                            */
} SettingsDebugPos;

/* Fill pos[SDBG_COUNT] for the ticked (enabled != 0) or unticked page and
 * return the total number of rows the page needs, title row included.
 * Visible slots are packed from row 1 in enum order; hidden ones take none.
 * Unticked, the page is the tickbox, its one-line hint and the current file. */
int settings_debug_layout(int enabled, SettingsDebugPos pos[SDBG_COUNT]);

/* The row recipe of slot `slot` (the widths the Debugging page builds its
 * controls with): 0 and *spec untouched for an unknown slot, else 1. */
int settings_debug_slot_spec(int slot, SettingsRowSpec *spec);

/* How many rows slot `slot` spans when visible (0 for an unknown slot). */
int settings_debug_slot_rows(int slot);

#endif
