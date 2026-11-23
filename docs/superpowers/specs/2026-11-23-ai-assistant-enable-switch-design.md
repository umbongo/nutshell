# Enable switch for the AI assistant — design

**Status**: DRAFT, for the maintainer's approval. Written 2026-11-23. It applies
the principle in CLAUDE.md, "Optional features: off unless enabled, not loaded
while off", to the AI assistant, and adds the configuration upgrade notice that
principle calls for. Decisions are marked **(maintainer)** or **(call)**.

It ships as its own pull request, before voice PR 1
(`2026-11-23-voice-input-output-design.md`, §9), which depends on it.

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

## 3. Configuration upgrade notice

This is general and applies to every version that adds settings
**(maintainer)**.

- At launch, if the config file lacks fields this version knows (a new
  install has no file and is exempt), the missing fields are added with their
  decided values and the file is saved immediately, not at the next Settings
  save.
- A one-time notice then appears once the main window is shown. It is
  **non-blocking**: a themed dismissible notice, not a modal message box **(call)**.
  Its wording:

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

  - The list names each added setting and the value it was given, in plain
    words. A version that adds no settings shows nothing.
  - "Review in Settings" opens the page of the first listed setting.
- **Older versions** **(maintainer, from review)**: an older exe saves only the
  fields it knows (`loader.c:1063`), so it drops the new ones. The next launch
  of a newer version adds them again and shows the notice again. If the user
  had turned the assistant off but kept the key, it comes back on, and the
  notice says so. This is accepted and documented, since shipped versions
  cannot be changed. A native test names the behaviour.

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
  - Which fields the notice lists, and its text.
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
  refusing while off, if it is added to the wintest sources.
- **Integration harness:** the existing AI cases run with the switch on. A
  gate case starts with it off and checks that the View menu has no AI items
  and that the self-report is clean. A case with an upgrade-lacking config
  checks that the file was rewritten and the notice shown.
- **Windows hand checks:**
  - Upgrading the maintainer's real config, which has a key: the assistant
    stays on and the notice reads well.
  - Turning it off mid-reply stops at once.

## 7. Left to the builder

- Removing the menu items versus greying them out. Removal is preferred, per
  "as if it did not exist".
- The notice's visual form within the design system.
- The module boundary for AI init and teardown, and the handle registry for
  cancellation.
