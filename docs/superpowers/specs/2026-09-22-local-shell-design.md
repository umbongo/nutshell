# Local shell: Nutshell as a portable bash for Windows

**Date**: 2026-09-22
**Status**: DRAFT, revised after one Opus critique (see the Critique section)
**Branch**: `local-shell`
**Versions**: two pull requests. **1.2.0** is the transport seam (section 2), a refactor with
no behaviour change. **1.2.1** is the local shell itself (sections 3-6). A third change,
embedding the shell payload in the exe (section 4.4), waits on the maintainer's licence
decision and gets its own version when it comes.
**Builds on**: `2026-08-28-cli-params-and-autoconnect-design.md` (startup paths),
`2026-09-11-platform-plumbing-design.md` (`Profile.platform`, `CMD_PLATFORM_*`).

---

## Problem

Nutshell is an SSH terminal with an AI assistant. The maintainer wants the same window,
terminal and assistant to work with **no remote host at all**: a portable `nutshell.exe`
that, launched on any Windows PC, opens a bash shell with the usual commands (`ls`, `grep`,
`sed`, `awk`, pipes, scripts) on the local machine, with the AI assistant reading and driving
that shell exactly as it drives an SSH session today.

Today every byte in and out of the terminal goes through libssh2. The `Session` struct in
`src/ui/window.c:82-105` holds an `SshSession *` and an `SSHChannel *`; `s->channel` is not
only the transport but the app's "connected" predicate, tested in 43 places in `window.c`
(the poll gate at `:2274`, paste at `:1330`, the grid sync at `:243`, the paste-owner
identity checks at `:1167`, `:2341`, `:2365`, `:2379`). The AI panel is typed on
`SSHChannel *` (`src/ui/ai_chat.h:7,35,45`) and uses it as its enable predicate in six
places, with the error text "[error: no active SSH channel]". The system prompt opens with
"You are an AI assistant for an SSH terminal session" (`src/core/ai_prompt.c:163`). The
terminal emulator (`src/term/parser.c`; `term.c` is an empty unit) has no SSH dependency;
`tests/test_term.c` feeds it raw bytes.

Three decisions are costly to migrate later: where the shell comes from (section 1), the
seam between the terminal and its byte source (section 2), and how a local session is
represented in the config and the UI (section 5).

---

## 1. Where the shell comes from

"Bash on Windows, portable" has no free answer. The candidates:

| | Payload | Size added to the exe | What the user gets | Portable |
|---|---|---|---|---|
| **A** | busybox-w32 (`busybox64.exe`, one file) | ~0.75 MB raw if embedded; 0 as a sidecar | ash with bash compatibility, ~200 applets including coreutils, `grep`, `sed`, `awk`, `find`, `tar`, `gzip`, `vi`, `less`, `wget` | yes, offline, Windows 10 1809+ |
| **B** | MSYS2 / Git for Windows bash 5 + coreutils | ~15-25 MB raw, ~6-8 MB packed | real GNU bash and coreutils | yes, but the committed exe roughly triples on every build commit (104 exe commits already; LFS is an open item) |
| **C** | detect an installed Git for Windows (`HKLM\SOFTWARE\GitForWindows\InstallPath`, `%ProgramFiles%\Git\bin\bash.exe`) or MSYS2 (`C:\msys64\usr\bin\bash.exe`) | 0 | real bash where present | no: a clean PC gets nothing |
| **D** | WSL | 0 | a real Linux, not the local Windows filesystem | no |

**Decision.** The shell is resolved at session start from a fixed order (section 4.2):
an explicit command in the profile, then a `busybox64.exe` **sidecar** next to `nutshell.exe`
or in the runtime directory, then Git for Windows bash, then MSYS2 bash. That gives the
portable case (drop `busybox64.exe` beside the exe; two files in a folder) and the
real-bash case (C) with no payload in the exe, no extraction, and no licence question in the
code change. The busybox shell in bash-compatibility mode runs `ls`, `grep` and the rest as
internal applets, so scripts behave the same on every PC, which also keeps the AI's command
classification identical across machines.

