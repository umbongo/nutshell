# Debug logging: a Settings section, AI-readable log files, an issue marker

**Date**: 2026-10-17
**Status**: DRAFT
**Branch**: `feat/debug-logging`
**Prompted by**: the maintainer, 2026-10-17, in their words: "Add debug to the app, create a
new section in the settings for debugging. Present an 'enable debugging' tickbox. Once pressed
show a series of settings for the debug intensity and where they're stored. The goal is to run
this semi-permanently and have AI access and analyse the logs when I notice an issue."
Their decisions: AI access is **both** plain log files a Claude Code session can read **and**
a read-only AI Assist tool; the default folder is `logs\` beside the exe, changeable; terminal
content is an opt-in tickbox, off by default; passwords, passphrases, keys and the AI API key
are never logged.
**Builds on**: `2026-09-07-design-system-foundation-design.md` (tokens, fonts, scale), the
settings page model in `src/core/settings_layout.c`, the tool registry in
`src/core/ai_tools.h`, and the v1.2.34 stale-band investigation in the notes document
("Paint-path follow-ups from the 2026-10-14 stale-band investigation").

---

## Problem

The stale-row redraw bug came back while the AI was installing Rust. The last fix (PR #66,
v1.2.34) was found only because the maintainer ran a special build: `REDRAW_DEBUG` is a
compile-time flag (`src/ui/redraw_log.h`, Makefile `redraw-debug`) that writes three
untimestamped `[REDRAW]` lines to `%TEMP%\nutshell_redraw.log`. It does not say which tab a
paint was for, what the poll loop decided against the 16 ms cooldown, or which session's dirty
state caused the invalidate. Those are exactly what the notes say the next diagnosis needs.

Diagnostics are spread over three unrelated mechanisms:

| Mechanism | Where | Gate | Problem |
|---|---|---|---|
| `logger.c` (`log_init`, `LOG_*`) | `%TEMP%\nutshell.log`, started by `init_app_log()` (`src/main.c:29`) | always on, INFO | no rotation, not thread-safe, second-resolution timestamps, 5 call sites |
| `REDRAW_DEBUG` | `%TEMP%\nutshell_redraw.log` | compile time | needs a special build; no time, tab or cooldown context |
| `debug_terminal` | `<exe dir>\<name>-debug-<time>.log` per session | Settings > Logging tickbox | a new file per connection, forever; unbounded |

None of them can be switched on for weeks and read by an AI afterwards.

---

## Design

### 1. One logger, `dlog`, in `src/core`

`src/core/dlog.h/.c` replaces `logger.h/.c`, `redraw_log.h` and the `debug_terminal` file.
The portable parts are native-tested: line formatting, filtering, redaction, the memory
ring, the file writer, rotation decisions, path resolution and the query used by the AI
tool. The writer thread is Win32 code under `#ifdef _WIN32` in the same module.

```c
typedef enum { DLOG_GENERAL, DLOG_RENDER, DLOG_TERM, DLOG_AI, DLOG_SSH, DLOG_SHELL,
               DLOG_CAT_COUNT } DlogCat;
typedef enum { DLOG_OFF, DLOG_ERROR, DLOG_WARN, DLOG_INFO, DLOG_DEBUG, DLOG_TRACE } DlogLevel;

bool dlog_on(DlogCat cat, DlogLevel lvl);          /* one array read, no lock */
void dlog_write(DlogCat cat, DlogLevel lvl, int tab, const char *event,
                const char *fmt, ...);             /* fmt builds the key=value tail */
#define DLOG(cat, lvl, tab, event, ...) \
    do { if (dlog_on(cat, lvl)) dlog_write(cat, lvl, tab, event, __VA_ARGS__); } while (0)
```

A site that costs anything to describe (counting dirty rows, rendering a cursor row) runs
only behind `dlog_on()`, so a disabled Trace site costs one comparison.

**Categories** are the six the maintainer named, and each one says where a call site belongs.

| Category | Tag in the file | Covers |
|---|---|---|
| General | `general` | startup, version, config load, settings applied, menu actions, shutdown, crashes, the issue marker |
| Rendering | `render` | `WM_PAINT`, `renderer_draw`, `invalidate_terminal`, the poll loop's invalidate and cooldown decision, per-second paint statistics |
| Terminal | `term` | resize, alt screen enter and leave, full-redraw requests, bytes received, terminal content (opt-in) |
| AI Assist | `ai` | stream start and end, HTTP status, latency, tool calls, command dispatch start, settle and stall |
| SSH | `ssh` | connect, auth method, host key result, disconnect reason, errors |
| Local shell | `shell` | shell resolution, ConPTY spawn, exit code, ring overrun, environment failures |

