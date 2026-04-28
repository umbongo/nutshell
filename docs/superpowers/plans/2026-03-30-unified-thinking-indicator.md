# Unified Thinking Box Indicator — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the collapsible thinking box the sole indicator during the AI thinking phase — visible and clickable from the first thinking token, replacing the separate green dot.

**Architecture:** Remove `thinking_complete` gates from measure/paint/hit-test code so the thinking box renders during streaming. Add a pulsing green dot to the header during active thinking. Suppress the standalone activity indicator during the thinking phase.

**Tech Stack:** C, Win32 GDI, MinGW cross-compile

**Spec:** `docs/superpowers/specs/2026-03-30-unified-thinking-indicator-design.md`

---

### Task 1: Remove `thinking_complete` gate from `measure_item()`

**Files:**
- Modify: `src/ui/chat_listview.c:712-715`

- [ ] **Step 1: Remove the `thinking_complete` check from the measurement condition**

At line 712-715, change:

```c
        /* Thinking block height (only after thinking is complete —
         * during active thinking the activity indicator handles display) */
        if (item->u.ai.thinking_text && item->u.ai.thinking_text[0]
            && item->u.ai.thinking_complete) {
```

to:

```c
        /* Thinking block height — shown during streaming and after completion */
        if (item->u.ai.thinking_text && item->u.ai.thinking_text[0]) {
```

- [ ] **Step 2: Cross-compile to verify no errors**

Run: `cd /home/thomas/nutshell && make clean && make release 2>&1 | tail -20`
Expected: Build succeeds (after version bump per CLAUDE.md rules)

- [ ] **Step 3: Commit**

```bash
git add src/ui/chat_listview.c
git commit -m "feat: show thinking box during streaming in measure_item"
```

---

### Task 2: Remove `thinking_complete` gate from `paint_ai_item()`

**Files:**
- Modify: `src/ui/chat_listview.c:1035-1039`

- [ ] **Step 1: Remove the `thinking_complete` check from the paint condition**

At line 1035-1039, change:

```c
    /* ── Thinking block (contained box) — only after thinking is done.
     * During active thinking the activity indicator (green dot + timer)
     * shows status, avoiding overlap. ─────────────────────────────── */
    if (item->u.ai.thinking_text && item->u.ai.thinking_text[0]
        && item->u.ai.thinking_complete) {
```

to:

```c
    /* ── Thinking block (contained box) — shown during streaming and
     * after completion.  During streaming the header shows a pulsing
     * green dot; after completion it shows "Thought for X.Xs". ───── */
    if (item->u.ai.thinking_text && item->u.ai.thinking_text[0]) {
```

- [ ] **Step 2: Commit**

```bash
git add src/ui/chat_listview.c
git commit -m "feat: show thinking box during streaming in paint_ai_item"
```

---

### Task 3: Add pulsing dot to collapsed header during streaming

**Files:**
- Modify: `src/ui/chat_listview.c:1065-1082`

The collapsed header currently renders `"▶  Thought for X.Xs"` or `"▶  Thinking... (X.Xs)"`. During active streaming we want a pulsing green dot between the chevron and the label text.

- [ ] **Step 1: Replace the collapsed header text rendering block**

At lines 1065-1082, replace:

```c
            /* Header text: chevron + "Thought for X.Xs" */
            SetTextColor(hdc, RGB_FROM_THEME(tc->thinking_text));
            SelectObject(hdc, lv->hBoldFont ? lv->hBoldFont
                              : GetStockObject(DEFAULT_GUI_FONT));
            char hdr_buf[64];
            if (item->u.ai.thinking_complete)
                snprintf(hdr_buf, sizeof(hdr_buf),
                         "\xe2\x96\xb6  Thought for %.1fs",
                         (double)item->u.ai.thinking_elapsed);
            else
                snprintf(hdr_buf, sizeof(hdr_buf),
                         "\xe2\x96\xb6  Thinking... (%.1fs)",
                         (double)item->u.ai.thinking_elapsed);
            RECT hdr_rc;
            SetRect(&hdr_rc, box_rc.left + pad, box_rc.top + pad,
                    box_rc.right - pad, box_rc.bottom - pad);
            draw_text_utf8(hdc, hdr_buf, &hdr_rc,
                           DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
```

