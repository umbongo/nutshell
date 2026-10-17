# Debug logging: a runtime log an AI can read, and an issue marker

**Date**: 2026-10-17
**Status**: DRAFT, not yet critiqued.
**Branch**: `feat/debug-logging`
**Prompted by**: the maintainer, 2026-10-17: "Add debug to the app, create a new section in the
settings for debugging. Present an 'enable debugging' tickbox. Once pressed show a series of
settings for the debug intensity and where they're stored. The goal is to run this
semi-permanently and have AI access and analyse the logs when I notice an issue." Decided with
the maintainer: both a Claude Code session (plain files) and Nutshell's own AI Assist (a
read-only tool) read the logs; the default folder is `logs\` beside the exe, changeable;
terminal content is an opt-in tickbox, off by default; passwords, passphrases, keys and the AI
API key are never logged. Motivation: the stale-rows redraw bug recurred while the AI was
installing Rust. PR #66 (v1.2.34) was diagnosed from a compile-time `REDRAW_DEBUG` build; the
next occurrence must be diagnosable from the shipped exe.
**Builds on**: `2026-08-29-settings-window-redesign-design.md` (page table, `add_ctrl`),
`2026-09-07-design-system-foundation-design.md` (tokens, fonts), `2026-04-09-ai-tooling-design.md`
(tool registry), the notes doc's "Paint-path follow-ups from the 2026-10-14 stale-band
investigation" (`2026-09-06-ui-redesign-notes.md:288-304`).

---

## Problem

Nutshell has four unrelated logs and none of them answers "what was the app doing when it went
wrong":

- **`%TEMP%\nutshell.log`** (`src/main.c:15-40`, since PR #59): `logger.c` at INFO, always on,
  no rotation, no category, second-resolution timestamps, `localtime()` from any thread
  (`src/core/logger.c:41-58`), `fflush` per line. Ten call sites, all in `window.c` (two
  registration failures) and `ai_chat.c` (dispatcher start, stall, settle). It grows forever.
- **`debug_terminal`** (`config.h:27`, Settings > Logging checkbox `settings.c:991-995`): one
  raw-byte file per session beside the exe (`window.c:1041-1070`), escaped by two copies of
  `debug_log_chunk()` (`src/config/ssh_io.c:28-45`, `src/ui/local_pty.c:141`), plumbed as a
  `FILE *` through the transport vtable (`src/term/session_io.h:26`). All content, no context.
- **`REDRAW_DEBUG`** (`src/ui/redraw_log.h`, `Makefile:194-197`): three `REDRAW_LOG` sites,
  compiled out of every shipped exe. It records WM_PAINT's `rcPaint`, the count of dirty
  visible rows and `full_redraw_needed` (`window.c:4534-4550`); `renderer_draw`'s row count,
  rows inside the paint rect, the rect and `charHeight` (`renderer.c:160-175`); and every
  `invalidate_terminal` rect (`window.c:481`). The v1.2.34 diagnosis also needed facts it does
  not record and that had to be inferred: that a poll tick fell inside the 16 ms cooldown
  (`window.c:532-533`, `:4052-4062`), that `update_scrollbar()` hid the scrollbar for the
  alternate screen (`window.c:2387`), and which session's dirty row was driving the 60 fps
  invalidate loop.
- **Per-session text logs** (`window.c:1004-1036`, File > Start Logging): user transcripts, not
  diagnostics. Out of scope and untouched.

And there is no way to say "now": by the time the maintainer notices a glitch and finds a
log, the clicks and resizes that follow have rewritten the state worth seeing.

---

## Design

### 1. One logger, six categories, five levels per category

A new portable module `src/core/dlog.{c,h}` replaces `logger.c`. `logger.h` stays as a
three-line shim (`LOG_ERROR(msg)` becomes `DLOG(DLOG_GENERAL, DLOG_ERROR, 0, "legacy", "msg=%s", msg)`)
so nothing has to move at once; slice 5 converts the ten existing sites and deletes it.

| Category | Id | Covers |
|---|---|---|
| Rendering | `render` | WM_PAINT, `renderer_draw`, invalidation, cooldown, scrollbar, display buffer, DPI, resize |
| Terminal | `term` | VT parser mode changes, buffer, resize, alt screen, received bytes |
| AI Assist | `ai` | requests, streams, tools, approvals, the dispatcher |
| SSH | `ssh` | connect thread stages, host key, auth method, channel, keepalive, timeouts |
| Local shell | `pty` | resolver choice, ConPTY create/close, reader thread, child exit |
| General | `general` | startup, settings, menus, config, crashes, the logger itself |

Levels: `Off` 0, `Errors` 1 (ERROR and WARN), `Info` 2, `Debug` 3, `Trace` 4, plus an
internal `ISSUE` that is always written.

**Per-category levels, not a global level plus category ticks.** The call-site cost is the
same: one byte compare against `g_dlog_capture[cat]`, a `static unsigned char` array read with
`__atomic_load_n(..., __ATOMIC_RELAXED)` (the idiom `ai_stream.c:70` already uses). A global
level cannot express the motivating setup, Rendering at Trace while everything else stays at
Info, and that is the setup that must run for weeks. In Win32 it is six identical label+combo
rows, which the settings page table already lays out (section 9).

```c
#define DLOG(cat, lvl, sid, event, ...)                                   \
    do { if ((lvl) <= dlog_capture_level(cat))                            \
             dlog_emit((cat), (lvl), (sid), (event), __VA_ARGS__); } while (0)