**Levels: one per category, not one global level plus ticks.** The maintainer's real case
is Rendering at Trace while everything else stays at Info. A global level cannot express
that. Category tickboxes plus one level would turn Trace on for every ticked area. Six rows
of `[Category] [Level v]` say exactly what is logged, so each category gets its own level.
The UI offers five values: **Off, Errors, Info, Debug, Trace**. "Errors" admits ERROR and
WARN lines. Internally WARN is its own level so a line can say which it is.

**What each level is for**

| Level | Intended use | Volume, measured on the poll loop |
|---|---|---|
| Errors | always-on baseline | a few lines a day |
| Info | semi-permanent default when debugging is on | lifecycle events, tens of lines an hour |
| Debug | semi-permanent for a suspect area | Rendering emits one `render.stats` line a second, about 5 MB a day |
| Trace | reproduction sessions | every paint and invalidate, about 100 lines a second while a tab is busy |

**When debugging is OFF**, every category runs at Errors, into the same folder, with the same
rotation. The file stays small, and an error or a stall from last Tuesday is still there when
someone asks. The issue marker (section 6) is always written. The old always-on INFO lines
are reclassified:

- The dispatch stall diagnostic (`ai_chat.c:2034`) becomes `ai` WARN, so it still lands with
  debugging off.
- `dispatch_start` and `settled` become `ai` INFO.

### 2. File format: one event per line, logfmt

```
2026-10-17T23:25:56.123+01:00 TRACE render tab=3 poll.invalidate active=1 poll_rc=0 dirty=73 critical=0 since_paint_ms=9 cooldown=hold
2026-10-17T23:25:56.140+01:00 WARN  ai     tab=3 dispatch.stall batch=2 next=cmd sent=1 await_echo=1 quiet=1(3120ms) alt_screen=0 cursor=4,0 term=120x40 row_len=17
2026-10-17T23:25:58.002+01:00 WARN  general tab=3 ISSUE n=1 note="marked by user" png="issue-20261017-232558-1.png"
```

The fixed prefix is: timestamp (local time with milliseconds and UTC offset), level padded
to five characters, category, `tab=<id>` (`tab=0` for app-wide events), and an event name of
the form `area.verb`. The prefix is followed by `key=value` pairs.

- A value containing a space, quote, `=` or backslash is double-quoted, with `\"` and `\\`
  escaped.
- A control character is written as `\n`, `\r`, `\t`, `\e` or `\xHH`.
- Escaping is applied by the formatter itself (`dlog_fmt_value()`), so a line is one line by
  construction.
- Lines are capped at 4 KB. Anything longer is cut at a UTF-8 boundary and ends with
  `truncated=1`.

**Why logfmt rather than JSON Lines.**

- Both are one event per line and grep cleanly.
- logfmt is about 40% shorter for these events, and the in-app tool has a token budget.
- A person can read it in Notepad.
- Every value is a scalar, so JSON buys nothing here.
- An AI reads either format without help.

**Why local time with an offset.** The maintainer will say "it happened at about 23:25". The
offset keeps the time unambiguous across a daylight-saving change.

**Tab ids.** Each `Session` gets a monotonically increasing `log_id` (1, 2, 3, …) when it is
created. The same number appears in every line and in the issue snapshot, so "tab=3" can be
followed across a whole file. The issue snapshot maps the id to the tab's title and position.

### 3. Files, naming and rotation

- The current file is always **`<folder>\nutshell.log`**. It is a stable path that can be
  given to a Claude Code session.
- When it reaches the size cap, it is closed and renamed to
  `nutshell-YYYYMMDD-HHMMSS.log` (the time of the rename), and a new `nutshell.log` is
  started. The timestamped names sort by age and survive restarts.
- Once the rotated files exceed the "files kept" limit, the oldest are deleted.

Defaults are **10 MB per file and 10 files kept**, so the folder holds at most about 110 MB.

| Setting | Range |
|---|---|
| File size | 1–100 MB |
| Files kept | 2–50 |

At those defaults:

