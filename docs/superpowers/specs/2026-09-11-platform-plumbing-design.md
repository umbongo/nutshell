# Per-profile device platform, with auto-detection (audit H2)

**Date**: 2026-09-11
**Fixes**: security audit **H2** — every command is classified with the Linux ruleset whatever
the device is, because both `chat_approval_add` call sites pass `CMD_PLATFORM_LINUX` literally
and `AiSessionState.platform` is never assigned.
**Builds on**: `2026-09-11-command-classification-coverage.md`, which added the rules this
change finally routes traffic to.

---

## 1. The shape of the fix

A profile gains a **Device platform** setting. Its default, and the value every existing
profile inherits when an older config is loaded, is **Auto-detect**. At connect time the
session takes the profile's platform; when that is Auto-detect, a pure detector reads the
login banner and first prompt and resolves it. Both `chat_approval_add` call sites then pass
the session's platform instead of the `CMD_PLATFORM_LINUX` literal.

Three things have to be true for this to be an improvement rather than a new hazard:

1. An explicit profile setting always wins. Detection never overrides what the operator chose.
2. Detection resolves only on a **positive** signal. No signal means unresolved, not "Linux".
3. Unresolved must be **safe and usable**, because it is the state every session starts in and
   the state a quiet or unusual device stays in. Section 2 is about this.

---

## 2. `CMD_PLATFORM_UNKNOWN` — what an unresolved session uses

The obvious two candidates for the unresolved default are both wrong:

- **Default to Linux** is today's bug. `reload`, `write erase`, `reset saved-configuration`
  and `erase startup-config` all classify SAFE on a switch, so they auto-approve at the lowest
  auto-approve level. This is exactly what H2 describes.
- **Default to a network ruleset** is safe but hostile on a Linux host: every network
  classifier ends in a conservative `CMD_WRITE` fallthrough, so `ls -la` becomes WRITE and
  needs `permit_write`. An operator who hits that on every second command turns on a blanket
  auto-approve, and the net effect on safety is negative.

So unresolved gets its own ruleset: **the Linux classifier plus a network-verb overlay.**

```c
CMD_PLATFORM_UNKNOWN   /* appended to the enum, like the others */
```

`classify_unknown_segment()` runs the overlay first, then delegates to
`classify_linux_segment()`. The overlay is a short list of first-token verbs that are
destructive on some network CLI and are **not** commands on any Linux system, so claiming
them costs a Linux session nothing:

| First token | Destructive on | Level |
|---|---|---|
| `reload` | IOS, NX-OS, ASA, Aruba, ArubaOS | CRITICAL |
| `reboot` | Comware, VyOS, RouterOS *(already CRITICAL on Linux — harmless overlap)* | CRITICAL |
| `erase` | IOS, ProCurve, Aruba CX | CRITICAL |
| `factory-reset`, `restore` *(+ `factory-default`)* | ArubaOS, Comware | CRITICAL |
| `write` *(with `erase`)* | IOS, ArubaOS | CRITICAL |
| `commit` | PAN-OS, Junos, VyOS, IOS-XR | CRITICAL |
| `rollback` | PAN-OS, NX-OS, Comware | CRITICAL |
| `delete` | PAN-OS, Junos, FortiOS | CRITICAL |
| `purge` | FortiOS, NX-OS | CRITICAL |
| `undo` | Comware | CRITICAL |
| `request` *(+ `system`/`restart`/`shutdown`)* | PAN-OS, Junos | CRITICAL |
| `execute` *(+ `factoryreset`/`reboot`/`restore`)* | FortiOS | CRITICAL |
| `boot` | Aruba CX, ProCurve, IOS `boot system` | CRITICAL |
| `no`, `shutdown`, `configure`, `system-view`, `set`, `config` | every network CLI | WRITE |
| `reset` *(+ `saved-configuration`/`bgp`/`ospf`/`isis`/`session`)* | Comware | CRITICAL |
| a `/path` head with `remove` / `reset-configuration` / `reboot` / `shutdown` / `disable` / `downgrade` / `format-drive` anywhere | RouterOS | CRITICAL |

