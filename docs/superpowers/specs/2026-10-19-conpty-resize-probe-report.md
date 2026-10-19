# ConPTY resize probe: what conhost does on a resize

**Date**: 2026-10-19 (template). The Windows run that fills it: _date, Windows build from `winver`_.
**Status**: DRAFT until the maintainer has filled every section from one Windows run.
**Spec**: `2026-10-18-conpty-resize-anchor-design.md`, section 0 ("Probe first") and 1.8.
**Produced by**: `test_local_pty_resize_parity` in `tests/test_local_pty.c`, run by
`mingw32-make wintest` on Windows (MSYS2 MINGW64). The case runs under Wine on Linux as well,
but Wine's `ResizePseudoConsole` fails, so a Wine run says nothing about conhost and is not this
report.

## How to read the case's output

Each scenario runs twice, `flags0` (what `local_pty_open()` passes today) and `quirk`
(`PSEUDOCONSOLE_RESIZE_QUIRK`, 0x2). Per run the case prints:

- `dump N: conhost buffer=BWxBH window=WWxWH (rect ...) cursor=...`: printed once per dump
  the case reads (dump 0 before the resize, dump 1 and up after it; a retry prints another).
  `buffer=` is the screen buffer's size, `window=` the visible rectangle's, and the later
  comparison lines use the window only. `buffer` equal to `window` means conhost's buffer is
  the viewport.
- `before the resize: conhost vs scenario layout`: conhost's buffer once the probe child has
  written the scenario. It should say AGREES. If it does not, the scenario itself did not
  render as `tests/conpty_expect.c` assumes, and the run's later lines cannot be read against
  the rule.
- `after the resize: conhost vs spec section 1.3 (top-anchored)`: conhost's buffer and cursor
  after `ResizePseudoConsole`, against the rule computed from the scenario text. AGREES means
  section 1 describes this conhost for that scenario.
- `after the resize: conhost vs today's model before its repaint`: the same buffer against
  Nutshell's bottom-anchored model, snapshotted before any post-resize byte reached it.
- `VERDICT rule=... model=...`: the two comparisons in one line.
- `post-resize bytes`: everything ConPTY sent after the resize, with ESC, `\r` and `\n` shown.
- `after the repaint: conhost vs the model`: whether the repaint brought the model into line.

## Results

| Scenario | Flags | Rule verdict | Model verdict | Post-resize bytes | Starts with a full repaint from `ESC[H`? |
|---|---|---|---|---|---|
| grow (40x12 to 40x20) | flags0 | | | | |
| grow | quirk | | | | |
| shrink_bottom (40x12 to 40x8) | flags0 | | | | |
| shrink_bottom | quirk | | | | |
| shrink_below (40x12 to 40x8) | flags0 | | | | |
| shrink_below | quirk | | | | |
| width (40x12 to 25x12) | flags0 | | | | |
| width | quirk | | | | |

## Answers to the spec's "Open for the maintainer"

1. **conhost's resize behaviour** (grow keeps the top line; a shrink drops from the top only as
   far as the cursor needs; a width change rewraps; the buffer is the viewport). Answered by the
   rule verdicts for grow, shrink_bottom and width, and by the `dump N: conhost buffer=... window=...`
   output lines (a buffer taller than its window would mean the buffer is not the viewport). Answer:
2. **Content below the cursor on a shrink.** Answered by shrink_below. AGREES means conhost
   scrolls out from the top and section 1.3 step 5 stands. If conhost instead shows rows
   `<01>` to `<06>`, `P>` and one blank row with the cursor on row 6, it dropped from the bottom
   and section 1.8's first change applies. Answer:
3. **Soft wraps in ConPTY's repaint.** Answered by the width run's post-resize bytes: is a
   rewrapped row (for example `<W3> abcdefghijklmnopqrst` followed by `uvwxyz012`) sent as two
   rows each ending `ESC[K\r\n`, or as one run of text with no `\r\n` between? Answer:
4. **Trailing spaces.** Not answerable by this probe: `ReadConsoleOutputCharacterW` returns a
   space for a written space and for an empty cell alike. Stays open.
5. **`PSEUDOCONSOLE_RESIZE_QUIRK`.** Compare each scenario's `flags0` and `quirk` rows. The
   maintainer's rule (2026-10-19): the product adopts the flag if, with it, the post-resize
   bytes no longer start with a full repaint from `ESC[H` **and** conhost's cursor after the
   resize still equals the rule's cursor. Answer:

## Decisions

- Section 1 as written, or amended per 1.8 (say which change):
- `PSEUDOCONSOLE_RESIZE_QUIRK` adopted for every local session (yes or no, and why):

## Fixtures recorded

`NSPROBE_RECORD=1` writes these from the `flags0` runs (`tests/fixtures/*.bin` is `binary` in
`.gitattributes`, so git stores the bytes unchanged):

| File | Bytes |
|---|---|
| `tests/fixtures/conpty_resize_grow.bin` | |
| `tests/fixtures/conpty_resize_shrink_bottom.bin` | |
| `tests/fixtures/conpty_resize_shrink_below.bin` | |
| `tests/fixtures/conpty_resize_width.bin` | |

## Appendix: the case's output

Paste everything from `[RUN ] test_local_pty_resize_parity` to its `[PASS]` or `[FAIL]` line.