#define DLOG_CONTENT(cat, lvl, sid, event, key, data, len)  /* section 6 */
```

`sid` is a new `unsigned log_id` on `Session` (`window.c:94-133`), assigned from a counter at
creation and printed as `s3`; 0 prints `-`. Event names are stable dotted identifiers
(`paint`, `poll.cooldown_skip`, `ssh.auth.result`) so grep and the AI tool can filter on them.

### 2. When debugging is off

Errors and warnings from every category, plus ISSUE marks, are written to the same folder
with a 1 MB file cap; nothing else. The file is created lazily on the first line that passes
the filter, so a clean run leaves no file and `--help`/`--version` never create the folder.
The startup header (section 3) is held in memory and written ahead of that first line.

What folds in:

- **`%TEMP%\nutshell.log` goes away.** `init_app_log()` becomes `dlog_start(exe_dir)` with
  the off-mode defaults, before `ui_init()` (`main.c:324`); `WM_CREATE` calls
  `dlog_configure(&g_config->settings)` once the config loads (`window.c:3472`), and the
  settings path calls it again after Save (`window.c:1641`). `dispatch_start` and
  `settled` become `ai` Info; `dispatch stall` becomes `ai` WARN, so it is still recorded with
  debugging off.
- **`debug_terminal` is superseded** by `term` at Trace with "Include terminal content"
  (section 6). Its checkbox and tooltip leave the Logging page (`settings.c:991-995`,
  `:400-402`, `:1626-1628`), the `FILE *debug_log` leaves `Session`, `open_debug_log()` and
  the vtable's `poll()` (four call sites, two tests in `tests/test_local_pty.c`), and the two
  `debug_log_chunk()` copies become one pure `dlog_escape_bytes()` in `src/core`.
- **`REDRAW_DEBUG` is superseded and removed**: `redraw_log.h`, the three `#ifdef` blocks and
  the Makefile `redraw-debug` target. Everything it recorded is a `render` Trace event below,
  and the runtime version records more:

| Event | Fields |
|---|---|
| `paint` | `rect`, `dirty_rows` (visible), `full_redraw`, `alt`, `sb` (scrollback offset), `tick` |
| `render.draw` | `rows`, `rows_in_rect`, `rows_covered`, `rows_left_dirty`, `cells_drawn`, `charH`, `us` |
| `invalidate` | `rect`, `why` (`poll`, `full`, `scroll`, `resize`, `tab`, `theme`, ...) |
| `poll.invalidate` / `poll.cooldown_skip` | `sid`, `poll_rc`, `dirty_rows`, `critical`, `since_paint_ms` |
| `dispbuf.invalidate` | `why`, `force_all_rows` |
| `scrollbar` | `visible` before/after, `range`, `pos` |
| `grid.sync` | `sid`, old and new `rows`x`cols`, `client` size |

`poll.cooldown_skip` is the line the v1.2.34 diagnosis had to infer; `rows_left_dirty` is the
bookkeeping the fix added (`renderer.c:234`, `:408-411`).

### 3. File format: one event per line, logfmt-style text

JSON Lines was considered and rejected: every line pays for quotes and braces in tokens and in
the 16 KB tool budget, a human reading in Notepad gets noise, and Claude reads `key=value` as
reliably. The fixed prefix makes grep and the tool's parser simple:

```
<timestamp> <LEVEL> <category> <sid> <thread> <event> key=value key="quoted value" ...
```

- Timestamp: local ISO-8601 with milliseconds and UTC offset, `2026-10-17T14:03:22.418+01:00`
  (always 29 characters). The platform hook supplies a broken-down time and offset; the bias
  is cached and refreshed once a minute, never per line.
- Level padded to 5 (`ERROR`, `WARN `, `INFO `, `DEBUG`, `TRACE`, `ISSUE`). Thread is one of
  `ui`, `ssh`, `ai`, `pty`, `set` (settings model fetch), `log`.