**The overlay may only raise a level, never lower it.** Two of the WRITE verbs are worse than
WRITE on a real Linux host — `shutdown` is in `linux_critical_cmds` — so an overlay that simply
claimed the verb would *downgrade* an unresolved Linux session from CRITICAL to WRITE, which is
the opposite of the point. Where an overlay WRITE row matches, `classify_unknown_segment()`
therefore also asks `classify_linux_segment()` and returns the higher of the two. The CRITICAL
rows need no such guard, CRITICAL being the ceiling.

The last two rows exist because their first token identifies nothing on its own: a bare
`reset` on Linux is the harmless terminfo terminal reset, and every RouterOS command hides its
verb behind a `/path` head (`/system reset-configuration`), which the Linux classifier reduces
to a basename it does not know. A Linux absolute-path invocation such as
`/etc/init.d/nginx stop` shares none of the RouterOS verbs.

A bare `commit` or `delete` typed at a Linux shell is a command-not-found, so over-claiming
them is free. The one real overlap is `set`, which is a shell builtin — it is WRITE in the
overlay, and `set` with no arguments on a Linux host merely prints the environment. That is
the single accepted false positive, and it is in the safe direction.

**Consequence to state plainly**: a Linux session whose banner the detector does not recognise
classifies exactly as it does today, except that those verbs may be raised. With the
raise-only rule above, nothing regresses.

---

## 3. Detection

A pure function, in `src/core/` so it is testable natively (`src/ui` is excluded from test
builds):

```c
/* src/core/cmd_detect.h */
typedef enum {
    CMD_DETECT_NONE = 0,   /* nothing recognised; caller keeps CMD_PLATFORM_UNKNOWN */
    CMD_DETECT_PROMPT,     /* prompt shape only -- weaker */
    CMD_DETECT_BANNER      /* an unambiguous product string */
} CmdDetectConfidence;

CmdPlatform cmd_detect_platform(const char *text, size_t len,
                                CmdDetectConfidence *confidence_out);
```

Rules:

- Case-insensitive substring search over the text, banner signals first, prompt shapes second.
- Returns `CMD_PLATFORM_UNKNOWN` with `CMD_DETECT_NONE` when nothing matches. **Never guesses
  Linux from absence** — Linux needs its own positive signal like everything else.
- A `CMD_DETECT_BANNER` result may replace a `CMD_DETECT_PROMPT` result; a prompt result never
  downgrades a banner result. The caller stores the confidence alongside the platform.

### 3.1 Banner signals (strong)

| Substring | Platform |
|---|---|
| `Cisco Nexus Operating System`, `NX-OS` | NX-OS |
| `Cisco Adaptive Security Appliance` | ASA |
| `Cisco IOS Software`, `IOS-XE Software`, `Cisco Internetwork Operating System` | IOS |
| `ArubaOS-CX`, `AOS-CX` | Aruba OS-CX |
| `ArubaOS (MODEL:`, `Aruba Operating System` | ArubaOS |
| `ProCurve`, `ProVision`, `HP J9`, `Aruba JL` | ProCurve |
| `Comware Software`, `H3C Comware`, `HPE Comware` | Comware |
| `PAN-OS`, `Palo Alto Networks` | PAN-OS |
| `JUNOS `, `Junos OS`, `Juniper Networks` | Junos |
| `FortiGate`, `FortiOS`, `Fortinet` | FortiOS |
| `Welcome to VyOS`, `VyOS ` | VyOS |
| `MikroTik RouterOS`, `RouterOS ` | RouterOS |
| `Welcome to Ubuntu`, `Debian GNU/Linux`, `Red Hat Enterprise Linux`, `CentOS`, `Rocky Linux`, `AlmaLinux`, `SUSE Linux`, `Alpine Linux`, `Arch Linux`, `Linux ` *(as in the uname line of a motd)* | Linux |

