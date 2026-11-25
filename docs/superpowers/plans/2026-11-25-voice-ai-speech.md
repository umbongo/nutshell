# AI speech (PR 3 of live voice) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let a conversation's replies be read by an AI voice. The voice comes from OpenAI's or
an OpenAI-compatible `/audio/speech`, streamed as 24 kHz PCM into Nutshell's own WASAPI render
client. It has its own speech setting and key, cut-in ducking and repeat/continue positions, and
falls back to Windows speech for the rest of a reply whenever AI speech fails.

**Architecture:** The pure parts go in `src/core` with native tests:
- `voice_aispeech.c`: the provider table, target resolution and the shared-key rule, the request
  body, PCM byte alignment, and the byte-to-text-offset map.
- `voice_playq.c`: the play queue. It handles stale-reply drops, gain, end markers and play
  positions.
- `voice_conv.c` and `voice_echo.c`: the conversation-machine follow-ups.

The Win32 parts go in `src/ui`:
- A render client on the existing audio thread (`voice_audio_win.c`).
- A streaming POST on a caller's session (`ai_http_post_stream_ex`).
- The AI speech engine (`voice_aispeech_win.c`). It runs one worker per spoken text, its requests
  go through `voice_net_open(VNK_TTS)`, and it posts the same `WM_VOICE_TTS` messages SAPI does,
  so the conversation glue treats both engines alike.
- A speech router in the real ops table (`voice_win.c`).
- The fallback itself, in `voice_conv_win.c`.

**Tech Stack:** C11, Win32 (MinGW cross-compile), WASAPI render (`IAudioRenderClient`), WinHTTP
(delay-loaded, no new entry points), custom `test_framework.h`, Wine `wintest`,
`make selftest-wine`.

**Spec:** `docs/superpowers/specs/2026-11-24-voice-live-conversation-design.md` (the "live spec").
PR 3's scope is §15 item 3: the PR 3 half of §7.4, §8.4, the AI half of §8.5, §11.3, and the PR 3
rows of §12, §13 and §14. PR 1's and PR 2's build notes are in §16 "Found at build time". The
previous plan, for format, is `docs/superpowers/plans/2026-11-24-voice-conversation.md`.

## Before execution: the maintainer decides (OpenAI retires every `/audio/speech` model)

The §16 item 13 recheck (2026-11-25, sources under "Rechecks") found the following:
- `tts-1`, `tts-1-hd` and `gpt-4o-mini-tts` (all snapshots) are deprecated. They shut down on
  **6 January 2027**, six weeks after this plan.
- The named successor is `gpt-realtime-2.1-mini`, and only through the **Realtime API**.
  OpenAI's guide says it "isn't a supported model for `/v1/audio/speech`".
- So the spec's request shape (§8.4: `POST {base}/audio/speech`) has no OpenAI model after that
  date. The endpoint itself is not listed as deprecated.

`Ruling: build PR 3 as the spec says (/audio/speech for OpenAI and Custom) — the spec fixes the
request shape, Custom (OpenAI-compatible) servers keep it valid indefinitely, and §8.5's fallback
makes the retirement safe (every reply then falls back to Windows speech with the "AI speech
failed" header) — cost if wrong: OpenAI AI speech works for six weeks; a follow-up PR ("Realtime
speech": gpt-realtime-2.1-mini over PR 1's WebSocket transport, an out-of-band response.create
with output audio and a read-verbatim instruction) is needed to keep OpenAI.`

The provider table carries a `shape` field (as `SttProvider` does), so a Realtime shape can be
added later without touching the settings or the engine's callers. The orchestrator shows this
section to the maintainer **before Task 1 is dispatched**. The maintainer may instead redirect
the OpenAI half to the Realtime API. That would replace Tasks 2's body builder and 7's request
loop, and would need a spec amendment first.

## Global Constraints

- **Where to work:** worktree `/home/thomas/nutshell-wt/voice-ai-speech`, branch
  `feat/voice-ai-speech`, based on `main` `58dbd00` (v1.2.99). Never touch `~/nutshell` or any
  other worktree. Do not push.
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
  - The baseline is 4525 native tests on `automaton` (libssh2 stub) and 173 `wintest`.
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
  - `VRES_RENDER` is acquired when `voice_audio_render_open` is pushed, and released when
    `voice_audio_render_close` is pushed or a pre-emption is handled.
  - `VRES_SPEECH_REQ` counts one per speech worker: acquired at its creation, released when it
    is joined (or detached after a bounded wait).
- **Content.** Spoken text, request bodies, PCM and keys are `secure_zero`ed before free. Text and
  bodies are logged only through `DLOG_CONTENT`. A request is logged by kind and HTTP status
  only. The `Authorization` header buffer is wiped after the call.
- **New window message:** `WM_VOICE_AIS (WM_APP + 0x49)`. 0x44 to 0x48 are taken; 0x48 is
  `WM_VOICE_ACTIVATION`.
- **New audio messages:** `VAM_RENDER_OPENED`, `VAM_PLAYED`, `VAM_PLAY_DONE` and
  `VAM_RENDER_ERROR`. **New error:** `VAE_RENDER_PREEMPTED`.
- **New control ids:** `IDC_VOICE_SPEECH 3121`, `IDC_VOICE_TTS_PROVIDER 3122`,
  `IDC_VOICE_TTS_URL 3123`, `IDC_VOICE_TTS_KEY 3124`, `IDC_VOICE_AI_VOICE 3125`,
  `IDC_VOICE_AI_MODEL 3126` and `IDC_VOICE_AI_STYLE 3127`. 3120 is the highest `IDC_` today;
  recheck before use.
- **Copy fixed by the spec** (UTF-8; the dash is U+2014):
  - header: `AI speech failed: using Windows speech`
  - the notice dialog (once per launch, Windows speech only): `Windows has no British English
    woman's voice installed, so replies use <voice>. To get one, add English (United Kingdom)
    under Windows Settings > Time & language > Speech, or choose AI speech in Settings > Voice.`
  - the default style: `Speak as an English woman with a standard British accent (received
    pronunciation), clear and natural.`
  - Play sample: `Hello. This is how replies will sound.`
  - notice lines (label — value):
    - `Voice: speech` — `Windows speech`
    - `Voice: speech provider` — `OpenAI`
    - `Voice: speech URL` — `none`
    - `Voice: speech API key` — `none; the speech-to-text key is used when the provider is the
      same`
    - `Voice: AI voice` — `coral`
    - `Voice: AI speech model` — `the provider's default`
    - `Voice: accent and style` — `an English woman with a standard British accent`
- **Numbers fixed here or by the spec:**
  - PCM is 24 000 Hz, mono, 16-bit signed little-endian, with no header.
  - The duck is `VOICE_TTS_DUCK_PERCENT` (18, about -15 dB).
  - The first chunk is about 200 bytes and later chunks up to 1000 (`voice_speak_chunks`). The
    API input limit is 4096 characters.
  - The render buffer is 100 ms. `VAM_PLAYED` is posted every 100 ms while playing.
  - The next chunk's request starts only when less than 20 000 ms of audio is queued.
  - Bounded joins wait 3000 ms.
  - The default model is `gpt-4o-mini-tts` and the default voice is `coral`.
