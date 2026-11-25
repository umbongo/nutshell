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
- **(critique)**: changed after the design critique of 2026-11-24, with the
  main session's disposition (§17). Where a critique change replaces a main
  session decision, it says so.
- **Verify at build time**: a fact from the research of 2026-11-24 that may
  have changed. Check it against the provider's live documentation on the day.

Review: `claude-fable-5-2` (effort high) reviews this design and the code
**(maintainer)**. The design critique of 2026-11-24 is folded in; each
finding and its disposition is in §17. Findings on the code are shown to the
maintainer before any is fixed.

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
  conversation mode only, under the safeguards of §5.7. A wake word is still a
  non-goal.
- Voice spec §2 anti-goal "a microphone that is open when the maintainer is
  not talking to Nutshell": superseded in conversation mode only. There the
  microphone stays open while listening for a turn and while a reply is
  spoken (closed while the assistant works or Nutshell is paused), and §5.7
  lists what keeps that safe.
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
  communications category for echo cancellation. Windows may then lower other
  sounds during a conversation, as its Sound > Communications setting says
  (§7.3) **(critique)**. Dictation does not use that category and never ducks.
- Voice spec §4.1 "While the assistant is busy ... voice is unavailable": in
  conversation mode the conversation carries on while the assistant works,
  with the microphone closed until listening or speaking resumes (§5.2)
  **(critique round 2)**. Starting a conversation still needs the assistant
  idle.
- Voice spec §5 to §7, spoken replies: AI speech keeps that spec's G3, its own
  provider setting (§8.4). What changes: it speaks in conversation mode only,
  not behind a speaker toggle; the providers are Mistral and Custom (an
  OpenAI-compatible `/audio/speech`), with OpenAI not offered (its speech
  models shut down on 2027-01-06), Groq cut (Preview, 100 requests a day) and
  Gemini's speech not in this design; and
  the key is shared with speech-to-text when both use the same provider
  **(critique round 2; provider set amended 2026-11-25, §16 "Decisions of
  2026-11-25: AI speech provider")**.

**Non-goals (this design)**
- A wake word, and listening while Nutshell is closed.
- A global hotkey. The key still works only while focus is in Nutshell's AI
  panel (voice spec §4.1).
- Approving or rejecting cards by voice. Approval stays a click
  **(maintainer)**.
- A spoken "stop" while the assistant works. Speech during "working" is
  ignored **(main session)**; Escape and Stop cancel.
- Voice commands in general. The one exception is a short repeat or continue
  phrase, handled in Nutshell (§10) **(critique)**.
- Spoken replies in dictation mode. The voice spec's speaker toggle (§5) is
  not built here (§15).
- Reading code blocks, tables, raw tool output or URLs aloud **(maintainer,
  voice spec §2)**.
- Live text with Groq, Gemini or a custom URL (§6.4). Gemini Live is later.
- Voice for the terminal itself.

**Anti-goals: failures even if it "works"**
- A spoken turn that runs or writes anything without the approval a typed one
  would get. Voice never builds a conversation turn itself.
- A microphone left open after any end of a conversation (§5.6), or open in
  dictation when not recording.
- Our own spoken reply transcribed and sent back as the maintainer's turn.
- A loud noise (a door, a keyboard, a radio) sent as a turn **(critique)**.
  Said plainly **(critique round 2)**: the detector cannot tell the
  maintainer's voice from other voices in the room, so another person, or a
  video playing, can be heard and sent. The safeguards are the pause while
  Nutshell is in the background or behind a dialog (§5.4), the auto-approve
  round cap that a voice turn does not reset (§5.2), and approvals.
- Audio sent while Nutshell is in the background during a conversation
  **(critique)**.
- A refused send that leaves the conversation waiting for a reply that will
  never come **(critique)**.
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
- Escape during a recording discards it and is consumed **wherever focus
  is** (`src/core/voice_ptt.c:100-110`); the filter passes `in_panel` but the
  Escape rule does not use it (`voice_win.c:128-156`).
- Delivery: `voice_deliver` (fill, append or send) and `VoiceBinding` (session,
  session generation, conversation generation) in `voice_ptt.h`;
  `ai_chat_voice_deliver` writes into the box with `EM_REPLACESEL` or sends
  through `ai_chat_send_user_message` (`src/ui/ai_chat_voice.c:174-222`,
  `src/ui/ai_chat_send.c:569`).
- "Busy" is `busy_of`: streaming, dispatching, or a command batch not finished
  (`ai_chat_voice.c:36-45`).
- `ai_chat_send_user_message` returns `void`. It returns silently when busy
  or with no session (`ai_chat_send.c:575-576`), puts a "conversation full"
  line in the chat instead of sending (`:613`), and the stream launch
  (`:671`) can fail with nothing reported to its caller. On a send it resets
  the auto-approve round counter (`ai_rounds_user_message`, `:628`); Stop
  does too (`ai_chat.c:723`).
- The busy callback `ai_chat_voice_busy_changed` fires at
  `src/ui/ai_chat_dispatch.c:1304` and `:1350`, before the relaunch decision
  at `:1313` and `:1365`, so "not busy" there does not mean the turn is over.
- Voice errors go to the chat as a persistent status line; other statuses go
  to the header (`ai_chat_voice_status`, `ai_chat_voice.c:156-170`).
- A transcript result is a fixed `char text[4096]` (`VoiceSttResult`,
  `src/ui/voice_stt_win.h:18`).
- The input is a plain `EDIT` (`src/ui/ai_chat.c:389`) with a subclass
  (`src/ui/ai_chat_input.c:121`). It is shared by every tab.
- A reply completes in `WM_AI_RESPONSE` (`ai_chat.c:1462`), with `wParam`
  and `took_calls` (`:1488`) telling the cases apart. This design does not
  infer the end of a turn from them (§5.2) **(critique)**.
- The AI panel keeps one `AiSessionState` per tab (`active_state`,
  `src/ui/ai_chat_internal.h:316`), so a tab change is a change of chat.
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
  `k_setting_keys` (`loader.c:484-541`) has 57 keys; `CFG_KEYS_MAX` is 96
  (both rechecked against the code after the critique).
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
  opens one WebSocket to the provider (§6.6) **(main session)**. Audio
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
- **"Send automatically when I finish speaking"** stays a dictation option:
  with it on, the final text is sent when the mic stops, provided the box was
  empty at start, as today's `VD_SEND` rule. It is the maintainer's earlier
  decision (review by default, auto-send opt-in) and is not removed
  **(critique, replacing the main session's removal in PR 2)**. Conversation
  mode ignores it (§5.1).

### 4.4 Every way a dictation recording ends

| How | Mic | Words already in the box | Sent? |
|---|---|---|---|
| Key or mic button again (Toggle), key up (Hold) | closed | kept, final text added | only with auto-send on |
| Send or Enter | closed | kept, final text added | yes, after the final text (§4.3) |
| Escape with focus in the AI panel | closed | this recording's words removed; the draft is as it was | no |
| The 10-minute cap | closed | kept, final text added | as for a key press |
| Nutshell loses activation, or a modal dialog opens | closed | kept | no, even with auto-send on |
| Tab change, New chat, or the session closes | closed | kept | no, even with auto-send on |
| Session lock (Win+L), PR 2 onwards | closed | kept | no |
| Voice or AI switched off | closed at once | kept | no |
| Settings saved with any voice setting changed (critique round 2) | closed at once | kept | no; the next press starts cleanly with the new settings |
| Device lost | closed | kept | no; error shown |

Notes:
- Escape removes the recording's words **(spec author's call)**. It is the one
  deliberate cancel, as Escape discards a recording today. Every other end
  keeps what is in the box, because those words are already on screen and a
  draft is never thrown away by an accident such as Alt+Tab.
- **Escape only in the panel** **(critique; in PR 1 since round 2, because
  PR 1 ships 10-minute recordings)**: Escape ends a
  recording or a conversation only while keyboard focus is in the AI panel
  (or voice-owned UI such as the mic button). In the terminal, Escape goes to
  the terminal as normal and the recording carries on. Recordings are now up
  to 10 minutes long, so the maintainer will be using vi or `less` while one
  runs. `voice_ptt_step`'s Escape rule (`voice_ptt.c:100-110`) changes to use
  `in_panel`, with tests.
- Losing activation and a modal dialog keep today's rule for dictation (voice
  spec §4.1): the recording stops. Conversation mode differs (§5.4, §5.6).
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
- **Sized for the cap** **(critique round 2)**: the transcript result is
  heap-allocated and sized for a 10-minute recording, at least 64 KB,
  replacing `VoiceSttResult`'s fixed 4,096-byte `text`
  (`voice_stt_win.h:18`).
- The audio is kept until the fallback succeeds, then wiped
  (`secure_zero`) and freed.
- If the fallback fails, the live text already in the box stays, and the
  error says so: "The recording could not be transcribed again: the text in
  the box is what arrived live" (error, §13).
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
  conversation; pressing it again ends it, except while a reply is being
  spoken, when it stops the speech (§5.2) **(main session; the speaking case
  critique round 2)**.
