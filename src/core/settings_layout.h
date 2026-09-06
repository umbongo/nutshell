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

#endif