- Errors or Info runs for months.
- Debug on Rendering runs for weeks.
- Rendering at Trace on a busy tab keeps about the last two hours. That is enough when the
  maintainer marks the issue within minutes of seeing it.

Rotation is a pure decision, which is what the tests cover:

```c
/* Which rotated files to delete, given the folder's nutshell-*.log and issue-*.png
 * names (any order) and the limits. Never selects nutshell.log or a file that does
 * not match the two patterns exactly. */
int dlog_rotation_victims(const char *const *names, int n, int max_files, int max_png,
                          int *victims /* indices, out */);
```

Rotation also prunes issue screenshots beyond the newest 20.

- **A rename that fails is not fatal.** For example, a reader on Windows holds the file
  without `FILE_SHARE_DELETE`. The writer keeps appending to `nutshell.log` and retries at
  the next flush. The file overshoots its cap briefly rather than losing lines.
- **A second Nutshell instance** cannot open `nutshell.log`, because the writer opens it
  with `_fsopen(..., _SH_DENYWR)`. It writes to `nutshell-pid<PID>.log` instead, and its
  Settings line shows that path. Two processes never interleave writes in one file or race
  a rename.

### 4. The folder

The folder comes from the `debug_log_dir` setting, default `logs`.

1. A relative path is resolved against the exe's directory, so the folder travels with the
   exe and `nutshell.config`, as the config does.
2. An absolute path is used as is.
3. The folder is created if it is missing.
4. If it cannot be created or written, for example because the exe is in Program Files, the
   writer falls back to `%LOCALAPPDATA%\Nutshell\logs`. It writes a `general` WARN saying
   so.
5. If that fails too, it keeps lines in memory only. The Settings line then shows the
   reason.

Path resolution is a pure function, `dlog_resolve_dir(setting, exe_dir, out, cap)`, and is
tested natively. `get_exe_dir()` stays in `window.c`.

### 5. Write path: never stall the UI thread

- **Producers** (the UI thread, the AI stream thread, the connection thread) format the line
  into a stack buffer. They then copy it into a 1 MB in-memory ring under a lock. The lock
  is a `CRITICAL_SECTION` on Windows and a `pthread_mutex` elsewhere, held for one
  `memcpy`.
- **A writer thread** drains the ring to the file every 250 ms, using `fwrite` and then
  `fflush`. It is woken at once by an ERROR, a WARN or an issue marker, so those reach the
  disk within milliseconds. All file I/O, rotation and pruning happen on this thread.
- **Overflow.** When the ring is full, a producer drops the line and increments a counter. It
  never waits. The next line written carries `dropped=N`. At 1 MB the ring absorbs about
  ten seconds of the heaviest Trace even if the disk stalls.
- **Crash safety.** A `SetUnhandledExceptionFilter` handler drains the ring synchronously
  and writes a `general` ERROR `crash code=0x… addr=0x…` line. It chains to any previous
  filter. A hard kill can lose up to 250 ms of Debug or Trace lines, but never an ERROR,
  WARN or issue marker, because those wake the writer at once.
- **Before the config is loaded.** The logger starts in `WinMain` before the config is read.
  Until `dlog_configure()` runs from `WM_CREATE`, lines collect in the ring. The defaults
  are debugging off, levels at Errors, and no file. Nothing from startup is lost.
- **Shutdown.** `dlog_shutdown()` signals the thread, joins it, drains the ring and closes
  the file.

**Settings change at runtime.** `dlog_configure()` takes the new levels, folder and limits on
the UI thread:

- The level table is swapped at once.
- A folder change is handed to the writer, which closes the old file and opens the new one.
- No restart is needed: ticking Enable and pressing Save starts logging immediately.

### 6. The issue marker: "I just saw it"

- **Help > Mark Issue**, shortcut **Ctrl+Shift+M**. The shortcut is
  free: `key_on_keydown` takes Ctrl+Shift+T, W, C, V and Space; plain Ctrl+letters belong to
  the shell; Alt chords go to the shell.
- It follows the T and W pattern: `drop_queued_char()`, ignore auto-repeat, and return
  handled.
- The menu item uses a new `IDM_HELP_MARK_ISSUE` (2031).
- It works whether or not debugging is on.

**What one press writes**, as one block of consecutive lines, flushed immediately:

1. `general WARN ISSUE n=<k> tab=<active> note="marked by user"`. The word `ISSUE` is
   greppable, and `n` counts the markers in this process.