**Embedding A in the exe (one file instead of two) is section 4.4 and a separate change.**
Nutshell is MIT; busybox-w32 is GPLv2. Embedding it as a resource and UPX-packing it inside
`nutshell.exe` distributes GPL object code inside the shipped binary, so GPLv2 section 3
applies to the release: the corresponding source must accompany it or a written offer must.
That is satisfiable (attach the exact busybox-w32 source archive to every GitHub release,
record version and SHA-256 in `third_party/busybox/README.md`, name it in About and README),
but it is the maintainer's call and it is made before any embedding code is written, not
after. Everything in this spec works identically whether the payload is a sidecar or embedded.

Known gaps of A against real bash, stated in the README: no arrays (`declare -a`,
`mapfile`), no associative arrays, no `coproc`; `[[ ]]`, functions, `local`, arithmetic,
here-documents, `source`, brace expansion and process substitution work. B is rejected on
size; D is out of scope.

---

## 2. The seam: `SessionIo` (version 1.2.0, refactor only)

Everything above the byte stream stops naming libssh2 types. A vtable, **header only**, in
`src/term/session_io.h`:

```c
typedef enum { SESSION_SSH = 0, SESSION_LOCAL = 1 } SessionKind;

typedef struct SessionIo {
    /* Same contract as ssh_io_poll: >0 read, 0 nothing, -1 error, -2 EOF. */
    int  (*poll)(void *ctx, Terminal *term, FILE *log_file, FILE *debug_log);
    int  (*write)(void *ctx, const char *data, size_t len);
    int  (*resize)(void *ctx, int cols, int rows);
    void (*close)(void *ctx);        /* releases the transport; the SessionIo is dead after */
    void *ctx;
    SessionKind kind;
} SessionIo;
```

- **The SSH implementation lives in `src/config/ssh_io.c`**, which the Makefile already
  drops from the native build when libssh2 is absent (`Makefile:118-119`). No new `.c` file
  goes into `src/term` or `src/config`, so `make test` on a Linux host without libssh2 keeps
  linking. `session_io_ssh(SshSession *, SSHChannel *)` fills the vtable from today's four
  functions; behaviour is unchanged.
- **`Session` gains `SessionIo io`** (by value, `io.ctx == NULL` meaning "no transport").
  `io.ctx != NULL` replaces `s->channel != NULL` as the connected predicate at all 43 sites.
  `g_paste.channel` becomes `g_paste.io_ctx` (a `void *` compared by identity, as today).
  `ssh` and `channel` stay on `Session` only for the SSH-specific paths: the connection
  thread, host-key prompt, keepalive and idle timeout (`2026-04-27-ssh-keepalives-...`,
  skipped when `io.kind == SESSION_LOCAL`), and `bytes_read_total`.
- **The AI panel** takes `SessionIo *` (`ai_chat_set_session(HWND, Terminal *, SessionIo *)`,
  `active_io` instead of `active_channel`); its six enable checks test `active_io != NULL`;
  the three writes at `ai_chat.c:1629-1633` become `io->write`; the error text becomes
  "[error: no active session]". `ai_chat.h` stops including `ssh_channel.h`.
- **EOF and close order.** The EOF branch in `window.c:2377-2390` and every close path call
  `ai_chat_set_session(hwnd, NULL, NULL)` **before** `io.close()`, and the pending-dispatch
  waiter (`ai_chat.c:1803-1811`) aborts its batch with a visible "session ended" note when
  `active_io` becomes NULL instead of waiting for a prompt that cannot come. This fixes an
  existing dangling pointer, not only a future one.
- **Reconnect** (`on_status_click`, `window.c:1179-1203`) dispatches on the profile kind:
  SSH re-launches the connection thread as today; a local profile re-spawns the shell
  (section 3). The password re-population at `:1192-1203` runs only for SSH profiles and
  matches on kind as well as host, user and port.
