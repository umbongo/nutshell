# Screen Clear Repaint Bug Fix — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix stale screen content remaining after TUI apps (nano, man) exit, caused by dirty marking not covering all visible screen rows when `lines_count < rows`.

**Architecture:** Three-part fix — (1) fix dirty marking loops in `buffer.c` to use `screen_to_phys` over screen rows `0..rows-1`, (2) add `force_all_rows` bypass in `renderer.c` when `full_redraw_needed` fires, (3) bypass paint cooldown in `window.c` for critical repaints. TDD: failing tests first, then minimal fixes.

**Tech Stack:** C, MinGW cross-compile, custom test framework (`test_framework.h`)

---

### Task 1: Write Failing Tests for Sparse-Buffer Dirty Marking

**Files:**
- Modify: `tests/test_dirty.c:229` (append after `test_resize_sparse_rows_accessible`)
- Modify: `tests/runner.c:538` (add declarations)
- Modify: `tests/runner.c:1939` (add invocations)

- [ ] **Step 1: Write test for alt-screen exit with `lines_count < rows`**

Append to `tests/test_dirty.c` after the `test_resize_sparse_rows_accessible` function (line 248):

```c
/* Alt-screen exit must mark ALL visible screen rows dirty,
 * even when lines_count < rows (sparse buffer after init). */
int test_dirty_alt_screen_sparse(void)
{
    TEST_BEGIN();
    /* Create terminal with 10 rows but only write to 2 lines.
     * lines_count will be ~2, much less than rows=10. */
    Terminal *t = term_init(10, 40, 0);
    feed(t, "line1\nline2");
    int saved_count = t->lines_count;
    ASSERT_TRUE(saved_count < t->rows);  /* precondition: sparse */

    /* Enter alt-screen, draw something, then exit */
    term_alt_screen_enter(t);
    feed(t, "ALT CONTENT FILLS SCREEN");
    term_clear_dirty(t);
    term_alt_screen_exit(t);

    /* Every visible screen row must be dirty after exit */
    int dirty = 0;
    for (int i = 0; i < t->rows; i++) {
        int top = (t->lines_count >= t->rows)
                ? (t->lines_count - t->rows) : 0;
        int phys = (t->lines_start + top + i) % t->lines_capacity;
        if (t->lines[phys] && t->lines[phys]->dirty) dirty++;
    }
    ASSERT_EQ(dirty, t->rows);
    term_free(t);
    TEST_END();
}
```

- [ ] **Step 2: Write test for `term_mark_all_dirty` with `lines_count < rows`**

Append to `tests/test_dirty.c` after the previous test:

```c
/* term_mark_all_dirty must cover rows beyond lines_count when sparse. */
int test_mark_all_dirty_sparse(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(10, 40, 0);
    feed(t, "hi");
    ASSERT_TRUE(t->lines_count < t->rows);  /* precondition */
    term_clear_dirty(t);
    term_mark_all_dirty(t);

    /* All screen rows must be dirty, not just 0..lines_count-1 */
    int dirty = 0;
    for (int i = 0; i < t->rows; i++) {
        int top = (t->lines_count >= t->rows)
                ? (t->lines_count - t->rows) : 0;
        int phys = (t->lines_start + top + i) % t->lines_capacity;
        if (t->lines[phys] && t->lines[phys]->dirty) dirty++;
    }
    ASSERT_EQ(dirty, t->rows);
    term_free(t);
    TEST_END();
}
```

- [ ] **Step 3: Write test for `term_has_dirty_rows` detecting rows beyond `lines_count`**

Append to `tests/test_dirty.c`:

```c
/* term_has_dirty_rows must detect dirty rows beyond lines_count. */
int test_has_dirty_rows_sparse(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(10, 40, 0);
    feed(t, "hi");
    ASSERT_TRUE(t->lines_count < t->rows);
    term_clear_dirty(t);

    /* Manually dirty a row beyond lines_count */
    int beyond = t->lines_count;  /* first row past lines_count */
    int phys = (t->lines_start + beyond) % t->lines_capacity;
    if (t->lines[phys]) t->lines[phys]->dirty = true;

    ASSERT_TRUE(term_has_dirty_rows(t));
    term_free(t);
    TEST_END();
}
```

