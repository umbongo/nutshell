# AI speech (PR 3 of live voice) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Revision 3 (2026-11-25).**
- Revision 2 (`8b86ecd`) was critiqued by `claude-fable-5-2` (high), with the verdict "sound
  after changes". The main session ruled on every finding (spec §17 round 3), and this revision
  applies them:
  - Groq is cut, and there is no resampler.
  - A maintainer capture, Task 0, gates the PR.
  - Tasks 7 and 8 are split.
- Revision 1 is `a3b012f`. Tasks 3, 4 and 6 are built and do not change.

**Goal:** Let a conversation's replies be read by an AI voice.
- The voice comes from a real text-to-speech endpoint: Mistral by default, or a Custom
  OpenAI-compatible server.
- The audio streams as WAV, or as Mistral's float32 PCM if the capture says so. It is decoded
  in `src/core`, accepted only at 24 kHz, and played through Nutshell's own WASAPI render
  client.
- It has its own speech setting and a key bound to its provider.
- It falls back to Windows speech for the rest of a reply when AI speech fails in passing.
  When the setting cannot work, it says so once per conversation.

**Architecture:** The pure parts go in `src/core` with native tests and fuzz targets:
- `voice_aispeech.c`: the providers, target resolution, the shared and bound key, the request
  bodies, Mistral's event parser, failure classes, and the offset map.
- `voice_wavstream.c`: the WAV/PCM stream decoder.
- `voice_playq.c` (built).
- `voice_conv.c` (built, plus Task 13).

The Win32 parts go in `src/ui`:
- the render client (built);
- `ai_http_post_stream_ex`;
- the AI speech engine (`voice_aispeech_win.c`, in two tasks), which posts the same
  `WM_VOICE_TTS` messages SAPI does;
- the speech router in `voice_win.c`;
- the fallback and the follow-ups in `voice_conv_win.c` (in two tasks).

**Tech Stack:** C11, Win32 (MinGW cross-compile), WASAPI render, WinHTTP (delay-loaded, no new
entry points), custom `test_framework.h`, Wine `wintest`, libFuzzer `make fuzz-run`,
`make selftest-wine`.

**Spec:** `docs/superpowers/specs/2026-11-24-voice-live-conversation-design.md`.
- PR 3's scope is §15 item 3, as amended in §2, §6.1, §8.4, §8.5, §11.3, §13, §14 and §15.
- Its record is §16 "Decisions of 2026-11-25: AI speech provider" and §17 round 3. Read both
  before any task.

## Execution order

The capture does not block anything. Everything that does not depend on it comes first:

1. **Task 0**, the maintainer's Mistral capture, which can run at any time.
2. Tasks **1, 2, 12, 13, 5, 7a, 8b, 8a, 9, 10**.
3. **7b**, the network path, whose Mistral half the capture finalises.
4. **11**, gated on Task 0's fixtures.

Tasks 3, 4 and 6 are built.

## Global Constraints

- **Where to work:** worktree `/home/thomas/nutshell-wt/voice-ai-speech`, branch
  `feat/voice-ai-speech` (main `58dbd00` plus Tasks 3, 4 and 6, HEAD `31f9a20`). Never touch
  `~/nutshell` or another worktree. Do not push.
- **Reviews** are by Opus or Fable only, never Sonnet or Haiku, and never by the model that
  wrote the code. Every Opus review's findings go to the maintainer before any is fixed.
- **Version:** only Task 11 changes it: `"1.3.0"` and `1,3,0,0` in `src/ui/resource.h`,
  README's `**Version**:` line, and `build/win/nutshell.exe`.
- **Test first.** A test that tells two states apart needs a fixture in which they differ.
  - A test can always fail. A Wine or fixture skip uses `TEST_SKIP(reason)` (added by
    Task 2), which prints `[SKIP]` and is counted in the runner's summary. Task 11 lists every
    skip, and a capture-fixture skip blocks it.
- **Line endings are preserved.**
  - CRLF: `src/config/loader.c`, `Makefile`, `src/core/ai_http.h` and `src/ui/ai_http_win.c`.
  - Every other file touched here is LF.
  - New fixtures and fuzz seeds are marked `binary` in `.gitattributes` (Task 2).
  - Before each commit, `git diff --stat` and `git diff --stat --ignore-cr-at-eol` agree.
- **Portable logic goes in `src/core`.** New `src/core/*.c` and native `tests/*.c` are picked
  up by the wildcards; every test function is declared and called in `tests/runner.c`.
  Win32-only tests are `tests/*_win.c`, in `WIN_ONLY_TESTS` and `WIN_TEST_SRCS`, called from
  `tests/win_runner.c`.
- **Both builds use `-Werror`.** MinGW adds `-Wpedantic -Wshadow -Wformat=2 -Wconversion`.
  - Every task that touches `src/ui` runs `make -j release` far enough to compile its objects.
  - Baselines at `31f9a20`: 4,547 native and 185 `wintest`. Before PR 3: 4,527 native.
- **Design-system rules (CLAUDE.md):**
  - no `RGB(` literal in `src/ui`;
  - use `ns_tokens()`, `ns_scale`, `ns_font`, the page's `make_*` helpers, `ns_motion` and
    `ns_hover`;
  - no new `MessageBox`.
- **Optional-feature rule.** With voice off, AI off, Dictation mode, or a conversation that has
  not spoken yet:
  - no render client, no speech worker, no speech request;
  - no DLL request.

  `voice_ais_start` is the only AI speech context constructor, and `voice_net_open` the only
  voice network opener. Each refuses unless `voice_gate_on()`. `sapi.dll` loads with AI speech
  only on a fallback.
- **The resource ledger** is UI-thread only.
  - `VRES_RENDER` is acquired whenever a context pushes `render_open` while not holding the
    render client. It is released when the context pushes `render_close` or handles
    `VAE_RENDER_PREEMPTED`.
  - `VRES_SPEECH_REQ` counts one per worker thread. It is acquired at the worker's creation
    and released only when that thread has ended (on `VAI_EXITED`).
- **Content.** Spoken text, bodies, audio and keys are `secure_zero`ed before free. Text and
  bodies are logged only through `DLOG_CONTENT`. A request is logged by kind and HTTP status
  only, and the `Authorization` buffer is wiped after the call.
- **Messages:**
  - new: `WM_VOICE_AIS (WM_APP + 0x49)`;
  - built (Task 6): `VAM_RENDER_OPENED`, `VAM_PLAYED`, `VAM_PLAY_DONE`, `VAM_RENDER_ERROR` and
    `VAE_RENDER_PREEMPTED`.
- **Control ids:** `IDC_VOICE_SPEECH 3121`, `IDC_VOICE_TTS_PROVIDER 3122`,
  `IDC_VOICE_TTS_URL 3123`, `IDC_VOICE_TTS_KEY 3124`, `IDC_VOICE_AI_VOICE 3125`,
  `IDC_VOICE_AI_MODEL 3126` and `IDC_VOICE_AI_STYLE 3127` (recheck that they are free).
- **Copy fixed by the spec** (UTF-8; the dash is U+2014):
  - header: `AI speech failed: using Windows speech`
  - chat, once per conversation: `AI speech is off for this conversation: <reason>. Replies are
    read with Windows speech.`
  - chat (Task 13): `Conversation ended: the last turn took too long`
  - Voice page: `Each spoken reply's text is sent to <host>.`
  - the British-voice notice: `Windows has no British English woman's voice installed, so
    replies use <voice>. To get one, add English (United Kingdom) under Windows Settings > Time
    & language > Speech, or choose AI speech in Settings > Voice.`
  - Play sample: `Hello. This is how replies will sound.`
  - notice lines (label — value):
    - `Voice: speech` — `Windows speech`
    - `Voice: speech provider` — `Mistral`
    - `Voice: speech URL` — `none`
    - `Voice: speech API key` — `none; the speech-to-text key is used when the provider is the
      same`
    - `Voice: AI voice` — `the provider's default (for Mistral, Mary, British English)`
    - `Voice: AI speech model` — `the provider's default`
    - `Voice: accent and style` — `none`
  - failure reasons (Task 2):
    - `the speech provider refused the key (HTTP <n>)` for 401 and 403;
    - `the speech model or voice was not found (HTTP 404)`;
    - `the speech provider refused the request (HTTP 422)`;
    - `unsupported audio format`;
    - and the resolve errors of Task 2.
- **Numbers:**
  - Audio accepted: exactly 24,000 Hz; 16-bit or 32-bit float; 1 or 2 channels.
  - The duck is 18 %.
  - Chunks are `voice_speak_chunks` as built: the first about 200 bytes, later up to 1,000, at
    sentence ends.
  - The render buffer is 100 ms, with `VAM_PLAYED` every 100 ms.
  - The next request waits until less than 20,000 ms of audio is queued.
  - The done guard fires after 2,000 ms without progress.
  - Bounded joins wait 3,000 ms.
  - The Ending deadline is max(180,000 ms, upload budget + 10,000 ms).
- **Settings keys:** 62, then 69 after Task 1. `CFG_KEYS_MAX` 96, `NOTICE_ITEMS_MAX` 24,
  `winhttp.def` 17.

## Review Focus

1. **A stream split anywhere**: inside the WAV header, a sample, a float, a base64 group or an
   event line. A second `RIFF…WAVE` in mid-stream is a failure. Pinned by
   `test_vwav_split_everywhere` and `test_vwav_second_header_fails` (Task 12),
   `test_vais_sse_split_lines` (Task 2), and `fuzz_wavstream` and `fuzz_aisse`.
2. **Old audio after a stop or a cut-in.** Pinned by `test_vplayq_flush_drops_late_data`
   (built), `test_voice_ais_stop_then_speak_no_old_audio` (Task 7a) and
   `test_voice_ais_dead_worker_never_pushes` (Task 7a).
3. **A slow network.** An underrun is not the end, and the done guard never cuts a reply still
   making progress. Pinned by `test_vplayq_underrun_not_done` (built) and
   `test_voice_ais_done_guard_waits_for_progress` (Task 7a).
4. **A setting that cannot work** (a wrong key, a key bound to another provider, an unknown
   provider, a 404 model, an unsupported format). One chat note per conversation, then
   Windows speech without retrying; a 429 or a network error keeps falling back per reply.
   Pinned by `test_vconv_win_ai_fatal_note_once` and `test_vconv_win_ai_passing_retries`
   (Task 8a), and `test_vais_key_binding` (Task 2).