- **`ai_build_system_prompt()`** gains the session kind and shell name; the SSH text is
  unchanged for SSH sessions. The prompt tests gain the local variant.

The rejected alternative, a second pair of pointers on `Session` with a branch at each call
site, turns 43 sites into 43 branches and puts the same branch in the AI panel.

**Acceptance for 1.2.0.** `make test` green on Windows and on a Linux host; the integration
`gate` tier passes by hand (the SSH path is what this refactor can break, and only that tier
exercises it); the gallery unchanged.

---

## 3. The ConPTY backend (version 1.2.1)

`src/ui/local_pty.c`, Win32 only, excluded from the native build by the existing
`NON_TEST_SRCS` rule. Exposes `LocalPty *local_pty_open(const LocalShellSpec *)`,
`session_io_local(LocalPty *)`, and the four vtable functions.

- **Compile target.** The file does **not** raise `_WIN32_WINNT`. The three functions
  (`CreatePseudoConsole`, `ResizePseudoConsole`, `ClosePseudoConsole`) are resolved with
  `GetProcAddress` on `kernel32.dll`; the constants the headers guard behind
  `NTDDI_WIN10_RS5` are defined locally when absent
  (`PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE` = `0x20016`, `HPCON` is unguarded in
  `wincontypes.h:122`). Missing functions fail the open with "Local shell needs Windows 10
  version 1809 or later." in the terminal.
- **Open.** Two anonymous pipes, the pseudo-console created at the tab's current grid size,
  `CreateProcessW` with `EXTENDED_STARTUPINFO_PRESENT`, the pseudo-console attribute,
  `CREATE_UNICODE_ENVIRONMENT`, the command line and environment from section 4 converted
  from UTF-8 with `MultiByteToWideChar(CP_UTF8, ...)`, working directory from
  `GetEnvironmentVariableW("USERPROFILE")`. Nutshell is a GUI process with no console of its
  own, so it is never attached to the pseudo-console and never receives its control events.
  The child-side pipe ends are closed after the process starts.
- **Reading.** A reader thread (default stack; nothing large on it) blocks in `ReadFile` and
  appends to a 64 KB heap ring guarded by a critical section, dropping into a "full" flag
  rather than blocking if the UI is not draining. `poll()` runs on the UI thread, drains the
  ring into `term_process()` and the two log files as `ssh_io_poll()` does, and returns `-2`
  once the reader has seen EOF or the process handle is signalled. The Terminal is touched
  only from the UI thread, as everywhere else.
- **Writing.** `WriteFile` on the input pipe. Enter is `\r`, as the key encoder already
  sends. **The paste path** (`window.c:1285-1287`, `:1391-1396`) today strips `\r` and sends
  `\n`; for a local session it sends `\r` per line end, because a console child reads Enter,
  not LF. Ctrl+C is the byte `0x03`; conhost turns it into the child's control event.
- **Resize.** `ResizePseudoConsole`; a resize that arrives before the process exists is
  recorded and applied at open, so the grid sync guard in `window.c:243` moves to
  `io.ctx != NULL` without a window where the size is lost.
- **Close.** In order: `ClosePseudoConsole` (ends the conhost, which closes the pipe and
  unblocks the reader for a well-behaved tree), close our pipe handles, `CancelSynchronousIo`
  on the reader thread, wait 500 ms; if the process is still alive, `TerminateProcess`; wait
  another 500 ms for the reader; if it has still not returned, detach it (the handles are
  closed, its next `ReadFile` fails) rather than block the UI thread. Free.
- **Bytes.** ConPTY emits xterm sequences. `handle_private_mode` (`src/term/parser.c:276-288`)
  handles modes 1, 25, 1049 and 2004 and discards the rest; there is no DSR/DA reply path.
  The Windows-only test in section 8 records what ConPTY actually sends on this host
  (including whether it queries with `ESC[6n` or `ESC[c`); a query that needs an answer gets a
  reply hook (`poll` given a write-back) in this change, otherwise the limitation is recorded
  in the notes document's open list. No special case in the backend.

