# Notice message boxes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move the 19 remaining OK-only message boxes in `src/ui` onto the shared `Nutshell_Notice` dialog, and add a native gate that fails when an OK-only `MessageBox` reappears.

**Architecture:** Each box's title, text and tone become one row of a table in `src/core/notice.c` (content as data, as the spec asks), built by `notice_msg()`. A thin `notice_dlg_msg()` in `src/ui/notice_dlg.c` builds and shows one. Each call site swaps its `MessageBox` for `notice_dlg_msg()` with the same owner and the same early return. A source-scanning gate in `tests/test_ui_tokens.c`, modelled on the colour gate, holds the remaining `MessageBox` calls to an exact per-file allow-list.

**Tech Stack:** C11, Win32 (MinGW cross-compile), native test runner, Wine `wintest`, PowerShell harness (unrunnable here).

**Spec:** `docs/superpowers/specs/2026-11-23-ai-assistant-enable-switch-design.md` §3 (the bullet "The other 19 OK-only message boxes move in a follow-up pull request"; the form, owner and timing bullets above it). CLAUDE.md "Design system rules", "Notices use the shared notice dialog".

## Global Constraints

- Each box keeps its text and behaviour; tone follows today's icon: `MB_ICONINFORMATION` -> `NOTICE_INFO`; `MB_ICONWARNING`, `MB_ICONERROR` -> `NOTICE_WARNING`.
- Title = today's caption, unchanged (decision: no caption reads badly enough to change; none is reworded).
- Owner = the window that raises it today. Worker threads never show one (all 19 are raised on the UI thread today; keep it so).
- Out of scope: yes/no prompts, the host-key prompt (`WM_CONN_PROMPT`), `window.c`'s registration/creation failures (NULL owner), moving field validation beside fields.
- All strings ASCII. Files here are LF; keep each file's line endings.
- Both builds `-Werror`; MinGW adds `-Wshadow -Wconversion -Wformat=2`.
- Version: one above both `origin/main`'s and the branch's last build, at build time.

## Review Focus

1. A notice owned by a child window (the docked AI panel) must still disable the main window, as `MessageBox` does (it uses the root owner). -> Task 2 wintest.
2. An argument containing `%` (a session named `50%s`) must print literally. -> Task 1 test.
3. An argument longer than `Notice.opening` (768) is truncated and NUL-terminated, never overflowing. -> Task 1 test.
4. A NULL or empty argument gives a readable notice, not a blank one. -> Task 1 test.
5. An out-of-range id returns 0 with a blank notice and does not crash; `notice_dlg_msg` then shows nothing. -> Task 1 test (core half).

---

### Task 1: The message table in `src/core/notice.[ch]`

**Files:**
- Modify: `src/core/notice.h`, `src/core/notice.c`
- Test: `tests/test_notice.c`, `tests/runner.c` (declare and call new tests)

**Interfaces:**
- Produces (in `notice.h`):

```c
typedef enum {
    NOTICE_MSG_CLOSE_CONNECTING = 0, /* window.c on_tab_close        */
    NOTICE_MSG_TAB_LIMIT,            /* window.c tabs_add failed      */
    NOTICE_MSG_NO_RECONNECT_PROFILE, /* window.c on_status_click      */
    NOTICE_MSG_DEBUG_LOG_MEM_ONLY,   /* window.c open_debug_log_folder */
    NOTICE_MSG_DEBUG_LOG_PATH_INVALID,
    NOTICE_MSG_SESSION_NOT_FOUND,    /* arg: the session name          */
    NOTICE_MSG_CONNECTION_ERROR,     /* arg: the error text            */
    NOTICE_MSG_SELECT_TO_EDIT,       /* session_manager.c              */
    NOTICE_MSG_SELECT_TO_DELETE,
    NOTICE_MSG_HOSTNAME_SAVE,
    NOTICE_MSG_HOSTNAME_CONNECT,
    NOTICE_MSG_LOG_FOLDER_INVALID,   /* settings.c debug_open_folder   */
    NOTICE_MSG_MODELS_ERROR,         /* arg: ctx->result ("Error: ...") */
    NOTICE_MSG_MODELS_NONE,
    NOTICE_MSG_MODELS_NO_URL,
    NOTICE_MSG_MODELS_NO_KEY,
    NOTICE_MSG_FOLDER_MISSING,       /* open_dir.h; arg: the title     */
    NOTICE_MSG_FOLDER_IS_FILE,       /* open_dir.h; arg: the title     */
    NOTICE_MSG_CHAT_SAVE_FAILED,     /* ai_chat.c Save                 */
    NOTICE_MSG_COUNT
} NoticeMsgId;

/* Fills `n` with message `id` (no list, no closing, no action). `arg` is
 * used only by the ids noted above and is always data, never a format.
 * Returns 1, or 0 with a blank notice (title "") for an out-of-range id or
 * NULL `n` (then nothing is written). */
int notice_msg(Notice *n, NoticeMsgId id, const char *arg);
```

