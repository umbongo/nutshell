# Voice improvements (v1.2.94) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** After the maintainer's first Windows test of voice input (v1.2.93): review by default
(already committed as be421b1), a software microphone gain slider, a level meter with an optimal
zone, a Test that stays on until stopped, panel status moved into the header (model chip
removed), and a Language dropdown defaulting to English (UK).

**Architecture:** Every decision that can be pure goes in `src/core` with native tests: gain
maths and dB labels (`wav.c`), meter ballistics and level classification (`voice_level.c`),
the Test toggle's state machine (`voice_test.c`), the language table and request fields
(`voice_lang.c`, `voice_stt.c`), the header status choice and layout (`ai_header_layout.c`).
`src/ui` only wires them: the audio thread applies the gain, Settings draws a themed slider
(`ns_slider.c`) and the zoned meter, the AI panel paints status in its header.

**Tech Stack:** C11, Win32 (MinGW cross-compile), WASAPI on the audio thread, custom
`test_framework.h`, Wine `wintest`.

**Spec:** `docs/superpowers/specs/2026-11-23-voice-input-output-design.md` (voice). Header and
strip: `docs/superpowers/specs/2026-09-07-ai-assist-panel-design.md` and the 2026-10-18 spec's
§3 (the busy label). Every spec change made here is tagged **(maintainer, 2026-11-24)**.

## Global Constraints

- Worktree `/home/thomas/nutshell-wt/voice-review`, branch `feat/voice-improvements`. Never
  touch `~/nutshell` or another worktree.
- Version stays **1.2.94** (`APP_VERSION "1.2.94"`, `APP_VERSION_BINARY 1,2,94,0`, README
  `**Version**: v1.2.94`); the bump and the rebuilt exe are committed only in the last task.
  No task commits `build/win/nutshell.exe`, `README.md`'s version line or `resource.h`'s version.
- Test first. A test that tells two states apart needs a fixture in which they differ.
- Line endings preserved: `src/config/loader.c`, `Makefile`, `src/ui/custom_scrollbar.h`,
  `src/ui/themed_button.h` are CRLF; `src/ui/ai_chat.h` is CRLF except lines 106-111. Before
  each commit compare `git diff --stat` with `git diff --stat --ignore-cr-at-eol`.
- Design-system rules (CLAUDE.md): no `RGB(` literal in `src/ui` outside `ns_draw.c`; colours
  from `ns_tokens()` (intents `success`/`warning`/`danger`, `rgb_alpha()` for tints); sizes via
  `ns_scale(px, dpi)` and the `SP_*`/`SZ_*`/`R_*`/`STROKE_*` grid; fonts via `ns_font(role, dpi)`,
  never `CreateFont`; animation via `ns_motion`; hover via `ns_hover`. No new `MessageBox`.
- Optional-feature rule: nothing added here runs, opens a device or makes a request while voice
  is off. Test never uploads anything.
- Both builds `-Werror`; MinGW adds `-Wpedantic -Wshadow -Wformat=2 -Wconversion`.
- Commits: stage by path, read every `??` line, imperative subject, end with
  `Models: claude-opus-5-5 (orchestration, review); claude-sonnet-5-5 (implementation, task reviews)`
  then `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` and
  `Claude-Session: https://claude.ai/code/session_01AHuBtDU8oXSVxrKcPYYtQG`.
- Focused tests while iterating: `make -j test 2>&1 | tail -5` (native, stub libssh2);
  `xvfb-run -a make wintest` for Win32 harness changes (check `pgrep -a wine` before any
  `wineserver -k`).

## Review Focus

1. Gain at +30 dB on a loud signal saturates at ±32767 and never wraps (−32768 input × gain
   included) — Task 1 tests it.
2. A Test left on for minutes must not grow memory or hit the 60 s capture cap — Task 4 makes a
   test capture keep no samples, with a wintest seam asserting the buffer stays empty.
3. A config saved by an older build with `voice_stt_language` "fr", "en" or "pt-BR"
   (deliberately chosen) must keep it — "fr" selected in the list, the others shown as
   "Other…" with the raw value — and save it back unchanged when the row is not touched —
   Task 3 tests the load; Task 5 keeps the untouched raw value on Save.
4. The header with a 250-character error at 100 % and 200 % DPI never overlaps the buttons and
   the title keeps a minimum width — Task 6 sweep test.
5. Moving the device combo, the page, or the voice tick while testing leaves no capture running
   — Task 2 state-machine tests cover every trigger; Task 5 wires each.

---

### Task 0: Verify the committed review-by-default change (be421b1)

**Files:** whatever be421b1 touched (`git show --stat be421b1`), tests to add as needed.

Item 0 as the maintainer asked it: a transcript is written into the AI input box for review,
never sent, by default; sending automatically is an opt-in tick box `voice_auto_send` (default
0, replacing `voice_review`; every existing config gets auto-send off; the upgrade notice lists
it in plain words). Draft present → append after one space; caret at end; focus to the input.
Auto-send on = the old behaviour, approvals unchanged.

- [ ] **Step 1:** Read `git show be421b1` against each sentence above and list any gap (e.g. an
  existing config holding `voice_review: 1` must still load with auto-send off; the notice line
  must name the setting in plain words; the append path must not add a second space when the
  draft already ends in whitespace; a native test for the fill/append decision).