- [ ] **Step 4: Add declarations and invocations in runner.c**

Add declarations after line 538 (`int test_resize_sparse_rows_accessible(void);`):

```c
int test_dirty_alt_screen_sparse(void);
int test_mark_all_dirty_sparse(void);
int test_has_dirty_rows_sparse(void);
```

Add invocations after line 1939 (`failed += test_resize_sparse_rows_accessible();`):

```c
    failed += test_dirty_alt_screen_sparse();
    failed += test_mark_all_dirty_sparse();
    failed += test_has_dirty_rows_sparse();
```

- [ ] **Step 5: Run tests — verify the three new tests FAIL**

Run: `make test 2>&1 | tail -30`

Expected: `test_dirty_alt_screen_sparse` FAILS (dirty count < rows), `test_mark_all_dirty_sparse` FAILS (dirty count < rows), `test_has_dirty_rows_sparse` FAILS (returns false). All other tests still pass.

- [ ] **Step 6: Commit failing tests**

```bash
git add tests/test_dirty.c tests/runner.c
git commit -m "test: add failing tests for sparse-buffer dirty marking (lines_count < rows)"
```

---

### Task 2: Fix Dirty Marking Functions in `buffer.c`

**Files:**
- Modify: `src/term/buffer.c:357-387` (`term_clear_dirty`, `term_has_dirty_rows`, `term_mark_all_dirty`)
- Modify: `src/term/buffer.c:443-448` (dirty loop in `term_alt_screen_exit`)

All four functions currently iterate `0..lines_count-1`. They must iterate screen rows `0..rows-1` using the same `screen_to_phys` helper used by `term_scroll_up`/`term_scroll_down`.

- [ ] **Step 1: Fix `term_clear_dirty`**

In `src/term/buffer.c`, replace the function body (lines 357-365):

```c
void term_clear_dirty(Terminal *term)
{
    if (!term) return;
    for (int i = 0; i < term->rows; i++) {
        int idx = screen_to_phys(term, i);
        if (idx >= 0 && idx < term->lines_capacity && term->lines[idx])
            term->lines[idx]->dirty = false;
    }
}
```

- [ ] **Step 2: Fix `term_has_dirty_rows`**

In `src/term/buffer.c`, replace the function body (lines 367-377):

```c
bool term_has_dirty_rows(Terminal *term)
{
    if (!term) return false;
    for (int i = 0; i < term->rows; i++) {
        int idx = screen_to_phys(term, i);
        if (idx >= 0 && idx < term->lines_capacity &&
            term->lines[idx] && term->lines[idx]->dirty)
            return true;
    }
    return false;
}
```

- [ ] **Step 3: Fix `term_mark_all_dirty`**

In `src/term/buffer.c`, replace the function body (lines 379-387):

```c
void term_mark_all_dirty(Terminal *term)
{
    if (!term) return;
    for (int i = 0; i < term->rows; i++) {
        int idx = screen_to_phys(term, i);
        if (idx >= 0 && idx < term->lines_capacity && term->lines[idx])
            term->lines[idx]->dirty = true;
    }
}
```

- [ ] **Step 4: Fix `term_alt_screen_exit` dirty loop**

In `src/term/buffer.c`, replace the dirty marking loop in `term_alt_screen_exit` (lines 443-448):

```c
    /* Mark all visible screen rows dirty — not just 0..lines_count-1.
     * When lines_count < rows (sparse buffer), rows beyond lines_count
     * are still visible on screen and must be repainted. */
    for (int i = 0; i < term->rows; i++) {
        int idx = screen_to_phys(term, i);
        if (idx >= 0 && idx < term->lines_capacity && term->lines[idx])
            term->lines[idx]->dirty = true;
    }
```

- [ ] **Step 5: Run tests — verify all pass**

Run: `make test 2>&1 | tail -30`

Expected: All tests pass, including the three new sparse-buffer tests from Task 1.

- [ ] **Step 6: Commit**

```bash
git add src/term/buffer.c
git commit -m "fix: dirty marking covers all screen rows when lines_count < rows"
```

---

### Task 3: Add Renderer `force_all_rows` Bypass

**Files:**
- Modify: `src/ui/renderer.c:208-213` (full_redraw_needed block)
- Modify: `src/ui/renderer.c:244-246` (row skip condition)

