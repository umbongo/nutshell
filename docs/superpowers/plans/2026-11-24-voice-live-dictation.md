# Live dictation (PR 1 of live voice) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Words stream into the AI panel's input box while the maintainer speaks (OpenAI and
Mistral), nothing is sent until Send, a failed live connection falls back to one batch
transcription, and every voice network request goes through one counted opener.

**Architecture:** Every decision that can be pure is in `src/core` with native tests: the
provider protocol (`voice_live.c`), the 16-to-24 kHz resampler (`voice_resample.c`), the
streaming speech gate (`voice_sgate.c`), the live-transcript buffer (`voice_transcript.c`), the
dictation recording controller and the resource ledger (`voice_dict.c`, `voice_ledger.c`).
`src/ui` is glue: a counted voice WinHTTP opener (`voice_net_win.c`), a WinHTTP WebSocket client
(`voice_ws_win.c`), a live session with a sender and a receiver thread fed by the audio thread
(`voice_live_win.c`), and the controller in `voice_win.c` driving `voice_dict` and writing into
the box through `ai_chat_voice.c`.

**Tech Stack:** C11, Win32 (MinGW cross-compile), WinHTTP WebSocket API (delay-loaded),
WASAPI on the existing audio thread, custom `test_framework.h`, Wine `wintest`.

**Spec:** `docs/superpowers/specs/2026-11-24-voice-live-conversation-design.md` (the "live
spec"), PR 1 scope in its §15. It builds on `2026-11-23-voice-input-output-design.md`.

## Global Constraints

- Worktree `/home/thomas/nutshell-wt/voice-live`, branch `feat/voice-live-dictation`. Never
  touch `~/nutshell` or another worktree.
- Version: the orchestrator bumps to one above `origin/main`'s at build time (v1.2.95 while
  main is v1.2.94) in the last task only. No implementer task edits `APP_VERSION`, README's
  `**Version**:` line or commits `build/win/nutshell.exe`.
- PR 1 scope only: no conversation mode, no playback, no `sapi.dll`, no `wtsapi32.dll`, no new
  `Mode: Dictation/Conversation` row, no session-lock code. Seams only where a task says so.