2. `general WARN issue.window client=WxH dpi= iconic= ai_panel=docked|floating|hidden ai_w=
   tabs=<count> active_index=`.
3. **One `issue.session` line per tab**, active first, with these fields:
   - `tab`, `index`, `title`, `kind=ssh|local`, `connected`, `conn_state`
   - `rows`, `cols`, `lines_count`, `lines_start`, `lines_capacity`, `scrollback_offset`
   - `cursor=row,col`, `cursor_visible`, `alt_screen`, `full_redraw`, `scroll=top..bot`,
     `write_seq`
   - `dirty_rows=<count>` and `dirty=<ranges, e.g. 0-3,7>`
4. `issue.paint`, with these fields:
   - `since_paint_ms` (now minus `g_last_paint_tick`) and `cooldown_ms=16`
   - `char=WxH` and `dispbuf=RxC`
   - `prev_cursor=r,c`
   - `render_stats=…`: the last second's paint and invalidate counts, kept by Rendering even
     when its level is below Debug, because they are integer increments
5. With "Include terminal content" ticked, `issue.screen` lines with the active tab's visible
   rows as escaped text, one line per row. Without it, `row_lens=…` only.
6. `issue.png file=<name>`, if the screenshot is enabled and saved.

**The screenshot** is an `issue-YYYYMMDD-HHMMSS-<k>.png` in the log folder. It is taken with
`PrintWindow(hwnd, hdc, PW_RENDERFULLCONTENT)` into a DIB section, then
`GdipCreateBitmapFromHBITMAP`, then `GdipSaveImageToFile` with the PNG CLSID that
`ai_chat.c:88` already hardcodes. GDI+ is already linked and started, so the cost is about
40 lines. A screenshot shows the terminal, so it has its own tickbox, "Save a screenshot with
each issue marker", which defaults to on. The marker is a deliberate press in the moment, and
the file never leaves the folder: the AI Assist tool reads text logs only.

**Feedback.**

- `MessageBeep(MB_OK)`.
- If the AI panel is showing, a status item: `Issue #k marked in the debug log at 23:25:58`.
  It is display only and is never sent to the model.

### 7. Rendering instrumentation: what replaces REDRAW_DEBUG

The three REDRAW_DEBUG sites become `render` Trace lines with the same fields plus the tab:

- `paint.begin` in `WM_PAINT`: `rect`, `dirty_rows`, `full_redraw`.
- `paint.draw` in `renderer_draw`: `rows`, `in_rect`, `rect`, `char_h`, `force_all`, and
  `cleared`, the rows whose dirty bit this paint cleared.
- `invalidate` in `invalidate_terminal`: `rect`, `why`.

The notes asked for more, so the poll loop (`window.c:4052`) adds `poll.invalidate` with
these fields:

- `tab` and `active`
- `poll_rc` and `dirty` (the dirty row count)
- `critical`
- `since_paint_ms`
- `decision=paint|hold`
- `dispbuf_invalidated`

This line is written only when the loop considered an invalidate, so an idle tab writes
nothing.

At **Debug**, Rendering writes `render.stats` once a second, while there was any activity, with
these fields:

- `paints`
- `noop_paints`, paints where no row was drawn
- `invalidates`, per tab
- `holds`, cooldown holds
- `dirty_tabs`

That is enough to see a starving pattern semi-permanently without Trace's volume.

**A stuck-dirty detector**, at WARN so it is logged even with debugging off: if a session that
is not the active tab has had dirty rows for 5 consecutive seconds while the poll loop keeps
invalidating for it, write `render.stuck_dirty tab=… seconds=5 rows=…` once per episode.
That is weakness (a) in the notes, the background tab that drove 22,000 no-op paints. It will
now announce itself.

`redraw_log.h` and the Makefile `redraw-debug` target are removed. They are fully
superseded: the same three lines, plus timestamps, tabs and the poll decision, are available
at runtime.

### 8. Other categories, initial sites

Each category starts with a deliberately small set of sites. Sites are added later when a
diagnosis needs them, so the log is not padded in advance.

- **General**:
  - `app.start` with version, exe dir, Windows build, DPI and the log path
  - `config.load` with its status
  - `settings.apply`, listing which debug settings changed
  - `app.exit`
  - the two window-creation failures at ERROR
  - `crash`
