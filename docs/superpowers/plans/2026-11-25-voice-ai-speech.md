# AI speech (PR 3 of live voice) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Revised 2026-11-25 for the maintainer's provider decision.** AI speech uses real
text-to-speech providers: Mistral (the default), Groq, or a Custom OpenAI-compatible server.
OpenAI is not a preset. The first version of this plan is commit `a3b012f`. Tasks 3, 4 and 6 are
built and do not change.

**Goal:** Let a conversation's replies be read by an AI voice.
- The voice comes from a real text-to-speech endpoint, streamed as WAV.
- Nutshell decodes and converts it in `src/core` to 24 kHz mono PCM16 and plays it through its
  own WASAPI render client.
- It has its own speech setting and key, cut-in ducking, and repeat/continue positions.
- It falls back to Windows speech for the rest of a reply whenever AI speech fails.

**Architecture:** The pure parts go in `src/core` with native tests:
- `voice_aispeech.c`: the provider table with two request shapes (Mistral's streamed
  server-sent events, and OpenAI-compatible raw bytes); target resolution; the shared-key rule;
  the request bodies; Mistral's event parser; the byte-to-text-offset map.
- `voice_wavstream.c`: a streaming WAV decoder and an any-rate to 24 kHz resampler (new
  Task 12).
- `voice_playq.c`: the play queue (Task 3, built).
- `voice_conv.c` and `voice_echo.c`: the conversation-machine follow-ups (Task 4, built, plus
  new Task 13).

The Win32 parts go in `src/ui`:
- The render client on the existing audio thread (Task 6, built).
- A streaming POST on a caller's session (`ai_http_post_stream_ex`).
- The AI speech engine (`voice_aispeech_win.c`). It runs one worker per spoken text, its
  requests go through `voice_net_open(VNK_TTS)`, and it posts the same `WM_VOICE_TTS` messages
  SAPI does.
- A speech router in the real ops table (`voice_win.c`).
- The fallback itself, in `voice_conv_win.c`.

**Tech Stack:** C11, Win32 (MinGW cross-compile), WASAPI render (`IAudioRenderClient`), WinHTTP
(delay-loaded, no new entry points), custom `test_framework.h`, Wine `wintest`,
`make selftest-wine`.

**Spec:** `docs/superpowers/specs/2026-11-24-voice-live-conversation-design.md` (the "live spec").
- PR 3's scope is §15 item 3, as amended on 2026-11-25: §2's supersede line, the §6.1 table,
  §8.4, §8.5, §11.3, §13 and §14's PR 3 hand checks.
- The provider research, the rejected Realtime design and every provider ruling are in §16
  "Decisions of 2026-11-25: AI speech provider". Read it before Tasks 1, 2, 7 and 12.
- PR 1's and PR 2's build notes are in §16 "Found at build time".

## The provider decision (2026-11-25), in brief

OpenAI shuts down every `/audio/speech` model on 2027-01-06. Its replacement,
`gpt-realtime-2.1-mini`, runs only on the Realtime API. A Realtime design (`f79e110`) was
critiqued by Fable and rejected: a conversational model is checked only by its own
"model-generated" transcript. The maintainer chose "Real TTS provider instead":
- **Mistral** (`voxtral-mini-tts-3-0`, the default), streamed as server-sent events, with
  English (UK) women's voices.
- **Groq** (`canopylabs/orpheus-v1-english`, Preview), OpenAI-compatible, WAV only, 200
  characters a request.
- **Custom**, any OpenAI-compatible `/audio/speech` (Kokoro-FastAPI and the like).
- OpenAI works only as a Custom URL, until its shutdown.

Every request asks for `wav`. The spec's §16 subsection has the facts, sources and rulings.

## Global Constraints

- **Where to work:** worktree `/home/thomas/nutshell-wt/voice-ai-speech`, branch
  `feat/voice-ai-speech` (main `58dbd00`, v1.2.99, plus Tasks 3, 4 and 6). Never touch
  `~/nutshell` or another worktree. Do not push.
- **Order of the remaining tasks:** 1, 2, 12, 13, 5, 7, 8, 9, 10, 11. Tasks 3, 4 and 6 are
  built.
- **Reviews** are by Opus or Fable only, never Sonnet or Haiku, and never by the model that
  wrote the code. Every Opus review's findings go to the maintainer before any is fixed.
- **Version:** only Task 11 (the orchestrator) changes it. Bump `APP_VERSION` to `"1.3.0"` and
  `APP_VERSION_BINARY` to `1,3,0,0` (the patch number never passes 99), update README's
  `**Version**:` line, and commit `build/win/nutshell.exe`. No implementer task does any of this.
- **Test first.** A test that tells two states apart needs a fixture in which they differ.
- **Line endings are preserved.**
  - CRLF: `src/config/loader.c`, `Makefile`, `src/core/ai_http.h` and `src/ui/ai_http_win.c`.
  - Every other file touched here is LF.
  - Before each commit, `git diff --stat` and `git diff --stat --ignore-cr-at-eol` must agree.
- **Portable logic goes in `src/core`**, because `src/ui` is not in the native build.
  - New `src/core/*.c` files and native `tests/*.c` files are picked up by the Makefile's
    wildcards. Every test function is declared and called in `tests/runner.c`.
  - A Win32-only test is `tests/*_win.c`. It is listed in `WIN_ONLY_TESTS`, its sources go in
    `WIN_TEST_SRCS`, and it is called from `tests/win_runner.c`.
- **Both builds use `-Werror`.** MinGW adds `-Wpedantic -Wshadow -Wformat=2 -Wconversion`.
  - Every task that touches `src/ui` also runs `make -j release` far enough to compile its
    objects.
  - Test baselines: 4,527 native before PR 3. After Tasks 3, 4 and 6, and before Task 6's fix
    round, 4,547 native and 182 `wintest`.
- **Design-system rules (CLAUDE.md):**
  - no `RGB(` literal in `src/ui`;
  - use `ns_tokens()`, `ns_scale`, `ns_font`, the page's `make_*` helpers, `ns_motion` and
    `ns_hover`;
  - no new `MessageBox`; notices go through `notice_dlg_*`.
- **Optional-feature rule.** With voice off, AI off, Dictation mode, or a conversation that has
  not spoken yet, nothing added here:
  - opens a render client, starts a speech worker, or makes a speech request;
  - asks for any DLL.

  One gate per entry point: `voice_ais_start` is the only AI speech context constructor, and
  `voice_net_open` is the only voice network opener. Each refuses unless `voice_gate_on()`.
  `sapi.dll` loads with AI speech only when a reply falls back to Windows speech.
- **The resource ledger** (`src/core/voice_ledger.h`) is UI-thread only.
  - `VRES_RENDER` is acquired once per AI speech context, when its one `render_open` is pushed.
    It is released when `render_close` is pushed or a pre-emption is handled.
  - `VRES_SPEECH_REQ` counts one per speech worker: acquired at its creation, released when it
    is joined (or detached after a bounded wait).
- **Content.** Spoken text, request bodies, audio and keys are `secure_zero`ed before free. Text
  and bodies are logged only through `DLOG_CONTENT`. A request is logged by kind and HTTP status
  only. The `Authorization` header buffer is wiped after the call.
- **New window message:** `WM_VOICE_AIS (WM_APP + 0x49)`. 0x44 to 0x48 are taken; 0x48 is
  `WM_VOICE_ACTIVATION`.
- **Audio messages and errors** are those Task 6 built: `VAM_RENDER_OPENED`, `VAM_PLAYED`,
  `VAM_PLAY_DONE`, `VAM_RENDER_ERROR` and `VAE_RENDER_PREEMPTED`.
- **New control ids:** `IDC_VOICE_SPEECH 3121`, `IDC_VOICE_TTS_PROVIDER 3122`,
  `IDC_VOICE_TTS_URL 3123`, `IDC_VOICE_TTS_KEY 3124`, `IDC_VOICE_AI_VOICE 3125`,
  `IDC_VOICE_AI_MODEL 3126` and `IDC_VOICE_AI_STYLE 3127`. Recheck that they are free before
  use.
- **Copy fixed by the spec** (UTF-8; the dash is U+2014):
  - header: `AI speech failed: using Windows speech`
  - chat note (Task 13): `Conversation ended: the last turn could not be transcribed`
  - the notice dialog (once per launch, Windows speech only): `Windows has no British English
    woman's voice installed, so replies use <voice>. To get one, add English (United Kingdom)
    under Windows Settings > Time & language > Speech, or choose AI speech in Settings > Voice.`
  - the default style: `Speak as an English woman with a standard British accent (received
    pronunciation), clear and natural.`
  - Play sample: `Hello. This is how replies will sound.`
  - notice lines (label — value):
    - `Voice: speech` — `Windows speech`
    - `Voice: speech provider` — `Mistral`
    - `Voice: speech URL` — `none`
    - `Voice: speech API key` — `none; the speech-to-text key is used when the provider is the
      same`
    - `Voice: AI voice` — `the provider's default (for Mistral, Jane, British English)`
    - `Voice: AI speech model` — `the provider's default`
    - `Voice: accent and style` — `an English woman with a standard British accent`
- **Numbers fixed here or by the spec:**
  - The render format is 24,000 Hz mono PCM16; every stream is converted to it (Task 12).
  - Accepted WAV: format 1 (16-bit) or 3 (32-bit float), or `WAVE_FORMAT_EXTENSIBLE` holding
    either; 1 or 2 channels; 8,000 to 48,000 Hz.
  - The duck is `VOICE_TTS_DUCK_PERCENT` (18).
  - Chunks: the first about 200 bytes and later ones up to 1,000. For Groq every chunk is at
    most 200 bytes.
  - The render buffer is 100 ms. `VAM_PLAYED` comes every 100 ms while playing.
  - The next chunk's request starts only when less than 20,000 ms of audio is queued.
  - Bounded joins wait 3,000 ms.
- **Settings keys:** 62 before Task 1, 69 after it. `CFG_KEYS_MAX` is 96 and
  `NOTICE_ITEMS_MAX` is 24. `winhttp.def` has 17 entries and this PR adds none.

## Review Focus

1. **A WAV stream split anywhere.** The header can be cut across callbacks, inside a chunk id,
   inside a sample or a float, and inside a base64 group (Mistral). No sample may be shifted, and
   a streamed size of `0xFFFFFFFF` means "until the end". Pinned by `test_vwav_split_everywhere`
   (Task 12), `test_vais_sse_split_lines` (Task 2) and the loopback server's split writes in
   `test_voice_ais_speak_done` (Task 7).
2. **Old audio after a stop or a cut-in.** A worker that pushes one more block after the UI
   flushed must not have it played. Pinned by `test_vplayq_flush_drops_late_data` (Task 3,
   built) and `test_voice_ais_stop_then_speak_no_old_audio` (Task 7).
3. **A network slower than real time.** An underrun mid-reply is not the reply's end. Pinned by
   `test_vplayq_underrun_not_done` (Task 3, built).
4. **AI speech failing on every reply** (a wrong key, a Groq model withdrawn from Preview, a
   Mistral error event, a format Nutshell cannot read). Each reply falls back to Windows speech
   from where it stopped, with no hang and the ledger at zero. Pinned by
   `test_vconv_win_ai_fail_every_reply` (Task 8), `test_voice_ais_http_401` and
   `test_voice_ais_sse_error_event` (Task 7), and `test_vwav_rejects_unsupported` (Task 12).