with:

```c
            /* Header text: chevron + optional pulsing dot + label */
            SetTextColor(hdc, RGB_FROM_THEME(tc->thinking_text));
            SelectObject(hdc, lv->hBoldFont ? lv->hBoldFont
                              : GetStockObject(DEFAULT_GUI_FONT));
            RECT hdr_rc;
            SetRect(&hdr_rc, box_rc.left + pad, box_rc.top + pad,
                    box_rc.right - pad, box_rc.bottom - pad);

            if (item->u.ai.thinking_complete) {
                char hdr_buf[64];
                snprintf(hdr_buf, sizeof(hdr_buf),
                         "\xe2\x96\xb6  Thought for %.1fs",
                         (double)item->u.ai.thinking_elapsed);
                draw_text_utf8(hdc, hdr_buf, &hdr_rc,
                               DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX
                               | DT_END_ELLIPSIS);
            } else {
                /* Draw chevron */
                draw_text_utf8(hdc, "\xe2\x96\xb6", &hdr_rc,
                               DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
                /* Measure chevron width to position dot after it */
                SIZE chev_sz;
                GetTextExtentPoint32A(hdc, ">", 1, &chev_sz);
                int dot_x = hdr_rc.left + chev_sz.cx + CLV_SCALE(lv, 6);
                int dot_sz = CLV_SCALE(lv, BASE_DOT_SIZE);
                int dot_y = hdr_rc.top
                            + (hdr_rc.bottom - hdr_rc.top - dot_sz) / 2;
                /* Pulsing dot — blend with bg on alternate ticks */
                COLORREF dot_clr = RGB_FROM_THEME(tc->thinking_text);
                if (lv->pulse_toggle) {
                    COLORREF bg = RGB_FROM_THEME(tc->cmd_bg);
                    dot_clr = RGB(
                        (GetRValue(dot_clr) + GetRValue(bg)) / 2,
                        (GetGValue(dot_clr) + GetGValue(bg)) / 2,
                        (GetBValue(dot_clr) + GetBValue(bg)) / 2);
                }
                HBRUSH dbr = CreateSolidBrush(dot_clr);
                HPEN   dpen = CreatePen(PS_SOLID, 1, dot_clr);
                HGDIOBJ obr = SelectObject(hdc, dbr);
                HGDIOBJ open = SelectObject(hdc, dpen);
                Ellipse(hdc, dot_x, dot_y,
                        dot_x + dot_sz, dot_y + dot_sz);
                SelectObject(hdc, open);
                SelectObject(hdc, obr);
                DeleteObject(dpen);
                DeleteObject(dbr);

                /* "Thinking..." label after the dot */
                char hdr_buf[64];
                snprintf(hdr_buf, sizeof(hdr_buf), "Thinking...");
                RECT lbl_rc;
                SetRect(&lbl_rc, dot_x + dot_sz + CLV_SCALE(lv, 4),
                        hdr_rc.top, hdr_rc.right, hdr_rc.bottom);
                draw_text_utf8(hdc, hdr_buf, &lbl_rc,
                               DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

                /* Right-aligned elapsed timer */
                char time_buf[16];
                snprintf(time_buf, sizeof(time_buf), "%.1fs",
                         (double)item->u.ai.thinking_elapsed);
                RECT time_rc;
                SetRect(&time_rc, hdr_rc.left, hdr_rc.top,
                        hdr_rc.right, hdr_rc.bottom);
                draw_text_utf8(hdc, time_buf, &time_rc,
                               DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX
                               | DT_RIGHT);
            }
```

- [ ] **Step 2: Cross-compile to verify no errors**

Run: `cd /home/thomas/nutshell && make clean && make release 2>&1 | tail -20`
Expected: Build succeeds

- [ ] **Step 3: Commit**

```bash
git add src/ui/chat_listview.c
git commit -m "feat: pulsing dot + timer in collapsed thinking header during streaming"
```

---

### Task 4: Add pulsing dot to expanded header during streaming

**Files:**
- Modify: `src/ui/chat_listview.c:1121-1138`

