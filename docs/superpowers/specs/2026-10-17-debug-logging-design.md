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
investigation" (`2026-09-06-ui-redesign-notes.md:288-304`), and the first draft of this spec
(commit `a5875bb`, whose structure and detail this document keeps).
**Supersedes**: both earlier bodies of this file, the `a5875bb` draft and the `0660d44`
rewrite. Section 2 keeps the second draft's `render.stats` and `render.stuck_dirty`
instrumentation; everything else follows the first, with the maintainer's decisions applied.
See "Merged-design decisions (2026-10-18)" at the end.

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
  compiled out of every shipped exe: WM_PAINT's `rcPaint`, dirty visible rows and
  `full_redraw_needed` (`window.c:4534-4550`); `renderer_draw`'s row count, rows in the paint
  rect, the rect and `charHeight` (`renderer.c:160-175`); every `invalidate_terminal` rect
  (`window.c:481`). The v1.2.34 diagnosis also needed facts it does not record and that had to
  be inferred: a poll tick inside the 16 ms cooldown (`window.c:532-533`, `:4052-4062`),
  `update_scrollbar()` hiding the scrollbar for the alternate screen (`window.c:2387`), and
  which session's dirty row drove the 60 fps invalidate loop.
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
| Local shell | `shell` | resolver choice, ConPTY create/close, reader thread, child exit |
| General | `general` | startup, settings, menus, config, crashes, the logger itself |

Levels: `Off` 0, `Errors` 1 (ERROR and WARN), `Info` 2, `Debug` 3, `Trace` 4, plus an
internal `ISSUE` that is always written.

**Per-category levels, not a global level plus category ticks.** The call-site cost is the
same: one byte compare against `g_dlog_capture[cat]`, a `static unsigned char` array read with
`__atomic_load_n(..., __ATOMIC_RELAXED)` (the idiom `ai_stream.c:70` uses). A global level
cannot express the motivating setup, Rendering at Trace while everything else stays at Info,
which must run for weeks. In Win32 it is six identical label+combo rows (section 9).

```c
#define DLOG(cat, lvl, sid, event, ...)                                   \
    do { if ((lvl) <= dlog_capture_level(cat))                            \
             dlog_emit((cat), (lvl), (sid), (event), __VA_ARGS__); } while (0)
#define DLOG_EV(cat, lvl, sid, event)  /* fieldless event, no format string */ \
    do { if ((lvl) <= dlog_capture_level(cat))                            \
             dlog_emit_ev((cat), (lvl), (sid), (event)); } while (0)
#define DLOG_CONTENT(cat, lvl, sid, event, key, data, len)  /* section 6 */
```

`DLOG_EV` exists because `DLOG(..., "")` trips `-Wformat-zero-length`, fatal under `-Werror`;
an event with no fields uses it. Formatting is a pure entry point, `dlog_format_line(buf, cap,
&fields, ...)`, so layout, quoting, truncation and redaction are tested without a clock,
thread or file. A second pure helper, `dlog_relativize_dir(dir, exe_dir, out, cap)`, shows the
folder relative to the exe where it is below it (`logs`, not the absolute path), in Settings
and the startup header.

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

| Event | Level | Fields |
|---|---|---|
| `paint` | Trace | `rect`, `dirty_rows` (visible), `full_redraw`, `alt`, `sb` (scrollback offset), `tick` |
| `render.draw` | Trace | `rows`, `rows_in_rect`, `rows_covered`, `rows_left_dirty`, `cells_drawn`, `charH`, `us` |
| `invalidate` | Trace | `rect`, `why` (`poll`, `full`, `scroll`, `resize`, `tab`, `theme`, ...) |
| `poll.invalidate` / `poll.cooldown_skip` | Trace | `sid`, `poll_rc`, `dirty_rows`, `critical`, `since_paint_ms` |
| `dispbuf.invalidate` | Trace | `why`, `force_all_rows` |
| `scrollbar` | Trace | `visible` before/after, `range`, `pos` |
| `grid.sync` | Trace | `sid`, old and new `rows`x`cols`, `client` size |
| `render.stats` | Debug | per paint: `rows_painted`, `rows_skipped`, `cooldown_skips` since the previous paint |
| `render.stuck_dirty` | WARN | `sid`, `rows`, `paints` (consecutive paints the row stayed dirty), `first_row` |

`poll.cooldown_skip` is the line the v1.2.34 diagnosis had to infer; `rows_left_dirty` is the
bookkeeping the fix added (`renderer.c:234`, `:408-411`).