No unit tests — `renderer.c` is in `src/ui/` (excluded from test builds, uses Win32 GDI). Correctness verified by existing tests passing + manual testing.

- [ ] **Step 1: Add `force_all_rows` flag**

In `src/ui/renderer.c`, change the `full_redraw_needed` block (lines 208-213) to:

```c
    /* If the terminal buffer was swapped (e.g. alt-screen exit), the display
     * buffer shadow is stale — invalidate it to force a full repaint.
     * Also bypass the row-level dirty skip so every row reaches the
     * cell-level shadow check (defense-in-depth). */
    bool force_all_rows = false;
    if (term->full_redraw_needed) {
        dispbuf_invalidate(&r->dispbuf);
        term->full_redraw_needed = false;
        force_all_rows = true;
    }
```

- [ ] **Step 2: Update row skip condition**

In `src/ui/renderer.c`, change the row skip condition (lines 244-246) to:

```c
        if (!force_all_rows &&
            term->scrollback_offset == 0 &&
            !row->dirty && row_idx != cursor_row && row_idx != prev_row &&
            !has_sel) continue;
```

- [ ] **Step 3: Verify cross-compile succeeds**

Run: `make clean && make release 2>&1 | tail -5`

Expected: Clean build with no warnings or errors.

- [ ] **Step 4: Verify tests still pass**

Run: `make test 2>&1 | tail -10`

Expected: All tests pass (renderer changes are UI-only, no test regressions).

- [ ] **Step 5: Commit**

```bash
git add src/ui/renderer.c
git commit -m "fix: renderer bypasses row-level dirty skip on full_redraw_needed"
```

---

### Task 4: Bypass Paint Cooldown for Critical Repaints

**Files:**
- Modify: `src/ui/window.c:1956-1964` (timer poll loop paint logic)

No unit tests — `window.c` is in `src/ui/` (Win32 UI). Correctness verified by build + manual testing.

- [ ] **Step 1: Add critical repaint bypass**

In `src/ui/window.c`, replace lines 1956-1964:

```c
                        if (poll_rc > 0 || term_has_dirty_rows(s->term)) {
                            DWORD now = GetTickCount();
                            bool critical = (s == g_active_session &&
                                             s->term->full_redraw_needed);
                            if (critical || now - g_last_paint_tick >= PAINT_COOLDOWN_MS) {
                                if (poll_rc > 0 || critical)
                                    dispbuf_invalidate(&g_renderer.dispbuf);
                                invalidate_terminal(hwnd);
                                g_last_paint_tick = now;
                            }
                        }
```

- [ ] **Step 2: Verify cross-compile succeeds**

Run: `make clean && make release 2>&1 | tail -5`

Expected: Clean build, no warnings.

- [ ] **Step 3: Verify tests still pass**

Run: `make test 2>&1 | tail -10`

Expected: All tests pass.

- [ ] **Step 4: Commit**

```bash
git add src/ui/window.c
git commit -m "fix: bypass paint cooldown for critical repaints (alt-screen exit)"
```

---

### Task 5: Version Bump, Final Build, and Verification

**Files:**
- Modify: `src/ui/resource.h` (APP_VERSION, APP_VERSION_BINARY)
- Modify: `README.md` (Version line)

- [ ] **Step 1: Bump version**

Read current version from `src/ui/resource.h`, increment patch. Update both `APP_VERSION` string and `APP_VERSION_BINARY` macro. Update the `**Version**:` line in `README.md` to match.

- [ ] **Step 2: Clean build**

Run: `make clean && make release 2>&1 | tail -5`

Expected: Clean build, zero warnings, zero errors.

- [ ] **Step 3: Run full test suite**

Run: `make test 2>&1`

Expected: All tests pass, including the three new sparse-buffer tests. Zero failures.

- [ ] **Step 4: Commit version bump**

```bash
git add src/ui/resource.h README.md
git commit -m "chore: bump version to <new_version>"
```

- [ ] **Step 5: Mark todo.md item as done**

In `todo.md`, change the screen rendering bug line from `- [ ]` to `- [x]`.

```bash
git add todo.md
git commit -m "docs: mark screen-clear repaint bug as done"
```
