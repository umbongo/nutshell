# Enable switch for the AI assistant — design

**Status**: DRAFT, for the maintainer's approval. Written 2026-11-23. It applies
the principle in CLAUDE.md, "Optional features: off unless enabled, not loaded
while off", to the AI assistant, and adds the configuration notice that principle
calls for, shown on an upgrade and on a first run. Decisions are marked **(maintainer)** or **(call)**.

It ships as its own pull request, before voice PR 1
(`2026-11-23-voice-input-output-design.md`, §9), which depends on it. A
small follow-up pull request moves the remaining message boxes onto the
notice dialog straight after it, also before voice PR 1 (§3)
**(maintainer)**.

Reviewed 2026-11-23 by `claude-fable-5-2` (effort high). Its findings are
folded in and marked "from review".

## 1. The switch

- **Setting:** an **Enable AI assistant** tick box at the top of
  AI Assistant > Provider **(call: placement)**.
- **Default** **(maintainer)**:
  - A new install, or a config with no AI API key saved, starts **off**.
  - A config that has an AI key saved when this version first loads it starts
    **on**. The key may be `ai_api_key` or only its `ai_api_key_enc_preserved`
    blob.
    - A preserved blob alone counts as configured. The blob is the user's key
      locked to another PC. A saved "off" would then follow the file home,
      which is wrong for the user who set it up there.
  - The decision is made once, at the first load that lacks the field, and is
    written to the file at once (§3).

## 2. Off means not loaded

The claim is that **Nutshell's own code** never loads the assistant while it
is off. Windows' shell dialogs and shell extensions can map DLLs into the
process on their own, and no design can promise otherwise **(from review)**.

The switch is checked at one gate per entry point **(from review)**:

- **`create_ai_chat()`** is the only constructor of the panel (`g_hwndAiChat`),
  docked or undocked. It refuses while off. This also covers:
  - the tab strip's AI button (`window.c:4569-4572`), which is hidden while off;
  - the reopen-after-connect path (`g_ai_reopen_after_connect`,
    `window.c:2232-2236`, `2292`, `5560`);
  - the demo's direct create (`window.c:2601`), via the runtime override in
    §5.
- **`ai_chat_init()`** moves from `WM_CREATE` (`window.c:4574`) to the first
  enable. It loads `Riched20.dll` and `Msftedit.dll` (`ai_chat.c:2307-2308`),
  which nothing else uses.
- **`ns_winhttp_open()`** (`src/core/winhttp_open.c`) is the single opener of
  every WinHTTP session (`ai_http_win.c:78,221,381`,
  `ai_tool_web_fetch.c:492`, `ai_tool_web_search.c:419,551`). It refuses while
  off. No provider request, model-list fetch, web_fetch or web_search can then
  happen, and `winhttp.dll` (delay-loaded) is never mapped by our code.
- **Menu and shortcuts:** View > "AI Assist" and "Undock AI Assist" are
  removed, and the AI shortcuts are inactive, so their keys reach the terminal
  as if the feature did not exist. The Help > Keyboard Shortcuts sheet leaves
  out its "AI Assist panel" section while off. The README block, which is
  generated and byte-compared, stays complete **(from review)**.
- **Settings:** the AI pages' controls are disabled except the switch. Refresh
  (models) makes no request until the switch has been saved on. The key and
  other AI settings are kept, unused.
- **Voice** is off and its switch disabled, with the reason "Needs the AI
  assistant".

## 3. Notices: one format, starting with upgrade and first run

**A shared notice format** **(maintainer)**. Nutshell gets one notice
component, and every notice uses it: the two below, the configuration
warnings, and any added later. Each notice has its own text. What stays the
same is the form and how it behaves.