**`render.stats`** (Debug, once per paint) shows a starving or no-op pattern without Trace's
volume; repeat suppression (section 3) collapses an unchanging run into one line.
**`render.stuck_dirty`** is a WARN, so it is written with debugging off too: when a row has stayed
dirty across `RENDER_STUCK_DIRTY_PAINTS` consecutive paints (a named constant in `renderer.c`)
it writes one line per episode. That is the 2026-10-14 background tab that drove 22,000 no-op
paints announcing itself instead of being found afterwards.

Other categories start with a deliberately small set of sites (`app.start`, `config.load`,
`crash`; `resize`, `alt_screen.*`, `rx` counts; `stream.*`, `tool.call`, `dispatch.*`;
`connect.*`, `hostkey`; `resolve`, `spawn`, `exit`), added to as diagnoses need them. The
profile name and session id are logged at any setting; **host and port appear in `connect.*`
events only with "Include terminal content" on** (decision 3).

### 3. File format: one event per line, logfmt-style text

JSON Lines was considered and rejected: quotes and braces cost tokens in the 16 KB tool budget,
Notepad readers get noise, and Claude reads `key=value` as reliably. The fixed prefix makes
grep and the tool's parser simple:

```
<timestamp> <LEVEL> <category> <sid> <thread> <event> key=value key="quoted value" ...
```

- Timestamp: local ISO-8601 with milliseconds and UTC offset, `2026-10-17T14:03:22.418+01:00`
  (always 29 characters). The platform hook supplies a broken-down time and offset; the bias
  is cached and refreshed once a minute, never per line.
- Level padded to 5 (`ERROR`, `WARN `, `INFO `, `DEBUG`, `TRACE`, `ISSUE`). Thread is one of
  `ui`, `ssh`, `ai`, `shell`, `set` (settings model fetch), `log`.
- Values with a space, `"` or `=` are quoted with `\"` and `\\` escapes; bytes below 0x20 and
  0x7F are `\xNN`; valid UTF-8 passes through. Lines end in LF. A line is capped at **4096
  bytes**, cut at a UTF-8 boundary (never mid-sequence), and ends ` truncated=1` when cut.
- **Repeat suppression**: a line whose body (event and fields) is byte-identical to the
  previous line of the same category and session is counted, not written; the next different
  line, or 5 s, emits `repeat n=<count> last=<timestamp>`. The 22,000 no-op paints of
  2026-10-14 become one line.

```
2026-10-17T14:03:22.418+01:00 INFO  general - ui app.start version=1.2.40 pid=8124 exe="C:\Tools\nutshell\nutshell.exe" levels=render:trace,term:debug,ai:info,ssh:info,shell:info,general:info content=0 recorder=1:trace
2026-10-17T14:03:22.431+01:00 TRACE render s2 ui poll.cooldown_skip poll_rc=1 dirty_rows=73 critical=0 since_paint_ms=9
2026-10-17T14:05:10.002+01:00 ISSUE general s2 ui issue.mark n=1 recorder="nutshell-20261017-140310-p8124.issue1.log" screenshot=none
```

Every file starts with `app.start` (version, pid, Windows build, exe path, DPI per monitor,
theme, effective levels) and every settings change writes `dlog.config`, so a reader always
knows what the file can contain. The folder also gets `FORMAT.txt` (format, levels, categories,
event list; rewritten when the version changes) for an AI reading it cold.

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
  1 when debugging is off). There is no file-count limit; the total cap is the only control.
- **Retention**: after each rotation and at start, list `nutshell-*` files in the folder and
  delete oldest-first until the total is at most `debug_max_total_mb` (default 50, range
  5-5000). The current file is never deleted; a file another instance holds open fails to
  delete and is skipped. At Rendering Trace a busy session writes roughly 40 MB an hour
  before repeat suppression, so 50 MB keeps about an hour of Trace and weeks at Info. The
  decision is pure: `dlog_retention_pick(files[], n, cap, current) -> victims`.
- **Fallback chain**: the configured folder (environment variables expanded); if it cannot be
  created or the file opened, `%LOCALAPPDATA%\Nutshell\logs`; if that fails too, **memory
  only**. In memory-only mode the write queue has no file to drain to, so the oldest events
  are evicted to make room for new ones; the recorder (section 5) still serves issue marks
  and the AI tool. One `general` WARN names the folder and the Windows error; Settings shows
  the real path in use and why (section 9).

### 5. Write path: buffered, off the UI thread, never blocking a producer