- **Settings keys:** 62 today (counted with
  `awk 'NR>=484&&NR<=560' src/config/loader.c | grep -c '^ *CK('`), 69 after Task 1.
  `CFG_KEYS_MAX` is 96 and `NOTICE_ITEMS_MAX` is 24. `winhttp.def` has 17 entries and this PR
  adds none (`WinHttpSetOption` is already there).

## Review Focus

1. **A PCM stream split on odd byte boundaries.** The network hands the callback 1, 3 or 4095
   bytes at a time, and a sample split across two callbacks must not shift every later sample
   (which would be loud noise). Pinned by `test_vais_pcm_carry_odd_splits` (Task 2) and the
   loopback server's odd-sized writes in `test_voice_ais_speak_done` (Task 7).
2. **Old audio after a stop or a cut-in.** A worker that pushes one more block after the UI
   flushed must not have that block played. Pinned by `test_vplayq_flush_drops_late_data`
   (Task 3) and `test_voice_ais_stop_then_speak_no_old_audio` (Task 7).
3. **A network slower than real time.** An underrun mid-reply must not count as the reply's end,
   and the reply must carry on when data arrives. Pinned by `test_vplayq_underrun_not_done`
   (Task 3).
4. **AI speech failing on every reply** (a wrong key, or a model retired on 2027-01-06). Each
   reply falls back to Windows speech from where it stopped, with no hang, no chat note and the
   ledger at zero afterwards. Pinned by `test_vconv_win_ai_fail_every_reply` (Task 8) and
   `test_voice_ais_http_401` (Task 7).
5. **Voice switched off, or Settings saved, while the speech server has accepted the request and
   never answers.** Teardown stays within its bounded join and the ledger returns to zero.
   Pinned by `test_voice_ais_quit_while_server_hangs` (Task 7). A reply full of quotes,
   newlines, backslashes and non-ASCII text must still make a valid JSON body: pinned by
   `test_vais_body_escapes` (Task 2).

## Rechecks (live spec §16 items 13, 20 and 25, §8.4), 2026-11-25

Sources:
- `developers.openai.com/api/docs/guides/text-to-speech.md`
- `developers.openai.com/api/reference/resources/audio/subresources/speech/methods/create.md`
- `developers.openai.com/api/docs/deprecations.md`
- `developers.openai.com/api/docs/pricing.md`
- `developers.openai.com/api/docs/guides/realtime-conversations.md`

**Model.** The current model is `gpt-4o-mini-tts`; its newest snapshot is
`gpt-4o-mini-tts-2025-12-15`. The reference's enum is `tts-1`, `tts-1-hd`, `gpt-4o-mini-tts` and
`gpt-4o-mini-tts-2025-12-15`.

**Retirement.** All of them were noticed on 2026-10-01 and shut down on 2027-01-06. The
replacement is `gpt-realtime-2.1-mini`, through the Realtime API only (see the section above).

**Voices** (the reference lists 13):
- `alloy`, `ash`, `ballad`, `coral`, `echo`, `fable`, `onyx`, `nova`, `sage`, `shimmer`, `verse`,
  `marin` and `cedar`.
- The guide's prose says "11" in one place and "13" in another.
- The guide recommends `marin` or `cedar` for the best quality.
- `tts-1` and `tts-1-hd` take only nine of them.

**Price** of `gpt-4o-mini-tts`:
- $0.60 per 1M text input tokens and $12.00 per 1M audio output tokens.
- The pricing page no longer gives a per-minute estimate. The spec's "about $0.015 a minute"
  equals 1250 audio tokens a minute at $12/1M; that rate is not verified.
- `tts-1` costs $15 per 1M characters and `tts-1-hd` $30 per 1M characters.
- `gpt-realtime-2.1-mini` costs $20 per 1M audio output tokens.

**Format.**
- `response_format: "pcm"` is "raw samples in 24kHz (16-bit signed, low-endian), without the
  header". Mono is not stated in the text; the guide's Java example plays it as one channel.
- It is streamed with chunked transfer encoding. `stream_format` defaults to `audio`; `sse` is
  the alternative.
- `instructions` "does not work with `tts-1` or `tts-1-hd`".
- `input` is at most 4096 characters.
- The guide asks apps to disclose that the voice is AI-generated. The setting is named "AI
  speech" and its help says so (Task 1).

**What changes against the spec:**
- §8.4's "or its current successor" has no `/audio/speech` successor.
- The voice list gains `marin`, `cedar`, `ballad` and `verse` over the research of 2026-11-24.
- The price is stated per token, not per minute.
- §16 item 25 is decided below (Ruling R6).

## Rulings

Each ruling below is `decision — why — cost if wrong`. The tasks refer to them by number.

- **R1** — see the section above (build `/audio/speech`; the maintainer decides first).
- **R2** — Default voice `coral`, as the spec says, and the combo is seeded with all 13 voices. —
  The spec author chose a female-sounding voice that takes accent instructions; `marin` is
  OpenAI's quality pick. — Cost if wrong: the maintainer changes one combo.
- **R3** — Model seeds: `gpt-4o-mini-tts` (the default, stored as `""`),
  `gpt-4o-mini-tts-2025-12-15`, `tts-1-hd` and `tts-1`, the same list for Custom.
  `instructions` is sent only when the style is not empty and the model does not start with
  `tts-1`. — The reference says those two models do not take it. — Cost if wrong: a custom
  model that rejects the field. Clearing the style then removes it.
- **R4** — The body carries exactly `model`, `voice`, `input`, `instructions` (when sent) and
  `"response_format":"pcm"`. No `stream_format` (its default is raw audio) and no `speed`. —
  Fewer fields for an OpenAI-compatible server to reject. — Cost if wrong: none known.
- **R5** — A speech request never follows a redirect (WinHTTP's redirect policy is set to
  never). — The bearer key must not be re-sent to another host, as with PR 1's WebSocket
  upgrade. — Cost if wrong: a custom server behind a redirect fails and falls back.