5. **Voice switched off while the server never answers.** Teardown stays bounded. Pinned by
   `test_voice_ais_quit_while_server_hangs` (Task 7). A reply full of quotes, newlines,
   backslashes and non-ASCII text must make valid JSON in both body shapes: pinned by
   `test_vais_body_escapes` (Task 2).

## Rechecks, 2026-11-25

The provider table, its sources and what could not be verified are in the spec's §16
"Decisions of 2026-11-25: AI speech provider". In short:

**Mistral.**
- `POST https://api.mistral.ai/v1/audio/speech`, model `voxtral-mini-tts-3-0`.
- Body fields: `voice_id`, `response_format` (mp3, wav, pcm, flac, opus) and `stream: true`.
  The reply is server-sent events `speech.audio.delta` with base64 `audio_data`, then
  `speech.audio.done`.
- The audio is mono at 24 kHz. Whether `pcm` is float32 is not stated by the current pages.
- English (UK) women's voices: `gb_jane_neutral`, Mary `voice_1787`, Sky `voice_1786` and
  Harriet `voice_590`.
- No style instructions. $16 per 1M characters. No input limit is stated.

**Groq.**
- `POST https://api.groq.com/openai/v1/audio/speech`, model `canopylabs/orpheus-v1-english`,
  in Preview.
- WAV only, at most 200 characters a request, $22 per 1M characters.
- Six voices with no accent stated. `sample_rate` is in the API reference but not confirmed for
  Orpheus. Whether it streams is not stated.

**OpenAI.**
- `tts-1`, `tts-1-hd` and `gpt-4o-mini-tts` (all snapshots) shut down on 2027-01-06.
- Speech-to-text, outside PR 3:
  - `gpt-4o-transcribe`, `gpt-4o-mini-transcribe` (with `-2025-12-15`), `whisper-1` and
    `gpt-4o-transcribe-diarize` shut down on **2027-02-26**, replaced by `gpt-transcribe` or
    `gpt-live-transcribe`.
  - `gpt-4o-mini-transcribe-2025-03-20` shuts down on 2027-01-20.
  - Confirmed on `developers.openai.com/api/docs/deprecations.md`. Task 11 files it as a todo.

## Rulings

Each ruling below is `decision — why — cost if wrong`. "(revised)" marks a change from
`a3b012f`. The provider rulings are recorded in the spec's §16 subsection, and R1 to R4, R25,
R27 and R28 restate them.

- **R1 (revised)** — The AI speech providers are Mistral (the default), Groq and Custom; OpenAI
  is not a preset. A Custom URL of `https://api.openai.com/v1` with `gpt-4o-mini-tts` works
  until 2027-01-06. — These are the maintainer's decision and the spec's rulings. — Cost if
  wrong: see the spec.
- **R2 (revised)** — The default voice is per provider. The setting `""` means the provider's
  default: `gb_jane_neutral` for Mistral, `autumn` for Groq, and none for Custom (a resolve
  error). The seeds are:
  - Mistral: `gb_jane_neutral`, `voice_1787` (Mary), `voice_1786` (Sky), `voice_590` (Harriet).
  - Groq: `autumn`, `diana`, `hannah`, `austin`, `daniel`, `troy`.
  - Custom: `bf_emma`, `af_heart`, `alloy`, `coral`.

  — Voice ids differ between providers, so one default cannot serve all of them. — Cost if
  wrong: the maintainer picks another voice in the combo.
- **R3 (revised)** — The default model is per provider: `voxtral-mini-tts-3-0` (seeds also
  `voxtral-mini-tts-2603`) for Mistral, `canopylabs/orpheus-v1-english` for Groq, and none for
  Custom (seeds `kokoro`, `tts-1`, `tts-1-hd`, `gpt-4o-mini-tts`). The style is sent only to
  Custom, as `instructions`, when it is not empty and the model does not start with `tts-1`. —
  Mistral takes no style, and Groq takes only cues inside the text. — Cost if wrong: none.
- **R4 (revised)** — Two body shapes, both asking for `"response_format":"wav"`:
  - `VAIS_SHAPE_MISTRAL`: `model`, `input`, `voice_id`, `"stream":true`.
  - `VAIS_SHAPE_OPENAI` (Groq, Custom): `model`, `voice`, `input`, and `instructions` per R3.

  No `sample_rate`, `speed` or `stream_format`. — Each body follows its provider's documented
  fields, and fewer fields means fewer rejections. — Cost if wrong: one provider fails and falls
  back, and the hand check shows which field.
- **R5** — A speech request never follows a redirect, so the bearer key is never re-sent to
  another host. — Cost if wrong: a server behind a redirect fails and falls back.
- **R6** — The render client opens at the first speech of a context. It is stopped and reset
  between texts, and released at the context's end. (Built in Task 6.)
- **R7** — One render client with one owner token; a newer open pre-empts the old owner with
  `VAE_RENDER_PREEMPTED`. (Built in Task 6.)
- **R8** — One worker thread per spoken text, with its chunks' requests in sequence. The next
  request waits until less than 20 s of audio is queued; a response is never stalled mid-body.
  — Memory stays bounded with no server timeouts. — Cost if wrong: a gap between chunks on a
  very slow link.
- **R9** — The cut-off is in proportion to UTF-8 bytes within the chunk. A chunk still streaming
  uses an estimate of 1,600 frames per byte. The result is snapped back to its word start on a
  UTF-8 boundary. Byte offsets beat the spec's "characters" because every offset in the glue is
  a byte offset. — Cost if wrong: a few words repeated or skipped.
- **R10 (revised)** — Any failure lets what is queued play out, then posts `VTT_ERROR` with the
  resume offset; the glue continues with Windows speech from there. A failure is: an HTTP status
  other than 200, a transport error, a Mistral `error` event, a WAV Nutshell cannot read, or a
  target that does not resolve (that one at once, at `from`). The next speech tries AI again.
  A fallback never raises the British-voice notice. — No overlap and no lost words. — Cost if
  wrong: up to 20 s of queued audio before the handover.
- **R11** — Spoken phrases use the configured engine. — Cost if wrong: about 1 s of latency on
  an announcement.
- **R12** — Repeat re-synthesises; no audio is cached. — Cost if wrong: a second request.
- **R13 (revised)** — The shared key is that of the effective speech-to-text target
  (`voice_stt_resolve`) when its provider id equals the speech provider's id. For Custom the
  two normalised base URLs must also be equal. The shared key is the target's own: `ai_api_key`
  when `key_from_chat`, else `stt_api_key`. An unresolved STT target means a separate key.
  While the key is shared its row is hidden, and a saved `tts_api_key` is kept. — This is spec
  §8.4 for the new set (Mistral with Mistral, Groq with Groq). — Cost if wrong: a second key
  prompt in an edge case.
- **R14** — `voice_ai_style` is `char[1024]`. A missing key gives the default text; `""` means
  no instructions.
- **R15** — An AI speech failure shows the spec's header only, plus a debug log line with the
  HTTP status or the decoder's error (no content). No chat note.
- **R16 (revised)** — Row order in Conversation: Pause before sending, **Speech**, Windows voice
  (and its two dim lines), the AI rows, Play sample, the echo line and the ducking line.
  - Windows voice stays enabled, because the fallback uses it.
  - The English (UK) line is hidden with AI speech.
  - The style row is enabled only for Custom.
  - Changing the provider reseeds the voice and model combos, with the provider default first;
    that first entry is stored as `""`, as the speech-to-text model is.
- **R17** — Play sample uses the page's unsaved values. The gate still requires voice saved on.
- **R18** — The self-report gains `voice-resources: N` and the `ai` mode; there is no exe-level
  speech control.
- **R19** — No harness case for AI speech.
- **R20** — The ops table: `tts_start(token, engine)` and an optional `speech_engine()`; the
  real table routes by token.
- **R21** — The duck is a software render gain within the 100 ms buffer. (Built in Task 6.)
- **R22** — `VAM_PLAYED` and `VAM_PLAY_DONE` carry a malloc'd `VoiceRenderMsg`. (Built in
  Task 6.)
- **R23** — `VCE_EXTERNAL_SEND` is fed from Retry's two exits and any other launch Task 8's grep
  finds. (The core half is built in Task 4.)
- **R24** — The play position is the written position less the device padding, within the
  current chunk. (Built in Task 6.)
- **R25 (new)** — Every request asks for `wav`. A streaming decoder in `src/core` reads the
  header (skipping `LIST` and other chunks before `data`) and ignores the `data` size: 0,
  `0xFFFFFFFF` or a real one all mean "until the end of the body". — `wav` is the one format
  every provider offers and the only one that says its sample type and rate; servers write
  placeholder sizes. — Cost if wrong: a chunk after `data` would be played as noise (not seen
  in text-to-speech servers).
- **R26 (new)** — Conversion to 24 kHz mono PCM16 happens in `src/core` on the worker thread:
  stereo averaged, float32 clamped and scaled, any rate from 8 to 48 kHz through a streaming
  windowed-sinc resampler, and 24 kHz passed through unchanged. **The Task 6 render client needs
  no follow-up task.** — One tested conversion, and the render client stays as built. — Cost if
  wrong: CPU on the worker for 22.05 or 48 kHz streams (small next to the network).
- **R27 (new)** — The chunker takes per-provider limits: `voice_speak_chunks_lim(text, from,
  first_max, later_max, ...)`, with today's `voice_speak_chunks` as the 200/1,000 wrapper. Groq
  is 200/200. — Groq's documented input limit is 200 characters. — Cost if wrong: Groq makes
  about five times as many requests (each one is short).
- **R28 (new)** — Mistral's stream is read as server-sent events through `sse_line`:
  - An event whose `event:` name, or `"type"`, ends in `.delta` and whose data carries
    `audio_data` is audio. `audio_data` is read with `json_scan_string`, because base64 can
    exceed the tokenizer's 8,191-byte string cap.
  - `.done` ends the stream.
  - An `error` member or event is a failure.
  - Unknown events are ignored and logged by name only.

  — The docs name the events but show no raw envelope. — Cost if wrong: Mistral falls back on
  every reply until the parser is fixed; the hand check shows it at once.
- **R29 (new, spec owner)** — The Ending watchdog (180 s) ends the conversation with
  `Conversation ended: the last turn could not be transcribed` (Task 13). — The spec's §16
  ruling: otherwise the silence end fires on the next tick with the wrong reason. — Cost if
  wrong: none beyond what the silence end did anyway.
- **R30 (new, from the build)** — The done guard. When a worker has finished every chunk and no
  `VAM_PLAY_DONE` arrives within `voice_audio_render_queued_ms()` at that moment plus 2,000 ms,
  the reply counts as done. — `render_end` (like data and gain) is not among the commands that
  survive running out of memory. — Cost if wrong: a reply ends up to 2 s early in a pathological
  case.
- **R31 (new, from the build)** — `voice_playq`'s position accumulates `frames_in_chunk` across
  consecutive blocks of the same reply and chunk. Today it counts within a block. This is a
  small change to `voice_playq.c` made in Task 7, with its own test. — A chunk arrives in many
  network blocks; pushing a whole chunk as one block would lose streaming. — Cost if wrong:
  none.

**Spec against the merged code, and which wins:**
- §3's counts: the code (62 keys, 69 after Task 1).
- §6.5's `VNK_TTS` "declared, unused": the spec (it becomes used).
- The notice clause: the spec (added).
- §10's "characters": the code (R9).
- §16 item 7's render-client fallback for SAPI is not built.

