# Older backlog: hand checks still open from PRs #85 to #103

**Status**: NOT RUN
**Needs**: an SSH host (a Linux host, not a network device); an AI key; the second monitor and display scales for #87; a paramiko server for #89 (`tests/manual/kbdint_server.py`; needs `python3` with paramiko on the Linux host)
**Time**: 60 minutes

These come from the "Windows hand checks never recorded as run" item in the
notes' Open list (docs/superpowers/specs/2026-09-06-ui-redesign-notes.md). The
notes give no step by step for most of them, so the lines are short. Ask the
session for steps where one says so. Test these last: they are older than
every other file here.

## PR #85: guard rails

1. **web_fetch refuses local addresses**
   Do: With Permit web fetch on, ask the assistant to fetch `http://127.0.0.1`, `http://[::1]` and `http://169.254.169.254`, then a page that redirects to one of them.
   Expect: Each is refused.
   If it fails: Note the address and what happened.
   Result:

2. **SSH to a silent port**
   Do: Connect an SSH profile to a port where nothing answers the handshake (steps for a silent port: ask the session).
   Expect: It fails after about 15 seconds with the stage named.
   If it fails: Note the time and message.
   Result:

3. **Clear-text URL warning**
   Do: Use a Custom provider with an `http://` Base URL.
   Expect: A warning that the URL is not encrypted.
   If it fails: Screenshot.
   Result:

4. **The "quoted, not run" chip**
   Do: Ask the assistant to show an `[EXEC]` command inside a code block.
   Expect: It is shown with a "quoted, not run" chip and not run.
   If it fails: Screenshot.
   Result:

## PR #86: AI chat split

5. **A real session**
   Do: Connect, send a message that streams, paste an image, and run one approved command.
   Expect: All four work. (Likely covered by the v1.2.77 Kimi test, not recorded as such.)
   If it fails: Note which step.
   Result:

## PR #87: About, version, build

6. **About at 125 % and 200 % and after a DPI change**
   Do: Open Help > About at 125 % and 200 %, and again after changing the scale while Nutshell is open.
   Expect: Text fits and shows a real zlib version.
   If it fails: Screenshot.
   Result:

7. **`-v` prints two lines**
   Do: `.\nutshell.exe -v`
   Expect: Two lines.
   If it fails: Note the output.
   Result:

8. **`mingw32-make test` twice**
   Do: Run `mingw32-make test` twice in a row on the Windows box.
   Expect: Both pass. The harness case `cli_version_redirect_file` is also listed unrun (see `harness.md`).
   If it fails: Note the failure.
   Result:

## PR #88: data block and cache control

9. **Data block at a loopback listener**
   Do: (steps not recorded; ask the session). The notes say the data block must be seen at a loopback listener.
   Expect: Not recorded.
   If it fails: Not recorded.
   Result:

10. **Anthropic `cache_control` returns 200**
    Do: Send a message with the Anthropic provider and a real key.
    Expect: A normal reply, no HTTP error.
    If it fails: Note the message.
    Result:

11. **Sensitive cards at Read**
    Do: Set the ceiling to Read. Ask the assistant to run a sensitive command (for example reading shell history).
    Expect: It still asks first.
    If it fails: Note what happened.
    Result:

## PR #89: keyboard-interactive login

12. **Hand tests against the test server**
    Do: Run `tests/manual/kbdint_server.py` on a Linux host and connect to it with a profile (the spec's hand tests: ask the session).
    Expect: Each server prompt is shown and answered as the spec says.
    If it fails: Note the prompt and what happened.
    Result:

## PR #92: themes

13. **Four-theme gallery judgement**
    Do: Run `.\nutshell.exe --ui-demo=all` on each of the four themes (`--theme "<name>"`).
    Expect: Moss & Mist's deep-sage accent and the light-theme indicator greens look right to you.
    If it fails: Screenshot and theme name.
    Result:

## PR #93: Settings

14. **The spec's 13-item checklist**
    Do: (steps not recorded here; the list is in the PR #93 spec: ask the session).
    Expect: All items pass.
    If it fails: Note the item.
    Result:

## PR #94: AI panel and run_command

15. **v1.2.82 on screen**
    Do: Look at the AI panel title, the fold anchor and the row heights, then change the display scale.
    Expect: Rows keep sensible heights after the scale change.
    If it fails: Screenshot.
    Result:

16. **Retry, New Chat, undock and redock**
    Do: Use Retry on a failed reply, New Chat, Undock AI Assist and redock.
    Expect: Each works and the conversation survives.
    If it fails: Note which.
    Result:

17. **An error ending**
    Do: Cause a reply to end with an error (wrong model name).
    Expect: The reply ends with a clear error.
    If it fails: Screenshot.
    Result:

18. **`cls` and `Clear-Host` capture**
    Do: Run `cls` in the local PowerShell shell and `clear` on the Linux host, then ask the assistant what is on screen.
    Expect: It sees the cleared screen.
    If it fails: Note what it saw.
    Result:

19. **`--ui-demo=tool`**
    Do: `.\nutshell.exe --ui-demo=tool`
    Expect: The demo window shows the tool row.
    If it fails: Screenshot.
    Result:

## PR #96 and #101: Thinking row

20. **Extended mode**
    Do: Run `.\nutshell.exe --ui-demo=thinking-extended` on the four themes. Then with a real reasoning model watch a reply stream.
    Expect: A box of 25 lines that follows the tail. A click cycles collapsed, compact, extended, open. Settings > AI Assistant > Behaviour > Thinking display "Always extended" persists after a restart.
    If it fails: Screenshot.
    Result:

21. **Reading in place**
    Do: Scroll up while a reply completes under Automatic.
    Expect: The text you are reading stays in place.
    If it fails: Note the jump.
    Result:

22. **All four modes**
    Do: `--ui-demo=thinking`, `thinking-compact`, `thinking-open` on four themes. Click to cycle. Change it, restart, undock and redock.
    Expect: Mode survives restart and undock. Settings > Automatic resets it. (The maintainer reported #96 fine on 2026-11-13; run again only if you want it itemised.)
    If it fails: Screenshot.
    Result:

## PR #103: help text

23. **Search Engine help tip**
    Do: In Settings > AI Assistant > Web Access hover the Search Engine row.
    Expect: The tip does not say Gemini and Custom get no tools. It says they get them with "Commands as tool calls" On.
    If it fails: Screenshot.
    Result:

## All

24. **Gallery**
    Do: `.\nutshell.exe --ui-demo=all` on the four themes.
    Expect: Every panel state looks right. (The `ui_gallery` harness case does the same, 20 states x 4 themes: see `harness.md`.)
    If it fails: Screenshot and theme.
    Result:

## Not in these checks

The notes' Open list also records Windows-box builds for several PRs (`mingw32-make test`, `mingw32-make wintest`, `mingw32-make clean && mingw32-make release`). Those are in the files above.