- **R6** (§16 item 25) — The render client opens at the first speech of a conversation (or a
  sample). It is stopped and reset when a text has played out, and started again by the next.
  It is released at the conversation's end. — A started stream with nothing to play keeps an
  active session (in the mixer, against sleep, and in Windows' ducking logic). — Cost if wrong:
  a few ms of start latency per reply.
- **R7** — One render client with one owner token. A newer `render_open` pre-empts the old
  owner, which gets `VAM_RENDER_ERROR(VAE_RENDER_PREEMPTED)`. — Play sample during a (paused)
  conversation is the only overlap, and the conversation's speech is already stopped then. —
  Cost if wrong: a sample cuts a held reply, which is spoken again on resume.
- **R8** — One worker thread per spoken text (a reply, a phrase or a sample), with its chunks'
  requests in sequence. Chunk k+1's request starts only when less than 20 s of audio is queued,
  checked in 50 ms slices against the cancel token. A response is never blocked mid-body. —
  This bounds memory to about 20 s plus one chunk (about 4.4 MB) without risking a server
  timeout. — Cost if wrong: a gap between chunks on a very slow link.
- **R9** — The cut-off is in proportion to UTF-8 bytes within the chunk, not characters, because
  every offset in the glue is a byte offset (this is where the code wins over the wording of
  spec §10). A chunk still streaming uses an estimated length of
  `VOICE_AIS_EST_FRAMES_PER_BYTE` (1600 frames, about 15 bytes a second). The result is snapped
  back to the start of its word, on a UTF-8 boundary. — Proportional mapping is approximate
  anyway, so "carry on" should start on a word. — Cost if wrong: a few words repeated or
  skipped.
- **R10** — A failed chunk request does not cut the audio short. What was already queued plays
  out, then the engine posts `VTT_ERROR` with `offset` set to the resume point (R9's map at the
  end of what played). The glue shows the header, quits the AI context, and speaks the same text
  from that offset with Windows speech. The next speech (reply, phrase, repeat or continue) uses
  AI speech again. A fallback never raises the British-voice notice. If AI speech cannot start
  at all (gate, resolve error), it is the same fallback with offset `from`. — No overlap of two
  engines and no lost words. — Cost if wrong: up to 20 s of queued audio plays before the
  handover.
- **R11** — Spoken phrases (approval, "couldn't send", "went wrong") use the configured engine,
  so one request each with AI speech. — One voice throughout. — Cost if wrong: about 1 s of
  latency on an announcement.
- **R12** — Repeat re-synthesises from the kept text; no PCM cache (§10's builder's call). — No
  tens of MB of reply audio held in memory; same path as SAPI. — Cost if wrong: a second
  request's price and latency.
- **R13** — The shared-key rule:
  - It compares the speech provider with the effective speech-to-text target from
    `voice_stt_resolve`. OpenAI matches `"openai"`. Custom matches `"custom"` when both base
    URLs are equal after `voice_base_url_normalise`.
  - The shared key is the STT target's own: `ai_api_key` when `key_from_chat`, else
    `stt_api_key`.
  - When the STT target does not resolve, the speech key is its own.
  - While shared, the key row is hidden and a saved `tts_api_key` is kept unchanged.

  — This follows spec §8.4 literally. — Cost if wrong: a second key prompt in an edge case.
- **R14** — `voice_ai_style` is `char[1024]`. A config without the key gets the default text. A
  key present with `""` means no instructions (the user cleared it). — Clearing the style must
  be possible for servers that reject `instructions`. — Cost if wrong: none.
- **R15** — An AI speech failure shows the spec's header only, plus a debug log line with the
  HTTP status (no content). No chat note. — §13 puts it in the header. — Cost if wrong: a wrong
  key is noticed later. The hand check (d) covers it.
- **R16** — Settings order inside Conversation:
  - Pause before sending, **Speech**, Windows voice (and its two dim lines), then the AI rows,
    Play sample, the echo line and the ducking line.
  - Windows voice stays enabled with AI speech, because the fallback uses it.
  - The English (UK) line is hidden while AI speech is selected.

  — Spec §11.3 orders the AI rows but not their place. — Cost if wrong: a layout change.
- **R17** — Play sample uses the page's unsaved values (engine, provider, URL, key, voice, model
  and style). As in PR 2, it still needs voice saved on (the gate). — So voices can be tried
  before saving. — Cost if wrong: none.