### 3.2 Prompt shapes (weaker, checked only if no banner matched)

Matched against the **last non-empty line** of the text, which is where the prompt sits.

| Shape | Platform | Note |
|---|---|---|
| `<hostname>` — a line that starts `<` and ends `>` | Comware | unique to Comware |
| `(hostname) #` or `(hostname) *#` | ArubaOS | the parenthesised form is unique |
| `[admin@name] >` | RouterOS | unique |
| `user@host:~$`, `user@host:/path#`, or any line ending `$ ` with a `:` before it | Linux | the `:`+`$` pair is what distinguishes it from Junos/PAN-OS `user@host>` |
| `user@host>` | **ambiguous** — Junos *and* PAN-OS | resolve to `CMD_DETECT_NONE`; see below |
| `hostname#` / `hostname>` | **ambiguous** — IOS, NX-OS, ASA, ProCurve, Aruba CX, FortiOS | resolve to `CMD_DETECT_NONE` |

Ambiguous prompt shapes deliberately return NONE rather than a guess. The session stays on
`CMD_PLATFORM_UNKNOWN`, whose overlay (section 2) already covers the destructive verbs those
families share — which is precisely the case the overlay was designed for.

---

## 4. Config and UI

### 4.1 `Profile`

```c
char platform[32];   /* "auto" (default), "linux", "cisco-ios", ... */
```

Stored as a string to match every other config field and to keep older and newer builds
readable against the same file. `cmd_classify.h` gains the two mappings, so the string ↔ enum
conversion is tested natively rather than living in the dialog code:

```c
CmdPlatform cmd_platform_from_name(const char *name);  /* unknown name -> CMD_PLATFORM_UNKNOWN */
const char *cmd_platform_name(CmdPlatform p);          /* stable config token */
const char *cmd_platform_label(CmdPlatform p);         /* UI label, e.g. "Cisco IOS / IOS-XE" */
```

Config tokens, in dropdown order: `auto`, `linux`, `cisco-ios`, `cisco-nxos`, `cisco-asa`,
`hp-procurve`, `hp-comware`, `aruba-cx`, `aruba-os`, `panos`, `junos`, `fortios`, `vyos`,
`routeros`. `auto` is not a `CmdPlatform` value — it is the absence of an override, stored in
the profile and resolved at connect time; `cmd_platform_from_name("auto")` returns
`CMD_PLATFORM_UNKNOWN`, which is the correct starting state for a session awaiting detection.

Loader: read with a default of `"auto"` when the key is missing, so every existing profile in
an existing `nutshell.config` keeps working and opts into detection automatically.

### 4.2 Dialog

One combo box, **Device platform**, in the profile editor, defaulting to *Auto-detect*. The
dialog holds no vendor strings of its own: it fills the list by walking
`cmd_platform_choice_count()` / `cmd_platform_choice_label()`, and converts the selected index
with `cmd_platform_choice_name()`. Adding a vendor later then touches one table in
`cmd_classify.c` and nothing in the UI.

The profile editor is resource-script driven, not hand-positioned: `IDD_SESSION_MANAGER` is a
fixed `DIALOGEX 0, 0, 530, 317` in `src/ui/resource.rc:12-53`, with rows packed at fixed 18-20
dialog-unit steps (Name 18, Host 36, User 54, Auth 72, Password 90, Key 110, AI Notes 130) and
the Save/Connect/Cancel buttons at y=262. There is no vertical slack, so the new row goes in
directly below **Auth** and everything below it shifts down one step, with the dialog's `cy`
raised to match. `IDC_COMBO_AUTH` (`src/ui/resource.h:42`) is the control to copy — both its
`COMBOBOX ... CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP` line and its wiring:

- populate + select in `WM_INITDIALOG` (`session_manager.c:221-224`),
- profile → control in `form_load()` (`:119-130`),
- default in `form_clear()` (`:106-116`),
- control → profile in `form_read()` (`:197-199`).

