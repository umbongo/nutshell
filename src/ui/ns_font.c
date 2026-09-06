#ifdef _WIN32
#include <winsock2.h>   /* Must come before windows.h */
#endif

#include "ns_font.h"

#ifdef _WIN32

#include "app_font.h"
#include <stdio.h>
#include <string.h>

/* One HFONT per (role, dpi, face_id) slot, lazily created and owned until
 * ns_font_flush(). Indexed by ns_type_font_slot(). */
static HFONT g_cache[NS_FONT_SLOTS];

static char g_ui_face[64]   = APP_FONT_UI_FACE;
static char g_mono_face[64] = "Consolas";
static int  g_mono_pt       = APP_FONT_DEFAULT_SIZE;

static HFONT ns_font_create(NsFontRole role, int dpi, int face_id)
{
    const NsFontSpec *spec = ns_type_font(role);
    int px = ns_type_font_px(role, dpi, g_mono_pt);
    int is_mono = (face_id == 2);
    const char *face = is_mono ? g_mono_face : g_ui_face;
    DWORD pitch_family = is_mono ? (FIXED_PITCH | FF_MODERN)
                                  : (DEFAULT_PITCH | FF_SWISS);

    return CreateFont(-px, 0, 0, 0, spec->weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, pitch_family, face);
}

HFONT ns_font(NsFontRole role, int dpi)
{
    int face_id = ns_type_font_face_id(role);
    int slot = ns_type_font_slot(role, dpi, face_id);
    if (slot < 0 || slot >= NS_FONT_SLOTS)
        return NULL;

    if (!g_cache[slot])
        g_cache[slot] = ns_font_create(role, dpi, face_id);

    return g_cache[slot];
}

void ns_font_set_faces(const char *ui_face, const char *mono_face, int mono_pt)
{
    const char *ui   = (ui_face && ui_face[0]) ? ui_face : APP_FONT_UI_FACE;
    const char *mono = (mono_face && mono_face[0]) ? mono_face : "Consolas";
    if (mono_pt <= 0) mono_pt = APP_FONT_DEFAULT_SIZE;

    int changed = (strncmp(g_ui_face, ui, sizeof(g_ui_face)) != 0) ||
                  (strncmp(g_mono_face, mono, sizeof(g_mono_face)) != 0) ||
                  (g_mono_pt != mono_pt);
    if (!changed)
        return;

    (void)snprintf(g_ui_face, sizeof(g_ui_face), "%s", ui);
    (void)snprintf(g_mono_face, sizeof(g_mono_face), "%s", mono);
    g_mono_pt = mono_pt;

    ns_font_flush();
}

void ns_font_flush(void)
{
    for (int i = 0; i < NS_FONT_SLOTS; i++) {
        if (g_cache[i]) {
            DeleteObject(g_cache[i]);
            g_cache[i] = NULL;
        }
    }
}

#endif /* _WIN32 */
