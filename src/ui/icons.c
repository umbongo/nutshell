/* src/ui/icons.c — vector icon renderer backed by GDI+.
 *
 * Each glyph is an op-stream on a 0..128 grid (1/8th of a 16 unit
 * cell); coordinates are stored as uint8_t so a value of 128
 * (center-line) does not overflow. The decoder walks the stream,
 * builds GDI+ paths, and strokes (and optionally fills) them.
 */
#include "icons.h"

#include <stdint.h>
#include <gdiplus.h>

enum {
    OP_END = 0,
    OP_MOVE,
    OP_LINE,
    OP_CURVE,
    OP_CLOSE,
    OP_FILLSTROKE,
    OP_STROKE
};

/* Coordinates: uint8_t on a 0..128 grid. 128 is the max coordinate
 * (right/bottom of the 16x16 cell at 1/8th subdivision) and must
 * fit in the storage type, which is why int8_t would be wrong. */
static const uint8_t g_glyphs[NS_ICON_COUNT][96] = {
    /* NS_ICON_CLOSE */
    { OP_MOVE, 32, 32, OP_LINE, 96, 96, OP_STROKE,
      OP_MOVE, 96, 32, OP_LINE, 32, 96, OP_STROKE, OP_END },
    /* NS_ICON_CHEV_LEFT */
    { OP_MOVE, 80, 28, OP_LINE, 44, 64, OP_LINE, 80, 100, OP_STROKE, OP_END },
    /* NS_ICON_CHEV_RIGHT */
    { OP_MOVE, 48, 28, OP_LINE, 84, 64, OP_LINE, 48, 100, OP_STROKE, OP_END },
    /* NS_ICON_PLUS */
    { OP_MOVE, 64, 28, OP_LINE, 64, 100, OP_STROKE,
      OP_MOVE, 28, 64, OP_LINE, 100, 64, OP_STROKE, OP_END },
    /* NS_ICON_AI — rounded chat bubble with a sparkle cut out of the
     * upper-right.  The bubble and spark sit in the same path so the
     * even-odd fill rule turns the spark into a hole that reveals the
     * button background; the stroke outlines both. */
    { OP_MOVE, 24, 24, OP_LINE, 92, 24,
      OP_CURVE, 100, 24, 108, 32, 108, 40,
      OP_LINE, 108, 76,
      OP_CURVE, 108, 84, 100, 92, 92, 92,
      OP_LINE, 56, 92, OP_LINE, 36, 108, OP_LINE, 44, 92,
      OP_LINE, 24, 92,
      OP_CURVE, 16, 92, 8, 84, 8, 76,
      OP_LINE, 8, 40,
      OP_CURVE, 8, 32, 16, 24, 24, 24,
      OP_CLOSE,
      OP_MOVE, 88, 20,
      OP_LINE, 91, 29, OP_LINE, 100, 32, OP_LINE, 91, 35,
      OP_LINE, 88, 44, OP_LINE, 85, 35, OP_LINE, 76, 32, OP_LINE, 85, 29,
      OP_CLOSE, OP_FILLSTROKE, OP_END },
    /* NS_ICON_DOCK — rect with right quarter filled */
    { OP_MOVE, 16, 24, OP_LINE, 112, 24, OP_LINE, 112, 104,
      OP_LINE, 16, 104, OP_CLOSE, OP_STROKE,
      OP_MOVE, 80, 24, OP_LINE, 112, 24, OP_LINE, 112, 104,
      OP_LINE, 80, 104, OP_CLOSE, OP_FILLSTROKE, OP_END },
    /* NS_ICON_UNDOCK */
    { OP_MOVE, 16, 40, OP_LINE, 88, 40, OP_LINE, 88, 104,
      OP_LINE, 16, 104, OP_CLOSE, OP_STROKE,
      OP_MOVE, 48, 40, OP_LINE, 48, 24, OP_LINE, 112, 24,
      OP_LINE, 112, 88, OP_LINE, 96, 88, OP_STROKE, OP_END },
    /* NS_ICON_SAVE — floppy outline + tab + inner rect */
    { OP_MOVE, 24, 28, OP_LINE, 88, 16, OP_LINE, 104, 32,
      OP_LINE, 104, 104, OP_LINE, 24, 104, OP_CLOSE, OP_STROKE,
      OP_MOVE, 44, 16, OP_LINE, 84, 16, OP_LINE, 84, 44,
      OP_LINE, 44, 44, OP_CLOSE, OP_FILLSTROKE,
      OP_MOVE, 40, 68, OP_LINE, 88, 68, OP_LINE, 88, 100,
      OP_LINE, 40, 100, OP_CLOSE, OP_STROKE, OP_END },
    /* NS_ICON_NEW_CHAT — bubble + pencil */
    { OP_MOVE, 108, 64, OP_LINE, 108, 92, OP_CURVE, 108, 100, 100, 104, 92, 104,
      OP_LINE, 48, 104, OP_LINE, 28, 120, OP_LINE, 28, 36,
      OP_CURVE, 28, 28, 36, 24, 44, 24, OP_LINE, 72, 24, OP_STROKE,
      OP_MOVE, 88, 24, OP_LINE, 112, 48, OP_LINE, 88, 72, OP_LINE, 64, 72,
      OP_LINE, 64, 48, OP_CLOSE, OP_FILLSTROKE, OP_END },
    /* NS_ICON_LOCK */
    { OP_MOVE, 26, 56, OP_LINE, 102, 56, OP_LINE, 102, 112,
      OP_LINE, 26, 112, OP_CLOSE, OP_STROKE,
      OP_MOVE, 44, 56, OP_LINE, 44, 40, OP_CURVE, 44, 20, 84, 20, 84, 40,
      OP_LINE, 84, 56, OP_STROKE, OP_END },
    /* NS_ICON_UNLOCK */
    { OP_MOVE, 26, 56, OP_LINE, 102, 56, OP_LINE, 102, 112,
      OP_LINE, 26, 112, OP_CLOSE, OP_STROKE,
      OP_MOVE, 44, 56, OP_LINE, 44, 40, OP_CURVE, 44, 20, 84, 20, 84, 40, OP_STROKE, OP_END },
    /* NS_ICON_BOLT — lightning */
    { OP_MOVE, 72, 12, OP_LINE, 28, 72, OP_LINE, 58, 72,
      OP_LINE, 52, 116, OP_LINE, 100, 56, OP_LINE, 70, 56, OP_CLOSE, OP_FILLSTROKE, OP_END },
    /* NS_ICON_THINKING — cloud + 3 dots */
    { OP_MOVE, 36, 92, OP_CURVE, 20, 92, 12, 80, 12, 72,
      OP_CURVE, 12, 60, 20, 52, 28, 54, OP_CURVE, 32, 38, 64, 32, 72, 46,
      OP_CURVE, 96, 38, 116, 54, 112, 72, OP_CURVE, 116, 84, 104, 92, 92, 92,
      OP_CLOSE, OP_FILLSTROKE,
      OP_MOVE, 48, 72, OP_LINE, 48, 72, OP_CLOSE, OP_FILLSTROKE,
      OP_MOVE, 64, 72, OP_LINE, 64, 72, OP_CLOSE, OP_FILLSTROKE,
      OP_MOVE, 80, 72, OP_LINE, 80, 72, OP_CLOSE, OP_FILLSTROKE, OP_END },
    /* NS_ICON_SEND — up arrow */
    { OP_MOVE, 64, 100, OP_LINE, 64, 36, OP_STROKE,
      OP_MOVE, 40, 60, OP_LINE, 64, 36, OP_LINE, 88, 60, OP_STROKE, OP_END },
    /* NS_ICON_STOP — filled square */
    { OP_MOVE, 32, 32, OP_LINE, 96, 32, OP_LINE, 96, 96, OP_LINE, 32, 96,
      OP_CLOSE, OP_FILLSTROKE, OP_END },
    /* NS_ICON_CHECK */
    { OP_MOVE, 24, 66, OP_LINE, 50, 92, OP_LINE, 104, 40, OP_STROKE, OP_END },
    /* NS_ICON_X */
    { OP_MOVE, 32, 32, OP_LINE, 96, 96, OP_STROKE,
      OP_MOVE, 96, 32, OP_LINE, 32, 96, OP_STROKE, OP_END },
    /* NS_ICON_SETTINGS — gear (simplified) */
    { OP_MOVE, 64, 44, OP_CURVE, 44, 44, 44, 84, 64, 84,
      OP_CURVE, 84, 84, 84, 44, 64, 44, OP_CLOSE, OP_STROKE,
      OP_MOVE, 64, 12, OP_LINE, 64, 24, OP_STROKE,
      OP_MOVE, 64, 104, OP_LINE, 64, 116, OP_STROKE,
      OP_MOVE, 12, 64, OP_LINE, 24, 64, OP_STROKE,
      OP_MOVE, 104, 64, OP_LINE, 116, 64, OP_STROKE, OP_END },
    /* NS_ICON_KEY */
    { OP_MOVE, 40, 88, OP_CURVE, 40, 72, 60, 72, 60, 88,
      OP_CURVE, 60, 104, 40, 104, 40, 88, OP_CLOSE, OP_STROKE,
      OP_MOVE, 58, 76, OP_LINE, 108, 28, OP_STROKE,
      OP_MOVE, 88, 40, OP_LINE, 100, 52, OP_STROKE, OP_END },
    /* NS_ICON_PASSWORD — 3 dots */
    { OP_MOVE, 32, 64, OP_LINE, 32, 64, OP_CLOSE, OP_FILLSTROKE,
      OP_MOVE, 64, 64, OP_LINE, 64, 64, OP_CLOSE, OP_FILLSTROKE,
      OP_MOVE, 96, 64, OP_LINE, 96, 64, OP_CLOSE, OP_FILLSTROKE, OP_END },
    /* NS_ICON_SERVER — stacked racks */
    { OP_MOVE, 20, 24, OP_LINE, 108, 24, OP_LINE, 108, 58,
      OP_LINE, 20, 58, OP_CLOSE, OP_STROKE,
      OP_MOVE, 20, 70, OP_LINE, 108, 70, OP_LINE, 108, 104,
      OP_LINE, 20, 104, OP_CLOSE, OP_STROKE, OP_END },
    /* NS_ICON_TRASH */
    { OP_MOVE, 24, 36, OP_LINE, 104, 36, OP_STROKE,
      OP_MOVE, 48, 36, OP_LINE, 48, 26, OP_LINE, 80, 26, OP_LINE, 80, 36, OP_STROKE,
      OP_MOVE, 36, 36, OP_LINE, 42, 104, OP_LINE, 86, 104, OP_LINE, 92, 36, OP_STROKE, OP_END },
    /* NS_ICON_LOG — document */
    { OP_MOVE, 24, 16, OP_LINE, 76, 16, OP_LINE, 104, 44,
      OP_LINE, 104, 112, OP_LINE, 24, 112, OP_CLOSE, OP_STROKE,
      OP_MOVE, 76, 16, OP_LINE, 76, 44, OP_LINE, 104, 44, OP_STROKE, OP_END },
    /* NS_ICON_COPY */
    { OP_MOVE, 40, 40, OP_LINE, 104, 40, OP_LINE, 104, 104,
      OP_LINE, 40, 104, OP_CLOSE, OP_STROKE,
      OP_MOVE, 84, 40, OP_LINE, 84, 20, OP_LINE, 24, 20, OP_LINE, 24, 84,
      OP_LINE, 40, 84, OP_STROKE, OP_END },
    /* NS_ICON_ZOOM_IN */
    { OP_MOVE, 56, 56, OP_CURVE, 24, 56, 24, 88, 56, 88,
      OP_CURVE, 88, 88, 88, 56, 56, 56, OP_CLOSE, OP_STROKE,
      OP_MOVE, 56, 56, OP_LINE, 56, 88, OP_STROKE,
      OP_MOVE, 40, 72, OP_LINE, 72, 72, OP_STROKE,
      OP_MOVE, 80, 80, OP_LINE, 104, 104, OP_STROKE, OP_END },
    /* NS_ICON_ZOOM_OUT */
    { OP_MOVE, 56, 56, OP_CURVE, 24, 56, 24, 88, 56, 88,
      OP_CURVE, 88, 88, 88, 56, 56, 56, OP_CLOSE, OP_STROKE,
      OP_MOVE, 40, 72, OP_LINE, 72, 72, OP_STROKE,
      OP_MOVE, 80, 80, OP_LINE, 104, 104, OP_STROKE, OP_END },
    /* NS_ICON_IMAGE */
    { OP_MOVE, 20, 24, OP_LINE, 108, 24, OP_LINE, 108, 104,
      OP_LINE, 20, 104, OP_CLOSE, OP_STROKE,
      OP_MOVE, 24, 96, OP_LINE, 48, 72, OP_LINE, 68, 92,
      OP_LINE, 88, 64, OP_LINE, 104, 80, OP_STROKE, OP_END },
    /* NS_ICON_TERMINAL */
    { OP_MOVE, 16, 24, OP_LINE, 112, 24, OP_LINE, 112, 104,
      OP_LINE, 16, 104, OP_CLOSE, OP_STROKE,
      OP_MOVE, 40, 56, OP_LINE, 56, 68, OP_LINE, 40, 80, OP_STROKE,
      OP_MOVE, 68, 82, OP_LINE, 92, 82, OP_STROKE, OP_END },
    /* NS_ICON_INFO */
    { OP_MOVE, 64, 19, OP_CURVE, 19, 19, 19, 109, 64, 109,
      OP_CURVE, 109, 109, 109, 19, 64, 19, OP_CLOSE, OP_STROKE,
      OP_MOVE, 64, 60, OP_LINE, 64, 86, OP_STROKE,
      OP_MOVE, 64, 42, OP_LINE, 64, 42, OP_CLOSE, OP_FILLSTROKE, OP_END },
    /* NS_ICON_SHIELD */
    { OP_MOVE, 64, 16, OP_LINE, 24, 32, OP_LINE, 24, 68,
      OP_CURVE, 24, 92, 40, 108, 64, 116, OP_CURVE, 88, 108, 104, 92, 104, 68,
      OP_LINE, 104, 32, OP_CLOSE, OP_FILLSTROKE,
      OP_MOVE, 48, 66, OP_LINE, 60, 78, OP_LINE, 84, 54, OP_STROKE, OP_END },
    /* NS_ICON_USER */
    { OP_MOVE, 64, 44, OP_CURVE, 44, 44, 44, 76, 64, 76,
      OP_CURVE, 84, 76, 84, 44, 64, 44, OP_CLOSE, OP_STROKE,
      OP_MOVE, 24, 108, OP_CURVE, 30, 88, 48, 78, 64, 78,
      OP_CURVE, 80, 78, 98, 88, 104, 108, OP_STROKE, OP_END },
    /* NS_ICON_SPARKLE — 4-point star */
    { OP_MOVE, 64, 16, OP_LINE, 72, 56, OP_LINE, 112, 64,
      OP_LINE, 72, 72, OP_LINE, 64, 112, OP_LINE, 56, 72,
      OP_LINE, 16, 64, OP_LINE, 56, 56, OP_CLOSE, OP_FILLSTROKE, OP_END },
};

