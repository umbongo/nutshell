# Conversation mode with Windows speech (PR 2 of live voice) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A Conversation mode for voice: press the key once, talk, and each turn is sent at the
pause, the reply is read aloud with Windows speech (SAPI) in an English (UK) woman's voice where
one is installed, cutting in stops it, "carry on" and "say that again" are handled locally, and
the microphone is closed while the assistant works, in the background, behind a dialog and at
every end of the conversation.

**Architecture:** Every decision that can be pure lives in `src/core` with native tests: the
adaptive speech detector and pause endpointer (`voice_vad.c`), the listener that combines them
with the pre-roll and the two-stage cut-in (`voice_listen.c`), the speakable-text extractor and
chunker (`voice_speak.c`), the echo filter, phrase matcher and turn classifier (`voice_echo.c`),
the Windows voice choice rule (`voice_voices.c`) and the conversation state machine
(`voice_conv.c`). `src/ui` is glue: the conversation capture on the existing audio thread
(communications category, echo-cancellation objects, the listener), a SAPI speech thread
(`voice_tts_win.c`), the registry voice list (`voice_voices_win.c`), the session-lock
registration (`voice_lock_win.c`), and the conversation controller (`voice_conv_win.c`), which
drives `voice_conv` through a table of operations so `wintest` can run it with fakes.

**Tech Stack:** C11, Win32 (MinGW cross-compile), WASAPI (`IAudioClient2`, hand-declared
`IAudioEffectsManager`/`IAcousticEchoCancellationControl`), SAPI 5 (`sapi.h`, COM at run time),
WTS session notifications (delay-loaded `wtsapi32.dll`), custom `test_framework.h`, Wine
`wintest`, `make selftest-wine`.

