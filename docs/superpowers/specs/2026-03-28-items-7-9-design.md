# Items 7 & 9: Grey Echo Removal and Permit Write Re-blocking

**Date:** 2026-03-28
**Status:** Approved

## Item 7: Remove grey command echo after approval

### Problem
After commands are approved and executed, `execute_command()` appends a `CHAT_ITEM_STATUS` item showing the command in grey (e.g. `$ find ~...`). This is redundant because the commands are already visible as purple [EXEC] blocks in the AI response text.

### Solution
Remove the status item creation in `execute_command()` at `src/ui/ai_chat.c:673-677`. Delete the `status_buf` variable, `snprintf` call, and `chat_msg_append(..., CHAT_ITEM_STATUS, status_buf)` call. The `CHAT_ITEM_STATUS` type remains available for other uses.

### Files changed
- `src/ui/ai_chat.c` — remove ~5 lines in `execute_command()`

---

## Item 9: Re-block commands when Permit Write is disabled

### Problem
When Permit Write is toggled on, blocked write commands are correctly unblocked. But toggling Permit Write back off does not re-block pending write/critical commands. The toggle handler at `src/ui/ai_chat.c:1483-1504` only handles the enabling case.

### Solution
1. Add `chat_approval_block_pending_writes(ApprovalQueue *q)` to `src/core/chat_approval.c` — iterates queue entries, changes `APPROVE_PENDING` to `APPROVE_BLOCKED` for entries where `safety > CMD_SAFE`. Returns count of blocked entries.
2. Declare the function in `src/core/chat_approval.h`.
3. Add `else` branch to the Permit Write toggle handler:
   - Call `chat_approval_block_pending_writes(&d->approval_q)`
   - Sync to ChatMsgItem list: for each `CHAT_ITEM_COMMAND` where `approved == -1` and `safety > CMD_SAFE`, set `blocked = 1`
   - Invalidate chat list view

### Files changed
- `src/core/chat_approval.c` — new function (~15 lines)
- `src/core/chat_approval.h` — 1 line declaration
- `src/ui/ai_chat.c` — add else branch (~15 lines)

---

## Testing
- Build with `make clean && make release`
- Run `make test`
- Manual test item 7: approve and execute commands, verify no grey echo appears
- Manual test item 9: with pending write commands visible, toggle Permit Write on (unblocks), toggle off (re-blocks), toggle on again (unblocks)
