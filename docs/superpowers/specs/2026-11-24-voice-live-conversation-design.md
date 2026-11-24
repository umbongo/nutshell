# Live dictation and conversation mode for voice — design

**Status**: DRAFT, for the maintainer's approval. Written 2026-11-24 from the
maintainer's requirements of 2026-11-24 and the research of the same day. It
builds on `2026-11-23-voice-input-output-design.md` (the "voice spec" below),
which stays in force except where this one says otherwise.

How decisions are marked:
- **(maintainer)**: the maintainer's own requirement.
- **(main session)**: the main session's call, recorded in the decisions of
  2026-11-24. The maintainer said "record your decisions and proceed ... any
  decisions that i dont like i change in the next iteration", so each of these
  can be changed.
- **(spec author's call)**: a point the decisions did not cover, chosen while
  writing this spec. Also open to change.
- **(builder's call)**: left to whoever builds it, within the rules here.
- **Verify at build time**: a fact from the research of 2026-11-24 that may
  have changed. Check it against the provider's live documentation on the day.

Review: `claude-fable-5-2` (effort high) reviews this design and the code
**(maintainer)**. Its findings are shown to the maintainer before any is
fixed.

## 1. Intent

The maintainer wants two ways to talk to the AI assistant **(maintainer)**:
"One is dictation mode and the other is conversation mode."

- **Dictation**: "I want to be able to see the words appear via streaming as I
  talk, but then the prompt is only executed once I press the send button."
- **Conversation**: "I press the microphone button and I say, what is my free
  hard drive space? I want the AI to start going through and doing its checks
  and then come back and tell me the result without me having to press
  anything else. Once that's done, I can then ask it something else ...
  instead of having to constantly press the microphone button on and off and
  waiting for it to be processed. If there is a approval step required ...
  then that should still occur as it does today." "we use pauses in the
  conversation to mark whose turn it is to speak." "In conversation mode,
  replies should be read out loud."
- **Cutting in**: "if i want to cut it, it should let me, i can ask it to
  repeat what it said or continue if i want to hear the rest or hear it
  again."
- **Voice**: "be sure theres a setting to specify the accent used, the default
  should be an english lady (queens english)". "there should be a choice to
  either use windows speech or AI speech, the default should be windows
  speech".

What success looks like:
- Dictation: press Right Shift, talk, watch the words arrive in the box, press
  Right Shift again, fix a word, press Send.
- Conversation: click the mic, ask "what is my free hard drive space?", stop
  talking. After the pause the question is sent. The assistant runs its
  checks, stopping at the approval card as today, and reads the answer aloud.
  Ask the next question without touching anything. Talk over the answer to
  cut it short. Say "carry on" or "say that again". Press Right Shift to end.

## 2. Goals, non-goals, anti-goals

**Goals**
- G1. Live dictation: words stream into the input box while the maintainer
  speaks, with OpenAI and Mistral. Nothing is sent until Send **(maintainer)**.
- G2. Conversation mode: open-mic turn taking by pause, each turn sent through
  the same send path as a typed message, every approval unchanged
  **(maintainer)**.
- G3. Spoken replies in conversation mode, with Windows speech (default) or AI
  speech, and an accent setting whose default is an English woman's voice in
  standard British English **(maintainer)**.
- G4. Cutting in while a reply is spoken, and asking for it to be repeated or
  continued **(maintainer)**.
- G5. Dictation stays the default mode; transcripts wait for review by default
  **(maintainer, earlier decision still in force)**.

**What this supersedes in the voice spec, explicitly and only for these
modes** **(maintainer)**:
- Voice spec §2 non-goal "a wake word, continuous listening, or streaming
  (live) transcription": streaming transcription is now a goal in dictation
  mode and conversation mode. Continuous listening is now a goal in
  conversation mode only, under the safeguards of §5.6. A wake word is still a
  non-goal.
- Voice spec §2 anti-goal "a microphone that is open when the maintainer is
  not talking to Nutshell": superseded in conversation mode only. There the
  microphone stays open between turns, and §5.6 lists what keeps that safe.
  Dictation keeps the anti-goal: the device is open only while recording.
- Voice spec §2 non-goal "offline engines (whisper.cpp, SAPI)": SAPI comes
  into scope as the Windows speech engine for spoken replies (§8.2).
  Whisper.cpp stays a non-goal.
- Voice spec §4.4 "the microphone is opened at recording start and closed at
  its end": in conversation mode it is opened at conversation start and closed
  at its end.
- Voice spec §4.1 "Length cap: 60 s": 10 minutes for a live dictation
  recording **(main session)**. The 60 s cap stays for batch.
- Voice spec §4.4 "Windows default means the default device (`eCapture`,
  `eConsole`)" and "the communications role would make Windows duck other
  audio": conversation mode keeps the same device but opens it with the
  communications category for echo cancellation, and opts out of ducking (§7.3).
- Voice spec §4.1 "While the assistant is busy ... voice is unavailable": in
  conversation mode the microphone stays open while the assistant works, but
  speech is not sent (§5.2). Starting a conversation still needs the
  assistant idle.

**Non-goals (this design)**
- A wake word, and listening while Nutshell is closed.
- A global hotkey. The key still works only while focus is in Nutshell's AI
  panel (voice spec §4.1).
- Approving or rejecting cards by voice. Approval stays a click
  **(maintainer)**.
- A spoken "stop" while the assistant works. Speech during "working" is
  ignored **(main session)**; Escape and Stop cancel.
- Spoken replies in dictation mode. The voice spec's speaker toggle (§5) is
  not built here (§15).
- Reading code blocks, tables, raw tool output or URLs aloud **(maintainer,
  voice spec §2)**.
- Live text with Groq, Gemini or a custom URL (§6.4). Gemini Live is later.
- Voice for the terminal itself.

**Anti-goals: failures even if it "works"**
- A spoken turn that runs or writes anything without the approval a typed one
  would get. Voice never builds a conversation turn itself.
- A microphone left open after any end of a conversation (§5.5), or open in
  dictation when not recording.
- Our own spoken reply transcribed and sent back as the maintainer's turn.
- A turn sent to a different chat, tab or host than the conversation started
  in.
- Words lost because the live connection failed. What was said is kept and
  transcribed in one batch instead (§4.5).
- sapi.dll, a speech request or a WebSocket while in dictation mode and not
  recording, or with voice off (§12).
- Audio, transcripts or spoken text written to disk or the debug log without
  the existing content opt-in (`DLOG_CONTENT`).
- Breaking any chord the voice spec protects (Ctrl+C, Ctrl+V, Ctrl+=,
  capitals with Right Shift).

## 3. Constraints from the codebase

Surveyed facts, file:line as of `main` eca4b37 (v1.2.94).

**Capture today**
- One MTA audio thread owns every WASAPI object; the UI thread posts commands
  to it and gets `WM_VOICE_AUDIO` back (`src/ui/voice_audio_win.h:8-18`). It
  starts when voice is saved on (`voice_feature_apply`,
  `src/ui/voice_win.c:188-212`). The device enumerator is created, and
  `mmdevapi.dll` noted in the DLL record, at thread start
  (`src/ui/voice_audio_win.c:435-445`).
- A device is opened per recording in `va_begin`
  (`voice_audio_win.c:242-292`), on the chosen endpoint or the `eCapture`,
  `eConsole` default (`:268-270`), as 16 kHz mono 16-bit with
  `AUTOCONVERTPCM | SRC_DEFAULT_QUALITY` (`:206-222`, `WAV_RATE`).
- The whole recording is buffered (`PcmBuf`, sized `VA_CAP_SAMPLES` from
  `VOICE_CAP_MS` 60000, `voice_audio_win.c:36`, `src/core/voice_config.h`) and
  posted once at the end as `VAM_CAPTURED`. Level reports come every 50 ms
  (`VAM_LEVEL`, `VA_LEVEL_MS`).
- The silence gate judges the **whole** recording (`pcm_classify`, 20 ms
  windows, `PCM_SPEECH_RMS` 250, about -42 dBFS, `src/core/wav.h:33-42`). There
  is no streaming speech detector.
- There is **no render (playback) code**. Voice spec §5 (spoken replies) was
  never built.

**Transcription today**
- One upload thread per recording, multipart through `ai_http_post_ct`, one
  retry on HTTP 400 without language and prompt
  (`src/ui/voice_stt_win.c:72-180`). It checks `voice_gate_on()` before
  sending (`:88`, `:136`). Providers, resolution, prompts and parsing are pure
  code in `src/core/voice_stt.[ch]`.
- Every request goes through `ns_winhttp_open` (`src/core/winhttp_open.c:17-24`),
  which refuses while the AI gate is off. Cancel tokens (`AiNetCancel`) close
  the request handle from another thread (`src/ui/ai_http_win.c:69-80`).
- `ai_http_post_stream` (`src/core/ai_http.h`) already streams a response body
  through a chunk callback, with a cancel token. AI speech can use it as is.
- `build-support/delayload/winhttp.def` lists 13 entry points and no
  WebSocket ones. Delay-load libraries are built from it (`Makefile:76-81`,
  `DELAY_LIBS`).

**The panel and push-to-talk**
- Push-to-talk is a pure state machine (`src/core/voice_ptt.h`: `VoiceState`,
  `VoiceEvent`, `voice_ptt_step`), driven from `pump_dispatch` through
  `g_voice_filter` (`voice_win.h:20-24`). The controller's `act()` opens,
  finishes and discards recordings (`voice_win.c:63-103`).
- Delivery: `voice_deliver` (fill, append or send) and `VoiceBinding` (session,
  session generation, conversation generation) in `voice_ptt.h`;
  `ai_chat_voice_deliver` writes into the box with `EM_REPLACESEL` or sends
  through `ai_chat_send_user_message` (`src/ui/ai_chat_voice.c:174-222`,
  `src/ui/ai_chat_send.c:569`).
- "Busy" is `busy_of`: streaming, dispatching, or a command batch not finished
  (`ai_chat_voice.c:36-45`).
- The input is a plain `EDIT` (`src/ui/ai_chat.c:389`) with a subclass
  (`src/ui/ai_chat_input.c:121`). It is shared by every tab.
- A reply completes in `WM_AI_RESPONSE` (`ai_chat.c:1462`); `wParam == 2` with
  `took_calls` (`:1488`) is a tool turn, without it a final answer.
- Short statuses go to the panel header (`ai_chat_voice_status`,
  `ai_chat_voice.c:156`). The maintainer wants the header status redesigned
  later (notes, todo of 2026-11-24); this design reuses the slot as it is.

**Settings and config**
- The Voice page is built at `src/ui/settings.c:2192-2405`. Microphones are
  listed only once voice has been saved on (`:2196`). The push-to-talk mode
  row's label is "Mode:" (`SH_VOICE_PTT_MODE`, `src/core/settings_help.c:491`).
  The auto-send tick is `IDC_VOICE_AUTO_SEND` (`settings.c:2390`).
- Voice fields: `src/config/config.h:86-102`. Loaded at
  `src/config/loader.c:948-1004`. The migration marker `voice_defaults_rev` is
  read at `loader.c:993` and written as 3 at `:1336`. The keys table
  `k_setting_keys` (`loader.c:484-541`) has 57 keys; `CFG_KEYS_MAX` is 96.
- The upgrade notice folds voice sub-keys into one "Voice" line when the
  switch itself is new, and has a special value for `voice_auto_send`
  (`src/core/notice.c:204-268`). `NOTICE_ITEMS_MAX` is 24.
- Voice is `OPT_VOICE`, which requires `OPT_AI` (`src/core/opt_feature.h`);
  the runtime flag is `voice_gate_on()` (`src/core/voice_gate.h`).

**Reusable pure code**: `src/core/base64.c` (audio frames as base64 JSON),
`src/core/md_blocks.c` and `md_table.c` (finding code blocks and tables for
the speakable-text extractor), `json_escape_string` for message bodies.

**Tests of the exe**: `make selftest-wine` runs the real exe three times (AI
off, voice off, voice on) and checks the DLLs it requested
(`Makefile:432-465`, `tests/selftest/check.sh`).

**Open item this fixes**: notes item (u)
(`docs/superpowers/specs/2026-09-06-ui-redesign-notes.md:554`): Win+L during
a Toggle recording may not end it.

## 4. Dictation mode (live)

Dictation is the default mode **(maintainer)**. It is today's push-to-talk
with the words arriving as they are spoken.

### 4.1 Starting

- Press the voice key or the mic button to start, press again to stop. Toggle
  stays the default; Hold to talk still works **(main session)**. Every rule
  of voice spec §4.1 stays: focus in the AI panel, chord safety, the Mic start
  delay in Hold, the busy gate, the short-tap discard in Hold.
- **Live** applies when the effective speech-to-text provider is OpenAI or
  Mistral **(main session)**. With Groq, Gemini or a custom URL dictation
  behaves as today: the text arrives after stopping.
- At start the controller opens the audio device as today and, when live,
  opens one WebSocket to the provider (§6.5) **(main session)**. Audio
  captured before the connection is ready is held and sent once it is.

### 4.2 The words in the box

- Words go into the input box after any existing draft, separated by one
  space, with the caret at the end **(main session)**.
- **Committed text and a revisable tail** **(main session)**. The box holds:
  the draft as it was at start, then the recording's committed text, then a
  tail that may still change.
  - OpenAI deltas may revise earlier partial text: the tail is replaced on
    each update, and becomes committed when the provider marks the segment
    complete.
  - Mistral's deltas only append: they go straight to committed text.
  - The controller replaces the range from the end of the committed text to
    the end of the box (`EM_SETSEL` + `EM_REPLACESEL`) and puts the caret at
    the end. The buffer logic is pure code (§14).
- **Typing is paused** while a live recording runs **(main session)**. The
  header cue says so: "Listening… press Right Shift again to finish. Typing is
  paused." Editing comes back when the mic stops. How it is blocked (swallowing
  `WM_CHAR`, paste and cut in the subclass, or `EM_SETREADONLY`) is the
  **builder's call**; a read-only `EDIT` repaints with the static colour, so
  the subclass route is likely the better fit for the theme.