The table (exact copy from today's boxes; `arg` kinds: NONE ignores it, TEXT makes it the opening, NAME formats it into the opening, TITLE makes it the title):

| id | tone | title | opening |
|---|---|---|---|
| CLOSE_CONNECTING | info | `Close Tab` | `Connection in progress. Please wait.` |
| TAB_LIMIT | info | `Tab Limit` | `Maximum number of tabs reached.` |
| NO_RECONNECT_PROFILE | info | `Reconnect` | `No connection profile available for reconnection.` |
| DEBUG_LOG_MEM_ONLY | info | `Debug Log Folder` | `No log folder is writable, so Nutshell is keeping the log in memory only. Mark Issue still works.` |
| DEBUG_LOG_PATH_INVALID | warning | `Debug Log Folder` | `The log folder path is not valid.` |
| SESSION_NOT_FOUND (NAME) | warning | `Session Not Found` | `Session "<arg>" not found.` (NULL arg -> empty name) |
| CONNECTION_ERROR (TEXT) | warning | `Connection Error` | arg; NULL/empty -> `The connection failed.` |
| SELECT_TO_EDIT | info | `Edit` | `Select a session first.` |
| SELECT_TO_DELETE | info | `Delete` | `Select a session to delete.` |
| HOSTNAME_SAVE | warning | `Save` | `Please enter a hostname.` |
| HOSTNAME_CONNECT | warning | `Connect` | `Please enter a hostname.` |
| LOG_FOLDER_INVALID | warning | `Open Log Folder` | `That log folder path is not valid.` |
| MODELS_ERROR (TEXT) | warning | `Refresh Models` | arg; NULL/empty -> `Error: the models request failed.` |
| MODELS_NONE | info | `Refresh Models` | `No models found.` |
| MODELS_NO_URL | warning | `Refresh Models` | `Cannot determine models URL for this provider.` |
| MODELS_NO_KEY | warning | `Refresh Models` | `Enter an API key first.` |
| FOLDER_MISSING (TITLE) | warning | arg; NULL/empty -> `Open Folder` | `That folder does not exist and could not be created.` |
| FOLDER_IS_FILE (TITLE) | warning | arg; NULL/empty -> `Open Folder` | `That path is a file, not a folder, so it was not opened. Choose a folder for the log.` |
| CHAT_SAVE_FAILED | warning | `Save Error` | `Failed to save file.` |

- [ ] **Step 1: Write the failing tests** in `tests/test_notice.c`:
  - `test_notice_msg_table_matches_todays_boxes`: a local expected table (id, tone, title, opening) for every NONE-kind row above; for each, `notice_msg` returns 1, `ASSERT_EQ(n.tone, ...)`, `ASSERT_STR_EQ` title and opening, `item_count == 0`, `closing[0] == 0`, `action[0] == 0`, `action_page == -1`.
  - `test_notice_msg_every_id_has_title_and_text`: for every id `< NOTICE_MSG_COUNT` with `arg = "x"`, title and opening are non-empty.
  - `test_notice_msg_session_not_found_formats_name`: arg `"web01"` -> `Session "web01" not found.`; arg `"50%s %d"` -> `Session "50%s %d" not found.` (literal).
  - `test_notice_msg_text_args`: CONNECTION_ERROR with `"Host unreachable"` -> opening equals it, title `Connection Error`, tone warning; with NULL and `""` -> `The connection failed.`; MODELS_ERROR with `"Error: HTTP 401"` -> equals it; NULL -> `Error: the models request failed.`
  - `test_notice_msg_title_args`: FOLDER_MISSING with `"Debug Log Folder"` -> title `Debug Log Folder`; with NULL -> `Open Folder`; FOLDER_IS_FILE with `"Open Log Folder"` -> that title and the file text.
  - `test_notice_msg_long_arg_truncates`: a 2,000-char arg for CONNECTION_ERROR -> `strlen(opening) == sizeof(opening) - 1`; a 300-char title arg -> `strlen(title) == sizeof(title) - 1`.
  - `test_notice_msg_bad_id`: `notice_msg(&n, NOTICE_MSG_COUNT, NULL)` and `(NoticeMsgId)-1` return 0 with `n.title[0] == 0`; `notice_msg(NULL, NOTICE_MSG_TAB_LIMIT, NULL)` returns 0.
- [ ] **Step 2:** `make test` fails to compile (no `notice_msg`).
- [ ] **Step 3:** Implement `notice_msg` in `notice.c` with a static table indexed by id (reuse `blank()`/`set()`), using `snprintf` with `"%s"` for every argument.
- [ ] **Step 4:** `make -j test` passes; the new tests run.
- [ ] **Step 5:** Commit `feat(notice): message table for the remaining OK-only boxes`.

### Task 2: `notice_dlg_msg()` and the root owner

**Files:**
- Modify: `src/ui/notice_dlg.h`, `src/ui/notice_dlg.c`
- Test: `tests/test_notice_dlg_win.c`, `tests/win_runner.c` (or wherever the `test_notice_dlg_win_*` tests are declared/called)

**Interfaces:**
- Consumes: `notice_msg()` (Task 1).
- Produces: `int notice_dlg_msg(HWND owner, NoticeMsgId id, const char *arg, const ThemeColors *theme);` builds with `notice_msg` and returns `notice_dlg_show(...)`; returns 0 without showing anything when `notice_msg` returns 0.
- Changes `notice_dlg_show`: when `owner` has `WS_CHILD`, it uses `GetAncestor(owner, GA_ROOT)` as the owner (for disabling, centring, DPI and the foreground restore), matching `MessageBox`, which takes the root owner.

- [ ] **Step 1: Write the failing wintests** in `tests/test_notice_dlg_win.c`, following the existing close-by-posted-key pattern there:
  - `test_notice_dlg_win_child_owner_disables_root`: a top-level window with a `WS_CHILD` child; show a notice owned by the child; while it is up (in the hook/timer the file already uses to close it) assert `IsWindowEnabled(top) == FALSE` and `GetWindow(notice, GW_OWNER) == top`; after it returns, `IsWindowEnabled(top)` and `IsWindowEnabled(child)` are TRUE.
  - `test_notice_dlg_win_msg_shows_table_text`: `notice_dlg_msg(owner, NOTICE_MSG_TAB_LIMIT, NULL, NULL)` opens a `Nutshell_Notice` whose caption is `Tab Limit` and whose `NOTICE_ID_OPENING` text is `Maximum number of tabs reached.`; Escape returns 0.
  - `test_notice_dlg_win_msg_bad_id_shows_nothing`: `notice_dlg_msg(owner, NOTICE_MSG_COUNT, NULL, NULL)` returns 0 at once and no `Nutshell_Notice` window exists.
- [ ] **Step 2:** `xvfb-run -a make wintest` fails (link error / first assertion).
- [ ] **Step 3:** Implement both changes.
- [ ] **Step 4:** `xvfb-run -a make wintest` and `make -j test` pass.
- [ ] **Step 5:** Commit `feat(notice): notice_dlg_msg, and a child owner means its root window`.

### Task 3: The MessageBox gate, then the 19 call sites

**Files:**
- Test: `tests/test_ui_tokens.c`, `tests/runner.c`
- Modify: `src/ui/window.c`, `src/ui/session_manager.c`, `src/ui/settings.c`, `src/ui/open_dir.h`, `src/ui/ai_chat.c`, `CLAUDE.md` ("Design system rules": the intro's "Two native tests ... enforce the first two rules" sentence and the notice bullet name the new gate)

**Interfaces:**
- Consumes: `notice_dlg_msg()` (Task 2).
- Changes: `static inline int open_dir_in_explorer(HWND hwnd, const char *dir, const char *title, const ThemeColors *theme)`; `open_dir.h` includes `notice_dlg.h`.

- [ ] **Step 1: Write the failing gate** `int test_ui_tokens_msgbox_gate(void)` in `tests/test_ui_tokens.c`, beside the colour gate and reusing `read_whole_file`:
  - Scans every `src/ui/*.c` and `src/ui/*.h` (skip, as the colour gate does, when `src/ui` cannot be opened).
  - Before counting, blanks out `/* */` and `//` comments and string/char literals, so prose such as "a host-key MessageBox" or `"MessageBox("` in a string is not counted.
  - Counts calls: the identifier `MessageBox`, `MessageBoxA`, `MessageBoxW`, `MessageBoxExA`, `MessageBoxExW` or `MessageBoxIndirectA/W` (whole word: previous char not `[A-Za-z0-9_]`), followed by optional whitespace and `(`.
  - Exact per-file allow-list, each entry commented with what it is; every other file must have 0; prints `[msgbox gate] <file> MessageBox calls: N (allowed M)` per non-zero or mismatched file:
    - `window.c` 4: the Disconnect yes/no (`on_status_click`), the `WM_CONN_PROMPT` worker prompt (the host-key prompt), and the two NULL-owner registration/creation failures in `WinMain`/`ui_run`.
    - `settings.c` 2: the two yes/no prompts (`int ans = MessageBoxA` and `CMD_POLICY_CONFIRM_TEXT`).
    - `session_manager.c` 2: "Delete Session" and "Save Profile" yes/no.
    - `ai_chat_status.c` 1: the Auto-approve confirm.
  - A comment above the list: a new notice uses `notice_dlg_msg`/`notice_dlg_show`; a new yes/no prompt is added here deliberately; never an OK-only box (CLAUDE.md).
  - Also `test_ui_tokens_msgbox_gate_counter` (pure, no files): the counter function on fixtures: `"MessageBoxA(h,\"a\",\"b\",MB_OK);"` -> 1; `"/* a MessageBox( in a comment */ x = 1;"` -> 0; `"s = \"MessageBoxA(\";"` -> 0; `"MyMessageBoxA(h);"` -> 0; `"MessageBox (h, ...); MessageBoxW(h, ...);"` -> 2; `"// MessageBoxA(\n MessageBoxExA(h,0,0,0,0);"` -> 1.
- [ ] **Step 2:** `make test`: the counter test passes once written against the new static helper; the gate fails, listing window.c 11 (allowed 4), settings.c 7 (2), session_manager.c 6 (2), open_dir.h 2 (0), ai_chat.c 1 (0).
- [ ] **Step 3: Convert the 19 sites** (find each by its text; same owner, same control flow and return afterwards; `(void)` the result):
  - `window.c` (theme `g_theme`): `on_tab_close` -> CLOSE_CONNECTING, owner `GetParent(g_hwndTabs)`; tab limit -> TAB_LIMIT, owner `GetParent(g_hwndTabs)`; `on_status_click` reconnect -> NO_RECONNECT_PROFILE, owner `hParent`; `open_debug_log_folder` -> DEBUG_LOG_MEM_ONLY and DEBUG_LOG_PATH_INVALID, owner `hwnd`, and `open_dir_in_explorer(hwnd, dir, "Debug Log Folder", g_theme)`; `WM_STARTUP_CONNECT` -> SESSION_NOT_FOUND with arg `wanted` (drop `nf_text`), then the `PostMessage` as today; the connection result -> CONNECTION_ERROR with arg `conn_error`, still skipped for `conn_result == 4`.
  - `session_manager.c` (theme `st->theme`, owner `hwnd`): SELECT_TO_EDIT, SELECT_TO_DELETE, HOSTNAME_SAVE, HOSTNAME_CONNECT.
  - `settings.c` (theme `d->theme`, owner `hwnd`): LOG_FOLDER_INVALID and `open_dir_in_explorer(hwnd, dir, "Open Log Folder", d->theme)`; MODELS_ERROR with arg `ctx->result`; MODELS_NONE; MODELS_NO_URL; MODELS_NO_KEY. In `WM_AI_MODELS_DONE`, keep `ctx` alive until after the notice returns (it is released after, as today), and do not touch `d` after a notice returns (the window may be gone).
  - `open_dir.h`: FOLDER_MISSING and FOLDER_IS_FILE with arg `title`, owner `hwnd`, the new `theme` parameter; update the header comment ("a notice says why").
  - `ai_chat.c`: CHAT_SAVE_FAILED, owner `hwnd` (the panel; a docked panel is a child and Task 2 resolves it to the main window), theme from the panel's data (`d->theme` or equivalent; NULL is accepted).
- [ ] **Step 4:** `make -j test` passes with the gate at allow-list; `make clean && make release` compiles clean under MinGW (`-Werror -Wshadow -Wconversion`), then `git restore build/win/nutshell.exe` (the real build is Task 5).
- [ ] **Step 5:** Commit `feat(notice): move the 19 OK-only message boxes onto the notice dialog`.

### Task 4: Integration harness and docs

**Files:**
- Modify: `tests/integration/cases/*.ps1`, `tests/integration/README.md`, `docs/superpowers/specs/2026-09-06-ui-redesign-notes.md` (Open list)

- [ ] **Step 1:** Survey `tests/integration` (cases, `NutshellIT.psm1`, README) for anything that waits for, reads or closes one of the 19 boxes (captions `Close Tab`, `Tab Limit`, `Reconnect`, `Debug Log Folder`, `Open Log Folder`, `Session Not Found`, `Connection Error`, `Edit`, `Delete`, `Save`, `Connect`, `Refresh Models`, `Save Error`), by caption or by class `#32770`, including reading its text from a `Static` child (a notice's opening is control id 101, `NOTICE_ID_OPENING`; see `50-window.ps1`'s notice reader), and any "no dialog appeared" assertion that filters only `#32770` and would now miss a `Nutshell_Notice` (e.g. `20-ai.ps1`'s panel cases): extend those filters to `#32770|Nutshell_Notice`. Leave the CLI `MessageBox` (`70-cli.ps1`, `src/main.c`) and the yes/no prompts alone. Title-matching checks (`85-telnet.ps1`'s `Test-TelnetErrorBoxOpen`) keep working because the notice's caption is its title; fix only wording that says "message box".
- [ ] **Step 2:** Update README case rows whose wording changes. No `pwsh` here: list every changed case as unrun.
- [ ] **Step 3:** Add to the notes document's Open list: the changed harness cases (unrun), and a Windows hand check of each of the 19 notices (owner, tone, text, Enter/Escape, focus back to the owner; the docked AI panel's Save Error disables the main window).
- [ ] **Step 4:** Commit `test(integration): the moved boxes are Nutshell_Notice dialogs`.

### Task 5: Version, build, full verification

- [ ] **Step 1:** Bump `APP_VERSION`/`APP_VERSION_BINARY` (`src/ui/resource.h`) and README `**Version**:` to one above `origin/main`'s and the branch's last build.
- [ ] **Step 2:** `make clean && make release`; commit `build/win/nutshell.exe` with the bump: `build: rebuild nutshell.exe (vX.Y.Z)`.
- [ ] **Step 3:** All pass: `make -j test`, `make test SAN=1`, `make lint-gate`, `make fuzz-run`, `xvfb-run -a make wintest`.
- [ ] **Step 4:** Line endings: `git diff --stat origin/main...HEAD` equals `--ignore-cr-at-eol`.