- Values with a space, `"` or `=` are quoted with `\"` and `\\` escapes; bytes below 0x20 and
  0x7F are `\xNN`; valid UTF-8 passes through. Lines end in LF. A line is capped at 2 KB and
  ends `trunc=<bytes dropped>` when cut.
- **Repeat suppression**: a line whose body (event and fields) is byte-identical to the
  previous line of the same category and session is counted, not written; the next different
  line, or 5 s, emits `repeat n=<count> last=<timestamp>`. The 22,000 no-op paints of
  2026-10-14 become one line.

```
2026-10-17T14:03:22.418+01:00 INFO  general - ui app.start version=1.2.40 pid=8124 exe="C:\Tools\nutshell\nutshell.exe" levels=render:trace,term:debug,ai:info,ssh:info,pty:info,general:info content=0 recorder=1
2026-10-17T14:03:22.431+01:00 TRACE render s2 ui poll.cooldown_skip poll_rc=1 dirty_rows=73 critical=0 since_paint_ms=9
2026-10-17T14:05:10.002+01:00 ISSUE general s2 ui issue.mark n=1 recorder="nutshell-20261017-140310-p8124.issue1.log" screenshot=none
```

Every file starts with `app.start` (version, pid, Windows build, exe path, DPI per monitor,
theme, the effective levels) and every settings change writes `dlog.config` with the new
levels, so a reader always knows what the file can and cannot contain. On first creation the
folder also gets `FORMAT.txt`: the format, the levels, the categories and the event list,
rewritten when the version changes. It is there for an AI reading the folder cold.

### 4. Names, rotation, retention, an unwritable folder

- **Folder**: `debug_log_dir`, default `logs`. Relative paths resolve against the exe
  directory (`get_exe_dir()`, `window.c:971`), so the logs travel with the exe and the
  config; environment variables are expanded first (`%TEMP%\nutshell`). Pure:
  `dlog_resolve_dir(setting, exe_dir, out, cap)`.
- **Name**: `nutshell-YYYYMMDD-HHMMSS-p<pid>.log`, the run's start time and process id, so two
  running instances never interleave and the names sort chronologically. A rotation within a
  run continues in `...-p<pid>.2.log`, `.3.log`; the new file starts with `log.continue
  prev=<name>`. Issue files share the run's stem: `.issue<n>.log`, `.issue<n>.png`.
- **Rotation**: when the current file reaches `debug_max_file_mb` (default 10, range 1-100;
  1 when debugging is off).
- **Retention**: after each rotation and at start, list `nutshell-*` files in the folder and
  delete oldest-first until the total is at most `debug_max_total_mb` (default 200, range
  5-5000). The current file is never deleted; a file another instance holds open fails to
  delete and is skipped. At Rendering Trace a busy session writes roughly 40 MB an hour
  before repeat suppression, so 50 MB keeps about an hour of Trace and weeks at Info. The
  decision is pure: `dlog_retention_pick(files[], n, cap, current) -> victims`.
- **Unwritable**: if the folder cannot be created or the file opened, fall back to
  `%TEMP%\Nutshell\logs`; if that fails, keep events in memory only (the recorder, section 5,
  still serves issue marks and the AI tool). One `general` WARN names the folder and the
  Windows error; Settings shows the real path in use and why (section 9).

### 5. Write path: buffered, off the UI thread, crash-aware

- **Emit** (any thread): format into a 2 KB stack buffer, run the key-name redaction (section
  6), then take the lock and copy the line into two rings: the **write queue** (1 MB, lines
  at or below the file level) and the **recorder** (4 MB, overwriting, every captured line).
  The lock is held for two `memcpy`s. No allocation, no I/O, no `localtime()` on the caller.
- **Writer thread** (`src/ui/dlog_win.c`): wakes on an event or every 250 ms, drains the
  queue with `WriteFile` to a handle opened `FILE_SHARE_READ | FILE_SHARE_DELETE`, rotates,
  applies retention. Flush triggers: ERROR or ISSUE (wake at once), the 250 ms timer, a
  `dlog_flush_wait(ms)` from the AI tool, and shutdown.
- **Full queue**: Trace and Debug lines are dropped and counted; the next drain writes
  `dlog.dropped n=<count>`. ERROR, WARN and ISSUE are never dropped: the emitter waits for
  the writer instead, up to 50 ms.
- **Exit**: `WM_DESTROY` and `main()`'s `log_close()` site (`main.c:336`) call
  `dlog_shutdown()`, which drains and closes.
- **Crash**: a new `SetUnhandledExceptionFilter` handler (there is none today) writes
  `app.crash code=... addr=...` and drains the queue directly with `WriteFile`, taking the
  lock with `TryAcquireSRWLockExclusive` and proceeding without it if that fails. At most the
  last 250 ms of non-error lines can be lost to a hard kill.