- [ ] **Step 2:** For each gap: failing test, fix, passing test. If none, add nothing.
- [ ] **Step 3:** `make -j test 2>&1 | tail -3` — 0 failed.
- [ ] **Step 4:** Commit only if something changed: `fix(voice): <gap>`.

### Task 1: Software microphone gain — maths, setting, help

**Files:**
- Modify: `src/core/wav.c`, `src/core/wav.h`, `src/core/voice_config.c`, `src/core/voice_config.h`,
  `src/config/config.h`, `src/config/loader.c` (CRLF), `src/core/settings_help.c`,
  `src/core/settings_help.h`, `src/core/notice.c` (only if the default text does not read well)
- Test: `tests/test_wav.c`, `tests/test_voice_config.c`, `tests/test_settings_help.c`,
  `tests/test_config.c`, `tests/runner.c`

**Interfaces — Produces:**
- `voice_config.h`: `#define VOICE_GAIN_MIN_DB (-12)`, `#define VOICE_GAIN_MAX_DB 30`,
  `#define VOICE_GAIN_DEFAULT_DB 0`; `int voice_gain_clamp(int db);`
- `wav.h`: `uint32_t pcm_gain_q16(int db);` (clamps db to the range, returns
  `lround(65536.0 * pow(10.0, db / 20.0))`; 0 dB → exactly 65536) and
  `void pcm_apply_gain_q16(int16_t *s, size_t n, uint32_t q16);` (in place, 64-bit product,
  round to nearest, saturate to [−32768, 32767]; q16 == 65536 returns without touching samples;
  NULL or n == 0 is a no-op).
- `voice_config.h`: `void voice_gain_label(int db, char *out, size_t cap);` → `"+12 dB"`,
  `"0 dB"`, `"-12 dB"` (ASCII hyphen-minus; positive values carry `+`).
- `Settings.voice_mic_gain_db` (int), JSON key `voice_mic_gain_db`, default 0, clamped in
  `voice_settings_validate`, placed in `k_setting_keys` and the save order right after
  `voice_input_device_name` (before the marker). Help entry `SH_VOICE_MIC_GAIN`, kind slider/int
  (use the nearest existing kind), label `"Microphone gain:"`, title "Microphone gain", text:
  "Boosts or lowers the microphone in Nutshell only, before the recording is sent. Other apps
  are not affected. Watch the meter while you press Test and aim for Good.", default text
  `"0 dB"`, min −12, max 30, placed after `SH_VOICE_DEVICE`/`SH_VOICE_TEST` in page order.

- [ ] **Step 1: failing tests** in `tests/test_wav.c`:
  `test_pcm_gain_q16_values` (`pcm_gain_q16(0) == 65536`; `pcm_gain_q16(6)` within 1 of
  130762; `pcm_gain_q16(-12)` within 1 of 16422; `pcm_gain_q16(31) == pcm_gain_q16(30)`;
  `pcm_gain_q16(-99) == pcm_gain_q16(-12)`),
  `test_pcm_apply_gain_identity` (0 dB leaves {−32768, −1, 0, 1, 32767} unchanged),
  `test_pcm_apply_gain_saturates` (+30 dB on {20000, −20000, −32768, 32767, 1000} gives
  {32767, −32768, −32768, 32767, 31623±1}),
  `test_pcm_apply_gain_negative` (−6 dB on {1000, −1000} gives {501±1, −501±1}),
  `test_pcm_apply_gain_null_is_noop`.
  In `tests/test_voice_config.c`: `test_voice_gain_default_and_clamp` (default 0; validate
  clamps 99 → 30, −50 → −12), `test_voice_gain_roundtrip` (save 12, load 12),
  `test_voice_gain_label` ("+12 dB", "0 dB", "-12 dB", "+30 dB").
  Update `test_settings_help_voice_entries` for the new count (13 → 14) and label, and
  `test_config_keys_match_saved_file` for the new key's position. Register all in `tests/runner.c`.
- [ ] **Step 2:** `make -j test 2>&1 | tail -5` — the new tests fail (link or assert).
- [ ] **Step 3:** Implement the interfaces above; loader load/save (CRLF preserved).
- [ ] **Step 4:** `make -j test 2>&1 | tail -3` — 0 failed. An existing config with voice
  enabled now lists "Microphone gain: 0 dB" in the upgrade notice; confirm the notice test
  for missing voice keys still passes and reads in plain words.
- [ ] **Step 5:** Update the spec §4.4 (gain stage: software, saturating, applied before the
  silence gate, WAV encode and meter; not the Windows endpoint volume, which would change every
  app) and §7 (the slider row), tagged **(maintainer, 2026-11-24)**. Commit
  `feat(voice): software microphone gain setting and saturating gain maths`.

### Task 2: Level meter maths and the Test toggle state machine

**Files:**
- Create: `src/core/voice_level.c`, `src/core/voice_level.h`, `src/core/voice_test.c`,
  `src/core/voice_test.h`
- Test: `tests/test_voice_level.c`, `tests/test_voice_test.c`, `tests/runner.c`

