# Voice input and spoken replies for the AI assistant — design

**Status**: DRAFT, for the maintainer's approval. Written 2026-11-23 from the
brainstorm of 2026-11-22/23. Decisions are marked **(maintainer)** where the
maintainer made them and **(call)** where the author chose and the maintainer
may overrule. Everything else is left to the builder.

Reviewed 2026-11-23 by `claude-fable-5-2` (effort high) as builder and
architecture critic. Its findings are folded in and marked "from review". It
raised one question for the maintainer: whether opening the microphone at
key-down is acceptable. The answer is the Mic start delay, default 150 ms
(§4.1). It corrected one claim about `pump_dispatch` (§3).

## 1. Intent

The maintainer wants to speak to the AI assistant instead of typing, for
example "find me the largest files on the hard drive", "install nginx",
"delete this file". The AI handles a spoken request exactly as it would the
typed one, with its usual tools. The maintainer also wants replies spoken
back when they ask for that.

What success looks like: with Nutshell focused, hold a key, say a request, let
go. The words show up in the chat as the maintainer's message and the
assistant gets on with it. Any command or file write still stops at the same
approval card. With the speaker on, the prose of each reply is read aloud.

## 2. Goals, non-goals, anti-goals

**Goals**
- G1. Push-to-talk speech-to-text (STT) into the AI assistant's chat, through
  the same send path as a typed message.
- G2. A speaker toggle that reads the prose of completed replies aloud via a
  text-to-speech (TTS) service.
- G3. STT and TTS providers are configured separately from the chat provider,
  so a cheaper model can be used **(maintainer)**. Neither is locked to a
  vendor: each has a Custom option with a base URL **(maintainer)**.
- G4. Microphone controls in Settings: input device, push-to-talk key, hold or
  toggle mode, and a test with a level meter.

**Non-goals (this design)**
- A global hotkey, or listening while another application has focus
  **(maintainer)**.
- A wake word, continuous listening, or streaming (live) transcription.
- Approving or rejecting cards by voice. Approval stays a click **(maintainer)**.
- Offline engines (whisper.cpp, SAPI). They can be added later as further
  provider entries (§9).
- Reading code blocks, command output or tables aloud **(maintainer)**.
- Voice for the terminal itself. Voice reaches only the AI assistant.

**Anti-goals: failures even if it "works"**
- A spoken request that runs anything, or writes anything, without the approval
  a typed one would get. Voice must never build a conversation turn itself.
- A microphone that is open when the maintainer is not talking to Nutshell.
- Any audio code, audio DLL or push-to-talk hook active while voice is
  disabled (§7).
- Audio or transcripts written to disk or to the debug log without the
  existing content opt-in.
- Breaking Ctrl shortcuts (Ctrl+C, Ctrl+V, Ctrl+=, control characters to the
  terminal) for the push-to-talk key.

## 3. Constraints from the codebase

These are surveyed facts, file:line as of `main` 4c1a4e7 (v1.2.89).

- Chat providers are `anthropic`, `openai`, `gemini`, `moonshot`, `deepseek`
  and `custom` (`src/ui/settings.c:89-97`, `src/core/ai_prompt.c:2558-2620`).
  Only **one** chat API key is stored (`Settings.ai_api_key`,
  `src/config/config.h:48-56`).
- Of those, only OpenAI and Gemini sell audio APIs, and Custom might.
  Anthropic, DeepSeek and Moonshot have none (research, 2026-11-23).
- HTTP is WinHTTP behind `src/core/ai_http.h` / `src/ui/ai_http_win.c`. It
  sends a byte body and receives a binary body into memory. `Content-Type:
  application/json` is hard-coded (`ai_http_win.c:110,252`), so multipart
  needs a content-type parameter or a sibling function. winhttp.dll is
  delay-loaded; new entry points go in `build-support/delayload/winhttp.def`.
- There is no audio code. WASAPI needs no import library: the device
  enumerator is created through `CoCreateInstance` (`-lole32` is already
  linked), and Windows maps `mmdevapi.dll`/`audioses.dll` only when that call
  is made. (winmm.dll, the older API, is not a KnownDLL and would have needed
  run-time loading.)
- The chat input is `d->hInput` (`src/ui/ai_chat.c:387`, subclass
  `src/ui/ai_chat_input.c:121`). The suggestion chips already do
  `SetWindowText(d->hInput, text); ai_chat_send_user_message(d);`
  (`ai_chat.c:727-741`). Every tool approval sits after that function
  (`ai_chat_stream.c:95`, `ai_chat_dispatch.c:453`).
- A reply completes in the `WM_AI_RESPONSE` handler (`ai_chat.c:1441`,
  `wParam==2`, `rmsg->content`).