**Delivery: a themed modal dialog** **(maintainer: "if that means using
popups, so be it"; recommended by review)**. It is the only form that works
both at launch and from inside Settings or the Session Manager, because the
window that raises it owns it. Reviewed 2026-11-23 by `claude-fable-5-2`
(effort high), which weighed it against the alternatives:

- **A non-blocking banner in the main window** fails twice. The main loop
  has no `IsDialogMessage` (`window.c:1257-1261`), so Enter and Escape would
  go to the shell. A banner would also sit in a disabled main window whenever
  Settings or the Session Manager is open, including the Session Manager
  opened at start (`window.c:5276-5278`).
- **`TaskDialogIndirect`** has no dark mode, and a static import would risk
  the comctl32 launch failure of PR #90.
- **`MessageBox`** cannot be themed and has no bold labels or "Review in
  Settings" button.

The form:

- **Layout:** a title, an opening line, an optional list of items in bold
  label + plain value form, an optional closing line, and up to two buttons
  (an optional action such as "Review in Settings", and OK).
- **Tone:** information or warning, drawn with the design-system `info` and
  `warning` intents. The tone changes only the accent and icon, never the
  layout.
- **Controls:** real `STATIC` and `BUTTON` children, as in `pw_prompt.c:107-149`,
  not painted text, so screen readers and the harness can read it. Colours
  come from `ns_tokens()`, fonts from `ns_font`, buttons from
  `ns_draw_button`. It takes the owner's DPI and handles `WM_DPICHANGED` as
  `cheatsheet_dlg.c:334-345` does. It is centred on its owner and is not
  topmost.
- **Keys:** the modal loop handles them, since owner-drawn buttons defeat
  default-button handling (`window.c:3611-3618`). Enter presses the focused
  button, Escape or the close box means OK, Tab moves between the buttons,
  and Ctrl+C copies the notice as plain text.
- **Modal loop:** a copy of the cheatsheet's (`cheatsheet_dlg.c:396-400`,
  `pw_prompt.c:243-254, 337-356`): it re-enables the owner only if it was
  enabled before, ends on `IsWindow` or a closing flag, and reposts a real
  `WM_QUIT`.
- **The action button** closes the notice, then posts `IDM_EDIT_SETTINGS`
  with the page (`window.c:4716`), so Settings never opens nested inside the
  notice's loop.
- **Owner and timing:**
  - At launch the config loads inside `WM_CREATE` (`window.c:4420-4487`),
    before `ShowWindow` (`window.c:6308`) and before the theme is resolved
    (`window.c:4528`). `WM_CREATE` therefore only queues its notices, by
    value. The queue is shown at the top of `WM_STARTUP_CONNECT`
    (`window.c:5233`), once the main window is visible and before any
    auto-connect. Today's boxes appear with no main window at all, so this
    is also a fix.
  - Elsewhere the owner is the window that raised it: Settings or the
    Session Manager for their save failures.
  - Worker threads never show one directly. They post to the UI thread, as
    `WM_CONN_PROMPT` does (`window.c:5295`).
- **Order:** one at a time, warnings before information. At most two are due
  at launch (an invalid config whose save then fails).
- **The code:** content and ordering as data in `src/core/notice.c`, layout
  in `src/core/notice_layout.c`, the window (class `Nutshell_Notice`) in
  `src/ui/notice_dlg.c`, matching the `*_layout.c` + dialog split of
  `cheatsheet_dlg.c` and `pw_prompt.c` **(from review)**.
- **Demo:** launch notices are discarded under `--ui-demo`. Two states,
  `notice-info` and `notice-warning`, open it over the demo window. Like
  `shortcuts` (`window.c:2641-2644`) they are not part of `all`, because the
  dialog is modal, and a gallery case modelled on
  `help_shortcuts_sheet_gallery` captures them on all four themes.
- **What does not use it:**
  - Yes/no decisions: `session_manager.c:796,846`, `settings.c:2200`,
    `ai_chat_status.c:103`, `window.c:2749`, and the host-key prompt
    (`window.c:5350`), which is security-critical and blocks a worker.
  - `window.c:6277,6304` (window registration or creation failed), where
    there is no window or theme yet.
  - Transient status, which belongs in the tab or panel.