**Interfaces — Produces (`voice_level.h`):**
- `double voice_dbfs(int peak);` — `peak` 0..32768 → `20*log10(peak/32768.0)`, floor
  `VOICE_METER_FLOOR_DB` (−60.0) for 0 and anything below the floor.
- Zone constants (dBFS of the peak): `VOICE_LEVEL_SILENT_DB -45.0` (below: nobody is speaking;
  the silence gate's speech threshold is RMS 250 ≈ −42 dBFS, so a peak below −45 cannot pass
  it), `VOICE_LEVEL_GOOD_LO_DB -18.0`, `VOICE_LEVEL_GOOD_HI_DB -6.0`,
  `VOICE_LEVEL_LOUD_DB -3.0`.
- `typedef enum { VLEVEL_SILENT, VLEVEL_QUIET, VLEVEL_GOOD, VLEVEL_HOT, VLEVEL_LOUD } VoiceLevelClass;`
  `VoiceLevelClass voice_level_classify(double peak_db);` — `< −45` SILENT, `< −18` QUIET,
  `<= −6` GOOD, `< −3` HOT, else LOUD.
- `const char *voice_level_label(VoiceLevelClass c);` — SILENT "Speak to see the level",
  QUIET "Too quiet — raise the gain", GOOD and HOT "Good", LOUD "Too loud — lower the gain"
  (UTF-8 em dash; Task 5 renders it with a wide-char draw call).
- `typedef struct { double level_db; double hold_db; uint32_t hold_age_ms; double win_db[20]; int win_pos; uint32_t win_acc_ms; } VoiceMeter;`
  `void voice_meter_reset(VoiceMeter *m);`
  `void voice_meter_feed(VoiceMeter *m, int peak, uint32_t elapsed_ms);` — fast attack (a
  higher peak is taken at once), release 20 dB/s, peak-hold tick held 1000 ms then released at
  20 dB/s, and a 1 s window of 20 × 50 ms slots holding each slot's max.
  `double voice_meter_window_peak_db(const VoiceMeter *m);` (max over the window — what the
  label classifies). `double voice_meter_frac(double db);` → (db − floor) / (0 − floor) in [0,1].

**Interfaces — Produces (`voice_test.h`):**
- `typedef enum { VT_EV_PRESS, VT_EV_CLOSE, VT_EV_PAGE_AWAY, VT_EV_DEVICE_CHANGED, VT_EV_VOICE_OFF, VT_EV_CAPTURE_FAILED } VoiceTestEvent;`
- `typedef enum { VT_ACT_NONE, VT_ACT_START, VT_ACT_STOP, VT_ACT_RESTART } VoiceTestAction;`
- `VoiceTestAction voice_test_next(int *running, VoiceTestEvent ev, int can_start);` — while
  stopped: PRESS → START only if `can_start` (device list loaded, voice and AI ticked and
  running), every other event → NONE. While running: PRESS, CLOSE, PAGE_AWAY, VOICE_OFF,
  CAPTURE_FAILED → STOP; DEVICE_CHANGED → RESTART (the maintainer compares microphones; the
  meter follows the new choice). Updates `*running`.
- `const char *voice_test_button_label(int running);` → "Test" / "Stop test".

- [ ] **Step 1: failing tests.** `test_voice_dbfs` (0 → −60; 32768 → 0.0; 16384 → −6.02±0.01;
  328 → −40.0±0.1); `test_voice_level_classify_boundaries` (−45.1 SILENT, −45.0 QUIET, −18.1
  QUIET, −18.0 GOOD, −6.0 GOOD, −5.9 HOT, −3.0 LOUD, 0 LOUD); `test_voice_level_labels`;
  `test_voice_meter_attack_release` (feed 16384 for 50 ms → level −6.02; then 0 for 500 ms →
  level −16.02±0.5); `test_voice_meter_hold` (after a −6 peak and 900 ms of silence the hold is
  still −6.02; after 1500 ms it has fallen by 10±1 dB); `test_voice_meter_window_expires` (a
  −6 peak then 1100 ms of silence → window peak −60); `test_voice_meter_frac`;
  `test_voice_test_transitions` (every event × both states, including PRESS with
  `can_start` 0 → NONE and still stopped; DEVICE_CHANGED while stopped → NONE);
  `test_voice_test_button_label`.
- [ ] **Step 2:** run, see them fail. **Step 3:** implement. **Step 4:** `make -j test` 0 failed.
- [ ] **Step 5:** spec §7 Microphone: the meter shows dBFS of the gained signal with fast
  attack, 20 dB/s release, 1 s peak hold, zones and the label; Test toggles and its stop
  triggers; record the zone figures and the reason for −45 **(maintainer, 2026-11-24)**.
  Commit `feat(voice): level meter ballistics, zones and the Test toggle rules`.

### Task 3: Language list, provider fields and the English (UK) migration

**Files:**
- Create: `src/core/voice_lang.c`, `src/core/voice_lang.h`
- Modify: `src/core/voice_stt.c`, `src/core/voice_stt.h`, `src/core/voice_config.c`,
  `src/config/loader.c` (CRLF), `src/core/settings_help.c`
- Test: `tests/test_voice_lang.c`, `tests/test_voice_stt.c`, `tests/test_voice_config.c`,
  `tests/runner.c`