5. **Teardown against a server that never answers**, and voice off with a worker detached.
   Pinned by `test_voice_ais_quit_while_server_hangs` (Task 7b) and
   `test_voice_ais_dead_worker_never_pushes` (Task 7a). A reply with quotes, newlines, brackets
   and non-ASCII text makes a valid body with its brackets neutralised:
   `test_vais_body_escapes` and `test_vais_brackets_neutralised` (Task 2).

## Rulings

Each ruling is `decision — why — cost if wrong`. The provider rulings are also in the spec's
§16. "(round 3)" marks a main-session ruling on the critique of `8b86ecd`.

- **R1** `Ruling: the AI speech providers are Mistral (the default) and Custom; OpenAI is not a preset, and Groq is cut (round 3) — OpenAI shuts down on 2027-01-06; Orpheus is Preview at 10 requests a minute and 100 a day — cost if wrong: OpenAI users use Custom until the shutdown; Groq users wait for a later PR.`
- **R2** `Ruling (round 3): the default Mistral voice is voice_1787 (Mary, English (UK), female), to be confirmed by Task 0; the seeds are voice_1787, voice_1786 (Sky), voice_590 (Harriet) and gb_jane_neutral; Custom has no default voice, and its seeds are bf_emma and af_heart (Kokoro) — Mary is a documented Voxtral 3 English (UK) woman — cost if wrong: the default changes after the capture.`
- **R3** `Ruling (round 3): the default Mistral model is voxtral-mini-tts-3-0, the only Mistral seed (voxtral-mini-tts-2603 is deprecated Voxtral 2); Custom has no default model, and its seed is kokoro; the style is empty by default, enabled for Custom only, and sent as instructions only when typed; no tts-1 rule and no OpenAI seeds — cost if wrong: one default string.`
- **R4** `Ruling: Mistral's body is model, input, voice_id, response_format and stream:true, sent with Accept: text/event-stream; Custom's is model, voice, input and response_format "wav", plus instructions when typed; nothing else, because Mistral refuses unknown fields with 422 — cost if wrong: one provider fails, and the note names the status.`
- **R5** `Ruling: a speech request never follows a redirect — the bearer key is never re-sent to another host — cost if wrong: a server behind a redirect fails.`
- **R6 to R8** (unchanged; R6 and R7 built in Task 6):
  - the render client opens at a context's first speech, stops between texts and is released
    at the context's end;
  - it has one owner, and pre-emption reports `VAE_RENDER_PREEMPTED`;
  - there is one worker thread per spoken text, its next request waits below 20 s queued, and
    a response is never stalled mid-body.
- **R9** `Ruling: the cut-off is in proportion to UTF-8 bytes within the chunk (an estimate of 1,600 frames per byte while it streams), snapped back to its word start — every offset in the glue is a byte offset — cost if wrong: a few words repeated or skipped.`
- **R10** `Ruling (wording corrected, round 3): a passing failure (network, 429, 5xx, a lost device, any stream failure of R32) lets what is queued play out (up to 20 s of audio plus one chunk), then posts VTT_ERROR with the resume offset; the glue reads on with Windows speech, and the next speech tries AI again; a fallback never raises the British-voice notice — no overlap and no lost words — cost if wrong: that much audio plays before the handover.`
- **R11, R12** (unchanged): phrases use the configured engine; repeat re-synthesises.
- **R13** `Ruling: the shared key is the effective speech-to-text target's own (ai_api_key when key_from_chat, else stt_api_key) when its provider id equals the speech provider's, and for Custom also its normalised base; an unresolved STT target means no sharing; while shared, the key row is hidden and a saved tts_api_key is kept — spec §8.4 — cost if wrong: a second key prompt.`
- **R14** (unchanged): `voice_ai_style` is `char[1024]`; a missing key gives the default (now
  empty), and `""` means no instructions.
- **R15** `Ruling: a passing failure shows the header only; the debug log gets the status or decoder error, with no content — cost if wrong: none.`
- **R16** `Ruling: rows in Conversation are Pause, Speech, Windows voice (and its lines), the AI rows, the privacy line, Play sample, the echo line and the ducking line; Windows voice stays enabled; the English (UK) line hides with AI speech; the style row is enabled for Custom only; a provider change reseeds voice and model, with the provider's default first and stored as "" — cost if wrong: a layout change.`
- **R17 to R24** (unchanged):
  - Play sample uses unsaved values;
  - the self-report gains `voice-resources: N` and the `ai` mode;
  - no harness case;
  - `tts_start(token, engine)` plus an optional `speech_engine()`;
  - the duck is a render gain (built);
  - `VoiceRenderMsg` is malloc'd (built);
  - `VCE_EXTERNAL_SEND` comes from Retry (the core half is built);
  - the position is the written position less the padding (built).
- **R25** `Ruling (round 3): Custom asks for wav; Mistral asks for wav unless Task 0 shows one WAV header per delta, in which case pcm, decoded as raw float32 24 kHz mono through the same decoder's raw mode; the WAV data size is ignored (read to the end) — wav describes itself, and pcm is a one-line switch — cost if wrong: none beyond Task 0.`
- **R26** `Ruling (round 3): no resampler; exactly 24,000 Hz is accepted, 16-bit or float32, mono or stereo (averaged); WAVE_FORMAT_EXTENSIBLE only if the capture shows it; anything else fails the reply as "unsupported audio format", a setting that cannot work (R37); the Task 6 render client needs no follow-up — both supported servers deliver 24 kHz — cost if wrong: a 22,050 Hz Custom server cannot be used.`
- **R27** — removed (round 3: the Groq chunk limit went with Groq).
- **R28** `Ruling: Mistral's stream is read through sse_line; an event whose event: name or data "type" is speech.audio.delta carries audio_data, read with json_scan_string; speech.audio.done ends it; an error member or event fails it; unknown events are ignored and logged by name only — the SDK's shape (spec §16), to be checked against Task 0 — cost if wrong: the capture shows another envelope, and Task 7b's fixtures change.`
- **R29** `Ruling (round 3): the Ending deadline is max(180 s, this turn's upload budget + 10 s), where the budget is twice the send plus receive timeouts of voice_stt_upload_timeouts (the one retry); at the deadline the conversation ends with "Conversation ended: the last turn took too long"; the turn's audio is wiped, its words stay — cost if wrong: a hung upload holds the conversation open a little longer.`
- **R30** `Ruling (round 3): the done guard counts the frames pushed per reply in the engine; once the worker has finished, it is armed and re-armed on every VAM_PLAYED that shows progress, and fires (VTT_DONE when every chunk completed) only after 2,000 ms with none — render_end can be lost, and a fixed delay could cut a slow reply — cost if wrong: a lost end costs 2 s of silence.`
- **R31** `Ruling: voice_playq's frames_in_chunk accumulates across consecutive blocks of the same reply and chunk (a small change to the built file, made in Task 7a with its own test) — a chunk arrives in many blocks — cost if wrong: none.`
- **R32** `Ruling (round 3): besides HTTP and network errors, these fail a reply (passing, R10), each with a test: a chunk that produced 0 frames; a decoder not in VWS_DATA at the chunk's end; sse_line_skipped() > 0; a second RIFF....WAVE in mid-stream; a speech.audio.done with no audio before it — none of them may pass as a quiet, short reply — cost if wrong: a false failure falls back to Windows speech.`
- **R33** `Ruling (round 3): Task 0 is the maintainer's Mistral capture (two streams, one forced error); the build does not wait for it; Task 2's parser and Task 7b's worker are built against the SDK's shape with fixtures named provisional_*; Task 11 has a hard gate: the PR is not marked ready until the captures are in, the provisional fixtures replaced, and the tests green against them — the raw bytes are unverified — cost if wrong: Task 7b reworks its Mistral half after the capture.`
- **R34** `Ruling (round 3): fuzz targets fuzz_wavstream (Task 12) and fuzz_aisse (Task 2), each with at least 5 seeds under tests/fuzz/corpus/<name>/, in FUZZ_TARGETS so make fuzz-run replays them — both parse untrusted bytes — cost if wrong: none.`
- **R35** `Ruling (round 3): any speak opens the render client when the context does not hold it (first speak, after a pre-emption, after a render error), acquiring VRES_RENDER and re-sending the gain when it is not 100; VAE_RENDER_PREEMPTED while not playing marks the context as not holding, releases VRES_RENDER and posts nothing; while playing, it is a passing failure at the current offset — cost if wrong: none.`
- **R36** `Ruling (round 3): every worker has a dead flag under the engine's lock; the worker checks it, holding that lock, before every render push and every PostMessage, and stops when it is set; quit sets it before its bounded join and, on timeout, detaches the thread without releasing VRES_SPEECH_REQ, which is released only on that thread's VAI_EXITED; voice_feature_apply and shutdown call voice_ais_quit_all (which marks every worker dead under the lock) before voice_audio_stop, so no push can begin after the audio thread is asked to stop — cost if wrong: a wedged worker keeps one ledger count until it ends.`
- **R37** `Ruling (round 3): a resolve error, HTTP 401, 403, 404, 422, or VOICE_AIS_HR_FORMAT is a setting that cannot work: the glue writes one chat note per conversation ("AI speech is off for this conversation: <reason>. Replies are read with Windows speech."), reads with Windows speech, and does not try AI speech again in that conversation; network errors, 429, 5xx, device loss and stream failures keep R10 — cost if wrong: a passing 404 turns AI speech off until the next conversation.`
- **R38** `Ruling (round 3): an unknown provider id, openai and gemini included, is the resolve error "The speech provider is not known"; validation keeps the stored id; the page shows no provider selected, and Save keeps the id unless one is picked — cost if wrong: none.`
- **R39** `Ruling (round 3): with AI speech selected the page shows a dim line, "Each spoken reply's text is sent to <host>." (api.mistral.ai, or the Custom URL's host); the Base URL help says the reply text and the key go to that server and http:// is not encrypted — cost if wrong: none.`
- **R40** `Ruling (round 3): voice_ais_body maps [ to ( and ] to ) in the input it sends, byte for byte, so the offsets into the kept text stay valid; SAPI and the echo filter keep the original — cost if wrong: a bracket is spoken as a parenthesis.`
- **R41** `Ruling (round 3): Task 7 is split into 7a (context, render, ledger, guard and dead flag, driven by a fake fetch) and 7b (the real fetch: HTTP, the two shapes, the decoder, failure classes); Task 8 into 8b (the PR 2 follow-ups) and 8a (the router, engine choice, fallback and the once-per-conversation note) — each half is one reviewable cycle — cost if wrong: none.`
- **R42** `Ruling (round 3): no test passes silently; TEST_SKIP(reason) prints and counts; the Wine-only cancel test skips that way and must pass on Windows; the offsets test requires at least one VTT_WORD at or past the offset of "Two" — cost if wrong: none.`
- **R43** `Ruling (round 3, the smallest form): a saved speech key is bound to its provider by storing "<binding>\n<key>" as the DPAPI plaintext of tts_api_key, where the binding is voice_ais_binding() ("mistral", or "custom " and the normalised base URL); it loads into the runtime fields tts_api_key and tts_api_key_for (no JSON key, so no new notice line); voice_ais_key returns "" when tts_api_key_for differs from the current binding, which R37 reports as "Enter a speech API key"; the page rebinds only a key the user edited — a key can never reach a host it was not entered for — cost if wrong: changing the provider asks for the key again.`
- **R44** `Ruling (round 3, finding rejected as stated): plain http:// stays allowed for Custom, with R39's warning, as speech-to-text allows it — a LAN server is reached by name, and a private-address test would refuse it — cost if wrong: a user sends text and key in clear to a public http:// host despite the warning.`

