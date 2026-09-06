#ifndef NUTSHELL_NS_SCALE_H
#define NUTSHELL_NS_SCALE_H

/*
 * ns_scale — the one DPI-scaling helper for the whole UI (core, Win32-free).
 *
 * Replaces the ad-hoc scale helpers scattered across the UI layer (the S()
 * macros in ai_chat.c / help_guide.c, CLV_SCALE in chat_listview.c, and
 * settings_scale() in settings_layout.c, which becomes a thin alias during
 * migration and is then removed). See
 * docs/superpowers/specs/2026-09-07-design-system-foundation-design.md
 * section 2.
 */

/* Scale a 96-DPI pixel value to the given dpi: round-half-up
 * ((px * dpi + 48) / 96). dpi <= 0 is treated as 96. Never rounds a
 * positive px down to zero. */
int ns_scale(int px, int dpi);

#endif
