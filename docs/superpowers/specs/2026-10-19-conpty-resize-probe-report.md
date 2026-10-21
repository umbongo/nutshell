# ConPTY resize probe: what conhost does on a resize

**Date**: 2026-10-19 (template); filled from the Windows run of 2026-10-21 11:25 +11:00 on the maintainer's box, Windows 11 26H2, build 26300.9711 (MSYS2 MINGW64, `NSPROBE_RECORD=1 mingw32-make wintest`, 18 tests, 0 failed; pty child 10/0).
**Status**: FILLED, 2026-10-21. One run; every scenario printed a `VERDICT` line and every fixture was written.
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
- `VERDICT rule=... model=...`: the two comparisons in one line. `rule=N/A` means the rule could
  not be computed for the scenario. No `VERDICT` line at all means conhost's dump never showed
  the new size (the case prints that instead, and fails on Windows).
- `AGREES` includes the cursor: `cpx_view_diff` counts it. The `cursor want r,c got r,c` line
  shows the cursor on its own.
- `post-resize bytes`: everything ConPTY sent after the resize, with ESC, `\r` and `\n` shown.
- `after the repaint: conhost vs the model`: whether the repaint brought the model into line.
- `fixture tests/fixtures/conpty_resize_<scenario>.bin: written` or `NOT written (a check
  failed)`: printed for the `flags0` runs when `NSPROBE_RECORD=1`; it is the evidence for
  "Fixtures recorded" below.

## Results

| Scenario | Flags | Rule verdict | Model verdict | Post-resize bytes | Starts with a full repaint from `ESC[H`? |
|---|---|---|---|---|---|
| grow (40x12 to 40x20) | flags0 | AGREES | DISAGREES (21) | 242 | yes |
| grow | quirk | AGREES | DISAGREES (21) | 242 | yes |
| shrink_bottom (40x12 to 40x8) | flags0 | AGREES | AGREES | 138 | yes |
| shrink_bottom | quirk | AGREES | AGREES | 138 | yes |
| shrink_below (40x12 to 40x8) | flags0 | AGREES | AGREES | 109 | yes |
| shrink_below | quirk | AGREES | AGREES | 109 | yes |
| width (40x12 to 25x12) | flags0 | AGREES | AGREES | 247 | yes |
| width | quirk | AGREES | AGREES | 247 | yes |

The `quirk` run was byte-identical to the `flags0` run in every scenario: the same conhost
buffer, the same cursor, the same post-resize bytes. Every dump printed `buffer=` equal to
`window=` (`40x12`, `40x20`, `40x8`, `25x12`), so conhost's buffer is the viewport. After the
repaint, today's model agreed with conhost in all eight runs, which is why the bottom-anchored
model only shows its grow disagreement between the resize and the repaint.

The shape of every repaint is the same: `ESC[?25l ESC[8;ROWS;COLSt ESC[H`, then one run per
logical line ending `ESC[K\r\n` (a blank row is a bare `ESC[K\r\n`), the last row without the
`\r\n`, then the cursor placed with an absolute `ESC[r;cH` (or a relative `ESC[1C` when the
repaint already ended on the cursor's row), then `ESC[?25h`.

## Answers to the spec's "Open for the maintainer"

1. **conhost's resize behaviour** (grow keeps the top line; a shrink drops from the top only as
   far as the cursor needs; a width change rewraps; the buffer is the viewport). Answered by the
   rule verdicts for grow, shrink_bottom and width, and by the `dump N: conhost buffer=... window=...`
   output lines (a buffer taller than its window would mean the buffer is not the viewport). Answer:
   **confirmed on build 26300.9711.** Grow kept `<10> alpha` on row 0 and left rows 12-19 blank
   with the cursor still at 11,3. shrink_bottom dropped `<10>` to `<13>` from the top and put the
   cursor at 7,3. width rewrapped by logical line and scrolled `<W1>` and `<W2>` out the top.
   Every dump had `buffer` equal to `window`. Section 1.8 is not triggered.
2. **Content below the cursor on a shrink.** Answered by shrink_below. AGREES means conhost
   scrolls out from the top and section 1.3 step 5 stands. If conhost instead shows rows
   `<01>` to `<06>`, `P>` and one blank row with the cursor on row 6, it dropped from the bottom
   and section 1.8's first change applies. Answer: **AGREES; conhost scrolled out from the top.**
   After the resize it showed `<04>`, `<05>`, `<06>`, `P>`, three blank rows and `<B> below` on
   row 7, cursor 3,3. The content below the cursor was kept; the three rows that went were
   `<01>` to `<03>` at the top. Section 1.3 step 5 stands.