- **Threads that log**: UI, the SSH connect thread (`window.c:1297`), the AI stream thread
  (`ai_chat.c:1335`), the ConPTY reader (`local_pty.c:510`, dropped-byte counts only; bytes
  are logged by the UI-thread poll), the settings model fetch (`settings.c:1558`), and the
  writer. The lock is an SRW lock in the Windows backend; the core sees it through a
  `DlogPlatform` function-pointer struct (`lock`, `unlock`, `now`, `wake`), the same seam as
  `CryptoDpapiBackend`, so native tests install a deterministic fake.

**The recorder** is what makes semi-permanent use cheap. With "Keep recent detail for issue
marks" ticked (default on), `g_dlog_capture[cat]` is Trace for every category not set to Off,
while the file still gets only the chosen level. An issue mark (section 7) writes the
recorder's contents, roughly the last several minutes of everything, to its own file. So the
everyday setting can stay at Info and the moment of the glitch still arrives at Trace. The
cost is formatting Trace lines that are usually overwritten: about 60 paint lines a second
while output is flowing, nothing while idle. Unticked, capture equals the file level.

### 6. Redaction, and what "Include terminal content" adds

Never logged, with or without the tickbox:

- Passwords, key passphrases, private-key bytes, the AI API key, DPAPI blobs, any
  `ConnUiRequest` answer.
- HTTP request bodies and headers to AI providers or web tools: only provider, model,
  endpoint host, status, byte counts, token counts and timings. A provider error is logged as
  its `error.message` field, cut to 200 bytes.
- **Keyboard input and pastes**: special keys are logged by name at `term` Trace
  (`key name=PgUp mods=shift`); printable characters and paste text never are (a paste logs
  `paste bytes=N lines=N`). Typed input is where a password at a no-echo prompt lives.
- OSC 52 clipboard payloads (`osc=52 bytes=N`), even in a raw dump: `dlog_escape_bytes()`
  carries a small per-session state so a payload split across reads is still withheld.

Enforcement, in three layers:

1. **Key-name redaction** in the formatter: any field whose key contains `pass`, `secret`,
   `token`, `key` (except `key name=` events), `auth` or `cookie`, case-insensitive, is written
   `=[REDACTED]`. Pure, tested.
2. **Content only through `DLOG_CONTENT()`**, which checks `g_dlog_content` and escapes; a plain
   `DLOG()` never carries terminal text, chat text or command text, so a reviewer greps one
   macro to see every content site. Content bytes are also scrubbed of registered known
   secrets (`dlog_secret_add()`: the decrypted API key at config load, a profile password while
   its connect runs; up to 8, wiped with `secure_zero()` on removal and at exit).
3. **A source gate** in `tests/test_dlog_gate.c`, in the style of `test_ui_tokens.c`: no
   `DLOG(`/`DLOG_CONTENT(` argument list in `src/` may mention `password`, `passphrase`,
   `api_key`, `privkey` or `secret` identifiers. Exact allow-list, not a ratchet.

"Include terminal content" (`debug_include_content`, off) adds: received terminal bytes
(`term.rx`, escaped, Trace; this is what `debug_terminal` was), the cursor-row text in
`dispatch stall` (`ai_chat.c:2066-2087`; without content it logs `row_len` and the reason
only), OSC window titles, AI command text and chat message text (`ai` Debug), tool inputs and
result previews, and the issue screenshot. The label is the maintainer's; the warning line
under it names chat and commands so nobody is surprised.

### 7. Mark issue

**Help > Mark Issue in Debug Log** (`Ctrl+Shift+M`) and **Help > Open Debug Log Folder**,
both always present (`window.c:2618-2622`; new IDs `IDM_HELP_MARK_ISSUE 2031`,
`IDM_HELP_LOG_FOLDER 2032`). The chord joins the `ctrl_only && shift` block
(`window.c:3116-3133`) with `drop_queued_char()`. Ctrl+Shift+M otherwise sends CR, which a
legacy-encoding terminal cannot tell from Ctrl+M or Enter, so no shell or full-screen program
loses anything; Nutshell has no modifyOtherKeys or CSI-u encoding that would make it distinct.

Order matters, because any dialog or repaint can heal the glitch being recorded:

1. Before anything is painted: write `issue.mark n=<k>` (ISSUE level, always written).
2. Snapshot at ISSUE level, from a plain struct filled on the UI thread and formatted by a pure
   `dlog_snapshot_format()`:
   - `issue.window`: client size, DPI, `tab_height`, `left_margin`, AI docked/width,
     scrollbar visible, iconic, fullscreen, `ms_since_last_paint`, cooldown, focus window.
   - `issue.term`, active session: `sid`, kind, connected, `rows`x`cols`, `lines_count`,
     `lines_start`, `lines_capacity`, `scrollback_offset`, cursor row/col/visible,
     `alt_screen_active`, scroll region, `full_redraw_needed`, `write_seq`, dirty rows as
     ranges (`dirty=0-72`).
   - `issue.render`: `charW`/`charH`, display-buffer size, previous cursor cell.
   - `issue.session` for every other session: `sid`, kind, connected, dirty-row count, alt.
   - `issue.ai`: panel visible, stream live, dispatch batch, policy.