- The main message loop's `pump_dispatch()` (`src/ui/window.c:1257`, called
  only from `ui_run`, `window.c:6315`) sees keys before translation, whichever
  window has focus. It is a bare Translate/Dispatch pair today. **Modal loops
  pump for themselves** (`settings.c:2404`, `paste_dlg.c:549`,
  `pw_prompt.c:340`, `cheatsheet_dlg.c:408`, `cmd_detail_dlg.c:588`,
  `help_guide.c:645`, `window.c:3609`), so a hook in `pump_dispatch` misses
  keys while one is open. `ai_chat_focus_role()` (`ai_chat.c:3124`) says
  whether focus is in the AI panel, docked or undocked. Modifiers never reach
  `key_on_keydown` today (`window.c:3702-3713`).
- Ctrl+mouse wheel zooms the chat (`ai_chat.c:2066-2072`,
  `chat_listview.c:4633`). Enter while the assistant is busy means **Stop**
  (`shortcuts.c:62-63`, `ai_chat.c:695-716`).
- The panel has one `hInput` shared by every tab, with no per-session draft.
- `Settings.ai_custom_url` is a **full endpoint** ("for example
  https://host/v1/chat/completions", `settings_help.c:228`), and
  `ai_provider_url()` returns full endpoints too.
- Settings pages: `NAV_TABLE[]` (`src/core/settings_layout.c:12-26`), help
  entries (`src/core/settings_help.c`, checked by `tests/test_settings_help.c`),
  controls built with `make_*2` / `add_ctrl` in `settings.c`. The AI Provider
  page (`settings.c:1352-1410`) is the template for a provider picker,
  including the Custom base-URL row and its clear-text-URL warning.
- Secrets are DPAPI with an `*_enc_preserved` twin (CLAUDE.md, "Secrets").
  Content logging only through `DLOG_CONTENT` (`src/core/dlog.h`).
- Design-system rules (CLAUDE.md) apply to every new pixel: tokens, `ns_scale`,
  `ns_font`, `ns_motion` (one timer per window), `ns_hover`.
- Shortcuts are registered in `src/core/shortcuts.c` (section "AI Assist
  panel"), and README's generated block must match byte for byte.
- Portable logic goes in `src/core` with native tests. `src/ui` is Win32 only.

## 4. Voice input (speech-to-text)

### 4.1 Talking

- **Where it starts:** only while keyboard focus is in the AI assistant panel
  (input, message list or panel, docked or undocked) **(maintainer)**.
- **Key:** Right Shift by default **(maintainer)**. A setting offers Right
  Shift, Right Ctrl, F8 and F9 **(call)**. "Either Ctrl" was dropped after
  review: Left Ctrl is in every editing chord, and AltGr synthesises Left Ctrl.
  Maintainer's decision of 2026-11-23: the default push-to-talk key is Right
  Shift (was Right Ctrl) and the default mode is Toggle (was Hold to talk);
  Right Ctrl, F8 and F9 stay as choices. A config whose voice settings were
  never touched (voice off, Right Ctrl + Hold to talk, no marker: what 1.2.92
  wrote by default) migrates once to Right Shift + Toggle; one with voice on,
  or with any other key or mode, keeps what it has. The marker is the internal
  settings key `voice_defaults_rev` (2), written by every save; it is not in
  the upgrade notice's key list, so its absence alone shows no notice. The
  loader never saves for it (`--ui-demo` and the read-only session list load
  the real config): it applies in memory on every load and reports it
  (`ConfigLoadReport.voice_migrated`); the launch path then saves, silently,
  unless it is demo mode or saving is disabled.
  - **Detecting Right Shift:** a key-down or key-up of `VK_RSHIFT`, or of
    `VK_SHIFT` with scan code 0x36, with the extended bit clear. Left Shift
    (scan 0x2A) never counts, and neither does an extended-flagged Shift: the
    driver's fake shifts around the numpad arrows with NumLock on are not the
    key.
  - **Shift is a chord key:** a capital letter, Shift+click, Shift+Insert,
    Ctrl+Shift+C/V and Shift+arrows all begin with the key down, so a press
    counts only if no other modifier (Left Shift, Ctrl, Alt, Win) was already
    held when the key went down (a chord such as Ctrl+Shift or Win+Shift+S is
    never a press), and the chord-safety rule below applies if any other input
    arrives while the key is held. The chord filter covers all four keys: a
    press with another modifier already held is a chord for every key (so
    Ctrl+F8 or Shift+Right Ctrl does not start a recording).
  - **Never consumed:** like Right Ctrl, Right Shift is never swallowed. Its
    key-down and key-up still reach the terminal or the edit control, so
    capitals and selections work as before.
- **Modes** **(maintainer)**, set in Settings:
  - **Toggle** (default): one press starts recording, the next stops it.
  - **Hold to talk**: recording runs while the key is held.
- **Mic start delay** **(maintainer)**: in Hold to talk, the microphone opens
  only once the key has been held alone for this long. It does not apply to
  Toggle, where a press counts only on its clean release; the Settings page
  disables the field while Toggle is selected. The default is **150 ms** and the range is
  0-400 ms. A shortcut's second key normally follows within about 100 ms, so a
  chord never opens the device: no mic-in-use flash, and no Bluetooth headset
  dropping into its hands-free profile. At 0 the device opens at key-down. The
  device is opened off the UI thread, so a slow `waveInOpen` never stalls the
  window **(call)**.
- **Chord safety** **(call)**: if any other key, mouse button or wheel event
  arrives while the push-to-talk key is held, it was a shortcut (Ctrl+wheel
  zooms the chat). A Hold to talk recording is discarded, never sent, and the
  input behaves as it does today; a Toggle recording carries on (see the
  stop-press chord rule below), and a Toggle press that has not yet started
  one is spent. In Toggle mode a press counts only if it is
  released with no other input in between.
- **Accidental taps:** a hold shorter than about 300 ms in total is discarded
  in Hold mode **(call)**. Auto-repeat key-downs are ignored.
- **Stop-press chord rule (Toggle):** during a Toggle recording, another input
  while the key is down for the stopping press (a capital typed with Right
  Shift) spends that press but keeps the recording; releasing the key returns to
  the latched recording, and a later clean press stops it. Escape, losing focus
  and the cap behave as for any recording. Hold mode is unchanged: another
  input discards.
- **Lost key-up (both Shifts):** Windows sends no key-up for the first Shift
  released while both are down, so the machine can be left with the key
  spent. A real (non-repeat) key-down in that state means the up was lost and
  is treated as a fresh press.  In a spent stop press of a Toggle recording the same real key-down is the
  stopping press again.
- **Silence gate** **(call, from review)**: if the recording's level never
  rises above a speech threshold, it is not uploaded ("Didn't catch that").
  Whisper-family models return phantom phrases such as "Thank you." on silence,
  and an agent with tools must not receive one.
- **Escape** while recording cancels and discards **(call)**.
- **Length cap:** 60 s per recording. At the cap the recording stops and is
  handled as if the key had been released **(call)**.
- **Mic button:** next to Send in the composer **(call)**. Clicking it toggles
  recording in either mode, for mouse users and as the visible affordance.
- **While the assistant is busy** (streaming, a command running, or a card
  waiting): voice is unavailable. The mic button is disabled, and the key shows
  a brief "Assistant is busy" **(call, from review)**. Parking a transcript in
  the box would invite an Enter, which means Stop while busy. The start gate is
  kept in both modes, auto-send on or off: that reason holds either way. The key
  still stops any speech playing (§5).
- **Focus and modal dialogs** **(call)**:
  - If Nutshell loses activation (Alt+Tab, another app) or a modal dialog
    opens, the recording is discarded and the device closed (anti-goal 2).
  - Moving focus elsewhere *inside* Nutshell (the terminal, say) keeps a
    recording going, and the next press of the key or the mic button ends it as
    usual. The key is recognised for ending a recording wherever focus is in
    the main window.
- **The microphone is opened at recording start and closed at its end.**
  Nothing captures between recordings.
- **Language:** a dropdown in Settings, **English (UK) by default**: Auto-detect,
  English (UK), English (US), then Arabic, Chinese, Dutch, French, German,
  Hindi, Italian, Japanese, Korean, Polish, Portuguese, Russian, Spanish,
  Swedish, Turkish, Ukrainian, and **Other...**, which shows a text box for a
  two-letter ISO 639-1 code or the language's English name (`cy` or Welsh),
  with an inline hint that says which format is wanted and what it recognised
  ("Welsh (cy)"). An unrecognised entry is not saved. Auto-detect is weakest on
  one- or two-second commands. The provider gets the ISO code, never the
  region **(maintainer, 2026-11-24)**. A config saved before this (no
  `voice_defaults_rev` of 3) with an empty language becomes English (UK) once;
  a chosen language, and a later deliberate Auto-detect, are never touched.

### 4.2 What happens after you stop

1. The mic button and input placeholder show "Transcribing…".
2. The audio is uploaded to the STT provider on a worker thread. The result is
   posted back to the UI thread.
3. **Session binding** **(call, from review)**: the recording is tied to the
   session (tab) and conversation that were active when it started. If, when
   the transcript returns, a different tab is shown, New chat was pressed, or
   that session has closed, the transcript is discarded with a short status
   ("Voice message dropped: the chat changed"). Short statuses appear in the
   panel header after the session name, not in the bottom strip, where they
   were truncated **(maintainer, 2026-11-24)**. It is never sent to another
   host.
4. If the input box is empty, the transcript is written into `hInput` with the
   caret at the end and the focus in the box, and **not** sent, so the
   maintainer reads it, edits it and sends it with Enter **(maintainer,
   2026-11-23: review by default, auto-send opt-in)**.
   If the box already holds typed text, the transcript is appended after it,
   separated by a space, and **not** sent, so a draft is never overwritten or
   sent half-finished **(call)**.
5. A setting "Send automatically when I finish speaking" (default **off**)
   sends a transcript that arrives in an empty box through
   `ai_chat_send_user_message`, exactly as the suggestion chips do. It appears
   in the chat as the maintainer's own message, and every approval applies
   unchanged. If the assistant has become busy by then, the send is downgraded
   to a fill, with the status "Assistant is busy: your words are in the box"
   **(maintainer, 2026-11-23: review by default, auto-send opt-in)**.
6. An empty or whitespace transcript sends nothing. A short status says
   "Didn't catch that" **(call)**.
7. Errors (no microphone, Windows has blocked microphone access, network,
   provider error, missing key) are shown in the panel's existing error style
   with the next step, e.g. "Windows is blocking the microphone: Settings >
   Privacy & security > Microphone". Nothing is sent.

### 4.3 Recording indicator

While recording, the mic button is filled with the `danger` intent and pulses
gently (driven by the panel's one `ns_motion` timer), and the input placeholder
names the configured key and mode: in the default Toggle mode "Listening…
press Right Shift again to finish", in Hold to talk "release Right Shift to
finish"; with "Send automatically when I finish speaking" on, "to send" in place
of "to finish". A thin live level bar under the input is **left to the builder**.

### 4.4 Capture

- 16 kHz, mono, 16-bit PCM, wrapped as a WAV in memory **(call)**. Every
  provider in §6 accepts it, and 60 s is under 2 MB.
- **WASAPI** in shared mode for capture **(maintainer)**:
  - `IMMDeviceEnumerator` finds the device, and `IAudioClient` +
    `IAudioCaptureClient` capture on an event-driven worker thread.
  - Use `AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY`
    (Windows 10 and later) so the engine delivers 16 kHz mono 16-bit, with no
    resampler of our own.
  - "Windows default" means the **default device** (`eCapture`, `eConsole`)
    **(maintainer)**. The communications role would make Windows duck other
    audio on every press. Playback uses the same role, so input and output
    follow the same choice.
  - A chosen device is stored by its endpoint ID, with its full friendly name
    kept for display. If the ID cannot be opened, or its state is not
    `DEVICE_STATE_ACTIVE` (`GetDevice` succeeds for unplugged endpoints), then
    Windows default is used and Settings says so. The stored ID is **not**
    overwritten by that fallback, because endpoint IDs are per PC and the
    config travels **(from review)**.
  - Device arrival and removal (`IMMNotificationClient`) refreshes an open
    Settings list. Its callbacks arrive on a system thread, so they only
    `PostMessage` and never unregister inside a callback.
  - Removal of the active device ends a recording with an error, never a hang.
    The capture loop waits on the buffer event with a timeout and also on a
    stop event, because the buffer event simply stops firing when the device
    goes, and the error shows only at the next `GetBuffer`.
  - **Microphone privacy:** Windows' privacy block delivers silence, not an
    error. A capture whose samples are all zero reports "Windows may be
    blocking the microphone: Settings > Privacy & security > Microphone"
    instead of "Didn't catch that" **(from review)**.
- **Microphone gain** is a software stage **(maintainer, 2026-11-24)**: each
  captured block is scaled by `voice_mic_gain_db` (-12 to +30 dB, default 0)
  with saturating 16-bit maths, before the silence gate, the WAV encode and the
  level meter, so the meter shows what is sent. It is not the Windows endpoint
  volume, which would change the level for every app.
- The COM work is in `src/ui`, written in C with `COBJMACROS` against
  MinGW's `mmdeviceapi.h` / `audioclient.h`. **One audio thread**, initialised
  for the MTA, owns every audio COM object, including enumeration for
  Settings. The UI thread never touches them; it is only scoped-STA today
  (`settings.c:967`).
- Under Wine on `automaton` there is likely no endpoint, so the tests accept
  `E_NOTFOUND`. Only a Windows hand check counts for real capture.
- The WAV header writer, the PCM buffer and the level computation (peak or RMS
  for the meter) are pure code in `src/core` with native tests.
- Audio buffers are wiped with `secure_zero` and freed after the upload, never
  written to disk. Transcripts are logged only through `DLOG_CONTENT`.

## 5. Spoken replies (text-to-speech)

- **Speaker toggle** in the AI panel header, beside New chat, Save and Undock
  **(maintainer; placement call)**. It is on or off, and the state persists
  across restarts **(call)**.
- **Which replies** **(call, from review)**:
  - Only replies for the tab on screen. A reply completing in a background tab
    is silent.
  - Each completed reply is spoken, including the prose that comes with a
    run_command turn ("I'll check disk usage…") while its card waits, and the
    reply after the commands run. Both are what a reader would see.
  - A reply that completes while a recording is in progress waits and plays
    when the recording ends, unless that recording sends a new message, which
    drops it.
- **What is read:** only the prose **(maintainer)**:
  - Markdown formatting is removed (emphasis, headings, link targets; the link
    text is read).
  - Fenced code blocks, tables and quoted command output are skipped, each
    replaced once by a short spoken marker such as "Code block skipped"
    **(call)**.
  - Inline code is read as plain text **(call)**.
  - Thinking/reasoning is never read. Approval cards, tool results and errors
    are not read.
  - A reply that is only tool calls has no prose and is silent.
- **Chunking for latency** **(call, from review)**: the first chunk is one or
  two sentences (about 200 characters), so speech starts within a second or
  so. Later chunks grow (to about 1,000 characters), always split at sentence
  boundaries and under the provider's limit (OpenAI's is 4,096). The next chunk
  is fetched while the current one plays. `ai_http_post` buffers a whole
  response, and 4,096 characters of 24 kHz PCM is about 13 MB and minutes of
  audio, hence the cap.
- **Stopping:** turning the speaker off, pressing the push-to-talk key (start
  of a new recording), Escape in the panel, New chat, or closing the panel
  stops playback at once and drops the queue **(call)**.
- **Audio format:** request uncompressed PCM or WAV from the provider (OpenAI
  `response_format=wav` or `pcm`; Gemini returns 24 kHz PCM), so there is no
  decoder **(call)**. MP3 decoding is a non-goal.
- **Playback** is WASAPI render in shared mode **(maintainer)**, on the default
  device (`eRender`, `eConsole`), with the same auto-convert flags, on the same
  audio thread as capture.
- **Logging:** the text sent for speech is logged only through `DLOG_CONTENT`.

## 6. Providers

Two independent provider settings, one for STT and one for TTS **(maintainer)**.
Each is built on one code path, the OpenAI audio format, with the named
providers as presets **(call)**:

| Provider | STT | TTS | Notes |
|---|---|---|---|
| OpenAI | `POST /v1/audio/transcriptions`, multipart | `POST /v1/audio/speech`, JSON | Same key as OpenAI chat |
| Groq | `…/openai/v1/audio/transcriptions` | `…/openai/v1/audio/speech` | OpenAI-compatible; cheapest STT found |
| Mistral | `/v1/audio/transcriptions` | (not in v1) | OpenAI-compatible STT |
| Google Gemini | Audio as `input_audio` in the OpenAI-compatible chat completions, with a "transcribe verbatim" instruction | Native `generateContent` with an audio response modality, 24 kHz PCM | Same key as Gemini chat. Different request shapes; see note |
| Custom | `{base}/audio/transcriptions` | `{base}/audio/speech` | Any OpenAI-compatible server, hosted or local **(maintainer)** |

**URLs** **(call, from review)**:
- The voice Custom field is a **base URL** (`https://host/v1`), labelled
  "Base URL" with help text that says so. The chat page's Custom field stays a
  full endpoint. Trailing slashes are trimmed, and a pasted
  `/audio/transcriptions` or `/audio/speech` suffix is stripped, so either form
  works.
- "Use current AI assistant" resolves to the chat provider's audio base:
  - OpenAI: `https://api.openai.com/v1`.
  - Gemini: its own adapter.
  - Custom: the chat URL with a trailing `/chat/completions` removed. If the
    chat URL does not end that way, the tick box is disabled, with the reason
    "Set a separate voice provider: the custom chat URL has no
    /chat/completions to derive from".
- This resolution is one pure function in `src/core`, with tests.

**Language fields per provider** **(maintainer, 2026-11-24)**: the language
goes out as the ISO code. OpenAI-format endpoints get `language`; a
`gpt-transcribe` model gets one `languages[]` part instead (OpenAI documents
`languages` as an array for that model only). Nothing is sent for Auto-detect.
English (UK) also sends a `prompt` of sample British text ("Colour, organise,
centre, travelled, favourite.") to OpenAI, Groq and custom servers. It is
sample text, not an instruction, because Whisper-family models read `prompt` as
the preceding transcript and can echo an instruction **(final review,
2026-11-24)**. The Gemini request, an LLM, instead gets an instruction appended
("Use British English spelling, for example colour, organise and centre.") and
names the language in words ("The language is English."). Mistral
gets no prompt: its `context_bias` is vocabulary biasing, not a spelling
switch.

**One retry** **(final review, 2026-11-24)**: a transcription request that
carried a language or a prompt and got HTTP 400 is sent once more without them
(Gemini: without the language and spelling sentences), then the result or the
error is delivered as usual. No other status retries, nor a cancelled job.
The `languages[]` form follows OpenAI's reference (read 2026-11-24).

Notes:
- Model names and voices are **editable combos** seeded with defaults, never
  hard-coded only. Several audio models change or retire within months (some
  Gemini TTS previews were reported retired on 2026-11-17, and OpenAI's
  `tts-1`/`gpt-4o-mini-tts` reportedly retire in early 2027, unverified).
  Defaults at build time: STT `gpt-4o-mini-transcribe` (OpenAI),
  `whisper-large-v3-turbo` (Groq), `voxtral-mini-latest` (Mistral). TTS
  `gpt-4o-mini-tts` (OpenAI). The builder verifies these against each
  provider's docs on the day.
- Gemini is the only non-OpenAI shape. If it costs disproportionate effort, it
  may move to a follow-up PR, with the provider entry absent rather than
  broken **(call)**.
- ElevenLabs, Deepgram and Azure have their own request shapes and are not in
  this design. They can be added later as presets with their own adapters.

## 7. Settings

A new nav entry under **AI Assistant**: **Voice** **(call)**, between
Behaviour and Web Access.

**Enable voice** (tick box at the top of the page, **off by default**)
**(maintainer)**. It is the security boundary for the whole feature. While it
is off:
- No audio code runs. No `CoCreateInstance` of the device enumerator and no
  device enumeration, so `mmdevapi.dll` and `audioses.dll` are never mapped
  into the process. No capture or render thread exists.
- The push-to-talk hook is not installed. Right Shift, Right Ctrl, F8 and F9 behave exactly
  as they do today.
- The mic button and the speaker toggle are not created.
- No STT or TTS request is ever made.
- The rest of the page is shown disabled, with no device list (listing devices
  would load the audio stack). The keys and other voice settings are kept, but
  not used.

Ticking it in the dialog enables the page's controls. Devices are enumerated
only after it has been **saved** on, so ticking it and pressing Cancel loads
nothing **(from review)**. Until then the device list shows "Save to list
microphones". Saving with it on starts the feature without a restart. On the
first load of a config without the field, it is added as off and named in the
configuration notice (AI switch design, §3). The first-run variant lists
it too.

Saving with it off tears the feature down at once:
- Any recording is discarded and playback stopped.
- In-flight requests are cancelled and their results dropped.
- Threads are joined, COM objects released, and the hook removed.
- The mic button and the speaker toggle are destroyed.

Windows may keep the audio DLLs mapped until the process exits. The design
claims "never loaded" only for a session that started with voice off.

The page then has two titled sections.

**Speech to Text**
- `Use current AI assistant to convert speech to text` (tick box)
  **(maintainer)**. It can be ticked only when the chat provider is OpenAI,
  Gemini or Custom. Otherwise it is disabled and unticked, with a one-line
  reason such as "Anthropic has no speech-to-text API" **(call)**. When ticked,
  STT uses the chat provider's key and base URL, and only the STT model is
  chosen here.
- When unticked: Provider (combo), API key (password edit with reveal), Base
  URL (Custom only, with the existing clear-text warning), Model (editable
  combo).
- Microphone: Input device (combo: "Windows default" plus each active capture
  endpoint by its full friendly name), a **Test** button (a toggle, below) and, on the next row, the zoned
  level meter with its verdict label (below), Push-to-talk key (combo: Right Shift, Right Ctrl, F8, F9; Right
  Shift is first and the default), Mode (Toggle / Hold to talk; Toggle is first
  and the default), Microphone gain (slider, -12 to +30 dB in 1 dB steps,
  default 0 dB, shown as "+12 dB" beside it; software gain in Nutshell only,
  set while watching the Test meter; it takes effect live during a Test, and is
  enabled by the same rule as the device combo) **(maintainer, 2026-11-24)**.
  Mic start delay (0-400 ms, default 150; greyed while Toggle
  is selected, since it applies to Hold to talk only), Send
  automatically when I finish speaking (tick, off) **(maintainer, 2026-11-23:
  review by default, auto-send opt-in)**.
- **The gain slider** is a custom-drawn control (`ns_slider`, `Nutshell_Slider`), not the
  stock trackbar, because the stock trackbar ignores the theme tokens: the track
  is a `raised.base` pill, the filled part `accent.base`, the thumb takes the
  hover surface from `ns_hover`, the focus ring is `tokens->focus`, and it is a
  tab stop that reads Left/Right/Up/Down, PageUp/PageDown, Home and End **(maintainer, 2026-11-24)**.
- **Test is a toggle** **(maintainer, 2026-11-24)**: "Test" starts the
  capture and the live meter, "Stop test" stops it (rules in
  `src/core/voice_test.c`). It also stops when Settings closes, when the page
  is left, when voice is unticked or when the capture fails; changing the input
  device while it runs restarts it on the new microphone, so the maintainer can
  compare microphones. It never uploads anything.
- **The meter row** **(maintainer, 2026-11-24)**: the zoned bar on the left
  (`ns_draw_level_meter`: bands for quiet, good, hot and loud under the bar, the
  fill in the colour of the zone it sits in, a peak-hold tick) and the verdict
  label on the right, coloured dim, amber, green or red by class; the label's
  width is reserved from the widest text, so the bar does not jump. Before a
  Test it is empty and reads "Press Test to check the level".
- **The meter** shows dBFS of the gained signal (`src/core/voice_level.c`):
  fast attack, 20 dB/s release, a peak-hold tick held 1 s, floor -60 dBFS. A
  label classifies the peak of the last 1 s: below -45 "Speak to see the
  level", -45 to -18 "Too quiet — raise the gain", -18 to -6 "Good", -6 to -3
  "Good" (hot, drawn amber), -3 and above "Too loud — lower the gain" (the exact
  texts of `voice_level.c`, with an em dash). The -45 figure is the silence gate's
  speech threshold (RMS 250, about -42 dBFS) with margin: a peak below -45
  cannot pass the gate, so nobody is speaking **(maintainer, 2026-11-24)**.
- **Language** **(maintainer, 2026-11-24)**: a drop-down of the table in 4.1
  (Auto-detect first), then **Other…** (the item reads "Other" followed by an
  ellipsis character; the help text spells it "Other..." in ASCII).
  Picking Other… shows a text box beside the drop-down (31 characters) and
  moves focus to it. The box carries the cue "2-letter code or name, e.g. cy or
  Welsh" (kept visible while the empty box has focus) and a hint row under the Language row says, on every edit, what the
  entry means or which format is wanted ("Welsh (cy)" in the neutral dim colour,
  a format complaint in the warning colour; while the box still holds an
  unrecognised value loaded from the config, a neutral "Saved as pt-BR. ..."
  line instead of the complaint, since Save keeps it). The box and hint are hidden while
  another item is picked. Save writes the list item's id; for Other, the code
  the entry parsed to; an unrecognised entry keeps the language saved before,
  so an untouched legacy value such as `pt-BR` is written back unchanged. A saved
  code that is not in the list reloads as Other… with the box filled. Both the
  drop-down and the box have the page's hover tooltip (`SH_VOICE_LANGUAGE`,
  `SH_VOICE_LANGUAGE_OTHER`). The mapping is `voice_lang_combo_index` and
  `voice_lang_save_value` in `src/core/voice_lang.c`.

**Text to Speech**
- `Use current AI assistant to read replies aloud` (tick box), with the same
  rules as the STT one: OpenAI, Gemini or Custom only **(call, mirroring the
  maintainer's STT rule)**.
- When unticked: Provider, API key, Base URL (Custom only), Model.
- Voice (editable combo seeded per provider), and a **Play sample** button.

**Keys:** two new DPAPI secret fields, `stt_api_key` and `tts_api_key`, each
with its `*_enc_preserved` twin and the same load, save and drop-stale rules as
`ai_api_key` **(call)**. A user who picks the same provider for both enters the
key twice. That is accepted for simplicity.

**Other new settings fields** (in `Settings`, loaded and saved in `loader.c`):
the voice enable flag (default off); STT use-chat flag, provider, base URL,
model, language, input device endpoint ID and display name, key choice, mode,
mic start delay, auto-send flag; TTS use-chat flag, provider, base URL, model,
voice; the speaker on/off state. If the stored endpoint ID has gone, Windows
default is used.

Every control gets a `settings_help` entry, label and help text.

## 8. Shortcuts, help and demos

- Register "Right Shift: talk to the assistant" in the AI Assist panel
  section of `shortcuts.c` (as built: `Right Shift`, "Talk to the assistant:
  press, speak, press again", with the note "Voice on; key and mode in
  Settings > Voice"). The entry follows the configured key and mode if
  the registry allows that; otherwise it names the default. The Settings help
  texts for the key and mode (`settings_help.c`) name Right Shift and Toggle as
  the defaults and list the chords that keep working. Regenerate README's
  block.
- Help > Keyboard Shortcuts and the AI panel's empty-state hint mention it.
- The Listening and Transcribing cue is also shown in the panel header's status
  slot, besides the input placeholder, because a draft in the box hides the
  placeholder **(maintainer, 2026-11-24)**.
- `--ui-demo=header-status` **(maintainer, 2026-11-24)** shows a long status
  note in the header beside the Executing dot, and is added to `ui_gallery`.
- `--ui-demo` states: `voice-recording`, `voice-transcribing`,
  `voice-speaking`, and the Voice settings page. All are added to `ui_gallery`.
- `--ui-demo=settings-voice` **(maintainer, 2026-11-24)** opens Settings on the
  Voice page with AI and voice ticked in memory, no device list and no capture.
  It shows the gain slider at +6 dB and the meter in a fixed Good state (a level
  in the good zone, a hold tick, the label "Good") through a demo-only fill, and
  the Language row on English (UK), so the gallery captures the page without a
  microphone.

## 9. Delivery

Two pull requests **(call)**, each its own version bump and build. Both come
after the AI assistant switch pull request and its notice-dialog follow-up
(AI switch design, opening lines and §3):

1. **Voice input**: the Enable voice switch and its teardown, WASAPI capture,
   STT providers, the Voice settings page (STT section and microphone
   controls), push-to-talk, the mic button, shortcuts and demos. TTS controls
   are absent until PR 2.
2. **Spoken replies**: the TTS section, the speaker toggle, the prose
   extractor, chunking and playback.

Later, outside this design: offline engines (whisper.cpp for STT, SAPI for
TTS) as provider entries; ElevenLabs and Deepgram adapters.

## 10. Testing

- Native tests (`src/core`):
  - WAV header and PCM framing.
  - Multipart body builder: boundary, file part, model field.
  - STT response parsing: OpenAI `{"text":…}`, Gemini chat-completion text.
  - Provider table and URL resolution, including the "use chat provider"
    rules and the disabled-reason text.
  - Push-to-talk state machine: hold, toggle, start delay (0, 150, 400 ms),
    chord cancel by key, mouse button and wheel, short tap, Escape, cap,
    repeat, deactivation, a modal opening, busy, and session change before the
    transcript returns.
  - Silence gate threshold on generated silence, noise and speech-level
    buffers.
  - Voice URL resolution: base URL trimming, suffix stripping, "use current AI
    assistant" per chat provider, and the disabled reasons.
  - Markdown-to-prose extractor: the skip markers, inline code, links.
  - Sentence chunker against limits.
  - Settings load/save round-trip for every new field and both secrets.
- **Voice off, against the real exe** (CLAUDE.md, "Optional features"): the
  self-report shows that our code requested no audio DLL and created no device
  enumerator. A posted Right Shift and a posted Right Ctrl reach the input unchanged.
- `wintest`:
  - WASAPI device enumeration under Wine.
  - The push-to-talk hook driven by posted key messages.
  - Headless Wine on `automaton` may have no audio endpoint, so the capture
    and playback tests assert a **defined "no device" outcome** (a clean error,
    no hang, no leak) when enumeration returns none. They exercise the real
    path only when an endpoint exists.
- The integration harness repeats the voice-off self-report check on Windows,
  with a config that has voice off.
- Integration harness: a debug-only way to feed a WAV file in place of the
  microphone is **left to the builder**. A harness case speaks a fixture
  ("list the files in my home directory"), checks that the message appears and
  that the run_command card waits for approval.
- Windows hand checks before each merge: a real microphone, the privacy-blocked
  path, Right Shift chords (capitals, Shift+click, Shift+Insert, Ctrl+Shift+C/V,
  Shift+arrows) and Right Ctrl chords in the terminal and the AI input, docked
  and undocked,
  and a spoken reply with a code block.

## 11. Left to the builder

- The exact module split (e.g. `src/core/voice_*.c`, `src/ui/voice_*.c`) and
  the WinHTTP content-type change.
- Whether the push-to-talk hook lives in `pump_dispatch` or the panel's
  subclasses, provided the §4.1 focus and modal rules hold (modal loops bypass
  `pump_dispatch`; a modal opening discards the recording).
- The live level bar under the input, visual details within the design system.
- Retry and timeout values for audio requests.
- The debug WAV-injection hook for the harness.
