#include "settings_layout.h"
#include "ns_scale.h"
#include <stddef.h>

/* ---- Nav table -----------------------------------------------------------
 * Single source of truth for the two-level category list: group headers
 * (non-selectable) interleaved with the pages they contain, in display
 * order. See docs/superpowers/specs/2026-08-29-settings-window-redesign-design.md
 * for the category structure this encodes. */

static const SettingsNavEntry NAV_TABLE[] = {
    { SETTINGS_PAGE_NONE,         0, 1, "General" },
    { SETTINGS_PAGE_APPEARANCE,   1, 0, "Appearance" },
    { SETTINGS_PAGE_TERMINAL,     1, 0, "Terminal" },
    { SETTINGS_PAGE_LOGGING,      1, 0, "Logging" },
    { SETTINGS_PAGE_NONE,         0, 1, "Connection" },
    { SETTINGS_PAGE_SSH,          1, 0, "SSH" },
    { SETTINGS_PAGE_STARTUP,      1, 0, "Startup" },
    { SETTINGS_PAGE_NONE,         0, 1, "AI Assistant" },
    { SETTINGS_PAGE_AI_PROVIDER,  1, 0, "Provider" },
    { SETTINGS_PAGE_AI_BEHAVIOUR, 1, 0, "Behaviour" },
    { SETTINGS_PAGE_AI_WEB,       1, 0, "Web Access" },
    { SETTINGS_PAGE_ABOUT,        0, 0, "About" },
};

#define NAV_TABLE_COUNT ((int)(sizeof(NAV_TABLE) / sizeof(NAV_TABLE[0])))

/* ---- Scaling --------------------------------------------------------------
 * Thin alias onto the shared ns_scale() helper (src/core/ns_scale.h), kept
 * during the design-system migration so this module's callers don't all
 * need to change at once. */

int settings_scale(int px, int dpi)
{
    return ns_scale(px, dpi);
}

void settings_metrics_init(SettingsMetrics *m, int dpi)
{
    if (!m) return;
    if (dpi <= 0) dpi = 96;

    m->dpi        = dpi;
    m->nav_w      = settings_scale(168, dpi);
    m->pad        = settings_scale(14, dpi);
    m->label_w    = settings_scale(150, dpi);
    m->gap        = settings_scale(10, dpi);
    m->row_h      = settings_scale(30, dpi);
    m->ctrl_h     = settings_scale(22, dpi);
    m->nav_item_h = settings_scale(22, dpi);
    m->btnbar_h   = settings_scale(56, dpi);
    m->btn_w      = settings_scale(82, dpi);
    m->btn_h      = settings_scale(26, dpi);
    m->min_w      = settings_scale(620, dpi);

    /* min_h must always be large enough to show the whole nav list (every
     * header and page, no scrolling) plus the button bar underneath it,
     * at whatever DPI we scaled to — derive the floor rather than trust a
     * fixed base value to stay ahead of the nav list as it grows. */
    int suggested_min_h = settings_scale(420, dpi);
    int nav_list_h = m->pad * 2 + m->nav_item_h * settings_nav_count();
    int required_min_h = nav_list_h + m->btnbar_h;
    m->min_h = (suggested_min_h > required_min_h) ? suggested_min_h : required_min_h;
}

/* ---- Nav model ------------------------------------------------------------ */

int settings_nav_count(void)
{
    return NAV_TABLE_COUNT;
}

const SettingsNavEntry *settings_nav_at(int index)
{
    if (index < 0 || index >= NAV_TABLE_COUNT) return NULL;
    return &NAV_TABLE[index];
}