- **Emit** (any thread): format into a 4 KB stack buffer (one line at most), run the
  redaction passes (section 6), then take the lock and copy the line into two rings: the
  **write queue** (1 MB, lines at or below the file level) and the **recorder** (4 MB,
  overwriting, every captured line). The lock is held for two `memcpy`s. No allocation, no
  I/O, no `localtime()` on the caller. **A producer never blocks**: no wait on the writer, no
  sleep, no retry.
- **Writer thread** (`src/ui/dlog_win.c`): wakes on an event or every 250 ms, drains the
  queue with `WriteFile` to a handle opened `FILE_SHARE_READ | FILE_SHARE_DELETE`, rotates,
  applies retention. Flush triggers: ERROR, WARN or ISSUE (wake at once), the 250 ms timer,
  a `dlog_flush_wait(ms)` before an AI tool read, and exit.
- **Full queue, by reserved headroom**: the last slots of the queue are usable only by ERROR,
  WARN and ISSUE lines. Once the queue is full up to that reserve, Trace, Debug and Info lines
  are dropped and counted; the next drain writes `dlog.dropped n=<count>`. ERROR, WARN and
  ISSUE are never dropped on overflow and the emitter never waits; only a hard kill loses them.
- **Exit and crash**: `WM_DESTROY` and `main()`'s `log_close()` site (`main.c:336`) call
  `dlog_shutdown()`, which drains and closes. A new `SetUnhandledExceptionFilter` handler (there
  is none today) writes `app.crash code=... addr=...` and drains the queue directly with
  `WriteFile`, taking the lock with `TryAcquireSRWLockExclusive` and proceeding without it if
  that fails. At most the last 250 ms of non-error lines can be lost to a hard kill.
- **Threads that log**: UI, the SSH connect thread (`window.c:1297`), the AI stream thread
  (`ai_chat.c:1335`), the ConPTY reader (`local_pty.c:510`, dropped-byte counts only), the
  settings model fetch (`settings.c:1558`) and the writer. The lock is an SRW lock in the
  Windows backend; the core sees it through a `DlogPlatform` function-pointer struct (`lock`,
  `unlock`, `now`, `wake`), the same seam as `CryptoDpapiBackend`, so native tests install a
  deterministic fake (`tests/fake_dlog.{c,h}`).

**The recorder** is what makes semi-permanent use cheap: a 4 MB ring, **on by default**
(`debug_recorder`), with its own capture level `debug_recorder_level` (default Trace). For each
category that is not Off, `g_dlog_capture[cat]` is the higher of the file level and the
recorder level, while the file still gets only the file level. An issue mark (section 7)
writes the ring, roughly the last several minutes of everything, to its own file, so the
everyday setting can stay at Info and the glitch still arrives at Trace. The cost is
formatting Trace lines that are usually overwritten: about 60 paint lines a second while
output flows, nothing while idle. A category at Off captures nothing; with the recorder
unticked, capture equals the file level.

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

With the tickbox on, the shell's echo of a command typed at an echoing prompt is terminal
text and is logged as such (a password at a no-echo prompt is not echoed, so it is not
there); the tickbox's warning line names the echo for that reason.

Enforcement, in four layers:

1. **Key-name redaction** in the formatter: any field whose key contains `pass`, `secret`,
   `token`, `key`, `auth` or `cookie`, case-insensitive, is written `=[REDACTED]`. The
   allow-list is exact and short: `key name=` events (a special key's name) and nothing else.
   Pure, tested.
2. **Value-pattern redaction** on the final formatted line, whatever the key was called, so a
   secret in an innocently named field or free text is still caught: `Bearer <token>`, `sk-`
   style API keys, `$dpapi$...` and `$aes256gcm$...` blobs, and PEM blocks (`-----BEGIN ...`
   through `-----END ...` or the end of the line), each replaced with `[REDACTED]`. Both
   layers run on every line; neither substitutes for the other. Pure, tested.
3. **Content only through `DLOG_CONTENT()`**, which checks `g_dlog_content` and escapes; a plain
   `DLOG()` never carries terminal text, chat text or command text, so a reviewer greps one
   macro to see every content site. Content bytes are also scrubbed of registered known
   secrets (`dlog_secret_add()`: the decrypted API key at config load, a profile password while
   its connect runs; up to 8, wiped with `secure_zero()` on removal and at exit).
4. **A source gate** in `tests/test_dlog_gate.c`, in the style of `test_ui_tokens.c`: no
   `DLOG(`/`DLOG_EV(`/`DLOG_CONTENT(` argument list in `src/` may mention `password`,
   `passphrase`, `api_key`, `privkey` or `secret` identifiers. Exact allow-list, not a ratchet.

"Include terminal content" (`debug_include_content`, off) adds: received terminal bytes
(`term.rx`, escaped, Trace; this is what `debug_terminal` was), the cursor-row text in
`dispatch stall` (`ai_chat.c:2066-2087`; without content it logs `row_len` and the reason
only), OSC window titles, AI command and chat text (`ai` Debug), tool inputs and result
previews, and host, port and user name in `connect.*` events (profile name and session id are
logged at any setting). The warning line under the tickbox names chat and commands. The
screenshot is independent of this tickbox (section 7).

### 7. Mark issue

**Help > Mark Issue** (`Ctrl+Shift+M`) and **Help > Open Debug Log Folder**, both always
present (`window.c:2618-2622`; new IDs `IDM_HELP_MARK_ISSUE 2031`, `IDM_HELP_LOG_FOLDER 2032`).
The chord joins the `ctrl_only && shift` block (`window.c:3116-3133`) with `drop_queued_char()`.
Ctrl+Shift+M otherwise sends CR, which a legacy-encoding terminal cannot tell from Ctrl+M or
Enter, so no shell or full-screen program loses anything; Nutshell has no modifyOtherKeys or
CSI-u encoding that would make it distinct.

The mark is **silent**: no dialog and no repaint beyond a short status-line flash ("Issue 3
marked"). Order still matters, because a dialog or repaint can heal the glitch being recorded:

1. Before anything is painted: write `issue.mark n=<k>` (ISSUE level, always written).
2. Snapshot at ISSUE level, from a plain struct filled on the UI thread and formatted by a pure
   `dlog_snapshot_format()`: `issue.window` (client size, DPI, `tab_height`, `left_margin`, AI
   docked/width, scrollbar, iconic, fullscreen, `ms_since_last_paint`, cooldown, focus window);
   `issue.term` for the active session (`sid`, kind, connected, `rows`x`cols`, `lines_count`,
   `lines_start`, `lines_capacity`, `scrollback_offset`, cursor, `alt_screen_active`, scroll
   region, `full_redraw_needed`, `write_seq`, dirty rows as ranges `dirty=0-72`);
   `issue.render` (`charW`/`charH`, display-buffer size, previous cursor cell);
   `issue.session` per other session (`sid`, kind, connected, dirty-row count, alt);
   `issue.ai` (panel visible, stream live, dispatch batch, policy).
3. Write the recorder to its own dump, `.issue<n>.log` (chronological, own `app.start`-style
   header). The main log only gets the mark and the snapshot.
4. If **"Save a screenshot with each issue mark"** is ticked (`debug_screenshot`,
   default off, independent of the content tickbox): a PNG of the main window's client area,
   `BitBlt` into a DIB, saved through the GDI+ flat API already declared in
   `ai_chat.c:60-90` (`GdipCreateBitmapFromHBITMAP`, PNG CLSID; add `GdipSaveImageToFile`).
   GDI+ is already started (`window.c:3613`). The declarations move to a small
   `src/ui/gdip_png.{c,h}` both files use.
5. `dlog_flush_wait(500)`, then the status-line flash. No note dialog (decision 1).

Works with debugging off: the mark and snapshot land in the errors-only file and the recorder
holds whatever capture level applied (Errors only, when off).

### 8. AI Assist tool: `read_debug_log`

Registered in `chat_register_tools()` (`ai_chat.c:1181-1242`) only while `debug_enabled`;
`ai_chat_update_tools()` (`ai_chat.c:5121`, called from `window.c:1697`, `:1757`) gains the
flag. Safety `TOOL_SAFE`, like `web_search`: read-only, no approval. Its `DlogToolContext`
(resolved folder, run stem, pid) is copied by value and rebound by `ai_stream_copy_tools()`
(`ai_stream.c:73-91`, which gains a third context argument) before the stream thread starts.

```json
{"type":"object","properties":{
  "since_minutes":{"type":"integer","description":"How far back to read, 1-1440, default 30"},
  "category":{"type":"string","enum":["general","render","term","ai","ssh","shell"]},
  "min_level":{"type":"string","enum":["error","warn","info","debug","trace"]},
  "contains":{"type":"string","description":"Case-insensitive substring, max 128 chars"},
  "session":{"type":"string","description":"Session id such as s2"},
  "around_issue":{"type":"integer","description":"0 = latest issue mark, n = issue n; reads its recorder file and the 2 minutes before it"},
  "summary":{"type":"boolean","description":"Counts per category, level and event plus the issue list, no lines"},
  "max_lines":{"type":"integer","description":"1-1000, default 200"}}}
```

- **Reading**: `dlog_flush_wait(200)` first, then the run's files newest-first within the
  window, opened `FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE`, reading up to the
  last complete LF; a file that disappears mid-read just ends. With no file in use, it reads
  the recorder.
- **Output cap**: 16 KB (about 4-5k tokens), lines over 512 bytes cut with `+N bytes`; the
  maintainer's test key has limited credit, so `summary:true` is the cheap first call the
  description recommends. Truncation keeps the newest lines, or centres on the mark for
  `around_issue`, and a header line says what was dropped:
  `# 2341 matching lines, showing last 200; files: ...; window 13:35:10-14:05:10`.
- **Rate**: at most 4 calls per user message (`agentic_check_rate_limit()`,
  `ai_agentic.c:18-42`), inside the existing `AI_TOOL_LOOP_MAX` of 5.
- **In the chat**: the `using tool:` row (`ai_chat.c:775`) gains a suffix (`using tool:
  read_debug_log - render, last 30 min`); the result row shows the header line, not log lines.
  What the tool reads goes to the configured AI provider; Settings says so (section 9).
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
     [x] Keep recent detail for issue marks   at [ Trace   v]   (last few minutes, in memory)
     [ ] Save a screenshot with each issue mark
     [ ] Include terminal content
         Adds terminal text and the echo of what you type, AI commands, chat text and host
         names. Passwords and keys are never logged.
           Log folder  [ logs                                  ] [Browse...] [Open]
   Max file size (MB)  [ 10  ]
     Keep at most (MB) [ 50  ]   using 37 MB in 9 files
     Current log file  C:\Tools\nutshell\logs\nutshell-20261017-140310-p8124.log
                       Give this folder to a Claude Code session to analyse it.
                       AI Assist can read it too; what it reads goes to your AI provider.
                       Mark an issue with Help > Mark Issue (Ctrl+Shift+M).
```

Unticked, the page shows the tickbox, "Errors are still written to the log folder." and the
Current log file line ("none yet, created at the first error"; in the fallback case the real
path and why; "logging to memory only" when both folders fail).

Browse uses `SHBrowseForFolderA` (`shell32` is already linked, `Makefile:52`); Open uses
`ShellExecuteA(..., "open", folder, ...)`. The current-path line is a read-only `EDIT`. Changes
apply on Save without a restart: `dlog_configure()` swaps levels atomically, and a changed
folder closes the current file with `log.moved to=...` and continues in the new one.

### 10. Config fields

In `Settings` (`config.h:18-64`), defaults in `config_default_settings()` (`loader.c:481`),
clamped in `settings_validate()` (`loader.c:442`). The written form is one nested object for the
six category levels, plus flat keys for everything else (`config_save()`, `loader.c:950-968`):

```json
"debug_enabled": false,
"debug_levels": {"render":"info","term":"info","ai":"info","ssh":"info","shell":"info","general":"info"},
"debug_include_content": false,
"debug_screenshot": false,
"debug_recorder": true,
"debug_recorder_level": "trace",
"debug_log_dir": "logs",
"debug_max_file_mb": 10,
"debug_max_total_mb": 50
```

| JSON key (as written) | Type | Default |
|---|---|---|
| `debug_enabled` | bool | 0 |
| `debug_levels` | object: `render`, `term`, `ai`, `ssh`, `shell`, `general` each a level name | `"info"` each |
| `debug_include_content` | bool | 0 |
| `debug_screenshot` | bool | 0 |
| `debug_recorder` | bool | 1 |
| `debug_recorder_level` | level name | `"trace"` |
| `debug_log_dir` | string (`CFG_STR_MAX`) | `"logs"` |
| `debug_max_file_mb` | number | 10 (1-100) |
| `debug_max_total_mb` | number | 50 (5-5000) |

The loader also accepts two alternative forms on read, but never writes them: the flat
`debug_level_<category>` keys (`debug_level_render`, `debug_level_term`, ... `debug_level_general`),
read after `debug_levels` so they win when both are present, and `debug_issue_screenshot`, read
after `debug_screenshot` and winning likewise. A save writes only the nested and
`debug_screenshot` forms.

Levels are stored as names (`off`, `errors`, `info`, `debug`, `trace`) so a hand-edited config
reads clearly; an unknown name keeps the default. `config.h` includes a tiny
`src/core/dlog_types.h` for the enum, as it already includes `cmd_policy.h`. Backward
compatibility: old configs simply lack the keys. A config with `"debug_terminal": true`
migrates once to `debug_enabled=1`, `debug_levels.term=trace` and `debug_include_content=1`
(the user had already opted in to raw bytes), and `debug_terminal` is no longer written. Older
Nutshell versions ignore the new keys, as the loader only reads keys it knows.

### 11. README: "Analysing an issue with AI" (draft)

> **Analysing an issue with AI.** Turn on **Settings > Debugging > Enable debugging** and leave
> it on; the defaults cost almost nothing. When something looks wrong, press **Ctrl+Shift+M**
> (Help > Mark Issue) *before* clicking or resizing: Nutshell silently writes the state of
> every tab and the last few minutes of detail to the log folder (`logs\` beside
> `nutshell.exe` unless you changed it) and flashes a confirmation. Then ask AI Assist "read the
> debug log around the last issue", or point a Claude Code session at the folder: "Read
> FORMAT.txt and the newest nutshell-*.issue*.log in C:\Tools\nutshell\logs and tell me what
> happened." Passwords, passphrases and keys are never logged, and neither are keystrokes or pastes as such. Terminal text, commands, chat
> and host names appear only if you tick **Include terminal content** (the terminal text then includes the shell's echo of the
> commands you type, so keep secrets off the command line while it is on), and a screenshot is saved
> only if you tick **Save a screenshot with each issue mark**; check before sharing a log.

The Settings table row (`README.md:184`) loses "Debug terminal log" and the shortcuts table
(`README.md:329-355`) gains Ctrl+Shift+M.

### 12. Out of scope

Minidumps, uploading logs, a log viewer inside Nutshell, the per-session text logs, and fixing
the two paint-path follow-ups themselves (this feature is how they get diagnosed).

---

## Files

| Area | File | Change |
|---|---|---|
| core | `src/core/dlog.{c,h}`, `dlog_types.{c,h}` | new: levels, categories and their names, `DLOG`/`DLOG_EV`, `dlog_format_line`, redaction, repeat suppression, rings, `DlogPlatform` |
| core | `src/core/dlog_files.{c,h}` | new: `dlog_resolve_dir`, `dlog_relativize_dir`, naming, rotation and retention decisions, fallback chain, `FORMAT.txt` text |
| core | `src/core/dlog_query.{c,h}` | new: line parser, `dlog_query()`, summary, cap |
| core | `src/core/dlog_snapshot.{c,h}` | new: issue snapshot struct and formatter |
| core | `src/core/logger.{c,h}` | shim in slice 1, deleted in slice 5 |
| core | `src/core/ai_agentic.c:18-42`, `ai_stream.c:73-91` | `read_debug_log` per-message cap; third tool context, rebound |
| config | `src/config/config.h:18-64`, `loader.c:442-496`, `:613-614`, `:884-885` | fields, defaults, clamp, load, save, migration |
| config | `src/config/ssh_io.c:28-81`, `src/term/session_io.h:26` | `debug_log_chunk` and `FILE *` replaced by `DLOG_CONTENT`; `poll()` loses `debug_log` |
| ui | `src/ui/dlog_win.c` | new: SRW lock, writer thread, files, crash filter, clock |
| ui | `src/ui/gdip_png.{c,h}` | new: shared GDI+ declarations, PNG save (from `ai_chat.c:60-90`, `:2392-2440`) |
| ui | `src/main.c:15-40`, `:324`, `:336` | `init_app_log()` replaced by `dlog_start`/`dlog_shutdown` |
| ui | `src/ui/window.c` | `log_id` on `Session` (`:94-133`); `debug_log` removed (`:103`, `:591`, `:691`, `:1041-1070`, `:1243`, `:2014`, `:2034`, `:2994`, `:3887`, `:4327`); render events at `:481`, `:490`, `:2387`, `:500-529`, `:4052-4062`, `:4534-4550`; menu `:2618-2622`; chord `:3116-3133`; configure at `:3472`, `:1641`; tools flag `:1697`, `:1757` |
| ui | `src/ui/renderer.c:160-175`, `:234`, `:408-411`; `redraw_log.h`, `Makefile:194-197` | `REDRAW_DEBUG` block replaced by `render.draw`, `render.stats`, `render.stuck_dirty`; header and target deleted |
| ui | `src/ui/ai_chat.c` | `LOG_INFO` sites `:1820`, `:2087`, `:2341`; tool registration `:1181-1242`, `:5121-5135`; chat suffix `:758-775`; request lifecycle events |
| ui | `src/ui/local_pty.c:141`, `:542-578` | escaper and `FILE *` replaced |
| ui | `src/ui/settings.c`, `src/core/settings_layout.{c,h}`, `src/ui/resource.h` | page, controls, Browse/Open, apply, nav row (`SETTINGS_PAGE_DEBUG`); old checkbox removed (`settings.c:991-995`, `:400`, `:1626`); `IDM_HELP_MARK_ISSUE`, `IDM_HELP_LOG_FOLDER`, control ids; `IDC_DEBUG_TERMINAL` (`:85`) removed |
| demo | `src/core/ui_demo.c:15`, `ui_demo.h`; `tests/integration/cases/30-ui-demo.ps1:34` | `settings-debug` state |
| tests | `tests/test_dlog*.c`, `tests/fake_dlog.{c,h}`, `tests/test_dlog_gate.c`, `test_config.c`, `test_settings_layout.c`, `test_local_pty.c`, `runner.c` | new and updated; `test_logger.c` replaced |
| docs | `README.md:151-215`, `:315-355`; `src/ui/help_guide.c:176` | section 11 |

## Test plan

Native (`make test`, `TEST_TMP_DIR` for every file), positive and negative cases each:

- **Filtering**: every level against every threshold; Off writes nothing; ISSUE always
  passes; capture versus file level; recorder on and off, its level above, equal and below.
- **Formatting** (`dlog_format_line`): UTC offsets, ms padding; quoting of spaces, `"`, `=`,
  controls, invalid UTF-8; the 4096-byte cut on a UTF-8 boundary (straddling, exact, one over)
  with ` truncated=1`; `DLOG_EV`; repeat suppression (identical, differing field, other
  category or session, the 5 s flush).
- **Redaction**: each key name redacted, `key name=` not; each value pattern (Bearer, `sk-`,
  `$dpapi$`, `$aes256gcm$`, PEM) in an innocently named field and in free text, plus near-misses
  that pass; `DLOG_CONTENT` dropped when off; known-secret scrub across a buffer edge;
  `dlog_escape_bytes` on ESC, CR/LF, tab and OSC 52 whole and split across chunks.
- **Rings**: Trace/Debug/Info dropped and counted at the reserve, ERROR/WARN/ISSUE accepted
  into the headroom (fake platform), `dlog.dropped` on the next drain, recorder wrap order,
  memory-only mode evicts oldest.
- **Files**: `dlog_resolve_dir` (empty, relative, absolute, UNC, trailing slash, overlong,
  environment variable); `dlog_relativize_dir`; the fallback chain; `.N` continuation;
  rotation threshold; retention oldest-first, never the current file, total cap only.
- **Query**: malformed lines; each parameter alone and combined; `since_minutes` across a DST
  change; `around_issue` 0, n, missing; summary; both caps and the header; a half line ignored.
- **Snapshot**: dirty-row ranges (none to all). **Instrumentation**: `render.stuck_dirty`
  fires once per episode at the threshold, not below; `render.stats`.
- **Config**: defaults, round-trip, clamps, unknown level names, the `debug_terminal`
  migration, a config with none of the keys. **Settings layout**: nav order, `min_h`.
- **Source gate**, plus a check that it fails on a planted violation.

`--ui-demo=settings-debug` opens Settings on the Debugging page with debugging ticked in
memory only (never saved) and is added to the gallery list.

Manual, on Windows, before the PR is marked ready:

1. Rendering at Trace, Terminal at Debug, recorder on. Reproduce the 2026-10-14 sequence: two
   tabs, one disconnected; `nano` in a tab with scrollback; switch tabs; a long `cargo install`
   with tab switches during output. Confirm `poll.cooldown_skip`, `scrollbar` and
   `render.draw rows_left_dirty` lines appear and the file stays within caps.
2. Validate against the real bug with a throwaway local build that reverts the v1.2.34
   condition in `renderer.c:408` (never committed). When the stale band appears, press
   Ctrl+Shift+M before clicking; check the issue file shows the frame (all rows clean after a
   paint whose `rows_covered=0`), that `render.stuck_dirty` fired, and that a Claude Code
   session given only the folder and section 11's prompt reaches the v1.2.34 diagnosis.
3. Ask AI Assist "read the debug log around the last issue": chat row, 16 KB cap, `summary:true` first.
4. Point the folder at a read-only directory: fallback path shown in Settings, WARN written;
   make the fallback unwritable too: memory-only shown, marks and the AI tool still work.
5. Debugging off for a day of normal use: no file unless an error occurred.
6. Kill the process mid-output: the file ends with complete lines. During a Trace flood the
   UI never stalls, `dlog.dropped` appears and an ERROR written meanwhile is in the file.

## Implementation slices (each independently testable)

1. **Core logger**: `dlog_types.{c,h}`, `dlog.c` (filter, `dlog_format_line`, redaction, repeat
   suppression, rings with headroom, platform seam, fake), `logger.h` shim. Tests: filtering,
   formatting, redaction, rings.
2. **File policy**: `dlog_files.c`, `FORMAT.txt` text. Tests: paths, naming, rotation,
   retention, fallback chain.
3. **Config**: fields, defaults, clamp, load/save, migration. Tests: round-trip, migration.
4. **Windows backend and startup**: `dlog_win.c`, `main.c`, `WM_CREATE` configure, crash
   filter; `%TEMP%\nutshell.log` gone. Check: a run writes `app.start`; kill test.
5. **Instrumentation**: render (replacing `REDRAW_DEBUG`, adding `render.stats` and
   `render.stuck_dirty`), term (`term.rx`, `debug_log` out of the vtable), ai (the ten legacy
   sites), ssh, shell; `logger.*` deleted; the source gate. Check: manual step 1.
6. **Settings page** and `--ui-demo=settings-debug`. Check: gallery screenshot, four themes.
7. **Issue marker**: snapshot formatter (tested), recorder dump, `gdip_png`, menu, chord,
   status flash. Check: manual step 2.
8. **AI tool**: `dlog_query.c` (tested), registration, context rebind, rate cap, chat suffix.
   Check: manual step 3.
9. **Docs**: README section, Settings table, shortcuts, help guide.

Every build bumps the version per CLAUDE.md; the exe is committed with the final slice's PR.

## Open questions

1. The issue note dialog: wanted, or mark silently with only a brief status flash?
2. Ctrl+Shift+M as the chord (it would otherwise send CR); or Ctrl+Shift+F12, or menu only?
3. Host names, user names and profile names are logged without the content tickbox because
   SSH diagnosis needs them. Should they be content instead, for logs that get shared?
4. Should the screenshot sit behind "Include terminal content" or be its own tickbox?
5. Recorder default on, at 4 MB: acceptable for a portable app, or default off?
6. One tickbox for terminal, command and chat content, or two?
7. Migrating `debug_terminal: true` to content on: right, or leave content off and ask?
8. Should AI Assist also get a "Mark issue" button?
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

## Merged-design decisions (2026-10-18)

The two drafts (`a5875bb`, `0660d44`) disagreed in places; this document reconciles them.

1. The local shell's category tag is `shell` (not `pty`), UI label "Local shell", everywhere.
2. Maximum line length 4096 bytes, cut at a UTF-8 boundary, ` truncated=1` appended.
3. Rotation 10 MB per file (1-100), total cap 50 MB (5-5000) by default; no `max_files`, no 200 MB.
4. Fallback chain: configured folder (environment variables expanded), `%LOCALAPPDATA%\Nutshell\logs`, memory only (evicts oldest).
5. Producers never block: reserved queue headroom for ERROR/WARN/ISSUE replaces the 50 ms wait; Trace/Debug/Info are dropped and counted; writer, 250 ms timer, crash drain and flushes unchanged.
6. The recorder is a 4 MB ring, on by default, with its own `debug_recorder_level` (default Trace), captured per category that is not Off.
7. API: fieldless `DLOG_EV` beside `DLOG` (empty format trips `-Wformat-zero-length`), pure `dlog_format_line`, `dlog_relativize_dir`.
8. Redaction runs both the key-name rule with its allow-list and a value-pattern pass (Bearer, `sk-`, `$dpapi$`, `$aes256gcm$`, PEM); `DLOG_CONTENT()` and the source gate stay.
9. From the second draft: `render.stats` (Debug) and `render.stuck_dirty` (WARN) plus all `REDRAW_DEBUG` data; host and port in `connect.*` only with content on, profile name and session id always.
10. Issue marker: silent status flash, Ctrl+Shift+M, Help > Mark Issue and Open Debug Log Folder, screenshot behind its own tickbox (off), GDI+ path, separate `.issue<n>.log`.
11. `read_debug_log` as in the first draft (limits, six categories, 16 KB, 4 calls per message, only while debugging is on, provider note in Settings).
12. Config keys: the file writes one nested `debug_levels` object (`render`, `term`, `ai`, `ssh`, `shell`, `general`) and the flat `debug_screenshot`, `debug_recorder`, `debug_recorder_level` and the rest of section 10; on read it also accepts the flat `debug_level_<cat>` keys and `debug_issue_screenshot`, the flat form winning when both are present. `debug_terminal: true` migrates to `debug_levels.term=trace`, `debug_include_content=1` (and `debug_enabled=1` so it takes effect).