**Interfaces — Produces (`voice_lang.h`):**
- `typedef struct { const char *id; const char *label; const char *iso; } VoiceLang;`
  `const VoiceLang *voice_lang_table(size_t *n);` in this order: {"", "Auto-detect", ""},
  {"en-GB", "English (UK)", "en"}, {"en-US", "English (US)", "en"}, then Arabic ar, Chinese zh,
  Dutch nl, French fr, German de, Hindi hi, Italian it, Japanese ja, Korean ko, Polish pl,
  Portuguese pt, Russian ru, Spanish es, Swedish sv, Turkish tr, Ukrainian uk (id == iso).
  The dropdown's last entry is "Other…" (not in the table; Settings appends it). Maintainer,
  2026-11-24: "provide the most common languages, but also provide an option for 'other' where
  the user can specify their language if it doesnt exist" and "be sure to tell the user what
  format you need for the 'other' language setting".
- `int voice_lang_find(const char *id);` → table index, or −1 for a value not in the table
  (Settings shows −1 as "Other…" with the text box holding the stored value, kept verbatim).
- `int voice_lang_parse_other(const char *text, char *code, size_t cap);` — for the Other box.
  Trims spaces. Returns 1 and writes the lower-case code when `text` is a two-letter ISO 639-1
  code in a built-in table of all ISO 639-1 codes with their English names (`voice_lang.c`,
  e.g. cy Welsh, mi Maori, ga Irish, gd "Scottish Gaelic"), any case ("CY" → "cy"), or one of
  those English names matched case-insensitively ("welsh" → "cy"). Returns 0 (and writes "")
  for anything else, including "", "xx", "en-GB", "English-ish". A listed language's code
  typed in Other is accepted too ("fr" → "fr").
- `const char *voice_lang_name(const char *code);` → the English name from the ISO table, or
  NULL (used for the Gemini instruction and the recognised hint "Welsh (cy)").
- Format copy, fixed here and used by Task 5 verbatim:
  `#define VOICE_LANG_OTHER_CUE "2-letter code or name, e.g. cy or Welsh"` (the Other box's
  cue banner);
  `int voice_lang_other_hint(const char *text, char *out, size_t cap);` returns the hint tone
  (0 neutral, 1 error) and writes the inline hint: empty text → neutral "Type a two-letter
  ISO 639-1 language code such as cy for Welsh, or the language's English name such as Welsh.";
  recognised → neutral "<Name> (<code>)" e.g. "Welsh (cy)"; not recognised → error "Not
  recognised. Use a two-letter language code such as cy, or the language's English name such
  as Welsh. It will not be saved."
- `void voice_lang_iso(const char *id, char *out, size_t cap);` → the table's iso for a known
  id; for an unknown id the part before the first '-' or '_', lower-cased ("pt-BR" → "pt",
  "en" → "en"); "" → "".
- `const char *voice_lang_spelling_hint(const char *id);` → for "en-GB":
  `"Use British English spelling, for example colour, organise, centre and travelled."`; NULL
  otherwise.
- `#define VOICE_LANG_DEFAULT "en-GB"`; `voice_settings_default` sets it.
- `SttTarget` gains what the request builder needs: the provider id (to know whether `prompt`
  is accepted) — openai, groq and custom (and use-AI openai/custom) get `prompt`; mistral gets
  none (its `context_bias` is vocabulary biasing, not a spelling switch — reported, not used).
- `voice_stt_multipart`: the language part is `language` = iso, except for a model whose name
  starts with `gpt-transcribe`, which gets one `languages[]` part = iso (OpenAI documents
  `languages` as an array for gpt-transcribe only; repeated `name[]` parts are OpenAI's
  multipart array form, as with `timestamp_granularities[]`). Nothing for Auto-detect. `prompt`
  part = the hint when non-NULL and the provider accepts it.
- `voice_stt_gemini_body`: the instruction names the language in words ("The language is
  English." from the table label without the region) and, for en-GB, appends the hint.
- Migration: `"voice_defaults_rev"` becomes **3**. On load, `rev = json_obj_num(jset,
  "voice_defaults_rev", 0)`: the PR #109 Right Shift migration runs only when `rev < 2` (same
  conditions as today); new: when `rev < 3` and the loaded `voice_stt_language` is empty, set
  it to `"en-GB"` and set `rep->voice_migrated = 1`. A non-empty language is never touched.
  The marker stays out of `k_setting_keys` and out of the notice.