3. Write the recorder to `.issue<n>.log` (chronological, own `app.start`-style header).
4. If content is on: a PNG of the main window's client area, `BitBlt` into a DIB, saved through
   the GDI+ flat API already declared in `ai_chat.c:60-90` (`GdipCreateBitmapFromHBITMAP`,
   PNG CLSID; add `GdipSaveImageToFile`). GDI+ is already started (`window.c:3613`). The
   declarations move to a small `src/ui/gdip_png.{c,h}` both files use.
5. `dlog_flush_wait(500)`.
6. Only then a small dialog: "Issue 3 marked at 14:05:10", an optional one-line note
   ("What did you see?") logged as `issue.note n=3 note="..."` (the user wrote it for the log,
   so it is not content-gated), and Copy folder path / Open folder / Close.

Works with debugging off: the mark and snapshot land in the errors-only file and the recorder
holds whatever capture level applied (Errors only, when off).

### 8. AI Assist tool: `read_debug_log`

Registered in `chat_register_tools()` (`ai_chat.c:1181-1242`) only while `debug_enabled`;
`ai_chat_update_tools()` (`ai_chat.c:5121`, called from `window.c:1697`, `:1757`) gains the
flag. Safety `TOOL_SAFE`, like `web_search`: read-only, no approval. Its context, a
`DlogToolContext` (resolved folder, current run stem, pid), is copied by value and rebound by
`ai_stream_copy_tools()` (`ai_stream.c:73-91`, which gains a third context argument) before the
stream thread starts, exactly as search and fetch are.

```json
{"type":"object","properties":{
  "since_minutes":{"type":"integer","description":"How far back to read, 1-1440, default 30"},
  "category":{"type":"string","enum":["render","term","ai","ssh","pty","general"]},
  "min_level":{"type":"string","enum":["error","warn","info","debug","trace"]},
  "contains":{"type":"string","description":"Case-insensitive substring, max 128 chars"},
  "session":{"type":"string","description":"Session id such as s2"},
  "around_issue":{"type":"integer","description":"0 = latest issue mark, n = issue n; reads its recorder file and the 2 minutes before it"},
  "summary":{"type":"boolean","description":"Counts per category, level and event plus the issue list, no lines"},
  "max_lines":{"type":"integer","description":"1-1000, default 200"}}}
```

- **Reading**: `dlog_flush_wait(200)` first, then the run's files newest-first within the
  window, opened `FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE`, reading up to the
  last complete LF so a line being appended is never half-read. A file that disappears mid-read
  just ends. With no file in use, it reads the recorder.
- **Output cap**: 16 KB (about 4-5k tokens), lines over 512 bytes cut with `+N bytes`. Results
  stay in the conversation and are re-sent every turn, and the maintainer's test key has
  limited credit, so the cap is deliberately low; `summary:true` is the cheap first call the
  description recommends. Truncation keeps the newest lines, or centres on the mark for
  `around_issue`, and a header line says what was dropped:
  `# 2341 matching lines, showing last 200; files: ...; window 13:35:10-14:05:10`.
- **Rate**: at most 4 calls per user message (`agentic_check_rate_limit()`,
  `ai_agentic.c:18-42`), inside the existing `AI_TOOL_LOOP_MAX` of 5.
- **In the chat**: the existing `using tool:` row (`ai_chat.c:775`) gains a suffix like
  web_search's provider, `using tool: read_debug_log - render, last 30 min`; the result row
  shows the header line, not log lines.
- The filter and cap are a pure `dlog_query()` over a line iterator, tested on fixture text.

### 9. Settings: a "Debugging" page

A new standalone nav entry `SETTINGS_PAGE_DEBUG` ("Debugging", depth 0) before About
(`settings_layout.h:22-34`, `settings_layout.c:11-24`, title at `:70-80`).
`MAX_SETTINGS_CTRLS` (`settings.c:144`) rises from 48 to 64. Every control below the tickbox is
an `add_ctrl` row whose `visible` follows the tickbox: `BN_CLICKED` sets them and calls
`relayout()`, the pattern the custom-URL field uses (`settings.c:1476-1489`). Colours come from
the page's existing `WM_CTLCOLORSTATIC` path; dim lines use `text_dim`; no new fonts or sizes.

