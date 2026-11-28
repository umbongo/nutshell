# Windows test log

This folder lists what still has to be tested by hand on Windows. The exe is
built on Linux (`automaton`) and has not been run on Windows since v1.2.94
(2026-11-24).
Wine runs the automated tests there, but it cannot show a real microphone,
real speakers, the Windows 11 title bar, sound, DPI changes or a real
provider.

When the maintainer says "I'm ready to test", a Claude session reads these
files and walks through them one group at a time. Each check should take a
minute or two and be judged by eye or ear.

## One file per build

Every code pull request adds one file here, named `vX.Y.Z-<topic>.md`, for
the checks it leaves unrun. Docs-only pull requests add nothing.

Each file starts like this:

```
# vX.Y.Z: topic (PR #N)

**Status**: NOT RUN
**Needs**: what must be at hand
**Time**: rough minutes
```

Then come groups (`## Group name`) of numbered checks. Numbers run through the
whole file. Each check has four lines:

- **Do**: the exact steps, in order.
- **Expect**: what you should see or hear.
- **If it fails**: what to note down.
- **Result**: empty until the check is run.

## Order to test in

One exe carries everything, so test the newest build first and work back.

1. Start with `v1.3.3-terminal-escapes.md`, then `v1.3.2-core-quick-wins.md`.
2. Within a file, the checks that can lose work or data come first (a
   recording or a conversation that ends and drops words, a notice that hides
   behind another window). Cosmetic checks come later.
3. Voice goes last. It needs a microphone, headphones and provider keys, so
   it takes the longest to set up.
4. `older-backlog.md` and `harness.md` come after everything else.

## Getting the build

```
cd C:\tom_lapster_local\nutshell
Invoke-WebRequest https://github.com/umbongo/nutshell/raw/main/build/win/nutshell.exe -OutFile .\nutshell.exe
.\nutshell.exe -v
```

Check that `-v` prints the version the file under test names (or a newer one).
An old copy in OneDrive or elsewhere can be stale.

## Recording results

The session fills in each check's `Result:` line with `pass` or `fail`, what
was seen, and the date (for example `pass, 2026-12-01` or `fail, 2026-12-01:
the line was missing`). It changes `**Status**:` to `PARTIAL` while some
checks are open and `PASSED` when all are done. A file whose checks all passed
is moved to `docs/test-log/done/`. A failed check stays in its file until the
fix has been built and the check passes again.

Nothing here asks you to change the configuration of a router, switch or
firewall. Terminal checks use a Linux host or the local shell.

## Files

- `v1.3.3-terminal-escapes.md`
- `v1.3.2-core-quick-wins.md`
- `v1.3.1-voice-ai-speech.md`
- `v1.2.99-voice-conversation.md`
- `v1.2.95-voice-live-dictation.md`
- `v1.2.92-94-voice-input.md`
- `v1.2.90-91-ai-switch-and-notices.md`
- `v1.2.89-checkboxes.md`
- `older-backlog.md`
- `harness.md`