---

## 4. The runtime: which shell, command line, environment

Pure logic in `src/core/local_shell.c` (tested natively); Win32 calls only in `local_pty.c`.

### 4.1 Runtime directory
`%LOCALAPPDATA%\Nutshell\runtime\`. Only read in this change (a place to put a sidecar
busybox that survives moving the exe). If `LOCALAPPDATA` is unset the directory is simply
absent from the search; nothing is written.

### 4.2 Which shell
`local_shell_resolve(const char *profile_shell, const LocalShellProbe *probe, LocalShellSpec *out)`
takes the profile's `shell` and a probe struct of callbacks (`exists(path)`, `env(name)`,
`registry_string(key, value)`), and fills the command line, the shell kind and the extra
environment. Order:

1. `profile_shell` non-empty: used verbatim, kind `SHELL_CUSTOM`;
2. `busybox64.exe` (or `busybox.exe`) next to `nutshell.exe`: `"<path>" bash -l`, kind `SHELL_BUSYBOX`;
3. the same in the runtime directory;
4. Git for Windows: registry `InstallPath`, else `%ProgramFiles%\Git`; `"<path>\bin\bash.exe" --login -i`, kind `SHELL_GITBASH`;
5. MSYS2 at `C:\msys64\usr\bin\bash.exe`: the same command, plus `MSYSTEM=MSYS`, kind `SHELL_MSYS2`;
6. none: `LOCAL_SHELL_NONE`, and the terminal shows "No shell found. Put busybox64.exe next to nutshell.exe, install Git for Windows, or set a shell command in the profile."

Paths with spaces are quoted; the resolver is a pure function of the probe results.

### 4.3 Environment
The parent's block plus `TERM=xterm-256color`, `HOME=%USERPROFILE%`, `SHELL=<exe>`,
`NUTSHELL=<APP_VERSION>`, `MSYSTEM` when 4.2 says so, and the chosen shell's directory
prepended to `PATH`. Nothing is removed; `git`, `python`, `code` keep working.

### 4.4 Embedded payload (deferred, own change)
If the maintainer settles the licence (section 1): `IDR_BUSYBOX RCDATA` in `nutshell.rc`
behind `#ifdef NUTSHELL_HAVE_BUSYBOX`, extracted to
`<runtime>\<APP_VERSION>\busybox64.exe` by temp-file-then-`MoveFileEx`, verified against the
SHA-256 recorded at build time before first use, falling back to 4.2's search when
`LOCALAPPDATA` is unset or the write fails (a read-only share, an AppLocker policy). It slots
in as step 2 of 4.2. Not in 1.2.1.

---

## 5. Profile, startup and UI (version 1.2.1)

- **Profile.** `Profile` gains `char kind[16]` (`"ssh"` when the key is absent, or `"local"`)
  and `char shell[MAX_PATH]` (custom command line; empty means 4.2). A local profile ignores
  host, port, username and auth, and the loader accepts them empty when `kind` is `local`.
  **Downgrade**: an older exe ignores unknown keys and rewrites profiles from its struct
  (`loader.c:371-405`, `:531-559`), so opening the config with 1.1.x and saving drops `kind`
  and `shell`. Recorded in the README's upgrade note; not fixable from this side.
- **A real saved profile named "Local shell".** On the first load by 1.2.1, if no profile
  with kind `local` exists, one named "Local shell" is inserted at index 0 and saved like any
  other. It is an ordinary row: Edit and Delete act on it by index as they do today, the user
  may rename it, and it can be the autoconnect target or `-sn` argument. No synthetic row.
- **`--local`** is `CLI_CONNECT_LOCAL`: start a session from a transient profile (name
  "Local shell", kind `local`, empty `shell`) **without** looking the name up, so a saved
  profile with that name pointing at a host cannot hijack it. `--local` with `-sn` or `-h`
  is `CLI_ERROR`.