## Notes items (a) to (s) (`docs/superpowers/specs/2026-09-06-ui-redesign-notes.md`, "Conversation mode (PR 2 of live voice)")

**Folded in:**
- (r) Retry with the mic open: Task 4 (built) and Task 8.
- (s) The refusal phrase and a paused local repeat restarting the silence clock: Task 4 (built).
- (q) The Hearing and Ending watchdogs restarting the silence clock: Task 4 (built), and
  Task 13 for the Ending watchdog's end.
- (q) The 10-minute Speaking watchdog: Task 4 (built) and Task 8.
- (s) The `voice_echo` tick wrap: Task 4 (built).
- (r) A close lost when `realloc` fails: Task 6 (built).
- (r) The event queue dropping `VCE_END`: Task 8.
- (o) Play sample's help: Task 1.
- (o) The notice lost when the conversation ends first: Task 8.

**Left out:**
- Enter while a reply is spoken (the maintainer decides).
- The RDP or console disconnect wording.
- The one-line triple-backtick fence.
- The header after a pause cuts an announcement.
- Settings destroyed before its owner is re-enabled (a hand check).
- Everything in (a) to (n) and (p), and the rest of (r): hand checks or "no code".

---

### Task 1: The PR 3 settings, key, help, notice lines and digest

**Status:** to build. Revised 2026-11-25 for the provider set.

