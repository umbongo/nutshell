# Hold-to-Reveal Passwords Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** An eye button inside the right edge of every existing password field (profile editor, key passphrase prompt, AI API key) shows the password in plain text only while the left mouse button is held on it.

**Architecture:** A pure reveal state machine (`src/core/pw_reveal_state.c`) decides masked or revealed from press, release, leave, enter, capture-lost, deactivate, emptied and destroy events, with native tests. One Win32 helper (`src/ui/pw_reveal.c`) attaches to any `EDIT` control: it subclasses the edit, creates the eye as a child window of the edit at its right edge, reserves that width with `EM_SETMARGINS`, and feeds window messages into the state machine; it is tested under `make wintest`. The passphrase prompt moves out of `window.c` into `src/ui/pw_prompt.c` and is themed from `ns_tokens()`, so all three hosts use token colours and PR 2's password prompt box can reuse it.

**Tech Stack:** C11, Win32/GDI/GDI+ cross-compiled with `x86_64-w64-mingw32-gcc`, comctl32 (`SetWindowSubclass`, tooltips), native tests through `make test` (`tests/test_framework.h`, `tests/runner.c`), Win32 tests through `make wintest` (`tests/win_runner.c`), design-system modules (`ns_tokens`, `ns_scale`, `ns_type.h`, `ns_font`, `ns_hover`, `ns_draw`, `icons`).

**Spec:** `docs/superpowers/specs/2026-11-03-telnet-and-password-reveal-design.md` (approved 2026-11-03), sections "Hold-to-reveal", "Anti-goals" and "Order of work" item 1. This is pull request 1 of 2; `docs/superpowers/plans/2026-11-03-telnet.md` is cut after this one merges and consumes `pw_reveal_attach()` and `pw_prompt_show()` by the names in the Interfaces blocks below.

## Global Constraints

- From the spec: "an eye button at the right edge of every password field shows the password in plain text only while the mouse button is held down on it."
- From the spec: "Press and hold the left mouse button on it: the field shows plain text (`EM_SETPASSWORDCHAR` 0, then redraw). Release anywhere, capture lost, the window deactivated or the field emptied/destroyed: the mask comes back immediately. The button captures the mouse on press so a release outside it is still seen."
- From the spec: "Revealing never copies or selects text and does not change the field's contents or caret."
- From the spec: "Hover state via `ns_hover`; a tooltip "Hold to show password"." Tooltip text is exactly `Hold to show password`.
- From the spec: "A new `NS_ICON_EYE` is added to the icon set (`src/ui/icons.h/.c`), drawn with design-system tokens and `ns_scale`."
- Non-goal: "Reveal that stays on (a toggle), or reveal by keyboard shortcut alone." The eye is not a tab stop and takes no keyboard input.
- Anti-goal: "A revealed password left on screen after the button is released, the pointer leaves, the dialog loses focus, or capture is lost."
- Design-system gate (CLAUDE.md, enforced by `tests/test_ui_tokens.c`): no `RGB(` in `src/ui/*.c`; no `MulDiv(x, dpi, 96)` or local scale macro, use `ns_scale(px, dpi)`; no `CreateFont`, use `ns_font(role, dpi)`; sizes from `ns_type.h` (`SP_*`, `SZ_*`, `R_CTRL`, `STROKE_*`); colours from `ns_tokens()` converted with `theme_cr()`, blends with `rgb_alpha()`; hover through `ns_hover`.
- TDD: write the failing test first. Native tests are declared and called in `tests/runner.c`; Win32 tests are declared and called in `tests/win_runner.c`. Tests that write files build paths from `TEST_TMP_DIR`.
- Portable logic in `src/core/` (native-tested); `src/ui/*.c` is excluded from `make test` (`NON_TEST_SRCS`). Everything compiles clean with `-Werror` under MinGW and native gcc. `winsock2.h` before `windows.h` in any file that needs both.
- Preserve each file's existing line endings (`src/core/ai_prompt.c`, `src/config/loader.c`, `src/term/buffer.c` and the `Makefile` are CRLF; most others LF). New files are LF. Before each review run `git diff --stat --ignore-cr-at-eol` and confirm no file flipped.
- `make test` passes after every task (`Tests Run: N, Failed: 0`). Tasks that touch `src/ui/` also compile-check with MinGW without producing a binary (`x86_64-w64-mingw32-gcc <CFLAGS from the Makefile> -c <file> -o /dev/null`) and, from Task 2 on, run `xvfb-run -a make wintest` on `automaton` (it builds only `build/win/test_runner.exe`, not the product exe).
- The version bump and `make clean && make release` happen once, in Task 6. The version is one patch above both `origin/main`'s version and the branch's last build, computed at build time; never a literal in this plan. The patch never passes 99 (after `1.x.99` comes `1.(x+1).0`).
- Commits: stage new files by path and read every `??` line of `git status --short` (never `git add -u` alone). Message body ends with a `Models:` line naming every model that touched the change and what it did, then `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` and the session trailer the tooling adds.
- `secure_zero()` (`src/core/secure_zero.h`) for every buffer that held a password or passphrase.