**Spec against the merged code, and which wins:**
- §3's counts: the code.
- `VNK_TTS` "unused": the spec.
- The notice clause: the spec.
- "Characters": the code (bytes). The spec has been corrected to match (round 3, finding 19).
- §16 item 7's SAPI render-client fallback is not built.

## Notes items (a) to (s)

**Folded in:**
- (r) Retry with the mic open: Task 4 (built) and Task 8b.
- (s) The silence clock after a refusal phrase or a paused local repeat: Task 4 (built).
- (q) The Hearing and Ending watchdogs restarting the silence clock: Task 4 (built) and
  Task 13.
- (q) The 10-minute Speaking watchdog: Task 4 (built) and Task 8b.
- (s) The `voice_echo` tick wrap: Task 4 (built).
- (r) A close lost when `realloc` fails: Task 6 (built).
- (r) The event queue dropping `VCE_END`: Task 8b.
- (o) Play sample's help: Task 1.
- (o) The notice lost when the conversation ends first: Task 8b.

**Left out:** Enter while a reply is spoken (the maintainer decides); the RDP wording; the
one-line triple-backtick fence; the header after a paused announcement; Settings destroyed
before its owner; and every hand check.

---

### Task 0 (the maintainer's): capture Mistral's real stream

**Status:** the maintainer's, at any time before Task 11. Nothing waits for it except Task 11's
gate (R33).

**Where:**
- Run from `~/nutshell-wt/voice-ai-speech` on `automaton`, in bash, with the Mistral key in
  the environment variable `MISTRAL_API_KEY`. The key is never echoed and never on a command
  line: the header comes from a process substitution.
- Files go to `tests/fixtures/voice_tts/` on `feat/voice-ai-speech`, as `<name>.sse` (the raw
  body) and `<name>.headers` (the response headers).

```bash
cd ~/nutshell-wt/voice-ai-speech
: "${MISTRAL_API_KEY:?set MISTRAL_API_KEY first}"
D=tests/fixtures/voice_tts
mkdir -p "$D"
auth() { printf 'Authorization: Bearer %s\n' "$MISTRAL_API_KEY"; }
TXT='Hello. This is how replies will sound. A second sentence checks that the stream carries on.'
for fmt in wav pcm; do
  curl -sS -N --max-time 90 -X POST https://api.mistral.ai/v1/audio/speech \
    -H @<(auth) -H 'Content-Type: application/json' -H 'Accept: text/event-stream' \
    -D "$D/mistral_stream_$fmt.headers" -o "$D/mistral_stream_$fmt.sse" \
    --data "{\"model\":\"voxtral-mini-tts-3-0\",\"input\":\"$TXT\",\"voice_id\":\"voice_1787\",\"response_format\":\"$fmt\",\"stream\":true}"
done
curl -sS -N --max-time 90 -X POST https://api.mistral.ai/v1/audio/speech \
  -H @<(auth) -H 'Content-Type: application/json' -H 'Accept: text/event-stream' \
  -D "$D/mistral_error_bad_voice.headers" -o "$D/mistral_error_bad_voice.sse" \
  --data "{\"model\":\"voxtral-mini-tts-3-0\",\"input\":\"$TXT\",\"voice_id\":\"no_such_voice_0\",\"response_format\":\"wav\",\"stream\":true}"
sed -i '/^[Ss]et-[Cc]ookie:/d' "$D"/*.headers
grep -c -F -f <(printf '%s\n' "$MISTRAL_API_KEY") "$D"/* ; ls -l "$D"
python3 - "$D/mistral_stream_wav.sse" /tmp/mistral_mary.wav <<'PY'
import sys, json, base64
out = open(sys.argv[2], 'wb')
for line in open(sys.argv[1], encoding='utf-8'):
    if line.startswith('data:'):
        try: d = json.loads(line[5:])
        except ValueError: continue
        if 'audio_data' in d: out.write(base64.b64decode(d['audio_data']))
PY
```

- **What it must show:**
  - Every file has a `grep` count of 0 (the key appears nowhere).
  - Both `.headers` files start `HTTP/2 200` (or `HTTP/1.1 200`), and the error capture gets a
    4xx.
  - Play `/tmp/mistral_mary.wav` (it is not committed) and check that the voice is Mary, an
    English (UK) woman. This confirms R2.
- **Commit** the six files, plus `.gitattributes`' `tests/fixtures/voice_tts/* binary` line if
  Task 2 has not landed yet. Use a message such as `test(voice): Mistral speech captures for
  the AI speech parser`.
- **Then tell the orchestrator.** Task 11's gate starts from these files:
  - whether `type` is inside `data`;
  - one WAV header or one per delta (R25 chooses `wav` or `pcm` from this);
  - the delta sizes;
  - how the bad voice was reported (HTTP status or an event).

---

### Task 1: The PR 3 settings, the bound key, help, notice lines and digest

**Status:** to build. Revision 3: providers Mistral and Custom; the style is empty by default;
the key's binding (R43).

