# Enable AI assistant switch and the notice dialog: plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** an Enable AI assistant switch (off by default, on for a config that
already has a key) under which Nutshell's own code loads none of the
assistant, plus the shared themed notice dialog that reports added settings,
a first run and the configuration warnings.

**Architecture:** portable decisions in `src/core/` and `src/config/`
(first-load rule, missing-field report, notice content and order, notice
geometry, the runtime gate, the cancel token, the DLL record), Win32 wiring
in `src/ui/`. Order: config and notice first, then the gate, then the UI
gates, then cancellation, then the self-report, so no commit is broken.

**Tech Stack:** C11, Win32, MinGW cross-compile, native gcc tests, Wine for
`wintest` and the self-report check, PowerShell harness (unrun here).

**Spec:** `docs/superpowers/specs/2026-11-23-ai-assistant-enable-switch-design.md`
(sections 1-6; section 7 decided below). Principle: CLAUDE.md "Optional
features: off unless enabled, not loaded while off" and the design-system
rule "Notices use the shared notice dialog".

Branch `feat/ai-switch` from `ef74f19`. Every task is a Sonnet implementer
(`claude-sonnet-5-5`), tests first, with a Sonnet task review. Line endings
are preserved: `git diff --stat` must equal `git diff --stat
--ignore-cr-at-eol`. CRLF files touched here include `Makefile`,
`src/config/loader.c`, `src/core/ai_prompt.[ch]`, `src/core/tab_manager.[ch]`,
`src/ui/ai_http_win.c`, `tests/test_tabs.c`, `tests/test_settings.c`; new
files are LF. Every new test is declared and called in `tests/runner.c`
(native) or `tests/win_runner.c` (wintest); a new `tests/*_win.c` joins
`WIN_ONLY_TESTS` and `WIN_TEST_SRCS`.

## Global Constraints

- JSON key `"ai_enabled"`, a bool, in `settings`; `Settings.ai_enabled` (int).
- First load lacking the field: on when `ai_api_key` or
  `ai_api_key_enc_preserved` is non-empty after loading, else off. A saved
  field wins. `config_new_default()` gives off.
- New install, first run: off.
- Missing fields are added and the file saved at once (in `WM_CREATE`),
  notices only queued there and shown at the top of `WM_STARTUP_CONNECT`;
  under `--ui-demo` launch notices are discarded.
- Notice: title, opening line, optional list of bold label + plain value,
  optional closing line, up to two buttons (optional action, then OK
  rightmost); tone info or warning changes only the accent and icon.
  Window class `Nutshell_Notice`; caption = title. Real `STATIC`/`BUTTON`
  children. Colours `ns_tokens()`, fonts `ns_font`, buttons
  `ns_draw_button`, sizes `ns_type.h`, scale `ns_scale`. Not topmost,
  centred on owner, owner's DPI, `WM_DPICHANGED` as `cheatsheet_dlg.c`.
- Keys: Enter presses the focused button, Escape or the close box = OK, Tab
  and Shift+Tab move between buttons, Ctrl+C copies the notice as plain text.
- Exact copy (spec §3 table and wording), with `<ver>` = `APP_VERSION`:
  - Upgrade: title `Welcome to Nutshell <ver>`; opening `This version
    introduces new settings, and your configuration has been updated to
    include them:`; closing `Everything else is just as you left it.`;
    action `Review in Settings`.
  - First run: title `Welcome to Nutshell <ver>`; opening `No configuration
    was found, so a new one has been created beside nutshell.exe. These
    optional features start switched off:` (when the file went to
    %LOCALAPPDATA%: `... has been created in <folder>. ...`); closing
    `Everything else uses the standard settings.`; action `Review in
    Settings`.
  - AI item: label `AI assistant`; value `on, as your API key is already
    set up` or `off until you choose to turn it on`.
  - Warnings, titles and texts exactly as the spec's table (no folder,
    unreadable, invalid with the real `.bad-` name, save failed at launch,
    save failed in Settings / Session Manager with each one's existing text).
- Yes/no prompts, `window.c:6277,6304` and transient status stay as they
  are; the 19 other OK boxes are NOT moved in this PR.
- `ns_winhttp_open()` returns NULL with `ERROR_ACCESS_DENIED` while the gate
  is off.
- Turn-off confirm text: `Turn off the AI assistant? The running reply will
  be stopped and open conversations closed.`
- No voice settings fields in this PR.

## Section 7 decisions (left to the builder)

- **Menu items are removed**, not greyed: View > "AI Assist" and "Undock AI
  Assist", and File > "Save AI Chat..." with its own separator. The View
  menu keeps Fullscreen.