**Files:**
- Modify:
  - `src/config/config.h`;
  - `src/config/loader.c` (CRLF: load, save, the secret, `k_setting_keys`, the free path's
    `secure_zero`);
  - `src/core/voice_config.[ch]`;
  - `src/core/settings_help.[ch]`;
  - `src/core/notice.c` (`is_voice_subkey`, the British-voice notice's closing clause).
- Test: `tests/test_voice_config.c`, `tests/test_notice.c`, and the existing config round-trip
  and `k_setting_keys` tests.

**Interfaces:**
- Produces, in `Settings` (after `voice_windows_voice_name`):
  ```c
  int  voice_speech_engine;            /* VOICE_SPEECH_WINDOWS (0) or VOICE_SPEECH_AI (1); JSON "windows"/"ai" */
  char voice_tts_provider[32];         /* "mistral" (default), "groq" or "custom" */
  char voice_tts_url[CFG_STR_MAX];     /* base URL, Custom only */
  char tts_api_key[CFG_STR_MAX];       /* DPAPI like stt_api_key */
  char tts_api_key_enc_preserved[CFG_BLOB_MAX];
  char voice_ai_voice[64];             /* "" = the provider's default (R2) */
  char voice_ai_tts_model[CFG_STR_MAX];/* "" = the provider's default (R3) */
  char voice_ai_style[1024];           /* default VOICE_AIS_DEFAULT_STYLE; "" = no instructions (R14) */
  ```
- In `voice_config.h`:
  - `#define VOICE_SPEECH_WINDOWS 0`, `#define VOICE_SPEECH_AI 1`;
  - `const char *voice_speech_json(int e)` (`"windows"`/`"ai"`);
  - `int voice_speech_from_json(const char *s)` (unknown or NULL gives Windows);
  - `#define VOICE_AIS_DEFAULT_PROVIDER "mistral"` and `#define VOICE_AIS_DEFAULT_STYLE` (the
    Global Constraints text).
- `SettingsHelpId` gains, before `SH_COUNT`: `SH_VOICE_SPEECH`, `SH_VOICE_TTS_PROVIDER`,
  `SH_VOICE_TTS_URL`, `SH_VOICE_TTS_KEY`, `SH_VOICE_AI_VOICE`, `SH_VOICE_AI_MODEL`,
  `SH_VOICE_AI_STYLE` and `SH_VOICE_PLAY_SAMPLE`.

- [ ] **Step 1: Write the failing tests**
  - `test_voice_ais_settings_defaults`: a config without the seven keys loads with Windows,
    `"mistral"`, an empty URL, an empty key, an empty voice, an empty model and the default
    style.
  - `test_voice_ais_settings_roundtrip`: save then load keeps AI, `"custom"`,
    `"http://10.0.0.5:8880/v1"`, `"bf_emma"`, `"kokoro"`, a style with a quote and a newline,
    and a key. The file holds `"voice_speech_engine": "ai"`. The key is saved as a `$dpapi$v1$`
    blob through the fake DPAPI, never in clear text.
  - `test_voice_ais_key_preserved`: an undecryptable blob loads as an empty key, is kept in
    `tts_api_key_enc_preserved` and is written back verbatim. A new key drops it.
  - `test_voice_ais_style_empty_kept`: `"voice_ai_style": ""` loads as `""`.
  - `test_voice_ais_settings_validate`: `voice_tts_provider` `"openai"` and `"gemini"` validate
    to `"mistral"`; `"groq"` and `"custom"` are kept; `"voice_speech_engine": "loud"` loads as
    Windows.
  - `test_voice_ais_notice_lines`: with `voice_enabled` present and the seven keys missing,
    exactly the seven Global Constraints lines appear, in key order. With `voice_enabled`
    missing they fold into "Voice", `tts_api_key` included.
  - `test_voice_ais_digest`: changing any one of the seven fields, the key included, changes
    `voice_settings_digest`.
  - The existing `k_setting_keys`-against-a-saved-file test passes with 69 rows.
  - `test_notice_msg_voice_british_names_the_voice` is updated to the full text with
    `, or choose AI speech in Settings > Voice.`
  - `test_settings_help_play_sample`: label `Play sample`, kind `SH_KIND_ACTION`.
- [ ] **Step 2: Run** `make -j test 2>&1 | tail -5`: the new tests FAIL (or do not link).
- [ ] **Step 3: Implement.**
  - The seven `CK(...)` rows carry the labels of the Global Constraints. `tts_api_key` goes
    through `load_secret`/`secret_prepare` exactly as `stt_api_key` does, and is wiped in the
    free path.
  - Help entries (`SETTINGS_PAGE_VOICE`). Each `default_text` is the notice value.
    - `SH_VOICE_SPEECH`, label `Speech:`. Windows speech uses a voice installed on this PC. AI
      speech sends each reply's text to the speech provider and plays the AI-generated voice it
      returns. If AI speech fails, the rest of the reply is read with Windows speech.
      Conversation mode only.
    - `SH_VOICE_TTS_PROVIDER`, label `Speech provider:`. Mistral (Voxtral, English (UK) voices),
      Groq (Orpheus, in Preview at Groq, short requests), or Custom (an OpenAI-compatible
      `/audio/speech`, for example a Kokoro server on your network). It must say: "OpenAI's
      speech models shut down on 6 January 2027; until then, Custom with
      https://api.openai.com/v1 reaches them."
    - `SH_VOICE_TTS_URL`, label `Base URL:`. A pasted `/audio/speech` is trimmed.
    - `SH_VOICE_TTS_KEY`, label `Speech API key:`. It is shown only when the speech provider is
      not the speech-to-text one, and is kept encrypted for this Windows user.
    - `SH_VOICE_AI_VOICE`, label `AI voice:`. The voice id the provider knows; empty means the
      provider's default; Custom needs one.
    - `SH_VOICE_AI_MODEL`, label `AI speech model:`. Empty means the provider's default; Custom
      needs one.
    - `SH_VOICE_AI_STYLE`, label `Accent and style:`. Sent to a Custom server as instructions,
      which some servers ignore. Mistral and Groq take the accent from the voice.
    - `SH_VOICE_PLAY_SAMPLE`, `SH_KIND_ACTION`, label `Play sample`, `default_text` NULL. It
      speaks a sentence with the chosen speech and voice, using the values on the page before
      Save.
  - Add `"tts_api_key"` to `is_voice_subkey` and the clause to `NOTICE_MSG_VOICE_BRITISH`. Add
    the seven fields to `voice_settings_digest`.
- [ ] **Step 4: Run** `make -j test 2>&1 | tail -3`. All pass. Record the key count (69).
- [ ] **Step 5: Commit** `feat(voice): AI speech settings, key, help and notice lines`.

Suggested implementer: Sonnet. Reviewer: Opus or Fable.

---

### Task 2: AI speech in core: providers, targets, the shared key, bodies, Mistral's events, the offset map

**Status:** to build. Revised 2026-11-25: two shapes, Mistral's event parser, base64 decoding,
the chunker's limits; PCM alignment moved to Task 12.

**Files:**
- Create: `src/core/voice_aispeech.h`, `src/core/voice_aispeech.c`
- Modify:
  - `src/core/base64.[ch]` (a decoder);
  - `src/core/voice_speak.[ch]` (`voice_speak_chunks_lim`; `voice_speak_chunks` becomes its
    200/1,000 wrapper with unchanged output).
- Test: `tests/test_voice_aispeech.c`, `tests/test_base64.c` (or wherever `base64_encode` is
  tested), `tests/test_voice_speak.c`

**Interfaces:**
- Consumes: Task 1's fields; `voice_stt_resolve`, `voice_base_url_normalise`,
  `json_escape_string`, `json_scan_string` and `json_parse`.
- Produces:
```c
/* base64.h */
size_t base64_decode(const char *src, size_t len, unsigned char *dst, size_t cap);
       /* standard alphabet, '=' padding, whitespace ignored; (size_t)-1 on a bad byte or cap */
/* voice_speak.h */
size_t voice_speak_chunks_lim(const char *text, size_t from, size_t first_max, size_t later_max,
                              size_t *starts, size_t *lens, size_t max);
/* voice_aispeech.h */
typedef enum { VAIS_SHAPE_OPENAI = 0, VAIS_SHAPE_MISTRAL } VoiceAisShape;
typedef enum { VAIS_KEY_OWN = 0, VAIS_KEY_STT, VAIS_KEY_CHAT } VoiceAisKey;
typedef struct {
    const char *id, *label, *base;          /* "mistral" "Mistral" "https://api.mistral.ai/v1"; custom: base "" */
    const char *const *models;              /* NULL-terminated seeds; [0] the default, or NULL list entry 0 for custom */
    const char *const *voices;              /* likewise */
    VoiceAisShape shape;
    size_t first_chunk, later_chunk;        /* 200/1000; Groq 200/200 */
} VoiceAisProvider;
const VoiceAisProvider *voice_ais_providers(size_t *n);       /* mistral, groq, custom, in that order */
const VoiceAisProvider *voice_ais_provider_find(const char *id);
typedef struct {
    char url[512]; char model[CFG_STR_MAX]; char voice[64]; char style[1024];
    char provider[16]; VoiceAisShape shape; VoiceAisKey key_from;
    int send_instructions; size_t first_chunk, later_chunk;
} VoiceAisTarget;
int  voice_ais_resolve(const Settings *s, VoiceAisTarget *t, char *err, size_t cap);
int  voice_ais_key_shared(const Settings *s);                       /* R13 */
const char *voice_ais_key(const Settings *s, const VoiceAisTarget *t); /* never NULL; may be "" */
char *voice_ais_body(const VoiceAisTarget *t, const char *text, size_t len, size_t *out_len);
typedef enum { VAIS_EV_NONE = 0, VAIS_EV_AUDIO, VAIS_EV_DONE, VAIS_EV_ERROR } VoiceAisEvKind;
typedef struct { char event[64]; } VoiceAisSse;                     /* the last "event:" name */
typedef struct { VoiceAisEvKind kind; unsigned char *audio; size_t audio_len; } VoiceAisEv;
void voice_ais_sse_init(VoiceAisSse *s);
int  voice_ais_sse_line(VoiceAisSse *s, const char *line, size_t len, VoiceAisEv *ev);
       /* one sse_line line; AUDIO: ev->audio is malloc'd decoded bytes (caller wipes and frees) */
#define VOICE_AIS_EST_FRAMES_PER_BYTE 1600
size_t voice_ais_offset(const char *text, size_t chunk_from, size_t chunk_len,
                        uint64_t played, uint64_t total, int complete);   /* R9 */
```
- `voice_ais_resolve`:
  - URL: Mistral `https://api.mistral.ai/v1/audio/speech`; Groq
    `https://api.groq.com/openai/v1/audio/speech`; Custom normalised `{base}/audio/speech`.
  - An empty model or voice takes the provider's `[0]`.
  - Errors in `err`: `Enter the speech Base URL`, `The speech Base URL is not valid`, `Enter the
    speech model your server uses`, `Enter the voice your server uses` (Custom only), and
    `Enter a speech API key` when the key is empty. A Custom server with no key is allowed, so
    local servers work: the last error applies to Mistral and Groq only.
- `voice_ais_sse_line` follows R28. A `data:` line whose JSON (or whose `event:` name) is
  neither audio, done nor error returns `VAIS_EV_NONE`.

- [ ] **Step 1: Write the failing tests**
  - Resolution:
    - `test_vais_resolve_mistral_defaults`: the Mistral URL, `voxtral-mini-tts-3-0`,
      `gb_jane_neutral`, the shape `VAIS_SHAPE_MISTRAL`, `send_instructions` 0 and chunks
      200/1,000.
    - `test_vais_resolve_groq`: the Groq URL, `canopylabs/orpheus-v1-english`, `autumn`, shape
      OPENAI, chunks 200/200.
    - `test_vais_resolve_custom`: `http://10.0.0.5:8880/v1/audio/speech/` normalises; an empty
      URL, `ftp://x`, an empty model and an empty voice each give their error. An empty key is
      allowed.
    - `test_vais_instructions_custom_only`: Custom with a style and `kokoro` gives 1; `tts-1`
      and `tts-1-hd` give 0; an empty style gives 0; Mistral and Groq always give 0.
    - `test_vais_unknown_provider`: `"openai"` resolves as Mistral (validation already mapped
      it; resolve does not crash on it).
  - The shared key:
    - `test_vais_key_shared`: STT Mistral with TTS Mistral shares (`VAIS_KEY_STT`). STT Groq
      with TTS Groq shares. "Use current AI assistant" never shares with Mistral or Groq (no
      chat provider is either), except through Custom: a Custom chat and a Custom speech URL
      with the same base share (`VAIS_KEY_CHAT`).
    - `test_vais_key_not_shared`: STT OpenAI with TTS Mistral, STT Groq with TTS Mistral, and
      two different Custom bases all give `VAIS_KEY_OWN`.
    - `test_vais_key_stt_unresolved`: an STT target that does not resolve gives
      `VAIS_KEY_OWN`.
  - The bodies:
    - `test_vais_body_mistral`: the body parses and has exactly `model`, `input`, `voice_id`,
      `"response_format":"wav"` and `"stream":true`.
    - `test_vais_body_openai_shape`: exactly `model`, `voice`, `input` and `response_format`
      `"wav"`, plus `instructions` only when `send_instructions`.
    - `test_vais_body_escapes`: the text `He said "C:\temp"\nnext\ttab é ✓` round-trips through
      `json_parse` in both shapes.
    - `test_vais_body_rejects_crlf`: a CR or LF in the model or voice gives NULL.
  - Mistral's events:
    - `test_vais_sse_audio_delta`: the lines `event: speech.audio.delta` then
      `data: {"type":"speech.audio.delta","audio_data":"<base64 of 20 000 bytes>"}` give AUDIO
      with those 20,000 bytes (larger than the tokenizer's cap).
    - `test_vais_sse_type_only`: a `data:` line with `"type":"speech.audio.delta"` and no
      `event:` line is AUDIO too.
    - `test_vais_sse_done_and_error`: `event: speech.audio.done` gives DONE;
      `data: {"error":{"message":"x"}}` gives ERROR; `data: {"type":"other"}` gives NONE.
    - `test_vais_sse_split_lines`: fed through `sse_line_feed` in 1-byte pieces, the same
      stream gives the same events.
    - `test_vais_sse_bad_base64`: an invalid `audio_data` gives ERROR.
  - base64 and the chunker:
    - `test_base64_decode_roundtrip`: lengths 0 to 64 round-trip with `base64_encode`; a byte
      outside the alphabet and a short `cap` give `(size_t)-1`.
    - `test_vspeak_chunks_lim`: limits 200/200 keep every chunk at 200 bytes or less, on
      sentence or word ends. `voice_speak_chunks` gives exactly what it gave before for the
      existing test texts.
  - The offset map: `test_vais_offset_complete`, `test_vais_offset_estimate` and
    `test_vais_offset_utf8`, as in `a3b012f`. For the chunk `"One two three four."` at 10:
    played 0 gives 10, half gives the start of `three`, and all of it gives the end. An
    incomplete chunk never passes its end. No offset falls inside a UTF-8 character.
- [ ] **Step 2: Run.** FAIL.
- [ ] **Step 3: Implement.** No Win32, no I/O, no logging.
- [ ] **Step 4: Run** `make -j test 2>&1 | tail -3`. PASS, and the existing `test_vspeak_*`
  tests are unchanged.
- [ ] **Step 5: Commit** `feat(voice): AI speech providers, request bodies and Mistral's stream events`.

Suggested implementer: Sonnet. Reviewer: Opus or Fable.

---

### Task 3: The play queue

**Status: built and reviewed** (`932bb02`). Unchanged by the 2026-11-25 provider revision; R31 (Task 7) adds position accumulation across blocks.

**Files:**
- Create: `src/core/voice_playq.h`, `src/core/voice_playq.c`
- Test: `tests/test_voice_playq.c`

**Interfaces:**
- Produces:
```c
typedef struct { unsigned reply_id, chunk; uint64_t frames_in_chunk; } VoicePlayPos;
typedef struct VoicePlayQ VoicePlayQ;   /* builder's fields: a FIFO of blocks {reply_id, chunk,
                                           samples, n, read, is_end}, min_reply, gain_q16, pos */
VoicePlayQ *voice_playq_new(void);
void   voice_playq_free(VoicePlayQ *q);                  /* secure_zero every block */
int    voice_playq_push(VoicePlayQ *q, unsigned reply_id, unsigned chunk,
                        int16_t *s, size_t n);           /* takes s (malloc'd); 0 = dropped
                                                            (stale or OOM) and s freed+wiped */
void   voice_playq_end(VoicePlayQ *q, unsigned reply_id);   /* the reply's last block is in */
void   voice_playq_flush(VoicePlayQ *q, unsigned min_reply);/* drop all; later pushes whose
                                                            reply_id is before min_reply drop */
void   voice_playq_set_gain(VoicePlayQ *q, int percent);    /* 0..100, Q16, saturating */
size_t voice_playq_read(VoicePlayQ *q, int16_t *out, size_t frames, VoicePlayPos *pos);
int    voice_playq_take_end(VoicePlayQ *q, unsigned *reply_id); /* 1 once per reply: its end
                                                            marker was reached by read */
uint64_t voice_playq_queued_frames(const VoicePlayQ *q);
```
- Reply ids are compared wrap-safe (`(int)(a - b) < 0` means before).
- `read` returns fewer frames than asked on an underrun. The audio thread then writes only what
  it got; WASAPI plays silence for the rest. `pos` is the position after the last frame
  written.

- [ ] **Step 1: Write the failing tests**
  - `test_vplayq_order_and_pos`: two chunks of 100 and 50 samples, then a read of 120, give the
    samples in order and a `pos` of chunk 1, frame 20.
  - `test_vplayq_flush_drops_late_data`: push reply 1, `flush(2)`, then push reply 1 again;
    `read` gives 0 frames. Push reply 2 and `read` gives it.
  - `test_vplayq_underrun_not_done`: push 100 frames of reply 3 (no end), then read 200. It
    returns 100 and `take_end` is 0. Push 50 more and `end(3)`, read, and `take_end` gives 3
    exactly once.
  - `test_vplayq_gain`: 18 % (Q16 11796) turns 10000 into 1799 or 1800 (the test states which,
    from the rounding chosen). 100 % leaves samples alone. -32768 at 100 % is unchanged.
  - `test_vplayq_end_without_data`: `end(4)` alone gives `take_end` 4 after a read of 0
    frames.
  - `test_vplayq_wrap_ids`: reply ids `0xFFFFFFFE` then `1` with `flush(1)`: the old one drops.
  - `test_vplayq_queued_frames`: the count follows pushes and reads, and is 0 after a flush.
- [ ] **Step 2: Run.** FAIL. **Step 3: Implement.** **Step 4: Run** `make -j test`. PASS.
- [ ] **Step 5: Commit** `feat(voice): a play queue for AI speech PCM`.

Suggested implementer: Sonnet. Haiku would also do, since the interface fixes it.

---

### Task 4: The conversation machine: external send, speech progress, the silence clock, the echo tick wrap

**Status: built and reviewed** (`13db061`). Unchanged by the 2026-11-25 provider revision; Task 13 replaces `test_vconv_ending_watchdog_keeps_silence_clock` with the stronger Ending rule (R29).

**Files:**
- Modify: `src/core/voice_conv.[ch]`, `src/core/voice_echo.c`
- Test: `tests/test_voice_conv.c`, `tests/test_voice_echo.c`

**Interfaces:**
- Produces, in `voice_conv.h`:
  - `VCE_EXTERNAL_SEND` and `VCE_SPEECH_PROGRESS`, appended to `VoiceConvEvent` before
    `VCE_TICK`;
  - `VCH_AI_FAILED`, appended to `VoiceConvHeader`, with text
    `AI speech failed: using Windows speech`;
  - `held_local` in `VoiceConv`.
- Rules:
  - `VCE_EXTERNAL_SEND`:
    - In LISTENING (paused or not), HEARING, ENDING with `!sending`, and SPEAKING, it goes to
      WORKING:
      - capture closed;
      - any turn dropped (`VCA_DROP_TURN`, link, batch and typing released);
      - speech stopped and the caption released (SPEAKING);
      - a held reply dropped (`VCA_DROP_HELD`);
      - `sent_at = now`, `idle_seen = 0`, `card = 0`;
      - the header is `VCH_WORKING`, or the pause header.
    - In WORKING, ENDING with `sending`, and IDLE it does nothing.
  - `VCE_SPEECH_PROGRESS` in SPEAKING sets `state_at = now` (the Speaking watchdog counts from
    the last progress) and changes nothing else.
  - The silence clock (notes items (q) and (s)):
    - the refusal path of `on_sent` leaves `local_speech = 1`;
    - `hold()` records `held_local = c->local_speech`, and the resume restores it after
      `enter_speaking`;
    - the Hearing watchdog uses `enter_listening` (not `_at`);
    - the Ending watchdog no longer sets `idle_at`.
  - `voice_echo.c`: both age checks become `(long)(now - x) <= LIMIT`. A time in the future
    still counts as inside the window.

- [ ] **Step 1: Write the failing tests**
  - External send:
    - `test_vconv_external_send_from_listening`: `VCA_CLOSE_CAPTURE`, state WORKING, header
      Working. Then `VCE_CHAT_IDLE(1, 0)` speaks the reply.
    - `test_vconv_external_send_from_hearing_and_speaking`: HEARING gives `VCA_DROP_TURN` and
      the link close. SPEAKING gives `VCA_STOP_SPEECH`. Both end in WORKING with the capture
      closed.
    - `test_vconv_external_send_ignored`: in WORKING, ENDING-while-sending and IDLE the output
      is `{0}`.
    - `test_vconv_external_send_paused`: paused LISTENING with a held reply gives WORKING, the
      pause header and `VCA_DROP_HELD`.
  - Speech progress:
    - `test_vconv_progress_refreshes_speaking_watchdog`: `SPEECH_PROGRESS` every 60 s for 15
      minutes keeps SPEAKING. Without it, the watchdog fires at 600 s.
  - The silence clock:
    - `test_vconv_refusal_phrase_keeps_silence_clock`: a `VSS_FULL` refusal, its phrase done,
      then 180 s after the last real turn ends the conversation.
    - `test_vconv_paused_local_repeat_keeps_silence_clock`: a local repeat paused, resumed and
      done does not restart the clock.
    - `test_vconv_hearing_watchdog_keeps_silence_clock` and
      `test_vconv_ending_watchdog_keeps_silence_clock`: after either watchdog, `VCE_TICK` at
      180 s from the last turn sent ends the conversation.
  - Header text:
    - `test_vconv_ai_failed_header_text`: `voice_conv_header_text(VCH_AI_FAILED)` is the
      Global Constraints text.
  - The tick wrap:
    - `test_vturn_echo_window_tick_wrap`: `spoken_end_at = (unsigned long)-500` and
      `now = 1000` (1500 ms later) gives `VTC_ECHO` for a matching transcript;
      `now = 2600` gives `VTC_TEXT`.
    - `test_vturn_phrase_age_tick_wrap`: the same for `reply_at` and `VOICE_PHRASE_MAX_AGE_MS`.
- [ ] **Step 2: Run.** FAIL. **Step 3: Implement.** **Step 4: Run** `make -j test`. PASS, and
  every existing `test_vconv_*` still passes.
- [ ] **Step 5: Commit** `fix(voice): external sends, speech progress and the silence clock`.

Suggested implementer: Sonnet.

---

### Task 5: Streaming POST on a voice session, never redirected

**Status:** to build. Revised 2026-11-25: the loopback server speaks WAV and Mistral's events.

**Files:**
- Modify: `src/core/ai_http.h` (CRLF), `src/ui/ai_http_win.c` (CRLF; the non-Windows stub too),
  `src/ui/voice_net_win.h` (the `VNK_TTS` comment: now used),
  `build-support/delayload/winhttp.def` (the header comment names AI speech; no new entry)
- Create: `tests/http_loopback_win.h`, `tests/http_loopback_win.c` (a test helper, listed in
  `WIN_TEST_SRCS`)
- Test: `tests/test_voice_net_win.c` (extend)

**Interfaces:**
- Produces:
```c
/* ai_http_post_stream on a session the caller owns (as ai_http_post_ct_ex): never opens or
 * closes it; connect 10 s, send 30 s, receive recv_ms; the request never follows a redirect
 * (WINHTTP_OPTION_REDIRECT_POLICY = NEVER), so a 3xx is returned as the status. NULL session:
 * -1, "No session". Same callback, status, error and cancel contract as ai_http_post_stream. */
int ai_http_post_stream_ex(void *session, const char *url, const char * const *headers,
                           const char *body, size_t body_len, int recv_ms,
                           AiStreamCallback cb, void *userdata,
                           int *status_out, char *error, size_t error_size, AiNetCancel *nc);
```
- `http_loopback_win.h`:
  - modes:
    - `HLS_WAV`: 200 and `Transfer-Encoding: chunked`. The body is a WAV with `0xFFFFFFFF`
      sizes, a `LIST` chunk before `data`, and a parameterised format (rate, channels, 16-bit
      or float). It is sent in writes of 1, 7 (splitting the header), 4,095 and the rest.
    - `HLS_SSE`: Mistral-like `text/event-stream`: `event: speech.audio.delta` and
      `data: {"type":"speech.audio.delta","audio_data":"<base64 of a WAV piece>"}` lines,
      three deltas, then `event: speech.audio.done` and `data: {"type":"speech.audio.done"}`.
    - `HLS_SSE_ERROR`: one delta, then `data: {"error":{"message":"x"}}`.
    - `HLS_401`: a JSON error body.
    - `HLS_HANG`: reads the request and never answers.
    - `HLS_302`: a redirect to itself, counting requests.
  - `hl_start(mode, const HlAudio *a)` with `HlAudio { int rate, channels, float32;
    double seconds; }`, plus `hl_port`, `hl_stop` (joins every thread), `hl_request_head`,
    `hl_request_body` and `hl_requests`.
  - The generated samples are a known ramp, so a test can check them.
- Refactor `ai_http_post_stream_impl` to take the session, as `ai_http_post_ct_ex`'s impl does.
  `ai_http_post_stream` keeps its own session and its behaviour.

- [ ] **Step 1: Write the failing wintests**
  - `test_ai_stream_ex_wav_bytes`: through `voice_net_open(VNK_TTS)`, an `HLS_WAV` body arrives
    whole and in order across callbacks, with status 200. `voice_net_open_count()` went up by
    exactly 1.
  - `test_ai_stream_ex_sse`: the `HLS_SSE` text arrives whole, byte for byte.
  - `test_ai_stream_ex_401`: status 401, rc -1, and the error holds the JSON body.
  - `test_ai_stream_ex_no_redirect`: `HLS_302` gives status 302 and exactly 1 request.
  - `test_ai_stream_ex_cancel_hang`: `ai_nc_fire` from another thread returns within 2 s, or a
    defined Wine SKIP as `test_ai_nc_fire_wakes_blocked_winhttp_call` does.
  - `test_ai_stream_ex_null_session`: rc -1 and "No session".
- [ ] **Step 2: Run** `xvfb-run -a make wintest`. FAIL. **Step 3: Implement.**
  **Step 4: Run** `xvfb-run -a make wintest`, `make -j test` and `make -j release`. PASS.
- [ ] **Step 5: Commit** `feat(voice): streaming POST on a voice session, never redirected`.

Suggested implementer: Sonnet. Reviewer: Opus or Fable.

---

### Task 6: The render client on the audio thread

**Status: built** (`5b47a28`), fix round in progress (`31f9a20` and after). Unchanged by the 2026-11-25 provider revision: every stream reaches it as 24 kHz mono PCM16 (R26), so no follow-up task is needed.

**Files:**
- Modify: `src/ui/voice_audio_win.[ch]`
- Test: `tests/test_voice_render_win.c` (new), plus `Makefile` (CRLF: `WIN_ONLY_TESTS`) and
  `tests/win_runner.c`

**Interfaces:**
- Consumes: Task 3's `VoicePlayQ`.
- Produces, in `voice_audio_win.h`:
```c
/* VoiceAudioMsg additions */
VAM_RENDER_OPENED,  /* lParam = VOICE_ERR_PACK(0, token) */
VAM_PLAYED,         /* lParam = VoiceRenderMsg* (receiver frees with free), every 100 ms while playing */
VAM_PLAY_DONE,      /* lParam = VoiceRenderMsg*: reply_id's end marker played out (padding 0) */
VAM_RENDER_ERROR    /* lParam = VOICE_ERR_PACK(err, token); VAE_DEVICE_LOST, VAE_OPEN_FAILED,
                       VAE_NO_DEVICE, VAE_RENDER_PREEMPTED */
typedef struct { unsigned token, reply_id, chunk; uint64_t frames_in_chunk; } VoiceRenderMsg;
int  voice_audio_render_open(unsigned token);       /* push; 1 queued, 0 no thread */
int  voice_audio_render_data(unsigned token, unsigned reply_id, unsigned chunk,
                             int16_t *s, size_t n);  /* takes s on 1; 0: caller still owns s */
void voice_audio_render_end(unsigned token, unsigned reply_id);
void voice_audio_render_flush(unsigned token, unsigned min_reply);   /* cannot be lost */
void voice_audio_render_gain(unsigned token, int percent);
void voice_audio_render_close(unsigned token);                       /* cannot be lost */
long voice_audio_render_queued_ms(void);           /* any thread, interlocked; 0 with none */
/* TEST-ONLY seams, never product code: */
void voice_audio_test_render_null(int on);         /* a 10 ms waitable timer and a simulated
                                                      100 ms buffer draining in real time stand
                                                      in for WASAPI */
uint64_t voice_audio_test_render_frames(unsigned reply_id);  /* frames written for reply_id */
int  voice_audio_test_render_peak(void);           /* largest |sample| the null sink took since the last call */
void voice_audio_test_fail_push(int n);            /* the next n raw pushes fail as on OOM */
```
- On the audio thread:
  - `render_open` activates `IAudioClient` on the default `eRender`/`eConsole` endpoint:
    shared mode, 24 kHz mono 16-bit, `EVENTCALLBACK | AUTOCONVERTPCM |
    SRC_DEFAULT_QUALITY`, a 100 ms buffer. It does `GetService(IAudioRenderClient)` and does
    not start the client.
  - A previous owner gets `VAE_RENDER_PREEMPTED` (R7).
  - Data starts the client. Each render event (or 100 ms timeout) fills `buffer - padding`
    frames from the `VoicePlayQ`.
  - When `take_end` has fired and padding is 0, the thread posts `VAM_PLAY_DONE` and calls
    `IAudioClient_Stop` and `Reset` (R6).
  - Flush drops the queue (`voice_playq_flush`), then Stop and Reset.
  - Close releases everything. So does thread exit: `va_thread`'s exit path closes the render
    client after `va_finish`.
  - A device invalidated, or no render event for 2 s while started, gives
    `VAM_RENDER_ERROR(VAE_DEVICE_LOST)`.
  - The wait loop adds the render event to its handles while a render client is open, and
    uses a 100 ms timeout while it is started.
- `IID_IAudioRenderClient` is copied locally beside the other IIDs (no `libuuid`). Check the
  value against MinGW's `audioclient.h`; copy it under a guard only if the header lacks it
  (§7.5).
- Pending fields under `qcs` for notes item (r). When `va_push_raw` fails for
  `voice_audio_conv_end`, `voice_audio_render_flush` or `voice_audio_render_close`, the call
  records `pend_conv_end`, `pend_flush` (token, min reply) or `pend_close` and sets `wake`. The
  thread applies them after draining the queue, so a close cannot be lost.

- [ ] **Step 1: Write the failing wintests** (the gate on and the audio thread started, as the
  existing `test_voice_audio_win.c` does):
  - `test_voice_render_null_plays_and_done`: with the null sink, open, push 0.5 s of reply 1,
    then `end(1)`. `VAM_PLAYED` arrives with a rising `frames_in_chunk`, then one
    `VAM_PLAY_DONE` for reply 1. Test frames are 12 000 ± 1.
  - `test_voice_render_flush_then_old_data`: push reply 1, flush(2), then push reply 1 again:
    `voice_audio_test_render_frames(1)` does not grow after the flush.
  - `test_voice_render_gain`: gain 18 then data. The null sink records the peak written, which
    is 18 % ± 1 of the input's.
  - `test_voice_render_preempt`: open A then open B gives `VAM_RENDER_ERROR(VAE_RENDER_PREEMPTED)`
    for A.
  - `test_voice_render_real_wine`: the real WASAPI path under headless Wine. Open, push and
    close give a defined outcome within 3 s (OPENED and DONE, or `VAM_RENDER_ERROR`), with no
    hang and no leaked thread.
  - `test_voice_audio_close_cannot_be_lost`:
    `voice_audio_test_fail_push(1)` then `voice_audio_conv_end(t)` on a test conversation
    state (`voice_audio_test_conv_open`): a later `voice_audio_test_conv_ingest(t, ...)` posts
    no conversation event.
    The same for `render_close`: a later `render_data` for that token is not played.
- [ ] **Step 2: Run.** FAIL. **Step 3: Implement.** **Step 4: Run**
  `xvfb-run -a make wintest`, `make -j test` and `make -j release`. PASS. The existing
  `test_voice_audio_*` tests are unchanged.
- [ ] **Step 5: Commit** `feat(voice): a render client and play queue on the audio thread`.

Suggested implementer: Sonnet.

---

### Task 7: The AI speech engine

**Status:** to build. Revised 2026-11-25: two response shapes through the WAV decoder, and the
build's lessons on the render client and play queue (R30, R31).

**Files:**
- Create: `src/ui/voice_aispeech_win.h`, `src/ui/voice_aispeech_win.c`,
  `tests/test_voice_aispeech_win.c`
- Modify:
  - `src/core/voice_playq.c` and `tests/test_voice_playq.c` (R31 only);
  - `Makefile` (CRLF: `WIN_ONLY_TESTS`, and `WIN_TEST_SRCS` with `http_loopback_win.c`);
  - `tests/win_runner.c`.

**Interfaces:**
- Consumes:
  - Task 2: `VoiceAisTarget`, `voice_ais_body`, `voice_ais_sse_*`, `voice_ais_offset` and
    `voice_speak_chunks_lim`;
  - Task 12: `VoiceWavStream`;
  - Task 5: `ai_http_post_stream_ex`;
  - Task 6: the render API as built;
  - `sse_line`, `voice_net_open(VNK_TTS)` and `VoiceTtsMsg`/`WM_VOICE_TTS`.
- Produces:
```c
#define WM_VOICE_AIS (WM_APP + 0x49)      /* lParam = VoiceAisMsg*, receiver frees */
typedef enum { VAI_CHUNK_DONE = 1, VAI_FAILED, VAI_EXITED } VoiceAisKind;
typedef struct { unsigned token, reply_id, chunk; VoiceAisKind kind;
                 uint64_t frames; int http_status; } VoiceAisMsg;
#define VOICE_AIS_SAMPLE_TOKEN 0x7FFFFFF2u
int  voice_ais_start(HWND notify, unsigned token);   /* the one constructor: 0 unless voice_gate_on()
                                                        and the audio thread is up; no thread, no
                                                        render yet */
void voice_ais_speak(unsigned token, unsigned reply_id, const char *utf8, size_t from,
                     const VoiceAisTarget *t, const char *key);   /* copies text, target, key */
void voice_ais_stop(unsigned token);
void voice_ais_volume(unsigned token, int percent);
void voice_ais_quit(unsigned token, unsigned wait_ms);
void voice_ais_quit_all(unsigned wait_ms);
int  voice_ais_sample(HWND notify, const VoiceAisTarget *t, const char *key);
int  voice_ais_running(void);
int  voice_ais_owns(unsigned token);
void voice_ais_on_audio(UINT kind, LPARAM lp);   /* VAM_RENDER_* / VAM_PLAYED / VAM_PLAY_DONE; frees */
void voice_ais_on_msg(VoiceAisMsg *m);           /* WM_VOICE_AIS; frees */
void voice_ais_on_timer(UINT_PTR id);            /* the done guard (R30) */
#define VOICE_AIS_TIMER_BASE 0x56F4              /* + context slot (0 or 1); check it is free */
```
- **Contexts.** At most two, the conversation's and the sample's, keyed by token.
  - Each context pushes `render_open` once, on its first speak. `VRES_RENDER` is acquired only
    then: a same-token `render_open` closes and reopens silently, so it is never pushed twice.
  - Right after `render_open`, the context sends its current gain when it is not 100: every open
    starts at 100 %, and the gain belongs to the current owner.
  - `quit` fires cancel, flushes, does a bounded join (detaching and releasing after `wait_ms`),
    pushes `render_close` and releases `VRES_RENDER`.
- **`speak`:**
  - fire the old worker's cancel and `render_flush(token, reply_id)` first, so two replies are
    never queued back to back;
  - chunk `utf8[from..]` with `voice_speak_chunks_lim(t->first_chunk, t->later_chunk)` and keep
    each chunk's `(start, len)`;
  - start one worker (`VRES_SPEECH_REQ`).
- **The worker** (R8), for each chunk:
  - wait until `voice_audio_render_queued_ms() < 20000` (50 ms slices, cancel-aware);
  - `voice_net_open(VNK_TTS)`; build the body; send the header `Authorization: Bearer <key>`
    only when the key is not empty, plus `Accept: text/event-stream` for the Mistral shape;
  - call `ai_http_post_stream_ex` with `recv_ms` 60,000.
  - Callback by shape:
    - OPENAI: bytes go straight to `voice_wavs_feed`.
    - MISTRAL: bytes go to `sse_line_feed`, then `voice_ais_sse_line`, and each AUDIO event's
      bytes go to `voice_wavs_feed`. DONE ends the chunk; ERROR fails it.
  - Each feed's output goes to `render_data(token, reply, k, samples, n)`. If that push fails,
    the worker frees the samples (a glitch, not a failure).
  - Chunk end: `voice_wavs_finish`, push the tail, then `VAI_CHUNK_DONE(k, frames)`.
  - Any failure (status not 200, transport, gate off, `VWS_ERROR`, a Mistral ERROR, or a
    Mistral body that ends with no DONE) gives `render_end`, then `VAI_FAILED(k, status)`, and
    stops. The decoder's error text goes to the debug log, with no content.
  - At the end: `render_end` and `VAI_EXITED`. Wipe the key, the body and the text, and close
    the session.
- **The UI side:**
  - `VAM_PLAYED` gives `VTT_WORD(offset)` through `voice_ais_offset` on the current reply's
    chunk table (`total` from `VAI_CHUNK_DONE`).
  - `VAM_PLAY_DONE` for the current reply gives `VTT_DONE` if every chunk completed, else
    `VTT_ERROR` with the mapped resume offset and `hr` = the status (or -1). A `VAM_PLAY_DONE`
    for an older id is ignored.
  - `VAM_RENDER_ERROR` while playing gives `VTT_ERROR` at the current offset.
  - A target with an empty `url` (the resolve failed) gives an immediate `VTT_ERROR` at `from`.
  - `VAI_EXITED` joins the worker (non-blocking) and releases `VRES_SPEECH_REQ`. If every chunk
    completed and no `VAM_PLAY_DONE` has come, it arms the done guard (R30): a one-shot timer
    of `voice_audio_render_queued_ms() + 2000` that posts `VTT_DONE` unless `VAM_PLAY_DONE`
    came first.
  - Every message is checked against the current reply id. The sample context quits itself at
    its `VTT_DONE` or `VTT_ERROR`.
- **R31, in `voice_playq.c`:** the position's `frames_in_chunk` accumulates across consecutive
  blocks with the same `reply_id` and `chunk`, and restarts at 0 when either changes.

- [ ] **Step 1: Write the failing tests**
  - Native (R31): `test_vplayq_pos_accumulates_across_blocks`: three blocks of 100 frames, all
    reply 1 chunk 0, then a block for chunk 1. Reading 250 frames gives `frames_in_chunk` 250;
    reading 100 more gives chunk 1 frame 50.
  - Wintests: the null render sink, `http_loopback`, a message-only window that pumps
    `WM_VOICE_TTS`, `WM_VOICE_AIS`, `WM_VOICE_AUDIO` and `WM_TIMER` into the module, and the
    ledger reset per test.
    - `test_voice_ais_gate_off`: `start` gives 0, no thread, ledger 0.
    - `test_voice_ais_speak_done`: `HLS_WAV` (24 kHz mono 16-bit, 0.25 s) and `One. Two.`
      through a Custom target with model `kokoro` and voice `bf_emma`:
      - the head has `Authorization: Bearer test-key`, and the body has `"input":"One. Two."`
        and `"response_format":"wav"`;
      - `voice_net_open_count()` went up by 1;
      - `voice_audio_test_render_frames(reply)` is 6,000 ± 1;
      - the `VTT_WORD` offsets do not decrease and stay within [0, 9];
      - exactly one `VTT_DONE`;
      - after `quit`, the ledger is zero.
    - `test_voice_ais_mistral_sse`: `HLS_SSE` (24 kHz float32) through a Mistral-shaped target
      pointed at the loopback URL gives exactly one `VTT_DONE`, with the frames written equal to
      the decoded duration ± 1. The body has `voice_id` and `"stream":true`.
    - `test_voice_ais_sse_error_event`: `HLS_SSE_ERROR` gives `VTT_ERROR`, and no `VTT_DONE`.
    - `test_voice_ais_resampled_rate`: `HLS_WAV` at 22,050 Hz stereo for 0.5 s gives 12,000 ± 2
      frames written.
    - `test_voice_ais_groq_chunks`: a 600-byte text through a target with 200/200 limits makes
      at least three requests (counted by the server), each body's `input` 200 bytes or less,
      and one `VTT_DONE`.
    - `test_voice_ais_http_401`: `VTT_ERROR` with offset 0 and `hr` 401, and no `VTT_DONE`.
    - `test_voice_ais_stop_then_speak_no_old_audio`: speak reply 1 (a 3 s body), stop after its
      first `VAM_PLAYED`, then speak reply 2. `voice_audio_test_render_frames(1)` does not grow
      after the stop.
    - `test_voice_ais_gain_survives_open`: volume 18 before the first speak; the null sink's
      peak is 18 % ± 1 of the input's.
    - `test_voice_ais_render_ledger_once`: two speaks and a quit: `VRES_RENDER` was 1 between
      them and is 0 after.
    - `test_voice_ais_done_guard`: drop the `render_end` with `voice_audio_test_fail_push`. The
      reply still gets `VTT_DONE` within the queued audio plus 2.5 s.
    - `test_voice_ais_quit_while_server_hangs`: `HLS_HANG`, then speak and `quit(token, 3000)`
      return within 4 s, with the ledger zero.
    - `test_voice_ais_sample_self_quits`: `voice_ais_sample` then DONE leave
      `voice_ais_running()` at 0 and the ledger at 0.
- [ ] **Step 2: Run.** FAIL. **Step 3: Implement.** **Step 4: Run**
  `xvfb-run -a make wintest`, `make -j test` and `make -j release`. PASS.
- [ ] **Step 5: Commit** `feat(voice): AI speech engine streaming WAV through the render client`.

Suggested implementer: Sonnet. Reviewer: Opus.

---

### Task 8: The conversation glue: engine choice, fallback, progress, external send, routing

**Status:** to build. Revised 2026-11-25: the build's three glue lessons, and the router for the
new provider set.

**Files:**
- Modify:
  - `src/ui/voice_conv_win.[ch]` (the ops change of R20, the fallback, progress, external send,
    the notice fixes, the pending end, and the three lessons below);
  - `src/ui/voice_win.[ch]`:
    - the real ops router;
    - `rc_notice_due` returns 0 with AI speech;
    - `voice_feature_apply`/`voice_feature_shutdown` call `voice_ais_quit_all(3000)` before
      `voice_audio_stop`;
    - `voice_on_audio_msg` routes the render messages to `voice_ais_on_audio` before
      `settings_voice_on_audio`;
    - new `voice_on_ais(LPARAM)`, `voice_on_ais_timer(UINT_PTR)` and
      `voice_on_external_send(void)`;
  - `src/ui/window.c` (route `WM_VOICE_AIS` and the `VOICE_AIS_TIMER_BASE` timers);
  - `src/ui/ai_chat.c` (the Retry handler's two exits call `voice_on_external_send()`, R23).
- Test: `tests/test_voice_conv_win.c` (update the fakes for R20, then the new tests)

**Interfaces:**
- Consumes: Task 4 (built: `VCE_EXTERNAL_SEND`, `VCE_SPEECH_PROGRESS`, `VCH_AI_FAILED`), Task 7,
  Task 2 (`voice_ais_resolve`, `voice_ais_key`) and Task 1.
- Produces:
  - `VoiceConvOps.tts_start(unsigned token, int engine)`;
  - optional `int (*speech_engine)(void)`;
  - `void voice_conv_external_send(void)`. It takes the reply baseline (`g_since =
    msg_count()`, `g_sent_tick`, `g_busy_seen = !chat_idle()`) in every non-idle state except
    ENDING-while-sending, then feeds `VCE_EXTERNAL_SEND`;
  - TEST-ONLY seams `voice_conv_test_queue(VoiceConvEvent, int, int)` and
    `voice_conv_test_pump(void)`.
- Glue rules:
  - **The engine.** `g_engine_cfg` is read at conversation start. Every `begin_speak` makes sure
    the context's engine is `g_engine_cfg`, quitting and restarting it if not. An AI start that
    fails falls back (R10).
  - **The fallback**, on `VTT_ERROR` for the current id while the context is AI:
    - show `VCH_AI_FAILED`;
    - quit the AI context;
    - start a Windows context;
    - `begin_speak` a **copy** of `g_spoken` from `m->offset`, with the same reply or phrase
      kind.
    - A Windows `VTT_ERROR` then follows today's path.
  - **Progress.** Every `VTT_WORD` for the current id feeds `VCE_SPEECH_PROGRESS`, at most once
    every 1,000 ms.
  - **The notice** (notes item (o)). `g_notice_raised` is set when `notice_voice` is called.
  - **The pending end** (notes item (r)). A `VCE_END` with the queue full goes to its own slot
    (the first end wins), and `pump` runs it after the queue. Other overflow is dropped with a
    `DLOG` warning.
  - **Lessons from the build:**
    - The `VCA_DROP_TURN` that comes with `VCE_EXTERNAL_SEND` discards the turn's audio and link
      but never removes words from the box. The send just made may have used them, and spec
      §5.6 keeps an unfinished turn's words. A flag set around that one event makes
      `drop_turn()` skip `drop_words`.
    - An external send from a ducked reply emits no `VCA_UNDUCK`. `stop_speech()` therefore
      sets the volume back to 100 whenever it stops a context that is ducked.
    - When a new reply loads (`speak_reply`), `g_reply_at` and `g_spoken_end_at` are zeroed
      before it is spoken, so the echo window and phrase age never see the previous reply's
      times.
  - **The real router** in `voice_win.c` keeps the engine per token.
    - `rc_tts_start(token, VOICE_SPEECH_AI)` calls `voice_ais_start(g_main, token)`.
    - `rc_tts_speak` for an AI token resolves `voice_ais_resolve(s, &t, ...)`. A failure clears
      `t.url` and logs the error text (no content); the engine then fails at `from`. It then
      calls `voice_ais_speak(token, id, text, from, &t, voice_ais_key(s, &t))`.
    - `rc_speech_engine` returns `s->voice_speech_engine` in Conversation mode.

- [ ] **Step 1: Write the failing wintests**, with the fake ops recording the engine per token
  and doing the ledger's acquire and release:
  - The engine and the fallback:
    - `test_vconv_win_ai_engine_started`, `test_vconv_win_ai_fallback_rest_of_reply`,
      `test_vconv_win_ai_fail_every_reply`, `test_vconv_win_ai_and_windows_fail` and
      `test_vconv_win_ai_stale_error_ignored`: as in `a3b012f`.
      - The fallback gives the header `AI speech failed: using Windows speech`, `tts_quit(ai)`,
        `tts_start(win, VOICE_SPEECH_WINDOWS)` and `tts_speak(win, ..., text, 40)`. The next
        reply returns to AI from 0.
      - Three failing replies each fall back, with no chat note and the ledger zero at the end.
      - AI then Windows failing gives `Could not speak the reply: it is on screen`.
  - The notice:
    - `test_vconv_win_no_notice_with_ai` and `test_vconv_win_notice_kept_if_ended_first`.
  - External send:
    - `test_vconv_win_external_send`: in Listening it gives `cap_end` and WORKING, and the reply
      after the new baseline is spoken.
    - `test_vconv_win_external_send_keeps_words`: while Hearing with live words in the box, an
      external send gives no `drop_words` call (the fake records it), and the turn's link is
      closed.
    - `test_vconv_win_external_send_unducks`: Speaking, then `VCE_DUCK` (volume 18), then an
      external send gives `tts_volume(token, 100)` before `tts_stop`, or with it.
  - The reply's times:
    - `test_vconv_win_new_reply_resets_times`: a reply spoken and ended, then 6 minutes later a
      new reply loads. A transcript "carry on" during it is classified against the new reply's
      times only: no stale echo window, and the phrase age counted from the new reply.
  - Progress and the pending end:
    - `test_vconv_win_progress_keeps_long_reply` and `test_vconv_win_end_never_dropped`: as in
      `a3b012f`.
- [ ] **Step 2: Run.** FAIL. **Step 3: Implement.** Grep every caller of
  `ai_chat_launch_stream_thread` and every send path outside `ai_chat_send_user_message_ex`.
  List them in the commit body, and add `voice_on_external_send()` where a user action launches
  a request (R23). **Step 4: Run** `make -j test`, `xvfb-run -a make wintest` and
  `make -j release`. PASS, and the exe links.
- [ ] **Step 5: Commit** `feat(voice): AI speech in conversations, with the Windows speech fallback`.

Suggested implementer: Sonnet. Reviewer: Opus (the integration task).

---

### Task 9: Settings > Voice: Speech, the AI rows, Play sample with AI speech

**Status:** to build. Revised 2026-11-25: three providers, per-provider seeds, and the style row
for Custom only.

**Files:**
- Modify: `src/ui/settings.c`, `src/ui/resource.h` (the ids of the Global Constraints),
  `src/core/voice_config.[ch]` (a pure enables helper)
- Test: `tests/test_voice_config.c`

**Interfaces:**
- Consumes:
  - Task 1 (fields, help ids);
  - Task 2 (`voice_ais_providers`, `voice_ais_provider_find`, `voice_ais_key_shared`,
    `voice_ais_resolve`, `voice_ais_key`);
  - Task 7 (`voice_ais_sample`);
  - PR 2's `voice_tts_sample`.
- Produces:
```c
typedef struct { int speech; int ai_rows; int url; int key; int style; int uk_line; } VoiceSpeechEnables;
void voice_speech_enables(int conversation_enabled, int engine, const char *provider,
                          int key_shared, int uk_voice_installed, VoiceSpeechEnables *out);
```
  - `speech`: Conversation mode.
  - `ai_rows`: Conversation and AI.
  - `url`: shown for Custom.
  - `key`: shown when `!key_shared`.
  - `style`: enabled for Custom with AI.
  - `uk_line`: shown with Windows and no en-GB woman's voice.
- **Rows** (R16):
  - Speech: `Windows speech` / `AI speech`.
  - Speech provider: `Mistral` / `Groq` / `Custom`, from `voice_ais_providers`.
  - Base URL: an edit with the clear-text warning line.
  - Speech API key: a password edit.
  - AI voice: an editable combo.
  - AI speech model: an editable combo.
  - Accent and style: a multi-line edit, three lines tall, `ES_AUTOVSCROLL`, limited to 1,023
    bytes.
- **Seeding.** On every provider change, the voice and model combos are reseeded from the
  provider's lists. For Mistral and Groq the first entry is the provider's default, and when it
  is selected, Save stores `""`. Custom's lists are suggestions with no default; its combos open
  empty unless a value is saved.
- **Events.** `CBN_SELCHANGE` on Speech or Speech provider, and `EN_CHANGE` on either URL,
  call `voice_enable_sync`, which applies `voice_speech_enables`. The visibility of the key and
  URL rows relays the page out.
- **Reading the page.** Factor the Voice page's save code into `static void
  voice_page_read(SettingsDlgData *d, Settings *s)`. Save uses it on the real settings.
  `voice_ais_key_shared` and Play sample use it on a copy of `d->cfg->settings` (R17), and the
  copy's key fields are wiped after use. Save does `config_secret_drop_stale_preserved` for
  `tts_api_key`. A hidden key row leaves the key unchanged (R13).
- **Play sample.** With AI speech: `voice_ais_resolve` on the copy, then
  `voice_ais_sample(owner, &t, key)`. A resolve failure shows its `err` in the page's dim
  test-note line. Windows speech: today's call.

- [ ] **Step 1: Write the failing test** `test_voice_speech_enables`:
  - Dictation: everything off.
  - Conversation with Windows: speech on, ai_rows off, `uk_line` as installed.
  - Conversation, AI, Mistral, shared: no key, no url, no style.
  - Groq, not shared: key shown.
  - Custom: url, key (not shared) and style on.
  - AI: `uk_line` hidden.
- [ ] **Step 2: Run.** FAIL. **Step 3: Implement.** **Step 4: Run** `make -j test`,
  `xvfb-run -a make wintest` and `make -j release`. Optionally run
  `wine build/win/nutshell.exe --ui-demo=settings-voice` and report what shows: the rows, the
  greying, the reseeding on a provider change, and the key row hidden with Mistral on both.
- [ ] **Step 5: Commit** `feat(voice): Settings > Voice gains Speech and the AI speech rows`.

Suggested implementer: Sonnet. Reviewer: Opus or Fable.

---

### Task 10: The self-report: AI speech mode and the resource count

**Status:** to build. Revised 2026-11-25: the config names a Custom model and voice.

**Files:**
- Modify: `src/ui/dll_hook_win.c` (a `voice-resources: N` line after `voice-net-opens`),
  `tests/selftest/check.sh`, `Makefile` (CRLF: the `ai` mode in `selftest-wine`'s loop and
  `case`), `tests/test_selftest_configs.c`
- Create: `tests/selftest/nutshell-voice-ai.config`: the conv config plus
  `"voice_speech_engine": "ai"`, `"voice_tts_provider": "custom"`,
  `"voice_tts_url": "http://127.0.0.1:9/v1"`, `"voice_ai_tts_model": "kokoro"` and
  `"voice_ai_voice": "bf_emma"`.

**Interfaces:**
- `voice-resources: N` is the sum of `voice_ledger_count(r)` over every `VoiceRes`, written on
  the UI thread.
- `check.sh`:
  - every mode except `sapi` asserts `voice-resources: 0`;
  - `sapi` asserts only that the line exists;
  - the new `ai` mode asserts winhttp, riched20, msftedit and mmdevapi were requested, mmdevapi
    is mapped, `no_speech ai`, `voice-net-opens: 0` and `voice-resources: 0`;
  - the usage line lists `off|voiceoff|on|conv|sapi|ai`.

- [ ] **Step 1:** extend `tests/test_selftest_configs.c` for the new config, and extend
  `check.sh`. It fails until the line exists.
- [ ] **Step 2: Run** `make -j test`, `make -j release` and `make selftest-wine`: `ai` is
  missing or fails.
- [ ] **Step 3: Implement.** **Step 4: Run** all three. Every mode passes.
- [ ] **Step 5: Commit** `test(selftest): AI speech mode opens nothing until a reply is spoken`.

Suggested implementer: Haiku. Reviewer: Opus or Fable.

---

### Task 12 (new): The WAV stream decoder and the 24 kHz converter

**Status:** to build (new, 2026-11-25). Run it after Task 2 and before Task 7.

**Files:**
- Create: `src/core/voice_wavstream.h`, `src/core/voice_wavstream.c`
- Test: `tests/test_voice_wavstream.c`

**Interfaces:**
- Produces:
```c
#define VOICE_WAVS_OUT_RATE 24000
typedef enum { VWS_HEADER = 0, VWS_DATA, VWS_ERROR } VoiceWavState;
typedef struct { /* builder's fields: header buffer (<= 4096 bytes), format, channels, rate,
                    partial-frame carry, the resampler's history and phase */ } VoiceWavStream;
void   voice_wavs_init(VoiceWavStream *w);
size_t voice_wavs_feed(VoiceWavStream *w, const unsigned char *in, size_t n,
                       int16_t *out, size_t cap);     /* 24 kHz mono PCM16 out; returns samples */
size_t voice_wavs_out_max(const VoiceWavStream *w, size_t n_in);   /* bound for one feed */
size_t voice_wavs_finish(VoiceWavStream *w, int16_t *out, size_t cap);  /* the resampler's tail */
VoiceWavState voice_wavs_state(const VoiceWavStream *w);
int    voice_wavs_rate(const VoiceWavStream *w);       /* the source rate, 0 before the header */
const char *voice_wavs_error(const VoiceWavStream *w); /* "" or a short reason, never content */
void   voice_wavs_wipe(VoiceWavStream *w);             /* secure_zero the carried bytes */
```
- **Header** (R25):
  - `RIFF`, any size, then `WAVE`.
  - Chunks before `data` are walked by size: `fmt ` is read; `LIST`, `fact` and the rest are
    skipped.
  - The `data` size is ignored: everything after it is audio until the body ends.
  - Accepted formats: tag 1 with 16 bits; tag 3 with 32 bits; tag 0xFFFE whose subformat GUID
    starts with 0x0001 or 0x0003, with those widths.
  - Channels 1 or 2. Rate 8,000 to 48,000.
  - Anything else is `VWS_ERROR`, with the reason (`"unsupported WAV format"`,
    `"not a WAV stream"`, `"WAV header too long"`).
- **Conversion** (R26):
  - Frames are split anywhere across feeds and carried.
  - Stereo is averaged to mono. Float is clamped to [-1, 1] and scaled by 32,767 with rounding.
  - At 24,000 Hz the samples pass through unchanged.
  - Any other rate goes through a streaming windowed-sinc resampler:
    - 32 taps, a Kaiser window (beta 7) and a 256-phase coefficient table;
    - linear interpolation between adjacent phases;
    - cutoff 0.45 × min(source rate, 24,000), as a fraction of the source rate;
    - each phase normalised to a DC gain of exactly 1;
    - int64 accumulation, saturated to int16.
  - The resampler state carries 32 inputs, so chunking never changes the output.
  - The existing 16-to-24 kHz `voice_resample` stays for the live transcription path; this one
    is general and separate.

- [ ] **Step 1: Write the failing tests**:
  - Pass-through and splits:
    - `test_vwav_pass_24k_mono16`: a header plus a ramp gives the ramp exactly.
    - `test_vwav_split_everywhere`: the same stream fed at every split point from 1 to its
      length, and in 1-byte pieces, gives byte-identical output.
    - `test_vwav_placeholder_sizes`: `RIFF`/`data` sizes of `0xFFFFFFFF` and of 0 both read to
      the end, and a `LIST` chunk before `data` is skipped.
    - `test_vwav_extensible`: tag 0xFFFE with float32 and 16-bit subformats decode.
  - Sample formats:
    - `test_vwav_float32`: -1.5, -1, 0, 0.5 and 1 give -32767, -32767, 0, 16384 and 32767.
    - `test_vwav_stereo_average`: L 1000 and R 3000 give 2000.
  - Resampling:
    - `test_vwav_resample_lengths`: 1 s of input at 8,000, 16,000, 22,050, 44,100 and 48,000 Hz
      gives 24,000 ± 2 samples (including `finish`), whatever the chunking.
    - `test_vwav_resample_dc_and_tone`: DC 10,000 stays 10,000 ± 2 after the transient. A 1 kHz
      tone at -6 dBFS keeps its RMS within 0.5 dB from every rate.
    - `test_vwav_resample_alias`: a 15 kHz tone at 48 kHz comes out at least 30 dB down.
    - `test_vwav_full_scale_no_overflow`: a full-scale square wave from 44,100 Hz saturates and
      never wraps.
  - Errors:
    - `test_vwav_rejects_unsupported`: 8-bit, 24-bit, 3 channels, 96 kHz, `OggS` and garbage
      each give `VWS_ERROR`, with no output.
- [ ] **Step 2: Run.** FAIL. **Step 3: Implement.** No Win32, no I/O, no logging, and no heap
  in the per-sample path. **Step 4: Run** `make -j test`. PASS.
- [ ] **Step 5: Commit** `feat(voice): a streaming WAV decoder that converts to 24 kHz mono`.

Suggested implementer: Sonnet. Reviewer: Fable (signal processing) or Opus.

---

### Task 13 (new): The Ending watchdog ends the conversation

**Status:** to build (new, 2026-11-25, the spec owner's decision R29). Run it before Task 8.

**Files:**
- Modify: `src/core/voice_conv.[ch]`
- Test: `tests/test_voice_conv.c`

**Interfaces:**
- Produces: `VCN_END_STUCK`, appended to `VoiceConvNote`, with text
  `Conversation ended: the last turn could not be transcribed`.
- In `VCE_TICK`'s ENDING branch, the `VOICE_CONV_ENDING_MAX_MS` expiry becomes
  `go_idle(c, VCN_END_STUCK, 0)`. It releases the link and the upload, and the turn's words
  stay in the box. The live-fallback branch before it is unchanged.

- [ ] **Step 1: Write the failing tests**:
  - `test_vconv_ending_watchdog_ends_conversation`: a batch turn with no result for 180 s gives
    IDLE, note `VCN_END_STUCK`, `VCA_CANCEL_BATCH`, no `VCA_DROP_TURN`, and every resource
    released (applying the actions to a test ledger leaves zero).
  - `test_vconv_ending_watchdog_live`: the same for a live turn whose final and fallback never
    come.
  - The built `test_vconv_ending_watchdog_keeps_silence_clock` (Task 4) is replaced by these
    two, which hold the stronger rule; say so in the commit.
- [ ] **Step 2: Run.** FAIL. **Step 3: Implement.** **Step 4: Run** `make -j test`. PASS.
- [ ] **Step 5: Commit** `fix(voice): a turn stuck for 3 minutes ends the conversation and says why`.

Suggested implementer: Sonnet. Reviewer: Opus or Fable.

---

### Task 11 (orchestrator): spec findings, notes, version, build, full verification

**Status:** to do last. Revised 2026-11-25.

- [ ] **Live spec §16:** add "Found at build time (PR 3, 2026-11-2x)" beside the 2026-11-25
  decisions subsection. It holds:
  - item 14's counts measured on the branch (keys 69, `CFG_KEYS_MAX` 96, `NOTICE_ITEMS_MAX`
    24, `winhttp.def` 17);
  - the IID source for `IAudioRenderClient`;
  - every ruling R2 to R31 as built, and anything an implementer had to decide differently;
  - the formats and rates each provider actually returned, if any hand check has run;
  - the test counts at the end.
- [ ] **Notes Open list:** a new item, "AI speech (PR 3 of live voice, feat/voice-ai-speech):
  nothing run on Windows", with one line per hand check:
  - (a) Mistral with `gb_jane_neutral`, Mary, Sky and Harriet: accent, time to the first word,
    no gap or click between chunks, and whether its streamed WAV has a header and float32 or
    16-bit samples. Also the cut-in duck and stop latency.
  - (b) Groq Orpheus: the 200-byte chunks back to back, the rate it returns, whether it
    streams, and how its voices sound.
  - (c) A Custom server (Kokoro-FastAPI with `bf_emma`, and one with another rate, such as
    piper at 22,050 Hz), with and without `instructions`.
  - (d) Shared and separate keys: Mistral with Mistral, Groq with Groq, and a separate key;
    the key row; the DPAPI blob; the blob kept when the config moves.
  - (e) The fallback: a wrong key (401), Wi-Fi pulled mid-reply, a Custom server returning
    mp3. The header shows, and Windows speech reads on from about the cut.
  - (f) "Carry on" after a cut-in resumes near the cut (R9).
  - (g) Headphones unplugged mid-reply, and a default-device change.
  - (h) Echo cancellation with our render client, and whether our render session is ducked.
  - (i) The cost on Mistral's and Groq's usage pages for one conversation.
  - (j) Play sample with unsaved values; disabled in Dictation.
  - (k) The Voice page at 100 %, 125 % and 200 %, with the reseeding on a provider change.
  - (l) `mingw32-make test` and `mingw32-make clean && mingw32-make release` on the Windows
    box.
  - (m) Retry during a conversation: the mic closes and the reply is spoken.
  - (n) A turn stuck for 3 minutes (an upload server that never answers) ends the conversation
    with its note.

  Mark items (q), (r), (s) and (o)'s folded parts as fixed in code, with their hand check
  pending.
- [ ] **Notes todo:** add an entry:
  - OpenAI's batch speech-to-text models `gpt-4o-mini-transcribe` (Nutshell's default,
    `src/core/voice_stt.c:14`), `gpt-4o-transcribe` and `whisper-1` (and `-diarize`) shut down
    on 2027-02-26.
  - The replacement is `gpt-transcribe` or `gpt-live-transcribe`.
  - `gpt-4o-mini-transcribe-2025-03-20` shuts down on 2027-01-20.
  - Source: `developers.openai.com/api/docs/deprecations.md`, checked 2026-11-25.
  - Change the default and seeds before then, in its own pull request.
- [ ] **Version:** `"1.3.0"` and `1,3,0,0` in `src/ui/resource.h`, and README's
  `**Version**: v1.3.0`.
- [ ] **Build and verify**, all from the worktree, in this order: `make clean && make release`;
  `make -j test`; `make test SAN=1`; `make lint-gate`; `make fuzz-run`;
  `xvfb-run -a make wintest`; `make selftest-wine` (all six modes);
  `.github/scripts/check-version.sh` against `main`.
  - Before `wintest`, check `pgrep -a wine` for other sessions' runs.
  - If the release fails after `clean`, run `git restore build/win/nutshell.exe` before
    committing anything.
- [ ] **Commit** the exe and docs. Stage new files by path and read every `??` line. The
  `Models:` line lists every model and what it did. Do not push.
- [ ] **Whole-branch reviews** before any push: an independent Opus review and a Fable review of
  the diff against `58dbd00`. Their findings go to the maintainer before any fix.

Suggested owner: Opus (orchestrator).