**Spec:** `docs/superpowers/specs/2026-11-24-voice-live-conversation-design.md` (the "live
spec"); PR 2 scope in §15 item 2. PR 1's build notes are in its §16 "Found at build time
(PR 1)". Nothing from PR 3 (AI speech, Nutshell's own render client, `voice_speech_engine`).

## Global Constraints

- Worktree `/home/thomas/nutshell-wt/voice-conv`, branch `feat/voice-conversation`. Never touch
  `~/nutshell` or another worktree.
- Version: only Task 17 (the orchestrator) bumps `APP_VERSION`/`APP_VERSION_BINARY`, README's
  `**Version**:` line, and commits `build/win/nutshell.exe`. No implementer task does.
- Test first. A test that tells two states apart needs a fixture in which they differ.
- Line endings preserved: `src/config/loader.c` and `Makefile` are CRLF; `src/ui/ai_chat.h` is
  mixed (keep each line's ending). Before each commit `git diff --stat` and
  `git diff --stat --ignore-cr-at-eol` must agree.
- Portable logic in `src/core` (files in `src/ui` are not in the native build). New
  `src/core/*.c` and native `tests/*.c` files are picked up by the Makefile's wildcards; every
  test function is declared and called in `tests/runner.c`. A Win32-only test is
  `tests/*_win.c`, listed in `WIN_ONLY_TESTS`, its sources in `WIN_TEST_SRCS`, called from
  `tests/win_runner.c`.
- Both builds `-Werror`; MinGW adds `-Wpedantic -Wshadow -Wformat=2 -Wconversion`. Every task
  that touches `src/ui` also runs `make -j release` far enough to compile (or
  `make build/win/obj/<file>.o` style targets) so MinGW warnings are caught before review.
- Design-system rules (CLAUDE.md): no `RGB(` literal in `src/ui`; `ns_tokens()`, `ns_scale`,
  `ns_font`, `ns_motion`, `ns_hover`; no new `MessageBox` (notices use `notice_dlg_*`).
- Optional-feature rule: with voice off, AI off, or voice on and idle in either mode, nothing
  added here creates a thread, a COM object, a WebSocket, a session-lock registration, or asks
  for `sapi.dll`/`wtsapi32.dll`. One gate per entry point: `voice_tts_start` is the only
  `SpVoice` constructor, `voice_lock_register` the only WTS registration, `voice_net_open` the
  only voice network opener; each refuses unless `voice_gate_on()`.
- The resource ledger (`src/core/voice_ledger.h`) is UI-thread only. `VRES_SPVOICE` is acquired
  when the speech thread is created and released when it is joined; `VRES_SESSION_LOCK` around
  the WTS registration; `VRES_CAPTURE` around each conversation capture open/close, as dictation
  does. No other thread calls the ledger.
- Content: audio, transcripts and spoken text are `secure_zero`ed before free and logged only
  through `DLOG_CONTENT`. SAPI input is always `SPF_ASYNC | SPF_IS_NOT_XML`.
- New window messages: `WM_VOICE_TTS (WM_APP + 0x44)`, `WM_VOICE_TURN_IDLE (WM_APP + 0x45)`,
  `WM_VOICE_CARD (WM_APP + 0x46)`, `WM_VOICE_NOTICE (WM_APP + 0x47)`. New timer
  `VOICE_CONV_TIMER_ID 0x56F2` on the main window.
- Copy fixed by the spec (UTF-8; the ellipsis is U+2026, `\xE2\x80\xA6`; the dash is U+2014):
  - header: `Listening… speak, then pause to send` / `Hearing you…` / `Sending…` /
    `Transcribing…` / `Working… press Escape to stop` / `Speaking… talk to cut in` /
    `Paused while Nutshell is in the background` / `Paused while a dialog is open` /
    `Waiting for your approval` / `Didn't catch that` / `Ignored the assistant's own voice` /
    `Repeating` / `Carrying on`
  - cue with a draft in the box: `Your text and what you say next will be sent after a pause.`
  - chat notes: `Conversation ended: the voice settings changed` /
    `Conversation ended: Windows was locked` / `Conversation ended: nothing said for 3 minutes` /
    `Conversation ended: the chat changed` / `The assistant is busy` /
    `Connect a session first` / `The conversation is full: start a New Chat` /
    `The request could not start` / `Could not speak the reply: it is on screen`
  - spoken: `I need your approval to run a command.` / `I need your approval to write a file.` /
    `I need your approval for the next step.` / `I couldn't send that. The details are on
    screen.` / `Something went wrong. The details are on screen.` / `The code is on screen.` /
    `The table is on screen.` / Play sample: `Hello. This is how replies will sound.`
  - Settings lines: `Your microphone reports no echo cancellation: a headset works best` /
    `Windows may lower other sounds during a conversation. To stop that: Control Panel > Sound >
    Communications > Do nothing.` / `The saved voice is not installed on this PC: using
    Automatic` / (plan's wording for the English (UK) line)
    `No British English woman's voice is installed: add English (United Kingdom) under Windows
    Settings > Time & language > Speech`
  - notice dialog (once per launch): `Windows has no British English woman's voice installed, so
    replies use <voice>. To get one, add English (United Kingdom) under Windows Settings > Time &
    language > Speech, or choose AI speech in Settings > Voice.`
- Numbers fixed by the spec: pause 500..3000 ms, default 1200; turn minimum 300 ms of speech
  (`VOICE_SHORT_TAP_MS`); turn cap 120 000 ms; final-text wait 3000 ms; safety timeout 3000 ms
  of chat idle, polled every 1000 ms; conversation end after 180 000 ms with no turn sent
  (from the later of last turn sent and last assistant idle, checked only in Listening or
  Paused); held reply spoken on resume only if under 30 000 ms old; echo filter active during
  playback and 2000 ms after, 3-word runs covering at least 90%; repeat/continue only with a
  spoken reply from the last 300 000 ms that did not end with a question, 6 words or fewer;
  cut-in duck at about 200 ms, stop at about 400 ms, un-duck within about 300 ms; duck about
  -15 dB; first speech chunk about 200 characters, later chunks up to about 1000.

## Review Focus

1. **Stale speech events.** A Stop or a new reply while SAPI still reports word boundaries for
   the old one: offsets from the old reply must never move the new reply's cut-off. Pinned by
   `test_vconv_win_stale_reply_events_ignored` (Task 13).
2. **Non-ASCII replies and transcripts** (é, curly quotes, an emoji, a non-breaking space): the
   speech text and every offset stay on UTF-8 character boundaries, and the echo filter's word
   split treats non-ASCII bytes as letters. Pinned by `test_vspeak_utf8_boundaries` (Task 4)
   and `test_vecho_non_ascii_words` (Task 5).
3. **A very long reply** (60 KB of prose and code): extraction and chunking finish in linear
   time and every chunk is within 1000 characters. Pinned by `test_vspeak_long_reply` (Task 4).
4. **Events arriving after their moment**: a "chat turn idle" while Ending (a typed send's idle
   from before), a transcript for a turn the pause dropped, a send result after the end. Each is
   ignored. Pinned by `test_vconv_stale_events_ignored` (Task 7).
5. **Press, press before anything opened** (start, then end at once before the capture or the
   speech thread reported back), and voice switched off mid-reply: the ledger returns to zero and
   no thread is left. Pinned by `test_vconv_press_press_ledger_zero` (Task 7) and
   `test_vconv_win_off_mid_reply` (Task 13).

---

### Task 1: The PR 2 settings, help, notice lines, digest and shortcuts

**Files:**
- Modify: `src/config/config.h` (voice fields), `src/config/loader.c` (CRLF: load, save,
  `k_setting_keys`), `src/core/voice_config.[ch]` (defaults, validation, digest),
  `src/core/settings_help.[ch]`, `src/core/notice.c` (only if the folding needs it),
  `src/core/shortcuts.c`, `README.md` (regenerated shortcuts block only)
- Test: `tests/test_voice_config.c`, `tests/test_config_*.c`/`tests/test_notice*.c` (whichever
  hold the existing voice round-trip and notice tests), `tests/test_shortcuts.c`

**Interfaces:**
- Produces (in `src/config/config.h`, `Settings`):
  - `int voice_mode;` with `#define VOICE_MODE_DICTATION 0` and `#define VOICE_MODE_CONVERSATION 1`
    in `src/core/voice_config.h`. Saved as the string `"dictation"` or `"conversation"` under key
    `voice_mode`; any other string loads as Dictation.
  - `int voice_pause_ms;` key `voice_pause_ms`, default 1200, clamped to 500..3000 on load and
    validate, saved rounded to the nearest 100.
  - `char voice_windows_voice[256];` key `voice_windows_voice` (a SAPI token id, empty =
    Automatic) and `char voice_windows_voice_name[128];` key `voice_windows_voice_name`.
  - `VOICE_PAUSE_MIN_MS 500`, `VOICE_PAUSE_MAX_MS 3000`, `VOICE_PAUSE_DEFAULT_MS 1200` in
    `voice_config.h`.
  - `SettingsHelpId` entries `SH_VOICE_MODE`, `SH_VOICE_PAUSE`, `SH_VOICE_WIN_VOICE` (before
    `SH_COUNT`).
- No speech-rate setting (`voice_speech_rate` is not built: the spec makes it optional).

- [ ] **Step 1: Write the failing tests**
  - `test_voice_conv_settings_defaults`: a config without the four keys loads with
    `voice_mode == VOICE_MODE_DICTATION`, `voice_pause_ms == 1200`, both voice strings empty.
  - `test_voice_conv_settings_roundtrip`: save then load keeps `VOICE_MODE_CONVERSATION`, 2300,
    a token id and its name; the saved file contains `"voice_mode": "conversation"`.
  - `test_voice_conv_settings_clamp`: `voice_pause_ms` 100 loads as 500, 9000 as 3000, 1249 saves
    as 1200 and 1250 as 1300; `"voice_mode": "shouting"` loads as Dictation.
  - `test_voice_conv_settings_notice_lines`: an existing config with `voice_enabled` present and
    the four keys missing yields exactly these four notice lines (label — value):
    `Voice: mode` — `Dictation, so your words wait in the box for you to press Send`;
    `Voice: pause before sending` — `1.2 seconds`;
    `Voice: Windows voice` — `Automatic (an English (UK) woman's voice if one is installed)`;
    `Voice: Windows voice name` — `none, as Automatic is used`.
    With `voice_enabled` itself missing they fold into the one "Voice" line, as today.
  - `test_voice_conv_settings_digest`: changing any one of the four fields changes
    `voice_settings_digest`.
  - `test_voice_conv_auto_send_unchanged`: `voice_auto_send` loads, saves and notices exactly as
    before (no `voice_defaults_rev` change).
  - The existing `k_setting_keys`-against-a-saved-file test passes with 62 rows (58 + 4).
  - `test_shortcuts_voice_rows_pr2`: the voice-key row's text is `Talk to the assistant: press,
    speak, press again (in Conversation mode, press to start and end a conversation, or to stop
    a spoken reply)`; the Escape row's text is `Cancel a voice recording, stop a spoken reply or
    end a conversation`, its condition `AI panel focused`.

- [ ] **Step 2: Run them to see them fail** — `make -j test 2>&1 | tail -5`: the new tests FAIL
  (or do not link).

- [ ] **Step 3: Implement.** The four `CK(...)` rows carry the labels above
  (`Voice: Windows voice name` follows the `voice_input_device_name` precedent, sharing
  `SH_VOICE_WIN_VOICE`). Help entries (`SETTINGS_PAGE_VOICE`, `SH_KIND_SETTING`):
  - `SH_VOICE_MODE`, label `Mode:`, says Dictation puts your words in the box for you to send,
    Conversation sends each turn after a pause and reads replies aloud, the key always toggles in
    Conversation mode, and a conversation ends on a second press, Escape in the AI panel, locking
    Windows or 3 minutes with nothing said. `default_text` as the notice line above.
  - The existing `SH_VOICE_PTT_MODE` label becomes `Key mode:`; its help adds that Hold to talk
    and the Mic start delay apply in Dictation only. Keep PR 1's 10-minute/60-second and Escape
    sentences.
  - `SH_VOICE_PAUSE`, label `Pause before sending:`, 0.5 to 3 seconds, Conversation only.
  - `SH_VOICE_WIN_VOICE`, label `Windows voice:`, Automatic picks an English (UK) woman's voice
    when one is installed; how to add one (the Settings line's path).
  Add the four fields to `voice_settings_digest`. Update the shortcuts rows and regenerate the
  README block with the recipe in `tests/test_shortcuts.c` (keep README's line endings).

- [ ] **Step 4: Run** `make -j test 2>&1 | tail -3` — all pass.

- [ ] **Step 5: Commit** `feat(voice): conversation mode settings, help and notice lines`.

---

### Task 2: The adaptive speech detector and the pause endpointer

**Files:**
- Create: `src/core/voice_vad.h`, `src/core/voice_vad.c`
- Test: `tests/test_voice_vad.c`

**Interfaces:**
- Produces:
```c
#define VVAD_WIN 320                 /* 20 ms at 16 kHz */
double voice_window_db(const int16_t *s, size_t n);      /* RMS in dBFS, floored at -90 */
typedef enum { VV_SILENCE = 0, VV_SPEECH, VV_NOISE_RESET } VoiceVadOut;
typedef struct { /* builder's fields */ } VoiceVad;
void        voice_vad_init(VoiceVad *v);
void        voice_vad_set_extra_db(VoiceVad *v, double extra_db);  /* added to the margin (cut-in) */
void        voice_vad_freeze_floor(VoiceVad *v, int frozen);       /* while our speech plays */
VoiceVadOut voice_vad_window(VoiceVad *v, double level_db);        /* one 20 ms window */
double      voice_vad_floor_db(const VoiceVad *v);
typedef enum { VEP_NONE = 0, VEP_START, VEP_END, VEP_CAP, VEP_ABANDON } VoiceEpOut;
typedef struct { /* builder's fields */ } VoiceEndpoint;
void       voice_ep_init(VoiceEndpoint *e, int pause_ms);   /* pause clamped 500..3000 */
void       voice_ep_reset(VoiceEndpoint *e);                /* out of any turn */
VoiceEpOut voice_ep_window(VoiceEndpoint *e, VoiceVadOut w);
int        voice_ep_in_turn(const VoiceEndpoint *e);
```
- The numbers (builder's call, record them in the header comment):
  - floor starts at the first window's level; falls at once to any lower level; on a window that
    is not a candidate and outside a speech burst it rises by at most 0.05 dB (2.5 dB/s);
  - a window is a candidate when `level >= floor + 12 dB + extra_db` and `level >= -55 dBFS`;
  - a burst starts when 6 of the last 8 windows are candidates (those windows are speech), and
    ends after 15 consecutive non-candidate windows (300 ms); inside a burst a candidate window is
    `VV_SPEECH`;
  - stationarity: after 1 s inside a burst, if the last 50 windows span less than 4 dB, the burst
    is noise: the floor jumps to their mean, the burst ends, and that window returns
    `VV_NOISE_RESET`.
- Endpointer: `VEP_START` on the first `VV_SPEECH` outside a turn; inside a turn, when
  `pause_ms` has passed since the last speech window: `VEP_END` if the turn holds at least 300 ms
  of speech windows, else `VEP_ABANDON`; `VEP_CAP` at 120 000 ms after `VEP_START`;
  `VV_NOISE_RESET` inside a turn gives `VEP_ABANDON`. After END, CAP or ABANDON it is out of the
  turn.

- [ ] **Step 1: Write the failing tests** (synthesise windows with a deterministic generator:
  white noise from a fixed LCG at a given dBFS, a 200 Hz tone amplitude-modulated at 4 Hz as
  "speech"):
  - `test_vvad_steady_noise_never_speech`: 30 s of steady noise at -60, -45, -35 and -25 dBFS
    each: never a `VEP_START`.
  - `test_vvad_noise_starting_midway`: 5 s silence at -70 then a fan at -35: a turn may start but
    it never reaches `VEP_END`; within 2 s of burst time it gets `VEP_ABANDON` (noise reset), and
    afterwards the floor is within 3 dB of -35.
  - `test_vvad_click_never_turn`: single 20 ms and 40 ms clicks at -6 dBFS over -60 noise: no
    `VEP_START`.
  - `test_vvad_door_slam_never_turn`: a 200 ms burst at -10 dBFS decaying 30 dB over 300 ms: no
    `VEP_END` (at most `VEP_ABANDON`).
  - `test_vvad_speech_over_noise`: 1.5 s of modulated tone at -20 dBFS over -50 noise, then
    silence: `VEP_START` within 200 ms of onset; `VEP_END` at onset + speech length + pause.
  - `test_vep_pause_lengths`: with pause 500, 1200 and 3000, `VEP_END` arrives exactly
    `pause_ms/20` windows after the last speech window.
  - `test_vep_minimum_speech`: 280 ms of speech then silence gives `VEP_ABANDON`; 300 ms gives
    `VEP_END`.
  - `test_vep_turn_cap`: continuous speech gives `VEP_CAP` at window 6000 after start.
  - `test_vvad_after_floor_change`: noise steps from -60 to -40 for 10 s, then speech at -15 is
    detected.
  - `test_vvad_extra_db`: with `extra_db` 6, speech 15 dB over the floor is detected and 10 dB is
    not.

- [ ] **Step 2: Run** — FAIL (undefined symbols).
- [ ] **Step 3: Implement** in `src/core/voice_vad.c`; no heap, no Win32.
- [ ] **Step 4: Run** `make -j test 2>&1 | tail -3` — PASS.
- [ ] **Step 5: Commit** `feat(voice): adaptive speech detector and pause endpointer`.

---

### Task 3: The listener: pre-roll, turns and two-stage cut-in

**Files:**
- Create: `src/core/voice_listen.h`, `src/core/voice_listen.c`
- Test: `tests/test_voice_listen.c`

**Interfaces:**
- Consumes: Task 2's `VoiceVad`, `VoiceEndpoint`, `voice_window_db`.
- Produces:
```c
typedef enum { VLM_HOLD = 0, VLM_LISTEN, VLM_SPEAK_AEC, VLM_SPEAK_HD } VoiceListenMode;
typedef enum { VLN_TURN_START = 1, VLN_TURN_END, VLN_TURN_CAP, VLN_TURN_ABANDON,
               VLN_DUCK, VLN_UNDUCK, VLN_CUT } VoiceListenEvent;
typedef struct {
    void (*audio)(void *u, const int16_t *s, size_t n);   /* this turn's audio, in order */
    void (*event)(void *u, VoiceListenEvent e);
    void *u;
} VoiceListenSink;
typedef struct { /* VoiceVad, VoiceEndpoint, a 15-window (300 ms) pre-roll ring, partial
                    window, mode, cut-in counters, echo estimate */ } VoiceListen;
void voice_listen_init(VoiceListen *l, int pause_ms);
void voice_listen_set_mode(VoiceListen *l, VoiceListenMode m);  /* resets cut-in counters and
                                         any turn in progress (abandoned silently, no event) */
void voice_listen_feed(VoiceListen *l, const int16_t *s, size_t n, const VoiceListenSink *k);
void voice_listen_wipe(VoiceListen *l);   /* secure_zero the ring and partial window */
```
- Behaviour:
  - `VLM_HOLD`: windows update the floor only; no events, no audio.
  - `VLM_LISTEN`: on `VEP_START` emit `VLN_TURN_START`, then the pre-roll ring (each window
    once, oldest first, including the start window), then every window until the turn ends;
    `VEP_END` → `VLN_TURN_END`, `VEP_CAP` → `VLN_TURN_CAP`, `VEP_ABANDON` → `VLN_TURN_ABANDON`.
    The ring is emptied at turn start and end.
  - `VLM_SPEAK_AEC`: the floor is frozen and the detector's `extra_db` is 6. When cut-in speech
    windows reach 200 ms (10 speech windows, at most 2 non-speech windows between them): emit
    `VLN_DUCK` then `VLN_TURN_START` and the pre-roll, as in listening. At 400 ms of speech
    windows since the first: `VLN_CUT`, and the mode becomes `VLM_LISTEN` with the turn carrying
    on. 300 ms (15 windows) without speech before the cut: `VLN_UNDUCK` then
    `VLN_TURN_ABANDON`.
  - `VLM_SPEAK_HD` (no echo cancellation): the echo estimate starts at the floor and follows the
    capture level up by at most 0.5 dB a window and down by 0.1 dB a window; a cut-in candidate
    needs `level >= max(floor + 12, echo + 10)`. At 200 ms: `VLN_DUCK` only. At 400 ms:
    `VLN_CUT`, then `VLN_TURN_START` with **no pre-roll and no audio from before the cut**, then
    listening. Un-duck as above, with no `VLN_TURN_ABANDON` (no turn had started).
  - Record these numbers in the header comment as the builder's call.

- [ ] **Step 1: Write the failing tests** (reuse Task 2's generators; a test sink records events
  and counts audio samples, tagging each sample with the window it came from):
  - `test_vlisten_turn_with_preroll`: in LISTEN, speech starting at window 100: the audio
    begins at window 100 - 15 + (windows needed to start) and ends with the turn; events START
    then END.
  - `test_vlisten_hold_emits_nothing`: the same input in HOLD gives no events and no audio.
  - `test_vlisten_cough_abandoned`: 120 ms of speech in LISTEN gives START then ABANDON.
  - `test_vlisten_cutin_aec_duck_then_cut`: SPEAK_AEC, voice from window 50: DUCK and
    TURN_START at window 60 (±1), CUT at window 70 (±1), then END after the pause; the turn audio
    includes the pre-roll before window 50.
  - `test_vlisten_cutin_cough_unducks`: SPEAK_AEC, 240 ms of voice then silence: DUCK,
    TURN_START, then UNDUCK and TURN_ABANDON 15 windows after the voice stops; no CUT.
  - `test_vlisten_cutin_hd_no_audio_before_cut`: SPEAK_HD with an echo signal at -30 dBFS and the
    user at -12: DUCK at about 200 ms, CUT and TURN_START at about 400 ms; no sample tagged
    before the CUT window reaches the sink.
  - `test_vlisten_hd_echo_alone_never_cuts`: SPEAK_HD, 20 s of modulated echo at -25 dBFS: no
    DUCK.
  - `test_vlisten_set_mode_drops_turn`: LISTEN mid-turn, then `set_mode(HOLD)`: no further
    events or audio.

- [ ] **Step 2: Run** — FAIL. **Step 3: Implement.** **Step 4: Run** `make -j test` — PASS.
- [ ] **Step 5: Commit** `feat(voice): conversation listener with pre-roll and two-stage cut-in`.

---

### Task 4: The speakable-text extractor and the sentence chunker

**Files:**
- Create: `src/core/voice_speak.h`, `src/core/voice_speak.c`
- Test: `tests/test_voice_speak.c`

**Interfaces:**
- Consumes: `src/core/md_blocks.h` and `md_table.h` (read them first; reuse their block
  detection rather than re-parsing fences and tables).
- Produces:
```c
#define VOICE_SPEAK_FIRST_CHUNK 200
#define VOICE_SPEAK_CHUNK_MAX   1000
char  *voice_speak_text(const char *markdown);   /* malloc'd UTF-8; NULL only on OOM; "" when
                                                    nothing is speakable */
size_t voice_speak_chunks(const char *text, size_t from, size_t *starts, size_t *lens,
                          size_t max);            /* chunks of text[from..]; returns count */
int    voice_speak_ends_with_question(const char *text);
```
- Rules: a fenced or indented code block becomes `The code is on screen.` once per block; a
  Markdown table becomes `The table is on screen.` once per table; `[text](url)` reads `text`;
  a bare `http://` or `https://` URL is dropped; inline code reads as its text; emphasis, heading
  `#`s, blockquote `>` and list bullets/numbers lose their marks; a list item or heading with no
  closing punctuation gets `.` so SAPI pauses; `<think>…</think>` and `[EXEC]…[/EXEC]` spans are
  removed; runs of whitespace collapse to one space.
- Chunks end at a sentence end (`.`, `!` or `?` followed by whitespace or the end, not a digit
  after `.` as in `1.5`). The first chunk takes whole sentences up to 200 characters (at least
  one sentence); later chunks up to 1000. A sentence longer than the limit is split at its last
  space within the limit, or at the limit on a UTF-8 boundary when there is no space.

- [ ] **Step 1: Write the failing tests:** `test_vspeak_strips_markdown` (heading, bold, italic,
  inline code, a list), `test_vspeak_code_block_once` (two blocks → the phrase twice, text
  between kept), `test_vspeak_table_once`, `test_vspeak_link_text_no_url` (`[the docs](https://x)`
  → `the docs`; a bare URL gone), `test_vspeak_thinking_and_exec_removed`,
  `test_vspeak_only_code` (`The code is on screen.` alone), `test_vspeak_empty` (`""` and
  whitespace → `""`), `test_vspeak_chunks_first_200` (first chunk ≤ 200, ends at a sentence end,
  later ≤ 1000), `test_vspeak_chunk_from_offset` (chunks of `text + from` start at `from`),
  `test_vspeak_decimal_not_sentence_end`, `test_vspeak_question` (`Shall I delete them?` → 1,
  `Done.` → 0, trailing spaces ignored), `test_vspeak_utf8_boundaries` (é, curly quotes, an
  emoji, U+00A0: every chunk start and end is on a character boundary), `test_vspeak_long_reply`
  (60 KB of mixed prose and code: completes, every chunk ≤ 1000).
- [ ] **Step 2: Run** — FAIL. **Step 3: Implement.** **Step 4: Run** — PASS.
- [ ] **Step 5: Commit** `feat(voice): speakable text and sentence chunks for spoken replies`.

---

### Task 5: The echo filter, the repeat/continue matcher and the turn classifier

**Files:**
- Create: `src/core/voice_echo.h`, `src/core/voice_echo.c`
- Test: `tests/test_voice_echo.c`

**Interfaces:**
- Consumes: Task 4's `voice_speak_ends_with_question`.
- Produces:
```c
#define VOICE_ECHO_AFTER_MS    2000
#define VOICE_PHRASE_MAX_AGE_MS 300000
int voice_echo_match(const char *spoken, const char *transcript);  /* 1 = echo */
typedef enum { VPH_NONE = 0, VPH_REPEAT, VPH_CONTINUE } VoicePhrase;
VoicePhrase voice_phrase_match(const char *transcript);
typedef enum { VTC_EMPTY = 0, VTC_ECHO, VTC_REPEAT, VTC_CONTINUE, VTC_TEXT } VoiceTurnClass;
typedef struct {
    const char   *spoken;        /* the whole text last spoken (reply or phrase), or NULL */
    int           playing;       /* speech is playing now */
    unsigned long spoken_end_at; /* when it stopped playing */
    const char   *reply;         /* the last spoken reply's speech text, or NULL */
    unsigned long reply_at;      /* when it was last spoken (end or cut) */
    int           read_to_end;   /* it was read to the end */
} VoiceReplyCtx;
VoiceTurnClass voice_turn_classify(const char *transcript, const VoiceReplyCtx *r,
                                   unsigned long now);
```
- Words: split on any byte that is not an ASCII letter or digit and not a byte ≥ 0x80 (so
  non-ASCII letters stay in words); ASCII lower-cased; apostrophes are separators removed
  (`don't` → `don`, `t` is fine as long as both sides normalise alike).
- Echo: drop when the transcript has at least 3 words and the union of its runs of 3 or more
  contiguous words that also occur contiguously in `spoken` covers at least 90% of its words.
- Phrases: case and punctuation ignored, at most 6 words, optional leading `please`, `can you`,
  `could you`, `can you please` or `could you please`, optional trailing `please`; the rest must
  be exactly one of: repeat — `repeat`, `repeat that`, `say that again`, `again`, `what did you
  say`; continue — `continue`, `carry on`, `go on`, `keep going`.
- Classify, in order: whitespace-only → `VTC_EMPTY`; echo window open (`playing` or
  `now - spoken_end_at <= 2000`) and `voice_echo_match(spoken, t)` → `VTC_ECHO`; a phrase, with
  `reply` non-NULL, `now - reply_at <= 300000` and the reply not ending with a question → repeat
  gives `VTC_REPEAT`, continue gives `VTC_CONTINUE` unless `read_to_end` (then `VTC_TEXT`);
  everything else `VTC_TEXT`.

- [ ] **Step 1: Write the failing tests:** `test_vecho_120gb_dropped` (reply "…the C drive has
  120 GB free…", transcript "120 GB free"), `test_vecho_80_percent_kept` ("yes, check the D
  drive" against a reply containing "check the D drive"), `test_vecho_two_word_runs_never`,
  `test_vecho_two_runs_cover_90` (two 3-word runs covering 9 of 10 words → dropped),
  `test_vecho_real_answer_kept` (an answer sharing scattered words), `test_vecho_whole_reply`
  (a run from the reply's third sentence matches), `test_vecho_non_ascii_words`
  ("café ouvert à 9h" matches itself, not "cafe"), `test_vphrase_each_phrase`,
  `test_vphrase_polite_forms` ("Could you please say that again?" → repeat, "carry on please"
  → continue), `test_vphrase_seven_words_never`, `test_vphrase_inside_request_never` ("carry on
  with the backup"), `test_vclass_echo_window` (dropped while playing and at +2000 ms, kept at
  +2001 ms), `test_vclass_phrase_needs_recent_reply` (no reply; 300 001 ms old; ending with a
  question → `VTC_TEXT`), `test_vclass_continue_after_read_to_end` → `VTC_TEXT`,
  `test_vclass_empty`.
- [ ] **Step 2: Run** — FAIL. **Step 3: Implement.** **Step 4: Run** — PASS.
- [ ] **Step 5: Commit** `feat(voice): echo filter and local repeat/continue phrases`.

---

### Task 6: The Windows voice choice rule

**Files:**
- Create: `src/core/voice_voices.h`, `src/core/voice_voices.c`
- Test: `tests/test_voice_voices.c`

**Interfaces:**
- Produces:
```c
#define VOICE_INFO_MAX 64
typedef struct {
    char id[256];     /* full token path, e.g. "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Speech\\Voices\\Tokens\\TTS_MS_EN-GB_HAZEL_11.0" */
    char name[128];   /* Attributes\Name, e.g. "Microsoft Hazel Desktop" */
    char lang[32];    /* Attributes\Language, hex LCIDs separated by ';', e.g. "809;9" */
    int  female;      /* Attributes\Gender == "Female" (case-insensitive) */
    int  onecore;     /* from the Speech_OneCore tree */
} VoiceInfo;
int    voice_info_lcid_primary(const VoiceInfo *v);   /* first LCID, 0 if none */
int    voice_info_is_en_gb(const VoiceInfo *v);       /* any LCID == 0x809 */
int    voice_info_is_english(const VoiceInfo *v);     /* any LCID with (lcid & 0x3FF) == 0x09 */
size_t voice_list_dedupe(VoiceInfo *v, size_t n);     /* one per display name; see below */
void   voice_display_name(const VoiceInfo *v, char *out, size_t cap);  /* name minus " Desktop" */
int    voice_choose_auto(const VoiceInfo *v, size_t n);   /* index, or -1 = system default */
int    voice_find_saved(const VoiceInfo *v, size_t n, const char *id);  /* index or -1 */
int    voice_notice_due(const VoiceInfo *v, size_t n, int resolved, int using_auto,
                        int shown_this_launch);
```
- Dedupe: two entries with the same display name (case-insensitive, " Desktop" removed) keep
  the SAPI desktop one (`onecore == 0`) — builder's call: SAPI desktop tokens are certain to work
  with `ISpVoice::SetVoice`; OneCore ones are must-verify item 6. Order is otherwise kept.
- Automatic chain: an en-GB female voice whose display name contains `Hazel`, then `Susan`, then
  any other en-GB female; else any English female; else any en-GB voice; else -1. Ties keep list
  order. (Libby is a Natural voice that apps cannot use, so it is not named.)
- Notice due: `using_auto && !shown_this_launch && (resolved < 0 || !(is_en_gb && female))`.

- [ ] **Step 1: Write the failing tests:** `test_vvoices_chain_known_names_first` (Susan listed
  before Hazel → Hazel chosen), `test_vvoices_other_en_gb_female`,
  `test_vvoices_any_english_female` (en-US Zira over en-GB George), `test_vvoices_any_en_gb`
  (only George and David → George), `test_vvoices_none` (→ -1), `test_vvoices_dedupe_desktop_wins`,
  `test_vvoices_saved_gone` (→ -1), `test_vvoices_lcid_parsing` ("809;9", "409", "", "zz"),
  `test_vvoices_notice_due` (Hazel resolved → 0; Zira resolved → 1; already shown → 0; a chosen
  voice, not Automatic → 0; -1 → 1).
- [ ] **Step 2: Run** — FAIL. **Step 3: Implement.** **Step 4: Run** — PASS.
- [ ] **Step 5: Commit** `feat(voice): Windows voice choice rule (English (UK) woman first)`.

---

### Task 7: The conversation machine: the turn cycle, sending, Working and every end

**Files:**
- Create: `src/core/voice_conv.h`, `src/core/voice_conv.c`
- Test: `tests/test_voice_conv.c`

**Interfaces:**
- Consumes: `VoiceTurnClass` (Task 5).
- Produces (the full header; Task 8 adds behaviour, not names):
```c
typedef enum { VCS_IDLE = 0, VCS_LISTENING, VCS_HEARING, VCS_ENDING, VCS_WORKING,
               VCS_SPEAKING } VoiceConvState;
typedef enum { VCP_NONE = 0, VCP_BACKGROUND, VCP_DIALOG } VoiceConvPause;
typedef enum { VSS_SENT = 0, VSS_BUSY, VSS_NO_SESSION, VSS_FULL, VSS_LAUNCH_FAILED,
               VSS_EMPTY } VoiceSendStatus;
typedef enum { VCARD_COMMAND = 0, VCARD_WRITE, VCARD_OTHER } VoiceCardKind;
typedef enum { VCX_PANEL_CLOSED = 1, VCX_OFF, VCX_SETTINGS, VCX_LOCK, VCX_CHAT_CHANGED,
               VCX_DEVICE_LOST } VoiceConvEnd;
typedef enum {
    VCE_START,        /* a = live provider (1/0) */
    VCE_PRESS,        /* the key or the mic button */
    VCE_ESCAPE,       /* Escape with focus in the AI panel */
    VCE_STOP,         /* the panel's Stop (or Send showing Stop while Speaking) */
    VCE_SEND,         /* Send or Enter while Listening or Hearing */
    VCE_PAUSE,        /* a = VoiceConvPause */
    VCE_RESUME,
    VCE_TURN_START, VCE_TURN_END, VCE_TURN_ABANDON,   /* from the listener (END covers CAP) */
    VCE_DUCK, VCE_UNDUCK, VCE_CUT,
    VCE_LINK_READY, VCE_LINK_FAILED, VCE_LINK_FINAL,
    VCE_BATCH_OK, VCE_BATCH_FAIL,
    VCE_TRANSCRIPT,   /* a = VoiceTurnClass, after LINK_FINAL or BATCH_OK */
    VCE_SENT,         /* a = VoiceSendStatus */
    VCE_CARD,         /* a = VoiceCardKind: a card became pending */
    VCE_CHAT_IDLE,    /* "chat turn idle": a = has prose, b = the reply was an error */
    VCE_IDLE_POLL,    /* once a second while Working: a = chat idle now, b = prose | err << 1 */
    VCE_SPEECH_DONE,  /* what was being spoken was read to the end */
    VCE_SPEECH_FAILED,/* speech could not start */
    VCE_END,          /* a = VoiceConvEnd */
    VCE_TICK
} VoiceConvEvent;
enum {  /* action bits */
    VCA_ENGINE_START = 1u << 0,  VCA_ENGINE_STOP  = 1u << 1,
    VCA_LOCK_REG     = 1u << 2,  VCA_LOCK_UNREG   = 1u << 3,
    VCA_OPEN_CAPTURE = 1u << 4,  VCA_CLOSE_CAPTURE= 1u << 5,
    VCA_MODE_LISTEN  = 1u << 6,  VCA_MODE_SPEAK   = 1u << 7,  VCA_MODE_HOLD = 1u << 8,
    VCA_OPEN_LINK    = 1u << 9,  VCA_END_TURN_AUDIO = 1u << 10, VCA_CLOSE_LINK = 1u << 11,
    VCA_SUBMIT_BATCH = 1u << 12, VCA_CANCEL_BATCH = 1u << 13,
    VCA_DROP_TURN    = 1u << 14, /* discard the turn's audio and remove its words from the box */
    VCA_LOCK_TYPING  = 1u << 15, VCA_UNLOCK_TYPING= 1u << 16,
    VCA_SEND_TURN    = 1u << 17, /* send the box as a voice turn */
    VCA_SEND_TYPED   = 1u << 18, /* send the box as a typed message */
    VCA_SPEAK_REPLY  = 1u << 19, /* fetch the reply and speak it from the start */
    VCA_SPEAK_REPEAT = 1u << 20, /* the last reply from the start */
    VCA_SPEAK_CONTINUE = 1u << 21, /* the last (or held) reply from its cut-off */
    VCA_SPEAK_PHRASE = 1u << 22, /* out.phrase */
    VCA_STOP_SPEECH  = 1u << 23, VCA_DUCK = 1u << 24, VCA_UNDUCK = 1u << 25,
    VCA_CANCEL_WORK  = 1u << 26, /* cancel the assistant's work as Stop does */
    VCA_HOLD_REPLY   = 1u << 27, /* keep the reply (and cut-off) for resume */
    VCA_DROP_HELD    = 1u << 28,
    VCA_STOP_CAPTION = 1u << 29, /* Send shows Stop (Speaking) */
    VCA_SEND_CAPTION = 1u << 30  /* Send shows Send again */
};
typedef enum { VCH_NONE = 0, VCH_LISTENING, VCH_HEARING, VCH_SENDING, VCH_TRANSCRIBING,
               VCH_WORKING, VCH_SPEAKING, VCH_PAUSED_BG, VCH_PAUSED_DIALOG, VCH_APPROVAL,
               VCH_DIDNT_CATCH, VCH_IGNORED_ECHO, VCH_REPEATING, VCH_CARRYING_ON,
               VCH_LIVE_UNAVAILABLE, VCH_LIVE_STOPPED } VoiceConvHeader;
typedef enum { VCN_NONE = 0, VCN_END_SETTINGS, VCN_END_LOCK, VCN_END_SILENCE, VCN_END_CHAT,
               VCN_BUSY, VCN_NO_SESSION, VCN_FULL, VCN_LAUNCH, VCN_NO_SPEECH } VoiceConvNote;
typedef enum { VCPH_NONE = 0, VCPH_APPROVE_COMMAND, VCPH_APPROVE_WRITE, VCPH_APPROVE_OTHER,
               VCPH_NOT_SENT, VCPH_WENT_WRONG } VoiceConvPhrase;
typedef struct { unsigned actions; VoiceConvHeader header; VoiceConvNote note;
                 VoiceConvPhrase phrase; } VoiceConvOut;
typedef struct { /* builder's fields: state, pause, live, the open-resource flags
                    (engine, lock, capture, link, batch, turn, typing locked, stop caption),
                    sent_at, idle_at, idle_since, final_deadline, held, held_at,
                    held_continue, speaking_reply (reply vs phrase) */ } VoiceConv;
#define VOICE_CONV_FINAL_WAIT_MS   3000
#define VOICE_CONV_IDLE_POLL_MS    1000
#define VOICE_CONV_IDLE_SAFETY_MS  3000
#define VOICE_CONV_SILENCE_END_MS  180000
#define VOICE_CONV_HELD_MAX_MS     30000
void         voice_conv_init(VoiceConv *c);
VoiceConvOut voice_conv_step(VoiceConv *c, VoiceConvEvent ev, int a, int b, unsigned long now);
long         voice_conv_next_deadline(const VoiceConv *c, unsigned long now); /* ms or -1 */
int          voice_conv_active(const VoiceConv *c);          /* state != IDLE */
int          voice_conv_typing_locked(const VoiceConv *c);
const char  *voice_conv_header_text(VoiceConvHeader h);
const char  *voice_conv_note_text(VoiceConvNote n);
const char  *voice_conv_phrase_text(VoiceConvPhrase p);
VoiceCardKind voice_card_kind(const char *tool_name);    /* NULL = an [EXEC] command batch */
```
- Conventions: an event that means nothing in the current state returns `{0}` and changes
  nothing. Release actions (`CLOSE_CAPTURE`, `CLOSE_LINK`, `CANCEL_BATCH`, `ENGINE_STOP`,
  `LOCK_UNREG`, `UNLOCK_TYPING`, `SEND_CAPTION`) are emitted only for what the machine knows is
  open, so applying every action to the ledger balances it. Header and note texts are the Global
  Constraints copy, in the order of the enums.
- `voice_card_kind`: NULL or `run_command` → `VCARD_COMMAND`; the tool gate's file-writing tools
  → `VCARD_WRITE` (read `src/core/tool_gate*.c`/the tool table and list every tool that writes or
  edits a file); anything else `VCARD_OTHER`.
- The cycle (this task):
  - IDLE + START → LISTENING: ENGINE_START, LOCK_REG, OPEN_CAPTURE, MODE_LISTEN; header
    LISTENING; `sent_at = idle_at = now`.
  - LISTENING + TURN_START → HEARING: LOCK_TYPING, OPEN_LINK when live; header HEARING.
  - LISTENING + SEND → ENDING: SEND_TYPED (the box is a typed message; nothing was heard).
  - HEARING + TURN_END, or HEARING + SEND → ENDING: MODE_HOLD; live: END_TURN_AUDIO, the 3 s
    final wait armed, header SENDING; batch: SUBMIT_BATCH, header TRANSCRIBING.
  - HEARING + TURN_ABANDON → LISTENING: DROP_TURN, CLOSE_LINK (if open), UNLOCK_TYPING.
  - ENDING + LINK_FAILED, or the final wait passing (TICK) with the link open → SUBMIT_BATCH of
    the turn's audio, CLOSE_LINK, header LIVE_STOPPED (LIVE_UNAVAILABLE when it never became
    ready); the turn waits for BATCH_OK/FAIL.
  - ENDING + LINK_FINAL or BATCH_OK: nothing yet; the glue classifies and sends TRANSCRIPT.
  - ENDING + TRANSCRIPT(EMPTY) → LISTENING: DROP_TURN, UNLOCK_TYPING, MODE_LISTEN, header
    DIDNT_CATCH. (ECHO/REPEAT/CONTINUE are Task 8; until then they behave as TEXT.)
  - ENDING + TRANSCRIPT(TEXT) → stays ENDING: SEND_TURN.
  - ENDING + BATCH_FAIL → LISTENING: UNLOCK_TYPING, MODE_LISTEN (the words stay; the glue shows
    the STT error).
  - ENDING + SENT(SENT) → WORKING: CLOSE_CAPTURE, CLOSE_LINK, UNLOCK_TYPING; header WORKING;
    `sent_at = now`.
  - ENDING + SENT(EMPTY) → LISTENING: UNLOCK_TYPING, MODE_LISTEN, header DIDNT_CATCH.
  - ENDING + SENT(BUSY/NO_SESSION/FULL/LAUNCH_FAILED) → SPEAKING (a phrase): UNLOCK_TYPING,
    SPEAK_PHRASE(NOT_SENT), MODE_SPEAK, note BUSY/NO_SESSION/FULL/LAUNCH; after SPEECH_DONE or
    SPEECH_FAILED → LISTENING.
  - WORKING + CARD(kind) → SPEAK_PHRASE(APPROVE_*), header APPROVAL; state stays WORKING (the
    capture stays closed; SPEECH_DONE in WORKING does nothing).
  - WORKING + CHAT_IDLE(a, b), or IDLE_POLL with the chat idle for 3000 ms in a row: `idle_at =
    now`; b (error) → SPEAKING with SPEAK_PHRASE(WENT_WRONG); else a (prose) → SPEAKING with
    SPEAK_REPLY; else LISTENING. Entering SPEAKING: OPEN_CAPTURE, MODE_SPEAK, STOP_CAPTION,
    header SPEAKING. Entering LISTENING: OPEN_CAPTURE, MODE_LISTEN, header LISTENING.
  - SPEAKING + SPEECH_DONE → LISTENING: MODE_LISTEN, SEND_CAPTION, header LISTENING.
  - SPEAKING + SPEECH_FAILED → LISTENING as above, note NO_SPEECH when a reply (not a phrase)
    was being spoken.
  - SPEAKING + PRESS, ESCAPE or STOP → LISTENING: STOP_SPEECH, MODE_LISTEN, SEND_CAPTION. The
    conversation carries on.
  - Any other state + PRESS → IDLE (the assistant's work is left running, nothing is cancelled).
  - Any other state + ESCAPE or STOP → IDLE, with CANCEL_WORK when WORKING. (STOP in WORKING
    arrives after the chat has already cancelled; CANCEL_WORK is then only for ESCAPE.)
  - END(reason) → IDLE with the note: SETTINGS → END_SETTINGS, LOCK → END_LOCK, CHAT_CHANGED →
    END_CHAT, others none (DEVICE_LOST's error is the glue's).
  - TICK in LISTENING (or paused, Task 8) with `now - max(sent_at, idle_at) >= 180000` → IDLE,
    note END_SILENCE.
  - Every move to IDLE carries every release action for what is open, STOP_SPEECH when the
    engine is on, and DROP_HELD. It never carries DROP_TURN: words of an unfinished turn stay in
    the box (spec §5.6); the glue wipes any kept turn audio whenever the machine reaches IDLE.
  - `voice_conv_next_deadline`: the final wait in ENDING; 1000 in WORKING (the idle poll); the
    silence end in LISTENING/paused; else -1.
  - Events for a past moment are ignored: CHAT_IDLE/IDLE_POLL outside WORKING, TRANSCRIPT and
    LINK_*/BATCH_* outside ENDING (and outside HEARING for LINK_READY), SENT outside ENDING,
    SPEECH_* outside SPEAKING (except as above), TURN_* outside LISTENING/HEARING/SPEAKING.

- [ ] **Step 1: Write the failing tests.** A test helper `apply(VoiceConvOut)` applies each
  action to the real ledger (`ENGINE_START` acquires `VRES_SPVOICE`, `LOCK_REG`
  `VRES_SESSION_LOCK`, `OPEN_CAPTURE` `VRES_CAPTURE`, `OPEN_LINK` `VRES_LIVE_SESSION`,
  `SUBMIT_BATCH` `VRES_STT_UPLOAD`, and their releases) and asserts no underflow.
  - `test_vconv_full_cycle_live`: START(live) → TURN_START → TURN_END → LINK_FINAL →
    TRANSCRIPT(TEXT) gives SEND_TURN → SENT(SENT) closes the capture → CHAT_IDLE(1,0) speaks →
    SPEECH_DONE → LISTENING with the capture open; then PRESS → IDLE with the ledger all zero.
  - `test_vconv_full_cycle_batch`: as above with SUBMIT_BATCH/BATCH_OK.
  - `test_vconv_capture_closed_in_working`: after SENT(SENT) `VRES_CAPTURE` is 0; after
    CHAT_IDLE it is 1.
  - `test_vconv_each_send_status`: BUSY, NO_SESSION, FULL, LAUNCH_FAILED each give the phrase
    NOT_SENT and its note, then LISTENING after SPEECH_DONE; EMPTY gives DIDNT_CATCH.
  - `test_vconv_send_in_listening_and_hearing`: SEND in LISTENING → SEND_TYPED; SEND in HEARING
    → END_TURN_AUDIO (live) / SUBMIT_BATCH (batch).
  - `test_vconv_final_wait_falls_back`: TICK at +3000 ms in ENDING with the link open →
    SUBMIT_BATCH, CLOSE_LINK, header LIVE_STOPPED.
  - `test_vconv_working_ends_on_idle_not_busy`: IDLE_POLL(0) for 60 s keeps WORKING;
    IDLE_POLL(1) three times a second apart ends it (safety timeout); CHAT_IDLE ends it at once.
  - `test_vconv_error_reply_phrase`: CHAT_IDLE(1,1) → SPEAK_PHRASE(WENT_WRONG).
  - `test_vconv_no_prose_resumes_listening`: CHAT_IDLE(0,0) → LISTENING.
  - `test_vconv_card_announced`: CARD for each kind gives its phrase and header APPROVAL, state
    WORKING, capture closed.
  - `test_vconv_card_kind`: `voice_card_kind(NULL)`, `"run_command"`, each write tool, `"web_fetch"`.
  - `test_vconv_speaking_press_escape_stop`: each stops speech only and keeps the conversation.
  - `test_vconv_ends_ledger_zero`: from each of LISTENING, HEARING (live and batch), ENDING,
    WORKING and SPEAKING, each of PRESS, ESCAPE, STOP, END(each reason) and the silence TICK ends
    the conversation with the ledger all zero and the right note (PRESS/ESCAPE/STOP in SPEAKING
    are the exception above).
  - `test_vconv_escape_cancels_work`: ESCAPE in WORKING → CANCEL_WORK; PRESS in WORKING → no
    CANCEL_WORK.
  - `test_vconv_silence_end`: LISTENING with no turn for 179 999 ms stays; at 180 000 ends with
    END_SILENCE; a CHAT_IDLE at t restarts the count from t.
  - `test_vconv_typing_lock`: locked in HEARING and ENDING, unlocked in LISTENING, WORKING and
    SPEAKING.
  - `test_vconv_stale_events_ignored`: CHAT_IDLE in ENDING, TRANSCRIPT in LISTENING, SENT in
    IDLE, LINK_FINAL in WORKING: no actions, no state change.
  - `test_vconv_press_press_ledger_zero`: START then PRESS at the same `now`: ledger zero.
- [ ] **Step 2: Run** — FAIL. **Step 3: Implement.** **Step 4: Run** — PASS.
- [ ] **Step 5: Commit** `feat(voice): conversation state machine: turns, sending and every end`.

---

### Task 8: The conversation machine: pausing, held replies, cutting in, echo and repeat

**Files:**
- Modify: `src/core/voice_conv.c` (and its header only to document; no new names unless the
  implementer needs a private helper)
- Test: `tests/test_voice_conv.c`

**Interfaces:**
- Consumes: Task 7's header in full.
- Behaviour added:
  - PAUSE(reason) from LISTENING, HEARING, ENDING (before SEND_TURN) or SPEAKING: CLOSE_CAPTURE;
    a turn in HEARING/ENDING is dropped (DROP_TURN, CLOSE_LINK, CANCEL_BATCH, UNLOCK_TYPING);
    SPEAKING: STOP_SPEECH, HOLD_REPLY with `held_continue = 1` (from its cut-off), `held_at =
    now`, SEND_CAPTION. Header PAUSED_BG or PAUSED_DIALOG. In WORKING: only the header (the
    capture is already closed). While paused a second PAUSE only updates the reason; TURN_*,
    DUCK/UNDUCK/CUT are ignored.
  - CHAT_IDLE (or the safety timeout) while paused in WORKING: with prose, HOLD_REPLY with
    `held_continue = 0`, `held_at = now`; an error reply holds the WENT_WRONG phrase the same way
    (builder's call: a phrase is held like a reply); no prose: nothing held. The state becomes
    LISTENING (paused).
  - RESUME: in WORKING (paused): header WORKING. Otherwise: a held reply with `now - held_at <
    30000` → SPEAKING (OPEN_CAPTURE, MODE_SPEAK, STOP_CAPTION, header SPEAKING) with
    SPEAK_CONTINUE when `held_continue`, else SPEAK_REPLY (or the held phrase); an older one →
    DROP_HELD and LISTENING (OPEN_CAPTURE, MODE_LISTEN). No held reply → LISTENING.
  - The 3-minute TICK also ends a paused conversation (not paused WORKING).
  - SPEAKING + TURN_START (AEC cut-in, after DUCK): LOCK_TYPING, OPEN_LINK when live; a turn is
    open but the state stays SPEAKING. SPEAKING + DUCK → DUCK; + UNDUCK → UNDUCK; + TURN_ABANDON
    → DROP_TURN, CLOSE_LINK, UNLOCK_TYPING.
  - SPEAKING + CUT → HEARING: STOP_SPEECH, SEND_CAPTION, header HEARING (the cut-off is the
    glue's last word offset). In half-duplex the listener sends TURN_START after CUT: HEARING +
    TURN_START with no turn open → LOCK_TYPING, OPEN_LINK when live.
  - ENDING + TRANSCRIPT(ECHO) → LISTENING: DROP_TURN, UNLOCK_TYPING, MODE_LISTEN, header
    IGNORED_ECHO.
  - ENDING + TRANSCRIPT(REPEAT) → SPEAKING: DROP_TURN, UNLOCK_TYPING, SPEAK_REPEAT, MODE_SPEAK,
    STOP_CAPTION, header REPEATING. CONTINUE likewise with SPEAK_CONTINUE, header CARRYING_ON.
  - A voice settings change, lock, chat change, voice/AI off while paused end it as in Task 7.

- [ ] **Step 1: Write the failing tests:** `test_vconv_pause_closes_capture_and_back`
  (LISTENING → PAUSE(BACKGROUND): capture 0, header; RESUME: capture 1, LISTENING),
  `test_vconv_pause_dialog_header`, `test_vconv_pause_drops_heard_turn` (HEARING + PAUSE →
  DROP_TURN, link closed, typing unlocked; nothing sent after RESUME),
  `test_vconv_held_reply_spoken_under_30s` (PAUSE in WORKING, CHAT_IDLE(1,0) at t, RESUME at t +
  29 999 → SPEAK_REPLY), `test_vconv_held_reply_dropped_at_30s` (RESUME at t + 30 000 →
  DROP_HELD, LISTENING), `test_vconv_speaking_paused_continues` (SPEAKING + PAUSE → STOP_SPEECH,
  HOLD_REPLY; RESUME → SPEAK_CONTINUE), `test_vconv_working_continues_while_paused` (PAUSE in
  WORKING then CHAT_IDLE: no capture opened until RESUME), `test_vconv_cutin_aec`
  (SPEAKING: DUCK, TURN_START opens the link and locks typing, CUT → HEARING with STOP_SPEECH;
  TURN_END → ENDING), `test_vconv_cutin_cough` (DUCK, TURN_START, UNDUCK, TURN_ABANDON: the reply
  carries on, link closed, ledger as before), `test_vconv_cutin_hd` (DUCK, CUT, TURN_START →
  HEARING with the link opened after CUT), `test_vconv_echo_dropped`, `test_vconv_repeat_local`,
  `test_vconv_continue_local`, `test_vconv_paused_ends_ledger_zero` (every END reason and the
  silence TICK from a paused conversation: ledger zero), `test_vconv_play_sample_stops_speech`
  is not here (Task 13 feeds STOP for it).
- [ ] **Step 2: Run** — FAIL. **Step 3: Implement.** **Step 4: Run** — PASS.
- [ ] **Step 5: Commit** `feat(voice): pause, held replies, cut-in and local repeat in conversations`.

---

### Task 9: The chat side: send status, the round cap, "chat turn idle", cards and the reply

**Files:**
- Modify: `src/core/ai_prompt.[ch]` (`ai_rounds_*`), `src/ui/ai_chat_send.c`, `src/ui/ai_chat.c`,
  `src/ui/ai_chat_dispatch.c`, `src/ui/ai_chat_voice.c`, `src/ui/ai_chat.h` (mixed line
  endings), `src/ui/ai_chat_internal.h`, `src/ui/voice_win.h` (message numbers only)
- Test: `tests/test_ai_rounds*.c` or the file that tests `ai_rounds_user_message`

**Interfaces:**
- Consumes: `VoiceSendStatus`, `VoiceCardKind`, `voice_card_kind` (Task 7).
- Produces:
  - `void ai_rounds_on_send(AiSessionState *s, int voice_turn);` in `ai_prompt.c`: a typed send
    resets the auto-approve round counter as `ai_rounds_user_message` does; a voice turn does
    not. `ai_rounds_user_message` stays for Stop.
  - `VoiceSendStatus ai_chat_send_user_message_ex(AiChatData *d, int voice_turn);` in
    `ai_chat_send.c`; `ai_chat_send_user_message(d)` becomes `(void)ai_chat_send_user_message_ex(d, 0)`.
    Busy/dispatching → `VSS_BUSY`; no session → `VSS_NO_SESSION`; empty box → `VSS_EMPTY`; the
    full-conversation path → `VSS_FULL` (its chat line unchanged); a launch failure →
    `VSS_LAUNCH_FAILED` (`ai_chat_launch_stream_thread` returns `int`, 0 on failure). The
    tool-round reset (`ai_tool_rounds_reset`) happens for both kinds of turn.
  - In `ai_chat.h`/`ai_chat_voice.c`:
    - `VoiceSendStatus ai_chat_voice_send_turn(HWND panel, int voice_turn);`
    - `unsigned ai_chat_voice_msg_count(HWND panel);` — the conversation's message count, taken
      by the glue just before a send.
    - `int ai_chat_voice_reply(HWND panel, unsigned since, char **text, int *is_error);` — the
      displayed text (commands removed, as the chat shows it) of the last assistant message with
      index ≥ `since`, malloc'd into `*text` (caller `secure_zero`s and frees); returns 1 when it
      has non-whitespace prose. `*is_error` is 1 when the turn ended in an error reply (a
      per-session flag set on `WM_AI_RESPONSE`'s error path and cleared by
      `ai_chat_send_user_message_ex`).
    - `int ai_chat_voice_idle(HWND panel);` — `!busy_of(d)` and no card waiting for the user.
    - `void ai_chat_voice_cancel_work(HWND panel);` — exactly what the Stop branch of the
      Send/Stop handler does today (dispatch cancel with `user_acted`, or the stream cancel and
      its `[cancelled]` row).
    - `void ai_chat_voice_send_caption(HWND panel, int stop);` — Send shows Stop (1) or Send (0)
      while the chat is idle.
  - "Chat turn idle": a static `voice_turn_check(AiChatData *d)` in `ai_chat_voice.c`, called
    at every `ai_chat_voice_busy_changed` site, at the end of the displayed-session
    `WM_AI_RESPONSE` path (after `ai_chat_maybe_start_next_batch`), after `launch_failed`, after
    each card answer and after the stream cancel. When `ai_chat_voice_idle` holds it posts
    `WM_VOICE_TURN_IDLE` to the main window (never calls the controller directly). Posting when
    no conversation runs is harmless: the controller ignores it.
  - Cards: where a card becomes pending and needs the user (`reply_batch_build` after
    `cmd_batch_add` with `chat_approval_needs_user`, and `ai_chat_on_tool_decide`'s ASK path),
    post `WM_VOICE_CARD` with `wParam = voice_card_kind(tool name or NULL)`, once per card.
  - In the Send/Stop handler (`ai_chat.c`, the dispatch-cancel and busy-stop branches), after
    the cancel, call `voice_on_chat_stop()` (declared in `voice_win.h`, defined in Task 13; give
    it an empty definition in `voice_win.c` in this task).

- [ ] **Step 1: Write the failing native tests:** `test_ai_rounds_voice_turn_keeps_count` (three
  unattended rounds, then `ai_rounds_on_send(s, 1)`: the counter is unchanged and the cap still
  applies), `test_ai_rounds_typed_turn_resets` (`ai_rounds_on_send(s, 0)` resets),
  `test_ai_rounds_stop_resets` (unchanged behaviour of `ai_rounds_user_message`).
- [ ] **Step 2: Run** — FAIL. **Step 3: Implement** the core function, then the `src/ui`
  changes. **Step 4:** `make -j test` PASS and the MinGW objects for the touched `src/ui` files
  compile with no warnings (`make -j release` stops only at link if later tasks are missing;
  report what you ran).
- [ ] **Step 5: Commit** `feat(ai): send status, chat-turn-idle and card notices for voice turns`.

---

### Task 10: Session lock: delay-loaded wtsapi32, for dictation and conversation

**Files:**
- Create: `build-support/delayload/wtsapi32.def`, `src/ui/voice_lock_win.h`,
  `src/ui/voice_lock_win.c`, `tests/test_voice_lock_win.c`
- Modify: `Makefile` (CRLF: `DELAY_LIBS`, `WIN_ONLY_TESTS`, `WIN_TEST_SRCS`, and
  `-lwtsapi32` in `WIN_TEST_LDFLAGS`), `src/ui/window.c` (`WM_WTSSESSION_CHANGE`),
  `src/ui/voice_win.c` (dictation registers at start, unregisters when the recording ends;
  a lock is `VDE_END_SOFT`), `tests/win_runner.c`
- Test: `tests/test_voice_lock_win.c`

**Interfaces:**
- Produces:
```c
int  voice_lock_register(HWND main);      /* 0 unless voice_gate_on(); WTSRegisterSessionNotification
                                             (NOTIFY_FOR_THIS_SESSION); acquires VRES_SESSION_LOCK on
                                             success; idempotent */
void voice_lock_unregister(HWND main);    /* releases it; idempotent */
int  voice_lock_registered(void);
int  voice_lock_is_lock_event(WPARAM wp); /* WTS_SESSION_LOCK */
void voice_on_session_change(WPARAM wp);  /* in voice_win.c: routes a lock to the dictation
                                             recording (VDE_END_SOFT) or, from Task 13, the
                                             conversation (VCE_END VCX_LOCK) */
```
- `wtsapi32.def` lists only `WTSRegisterSessionNotification` and
  `WTSUnRegisterSessionNotification`; the delay-load hook records the DLL. Registration happens
  only when a recording starts, so an idle launch never asks for the DLL.

- [ ] **Step 1: Write the failing wintest:** `test_voice_lock_register_balances` (gate on: a
  hidden test window registers, the ledger count is 1, unregister → 0; a second register is a
  no-op; gate off: register returns 0 and the count stays 0; under Wine a refused registration
  is a defined outcome, logged as a skip of the count check, not a failure),
  `test_voice_lock_is_lock_event` (7 → 1, 8 → 0).
- [ ] **Step 2: Run** `xvfb-run -a make wintest 2>&1 | tail -5` — FAIL.
- [ ] **Step 3: Implement.** In `window.c`, `WM_WTSSESSION_CHANGE` calls
  `voice_on_session_change(wParam)` and falls through to `DefWindowProc`.
- [ ] **Step 4: Run** `make -j test`, `xvfb-run -a make wintest` — PASS; `make -j release`
  links.
- [ ] **Step 5: Commit** `feat(voice): a session lock ends a recording (delay-loaded wtsapi32)`.

---

### Task 11: Conversation capture on the audio thread: communications category, echo cancellation, the listener

**Files:**
- Modify: `src/ui/voice_audio_win.c`, `src/ui/voice_audio_win.h`, `src/core/voice_live.h` and
  `src/ui/voice_live_win.c` (a gate threshold on the target)
- Test: `tests/test_voice_audio_win.c`

**Interfaces:**
- Consumes: Task 3's `VoiceListen`.
- Produces:
```c
/* voice_audio_win.h */
int  voice_audio_conv_begin(const char *device_id_utf8, unsigned token, double gain_db,
                            int pause_ms);   /* comms category; posts VAM_CONV_OPENED */
void voice_audio_conv_mode(unsigned token, VoiceListenMode m);
void voice_audio_conv_attach_live(unsigned token, unsigned live_token);
void voice_audio_conv_end(unsigned token);  /* close the device, wipe the turn */
/* new VoiceAudioMsg values, lParam packs (value << 24) | (token & VOICE_TOKEN_MASK) unless noted */
VAM_CONV_OPENED   /* value = echo cancellation: 0 none, 1 on, 2 turned on by us */
VAM_CONV_EVENT    /* value = VoiceListenEvent */
VAM_CONV_TURN     /* lParam = VoiceCapture* of the finished turn (TURN_END/CAP); receiver frees */
/* VAM_LEVEL is posted every 50 ms during a conversation capture too (the mic pulse) */
/* voice_live.h: VoiceLiveTarget gains `int gate_rms;` — the live session's speech gate
   threshold; dictation keeps PCM_SPEECH_RMS, a conversation turn passes 0 (every window is
   speech: the listener already gated it, and the flush at feed end commits) */
```
- On the audio thread, `VC_CONV_BEGIN` opens the device as `va_open_dev` does (chosen endpoint,
  else the `eCapture`/`eConsole` default), but first `QueryInterface`s `IAudioClient2` and calls
  `SetClientProperties` with `AudioClientProperties{ sizeof, FALSE, AudioCategory_Communications,
  AUDCLNT_STREAMOPTIONS_NONE }` before `Initialize`; failure of that call is not fatal (no echo
  cancellation). After `Initialize`:
  - `GetService(IID_IAudioEffectsManager)`; when it works, find the effect whose id is
    `AUDIO_EFFECT_TYPE_ACOUSTIC_ECHO_CANCELLATION`; state on → 1; off with `canSetState` →
    `SetAudioEffectState(on)` → 2; anything else → 0. Free the effects array with
    `CoTaskMemFree`.
  - `GetService(IID_IAcousticEchoCancellationControl)`; when it works, call
    `SetEchoCancellationRenderEndpoint` with the default render endpoint's id
    (`GetDefaultAudioEndpoint(eRender, eConsole)` → `GetId`).
  - Our own speech's ducking opt-out (spec §7.3 / §16 item 8, done up front): the default render
    endpoint's `IAudioSessionManager2` → `GetAudioSessionControl(NULL, 0)` → `IAudioSessionControl2`
    → `SetDuckingPreference(TRUE)`. Failures are ignored.
  - All of these objects are released when the capture closes.
- Hand-copied declarations: `IAudioEffectsManager`, `AUDIO_EFFECT`, `AUDIO_EFFECT_STATE`,
  `IAcousticEchoCancellationControl` and their GUIDs, each inside
  `#ifndef __IAudioEffectsManager_INTERFACE_DEFINED__` (resp. the `AUDIO_EFFECT` struct with a
  feature guard, `__IAcousticEchoCancellationControl_INTERFACE_DEFINED__`), with a C vtable as
  `g_nc_vtbl` does. **Take each GUID and vtable order from Microsoft's documentation or SDK
  header text** (learn.microsoft.com `audioclient.h` pages: IAudioEffectsManager,
  IAcousticEchoCancellationControl, AUDIO_EFFECT_TYPE_ACOUSTIC_ECHO_CANCELLATION); write the
  source of each in a comment and in the report. A wrong GUID must fail safe (`E_NOINTERFACE` →
  no echo cancellation). Also copy `IID_IAudioClient2`, `IID_IAudioSessionManager2`,
  `IID_IAudioSessionControl2` as static consts beside the others (no libuuid).
- The captured samples (after gain) go to a `VoiceListen`; its sink posts `VAM_CONV_EVENT` for
  each event and appends turn audio to a per-turn `PcmBuf` (capped at 2 minutes plus the
  pre-roll). With a live token attached, turn audio so far is fed through `voice_live_feed` at
  attach time and every later window as it comes; at TURN_END/CAP the thread calls
  `voice_live_feed_end(live_token)` and posts `VAM_CONV_TURN` with the turn's capture; at
  TURN_ABANDON or a mode change the turn is wiped. `voice_audio_conv_mode` sets the listener
  mode (and wipes a turn when moving to HOLD).
- No 60 s/10 min recording cap applies to a conversation capture (the listener caps each turn).

- [ ] **Step 1: Write the failing wintests** (no device needed; add a TEST-ONLY seam
  `voice_audio_test_conv_ingest(token, samples, n)` like `voice_audio_test_ingest_live`):
  - `test_voice_audio_conv_begin_headless`: under Wine with no device, `voice_audio_conv_begin`
    gives either `VAM_CONV_OPENED` (with a value 0..2) or a `VAM_ERROR` for that token within
    5 s; then `voice_audio_conv_end`; no hang; `voice_audio_stop` joins.
  - `test_voice_audio_conv_turn_posted`: through the seam, a synthetic turn (speech then the
    pause) posts `VAM_CONV_EVENT(TURN_START)`, then `VAM_CONV_EVENT(TURN_END)` and a
    `VAM_CONV_TURN` whose sample count is the speech plus pre-roll plus the pause.
  - `test_voice_audio_conv_attach_live_feeds`: with `voice_audio_test_live_hooks` installed, an
    attach mid-turn first replays the turn so far, then streams, then `feed_end` once at the end.
  - `test_voice_audio_conv_hold_wipes`: mode HOLD mid-turn: no `VAM_CONV_TURN`.
  - The existing `voice_audio` and `voice_live_win` wintests still pass (dictation's gate
    threshold unchanged).
- [ ] **Step 2: Run** — FAIL. **Step 3: Implement.** **Step 4: Run** `make -j test`,
  `xvfb-run -a make wintest` — PASS; `make -j release` compiles the file clean.
- [ ] **Step 5: Commit** `feat(voice): conversation capture with echo cancellation and the listener`.

---

### Task 12: Windows speech (SAPI speech thread), the registry voice list and Play sample

**Files:**
- Create: `src/ui/voice_tts_win.h`, `src/ui/voice_tts_win.c`, `src/ui/voice_voices_win.h`,
  `src/ui/voice_voices_win.c`, `tests/test_voice_tts_win.c`
- Modify: `Makefile` (CRLF: `WIN_ONLY_TESTS`, `WIN_TEST_SRCS`), `tests/win_runner.c`
- Test: `tests/test_voice_tts_win.c`

**Interfaces:**
- Consumes: Task 4 (`voice_speak_chunks`), Task 6 (`VoiceInfo`, `voice_list_dedupe`).
- Produces:
```c
/* voice_tts_win.h */
#define WM_VOICE_TTS (WM_APP + 0x44)    /* lParam = VoiceTtsMsg*, receiver frees */
typedef enum { VTT_READY = 1, VTT_WORD, VTT_DONE, VTT_ERROR, VTT_EXITED } VoiceTtsKind;
typedef struct { unsigned token; VoiceTtsKind kind; unsigned reply_id;
                 size_t offset;   /* VTT_WORD: UTF-8 byte offset into the text given to speak */
                 long hr; } VoiceTtsMsg;
int  voice_tts_start(HWND notify, unsigned token, const char *voice_id); /* the one SpVoice
                     constructor: 0 unless voice_gate_on(); MTA thread; CoCreateInstance(CLSID_SpVoice)
                     noted as "sapi.dll" in dll_record; acquires VRES_SPVOICE; empty/unknown voice_id
                     = the system default voice */
void voice_tts_speak(unsigned token, unsigned reply_id, const char *utf8, size_t from);
                     /* purges what is playing, then speaks utf8[from..] in voice_speak_chunks order;
                        VTT_WORD offsets are into utf8 (absolute), VTT_DONE after the last chunk */
void voice_tts_stop(unsigned token);          /* SPF_PURGEBEFORESPEAK on an empty speak */
void voice_tts_volume(unsigned token, int percent);  /* ISpVoice::SetVolume, 0..100 */
void voice_tts_quit(unsigned token, unsigned wait_ms); /* stop, release SpVoice, join (bounded);
                     releases VRES_SPVOICE on the UI thread once joined (or detaches after wait_ms) */
int  voice_tts_running(void);
int  voice_tts_sample(HWND notify, const char *voice_id); /* Play sample: start (token
                     VOICE_TTS_SAMPLE_TOKEN), speak "Hello. This is how replies will sound.", quit
                     itself after VTT_DONE/VTT_ERROR; the UI reaps on VTT_EXITED */
int  voice_tts_selftest_hold(void);  /* --self-report control: create SpVoice and keep it */
void voice_tts_selftest_release(void);
#define VOICE_TTS_DUCK_PERCENT 18   /* about -15 dB */
/* voice_voices_win.h */
size_t voice_voices_read(HKEY root, const wchar_t *sapi_tokens, const wchar_t *onecore_tokens,
                         VoiceInfo *out, size_t max);  /* either path may be NULL */
size_t voice_voices_list(VoiceInfo *out, size_t max);  /* HKLM\SOFTWARE\Microsoft\Speech\Voices\Tokens
                         and HKLM\SOFTWARE\Microsoft\Speech_OneCore\Voices\Tokens, deduped */
```
- One speech thread per conversation (a Play sample is its own short-lived one). Commands go
  through a locked queue and an event; the thread waits on the queue event and SAPI's
  `SetNotifyWin32Event` handle, interest `SPEI_WORD_BOUNDARY | SPEI_END_INPUT_STREAM`. Word
  boundary `lParam` is the UTF-16 position in the current chunk: map it back to the UTF-8 byte
  offset in the caller's text (keep a per-chunk UTF-16→UTF-8 table). Every message carries the
  `reply_id` of its `voice_tts_speak`; a purge never posts `VTT_DONE` for the purged text.
- The voice: `CoCreateInstance(CLSID_SpObjectToken)` → `ISpObjectToken::SetId(NULL, id, FALSE)`
  → `ISpVoice::SetVoice`; on failure the default voice is kept and the report says so (the
  OneCore case is must-verify item 6).
- Registry read: each subkey of the token path is a voice; `id` = the root's name
  (`HKEY_LOCAL_MACHINE` or `HKEY_CURRENT_USER`) + `\` + the path + `\` + the subkey; `Name`,
  `Gender`, `Language` from its `Attributes` subkey (`name` falls back to the token's default
  value). Never loads `sapi.dll`.
- Use the MinGW `sapi.h` declarations; anything missing is hand-declared under its
  `__I..._INTERFACE_DEFINED__` guard.

- [ ] **Step 1: Write the failing wintests:** `test_voice_voices_read_hkcu` (the test writes two
  tokens under `HKCU\Software\NutshellTest\Speech\Voices\Tokens` and one OneCore duplicate
  under `...\Speech_OneCore\Voices\Tokens`; the read returns their names, genders, languages and
  ids starting `HKEY_CURRENT_USER\`; the test deletes its keys), `test_voice_tts_gate_off`
  (gate off: `voice_tts_start` returns 0, no thread, ledger 0), `test_voice_tts_start_quit_wine`
  (gate on: start, then quit within 3 s; a defined outcome under Wine — `VTT_READY` or
  `VTT_ERROR` — no hang, `VRES_SPVOICE` back to 0, `sapi.dll` in the DLL record only when
  start was called), `test_voice_tts_speak_events_wine` (if READY: speaking "One. Two." posts
  word offsets inside the text and one `VTT_DONE` with the same `reply_id`, or a defined skip
  when Wine has no voice).
- [ ] **Step 2: Run** — FAIL. **Step 3: Implement.** **Step 4: Run** `xvfb-run -a make wintest`
  and `make -j test` — PASS; `make -j release` compiles clean.
- [ ] **Step 5: Commit** `feat(voice): Windows speech thread (SAPI) and the registry voice list`.

---

### Task 13: The conversation controller: glue, modes, pausing, the notice and the mic button

**Files:**
- Create: `src/ui/voice_conv_win.h`, `src/ui/voice_conv_win.c`, `tests/test_voice_conv_win.c`
- Modify: `src/ui/voice_win.c` (mode routing; `voice_on_chat_stop`; `voice_on_session_change`;
  settings-digest end; feature off), `src/ui/voice_win.h`, `src/ui/window.c` (route
  `WM_VOICE_TTS`, `WM_VOICE_TURN_IDLE`, `WM_VOICE_CARD`, `WM_VOICE_NOTICE`,
  `VOICE_CONV_TIMER_ID`; `WM_ACTIVATEAPP`/`WM_ENABLE` both directions), `src/ui/ai_chat.c` (the
  undocked panel's `WM_ACTIVATEAPP`/`WM_ENABLE` both directions), `src/ui/ai_chat_voice.c` (the
  mic pulse following the level; the conversation cue), `src/core/notice.[ch]` (a new notice
  message id for the British-voice notice), `Makefile` (CRLF: `WIN_ONLY_TESTS`,
  `WIN_TEST_SRCS`), `tests/win_runner.c`
- Test: `tests/test_voice_conv_win.c`

**Interfaces:**
- Consumes: Tasks 5, 7, 8 (core), 9 (chat API), 10 (lock), 11 (capture), 12 (speech), PR 1's
  `voice_live_start/close/reap`, `voice_stt_submit`/`voice_stt_cancel*`, `ai_chat_voice_live_*`,
  `ai_chat_voice_lock_typing`, `ai_chat_voice_status`, `ai_chat_voice_set_ui`.
- Produces:
```c
/* voice_conv_win.h */
typedef struct {     /* every effect the glue has; the real table calls the modules above */
    int  (*cap_begin)(unsigned token);  void (*cap_end)(unsigned token);
    void (*cap_mode)(unsigned token, VoiceListenMode m);
    void (*cap_attach_live)(unsigned token, unsigned live_token);
    int  (*link_start)(unsigned live_token); void (*link_close)(unsigned live_token);
    int  (*stt_submit)(VoiceCapture *turn, unsigned token); void (*stt_cancel)(unsigned token);
    int  (*tts_start)(unsigned token);  void (*tts_speak)(unsigned token, unsigned id,
                                                          const char *text, size_t from);
    void (*tts_stop)(unsigned token);   void (*tts_volume)(unsigned token, int percent);
    void (*tts_quit)(unsigned token);
    int  (*lock_reg)(void);             void (*lock_unreg)(void);
    /* chat */
    VoiceSendStatus (*send_turn)(int voice_turn);
    unsigned (*msg_count)(void);
    int  (*reply)(unsigned since, char **text, int *is_error);
    int  (*chat_idle)(void);
    void (*cancel_work)(void);
    void (*note)(const char *text);     void (*header)(const char *text);
    void (*live_begin)(void); void (*live_replace)(const char *text);
    void (*drop_words)(void); void (*lock_typing)(int on);
    void (*send_caption)(int stop);     void (*mic_ui)(int on);
    int  (*box_has_text)(void);
    int  (*notice_voice)(const char *voice_name);   /* the once-per-launch notice */
} VoiceConvOps;
void voice_conv_set_ops(const VoiceConvOps *ops);   /* NULL restores the real table (tests) */
int  voice_conv_running(void);
void voice_conv_start(int live);                    /* from voice_win.c after the busy checks */
void voice_conv_event(VoiceConvEvent ev, int a, int b);  /* feed + carry out, with now = GetTickCount */
void voice_conv_on_audio(UINT kind, LPARAM lp);     /* VAM_CONV_* and VAM_LEVEL/VAM_ERROR for it */
void voice_conv_on_live(const VoiceLiveMsg *m);     /* WM_VOICE_LIVE for the turn's token */
void voice_conv_on_stt(VoiceSttResult *r);          /* WM_VOICE_STT for the turn's token */
void voice_conv_on_tts(const VoiceTtsMsg *m);
void voice_conv_on_timer(void);                     /* VOICE_CONV_TIMER_ID */
void voice_conv_on_activation(void);                /* recompute paused/active */
int  voice_conv_last_aec(void);                     /* -1 unknown, 0 none, 1 or 2 present */
int  voice_conv_typing_locked(void);
#define VOICE_CONV_TIMER_ID 0x56F2
```
- Routing in `voice_win.c`: when `s->voice_mode == VOICE_MODE_CONVERSATION`:
  - the key machine runs in Toggle mode whatever `voice_ptt_mode` says, with no Mic start delay;
    on `VA_OPEN` the glue at once calls `voice_ptt_capture_ended(&g_ptt, 0)`, so every clean
    press yields `VA_OPEN`: not running → the busy/no-session checks (`voice_busy_text`, header)
    then `voice_conv_start(voice_live_resolve(...))`; running → `VCE_PRESS`. The mic button the
    same. Chords and capitals behave as in dictation.
  - Escape with focus in the panel and a conversation running → `VCE_ESCAPE`, consumed; out of
    the panel it goes to the terminal.
  - `voice_send_pressed` → `VCE_SEND` in LISTENING/HEARING, `VCE_STOP` in SPEAKING (Send shows
    Stop then); `voice_on_chat_stop` → `VCE_STOP`.
  - `voice_feature_apply`: a changed digest → `VCE_END VCX_SETTINGS`; feature off → `VCE_END
    VCX_OFF` then the existing teardown (speech thread joined bounded 3 s).
  - Chat change (`WM_VOICE_CHAT` with a different binding) → `VCX_CHAT_CHANGED`; panel closed →
    `VCX_PANEL_CLOSED`; lock → `VCX_LOCK`; a capture `VAM_ERROR` → `VCX_DEVICE_LOST` with the
    existing device error note.
  - `WM_ACTIVATEAPP` and `WM_ENABLE` (both TRUE and FALSE) call `voice_on_activation()`, which
    keeps dictation's existing FALSE behaviour and, for a conversation, computes paused =
    `!IsWindowEnabled(g_main)` (dialog) or our process not owning the foreground window
    (background), feeding `VCE_PAUSE` or `VCE_RESUME` only on a change.
- The glue carries out each `VoiceConvOut` action through the ops table, sets the header and
  notes from the machine's texts, keeps the cue `Your text and what you say next will be sent
  after a pause.` when the box holds a draft in LISTENING, and arms `VOICE_CONV_TIMER_ID` from
  `voice_conv_next_deadline` (one-shot, re-armed after every step). In WORKING each poll feeds
  `VCE_IDLE_POLL(chat_idle(), prose|err<<1)` (prose and err from `reply(since, ...)`).
- Turn tokens: each turn gets a fresh live token (`next_token()`); `voice_conv_on_live` drops
  any message not for the current turn's token, as `voice_on_live` does. Live words go into the
  box with PR 1's `VoiceTranscript` and `ai_chat_voice_live_*` exactly as dictation does
  (draft, one space, committed text and tail). A batch turn's transcript (or a fallback's) is
  written after the draft the same way, replacing any live text of that turn, and is never sent
  by the delivery itself: only `SEND_TURN` sends. `VCE_LINK_FINAL` and `VCE_BATCH_OK` are followed
  by `VCE_TRANSCRIPT(voice_turn_classify(text, &ctx, now))`. A live turn whose link failed is
  transcribed from the `VAM_CONV_TURN` audio (the fallback); the glue keeps that audio until the
  transcript is final, then wipes it.
- Speech: the glue keeps the last reply's speech text (`voice_speak_text` of `reply()`), its
  `reply_id`, the cut-off (the last `VTT_WORD` offset for the current `reply_id`; stale ids are
  ignored), `read_to_end`, and the `VoiceReplyCtx` for classification (`spoken` = what was last
  spoken, reply or phrase). `SPEAK_REPLY` fetches and speaks from 0; `SPEAK_REPEAT` speaks the
  kept text from 0; `SPEAK_CONTINUE` from the cut-off (the held one after a pause);
  `SPEAK_PHRASE` speaks the phrase text with its own id. `DUCK`/`UNDUCK` set the volume to
  `VOICE_TTS_DUCK_PERCENT`/100. `MODE_SPEAK` picks `VLM_SPEAK_AEC` when the last
  `VAM_CONV_OPENED` reported 1 or 2, else `VLM_SPEAK_HD`. `VTT_DONE` for the current id →
  `VCE_SPEECH_DONE` (and `read_to_end = 1` for a reply); `VTT_ERROR` → `VCE_SPEECH_FAILED`.
- The British-voice notice: at the first `SPEAK_REPLY` of the launch, when the voice setting is
  Automatic and `voice_notice_due(...)`, the glue does not speak; it posts `WM_VOICE_NOTICE`,
  whose handler shows the notice (the shared notice dialog with the Global Constraints text,
  `<voice>` = the display name or "the Windows default voice"). The dialog disables the main
  window, so the conversation pauses and holds the reply (from 0), and speaks it on resume.
- The mic button keeps the recording look for the whole conversation; `VAM_LEVEL` during a
  conversation drives the pulse's amplitude (`ai_chat_voice_set_level(HWND panel, double frac)`,
  frac from `voice_meter_frac(voice_dbfs(peak))`, through the existing `ns_motion` pulse).
- `voice_conv_last_aec()` keeps the last conversation's report for the Settings page.

- [ ] **Step 1: Write the failing wintests** with a fake ops table that records calls and does
  the ledger acquire/release each real op would do (`cap_begin`/`cap_end` → `VRES_CAPTURE`,
  `link_start`/`link_close` → `VRES_LIVE_SESSION`, `tts_start`/`tts_quit` → `VRES_SPVOICE`,
  `lock_reg`/`lock_unreg` → `VRES_SESSION_LOCK`, `stt_submit`/result or cancel →
  `VRES_STT_UPLOAD`); events are fed through the glue's real entry points
  (`voice_conv_on_audio`, `_on_live`, `_on_tts`, `_on_timer`, `voice_conv_event`):
  - `test_vconv_win_ends_ledger_zero`: every end of spec §5.6 (key, Escape, Stop, panel closed,
    voice off, settings changed, lock, 3 minutes, chat changed, device lost), each from
    Listening, Working and Speaking: the ledger is all zero afterwards and the right note was
    written.
  - `test_vconv_win_activate_and_enable_pause`: a posted `WM_ACTIVATEAPP(FALSE)` to the test's
    main window pauses (the fake `cap_end` called, header text), `TRUE` resumes; `EnableWindow
    (FALSE)` on the main window pauses with the dialog header, `TRUE` resumes.
  - `test_vconv_win_full_turn`: a turn through live events and a reply through `reply()`: the
    fake `send_turn` is called with `voice_turn = 1`, `tts_speak` with the reply's speech text.
  - `test_vconv_win_stale_reply_events_ignored`: word events with an old `reply_id` after a new
    reply started do not move the cut-off; "carry on" then continues from the new reply's offset.
  - `test_vconv_win_notice_once`: Automatic resolved to a non-British voice: the first reply
    raises the notice (fake) and is not spoken until RESUME; the second reply is spoken at once.
  - `test_vconv_win_off_mid_reply`: voice switched off while Speaking: `tts_stop`, `tts_quit`,
    `cap_end` called, ledger zero.
- [ ] **Step 2: Run** — FAIL. **Step 3: Implement.** **Step 4: Run** `make -j test`,
  `xvfb-run -a make wintest`, `make -j release` — PASS, exe links.
- [ ] **Step 5: Commit** `feat(voice): conversation mode controller`.

---

### Task 14: Settings > Voice: Mode, Key mode, the Conversation section and Play sample

**Files:**
- Modify: `src/ui/settings.c`, `src/ui/resource.h` (new control ids after the highest
  `IDC_VOICE_*`; check the whole header for the next free number), the `--ui-demo`
  `settings-voice` state if it needs the new rows to show
- Test: the existing Settings wintests (if any cover the Voice page) plus a new
  `test_settings_voice_conv_rows` in the Voice page's wintest file, or a native test of any
  pure helper the page uses

**Interfaces:**
- Consumes: Task 1 (fields, help ids), Task 6 (`voice_choose_auto`, `voice_find_saved`,
  `voice_display_name`), Task 12 (`voice_voices_list`, `voice_tts_sample`), Task 13
  (`voice_conv_last_aec`).
- Rows, in order: Enable voice; **Mode** (combo Dictation / Conversation) directly under it;
  the existing rows; the push-to-talk "Mode:" row now labelled `Key mode:`; then after the
  Microphone rows a **Conversation** section header and: **Pause before sending** (an edit in
  seconds with one decimal, e.g. `1.2`, plus a dim `seconds`; Save clamps to 0.5..3.0 and
  stores ms); **Windows voice** (combo: first item `Automatic (<display name>)` or `Automatic
  (the Windows default voice)`, then each listed voice by display name; the saved voice selected
  by token id; when the saved id is not listed, Automatic is selected and the dim line `The
  saved voice is not installed on this PC: using Automatic` shows); the English (UK) line when
  no en-GB female voice is listed; **Play sample** button; the echo line when
  `voice_conv_last_aec() == 0`; the ducking line in Conversation mode.
- Greying (in `voice_enable_sync`): in Conversation mode Key mode, the Mic start delay and
  "Send automatically when I finish speaking" are disabled; in Dictation mode the whole
  Conversation section is disabled (Play sample too). The Mode combo's `CBN_SELCHANGE` calls
  `voice_enable_sync`.
- Save writes `voice_mode`, `voice_pause_ms`, `voice_windows_voice` (empty for Automatic) and
  `voice_windows_voice_name`.
- Play sample calls `voice_tts_sample(notify, chosen id)` and, during a conversation, first feeds
  the conversation `VCE_STOP` (spec §5.2). Opening the page never loads `sapi.dll`.

- [ ] **Step 1: Write the failing tests** for whatever pure logic the page adds (e.g. a
  `voice_pause_parse("1.2") == 1200`, `"0.3"` → 500, `"abc"` → keep, in `src/core/voice_config.c`
  with a native test), and a wintest that builds the Voice page (the existing Settings wintest
  pattern) and asserts the Mode combo's two items, the `Key mode:` label, and that switching
  Mode to Conversation disables the auto-send tick and enables Pause before sending.
- [ ] **Step 2: Run** — FAIL. **Step 3: Implement.** **Step 4: Run** `make -j test`,
  `xvfb-run -a make wintest`, `make -j release`; then `wine build/win/nutshell.exe
  --ui-demo=settings-voice` style check is optional (report if run).
- [ ] **Step 5: Commit** `feat(voice): Settings > Voice gains Mode and a Conversation section`.

---

### Task 15: The self-report: sapi.dll and wtsapi32.dll checks, and the SAPI control

**Files:**
- Modify: `tests/selftest/check.sh`, `Makefile` (CRLF: `selftest-wine` modes),
  `src/ui/window.c` (`--self-report-sapi` handling in `WM_SELF_REPORT`), the CLI parser that
  reads `--self-report` (find it), `tests/test_selftest_configs.c`
- Create: `tests/selftest/nutshell-voice-conv.config`

**Interfaces:**
- Consumes: Task 12's `voice_tts_selftest_hold`/`_release`.
- New `selftest-wine` modes:
  - `on` (existing, voice on in Dictation mode) additionally asserts no `requested:` and no
    `module:` line for `sapi.dll` or `wtsapi32.dll`;
  - `conv`: `nutshell-voice-conv.config` (voice on, `"voice_mode": "conversation"`), the same
    assertions plus `mmdevapi.dll` requested and `voice-net-opens: 0`;
  - `sapi`: the `conv` config with `--self-report-sapi`, which holds an `SpVoice` (through the
    one constructor) while the report is written; asserts `requested: sapi.dll` and `module:
    sapi.dll`. If Wine cannot create it, the report says `sapi-control: unavailable` and
    `check.sh` prints `SKIP (sapi): ...` and exits 0, and the Windows hand check carries the
    claim.
- `--self-report-sapi` does nothing without `--self-report`, and nothing with voice off.

- [ ] **Step 1:** extend `tests/test_selftest_configs.c` so it checks the new config parses with
  voice on and Conversation mode, and extend `check.sh` with the new assertions (they fail until
  the mode exists).
- [ ] **Step 2: Run** `make -j test` and `make selftest-wine` (needs the exe:
  `make -j release` first; it runs under the private `build/wineprefix`) — the new modes fail or
  are missing.
- [ ] **Step 3: Implement.** **Step 4: Run** both — PASS (or the defined SAPI skip, reported).
- [ ] **Step 5: Commit** `test(selftest): conversation mode loads no sapi or wtsapi32 until used`.

---

### Task 16: The WAV-injection seam and the harness cases (written, unrun)

**Files:**
- Modify: `src/ui/voice_audio_win.c` (code under `#ifdef NS_VOICE_WAV_SEAM` only), the CLI
  parser (the `--voice-wav=` flag under the same `#ifdef`), `Makefile` (CRLF: a `harness-exe`
  target building `build/win/nutshell-it.exe` with `-DNS_VOICE_WAV_SEAM`, its own object dir,
  never the release target), `.gitignore` (`build/win/nutshell-it.exe`),
  `tests/integration/README.md`
- Create: `tests/integration/cases/97-voice.ps1`
- Test: a native or wintest check that the release build does not define the seam (e.g.
  `nm`/`strings` in a Makefile check target, or a `#ifdef` static assert in a test compiled with
  the release flags) — builder's call, it must fail if the seam leaks into `nutshell.exe`.

**Interfaces:**
- In the seam build only, `--voice-wav=<path>` makes every capture (dictation and conversation)
  read a 16 kHz mono 16-bit PCM WAV in 20 ms chunks paced in real time, then silence, in place of
  WASAPI; it reports `VAM_CONV_OPENED` with 0. The release exe neither parses the flag nor
  contains the code.
- Harness cases (PowerShell, using the module's helpers; the fixture WAV is synthesised at run
  time with `System.Speech.Synthesis.SpeechSynthesizer.SetOutputToWaveFile` at 16 kHz mono 16-bit,
  so no binary fixture is committed): `voice_live_dictation_fixture` (live text appears in the
  box, nothing sent until Send) and `voice_conversation_card_fixture` ("list the files in my
  home directory": the turn is sent after the pause, the run_command card waits, the chat shows
  the approval announcement's header, and the self-report taken meanwhile shows no open
  capture), both in the `ai` tier (they need a key). Document them in the README's case list as
  written and unrun.

- [ ] **Step 1:** write the leak check; it passes for the release build and fails if
  `NS_VOICE_WAV_SEAM` is forced on.
- [ ] **Step 2: Implement** the seam and the target; `make harness-exe` builds; `make -j release`
  unchanged.
- [ ] **Step 3:** write the two cases and the README entry.
- [ ] **Step 4: Run** `make -j test`, `xvfb-run -a make wintest`, `make harness-exe`.
- [ ] **Step 5: Commit** `test(voice): a WAV-fed microphone for the harness, outside the release exe`.

---

### Task 17 (orchestrator): Spec findings, notes, version, build, full verification

- [ ] Record in spec §16 a new "Found at build time (PR 2, 2026-11-24)" part: each must-verify
  item PR 2 needs (6, 7, 8, 9, 10, 11, 12, 14, 15; plus 18, 19, 21-23, 27, 28) with what was
  found here, the fallback built and what is left to the Windows hand checks; the builder's calls
  (the detector's numbers, half-duplex echo estimate, dedupe rule, no speech rate, a held phrase,
  the GUID sources).
- [ ] Notes Open list: a "Conversation mode (PR 2 of live voice)" item with each Windows hand
  check of spec §14's PR 2 list on its own line, plus `mingw32-make test` and a release build on
  the Windows box, the SAPI control if it skipped under Wine, and the two unrun harness cases.
  Close notes item (u) (Win+L during a Toggle recording) as fixed in code, hand check pending.
- [ ] Version one above `origin/main`'s at build time; `make clean && make release`; commit the
  exe.
- [ ] Full verification: `make -j test`, `make test SAN=1`, `make lint-gate`, `make fuzz-run`,
  `xvfb-run -a make wintest`, `make selftest-wine`.