- [ ] **Step 1: Replace the expanded header text rendering block**

At lines 1121-1138, replace:

```c
            /* Header row */
            SetTextColor(hdc, RGB_FROM_THEME(tc->thinking_text));
            SelectObject(hdc, lv->hBoldFont ? lv->hBoldFont
                              : GetStockObject(DEFAULT_GUI_FONT));
            char hdr_buf[64];
            if (item->u.ai.thinking_complete)
                snprintf(hdr_buf, sizeof(hdr_buf),
                         "\xe2\x96\xbc  Thought for %.1fs",
                         (double)item->u.ai.thinking_elapsed);
            else
                snprintf(hdr_buf, sizeof(hdr_buf),
                         "\xe2\x96\xbc  Thinking... (%.1fs)",
                         (double)item->u.ai.thinking_elapsed);
            RECT hdr_rc;
            SetRect(&hdr_rc, box_rc.left + pad, box_rc.top + pad,
                    box_rc.right - pad, box_rc.top + pad + hdr_h);
            draw_text_utf8(hdc, hdr_buf, &hdr_rc,
                           DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
```

with:

```c
            /* Header row */
            SetTextColor(hdc, RGB_FROM_THEME(tc->thinking_text));
            SelectObject(hdc, lv->hBoldFont ? lv->hBoldFont
                              : GetStockObject(DEFAULT_GUI_FONT));
            RECT hdr_rc;
            SetRect(&hdr_rc, box_rc.left + pad, box_rc.top + pad,
                    box_rc.right - pad, box_rc.top + pad + hdr_h);

            if (item->u.ai.thinking_complete) {
                char hdr_buf[64];
                snprintf(hdr_buf, sizeof(hdr_buf),
                         "\xe2\x96\xbc  Thought for %.1fs",
                         (double)item->u.ai.thinking_elapsed);
                draw_text_utf8(hdc, hdr_buf, &hdr_rc,
                               DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX
                               | DT_END_ELLIPSIS);
            } else {
                /* Draw chevron */
                draw_text_utf8(hdc, "\xe2\x96\xbc", &hdr_rc,
                               DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
                /* Pulsing dot after chevron */
                SIZE chev_sz;
                GetTextExtentPoint32A(hdc, "v", 1, &chev_sz);
                int dot_x = hdr_rc.left + chev_sz.cx + CLV_SCALE(lv, 6);
                int dot_sz = CLV_SCALE(lv, BASE_DOT_SIZE);
                int dot_y = hdr_rc.top
                            + (hdr_rc.bottom - hdr_rc.top - dot_sz) / 2;
                COLORREF dot_clr = RGB_FROM_THEME(tc->thinking_text);
                if (lv->pulse_toggle) {
                    COLORREF bg = RGB_FROM_THEME(tc->cmd_bg);
                    dot_clr = RGB(
                        (GetRValue(dot_clr) + GetRValue(bg)) / 2,
                        (GetGValue(dot_clr) + GetGValue(bg)) / 2,
                        (GetBValue(dot_clr) + GetBValue(bg)) / 2);
                }
                HBRUSH dbr = CreateSolidBrush(dot_clr);
                HPEN   dpen = CreatePen(PS_SOLID, 1, dot_clr);
                HGDIOBJ obr = SelectObject(hdc, dbr);
                HGDIOBJ open = SelectObject(hdc, dpen);
                Ellipse(hdc, dot_x, dot_y,
                        dot_x + dot_sz, dot_y + dot_sz);
                SelectObject(hdc, open);
                SelectObject(hdc, obr);
                DeleteObject(dpen);
                DeleteObject(dbr);

                /* "Thinking..." label */
                char hdr_buf[64];
                snprintf(hdr_buf, sizeof(hdr_buf), "Thinking...");
                RECT lbl_rc;
                SetRect(&lbl_rc, dot_x + dot_sz + CLV_SCALE(lv, 4),
                        hdr_rc.top, hdr_rc.right, hdr_rc.bottom);
                draw_text_utf8(hdc, hdr_buf, &lbl_rc,
                               DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

                /* Right-aligned elapsed timer */
                char time_buf[16];
                snprintf(time_buf, sizeof(time_buf), "%.1fs",
                         (double)item->u.ai.thinking_elapsed);
                RECT time_rc;
                SetRect(&time_rc, hdr_rc.left, hdr_rc.top,
                        hdr_rc.right, hdr_rc.bottom);
                draw_text_utf8(hdc, time_buf, &time_rc,
                               DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX
                               | DT_RIGHT);
            }
```

