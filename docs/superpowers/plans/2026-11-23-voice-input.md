# Voice input (push-to-talk speech-to-text) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Hold Right Ctrl (or F8/F9) with focus in the AI panel, speak, release: the
words are transcribed by a speech-to-text (STT) provider and sent through the chat's
normal send path, with every approval unchanged. Voice is an optional feature: off by
default, nothing of it loaded while off, unavailable while the AI assistant is off.

**Architecture:** Every decision that can be pure lives in `src/core` with native tests:
the settings fields and first-load rule, the voice gate, WAV/PCM/level/silence code, the
STT provider table, URL resolution, request bodies and response parsing, the
push-to-talk state machine, the message classifier and transcript delivery. `src/ui`
holds three thin Win32 modules: one MTA audio thread that owns every WASAPI object
(`voice_audio_win.c`), an STT upload worker (`voice_stt_win.c`) and the feature
controller (`voice_win.c`) that starts and tears the feature down and feeds key, mouse,
activation and modal events into the state machine from `pump_dispatch`.

**Tech Stack:** C11, MinGW-w64 cross-compile (gcc 13), WASAPI via COM in C
(`COBJMACROS`, `mmdeviceapi.h`, `audioclient.h`), WinHTTP (delay-loaded), native gcc
test runner, Wine `wintest`, `make selftest-wine`.

**Spec:** `docs/superpowers/specs/2026-11-23-voice-input-output-design.md` (§§1-4, §6
STT only, §7 without TTS, §8, §9 item 1, §10 input parts, §11). Also CLAUDE.md
"Optional features: off unless enabled, not loaded while off" and
`docs/superpowers/specs/2026-11-23-ai-assistant-enable-switch-design.md` (the AI gate,
notices, self-report). Spoken replies (TTS, §5) are NOT in this plan: no TTS field,
control, string or code.

## Global Constraints

- Enable voice is **off by default**; a config lacking `voice_enabled` gets it off and
  the upgrade notice names it "Voice: off until you choose to turn it on".
- While voice is off, or the AI assistant is off: no `CoCreateInstance` of the device
  enumerator, no audio thread, no push-to-talk hook, no mic button, no STT request; Right
  Ctrl, F8 and F9 behave exactly as today. The Voice page's controls are disabled with
  no device list. With AI off the Enable voice box is disabled with "Needs the AI
  assistant" (`opt_feature_blocked_reason`).
- Devices are enumerated only after Enable voice has been **saved** on; until then the
  device combo shows "Save to list microphones".
- Capture: WASAPI shared mode, default device = `eCapture`/`eConsole`,
  `AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY` (plus
  `AUDCLNT_STREAMFLAGS_EVENTCALLBACK`), 16 000 Hz, 1 channel, 16-bit PCM, wrapped as an
  in-memory WAV. **One audio thread** (`COINIT_MULTITHREADED`) owns every audio COM
  object, enumeration included. The UI thread never touches one.
- A stored endpoint ID that cannot be opened or is not `DEVICE_STATE_ACTIVE` falls back
  to Windows default **without overwriting** the stored ID.
- Recording cap 60 s; Hold-mode taps under 300 ms discarded; Mic start delay default
  150 ms, range 0-400; auto-repeat key-downs ignored; any other key, mouse button or wheel
  while the key is held discards; Escape while recording discards; losing activation or a
  modal dialog opening discards and closes the device.
- No new static DLL import. WASAPI comes in through `CoCreateInstance` (`-lole32`, GUIDs
  defined in our source or from static `libuuid`); never link `avrt`, `ksuser` or
  `mmdevapi`. New WinHTTP entry points, if any, go in `build-support/delayload/winhttp.def`.
- Audio buffers are `secure_zero`ed and freed after the upload, never written to disk.
  Transcripts are logged only through `DLOG_CONTENT`; the STT key never.
- The transcript goes into `hInput` and through `ai_chat_send_user_message`, exactly as
  the suggestion chips do. Voice never builds a conversation turn itself.
- Design-system rules (CLAUDE.md): colours from `ns_tokens()`, sizes via `ns_scale` and
  `ns_type.h`, fonts via `ns_font`, the pulse via the panel's one `ns_motion` timer,
  hover via `ns_hover`. No `RGB(` literal, no `CreateFont`, no `MulDiv(x, dpi, 96)`.
- Line endings: CRLF files stay CRLF (`Makefile`, `src/config/loader.c`,
  `src/core/ai_http.[ch]`, `src/core/ai_prompt.c`, `src/ui/ai_http_win.c`,
  `tests/test_ai_http.c`, `tests/test_settings.c` among those touched). New files are LF.
  After each task compare `git diff --stat` with `git diff --stat --ignore-cr-at-eol`.
- Both builds `-Werror`; MinGW adds `-Wpedantic -Wshadow -Wformat=2 -Wconversion`.
  New `tests/*_win.c` files go into `WIN_ONLY_TESTS`.
- Every test function is declared and called in `tests/runner.c` (native) or
  `tests/win_runner.c` (wintest). Tests that write files use `TEST_TMP_DIR`.

## Decisions this plan takes where the spec leaves a choice