- Help entry `SH_VOICE_LANGUAGE`: kind combo, text (also the dropdown's hover tooltip) "Tells
  the speech-to-text service which language you will speak. Auto-detect lets it guess, which
  is weakest on short commands. English (UK) also asks for British spelling where the provider
  allows it (OpenAI, Groq, custom servers, Gemini). Not listed? Choose Other… and type a
  two-letter ISO 639-1 language code such as cy for Welsh or mi for Maori, or the language's
  English name such as Welsh; your provider must support it.", default text "English (UK)".
  New entry `SH_VOICE_LANGUAGE_OTHER` (kind edit, label "Other language:", text — the Other
  box's tooltip — "Type a two-letter ISO 639-1 language code such as cy for Welsh or mi for
  Maori, or the language's English name such as Welsh. Your speech-to-text provider must
  support it.", default text "empty"); update the voice-entry count test.

- [ ] **Step 1: failing tests.** `test_voice_lang_table_order` (index 0 Auto-detect, 1 en-GB,
  2 en-US); `test_voice_lang_find` ("en-GB" 1, "fr" ≥ 3, "en" −1, "pt-BR" −1, "" 0);
  `test_voice_lang_parse_other` ("cy" → 1 "cy"; " CY " → 1 "cy"; "Welsh" → 1 "cy"; "welsh" →
  1 "cy"; "scottish gaelic" → 1 "gd"; "mi" → 1 "mi"; "fr" → 1 "fr"; "xx" → 0; "" → 0;
  "en-GB" → 0; "English-ish" → 0; a 300-char string → 0; NULL → 0);
  `test_voice_lang_name` ("cy" "Welsh", "zz" NULL);
  `test_voice_lang_other_hint` (empty → tone 0 and the format sentence; "welsh" → 0 "Welsh
  (cy)"; "xx" → 1 and the error naming the format; a 1-byte `cap` does not overflow);
  `test_voice_config_custom_language_roundtrip` (save "cy", load "cy", rev 3 kept, not migrated);
  `test_voice_lang_iso` ("en-GB" "en", "en-US" "en", "pt-BR" "pt", "EN_au" "en", "" "");
  `test_voice_lang_hint`; `test_voice_stt_multipart_language_gpt_transcribe` (model
  "gpt-transcribe", en-GB: body has `name="languages[]"` with `en`, no `name="language"`, has
  `name="prompt"` with the hint); `test_voice_stt_multipart_language_whisper` (whisper-1,
  en-GB: `name="language"` `en` and the prompt); `test_voice_stt_multipart_mistral_no_prompt`
  (en-GB: `language` en, no `prompt`); `test_voice_stt_multipart_en_us_no_prompt`;
  `test_voice_stt_multipart_auto_detect` (no language, no prompt);
  `test_voice_stt_gemini_language_words` (en-GB → "The language is English." and the hint);
  `test_voice_config_language_migration` (fixtures: rev absent + "" → "en-GB" and
  voice_migrated; rev 2 + "" → "en-GB"; rev 3 + "" → "" kept; rev 2 + "fr" → "fr"; rev 2 + "en"
  → "en"); `test_voice_config_rshift_migration_rev2_skips` (rev 2 config with Right Ctrl/Hold
  keeps them); save writes `"voice_defaults_rev": 3`; fresh defaults give "en-GB". Update the
  marker test and any test asserting rev 2.
- [ ] **Step 2:** run, see them fail. **Step 3:** implement. **Step 4:** `make -j test` 0 failed.
- [ ] **Step 5:** spec §4.1 Language bullet, §6 (fields per provider, gpt-transcribe's
  `languages`, the British-spelling hint, Mistral without it), §7 Language row, all
  **(maintainer, 2026-11-24)**. Commit `feat(voice): language list with English (UK) by default`.

### Task 4: Audio thread — gain, live gain, open-ended Test

**Files:**
- Modify: `src/ui/voice_audio_win.c`, `src/ui/voice_audio_win.h`, `src/ui/voice_win.c`
- Test: `tests/test_voice_audio_win.c`, `tests/win_runner.c`

**Interfaces:**
- Consumes: `pcm_gain_q16`, `pcm_apply_gain_q16` (Task 1).
- Produces: `voice_audio_capture_begin(const char *device_id, unsigned token, int test_only, int gain_db)`
  (new last parameter); `void voice_audio_set_gain(int gain_db);` (posts a new `VaCmdKind`
  `VA_SET_GAIN`, coalesced like the others, applied to the running capture);
  `VAM_LEVEL`'s lParam becomes the raw window peak 0..32768 (was 0..1000) — Settings converts.

- [ ] **Step 1: failing wintests** in `tests/test_voice_audio_win.c` using the existing seams:
  `test_voice_audio_test_capture_has_no_time_limit` (a test-only capture is still active after
  the old 3 s, using the hold/queue seam or an injected clock — no 3 s sleep if a seam can
  avoid it) and `test_voice_audio_test_capture_keeps_no_samples` (the test capture's PCM buffer
  is empty after level posts; add a seam `voice_audio_test_pcm_len()` if needed), plus
  `test_voice_audio_set_gain_coalesces`. Under Wine with no endpoint, follow the existing
  tests' `E_NOTFOUND` acceptance pattern and make each assertion meaningful where it can run.
- [ ] **Step 2:** `xvfb-run -a make wintest 2>&1 | tail -5` — the new ones fail.
- [ ] **Step 3:** In `va_drain`, apply the gain to the newly appended samples
  (`st->pcm.s + (n - take)`) right after `pcm_append`, so the meter, `pcm_classify` and
  `pcm_to_wav` all see the gained signal. Remove `VA_TEST_MS` and the 3 s end. A test-only
  capture computes the level from the new samples, then wipes them (`secure_zero`) and resets
  the length — it never accumulates and never posts `VAM_CAPTURED`. Push-to-talk passes
  `settings->voice_mic_gain_db` (`voice_win.c:74`).
- [ ] **Step 4:** `xvfb-run -a make wintest 2>&1 | tail -5` and `make -j test` — 0 failed.
  The MinGW build of the exe must also compile: `make -j release` is NOT run here (it would
  rebuild the tracked exe); instead `make -j wintest` already compiles these files with the
  MinGW flags.
- [ ] **Step 5:** spec §4.4 (Test capture keeps no samples, no time limit) **(maintainer,
  2026-11-24)**. Commit `feat(voice): apply the microphone gain and let Test run until stopped`.

### Task 5: Settings Voice page — gain slider, zoned meter, Test toggle, Language dropdown

**Files:**
- Create: `src/ui/ns_slider.c`, `src/ui/ns_slider.h`, `src/core/ns_slider_math.c`,
  `src/core/ns_slider_math.h`
- Modify: `src/ui/settings.c`, `src/ui/resource.h` (new IDs after 3112 only; not the version),
  `src/ui/ns_draw.c`, `src/ui/ns_draw.h`, `src/core/settings_help.c` (Test text), `Makefile`
  (CRLF; only if a list needs the new files), `src/core/ui_demo.c` if the settings-voice demo
  needs a gain/meter fixture
- Test: `tests/test_ns_slider_math.c`, `tests/runner.c`; a wintest
  `tests/test_ns_slider_win.c` (add to `WIN_ONLY_TESTS` and `WIN_TEST_SRCS`) for keyboard and
  drag messages

**Interfaces:**
- Consumes: Task 1 (`voice_gain_label`, range constants, `Settings.voice_mic_gain_db`),
  Task 2 (`VoiceMeter`, `voice_meter_feed`, `voice_meter_window_peak_db`,
  `voice_level_classify`, `voice_level_label`, `voice_meter_frac`, zone constants,
  `voice_test_next`, `voice_test_button_label`), Task 3 (`voice_lang_table`, `voice_lang_find`),
  Task 4 (`voice_audio_capture_begin(..., gain_db)`, `voice_audio_set_gain`, raw-peak
  `VAM_LEVEL`).
- Produces (`ns_slider_math.h`): `int ns_slider_value_at(int x, int track_x, int track_w, int min, int max);`
  (rounded, clamped), `int ns_slider_x_of(int value, int track_x, int track_w, int min, int max);`,
  `int ns_slider_key(int value, unsigned vk, int min, int max);` (Left/Down −1, Right/Up +1,
  PageDown −6, PageUp +6, Home min, End max, else unchanged).
- Produces (`ns_slider.h`): a registered class `Nutshell_Slider`: `HWND ns_slider_create(HWND parent, int id, int min, int max, int value);`
  `void ns_slider_set(HWND, int value);` `int ns_slider_get(HWND);` Notifies the parent with
  `WM_COMMAND(MAKEWPARAM(id, NSS_CHANGED), hwnd)` on every value change (drag, click, key).
  Paint from tokens: track `raised.base` pill (height `ns_scale(4)`-ish from the grid), filled
  part `accent.base`, thumb `surface`/`raised.hover` on hover (`ns_hover`), focus ring
  `tokens->focus` when focused, disabled with `.disabled`/`text_disabled`. Tab stop, mouse
  capture while dragging. Custom-drawn rather than `TRACKBAR_CLASS`, because the stock trackbar
  ignores the theme tokens (record this in the spec).
- Produces (`ns_draw.h`): `void ns_draw_level_meter(HDC, const RECT *bar, double level_frac, double hold_frac, const double zone_frac[4], const ThemeTokens *);`
  — track `raised.base`, zone bands under the bar tinted with `rgb_alpha()` of `warning`
  (below good), `success` (good), `warning` (hot), `danger` (loud) over `raised.base`; the fill
  in the intent of the zone the level sits in; a peak-hold tick `STROKE_*` wide in `text_main`.

Settings behaviour (in `settings.c`):
- **Gain row** "Microphone gain:" after Input device/meter: the slider (range −12..+30, step 1)
  with the value label ("+12 dB") as the row's `extra`. Moving it updates the label, and while
  a Test runs calls `voice_audio_set_gain`. Save stores it in `voice_mic_gain_db`. Enabled by
  the same rule as the device combo (`voice_enable_sync`).
- **Meter row**: owner-draw; the bar on the left and the label (`voice_level_label` of the
  window peak) on the right, its width reserved from the widest label measured in
  `ns_font(FONT_CAPTION, dpi)`, drawn with `DrawTextW` from UTF-8 (`MultiByteToWideChar
  CP_UTF8`) so the em dash renders, coloured `text_dim` (SILENT), `warning` (QUIET), `success`
  (GOOD/HOT), `danger` (LOUD). `VAM_LEVEL` feeds a `VoiceMeter` in the dialog state with the
  elapsed ms since the last feed (`GetTickCount64`); when not testing the meter is empty and
  the label reads "Press Test to check the level" in `text_dim`.
- **Test toggle**: every trigger goes through `voice_test_next` — Test button (label from
  `voice_test_button_label`), WM_DESTROY (Save/Cancel; already ends), the nav moving away from
  the Voice page, a device-combo selection change (RESTART: end and begin on the newly
  selected device with the slider's gain), unticking Enable voice or the AI assistant, and a
  capture error. Remove `IDT_VOICE_TEST` and the 3000 ms timer. The Test button stays enabled
  while running (it is the stop control). The existing refusal rule is unchanged: push-to-talk
  cannot run while Settings is open (its modal loop never calls the voice key filter, and
  opening Settings discards a recording).
- **Language**: replace the edit `IDC_VOICE_LANG` with a CBS_DROPDOWNLIST combo of
  `voice_lang_table` labels plus a last item "Other…"; select `voice_lang_find(value)`, or for
  −1 select "Other…" and fill the Other box with the stored value. The Other box (new edit
  `IDC_VOICE_LANG_OTHER`, the row's `extra`, ~ 140 px scaled, limit 31 chars, cue banner
  `VOICE_LANG_OTHER_CUE` via `EM_SETCUEBANNER`) is shown only while "Other…" is selected, and
  focus moves to it when the user picks "Other…". Under the row an inline hint row (the page's
  hint style) shows `voice_lang_other_hint` of the box on every edit change, `warning` colour
  for tone 1, `text_dim` for tone 0; hidden while "Other…" is not selected. Save writes the
  table id, or for Other the parsed code when recognised; an unrecognised Other keeps the
  previously saved language; an untouched box holding a legacy raw value ("pt-BR") saves it
  back unchanged. A saved custom code reloads as "Other…" with the box filled. Hover tooltips
  on the combo and the Other box use the page's existing settings-help tooltip mechanism
  (`ns_tip`, text from `SH_VOICE_LANGUAGE` / `SH_VOICE_LANGUAGE_OTHER`), not a new one.
- Help text `SH_VOICE_TEST`: "Listens to the microphone and shows the level on the meter until
  you press Stop test, change page or close Settings. Nothing is recorded or sent." Fix the
  stale "beside it" wording.
- `--ui-demo=settings-voice`: show the slider at a non-zero value (e.g. +6 dB) and the meter in
  a fixed demo state (level in GOOD, hold tick, label "Good") so the gallery captures it
  without a device.

- [ ] **Step 1: failing tests** `tests/test_ns_slider_math.c`: `test_ns_slider_value_at`
  (track 0..420, range −12..30: x 0 → −12, x 420 → 30, x 120 → 0, x −5 → −12, x 999 → 30,
  zero-width track → min); `test_ns_slider_x_of` (inverse at the ends and at 0);
  `test_ns_slider_key` (each key, clamping at both ends). Wintest
  `test_ns_slider_win_keys_and_notify` (create in a hidden parent, post VK_RIGHT/VK_END, value
  and NSS_CHANGED count change; a disabled slider ignores keys).
- [ ] **Step 2:** run both, see them fail. **Step 3:** implement. **Step 4:** `make -j test`
  and `xvfb-run -a make wintest` 0 failed; `make lint-gate`; the colour/scale/msgbox gates in
  `tests/test_ui_tokens.c` pass.
- [ ] **Step 5: look at it.** Build a scratch MinGW exe WITHOUT touching the tracked one
  (`make -j release` writes `build/win/nutshell.exe`; after looking, `git restore
  build/win/nutshell.exe`). Use a private Wine prefix so the shared `~/.wine` DPI is not
  changed: `export WINEPREFIX=<workspace>/wineprefix` (create once with `wineboot -i`), set
  `HKCU\Control Panel\Desktop\LogPixels` to 96, 120 and 192 in turn, and capture
  `--ui-demo=settings-voice` on each theme (`--theme "<name>"`; list the four from the theme
  code) with `xvfb-run -a` and `import -window root` (or the harness's capture method if one
  exists for Linux). Read the PNGs and fix anything clipped or overlapping. Save the PNGs in the
  workspace and list their paths in the report.
- [ ] **Step 6:** spec §7 Microphone rows and §8 demos **(maintainer, 2026-11-24)**. Commit
  `feat(voice): gain slider, zoned level meter, Test toggle and language list in Settings`.

### Task 6: Status in the header, model chip removed

**Files:**
- Modify: `src/core/ai_header_layout.c/.h`, `src/core/ai_panel_layout.c/.h`,
  `src/ui/ai_chat_view.c`, `src/ui/ai_chat_status.c`, `src/ui/ai_chat_internal.h`,
  `src/ui/ai_chat.c`, `src/ui/ai_chat_voice.c`, `src/core/ui_demo.c/.h`,
  `tests/integration/cases/30-ui-demo.ps1`, `README.md` (the panel and Status Line sections,
  not the version), `docs/superpowers/specs/2026-09-07-ai-assist-panel-design.md`, the
  2026-10-18 spec's §3
- Test: `tests/test_ai_header_layout.c`, `tests/test_ai_panel.c`, `tests/test_ui_demo.c`,
  `tests/runner.c`

Facts from the survey: the header already lays out buttons, a model chip, a status word (dot +
Processing/Thinking/Responding/Executing/Waiting) and the name (`ai_header_layout.c`); the
chip is paint-only (no click, hover, tooltip or menu), so removing it removes no behaviour —
the model stays visible on each reply's "AI · <model>" label and in the meter tooltip. The
bottom strip's status (`d->context_label[64]`: "Continuing", "Retrying", "Running N/M ·
waiting for prompt", the voice notes "Connect a session first", "Still transcribing the last
recording", "Assistant is busy", "Didn't catch that", "Assistant is busy: your words are in the
box", "Voice message dropped: the chat changed") is drawn right-aligned in place of the meter
and counter, cut at 63 bytes. Voice errors go to the thread and the Listening/Transcribing cue
is the input placeholder; neither is in the strip. The "empty pill" is the context meter
(`ns_draw_meter`, 60 px, accent fill = tokens/limit; 536 of 1000k rounds to 0 px), with its
own tooltip — it stays.

**Interfaces — Produces:**
- `ai_header_layout` loses the chip (parameter and rect removed); the status slot becomes a
  text slot: it follows the name, takes the free width up to the buttons minus a gap, and the
  name keeps at least `min_name_w` (input: the width of the name's first ~8 characters or the
  whole name if shorter) before the status is narrowed; the status reports `status_clipped`
  when its measured width exceeded the slot. Dropped only if the slot is narrower than a
  minimum (dot + gap + ~3 characters).
- `const char *ai_header_status_pick(const char *transient, const char *voice_cue, const char *activity_word);`
  in `ai_header_layout.c` — the first non-empty of: the transient status (`context_label`),
  the voice cue while recording/transcribing, the activity word; NULL when all are empty.
- `context_label` grows to 256 bytes; the strip no longer draws it (meter bar and counter are
  always drawn at their tier), and `AiStatusLayout.label` is removed with its tests.
- The header paints: the activity dot (as today) when activity is not idle, then the picked
  text in `ns_font(FONT_CAPTION, dpi)` with `DT_END_ELLIPSIS`, colour `text_dim` for a note
  and the activity health colour as today for the word; the early return when idle now
  happens only when nothing is picked. Every setter of `context_label` and of the voice cue
  calls the header invalidate + sync (re-clip the name). A rect tooltip on `d->hwnd` over the
  status rect (`STATUS_TOOL_BASE` range widened, or a new id the TTN_GETDISPINFO handler
  accepts) shows the full text.
- `--ui-demo` gains state `header-status`: a long transient status (e.g. "Still transcribing
  the last recording: wait for it to finish, or press Esc to cancel it first"), included in
  `all` and in `ui_gallery`'s state list in `30-ui-demo.ps1` (unrun here; say so).

- [ ] **Step 1: failing tests.** Rewrite the nine `test_ai_header_layout_*` tests for the new
  inputs (no chip): short name + short status both fit in order; long status is narrowed and
  `status_clipped` set while the name keeps `min_name_w`; long name + no status gets everything
  to the buttons; a slot below the minimum drops the status; a width sweep 0..1200 at 96 and
  192 DPI never overlaps name/status/buttons; NULL arguments. Add
  `test_ai_header_status_pick` (transient beats cue beats word; all empty → NULL). Update
  `test_ai_panel` strip tests for the removed `label` rect. `test_ui_demo` knows
  `header-status`.
- [ ] **Step 2:** run, fail. **Step 3:** implement. **Step 4:** `make -j test`, `make
  lint-gate`, `xvfb-run -a make wintest` 0 failed.
- [ ] **Step 5: look at it.** As Task 5 Step 5 (private `WINEPREFIX`, scratch exe restored
  afterwards): capture `--ui-demo=header-status`, `--ui-demo=executing` and `--ui-demo=local`
  on the four themes at 96, 120 and 192 DPI, docked; and undocked if a demo or flag opens the
  panel undocked (if none exists, say so: the header code is identical in both). Read the PNGs,
  fix anything wrong, list the paths.
- [ ] **Step 6:** docs: README panel bullet (no model chip; status in the header) and Status
  Line section; the 09-07 and 10-18 specs; the voice spec §4.2 status lines and §8
  **(maintainer, 2026-11-24)**. Commit `feat(ai): panel status in the header, model chip removed`.

### Task 7: Notes, README feature text, version and build (controller)

- [ ] Notes `docs/superpowers/specs/2026-09-06-ui-redesign-notes.md`: a "Done" entry for this
  branch and, in the voice Open item, the Windows hand checks: gain slider and meter with the
  HD Pro Webcam C920 (Good zone reachable by gain alone, clipping shows Too loud); Test stays
  on and stops on each trigger (button, page change, device change restarts, voice untick,
  Save, Cancel, unplug); header status docked and undocked with a long error, tooltip shows it
  in full; model chip gone; English (UK) on OpenAI gpt-transcribe spells "colour",
  "organise"; the Language tooltip on hover over the dropdown and the Other box; the Other
  flow (pick Other…, the cue "2-letter code or name, e.g. cy or Welsh" shows in the empty box
  and the format hint under it; type "Welsh" → "Welsh (cy)", Save, reopen shows Other… with
  cy; type "xx" → the not-recognised hint naming the format, and the old language kept);
  item 0's review flow ((ae)-(ai)).
- [ ] README voice paragraph: gain, meter, Test toggle, Language default.
- [ ] Bump to 1.2.94 (`resource.h`, README), `make clean && make release`, commit the exe:
  `build: bump to v1.2.94 and rebuild nutshell.exe (v1.2.94)`.
- [ ] Full list: `make -j test`, `make test SAN=1`, `make lint-gate`, `make fuzz-run`,
  `xvfb-run -a make wintest`.