- [ ] **Step 2: Cross-compile to verify no errors**

Run: `cd /home/thomas/nutshell && make clean && make release 2>&1 | tail -20`
Expected: Build succeeds

- [ ] **Step 3: Commit**

```bash
git add src/ui/chat_listview.c
git commit -m "feat: pulsing dot + timer in expanded thinking header during streaming"
```

---

### Task 5: Suppress activity indicator during thinking phase

**Files:**
- Modify: `src/ui/chat_listview.c:1536-1540`

- [ ] **Step 1: Add early return for ACTIVITY_THINKING**

At line 1539, change:

```c
    if (!lv->activity || lv->activity->phase == ACTIVITY_IDLE)
        return 0;
```

to:

```c
    if (!lv->activity || lv->activity->phase == ACTIVITY_IDLE
        || lv->activity->phase == ACTIVITY_THINKING)
        return 0;
```

- [ ] **Step 2: Cross-compile to verify no errors**

Run: `cd /home/thomas/nutshell && make clean && make release 2>&1 | tail -20`
Expected: Build succeeds

- [ ] **Step 3: Commit**

```bash
git add src/ui/chat_listview.c
git commit -m "feat: suppress standalone activity dot during thinking phase"
```

---

### Task 6: Fix click hit-test area to match rendered box height

**Files:**
- Modify: `src/ui/chat_listview.c:1741-1745`

The click handler checks `my < hdr_top + hdr_h` but the rendered collapsed box is `hdr_h + 2*pad` tall. This means clicks on the lower part of the box don't register.

- [ ] **Step 1: Fix the hit-test to cover the full box height**

At lines 1741-1745, change:

```c
            int hdr_h = CLV_SCALE(lv, BASE_THINK_HDR_H);
            int hdr_top = y + icon_sz + CLV_SCALE(lv, 4);
            int think_left = side_pad + lv->ai_indent;

            if (my >= hdr_top && my < hdr_top + hdr_h &&
                mx >= think_left) {
```

to:

```c
            int hdr_h = CLV_SCALE(lv, BASE_THINK_HDR_H);
            int click_h = hdr_h + 2 * lv->code_pad;
            int hdr_top = y + icon_sz + CLV_SCALE(lv, 4);
            int think_left = side_pad + lv->ai_indent;

            if (my >= hdr_top && my < hdr_top + click_h &&
                mx >= think_left) {
```

- [ ] **Step 2: Cross-compile to verify no errors**

Run: `cd /home/thomas/nutshell && make clean && make release 2>&1 | tail -20`
Expected: Build succeeds

- [ ] **Step 3: Commit**

```bash
git add src/ui/chat_listview.c
git commit -m "fix: expand thinking header click area to match rendered box height"
```

---

### Task 7: Version bump, final build, and test

**Files:**
- Modify: `src/ui/resource.h` — `APP_VERSION`
- Modify: `README.md` — `**Version**:` line
- Modify: `src/ui/nutshell.rc` — `FILEVERSION`, `PRODUCTVERSION`, string values

- [ ] **Step 1: Bump version in all three files**

Increment the patch version (e.g. `"1.0.11"` -> `"1.0.12"` — check current value first).

- [ ] **Step 2: Clean build**

Run: `cd /home/thomas/nutshell && make clean && make release 2>&1 | tail -20`
Expected: Build succeeds with zero warnings/errors

- [ ] **Step 3: Run tests**

Run: `cd /home/thomas/nutshell && make test 2>&1 | tail -20`
Expected: All tests pass

- [ ] **Step 4: Commit**

```bash
git add src/ui/chat_listview.c src/ui/resource.h src/ui/nutshell.rc README.md
git commit -m "feat: unified thinking box indicator — version bump and final build"
```