## Review Focus

1. **Release outside the eye or outside the dialog, or Alt+Tab while holding.** Expected: masked at once. Pinned: Task 2 `test_pw_reveal_win_capture_lost_masks`, `test_pw_reveal_win_release_outside_masks`, `test_pw_reveal_win_mask_all_masks_descendants`; hosts wire `WM_ACTIVATE`/`WA_INACTIVE` in Tasks 3-5.
2. **The field is cleared or replaced while revealed** (Session Manager's New button runs `form_clear`; choosing another saved profile runs `form_load`). Expected: masked, and the mask character afterwards is the control's original one, not `*` in place of a bullet. Pinned: Task 2 `test_pw_reveal_win_emptied_while_revealed_masks`, `test_pw_reveal_win_press_shows_release_restores_mask`.
3. **Ctrl+C or the context menu's Copy while revealed** (an edit without `ES_PASSWORD` allows copy). Expected: nothing is copied. Pinned: Task 2 `test_pw_reveal_win_copy_blocked_while_revealed`.
4. **The edit is resized or the DPI changes** (Settings re-lays out its pages; a monitor move). Expected: the eye stays at the right edge and typed text never runs under it. Pinned: Task 2 `test_pw_reveal_win_eye_tracks_resize_and_margin`.
5. **Pointer dragged off the eye while held, then back.** Expected: masked while off (anti-goal "the pointer leaves"), revealed again on return while still held, like a push button. Pinned: Task 1 `test_pw_reveal_leave_masks_enter_while_held_reveals`, Task 2 `test_pw_reveal_win_drag_off_masks_drag_back_reveals`.

## Decisions this plan makes (spec "Left to the builder", and readings)

1. **Inside, for every field.** The eye is a child window of the `EDIT` itself, at the right of its client area, with `WS_CLIPCHILDREN` added to the edit and the edit's right margin set to the eye's width. It moves, hides and resizes with the edit, so no dialog layout changes (`resource.rc` and Settings' `add_ctrl` are untouched).
2. **Pointer leaves while held (spec tension, listed in the report).** The decided section says a release outside is still seen (capture); the anti-goals say a reveal left on screen after "the pointer leaves" is a failure. This plan masks when the captured pointer leaves the eye and reveals again if it returns while the button is still down; release anywhere masks for good.
3. **Deactivation** is covered twice: `WM_CAPTURECHANGED`/`WM_CANCELMODE` on the eye, and each host calls `pw_reveal_mask_all(hwnd)` on `WM_ACTIVATE` with `WA_INACTIVE`.
4. **Copy is blocked while revealed**: the edit subclass swallows `WM_COPY` and `WM_CUT` while revealed (the spec's "never copies").
5. **The passphrase prompt is themed** as part of its move to `src/ui/pw_prompt.c` (Task 5): the eye's colours come from `ns_tokens()` (`bg_secondary`, `text_dim`, `text_main`), which only match a token-themed field; Session Manager and Settings already colour edits `text_main` on `bg_secondary`.
6. **Eye geometry**: width `ns_scale(SZ_ICON + SP_SM, dpi)` (28 px at 96 DPI), full client height; icon square `ns_scale(SZ_ICON, dpi)` clamped to the client height minus `ns_scale(SP_XS, dpi)`, centred.
7. **Colours**: background `bg_secondary.base` at rest, `.hover` when hot, `.pressed` while held; glyph `text_dim` at rest, `text_main` when hot or held.

## File structure

| File | Responsibility | Task |
|---|---|---|
| `src/core/pw_reveal_state.h`, `.c` (new) | Pure reveal state machine | 1 |
| `tests/test_pw_reveal_state.c` (new) | Its native tests | 1 |
| `src/ui/icons.h`, `src/ui/icons.c` | `NS_ICON_EYE` | 2 |
| `src/ui/pw_reveal.h`, `.c` (new) | Eye window class, edit subclass, tooltip, drawing | 2 |
| `tests/test_pw_reveal_win.c` (new), `tests/win_runner.c`, `Makefile` | Win32 tests of the helper | 2 |
| `src/ui/session_manager.c` | Attach to `IDC_EDIT_PASS`; mask on deactivate | 3 |
| `src/ui/settings.c` | Attach to `IDC_AI_KEY_EDIT`; mask on deactivate | 4 |
| `src/ui/pw_prompt.h`, `.c` (new), `src/ui/window.c` | Passphrase prompt moved, themed, eye attached | 5 |
| `src/ui/resource.h`, `README.md`, `build/win/nutshell.exe` | Version and build | 6 |

---

### Task 1: The reveal state machine

**Files:**
- Create: `src/core/pw_reveal_state.h`, `src/core/pw_reveal_state.c`
- Create: `tests/test_pw_reveal_state.c`
- Modify: `tests/runner.c` (declarations near the `test_conn_prompt.c` block, calls in `main`)

**Interfaces:**
- Consumes: nothing.
- Produces:
  ```c
  typedef enum {
      PWR_PRESS, PWR_RELEASE, PWR_LEAVE, PWR_ENTER,
      PWR_CAPTURE_LOST, PWR_DEACTIVATE, PWR_EMPTIED, PWR_DESTROY
  } PwRevealEvent;
  typedef struct { int pressed; int inside; int revealed; } PwRevealState;
  void pw_reveal_init(PwRevealState *s);
  /* Returns 1 when s->revealed changed, else 0. NULL s returns 0. */
  int  pw_reveal_feed(PwRevealState *s, PwRevealEvent ev, int field_has_text);
  ```
  Rules: `PRESS` sets pressed=1, inside=1, revealed=field_has_text. `RELEASE`, `CAPTURE_LOST`, `DEACTIVATE`, `DESTROY` set pressed=0, revealed=0. `LEAVE` sets inside=0, revealed=0. `ENTER` sets inside=1, revealed=(pressed && field_has_text). `EMPTIED` sets revealed=0 and leaves pressed alone.

- [ ] **Step 1: Write the failing tests** in `tests/test_pw_reveal_state.c`:
  - `test_pw_reveal_press_reveals_only_with_text`: press with text → `revealed == 1`, feed returns 1; fresh state, press with `field_has_text = 0` → `revealed == 0`, returns 0.
  - `test_pw_reveal_release_masks`: press, release → `revealed == 0`, `pressed == 0`, returns 1; a second release returns 0.
  - `test_pw_reveal_capture_lost_and_deactivate_mask`: press then `PWR_CAPTURE_LOST` → masked; press then `PWR_DEACTIVATE` → masked; both clear `pressed`.
  - `test_pw_reveal_leave_masks_enter_while_held_reveals`: press, `PWR_LEAVE` → masked, `pressed == 1`; `PWR_ENTER` with text → revealed; release → masked.
  - `test_pw_reveal_enter_without_press_stays_masked`: `PWR_ENTER` on a fresh state → `revealed == 0`, returns 0.
  - `test_pw_reveal_emptied_masks_until_next_press`: press, `PWR_EMPTIED` → masked, returns 1; `PWR_ENTER` with `field_has_text = 0` → still masked; release, press with text → revealed.
  - `test_pw_reveal_destroy_masks_and_clears_press`: press, `PWR_DESTROY` → `revealed == 0`, `pressed == 0`.
  - `test_pw_reveal_null_safe`: `pw_reveal_init(NULL)` does not crash; `pw_reveal_feed(NULL, PWR_PRESS, 1) == 0`.
  Declare all eight in `tests/runner.c` under a `/* test_pw_reveal_state.c */` comment and add `failed += ...();` calls.

- [ ] **Step 2: Run to verify they fail**
  Run: `make test 2>&1 | tail -5`
  Expected: link errors for `pw_reveal_init`/`pw_reveal_feed`.

- [ ] **Step 3: Implement** `pw_reveal_init` and `pw_reveal_feed` in `src/core/pw_reveal_state.c` per the rules above (no `windows.h`; header guarded `NUTSHELL_PW_REVEAL_STATE_H`).

- [ ] **Step 4: Run to verify they pass**
  Run: `make test 2>&1 | tail -1`
  Expected: `Tests Run: N, Failed: 0` with N eight above the previous baseline.

- [ ] **Step 5: Commit**
  ```bash
  git add src/core/pw_reveal_state.h src/core/pw_reveal_state.c tests/test_pw_reveal_state.c tests/runner.c
  git commit   # "feat(core): hold-to-reveal state machine" + Models line + trailers
  ```

### Task 2: The eye icon and the reveal helper

**Files:**
- Modify: `src/ui/icons.h` (add `NS_ICON_EYE` immediately before `NS_ICON_COUNT`), `src/ui/icons.c` (append its glyph as the last `g_glyphs` row; add `[NS_ICON_EYE] = 255` to `k_fill` in `ns_icon_draw`)
- Create: `src/ui/pw_reveal.h`, `src/ui/pw_reveal.c`
- Create: `tests/test_pw_reveal_win.c`
- Modify: `tests/win_runner.c` (declare and call the new tests), `Makefile` (`WIN_ONLY_TESTS += tests/test_pw_reveal_win.c`; `WIN_TEST_SRCS += src/ui/pw_reveal.c src/core/pw_reveal_state.c tests/test_pw_reveal_win.c`; `WIN_TEST_LDFLAGS` gains `-lcomctl32`)

**Interfaces:**
- Consumes: Task 1's `PwRevealState`, `pw_reveal_feed`. `ns_tokens()` (`src/ui/ns_tokens.h`; in the wintest binary it is already defined by `tests/test_chat_select_win.c`, so do not define it again), `theme_cr()` (`themed_button.h`), `ns_scale()`, `get_window_dpi()` (`dpi_util.h`), `ns_hover_*` (`src/core/ns_hover.h`), `ns_icon_draw()`.
- Produces (`src/ui/pw_reveal.h`, used by Tasks 3-5 and by plan 2's password prompt box):
  ```c
  /* Attach an eye button inside the right edge of `edit` (an ES_PASSWORD
   * EDIT). Idempotent: a second call returns the existing eye. Returns the
   * eye's HWND, or NULL on failure (the field still works, unrevealable).
   * Everything is released when the edit is destroyed. */
  HWND pw_reveal_attach(HWND edit);
  /* Mask every revealed field among root's descendants (hosts call it on
   * WM_ACTIVATE with WA_INACTIVE). NULL is a no-op. */
  void pw_reveal_mask_all(HWND root);
  /* 1 while `edit`'s text is shown in plain text, else 0 (tests, review). */
  int  pw_reveal_is_revealed(HWND edit);
  ```
  Window class name `NutshellPwEye`.

The glyph (`NS_ICON_EYE`, on the 0..128 grid): an almond outline and a filled pupil.
```c
{ OP_MOVE, 12, 64, OP_CURVE, 36, 28, 92, 28, 116, 64,
  OP_CURVE, 92, 100, 36, 100, 12, 64, OP_CLOSE, OP_STROKE,
  OP_DOT, 64, 64, 14, OP_END },
```
`tests/test_icons.c` loops to `NS_ICON_COUNT`, so it covers the new glyph with no change there.

- [ ] **Step 1: Write the failing Win32 tests** in `tests/test_pw_reveal_win.c` (`#ifdef _WIN32`). A shared fixture creates a hidden top-level window and a child `EDIT` with `WS_CHILD | WS_VISIBLE | ES_PASSWORD | ES_AUTOHSCROLL | WS_BORDER`, 200x24, text `s3cret`, records `orig = (WCHAR)SendMessageW(edit, EM_GETPASSWORDCHAR, 0, 0)`, calls `pw_reveal_attach(edit)` and finds the eye with `FindWindowExA(edit, NULL, "NutshellPwEye", NULL)`. Press is `SendMessage(eye, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(2, 2))`, release `WM_LBUTTONUP`.
  - `test_pw_reveal_win_press_shows_release_restores_mask`: `ASSERT_NOT_NULL(eye)`; after press `SendMessageW(edit, EM_GETPASSWORDCHAR,0,0) == 0` and `pw_reveal_is_revealed(edit) == 1`; after release it equals `orig` and `pw_reveal_is_revealed(edit) == 0`; `pw_reveal_attach(edit)` again returns the same eye.
  - `test_pw_reveal_win_text_caret_selection_unchanged`: `EM_SETSEL(2, 4)` first; after press and after release `GetWindowTextA` is `s3cret` and `EM_GETSEL` gives start 2, end 4.
  - `test_pw_reveal_win_capture_lost_masks`: press, then `SendMessage(eye, WM_CAPTURECHANGED, 0, 0)` → masked; press, then `WM_CANCELMODE` → masked.
  - `test_pw_reveal_win_release_outside_masks`: press, then `WM_LBUTTONUP` with `MAKELPARAM(-50, -50)` → masked.
  - `test_pw_reveal_win_drag_off_masks_drag_back_reveals`: press; `WM_MOUSEMOVE` (`MK_LBUTTON`) at `MAKELPARAM(-50, 2)` → masked; at `MAKELPARAM(2, 2)` → revealed; release → masked.
  - `test_pw_reveal_win_mask_all_masks_descendants`: press, `pw_reveal_mask_all(top)` → masked.
  - `test_pw_reveal_win_emptied_while_revealed_masks`: press, `SetWindowTextA(edit, "")` → `pw_reveal_is_revealed(edit) == 0` and `EM_GETPASSWORDCHAR == orig`.
  - `test_pw_reveal_win_empty_field_never_reveals`: `SetWindowTextA(edit, "")`, press → `EM_GETPASSWORDCHAR == orig`.
  - `test_pw_reveal_win_copy_blocked_while_revealed`: put `sentinel` on the clipboard (`OpenClipboard(top)`, `EmptyClipboard`, `SetClipboardData(CF_TEXT, ...)`), `EM_SETSEL(0, -1)`, press, `SendMessage(edit, WM_COPY, 0, 0)`, release; the clipboard's `CF_TEXT` is still `sentinel`.
  - `test_pw_reveal_win_eye_tracks_resize_and_margin`: `SetWindowPos(edit, NULL, 0, 0, 320, 30, SWP_NOMOVE | SWP_NOZORDER)`; the eye's rect mapped to the edit's client satisfies `right == client.right` and `top == 0`, `bottom == client.bottom`; `HIWORD(SendMessage(edit, EM_GETMARGINS, 0, 0)) >= eye width`.
  - `test_pw_reveal_win_destroy_while_revealed_is_clean`: press, `DestroyWindow(edit)`; `IsWindow(eye) == 0`; a fresh edit attaches again without failure.
  Declare and call all eleven in `tests/win_runner.c` (alongside the icon tests); `InitCommonControls()` once before them.

- [ ] **Step 2: Run to verify they fail**
  Run: `xvfb-run -a make wintest 2>&1 | tail -20` (Windows box: `mingw32-make wintest`)
  Expected: link errors for `pw_reveal_attach` and friends.

- [ ] **Step 3: Add `NS_ICON_EYE`** to `icons.h`/`icons.c` as given above.

- [ ] **Step 4: Implement `src/ui/pw_reveal.c`**
  - Per-edit state (`calloc`ed): `PwRevealState st`, `NsHover hover`, `WCHAR mask` (read with `SendMessageW(EM_GETPASSWORDCHAR)` at attach), `HWND eye`, `HWND tip`.
  - `pw_reveal_attach`: register the class once (`GetModuleHandle(NULL)`, `CS_HREDRAW | CS_VREDRAW`, arrow cursor, no background brush); add `WS_CLIPCHILDREN` to the edit; `SetWindowSubclass(edit, edit_proc, PW_REVEAL_SUBCLASS_ID, (DWORD_PTR)state)`; create the eye `WS_CHILD | WS_VISIBLE` (no `WS_TABSTOP`) as a child of the edit; lay it out (below); create a `TOOLTIPS_CLASS` window with one `TTF_IDISHWND | TTF_SUBCLASS` tool for the eye, text `Hold to show password`.
  - Layout (`edit_proc` on `WM_SIZE` and `WM_DPICHANGED_AFTERPARENT`, and once at attach): eye width `ns_scale(SZ_ICON + SP_SM, dpi)` with `dpi = get_window_dpi(edit)`, at `x = client.right - width`, `y = 0`, full client height; `EM_SETMARGINS` with `EC_RIGHTMARGIN` only, right margin = eye width.
  - Apply: whenever `pw_reveal_feed` returns 1, `SendMessageW(edit, EM_SETPASSWORDCHAR, revealed ? 0 : state->mask, 0)` then `InvalidateRect(edit, NULL, TRUE)`; touch nothing else.
  - Eye messages: `WM_LBUTTONDOWN` → `SetCapture(eye)`, `ns_hover_press`, feed `PWR_PRESS` (has_text = `GetWindowTextLengthW(edit) > 0`); `WM_MOUSEMOVE` → `TrackMouseEvent(TME_LEAVE)`, `ns_hover_move(&hover, inside ? 1 : -1)`, and while pressed feed `PWR_LEAVE`/`PWR_ENTER` when the point crosses the eye's client rect; `WM_MOUSELEAVE` → `ns_hover_leave`; `WM_LBUTTONUP` → feed `PWR_RELEASE`, `ns_hover_release`, `ReleaseCapture()`; `WM_CAPTURECHANGED` → `PWR_CAPTURE_LOST`; `WM_CANCELMODE` → `PWR_DEACTIVATE` and `ReleaseCapture()`; `WM_MOUSEACTIVATE` → `MA_NOACTIVATE` (the eye never takes focus); invalidate the eye only when `NsHoverChange.changed` or the reveal changed.
  - Paint: fill with `theme_cr(ns_tokens()->bg_secondary.base/.hover/.pressed)` by `ns_hover_state_for`/pressed; `ns_icon_draw(hdc, NS_ICON_EYE, &icon_rc, theme_cr(hot || pressed ? text_main : text_dim), dpi)`.
  - `edit_proc`: `WM_COPY`/`WM_CUT` while revealed → return 0 without calling `DefSubclassProc`; after `DefSubclassProc` for any message, if revealed and `GetWindowTextLengthW(edit) == 0` feed `PWR_EMPTIED`; `WM_NCDESTROY` → feed `PWR_DESTROY`, `RemoveWindowSubclass`, destroy the tooltip, `free(state)`.
  - `pw_reveal_mask_all`: `EnumChildWindows(root, ...)` over every window of class `NutshellPwEye`, feeding its edit's state `PWR_DEACTIVATE` and releasing capture if the eye holds it.

- [ ] **Step 5: Run the gates and the Win32 tests**
  Run: `make test 2>&1 | tail -1` then `xvfb-run -a make wintest 2>&1 | tail -5`
  Expected: `Failed: 0` for both; the colour and scale gates in `test_ui_tokens.c` list `pw_reveal.c` with 0 literal `RGB()`.

- [ ] **Step 6: Commit**
  ```bash
  git add src/ui/icons.h src/ui/icons.c src/ui/pw_reveal.h src/ui/pw_reveal.c tests/test_pw_reveal_win.c tests/win_runner.c Makefile
  git commit   # "feat(ui): hold-to-reveal eye button helper and NS_ICON_EYE"
  ```

### Task 3: Profile editor password

**Files:**
- Modify: `src/ui/session_manager.c` (`WM_INITDIALOG` after the `IDC_EDIT_PASS` `EM_SETLIMITTEXT` at ~526; a new `case WM_ACTIVATE` in `SessMgrDlgProc`)

**Interfaces:**
- Consumes: `pw_reveal_attach`, `pw_reveal_mask_all` (Task 2).
- Produces: nothing new.

- [ ] **Step 1: Attach** with `pw_reveal_attach(GetDlgItem(hwnd, IDC_EDIT_PASS))` after the font loop in `WM_INITDIALOG` (so the font's `WM_SETFONT` reaches the edit first). The eye is hidden and shown with the edit by `toggle_kind_fields()`; nothing there changes.
- [ ] **Step 2: Mask on deactivate**: `case WM_ACTIVATE: if (LOWORD(wParam) == WA_INACTIVE) pw_reveal_mask_all(hwnd); break;` (return FALSE, so default handling continues).
- [ ] **Step 3: Verify**
  Run: `make test 2>&1 | tail -1`; the MinGW compile check of `src/ui/session_manager.c`.
  Expected: `Failed: 0`; the compile check prints nothing.
- [ ] **Step 4: Commit** `src/ui/session_manager.c` — "feat(session-manager): hold-to-reveal on the profile password".

### Task 4: AI API key in Settings

**Files:**
- Modify: `src/ui/settings.c` (the API key block at ~1366-1378; `SettingsWndProc` at ~1166 gains `WM_ACTIVATE`)

**Interfaces:**
- Consumes: `pw_reveal_attach`, `pw_reveal_mask_all`.
- Produces: nothing new.

- [ ] **Step 1: Attach** with `pw_reveal_attach(ed)` right after the `EM_SETLIMITTEXT` call on the `IDC_AI_KEY_EDIT` edit. `make_edit2`'s left margin of 3 stays; the helper sets only the right margin.
- [ ] **Step 2: Mask on deactivate** in `SettingsWndProc`: on `WM_ACTIVATE` with `WA_INACTIVE` call `pw_reveal_mask_all(hwnd)` and fall through to the existing handling (the API key edit is a descendant of the page window, which `EnumChildWindows` reaches).
- [ ] **Step 3: Verify** as Task 3 Step 3, for `src/ui/settings.c`.
- [ ] **Step 4: Commit** `src/ui/settings.c` — "feat(settings): hold-to-reveal on the AI API key".

### Task 5: The passphrase prompt moves to `pw_prompt.c`, themed, with the eye

**Files:**
- Create: `src/ui/pw_prompt.h`, `src/ui/pw_prompt.c`
- Modify: `src/ui/window.c` (delete `PassCtx`, `pass_wnd_proc` and `prompt_passphrase` at ~1158-1322; the `WM_CONN_PROMPT` handler at ~4896 calls `pw_prompt_show`)

**Interfaces:**
- Consumes: `pw_reveal_attach`, `pw_reveal_mask_all`; `ns_tokens()`, `theme_cr()`, `themed_apply_title_bar()` (`themed_button.h`), `ns_draw_button()` (`ns_draw.h`), `ns_font(FONT_BODY, dpi)`, `ns_scale()`, `SP_*`/`SZ_CTRL_H`/`SZ_BTN_MIN_W`.
- Produces (plan 2 extends this struct and signature for its password prompt box):
  ```c
  typedef struct {
      const char        *title;  /* window caption, e.g. "SSH Key Passphrase" */
      const char        *label;  /* text above the field, e.g. "Enter key passphrase:" */
      const ThemeColors *theme;  /* for the title bar; NULL leaves it default */
  } PwPromptOpts;
  /* Modal prompt owned by `parent`. Returns 1 on OK with the text in `out`
   * (NUL-terminated, at most out_size - 1 bytes), 0 on Cancel, close or
   * WM_QUIT (re-posted). Every internal copy is wiped with secure_zero(). */
  int pw_prompt_show(HWND parent, const PwPromptOpts *opts, char *out, int out_size);
  ```

Behaviour carried over unchanged from `pass_wnd_proc`/`prompt_passphrase` (read their comments before deleting them; keep the reasoning comments in the new file): the loop ends on `!IsWindow(hwnd)`, never on `WM_QUIT`; a `WM_QUIT` seen by the loop is re-posted (and `gm == -1` is not); `was_disabled` re-enables `parent` only if this call disabled it; `WM_DESTROY` clears the edit's own text; the result buffer is `secure_zero`ed on every path; the window is centred on `parent`; `IsDialogMessageA` gives Enter/Esc/Tab. The message pump is a local `TranslateMessage`/`DispatchMessage` pair (window.c's `pump_dispatch` is static).

New in the move: geometry from the grid (`SP_MD` padding, field `SZ_CTRL_H` tall and `ns_scale(264, dpi)` wide, buttons `SZ_BTN_MIN_W` by `SZ_CTRL_H`, all through `ns_scale` with `get_window_dpi(parent)`); `ns_font(FONT_BODY, dpi)` on every child; background `bg_primary.base`, `WM_CTLCOLORSTATIC` `text_main` on `bg_primary.base`, `WM_CTLCOLOREDIT` `text_main` on `bg_secondary.base` (brushes created once per window, deleted in `WM_DESTROY`); OK/Cancel `BS_OWNERDRAW`, painted with `ns_draw_button` (OK on `accent`, Cancel on `raised`); `pw_reveal_attach` on the edit; `WM_ACTIVATE`/`WA_INACTIVE` → `pw_reveal_mask_all(hwnd)`. Class name stays `NutshellPassDlg` (the integration harness may find it by class).

- [ ] **Step 1: Create `pw_prompt.c`/`.h`** with the behaviour above.
- [ ] **Step 2: Switch `window.c`**: in `WM_CONN_PROMPT`, replace `prompt_passphrase(hwnd, passbuf, (int)sizeof(passbuf))` with `pw_prompt_show(hwnd, &(PwPromptOpts){ "SSH Key Passphrase", "Enter key passphrase:", g_theme }, passbuf, (int)sizeof(passbuf))`; delete the three old definitions; `#include "pw_prompt.h"`. Update the comments in `window.c` that name `prompt_passphrase()` to name `pw_prompt_show()`.
- [ ] **Step 3: Verify**
  Run: `make test 2>&1 | tail -1`; MinGW compile checks of `src/ui/pw_prompt.c` and `src/ui/window.c`; `grep -n "prompt_passphrase\|pass_wnd_proc" src/ui/*.c`.
  Expected: `Failed: 0`; compile checks silent; the grep finds nothing.
- [ ] **Step 4: Commit** `src/ui/pw_prompt.h src/ui/pw_prompt.c src/ui/window.c` — "refactor(ui): passphrase prompt moves to pw_prompt.c, themed, with hold-to-reveal".

### Task 6: Version, build, verify

Runs on `automaton` (or the Windows box with `mingw32-make`); a cloud session cannot (no MinGW) and must stop before it.

**Files:**
- Modify: `src/ui/resource.h` (`APP_VERSION`, `APP_VERSION_BINARY`), `README.md` (`**Version**:` line; the Features list line "Password and SSH key authentication..." gains "and a hold-to-reveal eye on every password field")
- Modify: `build/win/nutshell.exe`

**Interfaces:**
- Consumes: every earlier task.
- Produces: the committed exe whose FileVersion equals `APP_VERSION`.

- [ ] **Step 1: Compute the version**: `git fetch origin main`; read `APP_VERSION` from `git show origin/main:src/ui/resource.h` and from the working tree; next = one patch above the larger, rolling `1.x.99` to `1.(x+1).0`.
- [ ] **Step 2: Write it** into `APP_VERSION` (`"a.b.c"`), `APP_VERSION_BINARY` (`a,b,c,0`) and README's `**Version**: va.b.c`.
- [ ] **Step 3: Build**: `make clean && make release`. If it fails after `make clean`, `git restore build/win/nutshell.exe` before anything else.
  Expected: no error; `strings -el build/win/nutshell.exe | grep -F "<next>"` prints the version.
- [ ] **Step 4: Verify**: `make test 2>&1 | tail -1` (`Failed: 0`); `xvfb-run -a make wintest 2>&1 | tail -3` (`Failed: 0`); `.github/scripts/check-version.sh origin/main` exits 0.
- [ ] **Step 5: Eye check on Windows** (manual): Session Manager password, Settings > AI Provider > API Key, and an SSH key profile with a wrong stored passphrase (passphrase prompt) on one dark and one light theme: eye at the right edge, tooltip, hover tint, text revealed only while held, masked on release outside, on drag off, on Alt+Tab; at 150 % DPI the eye scales and text stops short of it.
- [ ] **Step 6: Commit** `src/ui/resource.h README.md build/win/nutshell.exe` — "build: v<next>".

---

## Self-review

1. **Spec coverage.** Eye on the profile editor (Task 3), key passphrase prompt (Task 5), AI API key (Task 4); the password prompt box is plan 2's, through `pw_reveal_attach`. `NS_ICON_EYE` with tokens and `ns_scale` (Task 2). Press/hold shows plain text via `EM_SETPASSWORDCHAR` 0 and redraw; release anywhere, capture lost, deactivation, emptied, destroyed all mask (Tasks 1-2, hosts in 3-5). Capture on press (Task 2). No copy, no select, no content or caret change (Task 2 tests). `ns_hover` and the exact tooltip (Task 2). Inside or beside: decided inside (Decision 1). Non-goals respected: no toggle, no keyboard reveal.
2. **Step scan.** Every code step names its file, signature and the values the spec pins; bodies are given only for the glyph op-stream, which nothing else determines.
3. **Type consistency.** `PwRevealEvent`/`PwRevealState`/`pw_reveal_feed` (Task 1) are what Task 2 consumes; `pw_reveal_attach`/`pw_reveal_mask_all`/`pw_reveal_is_revealed` (Task 2) are what Tasks 3-5 and the tests use; `PwPromptOpts`/`pw_prompt_show` (Task 5) are what plan 2 extends.
4. **Review Focus.** Five lines, each with its pinning test in Task 1 or 2 and host wiring in 3-5.
5. **Proportion.** Six tasks; code blocks are interfaces and one glyph.