- **Session Manager.** The New/Edit dialog gets a "Type" choice (SSH / Local shell); Local
  hides host, port, user and auth and shows "Shell command (blank = automatic)". Built
  token-only: `ns_tokens()`, `ns_font()`, `ns_scale()`, `ns_type.h`; no new entry in the
  `test_ui_tokens.c` allow-lists. Tab title is the profile name; `tabs_set_connect_info()`
  gets `%USERNAME%` and `COMPUTERNAME` so the status line reads as it does for SSH.
- **Session log** file name (`window.c:585-600`) uses the profile name when `host` is empty.
- **`--ui-demo`** gains a `local` state that builds a demo session like the others
  (`create_demo_session`, `window.c:949-990`): tab chrome and status line only, no process.
- **Not changed.** Themes, fonts, the AI panel layout, the demo harness otherwise.

---

## 6. The AI assistant on a local session (version 1.2.1)

- The assistant drives the shell as it drives SSH: `\x05\x15`, the command, `\r` through
  `io->write`; output via `term_extract_last_n()`. No new tool.
- **Platform.** From the profile; when `auto`, the shell kind from 4.2 decides:
  `CMD_PLATFORM_LINUX` for busybox, Git bash and MSYS2, with `platform_locked = 1` so the
  banner scan is skipped. `SHELL_CUSTOM` keeps `auto` with the scan.
- **Safety.** The classifier does not know that `rm -rf` now runs on the operator's own
  disk. That is a property of the ruleset, not the transport; the README says so in one
  sentence. Auto-approve levels apply as for SSH.
- **Context.** The system prompt's opening line depends on the kind (section 2): for local,
  "a local shell (busybox / Git bash / MSYS2 / custom) on a Windows PC", so the model does not
  suggest `apt` or `systemctl`.

---

## 7. Build, packaging and version

- 1.2.0: `APP_VERSION` `1.2.0`, `APP_VERSION_BINARY` `1,2,0,0`, README `v1.2.0`; 1.2.1 likewise.
- No Makefile change in either; `local_pty.c` is picked up by the `src/ui` wildcard and
  excluded from tests by the existing rule. The exe stays UPX-packed as today.
- README: a "Local shell" section (what it is, `--local`, the sidecar, which shell is used,
  the bash gaps, the safety sentence, the downgrade note).
- `third_party/busybox/README.md` only when 4.4 happens.

---

## 8. Tests

Native (`make test`), pure code in `src/core`:

- `tests/test_local_shell.c`: `local_shell_resolve()` for every combination of probe
  results and the custom field; exact command lines including quoting of paths with spaces;
  the environment additions (`TERM`, `HOME`, `PATH` prepend, `MSYSTEM` only for MSYS2,
  nothing dropped); the runtime path for a given `LOCALAPPDATA` and its absence.
- `tests/test_pty_ring.c`: the ring buffer (`src/core/pty_ring.c`) at wrap, full, and
  interleaved push/drain; the EOF flag.
- `tests/test_term_conpty.c`: recorded ConPTY byte streams (fixtures under `tests/fixtures/`)
  from busybox `ls -l` and `vi` open-and-quit, through `term_process()`, checked against the
  expected screen.
- `tests/test_cli_args.c`: `--local`; `--local` with `-sn` or `-h` is `CLI_ERROR`.
- Config: a profile without `kind` loads as `ssh`; `kind: local` with empty host loads; the
  first-load insertion of "Local shell" happens once and not when a local profile exists.
- Prompt: `ai_build_system_prompt()` for both kinds.

Windows (`make wintest`, the existing Win32 harness): `tests/test_local_pty.c` opens a
pseudo-console on `cmd.exe /c echo nutshell-ok`, drains through the emulator, checks the
screen, then checks close returns within a second; a second case spawns a child that spawns
a sleeping grandchild and checks close still returns. This case also records what ConPTY
sends on open (section 3, last bullet).

