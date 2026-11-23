# Enable switch for the AI assistant — design

**Status**: DRAFT, for the maintainer's approval. Written 2026-11-23. It applies
the principle in CLAUDE.md, "Optional features: off unless enabled, not loaded
while off", to the AI assistant. Decisions are marked **(maintainer)** or
**(call)**, as in the voice design. This ships as its own pull request, before
voice PR 1 (`2026-11-23-voice-input-output-design.md`, §9), which depends on it.

## Behaviour

- **Setting:** an **Enable AI assistant** tick box at the top of
  AI Assistant > Provider **(call: placement)**.
- **Default** **(maintainer)**:
  - A new install, or a config with no AI API key saved, starts **off**.
  - A config that has an AI key saved (`ai_api_key` or its
    `ai_api_key_enc_preserved` blob) when this version first loads it starts
    **on**. The decision is made once, at the first load that lacks the field,
    and the result is saved.
- **Off means not loaded** **(maintainer)**:
  - The AI panel (`g_hwndAiChat`) is never created, docked or undocked.
  - View > "AI Assist" and "Undock AI Assist" are removed from the menu, and
    the AI shortcuts (CLAUDE.md shortcut registry, section "AI Assist panel")
    are inactive. Their keys reach the terminal as if the feature did not
    exist.
  - No provider request, model-list fetch, web_fetch or web_search is ever
    made.
  - No AI worker thread starts.
  - `winhttp.dll` is delay-loaded and used only by the AI code
    (`src/ui/ai_http_win.c`, `src/core/ai_tool_web_fetch.c`,
    `src/core/ai_tool_web_search.c`, `src/core/winhttp_open.c`), so it is never
    mapped.
  - Settings shows the AI pages' controls disabled except the switch. The
    key and other AI settings are kept, unused. The Refresh (models) button
    makes no request.
  - Voice is off and its switch disabled, with the reason "Needs the AI
    assistant".
- **Turning it on** in Settings and saving creates the panel and menu items and
  activates the shortcuts, with no restart.
- **Turning it off** while the assistant is working (a reply streaming, a
  command running, or a card waiting) asks for confirmation at Save:
  "Turn off the AI assistant? The running reply will be stopped and open
  conversations closed." Then everything in flight is cancelled, threads are
  joined, the panel destroyed and the menu items removed. When nothing is
  running, it turns off without asking. Open conversations are discarded
  **(call)**: nothing would be left to show them. A conversation saved to a file
  is unaffected.
- **Startup paths** that would open the panel, such as `--ui-demo` states that
  need AI, override the switch for that run only and never save it **(call)**.
  The demo has to keep working for the gallery.

## Tests

- Native:
  - The first-load default rule: no field and no key gives off; no field and a
    key gives on; no field and only a preserved blob gives on; a saved field
    wins over both.
  - Load and save round-trip of the field.
  - The dependency rule (voice disabled while AI is off).
- `wintest` (per the CLAUDE.md principle):
  - With the switch off, after window creation, `GetModuleHandleW(L"winhttp.dll")`
    is NULL, no AI panel window exists, and the AI shortcuts pass through.
  - Switching on creates the panel. Switching off destroys it and leaves no AI
    thread running.
- Integration harness: the existing AI cases run with a config whose switch is
  on. One gate case starts with it off and checks that the View menu has no
  AI items.
- Windows hand check: upgrading the maintainer's real config, which has a key,
  keeps the assistant on.

## Left to the builder

- Whether "off" removes the menu items or greys them. Removal is preferred,
  per "as if it did not exist".
- The module boundary for "AI init" and "AI teardown" within `window.c` and
  `ai_chat*.c`.