The custom scrollbar widgets are positioned in C relative to their owning controls
(`session_manager.c:283-316`), so they follow controls moved in the `.rc` automatically.

---

## 5. Runtime wiring

`struct Session` (`src/ui/window.c:65-88`) already owns both halves as sibling fields —
`Profile conn_profile` and `AiSessionState ai_state` — so no new plumbing is needed to get
them into the same scope.

1. **At connect.** `on_session_connect()` copies the profile at `window.c:723`
   (`s->conn_profile = *info;`). Directly after that line:

   ```c
   s->ai_state.platform = (int)cmd_platform_from_name(s->conn_profile.platform);
   s->platform_locked   = (s->ai_state.platform != CMD_PLATFORM_UNKNOWN);
   s->platform_scanned  = 0;
   ```

   An explicit profile setting therefore locks the session: detection may never overwrite what
   the operator chose. `create_session()` (`:262`) already zeroes `ai_state` at `:278`, which
   leaves an unconnected session on `CMD_PLATFORM_LINUX` (value 0) — set it to
   `CMD_PLATFORM_UNKNOWN` there so the pre-connect state is the safe one too.

2. **Detection window.** There is no "banner arrived" event; data arrives through the periodic
   poll in the main timer (`window.c:~2170`), where `ssh_io_poll(...) > 0` means new bytes
   just landed. While `!s->platform_locked && !s->platform_scanned`, call
   `term_extract_last_n(s->term, 40, buf, sizeof buf)` (`src/core/term_extract.h`) and feed it
   to `cmd_detect_platform()`. Set `platform_scanned` once the detector returns
   `CMD_DETECT_BANNER`, or after a bounded number of poll ticks with no result — a banner that
   has not arrived within the first few screens is not going to. Re-running on each tick until
   then is what handles a banner split across chunks.

3. **Classification.** Both `chat_approval_add` call sites in `src/ui/ai_chat.c` (`:3600` and
   `:3749`) pass the session's platform — `(CmdPlatform)src->platform` and
   `(CmdPlatform)d->active_state->platform` respectively — instead of the `CMD_PLATFORM_LINUX`
   literal. This is the two-line change that closes H2; everything else in this document exists
   to make the value it passes correct.

Per-session, not global: `AiSessionState` is embedded in each `Session`, the AI panel tracks
the displayed one in `AiChatData.active_state` (`ai_chat.c:265`), and `on_tab_select`
(`window.c:305-328`) repoints it on every tab switch. So a Linux tab and a switch tab open at
once classify by their own rulesets, which is the point.

## 6. Test plan

Native tests, `src/core` only:

- `tests/test_cmd_detect.c` — one realistic multi-line banner per platform from §3.1, each
  asserting platform **and** `CMD_DETECT_BANNER`; one prompt-shape case per row of §3.2; both
  ambiguous shapes asserting `CMD_PLATFORM_UNKNOWN` + `CMD_DETECT_NONE`; empty input, NULL
  input, text with no prompt at all, and a banner arriving split across two chunks.
- `tests/test_cmd_classify.c` — the §2 overlay: each overlay verb under
  `CMD_PLATFORM_UNKNOWN`; plus `ls -la`, `cat /etc/hosts`, `grep -r x /var/log` → SAFE under
  `CMD_PLATFORM_UNKNOWN`, proving the Linux path is intact; plus `rm -rf /` → CRITICAL; plus
  `shutdown` → CRITICAL under **both** `CMD_PLATFORM_UNKNOWN` and `CMD_PLATFORM_LINUX`, which
  is the regression test for the raise-only rule.
- `tests/test_cmd_classify.c` — name mapping round-trips for every token, an unknown token,
  NULL, and `"auto"` → `CMD_PLATFORM_UNKNOWN`.
- `tests/test_config.c` — a profile with no `platform` key loads as `"auto"`; a profile with an
  explicit platform round-trips through save/load; a profile with a garbage platform string
  loads as something `cmd_platform_from_name` maps to `CMD_PLATFORM_UNKNOWN`.