- **Terminal**:
  - `resize` (rows and cols, old to new)
  - `alt_screen.enter` and `alt_screen.leave`
  - `full_redraw` with its reason
  - at Debug, `rx` with byte counts, aggregated per poll tick
  - at Trace with content ticked, `rx data="…"`, the escaped bytes. This replaces
    `debug_terminal`.
- **AI Assist**:
  - `stream.start` (provider, model, request bytes, tool count)
  - `stream.end` (HTTP status, ms, response bytes, stop reason)
  - `tool.call` (name, ms, result bytes, truncated)
  - `dispatch.start`, `dispatch.settled` and `dispatch.stall`
- **SSH**:
  - `connect.start` (host and port)
  - `connect.ok` (auth method, ms)
  - `connect.fail` (stage, libssh2 error code, message)
  - `hostkey` (result: known, new, mismatch)
  - `disconnect` (reason)
- **Local shell**:
  - `resolve` (the chosen shell and the reason)
  - `spawn` (path, pid)
  - `exit` (code)
  - `overrun` (WARN)
  - `env_fail` (WARN)

### 9. Redaction

Redaction is two layers.

1. **By construction.** No call site passes a password, passphrase, key file contents, the
   AI API key, a `$dpapi$`/`$aes256gcm$` blob or an `Authorization` header. Keystrokes the
   user *sends* are never logged as content, only as `tx bytes=N`. That is what keeps a typed
   password out even with content ticked: a password prompt does not echo, so it never
   appears in the received stream.
2. **A scrubber on every formatted line** (`dlog_redact()`, native-tested) as a safety net.
   It replaces the value with `[redacted]` for:
   - keys whose whole name is `password`, `passphrase`, `pass`, `secret`, `token`,
     `api_key`, `apikey`, `authorization` or `private_key`. A key like `keystroke_ms` is not
     touched.
   - `Bearer <token>`
   - `sk-` followed by 16 or more key characters
   - `$dpapi$v1$…` and `$aes256gcm$v1$…` blobs
   - PEM `-----BEGIN … PRIVATE KEY-----` lines

**Terminal content off (the default)** means no text from the terminal reaches the log:

- `dispatch.stall` logs `row_len=17` instead of `row="…"`.
- `issue.session` logs counts and positions, not text.
- `rx` logs byte counts only.

**Terminal content on** records:

- the escaped received bytes (`term rx data=`, Trace)
- the cursor row in `dispatch.stall`
- the visible screen in an issue marker

AI conversation text (prompts, replies, the terminal context sent to the provider) is never
logged at any setting. `stream.*` lines carry sizes, not bodies.

### 10. The AI Assist tool: `read_debug_log`

The tool is registered by `chat_register_tools` **only when debugging is enabled**. It is
re-registered through `ai_chat_update_tools()` after Settings closes. Only the providers
that support tools (`ai_provider_supports_tools`: Anthropic, OpenAI, DeepSeek) see it.

```json
{"type":"object","properties":{
  "minutes":     {"type":"integer","description":"How far back to read, 1-1440. Default 15."},
  "category":    {"type":"string","enum":["all","general","render","term","ai","ssh","shell"]},
  "min_level":   {"type":"string","enum":["error","warn","info","debug","trace"],
                  "description":"Default info."},
  "contains":    {"type":"string","description":"Case-insensitive substring filter."},
  "around_issue":{"type":"boolean","description":"Centre on the most recent ISSUE marker: the
                  'minutes' before it and 1 minute after."},
  "max_lines":   {"type":"integer","description":"1-400. Default 150."}}}
```

Description given to the model: *"Read Nutshell's own debug log (read-only). Use it when the
user reports a glitch, a hang or something that looked wrong, or says they marked an issue.
Lines are `time LEVEL category tab=N event key=value…`."*

**Execution** happens on the AI stream thread through the pure core function
`dlog_query(const DlogQueryCtx *ctx, const DlogQuery *q, char **out, size_t *len,
int *truncated)`.

- It reads `nutshell.log` and as many rotated files as the time window needs, newest first.
  Each is opened read-only (`fopen "rb"`), which the writer's `_SH_DENYWR` mode allows while
  it keeps appending.
- A final line without a trailing newline is ignored.
- It filters by time, category, level and substring. ISSUE lines always pass the level
  filter.
- It keeps the **most recent** `max_lines` lines that match. With `around_issue`, it keeps
  those nearest the marker.
