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

### 7. Grey command echo still visible after approval
- **Status:** Done (v0.9.36)
- **Description:** After commands are approved and executed, the `send_continue_message` flow causes the AI's follow-up response to echo the executed commands again (shown in grey, e.g. `$ find ~...`, `$ du -h...`). These are redundant — the commands are already shown in purple [EXEC] blocks in the AI text and were individually approved before execution. The grey echo lines need to be suppressed.
- **Fix:** Removed the `CHAT_ITEM_STATUS` creation (status_buf/snprintf/chat_msg_append) from `execute_command()` in ai_chat.c. Commands are already visible as purple [EXEC] blocks.

### 8. [RESEARCH] Best practice for show/hide thinking
- **Status:** Done (v0.9.40)
- **Description:** Research best practices for showing and hiding AI "thinking" / chain-of-thought in chat UIs. Then review the existing thinking implementation in the codebase to determine what changes are needed to reach best-practice state.
- **Fix:** Restored inline thinking block as a contained box (border, rounded corners, header/body). Collapsed by default with "Thought for X.Xs" header; click to expand. Auto-scrolls during streaming; disengages on user scroll-up, re-engages at bottom. Max height 400px with internal scrolling. Theme-aware colors across all 4 schemes.

### 9. Permit Write toggle should re-block commands when disabled
- **Status:** Done (v0.9.36)
- **Description:** When command cards are displayed for approval and some are blocked due to Permit Write being off, pressing Permit Write correctly unblocks them instantly. However, pressing Permit Write again (disabling it) should instantly re-block those pending write commands. Currently it does not.
- **Fix:** Added `chat_approval_block_pending_writes()` function that transitions PENDING → BLOCKED for write/critical entries. Added else branch to IDC_CHAT_PERMIT handler to call it and sync ChatMsgItem blocked state.

### 10. Remove extra spacing between commentary and [EXEC] commands
- **Status:** Done (v0.9.37)
- **Description:** In the AI chat panel, when the AI response contains commentary text followed by a purple [EXEC] command, there is an unwanted blank line between the commentary and the command. The space should be removed so the command sits directly beneath its preceding commentary. Keep the space between a command and the next commentary block (i.e., space *after* commands, not *before*).
- **Fix:** In `draw_ai_text_with_exec()`, strip trailing `\n`/`\r` from pre-EXEC text segments before rendering. Added `ai_text_for_measure()` helper to keep height measurement in sync with rendering.

### 11. Duplicate scrollbar in AI Assist panel — remove the misaligned one
- **Status:** Done (v0.9.37)
- **Description:** The AI Assist panel shows two scrollbars: the custom themed scrollbar (csb) and what appears to be a standard Windows scrollbar or a second scrollbar from the command card container. The one that doesn't match the visual design should be removed, keeping only the themed scrollbar.
- **Fix:** Removed `WS_VSCROLL` from ChatListView window creation, removed `SetScrollInfo`/`ShowScrollBar` calls. Only the custom themed scrollbar (csb) remains.

### 12. Auto Approve not persisting across command batches
- **Status:** Done (v0.9.38)
- **Description:** When Auto Approve is toggled on and a batch of command cards is approved/executed, the next batch of commands from the AI's follow-up response still requires manual approval despite Auto Approve being visibly active (indicator lit). Auto Approve should carry forward so that subsequent command batches from the same conversation are automatically approved without user intervention.
- **Fix:** When new commands arrive from AI, after adding them to the approval queue, check `chat_approval_all_decided()`. If all commands are already decided (auto-approved or blocked), bypass the approval UI (`pending_approval = 0`), sync ChatMsgItem states, settle commands, and execute immediately — mirroring the Allow All flow.

### 6. [REMINDER] Check fonts when moving between resolutions
- **Status:** Pending — needs clarification from user before work begins
- **Description:** Investigate font rendering/scaling issues when switching between display resolutions. AI needs to ask user for specifics before addressing.

---

## Progress Log

- **v0.9.34** — Fixed AI Assist panel lock-up after first command approval (settled flag)
- **v0.9.35** — Scrollbar theming, removed duplicate commands, EXEC purple styling, crontab -l safe, permit write grey indicator
- **v0.9.36** — Removed grey command echo after execution, permit write re-blocks commands when disabled
- **v0.9.37** — Removed blank line before [EXEC] commands, removed duplicate native scrollbar
- **v0.9.38** — Auto Approve now persists across command batches
- **v0.9.40** — Restored inline thinking block with contained box design, auto-scroll