static ULONG_PTR g_gdip_token = 0;

BOOL ns_icons_init(void)
{
    GdiplusStartupInput in;
    in.GdiplusVersion = 1;
    in.DebugEventCallback = NULL;
    in.SuppressBackgroundThread = FALSE;
    in.SuppressExternalCodecs = FALSE;
    return GdiplusStartup(&g_gdip_token, &in, NULL) == 0;
}

void ns_icons_shutdown(void)
{
    if (g_gdip_token) {
        GdiplusShutdown(g_gdip_token);
        g_gdip_token = 0;
    }
}

static ARGB argb_of(COLORREF c, BYTE a)
{
    return ((ARGB)a << 24)
         | ((ARGB)GetRValue(c) << 16)
         | ((ARGB)GetGValue(c) <<  8)
         | ((ARGB)GetBValue(c));
}

void ns_icon_draw_ex(HDC hdc, NsIconId id, const RECT *rc,
                     COLORREF fg, BYTE fill_alpha, UINT dpi)
{
    (void)dpi;

    if ((int)id < 0 || id >= NS_ICON_COUNT) return;

    GpGraphics *g = NULL;
    if (GdipCreateFromHDC(hdc, &g) != 0) return;
    GdipSetSmoothingMode(g, SmoothingModeAntiAlias);

    int w = rc->right - rc->left;
    int h = rc->bottom - rc->top;
    int side = (w < h) ? w : h;
    float scale = (float)side / 128.0f;

    float ox = (float)rc->left + ((float)w - 128.0f * scale) / 2.0f;
    float oy = (float)rc->top  + ((float)h - 128.0f * scale) / 2.0f;

    GpPen *pen = NULL;
    /* Stroke ≈ side/24 px so a 24 px icon strokes at ~1 px and a 48 px
     * icon at ~2 px — keeps the look proportional without going chunky
     * at larger sizes. */
    float stroke_w = (float)side / 24.0f;
    if (stroke_w < 1.0f) stroke_w = 1.0f;
    GdipCreatePen1(argb_of(fg, 255), stroke_w, UnitPixel, &pen);
    GdipSetPenLineCap197819(pen, LineCapRound, LineCapRound, DashCapFlat);
    GdipSetPenLineJoin(pen, LineJoinRound);

    GpSolidFill *brush = NULL;
    GdipCreateSolidFill(argb_of(fg, fill_alpha), &brush);

    GpPath *path = NULL;
    GdipCreatePath(FillModeAlternate, &path);

    const uint8_t *p = g_glyphs[id];
    float cx = 0.0f, cy = 0.0f;

    while (*p != OP_END) {
        uint8_t op = *p++;
        switch (op) {
        case OP_MOVE:
            cx = ox + (float)p[0] * scale;
            cy = oy + (float)p[1] * scale;
            p += 2;
            GdipStartPathFigure(path);
            break;
        case OP_LINE: {
            float nx = ox + (float)p[0] * scale;
            float ny = oy + (float)p[1] * scale;
            p += 2;
            GdipAddPathLine(path, cx, cy, nx, ny);
            cx = nx;
            cy = ny;
            break;
        }
        case OP_CURVE: {
            float c1x = ox + (float)p[0] * scale;
            float c1y = oy + (float)p[1] * scale;
            float c2x = ox + (float)p[2] * scale;
            float c2y = oy + (float)p[3] * scale;
            float nx  = ox + (float)p[4] * scale;
            float ny  = oy + (float)p[5] * scale;
            p += 6;
            GdipAddPathBezier(path, cx, cy, c1x, c1y, c2x, c2y, nx, ny);
            cx = nx;
            cy = ny;
            break;
        }
        case OP_CLOSE:
            GdipClosePathFigure(path);
            break;
        case OP_STROKE:
            GdipDrawPath(g, pen, path);
            GdipDeletePath(path);
            GdipCreatePath(FillModeAlternate, &path);
            break;
        case OP_FILLSTROKE:
            if (fill_alpha) GdipFillPath(g, (GpBrush *)brush, path);
            GdipDrawPath(g, pen, path);
            GdipDeletePath(path);
            GdipCreatePath(FillModeAlternate, &path);
            break;
        default:
            /* Malformed glyph stream — stop to avoid runaway parse. */
            goto done;
        }
    }

    GdipDrawPath(g, pen, path);

done:
    GdipDeletePath(path);
    GdipDeleteBrush((GpBrush *)brush);
    GdipDeletePen(pen);
    GdipDeleteGraphics(g);
}

void ns_icon_draw(HDC hdc, NsIconId id, const RECT *rc,
                  COLORREF fg, UINT dpi)
{
    /* Per-icon default interior fill alpha. Icons not listed here
     * default to stroke-only (alpha 0). */
    static const BYTE k_fill[NS_ICON_COUNT] = {
        [NS_ICON_AI]       = 255,
        [NS_ICON_BOLT]     = 56,
        [NS_ICON_STOP]     = 255,
        [NS_ICON_SHIELD]   = 38,
        [NS_ICON_THINKING] = 38,
        [NS_ICON_SAVE]     = 38,
        [NS_ICON_DOCK]     = 56,
        [NS_ICON_NEW_CHAT] = 56,
        [NS_ICON_SPARKLE]  = 255,
        [NS_ICON_PASSWORD] = 255,
        [NS_ICON_INFO]     = 255,
    };
    ns_icon_draw_ex(hdc, id, rc, fg, k_fill[id], dpi);
}