3. **Soft wraps in ConPTY's repaint.** Answered by the width run's post-resize bytes: is a
   rewrapped row (for example `<W3> abcdefghijklmnopqrst` followed by `uvwxyz012`) sent as two
   rows each ending `ESC[K\r\n`, or as one run of text with no `\r\n` between? Answer: **one run,
   no `\r\n` at the wrap.** The 25-column repaint sent `<W3> abcdefghijklmnopqrstuvwxyz012ESC[K\r\n`
   as 33 characters in one run and the 46-character `<WL>` line likewise; twelve rows arrived as
   six `\r\n`-terminated runs. The terminal wraps them itself, and the result agreed with conhost's
   buffer cell for cell.
4. **Trailing spaces.** Not answerable by this probe: `ReadConsoleOutputCharacterW` returns a
   space for a written space and for an empty cell alike. Stays open.
5. **`PSEUDOCONSOLE_RESIZE_QUIRK`.** Compare each scenario's `flags0` and `quirk` rows. The
   maintainer's rule (2026-10-19): the product adopts the flag if, with it, the post-resize
   bytes no longer start with a full repaint from `ESC[H` **and** conhost's cursor after the
   resize still equals the rule's cursor. Answer: **no effect.** With the flag every scenario still
   repainted in full from `ESC[H`, with the same byte count and the same bytes as without it. The
   first half of the rule fails, so the flag is not adopted.

Also recorded by this run, from the other pty cases: the opening sequence on this build is
`ESC[?9001h ESC[?1004h ESC[?25l ESC[2J ESC[m ESC[2;1H ESC]0;...\a ESC[?25h` (cmd.exe), with no
`ESC[3J` and no DSR or DA query; the edit.exe run saw `?1049h`/`?1049l` and no `?1h`.

## Decisions

- Section 1 as written, or amended per 1.8 (say which change): **as written.** All four rule
  verdicts agree, and 1.8's two triggers (drop from the bottom, no rewrap on a width change) did
  not occur.
- `PSEUDOCONSOLE_RESIZE_QUIRK` adopted for every local session (yes or no, and why): **no.** It
  changed nothing in the post-resize bytes, the buffer or the cursor on this build, so there is
  nothing to gain and a flag with no measured effect is not worth carrying. `local_pty_open()`
  keeps passing 0. Section 3's two residuals (duplicated rows from output during a resize; a
  sub-100 ms shrink-and-back) therefore stay as accepted on 2026-10-19.

## Fixtures recorded

`NSPROBE_RECORD=1` writes these from the `flags0` runs (`tests/fixtures/*.bin` is `binary` in
`.gitattributes`, so git stores the bytes unchanged):

| File | Bytes |
|---|---|
| `tests/fixtures/conpty_resize_grow.bin` | 606 |
| `tests/fixtures/conpty_resize_shrink_bottom.bin` | 501 |
| `tests/fixtures/conpty_resize_shrink_below.bin` | 335 |
| `tests/fixtures/conpty_resize_width.bin` | 638 |

Committed on branch `conpty/probe-report` (89c68d4) from the Windows box. Each starts with the
opening `ESC[?9001h ESC[?1004h ... ESC[H` and carries the in-band `ESC]9999;resize=COLSxROWS\a`
marker at the point of the `ResizePseudoConsole` call.

## Appendix: the case's output

The run's `[RUN ] test_local_pty_resize_parity` to `[PASS]` block, with the per-row
`want |...| got |...|` listings of the AGREES comparisons (0 differences each) omitted; the one
DISAGREES listing is kept in full. The byte sequences are verbatim.

