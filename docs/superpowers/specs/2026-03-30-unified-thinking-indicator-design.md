# Unified Thinking Box Indicator

**Date**: 2026-03-30
**Status**: Approved

## Problem

Previously there were two overlapping thinking indicators during AI streaming:
1. A grey collapsible thinking block (showed thinking text, clickable)
2. A green activity dot + label

The grey one was removed to fix the overlap, but now the green dot has no expand/collapse capability. Users can't see what the AI is thinking during the thinking phase.

## Solution

Make the collapsible thinking box the sole indicator during the thinking phase. Remove the separate green dot during thinking. The box appears as soon as the first thinking token arrives, is clickable at all times, and transitions to the final "Thought for X.Xs" state when thinking completes.

## Changes

### 1. Remove `thinking_complete` gate from rendering

**File**: `src/ui/chat_listview.c`

**`measure_item()` (~line 714)**: Change the condition from:
```c
if (item->u.ai.thinking_text && item->u.ai.thinking_text[0]
    && item->u.ai.thinking_complete)
```
to:
```c
if (item->u.ai.thinking_text && item->u.ai.thinking_text[0])
```

**`paint_ai_item()` (~line 1038)**: Same change — remove `&& item->u.ai.thinking_complete`.

### 2. Update collapsed header rendering for streaming state

**File**: `src/ui/chat_listview.c`, inside the collapsed branch (~line 1047)

When `thinking_complete == 0` (streaming):
- Draw a pulsing green dot (same size/animation as current activity dot)
- Text: `"Thinking..."` in green (`tc->thinking_text` theme color)
- Right-aligned elapsed timer: `"12.3s"` in muted green

When `thinking_complete == 1` (done):
- No dot
- Text: `"Thought for X.Xs"` (current behavior, unchanged)

The chevron `>` / `v` renders in both states.

### 3. Update expanded header rendering for streaming state

**File**: `src/ui/chat_listview.c`, inside the expanded branch (~line 1085)

Same header distinction as collapsed (dot + "Thinking..." during streaming, "Thought for X.Xs" after). Body renders `thinking_text` with auto-scroll when `thinking_autoscroll == 1`.

### 4. Suppress activity indicator during thinking phase

**File**: `src/ui/chat_listview.c`, `paint_activity_indicator()` (~line 1536)

Add early return:
```c
if (lv->activity->phase == ACTIVITY_THINKING)
    return 0;
```

This keeps the green dot for Responding, Executing, and Waiting phases.

### 5. Remove `thinking_complete` gate from click handler

**File**: `src/ui/chat_listview.c`, `on_lbuttondown()` (~line 1739)

The existing click handler already checks for `thinking_text` but may also gate on `thinking_complete`. Remove that gate so clicks work during streaming.

### 6. Remove `thinking_complete` gate from mousewheel handler

**File**: `src/ui/chat_listview.c`, mousewheel section (~line 2059)

Same — allow scroll-wheel interaction during streaming, not just after completion.

## Visual States

### During thinking (collapsed — default)
```
[>] * Thinking...                    12.3s
```
`>` = chevron, `*` = pulsing green dot, timer right-aligned

### During thinking (expanded)
```
[v] * Thinking...                    12.3s
─────────────────────────────────────────
  The user wants me to verify nginx is
  working correctly. I should check
  several things: the service status...█
                                      ▐ (scrollbar)
```

### After thinking (collapsed)
```
[>] Thought for 28.0s
```

### After thinking (expanded)
```
[v] Thought for 28.0s
─────────────────────────────────────────
  Full thinking text here...
                                      ▐ (scrollbar)
```

## Data Structures

No changes. All required fields already exist in `ChatMsgItem.u.ai`:
- `thinking_text` — accumulated thinking tokens
- `thinking_collapsed` — toggle state (default 1)
- `thinking_elapsed` — seconds elapsed
- `thinking_complete` — 0 during streaming, 1 after
- `thinking_scroll_y` — scroll offset in expanded body
- `thinking_autoscroll` — auto-scroll during streaming

## Files Modified

| File | Change |
|------|--------|
| `src/ui/chat_listview.c` | Remove `thinking_complete` gates, add pulsing dot to header, suppress activity indicator during thinking |

Single file change. No new files, no struct changes, no test changes needed (UI-only, Win32 GDI code not covered by native tests).