- **The output is capped at 24 KB, about 6,000 tokens.** It starts with a header line:
  `read_debug_log: N of M matching lines, window 23:10:58-23:26:58, files: nutshell.log`.

**Context.** The context is copied by value, in the same way as `search` and `fetch`:

- `AiStream` gains `DlogQueryCtx dlog` (the resolved folder, `char[260]`).
- `ai_stream_copy_tools` gains one argument and rebinds the pointer.
- The stream thread never reads `g_config` or any UI-thread state.

**In the chat**, the existing tool rows appear:

- `using tool: read_debug_log - render ≥ debug, last 15 min`
- the 200-character result preview

**Approval.** The tool is not approval-gated. `web_search` and `web_fetch` are not gated
either: tools have no classification (`ToolSafety` is never read). The user opted in by
enabling debugging.

**Disclosure.** What the tool returns is sent to the AI provider, like any tool result. The
Settings page says so in two places:

- under "Enable debugging"
- beside "Include terminal content", because that tick is what lets terminal text reach the
  provider through the tool

### 11. Settings: a "Debugging" page

A new nav entry, **Debugging**, sits at depth 0, standalone like About, just before About.
`SETTINGS_PAGE_DEBUG` goes before `SETTINGS_PAGE_ABOUT`. It uses the existing `add_ctrl` /
`relayout` machinery. Rows other than the first are `visible` only while "Enable debugging"
is ticked, using the same pattern as `ctrl_ai_search_url`. `MAX_SETTINGS_CTRLS` rises from 48
to 72.

```
 Debugging
 ──────────────────────────────────────────────────────────────────────────
 [x] Enable debugging
     Writes detailed diagnostics for troubleshooting. When off, only errors
     and warnings are kept. AI Assist can read these logs; what it reads is
     sent to your AI provider.

 Rendering          [ Trace          v]
 Terminal           [ Info           v]
 AI Assist          [ Info           v]
 SSH                [ Info           v]
 Local shell        [ Info           v]
 General            [ Info           v]

                    [ ] Include terminal content
                        Logs the text the shell sends. It can reach your AI
                        provider when AI Assist reads the log.
                    [x] Save a screenshot with each issue marker

 Log folder         [ logs                                ] [ Browse… ]
 File size (MB)     [ 10   ]
 Files kept         [ 10   ]   up to about 110 MB in total

 Current log file   [ C:\Tools\Nutshell\logs\nutshell.log ] [ Open folder ]
                    Give this path to a Claude Code session to analyse it.
                    Mark an issue: Help > Mark Issue, or Ctrl+Shift+M.
```

- Each level combo has Off, Errors, Info, Debug and Trace.
- **Browse…** uses `SHBrowseForFolderW` with `BIF_NEWDIALOGSTYLE | BIF_RETURNONLYFSDIRS`.
  shell32 and ole32 are already linked. `IFileDialog` would need COM vtables in C for no
  gain. A folder inside the exe's directory is stored relative, so it still travels.
- **Current log file** is a read-only edit, so it can be selected and copied. It shows the
  resolved path, including a fallback or `nutshell-pid….log`. When the writer is in memory
  only, it shows `(not writable: <reason>)`.
- **Open folder** uses `ShellExecuteW(…, L"open", folder, …)`.
- Everything is drawn with the page's existing theme colours, `ns_font`, `ns_scale` and the
  settings metrics. There are no new literals.
- **The Logging page loses its "Debug Terminal Log" tickbox** (`IDC_DEBUG_TERMINAL`). Its
  job moves to Terminal at Trace with content ticked.

**Apply.** On Save, the settings are validated and saved. Then `on_settings_clicked_page()`
calls `dlog_configure()`, which sets the levels at once. If the folder or limits changed, the
writer reopens on its own thread.

### 12. Config

New fields in `Settings` (`src/config/config.h`), all fixed size:

| Field | JSON key | Default | Validation |
|---|---|---|---|
| `int debug_enabled` | `debug_enabled` (bool) | false | — |
| `int debug_level[6]` | `debug_level_general` … `debug_level_shell` | Info | string `"off"`, `"errors"`, `"info"`, `"debug"`, `"trace"`; unknown → Info |
| `int debug_include_terminal` | `debug_include_terminal` (bool) | false | — |
| `int debug_issue_screenshot` | `debug_issue_screenshot` (bool) | true | — |
| `char debug_log_dir[CFG_STR_MAX]` | `debug_log_dir` | `"logs"` | empty → `"logs"` |
| `int debug_max_file_mb` | `debug_max_file_mb` | 10 | clamp 1–100 |
| `int debug_max_files` | `debug_max_files` | 10 | clamp 2–50 |