- Clicks in the box during a live recording do not move where text goes:
  every update re-sets the selection to the end **(spec author's call)**.

### 4.3 Sending

- Nothing is sent until Send or Enter is pressed **(maintainer)**.
- **Send (or Enter) while recording** **(main session)**: the mic stops, the
  controller waits for the final text, then sends what the box holds through
  `ai_chat_send_user_message`.
  - The wait is at most 3 s **(spec author's call)**.
  - On timeout nothing is sent. The box keeps what arrived and the status says
    "The last words did not arrive. Check the text and press Send."
    **(spec author's call)**: dictation exists so nothing goes unreviewed.
- **"Send automatically when I finish speaking"** keeps working in PR 1: with
  it on, the final text is sent when the mic stops, provided the box was empty
  at start, as today's `VD_SEND` rule **(main session)**. PR 2 removes the
  tick (§11.2).

### 4.4 Every way a dictation recording ends

| How | Mic | Words already in the box | Sent? |
|---|---|---|---|
| Key or mic button again (Toggle), key up (Hold) | closed | kept, final text added | only with auto-send on (PR 1) |
| Send or Enter | closed | kept, final text added | yes, after the final text (§4.3) |
| Escape | closed | this recording's words removed; the draft is as it was | no |
| The 10-minute cap | closed | kept, final text added | as for a key press |
| Nutshell loses activation, or a modal dialog opens | closed | kept | no, even with auto-send on |
| Tab change, New chat, or the session closes | closed | kept | no, even with auto-send on |
| Session lock (Win+L), PR 2 onwards | closed | kept | no |
| Voice or AI switched off | closed at once | kept | no |
| Device lost | closed | kept | no; error shown |

Notes:
- Escape removes the recording's words **(spec author's call)**. It is the one
  deliberate cancel, as Escape discards a recording today. Every other end
  keeps what is in the box, because those words are already on screen and a
  draft is never thrown away by an accident such as Alt+Tab.
- The chat-change row replaces voice spec §4.2 step 3 for live recordings:
  the box is shared by every tab, so the words stay, but nothing is sent
  **(spec author's call)**.
- At the cap the recording ends as if the key had been pressed
  **(spec author's call)**, as at today's 60 s cap.

### 4.5 When the live connection fails

- If the connection fails to open, or drops mid-recording, capture carries on
  **(main session)**. The whole recording's audio is kept, as today.
- When the mic stops, one batch transcription of the whole recording is made
  with the provider's batch model (`voice_stt_model`) **(main session)**. Its
  result replaces this recording's live text in the box, so nothing is doubled
  **(spec author's call)**.
- Status at the failure: "Live text stopped: the rest will appear when you
  stop" (or "Live text is unavailable: the words will appear when you stop" if
  it never opened) **(spec author's call)**.
- A 10-minute recording at 16 kHz is 19.2 MB of WAV, under OpenAI's 25 MB
  upload limit. Verify at build time: Mistral's batch limit, and whether
  `ai_http_post_ct`'s 30 s send and 60 s receive timeouts suit such an upload
  (**builder's call** to scale them by length).

### 4.6 Language

- OpenAI live gets the ISO 639-1 code from the existing language setting in
  its `languages` list, plus the existing British-spelling sample text as the
  prompt for English (UK) **(main session)**. Verify at build time whether the
  live model takes a prompt at all; if not, the British spelling cannot be
  asked for there, and the spec accepts that.
- Mistral gets the two-letter `language` **(main session)**.
- Auto-detect sends no language, as today.

## 5. Conversation mode

### 5.1 Choosing and starting it

- Settings > Voice gains "Mode: Dictation / Conversation", default Dictation
  **(main session)**.
- In Conversation mode, pressing the key or the mic button starts a
  conversation; pressing again ends it **(main session)**.
- The key always toggles in Conversation mode. Hold to talk and the Mic start
  delay apply to Dictation only, and the Settings page greys them in
  Conversation mode **(spec author's call)**: a conversation lasts many turns,
  and holding a key through it would defeat the point.
- A conversation is bound to the tab and chat that were showing when it
  started (`VoiceBinding`).
- It cannot start while the assistant is busy, or with no session, with
  today's statuses (`voice_busy_text`).
- **A draft in the box** at start becomes the beginning of the first turn: the
  first turn sends the draft, a space, then the spoken words. The cue says
  "Your text and what you say next will be sent after a pause."
  **(spec author's call)**. Later turns start from an empty box.
- The box does not accept typing for the whole conversation **(spec author's
  call)**. To type, end the conversation.

### 5.2 The turn cycle

A pure state machine in `src/core` drives it, with time injected, like
`voice_ptt` **(spec author's call)**. Its states:

1. **Listening**: the mic is open, no speech yet. Header: "Listening… speak,
   then pause to send".
2. **Hearing**: speech has started. With a live provider the words stream into
   the box as in dictation (§4.2). With a batch provider the audio is kept for
   upload and the box shows nothing yet. Header: "Hearing you…".
3. **Ending the turn**: the pause is reached (§5.3). With a live provider the
   controller asks for the final text (§6) and waits for it, at most 3 s
   **(spec author's call)**. With a batch provider the turn's audio is uploaded
   as today. Header: "Sending…" or "Transcribing…".
4. **Working**: the turn has been sent with `ai_chat_send_user_message`,
   exactly as a typed message. The mic stays open but speech is not sent
   **(main session)**. Header: "Working… press Escape to stop".
   - An approval card is announced aloud, "I need your approval to run a
     command" (for a file write, "I need your approval to write a file"), and
     answered on screen as today **(main session; second phrase spec
     author's call)**. Header: "Waiting for your approval".
   - Intermediate prose that comes with a tool turn is not read
     **(main session: the final answer only)**.
5. **Speaking**: the final answer's prose is read aloud (§8). Speech from the
   maintainer here is a cut-in (§9). Header: "Speaking… talk to cut in".
6. Back to **Listening** when the reply has been read, cut short, or had
   nothing to read.

Other rules:
- An empty or whitespace transcript sends nothing, says "Didn't catch that",
  and goes back to Listening.
- A transcript that matches what was just spoken is dropped silently as echo
  (§9.4) and goes back to Listening.
- A reply that ends in an error is not read in full. A short phrase is spoken,
  "Something went wrong. The details are on screen.", and listening resumes
  **(spec author's call)**.
- A final answer is a `WM_AI_RESPONSE` with `wParam == 2` for the
  conversation's session and no tool calls (`ai_chat.c:1488`).
- One turn is capped at 2 minutes of speech; at the cap it is ended as if the
  pause had been reached **(spec author's call)**.
- Send pressed while Listening or Hearing ends the turn at once, as if the
  pause had been reached **(spec author's call)**.

### 5.3 The pause

- Turn taking is by pause, detected in Nutshell for every provider, so one
  setting means the same thing everywhere **(main session)**. Provider-side
  turn detection (Mistral's) is not used for this.
- "Pause before sending": default 1.2 s, range 0.5 to 3 s **(main session)**.
- The endpointer works on the gained 20 ms windows the silence gate already
  uses (§7.2). Speech starts after a short run of windows above the speech
  threshold; the pause is measured from the last speech window. A turn needs
  at least about 300 ms of speech in total, so a cough is not a turn
  (`VOICE_SHORT_TAP_MS`). The exact run lengths are the **builder's call**,
  tested in `src/core`.

### 5.4 With a batch provider

Conversation mode works with Groq, Gemini and a custom URL too, since the
pause is found in Nutshell **(spec author's call, following the main
session's "for every provider")**. Each turn's audio is uploaded at the pause
through today's path, so a turn takes a little longer and no words appear
while speaking. The Voice page says "Live text needs OpenAI or Mistral".

### 5.5 Every way a conversation ends

| How | Assistant's work | Speech | Status |
|---|---|---|---|
| Key or mic button again | left running; its reply is not read | stopped | none |
| Escape, or the panel's Stop | cancelled as today | stopped | none |
| Closing the panel | as today | stopped | none |
| Voice or AI switched off | as today for AI off | stopped | none |
| Session lock (Win+L) | left running | stopped | "Conversation ended: Windows was locked" |
| 3 minutes with no speech | left running | stopped | "Conversation ended: nothing heard for 3 minutes" |
| Tab change, New chat, or the session closes | as today | stopped | "Conversation ended: the chat changed" |
| A modal dialog opens | left running | stopped | "Conversation ended: a dialog opened" |
| Device lost | left running | stopped | the device error |
| Speech output cannot start | left running | n/a | §13 |

- Every end closes the microphone at once, stops playback, cancels any speech
  request and closes the WebSocket (§12).
- Words in the box from an unfinished turn stay there and are not sent
  **(spec author's call)**, as in dictation.
- The first six rows are **(main session)**. The tab-change and modal rows are
  **(spec author's call)**: a turn must never reach another chat, and a modal
  (Settings, a password prompt) is outside the panel, as voice spec §4.1
  already treats it.
- **Losing activation (Alt+Tab) does not end a conversation**
  **(spec author's call)**. The maintainer may look at another window while
  the assistant works and still hear the answer. The listed safeguards cover
  the open mic. This differs from dictation, where losing activation still
  stops the recording.
- **The 3-minute clock** counts time since the last speech or the end of the
  last spoken reply. It is paused while the assistant streams or a command
  runs, and runs while an approval card waits **(spec author's call)**.

### 5.6 The open-mic safeguards

These are the conditions under which the voice spec's anti-goal is
superseded **(main session)**:
- The mic button and the header show a clear Listening state for the whole
  conversation. The mic button keeps the recording look (`danger` fill) from
  start to end; its pulse follows the input level so it is visibly alive
  (**builder's call** within `ns_motion`).
- It ends on a second press, Escape, closing the panel, voice or AI off,
  session lock, and 3 minutes without speech (§5.5).
- Only speech is streamed (§7.2); silence never leaves the machine.
- Nothing is recorded to disk; audio buffers are wiped with `secure_zero`.

## 6. Providers and protocols

### 6.1 Who supports what

| Provider | Live dictation | Conversation | AI speech |
|---|---|---|---|
| OpenAI | yes, WebSocket | live | yes (`gpt-4o-mini-tts` or successor) |
| Mistral | yes, WebSocket | live | no (Voxtral TTS later) |
| Groq | no, batch after stop | batch per turn | no |
| Gemini | no (Gemini Live later) | batch per turn | no |
| Custom | no | batch per turn | no |

"Provider" here is the effective speech-to-text provider: the Voice page's, or
the AI assistant's when "Use current AI assistant" is ticked (`voice_stt_resolve`).
Windows speech works with any of them.

### 6.2 OpenAI live transcription

From the research; **verify every name at build time**:
- `wss://api.openai.com/v1/realtime?intent=transcription`, with
  `Authorization: Bearer <key>`. The plain API key works; no ephemeral token is
  needed for a desktop client.
- Model `gpt-live-transcribe`. Default for the new Live model setting (§11.1).
- Audio: 24 kHz mono PCM16, base64 in `input_audio_buffer.append` messages.
  Nutshell captures at 16 kHz and upsamples to 24 kHz for this provider only
  **(spec author's call)**, so every other path (gate, meter, WAV, fallback)
  stays at 16 kHz. The resampler is pure code in `src/core`.
- No server turn detection on this model: `turn_detection` is null and the
  client commits (`input_audio_buffer.commit`). Nutshell commits when the
  speech gate closes after its hangover (§7.2), at the pause in conversation
  mode, and at stop **(spec author's call)**.
- `languages`: an array of ISO 639-1 codes. There is no `en-GB`: send `en`
  plus the British-spelling prompt (§4.6).
- Deltas may revise earlier partial text (§4.2). The `delay` setting trades
  latency for accuracy: **builder's call**, starting from the documented
  default.
- Reported price about $0.017 a minute, against $0.003 for batch
  `gpt-4o-mini-transcribe`. Only speech is streamed, which keeps this down.

### 6.3 Mistral live transcription

From the research; **verify every name at build time**:
- `wss://api.mistral.ai/v1/audio/transcriptions/realtime?model=voxtral-mini-transcribe-realtime-3`,
  Bearer key.
- Audio: `pcm_s16le` at 16 kHz (the default), so no resampling.
- Deltas only append (`transcription.text.delta`); `transcription.done` ends.
- Server turn events (`turn.end` and the like, `min_silence_ms`) exist and are
  not used for turn taking (§5.3).
- `language`: the two-letter code. `target_streaming_delay_ms`: 480
  recommended.
- About $0.006 a minute.
- Open questions for the builder (§16): whether audio goes as binary frames or
  base64 JSON; how to get a segment's final text without closing the session;
  whether gaps in the audio (only speech is sent) are accepted.

### 6.4 Batch providers

Groq, Gemini and custom URLs have no live API **(main session)**. They keep
today's request shapes (voice spec §6). Rolling re-transcription as a
pseudo-live mode is a later item (§15).

### 6.5 The WebSocket transport

- WinHTTP's WebSocket API: open a request, set
  `WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET`, send, receive the response, then
  `WinHttpWebSocketCompleteUpgrade`, `WinHttpWebSocketSend`,
  `WinHttpWebSocketReceive`, `WinHttpWebSocketClose` **(main session)**. WinHTTP
  does the handshake and the framing, so there is no hand-written frame code.
- Delay-loaded like the rest of WinHTTP: the new entry points
  (`WinHttpWebSocketCompleteUpgrade`, `WinHttpWebSocketSend`,
  `WinHttpWebSocketReceive`, `WinHttpWebSocketClose`,
  `WinHttpWebSocketQueryCloseStatus`, and `WinHttpWebSocketShutdown` if used)
  go into `build-support/delayload/winhttp.def`.
- **One opener**: a single `voice_ws_open` in `src/ui` that refuses unless
  `voice_gate_on()`, and gets its session from `ns_winhttp_open`, the existing
  network choke point (TLS 1.2 or newer, AI gate) **(main session: through the
  existing single network gate)**.
- One connection per recording (dictation) or per turn (conversation),
  opened at start and really cancelled at the end: the handle is armed on an
  `AiNetCancel` and closed from the UI thread on stop, cancel, voice off or AI
  off **(main session)**. In conversation mode a turn's connection opens at
  the first speech of the turn; pre-roll and the audio until it is ready are
  held locally **(spec author's call)**. Keeping one connection across turns
  instead is the **builder's call** if latency needs it.
- Threads: a sender fed from the audio thread through a locked queue, and a
  receiver blocked in `WinHttpWebSocketReceive`. One thread with WinHTTP's
  async mode instead is the **builder's call**. Events reach the UI thread as
  a new `WM_VOICE_LIVE` message whose payload the receiver frees.
- Provider messages are parsed in `src/core` into a small event set: text to
  append, tail to replace, segment complete, session ready, error, closed.

## 7. The audio pipeline

### 7.1 What stays

- One audio thread owns every audio COM object. The UI thread never touches
  one.
- 16 kHz mono 16-bit capture with auto-convert, software gain before
  everything else, the level meter and the Settings Test.
- Dictation in batch mode is unchanged: device open per recording, 60 s cap,
  one WAV upload.

### 7.2 The streaming speech gate

- Only speech is streamed **(main session)**. A gate on the gained 20 ms
  windows (the threshold of `PCM_SPEECH_RMS`) opens on speech and sends the
  audio from about 300 ms before it (pre-roll), so the first syllable is not
  cut. It closes after a hangover of silence.
- The hangover also flushes the provider: it is at least the provider's
  streaming delay plus a margin, so the last words come back (Mistral's 480
  ms, say). Whether a provider needs continuous audio instead is decided in
  the plan **(main session)**; §16.
- The whole recording is still kept in memory, for the batch fallback (§4.5)
  and for batch providers. A live recording's buffer is sized for 10 minutes
  (19.2 MB), wiped and freed at the end.
- The gate and pre-roll ring are pure code in `src/core`; the audio thread
  feeds frames to the live sender through a locked queue.

### 7.3 Conversation capture

- The device is opened at conversation start and closed at its end
  **(main session)**, not per turn.
- It is opened with the communications category for echo cancellation (§9.1):
  `IAudioClient2_SetClientProperties` with `AudioCategory_Communications`
  before `Initialize` **(main session)**. The device is the same as dictation's
  (the chosen endpoint, else the `eConsole` default) **(spec author's call)**,
  so the maintainer's choice of microphone holds in both modes.
- Windows ducks other audio when a communications stream opens. Nutshell opts
  out (`IAudioSessionControl2_SetDuckingPreference(TRUE)`) **(main session)**.
- The buffer keeps only the current turn (and the pre-roll ring), never the
  whole conversation **(spec author's call)**.

### 7.4 Playback (new)

- WASAPI render in shared mode on the default output (`eRender`, `eConsole`),
  24 kHz mono 16-bit with auto-convert, event-driven, on the audio thread.
  Both Windows speech (rendered to memory, §8.2) and AI speech arrive as 24 kHz
  PCM, so there is one format.
- The render client opens at conversation start and closes at its end; it is
  stopped between replies **(spec author's call)**. Leaving it started and
  silent is the **builder's call** if starting costs too much latency.
- A play queue of PCM chunks, each with a map from its bytes to characters of
  the spoken text (§10). The audio thread reports bytes played to the UI
  (`VAM_PLAYED`, say) so the cut-off point is known.
- A software gain on the render path, for ducking during a cut-in (§9.2).
- Stop is immediate: the queue is dropped and the buffer flushed
  (`IAudioClient_Stop` + `Reset`).

## 8. Speech output

### 8.1 What is read

- The prose of the final answer only, Markdown stripped **(main session)**.
- Code blocks are replaced by "The code is on screen." Tables by "The table is
  on screen." **(main session; second phrase spec author's call)**. Raw tool
  output is not read. Links read their text, never the URL.
- Inline code is read as plain text, emphasis and headings lose their marks
  (voice spec §5).
- Thinking is never read. Cards, tool results and errors are not read (an
  approval is announced, §5.2).
- The extractor is pure code in `src/core`, built on `md_blocks.c`.
- Chunking as in voice spec §5: the first chunk one or two sentences (about
  200 characters) so speech starts quickly, later chunks up to about 1,000
  characters, always at sentence ends. The next chunk is prepared while the
  current one plays.
- The text sent for speech is logged only through `DLOG_CONTENT`.

### 8.2 Windows speech (the default)

- "Speech: Windows speech (default) / AI speech" **(maintainer)**.
- SAPI 5: `CoCreateInstance(CLSID_SpVoice)` at conversation start, on a speech
  thread (MTA) **(main session; thread spec author's call)**. COM maps
  `sapi.dll`; the creation site notes it in the DLL record, as
  `voice_audio_win.c` does for `mmdevapi.dll`.
- Output goes to an in-memory SAPI stream at 24 kHz 16-bit mono, which
  Nutshell plays through its own render client (§7.4) **(main session)**. So
  stop is immediate and the echo reference is our own endpoint.
  `CreateStreamOnHGlobal` (ole32, already linked) under an `ISpStream`
  needs no new DLL.
- Text goes in with `SPF_ASYNC | SPF_IS_NOT_XML` **(spec author's call)**: a
  reply can never steer SAPI with XML tags.
- Word-boundary events give each word's character offset and its audio stream
  offset, which is what §10 needs.
- Stopping: drop the render queue, then purge SAPI (`SPF_PURGEBEFORESPEAK` on
  an empty speak).
- A speed setting (SAPI rate, -10 to 10, default 0) is optional: the
  **builder's call** **(main session)**.

### 8.3 Voice choice and fallback (Windows speech)

- "Windows voice": a drop-down of installed voices, SAPI and OneCore
  **(main session)**.
- **Default** **(main session)**: an English (UK) female voice, Hazel, then
  Susan, then Libby; else any English (UK) voice; else the system default
  voice.
- The first item is "Automatic", showing what it resolves to, e.g.
  "Automatic (Microsoft Hazel)" **(spec author's call)**. Automatic is
  resolved at each conversation start and never written back, because voice
  token IDs are per PC and the config travels (the same reason as the
  microphone ID, voice spec §4.4). A chosen voice is stored by its token ID
  and display name; if it is gone, Automatic is used and the page says so.
- **The list is read from the registry**, not from SAPI
  **(spec author's call)**: the token keys under
  `HKLM\SOFTWARE\Microsoft\Speech\Voices\Tokens` and
  `HKLM\SOFTWARE\Microsoft\Speech_OneCore\Voices\Tokens`, with their
  `Attributes` (`Name`, `Gender`, `Language`, `809` for English (UK)). That is
  what SAPI itself reads, and it means opening Settings never loads
  `sapi.dll`. Natural (Narrator) voices are not usable by apps and are not
  listed. A voice in both lists is shown once (**builder's call** which token
  wins).
- The choice rule is a pure function in `src/core` over that list.
- English (UK) voices exist only where the English (UK) speech pack is
  installed. The Voice page shows one line when none is found: "To add the
  English (UK) voices: Windows Settings > Time & language > Speech > Add
  voices > English (United Kingdom)." (verify the path on Windows 10 and 11).
- **Play sample** speaks "Hello. This is how replies will sound." with the
  chosen engine and voice. It is enabled only in Conversation mode, loads
  `sapi.dll` (or makes one speech request) on the click, and releases it when
  done **(spec author's call)**.

### 8.4 AI speech

- OpenAI `gpt-4o-mini-tts`, or its current successor **(main session)**.
  Verify at build time its retirement date (reported early 2027), its
  successor and its 2,000-token input limit.
- `POST {base}/audio/speech` with JSON `model`, `voice`, `input`,
  `instructions` and `response_format: "pcm"` (24 kHz mono PCM16), streamed
  through `ai_http_post_stream` into the play queue as chunks arrive. Each
  chunk of §8.1 is one request.
- Uses the voice provider's key (§6.1), so it needs the effective
  speech-to-text provider to be OpenAI. Otherwise the choice is disabled with
  the reason "AI speech needs OpenAI as the speech-to-text provider"
  **(main session)**. A custom OpenAI-compatible server is treated as not
  OpenAI here **(spec author's call)**; it is a later item.
- Settings **(main session)**:
  - "AI voice": an editable combo of the preset voices (verify the list at
    build time). Default `coral`, a female-sounding voice that takes accent
    instructions well **(spec author's call of the preset; the main session
    asked for a female-sounding voice suited to British RP)**.
  - "AI speech model": an editable combo, default `gpt-4o-mini-tts`
    **(spec author's call, following voice spec §6's editable-combo rule)**.
  - "Accent and style": free text, default "Speak as an English woman with a
    standard British accent (received pronunciation), clear and natural."
- Bytes played map to characters in proportion within the chunk (§10).
- About $0.015 a minute, reported.

### 8.5 Speech fallback

- AI speech fails for a reply (network, HTTP error, unavailable): the rest of
  that reply is read with Windows speech, and the status says "AI speech
  failed: using Windows speech" **(spec author's call)**. The next reply tries
  AI speech again.
- Windows speech cannot start (no voice, SAPI error): the reply is not read,
  the status says "Could not speak the reply: it is on screen", and listening
  resumes **(spec author's call)**. The conversation does not end.

## 9. Echo cancellation and cutting in

### 9.1 Echo cancellation

- Windows' own echo cancellation, through the communications category on the
  capture stream (§7.3) **(main session)**. No new DLL.
- On Windows 11 (build 22000 and later), `IAudioEffectsManager` (from the
  capture client's `GetService`) says whether acoustic echo cancellation is
  present and on, and can turn it on where the driver allows.
- On build 22621 and later, `IAcousticEchoCancellationControl` sets the
  reference render endpoint to the one Nutshell plays on. The reference is
  that endpoint's loopback, so our own playback is cancelled.
- MinGW's headers may lack these interfaces. Their GUIDs and vtables are
  copied locally, as `voice_audio_win.c` already does for the device
  enumerator (no `libuuid`) **(builder's call)**.
- Where no echo cancellation is reported (Windows 10, some drivers), cutting
  in still works with a raised threshold, and the Voice page says "Your
  microphone reports no echo cancellation: a headset works best"
  **(main session)**. The report is made at the first conversation and kept
  for the Settings page until the next one **(spec author's call)**, so
  Settings itself opens no capture stream.
- A software fallback (speexdsp) for Windows 10 and devices without echo
  cancellation is later (§15).

### 9.2 Cutting in

Two stages while Speaking **(main session)**:
1. **Duck**: sustained voice energy well over the echo estimate for about 200
   ms lowers our playback (a software gain, about -15 dB, **builder's call**).
   The STT connection opens now, so the pre-roll holds the first words.
2. **Stop**: about 400 ms of voice stops the reply. Playback and speech
   synthesis stop at once, any speech request is cancelled, and the cut-off
   point is recorded (§10). The state moves to Hearing.

- If the voice stops before stage 2 (a cough, a word to someone else), the
  reply un-ducks within about 300 ms and carries on **(spec author's call)**.
- The echo estimate: with echo cancellation on, the speech threshold plus a
  margin; without it, a margin over the level we are playing, learned during
  the conversation. Numbers are the **builder's call**, tuned in the hand
  checks.
- The detector and its timings are pure code in `src/core`, fed with levels.

### 9.3 Listening during speaking

The capture stream is never paused while Speaking. Speech that does not reach
stage 2 is not sent.

### 9.4 The echo filter

- A transcript that matches what was just spoken is dropped **(main
  session)**.
- The rule **(spec author's call; thresholds the builder's)**: normalise both
  (lower case, no punctuation, words). A transcript of two or more words is
  echo when at least 80% of its words appear, in order, in the text spoken in
  the last 30 s. A one-word transcript is echo only if it arrived while
  Speaking and that word was spoken in the last 5 s.
- A dropped transcript is logged only through `DLOG_CONTENT`, with no status.
- Pure code in `src/core` with native tests.

## 10. Repeat and continue

- Nutshell records how far the spoken reply got **(main session)**:
  - Windows speech: the word-boundary event of the last word whose audio was
    played.
  - AI speech: bytes played, mapped to characters in proportion within the
    chunk.
- In the next turn after a cut, the assistant is told where it was cut off, so
  "carry on" resumes there and "repeat that" starts again. These are ordinary
  requests to the assistant, not local commands **(main session)**.
- **The note** **(spec author's call)**: one plain sentence sent with the next
  user turn, not shown in the chat as the maintainer's words, for example:
  `[Your last reply was read aloud up to "...the C drive has 212 GB" and the
  user cut in there. The rest was not heard.]` It quotes the last dozen or so
  words heard. It goes with that one turn only, and only after a cut.
- Where it goes in the request (the user turn's text, or the delimited data
  block the prompt builder already adds) is the **builder's call**, provided
  it is never shown as typed text and is logged only through `DLOG_CONTENT`.
- The mapping from bytes played to a character offset, and from an offset to
  the quoted words, is pure code in `src/core`.

## 11. Settings and the upgrade notice

### 11.1 PR 1

- **Live model** **(spec author's call)**: an editable combo under Model,
  shown only when the effective provider is OpenAI or Mistral, seeded with
  `gpt-live-transcribe` or `voxtral-mini-transcribe-realtime-3`. Empty means
  the provider's default. Key `voice_stt_live_model`. Voice spec §6's rule:
  model names are never hard-coded only.
- **The live note**: with Groq, Gemini or a custom URL, a dim line under Model
  says "Live text as you speak needs OpenAI or Mistral" **(main session)**.
- The Mode row's help text says a live recording stops by itself after 10
  minutes and a batch one after 60 seconds.
- Upgrade notice line: "Voice: live speech-to-text model — the provider's
  default".

### 11.2 PR 2

New rows, in this order **(spec author's call)**:
- **Mode** (Dictation / Conversation) directly under Enable voice, because it
  decides how everything below works. The existing push-to-talk "Mode:" row is
  renamed "Key mode:" (Toggle / Hold to talk) so the two are not confused.
  Key mode's Hold choice and the Mic start delay are greyed in Conversation
  mode (§5.1).
- A new section **"Conversation"** after Microphone, greyed in Dictation mode:
  - Pause before sending (0.5 to 3.0 s, default 1.2; an edit in tenths or a
    slider, **builder's call** within the design system).
  - Speech (Windows speech / AI speech).
  - Windows voice (drop-down, §8.3), with the English (UK) note when needed.
  - Speed, if the builder adds it.
  - AI voice, AI speech model, Accent and style (multi-line edit). Disabled
    with the reason when AI speech is unavailable (§8.4), and greyed while
    Speech is Windows speech.
  - Play sample.
  - The echo-cancellation note when the last conversation found none (§9.1).
- "Send automatically when I finish speaking" is removed **(main session)**.

New keys (`Settings`, loaded and saved in `loader.c`, each with a
`settings_help` entry and a `k_setting_keys` row):

| Key | Default | Notice line |
|---|---|---|
| `voice_mode` | `"dictation"` | Voice: mode — Dictation, so your words wait in the box for you to press Send |
| `voice_pause_ms` | 1200 | Voice: pause before sending — 1.2 seconds |
| `voice_speech_engine` | `"windows"` | Voice: speech — Windows speech |
| `voice_windows_voice`, `voice_windows_voice_name` | empty (Automatic) | Voice: Windows voice — Automatic (an English (UK) woman's voice if one is installed) |
| `voice_speech_rate` (only if built) | 0 | Voice: speed — normal |
| `voice_ai_voice` | `"coral"` | Voice: AI voice — coral |
| `voice_ai_tts_model` | empty (`gpt-4o-mini-tts`) | Voice: AI speech model — the provider's default |
| `voice_ai_style` | the text in §8.4 | Voice: accent and style — an English woman with a standard British accent |

The values come from each entry's `default_text`, as the notice does today
(`notice.c:266`). With the voice switch itself new, all of them fold into the
one "Voice" line, as today.

**The auto-send replacement** **(main session; mechanism spec author's
call)**:
- `voice_auto_send` is no longer written and its `k_setting_keys` row is
  removed; the loader still reads it once for this rule.
- `voice_defaults_rev` goes to 4. A config with rev below 4 and
  `voice_auto_send` true gets one notice line, reported like
  `voice_lang_migrated` (`ConfigLoadReport.voice_auto_send_replaced`):
  "Voice: 'Send automatically when I finish speaking' — replaced by
  Conversation mode; your mode is Dictation, so your words wait in the box".
- A config with auto-send on does **not** switch to conversation mode
  **(main session)**: that mode also opens the microphone and speaks.
- With it off, no line: nothing the maintainer used has changed.
- An older exe drops the new keys and writes `voice_auto_send` again; the next
  launch adds them and says so. That is accepted (CLAUDE.md, "Optional
  features").

**Shortcuts** (`src/core/shortcuts.c:71-74`, README block regenerated): the
voice-key row reads "Talk to the assistant: press, speak, press again (in
Conversation mode, press to start and end a conversation)"; the Escape row
reads "Cancel a voice recording or end a conversation".

## 12. Optional features: what loads when

Spoken replies add speakers (a device) and, for AI speech, a network
destination. They are part of the voice feature, not a new optional feature
with its own switch **(main session)**. The rule "not loaded while off"
extends to each mode:

| State | What runs |
|---|---|
| Voice off, or AI off | nothing (as today) |
| Voice on, idle (either mode) | the audio thread and device enumerator (as today) |
| Dictation, batch recording | + the capture device (as today) |
| Dictation, live recording | + the WebSocket (new WinHTTP entry points), its sender and receiver threads |
| Conversation active | + capture with the communications category and echo-cancellation objects, the render client, the speech thread with `SpVoice` (Windows speech), the WebSocket per turn (live providers), one speech request per chunk (AI speech) |
| Any recording or conversation | + the session-lock registration (`wtsapi32.dll`) |
| Settings > Voice open | no `sapi.dll` (voices read from the registry); Play sample loads it on the click only |

- **sapi.dll** is requested only at conversation start with Windows speech,
  and by Play sample **(main session)**. Never in Dictation mode.
- **Session lock**: `WTSRegisterSessionNotification` on the main window,
  through a delay-loaded `wtsapi32.dll` (a new `wtsapi32.def`) **(main
  session)**. Registered when a recording or conversation starts and
  unregistered when it ends **(spec author's call)**, so the DLL is requested
  only once voice is actually used. `WTS_SESSION_LOCK` ends a dictation
  recording (closing notes item (u)) and a conversation.
- **One gate per entry point**: `voice_ws_open` (§6.5), the existing STT
  submit, one speech-request opener and one `SpVoice` constructor, each
  refusing unless `voice_gate_on()`.
- **Teardown at the end of a conversation, and at voice or AI off**, at once:
  - playback stopped, the queue dropped, SAPI purged and `SpVoice` released;
  - any speech request cancelled by closing its handle (`AiNetCancel`);
  - the WebSocket closed the same way;
  - the live and speech threads joined (bounded, as `voice_audio_stop`'s 3 s);
  - the echo-cancellation objects, render client and capture client released,
    the device closed;
  - the session-lock registration removed.
  Windows may keep the DLLs mapped until exit; the claim covers our code.

## 13. Error states and their wording

All in the panel header's status slot, the panel's error style where marked
"error" **(spec author's call for each wording)**:

| When | Text |
|---|---|
| Live connection never opened | Live text is unavailable: the words will appear when you stop |
| Live connection dropped | Live text stopped: the rest will appear when you stop |
| Live key rejected, rate limit, provider error | today's `voice_stt_http_error` texts (error) |
| Batch fallback failed | today's transcription errors (error) |
| Send pressed, final text late | The last words did not arrive. Check the text and press Send. |
| Empty turn | Didn't catch that |
| Conversation ended by lock | Conversation ended: Windows was locked |
| Conversation ended by silence | Conversation ended: nothing heard for 3 minutes |
| Conversation ended by a chat change | Conversation ended: the chat changed |
| Conversation ended by a modal | Conversation ended: a dialog opened |
| AI speech failed | AI speech failed: using Windows speech |
| No speech at all | Could not speak the reply: it is on screen (error) |
| Device lost, privacy block, no microphone | today's texts (error) |
| Start refused | today's `voice_busy_text` |

Settings > Voice lines: "Live text as you speak needs OpenAI or Mistral";
"AI speech needs OpenAI as the speech-to-text provider"; the English (UK)
voices line (§8.3); "Your microphone reports no echo cancellation: a headset
works best"; "The saved voice is not installed on this PC: using Automatic".

Spoken phrases: "I need your approval to run a command."; "I need your
approval to write a file."; "Something went wrong. The details are on
screen."; "The code is on screen."; "The table is on screen."

## 14. Testing

**Native (`src/core`, `make test`)** **(main session's list, plus the
resampler and the state machines)**:
- OpenAI and Mistral event parsers: deltas, revisions, segment complete,
  errors, unknown events ignored, malformed JSON.
- The message builders: session configuration (language, prompt, model), the
  audio append with base64, commit; CR/LF and quote escaping.
- The live-transcript buffer: committed text plus revisable tail, append-only
  mode, the draft prefix and one space, replacing the tail, Escape's removal.
- The 16-to-24 kHz resampler: length, DC, a tone, no overflow.
- The streaming speech gate: pre-roll length, hangover, a cough, steady noise
  under the threshold.
- The pause endpointer at 0.5, 1.2 and 3 s, the 300 ms minimum, the 2-minute
  turn cap.
- The conversation state machine: every state and every end in §5.5, Send in
  Listening and Hearing, the 3-minute clock paused and running as §5.5 says,
  approval announcement, error replies.
- The cut-in detector: duck at about 200 ms, stop at about 400 ms, un-duck
  after a cough, with and without echo cancellation.
- The speakable-text extractor: Markdown stripped, code blocks and tables
  replaced once each, link text without URL, inline code, thinking skipped,
  a reply that is only code.
- The sentence chunker against the 200 and 1,000 character targets.
- The cut-off mapping: SAPI word offsets, proportional bytes, the quoted words
  and the note text.
- The echo filter: exact echo, partial echo, one-word cases, a real answer
  that shares words with the reply.
- The voice choice rule: Hazel, Susan, Libby order, any English (UK), system
  default, duplicates across SAPI and OneCore, a saved voice that is gone.
- Settings: load and save round-trip of every new key, defaults, range clamps,
  `voice_defaults_rev` 4 and the auto-send line (on, off, absent, rev 4
  already), the notice lines and the folding when the switch is new, the
  `k_setting_keys` table against a saved file.
- Shortcuts and README block.

**`wintest` (Wine on `automaton`, and on Windows)**:
- The WebSocket client against a loopback WebSocket server written in the
  test (`ws://127.0.0.1`), if Wine's WinHTTP supports the API; otherwise a
  defined SKIP. Cancelling from another thread must return promptly; on Wine
  this may SKIP like `test_ai_nc_fire_wakes_blocked_winhttp_call`.
- The render client and the communications-category capture: a defined "no
  device" or "no echo cancellation" outcome under headless Wine, no hang, no
  leak.
- The registry voice list against keys the test writes under `HKCU\Software`
  (the reader takes its root keys as parameters).
- `SpVoice` creation: a defined outcome under Wine (it may have no voices).
- Session-lock registration and a posted `WM_WTSSESSION_CHANGE` ending a
  recording.

**The real exe (`make selftest-wine`)**: new runs with voice on in Dictation
mode and in Conversation mode with no conversation started. Both assert that
`sapi.dll` and `wtsapi32.dll` were never requested.

**Integration harness (written, not run here: no `pwsh` on `automaton`)**:
- A debug-only way to feed a WAV file in place of the microphone; none exists
  yet (voice spec §11 left it to the builder).
- Live dictation with a fixture: words appear in the box, nothing is sent
  until Send.
- A conversation with a fixture "list the files in my home directory": the
  turn is sent after the pause and the run_command card waits.
- The self-report case on Windows for Dictation and Conversation modes.

**Windows hand checks before each merge** (into the notes' Open list):
- PR 1: live text with OpenAI and Mistral, revisions settling, typing paused,
  Send while recording, Escape removing the words, Alt+Tab keeping them, the
  connection pulled mid-recording (fallback), a 5-minute recording, English
  (UK) spelling on OpenAI live.
- PR 2: a full conversation with the free-space question and its approval
  card; the default voice on a PC with and without the English (UK) pack; AI
  speech with the default style; cutting in on speakers and on a headset;
  "carry on" and "say that again"; Win+L; 3 minutes of silence; a Bluetooth
  headset (the communications category may move it to its hands-free
  profile); other apps' audio not ducked; the self-report.

## 15. Delivery

Two code pull requests, in order, each with its own version bump and build
**(main session)**. PR 1 is merged before PR 2 starts.

1. **PR 1, "live dictation"** (branch `feat/voice-live-dictation`): this spec;
   §4 (all of dictation); §6.1 to §6.3 and §6.5 (OpenAI and Mistral live, the
   WebSocket transport, `winhttp.def`); §7.1 and §7.2 (the speech gate,
   pre-roll, the 10-minute buffer, the resampler); §11.1; the PR 1 rows of
   §12 and §13; the PR 1 parts of §14. The auto-send tick keeps working. No
   conversation mode, no playback, no `sapi.dll`, no `wtsapi32.dll`.
2. **PR 2, "conversation mode"**: §5; §6.4 for conversation turns; §7.3 and
   §7.4; §8; §9; §10; §11.2 (including the auto-send replacement and
   `voice_defaults_rev` 4); the session lock of §12 for both modes; the rest
   of §12, §13 and §14.

Later, outside this design (todo) **(main session, plus the last three
spec author's)**:
- Gemini Live (`gemini-3.5-transcribe-live`, BidiGenerateContent WebSocket,
  en-GB supported, 10-minute sessions).
- Mistral (Voxtral) TTS as a second AI speech provider.
- speexdsp as a fallback echo canceller for Windows 10 and devices without one.
- Pseudo-live text for Groq and custom URLs by rolling re-transcription.
- Spoken replies in dictation mode (voice spec §5's speaker toggle), built on
  §8 rather than voice spec §5's design.
- AI speech through a custom OpenAI-compatible server.
- A spoken "stop" while the assistant works.

Already filed, not in this design: an Other... choice for the push-to-talk
key, and revisiting the header status design.

## 16. Left to the builder: open questions

1. OpenAI live: the exact event names, how revisions are signalled, whether
   text arrives before a commit, whether the model takes a prompt, and the
   `delay` value.
2. Mistral live: binary or base64 JSON audio; how to get a segment's final
   text without closing the session; whether it accepts gaps in the audio, or
   needs silence sent to flush its delay (§7.2). If it needs continuous
   audio, the plan says so and the "only speech" rule bends for it.
3. Whether closing a WebSocket handle from the UI thread wakes a blocked
   `WinHttpWebSocketReceive` on real Windows (related to the open item on
   `test_ai_nc_fire_wakes_blocked_winhttp_call`).
4. Which MinGW headers lack `IAudioClient2`, `AudioClientProperties`,
   `IAudioEffectsManager` and `IAcousticEchoCancellationControl`, and copying
   what is missing.
5. Whether a OneCore voice token works with `ISpVoice::SetVoice` in a desktop
   app on Windows 10 and 11, and which token wins for a voice in both lists.
6. The thresholds: speech-gate run lengths, hangover, cut-in margins with and
   without echo cancellation, the duck level, the echo filter's numbers.
7. The `gpt-4o-mini-tts` successor and the current preset voice list.
8. The module split (for example `src/core/voice_live.c`, `voice_conv.c`,
   `voice_speak.c`, `voice_echo.c`; `src/ui/voice_ws_win.c`,
   `voice_tts_win.c`) and the thread layout (one or two threads per WebSocket,
   the speech thread).
9. Where the cut-off note goes in the request (§10).
10. Whether the render stream stays started between replies (§7.4), and
    whether one WebSocket should span a conversation's turns (§6.5).
11. Upload timeouts for a long batch fallback (§4.5).
12. How typing is paused in the box (§4.2).