```
 Debugging
 [x] Enable debugging
     Records what Nutshell does so an AI can diagnose a problem you mark.

            Rendering  [ Trace   v]
             Terminal  [ Debug   v]
            AI Assist  [ Info    v]          levels: Off, Errors, Info, Debug, Trace
       SSH connection  [ Info    v]
          Local shell  [ Info    v]
              General  [ Info    v]

     [x] Keep recent detail for issue marks (last few minutes, in memory)
     [ ] Include terminal content
         Adds terminal text, AI commands and chat text. Passwords and keys are never logged.

           Log folder  [ logs                                  ] [Browse...] [Open]
   Max file size (MB)  [ 10  ]
     Keep at most (MB) [ 200 ]   using 37 MB in 9 files

     Current log file  C:\Tools\nutshell\logs\nutshell-20261017-140310-p8124.log
                       Give this folder to a Claude Code session to analyse it.
                       AI Assist can read it too; what it reads goes to your AI provider.
                       Mark an issue with Help > Mark Issue (Ctrl+Shift+M).
```

Unticked, the page shows the tickbox, "Errors are still written to the log folder." and the
Current log file line ("none yet, created at the first error" when lazy). The fallback case
reads `C:\Users\...\Temp\Nutshell\logs\... (logs\ is not writable: Access is denied)`.

Browse uses `SHBrowseForFolderA` (`shell32` is already linked, `Makefile:52`); Open uses
`ShellExecuteA(..., "open", folder, ...)`. The current-path line is a read-only `EDIT` so it can
be selected and copied. Changes apply on Save without a restart: `dlog_configure()` swaps
levels atomically, and a changed folder closes the current file with `log.moved to=...` and
continues in the new one.

### 10. Config fields

In `Settings` (`config.h:18-64`), defaults in `config_default_settings()` (`loader.c:468`),
clamped in `settings_validate()` (`loader.c:442`):

| Field | Type | Default | JSON |
|---|---|---|---|
| `debug_enabled` | `int` | 0 | `"debug_enabled": false` |
| `debug_levels[DLOG_CAT_COUNT]` | `unsigned char` | all Info | `"debug_levels": {"render":"info", ...}` |
| `debug_include_content` | `int` | 0 | bool |
| `debug_recorder` | `int` | 1 | bool |
| `debug_log_dir` | `char[CFG_STR_MAX]` | `"logs"` | string |
| `debug_max_file_mb` | `int` | 10 (1-100) | number |
| `debug_max_total_mb` | `int` | 200 (5-5000) | number |

Levels are stored as names so a hand-edited config reads clearly; an unknown name or category
keeps the default. `config.h` includes a tiny `src/core/dlog_types.h` for the enum, as it already
includes `cmd_policy.h`. Backward compatibility: old configs simply lack the keys. A config
with `"debug_terminal": true` migrates once to `debug_enabled`, `term` at Trace and content on
(the user had already opted in to raw bytes), and `debug_terminal` is no longer written. Older
Nutshell versions ignore the new keys, as the loader only reads keys it knows.

### 11. README: "Analysing an issue with AI" (draft)

