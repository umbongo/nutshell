# Thinking row: Extended mode: design addendum

Date: 2026-11-21. Status: Implemented (v1.2.86). Amends
`2026-11-12-thinking-row-modes-design.md`. Decided by the maintainer; this
records the decisions.

## Decisions

- **A fourth mode, Extended.** It behaves exactly like compact in every
  respect: the last N wrapped lines, followed as the text streams
  (`think_compact_offset`), trailing whitespace trimmed, no scrollbar, the
  wheel passes to the thread, and the label colour is the existing
  `thinking_compact` token (no new theme token). N is 25
  (`THINK_EXTENDED_LINES`) instead of 4. Text shorter than 25 lines takes its
  own height.
- **One predicate for the shared behaviour.** `think_mode_follows_tail(mode)`
  is true for compact and extended; every place that tested
  `mode == THINK_MODE_COMPACT` for that behaviour uses it.
- **Automatic streams in Extended.** `THINK_PREF_AUTO` (still the default)
  shows Extended while the reply streams and collapsed once it is complete.
- **Click cycle:** collapsed, compact, extended, open, collapsed. Under
  Automatic a click on a streaming row therefore goes Extended to Open.
- **Appended values.** `THINK_MODE_EXTENDED` = 3 after OPEN, `THINK_PREF_EXTENDED`
  = 4 after OPEN; the ints are never persisted, only the config string
  `"extended"`, so appending is safe. Unknown strings still parse as
  Automatic. `THINK_PREF_MAX` replaces the hardcoded `THINK_PREF_OPEN` upper
  bounds (listview, config validation, Settings, window).
- **Settings:** the "Thinking display" combo (AI Assistant > Behaviour) gains
  "Always extended" as item 4; the index equals the pref. The help text
  (`settings_help.c`) and README say Automatic shows the last 25 lines while
  streaming. The default label stays "Automatic".
- **Downgrade.** An older build reads `"extended"` as Automatic and writes
  `"auto"` on its next save, so the choice is lost after a downgrade.
- **Reader's place.** A layout that re-derives a row's mode (Automatic: the
  reply completes, Extended to collapsed) holds the first visible item in
  place when the view is not stuck to the bottom, as a fold does.
- **Demo:** the existing states show collapsed, compact and open, so a new
  `--ui-demo=thinking-extended` state (the long reasoning, pref Extended) sits
  between `thinking-compact` and `thinking-open`; the gallery grows to 20
  states.

## Tests

Native: the cycle, `effective` for every pref and streaming state, the
`name`/`parse` table, `body_cap`, label colour, the predicate and out-of-range
values (`test_thinking_mode.c`); load, round trip and validate for
`"extended"` (`test_config.c`); the help label mapping; the demo state.
Windows (`wintest`): row heights per pref, the 25-line box with its offset,
Automatic while streaming, the four-step click cycle.

Open: a Windows hand check of the live behaviour (notes document).


## Addendum 2026-11-28

Two things changed after review of this design:

- **A colour per mode.** Each mode's "Thinking" label has its own colour,
  chosen to be told apart under normal, protan and deutan vision (CIE76 of at
  least 15 for every pair, Machado 2009 simulation). The dark themes draw
  pastel text (collapsed is a warm sand); the light themes draw `text_main` on
  a pastel pill, the `thinking_*_pill` tokens of `ThemeChatColors`, painted
  inside the label's rectangle grown by a pixel or two. Contrast no longer
  has to rise with the mode.
- **The wheel and the reader's place.** A compact or extended box takes the
  mouse wheel only while its reasoning streams, its text overflows the box and
  the thread was not wheel-scrolled in the last 300 ms
  (`think_wheel_takes()`); otherwise the wheel scrolls the thread. Inside
  the box the chat list's stick-to-bottom rule holds the reader's place, and a
  mode change keeps it, except that a row going to or from collapsed follows
  the tail. The open box keeps its earlier behaviour.