- **The other 19 OK-only message boxes move in a follow-up pull request**
  **(maintainer)**, straight after this one and before voice PR 1. This one
  introduces the dialog, the upgrade logic and the harness changes; keeping
  the bulk move separate keeps each reviewable and limits each Windows
  harness run to the dialogs it changes. They are:
  - main window (7): `window.c:1194` (connection in progress), `2274` (tab
    limit), `2772` (no profile to reconnect), `3988` and `4001` (debug log
    folder), `5288` (session not found), `5499` (connection error);
  - Session Manager (4): `session_manager.c:768,787` (select a session
    first), `826,945` (enter a hostname);
  - Settings (5): `settings.c:1001` (log folder path), `1752,1781,1942,1951`
    (Refresh Models);
  - shared log-folder helper (2): `open_dir.h:21,26`;
  - AI panel (1): `ai_chat.c:855` (conversation save failed).

  Each keeps its text and behaviour, with the warning or information tone its
  icon has today. Whether some field-validation messages ("Enter a
  hostname", "Enter an API key first") would be better shown beside the
  field is a separate question, not part of that pull request.

**The configuration warnings move onto the format in this pull request**
**(maintainer)**. They are the four message boxes in `window.c`'s config
load (`window.c:4426-4487`) and the three "Save Failed" boxes in Settings
(`settings.c:2248`) and the Session Manager (`session_manager.c:809,911`).
Each keeps what it does today, including turning saving off for the run;
only the form and wording change **(call: wording)**:

| Today | Title | Text |
|---|---|---|
| No folder for the config (`4426`) | Settings can't be saved this run | Nutshell couldn't find a folder for nutshell.config: neither its own folder nor %LOCALAPPDATA% is available. Settings and sessions won't be saved until it is restarted with one available. |
| File unreadable (`4444`) | Your configuration couldn't be read | nutshell.config may be locked by another program, or larger than expected. This run uses the standard settings and saves nothing, so your file is left untouched. Restart Nutshell once the file can be read. |
| File invalid (`4457`) | Your configuration couldn't be loaded | nutshell.config wasn't valid, so it was kept beside the new one as `nutshell.config.bad-<timestamp>` (the real name is shown). This run starts with the standard settings. |
| Save failed at launch (`4484`) | Your configuration couldn't be saved | nutshell.config couldn't be written, so what was just added (the "Local shell" profile, new settings, or a new file) will be gone at the next start. |
| Save failed in Settings, Session Manager | Your changes couldn't be saved | The existing text for each: which change was not written to disk. |

The launch save-failure row also covers the upgrade and first-run saves
below: when either save fails, it replaces the information notice.

- **"Shown once" can be lost** **(call, from review)**. The file is saved
  before the notice is dismissed, so if the process is killed while it is
  open, the notice is not shown again. This is accepted.

**The first two notices.** This applies to every version that adds settings
**(maintainer)**. Today a first run with no `nutshell.config` says nothing
(`window.c:4453`, `CONFIG_LOAD_MISSING`).

- **When they are shown.** Each appears once, in one of two cases:
  - **Upgrade:** the config file lacks fields this version knows. The
    missing fields are added with their decided values and the file is saved
    at once, not at the next Settings save.
  - **First run:** no config file was found (`CONFIG_LOAD_MISSING`). The
    defaults are written to a new file at once, beside the exe as today.
  - Neither is shown when the file was found and is complete, nor
    for an invalid or unreadable file: those keep their existing
    warnings, and the unreadable case saves nothing.
- **Wording, upgrade:**

  > **Welcome to Nutshell 1.2.NN**
  >
  > This version introduces new settings, and your configuration has been
  > updated to include them:
  >
  > - **AI assistant**: on, as your API key is already set up
  > - **Voice**: off until you choose to turn it on
  >
  > Everything else is just as you left it.
  >
  > [Review in Settings]   [OK]

- **Wording, first run** **(call: wording)**:

  > **Welcome to Nutshell 1.2.NN**
  >
  > No configuration was found, so a new one has been created beside
  > nutshell.exe. These optional features start switched off:
  >
  > *(When the exe's folder was not usable and the file went to
  > %LOCALAPPDATA%, the opening line names that folder instead.)*
  >
  > - **AI assistant**: off until you choose to turn it on
  > - **Voice**: off until you choose to turn it on
  >
  > Everything else uses the standard settings.
  >
  > [Review in Settings]   [OK]

- **Their lists.**
  - On an upgrade it names each added setting and the value it was given, in
    plain words. A version that adds no settings shows no notice.
  - On a first run it names every optional feature (CLAUDE.md, "Optional
    features") and its starting state, which is off.
  - "Review in Settings" opens the page of the first listed setting.
- **If the file cannot be written** on an upgrade or a first run, the
  information notice is not shown, since it would be untrue. The "couldn't be
  saved" warning above appears instead.
- **Older versions** **(maintainer, from review)**: an older exe saves only the
  fields it knows (`loader.c:1063`), so it drops the new ones. The next launch
  of a newer version adds them again and shows the upgrade notice again. If
  the user had turned the assistant off but kept the key, it comes back on,
  and the notice says so. This is accepted and documented, since shipped
  versions cannot be changed. A native test names the behaviour.

## 4. Turning it on and off

- **On:** saving with the switch on runs `ai_chat_init`, creates the panel,
  menu items and tab button, and activates the shortcuts, with no restart.
- **Off, while idle:** it turns off without asking.
- **Off, while working** (a reply streaming, a command running, or a card
  waiting): Save asks "Turn off the AI assistant? The running reply will be
  stopped and open conversations closed." Then:
  - **Real cancellation** **(maintainer)**. Today cancel is a polled flag over
    synchronous WinHTTP calls with 120-300 s timeouts (`ai_stream.c:44-48`,
    `ai_http_win.c:86,229,389`). `web_search` ignores it
    (`ai_tool_web_search.c:323`), and the stream thread's handle is closed at
    launch (`ai_chat_send.c:304-311`). The change has four parts:
    - each in-flight request registers its request handle;
    - cancel closes it from the UI thread, which aborts the blocked WinHTTP
      call at once;
    - worker threads keep their handles so they can be waited on (bounded) and
      joined;
    - web_fetch and web_search honour cancel.

    The panel's Stop button uses the same path and becomes immediate too.
  - **Conversations are discarded** **(call)**. That means every session's
    `Session.ai_state` and pending tool batches (`window.c:1140`) are cleared,
    not only the panel window. The panel's `WM_DESTROY` otherwise saves the
    conversation back into the session (`ai_chat.c:2261-2264`). A conversation
    already saved to a file is unaffected.
  - The panel is destroyed (the dock toggle already exercises a full teardown,
    `window.c:2727-2739`, `ai_chat.c:2218-2278`), and the menu items, tab
    button and shortcuts are removed.

## 5. Demo override

`--ui-demo` states that need AI turn it on for that run through a **runtime
flag**, never by writing `settings`. A Settings save inside a demo must not
persist it **(from review)**.

## 6. Tests

- **Native:**
  - The first-load rule: no field and no key gives off; no field and a key
    gives on; no field and only a preserved blob gives on; a saved field wins
    over both.
  - The re-add after an older exe dropped the field.
  - Which fields the notice lists, and its text, in both variants: an
    upgrade, and a first run with no file.
  - Which notice each config load outcome produces (missing, invalid,
    unreadable, no folder, save failed) and their order, with the content
    built as data so it is testable without a window.
  - The notice layout's geometry.
  - The load and save round-trip, and the voice dependency rule.
- **The real exe, not a `wintest`** **(from review)**. The `wintest` binary
  does not contain `window.c`, `ai_chat*.c` or the delay-import library, so a
  module check there passes vacuously. Instead:
  - Install a delay-load notification hook (`__pfnDliNotifyHook2`). It records
    each DLL our code asks for, and our run-time `LoadLibraryExW` sites record
    theirs.
  - Add a diagnostic self-report: a command-line switch that prints the
    recorded list and the process's module list, then exits after window
    creation. It is cheap and has no effect otherwise.
  - Assert, with AI off, that `winhttp.dll`, `Riched20.dll` and `Msftedit.dll`
    were never requested by our code. This runs under Wine on `automaton` and
    in the harness on Windows.
- **`wintest`:** what it can honestly test. For example, `ns_winhttp_open`
  refusing while off, if it is added to the wintest sources. Also the notice
  dialog: it opens, its text reads back through its `STATIC` children, and it
  closes on Enter and Escape (`pw_prompt.c`, `ns_font.c` and `ns_draw.c` are
  already in `WIN_TEST_SRCS`).
- **Integration harness** **(from review)**: a modal notice blocks
  auto-connect, so every case needs a complete config.
  - `Nutshell_Notice` joins `DIALOG_CLASSES` (`NutshellIT.psm1:648`).
  - The new fields join `New-NutshellTestEnv`'s settings table
    (`NutshellIT.psm1:706-720`), so no case sees an upgrade notice. LAUNCH-1
    (`50-window.ps1:67`) then catches any future field that is forgotten.
  - LAUNCH-2, which asserts that a first run shows no dialog
    (`50-window.ps1:140`), is rewritten to expect the first-run notice.
    LAUNCH-3, which matches only a `#32770` titled "Configuration Warning"
    (`50-window.ps1:44,189-191`), is rewritten for the warning notice.
  - `automaton` has no `pwsh`, so the case files are checked by a Windows run
    before the merge.
- **Integration harness cases:** the existing AI cases run with the switch on. A
  gate case starts with it off and checks that the View menu has no AI items
  and that the self-report is clean. A case with an upgrade-lacking config
  checks that the file was rewritten and the notice shown. A case with no
  config file checks that one was created and the first-run notice shown.
  A case with an invalid config checks the warning notice and the `.bad-`
  copy.
- **Windows hand checks:**
  - Upgrading the maintainer's real config, which has a key: the assistant
    stays on and the notice reads well.
  - Turning it off mid-reply stops at once.
  - The notice on Windows 11: dark title bar, per-monitor DPI when dragged
    between monitors, and Narrator reading it (Wine cannot show these).

## 7. Left to the builder

- Removing the menu items versus greying them out. Removal is preferred, per
  "as if it did not exist".
- The notice dialog's exact visual form within the design system.
- The module boundary for AI init and teardown, and the handle registry for
  cancellation.