> **Analysing an issue with AI.** Turn on **Settings > Debugging > Enable debugging** and leave
> it on; the defaults (everything at Info, recent detail kept in memory) cost almost nothing.
> When something looks wrong, press **Ctrl+Shift+M** (Help > Mark Issue) *before* clicking or
> resizing: Nutshell writes the state of every tab and the last few minutes of detail to the
> log folder (`logs\` beside `nutshell.exe` unless you changed it) and asks what you saw. Then
> either ask AI Assist "read the debug log around the last issue", or open a Claude Code
> session and point it at the folder: "Read FORMAT.txt and the newest nutshell-*.issue*.log in
> C:\Tools\nutshell\logs and tell me what happened." Logs never contain passwords, keys or what
> you type. Terminal text, commands and chat appear only if you tick **Include terminal
> content**; check before sharing a log with anyone.

The Settings table row (`README.md:184`) loses "Debug terminal log" and the shortcuts table
(`README.md:329-355`) gains Ctrl+Shift+M.

### 12. Out of scope

Minidumps, uploading logs anywhere, a log viewer inside Nutshell, the per-session text logs,
and fixing the two paint-path follow-ups themselves (this feature is how they get diagnosed).

---

## Files

| Area | File | Change |
|---|---|---|
| core | `src/core/dlog.{c,h}`, `dlog_types.h` | new: levels, categories, `DLOG`, formatter, redaction, repeat suppression, rings, `DlogPlatform` |
| core | `src/core/dlog_files.{c,h}` | new: `dlog_resolve_dir`, naming, rotation and retention decisions, `FORMAT.txt` text |
| core | `src/core/dlog_query.{c,h}` | new: line parser, `dlog_query()`, summary, cap |
| core | `src/core/dlog_snapshot.{c,h}` | new: issue snapshot struct and formatter |
| core | `src/core/logger.{c,h}` | shim in slice 1, deleted in slice 5 |
| core | `src/core/ai_agentic.c:18-42` | `read_debug_log` per-message cap |
| core | `src/core/ai_stream.c:73-91` | third tool context, rebound |
| config | `src/config/config.h:18-64`, `loader.c:442-496`, `:613-614`, `:884-885` | fields, defaults, clamp, load, save, migration |
| config | `src/config/ssh_io.c:28-81` | `debug_log_chunk` and `FILE *` replaced by `DLOG_CONTENT` |
| term | `src/term/session_io.h:26` | `poll()` loses `debug_log` |
| ui | `src/ui/dlog_win.c` | new: SRW lock, writer thread, files, crash filter, clock |
| ui | `src/ui/gdip_png.{c,h}` | new: shared GDI+ declarations, PNG save (from `ai_chat.c:60-90`, `:2392-2440`) |
| ui | `src/main.c:15-40`, `:324`, `:336` | `init_app_log()` replaced by `dlog_start`/`dlog_shutdown` |
| ui | `src/ui/window.c` | `log_id` on `Session` (`:94-133`); `debug_log` removed (`:103`, `:591`, `:691`, `:1041-1070`, `:1243`, `:2014`, `:2034`, `:2994`, `:3887`, `:4327`); render events at `:481`, `:490`, `:2387`, `:500-529`, `:4052-4062`, `:4534-4550`; menu `:2618-2622`; chord `:3116-3133`; configure at `:3472`, `:1641`; tools flag `:1697`, `:1757` |
| ui | `src/ui/renderer.c:160-175`, `:234`, `:408-411` | `REDRAW_DEBUG` block replaced by `render.draw` |
| ui | `src/ui/redraw_log.h`, `Makefile:194-197` | deleted |
| ui | `src/ui/ai_chat.c` | `LOG_INFO` sites `:1820`, `:2087`, `:2341`; tool registration `:1181-1242`, `:5121-5135`; chat suffix `:758-775`; request lifecycle events |
| ui | `src/ui/local_pty.c:141`, `:542-578` | escaper and `FILE *` replaced |
| ui | `src/ui/settings.c` | page, controls, visibility, Browse/Open, apply; old checkbox removed (`:991-995`, `:400`, `:1626`) |
| ui | `src/core/settings_layout.{c,h}` | `SETTINGS_PAGE_DEBUG`, nav row, title |
| ui | `src/ui/resource.h` | `IDM_HELP_MARK_ISSUE`, `IDM_HELP_LOG_FOLDER`, control ids; `IDC_DEBUG_TERMINAL` (`:85`) removed |
| demo | `src/core/ui_demo.c:15`, `ui_demo.h`; `tests/integration/cases/30-ui-demo.ps1:34` | `settings-debug` state |
| tests | `tests/test_dlog*.c`, `tests/test_dlog_gate.c`, `test_config.c`, `test_settings_layout.c`, `test_local_pty.c`, `runner.c` | new and updated; `test_logger.c` replaced |
| docs | `README.md:151-215`, `:315-355`; `src/ui/help_guide.c:176` | section 11 |

## Test plan

Native (`make test`, `TEST_TMP_DIR` for every file), positive and negative cases each:

- **Filtering**: every level against every threshold; Off writes nothing; ISSUE always
  passes; capture versus file level, recorder on and off.
- **Formatting**: offsets +, - and 0, ms padding; quoting of spaces, `"`, `=`, controls,
  invalid UTF-8; the 2 KB cut; repeat suppression (identical, one field differs, other
  category or session, the 5 s flush).
- **Redaction**: each key name redacted, `key name=` not; `DLOG_CONTENT` dropped when off;
  known-secret scrub across a buffer edge; `dlog_escape_bytes` on ESC, CR/LF, tab and OSC 52
  whole and split across chunks.
- **Rings**: drop counting, ERROR never dropped (fake platform), recorder wrap order.
- **Files**: `dlog_resolve_dir` (empty, relative, absolute, UNC, trailing slash, overlong);
  `.N` continuation; rotation threshold; retention oldest-first, never the current file.
- **Query**: malformed lines; each parameter alone and combined; `since_minutes` across a DST
  change; `around_issue` 0, n, missing; summary; both caps and the header; a half line ignored.
- **Snapshot**: dirty-row ranges (none, one, contiguous, scattered, all).
- **Config**: defaults, round-trip, clamps, unknown level names, the `debug_terminal`
  migration, a config with none of the keys. **Settings layout**: nav order, `min_h`.
- **Source gate**, plus a check that it fails on a planted violation.

`--ui-demo=settings-debug` opens Settings on the Debugging page with debugging ticked in
memory only (never saved) and is added to the gallery list.

Manual, on Windows, before the PR is marked ready:

1. Rendering at Trace, Terminal at Debug, recorder on. Reproduce the 2026-10-14 sequence:
   two tabs, one disconnected; `nano` in a tab with scrollback; switch tabs; then a long
   `cargo install` with tab switches during output. Confirm `poll.cooldown_skip`,
   `scrollbar`, `render.draw rows_left_dirty` lines appear and the file stays within caps.
2. Validate the pipeline against the real bug with a throwaway local build that reverts the
   v1.2.34 condition in `renderer.c:408` (never committed). When the stale band appears,
   press Ctrl+Shift+M before clicking; check the issue file shows the frame (all rows clean
   after a paint whose `rows_covered=0`), and that a Claude Code session given only the folder
   and section 11's prompt reaches the v1.2.34 diagnosis.
3. Ask AI Assist "read the debug log around the last issue"; check the chat row, the 16 KB cap
   and that `summary:true` is tried first.
4. Point the folder at a read-only directory: fallback path shown in Settings, WARN written.
5. Debugging off for a day of normal use: no file unless an error occurred.
6. Kill the process from Task Manager mid-output: the file ends with complete lines.

## Implementation slices (each independently testable)

1. **Core logger**: `dlog_types.h`, `dlog.c` (filter, formatter, redaction, repeat
   suppression, rings, platform seam, fake for tests), `logger.h` shim. Tests: filtering,
   formatting, redaction, rings.
2. **File policy**: `dlog_files.c` and `FORMAT.txt` text. Tests: paths, naming, rotation,
   retention.
3. **Config**: fields, defaults, clamp, load/save, migration. Tests: round-trip, migration.
4. **Windows backend and startup**: `dlog_win.c`, `main.c`, `WM_CREATE` configure, crash
   filter; `%TEMP%\nutshell.log` gone. Check: a run writes `app.start`; kill test.
5. **Instrumentation**: render (replacing `REDRAW_DEBUG`, deleting `redraw_log.h` and the
   Makefile target), term (mode events, `term.rx`, `debug_log` removed from the vtable), ai
   (the ten legacy sites, request lifecycle), ssh, pty; `logger.*` deleted; the source gate
   test. Check: manual step 1.
6. **Settings page** and `--ui-demo=settings-debug`. Check: gallery screenshot in four themes.
7. **Issue marker**: snapshot formatter (tested), recorder dump, `gdip_png`, menu, chord, note
   dialog. Check: manual step 2.
8. **AI tool**: `dlog_query.c` (tested), registration, context rebind, rate cap, chat suffix.
   Check: manual step 3.
9. **Docs**: README section, Settings table, shortcuts, help guide.

Every build bumps the version per CLAUDE.md; the exe is committed with the final slice's PR.

## Open questions

1. The issue note dialog: wanted, or should Ctrl+Shift+M mark silently with only a brief
   status flash? The snapshot is taken first either way.
2. Ctrl+Shift+M as the chord (it would otherwise send CR). Alternatives: Ctrl+Shift+F12, or
   menu only.
3. Host names, user names and profile names are logged without the content tickbox because
   SSH diagnosis needs them. Should they be content instead, for logs that get shared?
4. Is the screenshot right to sit behind "Include terminal content", or should it be its own
   tickbox?
5. Recorder default on, at 4 MB: acceptable memory and CPU for a portable app, or default off?
6. One tickbox for terminal, command and chat content, or two (terminal; AI conversation)?
7. Migrating `debug_terminal: true` to content on: right, or should migration leave content
   off and ask?
8. Should AI Assist also get a "Mark issue" button, so a glitch noticed while chatting is one
   click?
9. Is 200 MB total a sensible default for an exe that may live on a USB stick?

## Decisions on the open questions (maintainer's session, 2026-10-17)

1. Mark Issue is silent: it writes the mark and flashes a short status line. No
   note dialog; a note can be given to the AI tool afterwards.
2. Ctrl+Shift+M stays (the terminal only ever sent a plain CR for it).
3. Host, user and profile names: the profile name and the session id are logged
   at any setting; the host name and user name only with "Include terminal
   content" on.
4. The screenshot has its own tickbox, "Save a screenshot with each issue mark",
   off by default, independent of the content tickbox.
5. The recorder is on by default at 4 MB.
6. One content tickbox.
7. `debug_terminal: true` migrates to Terminal at Trace with content on.
8. No Mark Issue button in the AI panel for now; menu and shortcut suffice.
9. Defaults: 10 MB per file, 50 MB total (not 200; the exe may live on a USB
   stick). Both adjustable in Settings.