Manual, Windows: the integration harness gains `local_shell` (`-Tier gate`): launch
`--local`, send `echo nutshell-$$`, expect the echo; `exit`, expect the tab disconnected;
click reconnect, expect a prompt again. The `gate` tier is run before 1.2.0 is marked ready,
because it is the only thing that exercises the refactored SSH path end to end.

---

## 9. Out of scope

WSL; SFTP or file transfer; tab titles from the shell's working directory (OSC 7); the
embedded payload (4.4) until the licence is settled; a `cmd.exe` or PowerShell profile (a
custom `shell` will run one, but it is untested and undocumented here); a DSR/DA reply path
unless the Windows test shows ConPTY needs one.

---

## Critique

One round, Opus (`claude-opus-5`), 2026-09-22, briefed per the `critique` skill: sixteen
findings, one cheaper alternative. Dispositions:

1. `PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE` guarded behind `NTDDI_WIN10_RS5`, would not compile. **Accepted**: section 3 defines the constant locally, no `_WIN32_WINNT` change.
2. Reconnect refuses empty-host profiles and always runs the SSH thread. **Accepted**: section 2, reconnect dispatches on kind.
3. Reconnect's password re-population would copy another profile's password into a local session. **Accepted**: SSH-only, matches on kind.
4. `s->channel` is the connected predicate in 43 places, not four. **Accepted**: section 2 replaces it with `io.ctx` at all sites and names the paste owner.
5. A new `.c` under `src/term` breaks the libssh2-less test build. **Accepted**: header-only vtable; SSH wrappers in `ssh_io.c`.
6. GPLv2 section 3 is not satisfied by a README URL and the decision was deferred past the point the code creates the obligation. **Accepted**: embedding is removed from this change (4.4, deferred); the sidecar and detected shells carry no obligation.
7. Config downgrade unstated; a synthetic first row breaks the manager's index model. **Accepted**: real saved profile; downgrade note in README.
8. Profile names are not unique, so `--local` as `-sn "Local shell"` can be hijacked. **Accepted**: `--local` never looks a name up.
9. The AI panel and system prompt are typed and worded for SSH; the cost was not priced. **Accepted**: section 2 lists the panel changes and the prompt parameter.
10. EOF leaves the panel holding a freed transport; the dispatch waiter spins. **Accepted**: `ai_chat_set_session(NULL)` before close, waiter aborts.
11. Close can deadlock the UI thread on a reader blocked in `ReadFile`. **Accepted**: `CancelSynchronousIo`, bounded waits, detach as last resort.
12. Section 8 could not fail on the risky parts and did not protect the SSH path. **Accepted**: `make wintest` cases for open, close and the grandchild; the `gate` tier is a stated acceptance for 1.2.0; ring buffer moved to `src/core` for a native test.
13. The fixture test does not cover DSR/DA or the discarded private modes; `term.c` is not the emulator. **Accepted in part**: the Windows test records what ConPTY actually sends and the reply hook is conditional on that; the file reference is corrected. A general reply path for SSH is not in scope.
14. Unhandled cases: resize before spawn, `LOCALAPPDATA`, AV/AppLocker on an extracted exe, no integrity check, ANSI vs Unicode, Ctrl+C, CRLF paste, session log name, `--ui-demo`, ring size. **Accepted**: each is now specified (sections 3, 4.1, 4.4, 5); the AV and integrity items move with the embedding to 4.4.
15. Session Manager UI vs the design-system gate. **Accepted**: token-only, no allow-list change, stated in section 5.
16. One version for four gate cycles; exe growth unpriced. **Accepted**: two pull requests, 1.2.0 seam and 1.2.1 shell; no exe growth in either because nothing is embedded.

Cheaper alternative (drop embedding, ship the seam, ConPTY, profile and UI with detected or
sidecar shells). **Accepted**; it is the shape above.

Not verified by the critic, to be settled by the `make wintest` case in 1.2.1: busybox-w32's
bash-compatibility surface; whether ConPTY treats a bare LF as Enter and whether it emits
DA/DSR queries; whether `0x03` reaches only the child's process group; UPX behaviour with an
embedded payload (moot until 4.4).