- **Notice visual form:** a 4 px accent bar down the left edge and a round
  16 px glyph (i / !) beside the title, both in the tone's intent colour
  (`info` / `warning`); title `FONT_TITLE`, body `FONT_BODY`, list labels
  bold body; client width `ns_scale(460)`; buttons right-aligned, OK
  rightmost and the default focus.
- **Module boundary:** `src/core/ai_gate.[ch]` (runtime gate, read by
  workers), `src/core/ai_netcancel.[ch]` (cancel token / handle registry),
  `src/core/ai_session_discard` in `ai_prompt.c` (clearing one session's AI
  state), and `ai_feature_on()` / `ai_feature_off()` in `window.c`, which
  owns the panel, the menu, the sessions and the tab strip.
- **Which fields an upgrade lists:** every settings key `config_save()`
  writes (the known-key table). A key counts as present when it, or the
  legacy key it is migrated from (`ai_auto_approve_mode`,
  `ai_auto_approve_default` for `ai_policy_default`), is in the file. The
  legacy read-only `debug_terminal` is not a known key. Profile fields are
  not part of the rule (they keep today's silent defaults).
- **Help sheet filter:** rows flagged `SC_AI` are left out while off: all of
  "AI Assist panel" and the AI rows of "Mouse".
- **Voice hook:** `src/core/opt_feature.[ch]` lists the optional features
  (AI only today); voice will append itself with `requires = OPT_AI`.

## Review Focus

1. A config whose save fails after an upgrade or first run: the info notice
   must not appear; the save-failed warning must (test in Task 2).
2. An invalid config: warning first, then a save; if that save fails, both
   warnings, invalid first (test in Task 2).
3. Turning off while a card is waiting or a reply is streaming: no hang, no
   use-after-free, conversation discarded in every session, not saved back
   (Task 6 idle path, Task 7 working path; wintest of the token, native test
   of `ai_session_discard`).
4. A Settings save in a `--ui-demo` run never writes `ai_enabled` from the
   demo override (test in Task 4: the override never touches `Settings`).
5. A key typed in Settings while the switch is still off: Refresh makes no
   request (Task 6: Refresh disabled until saved on; `ns_winhttp_open`
   refuses regardless, wintest in Task 4).

---

### Task 1: the `ai_enabled` field, the known-key table and the load report

**Files:**
- Modify: `src/config/config.h`, `src/config/loader.c` (CRLF),
  `src/core/settings_help.[ch]` (new `SH_AI_ENABLE`, first on Provider)
- Create: `src/core/opt_feature.[ch]`
- Test: `tests/test_config.c`, `tests/test_settings_help.c`,
  new `tests/test_opt_feature.c`

**Interfaces (produces):**
- `int ai_enabled;` in `Settings` (after `ai_provider`).
- `#define CFG_KEYS_MAX 64`
  `typedef struct { const char *key; const char *legacy[2]; int sh_id; const char *label; } ConfigSettingKey;`
  `const ConfigSettingKey *config_setting_keys(size_t *count);` the keys
  `config_save()` writes, in file order. `label` is NULL when `sh_id` names
  a settings_help entry whose label (trailing ':' dropped) is used; else the
  plain label (e.g. `debug_levels` -> "Debug log levels").
- `typedef struct { ConfigLoadStatus status; char bad_path[700];
  unsigned char missing[CFG_KEYS_MAX]; size_t missing_count;
  int ai_enabled_decided; } ConfigLoadReport;` (`missing` holds indices
  into the key table, file order; `ai_enabled_decided` is 1 when the
  first-load rule ran).
- `Config *config_load_report(const char *path, ConfigLoadReport *rep);`
  `config_load_ex()` becomes a wrapper. `bad_path` is filled on
  `CONFIG_LOAD_INVALID` (from `config_backup_unparseable_file()`).
- `opt_feature.h`: `typedef enum { OPT_AI = 0, OPT_COUNT } OptFeature;`
  `typedef struct { OptFeature id; const char *name; const char *json_key; int requires; } OptFeatureInfo;`
  (`requires` -1 for none), `const OptFeatureInfo *opt_feature_table(size_t *n);`
  `int opt_feature_enabled(const Settings *s, OptFeature f);`
  `const char *opt_feature_blocked_reason(const Settings *s, int requires);`
  returns `"Needs the AI assistant"` when `requires == OPT_AI` and AI is
  off, NULL otherwise (voice calls it with `OPT_AI`).

- [ ] **Step 1: failing tests** in `tests/test_config.c`:
  `test_config_ai_enabled_missing_no_key_off`,
  `test_config_ai_enabled_missing_with_key_on`,
  `test_config_ai_enabled_missing_preserved_blob_on` (fake DPAPI fails to
  decrypt a foreign blob -> preserved non-empty -> on),
  `test_config_ai_enabled_saved_false_wins_over_key`,
  `test_config_ai_enabled_saved_true_without_key`,
  `test_config_ai_enabled_default_off`,
  `test_config_ai_enabled_roundtrip` (and add it to
  `test_config_roundtrip_all_fields`),
  `test_config_older_exe_drop_readds_on` (save with key and ai_enabled
  false, strip the `ai_enabled` line as an older exe would, reload: on,
  `ai_enabled_decided` 1, `missing` names `ai_enabled`),
  `test_config_report_complete_file_has_no_missing` (a file written by
  `config_save()` reloads with `missing_count == 0`),
  `test_config_report_lists_missing_keys_in_order` (drop
  `ai_thinking_mode` and `ai_enabled`: both reported, table order),
  `test_config_report_legacy_key_counts_present` (`ai_auto_approve_mode`
  without `ai_policy_default` -> not missing),
  `test_config_report_no_settings_object_all_missing`,
  `test_config_keys_match_saved_file` (parse a saved file's `settings`
  object: every key is in the table and every table key is in the file,
  same order), `test_config_report_invalid_names_bad_path` (bad_path
  starts with the config path + `.bad-` and exists),
  `test_config_report_missing_and_unreadable_status`.
  `tests/test_settings_help.c`: count 48 -> 49, `SH_AI_ENABLE` in
  `k_names`, label `"Enable AI assistant"`, page Provider, default
  "Off" text. `tests/test_opt_feature.c`: table has AI first with key
  `ai_enabled`; `opt_feature_enabled`; blocked reason NULL/text both ways.
- [ ] **Step 2:** `make -j test` fails on the new tests.
- [ ] **Step 3:** implement. Load `ai_enabled` after the key is loaded;
  write it after `ai_provider` in `config_save()`; the missing scan runs
  over `jset` (all keys missing when absent).
- [ ] **Step 4:** `make -j test` green (summary line unchanged elsewhere).
- [ ] **Step 5:** commit `feat(config): ai_enabled with the first-load rule
  and a report of missing settings`.

### Task 2: notice content, order and layout (portable)

**Files:**
- Create: `src/core/notice.[ch]`, `src/core/notice_layout.[ch]`
- Test: new `tests/test_notice.c`, `tests/test_notice_layout.c`

**Interfaces:**
- Consumes: Task 1's `ConfigLoadReport`, `config_setting_keys()`,
  `opt_feature_table()`, `settings_help_get()`.
- Produces (`notice.h`):
  ```c
  typedef enum { NOTICE_INFO = 0, NOTICE_WARNING = 1 } NoticeTone;
  #define NOTICE_ITEMS_MAX 24
  typedef struct { char label[64]; char value[160]; } NoticeItem;
  typedef struct {
      NoticeTone tone;
      char title[128];
      char opening[768];
      NoticeItem items[NOTICE_ITEMS_MAX];
      int  item_count;           /* items beyond the cap: last item "...and N more" */
      char closing[256];
      char action[48];           /* "" = no action button */
      int  action_page;          /* SETTINGS_PAGE_*, -1 = none */
  } Notice;
  #define NOTICE_QUEUE_MAX 4
  typedef struct { Notice n[NOTICE_QUEUE_MAX]; int count; } NoticeQueue;
  void notice_queue_init(NoticeQueue *q);
  int  notice_queue_push(NoticeQueue *q, const Notice *n); /* warnings before info, stable; 0 when full */
  void notice_no_folder(Notice *n);
  void notice_unreadable(Notice *n);
  void notice_invalid(Notice *n, const char *bad_name);  /* base name of bad_path */
  void notice_save_failed_launch(Notice *n);
  void notice_save_failed(Notice *n, const char *text);  /* Settings / Session Manager */
  int  notice_upgrade(Notice *n, const char *version, const ConfigLoadReport *rep,
                      const Settings *s);                /* 0 = nothing added, no notice */
  void notice_first_run(Notice *n, const char *version,
                        const char *folder_or_null);     /* NULL = beside nutshell.exe */
  typedef struct {
      ConfigLoadStatus status; int no_folder; const ConfigLoadReport *rep;
      int saved;        /* a save was attempted */
      int save_ok;
      const char *version; const char *fallback_folder; /* NULL when beside the exe */
      const Settings *s;
  } NoticeLaunch;
  void notice_launch_plan(NoticeQueue *q, const NoticeLaunch *in);
  size_t notice_plain_text(const Notice *n, char *out, size_t cap);
  ```
  Upgrade item value: `ai_enabled` -> the AI item values (Global
  Constraints); any other key -> its settings_help `default_text`, else
  `the standard setting`. `action_page` = page of the first item (its
  `sh_id`'s page; Provider for `ai_enabled`).
- Produces (`notice_layout.h`):
  `typedef struct { int dpi, opening_h, closing_h, item_count, label_w, value_h[NOTICE_ITEMS_MAX], has_action; } NoticeLayoutIn;`
  `typedef struct { NsRect bar, icon, title, opening, label[NOTICE_ITEMS_MAX], value[NOTICE_ITEMS_MAX], closing, action, ok; int client_w, client_h, text_w; } NoticeLayout;`
  `void notice_layout(const NoticeLayoutIn *in, NoticeLayout *out);`
  (use the existing rect type from `ns_layout.h`; heights are measured by
  the dialog and passed in; `text_w` is what the dialog measures with).

- [ ] **Step 1: failing tests**, `tests/test_notice.c`:
  - exact copy of each builder (titles, opening, closing, items);
    `notice_invalid` embeds the real `.bad-` name;
    first run beside exe vs `%LOCALAPPDATA%` folder line; first-run items
    = every optional feature, value `off until you choose to turn it on`;
  - upgrade with only `ai_enabled` missing, decided on -> one item `on, as
    your API key is already set up`; decided off -> `off until ...`;
    `ai_thinking_mode` also missing -> second item with settings_help
    default text; nothing missing -> returns 0; action page Provider;
  - launch plan table: MISSING+save ok -> [first run]; MISSING+save fail
    -> [save failed]; OK+missing+ok -> [upgrade]; OK+missing+fail ->
    [save failed]; OK complete, local profile added, save fail -> [save
    failed]; OK complete, nothing saved -> []; INVALID+ok -> [invalid];
    INVALID+fail -> [invalid, save failed]; UNREADABLE -> [unreadable] and
    never an info notice; no folder -> [no folder];
  - queue: an info pushed before a warning is shown after it; full queue
    refuses; `notice_plain_text` layout (title, blank, opening, `- label:
    value` lines, blank, closing) and truncation at cap.
  `tests/test_notice_layout.c`: at 96 and 192 DPI, buttons right-aligned
  inside the pad, OK right of action, no rect overlaps, rows top to bottom
  in order, no action -> action rect empty and OK in the same place,
  `client_h` grows with each item, 192 DPI = 2x 96 within 1 px.
- [ ] **Step 2:** tests fail; **Step 3:** implement; **Step 4:** `make -j
  test` green; **Step 5:** commit `feat(notice): notice content, launch
  order and layout as portable data`.

### Task 3: the notice dialog, its demo states and gallery case

**Files:**
- Create: `src/ui/notice_dlg.[ch]`, `tests/test_notice_dlg_win.c`
- Modify: `Makefile` (CRLF: `WIN_ONLY_TESTS`, `WIN_TEST_SRCS` gets
  `src/ui/notice_dlg.c src/core/notice.c src/core/notice_layout.c` and what
  they need), `tests/win_runner.c`, `src/core/ui_demo.[ch]`,
  `tests/test_ui_demo.c`, `src/ui/window.c` (demo states only),
  `tests/integration/NutshellIT.psm1` (`DIALOG_CLASSES` gets
  `Nutshell_Notice`), `tests/integration/50-window.ps1` (gallery case),
  `tests/integration/README.md`

**Interfaces:**
- Produces: `int notice_dlg_show(HWND owner, const Notice *n, const ThemeColors *theme);`
  modal, returns 1 when the action button was pressed, 0 for OK, Escape,
  close or WM_QUIT (reposted). The `Notice` is copied at create time.
  `#define NOTICE_ID_TITLE 100, NOTICE_ID_OPENING 101, NOTICE_ID_CLOSING 102,
  NOTICE_ID_LABEL0 110, NOTICE_ID_VALUE0 140, NOTICE_ID_ACTION 1, NOTICE_ID_OK IDOK`.
- Modal loop as `cheatsheet_dlg.c:396-400` / `pw_prompt.c:243-254,337-356`:
  owner re-enabled only if it was enabled, loop ends on `!IsWindow` or the
  closing flag, a real WM_QUIT reposted. Keys are handled in the loop
  (owner-drawn buttons). Ctrl+C copies `notice_plain_text()` via
  `src/ui/clipboard.c`. Dark title bar via `dwm_util`.
- `ui_demo`: states `notice-info` (the upgrade sample, AI on) and
  `notice-warning` (the invalid-config sample), before `all`, not part of
  `all`, posted like `shortcuts` (`window.c:2639-2644`) via a new
  `WM_OPEN_DEMO_NOTICE`; the panel is still created for them.

- [ ] **Step 1: failing wintests** (`tests/test_notice_dlg_win.c`): it
  opens with class `Nutshell_Notice` and caption = title; title, opening,
  each label and value and closing read back from their `STATIC`s; Enter on
  OK returns 0 and the window is gone; Escape returns 0; Enter with the
  action focused returns 1; Tab moves focus OK -> action -> OK; no action
  button when `action` is ""; a disabled owner stays disabled after close,
  an enabled one is re-enabled. Drive keys from a timer or a helper thread
  posting to the dialog, as the pw_prompt wintests do.
  Native: `tests/test_ui_demo.c` state count 22 -> 24, tail order
  `..., shortcuts, notice-info, notice-warning, all` (or keep `shortcuts`
  last before `all` and say so), both build no panel content beyond `all`'s
  baseline and are not in `all`.
- [ ] **Step 2:** tests fail; **Step 3:** implement; **Step 4:** `make -j
  test` and `xvfb-run -a make wintest` green; MinGW compile check
  (`make clean && make -j release`, then `git restore build/win/nutshell.exe`).
- [ ] **Step 5:** harness: `Nutshell_Notice` in `DIALOG_CLASSES`; case
  `notice_dialog_gallery` modelled on `help_shortcuts_sheet_gallery`
  (four themes, `--ui-demo=notice-info` and `--ui-demo=notice-warning`,
  wait for the caption, save `gallery\<Theme>-notice-info.png` and
  `-notice-warning.png`, non-blank). README row. Unrun (no pwsh).
- [ ] **Step 6:** commit `feat(notice): themed Nutshell_Notice dialog with
  demo states`.

### Task 4: launch notices, save at once, the Settings and Session Manager warnings

**Files:**
- Modify: `src/ui/window.c` (WM_CREATE config load `4420-4487`, top of
  `WM_STARTUP_CONNECT` `5233`, the `IDM_EDIT_SETTINGS` post), `src/ui/settings.c`
  (`2248` Save Failed), `src/ui/session_manager.c` (`809`, `911`),
  `tests/integration/NutshellIT.psm1`, `tests/integration/50-window.ps1`,
  `tests/integration/README.md`

**Interfaces:**
- Consumes: Tasks 1-3.
- `static NoticeQueue g_launch_notices;` filled in WM_CREATE by
  `notice_launch_plan()`. One save at launch covers the upgrade, the first
  run, and the "Local shell" profile (`config_ensure_local_profile`): save
  when the file is new, lacked keys or gained the profile and saving is not
  disabled. Shown at the top of `WM_STARTUP_CONNECT`, one at a time, owner
  the main window, before the demo branch and any auto-connect; under
  `CLI_UI_DEMO` the queue is cleared, not shown. An action result posts
  `WM_COMMAND IDM_EDIT_SETTINGS` with `lParam` = `action_page` after the
  dialog has closed.
- Settings and Session Manager save failures call `notice_dlg_show(own
  hwnd, notice_save_failed(text))` with each one's existing text.

- [ ] **Step 1:** the launch decisions are already tested in Task 2; add
  any missing case found while wiring (native) before the code.
- [ ] **Step 2:** implement; remove the four config `MessageBoxA`s and the
  three "Save Failed" boxes; keep `g_config_save_disabled` behaviour.
- [ ] **Step 3:** `make -j test`, MinGW compile check, `xvfb-run -a make
  wintest`.
- [ ] **Step 4:** harness (unrun): `New-NutshellTestEnv`'s settings table
  becomes complete, every key of `config_setting_keys()` with its default
  (`ai_enabled = $false`; replace the legacy `ai_auto_approve_default` with
  `ai_policy_default = "read/none"` or the default token; drop
  `debug_terminal`), so LAUNCH-1 still sees no dialog. LAUNCH-2 expects the
  first-run `Nutshell_Notice` (caption `Welcome to Nutshell <ver>`, its
  `AI assistant` value `off until you choose to turn it on`), dismisses it
  with OK, then checks the file as today. LAUNCH-3 expects the warning
  `Nutshell_Notice` titled `Your configuration couldn't be loaded` naming
  the `.bad-` file that exists. New gate case `launch_upgrade_adds_fields`:
  a complete config minus `ai_enabled`, no key -> notice shown, file now
  holds `"ai_enabled": false`. README rows.
- [ ] **Step 5:** commit `feat(config): add missing settings at launch and
  report them in the notice dialog`.

### Task 5: the runtime gate, the WinHTTP refusal, the shortcut filter, the tab button

**Files:**
- Create: `src/core/ai_gate.[ch]`, `tests/test_ai_gate.c`
- Modify: `src/core/winhttp_open.c`, `src/core/shortcuts.[ch]`,
  `src/ui/cheatsheet_dlg.c`, `src/core/cheatsheet_layout.c` if needed,
  `src/ui/tabs.[ch]`, `src/core/tab_manager.[ch]` (CRLF), `Makefile`
  (wintest gets `src/core/winhttp_open.c src/core/ai_gate.c`, `-lwinhttp`),
  `tests/test_shortcuts.c`, `tests/test_tabs.c` (CRLF), a wintest file

**Interfaces (produces):**
- `void ai_gate_set_enabled(int on); void ai_gate_set_demo_override(int on);
  int ai_gate_on(void);` (enabled || override; `__atomic` loads/stores; the
  override never touches `Settings`).
- `ns_winhttp_open()`: `if (!ai_gate_on()) { SetLastError(ERROR_ACCESS_DENIED); return NULL; }`
- `#define SC_AI 0x?` row flag on every "AI Assist panel" row and the AI
  rows of "Mouse"; `size_t shortcuts_filtered(Shortcut *out, size_t cap, int ai_on);`
  the cheat sheet builds its rows and section headings from the filtered
  copy (`CheatData`). `shortcuts_readme_block()` stays the full table.
- `void tabs_set_ai_visible(HWND tabs, int visible);` hidden = no paint,
  hit, hover or tooltip, and its slot reclaimed; one geometry helper feeds
  all five places, and `tab_manager`'s tooltip takes the flag.

- [ ] **Step 1: failing tests:** `test_ai_gate_default_off`,
  `_enabled`, `_override_only`, `_override_does_not_change_enabled`;
  `test_shortcuts_filtered_off_drops_ai_rows` (no S5 section, no SC_AI
  rows, other sections intact, order kept), `_on_keeps_all`,
  `test_shortcuts_readme_block_unchanged_by_filter`; cheatsheet layout with
  the AI section absent still lays out two columns; tab tooltip/hit tests
  with the AI button hidden (no AI tip, neighbouring buttons shift);
  wintest `test_winhttp_open_refuses_while_off` (NULL,
  `ERROR_ACCESS_DENIED`) and `_opens_while_on` (non-NULL, closed after).
- [ ] **Step 2:** fail; **Step 3:** implement; **Step 4:** `make -j test`,
  `xvfb-run -a make wintest`, MinGW compile check; **Step 5:** commit
  `feat(ai): a runtime gate that WinHTTP, the help sheet and the tab strip
  follow`.

### Task 6: the switch in Settings and the gates in the main window

**Files:**
- Modify: `src/ui/window.c`, `src/ui/settings.c`, `src/ui/settings_dlg.h`,
  `src/core/ai_prompt.[ch]` (CRLF), `src/ui/ai_chat*.c` as needed,
  `tests/test_ai_prompt*.c` or a new `tests/test_ai_session_discard.c`,
  `tests/integration/*.ps1`, `NutshellIT.psm1`, `tests/integration/README.md`

**Interfaces:**
- Consumes: `ai_gate_*`, `tabs_set_ai_visible`, `shortcuts_filtered`.
- Produces: `void ai_session_discard(AiSessionState *st);` (portable): frees
  and re-inits `batches`, `run_log`, `conv` (`valid = 0`), the two stream
  buffers (NULL), clears `notes`, `busy`, activity, every round counter and
  `policy_seeded`; keeps `platform` and `platform_contradicted`.
- `window.c`: `static void ai_feature_on(HWND)` (runs `ai_chat_init` once,
  sets the gate, rebuilds the menu, shows the tab button) and `static void
  ai_feature_off(HWND)` (aborts every stream and cancels the dispatch,
  answers every session's tool cards, destroys the panel docked or not,
  then `ai_session_discard()` on every session, clears
  `g_ai_reopen_after_connect`, rebuilds the menu, hides the tab button,
  clears the gate). `int ui_ai_is_working(void)` for Settings: a reply
  streaming, a dispatch running, or any session with a pending tool batch.
- Gates: `create_ai_chat()` returns NULL while `!ai_gate_on()`;
  `ai_chat_init` leaves `WM_CREATE` for `ai_feature_on` (called in
  `WM_CREATE` only when `settings.ai_enabled`, or from the demo before
  `create_ai_chat`); `IDM_VIEW_AI_CHAT`, `IDM_VIEW_AI_UNDOCK`,
  `IDM_FILE_SAVE_AI` ignored while off; Ctrl+Shift+Space falls through to
  the terminal as an unbound key while off; menu items removed while off.
- Demo: `create_demo_session()` calls `ai_gate_set_demo_override(1)` and
  `ai_feature_on` without writing `settings.ai_enabled`.
- Settings: the `Enable AI assistant` tick box (`IDC_AI_ENABLE`, label from
  `SH_AI_ENABLE`) at the top of Provider; while unticked every other AI
  control on the three AI pages is disabled (live, as the box toggles);
  Refresh is enabled only when the box is ticked and `ai_gate_on()`; OK
  saves `ai_enabled`. After OK, `on_settings_clicked_page` calls
  `ai_feature_on` / `ai_feature_off` on a change. When turning off and
  `ui_ai_is_working()`, OK first asks the Global Constraints' yes/no
  question (`MessageBoxA`, MB_YESNO | MB_ICONWARNING; a yes/no decision is
  not a notice); No keeps the dialog open with the box ticked again.

- [ ] **Step 1: failing tests:** `test_ai_session_discard_clears_everything`
  (fixture with every field non-zero / non-empty and two batches),
  `_keeps_platform`, `_twice_is_safe`.
- [ ] **Step 2:** fail; **Step 3:** implement; **Step 4:** `make -j test`,
  `make test SAN=1`, MinGW compile check, `xvfb-run -a make wintest`.
- [ ] **Step 5:** harness (unrun): `$AiSettings` and the key-free AI gate
  cases (`ai_panel_opens_without_key`, `ai_auto_approve_critical_asks_first`,
  others that open the panel) set `ai_enabled = $true`; the `--ui-demo`
  cases need nothing (override). `menus_open_and_list_items`: File without
  2007 and its separator, View = (2021) by default; a twin with
  `ai_enabled = $true` keeps today's lists. `settings_help_tool_count`
  expects one more tool (count it from `page_track`). New gate case
  `launch_ai_off_no_ai_items`: View has no 2020/2022, posting 2020 and
  Ctrl+Shift+Space open no `Nutshell_AIChat`. README rows.
- [ ] **Step 6:** commit `feat(ai): Enable AI assistant switch; nothing of
  the assistant loads while it is off`.

### Task 7: real cancellation and joined workers

**Files:**
- Create: `src/core/ai_netcancel.[ch]`, `tests/test_ai_netcancel.c`
- Modify: `src/core/ai_stream.[ch]`, `src/ui/ai_http_win.c` (CRLF),
  `src/ui/ai_chat_send.c`, `src/ui/ai_chat_stream.c`, `src/ui/ai_chat.c`,
  `src/core/ai_tool_web_fetch.c`, `src/core/ai_tool_web_search.[ch]`,
  `src/ui/settings.c` (model refresh), `src/ui/window.c` (`ai_feature_off`,
  exit path), tests for web_search/web_fetch cancel

**Interfaces (produces):**
- ```c
  typedef void (*AiNcCloseFn)(uintptr_t h);
  typedef struct { int lock; int cancelled; uintptr_t h; AiNcCloseFn close; } AiNetCancel;
  void ai_nc_init(AiNetCancel *c);
  int  ai_nc_arm(AiNetCancel *c, uintptr_t h, AiNcCloseFn close); /* worker; -1 if already cancelled (worker closes h itself) */
  int  ai_nc_disarm(AiNetCancel *c);   /* worker; 1 = still owns h and must close it, 0 = already closed by fire */
  void ai_nc_fire(AiNetCancel *c);     /* any thread; sets cancelled, takes h under the lock, closes it after unlocking; idempotent */
  int  ai_nc_cancelled(const AiNetCancel *c);
  ```
  One token per `AiStream` (fired by `ai_stream_orphan`), passed to the
  tools beside today's flag; one per model refresh.
- Every WinHTTP request arms its request handle after `WinHttpOpenRequest`
  and disarms before its own close; after any failed call the worker checks
  the token before touching the handle again. web_fetch arms per hop;
  web_search honours cancel between calls, arms its requests, sets explicit
  timeouts and caps its read loops. The ≥300 error-body loop checks cancel.
- Stream threads keep their handles (`ai_chat_send.c`): finished ones are
  reaped when the next stream starts; `int ai_chat_join_streams(DWORD ms)`
  fires every token, then waits on the handles with
  `MsgWaitForMultipleObjects` (pumping sent messages) until all are done or
  `ms` passes, closes the finished handles and returns how many are still
  running (they stay safe as orphans through `WorkerLife`). `ai_feature_off`
  calls it with 2000 ms; the exit path uses it in place of
  `ai_chat_wait_for_streams`.
- The settings model refresh gets a `WorkerLife`, a token and its handle
  kept; closing Settings fires it; its ctx headers (the key) are wiped with
  `secure_zero` on free.
- The Stop button and New Chat use the same fire path, so they are immediate.

- [ ] **Step 1: failing tests:** `test_ai_nc_fire_before_arm_refuses`,
  `_fire_closes_armed_handle_once`, `_disarm_after_fire_returns_zero`,
  `_disarm_without_fire_returns_one`, `_fire_twice_closes_once`,
  `_concurrent_fire_and_disarm_close_exactly_once` (pthread race loop,
  counter == 1 each round, under SAN); web_search: a cancelled flag stops
  before the first request (backend fake or the native stub path); web_fetch:
  a fire between hops stops with the cancel reason.
- [ ] **Step 2:** fail; **Step 3:** implement; **Step 4:** `make -j test`,
  `make test SAN=1`, MinGW compile check, `xvfb-run -a make wintest`;
  **Step 5:** commit `feat(ai): cancel closes the request handle and turning
  the assistant off joins its threads`.

### Task 8: the DLL record and the self-report

**Files:**
- Create: `src/core/dll_record.[ch]`, `tests/test_dll_record.c`,
  `src/ui/dll_hook_win.c`, `tests/selftest/` (an AI-off and an AI-on
  `nutshell.config`, a check script)
- Modify: `src/core/cli_args.[ch]`, `tests/test_cli_args.c`, `src/main.c`
  or `src/ui/window.c` (`WM_SELF_REPORT`), the five run-time load sites
  (`dwm_util.c:26`, `ns_draw.c:549`, `crypto_dpapi.c:52`, `ai_chat.c:2307-2308`),
  `Makefile` (target `selftest-wine`), `tests/integration/` (gate case),
  `.github` untouched

**Interfaces (produces):**
- `void dll_record_note(const char *name);` (case-insensitive, de-duplicated,
  fixed 64 entries, thread-safe with `__atomic`), `int
  dll_record_was_requested(const char *name);`, `size_t
  dll_record_format(char *out, size_t cap, const char *const *modules, size_t n);`
  (lines `requested: <dll>` then `module: <dll>`).
- `dll_hook_win.c`: `PfnDliHook __pfnDliNotifyHook2` (defined in our object,
  `__attribute__((used))`; records on `dliNotePreLoadLibrary`). The
  toolchain's `libdelayimp.a` is empty; the helper is `libmingwex.a`'s
  (fix the Makefile comment). Modules via `CreateToolhelp32Snapshot`.
- `--self-report=<path>`: hidden modifier, allowed with run and `-nc`;
  after `WM_STARTUP_CONNECT` the main window writes the report to `<path>`
  (UTF-8) and closes normally.
- `make selftest-wine`: copies the exe and each config into
  `build/selftest/{off,on}/`, runs `WINEPREFIX=$(abspath build/wineprefix)
  WINEDEBUG=-all timeout 120 xvfb-run -a wine nutshell.exe -nc
  --self-report=report.txt`, asserts AI off: no `requested:` line for
  `winhttp.dll`, `riched20.dll`, `msftedit.dll`; AI on (control): both
  `riched20.dll` and `msftedit.dll` requested. Skip notice when wine or
  xvfb-run is missing. Both configs are complete (no notice blocks).

- [ ] **Step 1: failing tests:** dll_record (note, duplicate, case, cap,
  format with and without modules, NULL/empty name ignored); cli_args
  (`--self-report=x` with run and `-nc`, rejected with `--ui-demo`/`-v`,
  empty path rejected, not in usage text).
- [ ] **Step 2:** fail; **Step 3:** implement; **Step 4:** `make -j test`,
  `xvfb-run -a make wintest`, `make clean && make -j release` then
  `make selftest-wine` passes both runs, then `git restore
  build/win/nutshell.exe`. Harness gate case `self_report_ai_off_clean`
  (same assertion on Windows; unrun). **Step 5:** commit `test: the real
  exe reports no AI DLLs requested while the assistant is off`.

### Task 9: docs, version, build, PR

- [ ] README: Settings > AI Assistant > Provider gains the switch; the
  assistant is off on a new install; the notice; the hidden
  `--self-report` stays undocumented like `--ui-demo` unless README lists
  hidden flags. Integration README case rows.
- [ ] Notes document Open list: every unrun Windows check (harness cases
  touched or added in Tasks 3-8; spec §6 hand checks: upgrading the real
  config with a key, turning off mid-reply, the notice's dark title bar,
  per-monitor DPI and Narrator).
- [ ] Full list: `make -j test`, `make test SAN=1`, `make lint-gate`,
  `make fuzz-run`, `xvfb-run -a make wintest`, `make selftest-wine`.
- [ ] Version one above both `origin/main`'s and the branch's last build,
  `make clean && make release`, commit the exe, push, draft PR.

### Task 10: final review

One Opus agent reads `git diff origin/main...HEAD` (exe excluded) and the
spec, and returns ranked findings; they go to the maintainer before any fix.