const char *settings_page_title(int page_id)
{
    switch (page_id) {
        case SETTINGS_PAGE_APPEARANCE:   return "General > Appearance";
        case SETTINGS_PAGE_TERMINAL:     return "General > Terminal";
        case SETTINGS_PAGE_LOGGING:      return "General > Logging";
        case SETTINGS_PAGE_SSH:          return "Connection > SSH";
        case SETTINGS_PAGE_STARTUP:      return "Connection > Startup";
        case SETTINGS_PAGE_AI_PROVIDER:  return "AI Assistant > Provider";
        case SETTINGS_PAGE_AI_BEHAVIOUR: return "AI Assistant > Behaviour";
        case SETTINGS_PAGE_AI_WEB:       return "AI Assistant > Web Access";
        case SETTINGS_PAGE_ABOUT:        return "About";
        default:                         return NULL;
    }
}

int settings_nav_index_of_page(int page_id)
{
    if (page_id == SETTINGS_PAGE_NONE) return -1;

    for (int i = 0; i < NAV_TABLE_COUNT; i++) {
        if (!NAV_TABLE[i].is_header && NAV_TABLE[i].page_id == page_id) return i;
    }
    return -1;
}

int settings_nav_first_page(void)
{
    for (int i = 0; i < NAV_TABLE_COUNT; i++) {
        if (!NAV_TABLE[i].is_header) return i;
    }
    return -1;
}

/* ---- Regions --------------------------------------------------------------
 * Panes tile the client area exactly:
 *   +----------+----------------------------+
 *   |          |                            |
 *   |   nav    |          content           |
 *   |          |                            |
 *   +----------+----------------------------+
 *   |              buttons                  |
 *   +----------------------------------------+
 */

void settings_layout_regions(int client_w, int client_h,
                             const SettingsMetrics *m,
                             SettingsRect *nav,
                             SettingsRect *content,
                             SettingsRect *buttons)
{
    if (!m) return;

    int btnbar_h = m->btnbar_h;
    int upper_h = client_h - btnbar_h;
    if (upper_h < 0) upper_h = 0;

    if (nav) {
        nav->x = 0;
        nav->y = 0;
        nav->w = m->nav_w;
        nav->h = upper_h;
    }

    if (content) {
        content->x = m->nav_w;
        content->y = 0;
        content->w = client_w - m->nav_w;
        content->h = upper_h;
    }

    if (buttons) {
        buttons->x = 0;
        buttons->y = upper_h;
        buttons->w = client_w;
        buttons->h = btnbar_h;
    }
}

/* ---- Rows ------------------------------------------------------------------
 * Row `row` occupies the vertical slot [pad + row*row_h, pad + (row+1)*row_h)
 * (before y_offset is subtracted for scrolling). The control is vertically
 * centred within that slot; the label matches it so the two stay aligned. */

void settings_row_rects(int row, int content_w, int ctrl_w, int y_offset,
                        const SettingsMetrics *m,
                        SettingsRect *label, SettingsRect *ctrl)
{
    if (!m) return;

    int row_top = m->pad + row * m->row_h - y_offset;
    int vpad = (m->row_h - m->ctrl_h) / 2;
    if (vpad < 0) vpad = 0;
    int y = row_top + vpad;

    int ctrl_x = m->pad + m->label_w + m->gap;

    if (label) {
        label->x = m->pad;
        label->y = y;
        label->w = m->label_w;
        label->h = m->ctrl_h;
    }

    if (ctrl) {
        int w = ctrl_w > 0 ? ctrl_w : (content_w - ctrl_x - m->pad);
        if (w < 0) w = 0;
        ctrl->x = ctrl_x;
        ctrl->y = y;
        ctrl->w = w;
        ctrl->h = m->ctrl_h;
    }
}

int settings_page_height(int rows, const SettingsMetrics *m)
{
    if (!m) return 0;
    if (rows < 0) rows = 0;
    return m->pad * 2 + rows * m->row_h;
}

/* ---- Scrolling -------------------------------------------------------------- */

int settings_scroll_max(int view_h, int content_h)
{
    int max = content_h - view_h;
    return max > 0 ? max : 0;
}

int settings_scroll_clamp(int pos, int max)
{
    if (max < 0) max = 0;
    if (pos < 0) return 0;
    if (pos > max) return max;
    return pos;
}
