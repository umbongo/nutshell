/* src/ui/icons.h — Nutshell built-in vector icon system.
 *
 * Replaces the Segoe Fluent Icons / Segoe MDL2 Assets font dependency.
 * Glyphs are stored as a compact op-stream on a 16-unit grid and
 * rendered via GDI+ paths, so icons render identically even on
 * Windows installs where the Fluent icon fonts are missing.
 */
#ifndef NUTSHELL_ICONS_H
#define NUTSHELL_ICONS_H

#ifdef _WIN32
#include <windows.h>

typedef enum {
    NS_ICON_CLOSE = 0,
    NS_ICON_CHEV_LEFT,
    NS_ICON_CHEV_RIGHT,
    NS_ICON_PLUS,
    NS_ICON_AI,          /* chat bubble with sparkle cutout — AI panel mark */
    NS_ICON_DOCK,
    NS_ICON_UNDOCK,
    NS_ICON_SAVE,
    NS_ICON_NEW_CHAT,
    NS_ICON_LOCK,
    NS_ICON_UNLOCK,
    NS_ICON_BOLT,
    NS_ICON_THINKING,
    NS_ICON_SEND,
    NS_ICON_STOP,
    NS_ICON_CHECK,
    NS_ICON_X,
    NS_ICON_SETTINGS,
    NS_ICON_KEY,
    NS_ICON_PASSWORD,
    NS_ICON_SERVER,
    NS_ICON_TRASH,
    NS_ICON_LOG,
    NS_ICON_COPY,
    NS_ICON_ZOOM_IN,
    NS_ICON_ZOOM_OUT,
    NS_ICON_IMAGE,
    NS_ICON_TERMINAL,
    NS_ICON_INFO,
    NS_ICON_SHIELD,
    NS_ICON_USER,
    NS_ICON_SPARKLE,
    NS_ICON_COUNT
} NsIconId;

BOOL ns_icons_init(void);
void ns_icons_shutdown(void);

void ns_icon_draw(HDC hdc, NsIconId id, const RECT *rc,
                  COLORREF fg, UINT dpi);

void ns_icon_draw_ex(HDC hdc, NsIconId id, const RECT *rc,
                     COLORREF fg, BYTE fill_alpha, UINT dpi);

/* Two-color variant. Accent ops in the glyph stream
 * (OP_STROKE_ACCENT / OP_FILLSTROKE_ACCENT) use `accent` instead of
 * `fg`. Glyphs with no accent ops render identically to ns_icon_draw_ex. */
void ns_icon_draw_accent(HDC hdc, NsIconId id, const RECT *rc,
                         COLORREF fg, COLORREF accent,
                         BYTE fill_alpha, BYTE accent_fill_alpha,
                         UINT dpi);

#endif /* _WIN32 */
#endif /* NUTSHELL_ICONS_H */
