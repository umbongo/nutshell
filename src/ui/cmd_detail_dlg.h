#ifndef NUTSHELL_CMD_DETAIL_DLG_H
#define NUTSHELL_CMD_DETAIL_DLG_H

/*
 * cmd_detail_dlg -- the command details popup: what the assistant says it
 * intends (its note), the risk tag, and the full command in a read-only,
 * selectable box with Copy and Close. Informational only -- no Approve or
 * Deny; those stay on the card. Spec:
 * docs/superpowers/specs/2026-10-13-command-detail-and-copy-design.md
 * section 2.
 */

#ifdef _WIN32
#include <windows.h>
#include "cmd_classify.h"

/* Show the popup modally over `owner` (disabled while it is open) and return
 * when it closes (Close, Esc, Enter outside the text box, or the close box).
 * `command` and `note` are UTF-8; `note` may be NULL or empty, which shows
 * "The assistant gave no explanation for this command.". All three inputs
 * are COPIED at open, so the caller's list items may be freed or rebuilt
 * (a stream chunk, a batch eviction) while it runs. `dpi` is the owner's
 * DPI, used to size and place the window. Runs its own message loop and
 * re-posts WM_QUIT if it sees one. NULL `command` is treated as empty. */
void cmd_detail_show(HWND owner, const char *command, const char *note,
                     CmdSafetyLevel safety, int dpi);

#endif /* _WIN32 */
#endif /* NUTSHELL_CMD_DETAIL_DLG_H */
