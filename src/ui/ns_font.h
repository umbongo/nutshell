#ifndef NUTSHELL_NS_FONT_H
#define NUTSHELL_NS_FONT_H

/*
 * ns_font — the Win32 half of the type ramp: turns an (NsFontRole, dpi) pair
 * into a cached HFONT. The pure slot-key math lives in src/core/ns_type.c
 * (ns_type_font_slot, ns_type_font_face_id) so it is native-testable; this
 * file only owns the GDI CreateFont calls and the cache's lifetime.
 *
 * face_id 0 is the proportional UI face (APP_FONT_UI_FACE "Inter") at the
 * role's ramp weight; face_id 2 is the configured mono face (the terminal
 * font from Settings) at the terminal point size, used for FONT_MONO.
 * face_id 1/3 (bold variants) are reserved slots — nothing creates them yet.
 *
 * See docs/superpowers/specs/2026-09-07-design-system-foundation-design.md
 * section 2.
 */

#ifdef _WIN32
#include <windows.h>
#include "ns_type.h"

/* Cached HFONT for `role` at `dpi`. Never DeleteObject the result — the
 * cache owns it; call ns_font_flush() instead when fonts must change.
 * Returns NULL only if the underlying CreateFont call itself fails. */
HFONT ns_font(NsFontRole role, int dpi);

/* Configure the faces ns_font() draws from: ui_face is the proportional face
 * for face_id 0 (falls back to APP_FONT_UI_FACE if NULL/empty); mono_face and
 * mono_pt are the terminal font name/size used for FONT_MONO (mono_face falls
 * back to "Consolas", mono_pt to APP_FONT_DEFAULT_SIZE if <= 0). Flushes the
 * cache if anything actually changed. Call once at startup and again whenever
 * settings change. */
void ns_font_set_faces(const char *ui_face, const char *mono_face, int mono_pt);

/* Delete every cached HFONT and empty the cache. The whole of what
 * WM_DPICHANGED and a font-setting change need to do; callers that keep a
 * font handle in a struct across paints must re-fetch it from ns_font()
 * afterwards (the old handle is no longer valid). */
void ns_font_flush(void);

#endif /* _WIN32 */

#endif