- The key always toggles in Conversation mode. Hold to talk and the Mic start
  delay apply to Dictation only, and the Settings page greys them in
  Conversation mode **(spec author's call)**: a conversation lasts many turns,
  and holding a key through it would defeat the point.
- "Send automatically when I finish speaking" is ignored in Conversation mode,
  and greyed on the Settings page there **(critique; greying spec author's
  call)**. Every turn is sent at the pause.
- A conversation is bound to the tab and chat that were showing when it
  started (`VoiceBinding`).
- It cannot start while the assistant is busy, or with no session, with
  today's statuses (`voice_busy_text`).
- **The box and typing** **(critique round 2, replacing the first draft's
  "no typing for the whole conversation")**:
  - The box is locked only while speech is being captured into it (Hearing
    and Ending the turn, §5.2).
  - It accepts typing at other times, Working and Paused included. A typed
    message is sent with Enter as normal, and is a typed turn in every way
    (it resets the round counter, §5.2).
  - A draft in the box when a turn starts becomes the start of that turn: the
    turn sends the draft, a space, then the spoken words. A draft left in the
    box when Listening resumes therefore starts the next turn. The cue says
    "Your text and what you say next will be sent after a pause."

### 5.2 The turn cycle

The conversation and recording logic is a pure state machine in `src/core`,
with time injected, like `voice_ptt`. It owns the states, every end and the
calls to the resource ledger (§12), so all of it is tested natively;
`voice_win.c` becomes thin Win32 glue that feeds it events and carries out
its actions **(critique round 2, a refactor of today's controller)**. Its
states:

1. **Listening**: the mic is open, no speech yet. Header: "Listening… speak,
   then pause to send".
2. **Hearing**: speech has started (§5.3). With a live provider the words
   stream into the box as in dictation (§4.2). With a batch provider the audio
   is kept for upload and the box shows nothing yet. Header: "Hearing you…".
3. **Ending the turn**: the pause is reached. With a live provider the
   controller asks for the final text (§6) and waits for it, at most 3 s
   **(spec author's call)**. With a batch provider the turn's audio is uploaded
   as today. Header: "Sending…" or "Transcribing…". A short repeat or continue
   phrase is handled here, without a model turn (§10).
4. **Working**: the turn has been sent. **The capture device is closed**
   **(critique round 2)**, so an approval card that waits holds no open mic.
   Header: "Working… press Escape to stop".
5. **Speaking**: the reply's prose is read aloud (§8). The capture device is
   open again, for cutting in (§9). Header: "Speaking… talk to cut in".
   - The voice key or the mic button stops the speech and returns to
     Listening; the conversation carries on **(critique round 2)**.
   - Escape stops the speech only, and only with focus in the AI panel
     (§4.4).
   - The panel's Stop stops the speech too.
6. Back to **Listening** when the reply has been read, cut short, stopped, or
   had nothing to read. The capture device reopens.

And one state outside the cycle:
- **Paused**: Nutshell is in the background, or behind one of its own modal
  dialogs (§5.4). The capture device is closed. Header: "Paused while
  Nutshell is in the background" (or "Paused while a dialog is open").

**Sending a turn** **(critique)**:
- The turn goes through the typed message's path, so every approval applies
  unchanged. `ai_chat_send_user_message` (or a new voice entry point beside
  it, **builder's call**) returns a status: sent, busy, no session,
  conversation full, launch failed.
- Anything but "sent" leaves Working at once. A short phrase is spoken, "I
  couldn't send that. The details are on screen.", and a note in the chat
  says why: "The assistant is busy", "Connect a session first", "The
  conversation is full: start a New Chat", "The request could not start".
  Then back to Listening.
- The full-conversation and launch-failure paths (`ai_chat_send.c:613`,
  `:671`) report back to the caller instead of only writing to the chat.
- **The auto-approve round cap** **(critique round 2; main session's call,
  for the maintainer to confirm)**: a voice turn does **not** reset the
  auto-approve round counter (`ai_rounds_user_message`,
  `ai_chat_send.c:628`). Only a typed message, Stop as today, or an on-screen
  click on an approval card resets it. So once the usual cap of unattended
  rounds is reached, an on-screen approval is needed again, however many
  voice turns there were. An open mic in a room is not the maintainer typing.
  The tool-round count still resets as for a typed message **(spec author's
  call)**.

**When Working ends** **(critique; event named in round 2)**:
- Working ends on one **"chat turn idle"** notification from the chat. The
  builder adds it. It fires only after the relaunch decision, when there is no
  request in flight, no tool round about to relaunch and no card pending, and
  it is posted (`PostMessage`), never called synchronously.
- The existing busy callback is not enough: it fires at
  `ai_chat_dispatch.c:1304` and `:1350`, before the relaunch decision at
  `:1313` and `:1365`.
- It is not inferred from `WM_AI_RESPONSE`'s `wParam`.
- Then the prose of the last assistant reply that this turn produced is read
  (§8.1). With no prose, nothing is read and listening resumes.
- A reply that ended in an error is not read in full. A short phrase is
  spoken, "Something went wrong. The details are on screen.", and listening
  resumes **(spec author's call)**.
- Intermediate prose that comes with a tool turn is not read
  **(main session: the final answer only)**.
- **Safety timeout, tied to the chat state**: while Working, the controller
  rechecks the chat's idle state once a second. If the chat has been idle for
  3 s and no "chat turn idle" arrived (a lost message), Working ends as above.
  There is no wall-clock limit while the chat is really busy, since commands
  may run for minutes; Escape, Stop and the tool timeout
  (`ai_tool_timeout_sec`) cover that **(spec author's call of the numbers)**.

**Approval cards** **(critique)**:
- A pending approval card of any kind is announced aloud once, whether it is
  a command, a file write or any other tool-gate card: "I need your approval
  to run a command.", "I need your approval to write a file.", or for any
  other kind "I need your approval for the next step." The builder lists the
  card kinds from the tool gate.
- It is answered on screen as today. Header: "Waiting for your approval".

Other rules:
- An empty or whitespace transcript sends nothing, says "Didn't catch that",
  and goes back to Listening.
- A transcript that the echo filter drops (§9.4) goes back to Listening, with
  the header "Ignored the assistant's own voice" **(critique)**.
- One turn is capped at 2 minutes of speech; at the cap it is ended as if the
  pause had been reached **(spec author's call)**.
- Send pressed while Listening or Hearing ends the turn at once, as if the
  pause had been reached **(spec author's call)**.
- **Saving Settings** with any voice setting changed ends a running
  conversation (or dictation recording); the next press starts cleanly with
  the new settings **(critique round 2)**.
- **Play sample** pressed during a conversation stops the conversation's
  speech first **(critique round 2)**.

### 5.3 Speech and the pause

- **An adaptive speech detector** in `src/core` decides what is speech in
  conversation mode, replacing the fixed RMS threshold there
  **(critique)**. It tracks a noise floor over time; speech is a level some dB
  over that floor, held for a minimum duration. A door, a click or a steady
  fan is not a turn. The builder tunes the numbers, with native tests for
  steady noise, clicks and speech.
- It cannot tell the maintainer's voice from anyone else's (§2, anti-goals).
- Turn taking is by pause, detected in Nutshell for every provider, so one
  setting means the same thing everywhere **(main session)**. Provider-side
  turn detection (Mistral's) is not used for this.
- "Pause before sending": default 1.2 s, range 0.5 to 3 s **(main session)**.
- The pause is measured from the last window the detector called speech. A
  turn needs at least about 300 ms of speech in total (`VOICE_SHORT_TAP_MS`),
  so a cough is not a turn.

### 5.4 Paused: in the background or behind a dialog

**(critique; dialogs and held replies critique round 2)**:
- The conversation moves to Paused when Nutshell stops being the active app
  (`WM_ACTIVATEAPP` with FALSE), or when the main window is disabled by one of
  Nutshell's own modal dialogs (`WM_ENABLE` FALSE on the owner: Settings, a
  password prompt, the shared notice dialog).
- While Paused, the capture device is closed: no audio is captured or sent
  **(spec author's call, consistent with Working)**.
- A turn being heard at that moment is dropped, not sent, and its words are
  cleared from the box **(spec author's call)**.
- **Replies are not spoken while Paused**: they are held. On resume, the
  first held reply is spoken if it is less than 30 s old; older ones stay on
  screen only **(main session's call)**. A reply that was being spoken when
  the pause began stops, and is held from its cut-off point under the same
  rule **(spec author's call)**.
- Working carries on while Paused; its reply is held as above.
- When Nutshell is active and enabled again, the conversation resumes in the
  state the assistant is in (Listening, Working or Speaking a held reply).

### 5.5 With a batch provider

Conversation mode works with Groq, Gemini and a custom URL too, since the
pause is found in Nutshell **(spec author's call, following the main
session's "for every provider")**. Each turn's audio is uploaded at the pause
through today's path, so a turn takes a little longer and no words appear
while speaking. The Voice page says "Live text needs OpenAI or Mistral".

### 5.6 Every way a conversation ends

| How | Assistant's work | Speech | Note in the chat |
|---|---|---|---|
| Key or mic button, except while Speaking | left running; its reply is not read | stopped | none |
| Escape with focus in the AI panel, or the panel's Stop, except while Speaking | cancelled as today | stopped | none |
| Closing the panel | as today | stopped | none |
| Voice or AI switched off | as today for AI off | stopped | none |
| Settings saved with any voice setting changed | left running | stopped | "Conversation ended: the voice settings changed" |
| Session lock (Win+L) | left running | stopped | "Conversation ended: Windows was locked" |
| 3 minutes with no turn sent | left running | stopped | "Conversation ended: nothing said for 3 minutes" |
| Tab change, New chat, or the session closes | as today | stopped | "Conversation ended: the chat changed" |
| Device lost | left running | stopped | the device error |

- Every end closes the microphone at once, stops speech, cancels any speech
  request and closes the WebSocket (§12).
- Words in the box from an unfinished turn stay there and are not sent
  **(spec author's call)**, as in dictation.
- Ending and error messages are persistent notes in the chat, as voice
  errors are today (`ai_chat_voice.c:160-167`); the header carries transient
  states only **(critique round 2)**.
- The first four rows are **(main session)**, with Escape limited to the
  panel **(critique)** and the speaking exception **(critique round 2)**.
- The tab-change row is **(spec author's call)**, kept after the critique
  because the panel holds one chat per tab (§3): a turn must never reach
  another chat.
- **A modal dialog pauses a conversation and does not end it** (§5.4).
- **The 3-minute end** counts from the last turn sent, not from the last
  sound **(critique)**, so a noisy room cannot keep it alive. It is checked
  only while Listening or Paused, and counts from the later of the last turn
  sent and the moment the assistant last went idle **(spec author's call)**,
  so a 4-minute command does not end the conversation the moment it finishes.
- "Speech output cannot start" does not end a conversation (§8.5).

### 5.7 The open-mic safeguards

These are the conditions under which the voice spec's anti-goal is
superseded **(main session, with the critiques' additions)**:
- The mic button and the header show a clear Listening state for the whole
  conversation. The mic button keeps the recording look (`danger` fill) from
  start to end; its pulse follows the input level so it is visibly alive
  (**builder's call** within `ns_motion`).
- It ends on a second press, Escape in the panel, the panel's Stop, closing
  the panel, voice or AI off, session lock, a voice settings change, and 3
  minutes without a turn (§5.6).
- The microphone is closed while the assistant works and while Paused (§5.2,
  §5.4).
- Voice turns do not reset the auto-approve round cap (§5.2).
- Only speech is sent, plus at most the pre-roll before it and the hangover
  after it (§7.2) **(critique round 2 wording)**. The adaptive detector keeps
  noise out of turns (§5.3).
- Nothing is recorded to disk; audio buffers are wiped with `secure_zero`.

## 6. Providers and protocols

### 6.1 Who supports what

| Provider | Live dictation | Conversation | AI speech (PR 3) |
|---|---|---|---|
| OpenAI | yes, WebSocket | live | no: every `/audio/speech` model shuts down on 2027-01-06; a Custom URL pointed at it works until then (amended 2026-11-25) |
| Mistral | yes, WebSocket | live | yes, `voxtral-mini-tts-3-0`, the default (amended 2026-11-25) |
| Groq | no, batch after stop | batch per turn | no: Orpheus is Preview, 10 requests a minute and 100 a day (cut 2026-11-25, §17 round 3) |
| Gemini | no (Gemini Live later) | batch per turn | no |
| Custom | no | batch per turn | yes, an OpenAI-compatible `/audio/speech` |

For speech-to-text, "provider" is the effective one: the Voice page's, or the
AI assistant's when "Use current AI assistant" is ticked (`voice_stt_resolve`).
AI speech has its own provider setting (§8.4). Windows speech works with any
of them.

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
- Its idle limit (how long a session may go without audio) is a must-verify
  item (§16).

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
- Must verify (§16): whether the session closes after `transcription.done`,
  which would mean one session per utterance; its idle limit; its batch size
  limit for the fallback (§4.5).
- Open questions for the builder (§16): whether audio goes as binary frames
  or base64 JSON; how to get a segment's final text; whether gaps in the
  audio (only speech is sent) are accepted.

### 6.4 Batch providers

Groq, Gemini and custom URLs have no live API **(main session)**. They keep
today's request shapes (voice spec §6). Rolling re-transcription as a
pseudo-live mode is a later item (§15).

### 6.5 The voice network opener

- **One opener for all voice traffic** **(critique)**: a single
  `voice_net_open(kind, ...)` in `src/ui` through which the batch STT upload,
  the live WebSocket and the AI speech request all go, with the one
  `voice_gate_on()` check.
- **A voice-only WinHTTP session opener** sits under it **(critique round
  2)**: `voice_winhttp_open`, which checks the voice gate, counts the open and
  then calls `ns_winhttp_open`, the existing choke point (TLS 1.2 or newer, AI
  gate). `ai_http_post_ct` (when called for voice) and the WebSocket path both
  take their session from it, so a voice request that bypasses it shows up as
  a missing count. How `ai_http_post_ct` is handed the voice session (an
  `_ex` variant taking the session, say) is the **builder's call**.
- The counter of voice network opens is printed by the self-report, so a test
  can show that an idle launch with voice on opens none (§14).
- Today's STT worker (`voice_stt_win.c`) moves onto it in PR 1.

### 6.6 The WebSocket transport

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
- **Timeouts**: the WebSocket handle's receive timeout is set to infinite, and
  a connection is cancelled only by closing its handle **(critique)**. Must
  verify, with a fallback, in §16.
- One connection per recording (dictation) or per turn (conversation),
  opened at start and really cancelled at the end, from the UI thread, on
  stop, cancel, voice off or AI off **(main session)**. In conversation mode a
  turn's connection opens at the first speech of the turn; pre-roll and the
  audio until it is ready are held locally **(spec author's call)**. Keeping
  one connection across turns instead is the **builder's call** if latency
  needs it and the provider's idle limit allows.
- **Two workers, one cancel** **(critique round 2)**: a sender fed from the
  audio thread through a locked queue, and a receiver blocked in
  `WinHttpWebSocketReceive`. Cancelling must wake both, by closing the
  WebSocket handle from the UI thread. Today's `AiNetCancel` arms one handle
  for one worker and is not reused as is: the builder defines a two-worker
  cancel, or gives each worker its own handle. Must verify that closing the
  handle wakes a blocked synchronous receive; the fallback is a receive loop
  with short timeouts that checks a cancel flag (§16).
- Events reach the UI thread as a new `WM_VOICE_LIVE` message whose payload
  the receiver frees.
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
- Dictation never uses the communications category, so it never makes Windows
  lower other sounds **(critique)**.

### 7.2 The streaming speech gate

- Only speech is streamed **(main session)**. A gate opens on speech and
  sends the audio from about 300 ms before it (pre-roll), so the first
  syllable is not cut. It closes after a hangover of silence. Nothing else is
  sent.
- In dictation the gate is the fixed threshold of `PCM_SPEECH_RMS`: the
  maintainer has pressed the key, so a noise costs only a little streamed
  audio. In conversation mode it is the adaptive detector of §5.3
  **(critique; using the adaptive one in dictation too is the builder's
  call)**.
- The hangover also flushes the provider: it is at least the provider's
  streaming delay plus a margin, so the last words come back (Mistral's 480
  ms, say). Whether a provider needs continuous audio instead is decided in
  the plan **(main session)**; §16.
- The whole recording is still kept in memory, for the batch fallback (§4.5)
  and for batch providers. A live recording's buffer is sized for 10 minutes
  (19.2 MB), and wiped and freed once the transcript is final (after the
  fallback, when there is one).
- The gate and pre-roll ring are pure code in `src/core`; the audio thread
  feeds frames to the live sender through a locked queue.

### 7.3 Conversation capture

- The device is opened when a conversation starts listening and closed at
  its end **(main session)**, and in between it is closed during Working and
  Paused and reopened for Listening and Speaking **(critique round 2)**.
- It is opened with the communications category for echo cancellation (§9.1):
  `IAudioClient2_SetClientProperties` with `AudioCategory_Communications`
  before `Initialize` **(main session)**. The device is the same as dictation's
  (the chosen endpoint, else the `eConsole` default) **(spec author's call)**,
  so the maintainer's choice of microphone holds in both modes.
- **Ducking** **(critique)**: Windows may lower other sounds while a
  conversation's capture is open, as set in Control Panel > Sound >
  Communications ("When Windows detects communications activity"). Nutshell
  does not claim to stop it for other apps. The Voice page says, in
  Conversation mode: "Windows may lower other sounds during a conversation.
  To stop that: Control Panel > Sound > Communications > Do nothing." The
  builder verifies whether ducking happens at all when the capture device is
  not the default communications device.
- **Nutshell's own speech** must not be ducked by its own capture. Whether it
  is, is a must-verify item; the fallback is `IAudioSessionManager2` to reach
  our process's session and `IAudioSessionControl2::SetDuckingPreference(TRUE)`
  on it, the documented opt-out for a non-communications stream (§16)
  **(critique round 2)**.
- The buffer keeps only the current turn (and the pre-roll ring), never the
  whole conversation **(spec author's call)**.

### 7.4 Playback

- **PR 2: SAPI speaks directly to the default output device** **(critique
  round 2, replacing the first draft's in-memory stream)**. Stop is
  `SPF_PURGEBEFORESPEAK`; the position comes from word-boundary events; the
  duck during a cut-in is `ISpVoice::SetVolume`. The echo reference is the
  render endpoint's loopback, so playing through SAPI is equivalent for echo
  cancellation. PR 2 has no render client of its own.
- **PR 3: Nutshell's own render client**, for AI speech's PCM: WASAPI render
  in shared mode on the default output (`eRender`, `eConsole`), 24 kHz mono
  16-bit with auto-convert, event-driven, on the audio thread. A play queue of
  PCM chunks, each with a map from its audio bytes to byte offsets of the spoken text
  (§10); the audio thread reports bytes played (`VAM_PLAYED`, say). A software
  gain for the duck. Stop drops the queue and flushes the buffer
  (`IAudioClient_Stop` + `Reset`). It opens with the first reply and closes at
  the conversation's end; whether it stays started between replies is the
  **builder's call**.
- Both PRs keep the last reply's spoken text and where it was cut off, for
  repeat and continue (§10), until the next reply or the end of the
  conversation.

### 7.5 Hand-copied COM declarations

- Every COM declaration copied by hand (GUIDs, interfaces, vtables, structs
  such as `AudioClientProperties`) is guarded by the header's
  `__I..._INTERFACE_DEFINED__` / `__I..._FWD_DEFINED__` macros, or by a
  feature test, so a newer MSYS2 header that has them does not collide
  **(critique)**.
- Because the Windows box's MSYS2 headers and gcc are newer than the
  cross-compile's (MEMORY.md, "A Windows-host `make test` is a third build"),
  each PR asks the maintainer for a `mingw32-make test` and a release build on
  Windows. That request goes into the notes' Open list.

## 8. Speech output

PR 2 builds Windows speech (§8.1 to §8.3, §8.5); PR 3 adds AI speech (§8.4)
**(critique)**.

### 8.1 What is read

- The prose of the reply that ends the turn, Markdown stripped
  **(main session)**.
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
  characters, always at sentence ends (bytes of UTF-8, as built; amended
  2026-11-25). The next chunk is prepared while the
  current one plays.
- The text sent for speech is logged only through `DLOG_CONTENT`.

### 8.2 Windows speech (the default)

- "Speech: Windows speech (default) / AI speech" **(maintainer)**. The row
  appears in PR 3; PR 2 has Windows speech only.
- SAPI 5: `CoCreateInstance(CLSID_SpVoice)` when a conversation starts, on a
  speech thread (MTA) **(main session; thread spec author's call)**. COM maps
  `sapi.dll`; the creation site notes it in the DLL record, as
  `voice_audio_win.c` does for `mmdevapi.dll`.
- It speaks to the default output device (§7.4) **(critique round 2)**.
- Text goes in with `SPF_ASYNC | SPF_IS_NOT_XML` **(spec author's call)**: a
  reply can never steer SAPI with XML tags.
- Word-boundary events give each word's character offset, which is what §10
  needs.
- Stopping: `SPF_PURGEBEFORESPEAK` on an empty speak. Its latency, and
  `SetVolume` taking effect mid-utterance, are must-verify items (§16).
- A speed setting (SAPI rate, -10 to 10, default 0) is optional: the
  **builder's call** **(main session)**.

### 8.3 Voice choice and fallback (Windows speech)

- "Windows voice": a drop-down of installed, usable voices, SAPI and OneCore
  **(main session)**.
- **Default chain** **(critique, extending the main session's)**: an English
  (UK) female voice; else any English female voice (en-US, en-AU and the
  rest); else any English (UK) voice; else the system default voice. Among
  English (UK) female voices, the known names (Hazel, Susan) come first.
- **Which voices are usable** **(critique)**: the research named Hazel, Susan
  and Libby, but some may be Natural (Narrator) voices only, which apps cannot
  use. The builder verifies which en-GB voices ship as SAPI desktop voices,
  which as OneCore voices and which are Natural-only, and lists only usable
  ones. The name list in the choice rule follows that finding.
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
  `sapi.dll`. A voice in both lists is shown once (**builder's call** which
  token wins).
- The choice rule is a pure function in `src/core` over that list.
- **With no British woman's voice installed** **(critique round 2; main
  session's call, for the maintainer to confirm)**: Windows speech stays the
  default. At the first spoken reply, once per launch, and only when
  Automatic resolved to a voice that is not an English (UK) woman's
  **(spec author's call of that condition)**, the shared notice dialog (not a
  `MessageBox`) says: "Windows has no British English woman's voice
  installed, so replies use <voice>. To get one, add English (United Kingdom)
  under Windows Settings > Time & language > Speech." (The clause "or choose
  AI speech in Settings > Voice" is added with AI speech in PR 3.) The dialog is modal, so the conversation is Paused while
  it shows and the reply is held (§5.4); it is spoken when the dialog closes.
  The Voice page shows the same fact. Verify the Windows Settings path on
  Windows 10 and 11.
- **Play sample** speaks "Hello. This is how replies will sound." with the
  chosen engine and voice. It is enabled only in Conversation mode, loads
  `sapi.dll` (or, in PR 3, makes one speech request) on the click, and
  releases it when done **(spec author's call)**. During a conversation it
  stops the conversation's speech first (§5.2).

### 8.4 AI speech (PR 3)

**Amended 2026-11-25** (maintainer's decision, then critique round 3; §16 "Decisions of
2026-11-25: AI speech provider", §17 round 3). AI speech uses providers with a real
text-to-speech endpoint: **Mistral and Custom**.
- OpenAI is not offered: its speech models shut down on 2027-01-06. Its replacement is a
  conversational model on the Realtime API, which was designed for this PR, critiqued and
  rejected.
- Groq was offered and then cut: Orpheus is a Preview model, limited to 10 requests a minute
  and 100 a day.

The first text of this section (OpenAI `gpt-4o-mini-tts`, `pcm`, voice `coral`) is in git
history.

- **Its own provider setting**, as the voice spec's G3 says **(critique round 2, replacing the
  main session's "uses the voice provider's key, OpenAI only")**:
  - Speech provider:
    - **Mistral**, the default: `POST https://api.mistral.ai/v1/audio/speech`, model
      `voxtral-mini-tts-3-0`, streamed as server-sent events.
    - **Custom**: an OpenAI-compatible server. Its Base URL gives `{base}/audio/speech`,
      normalised by `voice_base_url_normalise`. Plain `http://` is allowed, with the
      clear-text warning, as for speech-to-text.
    - Any other provider id, `openai` and `gemini` included, is an error (§8.5); it is never
      replaced by Mistral.
  - **Key.** When the speech provider is the same as the effective speech-to-text provider
    (Mistral with Mistral; Custom with the same base URL), that key is reused and no second
    key is asked for. Otherwise the speech key is its own DPAPI secret, `tts_api_key` with its
    `*_enc_preserved` twin, as voice spec §7 defined it.
    - The saved key is **bound to the provider it was entered for**. The encrypted value holds
      the provider (`mistral`, or `custom` and the normalised base URL) before the key.
    - A key whose provider no longer matches the setting is not used: there is then no key,
      which §8.5 reports.
    - A Custom server may need no key (a local Kokoro server, say).
  - **Privacy.** With AI speech selected, the Voice page shows the dim line "Each spoken
    reply's text is sent to <host>." The Base URL help says that the reply text and the key
    go to that server, and that `http://` is not encrypted.
  - The §2 supersede list records what differs from the voice spec.
- **Requests.** Each chunk of §8.1 is one request, at sentence ends as in §8.1, opened through
  `voice_net_open` (§6.5).
  - Mistral: JSON `model`, `input`, `voice_id`, `response_format` and `stream: true`, sent
    with `Accept: text/event-stream`. Mistral refuses unknown fields with HTTP 422.
    - The reply is server-sent events, read through the existing `sse_line` splitter. Their
      shape, from Mistral's SDK: `event: speech.audio.delta` with data
      `{"type": "speech.audio.delta", "audio_data": "<base64>"}`, then `speech.audio.done`
      with `usage`.
    - The raw frames are captured on the maintainer's machine before the PR is marked ready
      (§14, "Task 0" of the plan). Until then the parser's fixtures are provisional.
  - Custom: JSON `model`, `voice`, `input` and `response_format: "wav"`, plus `instructions`
    only when the user typed a style. The body streams as raw bytes.
  - **Audio accepted.** Exactly 24,000 Hz, 16-bit or 32-bit float, mono or stereo (stereo is
    down-mixed).
    - Mistral's audio is mono at 24 kHz; Kokoro-FastAPI's is 24 kHz 16-bit.
    - Nothing is resampled: any other rate or format is the failure "unsupported audio format"
      (§8.5).
    - WAV is read as it streams; sizes of 0 or `0xFFFFFFFF` mean "until the end".
    - Mistral is asked for `wav`, or for `pcm` (float32) if the capture shows one WAV header
      per delta.
  - **Reply text in brackets.** In the text sent to a provider, `[` and `]` become `(` and
    `)`, so a reply cannot steer the voice with bracketed cues. This is byte for byte, so
    every offset still matches.
- Settings **(main session; values amended)**:
  - "AI voice": an editable combo seeded per provider. Empty means the provider's default.
    - Mistral: `voice_1787` (Mary, English (UK), female), to be confirmed by the capture.
      Also seeded: `voice_1786` (Sky), `voice_590` (Harriet) and `gb_jane_neutral` (Jane,
      English (UK), a Voxtral 2 preset that Voxtral 3 still accepts).
    - Custom has no default.
  - "AI speech model": an editable combo. Mistral: `voxtral-mini-tts-3-0` (the deprecated
    Voxtral 2 model `voxtral-mini-tts-2603` is not offered). Custom has no default.
  - "Accent and style": free text, empty by default. It is enabled for Custom only and sent
    as `instructions` only when not empty. Mistral takes the accent from the voice.
- Bytes played map to byte offsets of the text in proportion within the chunk (§10).
- Price (2026-11-25): Mistral $16 per 1M characters.

### 8.5 Speech fallback

- **AI speech fails for a reply in a passing way** (a network error, HTTP 429 or 5xx, the
  device lost):
  - the rest of that reply is read with Windows speech, and the header says "AI speech failed:
    using Windows speech" **(spec author's call)**;
  - the next reply tries AI speech again.
- **AI speech cannot work as set up** (amended 2026-11-25, critique round 3). The causes:
  - a setting that does not resolve (no key, a key bound to another provider, an unknown
    provider, a Custom server with no URL, model or voice);
  - HTTP 401, 403, 404 or 422;
  - audio Nutshell cannot read ("unsupported audio format").

  The reply is read with Windows speech. Once per conversation a chat note says "AI speech is
  off for this conversation: <reason>. Replies are read with Windows speech." AI speech is
  not tried again until the next conversation.
- **What fails a reply.** Besides an HTTP or network error, each of these counts (critique
  round 3):
  - a chunk that produced no audio;
  - a stream that ends before its audio began;
  - a server-sent-event line too long to read;
  - a second WAV header in the middle of a stream;
  - a `done` event with no audio.
- **Windows speech cannot start** (no voice, SAPI error): the reply is not read, a note in the
  chat says "Could not speak the reply: it is on screen", and listening resumes **(spec
  author's call)**. The conversation does not end.

## 9. Echo cancellation and cutting in

### 9.1 Echo cancellation

- Windows' own echo cancellation, through the communications category on the
  capture stream (§7.3) **(main session)**. No new DLL.
- On Windows 11, `IAudioEffectsManager` (from the capture client's
  `GetService`) says whether acoustic echo cancellation is present and on, and
  can turn it on where the driver allows. Its minimum build (the research says
  22000) and how widely devices offer echo cancellation are must-verify items
  (§16).
- On build 22621 and later, `IAcousticEchoCancellationControl` sets the
  reference render endpoint to the default output, which SAPI (PR 2) and our
  render client (PR 3) both play on. The reference is that endpoint's
  loopback, so our own speech is cancelled.
- MinGW's headers may lack these interfaces. Their GUIDs and vtables are
  copied locally, as `voice_audio_win.c` already does for the device
  enumerator (no `libuuid`), each guarded as §7.5 says.
- **Without echo cancellation** (Windows 10, some drivers, or not verified):
  the half-duplex fallback. While Speaking, the capture is used only to detect
  a cut-in, at a raised threshold. **Audio from before the cut-in is
  confirmed is not included in the turn** **(critique round 2)**: the turn
  starts after stage 2 (§9.2), so the first words said over the reply may be
  lost, but our own speech is never transcribed. The Voice page says "Your
  microphone reports no echo cancellation: a headset works best". The report
  is made at the first conversation and kept for the Settings page until the
  next one **(spec author's call)**, so Settings itself opens no capture
  stream.
- A software fallback (speexdsp) for Windows 10 and devices without echo
  cancellation is later (§15).

### 9.2 Cutting in

Two stages while Speaking **(main session)**:
1. **Duck**: sustained voice, as the adaptive detector judges it and well
   over the echo estimate, for about 200 ms lowers our speech
   (`ISpVoice::SetVolume` in PR 2, the render gain in PR 3; about -15 dB,
   **builder's call**). With echo cancellation, the STT connection opens now,
   so the pre-roll holds the first words.
2. **Stop**: about 400 ms of voice stops the reply. Speech stops at once, any
   speech request is cancelled, and the cut-off point is recorded (§10). The
   state moves to Hearing.

- If the voice stops before stage 2 (a cough, a word to someone else), the
  reply un-ducks within about 300 ms and carries on **(spec author's call)**.
- The echo estimate: with echo cancellation on, the detector's threshold plus
  a margin; without it, a margin over the level we are playing, learned during
  the conversation. Numbers are the **builder's call**, tuned in the hand
  checks.
- The detector and its timings are pure code in `src/core`, fed with levels.

### 9.3 Listening during speaking

The capture stream is open while Speaking (not while Paused, §5.4). Speech
that does not reach stage 2 is not sent.

### 9.4 The echo filter

**(critique, rule revised in round 2)**:
- It applies only during playback and for 2 s after it ends. At any other
  time no transcript is dropped as echo.
- It matches against the **whole last spoken reply**, not one sentence. Both
  are normalised: lower case, no punctuation, split into words.
- A transcript is dropped when it is covered by runs of 3 or more contiguous
  words that also appear in the reply, and those runs together cover 90% or
  more of the transcript's words.
  - Example: the reply said "...the C drive has 120 GB free..."; the
    transcript "120 GB free" is one 3-word run covering 100%: dropped.
  - Example: the transcript "yes, check the D drive". Even if the reply held
    the run "check the D drive", it covers 4 of the transcript's 5 words,
    80%: kept.
- A drop is visible: the header says "Ignored the assistant's own voice". The
  transcript is logged only through `DLOG_CONTENT`.
- Pure code in `src/core` with native tests (§14).

## 10. Repeat and continue

**(critique, replacing the main session's "ordinary requests to the
assistant, not local commands"; matcher widened in round 2)**:
- Nutshell records how far the spoken reply got:
  - Windows speech: the word-boundary event of the last word spoken.
  - AI speech (PR 3): bytes played, mapped to byte offsets in proportion within
    the chunk.
- **A short repeat or continue phrase is handled by Nutshell**, with no model
  turn. The phrases: "repeat", "repeat that", "say that again", "again",
  "continue", "carry on", "go on", "keep going", "what did you say".
  - Polite forms count: an optional leading "please", "can you", "could you"
    or "can you please", and an optional trailing "please".
  - The whole utterance must be one of these, 6 words or fewer, with case and
    punctuation ignored.
  - **Repeat** ("repeat", "repeat that", "say that again", "again", "what did
    you say") replays the last spoken reply from its start.
  - **Continue** ("continue", "carry on", "go on", "keep going") resumes the
    last reply from where it was cut off.
- **It acts locally only when** a spoken reply exists from the last 5
  minutes, and that reply did not end with a question. "Carry on" after
  "Shall I delete them?" is an answer, so it goes to the model.
  Otherwise, and for "continue" when the last reply was read to the end, the
  phrase goes to the model as an ordinary message **(critique round 2)**.
- The phrase is not added to the chat when handled locally. The header says
  "Repeating" or "Carrying on" **(spec author's call)**.
- Anything else goes to the model as an ordinary message.
- **No hidden cut-off note is sent to the model** **(critique round 2:
  the proposal was rejected)**: voice never builds turn content itself
  (§2, anti-goals).
- Replay re-synthesises from the kept text (§7.4); caching the last reply's
  PCM in PR 3 instead is the **builder's call**.
- The phrase matcher, and the mapping from the speech position to a character
  offset, are pure code in `src/core` with native tests.

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
  minutes and a batch one after 60 seconds, and that Escape cancels only with
  the AI panel focused.
- Upgrade notice line: "Voice: live speech-to-text model — the provider's
  default".

### 11.2 PR 2

New rows, in this order **(spec author's call)**:
- **Mode** (Dictation / Conversation) directly under Enable voice, because it
  decides how everything below works. The existing push-to-talk "Mode:" row is
  renamed "Key mode:" (Toggle / Hold to talk) so the two are not confused.
  Key mode's Hold choice, the Mic start delay and "Send automatically when I
  finish speaking" are greyed in Conversation mode (§5.1).
- "Send automatically when I finish speaking" stays, as a dictation option
  **(critique)**. No migration and no `voice_defaults_rev` bump for it.
- A new section **"Conversation"** after Microphone, greyed in Dictation mode:
  - Pause before sending (0.5 to 3.0 s, default 1.2; an edit in tenths or a
    slider, **builder's call** within the design system).
  - Windows voice (drop-down, §8.3), with the English (UK) line when needed.
  - Speed, if the builder adds it.
  - Play sample.
  - The echo-cancellation line when the last conversation found none (§9.1).
  - The ducking line (§7.3).

New keys (`Settings`, loaded and saved in `loader.c`, each with a
`settings_help` entry and a `k_setting_keys` row):

| Key | Default | Notice line |
|---|---|---|
| `voice_mode` | `"dictation"` | Voice: mode — Dictation, so your words wait in the box for you to press Send |
| `voice_pause_ms` | 1200 | Voice: pause before sending — 1.2 seconds |
| `voice_windows_voice` | empty (Automatic) | Voice: Windows voice — Automatic (an English (UK) woman's voice if one is installed) |
| `voice_windows_voice_name` | empty | Voice: Windows voice name — none, as Automatic is used |
| `voice_speech_rate` (only if built) | 0 | Voice: speed — normal |

`voice_windows_voice_name` gets its own line, following the precedent of
`voice_input_device_name` (`loader.c:536-537`) **(critique round 2)**.

### 11.3 PR 3

- **The rows.** Under Conversation (amended 2026-11-25):
  - Speech (Windows speech / AI speech);
  - Speech provider (Mistral / Custom);
  - Base URL (Custom only);
  - API key (shown only when it is not shared with speech-to-text, §8.4);
  - AI voice;
  - AI speech model;
  - Accent and style (multi-line edit, enabled for Custom only);
  - the dim line "Each spoken reply's text is sent to <host>".

  All of them are greyed while Speech is Windows speech.

| Key | Default | Notice line |
|---|---|---|
| `voice_speech_engine` | `"windows"` | Voice: speech — Windows speech |
| `voice_tts_provider` | `"mistral"` (amended 2026-11-25) | Voice: speech provider — Mistral |
| `voice_tts_url` | empty | Voice: speech URL — none |
| `tts_api_key` (and `tts_api_key_enc_preserved`) | empty | Voice: speech API key — none; the speech-to-text key is used when the provider is the same |
| `voice_ai_voice` | empty, the provider's default (amended 2026-11-25) | Voice: AI voice — the provider's default (for Mistral, Mary, British English) |
| `voice_ai_tts_model` | empty, the provider's default (amended 2026-11-25) | Voice: AI speech model — the provider's default |
| `voice_ai_style` | empty (amended 2026-11-25) | Voice: accent and style — none |

The provider the key belongs to is held inside the encrypted `tts_api_key` value (§8.4), not
under a key of its own, so PR 3 still adds seven notice lines.

### 11.4 All three

- The notice values come from each entry's `default_text`, as the notice does
  today (`notice.c:266`). With the voice switch itself new, all of them fold
  into the one "Voice" line, as today. No PR adds more than seven lines, under
  `NOTICE_ITEMS_MAX` (24).
- An older exe drops the new keys; the next launch adds them and says so.
  That is accepted (CLAUDE.md, "Optional features").
- **Shortcuts** (`src/core/shortcuts.c:71-74`, README block regenerated): in
  PR 1 the Escape row gains the condition "AI panel focused". In PR 2 the
  voice-key row reads "Talk to the assistant: press, speak, press again (in
  Conversation mode, press to start and end a conversation, or to stop a
  spoken reply)"; the Escape row reads "Cancel a voice recording, stop a
  spoken reply or end a conversation".

## 12. Optional features: what loads when

Spoken replies add speakers (a device) and, for AI speech, a network
destination. They are part of the voice feature, not a new optional feature
with its own switch **(main session)**. The rule "not loaded while off"
extends to each mode:

| State | What runs |
|---|---|
| Voice off, or AI off | nothing (as today) |
| Voice on, idle (either mode) | the audio thread and device enumerator (as today); no voice network opens |
| Dictation, batch recording | + the capture device (as today) |
| Dictation, live recording | + the WebSocket (new WinHTTP entry points), its sender and receiver threads |
| Conversation, Listening or Speaking | + capture with the communications category and echo-cancellation objects, the speech thread with `SpVoice` (Windows speech), the WebSocket per turn (live providers); in PR 3, the render client and one speech request per chunk (AI speech) |
| Conversation, Working or Paused | as above but with the capture device closed and no WebSocket |
| Any recording or conversation (PR 2 onwards) | + the session-lock registration (`wtsapi32.dll`) |
| Settings > Voice open | no `sapi.dll` (voices read from the registry); Play sample loads it on the click only |

- **sapi.dll** is requested only when a conversation starts with Windows
  speech, and by Play sample **(main session)**. Never in Dictation mode.
- **Session lock**: `WTSRegisterSessionNotification` on the main window,
  through a delay-loaded `wtsapi32.dll` (a new `wtsapi32.def`) **(main
  session)**. Registered when a recording or conversation starts and
  unregistered when it ends **(spec author's call)**, so the DLL is requested
  only once voice is actually used. `WTS_SESSION_LOCK` ends a dictation
  recording (closing notes item (u)) and a conversation.
- **One gate per entry point**: `voice_net_open` over `voice_winhttp_open`
  for every voice network request (§6.5) **(critique)**, and one `SpVoice`
  constructor, each refusing unless `voice_gate_on()`.
- **A resource ledger** in `src/core` **(critique)**: acquire and release
  counts for the capture client, `SpVoice`, the WebSocket, speech requests,
  the render client (PR 3), the session-lock registration and the voice
  threads. The Win32 code calls it at each acquire and release; the
  `src/core` controller (§5.2) checks it. Tests use it to show that every end
  of a conversation returns it to zero (§14).
- **The self-report lists the modules actually loaded** **(critique round
  2)**, not only the DLLs our code asked for: `EnumProcessModules` (psapi,
  delay-loaded) or Toolhelp (`CreateToolhelp32Snapshot`, kernel32), the
  **builder's call**. So a DLL mapped by a path the DLL record does not see
  (COM mapping `sapi.dll`, say) still shows.
- **Teardown at the end of a conversation, and at voice or AI off**, at once:
  - speech stopped (`SPF_PURGEBEFORESPEAK`) and `SpVoice` released; in PR 3
    the play queue dropped and the render client released;
  - any speech request cancelled by closing its handle (`AiNetCancel`);
  - the WebSocket closed from the UI thread, waking both its workers (§6.6);
  - the live and speech threads joined (bounded, as `voice_audio_stop`'s 3 s);
  - the echo-cancellation objects and capture client released, the device
    closed;
  - the session-lock registration removed.
  Windows may keep the DLLs mapped until exit; the claim covers our code.

## 13. Error states and their wording

**Where messages go** **(critique round 2)**: conversation-ending and error
messages are persistent notes in the chat, as voice errors are today
(`ai_chat_voice.c:160-167`). The header carries transient states only (the
cues of §5.2 and short statuses).

Each wording is the **spec author's call**, except where marked:

| When | Where | Text |
|---|---|---|
| Live connection never opened | header | Live text is unavailable: the words will appear when you stop |
| Live connection dropped | header | Live text stopped: the rest will appear when you stop |
| Live key rejected, rate limit, provider error | chat | today's `voice_stt_http_error` texts |
| Batch fallback failed (critique round 2) | chat | The recording could not be transcribed again: the text in the box is what arrived live |
| Send pressed, final text late | chat | The last words did not arrive. Check the text and press Send. |
| Empty turn | header | Didn't catch that |
| Turn dropped as echo (critique) | header | Ignored the assistant's own voice |
| Turn not sent (critique) | chat | The assistant is busy / Connect a session first / The conversation is full: start a New Chat / The request could not start |
| Paused (critique) | header | Paused while Nutshell is in the background / Paused while a dialog is open |
| Conversation ended by a settings change (critique round 2) | chat | Conversation ended: the voice settings changed |
| Conversation ended by lock | chat | Conversation ended: Windows was locked |
| Conversation ended by silence | chat | Conversation ended: nothing said for 3 minutes |
| Conversation ended because a turn did not finish by its deadline, max(180 s, its upload budget plus 10 s) (amended 2026-11-25) | chat | Conversation ended: the last turn took too long |
| Conversation ended by a chat change | chat | Conversation ended: the chat changed |
| Repeat or continue handled | header | Repeating / Carrying on |
| AI speech failed (PR 3) | header | AI speech failed: using Windows speech |
| AI speech cannot work as set up (PR 3, amended 2026-11-25) | chat, once per conversation | AI speech is off for this conversation: <reason>. Replies are read with Windows speech. |
| No speech at all | chat | Could not speak the reply: it is on screen |
| Device lost, privacy block, no microphone | chat | today's texts |
| Start refused | header | today's `voice_busy_text` |

The notice dialog, once per launch (critique round 2; main session's call):
"Windows has no British English woman's voice installed, so replies use
<voice>. To get one, add English (United Kingdom) under Windows Settings >
Time & language > Speech." (PR 3 adds "or choose AI speech in Settings > Voice".)

Settings > Voice lines: "Each spoken reply's text is sent to <host>." (PR 3,
amended 2026-11-25); "Live text as you speak needs OpenAI or Mistral";
the English (UK) voices line (§8.3); "Your microphone reports no echo
cancellation: a headset works best"; "Windows may lower other sounds during a
conversation. To stop that: Control Panel > Sound > Communications > Do
nothing."; "The saved voice is not installed on this PC: using Automatic".

Spoken phrases: "I need your approval to run a command."; "I need your
approval to write a file."; "I need your approval for the next step."; "I
couldn't send that. The details are on screen."; "Something went wrong. The
details are on screen."; "The code is on screen."; "The table is on screen."

## 14. Testing

**Native (`src/core`, `make test`)** **(main session's list, with both
critiques' additions)**:
- OpenAI and Mistral event parsers: deltas, revisions, segment complete,
  errors, unknown events ignored, malformed JSON.
- The message builders: session configuration (language, prompt, model), the
  audio append with base64, commit; CR/LF and quote escaping.
- The live-transcript buffer: committed text plus revisable tail, append-only
  mode, the draft prefix and one space, replacing the tail, Escape's removal.
- The transcript result for a 10-minute recording (over 4,096 bytes, up to
  the 64 KB minimum) and the fallback keeping the live text when it fails.
- The 16-to-24 kHz resampler: length, DC, a tone, no overflow.
- The streaming speech gate (dictation): pre-roll length, hangover, a cough,
  steady noise under the threshold.
- The adaptive speech detector: steady noise at several levels (never
  speech), clicks and door slams (never speech), speech over steady noise,
  speech after the noise floor changes.
- The pause endpointer at 0.5, 1.2 and 3 s, the 300 ms minimum, the 2-minute
  turn cap.
- **The `src/core` controller** (§5.2), for dictation and conversation:
  every state; every end in §4.4 and §5.6, each followed by the ledger at
  zero; the capture closed in Working and Paused and reopened for Listening
  and Speaking; Paused on deactivation and on `WM_ENABLE` FALSE and back; the
  turn dropped on pause; held replies spoken on resume under 30 s and not
  over; the key, Escape and Stop while Speaking stopping speech only; Send in
  Listening and Hearing; each send status leaving Working; Working ending on
  "chat turn idle" and on the safety timeout, never on a busy-changed alone;
  the 3-minute end; a settings change ending a conversation and a recording;
  Play sample stopping speech; approval announcements for each card kind;
  error replies; typing allowed in Working and Paused and locked in Hearing;
  a draft at Listening starting the next turn.
- **The round cap**: a voice turn does not reset the auto-approve round
  counter; a typed message and an approval click do (portable parts in
  `src/core`, the rest in `wintest`).
- The Escape rule: `voice_ptt_step` with Escape in and out of the panel, in
  dictation and conversation; out of the panel it neither discards nor is
  consumed.
- The cut-in detector: duck at about 200 ms, stop at about 400 ms, un-duck
  after a cough, with and without echo cancellation; in half-duplex, no audio
  from before stage 2 in the turn.
- The speakable-text extractor: Markdown stripped, code blocks and tables
  replaced once each, link text without URL, inline code, thinking skipped,
  a reply that is only code.
- The sentence chunker against the 200 and 1,000 byte targets.
- The cut-off mapping: SAPI word offsets; proportional bytes (PR 3).
- The repeat and continue matcher: each phrase; each polite form; case and
  punctuation; 7 words never matching; a phrase inside a longer request never
  matching; no local action with no spoken reply, with one older than 5
  minutes, or with one ending in a question; "continue" after a reply read to
  the end going to the model.
- The echo filter: dropped only during playback and within 2 s after; matched
  against the whole reply; "120 GB free" dropped; "yes, check the D drive"
  (80%) kept; runs of 2 words never counting; two 3-word runs together
  covering 90% dropped; a real answer that shares words with the reply kept.
- The voice choice chain: en-GB female (known names first), any English
  female, any en-GB, system default; duplicates across SAPI and OneCore; a
  saved voice that is gone; when the notice dialog is due (Automatic resolved
  to a voice that is not an en-GB woman's, once per launch).
- The resource ledger's counts.
- Settings: load and save round-trip of every new key, including
  `tts_api_key` and its preserved twin in PR 3; defaults, range clamps, the
  notice lines (one for `voice_windows_voice_name`) and the folding when the
  switch is new; the `k_setting_keys` table against a saved file;
  `voice_auto_send` unchanged; the shared-key rule for AI speech.
- Shortcuts and README block.
- PR 3 (amended 2026-11-25): the WAV stream decoder and Mistral's event
  parser, each failure case of §8.5 with its own test, and the fuzz targets
  `fuzz_wavstream` and `fuzz_aisse` with seeds in `make fuzz-run`.

**`wintest` (Wine on `automaton`, and on Windows)**:
- The WebSocket client against a loopback WebSocket server written in the
  test (`ws://127.0.0.1`), if Wine's WinHTTP supports the API; otherwise a
  defined SKIP. Closing the handle from the UI thread must wake both the
  sender and a receiver blocked in `WinHttpWebSocketReceive`; on Wine this may
  SKIP like `test_ai_nc_fire_wakes_blocked_winhttp_call`.
- **Every end of §5.6 through the Win32 glue**, with fakes where Wine cannot
  provide the real object (capture, `SpVoice`, WebSocket), asserting that the
  resource ledger returns to zero **(critique)**.
- A voice upload and a WebSocket open both counted by `voice_winhttp_open`;
  a chat request not counted.
- The communications-category capture: a defined "no device" or "no echo
  cancellation" outcome under headless Wine, no hang, no leak. The render
  client the same in PR 3.
- The registry voice list against keys the test writes under `HKCU\Software`
  (the reader takes its root keys as parameters).
- `SpVoice` creation: a defined outcome under Wine (it may have no voices).
- Session-lock registration and a posted `WM_WTSSESSION_CHANGE` ending a
  recording.
- Posted `WM_ACTIVATEAPP` and `WM_ENABLE` moving a conversation to Paused and
  back.

**The real exe (`make selftest-wine`)** **(critique round 2 for the module
list and the control)**:
- New runs with voice on in Dictation mode and in Conversation mode with no
  conversation started. Each asserts that `sapi.dll` and `wtsapi32.dll` were
  neither requested by our code nor present in the loaded-module list, and
  that the voice network open count is zero.
- **A control** proves the check can fail: a selftest config, or a
  `--self-report` flag, that does load SAPI (a Play-sample-like call at
  launch), with the check expecting `sapi.dll` in both lists. If Wine cannot
  load `sapi.dll` that way, the control is a Windows hand check instead, and
  the spec's claim rests on it.

**Integration harness (written, not run here: no `pwsh` on `automaton`)**:
- A way to feed a WAV file in place of the microphone is the **builder's
  call**, and must not ship in the exe's normal paths **(critique)**: for
  example compiled only into a test build, never reachable from a release
  exe's command line or config.
- Live dictation with a fixture: words appear in the box, nothing is sent
  until Send.
- A conversation with a fixture "list the files in my home directory": the
  turn is sent after the pause and the run_command card waits, is announced,
  and holds no open mic.
- The self-report case on Windows for Dictation and Conversation modes, and
  the SAPI control.

**Windows hand checks before each merge** (into the notes' Open list):
- Every PR: `mingw32-make test` and a release build on the Windows box
  (§7.5).
- PR 1: live text with OpenAI and Mistral, revisions settling, typing paused,
  Send while recording, Escape in the panel removing the words and Escape in
  the terminal going to the terminal, Alt+Tab keeping them, the connection
  pulled mid-recording (fallback, and the fallback failing), a 5-minute
  recording with pauses (the connection survives idle gaps), a 10-minute
  recording's fallback, English (UK) spelling on OpenAI live.
- PR 2, each listed on its own **(critique)**:
  - **Echo cancellation**: what the effects manager reports on the
    maintainer's devices; a reply on speakers not transcribed back.
  - **Ducking**: whether other apps' audio is lowered during a conversation,
    with the Communications setting at its default and at "Do nothing", and
    with a capture device that is not the default communications device;
    whether Nutshell's own speech is lowered.
  - **Cutting in**: on speakers and on a headset; a cough does not stop the
    reply; the key stopping speech and keeping the conversation; "carry on"
    and "say that again" handled locally; SAPI's stop and volume latency.
  - **Voices**: the default on a PC with and without the English (UK) pack;
    which en-GB voices are listed; a OneCore voice speaking; the notice
    dialog.
  - **Bluetooth**: a Bluetooth headset in a conversation (the communications
    category may move it to its hands-free profile, with worse sound).
  - A full conversation with the free-space question and its approval card
    (the mic indicator off while the card waits); the round cap needing an
    on-screen approval after voice turns; Alt+Tab and Settings pausing;
    Win+L; 3 minutes without a turn; the self-report and the SAPI control.
- PR 3 (amended 2026-11-25): before the PR is marked ready, the maintainer's
  Mistral capture ("Task 0" of the plan: two streams and one forced error,
  committed as fixtures that replace the provisional ones). Then AI speech
  with Mistral (the default voice `voice_1787`, Mary; Sky, Harriet, Jane) and
  with a custom server (Kokoro-FastAPI); the shared, separate and rebound
  keys; the passing fallback and the once-per-conversation note; the privacy
  line; price checks.

## 15. Delivery

Three code pull requests, in order, each with its own version bump and build
**(critique)**. Each is merged before the next starts.

1. **PR 1, "live dictation"** (branch `feat/voice-live-dictation`): this spec;
   §4 (all of dictation, with the Escape-in-the-panel rule and the 64 KB
   transcript result); §6.1 to §6.3, §6.5 and §6.6 (OpenAI and Mistral live,
   `voice_net_open` and `voice_winhttp_open` with the counter, the WebSocket
   transport with its two-worker cancel, `winhttp.def`); §7.1, §7.2 and §7.5
   (the speech gate, pre-roll, the 10-minute buffer, the resampler, guarded
   COM declarations where any are added); §11.1; the PR 1 rows of §12, §13
   and §14. No conversation mode, no playback, no `sapi.dll`, no
   `wtsapi32.dll`.
2. **PR 2, "conversation mode"**: the controller refactor into `src/core`
   (§5.2); §5 (turn taking, the adaptive detector, Paused, the round cap,
   "chat turn idle", every end); §6.4 for conversation turns; §7.3 and the PR
   2 half of §7.4 (SAPI to the default output); §8.1 to §8.3 and §8.5
   (Windows speech, the notice dialog); §9 (echo cancellation, cutting in,
   the echo filter); §10 (local repeat and continue); §11.2; the session
   lock, the resource ledger and the loaded-module self-report of §12; the
   PR 2 rows of §13 and §14.
3. **PR 3, "AI speech"**: the PR 3 half of §7.4 (Nutshell's own render
   client, play queue and byte-to-offset map); §8.4 (its own provider
   setting and key) and the AI half of §8.5; §11.3; the PR 3 rows of §12,
   §13 and §14.

**Reordering was rejected** (critique): the critique suggested building
conversation mode before live dictation. Live dictation stays first because
the maintainer asked for live words explicitly, and conversation mode shows
the same live words, so PR 2 builds on PR 1's transport and buffer.

Later, outside this design (todo) **(main session, plus the last three
spec author's)**:
- Gemini Live (`gemini-3.5-transcribe-live`, BidiGenerateContent WebSocket,
  en-GB supported, 10-minute sessions).
- Groq Orpheus as an AI speech provider, once it leaves Preview (cut from PR 3,
  2026-11-25).
- OpenAI speech over the Realtime API (designed and rejected for PR 3, §16).
- Resampling for Custom servers that do not deliver 24 kHz (piper, say).
- speexdsp as a fallback echo canceller for Windows 10 and devices without one.
- Pseudo-live text for Groq and custom URLs by rolling re-transcription.
- Spoken replies in dictation mode (voice spec §5's speaker toggle), built on
  §8 rather than voice spec §5's design.
- Gemini's speech as an AI speech provider (voice spec §6; its API is not
  `/audio/speech`).
- A spoken "stop" while the assistant works.

Already filed, not in this design: an Other... choice for the push-to-talk
key, and revisiting the header status design.

## 16. Left to the builder: open questions

**For the maintainer to confirm** (main session's calls from critique round
2):
- A. A voice turn does not reset the auto-approve round cap; only a typed
  message, Stop or an on-screen approval click does (§5.2).
- B. With no British woman's voice installed, Windows speech stays the
  default, and the shared notice dialog says so once per launch (§8.3).
- C. Replies held while Paused are spoken on resume only if under 30 s old
  (§5.4).

**Must verify, each with its fallback** **(both critiques)**:
1. **WebSocket timeouts and idle gaps**: set the WebSocket handle's receive
   timeout to infinite and cancel by closing the handle. If idle gaps (only
   speech is sent) drop the session, send silence keep-alives at a low rate,
   or reconnect per utterance.
2. **Closing a WebSocket handle wakes a blocked synchronous
   `WinHttpWebSocketReceive`** (and the sender). Fallback: a receive loop with
   short timeouts that checks a cancel flag.
3. **Provider idle limits**: how long OpenAI's and Mistral's live sessions
   survive without audio. Fallback: as item 1.
4. **Mistral after `transcription.done`**: whether the session closes, which
   means one session per utterance. Fallback: open a session per utterance.
5. **Mistral's batch size limit**, for a 10-minute fallback (§4.5). Fallback:
   split the fallback upload, or cap the live recording at what fits.
6. **OneCore voices with SAPI**: whether a OneCore token works with
   `ISpVoice::SetVoice` in a desktop app on Windows 10 and 11. Fallback: list
   SAPI desktop voices only.
7. **SAPI purge latency, and `SetVolume` taking effect mid-utterance.**
   Fallback: PR 3's own render client for Windows speech too (an in-memory
   SAPI stream played through it).
8. **Self-ducking**: whether Nutshell's own SAPI output is lowered while its
   communications capture is open. Fallback: `IAudioSessionManager2` to our
   process session, then `IAudioSessionControl2::SetDuckingPreference(TRUE)`.
9. **Ducking of other apps** with a capture device that is not the default
   communications device (§7.3). No code fallback; the Voice page line stays.
10. **Echo cancellation**: `IAudioEffectsManager`'s minimum build, and how
    widely devices offer echo cancellation. Fallback: the half-duplex path of
    §9.1 for every device that does not report it.
11. **Bluetooth hands-free profile**: what the communications category does
    to a Bluetooth headset. A Windows hand check; no code fallback beyond the
    Voice page's headset line.
12. **Which en-GB voices are usable** (SAPI, OneCore, Natural-only), §8.3.
13. **Prices, and `gpt-4o-mini-tts`'s retirement and successor**: answered
    2026-11-25 (§16 "Decisions of 2026-11-25: AI speech provider"): it
    shuts down on 2027-01-06 with no `/audio/speech` successor, so OpenAI is
    not an AI speech provider. The chosen providers' prices are in §8.4.
14. **The §3 counts** (57 settings keys, `CFG_KEYS_MAX` 96,
    `NOTICE_ITEMS_MAX` 24, 13 `winhttp.def` entries): rechecked against the
    code on 2026-11-24; recheck at build time, since other work may land
    first.
15. **The SAPI control under Wine** (§14). Fallback: a Windows hand check.

**Other open questions**:
16. OpenAI live: the exact event names, how revisions are signalled, whether
    text arrives before a commit, whether the model takes a prompt, and the
    `delay` value.
17. Mistral live: binary or base64 JSON audio; how to get a segment's final
    text; whether it needs silence sent to flush its delay (§7.2). If it
    needs continuous audio, the plan says so and the "only speech" rule bends
    for it.
18. Which MinGW headers lack `IAudioClient2`, `AudioClientProperties`,
    `IAudioEffectsManager` and `IAcousticEchoCancellationControl`; copy what
    is missing, guarded (§7.5).
19. The thresholds: the adaptive detector's margin over the floor and minimum
    duration, the hangover, cut-in margins with and without echo
    cancellation, the duck level.
20. The current OpenAI preset voice list (PR 3). Superseded 2026-11-25:
    the voices are seeded per provider (§8.4); OpenAI is not offered.
21. The module split (for example `src/core/voice_live.c`, `voice_conv.c`,
    `voice_vad.c`, `voice_speak.c`, `voice_echo.c`, `voice_ledger.c`;
    `src/ui/voice_net_win.c`, `voice_tts_win.c`) and the thread layout (the
    WebSocket's two workers and their cancel, the speech thread).
22. Whether the send status comes from `ai_chat_send_user_message` itself or
    a new voice entry point (§5.2), and the tool-gate card kinds to announce.
23. Where "chat turn idle" is posted from, after the relaunch decision
    (§5.2).
24. How `ai_http_post_ct` takes the voice session (§6.5), and
    `EnumProcessModules` or Toolhelp for the module list (§12).
25. Whether PR 3's render client stays started between replies (§7.4), and
    whether one WebSocket should span a conversation's turns (§6.6).
26. Upload timeouts for a long batch fallback (§4.5).
27. How typing is locked in the box while speech is captured (§4.2).
28. The WAV-injection seam for the harness, kept out of the exe's normal
    paths (§14).

### Found at build time (PR 1, 2026-11-24)

Checked against the providers' live documentation and Microsoft's WinHTTP
reference on 2026-11-24, and against the Mistral Python client's source
(`github.com/mistralai/client-python`, `src/mistralai/extra/realtime/`) where
the prose docs are silent. Sources:
`developers.openai.com/api/docs/guides/realtime-transcription.md`,
`developers.openai.com/api/reference/resources/realtime/client-events.md` and
`server-events.md`, `developers.openai.com/api/docs/pricing.md`,
`docs.mistral.ai/studio/audio/speech-to-text/transcribe-in-realtime`,
`learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpwebsocketreceive`
(and its sibling pages) and `learn.microsoft.com/en-us/windows/win32/winhttp/option-flags`.

**Corrections to §6.2 and §6.3:**
- OpenAI's session is configured with `session.update` carrying
  `session.type: "transcription"` and a nested
  `audio.input.{format, transcription, turn_detection}`; there is no
  `transcription_session.update`. The URL carries no model. The
  `OpenAI-Beta: realtime=v1` header is not sent (the GA interface drops it).
- `gpt-live-transcribe` takes only 24 kHz PCM (`audio/pcm`, `rate: 24000`), so
  the 16-to-24 kHz resampler is required, not a choice.
- It takes `languages` (an array of ISO 639-1 codes), never `language`, and
  it **does take a `prompt`**: the British-spelling sample is sent for English
  (UK), as §4.6 hoped.
- `turn_detection` must be `null` or absent: the model supports no server VAD.
  Nutshell commits (`input_audio_buffer.commit`) as §6.2 planned; an empty
  commit is an error, so a commit is sent only after audio.
- **Revisions**: there is no revision field or event. Deltas are "newly
  available transcript text" per `item_id`, and the production checklist says
  later deltas may correct earlier text. So the tail is per item: its deltas
  are provisional, and `...transcription.completed`'s `transcript` replaces
  them. Completions of different items may arrive out of order; everything is
  keyed by `item_id` and shown in first-seen order.
- `delay` (`minimal` to `xhigh`): the guide shows it with
  `gpt-live-transcribe`, the reference says only `gpt-realtime-whisper` takes
  it, and no default is documented. **Not sent** (builder's call): a rejected
  `session.update` would cost the live text for the whole recording.
- Prices: `gpt-live-transcribe` $0.017 a minute (as reported); batch
  `gpt-transcribe` $0.0045. `gpt-4o-transcribe`, `gpt-4o-mini-transcribe` and
  `whisper-1` are now marked deprecated for batch.
- Mistral: model `voxtral-mini-transcribe-realtime-3` (alias of `-3-0`, Public
  Preview), about $0.008 a minute. Configuration is a message: after
  `session.created`, `session.update` with
  `audio_format {encoding: pcm_s16le, sample_rate: 16000}`,
  `target_streaming_delay_ms` 480 and `language`; nothing may change once
  audio has started. A wrong model fails the handshake with HTTP 403.
- §16 item 17 answered: Mistral audio is **base64 JSON text frames**
  (`input_audio.append`, at most 262,144 decoded bytes each), never binary.
  `transcription.text.delta` only appends and is final on arrival;
  `transcription.done` carries the transcript of record and ends the session
  (one session per recording). `input_audio.flush` forces pending text out
  (from the client's source, not the prose docs); Nutshell sends it when the
  speech gate closes and before `input_audio.end` at stop, so the "only
  speech" rule holds without continuous audio. Verified on Windows by hand.

**The must-verify items PR 1 needs:**
1. *WebSocket timeouts and idle gaps.* Whether WinHTTP's receive timeout
   applies to `WinHttpWebSocketReceive` is not documented. Nutshell sets the
   WebSocket handle's receive timeout to 0 (none) and ignores a refusal;
   cancellation never relies on a timeout. WinHTTP sends keepalive pings every
   30 s by default (minimum 15 s), which covers the TCP side of an idle gap.
   Whether the providers drop a session during long pauses is item 3.
2. *Waking a blocked receive.* Microsoft documents `WinHttpWebSocketClose` as
   what cancels a pending receive (`ERROR_WINHTTP_OPERATION_CANCELLED`);
   `WinHttpCloseHandle` waking it is undocumented (Wine does it). The two-worker
   cancel is therefore: the UI asks the session to end, the sender thread
   calls `WinHttpWebSocketClose` (close timeout 2 s), which ends the
   receiver's wait; the UI joins both threads later, without blocking; at voice
   off and shutdown the join is bounded at 3 s, then `WinHttpCloseHandle` is
   the last resort and a thread still stuck is detached, as the STT workers
   are. Send and receive run on two threads at once, which the API allows
   (one pending of each). Measured under Wine 9 (`wintest`): neither
   `WinHttpWebSocketClose` nor closing the handle wakes a blocked synchronous
   receive, and closing the request handle does not abort a blocked
   handshake, so those tests take a defined skip there. A Windows hand check
   confirms the wake-up.
3. *Provider idle limits.* Not documented by either provider. OpenAI's
   Realtime sessions last at most 60 minutes. No keep-alive audio is sent; a
   dropped session falls back to the batch transcription (§4.5). The 5-minute
   recording with pauses is a Windows hand check.
4. *Mistral after `transcription.done`*: the session is over (the client
   closes it). One session per recording, as §6.6 planned.
5. *Mistral's batch size limit*: not documented (the limits page lists no
   size the docs render; an older page says recordings up to 3 hours). The
   live recording keeps its 10-minute cap; a 10-minute fallback to Mistral is a
   Windows hand check.
14. *The §3 counts* at build time: recorded in the PR body by the settings
    task (`k_setting_keys` 57 before `voice_stt_live_model`, 58 after;
    `CFG_KEYS_MAX` 96; `NOTICE_ITEMS_MAX` 24; `winhttp.def` 13 entries before,
    17 after the four WebSocket ones).
16. Answered above (event names, revisions, text before commit: yes, deltas
    arrive as audio is appended; the prompt: yes; `delay`: not sent).
24. `ai_http_post_ct_ex` takes the caller's WinHTTP session (from
    `voice_net_open`) and the send and receive timeouts. The module list is
    the existing Toolhelp snapshot in the self-report.
26. Upload timeouts scale with length: send 30 s plus 1 s per 64 KB, receive
    60 s plus 1 s per 10 s of audio, each capped at 10 minutes.
27. Typing is locked by the input's subclass (`WM_CHAR` but Ctrl+C, paste,
    cut, clear, undo, IME characters, Delete and Backspace), which keeps the
    theme's colours; it stays locked until the recording's final text (or the
    fallback's) is in the box, so a late replacement can never land on top of
    an edit.
28. The WAV-injection seam and the harness's live-dictation case are left to
    PR 2, where the conversation cases need the same seam; PR 1's live path is
    covered by native tests, `wintest` against a loopback WebSocket server that
    speaks both providers' protocols, and the Windows hand checks.

**Also found at build time:** the JSON tokenizer caps a string at 8,191
bytes, so a long transcript (a 10-minute recording's `transcription.done`, or
a long batch result) would be rejected. A shared scanner
(`src/core/json_scan.c`) reads one large string field when the full parse
fails, for the batch result and the two live final events. The WebSocket
upgrade never follows a redirect, so the bearer key cannot be re-sent to
another host.

**Builder's calls made in PR 1:**
- Ends of a dictation recording (§4.4) fall in two kinds. *Soft*: losing
  activation, a modal dialog, a chat change, the panel closing and a lost
  device stop the mic and let the last words arrive (or the fallback run), and
  never send. *Hard*: voice or AI off and a voice settings change close
  everything at once. A batch recording that loses activation is still
  discarded, as before.
**After the final review (claude-fable-5-2, 2026-11-24)**, these replace the
first build's choices where they differ:
- **The 3 s wait for the final text (§4.3) falls back instead of giving up.**
  If the live text is neither final nor failed 3 s after the mic stops, the
  live connection is treated as failed, and the whole recording is transcribed
  once in batch (§4.5). The status says "Live text stopped: the rest will appear
  when you stop". A Send pressed meanwhile sends after the fallback result, so
  "The last words did not arrive…" (§4.3, §13) is no longer shown. With no
  audio, the recording ends with "Didn't catch that". A recording is never
  wiped without a fallback or a status.
- **Timeouts and overflow.** A live session that is not ready 10 s after it
  starts is a failed connection ("Live text is unavailable…"). An overflowing
  send queue is a failed connection too.
- **Provider errors.** A provider `error` event is shown once in the chat and
  does not end the session; only the connection closing does. Unknown event
  types are logged by type only.
- **OpenAI items.** An OpenAI item is placed in the box in commit order, from
  `input_audio_buffer.committed`, so a completion that arrives before any delta
  still lands in the right place. A commit of less than 100 ms of audio is not
  sent.
- **Live models.** Only `gpt-live-transcribe` is offered for OpenAI;
  `gpt-realtime-whisper` was dropped, since its session shape is unverified. An
  invalid live model name says so in the header: "The live model name is not
  valid: the words will appear when you stop".
- **Ends of a recording (§4.4):**
  - Hold mode's short-tap and chord discards are immediate discards, as before.
  - Losing activation, a modal, a chat change and a lost device are soft ends:
    the last words arrive and nothing is sent.
  - An out-of-panel Escape during the stopping press counts as a chord.
  - Escape with focus in the panel cancels a running fallback or batch upload,
    keeps the live text and unlocks the box ("Transcription cancelled").
  - Send pressed while an upload runs says "Will send when the transcript
    arrives".
  - Send with a typed draft and no new words sends the draft.
- **Teardown.** Each live session's threads tell the UI when they exit, so the
  session is joined while voice sits idle. A session closed more than 5 s ago
  is aborted at the next reap.
- **The input box.** Starting a recording does not move the caret or the
  selection.

- Send pressed while a fallback transcription runs waits for it (bounded by
  the upload's timeouts, not by 3 s), then sends; if the fallback fails nothing
  is sent.
- Send pressed while a **batch** recording runs stops it and sends once the
  transcript is in the box.
- The speech gate opens after 40 ms of speech over the fixed threshold, with
  300 ms of pre-roll; its hangover is 500 ms for OpenAI and 800 ms for Mistral
  (480 ms streaming delay plus a margin). Audio is sent in 100 ms messages.
- The 16-to-24 kHz resampler is a 48-tap polyphase windowed-sinc filter.
- "A voice setting changed" is a digest of every voice field, the speech key
  and the AI provider fields "Use current AI assistant" reads.
- The input box is a plain (ANSI) `EDIT`, so the controller measures every
  position in the control's own units and replaces the whole recording's text
  on each update; characters outside the ANSI code page show as "?", as typed
  ones do today. If something else changes the box during a recording, it ends
  softly with "Live text stopped: the box was changed".
- Before any send, the chat binding is checked again; a changed chat gets
  "Voice message not sent: the chat changed" and nothing is sent. A Send that a
  soft end then cancels gets "Not sent: check the text and press Send".
- While a live recording waits for its last words after the mic stops, the
  header shows "Transcribing…" rather than "Listening…".
- A batch recording keeps today's rules: auto-send is decided when the
  transcript arrives (empty box), and losing activation while recording still
  discards it.

### Found at build time (PR 2, 2026-11-24)

Built and tested on `automaton` (native suite, `wintest` under Wine, MinGW
release compile) with no microphone, no SAPI engine that works and no Windows
audio stack, so most of the audio and speech facts below are *Windows hand
check pending*. "Measured here" means a test or command on `automaton`;
"reported" means read from a header or a metadata mirror, not run.

**The must-verify items PR 2 needs:**
6. *OneCore voices with SAPI.* Not measured: Wine has no usable SAPI (item 15).
   Ruling (builder's call): the list is deduplicated by name and **the SAPI
   desktop token is kept over a OneCore token of the same name**, so the
   unverified `ISpVoice::SetVoice` path with a OneCore token is only reached
   for a voice SAPI does not list itself. `VTT_READY.hr` carries a `SetVoice`
   failure and the default voice is kept. Windows hand check pending: a
   OneCore-only voice speaking.
7. *SAPI purge latency and `SetVolume` mid-utterance.* Not measured here (no
   SAPI under Wine). The speak path is built on `ISpVoice` as designed (word
   offsets, end of reply, purge, volume); the PR 3 render-client fallback is not
   built. Windows hand check pending: the purge latency, the duck level
   applying mid-reply, the word offsets, the end of a reply and the notify
   event's reset mode.
8. *Self-ducking.* The opt-out is **built up front, not held back as a
   fallback**: our default render session gets `SetDuckingPreference(TRUE)`
   through `IAudioSessionManager2` / `IAudioSessionControl2`. Costs nothing if
   it was not needed. Whether the opt-out works, and whether our own speech
   is lowered at all, is *Windows hand check pending*.
9. *Ducking of other apps.* No code fallback, as written. Windows hand check
   pending, with the Communications setting at its default and at "Do
   nothing", and with a capture device that is not the default communications
   device.
10. *Echo cancellation.* The effects-manager interface is not in the MinGW
    headers or in Wine. `IAudioEffectsManager` (4460B3AE-4B44-4527-8676-7548A8ACD260;
    methods in the order Register, Unregister, GetAudioEffects,
    SetAudioEffectState) and `IAcousticEchoCancellationControl`
    (F4AE25B5-AAA3-437D-B6B3-DBBE2D0E9549; one method,
    `SetEchoCancellationRenderEndpoint`) are taken from the windows-rs 0.58.0
    metadata mirror of the SDK's `audioclient.h` (reported; Microsoft's pages
    give the names and the Windows 11 availability, not the IIDs). The echo
    cancellation effect id (6f64adbe-8211-11e2-8c70-2c27d7f001fa) is from
    Microsoft's audio-signal-processing-modes page. **A wrong value fails
    safe:** the capture reports echo cancellation 0 and the half-duplex path of
    §9.1 is used. Under Wine only that failure path ran (measured here: the
    capture opens or reports an error, never hangs, ledger at zero). Windows
    hand check pending: what the effects manager reports on the maintainer's
    devices, the render-endpoint reference, and a reply on speakers not
    transcribed back.
11. *Bluetooth hands-free profile.* Not built beyond the Voice page's headset
    line. Windows hand check pending.
12. *Which en-GB voices are usable.* Not measured: no voices under Wine.
    The choice chain is built and tested on a fake list (§14). Windows hand
    check pending: which voices are listed on each of the maintainer's PCs.
14. *The §3 counts, rechecked on 2026-11-25 against the branch.* Settings
    keys: **62** (the spec's 57 was 2026-11-24; `main` at `6733361` has 58,
    `voice_stt_live_model` arrived with PR 1; PR 2 adds `voice_mode`,
    `voice_pause_ms`, `voice_windows_voice` and `voice_windows_voice_name`).
    `CFG_KEYS_MAX` is **96** and `NOTICE_ITEMS_MAX` is **24**, unchanged.
    `build-support/delayload/winhttp.def` lists **17** entries (13 at the
    spec's count, plus the four WebSocket calls PR 1 added; PR 2 adds none).
    PR 2 adds `build-support/delayload/wtsapi32.def` (session-lock
    registration; `wtsapi32.dll` is delay-loaded and absent from the static
    import table, checked with `objdump -p` on the unpacked exe). Greps:
    `awk 'NR>=484&&NR<=552' src/config/loader.c | grep -c '^ *CK('` (the
    `k_setting_keys` table, `git show 6733361:src/config/loader.c` for
    `main`); `grep -n 'define CFG_KEYS_MAX' src/config/config.h`;
    `grep -n 'define NOTICE_ITEMS_MAX' src/core/notice.h`;
    `grep -c '^WinHttp' build-support/delayload/winhttp.def`.
15. *The SAPI control under Wine.* Measured here: `make selftest-wine` passes
    in all five modes (off, voiceoff, on, conv, sapi). Under this Wine
    `CoCreateInstance(CLSID_SpVoice)` **succeeds**; `ISpVoice::SetInterest`
    and `SetNotifyWin32Event` return `E_NOTIMPL`, so speech cannot be set up
    (the Task 12 report first attributed this to `CoCreateInstance`, which was
    wrong). The report still shows `requested: sapi.dll` and `module:
    sapi.dll`, so the control proves the check can fail: the `sapi` mode
    asserts both lines, and no `wtsapi32.dll`, and notes `sapi-control:
    unavailable`. It would skip only if `sapi.dll` were never requested. The
    `on` and `conv` modes assert neither `sapi.dll` nor `wtsapi32.dll` is
    requested or mapped, and `conv` asserts `mmdevapi.dll` is requested and
    that the voice network open count is 0. The control's result on Windows
    (`sapi-control: held`) is *Windows hand check pending*.

**The open questions PR 2 answers:**
18. *MinGW headers.* The toolchain has `IAudioClient2`, `AudioClientProperties`
    and `IAudioSessionControl2`. `IAudioSessionManager2`'s IID
    (77AA99A0-1BD6-484F-8BC7-2C654C9A9B6F) is the SDK's value. The two echo
    cancellation interfaces and their `AUDIO_EFFECT` / state types are copied
    from the metadata mirror, each inside its own `#ifndef` guard, so a newer
    header wins (§7.5). The SAPI GUIDs (`CLSID_SpVoice`, `CLSID_SpObjectToken`,
    `IID_ISpVoice`, `IID_ISpObjectToken`) come from MinGW's `sapi53.h`; none is
    hand-written. A newer MSYS2 header set on the Windows box may collide:
    *Windows hand check pending* (`mingw32-make test` and a release build).
19. *The thresholds, as built* (`src/core/voice_vad.h` and `voice_listen.h`
    carry the numbers):
    - Detector, 20 ms windows: margin 12 dB over the floor (plus the extra
      while we speak), absolute minimum -55 dBFS, a burst opens after 6 of 8
      windows over the bar and closes after 15 windows without one; steady
      sound (50 windows within 4 dB) counts as noise; digital silence (-85 dBFS or less) never changes the floor, and
      the floor is clamped at -75 dBFS.
    - **Impulse rule:** a burst of at most 40 candidate windows that rises at
      least 3 dB from the window before and decays **15 dB or more** from its
      peak to its last candidate window is an impulse (`VV_IMPULSE`), and its
      windows are taken off the turn's speech. 15 dB, not 6: a smooth falling
      400 ms word was dropped at 6 dB; a slam decays 30 dB or more. The cost
      if wrong is one wasted transcription and "Didn't catch that".
    - A turn needs 300 ms of speech (`VOICE_SHORT_TAP_MS`), so a one-syllable
      "no" under 300 ms is not a turn. Pause 0.5 to 3 s, turn cap 2 minutes.
    - **Cut-in counting rule (builder's call):** speech windows count
      cumulatively and reset only after 300 ms of non-speech (the un-duck
      rule), in both speak modes; the plan's "at most two gap windows" rule
      never fired on modulated speech in half-duplex. Measured here with a
      flat tone: with echo cancellation, duck at 59 and cut at 69 windows from
      the start (about 200 and 400 ms after speech began, as designed); half
      duplex, duck at 109 and cut at 119 with the user starting at window 100.
    - **Half-duplex echo estimate:** follows the reply's level instantly for
      the first 25 windows, then rises at most 0.5 dB a window, and is held
      while a cut-in is accumulating; a candidate must be at least 10 dB over
      it. Known limit (hand check): speech in the first 500 ms of a reply lifts
      the estimate and can block a cut-in for a few seconds; a reply that
      jumps more than 10 dB louder while a cut-in is counting could cut with
      no user.
    - Real speech syllable dips and the 4 Hz modulated test tone are the only
      speech shapes tried: *Windows hand check pending* on speakers and on a
      headset (a cough must not stop a reply).
21. *The module split and thread layout, as built.*
    - `src/core`, portable: `voice_vad.[ch]` (detector and pause endpointer),
      `voice_listen.[ch]` (listener: turn and cut-in events), `voice_conv.[ch]`
      (the conversation state machine), `voice_speak.[ch]` (speakable text,
      sentence chunks, the question check), `voice_echo.[ch]` (echo filter and
      the repeat and continue matcher), `voice_voices.[ch]` (voice list,
      dedupe, the choice chain and when the notice is due). The turn classifier
      (`voice_turn_classify`) is in `voice_echo.[ch]` and the card kinds are in
      `voice_conv.[ch]`; the resource ledger needed no new file.
    - `src/ui`, Win32: `voice_conv_win.[ch]` (the controller glue: event queue,
      one flag per resource, ops table), `voice_tts_win.[ch]` (SAPI),
      `voice_voices_win.[ch]` (registry voice list), `voice_lock_win.[ch]`
      (session lock), plus the conversation capture inside
      `voice_audio_win.c` and the routing in `voice_win.c`, `window.c` and the
      `ai_chat*.c` files.
    - Threads: the audio thread (one capture, now also the conversation
      capture, with its own commands and tokens) and **one speech thread per
      speech context** (`tts_thread`, at most two contexts keyed by token: the
      conversation's and the Play sample's or the self-report's). A context is
      held after an error until the UI reaps it with `voice_tts_reap(token,
      gen)`; the generation stops a stale exit message from killing a restarted
      context, and a matching reap always goes through the bounded quit
      teardown. No other thread was added.
22. *Send status.* It comes from `ai_chat_send_user_message_ex(d, voice_turn)`,
    which `ai_chat_send_user_message` now wraps, and from
    `ai_chat_voice_send_turn(panel, voice_turn)`. Statuses: `VSS_SENT`,
    `VSS_BUSY`, `VSS_NO_SESSION`, `VSS_FULL`, `VSS_LAUNCH_FAILED`, `VSS_EMPTY`
    (`ai_chat_launch_stream_thread` now returns an `int`). `ai_rounds_on_send`
    keeps the auto-approve round count for a voice turn and resets it for a
    typed one; Stop still resets it. Card kinds announced: `VCARD_COMMAND`
    (`run_command`), `VCARD_WRITE` and `VCARD_OTHER`. No tool writes a file
    today, so `VCARD_WRITE` matches a forward-looking name list in
    `voice_conv.c`: a new write tool must be added there. A card is posted
    once, from `reply_batch_build` and from the tool-decide ask path.
23. *"Chat turn idle".* `voice_turn_check` in `ai_chat_voice.c` posts
    `WM_VOICE_TURN_IDLE` when the chat is idle (not busy, no card waiting) at
    every busy-changed site, at the end of the displayed session's response,
    after a launch failure, after a card answer and after a stream cancel. It
    posts rather than calls. Because the busy-changed sites run before the
    relaunch decision, and one turn can post twice, **the controller re-checks
    `ai_chat_voice_idle()` when the message arrives** and drops it outside
    Working; the 3-second idle poll starts only after the chat has been seen
    busy for the turn (with a 10-second fallback for a chat that never shows
    busy).
27. *Typing lock.* `voice_conv_is_typing_locked()` is true while a turn is
    open: Hearing, Ending, and Speaking while an echo-cancellation cut-in
    candidate is open (Listening, Working, Paused and the rest leave the box
    free). It was renamed from the plan's `voice_conv_typing_locked(void)`,
    which clashed with the core function of the same name. A typed Send while
    Listening with no speech heard is a typed message (it resets the round cap)
    and moves the conversation to Working.
28. *The WAV seam.* A separate harness build, `build/win/nutshell-it.exe`
    (`make harness-exe`), compiled with `-DNS_VOICE_WAV_SEAM`; it adds
    `--voice-wav=<path>` and a wall-clock-paced feed in place of the microphone.
    **It is never in the release exe.** `make seam-leak-check` compiles the
    three seam files with the release flags and greps the assembly for the
    seam's markers; `make release` also greps the linked, pre-UPX exe and
    aborts before packing. Measured here: the check passes on the release flags
    and fails with the seam forced on. The seam build writes `<wav>.capture`
    (open or closed) and `<wav>.header` (the painted approval header) so the
    PowerShell cases can assert what the self-report cannot show mid-run (the
    self-report is written once at start-up). The two harness cases
    (`voice_live_dictation_fixture`, `voice_conversation_card_fixture`, in
    `tests/integration/cases/97-voice.ps1`) are written and never parsed or
    run: no `pwsh` here. Both need `NUTSHELL_IT_STT_KEY` (OpenAI) besides the
    model key, and the conversation case approves the card and only waits,
    without asserting the command ran.

**The builder's calls, where a reader of the spec would expect otherwise:**
- **No speech-rate setting** (`voice_speech_rate`): the spec makes it
  optional; one setting row if it is wanted.
- **Key mode greyed whole** in Conversation mode: a single combo item cannot be
  greyed, so the whole Hold choice combo is.
- **Held replies and phrases.** A reply or a "went wrong" phrase held while Paused is spoken on resume only if
  under 30 s old (strict). A card raised while Paused is recorded and announced
  once on resume with no age limit: the card stays on screen, and the spoken
  line is only a cue (§5.4 gives the 30 s rule for replies; the code keeps it
  for replies and the held phrase). A pause that begins while an approval
  announcement is being spoken cuts it off; it is not repeated. A typed Send while Paused is accepted and drops any held reply.
  A notice open for longer than 30 s therefore loses the held reply.
- **Silence clock.** The 3-minute clock restarts when Listening resumes after
  Speaking (a long command or a long reply must not end the conversation) and
  does not restart on an empty, echo or abandoned turn (a noisy room must not
  keep a conversation alive).
- **Send while Hearing** ends the turn through a capture "end turn now"
  command (`voice_audio_conv_end_turn`): the same path as the endpointer's
  pause (live end, turn event, audio kept), so the batch fallback still works
  if the live link fails. The capture API of the first draft had no such
  command.
- **A pre-empted conversation capture** (a dictation or Test open, another
  conversation, or a non-conversation end arriving first) posts `VAE_CONV_ENDED`
  under the conversation's token, which ends the conversation as a lost device.
  `VAE_NO_MEMORY` under it is per turn and non-fatal. Capture tokens are unique
  per open.
- **Speech contexts are reaped by generation** (item 21).
- **Lock during a batch dictation recording:** the key state machine was left
  latched after a lock discarded the recording; `post_step` now ends the
  capture whenever dictation returns to idle. Windows hand check pending.
- **The Settings Voice page** adds Mode, Pause before sending, Windows voice and
  Play sample (`--ui-demo=settings-voice`). `settings.c` is not in the
  `wintest` build, so the layout has no automated test: the Mode and greying
  logic is tested through a pure helper (`voice_mode_enables`) and the layout is
  a hand check. Play sample does not end the conversation: Settings is modal, so
  the conversation is Paused and its speech already stopped; the button then
  speaks the sample. It does nothing, silently, if Enable voice and Conversation
  are ticked but not yet saved. Voice off and shutdown quit the sample's context
  too.
- **Win+L** ends a dictation or conversation recording through
  `WTSRegisterSessionNotification` (delay-loaded `wtsapi32.dll`, registered
  only while a recording or conversation runs). **An RDP or console disconnect
  (`WTS_REMOTE_DISCONNECT`, `WTS_CONSOLE_DISCONNECT`) counts as a lock** (main
  session's ruling after the final review, fail safe: nobody is at the
  microphone), for dictation and conversation alike; the note still reads
  "Windows was locked".
- **Final review fixes (2026-11-25).** A cut-in's turn is owned by the listener:
  the controller's `VCE_CUT` sends no mode command, and the audio thread's
  mode command for LISTEN is a no-op while the listener is already in a turn
  (it used to wipe it). A mode command that does wipe an open turn, and an
  end-turn with none open, post `VLN_TURN_ABANDON` so the controller never
  waits for audio that cannot come. Hearing (130 s), Ending (180 s, longer than the upload's own timeouts) and
  Speaking (10 min) have watchdogs back to Listening. The resume after a
  modal dialog is posted (`WM_VOICE_ACTIVATION`) so Settings' apply runs first.
  A turn or reply for a changed chat ends the conversation instead of being
  sent or read. The voice key works with the focus outside the panel while a
  conversation runs. Every reply starts at full volume (the cut and each
  speech reset the duck).
- **Opus review fixes (2026-11-25).** A speech thread that reports an error stays
  in its loop (no `VTT_EXITED`), so the glue quits it on `VTT_ERROR` for the
  current reply (an older reply's error is ignored) and voice off and shutdown
  quit every speech context (`voice_tts_quit_all`). A dialog that disables only
  the undocked AI panel pauses the conversation (`dialog_open` op; the panel's
  `WM_ENABLE` TRUE posts its resume). The speak mode is recomputed from each
  new capture's echo-cancellation report. A local repeat or continue does not
  restart the 3-minute silence clock (§5.6: from the last turn sent). Deadlines
  compare as signed differences so the 32-bit tick wrap is safe. Not fixed:
  a send that bypasses `voice_send_pressed` (Retry) does not take the
  conversation to Working (notes, Open list).
- **Not exercised by any test:** the real ops table and the routing in
  `voice_win.c`, `window.c` and `ai_chat*.c`. They compile clean under MinGW
  with `-Werror` and the extra flags, but no `wintest` links them.
- **Test counts at the end of PR 2's code:** 4525 native on `automaton` (libssh2
  stub), 173 `wintest` (after the final review fixes).

### Decisions of 2026-11-25: AI speech provider

**The shutdown.** Checked on the deprecations page on 2026-11-25.
- `tts-1`, `tts-1-hd` and `gpt-4o-mini-tts` (with `-2025-03-20` and `-2025-12-15`) were
  noticed on 2026-10-01 and shut down on **2027-01-06**. The named replacement is
  `gpt-realtime-2.1-mini`.
- OpenAI's text-to-speech guide says: "Migrate to `gpt-realtime-2.1-mini` using the Realtime
  API" and "`gpt-realtime-2.1-mini` isn't a supported model for `/v1/audio/speech`".
- `/v1/audio/speech` itself is not deprecated, but it has no OpenAI model after that date.

Sources: `developers.openai.com/api/docs/deprecations.md`,
`developers.openai.com/api/docs/guides/text-to-speech.md` and
`developers.openai.com/api/reference/resources/audio/subresources/speech/methods/create.md`.

**Realtime speech was designed, critiqued and rejected.** The maintainer first chose OpenAI
speech on the Realtime API. That amendment is commit `f79e110` (branch
`docs/voice-realtime-speech-spec`, its own §16 subsection). A `claude-fable-5-2` (high)
critique found it unsound:
1. The only check that a conversational model read the text word for word was its own
   transcript. OpenAI's reference calls that transcript "model-generated", and it can differ
   from the audio.
2. The matcher let substitutions through.
3. The rule that released audio only as far as it was verified was wrong in both directions.

**The maintainer's decision (2026-11-25): "Real TTS provider instead".**
- Build the `/audio/speech` design for providers with a true text-to-speech model.
- No OpenAI AI speech after 2027-01-06, and no reading verifier.

A second `claude-fable-5-2` critique (of `8b86ecd`) returned "sound after changes". Its
findings and the main session's rulings are §17 round 3. They cut Groq, removed the resampler,
added the Mistral capture ("Task 0") and made the changes recorded below.

**What the providers offer** (2026-11-25):

| Provider | Endpoint | Model | Voices | Audio | Streams | Price | In PR 3 |
|---|---|---|---|---|---|---|---|
| Mistral | `https://api.mistral.ai/v1/audio/speech` | `voxtral-mini-tts-3-0` (`voxtral-mini-tts-2603` is Voxtral 2, deprecated) | 36 Voxtral 3 and 30 Voxtral 2 presets. English (UK): Mary `voice_1787` (female), Sky `voice_1786`, Harriet `voice_590`, Jane `gb_jane_*` (Voxtral 2, "still accepted" by Voxtral 3) | mp3, wav, pcm, flac, opus; mono 24 kHz; `pcm` is float32 LE | `stream: true`: server-sent events | $16 per 1M characters | **yes, the default** |
| Groq | `https://api.groq.com/openai/v1/audio/speech` | `canopylabs/orpheus-v1-english`, Preview | 6, no accent stated | wav only, 200 characters a request | not stated | $22 per 1M characters | **no (cut)**: 10 requests a minute, 100 a day; Preview models "should not be used in production" |
| OpenAI | `https://api.openai.com/v1/audio/speech` | `gpt-4o-mini-tts` and the rest | 13 | pcm 24 kHz s16le, wav | yes | $12 per 1M audio tokens | **no**: shuts down 2027-01-06 |
| Gemini, Anthropic, DeepSeek, Moonshot | no `/audio/speech` found | — | — | — | — | — | no |
| Kokoro-FastAPI (local) | `http://localhost:8880/v1`, no key by default | `kokoro` | `bf_emma` (English (UK)), `af_heart` and more | streamed WAV with `0xFFFFFFFF` sizes, 24 kHz s16 | yes | free | as Custom |
| openedai-speech (local) | `/v1/audio/speech` | `tts-1` (piper), `tts-1-hd` (xtts) | OpenAI's names | piper 22,050 Hz (refused: not 24 kHz) | yes | archived 2026-01-04 | as Custom, xtts only |

**Verified by the critique of `8b86ecd`:**
- **Mistral's request fields** are `input`, `model`, `voice_id`, `response_format` and
  `stream`. An unknown field gets HTTP 422 "Extra inputs are not permitted". Without
  `stream`, the reply is JSON with base64 `audio_data`. Source:
  `raw.githubusercontent.com/mistralai/client-python/main/src/mistralai/client/models/speechrequest.py`
  and `.../speech.py`.
- **Mistral's stream** in the SDK:
  - `SpeechStreamAudioDelta { audio_data: str; type = "speech.audio.delta" }`;
  - `SpeechStreamDone { usage; type = "speech.audio.done" }`;
  - a wrapper `SpeechStreamEvents { event; data }` filled from the `event:` line;
  - `Accept: text/event-stream`;
  - no `[DONE]` sentinel.
- **Mistral's audio** is mono 24 kHz
  (`docs.mistral.ai/studio/audio/specs-and-limits/audio-formats`). Its `pcm` is float32 LE,
  per Mistral's older speech page and the Pipecat and LiveKit clients.
- **Mistral's voices** (`docs.mistral.ai/studio/audio/text-to-speech/voices`): Jane is English
  (UK); `gb_jane_neutral` is a Voxtral 2 preset that Voxtral 3 "still accepts"; `voice_1787`
  is Mary, English (UK), female. LiveKit's plugin refuses `voice_<n>` ids on
  `voxtral-mini-tts-2603`.
- **Groq's limits:** Orpheus allows 10 requests a minute and 100 a day (Groq's rate-limits
  page), and Preview models "should not be used in production".
- **Kokoro-FastAPI** (`github.com/remsky/Kokoro-FastAPI`): `http://localhost:8880/v1`, no key
  by default, streamed WAV with `0xFFFFFFFF` sizes, 24 kHz s16.

**Earlier sources:**
- `docs.mistral.ai/studio/audio/text-to-speech/generate-speech` and `.../stream-speech`;
  `mistral.ai/news/voxtral-tts` (the price);
  `reference-server.pipecat.ai/en/stable/_modules/pipecat/services/mistral/tts.html`.
- `console.groq.com/docs/text-to-speech`, `.../orpheus.md` and
  `console.groq.com/docs/model/canopylabs/orpheus-v1-english`.
- `aistudio.google.com/docs/openai`.
- `github.com/matatonic/openedai-speech` and `speaches.ai/usage/text-to-speech/`.

**Not verified by anyone.** These are what "Task 0", the maintainer's capture, settles:
- the raw bytes of a Mistral event (whether `type` sits inside `data`);
- one WAV header per stream or one per delta;
- the delta sizes;
- how an error in mid-stream is reported;
- that `voice_1787` speaks as Mary.

Also not verified: LocalAI (its page did not load), and that Anthropic, DeepSeek and Moonshot
have no speech endpoint (none was found).

**Rulings** (spec author's call unless marked). The first two are unchanged from the first
version of this subsection; the rest supersede it.
- `Ruling: the AI speech providers are Mistral and Custom; OpenAI is not a preset — a version shipped six weeks before its speech models shut down should not offer them — cost if wrong: an OpenAI user enters it as Custom, which works until 2027-01-06.`
- `Ruling: Mistral is the default speech provider — a true text-to-speech model with English (UK) women's voices, streaming, under no notice — cost if wrong: Speech still defaults to Windows speech, so this matters only once AI speech is chosen.`
- `Ruling (main session, critique round 3): Groq is cut — Orpheus is Preview, at 10 requests a minute and 100 a day, too few for a conversation — cost if wrong: Groq users wait for a later PR.`
- `Ruling (main session, critique round 3): no resampler; exactly 24,000 Hz is accepted, 16-bit or float32, mono or stereo (down-mixed); anything else is "unsupported audio format" — Mistral and Kokoro both deliver 24 kHz, and conversion code with no user is cost without benefit — cost if wrong: a Custom server at 22,050 Hz (piper) cannot be used.`
- `Ruling: every Custom request asks for wav, and Mistral is asked for wav unless the capture shows one header per delta, in which case pcm (float32, 24 kHz, mono) — wav describes itself; pcm is the fallback the capture can choose with a one-line change — cost if wrong: none beyond Task 0.`
- `Ruling (main session, critique round 3): an unknown provider id, openai and gemini included, is a resolve error, never Mistral — a silent switch would send the text and key to a provider the user did not choose — cost if wrong: none.`
- `Ruling (main session, critique round 3): a saved speech key is bound to its provider; the binding (mistral, or custom and the normalised base URL) is stored inside the encrypted tts_api_key value, before the key, so no new settings key or notice line is needed; a key whose binding does not match is not used — the smallest form that cannot send a key to a host it was not entered for — cost if wrong: changing the provider asks for the key again.`
- `Ruling (main session, critique round 3): a setting that cannot work (resolve error, HTTP 401, 403, 404, 422, unsupported audio) gives one chat note per conversation and no more tries in it; network errors, 429 and 5xx keep the per-reply fallback — a broken setting must be said once, not hidden behind a header on every reply — cost if wrong: a 404 from a passing server fault turns AI speech off until the next conversation.`
- `Ruling (main session, critique round 3): the style is empty by default and sent only to Custom when the user typed one — Mistral takes the accent from the voice, and Custom servers may refuse unknown instructions — cost if wrong: one default string.`
- `Ruling (main session, critique round 3): the default Mistral voice is voice_1787 (Mary, English (UK), female), to be confirmed by Task 0; voxtral-mini-tts-2603 is not offered — Mary is a documented Voxtral 3 English (UK) woman; 2603 is deprecated and refuses voice_<n> ids — cost if wrong: the capture shows another voice is better, and the default changes before ready.`
- `Ruling (main session, critique round 3): in the text sent to a provider, [ and ] become ( and ), byte for byte — a reply cannot steer the voice with bracketed cues, and the offsets stay valid — cost if wrong: a bracket reads as a parenthesis.`
- `Ruling (main session, critique round 3, rejecting a finding as stated): plain http:// stays allowed for Custom, with the warning in the help, as speech-to-text allows it — a self-hosted server on the LAN is reached by name, so a private-address test would refuse it — cost if wrong: a user sends text and key in clear to a public http:// host despite the warning.`
- `Ruling (spec owner, raised by the build; amended in critique round 3): the Ending deadline is max(180 s, this turn's upload budget plus 10 s); at the deadline the conversation ends with "Conversation ended: the last turn took too long", which is true for a live turn, a batch turn and a turn already handed over; the turn's audio is wiped without removing its words from the box — a turn stuck that long means nothing was sent, so the silence end would fire next with the wrong reason, and a long upload must get its own time — cost if wrong: a conversation whose uploads hang ends after the deadline, and says why.`

**Found while checking, outside PR 3:**
- `gpt-4o-transcribe`, `gpt-4o-mini-transcribe` (with `-2025-12-15`), `whisper-1` and
  `gpt-4o-transcribe-diarize` were noticed on 2026-08-26 and shut down on **2027-02-26**. The
  replacement is `gpt-live-transcribe` or `gpt-transcribe`.
- `gpt-4o-mini-transcribe-2025-03-20` shuts down on 2027-01-20.
- `gpt-4o-mini-transcribe` is the default of Nutshell's OpenAI batch speech-to-text list
  (`src/core/voice_stt.c:14`).
- This goes on the notes' todo; it is not PR 3's work.

## 17. Critique (2026-11-24, claude-fable-5-2 high)

Each finding in one line, then the main session's disposition.

### Round 1

1. The open mic sends anything loud, and Alt+Tab keeps it alive. — Accepted:
   an adaptive speech detector replaces the fixed gate in conversation mode
   (§5.3), the conversation pauses in the background (§5.4), and the 3-minute
   end counts from the last turn sent (§5.6).
2. "Final answer" was inferred from `WM_AI_RESPONSE`, and Working could hang.
   — Accepted: Working ends on the chat's idle state, every card kind is
   announced, the send returns a status that leaves Working with a reason, the
   full and launch-failure paths report back, and a safety timeout follows the
   chat state (§5.2).
3. `SetDuckingPreference` does not stop other apps being ducked. — Accepted:
   the claim is dropped, only conversation mode uses the communications
   category, and the Voice page says where to turn ducking off (§7.3).
4. The echo filter would drop real answers. — Accepted: it was narrowed to
   playback time and near-exact matches with a header status; round 2 revised
   the rule again (§9.4, §14).
5. Removing "Send automatically" went against the maintainer's earlier
   decision. — Accepted: it stays a dictation option, with no migration;
   conversation mode ignores it (§4.3, §11.2).
6. Escape anywhere ends a long recording, and modals ended conversations. —
   Accepted: Escape counts only with focus in the AI panel, and a modal no
   longer ends a conversation; the tab change stays because the panel holds
   one chat per tab (§4.4, §5.6).
7. The default voice chain was too short and the Libby claim unchecked. —
   Accepted: the chain adds any English female voice, the maintainer is told
   when the voice is not a British woman's, and the builder verifies which
   en-GB voices are usable (§8.3).
8. Repeat and continue as model turns are slow and need a hidden note. —
   Accepted: short phrases are handled locally and everything else goes to the
   model with no note (§10).
9. PR 2 was too large, and conversation mode could come first. — Partly
   accepted: PR 2 is split into conversation mode with Windows speech and a
   PR 3 for AI speech; reordering is rejected because the maintainer asked for
   live words and conversation mode shows them (§15).
10. The off-and-teardown claims were not testable. — Accepted: a voice network
    open counter in the self-report, a resource ledger driven to zero by
    every end, a WAV seam kept out of normal paths, and the device behaviours
    listed as Windows hand checks (§12, §14).
11. Voice had several network openers. — Accepted: one `voice_net_open(kind,
    ...)` with a single `voice_gate_on()` check carries batch STT, the
    WebSocket and TTS (§6.5).
12. Hand-copied COM declarations would collide with newer MSYS2 headers. —
    Accepted: each is guarded by the header's `__I..._DEFINED__` macros or a
    feature test, and each PR asks for a Windows `mingw32-make test` and
    release build (§7.5).
13. Several facts were unverified (WebSocket timeouts, OneCore tokens,
    `IAudioEffectsManager`'s build, Bluetooth, prices and TTS retirement, the
    §3 counts). — Accepted: each is a must-verify item with a fallback in
    §16; the §3 counts were rechecked against the code.

### Round 2

1. Open-mic voice turns reset the auto-approve round cap. — Accepted: a voice
   turn no longer resets it, as a main session's call for the maintainer to
   confirm, and the anti-goal now says plainly that other voices can be heard
   (§2, §5.2, §16 A).
2. The echo filter's sentence-scoped, 4-word rule was still wrong. —
   Accepted: it matches the whole reply with 3-word runs covering 90%, within
   playback plus 2 s, and half-duplex drops audio from before the cut-in
   (§9.1, §9.4, §14).
3. The fallback for a 10-minute recording overflowed a 4,096-byte result. —
   Accepted: the result is heap-allocated, at least 64 KB, the audio is kept
   until the fallback succeeds, and live text stays if it fails (§4.5, §13).
4. Modals and settings changes were unhandled. — Accepted: a Nutshell modal
   pauses a conversation, a voice settings change ends it, and Play sample
   stops its speech first (§5.2, §5.4, §5.6).
5. An approval card waiting kept the microphone open. — Accepted: capture is
   closed during Working and reopened for Listening and Speaking (§5.2, §7.3).
6. A reply could not be stopped without ending the conversation. — Accepted:
   the key, Escape in the panel and Stop stop speech only while Speaking, and
   replies are held while Paused, spoken on resume if under 30 s old as a
   main session's call (§5.2, §5.4).
7. The repeat and continue matcher was too narrow and could hijack answers. —
   Partly accepted: polite forms and 6 words are allowed, it acts only on a
   recent reply that did not end with a question; the hidden cut-off note is
   rejected because voice never builds turn content itself (§10).
8. The event that ends Working was not named. — Accepted: a posted "chat turn
   idle" notification after the relaunch decision, with the early busy
   callback sites named (§3, §5.2).
9. An in-memory SAPI stream through our own render client was more than PR 2
   needs. — Accepted: SAPI speaks to the default output in PR 2, and the
   render client, play queue and byte map move to PR 3 (§7.4, §8, §9, §15).
10. Several tests could pass without the feature working. — Accepted: the
    self-report lists loaded modules with a SAPI control, voice traffic goes
    through a voice-only WinHTTP session opener with the counter, and the
    controller moves into a `src/core` state machine (§5.2, §6.5, §12, §14).
11. The spec contradicted itself in five places. — Accepted: AI speech has its
    own provider as the voice spec's G3 says; endings and errors go to the
    chat; the "silence" wording is exact; Escape-in-the-panel moves to PR 1;
    `voice_windows_voice_name` gets its own notice line (§2, §8.4, §5.7, §11,
    §13, §15).
12. A PC with no English woman's voice gets a silent, wrong default. — Main
    session's call, for the maintainer to confirm: Windows speech stays the
    default and the shared notice dialog says so once per launch (§8.3, §16
    B).
13. The box was locked for the whole conversation. — Accepted: it is locked
    only while speech is captured into it, and a draft left when Listening
    resumes starts the next turn (§5.1).
14. The WebSocket's two threads could not both be cancelled with
    `AiNetCancel`. — Accepted: a two-worker cancel or separate handles, the
    builder's choice, with the wake-up a must-verify item and a short-timeout
    receive loop as the fallback (§6.6, §16).
15. Further facts were unverified (self-ducking of our own speech, SAPI purge
    latency and mid-utterance volume, Mistral's batch limit and closing after
    `transcription.done`, provider idle limits). — Accepted: each is a
    must-verify item with a fallback in §16.

### Round 3 (2026-11-25, claude-fable-5-2 high, of `8b86ecd`: PR 3's AI speech providers)

Verdict: "sound after changes". Each finding in one line, then the main session's disposition.

1. Several failures were silent and treated as a quiet end: a chunk with no audio, a decoder not
   past its header at the end, a skipped event line, a second WAV header, a `done` with no
   audio. — Accepted: each one fails the reply and falls back, with a test for each (§8.5).
2. Mistral's raw event bytes are known only from its SDK. — Accepted: the plan's "Task 0" is
   the maintainer's capture of two streams and one forced error, saved as fixtures. The build
   goes ahead against the SDK's shape with provisional fixtures, and the PR is not marked
   ready until the captures replace them and the tests pass (§8.4, §14).
3. Groq Orpheus is Preview, at 10 requests a minute and 100 a day. — Accepted: Groq is cut
   from PR 3 (§6.1, §8.4, §15).
4. The resampler had no user once Groq was cut. — Accepted: none is built. Exactly 24 kHz is
   accepted, 16-bit or float32, mono or stereo; anything else is "unsupported audio format",
   and `WAVE_FORMAT_EXTENSIBLE` only if the capture shows it (§8.4).
5. The WAV and event parsers take untrusted bytes and had no fuzzing. — Accepted: fuzz targets
   `fuzz_wavstream` and `fuzz_aisse`, with seeds, in `make fuzz-run` (§14).
6. The done guard fired on a fixed delay and could cut a slow reply. — Accepted: it counts the
   frames pushed per reply, re-arms on every play report, and fires only after 2 s with no
   progress.
7. A context that lost its render client (pre-empted or failed) did not reopen it, and a
   pre-emption while silent was undefined. — Accepted: any speak opens the render client when
   the context does not hold it, and resends the gain. A pre-emption while silent just
   releases it, with a test.
8. A worker detached after a bounded join could still push into the audio thread. —
   Accepted: a per-worker dead flag under the engine's lock is checked before every push. The
   ledger is released only when the thread ends, and `voice_audio_stop` runs only after the
   engine has marked every worker dead.
9. A broken setting (wrong key, missing model) fell back on every reply with only a header. —
   Accepted: a resolve error or HTTP 401, 403, 404 or 422 gives one chat note per
   conversation, and AI speech is not tried again in it. Network errors, 429 and 5xx keep the
   per-reply fallback (§8.5, §13).
10. An unknown provider was silently mapped to Mistral. — Accepted: it is a resolve error;
    `openai` and `gemini` are unknown too (§8.4).
11. The user is not told where the reply text goes. — Accepted: a dim line on the Voice page,
    "Each spoken reply's text is sent to <host>", and Base URL help on the text, the key and
    `http://` (§8.4, §11.3, §13).
12. A reply could steer the voice with bracketed cues. — Accepted: `[` and `]` become `(` and
    `)` in the text sent, byte for byte (§8.4).
13. The style default and the `tts-1` rule assumed OpenAI. — Ruled: the style row stays,
    enabled for Custom only, empty by default, sent only when typed. The `tts-1` rule, the
    OpenAI seeds and the help about reaching OpenAI through Custom are dropped.
14. `voxtral-mini-tts-2603` is deprecated Voxtral 2, and Jane is Voxtral 2. — Accepted: the
    seed is removed, and the default Mistral voice is `voice_1787` (Mary), to be confirmed by
    the capture (§8.4, §11.3).
15. The Ending watchdog's 180 s can be shorter than a long upload's own timeouts, and its note
    was false for a turn already sent. — Accepted: the deadline is max(180 s, the upload
    budget plus 10 s), and the note is "Conversation ended: the last turn took too long". The
    turn's audio is wiped without removing its words (§13, §16).
16. The AI speech engine and the glue tasks were too large to review. — Accepted: each is split
    in two in the plan.
17. Two tests could not fail: a skip that passes silently, and an offsets check met by any
    offset. — Accepted: a skip is printed and counted, and the offsets test needs one at or
    past "Two".
18. R10's wording of the handover bound was wrong. — Accepted: up to 20 s of queued audio plus
    one chunk.
19. The §15 "Later" list and several "characters" were out of date. — Accepted: fixed; offsets
    and chunk sizes are bytes.
20. A saved key could be sent to a provider it was not entered for. — Ruled: the key is bound
    to its provider inside the encrypted value; a mismatch means no key, which finding 9
    reports (§8.4).
21. (No change.)
22. Plain `http://` for Custom should be refused. — Rejected as stated: it stays allowed with
    the warning (finding 11), as speech-to-text does today. A self-hosted server on the LAN is
    reached by name, so a private-address test would refuse it. Cost if wrong: a user sends
    text and key in clear to a public `http://` host despite the warning.