- **R18** — The self-report gains `voice-resources: N` (the sum of the ledger's counts) and a
  new `selftest-wine` mode, `ai`. There is no exe-level AI speech control; `wintest` proves
  that `voice_net_open` counts a speech request. — An idle launch with AI speech must hold no
  render client, which the DLL list cannot show. — Cost if wrong: none.
- **R19** — No harness case for AI speech. — The Windows hand checks cover it and no `pwsh`
  exists here. — Cost if wrong: none.
- **R20** — The ops table:
  - `tts_start` gains an engine argument, `int (*tts_start)(unsigned token, int engine)`.
  - A new optional `int (*speech_engine)(void)` returns the configured engine (NULL means
    Windows).
  - The real table routes speak, stop, volume and quit by token.

  — The glue must start a second engine mid-reply. — Cost if wrong: test fakes change again.
- **R21** — The duck is a software gain applied as samples are written to the device buffer, so
  it takes effect within that buffer's 100 ms. — §9.2: about 200 ms is enough. — Cost if
  wrong: a slightly later duck.
- **R22** — `VAM_PLAYED` and `VAM_PLAY_DONE` carry a malloc'd `VoiceRenderMsg`, freed by the
  receiver (as `VAM_CAPTURED` is). — A reply id, a chunk and a frame count do not pack
  portably into one `LPARAM`. — Cost if wrong: 10 small allocations a second while speaking.
- **R23** — Notes item (r), the "external send" (Retry): `VCE_EXTERNAL_SEND`, fed from the Retry
  handler's two exits. Task 8 greps for any other launch that bypasses `voice_send_pressed` and
  adds the call there too. — Spec §5.2 closes the capture while Working. — Cost if wrong: a
  missed send path leaves the mic open while the chat works (as today).
- **R24** — The play position reported is the written position less the device padding,
  clamped within the current chunk. — WASAPI reports padding, not a per-sample clock. — Cost if
  wrong: up to 100 ms of error at a chunk boundary.

**Spec against the merged code, and which wins:**
- §3's counts (57 keys) are out of date. The code wins: 62 now, 69 after Task 1.
- §6.5's "later speech playback" seam (`VNK_TTS`, "declared, unused") becomes used. The spec
  wins.
- §8.3 and §13's notice clause, which PR 2 left out, is added. The spec wins.
- §10 "characters" against the glue's UTF-8 byte offsets: the code wins (R9).
- §16 item 7's fallback ("PR 3's own render client for Windows speech too") is not built. SAPI's
  purge and volume behaviour is still a pending Windows hand check, not a measured failure.

## Notes items (a) to (s) (`docs/superpowers/specs/2026-09-06-ui-redesign-notes.md`, "Conversation mode (PR 2 of live voice)")

**Folded in** (small, in code this PR changes):
- (r) Retry leaves the mic open while the chat works: `VCE_EXTERNAL_SEND` (Tasks 4 and 8, R23).
- (s) The "I couldn't send that" phrase restarts the silence clock: fixed (Task 4).
- (s) A paused-and-resumed local repeat restarts the silence clock: fixed (Task 4).
- (q) A Hearing or Ending watchdog expiry restarts the silence clock: fixed (Task 4).
- (q) A spoken reply longer than 10 minutes is cut by the Speaking watchdog: speech progress
  refreshes it (`VCE_SPEECH_PROGRESS`, Tasks 4 and 8).
- (s) The tick wrap at `voice_echo.c:204` and `:208` (Task 4).
- (r) `va_push_raw` drops a close when `realloc` fails: the conversation's end, render flush and
  render close become pending fields under the queue lock (Task 6).
- (r) The glue's event queue drops events past 24 silently, `VCE_END` included: a pending end
  slot that is never dropped (Task 8).
- (o) Play sample has no help entry: `SH_VOICE_PLAY_SAMPLE` (Task 1).
- (o) A notice raised for a launch is lost if the conversation ends before the handler runs: the
  launch flag is set only when the dialog is shown (Task 8).

**Left out:**
- Enter while a reply is spoken (the maintainer decides).
- The "Windows was locked" wording for an RDP or console disconnect (a wording decision; that
  code is not touched).
- A one-line triple-backtick span opening a fence (`voice_speak.c` is not changed; it fails
  closed).
- The header after a pause cuts an approval announcement (a recorded builder's call, a
  behaviour change).
- Settings destroyed before its owner is re-enabled (unverified, a hand check).
- Everything in (a) to (n) and (p): hand checks, or "no code".
- The rest of (r): the ducking-opt-out timing, the speech thread's idle CPU and message-list
  trimming are hand checks; the failed post of a turn cannot be tested through the seam.

---

### Task 1: The PR 3 settings, key, help, notice lines and digest

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
  char voice_tts_provider[32];         /* "openai" (default) or "custom" */
  char voice_tts_url[CFG_STR_MAX];     /* base URL, Custom only */
  char tts_api_key[CFG_STR_MAX];       /* DPAPI like stt_api_key */
  char tts_api_key_enc_preserved[CFG_BLOB_MAX];
  char voice_ai_voice[64];             /* default "coral" */
  char voice_ai_tts_model[CFG_STR_MAX];/* "" = the provider's default */
  char voice_ai_style[1024];           /* default VOICE_AIS_DEFAULT_STYLE; "" = no instructions (R14) */
  ```
- In `voice_config.h`:
  - `#define VOICE_SPEECH_WINDOWS 0`, `#define VOICE_SPEECH_AI 1`;
  - `const char *voice_speech_json(int e)` (`"windows"`/`"ai"`);
  - `int voice_speech_from_json(const char *s)` (unknown or NULL gives Windows);
  - `#define VOICE_AIS_DEFAULT_VOICE "coral"`, `#define VOICE_AIS_DEFAULT_MODEL
    "gpt-4o-mini-tts"`, and `#define VOICE_AIS_DEFAULT_STYLE` with the Global Constraints text.
    These live here so `voice_settings_default` and Task 2 share them.
- `SettingsHelpId` gains, before `SH_COUNT`: `SH_VOICE_SPEECH`, `SH_VOICE_TTS_PROVIDER`,
  `SH_VOICE_TTS_URL`, `SH_VOICE_TTS_KEY`, `SH_VOICE_AI_VOICE`, `SH_VOICE_AI_MODEL`,
  `SH_VOICE_AI_STYLE` and `SH_VOICE_PLAY_SAMPLE`.

- [ ] **Step 1: Write the failing tests**
  - `test_voice_ais_settings_defaults`: a config without the seven keys loads with Windows,
    `"openai"`, an empty URL, an empty key, `"coral"`, an empty model and the default style.
  - `test_voice_ais_settings_roundtrip`: save then load keeps AI, `"custom"`,
    `"http://10.0.0.5:8880/v1"`, `"marin"`, `"tts-1-hd"`, a style with a quote and a newline,
    and a key. The file holds `"voice_speech_engine": "ai"`. The key is saved as a `$dpapi$v1$`
    blob through the test's fake DPAPI and never in clear text.
  - `test_voice_ais_key_preserved`: an undecryptable `tts_api_key` blob loads as an empty key,
    is kept in `tts_api_key_enc_preserved` and is written back verbatim. Typing a new key drops
    it (`config_secret_drop_stale_preserved`).
  - `test_voice_ais_style_empty_kept`: `"voice_ai_style": ""` loads as `""`, not the default.
  - `test_voice_ais_settings_validate`: `voice_tts_provider` `"groq"` validates to `"openai"`;
    `"voice_speech_engine": "loud"` loads as Windows.
  - `test_voice_ais_notice_lines`: with `voice_enabled` present and the seven keys missing,
    exactly the seven Global Constraints lines appear, in key order. With `voice_enabled`
    missing they fold into the one "Voice" line, `tts_api_key` included.
  - `test_voice_ais_digest`: changing any one of the seven fields, the key included, changes
    `voice_settings_digest`.
  - The existing `k_setting_keys`-against-a-saved-file test passes with 69 rows.
  - `test_notice_msg_voice_british_names_the_voice` is updated to the full text with
    `, or choose AI speech in Settings > Voice.`
  - `test_settings_help_play_sample`: `settings_help_get(SH_VOICE_PLAY_SAMPLE)` has the label
    `Play sample` and kind `SH_KIND_ACTION`.
- [ ] **Step 2: Run** `make -j test 2>&1 | tail -5`: the new tests FAIL (or do not link).
- [ ] **Step 3: Implement.**
  - The seven `CK(...)` rows carry the labels of the Global Constraints, after
    `voice_windows_voice_name`. `tts_api_key` loads through `load_secret`, is saved through
    `secret_prepare` exactly as `stt_api_key` is, and is wiped in the config free path.
  - Help entries (`SETTINGS_PAGE_VOICE`). Each `default_text` is the notice value above.
    - `SH_VOICE_SPEECH`, label `Speech:`. Windows speech uses a voice installed on this PC. AI
      speech sends each reply's text to the speech provider and plays the AI-generated voice it
      returns. If AI speech fails, the rest of the reply is read with Windows speech.
      Conversation mode only.
    - `SH_VOICE_TTS_PROVIDER`, label `Speech provider:`. OpenAI, or Custom (an OpenAI-compatible
      `/audio/speech`). It must say: "OpenAI retires its speech models on 6 January 2027;
      after that, replies fall back to Windows speech."
    - `SH_VOICE_TTS_URL`, label `Base URL:`. As the speech-to-text Base URL help: a pasted
      `/audio/speech` is trimmed.
    - `SH_VOICE_TTS_KEY`, label `Speech API key:`. It is shown only when the speech provider is
      not the speech-to-text one; it is kept encrypted for this Windows user.
    - `SH_VOICE_AI_VOICE`, label `AI voice:`. A preset name, or a name your server knows.
    - `SH_VOICE_AI_MODEL`, label `AI speech model:`.
    - `SH_VOICE_AI_STYLE`, label `Accent and style:`. It is sent as instructions, and some
      servers and the `tts-1` models ignore it.
    - `SH_VOICE_PLAY_SAMPLE`, `SH_KIND_ACTION`, label `Play sample`, with `default_text` NULL.
      It speaks a sentence with the chosen speech and voice, using the values on the page
      before Save.
  - Add `"tts_api_key"` to `is_voice_subkey` and the clause to `NOTICE_MSG_VOICE_BRITISH`'s
    closing text. Add the seven fields to `voice_settings_digest`.
- [ ] **Step 4: Run** `make -j test 2>&1 | tail -3`. All pass. Record the key count (69) for
  Task 11.
- [ ] **Step 5: Commit** `feat(voice): AI speech settings, key, help and notice lines`.

Suggested implementer: Sonnet.

---

### Task 2: AI speech in core: targets, the shared key, the body, PCM alignment, the offset map

**Files:**
- Create: `src/core/voice_aispeech.h`, `src/core/voice_aispeech.c`
- Test: `tests/test_voice_aispeech.c`

**Interfaces:**
- Consumes: Task 1's fields and defaults; `voice_stt_resolve`, `voice_base_url_normalise` and
  `json_escape_string`.
- Produces:
```c
typedef enum { VAIS_SHAPE_SPEECH = 0 } VoiceAisShape;   /* /audio/speech; R1's seam for a Realtime shape */
typedef enum { VAIS_KEY_OWN = 0, VAIS_KEY_STT, VAIS_KEY_CHAT } VoiceAisKey;
typedef struct {
    char url[512];                 /* {base}/audio/speech */
    char model[CFG_STR_MAX];       /* never empty: the default when the setting is "" */
    char voice[64];                /* never empty: "coral" when the setting is "" */
    char style[1024];
    char provider[16];             /* "openai" | "custom" */
    VoiceAisShape shape;
    VoiceAisKey key_from;
    int send_instructions;         /* R3 */
} VoiceAisTarget;
#define VOICE_AIS_RATE 24000
#define VOICE_AIS_OPENAI_BASE "https://api.openai.com/v1"
#define VOICE_AIS_EST_FRAMES_PER_BYTE 1600
const char *const *voice_ais_voices(size_t *n);   /* the 13 of the Rechecks, in the reference's order */
const char *const *voice_ais_models(size_t *n);   /* R3's four; [0] the default */
int  voice_ais_resolve(const Settings *s, VoiceAisTarget *t, char *err, size_t cap);
int  voice_ais_key_shared(const Settings *s);                       /* R13 */
const char *voice_ais_key(const Settings *s, const VoiceAisTarget *t);   /* never NULL; may be "" */
char *voice_ais_body(const VoiceAisTarget *t, const char *text, size_t len, size_t *out_len);
typedef struct { int have; unsigned char lo; } VoicePcmCarry;
size_t voice_pcm_carry(VoicePcmCarry *c, const unsigned char *in, size_t n,
                       int16_t *out, size_t cap);     /* LE pairs, odd byte carried; returns samples */
size_t voice_ais_offset(const char *text, size_t chunk_from, size_t chunk_len,
                        uint64_t played, uint64_t total, int complete);   /* R9 */
```
- `voice_ais_resolve` errors (user-facing, in `err`): `Enter the speech Base URL` for an empty
  Custom URL; `The speech Base URL is not valid` when normalising fails; and `Enter a speech
  API key` when the key `voice_ais_key` would return is empty.

- [ ] **Step 1: Write the failing tests**
  - Resolution:
    - `test_vais_resolve_openai_defaults`: defaults give
      `https://api.openai.com/v1/audio/speech`, `gpt-4o-mini-tts`, `coral`, the default style
      and `send_instructions` 1.
    - `test_vais_resolve_custom`: `http://10.0.0.5:8880/v1/audio/speech/` normalises to
      `http://10.0.0.5:8880/v1/audio/speech`. Empty and `ftp://x` give the two errors.
    - `test_vais_no_instructions_tts1`: `tts-1` and `tts-1-hd` give 0, an empty style gives 0,
      and `gpt-4o-mini-tts` with a style gives 1.
  - The shared key, all through `voice_ais_key_shared` and `key_from`:
    - `test_vais_key_shared_openai`: STT OpenAI with its own key shares (`VAIS_KEY_STT`).
      "Use current AI assistant" with an OpenAI chat shares (`VAIS_KEY_CHAT`).
    - `test_vais_key_not_shared`: STT Groq with TTS OpenAI is `VAIS_KEY_OWN`. STT Custom
      `http://a/v1` with TTS Custom `http://b/v1` is not shared; `http://a/v1/` with
      `http://a/v1/audio/speech` is shared.
    - `test_vais_key_stt_unresolved`: an STT target that does not resolve gives
      `VAIS_KEY_OWN`.
  - The body:
    - `test_vais_body_fields`: the body parses with `json_parse`. It has exactly the five
      fields of R4, `response_format` is `"pcm"`, and `instructions` is absent when
      `send_instructions` is 0.
    - `test_vais_body_escapes`: the text `He said "C:\temp"\nnext\ttab é ✓` round-trips through
      `json_parse` unchanged.
    - `test_vais_body_rejects_crlf`: a CR or LF in the model or voice gives NULL.
  - PCM alignment:
    - `test_vais_pcm_carry_odd_splits`: the bytes of samples `{1, -2, 32767, -32768, 256}`, fed
      in splits of 1, 3, 1, 5 and 0, give exactly those five samples. A trailing odd byte stays
      in the carry.
  - The offset map:
    - `test_vais_offset_complete`: for the chunk `"One two three four."` at `chunk_from` 10,
      `played` 0 gives 10, half gives the start of `three`, and `played == total` gives the
      chunk's end.
    - `test_vais_offset_estimate`: with `complete` 0, `played` = 1600 × 5 gives the word
      start at or before byte 5. It never passes the chunk end, whatever `played` is.
    - `test_vais_offset_utf8`: a chunk of two-byte characters never yields an offset inside a
      character.
- [ ] **Step 2: Run.** FAIL.
- [ ] **Step 3: Implement** in `src/core/voice_aispeech.c`: no Win32, no I/O, no logging.
- [ ] **Step 4: Run** `make -j test 2>&1 | tail -3`. PASS.
- [ ] **Step 5: Commit** `feat(voice): AI speech targets, request body and offset map`.

Suggested implementer: Sonnet.

---

### Task 3: The play queue

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
  - modes `HLS_PCM` (200, `Transfer-Encoding: chunked`, a body of a known byte ramp sent in
    writes of 1, 3, 4095 and the rest), `HLS_401` (a JSON error body), `HLS_HANG` (reads the
    request and never answers) and `HLS_302` (a redirect to itself, counting requests);
  - `hl_start(mode, size_t body_bytes)`, `hl_port`, `hl_stop` (joins every thread);
  - `hl_request_head`, `hl_request_body`, `hl_requests`.
- Refactor `ai_http_post_stream_impl` to take the session, as `ai_http_post_ct_ex`'s impl does.
  `ai_http_post_stream` keeps its own `ns_winhttp_open` session and its behaviour.

- [ ] **Step 1: Write the failing wintests**
  - `test_ai_stream_ex_pcm_bytes`: through `voice_net_open(VNK_TTS)`, an `HLS_PCM` body of
    9 001 bytes arrives whole and in order across callbacks, with status 200.
    `voice_net_open_count()` went up by exactly 1.
  - `test_ai_stream_ex_401`: status 401, rc -1, and the error holds the JSON body.
  - `test_ai_stream_ex_no_redirect`: `HLS_302` gives status 302, and the server saw exactly 1
    request.
  - `test_ai_stream_ex_cancel_hang`: `ai_nc_fire` from another thread makes an `HLS_HANG` call
    return within 2 s with "cancelled". If Wine cannot wake it, the test takes a defined SKIP,
    as `test_ai_nc_fire_wakes_blocked_winhttp_call` does.
  - `test_ai_stream_ex_null_session`: rc -1 and "No session".
- [ ] **Step 2: Run** `xvfb-run -a make wintest`. FAIL. **Step 3: Implement.**
  **Step 4: Run** `xvfb-run -a make wintest`, `make -j test` and `make -j release` (compiles).
  PASS.
- [ ] **Step 5: Commit** `feat(voice): streaming POST on a voice session, never redirected`.

Suggested implementer: Sonnet.

---

### Task 6: The render client on the audio thread

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

**Files:**
- Create: `src/ui/voice_aispeech_win.h`, `src/ui/voice_aispeech_win.c`,
  `tests/test_voice_aispeech_win.c`
- Modify: `Makefile` (CRLF: `WIN_ONLY_TESTS`, `WIN_TEST_SRCS` with `http_loopback_win.c`),
  `tests/win_runner.c`

**Interfaces:**
- Consumes:
  - Task 2: `VoiceAisTarget`, `voice_ais_body`, `voice_pcm_carry` and `voice_ais_offset`;
  - Task 5: `ai_http_post_stream_ex`;
  - Task 6: the render API;
  - `voice_speak_chunks`, `voice_net_open(VNK_TTS)` and `VoiceTtsMsg`/`WM_VOICE_TTS`.
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
```
- At most two contexts, the conversation's and the sample's, keyed by token.
- `speak`:
  - stops what plays: fire the old worker's cancel and `render_flush(token, reply_id)`;
  - opens the render client on the first speak (`VRES_RENDER`);
  - chunks `utf8[from..]` with `voice_speak_chunks`, keeping each chunk's `(start, len)`;
  - starts one worker (`VRES_SPEECH_REQ`).
- The worker (R8):
  - for each chunk: wait until `voice_audio_render_queued_ms() < 20000` (50 ms slices,
    cancel-aware); `voice_net_open(VNK_TTS)`; build the body; send
    `Authorization: Bearer <key>`; `ai_http_post_stream_ex` with `recv_ms` 60 000;
  - align the PCM (`voice_pcm_carry`) and push it with `render_data(token, reply, k, ...)`;
  - post `VAI_CHUNK_DONE(k, frames)`;
  - on any failure (status ≠ 200, transport, gate off): `render_end` and `VAI_FAILED(k,
    status)`, then stop;
  - at the end: `render_end` and `VAI_EXITED`;
  - wipe the key, body and text; close the session.
- On the UI side:
  - `VAM_PLAYED` gives `VTT_WORD(offset)` through `voice_ais_offset` on the current reply's
    chunk table (`total` from `VAI_CHUNK_DONE`).
  - `VAM_PLAY_DONE` gives `VTT_DONE` if every chunk completed, else `VTT_ERROR` with
    `offset` = the map at the end of what played (R10) and `hr` = the HTTP status (or -1).
  - `VAM_RENDER_ERROR` while playing gives `VTT_ERROR` at the current offset.
  - A resolve error passed in (an empty `t->url`) gives an immediate `VTT_ERROR` at `from`.
  - `VAI_EXITED` joins the worker (non-blocking, as `voice_live_reap`) and releases
    `VRES_SPEECH_REQ`.
  - Every message is checked against the current reply id; stale ones are dropped.
  - The sample context quits itself at its `VTT_DONE`/`VTT_ERROR`.
- `quit`: fire cancel, flush, bounded join (detach and release after `wait_ms`), then
  `render_close` and release `VRES_RENDER`.

- [ ] **Step 1: Write the failing wintests.** They use the null render sink, `http_loopback`,
  a message-only test window pumping `WM_VOICE_TTS`, `WM_VOICE_AIS` and `WM_VOICE_AUDIO` into
  the module, and the ledger reset before each test.
  - `test_voice_ais_gate_off`: gate off gives `start` 0, no thread and the ledger at 0.
  - `test_voice_ais_speak_done`: `HLS_PCM` with 0.25 s of audio and the text `One. Two.`:
    - the server saw `Authorization: Bearer test-key`, and the body has
      `"input":"One. Two."` and `"response_format":"pcm"`;
    - `voice_net_open_count()` went up by 1;
    - the `VTT_WORD` offsets do not decrease and stay within [0, 9];
    - exactly one `VTT_DONE` with the reply id arrives;
    - after `quit`, the ledger is all zero.
  - `test_voice_ais_two_chunks_two_requests`: a 600-byte text of short sentences makes two or
    more requests, in order (the server counts them), and one `VTT_DONE`.
  - `test_voice_ais_http_401`: `VTT_ERROR` with offset 0 and `hr` 401, and no `VTT_DONE`.
  - `test_voice_ais_stop_then_speak_no_old_audio`: speak reply 1 (a 3 s body), stop after its
    first `VAM_PLAYED`, then speak reply 2. `voice_audio_test_render_frames(1)` does not grow
    after the stop.
  - `test_voice_ais_quit_while_server_hangs`: `HLS_HANG`, then speak and `quit(token, 3000)`
    return within 4 s, with the ledger all zero.
  - `test_voice_ais_sample_self_quits`: `voice_ais_sample` then DONE leaves `voice_ais_running()`
    at 0 and the ledger at 0.
- [ ] **Step 2: Run.** FAIL. **Step 3: Implement.** **Step 4: Run**
  `xvfb-run -a make wintest`, `make -j test` and `make -j release`. PASS.
- [ ] **Step 5: Commit** `feat(voice): AI speech engine streaming PCM through the render client`.

Suggested implementer: Sonnet.

---

### Task 8: The conversation glue: engine choice, fallback, progress, external send, routing

**Files:**
- Modify:
  - `src/ui/voice_conv_win.[ch]` (the ops change of R20, the fallback, progress, external
    send, the notice fixes, the pending end);
  - `src/ui/voice_win.[ch]`: the real ops router; `rc_notice_due` returns 0 with AI speech;
    `voice_feature_apply`/`voice_feature_shutdown` call `voice_ais_quit_all(3000)` before
    `voice_audio_stop`; `voice_on_audio_msg` routes the render messages to
    `voice_ais_on_audio` before `settings_voice_on_audio`; `voice_on_tts` stays; new
    `voice_on_ais(LPARAM)` and `voice_on_external_send(void)`;
  - `src/ui/window.c` (route `WM_VOICE_AIS`);
  - `src/ui/ai_chat.c` (the Retry handler's two exits call `voice_on_external_send()`, R23).
- Test: `tests/test_voice_conv_win.c` (update the fakes for R20, then the new tests)

**Interfaces:**
- Consumes: Task 4 (`VCE_EXTERNAL_SEND`, `VCE_SPEECH_PROGRESS`, `VCH_AI_FAILED`), Task 7, Task 2
  (`voice_ais_resolve`, `voice_ais_key`) and Task 1 (`voice_speech_engine`).
- Produces:
  - `VoiceConvOps.tts_start(unsigned token, int engine)`;
  - optional `int (*speech_engine)(void)`;
  - `void voice_conv_external_send(void)`. It takes the reply baseline (`g_since =
    msg_count()`, `g_sent_tick`, `g_busy_seen = !chat_idle()`) in every non-idle state except
    ENDING-while-sending, then feeds `VCE_EXTERNAL_SEND`;
  - TEST-ONLY seams `voice_conv_test_queue(VoiceConvEvent, int, int)` (enqueue without pumping)
    and `voice_conv_test_pump(void)`.
- Glue rules:
  - **The engine.** `g_engine_cfg` is read at conversation start; a settings change ends the
    conversation anyway. Every `begin_speak` makes sure the context's engine is `g_engine_cfg`.
    A context of the other engine is quit and a new one started. An AI start that fails falls
    back as R10.
  - **The fallback**, on `VTT_ERROR` for the current id while the context is AI:
    - show `VCH_AI_FAILED`;
    - quit the AI context;
    - start a Windows context;
    - `begin_speak` a **copy** of `g_spoken` from `m->offset` with the same reply or phrase
      kind (`begin_speak` replaces `g_spoken`).
    - A Windows `VTT_ERROR` then follows today's path (`VCE_SPEECH_FAILED`, and the
      "Could not speak the reply" note for a reply).
  - **Progress.** Every `VTT_WORD` for the current id feeds `VCE_SPEECH_PROGRESS`, at most once
    every 1000 ms.
  - **The notice** (notes item (o)). `g_notice_raised` is set in `voice_conv_on_notice` when
    `notice_voice` is actually called, not when it is posted.
  - **The pending end** (notes item (r)). An `enqueue` of `VCE_END` with the queue full is kept
    in a separate slot, and the first such end wins. `pump` runs it after the queue. Other
    overflow is dropped with a `DLOG` warning.
  - **The real router** in `voice_win.c` keeps the engine per token.
    - `rc_tts_start(token, AI)` calls `voice_ais_start(g_main, token)`.
    - `rc_tts_speak` for an AI token resolves `voice_ais_resolve(s, &t, ...)`; a failure passes
      a target with an empty URL. It then calls `voice_ais_speak(token, id, text, from, &t,
      voice_ais_key(s, &t))`.
    - `rc_speech_engine` returns `s->voice_speech_engine` in Conversation mode.

- [ ] **Step 1: Write the failing wintests**, with the fake ops recording the engine per token
  and doing the ledger's acquire and release:
  - The engine and the fallback:
    - `test_vconv_win_ai_engine_started`: with the engine set to AI, the start calls
      `tts_start(token, VOICE_SPEECH_AI)`.
    - `test_vconv_win_ai_fallback_rest_of_reply`: while a reply is spoken with AI, a
      `VTT_ERROR(offset 40)` for its id gives:
      - the header `AI speech failed: using Windows speech`;
      - `tts_quit(ai)`, then `tts_start(win, VOICE_SPEECH_WINDOWS)`;
      - `tts_speak(win, ..., text, 40)`.

      Then `VTT_DONE` on the Windows context and the next reply give `tts_quit(win)` and
      `tts_start(ai)`, speaking from 0.
    - `test_vconv_win_ai_fail_every_reply`: three replies, each failing at once, each fall
      back. There is no chat note, and after the end the ledger is all zero.
    - `test_vconv_win_ai_and_windows_fail`: AI fails, then the Windows start fails. The note
      `Could not speak the reply: it is on screen` shows, then Listening.
    - `test_vconv_win_ai_stale_error_ignored`: a `VTT_ERROR` for an older id changes nothing.
  - The notice:
    - `test_vconv_win_no_notice_with_ai`: `notice_due` would say yes, but with AI no notice is
      raised and the reply is spoken.
    - `test_vconv_win_notice_kept_if_ended_first`: the notice is posted, then the conversation
      ends before `voice_conv_on_notice`. The next conversation's first reply raises it.
  - External send:
    - `test_vconv_win_external_send`: in Listening, `voice_conv_external_send()` gives
      `cap_end` and WORKING. A later chat idle with a reply after the new baseline speaks it.
  - Progress:
    - `test_vconv_win_progress_keeps_long_reply`: `VTT_WORD` every 30 s with
      `voice_conv_test_advance` for 11 minutes keeps Speaking.
  - The pending end:
    - `test_vconv_win_end_never_dropped`: 30 `voice_conv_test_queue(VCE_TICK, ...)` calls,
      then `VCE_END(VCX_OFF)`, then `voice_conv_test_pump()`. The conversation is IDLE and the
      ledger is zero.
- [ ] **Step 2: Run.** FAIL. **Step 3: Implement.** Grep for every caller of
  `ai_chat_launch_stream_thread` and every send path outside `ai_chat_send_user_message_ex`.
  List them in the commit body and add `voice_on_external_send()` where a user action launches
  a request (R23). **Step 4: Run** `make -j test`, `xvfb-run -a make wintest` and
  `make -j release`. PASS, and the exe links.
- [ ] **Step 5: Commit** `feat(voice): AI speech in conversations, with the Windows speech fallback`.

Suggested implementer: Sonnet. The reviewer should be Opus, because this is the integration
task.

---

### Task 9: Settings > Voice: Speech, the AI rows, Play sample with AI speech

**Files:**
- Modify: `src/ui/settings.c`, `src/ui/resource.h` (the ids of the Global Constraints),
  `src/core/voice_config.[ch]` (a pure enables helper)
- Test: `tests/test_voice_config.c`

**Interfaces:**
- Consumes: Task 1 (fields, help ids), Task 2 (`voice_ais_voices`, `voice_ais_models`,
  `voice_ais_key_shared`, `voice_ais_resolve`, `voice_ais_key`), Task 7 (`voice_ais_sample`),
  and PR 2's `voice_tts_sample`.
- Produces:
```c
typedef struct { int speech; int ai_rows; int url; int key; int uk_line; } VoiceSpeechEnables;
void voice_speech_enables(int conversation_enabled, int engine, int custom, int key_shared,
                          int uk_voice_installed, VoiceSpeechEnables *out);
```
  - `speech`: the Speech row is enabled in Conversation mode.
  - `ai_rows`: the provider, URL, key, voice, model and style rows are enabled with
    Conversation and AI.
  - `url`: shown when Custom.
  - `key`: shown when `!key_shared`.
  - `uk_line`: shown when Windows is chosen and no en-GB woman's voice is installed (R16).
- Rows (R16), each built with the page's helpers and laid out as the existing ones:
  - Speech: a combo of `Windows speech` and `AI speech`.
  - Speech provider: a combo of `OpenAI` and `Custom`.
  - Base URL: an edit with the warning-coloured clear-text line, reusing
    `voice_url_is_cleartext`'s rule for this edit.
  - Speech API key: a password edit, as `IDC_VOICE_KEY`.
  - AI voice: an editable combo of the 13 voices.
  - AI speech model: an editable combo. The shown default is stored as `""`, as the STT model
    is.
  - Accent and style: a multi-line edit, three lines tall, `ES_AUTOVSCROLL`, limited to 1023
    bytes.
  - `CBN_SELCHANGE` on Speech or Speech provider, and `EN_CHANGE` on the URL, call
    `voice_enable_sync`, which applies `voice_speech_enables`. The key row's visibility relays
    the page out, as `voice_rows_apply` does.
- Factor the Voice page's save code into `static void voice_page_read(SettingsDlgData *d,
  Settings *s)`, which reads every Voice control into `s`. Save uses it on the real settings.
  `voice_ais_key_shared` and Play sample use it on a copy of `d->cfg->settings` (R17). The
  copy's key fields are wiped after use.
- Save also does `config_secret_drop_stale_preserved` for `tts_api_key`. A hidden key row
  leaves `tts_api_key` unchanged (R13).
- Play sample: AI speech gives `voice_ais_resolve` plus `voice_ais_sample(owner, &t, key)` on
  the copy, and a resolve failure shows its `err` in the page's existing dim test-note line.
  Windows gives today's call.

- [ ] **Step 1: Write the failing test** `test_voice_speech_enables`, covering each combination:
  - Dictation: everything disabled.
  - Conversation with Windows: speech on, ai_rows off, uk_line as installed.
  - Conversation with AI and OpenAI, shared: no key, no url.
  - Custom, not shared: url and key shown.
  - AI: uk_line hidden.
- [ ] **Step 2: Run.** FAIL. **Step 3: Implement.** **Step 4: Run** `make -j test`,
  `xvfb-run -a make wintest` and `make -j release`. Optionally run
  `wine build/win/nutshell.exe --ui-demo=settings-voice` and report what shows: the rows, the
  greying with Windows speech, the key row hidden with OpenAI on both.
- [ ] **Step 5: Commit** `feat(voice): Settings > Voice gains Speech and the AI speech rows`.

Suggested implementer: Sonnet.

---

### Task 10: The self-report: AI speech mode and the resource count

**Files:**
- Modify: `src/ui/dll_hook_win.c` (a `voice-resources: N` line after `voice-net-opens`),
  `tests/selftest/check.sh`, `Makefile` (CRLF: the `ai` mode in `selftest-wine`'s loop and
  `case`), `tests/test_selftest_configs.c`
- Create: `tests/selftest/nutshell-voice-ai.config`: the conv config plus
  `"voice_speech_engine": "ai"`, `"voice_tts_provider": "custom"` and
  `"voice_tts_url": "http://127.0.0.1:9/v1"`.

**Interfaces:**
- `voice-resources: N` is the sum of `voice_ledger_count(r)` over every `VoiceRes`, written on
  the UI thread where the report already is.
- `check.sh`:
  - every mode except `sapi` asserts `voice-resources: 0`;
  - `sapi` asserts only that the line exists;
  - the new `ai` mode asserts winhttp, riched20, msftedit and mmdevapi were requested,
    mmdevapi is mapped, `no_speech ai` (no `sapi.dll`, no `wtsapi32.dll`), `voice-net-opens: 0`
    and `voice-resources: 0`.
- The usage line lists `off|voiceoff|on|conv|sapi|ai`.

- [ ] **Step 1:** extend `tests/test_selftest_configs.c` so it checks the new config parses
  with voice on, Conversation and AI speech, and extend `check.sh`. It fails until the line
  exists.
- [ ] **Step 2: Run** `make -j test`, `make -j release` and `make selftest-wine`: `ai` is
  missing or fails.
- [ ] **Step 3: Implement.** **Step 4: Run** all three. Every mode passes, the `sapi` mode as
  today (`sapi-control: unavailable` under Wine).
- [ ] **Step 5: Commit** `test(selftest): AI speech mode opens nothing until a reply is spoken`.

Suggested implementer: Haiku. The changes follow the existing modes line for line.

---

### Task 11 (orchestrator): spec findings, notes, version, build, full verification

- [ ] **Live spec §16:** add a part, "Found at build time (PR 3, 2026-11-2x)", holding:
  - the Rechecks section above, with its sources;
  - item 13 (prices, the retirement on 2027-01-06 with no `/audio/speech` successor, and the
    maintainer's R1 decision);
  - item 20 (the 13 voices);
  - item 25 (R6; the one-WebSocket question is unchanged);
  - item 14's counts measured on the branch (keys 69, `CFG_KEYS_MAX` 96, `NOTICE_ITEMS_MAX`
    24, `winhttp.def` 17);
  - the IID source for `IAudioRenderClient`;
  - every ruling R2 to R24 as built, and anything an implementer had to decide differently;
  - the test counts at the end.
- [ ] **Notes Open list:** a new item, "AI speech (PR 3 of live voice, feat/voice-ai-speech):
  nothing run on Windows", with one line per hand check:
  - (a) OpenAI AI speech with the default style: accent, time to the first word, no gap or
    click between chunks, the cut-in duck and stop latency.
  - (b) A Custom OpenAI-compatible server, with and without `instructions` support.
  - (c) Shared and separate keys: the key row shown and hidden, the DPAPI blob in the config,
    and the blob kept when the config moves to another user.
  - (d) The fallback: a wrong key (401) and Wi-Fi pulled mid-reply. The header shows, and
    Windows speech reads the rest from about the cut.
  - (e) "Carry on" after a cut-in resumes near the cut (R9).
  - (f) Headphones unplugged mid-reply, and a default-device change.
  - (g) Echo cancellation with our render client (a reply on speakers is not transcribed back),
    and whether our render session is ducked.
  - (h) One conversation's cost on the OpenAI usage page.
  - (i) Play sample with AI speech using unsaved values; it is disabled in Dictation.
  - (j) The Voice page at 100 %, 125 % and 200 %.
  - (k) `mingw32-make test` and `mingw32-make clean && mingw32-make release` on the Windows
    box.
  - (l) Retry during a conversation: the mic closes and the reply is spoken.
  - (m) After 2027-01-06: OpenAI AI speech falls back on every reply, as designed, until the
    Realtime follow-up.

  Then mark items (q), (r), (s) and (o)'s folded parts as fixed in code, with their hand check
  pending. Add the Realtime-speech follow-up to the todo, unless the maintainer chose otherwise
  at R1.
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
- [ ] **Whole-branch reviews** before any push (the maintainer's rule): an independent Opus
  review and a Fable review of the diff against `58dbd00`. Their findings go to the maintainer
  before any fix.

Suggested owner: Opus (orchestrator).
