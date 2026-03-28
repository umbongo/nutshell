# AI Assist Panel Fixes — Task Log

## Tasks

### 1. Scrollbar styling in AI chat panel
- **Status:** Done (v0.9.35)
- **Description:** AI panel scrollbar needs to match the design/colour scheme of other scrollbars in the app
- **Fix:** Connected the existing csb (custom themed scrollbar) to ChatListView and hid the Windows standard WS_VSCROLL scrollbar. Added `ext_scrollbar` field to ChatListView, syncing range/position via `csb_set_range`/`csb_set_pos`.

### 2. Remove duplicate command display after approval
- **Status:** Done (v0.9.35)
- **Description:** After commands are approved and executed, the AI panel repeats them in a bordered box (the settled command cards). Remove this redundant display.
- **Fix:** Settled command items now return `measured_height = 0` in `measure_item()`, making them invisible. The command info is already visible in the AI text as purple [EXEC] blocks.

### 3. Style [EXEC][/EXEC] commands in pastel purple
- **Status:** Done (v0.9.35)
- **Description:** Commands shown with [EXEC][/EXEC] tags should be displayed in pastel purple for visibility across colour schemes
- **Fix:** Added `draw_ai_text_with_exec()` function that splits AI text at [EXEC]/[/EXEC] boundaries, rendering commands in pastel purple (RGB 180,140,220) with mono font, stripping the tags themselves.

### 4. Classify `crontab -l` as safe
- **Status:** Done (v0.9.35)
- **Description:** `crontab -l` and other read-only crontab parameters should be SAFE, but crontab commands that modify the system should not be
- **Fix:** Added `{ "crontab", "-l", CMD_SAFE }` and `{ "crontab", "--list", CMD_SAFE }` to `linux_subcmd_rules` in cmd_classify.c. Other crontab subcommands remain CMD_WRITE.

### 5. Permit Write indicator: grey/green instead of red/green
- **Status:** Done (v0.9.35)
- **Description:** The indicator light on the Permit Write button should be grey (off) or green (on), not red/green
- **Fix:** Changed inactive indicator from RGB(200,50,50) to RGB(128,128,128). Updated tooltip text from "Red" to "Grey".

### 6. [REMINDER] Check fonts when moving between resolutions
- **Status:** Pending — needs clarification from user before work begins
- **Description:** Investigate font rendering/scaling issues when switching between display resolutions. AI needs to ask user for specifics before addressing.

---

## Progress Log

- **v0.9.34** — Fixed AI Assist panel lock-up after first command approval (settled flag)
- **v0.9.35** — Scrollbar theming, removed duplicate commands, EXEC purple styling, crontab -l safe, permit write grey indicator
