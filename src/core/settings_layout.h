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

/* Total pixel height a page of `rows` rows occupies (including top/bottom pad). */
int settings_page_height(int rows, const SettingsMetrics *m);

/* Maximum scroll offset. 0 when the content fits. Never negative. */
int settings_scroll_max(int view_h, int content_h);

/* Clamp pos into [0, max]. */
int settings_scroll_clamp(int pos, int max);

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

/* How many rows slot `slot` spans when visible (0 for an unknown slot). */
int settings_debug_slot_rows(int slot);

#endif