**Files:**
- Modify:
  - `src/config/config.h`;
  - `src/config/loader.c` (CRLF: load, save, the bound secret, `k_setting_keys`, the free
    path's `secure_zero`);
  - `src/core/voice_config.[ch]`;
  - `src/core/settings_help.[ch]`;
  - `src/core/notice.c` (`is_voice_subkey` and the British-voice notice's clause).
- Test: `tests/test_voice_config.c`, `tests/test_notice.c`, and the existing config round-trip
  and `k_setting_keys` tests.

**Interfaces:**
- Produces, in `Settings` (after `voice_windows_voice_name`):
  ```c
  int  voice_speech_engine;            /* VOICE_SPEECH_WINDOWS (0) or VOICE_SPEECH_AI (1); JSON "windows"/"ai" */
  char voice_tts_provider[32];         /* "mistral" (default) or "custom"; any other id is kept as stored (R38) */
  char voice_tts_url[CFG_STR_MAX];     /* base URL, Custom only */
  char tts_api_key[CFG_STR_MAX];       /* DPAPI; plaintext "<binding>\n<key>" on disk (R43) */
  char tts_api_key_for[CFG_STR_MAX];   /* runtime only, never a JSON key: the binding read with it */
  char tts_api_key_enc_preserved[CFG_BLOB_MAX];
  char voice_ai_voice[64];             /* "" = the provider's default (R2) */
  char voice_ai_tts_model[CFG_STR_MAX];/* "" = the provider's default (R3) */
  char voice_ai_style[1024];           /* default "" (R3, R14) */
  ```
- In `voice_config.h`:
  - `VOICE_SPEECH_WINDOWS 0`, `VOICE_SPEECH_AI 1`;
  - `const char *voice_speech_json(int e)`;
  - `int voice_speech_from_json(const char *s)` (unknown or NULL gives Windows);
  - `#define VOICE_AIS_DEFAULT_PROVIDER "mistral"`.
- `SettingsHelpId` gains `SH_VOICE_SPEECH`, `SH_VOICE_TTS_PROVIDER`, `SH_VOICE_TTS_URL`,
  `SH_VOICE_TTS_KEY`, `SH_VOICE_AI_VOICE`, `SH_VOICE_AI_MODEL`, `SH_VOICE_AI_STYLE` and
  `SH_VOICE_PLAY_SAMPLE`.
- The bound secret (R43):
  - Save encrypts `tts_api_key_for + "\n" + tts_api_key` when the key is not empty (through a
    stack buffer, wiped).
  - Load splits the decrypted text at its first `\n`. With no `\n`, the binding is empty, so
    the key never matches.
  - Preservation and migration work as for `stt_api_key`.

- [ ] **Step 1: Write the failing tests**
  - `test_voice_ais_settings_defaults`: a config without the seven keys loads with Windows,
    `"mistral"`, an empty URL, an empty key, an empty voice, an empty model and an empty style.
  - `test_voice_ais_settings_roundtrip`: AI, `"custom"`, `"http://10.0.0.5:8880/v1"`,
    `"bf_emma"`, `"kokoro"`, a style with a quote and a newline, and a key bound to
    `"custom http://10.0.0.5:8880/v1"` all survive a save and load. The file holds a
    `$dpapi$v1$` blob and neither the key nor the binding in clear text.
  - `test_voice_ais_key_binding_roundtrip`: the binding comes back in `tts_api_key_for`. A
    blob whose plaintext has no `\n` loads with an empty binding.
  - `test_voice_ais_key_preserved`: an undecryptable blob is kept verbatim and written back;
    a new key drops it.
  - `test_voice_ais_style_empty_kept`: `"voice_ai_style": "x"` and `""` both load as written.
  - `test_voice_ais_unknown_provider_kept`: `"openai"`, `"gemini"` and `"groq"` load and save
    unchanged (R38: resolve reports them, validation does not rewrite them).
    `"voice_speech_engine": "loud"` loads as Windows.
  - `test_voice_ais_notice_lines`: the seven Global Constraints lines, in key order, and the
    fold into "Voice" when the switch is new (`tts_api_key` included).
  - `test_voice_ais_digest`: each of the seven fields, the key and its binding, changes
    `voice_settings_digest`.
  - The `k_setting_keys` test passes with 69 rows.
  - `test_notice_msg_voice_british_names_the_voice` gains the clause.
  - `test_settings_help_play_sample`: label `Play sample`, kind `SH_KIND_ACTION`.
- [ ] **Step 2: Run** `make -j test`. FAIL.
- [ ] **Step 3: Implement.** Help entries (`SETTINGS_PAGE_VOICE`), each `default_text` being
  the notice value:
  - `SH_VOICE_SPEECH` (`Speech:`): Windows speech uses a voice installed on this PC. AI speech
    sends each reply's text to the speech provider and plays the AI-generated voice it returns.
    If AI speech fails, the rest of the reply is read with Windows speech. Conversation mode
    only.
  - `SH_VOICE_TTS_PROVIDER` (`Speech provider:`): Mistral (Voxtral, English (UK) voices), or
    Custom (an OpenAI-compatible `/audio/speech`, for example a Kokoro server on your network).
  - `SH_VOICE_TTS_URL` (`Base URL:`): a pasted `/audio/speech` is trimmed. The text of each
    spoken reply, and the speech key, go to this server. `http://` is not encrypted (R39).
  - `SH_VOICE_TTS_KEY` (`Speech API key:`): shown only when the speech provider is not the
    speech-to-text one. It is kept encrypted for this Windows user and only for the provider
    it was entered for.
  - `SH_VOICE_AI_VOICE` (`AI voice:`): empty means the provider's default (Mary, for
    Mistral); Custom needs one.
  - `SH_VOICE_AI_MODEL` (`AI speech model:`): empty means the provider's default; Custom needs
    one.
  - `SH_VOICE_AI_STYLE` (`Accent and style:`): Custom only, sent as instructions when typed.
  - `SH_VOICE_PLAY_SAMPLE` (`SH_KIND_ACTION`, `Play sample`, `default_text` NULL): it speaks a
    sentence with the chosen speech and voice, using the page's values before Save.
- [ ] **Step 4: Run** `make -j test`. PASS. Record the count, 69.
- [ ] **Step 5: Commit** `feat(voice): AI speech settings, a provider-bound key, help and notice lines`.

Suggested implementer: Sonnet. Reviewer: Opus or Fable.

---

### Task 2: AI speech in core: providers, targets, keys, bodies, Mistral's events, failures, the offset map

**Status:** to build. Revision 3: Groq removed; unknown providers, key binding, brackets,
failure classes and the event parser's failures; a fuzz target; `TEST_SKIP`.

**Files:**
- Create: `src/core/voice_aispeech.h`, `src/core/voice_aispeech.c`, `tests/fuzz/fuzz_aisse.c`,
  `tests/fuzz/corpus/aisse/*` (at least 5 seeds: the provisional fixtures, an error, a split
  line, an oversized line, garbage), and `tests/fixtures/voice_tts/provisional_*.sse`
  (written from the SDK's shape, R33).
- Modify:
  - `src/core/base64.[ch]` (a decoder);
  - `tests/test_framework.h`, `tests/runner.c` and `tests/win_runner.c` (`TEST_SKIP` and a
    counted skip total in both summaries, R42);
  - `Makefile` (CRLF: `aisse` in `FUZZ_TARGETS`);
  - `.gitattributes` (`tests/fixtures/voice_tts/* binary`, `tests/fuzz/corpus/aisse/* binary`
    and `tests/fuzz/corpus/wavstream/* binary`).
- Test: `tests/test_voice_aispeech.c`, and the existing base64 tests.

**Interfaces:**
- Consumes: Task 1's fields; `voice_stt_resolve`, `voice_base_url_normalise`,
  `json_escape_string`, `json_scan_string` and `sse_line`.
- Produces:
```c
/* test_framework.h */
#define TEST_SKIP(reason)   /* prints "  [SKIP] <test>: <reason>", counts a skip, ends the test as not failed */
/* base64.h */
size_t base64_decode(const char *src, size_t len, unsigned char *dst, size_t cap); /* (size_t)-1 on a bad byte or cap */
/* voice_aispeech.h */
typedef enum { VAIS_SHAPE_OPENAI = 0, VAIS_SHAPE_MISTRAL } VoiceAisShape;
typedef enum { VAIS_KEY_OWN = 0, VAIS_KEY_STT, VAIS_KEY_CHAT } VoiceAisKey;
typedef struct { const char *id, *label, *base; const char *const *models, *const *voices;
                 VoiceAisShape shape; int needs_key; } VoiceAisProvider;   /* mistral, custom */
const VoiceAisProvider *voice_ais_providers(size_t *n);
const VoiceAisProvider *voice_ais_provider_find(const char *id);   /* NULL for any other id */
typedef struct { char url[512], model[CFG_STR_MAX], voice[64], style[1024], provider[16],
                 host[256]; VoiceAisShape shape; VoiceAisKey key_from; int send_instructions; } VoiceAisTarget;
int  voice_ais_resolve(const Settings *s, VoiceAisTarget *t, char *err, size_t cap);
void voice_ais_binding(const Settings *s, char *out, size_t cap);  /* "mistral" | "custom <base>" | "" */
int  voice_ais_key_shared(const Settings *s);
const char *voice_ais_key(const Settings *s, const VoiceAisTarget *t);   /* "" when unbound (R43) */
char *voice_ais_body(const VoiceAisTarget *t, const char *text, size_t len, size_t *out_len);
                                                         /* R4; [ ] -> ( ) in input (R40) */
/* failures: VoiceTtsMsg.hr for an AI context */
#define VOICE_AIS_HR_NET     (-1L)   /* transport, passing */
#define VOICE_AIS_HR_RESOLVE (-2L)   /* setting cannot work */
#define VOICE_AIS_HR_FORMAT  (-3L)   /* unsupported audio format: cannot work */
#define VOICE_AIS_HR_DEVICE  (-4L)   /* render lost or pre-empted while playing: passing */
#define VOICE_AIS_HR_STREAM  (-5L)   /* R32: passing */
int  voice_ais_failure_fatal(long hr);                    /* R37: RESOLVE, FORMAT, 401, 403, 404, 422 */
void voice_ais_failure_reason(long hr, const char *resolve_err, char *out, size_t cap);
typedef enum { VAIS_EV_NONE = 0, VAIS_EV_AUDIO, VAIS_EV_DONE, VAIS_EV_ERROR } VoiceAisEvKind;
typedef struct { char event[64]; int audio_seen; } VoiceAisSse;
typedef struct { VoiceAisEvKind kind; unsigned char *audio; size_t audio_len; } VoiceAisEv;
void voice_ais_sse_init(VoiceAisSse *s);
int  voice_ais_sse_line(VoiceAisSse *s, const char *line, size_t len, VoiceAisEv *ev);  /* R28 */
#define VOICE_AIS_EST_FRAMES_PER_BYTE 1600
size_t voice_ais_offset(const char *text, size_t chunk_from, size_t chunk_len,
                        uint64_t played, uint64_t total, int complete);   /* R9 */
```
- `voice_ais_resolve`:
  - URLs: Mistral `https://api.mistral.ai/v1/audio/speech`; Custom `{normalised base}/audio/speech`.
  - `host` is set for the privacy line.
  - An empty model or voice takes the provider's `[0]`.
  - Errors in `err`:
    - `The speech provider is not known` (R38);
    - `Enter the speech Base URL` and `The speech Base URL is not valid`;
    - `Enter the speech model your server uses` and `Enter the voice your server uses` (Custom);
    - `Enter a speech API key` (Mistral, whose `needs_key` is 1, when `voice_ais_key` gives
      `""`).
- `voice_ais_sse_line` follows R28. A DONE before any AUDIO (`audio_seen` 0) is returned as
  ERROR (R32).

- [ ] **Step 1: Write the failing tests**
  - Resolution and keys:
    - `test_vais_resolve_mistral_defaults`: the URL, `voxtral-mini-tts-3-0`, `voice_1787`,
      the MISTRAL shape, `send_instructions` 0, and the host `api.mistral.ai`.
    - `test_vais_resolve_custom`: normalising, the four Custom errors, an empty key allowed,
      and `send_instructions` 1 only with a style typed.
    - `test_vais_unknown_provider`: `openai`, `gemini`, `groq` and `""` each give
      `The speech provider is not known`.
    - `test_vais_key_shared`: STT Mistral with TTS Mistral gives `VAIS_KEY_STT`. A Custom chat
      used by "Use current AI assistant", with the same Custom base, gives `VAIS_KEY_CHAT`.
    - `test_vais_key_not_shared`: STT OpenAI with TTS Mistral, different Custom bases, and an
      unresolved STT target all give `VAIS_KEY_OWN`.
    - `test_vais_key_binding`: a key bound to `mistral` is returned for Mistral and is `""`
      for Custom. A key bound to `custom http://a/v1` is `""` after the URL becomes
      `http://b/v1`, and resolve then says `Enter a speech API key` for Mistral.
  - Bodies:
    - `test_vais_body_mistral`: exactly `model`, `input`, `voice_id`, `response_format` and
      `"stream":true`.
    - `test_vais_body_openai_shape`: exactly `model`, `voice`, `input` and `response_format`,
      plus `instructions` only when typed.
    - `test_vais_body_escapes`: quotes, a backslash, a newline, a tab, `é` and `✓` round-trip
      in both shapes.
    - `test_vais_brackets_neutralised`: the input `[whisper] go [now]` is sent as
      `(whisper) go (now)` with the same byte length. The caller's text is unchanged.
    - `test_vais_body_rejects_crlf`: a CR or LF in the model or voice gives NULL.
  - Failures:
    - `test_vais_failure_classes`: RESOLVE, FORMAT, 401, 403, 404 and 422 are fatal. NET,
      DEVICE, STREAM, 429, 500 and 503 are not.
    - `test_vais_failure_reasons`: each reason gives the exact Global Constraints text.
  - Mistral's events, against `provisional_mistral_wav.sse` and `provisional_mistral_error.sse`:
    - `test_vais_sse_audio_delta`: a delta of 20 KB of audio decodes.
    - `test_vais_sse_type_only`: `type` alone, with no `event:` line, is audio.
    - `test_vais_sse_done_and_error`: `done` after audio gives DONE; an `error` member gives
      ERROR; an unknown type gives NONE.
    - `test_vais_sse_done_without_audio`: gives ERROR (R32).
    - `test_vais_sse_split_lines`: 1-byte feeds through `sse_line_feed` give the same events.
    - `test_vais_sse_bad_base64`: gives ERROR.
  - The capture, through `TEST_SKIP` until Task 0's files exist:
    - `test_vais_sse_capture_wav`, `test_vais_sse_capture_pcm` and
      `test_vais_sse_capture_error` run the same assertions against
      `mistral_stream_wav.sse`, `mistral_stream_pcm.sse` and `mistral_error_bad_voice.sse`.
      A missing file is a counted skip, which blocks Task 11.
  - Elsewhere:
    - `test_base64_decode_roundtrip`: lengths 0 to 64, and the bad-byte and short-`cap`
      cases.
    - The offset map, on the chunk `"One two three four."` at 10:
      - `test_vais_offset_complete`: played 0 gives 10, half gives the start of `three`, and
        all of it gives the end.
      - `test_vais_offset_estimate`: with `complete` 0, played = 1600 × 5 gives a word start at
        or before byte 15, and the offset never passes the chunk end.
      - `test_vais_offset_utf8`: no offset falls inside a UTF-8 character.
    - `test_framework_skip_counts`: a skipping test adds 1 to the skip total and 0 to the
      failures.
- [ ] **Step 2: Run** `make -j test`. FAIL.
- [ ] **Step 3: Implement.** No Win32, no I/O, no logging. `fuzz_aisse` feeds its input through
  `sse_line_feed`, then `voice_ais_sse_line`, and frees every event.
- [ ] **Step 4: Run** `make -j test` and `make fuzz-run`. PASS, with the three capture tests
  counted as skips.
- [ ] **Step 5: Commit** `feat(voice): AI speech providers, bound keys, bodies and Mistral's event parser`.

Suggested implementer: Sonnet. Reviewer: Opus or Fable.

---

### Task 12: The WAV and PCM stream decoder

**Status:** to build. Revision 3: 24 kHz only, no resampler, a raw mode, second-header
detection, and a fuzz target.

**Files:**
- Create: `src/core/voice_wavstream.h`, `src/core/voice_wavstream.c`,
  `tests/fuzz/fuzz_wavstream.c`, `tests/fuzz/corpus/wavstream/*` (at least 5 seeds: mono16,
  stereo float, `LIST` before `data`, placeholder sizes, a truncated header)
- Modify: `Makefile` (CRLF: `wavstream` in `FUZZ_TARGETS`)
- Test: `tests/test_voice_wavstream.c`

**Interfaces:**
```c
#define VOICE_WAVS_RATE 24000
typedef enum { VWS_HEADER = 0, VWS_DATA, VWS_ERROR } VoiceWavState;
typedef enum { VWS_RAW_NONE = 0, VWS_RAW_F32_MONO } VoiceWavRaw;   /* R25: Mistral pcm */
typedef struct { /* builder's: header buffer (<= 4096), format, channels, partial-frame carry,
                    a 12-byte window for the second-header check, frames out */ } VoiceWavStream;
void   voice_wavs_init(VoiceWavStream *w, VoiceWavRaw raw);   /* VWS_RAW_NONE: expect a WAV header */
size_t voice_wavs_feed(VoiceWavStream *w, const unsigned char *in, size_t n,
                       int16_t *out, size_t cap);              /* 24 kHz mono PCM16; returns samples */
size_t voice_wavs_out_max(size_t n_in);                        /* n_in / 2 */
VoiceWavState voice_wavs_state(const VoiceWavStream *w);
int    voice_wavs_finish_ok(const VoiceWavStream *w);          /* 1: in VWS_DATA, no partial frame, frames > 0 */
uint64_t voice_wavs_frames(const VoiceWavStream *w);
const char *voice_wavs_error(const VoiceWavStream *w);         /* "", or a reason, never content */
int    voice_wavs_error_is_format(const VoiceWavStream *w);    /* 1 for "unsupported audio format" */
void   voice_wavs_wipe(VoiceWavStream *w);
```
- **The header:**
  - `RIFF`, any size, then `WAVE`. Chunks before `data` are walked by size: `fmt ` is read,
    and the rest (`LIST`, `fact`) are skipped. The `data` size is ignored (R25).
  - Accepted: tag 1 with 16 bits, or tag 3 with 32 bits; 1 or 2 channels; rate exactly
    24,000. No tag 0xFFFE unless the capture shows it (R26).
  - Anything else is `VWS_ERROR`:
    - `unsupported audio format` (`error_is_format` 1);
    - `not a WAV stream`, `WAV header too long` or `second WAV header` (each 0).
- **Data:**
  - Frames split across feeds are carried.
  - Stereo is averaged. Float is clamped to [-1, 1] and scaled by 32,767 with rounding.
  - The 12-byte pattern `RIFF????WAVE` appearing in the data is the error `second WAV header`
    (R32).
  - `VWS_RAW_F32_MONO` starts in `VWS_DATA` as float32 24 kHz mono.

- [ ] **Step 1: Write the failing tests**
  - Decoding:
    - `test_vwav_mono16`: a header plus a ramp gives the ramp exactly.
    - `test_vwav_split_everywhere`: every split point from 1 to the length, and 1-byte feeds,
      give identical output, for mono16 and for stereo float.
    - `test_vwav_placeholder_sizes`: sizes of `0xFFFFFFFF` and 0 read to the end; `LIST`
      before `data` is skipped.
    - `test_vwav_float32`: -1.5, -1, 0, 0.5 and 1 give -32767, -32767, 0, 16384 and 32767.
    - `test_vwav_stereo_average`: L 1000 and R 3000 give 2000.
    - `test_vwav_raw_f32`: raw mode decodes the same values with no header.
  - Errors:
    - `test_vwav_rejects_unsupported`: 22,050 Hz, 48,000 Hz, 8-bit, 24-bit, 3 channels and tag
      0xFFFE each give `VWS_ERROR` with `error_is_format` 1 and no output.
    - `test_vwav_not_wav`: `OggS`, `ID3` and garbage give `not a WAV stream`.
    - `test_vwav_second_header_fails`: a second complete WAV appended after 100 frames gives
      `second WAV header`.
    - `test_vwav_finish_states`: `finish_ok` is 0 in `VWS_HEADER` (a body cut in its header),
      with a partial frame, and with 0 frames; 1 after real frames.
- [ ] **Step 2: Run.** FAIL. **Step 3: Implement.** No Win32, no I/O, no logging, no heap in the
  sample path. `fuzz_wavstream` feeds its input in pieces of a size taken from its first byte.
  **Step 4: Run** `make -j test` and `make fuzz-run`. PASS.
- [ ] **Step 5: Commit** `feat(voice): a streaming WAV and PCM decoder for AI speech at 24 kHz`.

Suggested implementer: Sonnet. Reviewer: Opus or Fable.

---

### Task 13: The Ending deadline

**Status:** to build. Revision 3: the deadline follows the upload budget; neutral wording; the
turn's audio is wiped.

**Files:**
- Modify: `src/core/voice_conv.[ch]`, `src/ui/voice_conv_win.c` (it sets the budget when it
  submits a turn's upload)
- Test: `tests/test_voice_conv.c`, `tests/test_voice_conv_win.c`

**Interfaces:**
- Produces:
  - `VCN_END_STUCK`, text `Conversation ended: the last turn took too long`;
  - `unsigned long ending_max` in `VoiceConv`, set to `VOICE_CONV_ENDING_MAX_MS` on every
    entry to ENDING;
  - `void voice_conv_set_ending_budget(VoiceConv *c, unsigned long upload_ms)`, which sets
    `ending_max = max(VOICE_CONV_ENDING_MAX_MS, upload_ms + 10000)`.
- The glue calls the setter (through a new glue function, or inside `submit_batch`) after
  every batch submit, including the live fallback. `upload_ms` is twice `send_ms + recv_ms`
  from `voice_stt_upload_timeouts(wav_len)`.
- `VCE_TICK` in ENDING: at `now - state_at >= ending_max`, `go_idle(c, VCN_END_STUCK, 0)`.
  This releases the link and the upload, carries no `VCA_DROP_TURN`, and the glue's
  end-of-conversation reset wipes the turn's audio. `voice_conv_next_deadline` uses
  `ending_max`.

- [ ] **Step 1: Write the failing tests**
  - Core:
    - `test_vconv_ending_deadline_default`: a batch turn with no result ends at 180 s with
      `VCN_END_STUCK` and `VCA_CANCEL_BATCH`, no `VCA_DROP_TURN`, and the action-applied test
      ledger at zero.
    - `test_vconv_ending_deadline_budget`: a budget of 324,000 ms gives no end at 180 s and an
      end at 334 s.
    - `test_vconv_ending_deadline_live`: a live turn whose final and fallback never come ends
      the same way.
    - `test_vconv_ending_note_text`: the exact text.
  - Glue:
    - `test_vconv_win_ending_wipes_audio_keeps_words`: a turn's audio is held (a test seam
      counts the bytes kept), then the deadline passes. The kept bytes are 0, `drop_words` was
      not called, and the note was written.
  - The built `test_vconv_ending_watchdog_keeps_silence_clock` is replaced by these; the commit
    says so.
- [ ] **Step 2: Run.** FAIL. **Step 3: Implement.** **Step 4: Run** `make -j test` and
  `xvfb-run -a make wintest`. PASS.
- [ ] **Step 5: Commit** `fix(voice): a turn's Ending deadline follows its upload and says why it ended`.

Suggested implementer: Sonnet. Reviewer: Opus or Fable.

---

### Task 3: The play queue

**Status: built and reviewed** (`932bb02`). Unchanged by the provider revisions of 2026-11-25; R31 (Task 7a) adds position accumulation across blocks.

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

**Status: built and reviewed** (`13db061`). Unchanged by the provider revisions of 2026-11-25; Task 13 replaces `test_vconv_ending_watchdog_keeps_silence_clock` with the deadline rule (R29).

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

**Status:** to build. Revision 3: the loopback server serves 24 kHz audio only; the cancel test
skips visibly (R42).

**Files:**
- Modify: `src/core/ai_http.h` (CRLF), `src/ui/ai_http_win.c` (CRLF; the non-Windows stub too),
  `src/ui/voice_net_win.h` (`VNK_TTS` is now used), `build-support/delayload/winhttp.def` (the
  comment names AI speech; no new entry)
- Create: `tests/http_loopback_win.h`, `tests/http_loopback_win.c` (a helper in
  `WIN_TEST_SRCS`)
- Test: `tests/test_voice_net_win.c`

**Interfaces:**
```c
int ai_http_post_stream_ex(void *session, const char *url, const char * const *headers,
                           const char *body, size_t body_len, int recv_ms,
                           AiStreamCallback cb, void *userdata,
                           int *status_out, char *error, size_t error_size, AiNetCancel *nc);
/* the caller's session (never opened or closed here); connect 10 s, send 30 s, receive recv_ms;
 * never follows a redirect (WINHTTP_OPTION_REDIRECT_POLICY = NEVER); NULL session: -1 "No session" */
```
- `http_loopback_win.h`:
  - modes:
    - `HLS_WAV`: chunked, a WAV with `0xFFFFFFFF` sizes and a `LIST` before `data`, sent in
      writes of 1, 7, 4,095 and the rest;
    - `HLS_WAV_BADRATE`: the same at 22,050 Hz;
    - `HLS_WAV_TWICE`: two WAVs back to back;
    - `HLS_EMPTY`: 200 with an empty body;
    - `HLS_SSE`: the provisional Mistral fixture, or any file named by `HlAudio.sse_file`;
    - `HLS_SSE_ERROR`;
    - `HLS_SSE_DONE_ONLY`;
    - `HLS_401`, `HLS_404`, `HLS_422` and `HLS_429`;
    - `HLS_HANG`;
    - `HLS_302`.
  - `hl_start(mode, const HlAudio *a)` with `HlAudio { int channels, float32; double seconds;
    const char *sse_file; }`, plus `hl_port`, `hl_stop`, `hl_request_head`, `hl_request_body`
    and `hl_requests`.
- Refactor `ai_http_post_stream_impl` to take the session; `ai_http_post_stream` keeps its own.

- [ ] **Step 1: Write the failing wintests**
  - `test_ai_stream_ex_wav_bytes`: the body arrives whole and in order across callbacks, with
    status 200; `voice_net_open_count()` went up by exactly 1.
  - `test_ai_stream_ex_sse`: the SSE file arrives byte for byte.
  - `test_ai_stream_ex_statuses`: 401, 404, 422 and 429 each give their status, rc -1 and the
    body in the error.
  - `test_ai_stream_ex_no_redirect`: 302 and exactly 1 request.
  - `test_ai_stream_ex_cancel_hang`: `ai_nc_fire` returns within 2 s. Under Wine, if it does
    not, `TEST_SKIP("Wine does not abort a synchronous WinHTTP call")`; on Windows it must
    pass.
  - `test_ai_stream_ex_null_session`.
- [ ] **Step 2: Run** `xvfb-run -a make wintest`. FAIL. **Step 3: Implement.**
  **Step 4: Run** `xvfb-run -a make wintest`, `make -j test` and `make -j release`. PASS.
- [ ] **Step 5: Commit** `feat(voice): streaming POST on a voice session, never redirected`.

Suggested implementer: Sonnet. Reviewer: Opus or Fable.

---

### Task 6: The render client on the audio thread

**Status: built, through its fix round** (HEAD `31f9a20`). Unchanged by the provider revisions of 2026-11-25: every stream reaches it as 24 kHz mono PCM16 (R26), so no follow-up task is needed.

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

### Task 7a: The AI speech engine: contexts, render, ledger, the done guard and the dead flag

**Status:** to build (Task 7 split, R41). It runs on a fake fetch; Task 7b adds the real one.

**Files:**
- Create: `src/ui/voice_aispeech_win.h`, `src/ui/voice_aispeech_win.c`,
  `tests/test_voice_aispeech_win.c`
- Modify:
  - `src/core/voice_playq.c` and `tests/test_voice_playq.c` (R31 only);
  - `Makefile` (CRLF: `WIN_ONLY_TESTS`, `WIN_TEST_SRCS`);
  - `tests/win_runner.c`.

**Interfaces:**
- Consumes: Task 2 (`VoiceAisTarget`, `voice_ais_offset`, `VOICE_AIS_HR_*`), Task 6's render
  API as built, `voice_speak_chunks`, and `VoiceTtsMsg`/`WM_VOICE_TTS`.
- Produces:
```c
#define WM_VOICE_AIS (WM_APP + 0x49)      /* lParam = VoiceAisMsg*, receiver frees */
typedef enum { VAI_CHUNK_DONE = 1, VAI_FAILED, VAI_EXITED } VoiceAisKind;
typedef struct { unsigned token, reply_id, chunk; VoiceAisKind kind;
                 uint64_t frames; long hr; } VoiceAisMsg;
#define VOICE_AIS_SAMPLE_TOKEN 0x7FFFFFF2u
#define VOICE_AIS_TIMER_BASE   0x56F4      /* + slot 0 or 1; check that both ids are free */
/* One chunk's fetch, run on the worker thread: push decoded 24 kHz mono PCM16 through push()
 * (0 = stop: dead or cancelled), return 0 ok or a VOICE_AIS_HR_* / HTTP status. */
typedef int  (*VoiceAisPush)(void *job, const int16_t *s, size_t n);
typedef long (*VoiceAisFetch)(void *job, const VoiceAisTarget *t, const char *key,
                              const char *text, size_t len, VoiceAisPush push);
int  voice_ais_start(HWND notify, unsigned token);   /* the one constructor; 0 unless voice_gate_on() and audio up */
void voice_ais_speak(unsigned token, unsigned reply_id, const char *utf8, size_t from,
                     const VoiceAisTarget *t, const char *key);
void voice_ais_stop(unsigned token);
void voice_ais_volume(unsigned token, int percent);
void voice_ais_quit(unsigned token, unsigned wait_ms);
void voice_ais_quit_all(unsigned wait_ms);
int  voice_ais_sample(HWND notify, const VoiceAisTarget *t, const char *key);
int  voice_ais_running(void);
int  voice_ais_owns(unsigned token);
void voice_ais_on_audio(UINT kind, LPARAM lp);
void voice_ais_on_msg(VoiceAisMsg *m);
void voice_ais_on_timer(UINT_PTR id);
void voice_ais_set_fetch(VoiceAisFetch f);   /* 7b installs the real one; tests install fakes; NULL = none (every speak fails NET) */
```
- **Contexts.** At most two, the conversation's and the sample's.
- **Render (R35).**
  - Every `speak` first calls `render_flush(token, reply_id)` and fires the old worker's
    cancel.
  - If the context does not hold the render client, `speak` pushes `render_open`, acquires
    `VRES_RENDER`, and sends the gain if it is not 100.
  - `VAE_RENDER_PREEMPTED` or a render error clears the holding flag and releases
    `VRES_RENDER`. While playing, it is also a passing `VTT_ERROR(VOICE_AIS_HR_DEVICE)` at the
    current offset. While not playing, nothing is posted.
- **The worker:**
  - one thread per speak (`VRES_SPEECH_REQ`);
  - chunks from `voice_speak_chunks`;
  - before each chunk's fetch it waits below 20 s queued (50 ms slices against cancel and the
    dead flag);
  - `push` takes the engine's lock, checks the dead flag (R36), counts the frames pushed for
    the reply and chunk, and calls `render_data`;
  - a fetch returning 0 with 0 frames for the chunk becomes `VOICE_AIS_HR_STREAM` (R32);
  - on failure it pushes `render_end` and posts `VAI_FAILED(k, hr)`;
  - at the end it pushes `render_end` and posts `VAI_EXITED`;
  - it wipes its copies.
- **The UI side:**
  - `VAM_PLAYED` gives `VTT_WORD(offset)` (R9), and re-arms the done guard while the worker
    has finished (R30).
  - `VAM_PLAY_DONE` for the current id gives `VTT_DONE` when every chunk completed, else
    `VTT_ERROR(hr)` at the mapped offset. An older id is ignored.
  - The done guard posts the same result after 2,000 ms with no `VAM_PLAYED` progress.
  - An empty `t->url` gives `VTT_ERROR(VOICE_AIS_HR_RESOLVE)` at `from` at once.
  - `VAI_EXITED` joins the thread and releases `VRES_SPEECH_REQ`.
  - The sample quits itself at its end.
- **Quit (R36):** set the dead flag under the lock and fire cancel; join within `wait_ms`;
  on timeout, detach with the ledger count kept until `VAI_EXITED`. Then `render_close` and
  release `VRES_RENDER`. `voice_ais_quit_all` does this for every context, and the callers in
  Task 8a call it before `voice_audio_stop`.
- **R31 in `voice_playq.c`:** `frames_in_chunk` accumulates across consecutive blocks with the
  same reply and chunk.

- [ ] **Step 1: Write the failing tests** (a fake fetch that pushes a set number of frames per
  chunk, optionally slowly or failing; the null render sink; a message pump; the ledger reset
  per test):
  - Native: `test_vplayq_pos_accumulates_across_blocks`.
  - Wintests:
    - `test_voice_ais_gate_off`: with the gate off, `start` gives 0, no thread starts, and the
      ledger is 0.
    - `test_voice_ais_fake_done`: two chunks give `VTT_WORD` offsets that do not decrease, at
      least one of them at or past the offset of `Two` in `One. Two.` (R42), and one
      `VTT_DONE`. The ledger is zero after `quit`.
    - `test_voice_ais_empty_chunk_fails`: a chunk with 0 frames gives
      `VTT_ERROR(VOICE_AIS_HR_STREAM)`.
    - `test_voice_ais_stop_then_speak_no_old_audio`: speak reply 1 (3 s of fake audio), stop
      after its first `VAM_PLAYED`, then speak reply 2. `voice_audio_test_render_frames(1)`
      does not grow after the stop.
    - `test_voice_ais_gain_survives_open`: volume 18 before the first speak. The null sink's
      peak is 18 % ± 1 of the input's.
    - `test_voice_ais_reopen_after_preempt`: open B pre-empts A while A is silent. Nothing is
      posted for A and `VRES_RENDER` is released. A's next speak reopens, re-sends the gain and
      plays.
    - `test_voice_ais_preempt_while_playing`: gives `VTT_ERROR(VOICE_AIS_HR_DEVICE)`.
    - `test_voice_ais_done_guard_waits_for_progress`: `render_end` is dropped
      (`voice_audio_test_fail_push`). With audio still playing, no `VTT_DONE` comes before it
      ends. `VTT_DONE` comes 2 s after the last progress.
    - `test_voice_ais_dead_worker_never_pushes`: a fetch that blocks, then `quit(token, 50)`
      detaches it. Once released, the fetch's further pushes return 0;
      `voice_audio_test_render_frames(reply)` does not grow; `VRES_SPEECH_REQ` stays 1 until
      `VAI_EXITED`, then 0.
    - `test_voice_ais_resolve_error_at_once`: an empty `t->url` gives
      `VTT_ERROR(VOICE_AIS_HR_RESOLVE)` at `from`, with no thread and no render opened.
    - `test_voice_ais_sample_self_quits`: a sample's DONE leaves `voice_ais_running()` at 0
      and the ledger at 0.
- [ ] **Step 2: Run.** FAIL. **Step 3: Implement.** **Step 4: Run**
  `xvfb-run -a make wintest`, `make -j test` and `make -j release`. PASS.
- [ ] **Step 5: Commit** `feat(voice): AI speech contexts, render handling and the done guard`.

Suggested implementer: Sonnet. Reviewer: Opus.

---

### Task 7b: The AI speech engine: the network fetch for Mistral and Custom

**Status:** to build (Task 7 split, R41). Its Mistral half is finalised against Task 0's
capture (R33).

**Files:**
- Modify: `src/ui/voice_aispeech_win.c` (the real fetch, installed at `voice_ais_start` when
  none is set), `tests/test_voice_aispeech_win.c`

**Interfaces:**
- Consumes: Task 2 (`voice_ais_body`, `voice_ais_sse_*`, `base64_decode`), Task 12
  (`VoiceWavStream`), Task 5 (`ai_http_post_stream_ex`, `http_loopback`), `sse_line`, and
  `voice_net_open(VNK_TTS)`.
- **The real fetch.** For one chunk:
  - `voice_net_open(VNK_TTS)` and the body;
  - headers: `Authorization: Bearer <key>` only when the key is not empty, and
    `Accept: text/event-stream` for Mistral;
  - `ai_http_post_stream_ex` with `recv_ms` 60,000.
  - By shape:
    - Custom (OPENAI): the bytes go to `voice_wavs_feed(VWS_RAW_NONE)`.
    - Mistral: `sse_line_feed`, then `voice_ais_sse_line`, then AUDIO bytes into the decoder
      (R25 chooses the raw mode only if Task 0 shows per-delta headers). DONE ends the chunk;
      ERROR fails it.
  - Decoded samples go through `push`.
  - At the end of the chunk, the failures of R32 are checked, each a distinct test below:
    - `voice_wavs_finish_ok` must hold;
    - `sse_line_skipped(...)` must be 0;
    - a Mistral body that ended with no DONE is a failure.
  - Results:
    - a decoder format error gives `VOICE_AIS_HR_FORMAT`;
    - other stream failures give `VOICE_AIS_HR_STREAM`;
    - a non-200 status gives that status;
    - a transport failure gives `VOICE_AIS_HR_NET`.
  - The session is closed, and the key, body and buffers are wiped.

- [ ] **Step 1: Write the failing wintests** against `http_loopback`:
  - `test_voice_ais_custom_wav_done`: `HLS_WAV` mono 16-bit 0.25 s and `One. Two.`:
    - the head carries the key, and the body has `"input":"One. Two."` and
      `"response_format":"wav"`;
    - the open count went up by 1;
    - 6,000 ± 1 frames were written;
    - one `VTT_DONE` arrived.
  - `test_voice_ais_custom_no_key`: an empty key sends no `Authorization` header.
  - `test_voice_ais_mistral_sse`: the provisional fixture gives one `VTT_DONE`, with frames
    matching its duration, and a body with `voice_id` and `"stream":true`.
  - `test_voice_ais_mistral_capture`: the same against `mistral_stream_wav.sse` (or `_pcm`,
    as R25 chose). It is a `TEST_SKIP` until Task 0's file exists.
  - `test_voice_ais_sse_error_event` and `test_voice_ais_sse_done_only`: each gives
    `VTT_ERROR`.
  - `test_voice_ais_badrate_is_format`: `HLS_WAV_BADRATE` gives
    `VTT_ERROR(VOICE_AIS_HR_FORMAT)`.
  - `test_voice_ais_second_header_fails`: `HLS_WAV_TWICE` gives `VOICE_AIS_HR_STREAM`.
  - `test_voice_ais_empty_body_fails`: `HLS_EMPTY` gives `VOICE_AIS_HR_STREAM`.
  - `test_voice_ais_oversized_line_fails`: an SSE line over `SSE_LINE_MAX` gives
    `VOICE_AIS_HR_STREAM`.
  - `test_voice_ais_http_401` and `test_voice_ais_http_429`: each gives `VTT_ERROR` with that
    status, and no `VTT_DONE`.
  - `test_voice_ais_quit_while_server_hangs`: `HLS_HANG`; the quit returns within 4 s and the
    ledger is zero once the thread has ended.
- [ ] **Step 2: Run.** FAIL. **Step 3: Implement.** **Step 4: Run**
  `xvfb-run -a make wintest`, `make -j test`, `make -j release` and `make fuzz-run`. PASS, with
  only the capture tests skipped.
- [ ] **Step 5: Commit** `feat(voice): AI speech requests to Mistral and OpenAI-compatible servers`.

Suggested implementer: Sonnet. Reviewer: Opus.

---

### Task 8b: The conversation glue: the PR 2 follow-ups

**Status:** to build (Task 8 split, R41). It is independent of AI speech, so it comes before 8a.

**Files:**
- Modify: `src/ui/voice_conv_win.[ch]`, `src/ui/voice_win.[ch]` (`voice_on_external_send`),
  `src/ui/ai_chat.c` (the Retry handler's two exits, and any other user launch the grep finds,
  R23)
- Test: `tests/test_voice_conv_win.c`

**Interfaces:**
- `void voice_conv_external_send(void)`. It takes the reply baseline (`g_since =
  msg_count()`, `g_sent_tick`, `g_busy_seen = !chat_idle()`) in every non-idle state except
  ENDING-while-sending, then feeds `VCE_EXTERNAL_SEND`.
- TEST-ONLY seams `voice_conv_test_queue(VoiceConvEvent, int, int)` and
  `voice_conv_test_pump(void)`.
- Rules:
  - The `VCA_DROP_TURN` that comes with an external send discards the turn's audio and link,
    never its words (a flag around that one event).
  - `stop_speech()` sets the volume back to 100 when it stops a ducked context.
  - `speak_reply` zeroes `g_reply_at` and `g_spoken_end_at` before speaking a new reply.
  - Every `VTT_WORD` for the current id feeds `VCE_SPEECH_PROGRESS`, at most once a second.
  - `g_notice_raised` is set when `notice_voice` is called.
  - A `VCE_END` with the queue full goes to its own slot (the first end wins), run after the
    queue; other overflow is dropped with a `DLOG` warning.

- [ ] **Step 1: Write the failing wintests**:
  - External send:
    - `test_vconv_win_external_send`: in Listening, `cap_end` and WORKING; a reply after the
      new baseline is spoken.
    - `test_vconv_win_external_send_keeps_words`: while Hearing with live words in the box, no
      `drop_words` call, and the link is closed.
    - `test_vconv_win_external_send_unducks`: Speaking, `VCE_DUCK` (volume 18), then an
      external send gives `tts_volume(token, 100)`.
  - The reply's times:
    - `test_vconv_win_new_reply_resets_times`: a reply ended, a new one loading 6 minutes later,
      and "carry on" during it is classified against the new reply's times only.
  - Progress:
    - `test_vconv_win_progress_keeps_long_reply`: `VTT_WORD` every 30 s with
      `voice_conv_test_advance` for 11 minutes keeps Speaking.
  - The notice:
    - `test_vconv_win_notice_kept_if_ended_first`: the notice is posted, the conversation ends
      before `voice_conv_on_notice`, and the next conversation's first reply raises it.
  - The pending end:
    - `test_vconv_win_end_never_dropped`: 30 `voice_conv_test_queue(VCE_TICK, ...)` calls, then
      `VCE_END(VCX_OFF)`, then `voice_conv_test_pump()`: IDLE, with the ledger zero.
- [ ] **Step 2: Run.** FAIL. **Step 3: Implement.** List the grep's launch sites in the commit
  body. **Step 4: Run** `make -j test`, `xvfb-run -a make wintest` and `make -j release`.
  PASS.
- [ ] **Step 5: Commit** `fix(voice): Retry, long replies, the end event and the notice in conversations`.

Suggested implementer: Sonnet. Reviewer: Opus or Fable.

---

### Task 8a: The conversation glue: the speech router, engine choice, fallback and the once-per-conversation note

**Status:** to build (Task 8 split, R41).

**Files:**
- Modify:
  - `src/ui/voice_conv_win.[ch]` (R20's ops, the engine, the fallback, R37);
  - `src/ui/voice_win.[ch]`: the router; `rc_notice_due` returns 0 with AI speech;
    `voice_ais_quit_all(3000)` before `voice_audio_stop` in `voice_feature_apply` and
    `voice_feature_shutdown` (R36); the render messages routed to `voice_ais_on_audio` before
    `settings_voice_on_audio`; `voice_on_ais(LPARAM)` and `voice_on_ais_timer(UINT_PTR)`;
  - `src/ui/window.c` (route `WM_VOICE_AIS` and the `VOICE_AIS_TIMER_BASE` timers).
- Test: `tests/test_voice_conv_win.c` (the fakes gain R20)

**Interfaces:**
- `VoiceConvOps` changes:
  - `tts_start(unsigned token, int engine)`;
  - optional `int (*speech_engine)(void)`;
  - optional `int (*ai_resolve_error)(char *out, size_t cap)`. The real one runs
    `voice_ais_resolve` on the current settings.
- Rules:
  - `g_engine_cfg` is read at conversation start. `g_ai_off` (R37) is cleared at the start.
  - Every `begin_speak` uses AI unless `g_engine_cfg` is Windows or `g_ai_off` is set.
  - On `VTT_ERROR` for the current id from an AI context:
    - `voice_ais_failure_fatal(hr)`:
      - write the note once (`g_ai_off = 1`), with the reason from
        `voice_ais_failure_reason(hr, resolve_err)`, where `resolve_err` comes from the op for
        `VOICE_AIS_HR_RESOLVE`;
      - no header.
    - Else: the header `AI speech failed: using Windows speech`.
    - Either way:
      - quit the AI context;
      - start a Windows context;
      - `begin_speak` a copy of `g_spoken` from `m->offset`, with the same kind.
  - A Windows `VTT_ERROR` follows today's path.
- The real router keeps the engine per token.
  - `rc_tts_speak` resolves; a failure clears `t.url`.
  - It calls `voice_ais_speak(..., voice_ais_key(s, &t))`.
  - `rc_speech_engine` gives `s->voice_speech_engine` in Conversation mode.

- [ ] **Step 1: Write the failing wintests**:
  - The engine and passing failures:
    - `test_vconv_win_ai_engine_started`: the engine AI gives `tts_start(token,
      VOICE_SPEECH_AI)`.
    - `test_vconv_win_ai_fallback_rest_of_reply`: `VTT_ERROR(NET, offset 40)` gives the
      header, `tts_quit(ai)`, `tts_start(win, VOICE_SPEECH_WINDOWS)` and
      `tts_speak(win, ..., text, 40)`. The next reply returns to AI from 0.
    - `test_vconv_win_ai_and_windows_fail`: the AI error, then a failing Windows start, gives
      `Could not speak the reply: it is on screen`.
    - `test_vconv_win_ai_stale_error_ignored`: an older id's error changes nothing.
    - `test_vconv_win_no_notice_with_ai`: with `notice_due` true and AI speech, no notice is
      raised.
    - `test_vconv_win_ai_passing_retries`: three replies failing with `VOICE_AIS_HR_NET`, 429
      and 503 each fall back with the header. Every next reply starts AI again; there is no
      chat note; the ledger is zero at the end.
  - Failures that cannot work:
    - `test_vconv_win_ai_fatal_note_once`: a 401 on the first reply gives the note `AI speech is
      off for this conversation: the speech provider refused the key (HTTP 401). Replies are
      read with Windows speech.` exactly once. The second and third replies start Windows
      contexts only (no `tts_start(..., AI)`). A new conversation tries AI again.
    - `test_vconv_win_ai_fatal_resolve`: `VOICE_AIS_HR_RESOLVE`, with the fake op returning
      `Enter a speech API key`, gives that reason in the note.
    - `test_vconv_win_ai_fatal_format`: `VOICE_AIS_HR_FORMAT` gives `unsupported audio format`.
  - Teardown:
    - `test_vconv_win_ai_off_quits_before_audio`: voice off with an AI context alive calls the
      fake `tts_quit` before the audio stop (an order the fake records).
- [ ] **Step 2: Run.** FAIL. **Step 3: Implement.** **Step 4: Run** `make -j test`,
  `xvfb-run -a make wintest` and `make -j release` (the exe links).
- [ ] **Step 5: Commit** `feat(voice): AI speech in conversations, with the Windows speech fallback`.

Suggested implementer: Sonnet. Reviewer: Opus (the integration task).

---

### Task 9: Settings > Voice: Speech, the AI rows, the privacy line, Play sample with AI speech

**Status:** to build. Revision 3: Mistral and Custom; the privacy line; key rebinding; unknown
providers.

**Files:**
- Modify: `src/ui/settings.c`, `src/ui/resource.h`, `src/core/voice_config.[ch]`
- Test: `tests/test_voice_config.c`

**Interfaces:**
```c
typedef struct { int speech, ai_rows, url, key, style, uk_line, privacy; } VoiceSpeechEnables;
void voice_speech_enables(int conversation_enabled, int engine, const char *provider,
                          int key_shared, int uk_voice_installed, VoiceSpeechEnables *out);
```
- `privacy` is on whenever the AI rows are; `style` only for Custom.
- **Rows (R16):**
  - Speech;
  - Speech provider (Mistral / Custom; an unknown id shows no selection, R38);
  - Base URL with the clear-text warning;
  - Speech API key;
  - AI voice;
  - AI speech model;
  - Accent and style (multi-line, 1,023 bytes);
  - the dim `Each spoken reply's text is sent to <host>.`, with `<host>` from a resolve of the
    page's values (`api.mistral.ai`, or the Custom host; `the speech server` when it does not
    resolve).
- **Seeding.** A provider change reseeds the voice and model combos, the provider's default
  first and stored as `""`. Custom's combos open empty unless a value is saved.
- **Reading the page.** `voice_page_read(d, Settings *s)` is factored out of Save. Save sets
  `tts_api_key_for = voice_ais_binding(s)` only when the key edit was changed this session
  (R43), and drops a stale preserved blob for `tts_api_key`. Play sample and the shared-key
  check read a wiped copy (R17).
- **Play sample.** AI speech: resolve, then `voice_ais_sample`. A resolve error shows in the
  dim test-note line. Windows speech: today's call.

- [ ] **Step 1: Write the failing test** `test_voice_speech_enables`:
  - Dictation: everything off.
  - Windows: `uk_line` as installed, and no `privacy`.
  - AI with Mistral, shared: no key, no url, no style, and `privacy` on.
  - AI with Custom: url, key and style on.
  - An unknown provider: the AI rows stay on, and url is off.
- [ ] **Step 2: Run.** FAIL. **Step 3: Implement.** **Step 4: Run** `make -j test`,
  `xvfb-run -a make wintest` and `make -j release`. Optionally check
  `--ui-demo=settings-voice`.
- [ ] **Step 5: Commit** `feat(voice): Settings > Voice gains Speech and the AI speech rows`.

Suggested implementer: Sonnet. Reviewer: Opus or Fable.

---

### Task 10: The self-report: AI speech mode and the resource count

**Status:** to build (unchanged but for the config).

**Files:**
- Modify: `src/ui/dll_hook_win.c` (`voice-resources: N`), `tests/selftest/check.sh`,
  `Makefile` (CRLF: the `ai` mode), `tests/test_selftest_configs.c`
- Create: `tests/selftest/nutshell-voice-ai.config`: the conv config plus
  `"voice_speech_engine": "ai"`, `"voice_tts_provider": "custom"`,
  `"voice_tts_url": "http://127.0.0.1:9/v1"`, `"voice_ai_tts_model": "kokoro"` and
  `"voice_ai_voice": "bf_emma"`.

**Interfaces:**
- `voice-resources: N` is the sum of the ledger's counts.
- `check.sh`:
  - every mode except `sapi` asserts 0, and `sapi` only that the line exists;
  - `ai` asserts winhttp, riched20, msftedit and mmdevapi were requested, mmdevapi is mapped,
    `no_speech ai`, `voice-net-opens: 0` and `voice-resources: 0`.

- [ ] **Step 1:** extend `tests/test_selftest_configs.c` for the new config, and extend
  `check.sh` (the usage line lists `off|voiceoff|on|conv|sapi|ai`).
- [ ] **Step 2: Run** `make -j test`, `make -j release` and `make selftest-wine`: `ai` is
  missing or fails.
- [ ] **Step 3: Implement.** **Step 4: Run** all three. All six modes pass.
- [ ] **Step 5: Commit** `test(selftest): AI speech mode opens nothing until a reply is spoken`.

Suggested implementer: Haiku. Reviewer: Opus or Fable.

---

### Task 11 (orchestrator): the capture gate, spec findings, notes, version, build, verification

**Status:** last. Revision 3: the capture gate (R33).

- [ ] **The capture gate (R33).** The PR is not marked ready until all of these hold:
  - Task 0's six files are on the branch.
  - Every `provisional_*.sse` fixture is either replaced by its capture or kept beside it with
    a test proving the two parse alike.
  - R25's `wav` or `pcm` choice is made from the capture.
  - The default voice (R2) is confirmed or changed.
  - `make -j test` and `xvfb-run -a make wintest` report **no capture-fixture skips**.

  Before Task 0 lands, steps below may run, but this box stays open.
- [ ] **Spec §16:** add "Found at build time (PR 3, 2026-11-2x)". It holds:
  - what the capture showed (the envelope, headers, delta sizes, the error's form);
  - item 14's counts (69, 96, 24, 17);
  - the `IAudioRenderClient` IID source;
  - every ruling as built, with each implementer departure;
  - the test counts.
- [ ] **Notes Open list:** "AI speech (PR 3 of live voice): nothing run on Windows", one line
  each:
  - (a) Mistral `voice_1787` and the other English (UK) voices: accent, time to the first
    word, no gap or click between chunks, the duck, stop latency.
  - (b) Kokoro-FastAPI with `bf_emma`, and with no key.
  - (c) Shared, separate and rebound keys; the DPAPI blob and its binding.
  - (d) The passing fallback: Wi-Fi pulled mid-reply.
  - (e) The once-per-conversation note: a wrong key, a wrong model, a 22,050 Hz server.
  - (f) "Carry on" after a cut-in (R9).
  - (g) Headphones unplugged, and a default-device change.
  - (h) Echo cancellation with our render client, and whether our render session is ducked.
  - (i) The cost on Mistral's usage page.
  - (j) Play sample with unsaved values.
  - (k) The page at 100 %, 125 % and 200 %, with the privacy line and the reseeding.
  - (l) `mingw32-make test` and `mingw32-make clean && mingw32-make release` on the Windows
    box.
  - (m) Retry during a conversation.
  - (n) A hung upload ending the conversation at its deadline.

  Mark the folded parts of (o), (q), (r) and (s) as fixed in code, with their hand checks
  pending.
- [ ] **Notes todo:**
  - OpenAI's batch speech-to-text models `gpt-4o-mini-transcribe` (the default,
    `src/core/voice_stt.c:14`), `gpt-4o-transcribe`, `whisper-1` and `-diarize` shut down on
    2027-02-26, replaced by `gpt-transcribe` or `gpt-live-transcribe`.
  - `-2025-03-20` shuts down on 2027-01-20.
  - Source: `developers.openai.com/api/docs/deprecations.md`, 2026-11-25.
  - Change them in their own pull request.
  - Also later: Groq Orpheus once it leaves Preview; resampling for Custom servers.
- [ ] **Version** `1.3.0` (`1,3,0,0`) in `resource.h` and README.
- [ ] **Build and verify**, all from the worktree, in this order: `make clean && make release`;
  `make -j test`; `make test SAN=1`; `make lint-gate`; `make fuzz-run` (including `wavstream`
  and `aisse`); `xvfb-run -a make wintest`; `make selftest-wine` (all six modes);
  `.github/scripts/check-version.sh`.
  - List every `[SKIP]` with its reason.
  - Run `pgrep -a wine` before `wintest`.
  - If the release fails after `clean`, run `git restore build/win/nutshell.exe`.
- [ ] **Commit** the exe and docs. Stage new files by path, read every `??` line, and write the
  `Models:` line. Do not push.
- [ ] **Whole-branch reviews:** independent Opus and Fable reviews against `58dbd00`. Their
  findings go to the maintainer first.

Suggested owner: Opus (orchestrator).
