#ifndef NUTSHELL_CMD_DETAIL_LAYOUT_H
#define NUTSHELL_CMD_DETAIL_LAYOUT_H

/*
 * cmd_detail_layout -- pure geometry for the command details popup
 * (docs/superpowers/specs/2026-10-13-command-detail-and-copy-design.md,
 * section 2). Portable, no windows.h; compiled into both the cross build and
 * the native tests (tests/test_cmd_detail_layout.c).
 *
 * Top to bottom, inside a SP_LG margin:
 *
 *   label    "The assistant says:" caption line, SZ_ICON high
 *   note     the assistant's explanation, `note_h` high (the caller measures
 *            it with DrawText(DT_CALCRECT), capped at eight lines)
 *   tag      the row's risk tag, `tag_w` wide, SZ_TAG_H high, left-aligned
 *   command  the read-only command box, taking all the remaining height
 *   buttons  [ Copy ] [ Close ], SZ_BTN_MIN_W x SZ_CTRL_H, right-aligned
 *
 * Every size is a 96-DPI base from ns_type.h scaled with ns_scale(); the
 * `client_w`, `client_h`, `note_h` and `tag_w` inputs are already in pixels
 * at `dpi`.
 */

#include "ns_layout.h"   /* NsRect */

typedef struct {
    NsRect label, note, tag, command, copy_btn, close_btn;
} CmdDetailLayout;

/* Lay out the popup's client area. `dpi` <= 0 is treated as 96. Negative
 * inputs count as 0. `note_h` is clipped so the command box keeps a
 * non-negative height; at a client size too small for the content the
 * rects overlap rather than going negative: every x, y, w and h in `out` is
 * >= 0 for any input, and every rect lies inside the client whenever the
 * client is large enough for the content. `out` may be NULL (no-op). */
void cmd_detail_layout(int client_w, int client_h, int dpi, int note_h,
                       int tag_w, CmdDetailLayout *out);

#endif /* NUTSHELL_CMD_DETAIL_LAYOUT_H */