1. **Config keys** (in this file order, after `ai_policy_default`'s neighbours as Task 1
   says): `voice_enabled` (bool), `voice_stt_use_ai` (bool, default true),
   `voice_stt_provider` (`"openai"` default, `"groq"`, `"mistral"`, `"gemini"`,
   `"custom"`), `voice_stt_url` (base URL, Custom only), `voice_stt_model` (empty = the
   provider's default model), `voice_stt_language` (empty = auto-detect), `stt_api_key`
   (DPAPI, with `Settings.stt_api_key_enc_preserved`), `voice_input_device` (endpoint
   ID, empty = Windows default), `voice_input_device_name`, `voice_ptt_key`
   (`"right_ctrl"` default, `"f8"`, `"f9"`), `voice_ptt_mode` (`"hold"` default,
   `"toggle"`), `voice_mic_delay_ms` (0-400, default 150), `voice_review` (bool, default
   false). 13 keys.
2. **Upgrade notice:** when `voice_enabled` is missing, the other voice keys that are
   also missing are folded into its one "Voice" line (they are unused while voice is
   off). A voice key missing while `voice_enabled` is present is listed normally.
3. **Toggle mode** starts recording on a *clean release* of the key (pressed and released
   with no other input between) and stops on the next clean release. The Mic start delay
   applies to Hold mode only (in Toggle mode the device opens after the release, so a
   chord never opens it either).
4. **Busy** is checked at the moment recording would start (Hold: when the delay has
   elapsed with the key held alone; Toggle: at the clean release), so Right Ctrl chords
   while the assistant is busy never flash "Assistant is busy".
5. **Silence gate:** 20 ms windows (320 samples); speech when any window's RMS is
   at least 250 (about -42 dBFS). Every sample exactly 0 means the privacy block.
6. **STT request:** one attempt, no retry; WinHTTP timeouts resolve 10 s, connect 10 s,
   send 30 s, receive 60 s. Cancel through an `AiNetCancel` token.
7. **Defaults (verified against the providers' docs on 2026-11-23):** OpenAI
   `https://api.openai.com/v1`, model `gpt-4o-mini-transcribe` (the spec's; OpenAI lists
   it as deprecated with shutdown 2027-02-26, so `gpt-transcribe` is seeded in the
   combo); Groq `https://api.groq.com/openai/v1`, `whisper-large-v3-turbo`; Mistral
   `https://api.mistral.ai/v1`, `voxtral-mini-transcribe-3` (the spec's
   `voxtral-mini-latest` no longer appears in Mistral's docs; seeded too); Gemini
   `https://generativelanguage.googleapis.com/v1beta/openai` (chat completions with an
   `input_audio` part; Gemini has no OpenAI-compatible `/audio/transcriptions`), model
   `gemini-2.5-flash` (not confirmed by Google's page; editable). Every provider is sent
   `Authorization: Bearer <key>`.
8. **Not built in this PR** (spec "left to the builder"): the live level bar under the
   input (the mic button's pulse is the indicator), and the debug WAV-injection hook and
   the spoken-fixture harness case that needs it (a new input path into the shipped exe
   for a case that cannot run here).
9. **Hook location:** `pump_dispatch` (all keys and mouse of the UI thread, docked or
   undocked), plus `WM_ACTIVATEAPP(FALSE)` and `WM_ENABLE(FALSE)` on the main window and
   the undocked panel as the "lost activation / a modal opened" signal: every modal loop
   disables its owner.

## Review Focus

1. **Right Ctrl chords in the AI input and the terminal** (Ctrl+C, Ctrl+V, Ctrl+=,
   Ctrl+wheel zoom, Right Ctrl+letter control characters to the shell) must behave
   exactly as today with voice on — the classifier never consumes Right Ctrl, and a chord
   never opens the device (Task 5 tests `chord_before_delay_never_opens`).
2. **AltGr** (synthesised Left Ctrl + Right Alt) on a European layout must never count
   as the push-to-talk key: only `VK_CONTROL` with the extended-key bit is Right Ctrl
   (Task 5 `classify_left_ctrl_and_altgr_are_not_ptt`).
3. **A transcript arriving after the user moved on** (another tab, New chat, the tab
   closed, voice turned off) is dropped, never sent to another host (Task 5
   `binding_*`, Task 8 wiring).
4. **The device vanishing mid-recording** (USB headset unplugged) ends with an error and
   the UI back to idle, never a hang (Task 6: the loop waits on buffer event + stop event
   with a timeout and treats `AUDCLNT_E_DEVICE_INVALIDATED` as an error).
5. **Turning voice (or the AI assistant) off while recording or transcribing** cancels
   the upload, joins the threads and removes the mic button at once; a late transcript
   message is ignored (Task 8 `voice_feature_stop`, Task 7 cancel test).

---

## File structure

| File | New? | Responsibility |
|---|---|---|
| `src/core/voice_config.[ch]` | new | Enums and JSON names for the ptt key and mode; the voice settings defaults/validation helpers used by `loader.c`. |
| `src/core/voice_gate.[ch]` | new | Runtime "voice is running" flag (any thread); on only when set and `ai_gate_on()`. |
| `src/core/wav.[ch]` | new | WAV header writer, growable PCM16 buffer with a cap and secure wipe, peak/RMS level, silence and all-zero classification. |
| `src/core/voice_stt.[ch]` | new | STT provider table, base-URL normalisation, "use current AI assistant" resolution and reasons, request builders (multipart, Gemini JSON), response parsing, HTTP error text. |
| `src/core/voice_ptt.[ch]` | new | Push-to-talk state machine (time injected), Win32-message classifier (numeric constants, portable), transcript delivery decision, session binding. |
| `src/ui/voice_audio_win.[ch]` | new | The MTA audio thread: device enumeration, capture, 3 s test with levels, device-change notifications, shutdown. |
| `src/ui/voice_stt_win.[ch]` | new | STT upload worker thread with cancel; posts the result. |
| `src/ui/voice_win.[ch]` | new | Feature controller: start/stop, the `pump_dispatch` hook, the delay timer, activation/modal discard, delivery into the panel. |
| `src/config/config.h`, `src/config/loader.c` | modify | Fields, defaults, validate, load/save, the secret, the key table, `CFG_KEYS_MAX`. |
| `src/core/opt_feature.[ch]`, `src/core/notice.c` | modify | `OPT_VOICE`; the folded "Voice" upgrade line; first-run lists Voice. |
| `src/core/settings_layout.[ch]`, `src/core/settings_help.[ch]` | modify | The Voice page and its help entries. |
| `src/ui/settings.c`, `src/ui/resource.h` | modify | The Voice page controls. |
| `src/core/ai_http.h`, `src/ui/ai_http_win.c` | modify | `ai_http_post_ct()` with a content type. |
| `src/ui/window.c`, `src/ui/ai_chat*.c` | modify | Wiring, mic button, placeholder, statuses, demos, self-report. |
| `src/core/shortcuts.c`, `README.md`, `src/ui/cheatsheet_dlg.c` | modify | The shortcut and help text. |
| `tests/selftest/*`, `Makefile` | modify | `selftest-wine` gains voice off/on runs. |
| `tests/integration/*` | modify | New keys in the test env, voice-off cases, gallery states (unrun). |

## Task order

Core first (Tasks 1-4, all native-tested, nothing user-visible), then the Settings page
(5), capture (6), the STT client (7), push-to-talk and the panel (8), demos, help,
shortcuts and the harness (9), and the version, build and pull request (10). Each task
leaves `make -j test` green and the MinGW build compiling (`make -j release` without
`clean` is fine mid-run; it does not bump the version and its exe is not committed until
Task 10).

---

### Task 1: Voice settings, the feature registry, the gate and the upgrade notice

**Files:**
- Create: `src/core/voice_config.h`, `src/core/voice_config.c`, `src/core/voice_gate.h`,
  `src/core/voice_gate.c`, `tests/test_voice_config.c`
- Modify: `src/config/config.h` (Settings fields; `CFG_KEYS_MAX` 64 -> 96),
  `src/config/loader.c` (CRLF: defaults 589-631, validate 538-587, load after 784-807,
  save 1049-1311 including every `secure_zero` exit path at 1074/1089/1109/1278/1308,
  `config_free` wipe near 674, `k_setting_keys[]` 481-528),
  `src/core/opt_feature.[ch]`, `src/core/notice.c` (`notice_upgrade` 200-247),
  `tests/test_config.c`, `tests/test_notice.c` (`test_notice_first_run_copy` 72-95),
  `tests/test_opt_feature.c`, `tests/runner.c`,
  `tests/selftest/nutshell-ai-on.config`, `tests/selftest/nutshell-ai-off.config`,
  `tests/integration/NutshellIT.psm1` (`New-NutshellTestEnv` settings table 709-734)

**Interfaces:**
- Produces (`src/core/voice_config.h`):
  ```c
  typedef enum { VOICE_PTT_RIGHT_CTRL = 0, VOICE_PTT_F8 = 1, VOICE_PTT_F9 = 2, VOICE_PTT_COUNT } VoicePttKey;
  typedef enum { VOICE_MODE_HOLD = 0, VOICE_MODE_TOGGLE = 1, VOICE_MODE_COUNT } VoicePttMode;
  #define VOICE_MIC_DELAY_DEFAULT 150
  #define VOICE_MIC_DELAY_MIN 0
  #define VOICE_MIC_DELAY_MAX 400
  #define VOICE_CAP_MS 60000
  #define VOICE_SHORT_TAP_MS 300
  const char *voice_ptt_key_json(int key);        /* "right_ctrl" "f8" "f9"; default for out of range */
  int voice_ptt_key_from_json(const char *s);     /* unknown/NULL -> VOICE_PTT_RIGHT_CTRL */
  const char *voice_ptt_key_label(int key);       /* "Right Ctrl" "F8" "F9" */
  const char *voice_ptt_mode_json(int mode);      /* "hold" "toggle" */
  int voice_ptt_mode_from_json(const char *s);    /* unknown/NULL -> VOICE_MODE_HOLD */
  void voice_settings_default(Settings *s);       /* called by config_default_settings */
  void voice_settings_validate(Settings *s);      /* called by settings_validate */
  ```
- Produces (`Settings`, appended after `ai_policy_default`):
  `int voice_enabled; int voice_stt_use_ai; char voice_stt_provider[32];
  char voice_stt_url[CFG_STR_MAX]; char voice_stt_model[CFG_STR_MAX];
  char voice_stt_language[16]; char stt_api_key[CFG_STR_MAX];
  char stt_api_key_enc_preserved[CFG_BLOB_MAX]; char voice_input_device[CFG_STR_MAX];
  char voice_input_device_name[CFG_STR_MAX]; int voice_ptt_key; int voice_ptt_mode;
  int voice_mic_delay_ms; int voice_review;`
- Produces (`src/core/opt_feature.h`): `OPT_VOICE` (= 1, before `OPT_COUNT`), table row
  `{ OPT_VOICE, "Voice", "voice_enabled", OPT_AI }`, and
  `int opt_feature_active(const Settings *s, OptFeature f);` — enabled and, when it
  `requires` another, that one active too.
- Produces (`src/core/voice_gate.h`, any thread, portable, mirrors `ai_gate.h`):
  `void voice_gate_set_enabled(int on); void voice_gate_set_demo_override(int on);
  int voice_gate_on(void);` — `(enabled || demo) && ai_gate_on()`.

- [ ] **Step 1: Write the failing tests** in `tests/test_voice_config.c` (and the
  existing files named), registered in `tests/runner.c`:
  - `test_voice_config_defaults`: after `config_default_settings`, `voice_enabled == 0`,
    `voice_stt_use_ai == 1`, provider `"openai"`, `voice_mic_delay_ms == 150`,
    `voice_ptt_key == VOICE_PTT_RIGHT_CTRL`, `voice_ptt_mode == VOICE_MODE_HOLD`,
    `voice_review == 0`, every voice string and `stt_api_key` empty.
  - `test_voice_config_json_names`: the six json/label functions both ways, including
    NULL, `""` and `"bogus"` falling back to the defaults, and out-of-range ints.
  - `test_voice_config_roundtrip`: every voice field set to a non-default value
    (`voice_enabled=1, use_ai=0, provider "groq", url "https://h/v1", model "m",
    language "en", stt_api_key "sk-stt", device "{0.0.1.00000000}.{abc}", name
    "Headset Microphone (USB Audio)", key F9, mode toggle, delay 400, review 1`),
    `config_save` to a `TEST_TMP_DIR` path, `config_load`, all equal; the file text does
    not contain `sk-stt` and does contain `"stt_api_key": "$dpapi$v1$`.
  - `test_voice_config_first_load_voice_off`: a file with `ai_enabled: true`, an AI
    key and no voice keys loads with `voice_enabled == 0`, and the report's `missing`
    names all 13 voice keys in table order.
  - `test_voice_config_stt_key_foreign_blob_preserved`: as
    `test_config_ai_key_foreign_blob_preserved_through_load_save` (test_config.c:2124),
    for `stt_api_key` / `stt_api_key_enc_preserved`.
  - `test_voice_config_save_aborts_when_stt_encrypt_fails`: as
    `test_config_ai_key_save_aborts_when_dpapi_encrypt_fails` (2407) with only the STT
    key set; `config_save` returns -1 and the original file is unchanged.
  - `test_voice_config_validate_clamps`: delay -1 -> 0, 401 -> 400, 150 stays; ptt key 7
    -> right ctrl; mode 9 -> hold; provider `"elevenlabs"` -> `"openai"`; provider
    `"custom"` stays.
  - `test_opt_feature_*` extended: table has 2 rows in id order, Voice requires
    `OPT_AI`, `opt_feature_blocked_reason(&s, OPT_AI)` is `"Needs the AI assistant"`
    with AI off; `opt_feature_active(&s, OPT_VOICE)` is 0 for (ai 0, voice 1), 0 for
    (1, 0), 1 for (1, 1).
  - `test_voice_gate`: off by default; enabled with AI gate off -> 0; enabled with AI gate
    on -> 1; demo override alone with AI gate on -> 1; restore both gates to 0 at the end.
  - `test_notice_first_run_copy` updated: 2 items, `[1]` is `"Voice"` /
    `"off until you choose to turn it on"`; `action_page` still the AI page.
  - `test_notice_upgrade_folds_voice_keys`: a report missing all 13 voice keys and
    nothing else gives exactly 1 item, `"Voice"` / `"off until you choose to turn it on"`.
  - `test_notice_upgrade_lists_voice_subkey_when_switch_present`: a report missing only
    `voice_review` gives 1 item whose label is not `"Voice"`.
- [ ] **Step 2: Run** `make -j test 2>&1 | tail -5` — expect compile errors / FAILs for
  the new names.
- [ ] **Step 3: Implement.** Fields and `voice_config.c` as in Interfaces. Loader:
  load each key with the existing string/int/bool/enum patterns; `stt_api_key` through
  `load_secret()` (counting `migrated` like `ai_api_key`); save the 13 keys after
  `ai_policy_default` in Decision 1's order (move the "no trailing comma" to the new
  last key `voice_review`), the STT blob through `secret_prepare()` before any file I/O,
  and `secure_zero` of the STT blob on **every** path that zeroes `ai_api_key_blob`;
  `config_free` wipes `stt_api_key`. `k_setting_keys[]` gains the 13 keys in save order
  with `SH_NONE` and plain labels for now (`"Voice"` for `voice_enabled`; Task 5 swaps
  in the help ids). `notice_upgrade`: `voice_enabled` gets label `"Voice"` and
  `VAL_OFF`; keys named `voice_*` or `stt_api_key` are skipped when `voice_enabled` is
  also in `rep->missing` (and not counted in `total`). `voice_gate.c` mirrors
  `ai_gate.c` (atomic/volatile int as ai_gate does).
- [ ] **Step 4: Add the 13 keys with their defaults** to both
  `tests/selftest/nutshell-ai-*.config` files and to `New-NutshellTestEnv`'s settings
  hashtable (keep its "exactly the `k_setting_keys` set" comment true; `stt_api_key` =
  `""`). Without them `selftest-wine` hangs on an upgrade notice.
- [ ] **Step 5: Run** `make -j test 2>&1 | grep -E "Tests Run|FAIL"` — expect 0 failed,
  including the existing `test_config_keys_match_saved_file` and
  `test_config_report_*`. `make -j release 2>&1 | tail -3` compiles clean.
- [ ] **Step 6: Commit** `feat(voice): settings fields, the Voice feature switch and gate`.

### Task 2: WAV, PCM buffer, level and the silence gate

**Files:**
- Create: `src/core/wav.h`, `src/core/wav.c`, `tests/test_wav.c`
- Modify: `tests/runner.c`

**Interfaces:**
- Produces (`src/core/wav.h`):
  ```c
  #define WAV_RATE 16000
  #define WAV_HEADER_BYTES 44
  /* RIFF/WAVE PCM header, little-endian, for 1 channel 16-bit at `rate`. */
  void wav_header_write(unsigned char out[WAV_HEADER_BYTES], unsigned rate, size_t data_bytes);
  typedef struct { int16_t *s; size_t n, cap, max; } PcmBuf;   /* max = hard cap in samples */
  void   pcm_init(PcmBuf *b, size_t max_samples);
  size_t pcm_append(PcmBuf *b, const int16_t *src, size_t n);  /* returns samples taken; stops at max */
  int    pcm_full(const PcmBuf *b);
  void   pcm_free(PcmBuf *b);                                   /* secure_zero, free, reset */
  /* Heap WAV (header + data); *out_len set; NULL on OOM. Caller secure_zeroes + frees. */
  unsigned char *pcm_to_wav(const PcmBuf *b, unsigned rate, size_t *out_len);
  double pcm_peak(const int16_t *s, size_t n);   /* 0..1 of full scale */
  double pcm_rms(const int16_t *s, size_t n);    /* 0..1 of full scale */
  typedef enum { PCM_SPEECH = 0, PCM_SILENT = 1, PCM_ALL_ZERO = 2, PCM_EMPTY = 3 } PcmVerdict;
  #define PCM_WINDOW 320          /* 20 ms at 16 kHz */
  #define PCM_SPEECH_RMS 250      /* int16 RMS, about -42 dBFS */
  PcmVerdict pcm_classify(const int16_t *s, size_t n);
  ```
- [ ] **Step 1: Write the failing tests** (`tests/test_wav.c`):
  - `test_wav_header_fields`: for 32000 data bytes: `"RIFF"`, chunk size 36+32000,
    `"WAVE"`, `"fmt "`, 16, format 1, channels 1, rate 16000, byte rate 32000, block
    align 2, bits 16, `"data"`, 32000 — each read back little-endian.
  - `test_pcm_append_caps`: `pcm_init(&b, 1000)`; appending 600 then 600 takes 600 then
    400; `pcm_full` is 1; `pcm_free` leaves `s == NULL, n == 0`.
  - `test_pcm_to_wav_layout`: 3 samples {1, -1, 32767} give 50 bytes; bytes 44-49 are
    `01 00 FF FF FF 7F`.
  - `test_pcm_levels`: peak of {0, 16384, -32768} is 1.0; RMS of a constant 3277 buffer
    is within 0.001 of 0.1; empty input gives 0.
  - `test_pcm_classify`: 1 s all-zero -> `PCM_ALL_ZERO`; 1 s of deterministic noise in
    ±100 (an LCG) -> `PCM_SILENT`; 1 s of silence with one 20 ms window of a 440 Hz sine
    at amplitude 8000 -> `PCM_SPEECH`; noise ±100 plus a single sample of 1 -> still
    `PCM_SILENT` (not all-zero); n = 0 -> `PCM_EMPTY`; a buffer shorter than one window
    with amplitude 8000 -> `PCM_SPEECH`.
- [ ] **Step 2: Run** `make -j test` — FAIL (undefined).
- [ ] **Step 3: Implement** `src/core/wav.c` (growth by doubling from 16000 samples up to
  `max`, wiping the old block before freeing it; classification = all-zero check first,
  then any window's RMS >= `PCM_SPEECH_RMS`).
- [ ] **Step 4: Run** `make -j test` — PASS.
- [ ] **Step 5: Commit** `feat(voice): WAV framing, PCM buffer and the silence gate`.

### Task 3: STT providers, URL resolution, request bodies and responses

**Files:**
- Create: `src/core/voice_stt.h`, `src/core/voice_stt.c`, `tests/test_voice_stt.c`
- Modify: `tests/runner.c`

**Interfaces:**
- Consumes: `Settings` voice fields (Task 1), `base64_encode` (`src/core/base64.h`),
  `json_escape_string` (`src/core/json_validate.h`), `json_parse`/`json_obj_*`
  (`src/config/json_parser.h`).
- Produces (`src/core/voice_stt.h`):
  ```c
  typedef enum { STT_SHAPE_OPENAI = 0, STT_SHAPE_GEMINI_CHAT = 1 } SttShape;
  typedef struct {
      const char *id;            /* "openai" "groq" "mistral" "gemini" "custom" */
      const char *label;         /* "OpenAI" "Groq" "Mistral" "Google Gemini" "Custom" */
      const char *base;          /* default base URL, "" for custom */
      const char *const *models; /* NULL-terminated seed list, [0] is the default */
      SttShape shape;
  } SttProvider;
  const SttProvider *voice_stt_providers(size_t *n);
  const SttProvider *voice_stt_provider_find(const char *id);   /* NULL when unknown */
  /* Trim spaces and trailing '/', then strip a trailing "/audio/transcriptions" or
   * "/audio/speech" (and any '/' it leaves). Returns 0 when the result is empty or not
   * http(s)://, else 1. */
  int voice_base_url_normalise(const char *in, char *out, size_t cap);
  /* Why "Use current AI assistant" cannot be ticked for this chat provider, or NULL.
   * anthropic -> "Anthropic has no speech-to-text API"; deepseek -> "DeepSeek has no
   * speech-to-text API"; moonshot -> "Moonshot has no speech-to-text API"; custom whose
   * chat URL does not end in "/chat/completions" (after trimming '/') ->
   * "Set a separate voice provider: the custom chat URL has no /chat/completions to derive from". */
  const char *voice_stt_use_ai_reason(const char *chat_provider, const char *chat_custom_url);
  typedef struct {
      char url[512];        /* full endpoint */
      char model[CFG_STR_MAX];
      char language[16];
      SttShape shape;
      int key_from_chat;    /* 1: use Settings.ai_api_key; 0: stt_api_key */
  } SttTarget;
  /* 1 and *t filled, or 0 with a user-facing reason in err ("Choose a speech-to-text
   * provider", "Enter a base URL for the custom voice provider", or the use_ai reason). */
  int voice_stt_resolve(const Settings *s, SttTarget *t, char *err, size_t cap);
  /* multipart/form-data: part "file" (filename "speech.wav", Content-Type audio/wav),
   * part "model", part "language" only when non-empty, part "response_format" = "json"
   * only for the OpenAI shape when the model is not a gpt-4o* model. Heap result. */
  char *voice_stt_multipart(const char *boundary, const unsigned char *wav, size_t wav_len,
                            const char *model, const char *language, size_t *out_len);
  /* OpenAI chat-completions body for Gemini: one user message with a text part
   * "Transcribe this audio verbatim. Reply with only the words spoken, or nothing if no
   * one speaks." (plus " The language is <lang>." when set) and an input_audio part
   * {data: base64(wav), format: "wav"}; "model": model. Heap, NUL-terminated. */
  char *voice_stt_gemini_body(const unsigned char *wav, size_t wav_len,
                              const char *model, const char *language, size_t *out_len);
  /* 1 and the transcript (trimmed) in out, or 0 with err. OpenAI shape reads "text";
   * Gemini reads choices[0].message.content. */
  int voice_stt_parse(SttShape shape, const char *body, size_t len,
                      char *out, size_t cap, char *err, size_t errcap);
  /* User-facing text for a failed request: 401/403 -> "The speech-to-text key was
   * refused (HTTP 401). Check it in Settings > Voice."; 404 -> "...model or URL not found
   * (HTTP 404)..."; 413 -> "The recording was too large for the provider."; 429 ->
   * "The speech-to-text provider is rate limiting (HTTP 429). Try again shortly.";
   * other >= 400 -> "Speech-to-text failed (HTTP %d)."; 0 -> "Speech-to-text could not
   * connect: <transport error>". */
  void voice_stt_http_error(int status, const char *transport_err, char *out, size_t cap);
  ```
- Resolution rules (`voice_stt_resolve`): with `voice_stt_use_ai` ticked and
  `voice_stt_use_ai_reason(ai_provider, ai_custom_url) == NULL`: openai ->
  `https://api.openai.com/v1/audio/transcriptions`, gemini ->
  `https://generativelanguage.googleapis.com/v1beta/openai/chat/completions` (Gemini
  shape), custom -> chat URL minus `/chat/completions` + `/audio/transcriptions`;
  `key_from_chat = 1`. Ticked but the reason is non-NULL: return 0 with that reason
  (the Settings page shows it and leaves the box unticked/disabled). Unticked: the
  chosen provider's base (Custom: `voice_base_url_normalise(voice_stt_url)`) +
  `/audio/transcriptions`, Gemini -> `/chat/completions` with the Gemini shape;
  `key_from_chat = 0`. Model: `voice_stt_model` when non-empty, else the provider's
  `models[0]` (use-AI Custom with empty model: `"whisper-1"`).
- [ ] **Step 1: Write the failing tests** (`tests/test_voice_stt.c`):
  - `test_voice_stt_provider_table`: 5 rows in the order above; defaults
    `gpt-4o-mini-transcribe`, `whisper-large-v3-turbo`, `voxtral-mini-transcribe-3`,
    `gemini-2.5-flash`; OpenAI seeds also contain `gpt-transcribe`; Mistral's contain
    `voxtral-mini-latest`; `find("nope") == NULL`.
  - `test_voice_base_url_normalise`: `"https://h/v1/"` -> `"https://h/v1"`;
    `"https://h/v1/audio/transcriptions"` and `"https://h/v1/audio/speech/"` ->
    `"https://h/v1"`; `"  http://localhost:8000/v1  "` -> trimmed; `""`, `"ftp://x"`,
    `"/audio/transcriptions"` -> 0.
  - `test_voice_stt_use_ai_reason`: NULL for openai, gemini, and custom
    `"https://h/v1/chat/completions/"`; the exact strings above for anthropic, deepseek,
    moonshot and custom `"https://h/api/generate"`.
  - `test_voice_stt_resolve_use_ai`: chat openai -> OpenAI URL, key_from_chat 1, model
    default; chat custom `"https://h/v1/chat/completions"` ->
    `"https://h/v1/audio/transcriptions"`; chat gemini -> Gemini URL and shape; chat
    anthropic -> 0 and the Anthropic reason.
  - `test_voice_stt_resolve_separate`: unticked groq -> Groq URL, key_from_chat 0;
    custom `"https://h/v1/audio/transcriptions"` -> `"https://h/v1/audio/transcriptions"`;
    custom empty -> 0, `"Enter a base URL for the custom voice provider"`; model
    override `"x"` wins; language `"en"` copied.
  - `test_voice_stt_multipart`: boundary `"b0"`, wav bytes `{0,1,2,0}` (embedded NULs),
    model `"whisper-1"`, language `"en"`: the body starts `"--b0\r\n"`, contains
    `Content-Disposition: form-data; name="file"; filename="speech.wav"\r\nContent-Type:
    audio/wav\r\n\r\n` followed by the 4 raw bytes, contains `name="model"\r\n\r\nwhisper-1\r\n`
    and `name="language"\r\n\r\nen\r\n` and `name="response_format"\r\n\r\njson\r\n`, and ends
    `"--b0--\r\n"`; `*out_len` equals the byte count; with language `""` there is no
    language part; with model `gpt-4o-mini-transcribe` there is no response_format part.
  - `test_voice_stt_gemini_body`: parses with `json_parse`; `model` is the model;
    `messages[0].content[1].input_audio.format == "wav"` and its `data` base64-decodes
    to the WAV bytes; the language sentence appears only when set.
  - `test_voice_stt_parse`: `{"text":"  list my files \n"}` -> `"list my files"`;
    Gemini `{"choices":[{"message":{"content":"Install nginx."}}]}` -> `"Install nginx."`;
    `{"text":""}` -> 1 and `""`; invalid JSON, a missing field and an `{"error":
    {"message":"bad key"}}` body -> 0 with err containing `bad key` for the last.
  - `test_voice_stt_http_error`: 401, 404, 413, 429, 500 and status 0 with
    `"timed out"` give the texts above.
- [ ] **Step 2: Run** `make -j test` — FAIL.
- [ ] **Step 3: Implement** `src/core/voice_stt.c`.
- [ ] **Step 4: Run** `make -j test` — PASS. Also `make fuzz-run` builds (no change
  expected).
- [ ] **Step 5: Commit** `feat(voice): STT provider table, URL rules and request bodies`.

### Task 4: The push-to-talk state machine, the message classifier and delivery

**Files:**
- Create: `src/core/voice_ptt.h`, `src/core/voice_ptt.c`, `tests/test_voice_ptt.c`
- Modify: `tests/runner.c`

**Interfaces:**
- Consumes: `voice_config.h` (Task 1).
- Produces (`src/core/voice_ptt.h`, portable: define the Win32 message and VK numbers
  it needs under `#ifndef _WIN32`, e.g. `WM_KEYDOWN 0x0100`, `VK_CONTROL 0x11`):
  ```c
  typedef enum { VI_NONE = 0, VI_PTT_DOWN, VI_PTT_REPEAT, VI_PTT_UP, VI_ESCAPE, VI_OTHER } VoiceInput;
  /* Right Ctrl = VK_CONTROL (or VK_RCONTROL) with lParam bit 24 (extended) set, on
   * WM_KEYDOWN/WM_KEYUP; bit 30 set on a key-down = repeat. F8/F9 = VK_F8/VK_F9 when
   * ptt_key selects them. Escape key-down = VI_ESCAPE. Any other WM_KEYDOWN or
   * WM_SYSKEYDOWN (Left Ctrl, AltGr's synthesised Left Ctrl, Shift, letters, Right Ctrl
   * when ptt_key is F8/F9), WM_[LRMX]BUTTONDOWN, WM_NC[LRMX]BUTTONDOWN, WM_MOUSEWHEEL and
   * WM_MOUSEHWHEEL = VI_OTHER. Key-ups of other keys, mouse moves and everything else =
   * VI_NONE. */
  VoiceInput voice_ptt_classify(unsigned msg, uintptr_t wparam, intptr_t lparam, int ptt_key);

  typedef enum { VS_IDLE = 0, VS_ARMED, VS_TOGGLE_PRESS, VS_REC_HOLD, VS_REC_LATCHED,
                 VS_STOP_PRESS, VS_KEY_SPENT } VoiceState;
  typedef enum { VA_NONE = 0, VA_OPEN, VA_FINISH, VA_DISCARD, VA_BUSY } VoiceAction;
  typedef enum { VE_KEY_DOWN, VE_KEY_UP, VE_OTHER, VE_ESCAPE, VE_TICK, VE_MIC_CLICK,
                 VE_DEACTIVATE } VoiceEvent;
  typedef struct {
      VoiceState state; int mode; int delay_ms;
      unsigned long key_down_at, rec_start_at;
  } VoicePtt;
  typedef struct { VoiceAction action; int consume; } VoiceStep;
  void voice_ptt_init(VoicePtt *p, int mode, int delay_ms);
  /* in_panel: keyboard focus is in the AI panel; busy: the assistant is streaming,
   * running commands or has a card waiting. */
  VoiceStep voice_ptt_step(VoicePtt *p, VoiceEvent ev, unsigned long now_ms,
                           int in_panel, int busy);
  int voice_ptt_recording(const VoicePtt *p);  /* VS_REC_HOLD, VS_REC_LATCHED or VS_STOP_PRESS */
  /* ms until the next VE_TICK matters (delay end or cap), or -1 when none. */
  long voice_ptt_next_deadline(const VoicePtt *p, unsigned long now_ms);

  typedef enum { VD_NOTHING = 0, VD_SEND, VD_FILL, VD_APPEND } VoiceDelivery;
  /* Whitespace-only transcript -> VD_NOTHING. Empty input: review ? VD_FILL : VD_SEND,
   * out = transcript. Non-empty input -> VD_APPEND, out = input + (input ends in a space
   * or newline ? "" : " ") + transcript. Truncates to cap. */
  VoiceDelivery voice_deliver(const char *input, const char *transcript, int review,
                              char *out, size_t cap);

  typedef struct { const void *session; unsigned session_gen; unsigned conv_gen; } VoiceBinding;
  int voice_binding_same(const VoiceBinding *a, const VoiceBinding *b);  /* all three equal, session non-NULL */

  /* "Listening… release Right Ctrl to send" (hold, key started),
   * "Listening… press Right Ctrl again to send" (toggle, or a mic-button start),
   * with F8/F9 in place of Right Ctrl. UTF-8. */
  void voice_cue_text(int mode, int ptt_key, int latched, char *out, size_t cap);
  ```
- Transition rules (the tests pin each):
  - Hold, `VS_IDLE` + KEY_DOWN: not `in_panel` -> stay idle, `VA_NONE`; else
    `VS_ARMED` (key_down_at = now); with delay 0 the same call goes on as the delay
    check below.
  - `VS_ARMED` + TICK with `now - key_down_at >= delay`: `busy` -> `VA_BUSY`,
    `VS_KEY_SPENT`; else `VA_OPEN`, `VS_REC_HOLD`, rec_start_at = now.
  - `VS_ARMED` + KEY_UP -> idle, `VA_NONE`. `VS_ARMED` + OTHER -> `VS_KEY_SPENT`,
    `VA_NONE`. `VS_KEY_SPENT` + KEY_UP -> idle.
  - `VS_REC_HOLD` + KEY_UP: `now - key_down_at < VOICE_SHORT_TAP_MS` -> `VA_DISCARD`,
    else `VA_FINISH`; idle. + OTHER -> `VA_DISCARD`, `VS_KEY_SPENT`. + TICK at
    `now - rec_start_at >= VOICE_CAP_MS` -> `VA_FINISH`, `VS_KEY_SPENT`.
  - Toggle, `VS_IDLE` + KEY_DOWN (in_panel) -> `VS_TOGGLE_PRESS`; + OTHER ->
    `VS_KEY_SPENT`; + KEY_UP -> `busy` ? `VA_BUSY` idle : `VA_OPEN` `VS_REC_LATCHED`.
  - `VS_REC_LATCHED` + KEY_DOWN (no in_panel requirement) -> `VS_STOP_PRESS`;
    `VS_STOP_PRESS` + KEY_UP -> `VA_FINISH` idle; + OTHER -> `VA_DISCARD`
    `VS_KEY_SPENT`. `VS_REC_LATCHED` + OTHER -> no change (typing elsewhere while a
    latched recording runs is allowed). + TICK at the cap -> `VA_FINISH` idle.
  - MIC_CLICK: idle -> `busy` ? `VA_BUSY` : `VA_OPEN` `VS_REC_LATCHED`;
    `VS_REC_LATCHED` -> `VA_FINISH` idle; any other state -> treated as OTHER.
  - ESCAPE while recording -> `VA_DISCARD`, consume = 1, state `VS_KEY_SPENT` when the key
    is down (`VS_REC_HOLD`, `VS_STOP_PRESS`) else idle; ESCAPE otherwise -> `VA_NONE`,
    consume 0 (Escape keeps its meaning).
  - DEACTIVATE (activation lost or a modal opened): recording -> `VA_DISCARD`; any state
    -> idle.
  - Repeats never reach `voice_ptt_step` (the controller drops `VI_PTT_REPEAT`).
- [ ] **Step 1: Write the failing tests** (`tests/test_voice_ptt.c`), each a scripted
  sequence of `(event, now, in_panel, busy)` with the expected action and state:
  `test_voice_classify_right_ctrl` (down/up with bit 24 -> PTT_DOWN/UP; bit 30 ->
  REPEAT), `test_voice_classify_left_ctrl_and_altgr_are_not_ptt` (VK_CONTROL without
  bit 24 -> OTHER; VK_MENU with bit 24 -> OTHER), `test_voice_classify_f8_f9` (F8 is
  PTT only with `VOICE_PTT_F8`; with Right Ctrl selected F8 is OTHER and Right Ctrl is
  OTHER when F9 is selected), `test_voice_classify_mouse_and_wheel` (L/R/M/X button
  downs and both wheels -> OTHER; WM_MOUSEMOVE and key-ups of letters -> NONE),
  `test_voice_classify_escape`, `test_voice_hold_delay_0_150_400` (opens at down for 0;
  at 150 not at 149; at 400 not at 399), `test_voice_chord_before_delay_never_opens`
  (down, OTHER at +50, TICK at +200 -> no OPEN; KEY_UP -> idle),
  `test_voice_chord_while_recording_discards` (key, mouse button and wheel variants),
  `test_voice_short_tap_discarded` (delay 0: up at +299 -> DISCARD, at +300 -> FINISH),
  `test_voice_cap_finishes` (TICK at rec_start+59999 -> NONE, +60000 -> FINISH; then
  KEY_UP -> NONE, idle), `test_voice_escape_discards_and_consumes`,
  `test_voice_not_in_panel_ignored`, `test_voice_busy_reports_at_start_only` (busy at
  key-down then a chord -> no VA_BUSY; busy when the delay elapses -> VA_BUSY once),
  `test_voice_toggle_mode` (clean press-release opens; a press with OTHER between does
  not; the next clean press-release finishes; a KEY_DOWN from outside the panel still
  stops a latched recording), `test_voice_mic_click` (open, finish, busy),
  `test_voice_deactivate_and_modal_discard` (from ARMED -> NONE/idle; from REC_HOLD and
  REC_LATCHED -> DISCARD/idle), `test_voice_next_deadline` (ARMED at 0 with delay 150
  -> 150 at now 0, 50 at now 100; recording -> time to the cap; idle -> -1),
  `test_voice_deliver` (`""`+`"  \n"` -> NOTHING; `""`+`"hi"` -> SEND `"hi"`, with review
  -> FILL; `"draft"`+`"hi"` -> APPEND `"draft hi"`; `"draft "`+`"hi"` -> `"draft hi"`;
  truncation to cap 6), `test_voice_binding_same` (session change, session_gen change,
  conv_gen change each -> 0; NULL session -> 0), `test_voice_cue_text` (the three
  wordings with Right Ctrl and with F9).
- [ ] **Step 2: Run** `make -j test` — FAIL.
- [ ] **Step 3: Implement** `src/core/voice_ptt.c`.
- [ ] **Step 4: Run** `make -j test` — PASS.
- [ ] **Step 5: Commit** `feat(voice): push-to-talk state machine and transcript delivery`.

### Task 5: The Voice settings page

**Files:**
- Modify: `src/core/settings_layout.[ch]` (enum 22-35: `SETTINGS_PAGE_VOICE` after
  `SETTINGS_PAGE_AI_BEHAVIOUR`; `NAV_TABLE` 12-26: `"Voice"` depth 1 between Behaviour
  and Web Access; `settings_page_title` 71-86: `"Voice"`),
  `src/core/settings_help.[ch]` (enum 15-36, table 16-594: the new entries between the
  AI_BEHAVIOUR and AI_WEB entries), `src/config/loader.c` (swap the voice keys' `SH_NONE`
  for their ids; `voice_enabled` -> `SH_VOICE_ENABLE` and keep the `"Voice"` label via
  the `label` column), `src/ui/settings.c`, `src/ui/resource.h` (IDs from 3099),
  `src/ui/window.c` (`on_settings_clicked_page` 2356-2460), `tests/test_settings_layout.c`
  (`test_sl_nav_count_is_13` -> 14 and renamed `_is_14` in `tests/runner.c` 3758/8074,
  `test_sl_nav_exact_sequence`, `test_sl_page_title*`), `tests/test_settings_help.c`
  (`SH_COUNT` 49 -> 62 at 107, `k_names[]` 82-98, `render_default` 216-311),
  `tests/integration/cases/50-window.ps1` (`settings_help_tool_count` 722-751)
- Create: `src/ui/voice_win.h`, `src/ui/voice_win.c` (the controller, a stub here)

**Interfaces:**
- Consumes: Tasks 1 and 3 (`voice_stt_providers`, `voice_stt_use_ai_reason`,
  `voice_base_url_normalise`), `opt_feature_blocked_reason`, `voice_gate_on`.
- Produces (`src/ui/voice_win.h`):
  `void voice_feature_apply(HWND main);` — called after every Settings save and from
  `ai_feature_on/off` and at startup (window.c 4740-4741): sets
  `voice_gate_set_enabled(opt_feature_active(&settings, OPT_VOICE))` and, from Task 6
  on, starts or stops the feature to match. In this task it only sets the gate.
  `int voice_feature_running(void);`
- Help ids (13, in this order): `SH_VOICE_ENABLE`, `SH_VOICE_STT_USE_AI`,
  `SH_VOICE_STT_PROVIDER`, `SH_VOICE_STT_KEY`, `SH_VOICE_STT_URL`,
  `SH_VOICE_STT_MODEL`, `SH_VOICE_LANGUAGE`, `SH_VOICE_DEVICE`, `SH_VOICE_TEST`
  (`SH_KIND_ACTION`), `SH_VOICE_PTT_KEY`, `SH_VOICE_PTT_MODE`, `SH_VOICE_MIC_DELAY`
  (min 0, max 400, text contains "0 to 400"), `SH_VOICE_REVIEW`. Labels: "Enable voice",
  "Use current AI assistant to convert speech to text", "Provider:", "API key:",
  "Base URL:", "Model:", "Language:", "Input device:", "Test", "Push-to-talk key:",
  "Mode:", "Mic start delay (ms):", "Review before sending". Help bodies (ASCII, 2-6
  sentences, ending `Default: X.`) say: Enable voice — off by default, nothing of voice
  loads while it is off, needs the AI assistant, audio goes to the chosen provider only
  while you hold the key; Base URL — a **base** URL such as https://host/v1, not a full
  endpoint, a pasted /audio/transcriptions is accepted; Language — blank lets the
  provider detect it, a two-letter code such as en; Mic start delay — the microphone
  opens only once the key has been held alone this long, so shortcuts never open it;
  Review — the transcript waits in the box for Enter.
- Page rows, top to bottom (`add_ctrl`; raise `MAX_SETTINGS_CTRLS` 64 -> 96 and
  `tips[96]` -> 128): Enable voice (check, full width) and beside/below it a hint row
  showing `opt_feature_blocked_reason(...)` when the dialog's AI tick is off; a
  "Speech to Text" section label; Use current AI assistant (check, full width) and its
  reason hint (`voice_stt_use_ai_reason` for the dialog's current chat provider and
  custom URL; when non-NULL the box is unticked and disabled); Provider (combo of
  `voice_stt_providers` labels); API key (`ES_PASSWORD`, `EM_SETLIMITTEXT`,
  `pw_reveal_attach`); Base URL (Custom only) with the clear-text warning reusing
  `net_url_cleartext_remote` and the `hUrlWarn` colouring pattern (settings.c 750-799,
  838-842); Model (`CBS_DROPDOWN` editable, seeded from the effective provider's
  `models`, current value kept); Language (edit, limit 15); a "Microphone" section label;
  Input device (combo, `extra` = Test button) — items "Windows default" then the saved
  device name, or the single disabled item "Save to list microphones" while
  `!voice_gate_on()`; Test is disabled in this task; a meter row (owner-drawn static,
  painted with `ns_draw_meter`, fraction 0 for now); Push-to-talk key (Right Ctrl, F8,
  F9); Mode ("Hold to talk", "Toggle"); Mic start delay (edit, `keep_entry` pattern
  with min/max from the help entry); Review before sending (check). Provider, API key
  and Base URL rows are hidden while Use current AI assistant is ticked and allowed.
  Section labels reuse the page's existing heading style if one exists, else a
  `make_hint` row in `FONT_TITLE`.
- `voice_enable_sync(SettingsDlgData *d)`, called wherever `ai_enable_sync` is (1782,
  1971-1974, 1815, 2130) and on the voice boxes' `BN_CLICKED` and the AI provider
  combo's `CBN_SELCHANGE`: Enable voice is enabled iff the dialog's AI tick is checked;
  every other Voice row is enabled iff both ticks are checked.
- Save (`IDOK`, 2099-2366): copy every field; `voice_stt_url` through
  `voice_base_url_normalise` when non-empty (keep the raw text if it fails, so the user
  sees it); `config_secret_drop_stale_preserved(s->stt_api_key,
  s->stt_api_key_enc_preserved, ...)` next to the AI one (2201); a device combo left on
  "Windows default" clears `voice_input_device` and its name, a listed device stores
  its ID and name (Task 6 fills the list), the "Save to list microphones" item leaves
  both unchanged. Then `on_settings_clicked_page` calls `voice_feature_apply(hwnd)`
  after the AI on/off block; `ai_feature_on/off` call it too.
- [ ] **Step 1: Write/adjust the failing tests**: the layout tests (14 rows, `"Voice"`
  between Behaviour and Web Access, its title), `test_settings_help_*` (`SH_COUNT` 62,
  the names, `render_default` cases for the 12 settings), a new
  `test_settings_help_voice_entries` (every id's page is `SETTINGS_PAGE_VOICE`;
  `SH_VOICE_ENABLE` label `"Enable voice"` and default text `"Off"`;
  `SH_VOICE_MIC_DELAY` min 0 max 400 default `"150"`; `SH_VOICE_STT_URL` text contains
  `"base URL"`), and `test_config_keys_match_saved_file` still passing with the ids.
- [ ] **Step 2: Run** `make -j test` — FAIL on the counts and names.
- [ ] **Step 3: Implement** the core table changes, then the page in `settings.c`, the
  IDs, `voice_win.[ch]` stub and the window.c calls.
- [ ] **Step 4: Update `settings_help_tool_count`** in `50-window.ps1` and the two
  README mentions (`tests/integration/README.md:47, :218`) for the new tips: count each
  new labelled control that is enabled when the case opens Settings (the case's config
  has AI on? read the case) plus one eye tooltip window for the new `pw_reveal_attach`.
  Write the arithmetic in a comment; it is unrun.
- [ ] **Step 5: Run** `make -j test` (PASS) and `make -j release` (clean). The page is
  seen under Wine in Task 9 (`--ui-demo=settings-voice`).
- [ ] **Step 6: Commit** `feat(voice): the Voice settings page and its switch`.

### Task 6: WASAPI capture on one audio thread, and the self-report

**Files:**
- Create: `src/ui/voice_audio_win.h`, `src/ui/voice_audio_win.c`,
  `tests/test_voice_audio_win.c`, `tests/selftest/nutshell-voice-on.config`
- Modify: `src/ui/voice_win.c`, `src/ui/settings.c` (device list, Test, meter,
  device-change refresh), `src/ui/window.c` (`WM_SELF_REPORT` 5408-5420),
  `Makefile` (`WIN_TEST_SRCS` 282-316, `WIN_ONLY_TESTS` 154-158, `WIN_TEST_LDFLAGS` 321
  gains `-lole32`; `selftest-wine` 436-453), `tests/selftest/check.sh`,
  `tests/win_runner.c`

**Interfaces:**
- Consumes: `wav.h` (Task 2), `dll_record_note` (`src/core/dll_record.h`).
- Produces (`src/ui/voice_audio_win.h`):
  ```c
  /* Messages posted to the notify HWND given at start. */
  #define WM_VOICE_AUDIO (WM_APP + 0x40)   /* wParam = VoiceAudioMsg, lParam = payload */
  typedef enum {
      VAM_READY = 1,        /* lParam 0: thread up, enumerator created (or failed: VAM_ERROR first) */
      VAM_DEVICES,          /* lParam = VoiceDeviceList* (receiver frees with voice_audio_free_devices) */
      VAM_LEVEL,            /* lParam = level 0..1000 (peak of the last ~50 ms) */
      VAM_CAPTURED,         /* lParam = VoiceCapture* (receiver frees with voice_audio_free_capture) */
      VAM_ERROR,            /* lParam = VoiceAudioErr */
      VAM_DEVICES_CHANGED   /* lParam 0: from IMMNotificationClient; re-enumerate */
  } VoiceAudioMsg;
  typedef enum { VAE_NONE = 0, VAE_NO_DEVICE, VAE_DEVICE_LOST, VAE_OPEN_FAILED,
                 VAE_COM_FAILED, VAE_NO_MEMORY } VoiceAudioErr;
  typedef struct { int n; struct { wchar_t id[256]; char name[256]; } dev[32]; } VoiceDeviceList;
  typedef struct { PcmBuf pcm; PcmVerdict verdict; int used_default; unsigned token; } VoiceCapture;
  int  voice_audio_start(HWND notify);          /* creates the MTA thread; 0 on failure */
  void voice_audio_stop(void);                  /* stop event, join (bounded 3 s), release all */
  int  voice_audio_wait_ready(DWORD ms);        /* 1 once VAM_READY was reached */
  void voice_audio_enumerate(void);             /* -> VAM_DEVICES */
  void voice_audio_capture_begin(const char *device_id_utf8, unsigned token, int test_only);
  void voice_audio_capture_end(int keep);       /* keep=1 -> VAM_CAPTURED; 0 -> discard (wiped) */
  void voice_audio_watch_devices(int on);       /* register/unregister the notification client */
  void voice_audio_free_devices(VoiceDeviceList *l);
  void voice_audio_free_capture(VoiceCapture *c);  /* pcm_free (secure wipe) + free */
  ```
  Commands reach the thread through a small locked queue plus a wake event; the thread
  waits on {wake event, stop event, the capture buffer event} with a 100 ms timeout.
- Behaviour pinned by the spec (each is a code-review point): `CoInitializeEx(NULL,
  COINIT_MULTITHREADED)` on the thread; `dll_record_note("mmdevapi.dll")` immediately
  before `CoCreateInstance(&CLSID_MMDeviceEnumerator, ...)` (the only one, at thread
  start); default device = `GetDefaultAudioEndpoint(eCapture, eConsole)`; a stored ID is
  used only when `GetDevice` succeeds and `GetState` is `DEVICE_STATE_ACTIVE`, else the
  default with `used_default = 1` (the stored ID is never changed); `IAudioClient_
  Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
  AUTOCONVERTPCM | SRC_DEFAULT_QUALITY, 200 ms, 0, &wfx 16000/1/16, NULL)`; capture loop
  drains every packet into the `PcmBuf` (cap `VOICE_CAP_MS` worth); a `GetBuffer` or
  `GetNextPacketSize` failure of `AUDCLNT_E_DEVICE_INVALIDATED` (or any failure) ends
  with `VAE_DEVICE_LOST`; no buffer event for 2 s while capturing ends with
  `VAE_DEVICE_LOST`; at end the client is stopped and released before posting, so the
  device is closed between recordings; `test_only` captures run for 3 s, post
  `VAM_LEVEL` and discard; `IMMNotificationClient` (a C vtable) only `PostMessage`s
  `VAM_DEVICES_CHANGED` and is unregistered on the audio thread, never inside a
  callback; enumeration lists `DEVICE_STATE_ACTIVE` capture endpoints with
  `PKEY_Device_FriendlyName`. GUIDs (`CLSID_MMDeviceEnumerator`, `IID_IMMDeviceEnumerator`,
  `IID_IAudioClient`, `IID_IAudioCaptureClient`, `IID_IMMNotificationClient`,
  `PKEY_Device_FriendlyName`) are defined in this file (`INITGUID` or `DEFINE_GUID`
  copies), so no new import library or DLL import appears. If the cross-compile's
  `objdump -p build/win/nutshell.exe | grep "DLL Name"` lists anything new, stop.
- `voice_feature_apply` now: on -> `voice_audio_start(main)`; off -> `voice_audio_stop()`.
  `WM_VOICE_AUDIO` is handled in window.c's WndProc and forwarded to
  `voice_on_audio_msg(WPARAM, LPARAM)` in voice_win.c (a Settings page that is open is
  told through `settings_voice_on_audio(...)`, a function settings.c exports).
- Settings: while `voice_gate_on()`, opening the Voice page calls
  `voice_audio_enumerate()` and `voice_audio_watch_devices(1)` (0 on close); the device
  combo lists "Windows default" + each name; a saved device that is not in the list
  shows "Windows default (saved microphone not found: <name>)" as the selected item,
  and saving without touching it keeps the stored ID. Test: `capture_begin(id, 0, 1)`,
  the meter row repaints from `VAM_LEVEL` (`ns_draw_meter`, fraction level/1000), the
  button reads "Testing…" for 3 s; `VAE_NO_DEVICE` shows "No microphone was found." in
  the hint style.
- Self-report: `WM_SELF_REPORT`, before writing, calls `voice_audio_wait_ready(5000)`
  when `voice_gate_on()`.
- `selftest-wine`: three runs — `off` (AI off, **voice on** in the file: the dependency
  must keep it off), `voiceoff` (AI on, voice off) and `on` (AI on, voice on).
  `check.sh` gains: `off` and `voiceoff` fail if `requested: mmdevapi.dll` or a
  `module: mmdevapi.dll` / `module: audioses.dll` line appears; `voiceoff` additionally
  requires the AI DLLs as `on` does today; `on` requires `requested: mmdevapi.dll` as
  well. Keep the "record must not be empty" check. If Wine itself maps mmdevapi.dll at
  start-up in the `off` run (it should not), drop only the `module:` assertion for it,
  with a comment saying why, and keep the `requested:` one.
- [ ] **Step 1: Write the failing wintests** (`tests/test_voice_audio_win.c`, registered
  in `tests/win_runner.c`, added to `WIN_ONLY_TESTS` and `WIN_TEST_SRCS` with
  `src/ui/voice_audio_win.c`, `src/core/wav.c`, `src/core/dll_record.c` if not there):
  `test_voice_audio_start_stop` (start with a message-only window, `wait_ready(5000)`
  is 1, `requested: mmdevapi.dll` is in the record, stop returns within 3 s and a second
  stop is harmless); `test_voice_audio_enumerate_defined` (VAM_DEVICES arrives within
  5 s, `n >= 0`, or VAM_ERROR with `VAE_COM_FAILED`/`VAE_NO_DEVICE` — never nothing);
  `test_voice_audio_capture_no_device_or_data` (begin on the default; within 5 s either
  VAM_ERROR `VAE_NO_DEVICE`/`VAE_OPEN_FAILED`, or after `capture_end(1)` a VAM_CAPTURED
  whose pcm is a whole number of samples; the test prints which); 
  `test_voice_audio_bad_id_falls_back` (an ID "{bogus}" with an endpoint present ->
  `used_default == 1`; with none -> `VAE_NO_DEVICE`); `test_voice_audio_never_started`
  (stop/enumerate/capture calls with no thread are no-ops that post nothing). Pump the
  message-only window with `PeekMessage` + a 5 s `GetTickCount64` bound.
- [ ] **Step 2: Run** `xvfb-run -a make wintest 2>&1 | tail -15` — FAIL/undefined.
- [ ] **Step 3: Implement** `voice_audio_win.c`, the Settings wiring, the self-report
  wait, the selftest configs/runs and `check.sh`.
- [ ] **Step 4: Run** `xvfb-run -a make wintest` (PASS, note the no-device lines),
  `make -j test`, `make -j release`, `objdump -p build/win/nutshell.exe | grep "DLL Name"`
  (unchanged list) and `make selftest-wine` (PASS for off, voiceoff, on).
- [ ] **Step 5: Commit** `feat(voice): WASAPI capture on one MTA audio thread`.

### Task 7: The STT upload worker

**Files:**
- Create: `src/ui/voice_stt_win.h`, `src/ui/voice_stt_win.c`,
  `tests/test_voice_stt_win.c`
- Modify: `src/core/ai_http.h` (CRLF), `src/ui/ai_http_win.c` (CRLF: 140-142 and the
  non-Win32 stubs 602-636), `Makefile` (wintest lists), `tests/win_runner.c`

**Interfaces:**
- Produces (`src/core/ai_http.h`): `int ai_http_post_ct(const char *url, const char *
  const *headers, const char *content_type, const char *body, size_t body_len,
  AiHttpResponse *resp, AiNetCancel *nc);` — `ai_http_post` becomes a call to it with
  `"application/json"`; the header becomes `Content-Type: <content_type>` (reject a
  content type containing CR or LF, or longer than 200, with "Invalid content type").
  Timeouts are a parameter of an internal function so the STT call uses Decision 6's
  values; the chat path keeps its own.
- Produces (`src/ui/voice_stt_win.h`):
  ```c
  #define WM_VOICE_STT (WM_APP + 0x41)   /* lParam = VoiceSttResult*, receiver frees */
  typedef struct { unsigned token; int ok; char text[4096]; char err[300]; } VoiceSttResult;
  /* Takes ownership of wav (wiped and freed by the worker). Resolves the target with
   * voice_stt_resolve(s, ...) on the caller's thread and copies the key; returns 0 with
   * err filled when resolution fails or voice_gate_on() is 0 (nothing is sent). */
  int  voice_stt_submit(HWND notify, const Settings *s, unsigned char *wav, size_t wav_len,
                        unsigned token, char *err, size_t errcap);
  void voice_stt_cancel_all(void);   /* fire every token, join (bounded 3 s) */
  ```
  The worker builds the body (`voice_stt_multipart` with a random boundary from
  `BCryptGenRandom` — bcrypt is already linked — or `voice_stt_gemini_body`), sends
  `Authorization: Bearer <key>` through `ai_http_post_ct`, parses with
  `voice_stt_parse`, maps failures with `voice_stt_http_error`, wipes the body, the WAV
  and the key copy with `secure_zero`, and posts the result. `voice_gate_on()` is checked
  again on the worker right before sending. The transcript is logged only with
  `DLOG_CONTENT`; a `DLOG` line (category ai) records provider, status, byte counts and
  timing, never the key or text.
- [ ] **Step 1: Write the failing wintests** (`tests/test_voice_stt_win.c`):
  `test_voice_stt_refused_when_gate_off` (gates off -> `voice_stt_submit` returns 0,
  err non-empty, the WAV is freed, nothing posted within 500 ms);
  `test_voice_stt_unresolvable_target` (gates on, provider custom with empty URL -> 0
  and the "Enter a base URL" text); `test_voice_stt_cancel_unreachable` (gates on,
  custom base `http://127.0.0.1:9/v1` (discard port), submit then `cancel_all` -> a
  posted result with `ok == 0` arrives within 3 s, either "cancelled" or a connect
  error). `ai_http_win.c` and `winhttp_open.c` may already be in `WIN_TEST_SRCS`; add
  what is missing.
- [ ] **Step 2: Run** `xvfb-run -a make wintest` — FAIL.
- [ ] **Step 3: Implement**; `make -j test` still green (`test_ai_http.c` covers
  `ai_http_post` through the stubs; add a native test that `ai_http_post_ct` with a
  CR/LF content type returns -1 if the stub path can express it, else note why not).
- [ ] **Step 4: Run** `xvfb-run -a make wintest`, `make -j test`, `make -j release`.
- [ ] **Step 5: Commit** `feat(voice): upload recordings to the STT provider`.

### Task 8: Push-to-talk, the mic button and delivery into the chat

**Files:**
- Modify: `src/ui/voice_win.[ch]`, `src/ui/window.c` (`pump_dispatch` 1274-1278, WndProc:
  new `WM_ACTIVATEAPP` and `WM_ENABLE` cases, `WM_TIMER`, `WM_VOICE_STT`, `WM_DESTROY`),
  `src/ui/ai_chat.c` (WM_CREATE 385-403, New Chat 766-827, `WM_DRAWITEM` 2111,
  `WM_DESTROY` 2218-2284, the undocked panel's `WM_ACTIVATEAPP`/`WM_ENABLE`),
  `src/ui/ai_chat_view.c` (composer layout 341-357), `src/ui/ai_chat_input.c` (subclass
  121: the painted cue), `src/ui/ai_chat_internal.h` (`AiChatData` 160-419; timers
  143-147), `src/ui/ai_chat.h`, `tests/test_voice_ptt_win.c` (new), `tests/win_runner.c`,
  `Makefile` (wintest lists)

**Interfaces:**
- Consumes: Tasks 4, 6, 7.
- Produces (`src/ui/ai_chat.h`, implemented in the panel files):
  ```c
  int  ai_chat_voice_busy(HWND panel);          /* ACTIVE_BUSY || dispatch_active || an unfinished batch in active_state */
  void ai_chat_voice_binding(HWND panel, VoiceBinding *out);  /* active_state, session_gen, conv_gen */
  void ai_chat_voice_attach(HWND panel, int on);              /* create/destroy the mic button, relayout */
  typedef enum { AIV_IDLE = 0, AIV_RECORDING, AIV_TRANSCRIBING } AiVoiceUi;
  void ai_chat_voice_set_ui(HWND panel, AiVoiceUi st, const char *cue_utf8); /* button look, pulse, cue */
  void ai_chat_voice_status(HWND panel, const char *text_utf8, int is_error);
  /* Runs voice_deliver against the box and acts: SEND -> SetWindowText + ai_chat_send_user_message
   * (exactly the chips' path, ai_chat.c 729-744); FILL/APPEND -> SetWindowText, caret at the
   * end, focus to the box; NOTHING -> "Didn't catch that". If the panel is busy at this
   * moment, SEND degrades to FILL with "Assistant is busy: your words are in the box". */
  void ai_chat_voice_deliver(HWND panel, const char *transcript_utf8, int review);
  ```
  `AiChatData` gains `unsigned conv_gen` (bumped by New Chat and wherever `conv` is
  reset), `HWND hMicBtn`, `AiVoiceUi voice_ui`, `char voice_cue[128]`, and the panel's
  own motion: `NsAnimList anim` + `TIMER_MOTION` (8, 16 ms, killed when
  `ns_anim_list_step` returns 0 and in `WM_DESTROY`) and `TIMER_VOICE_STATUS` (9).
- Produces (`src/ui/voice_win.h`): `int voice_pump_filter(const MSG *m);` (1 = consumed),
  `void voice_on_deactivate(void);`, `void voice_on_timer(void);`,
  `void voice_on_stt(LPARAM);`, `void voice_mic_clicked(void);`, `#define
  VOICE_TIMER_ID` (a main-window timer id not used in window.c).
- Wiring rules:
  - `pump_dispatch` calls the filter through a function pointer that is NULL while the
    feature is stopped: `if (g_voice_filter && g_voice_filter(&msg)) return;` — so with
    voice off the hook is not installed. `voice_feature_apply` sets and clears it.
  - The filter: `voice_ptt_classify(m->message, m->wParam, m->lParam,
    settings.voice_ptt_key)`; NONE and REPEAT -> 0 (REPEAT of F8/F9 consumed when the
    machine is not idle). Map to `VoiceEvent`, `in_panel = ai_chat_focus_role() !=
    NULL`, `busy = ai_chat_voice_busy(g_hwndAiChat)` (1 when there is no panel), step,
    act, re-arm `VOICE_TIMER_ID` one-shot from `voice_ptt_next_deadline`. Consume:
    Right Ctrl never; F8/F9 when the step changed state or the machine is not idle;
    Escape when `consume`.
  - Actions: `VA_OPEN` -> new token, store the binding, `voice_audio_capture_begin(
    settings.voice_input_device, token, 0)`, UI RECORDING with `voice_cue_text`;
    `VA_FINISH` -> `capture_end(1)`, UI TRANSCRIBING; `VA_DISCARD` -> `capture_end(0)`,
    UI IDLE; `VA_BUSY` -> `ai_chat_voice_status(panel, "Assistant is busy", 0)`.
  - `VAM_CAPTURED` with the current token: `PCM_EMPTY`/`PCM_SILENT` -> "Didn't catch
    that", `PCM_ALL_ZERO` -> "Windows may be blocking the microphone: Settings > Privacy
    & security > Microphone" (error row), else `pcm_to_wav` + `voice_stt_submit`; the PCM
    is freed (wiped) either way. `VAM_ERROR` during a recording -> machine reset to idle,
    UI IDLE, error row: `VAE_NO_DEVICE` "No microphone was found. Connect one, or choose
    another in Settings > Voice."; `VAE_DEVICE_LOST` "The microphone stopped responding
    (was it unplugged?). Nothing was sent."; `VAE_OPEN_FAILED`/`VAE_COM_FAILED` "The
    microphone could not be opened."
  - `WM_VOICE_STT` with the current token: binding differs (or no panel) -> status
    "Voice message dropped: the chat changed"; `!ok` -> error row with `err`; else
    `ai_chat_voice_deliver(panel, text, settings.voice_review)`. Stale tokens are freed
    and ignored. UI back to IDLE in every case.
  - `WM_ACTIVATEAPP` (wParam FALSE) and `WM_ENABLE` (wParam FALSE) on the main window and
    the undocked panel -> `voice_on_deactivate()` (VE_DEACTIVATE).
  - The mic button: owner-drawn, left of Send, the same size as Send, drawn with
    `ns_draw` primitives (a microphone outline: capsule, arc, stem; no emoji font) in
    `text_dim` at rest and `ns_hover` hover; RECORDING fills with the `danger` intent and
    pulses (alpha between `danger.base` and the composer surface via `rgb_alpha`, a
    1 s ease cycle restarted by the panel timer; solid when reduced motion is on);
    TRANSCRIBING draws three dots in `text_dim` and the button is disabled; disabled
    while `ai_chat_voice_busy`. Click -> `voice_mic_clicked()` (VE_MIC_CLICK). Tooltip:
    "Talk to the assistant (Right Ctrl)" with the configured key.
  - The cue: while `voice_cue[0]` and the input is empty, the subclass paints it after
    the default `WM_PAINT` at the edit's formatting rect (`EM_GETRECT`) with the input's
    font in `text_dim`. Transcribing shows "Transcribing…". (A multi-line EDIT has no
    `EM_SETCUEBANNER`; this is the input placeholder the spec asks for.)
  - `voice_feature_apply` off (and the main window's `WM_DESTROY`): clear the filter,
    kill the timer, discard any capture, `voice_stt_cancel_all()`, `voice_audio_stop()`,
    `ai_chat_voice_attach(panel, 0)`, gate off. On: start audio, set the filter,
    attach the button if the panel exists; `create_ai_chat` attaches it when
    `voice_feature_running()`.
- [ ] **Step 1: Write the failing wintest** `tests/test_voice_ptt_win.c`
  (`test_voice_ptt_posted_keys`): create a window, `PostMessage` WM_KEYDOWN VK_CONTROL
  with lParam `0x011D0001` (scan 0x1D, extended), pull it with `GetMessage`, classify and
  step at injected times (delay 150) -> OPEN at 150, then WM_KEYUP `0xC11D0001` ->
  FINISH; the same with lParam `0x001D0001` (Left Ctrl) -> no OPEN; a posted
  WM_LBUTTONDOWN between -> DISCARD. Register it; add `src/core/voice_ptt.c`.
- [ ] **Step 2: Run** `xvfb-run -a make wintest` — FAIL; then implement.
- [ ] **Step 3: Implement** the panel functions, the controller and window.c wiring.
- [ ] **Step 4: Run** `xvfb-run -a make wintest`, `make -j test`, `make -j release`,
  `make lint-gate`, and the RGB/scale gates (`make test` includes
  `test_ui_tokens_colour_gate`/`scale_gate`).
- [ ] **Step 5: Commit** `feat(voice): push-to-talk and the mic button in the AI panel`.

### Task 9: Demos, shortcuts, help, the harness and the notes

**Files:**
- Modify: `src/core/ui_demo.c` (`STATE_NAMES` 20-25, `ui_demo_build` 686-740),
  `tests/test_ui_demo.c` (count 24 at 44 and the anchors 45-59), `src/ui/window.c`
  (`create_demo_session` 2522+, the modal-state posts 2675-2687 and handlers 5362-5372,
  a new `WM_OPEN_DEMO_SETTINGS_VOICE` at the next free `WM_USER` id), `src/ui/settings.c`
  (a `settings_dlg_show_demo_page(HWND, int page)` or reuse of the debug one),
  `src/core/shortcuts.[ch]` (S5 rows 59-75; a new flag `SC_VOICE` and
  `shortcuts_filtered_ex(out, cap, ai_on, voice_on)`; the existing `shortcuts_filtered(out,
  cap, ai_on)` keeps its callers and means `_ex(out, cap, ai_on, ai_on)`), `src/ui/cheatsheet_dlg.c:388` (`_ex(..., ai_gate_on(),
  voice_gate_on())`), `tests/test_shortcuts.c`, `README.md` (the generated block via the
  recipe at `tests/test_shortcuts.c:312-334`, plus a short "Voice input" paragraph in the
  AI section), `src/core/ai_panel_states.[ch]` and the empty-state painter
  (`chat_listview.c` 2850/3034), `tests/integration/cases/30-ui-demo.ps1:39`,
  `tests/integration/cases/50-window.ps1`, `tests/integration/cases/70-cli.ps1`
  (320-345), `tests/integration/README.md`,
  `docs/superpowers/specs/2026-09-06-ui-redesign-notes.md`

**Interfaces / content:**
- Demo states `voice-recording` and `voice-transcribing` (both join `all`; they show
  the panel with the mic button in that state and the cue, using
  `voice_gate_set_demo_override(1)` and `ai_chat_voice_attach`/`_set_ui` only — **no
  audio thread, no COM**), and `settings-voice` (modal, not in `all`, like `shortcuts`:
  opens Settings on the Voice page with voice and AI ticked for that run via the demo
  overrides, never written to `settings`).
- Shortcut rows in S5, flags `SC_AI | SC_VOICE`: chord `Right Ctrl (hold)`, action
  `Talk to the assistant: speak, then release to send`, condition `Voice on (F8 or F9,
  or Toggle mode, in Settings > Voice)`; chord `Esc`, action `Cancel a voice
  recording`, condition `While recording`. The registry is compile-time, so the row
  names the default key and the condition names the alternatives (spec §8).
  `shortcuts_filtered_ex` drops `SC_VOICE` rows when `voice_on` is 0. Tests: the
  grammar/markdown/duplicate tests pass; a filter test shows the rows only with both
  on; the README block test passes after regeneration.
- Empty-state hint: `const char *ai_panel_voice_hint(int mode, int ptt_key, char *out,
  size_t cap)` -> "Or hold Right Ctrl and say what you need." (hold) / "Or press Right
  Ctrl, say what you need, and press it again." (toggle), F8/F9 substituted; painted as
  one extra body line only while voice is running. Native test for both wordings.
- Harness (unrun, no `pwsh` here): `$galleryStates` gains `voice-recording`,
  `voice-transcribing`; a `settings_voice_gallery` case modelled on
  `help_shortcuts_sheet_gallery` captures `--ui-demo=settings-voice` on the four themes;
  `self_report_voice_off_clean` in 70-cli.ps1 (AI on, voice off: no `requested:
  mmdevapi.dll`, no `module: mmdevapi.dll`/`audioses.dll`); and
  `voice_off_right_ctrl_reaches_input` (voice off, panel open, posted Right Ctrl down/up
  through `Send-NutshellChord` semantics then a posted `a`: the input reads `a`).
- Notes document: a delivered entry for voice input and an Open item listing every
  Windows check this PR leaves unrun (below, Task 10 Step 6).
- [ ] **Step 1:** failing tests (`test_ui_demo` 27 states and anchors, shortcuts filter,
  voice hint), then implement, regenerate the README block, run `make -j test` and
  `xvfb-run -a make wintest`; under Wine, `xvfb-run -a wine build/win/nutshell.exe
  --ui-demo=voice-recording` must start and close cleanly (`timeout 20`).
- [ ] **Step 2: Commit** `feat(voice): demos, shortcuts, help and harness cases`.

### Task 10: Version, build, full verification and the draft pull request (orchestrator)

- [ ] **Step 1:** `git fetch origin main`; version = one above both `origin/main`'s
  `APP_VERSION` and this branch's; update `src/ui/resource.h` (both macros) and
  README's `**Version**:` line.
- [ ] **Step 2:** `make clean && make release` (on failure: `git restore
  build/win/nutshell.exe` before anything is committed);
  `objdump -p build/win/nutshell.exe | grep "DLL Name"` shows no new DLL.
- [ ] **Step 3:** in the background, one at a time: `make -j test`, `make test SAN=1`,
  `make lint-gate`, `make fuzz-run`, `xvfb-run -a make wintest`, `make selftest-wine`.
  Record each summary line.
- [ ] **Step 4:** line endings: `git diff --stat origin/main...` vs the same with
  `--ignore-cr-at-eol`; fix any flipped CRLF file.
- [ ] **Step 5:** commit the version bump and the exe
  (`build: bump to vX.Y.Z and rebuild nutshell.exe`), reading every `??` line first.
- [ ] **Step 6:** the notes' Open item (Windows, all unrun): a real microphone in Hold
  and Toggle at delays 0/150/400; the privacy-blocked path (Settings > Privacy & security
  > Microphone off gives the privacy message); unplugging a USB/Bluetooth mic mid-recording;
  a Bluetooth headset not dropping to hands-free on a Right Ctrl chord; Right Ctrl chords
  (Ctrl+C/V, Ctrl+=, Ctrl+wheel, Ctrl+letter to the shell) in the terminal and the AI
  input, docked and undocked; AltGr on a European layout; F8/F9; Alt+Tab and a modal
  opening mid-recording; a real STT round trip on OpenAI, Groq, Mistral, Gemini and a
  Custom base URL; "Use current AI assistant" with each chat provider; Settings device
  list with two microphones, hot-plug while the page is open, a saved device unplugged;
  Test and the meter; the mic button's pulse and cue on the four themes and at 125 %/200 %;
  `mingw32-make wintest`; the harness cases `settings_voice_gallery`,
  `self_report_voice_off_clean`, `voice_off_right_ctrl_reaches_input`, the updated
  `settings_help_tool_count` and `ui_gallery` with the two new states.
- [ ] **Step 7:** push, `gh pr create --draft` with the summary, the test lines, the
  unrun checks, the decisions above and the `Models:` line; then the final Opus review.