Levels are stored as words, so a person or an AI can read the config.

**Migration.** A config with the old `"debug_terminal": true` loads as these settings, and
`debug_terminal` is no longer written:

- `debug_enabled = 1`
- `debug_include_terminal = 1`
- Terminal at Trace

### 13. Startup order

`init_app_log()` in `main.c` becomes `dlog_init()`. It runs before `WSAStartup`, as today, at
Errors for every category, with no file yet. Lines from before the config loads (CLI and
early window creation) wait in the ring.

`WM_CREATE` loads the config. It then calls `dlog_configure(&settings, exe_dir)`, which opens
the file and starts the writer thread, and the waiting lines are flushed. `log_close()`
becomes `dlog_shutdown()`, which drains, joins the thread and closes the file.

The `%TEMP%\nutshell.log` and `%TEMP%\nutshell_redraw.log` files are no longer written.

### 14. `--ui-demo=debugsettings`

`--ui-demo=debugsettings` opens the main window as for `nosession`, then opens Settings on the
Debugging page with these settings:

- a **copy** of the config with debugging on
- Rendering at Trace
- `config_path` NULL, which disables Save so the demo never writes the user's config

`ui_demo.c` gets the state name before `"all"`. It is excluded from `"all"` and from the
integration gallery's `$galleryStates`, which screenshots the main window only.

### 15. How to analyse an issue with AI (README)

A short README section:

1. Turn on Settings > Debugging and raise the suspect area. Rendering at Debug is fine for
   weeks; use Trace while reproducing.
2. When the glitch appears, press **Ctrl+Shift+M**, or use Help > Mark Issue. Then either:
   - ask AI Assist: *"I just marked an issue; read the debug log around it and tell me what
     happened"*, or
   - give the "Current log file" path to a Claude Code session: *"read
     `C:\…\logs\nutshell.log`, find the last ISSUE marker and the render lines before it"*.

     The issue PNG sits beside the log.

---

## Out of scope

- Logging AI conversation bodies.
- Uploading logs anywhere.
- A log viewer inside Nutshell.
- Per-profile debug settings.
- Fixing the paint-path weaknesses (a) and (b). This change makes them visible; the fix is a
  separate PR once a log shows them again.

## Files

| File | Change |
|---|---|
| `src/core/dlog.h/.c` (new) | categories, levels, formatter, redaction, ring, writer, rotation, path resolution |
| `src/core/dlog_query.h/.c` (new) | the read-only query behind the AI tool |
| `src/core/logger.h/.c`, `tests/test_logger.c` | removed; the five call sites move to `DLOG` |
| `src/ui/redraw_log.h`, Makefile `redraw-debug` | removed |
| `src/config/config.h`, `src/config/loader.c` | new fields, defaults, validation, load and save, migration |
| `src/core/settings_layout.*` | `SETTINGS_PAGE_DEBUG`, nav entry, title |
| `src/ui/settings.c` | the Debugging page; the Logging page loses `IDC_DEBUG_TERMINAL` |
| `src/ui/window.c` | the per-session `log_id`, the Rendering and Terminal sites, the issue marker, menu item and shortcut, apply after Settings, `debug_log` file removed |
| `src/ui/renderer.c` | `paint.draw` |
| `src/ui/ai_chat.c`, `src/core/ai_stream.*` | `read_debug_log` registration and the context copy, `ai` sites |
| `src/config/ssh_io.c`, `src/ui/local_pty.c`, the SSH connect path | `term rx`, `ssh`, `shell` sites replacing `debug_log_chunk` |
| `src/main.c` | `dlog_init` and `dlog_shutdown` |
| `src/core/ui_demo.c`, `src/ui/resource.h` | demo state, `IDM_HELP_MARK_ISSUE`, control ids |
| `tests/test_dlog.c`, `tests/test_dlog_query.c` (new), `tests/test_config.c`, `tests/test_settings_layout.c`, `tests/test_ui_demo.c`, `tests/runner.c` | tests |
| `README.md` | the AI analysis section |