```
[RUN ] test_local_pty_resize_parity
  [grow/flags0] dump 0: conhost buffer=40x12 window=40x12 (rect left=0 top=0 right=39 bottom=11) cursor=11,3
  [grow/flags0] before the resize: conhost vs scenario layout: AGREES (0 differences)
  [grow/flags0] dump 1: conhost buffer=40x20 window=40x20 (rect left=0 top=0 right=39 bottom=19) cursor=11,3
  [grow/flags0] after the resize: conhost vs spec section 1.3 (top-anchored): AGREES (0 differences)
  [grow/flags0] after the resize: conhost vs today's model before its repaint: DISAGREES (21 differences)
    *  0 want |<02> alpha| got |<10> alpha|
    *  1 want |<03> alpha| got |<11> alpha|
    *  2 want |<04> alpha| got |<12> alpha|
    *  3 want |<05> alpha| got |<13> alpha|
    *  4 want |<06> alpha| got |<14> alpha|
    *  5 want |<07> alpha| got |<15> alpha|
    *  6 want |<08> alpha| got |<16> alpha|
    *  7 want |<09> alpha| got |<17> alpha|
    *  8 want |<10> alpha| got |<18> alpha|
    *  9 want |<11> alpha| got |<19> alpha|
    * 10 want |<12> alpha| got |<20> alpha|
    * 11 want |<13> alpha| got |P>|
    * 12 want |<14> alpha| got ||
    * 13 want |<15> alpha| got ||
    * 14 want |<16> alpha| got ||
    * 15 want |<17> alpha| got ||
    * 16 want |<18> alpha| got ||
    * 17 want |<19> alpha| got ||
    * 18 want |<20> alpha| got ||
    * 19 want |P>| got ||
    * cursor want 19,3 got 11,3; size want 40x20 got 40x20
  [grow/flags0] VERDICT rule=AGREES model=DISAGREES
  [grow/flags0 post-resize bytes] 242 bytes: ESC[?25lESC[8;20;40tESC[H<10> alphaESC[K\r\n<11> alphaESC[K\r\n<12> alphaESC[K\r\n<13> alphaESC[K\r\n<14> alphaESC[K\r\n<15> alphaESC[K\r\n<16> alphaESC[K\r\n<17> alphaESC[K\r\n<18> alphaESC[K\r\n<19> alphaESC[K\r\n<20> alphaESC[K\r\nP>ESC[K\r\nESC[K\r\nESC[K\r\nESC[K\r\nESC[K\r\nESC[K\r\nESC[K\r\nESC[K\r\nESC[KESC[12;4HESC[?25h
  [grow/flags0] after the repaint: conhost vs the model: AGREES (0 differences)
  [grow/flags0] fixture tests/fixtures/conpty_resize_grow.bin: written
  [grow/quirk] dump 0: conhost buffer=40x12 window=40x12 (rect left=0 top=0 right=39 bottom=11) cursor=11,3
  [grow/quirk] before the resize: conhost vs scenario layout: AGREES (0 differences)
  [grow/quirk] dump 1: conhost buffer=40x20 window=40x20 (rect left=0 top=0 right=39 bottom=19) cursor=11,3
  [grow/quirk] after the resize: conhost vs spec section 1.3 (top-anchored): AGREES (0 differences)
  [grow/quirk] after the resize: conhost vs today's model before its repaint: DISAGREES (21 differences)
    (the same 21 lines as grow/flags0)
  [grow/quirk] VERDICT rule=AGREES model=DISAGREES
  [grow/quirk post-resize bytes] 242 bytes: (identical to grow/flags0)
  [grow/quirk] after the repaint: conhost vs the model: AGREES (0 differences)
  [shrink_bottom/flags0] dump 0: conhost buffer=40x12 window=40x12 (rect left=0 top=0 right=39 bottom=11) cursor=11,3
  [shrink_bottom/flags0] before the resize: conhost vs scenario layout: AGREES (0 differences)
  [shrink_bottom/flags0] dump 1: conhost buffer=40x8 window=40x8 (rect left=0 top=0 right=39 bottom=7) cursor=7,3
  [shrink_bottom/flags0] after the resize: conhost vs spec section 1.3 (top-anchored): AGREES (0 differences)
       (rows: <14> alpha .. <20> alpha, P>; cursor 7,3)
  [shrink_bottom/flags0] after the resize: conhost vs today's model before its repaint: AGREES (0 differences)
  [shrink_bottom/flags0] VERDICT rule=AGREES model=AGREES
  [shrink_bottom/flags0 post-resize bytes] 138 bytes: ESC[?25lESC[8;8;40tESC[H<14> alphaESC[K\r\n<15> alphaESC[K\r\n<16> alphaESC[K\r\n<17> alphaESC[K\r\n<18> alphaESC[K\r\n<19> alphaESC[K\r\n<20> alphaESC[K\r\nP>ESC[KESC[1CESC[?25h
  [shrink_bottom/flags0] after the repaint: conhost vs the model: AGREES (0 differences)
  [shrink_bottom/flags0] fixture tests/fixtures/conpty_resize_shrink_bottom.bin: written
  [shrink_bottom/quirk] (identical to shrink_bottom/flags0: dumps, verdicts and 138 bytes)
  [shrink_bottom/quirk] VERDICT rule=AGREES model=AGREES
  [shrink_below/flags0] dump 0: conhost buffer=40x12 window=40x12 (rect left=0 top=0 right=39 bottom=11) cursor=6,3
  [shrink_below/flags0] before the resize: conhost vs scenario layout: AGREES (0 differences)
       (rows: <01> alpha .. <06> alpha, P>, blank, blank, blank, <B> below, blank)
  [shrink_below/flags0] dump 1: conhost buffer=40x8 window=40x8 (rect left=0 top=0 right=39 bottom=7) cursor=3,3
  [shrink_below/flags0] after the resize: conhost vs spec section 1.3 (top-anchored): AGREES (0 differences)
       (rows: <04> alpha, <05> alpha, <06> alpha, P>, blank, blank, blank, <B> below; cursor 3,3)
  [shrink_below/flags0] after the resize: conhost vs today's model before its repaint: AGREES (0 differences)
  [shrink_below/flags0] VERDICT rule=AGREES model=AGREES
  [shrink_below/flags0 post-resize bytes] 109 bytes: ESC[?25lESC[8;8;40tESC[H<04> alphaESC[K\r\n<05> alphaESC[K\r\n<06> alphaESC[K\r\nP>ESC[K\r\nESC[K\r\nESC[K\r\nESC[K\r\n<B> belowESC[KESC[4;4HESC[?25h
  [shrink_below/flags0] after the repaint: conhost vs the model: AGREES (0 differences)
  [shrink_below/flags0] fixture tests/fixtures/conpty_resize_shrink_below.bin: written
  [shrink_below/quirk] (identical to shrink_below/flags0: dumps, verdicts and 109 bytes)
  [shrink_below/quirk] VERDICT rule=AGREES model=AGREES
  [width/flags0] dump 0: conhost buffer=40x12 window=40x12 (rect left=0 top=0 right=39 bottom=11) cursor=8,3
  [width/flags0] before the resize: conhost vs scenario layout: AGREES (0 differences)
       (rows: <W1>, <W2>, <W3> abcdefghijklmnopqrstuvwxyz012; <WL> abcdefghijklmnopqrstuvwxyzABCDEFGHI / JKLMNOPQRSTU; <W4>, <W5>, <W6>; P>; blank x3)
  [width/flags0] dump 1: conhost buffer=25x12 window=25x12 (rect left=0 top=0 right=24 bottom=11) cursor=11,3
  [width/flags0] after the resize: conhost vs spec section 1.3 (top-anchored): AGREES (0 differences)
       0 |<W3> abcdefghijklmnopqrst|  1 |uvwxyz012|  2 |<WL> abcdefghijklmnopqrst|  3 |uvwxyzABCDEFGHIJKLMNOPQRS|  4 |TU|
       5 |<W4> abcdefghijklmnopqrst|  6 |uvwxyz012|  7 |<W5> abcdefghijklmnopqrst|  8 |uvwxyz012|  9 |<W6> abcdefghijklmnopqrst|
      10 |uvwxyz012| 11 |P>|; cursor 11,3
  [width/flags0] after the resize: conhost vs today's model before its repaint: AGREES (0 differences)
  [width/flags0] VERDICT rule=AGREES model=AGREES
  [width/flags0 post-resize bytes] 247 bytes: ESC[?25lESC[8;12;25tESC[H<W3> abcdefghijklmnopqrstuvwxyz012ESC[K\r\n<WL> abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUESC[K\r\n<W4> abcdefghijklmnopqrstuvwxyz012ESC[K\r\n<W5> abcdefghijklmnopqrstuvwxyz012ESC[K\r\n<W6> abcdefghijklmnopqrstuvwxyz012ESC[K\r\nP>ESC[KESC[1CESC[?25h
  [width/flags0] after the repaint: conhost vs the model: AGREES (0 differences)
  [width/flags0] fixture tests/fixtures/conpty_resize_width.bin: written
  [width/quirk] (identical to width/flags0: dumps, verdicts and 247 bytes)
  [width/quirk] VERDICT rule=AGREES model=AGREES
[PASS] test_local_pty_resize_parity

[pty child] Tests Run: 10, Failed: 0
Tests Run: 18, Failed: 0
```