- Test first. A test that tells two states apart needs a fixture in which they differ.
- Line endings preserved: `src/config/loader.c`, `Makefile` and `src/ui/ai_chat.h` are CRLF
  (`ai_chat.h` mixed: keep each line's ending). Before each commit compare `git diff --stat` with
  `git diff --stat --ignore-cr-at-eol`; they must match.
- Portable logic in `src/core` (files in `src/ui` are not in the native test build). New
  `src/core/*.c` files are picked up by the Makefile's wildcard; new `tests/*.c` native tests are
  picked up too, and every test function is declared and called in `tests/runner.c`. A Win32-only
  test file is named `tests/*_win.c`, goes into `WIN_ONLY_TESTS` and its sources into
  `WIN_TEST_SRCS`, and is called from `tests/win_runner.c`.
- Both builds `-Werror`; MinGW adds `-Wpedantic -Wshadow -Wformat=2 -Wconversion`.
- Design-system rules (CLAUDE.md): no `RGB(` literal in `src/ui`; colours from `ns_tokens()`;
  `ns_scale`, `ns_font`, `ns_motion`, `ns_hover`; no new `MessageBox`.
- Optional-feature rule: with voice off, or voice on and idle, nothing added here opens a
  socket, starts a thread or counts a voice network open. One gate per entry point:
  `voice_net_open` is the only way voice reaches the network.
- Secrets and content: the API key is copied per live session and `secure_zero`ed; audio,
  transcripts and base64 audio are `secure_zero`ed before free and logged only through
  `DLOG_CONTENT`. Never log the key or a message body.
- Copy fixed by the spec (exact strings, U+2026 ellipsis as `\xE2\x80\xA6`):
  - header cue: `Listening… press <key> again to finish. Typing is paused.` (with auto-send on:
    `…again to send. Typing is paused.`; Hold mode: `Listening… release <key> to finish. Typing
    is paused.`). `<key>` is `voice_ptt_key_label()`.
  - header: `Live text is unavailable: the words will appear when you stop`
  - header: `Live text stopped: the rest will appear when you stop`
  - chat: `The recording could not be transcribed again: the text in the box is what arrived live`
  - chat: `The last words did not arrive. Check the text and press Send.`
  - Settings dim note: `Live text as you speak needs OpenAI or Mistral`
  - notice: `Voice: live speech-to-text model` — `the provider's default`
- Commits: stage by path, read every `??` line of `git status --short`, imperative subject, end
  with `Models: claude-opus-5-5 (orchestration, review); claude-sonnet-5-5 (implementation, task
  reviews)` then `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` and
  `Claude-Session: https://claude.ai/code/session_01AHuBtDU8oXSVxrKcPYYtQG`.
- Focused runs while iterating: `make -j test 2>&1 | tail -5`; `xvfb-run -a make wintest 2>&1 |
  tail -15` for Win32 changes (check `pgrep -a wine` before any `wineserver -k`). Run long jobs as
  background commands; never wait with `pgrep -f`, `wait` on a PID or `kill -0` loops.

## Wire protocol (verified 2026-11-24, recorded in the spec's §16 by Task 12)

**OpenAI** (`gpt-live-transcribe`, the default live model):
1. `wss://api.openai.com/v1/realtime?intent=transcription`, header `Authorization: Bearer <key>`.
   No `OpenAI-Beta` header; no `model` in the URL.
2. Wait for `session.created`, then send
   `{"type":"session.update","session":{"type":"transcription","audio":{"input":{"format":{"type":"audio/pcm","rate":24000},"transcription":{"model":"<model>","languages":["<iso>"],"prompt":"<hint>"},"turn_detection":null}}}}`.
   `languages` only when the language setting is not auto (never `language`); `prompt` only when
   `voice_lang_spelling_hint()` gives one (English (UK)). `delay` is **not** sent (the reference
   and the guide disagree on whether `gpt-live-transcribe` takes it; the server default is used).
3. Wait for `session.updated` (= ready). Then `{"type":"input_audio_buffer.append","audio":"<b64>"}`
   with 24 kHz s16le mono, 100 ms (2,400 samples, 4,800 bytes) per message, even byte count.
4. `{"type":"input_audio_buffer.commit"}` when the speech gate closes and at stop, only when audio
   was appended since the last commit (an empty commit is an error).
5. Events: `conversation.item.input_audio_transcription.delta` `{item_id, delta}` (provisional,
   per item); `...completed` `{item_id, transcript}` (replaces the item's text, final);
   `...failed` `{item_id, error.message}`; `input_audio_buffer.committed` `{item_id}`;
   `error` `{error.message}`. Everything else is ignored. Completions of different items may
   arrive out of order: key everything by `item_id`.
6. Final at stop = every committed item has completed or failed.

**Mistral** (`voxtral-mini-transcribe-realtime-3`):
1. `wss://api.mistral.ai/v1/audio/transcriptions/realtime?model=<model>`, `Authorization: Bearer
   <key>`. A wrong model fails the handshake with HTTP 403.
2. Wait for `session.created`, send
   `{"type":"session.update","session":{"audio_format":{"encoding":"pcm_s16le","sample_rate":16000},"target_streaming_delay_ms":480,"language":"<iso>"}}`
   (`language` omitted for auto). Wait for `session.updated` (= ready). Nothing may change after
   audio starts.
3. `{"type":"input_audio.append","audio":"<b64>"}` with 16 kHz s16le mono, 100 ms (1,600 samples)
   per message (the limit is 262,144 decoded bytes).
4. `{"type":"input_audio.flush"}` when the speech gate closes; at stop `input_audio.flush` then
   `{"type":"input_audio.end"}`.
5. Events: `transcription.text.delta` `{text}` (append-only, final on arrival, carries its own
   spaces); `transcription.done` `{text}` (the transcript of record: replaces the session's live
   text; the session is over, close it); `error` `{error.message}` (`message` may be an object:
   take its string form); `session.created`, `session.updated`; unknown types ignored.
6. One session per recording. Final at stop = `transcription.done`.

**WinHTTP WebSocket**: `WinHttpOpenRequest(GET)` → `WinHttpSetOption(req,
WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, NULL, 0)` → `WinHttpSendRequest` (headers carry the
bearer) → `WinHttpReceiveResponse` → status 101 → `WinHttpWebSocketCompleteUpgrade(req, 0)` →
close `req`. A receive may return a fragment (`..._FRAGMENT_BUFFER_TYPE`): loop until a whole
message. Send and Receive may run on two threads at once (one of each). Microsoft documents
`WinHttpWebSocketClose` as what cancels a pending receive (`ERROR_WINHTTP_OPERATION_CANCELLED`);
`WinHttpCloseHandle` waking a blocked receive is undocumented (Wine does it). Keepalive pings
default to 30 s (minimum 15 s). Close handshake timeout default 10 s
(`WINHTTP_OPTION_WEB_SOCKET_CLOSE_TIMEOUT`).

## Review Focus

1. **A provider that sends deltas for two items interleaved, or completes them out of order**
   (OpenAI says ordering is not guaranteed): the box must show the items in commit order, each
   item's provisional text replaced by its transcript, no text doubled or lost — Task 5 tests it.
2. **Non-ASCII text in the box** (an accented name, an emoji from a provider): every `EM_SETSEL`
   offset is in UTF-16 units, so a surrogate pair or a 3-byte UTF-8 character before the tail
   must not shift the replaced range — Task 5 tests offsets with `é` and U+1F600.
3. **The live connection drops between the stop press and the final text** (after stop, before
   `completed`/`done`): the fallback still runs on the kept audio and replaces the live text —
   Task 6 tests the order CAPTURED-then-LINK_FAILED and LINK_FAILED-then-CAPTURED.
4. **A recording that ends while the WebSocket is still in its handshake** (a quick tap, or
   Escape within 200 ms): nothing hangs, both workers exit, the ledger returns to zero — Task 9
   tests a cancel during connect against a loopback server that never answers the upgrade.
5. **A resampler fed in odd chunk sizes** (the audio thread delivers 10 ms packets of any length):
   output identical to one-shot resampling, no click at chunk boundaries — Task 3 tests it.

---

### Task 1: Escape only in the panel, a per-recording cap, help and shortcuts

**Files:**
- Modify: `src/core/voice_ptt.h`, `src/core/voice_ptt.c`, `src/core/voice_config.h`
- Modify: `src/core/settings_help.c` (`SH_VOICE_PTT_MODE` text), `src/core/shortcuts.c:71-74`,
  `README.md` (the generated shortcuts block between `<!-- shortcuts:begin -->` and
  `<!-- shortcuts:end -->`, regenerated with the recipe in `tests/test_shortcuts.c`)
- Test: `tests/test_voice_ptt.c`, `tests/runner.c`

**Interfaces:**
- Produces: `#define VOICE_LIVE_CAP_MS 600000` in `voice_config.h`; `VoicePtt` gains
  `long cap_ms` (set by `voice_ptt_init` to `VOICE_CAP_MS`); `void voice_ptt_set_cap(VoicePtt *p,
  long cap_ms)` (values `<= 0` mean `VOICE_CAP_MS`); `cap_reached` and
  `voice_ptt_next_deadline` use `p->cap_ms`.

- [ ] **Step 1: Write the failing tests** in `tests/test_voice_ptt.c`:
  - `test_voice_ptt_escape_out_of_panel_keeps_recording`: Toggle recording latched
    (`VS_REC_LATCHED`), `st(&p, VE_ESCAPE, t, /*in_panel*/0, 0)` returns `{VA_NONE, consume 0}` and
    `voice_ptt_recording(&p)` stays 1; the same in `VS_REC_HOLD` (Hold) and `VS_STOP_PRESS`; then
    `VE_ESCAPE` with `in_panel` 1 returns `{VA_DISCARD, 1}`.
  - `test_voice_ptt_escape_in_panel_unchanged`: the existing in-panel cases still pass (no edit).
  - `test_voice_ptt_live_cap`: after `voice_ptt_set_cap(&p, VOICE_LIVE_CAP_MS)` and an open at
    1000, a `VE_TICK` at `1000 + VOICE_CAP_MS + 5` returns `VA_NONE`, one at
    `1000 + VOICE_LIVE_CAP_MS` returns `VA_FINISH`; `voice_ptt_next_deadline(&p, 1000)` is
    `VOICE_LIVE_CAP_MS`; `voice_ptt_set_cap(&p, 0)` restores `VOICE_CAP_MS`.
  - In `tests/test_shortcuts.c` nothing new: its README comparison fails until Step 3's README
    regeneration.
- [ ] **Step 2: Run** `make -j test 2>&1 | tail -5`. Expected: the new tests FAIL.
- [ ] **Step 3: Implement.** Escape: only when `in_panel` does a recording discard and consume;
  out of the panel a recording returns `VA_NONE`, consume 0, state unchanged (the ARMED/TOGGLE_PRESS
  "spends the key" rule is unchanged in and out of the panel). `SH_VOICE_PTT_MODE` text: keep the
  first two sentences, replace the third with "A recording stops by itself after 10 minutes when
  the words appear as you speak (OpenAI and Mistral), and after 60 seconds otherwise. Escape
  throws it away, but only while the AI panel has the keyboard focus." The Escape shortcut row's
  condition becomes `"While recording, AI panel focused"`; regenerate README's block.
- [ ] **Step 4: Run** `make -j test 2>&1 | tail -5`. Expected: 0 failed.
- [ ] **Step 5: Commit** `feat(voice): Escape cancels a recording only in the AI panel; per-recording cap`.

### Task 2: The live model setting, the settings digest and the notice line

**Files:**
- Modify: `src/config/config.h:86-102` (after `voice_stt_model`), `src/core/voice_config.[ch]`,
  `src/config/loader.c` (CRLF: `k_setting_keys` after the `voice_stt_model` row, load after
  `:958-959`, save after `:1311-1312`), `src/core/settings_help.[ch]`
- Test: `tests/test_voice_config.c`, the notice tests (`grep -l notice_upgrade tests/*.c`),
  `tests/runner.c`

**Interfaces:**
- Produces: `char voice_stt_live_model[CFG_STR_MAX];` in `Settings` (empty = the provider's
  default); JSON key `voice_stt_live_model`; `SH_VOICE_STT_LIVE_MODEL` (label `"Live model:"`,
  `default_text` `"the provider's default"`); `k_setting_keys` row
  `CK("voice_stt_live_model", NULL, NULL, SH_VOICE_STT_LIVE_MODEL, "Voice: live speech-to-text model")`.
- Produces: `uint64_t voice_settings_digest(const Settings *s);` in `voice_config.h`: FNV-1a 64
  over every `voice_*` field, `stt_api_key`, and `ai_provider`/`ai_api_key`/`ai_custom_url` (the
  "Use current AI assistant" inputs). Used to tell "a voice setting changed" at a Settings save.

- [ ] **Step 1: Write the failing tests:**
  - `test_voice_live_model_default_and_roundtrip`: default `""`; set `"gpt-live-transcribe"`,
    save, load: equal; a value with CR/LF is rejected by validation like `voice_stt_model`
    (mirror the existing model test).
  - `test_voice_live_model_missing_key_reported`: a saved config without `voice_stt_live_model`
    reports it missing; `notice_upgrade` (voice switch present) has an item labelled
    `Voice: live speech-to-text model` with value `the provider's default`; with `voice_enabled`
    also missing, no such separate item (folded into "Voice").
  - `test_voice_settings_digest`: equal for two defaults; differs after changing each of
    `voice_stt_provider`, `voice_stt_live_model`, `voice_ptt_key`, `voice_input_device`,
    `stt_api_key`, `voice_stt_use_ai`, `ai_provider`; unchanged after changing a non-voice field
    (`font_size` or another plain field from `config.h`).
  - The existing `k_setting_keys`-against-a-saved-file test must cover the new row (it iterates
    the table; confirm it fails before the save code exists).
- [ ] **Step 2: Run** `make -j test 2>&1 | tail -5`. Expected: FAIL.
- [ ] **Step 3: Implement** field, default, validate (same rules as `voice_stt_model`), load, save,
  table row, help entry (body: "Live model:\n\nThe model used while the words appear as you speak,
  with OpenAI or Mistral. Shown only for those two providers. Left empty, OpenAI uses
  gpt-live-transcribe and Mistral voxtral-mini-transcribe-realtime-3. A typed name must match the
  provider's model id exactly.\nDefault: empty (the provider's default)."), digest. Recheck the
  spec §16 item 14 counts (`k_setting_keys` 57 before this row, `CFG_KEYS_MAX` 96,
  `NOTICE_ITEMS_MAX` 24) and report the numbers you find.
- [ ] **Step 4: Run** `make -j test 2>&1 | tail -5`. Expected: 0 failed. CRLF check on loader.c.
- [ ] **Step 5: Commit** `feat(voice): live speech-to-text model setting`.

### Task 3: The resampler and the streaming speech gate

**Files:**
- Create: `src/core/voice_resample.[ch]`, `src/core/voice_sgate.[ch]`
- Test: `tests/test_voice_resample.c`, `tests/test_voice_sgate.c`, `tests/runner.c`

**Interfaces:**
- Produces (`voice_resample.h`):
  ```c
  typedef struct { int16_t hist[VRS_TAPS]; unsigned phase; size_t nhist; } VoiceResampler;
  void   voice_rs_init(VoiceResampler *r);
  size_t voice_rs_out_max(size_t n_in);   /* worst-case output count for n_in input samples */
  size_t voice_rs_16_to_24(VoiceResampler *r, const int16_t *in, size_t n, int16_t *out, size_t cap);
  ```
  A streaming 3/2 polyphase FIR: 3 phases, `VRS_TAPS` = 16 taps per phase, a Kaiser-windowed
  sinc (beta about 6) with its cutoff at 7.6 kHz of the 48 kHz intermediate rate, coefficients as
  a `static const` Q15 table computed once (or precomputed in the source), accumulation in
  `int64_t`, rounded and saturated to int16. Output count over a stream is exactly
  `floor(total_in * 3 / 2)` once the filter delay is accounted for as the implementer defines it;
  chunking never changes the output.
- Produces (`voice_sgate.h`):
  ```c
  #define VSG_WIN 320            /* 20 ms at 16 kHz */
  #define VSG_PREROLL_MS 300
  #define VSG_OPEN_WINDOWS 2     /* 40 ms of speech opens the gate */
  typedef struct {
      void (*audio)(void *u, const int16_t *s, size_t n);  /* gated audio, in order */
      void (*closed)(void *u);                             /* the gate closed after its hangover */
      void *u;
  } VoiceSgateSink;
  typedef struct { /* pre-roll ring of 15 windows, partial window, state, counters */ ... } VoiceSgate;
  void voice_sgate_init(VoiceSgate *g, int threshold_rms, int hangover_ms);
  void voice_sgate_feed(VoiceSgate *g, const int16_t *s, size_t n, const VoiceSgateSink *k);
  void voice_sgate_flush(VoiceSgate *g, const VoiceSgateSink *k); /* stop: emit any partial window if open, then closed() if it was open */
  int  voice_sgate_open(const VoiceSgate *g);
  ```
  Per 20 ms window: speech if `pcm_rms(window) * 32768 >= threshold_rms`. Closed → open after
  `VSG_OPEN_WINDOWS` consecutive speech windows; on opening it emits the pre-roll ring (the 300 ms
  before, which contains those windows) and from then every window. Open → closed after
  `hangover_ms` of consecutive non-speech windows (those windows are emitted, then `closed()`).
  The hangovers are constants in `voice_live.h` (Task 4): `VOICE_LIVE_HANGOVER_OPENAI_MS 500`,
  `VOICE_LIVE_HANGOVER_MISTRAL_MS 800` (480 ms streaming delay plus a margin).

- [ ] **Step 1: Write the failing tests** (`tests/test_voice_resample.c`):
  - `test_voice_rs_length`: 16,000 samples in one call give 24,000 (± the documented delay) and
    `voice_rs_out_max(n) >= ` every real output.
  - `test_voice_rs_chunking_identical`: a 1 kHz tone of 4,801 samples fed in one call and in
    chunks of 1, 7, 160, 333 and the rest produce byte-identical output.
  - `test_voice_rs_dc`: input 1000 for 1 s: every output after the filter delay is 1000 ± 1.
  - `test_voice_rs_tone`: a 1 kHz sine at amplitude 16000: output zero crossings over the last
    0.5 s count 1,000 ± 2; output RMS within 0.5 dB of the input RMS.
  - `test_voice_rs_no_overflow`: a full-scale square wave (±32767, and -32768) never wraps:
    every output in `[-32768, 32767]` and the sign of each sample after the delay matches the
    square's (no wrap to the opposite sign).
  (`tests/test_voice_sgate.c`, with a sink that records emitted samples and `closed` calls):
  - `test_voice_sgate_silence_never_opens`: 2 s of RMS-100 noise with threshold 250: nothing
    emitted, no `closed`.
  - `test_voice_sgate_preroll`: 1 s of silence, then 500 ms speech (RMS 2000), then 1 s silence,
    hangover 500: emitted count = 300 ms pre-roll + 500 ms − the 40 ms already in the pre-roll
    + 500 ms hangover, in samples, ± one window; exactly one `closed`; the first emitted sample
    is the sample 300 ms before the gate opened.
  - `test_voice_sgate_click`: one 20 ms window of RMS 8000 in silence: never opens.
  - `test_voice_sgate_cough`: 100 ms of speech: opens, emits, closes once after the hangover.
  - `test_voice_sgate_flush`: open gate, `voice_sgate_flush`: the partial window is emitted and
    `closed` is called once; flush on a closed gate does nothing.
  - `test_voice_sgate_two_utterances`: two bursts 2 s apart: two `closed` calls, two pre-rolls.
- [ ] **Step 2: Run** `make -j test 2>&1 | tail -5`. Expected: FAIL (undefined symbols).
- [ ] **Step 3: Implement** both modules.
- [ ] **Step 4: Run** `make -j test 2>&1 | tail -5`. Expected: 0 failed.
- [ ] **Step 5: Commit** `feat(voice): 16-to-24 kHz resampler and streaming speech gate`.

### Task 4: The live provider protocol

**Files:**
- Create: `src/core/voice_live.[ch]`
- Test: `tests/test_voice_live.c`, `tests/runner.c`

**Interfaces:**
- Consumes: `voice_stt_resolve` / `SttTarget` (`src/core/voice_stt.h`), `voice_lang_iso`,
  `voice_lang_spelling_hint` (`voice_lang.h`), `base64_encode`, `json_escape_string`,
  `json_parse`/`json_obj_get`/`json_obj_str`/`json_free`.
- Produces:
  ```c
  typedef enum { VLP_NONE = 0, VLP_OPENAI, VLP_MISTRAL } VoiceLiveProvider;
  #define VOICE_LIVE_HANGOVER_OPENAI_MS 500
  #define VOICE_LIVE_HANGOVER_MISTRAL_MS 800
  #define VOICE_LIVE_CHUNK_MS 100
  typedef struct {
      VoiceLiveProvider prov;
      char host[128]; int port; int secure;      /* api.openai.com / api.mistral.ai, 443, 1 */
      char path[512];                             /* path and query */
      char model[CFG_STR_MAX];
      char iso[8];                                /* "" = auto */
      const char *prompt;                         /* static spelling hint or NULL (OpenAI only) */
      int key_from_chat;                          /* as SttTarget */
      unsigned rate;                              /* 24000 OpenAI, 16000 Mistral */
      int hangover_ms;
  } VoiceLiveTarget;
  VoiceLiveProvider voice_live_provider(const Settings *s);   /* effective STT provider, VLP_NONE when not live */
  int  voice_live_resolve(const Settings *s, VoiceLiveTarget *t);  /* 1 when live applies */
  const char *const *voice_live_models(VoiceLiveProvider p);  /* seed list, [0] the default */
  char *voice_live_msg_session(const VoiceLiveTarget *t, size_t *len);       /* heap JSON */
  char *voice_live_msg_audio(const VoiceLiveTarget *t, const int16_t *pcm, size_t n, size_t *len);
  char *voice_live_msg_gate_closed(const VoiceLiveTarget *t, size_t *len);   /* commit / flush */
  char *voice_live_msg_end(const VoiceLiveTarget *t, size_t *len);           /* NULL for OpenAI (commit is sent by the caller), flush+end as two calls for Mistral: see below */
  typedef enum { VLE_IGNORE = 0, VLE_CREATED, VLE_READY, VLE_DELTA, VLE_COMPLETED, VLE_COMMITTED,
                 VLE_ITEM_FAILED, VLE_DONE, VLE_ERROR, VLE_CLOSED } VoiceLiveEvKind;
  typedef struct {
      VoiceLiveEvKind kind;
      char item[64];      /* OpenAI item_id; "" for Mistral */
      char *text;         /* heap: delta / transcript / done text / error message; may be NULL */
  } VoiceLiveEvent;
  int  voice_live_parse(VoiceLiveProvider p, const char *json, size_t len, VoiceLiveEvent *ev);
  void voice_live_event_free(VoiceLiveEvent *ev);   /* secure_zero + free text */
  ```
  Mistral's end is two messages: the caller sends `voice_live_msg_gate_closed` (flush) and then
  `voice_live_msg_end` (`input_audio.end`). For OpenAI `voice_live_msg_end` returns NULL with
  `*len` 0 and the caller sends a commit only when audio is uncommitted. Models seeds: OpenAI
  `{"gpt-live-transcribe", "gpt-realtime-whisper", NULL}`; Mistral
  `{"voxtral-mini-transcribe-realtime-3", NULL}`. `voice_stt_live_model` overrides the default.
  `VLE_CREATED` = `session.created`, `VLE_READY` = `session.updated`. `voice_live_msg_audio`
  base64-encodes the samples as little-endian bytes (explicit byte order, not a memcpy cast).

- [ ] **Step 1: Write the failing tests** (exact JSON compared as strings where the builder fixes
  the order):
  - `test_voice_live_resolve`: provider openai → `VLP_OPENAI`, host `api.openai.com`, port 443,
    path `/v1/realtime?intent=transcription`, model `gpt-live-transcribe`, rate 24000; mistral →
    path `/v1/audio/transcriptions/realtime?model=voxtral-mini-transcribe-realtime-3`, rate 16000;
    groq, gemini, custom → 0 / `VLP_NONE`; "Use current AI assistant" with `ai_provider` openai →
    `VLP_OPENAI`, `key_from_chat` 1; `voice_stt_live_model` "gpt-realtime-whisper" → that model
    (and for Mistral it appears URL-escaped-safe in the query; a model with characters outside
    `[A-Za-z0-9._-]` makes resolve return 0).
  - `test_voice_live_session_openai`: en-GB → exactly
    `{"type":"session.update","session":{"type":"transcription","audio":{"input":{"format":{"type":"audio/pcm","rate":24000},"transcription":{"model":"gpt-live-transcribe","languages":["en"],"prompt":"<the en-GB hint, JSON-escaped>"},"turn_detection":null}}}}`;
    auto language → no `languages` key and no `prompt`; French → `"languages":["fr"]`, no prompt;
    never a `"language"` key; never `"delay"`.
  - `test_voice_live_session_mistral`: en-GB →
    `{"type":"session.update","session":{"audio_format":{"encoding":"pcm_s16le","sample_rate":16000},"target_streaming_delay_ms":480,"language":"en"}}`;
    auto → no `language`.
  - `test_voice_live_audio_msg`: samples `{1, -2, 0x1234}` → OpenAI
    `{"type":"input_audio_buffer.append","audio":"AQD+/zQS"}`, Mistral
    `{"type":"input_audio.append","audio":"AQD+/zQS"}`; n 0 → NULL.
  - `test_voice_live_gate_and_end_msgs`: OpenAI gate closed → `{"type":"input_audio_buffer.commit"}`,
    end → NULL; Mistral gate closed → `{"type":"input_audio.flush"}`, end → `{"type":"input_audio.end"}`.
  - `test_voice_live_escape`: a model or prompt with `"`, `\`, CR and LF is escaped by
    `json_escape_string` and the output parses with `json_parse`.
  - `test_voice_live_parse_openai`: each event above → its kind, `item`, `text`;
    `...delta` `{"item_id":"item_3","delta":"Hello,"}` → `VLE_DELTA`, "item_3", "Hello,";
    `...completed` → `VLE_COMPLETED` with the transcript; `...failed` → `VLE_ITEM_FAILED` with
    the message; `error` → `VLE_ERROR` with `error.message`; `conversation.item.created` →
    `VLE_IGNORE`; malformed JSON → returns 0 and `VLE_IGNORE`; a delta without `item_id` → 0.
  - `test_voice_live_parse_mistral`: `transcription.text.delta` `{"text":" world"}` →
    `VLE_DELTA` with " world" (spaces kept); `transcription.done` → `VLE_DONE` with its `text`;
    `error` with `"message":"bad"` and with `"message":{"detail":"x"}` → `VLE_ERROR` with a
    non-empty text; `transcription.language` → `VLE_IGNORE`; `session.updated` → `VLE_READY`.
- [ ] **Step 2: Run** `make -j test 2>&1 | tail -5`. Expected: FAIL.
- [ ] **Step 3: Implement** `voice_live.c`.
- [ ] **Step 4: Run** `make -j test 2>&1 | tail -5`. Expected: 0 failed.
- [ ] **Step 5: Commit** `feat(voice): OpenAI and Mistral live transcription protocol`.

### Task 5: The live-transcript buffer

**Files:**
- Create: `src/core/voice_transcript.[ch]`
- Test: `tests/test_voice_transcript.c`, `tests/runner.c`

**Interfaces:**
- Produces:
  ```c
  #define VT_SEG_MAX 32
  typedef struct {
      size_t draft_u16;         /* box length (UTF-16 units) at start */
      int    need_sep;          /* draft non-empty and not ending in whitespace */
      char  *committed;  size_t clen, ccap;   /* final text of finished segments, in order */
      struct { char id[64]; char *text; int done; } seg[VT_SEG_MAX];
      int    nseg;
      size_t shown_u16;         /* UTF-16 length, after draft_u16, of what the box holds now */
      size_t stable_u16;        /* UTF-16 length, after draft_u16, that will never change */
  } VoiceTranscript;
  void   vt_init(VoiceTranscript *t, size_t draft_u16, int need_sep);
  int    vt_delta(VoiceTranscript *t, const char *seg, const char *text, int append_only);
  int    vt_complete(VoiceTranscript *t, const char *seg, const char *final_text); /* NULL = keep the deltas */
  int    vt_replace_all(VoiceTranscript *t, const char *text);  /* Mistral done, batch fallback */
  char  *vt_render(VoiceTranscript *t, size_t *from_u16);       /* heap UTF-8 for [from, end); updates shown/stable */
  char  *vt_text(const VoiceTranscript *t);                     /* heap: the recording's whole text, no separator */
  int    vt_all_done(const VoiceTranscript *t);
  void   vt_free(VoiceTranscript *t);                            /* secure_zero */
  size_t vt_utf16_len(const char *utf8, size_t n);
  ```
  Segments are kept in first-seen order (OpenAI item order; Mistral uses one segment id `""`).
  `append_only` (Mistral) appends to the segment's text; otherwise (OpenAI) deltas accumulate per
  item as provisional text. `vt_complete` replaces the item's text with `final_text` and marks it
  done; leading done segments fold into `committed`. Joining: a single space is inserted between
  two pieces when neither side has whitespace at the join; the first piece gets the `need_sep`
  space; leading whitespace of the first piece is dropped when `need_sep` is 0 and the draft is
  empty; CR and LF in provider text become spaces. `vt_render` returns the text for the box range
  `[draft_u16 + stable_u16_before_this_update, end)` so the glue does
  `EM_SETSEL(from, -1)` + `EM_REPLACESEL`. Escape's removal is the glue replacing
  `[draft_u16, end)` with "" (no buffer call).

- [ ] **Step 1: Write the failing tests:**
  - `test_vt_draft_sep`: draft "hi" (`need_sep` 1): first delta "there" renders from 2 with
    " there"; draft "hi " (`need_sep` 0) → "there"; empty draft → "there".
  - `test_vt_openai_revision`: delta i1 "I sea", delta i1 " the" → render shows "I sea the";
    `vt_complete(i1, "I see the boat.")` → the box text after draft is "I see the boat."; a
    render after it has `from` = draft + that length (stable).
  - `test_vt_openai_interleaved`: deltas i1 "one", i2 "two", i1 " more"; complete i2 "Two." first,
    then i1 "One more." → final text "One more. Two."; while i1 is open, i2 is not folded into
    `committed`.
  - `test_vt_mistral_append`: deltas "Hello", " world", "." append-only → "Hello world."; then
    `vt_replace_all("Hello, world.")` → "Hello, world."; render from = draft (whole range).
  - `test_vt_utf16_offsets`: draft "Café 😀" (`vt_utf16_len` 7), delta "ok" → from 7, text " ok";
    `vt_utf16_len("é", 2)` is 1, of U+1F600's 4 bytes is 2.
  - `test_vt_newlines`: a delta "a\r\nb" renders "a  b" (each of CR, LF a space).
  - `test_vt_seg_overflow`: 33 distinct open items: the 33rd is folded rather than lost (its
    text appears once in `vt_text`; the implementer folds the oldest open segment into committed
    as final when the table is full).
  - `test_vt_free_wipes`: after `vt_free` every pointer is NULL and counts 0.
- [ ] **Step 2: Run**, expect FAIL. **Step 3: Implement.** **Step 4: Run**, expect 0 failed.
- [ ] **Step 5: Commit** `feat(voice): live transcript buffer with a revisable tail`.

### Task 6: The dictation controller and the resource ledger

**Files:**
- Create: `src/core/voice_ledger.[ch]`, `src/core/voice_dict.[ch]`
- Test: `tests/test_voice_dict.c`, `tests/runner.c`

**Interfaces:**
- Produces (`voice_ledger.h`), UI-thread-only by rule (each acquire and release happens on the UI
  thread at the call that creates or joins/closes the resource):
  ```c
  typedef enum { VRES_CAPTURE = 0, VRES_LIVE_SESSION, VRES_LIVE_THREAD, VRES_STT_UPLOAD,
                 VRES_SPVOICE, VRES_SPEECH_REQ, VRES_RENDER, VRES_SESSION_LOCK, VRES_COUNT } VoiceRes;
  void voice_ledger_reset(void);
  void voice_ledger_acquire(VoiceRes r);
  void voice_ledger_release(VoiceRes r);   /* below zero: clamps at 0 and sets the underflow flag */
  int  voice_ledger_count(VoiceRes r);
  int  voice_ledger_all_zero(void);
  int  voice_ledger_underflow(void);
  ```
  (The last four kinds are PR 2/3 seams: declared, unused in PR 1.)
- Produces (`voice_dict.h`), time injected:
  ```c
  typedef enum { VDS_IDLE = 0, VDS_RECORDING, VDS_FINISHING } VoiceDictState;
  typedef enum { VDL_NONE = 0, VDL_CONNECTING, VDL_READY, VDL_FAILED, VDL_DONE } VoiceDictLink;
  typedef enum {
      VDE_START,          /* a = live (1/0), b = auto_send allowed (auto_send on and box empty) */
      VDE_STOP,           /* key, mic button, the cap: finish */
      VDE_SEND,           /* Send or Enter while RECORDING or FINISHING */
      VDE_ESCAPE,         /* Escape in the panel while RECORDING */
      VDE_END_SOFT,       /* deactivate, modal, chat change, device lost: finish, never send */
      VDE_END_HARD,       /* voice/AI off, voice settings changed: close at once, never send */
      VDE_LINK_READY, VDE_LINK_FAILED,   /* a = 1: it never opened */
      VDE_LINK_FINAL,     /* the provider's text for this recording is complete */
      VDE_CAPTURED,       /* a = 1: audio kept and has speech; 0: none or silent */
      VDE_BATCH_OK, VDE_BATCH_FAIL,      /* the batch upload (batch recording or fallback) */
      VDE_TICK
  } VoiceDictEvent;
  enum {  /* action bits */
      VDA_OPEN_CAPTURE = 1u<<0, VDA_OPEN_LINK = 1u<<1, VDA_END_CAPTURE_KEEP = 1u<<2,
      VDA_END_CAPTURE_DISCARD = 1u<<3, VDA_CLOSE_LINK = 1u<<4, VDA_REMOVE_WORDS = 1u<<5,
      VDA_SUBMIT_BATCH = 1u<<6,   /* upload the kept audio: batch recording or fallback */
      VDA_DROP_AUDIO = 1u<<7, VDA_SEND_BOX = 1u<<8, VDA_LOCK_TYPING = 1u<<9,
      VDA_UNLOCK_TYPING = 1u<<10, VDA_CANCEL_BATCH = 1u<<11, VDA_DELIVER_BATCH = 1u<<12
  };
  typedef enum { VDT_NONE = 0, VDT_LIVE_UNAVAILABLE, VDT_LIVE_STOPPED, VDT_FALLBACK_FAILED,
                 VDT_FINAL_LATE, VDT_DIDNT_CATCH, VDT_TRANSCRIBING } VoiceDictText;
  typedef struct { unsigned actions; VoiceDictText text; } VoiceDictOut;
  typedef struct {
      VoiceDictState state; VoiceDictLink link;
      int live, auto_ok, send_after, no_send, have_audio, fallback, batch_running;
      unsigned long final_deadline;
  } VoiceDict;
  #define VOICE_DICT_FINAL_WAIT_MS 3000
  void voice_dict_init(VoiceDict *d);
  VoiceDictOut voice_dict_step(VoiceDict *d, VoiceDictEvent ev, int a, int b, unsigned long now);
  long voice_dict_next_deadline(const VoiceDict *d, unsigned long now);  /* -1 when none */
  const char *voice_dict_text(VoiceDictText t);   /* the fixed copy of Global Constraints */
  ```
  Rules (the spec's §4.3-§4.5, with the calls this plan makes):
  - START: `OPEN_CAPTURE | LOCK_TYPING`, plus `OPEN_LINK` when live; link CONNECTING.
  - STOP in RECORDING: `END_CAPTURE_KEEP`, FINISHING, deadline now + 3000 when live and the link
    is not FAILED. For live, the audio thread itself sends the gate flush and the end messages
    after the last samples (Task 9), so there is no separate "finish link" action.
  - SEND in RECORDING: as STOP plus `send_after`. SEND in FINISHING: sets `send_after`.
  - ESCAPE in RECORDING: `END_CAPTURE_DISCARD | CLOSE_LINK | REMOVE_WORDS | UNLOCK_TYPING`, IDLE.
  - END_SOFT in RECORDING: as STOP with `no_send`. For a batch recording END_SOFT is
    `END_CAPTURE_DISCARD | UNLOCK_TYPING` → IDLE (today's deactivate discard).
  - END_HARD anywhere but IDLE: `END_CAPTURE_DISCARD | CLOSE_LINK | CANCEL_BATCH | DROP_AUDIO |
    UNLOCK_TYPING`, IDLE; words in the box stay.
  - LINK_FAILED (a=1 never opened → text `LIVE_UNAVAILABLE`, else `LIVE_STOPPED`) while RECORDING
    or FINISHING before final: `CLOSE_LINK`, `fallback` = 1, link FAILED, deadline cleared.
  - LINK_FINAL in FINISHING: link DONE, `CLOSE_LINK | DROP_AUDIO` (when `have_audio`), then
    finish (below). LINK_FINAL in RECORDING (Mistral done early is not expected; treat as DONE and
    keep recording, later text goes to the fallback only if the link fails).
  - CAPTURED(a): `have_audio` = a. Batch recording: a=1 → `SUBMIT_BATCH`, text TRANSCRIBING;
    a=0 → text DIDNT_CATCH, `UNLOCK_TYPING`, IDLE. Live: if `fallback` and a=1 → `SUBMIT_BATCH`,
    text TRANSCRIBING; if link DONE → `DROP_AUDIO`; else keep it until LINK_FINAL or LINK_FAILED.
  - BATCH_OK: batch recording → `DELIVER_BATCH` (glue: `ai_chat_voice_deliver` with auto-send
    when `auto_ok && !no_send`, forced send when `send_after`), `DROP_AUDIO`; fallback →
    `DELIVER_BATCH` (glue: `vt_replace_all` + render, then send per the finish rule). Then finish.
    BATCH_FAIL: batch recording → finish with the glue's error status; fallback → text
    FALLBACK_FAILED, finish without sending.
  - TICK past the deadline in FINISHING (live, link not FAILED, no final): `CLOSE_LINK |
    DROP_AUDIO`; text FINAL_LATE when `send_after`; finish without sending.
  - Finish = `UNLOCK_TYPING`, IDLE, and `SEND_BOX` when (`send_after` or `auto_ok`) and not
    `no_send` and the final text arrived (live final, or the batch/fallback result).
  - Send pressed during a fallback waits for the fallback result (no 3 s limit: the upload's own
    timeouts bound it) — a builder's call recorded in the spec by Task 12.
  - Every event in a state where it means nothing returns no action.

- [ ] **Step 1: Write the failing tests** (`tests/test_voice_dict.c`). A helper `run(d, ev, a, b,
  now)` applies the actions to the ledger the way the glue will (`OPEN_CAPTURE` acquires
  `VRES_CAPTURE`, `END_CAPTURE_*` releases it, `OPEN_LINK` acquires `VRES_LIVE_SESSION`,
  `CLOSE_LINK` releases it when held, `SUBMIT_BATCH` acquires `VRES_STT_UPLOAD`,
  `BATCH_OK/FAIL` and `CANCEL_BATCH` release it). Each test ends with `ASSERT_TRUE(voice_ledger_all_zero())`
  and `ASSERT_EQ(voice_ledger_underflow(), 0)`:
  - `test_vdict_live_key_stop`: START(1,0), LINK_READY, STOP, CAPTURED(1), LINK_FINAL → IDLE, no
    SEND_BOX, DROP_AUDIO seen, UNLOCK_TYPING last.
  - `test_vdict_live_auto_send`: START(1,1) … LINK_FINAL → SEND_BOX.
  - `test_vdict_live_send_while_recording`: START(1,0), READY, SEND, LINK_FINAL → SEND_BOX;
    the same with no final and TICK at +3000 → no SEND_BOX, text FINAL_LATE, CLOSE_LINK.
  - `test_vdict_live_escape`: REMOVE_WORDS and END_CAPTURE_DISCARD; no SEND_BOX afterwards for any
    late LINK_FINAL.
  - `test_vdict_live_soft_end_never_sends`: START(1,1), END_SOFT, LINK_FINAL → no SEND_BOX.
  - `test_vdict_hard_end`: in RECORDING, in FINISHING with a fallback upload running (CANCEL_BATCH
    present) → IDLE, ledger zero.
  - `test_vdict_link_never_opened`: START(1,0), LINK_FAILED(1) → text LIVE_UNAVAILABLE; STOP,
    CAPTURED(1) → SUBMIT_BATCH; BATCH_OK → DELIVER_BATCH, IDLE.
  - `test_vdict_link_drops_after_stop_either_order`: STOP, CAPTURED(1), LINK_FAILED(0) →
    SUBMIT_BATCH; and STOP, LINK_FAILED(0), CAPTURED(1) → SUBMIT_BATCH.
  - `test_vdict_fallback_fails`: … BATCH_FAIL → text FALLBACK_FAILED, no SEND_BOX even with
    `send_after`.
  - `test_vdict_send_during_fallback`: SEND then BATCH_OK → SEND_BOX.
  - `test_vdict_batch_recording`: START(0,0), STOP, CAPTURED(1) → SUBMIT_BATCH, BATCH_OK →
    DELIVER_BATCH; CAPTURED(0) → DIDNT_CATCH; END_SOFT while recording → END_CAPTURE_DISCARD
    (today's behaviour).
  - `test_vdict_deadline`: `voice_dict_next_deadline` is -1 when idle, 3000 right after a live
    STOP, -1 after LINK_FAILED.
  - `test_voice_ledger_counts`: acquire twice, release once → 1; release past zero sets
    underflow and stays 0; reset clears both.
- [ ] **Step 2: Run**, expect FAIL. **Step 3: Implement.** **Step 4: Run**, expect 0 failed.
- [ ] **Step 5: Commit** `feat(voice): dictation controller state machine and resource ledger`.

### Task 7: The voice network opener, the 64 KB transcript, upload timeouts, the self-report

**Files:**
- Create: `src/ui/voice_net_win.[ch]`
- Modify: `src/core/ai_http.h`, `src/ui/ai_http_win.c` (and the non-Windows stub in
  `src/core/ai_http.c` if `ai_http_post_ct` has one), `src/ui/voice_stt_win.[ch]`,
  `src/core/voice_stt.[ch]`, `src/ui/voice_win.c` (`voice_on_stt` reads the heap text),
  `src/ui/dll_hook_win.c` (report line), `tests/selftest/check.sh`, `Makefile` (`WIN_TEST_SRCS`)
- Test: `tests/test_voice_stt.c`, `tests/test_voice_stt_win.c`, `tests/test_voice_net_win.c`
  (new, Win32), `tests/win_runner.c`, `tests/runner.c`

**Interfaces:**
- Produces (`voice_net_win.h`):
  ```c
  typedef enum { VNK_STT_UPLOAD = 1, VNK_LIVE_WS = 2, VNK_TTS = 3 } VoiceNetKind;  /* VNK_TTS: PR 3 seam */
  HINTERNET voice_winhttp_open(void);         /* voice_gate_on() or NULL + ERROR_ACCESS_DENIED; counts; ns_winhttp_open(L"Nutshell/1.0") */
  HINTERNET voice_net_open(VoiceNetKind k);   /* the one voice entry point: gate check, DLOG of the kind (no content), voice_winhttp_open */
  long      voice_net_open_count(void);       /* InterlockedCompareExchange read */
  void      voice_net_test_reset_count(void); /* TEST-ONLY seam */
  ```
- Produces (`ai_http.h`): `int ai_http_post_ct_ex(void *session, const char *url, const char *
  const *headers, const char *content_type, const char *body, size_t body_len, int send_ms, int
  recv_ms, AiHttpResponse *resp, AiNetCancel *nc);` — uses the caller's WinHTTP session (never
  opens or closes one) with resolve 10 s, connect 10 s and the given send/receive timeouts.
  `ai_http_post_ct` is unchanged for its other callers.
- Produces (`voice_stt.h`): `#define VOICE_STT_TEXT_MIN 65536`;
  `void voice_stt_upload_timeouts(size_t wav_len, int *send_ms, int *recv_ms);` — send
  `30000 + 1000 * (wav_len / 65536)`, receive `60000 + 1000 * (seconds of audio / 10)` where
  seconds = `(wav_len - 44) / 32000`; both capped at 600000.
- Produces (`voice_stt_win.h`): `VoiceSttResult` becomes
  `{ unsigned token; int ok; char *text; size_t text_cap; char err[300]; }` with
  `text_cap >= VOICE_STT_TEXT_MIN` allocated at submit; `void voice_stt_result_free(VoiceSttResult *r);`
  (wipes text and record). `voice_stt_submit` gains `int kind_fallback` only if the implementer
  needs it for logging; otherwise unchanged.
- The self-report gains one line, `voice-net-opens: N`, after the DLL record (written by
  `ns_self_report_write`). `check.sh`: every mode asserts the line exists and, for `off`,
  `voiceoff` and `on` (an idle launch), that N is 0.

- [ ] **Step 1: Write the failing tests:**
  - native `test_voice_stt_upload_timeouts`: 1 s WAV (32,044 bytes) → send 30000, recv 60000;
    a 10-minute WAV (19,200,044) → send 30000 + 292,000 = 322000, recv 60000 + 60000 = 120000;
    a 2 GB length → both 600000.
  - native `test_voice_stt_parse_large`: a JSON `{"text":"<70,000 chars>"}` parsed into a
    `VOICE_STT_TEXT_MIN + 8192` buffer keeps all 70,000; into 4,096 it truncates without overflow.
  - wintest `test_voice_net_counts`: with the voice gate off `voice_net_open(VNK_STT_UPLOAD)`
    returns NULL and the count stays 0; gate on: two opens → count 2 (close the handles);
    `ai_http_post` (the chat path) to `http://127.0.0.1:1/` leaves the count unchanged.
  - wintest (in `test_voice_stt_win.c`, extending its local HTTP server fixture): one upload
    increments `voice_net_open_count()` by exactly 1 (2 with the 400 retry); a result with a
    5,000-character transcript is delivered whole (the old 4,096 limit would truncate).
- [ ] **Step 2: Run** native and wintest, expect FAIL.
- [ ] **Step 3: Implement.** `stt_thread` opens its session with `voice_net_open(VNK_STT_UPLOAD)`
  (failure → "Voice input is turned off." when the gate is off, else the WinHTTP error text), sets
  timeouts from `voice_stt_upload_timeouts`, calls `ai_http_post_ct_ex` for the first try and the
  retry, closes the session at the end. Add `src/ui/voice_net_win.c` and the new test to
  `WIN_TEST_SRCS`/`WIN_ONLY_TESTS`.
- [ ] **Step 4: Run** `make -j test`, `xvfb-run -a make wintest`, and `make release` in a scratch
  copy is not needed here; run `make selftest-wine` only if a release exe already exists, else
  leave it to Task 12. Expected: 0 failed.
- [ ] **Step 5: Commit** `feat(voice): one counted network opener for voice; 64 KB transcripts`.

### Task 8: The WebSocket client

**Files:**
- Create: `src/ui/voice_ws_win.[ch]`
- Modify: `build-support/delayload/winhttp.def` (add `WinHttpWebSocketClose`,
  `WinHttpWebSocketCompleteUpgrade`, `WinHttpWebSocketQueryCloseStatus`,
  `WinHttpWebSocketReceive`, `WinHttpWebSocketSend`, `WinHttpWebSocketShutdown` if used;
  update the comment naming the voice files), `Makefile` (`WIN_TEST_SRCS`, `WIN_ONLY_TESTS`)
- Test: `tests/test_voice_ws_win.c` (new), `tests/ws_loopback_win.[ch]` (new test fixture: a
  loopback WebSocket server), `tests/win_runner.c`

**Interfaces:**
- Consumes: `voice_net_open(VNK_LIVE_WS)` (Task 7), `AiNetCancel` (`ai_netcancel.h`).
- Produces (`voice_ws_win.h`):
  ```c
  typedef struct VoiceWs VoiceWs;
  /* Blocks: session from voice_net_open(VNK_LIVE_WS), connect, GET with the upgrade option and
   * the extra headers, 101 check, CompleteUpgrade. The request handle is armed on nc during the
   * handshake, so ai_nc_fire from another thread aborts it. NULL with err (and *http_status when
   * the server answered something other than 101). */
  VoiceWs *voice_ws_connect(const char *host, int port, int secure, const char *path,
                            const char *const *headers, AiNetCancel *nc,
                            int *http_status, char *err, size_t errcap);
  int  voice_ws_send_text(VoiceWs *ws, const char *s, size_t n);          /* 0 ok */
  /* One whole message (fragments joined), heap, NUL-terminated; *is_close set on a close frame.
   * Messages over 1 MiB fail. 0 ok, -1 error or cancelled. */
  int  voice_ws_recv(VoiceWs *ws, char **msg, size_t *len, int *is_close);
  /* Cancel from any thread: WinHttpWebSocketClose with WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS
   * (close timeout set to 2000 ms at connect), which ends a pending receive. Idempotent. */
  void voice_ws_shutdown(VoiceWs *ws);
  /* Last resort, UI thread: WinHttpCloseHandle on the WebSocket handle (through the AiNetCancel
   * token it is armed on after the upgrade), waking a send or receive the close did not. */
  void voice_ws_abort(VoiceWs *ws);
  void voice_ws_free(VoiceWs *ws);   /* after both workers are joined: closes what is left */
  ```
  The WebSocket handle is armed on the same `AiNetCancel` after the upgrade, so exactly one side
  ever closes it (the `ai_netcancel.h` contract). Keepalive interval left at the system default
  (30 s). The receive timeout on the WebSocket handle is set to 0 (no timeout) after the upgrade;
  if the option is refused the error is ignored (spec §16 item 1; recorded by Task 12).
- The loopback fixture (`tests/ws_loopback_win.[ch]`): `WsLoop *ws_loop_start(int mode)`,
  `int ws_loop_port(WsLoop *)`, `void ws_loop_stop(WsLoop *)`, plus a script hook for Task 9:
  `void ws_loop_set_script(WsLoop *, int (*on_msg)(WsLoop *, const char *msg, size_t n, void *u), void *u)`
  and `int ws_loop_send_text(WsLoop *, const char *s)`. It accepts one connection on 127.0.0.1,
  answers the upgrade (Sec-WebSocket-Accept via OpenSSL `SHA1`, already linked in wintest, and
  `base64_encode`), unmasks client frames, sends unmasked server frames, answers a close frame.
  Modes: `WSL_ECHO`, `WSL_SILENT` (upgrades, then never sends), `WSL_NO_UPGRADE` (accepts TCP,
  never answers), `WSL_REJECT_403`.

- [ ] **Step 1: Write the failing tests** (`tests/test_voice_ws_win.c`, each with the voice and AI
  gates on, and with `ai_gate`/`voice_gate` off for the first):
  - `test_voice_ws_gate_off`: gates off → `voice_ws_connect` NULL, count unchanged.
  - `test_voice_ws_echo`: `WSL_ECHO`: connect with header `Authorization: Bearer test`, the
    fixture sees that header; send `{"a":1}` → receive the same text; `voice_net_open_count()`
    rose by 1.
  - `test_voice_ws_fragmented`: the fixture sends a 200 KB message in 3 fragments → one message
    of the right length.
  - `test_voice_ws_reject`: `WSL_REJECT_403` → NULL, `*http_status` 403.
  - `test_voice_ws_shutdown_wakes_receiver`: `WSL_SILENT`, a thread blocked in `voice_ws_recv`;
    `voice_ws_shutdown` from the test thread → the receive returns -1 within 3 s. If Wine's
    WinHTTP does not return, `voice_ws_abort` is tried; if that does not either, the test prints
    `SKIP: wine does not wake a blocked WebSocket receive` and passes (like
    `test_ai_nc_fire_wakes_blocked_winhttp_call`).
  - `test_voice_ws_cancel_during_handshake`: `WSL_NO_UPGRADE`, connect on a thread, `ai_nc_fire`
    after 200 ms → connect returns NULL within 3 s.
  - `test_voice_ws_send_and_receive_concurrently`: `WSL_ECHO`, a receiver thread loops while the
    test thread sends 50 messages → all 50 come back in order.
- [ ] **Step 2: Run** `xvfb-run -a make wintest 2>&1 | tail -15`. Expected: FAIL (build).
- [ ] **Step 3: Implement** client, fixture, def entries.
- [ ] **Step 4: Run** wintest, then `make clean && make release` in the worktree to prove the
  delay-load def links (then `git restore build/win/nutshell.exe` — the exe is committed only in
  Task 12). Expected: wintest 0 failed; the release links.
- [ ] **Step 5: Commit** `feat(voice): WinHTTP WebSocket client, delay-loaded`.

### Task 9: The live session and the audio thread's feed

**Files:**
- Create: `src/ui/voice_live_win.[ch]`
- Modify: `src/ui/voice_audio_win.[ch]` (`VC_BEGIN` carries `cap_ms` and `live`; live captures
  feed the session; the end marker), `Makefile` (`WIN_TEST_SRCS`)
- Test: `tests/test_voice_live_win.c` (new), `tests/test_voice_audio_win.c`, `tests/win_runner.c`

**Interfaces:**
- Consumes: `voice_live_*` (Task 4), `voice_rs_*`, `voice_sgate_*` (Task 3), `voice_ws_*`
  (Task 8), `voice_ledger_*` (Task 6).
- Produces (`voice_live_win.h`):
  ```c
  #define WM_VOICE_LIVE (WM_APP + 0x42)   /* lParam = VoiceLiveMsg*, receiver frees with voice_live_msg_free */
  typedef struct { unsigned token; VoiceLiveEvent ev; } VoiceLiveMsg;   /* ev.kind VLE_CLOSED when the link ended without a final */
  void voice_live_msg_free(VoiceLiveMsg *m);
  /* UI thread. Starts a session for recording `token` on target t with the key (copied, wiped at
   * the end): one receiver thread (connects, waits for session.created, hands the session message
   * to the sender, posts VLE_READY on session.updated, then posts every parsed event) and one
   * sender thread (drains the feed, sends audio chunks of VOICE_LIVE_CHUNK_MS once ready, the
   * gate-closed and end messages in order). Acquires VRES_LIVE_SESSION and 2 x VRES_LIVE_THREAD.
   * 0 with err when it cannot start. */
  int  voice_live_start(HWND notify, const VoiceLiveTarget *t, const char *key, unsigned token,
                        char *err, size_t errcap);
  /* Audio thread, any token: ignored unless it is the running session's. */
  void voice_live_feed(unsigned token, const int16_t *s, size_t n);
  void voice_live_feed_end(unsigned token);   /* after the last samples: gate flush, then the end message(s) */
  /* UI thread: ask the current session to end (shutdown its WebSocket, wake the sender);
   * returns at once. The session's threads are joined by voice_live_reap. Releases
   * VRES_LIVE_SESSION. */
  void voice_live_close(unsigned token);
  /* UI thread: join finished sessions (wait_ms 0: only those already done). At voice off and
   * shutdown wait_ms is 3000: still running → voice_ws_abort, then detached like the STT jobs.
   * Releases VRES_LIVE_THREAD per joined (or detached) thread. */
  void voice_live_reap(DWORD wait_ms);
  ```
  The feed: a session-owned locked queue (critical section + event). `voice_live_feed` runs the
  speech gate (threshold `PCM_SPEECH_RMS`, the target's hangover) under the lock; gated audio is
  resampled to the target rate (OpenAI) and appended to the queue; the gate's `closed` callback
  queues a "gate closed" marker. `voice_live_feed_end` flushes the gate and queues the end
  marker. The sender turns markers into `voice_live_msg_gate_closed` / end messages, sends a
  commit (OpenAI) at end only when audio is uncommitted, and holds everything until ready.
  The receiver tracks finality per provider (OpenAI: once the end marker was sent, the number of
  `completed` plus `failed` events equals the number of commits the sender sent — counted with
  an interlocked counter, not from `committed` events, which may lag — and zero commits means
  final at once; Mistral: `VLE_DONE`) and posts one `VLE_DONE`-kind
  "final" message (`ev.kind = VLE_DONE`, text NULL for OpenAI) when it is reached; an error,
  a close or a receive failure before that posts `VLE_CLOSED`. A session that ended posts nothing
  more. Each thread posts nothing after `voice_live_close`.
- Audio thread: `voice_audio_capture_begin(id, token, test_only, gain_db, cap_ms, live)` (all
  callers updated); `VA_CAP` from `cap_ms` (16 kHz samples); for a live capture `va_ingest` calls
  `voice_live_feed(token, fresh, take)` after the gain, and `va_finish` (keep or not, and the cap)
  calls `voice_live_feed_end(token)` before posting `VAM_CAPTURED`.

- [ ] **Step 1: Write the failing tests** (`tests/test_voice_live_win.c`, against the Task 8
  fixture scripted as a fake provider; targets built by hand with host 127.0.0.1, `secure` 0):
  - `test_voice_live_openai_flow`: the script answers `session.created`, then on the
    `session.update` checks `"rate":24000` and `"turn_detection":null` and answers
    `session.updated`; on appends it counts decoded bytes; on commit it sends a delta and a
    `completed` for `item_1`. The test feeds 1 s of 16 kHz speech (RMS 2000) and 1 s of silence,
    then `voice_live_feed_end`. Asserts (pumping messages until done, 5 s limit): the fixture
    received at least 24,000 × 2 × 1.3 bytes of appended audio (1 s plus pre-roll and hangover at
    24 kHz), exactly one commit, the UI got `VLE_READY`, `VLE_DELTA`, `VLE_COMPLETED`, then the
    final message; no append arrived before `session.updated`.
  - `test_voice_live_mistral_flow`: the same with Mistral messages: `input_audio.flush` at the
    gate close, `input_audio.flush` + `input_audio.end` at the end, the script answers
    `transcription.text.delta` and `transcription.done`; appended bytes at 16 kHz.
  - `test_voice_live_close_mid_handshake`: `WSL_NO_UPGRADE`, `voice_live_close` after 100 ms,
    `voice_live_reap(3000)` → ledger all zero, no message posted after the close.
  - `test_voice_live_server_drops`: the script closes the socket after `session.updated` →
    a `VLE_CLOSED` message arrives.
  - `test_voice_live_stale_token_feed`: `voice_live_feed` with another token adds nothing.
  - In `tests/test_voice_audio_win.c`, extend the ingest seam test: a live capture with a fake
    feed records that `voice_live_feed` received the gained samples and `voice_live_feed_end`
    was called once at finish (add a TEST-ONLY hook pointer if needed, NULL in product code).
- [ ] **Step 2: Run** wintest, expect FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** `make -j test` and `xvfb-run -a make wintest`. Expected: 0 failed.
- [ ] **Step 5: Commit** `feat(voice): live transcription session fed by the audio thread`.

### Task 10: The controller: live words in the box

**Files:**
- Modify: `src/ui/voice_win.[ch]`, `src/ui/ai_chat_voice.c`, `src/ui/ai_chat.h` (CRLF/mixed),
  `src/ui/ai_chat_internal.h`, `src/ui/ai_chat_input.c`, `src/ui/ai_chat.c` (Send handler and the
  three chat-change sites at `:808`, `:2685`, `:2906`), `src/ui/window.c` (WndProc routing)
- Test: `tests/test_voice_ptt_win.c` (extend), `tests/runner.c` if a native helper is added

**Interfaces:**
- Consumes: everything above.
- Produces (`ai_chat.h`):
  ```c
  size_t ai_chat_voice_live_begin(HWND panel, int *need_sep);   /* box length (UTF-16) and whether a space is needed */
  void   ai_chat_voice_live_replace(HWND panel, size_t from_u16, const char *text_utf8); /* EM_SETSEL(from,-1)+EM_REPLACESEL(FALSE), caret at end, no undo */
  void   ai_chat_voice_lock_typing(HWND panel, int on);
  int    ai_chat_voice_send_box(HWND panel);                    /* ai_chat_send_user_message when not busy; 0 when busy (status note) */
  ```
  `ai_chat_voice_deliver` gains `int force_send` (append, then send).
- Produces (`voice_win.h`): `int voice_send_pressed(void);` (1: voice took the Send — the
  IDC_CHAT_SEND handler returns without sending); `void voice_on_live(LPARAM lp);`;
  `void voice_on_chat_changed(void);` (posts `WM_VOICE_CHAT` = `WM_APP + 0x43` to the main
  window; its handler compares bindings and steps `VDE_END_SOFT`).
- Wiring rules:
  - `voice_win.c` keeps `voice_ptt` for keys; `VA_OPEN` → `voice_dict_step(VDE_START, live,
    auto_ok)` where `live = voice_live_resolve(s, &g_live_target)` and `auto_ok = auto_send &&
    box empty`; `voice_ptt_set_cap(VOICE_LIVE_CAP_MS)` when live. `VA_FINISH` → `VDE_STOP`;
    `VA_DISCARD` from Escape → `VDE_ESCAPE`, from `VE_DEACTIVATE` → `VDE_END_SOFT` (live) or
    today's discard (batch, through the machine's END_SOFT rule); the cue text uses the "Typing
    is paused." form while live.
  - Actions: `OPEN_LINK` → `voice_live_start` (failure → step `VDE_LINK_FAILED, 1`);
    `CLOSE_LINK` → `voice_live_close`; `REMOVE_WORDS` → `ai_chat_voice_live_replace(panel,
    draft_u16, "")`; `SUBMIT_BATCH` → `voice_stt_submit` with the kept capture's WAV;
    `DELIVER_BATCH` → `ai_chat_voice_deliver` (batch) or `vt_replace_all` + replace (fallback);
    `SEND_BOX` → `ai_chat_voice_send_box`; texts → `ai_chat_voice_status` (header for
    LIVE_UNAVAILABLE, LIVE_STOPPED, DIDNT_CATCH, TRANSCRIBING; chat for FALLBACK_FAILED and
    FINAL_LATE). The ledger is acquired/released at the real sites (capture begin / VAM_CAPTURED
    or error; upload submit / WM_VOICE_STT).
  - `WM_VOICE_LIVE` with the current token: `VLE_DELTA`/`VLE_COMPLETED` → `vt_*` then
    `vt_render` + `ai_chat_voice_live_replace`; `VLE_DONE` with text → `vt_replace_all`, render,
    then `VDE_LINK_FINAL`; `VLE_DONE` without text → `VDE_LINK_FINAL`; `VLE_READY` →
    `VDE_LINK_READY`; `VLE_CLOSED`/`VLE_ERROR` → `VDE_LINK_FAILED, 0` (an HTTP 401/403/429 at the
    handshake also puts `voice_stt_http_error`'s text in the chat). Before replacing, if the box
    is shorter than `draft_u16 + shown`, the recording ends with `VDE_END_SOFT` (something else
    changed the box).
  - `voice_feature_apply`: voice off → `VDE_END_HARD`, `voice_live_reap(3000)`; voice on with a
    recording running and `voice_settings_digest` different from the one taken at START →
    `VDE_END_HARD`.
  - Typing lock (in `ai_chat_input.c`, while `d->voice_typing_locked`): swallow `WM_CHAR` (all
    but Ctrl+C `0x03`), `WM_PASTE`, `WM_CUT`, `WM_CLEAR`, `WM_UNDO`, `EM_UNDO`, `WM_IME_CHAR`, and
    `WM_KEYDOWN` of `VK_DELETE` and `VK_BACK`; Enter still posts the Send command (which voice
    takes). Arrow keys and Ctrl+C still work. Our own `EM_REPLACESEL` is not blocked.
  - Escape in the panel while FINISHING does nothing new (the final text still lands).
  - `ai_chat_voice_on_destroy`: a live recording ends with `VDE_END_SOFT`.
- [ ] **Step 1: Write the failing tests** (`tests/test_voice_ptt_win.c` builds a real panel
  in wintest? Check what it covers today; if it cannot host `ai_chat`, write the typing-lock test
  against `ai_chat_input_subclass_proc` on a plain EDIT whose parent's `GWLP_USERDATA` points at
  a zeroed `AiChatData` with `voice_typing_locked = 1`): typed `WM_CHAR 'a'`, `WM_PASTE`,
  `VK_BACK` do not change the text; `EM_REPLACESEL` does; with the lock off, `'a'` is typed.
  And a native test `test_vdict_glue_send_intercept` is not possible here; the Send intercept is
  a Windows hand check (Task 12 lists it).
- [ ] **Step 2: Run** wintest, expect FAIL. **Step 3: Implement.**
- [ ] **Step 4: Run** `make -j test`, `xvfb-run -a make wintest`, and a `make release` link check
  (`git restore build/win/nutshell.exe` afterwards). Expected: 0 failed, links.
- [ ] **Step 5: Commit** `feat(voice): live dictation in the AI panel's input box`.

### Task 11: Settings > Voice: the Live model row and the live note

**Files:**
- Modify: `src/ui/settings.c` (the Model row at `:2267` and its seed at `:900`, the save at
  `:3184`), `src/ui/settings_dlg.h` / `src/core/settings_layout.c` if a new control id needs a
  layout slot
- Test: `tests/test_settings_label_win.c` or the settings layout tests that cover row ids

**Interfaces:**
- Consumes: `voice_live_provider`, `voice_live_models` (Task 4), `SH_VOICE_STT_LIVE_MODEL`,
  `voice_stt_live_model` (Task 2).
- Produces: `IDC_VOICE_LIVE_MODEL` (editable combo, `CBS_DROPDOWN`, seeded with
  `voice_live_models`, placeholder empty = provider default) directly under Model, shown only when
  the effective provider (the page's current Provider / "Use current AI assistant" choice, not the
  saved one) is OpenAI or Mistral; otherwise a dim static line
  `Live text as you speak needs OpenAI or Mistral` in the same slot. It is re-seeded when the
  provider changes, like `voice_model_seed`. Saved into `voice_stt_live_model`.
- [ ] **Step 1: Write the failing test** following how the existing tests check the Voice page's
  rows (find them with `grep -n IDC_VOICE_MODEL tests/*.c`): the row exists with OpenAI, the note
  with Groq. If no test harness reaches the Settings page, add a pure helper
  `int voice_live_row_kind(const char *provider, int use_ai, const char *ai_provider)` (1 combo,
  0 note) in `src/core/voice_live.c` with a native test for all five providers and the use-AI
  case, and use it from `settings.c`.
- [ ] **Step 2: Run**, expect FAIL. **Step 3: Implement.** **Step 4: Run** tests and a release link
  check. **Step 5: Commit** `feat(voice): Live model row on the Voice page`.

### Task 12 (orchestrator): Spec findings, notes, version, build, full verification

- [ ] Record in the spec's §16, under a new "Found at build time (PR 1, 2026-11-24)" block: the
  wire protocol section above with sources; items 1-5, 14, 16, 17, 24, 26, 27, 28 with what was
  decided; the builder's calls of this plan (no `delay`; fallback waits for Send; soft and hard
  ends; typing stays locked until the final text; hangovers; flush at the gate close for
  Mistral; the WAV-injection seam and its harness case deferred to PR 2 with a hand check).
  Correct §6.2/§6.3 facts the research overturned (prices, deprecated batch models, Mistral's
  configuration message).
- [ ] Add PR 1's Windows hand checks (spec §14's PR 1 list, plus the Send intercept, the typing
  lock, the WebSocket close waking the receiver on real Windows, Mistral's 10-minute batch
  fallback size, OpenAI's idle limit with long pauses, `mingw32-make test` and a release build on
  the Windows box) to the notes' Open list.
- [ ] Bump to one above `origin/main` (v1.2.95), `make clean && make release`, commit the exe.
- [ ] Full list: `make -j test`, `make test SAN=1`, `make lint-gate`, `make fuzz-run`,
  `xvfb-run -a make wintest`, `make selftest-wine`. Push, open the draft PR, final review.
