#ifndef NUTSHELL_NS_TYPE_H
#define NUTSHELL_NS_TYPE_H

/*
 * ns_type — the spacing grid, corner radii, stroke widths and type ramp for
 * the whole UI, plus the pure (role, dpi, face) -> font-cache-slot key.
 * Portable, no windows.h: src/ui/ns_font.c turns an NsFontSpec + dpi + slot
 * index into an actual HFONT. See
 * docs/superpowers/specs/2026-09-07-design-system-foundation-design.md
 * section 2.
 *
 * All SP_* / SZ_* values are 96-DPI bases; scale them with ns_scale() before
 * use. Strokes are drawn at a fixed pixel width regardless of DPI and are
 * deliberately not multiples of 4.
 */

/* ---- Spacing grid --------------------------------------------------------- */
enum { SP_XS = 4, SP_SM = 8, SP_MD = 12, SP_LG = 16, SP_XL = 24, SP_XXL = 32 };

/* ---- Component sizes ------------------------------------------------------- */
enum {
    SZ_CTRL_H    = 28,
    SZ_TAB_H     = 32,
    SZ_ICON      = 20,
    SZ_TAG_H     = 20,
    SZ_AVATAR    = 20,
    SZ_BTN_MIN_W = 80
};

/* ---- Corner radii ----------------------------------------------------------
 * R_PILL isn't a constant here: it is always half of an element's own
 * (already-scaled) height, so it is a function, ns_type_pill(height). */
enum { R_CTRL = 4, R_CARD = 8 };

/* ---- Stroke widths (not scaled; the only non-multiples of 4 in this file) - */
enum { STROKE_HAIRLINE = 1, STROKE_RULE = 2, STROKE_BAR = 3 };

/* ---- Type ramp -------------------------------------------------------------- */
typedef enum {
    FONT_CAPTION,
    FONT_BODY,
    FONT_TITLE,
    FONT_HEADING,
    FONT_MONO,
    FONT_ROLE_COUNT
} NsFontRole;

typedef struct {
    int    size_pt;      /* points at 96 DPI; 0 for FONT_MONO (see mono_size_pt below) */
    int    weight;        /* GDI font weight: 400 regular, 600 semibold */
    double line_height;   /* multiple of size_pt */
    int    is_mono;        /* 1 for FONT_MONO, 0 otherwise */
} NsFontSpec;

/* Ramp entry for `role`. Never returns NULL: FONT_ROLE_COUNT and any other
 * out-of-range role (including negative) fall back to the FONT_BODY spec. */
const NsFontSpec *ns_type_font(NsFontRole role);

/* Pixel character height for CreateFont's lfHeight (a positive value, i.e.
 * character height rather than cell height): round(size_pt * dpi / 72).
 * FONT_MONO ignores the ramp's size_pt (which is 0) and uses mono_size_pt
 * instead, the caller's configured terminal font size in points. dpi <= 0
 * is treated as 96; the result is always >= 1. */
int ns_type_font_px(NsFontRole role, int dpi, int mono_size_pt);

/* ---- Font cache slot key ----------------------------------------------------
 * A stable, collision-free index into src/ui/ns_font.c's cache array, over
 * role x a fixed DPI table x face_id.
 *
 * The DPI table (NS_DPI_TABLE_COUNT entries) is:
 *   { 96, 120, 144, 168, 192, 216, 240, 288 }
 * A dpi that isn't in the table maps to its nearest entry, ties favouring
 * the lower entry (e.g. 150 -> 144, 100 -> 96).
 *
 * face_id (0..NS_FACE_COUNT-1) is ns_font.c's own axis — e.g. the
 * proportional face at the configured weight, its bold variant, the
 * configured mono face, and its bold variant; this module only reserves
 * the slots, it does not interpret face_id.
 *
 * slot = role * (NS_DPI_TABLE_COUNT * NS_FACE_COUNT)
 *      + dpi_index * NS_FACE_COUNT
 *      + face_id
 *
 * NS_FONT_SLOTS = FONT_ROLE_COUNT * NS_DPI_TABLE_COUNT * NS_FACE_COUNT is
 * the capacity ns_font.c must allocate for its cache array. Every
 * (role, dpi, face_id) with face_id in [0, NS_FACE_COUNT) maps to a
 * distinct slot < NS_FONT_SLOTS. */
enum { NS_DPI_TABLE_COUNT = 8, NS_FACE_COUNT = 4 };
#define NS_FONT_SLOTS (FONT_ROLE_COUNT * NS_DPI_TABLE_COUNT * NS_FACE_COUNT)

int ns_type_font_slot(NsFontRole role, int dpi, int face_id);

/* Pill radius for an element of the given (already-scaled) height. */
int ns_type_pill(int height);

#endif
