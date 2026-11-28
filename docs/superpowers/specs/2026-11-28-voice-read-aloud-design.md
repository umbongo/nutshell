# Read aloud: a button under each AI reply (Dictation mode)

**Date**: 2026-11-28
**Status**: APPROVED by the maintainer on 2026-11-28, with two changes that are folded in
(one button per turn; the control that stops dictation stays live)
**Branch**: `feat/voice-read-aloud`

## 1. Intent

Voice has two modes. In Conversation mode every reply is spoken as it
arrives. In Dictation mode the user speaks and the reply is silent text.

The maintainer wants to hear a reply on demand while in Dictation mode
(2026-11-28): "a button that reads the AI response out aloud when in
dictation mode", and "Have a button at the bottom of each reply. So I can
listen to the latest reply and the older ones too if needed."

## 2. What the user sees

Under every finished AI reply there is one small speaker button, at the
bottom left of the reply. A reply is a whole turn: everything the assistant
wrote between one user message and the next.

```
  Nutshell AI
  The interface is down because the peer stopped
  sending keepalives. Check the cable first.
  [speaker]                    <- dim; brighter on hover
```

- **Press it:** that reply is read aloud. Its button turns into a Stop
  button (a square, in the accent colour) for as long as that reply is
  being fetched or spoken.
- **Press Stop:** the reading stops at once.
- **Press another reply's button:** the first reading stops and the other
  reply starts. Only one reply is ever read at a time, across the whole app.
- **The reply ends:** the button goes back to the speaker.

The button is shown only while **Voice is on and Mode is Dictation**. With
Voice off, or in Conversation mode, the chat looks exactly as it does today.

The button has no tooltip in this change: the code-block copy button, which
it is modelled on, has none, and the chat list view has no tooltip code.

**While a dictation is recording** the read-aloud buttons are drawn
disabled and a press on one does nothing. The microphone is open, and a
reading would be typed back into the input box. Only these buttons are
disabled: the mic button and the dictation key, which stop the dictation,
work as they do today (maintainer, 2026-11-28: "Ok, except for the button
to stop dictation").

### Which replies get a button

One button per turn (maintainer, 2026-11-28: "1 button per turn please").
A turn is the run of assistant rows between one user message and the next,
or the end of the chat. When a turn used tools it has several rows of
assistant text ("Let me check the interface.", then the commands, then the
answer); the button sits under the last row of assistant text in the turn.

The button appears when the turn has finished: nothing in it is still
streaming and no command of it is still running or waiting for approval. A
turn with no assistant text at all (an error, a stopped request) has no
button. The user's own messages and the other rows of the chat (thinking,
tool and command rows, approvals, error and status notices) never carry one.

### What is read

All of the turn's assistant text, in the order it appears, each row made
speakable the way Conversation mode makes a reply speakable:
`voice_speak_text()` (`src/core/voice_speak.h`) applied to the row's
markdown, the rows joined with a sentence break. Commands, tool output and
thinking are not read. (Main session's call. Conversation mode itself
speaks only the last assistant message of a turn, up to its first command,
`src/ui/ai_chat_voice.c`; the reader reads the whole turn so that an answer
given before a command is not lost.)

A reply with nothing speakable (only a code block, say) still has
the button; pressing it does nothing audible and the button returns to the
speaker at once. The builder may show it disabled instead if that is cheap
to know at paint time.

### Which voice

The **Speech** choice in Settings > Voice: the Windows voice, or AI speech
(Mistral or Custom). Today, in Dictation mode, the Speech row is greyed and
the rows beneath it are greyed or hidden, because nothing there speaks. With
this feature they are live whenever Voice is on, in either mode: the Speech
row, the Windows voice row and its notes, the AI speech rows (provider,
voice, model, URL, key, style), the privacy line and Play sample.

Those rows sit under the "Conversation" heading today, which is greyed in
Dictation mode. They move under a heading of their own, **"Speech"**, placed
between the dictation rows and the Conversation section, so that no live row
sits under a greyed heading. The Conversation section keeps the rows that
only make sense in a conversation (the pause length and the rest), and
stays Conversation-only. (Main session's call.)

With AI speech, every press sends that reply's text to the speech provider
and is a paid request; nothing is cached. The privacy line that already
says so is shown in Dictation mode too.

### What stops a reading

1. Its Stop button, or the speaker button of another reply.
2. **Starting to dictate** (the mic button or the dictation key). The
   microphone must never hear the speakers reading a reply and type it back.
3. **Escape** while the keyboard focus is in the AI panel. Escape in the
   terminal still goes to the session.
4. **The reply's Stop button no longer being reachable:** the chat is
   cleared or a new chat is started, the AI panel is closed, the user
   switches to another tab, or the session closes.
5. Saving Settings with a change to any voice setting, turning Voice or the
   AI assistant off, switching Mode, and exiting.
6. **Play sample** in Settings. The sample and the reading would talk over
   each other (Windows voice) or the sample would take the audio output
   away from the reading (AI speech); the reading stops first, cleanly, and
   is not reported as an error.

Sending a new message does **not** stop a reading: listening to an older
reply while typing the next question is the use the maintainer described.

### Errors

A reading that cannot start or fails part-way (no voice installed, no AI
speech key, the provider refuses, the network drops) stops, puts the button
back to the speaker, and shows the reason in plain words where Dictation
mode shows its own errors today. The text is "Could not read the reply
aloud" followed by the reason when the engine gives one: AI speech already
has plain reasons (`voice_ais_failure_reason`, `src/core/voice_aispeech.h`).
Windows speech has none today (a failed Windows-voice Play sample shows
nothing, and Conversation mode says only "Could not speak the reply: it is
on screen"), so for the Windows voice the text is "Could not read the reply
aloud: the Windows voice did not start." The audio output being taken by
something else of ours (stop reason 6) is a stop, never an error.

## 3. Goals, non-goals, anti-goals

**Goals**
- Hear any finished reply, latest or older, with one click.
- One obvious way to stop.
- Use the voice the user already chose.

**Non-goals** (not in this change)
- The button in Conversation mode.
- Reading replies automatically in Dictation mode.
- Pause and resume, speed, skipping, word highlighting while reading.
- A keyboard shortcut ("read the latest reply"). The button is mouse-only
  in this change, like the code-block copy button.
- A tooltip on the button.
- Reading the user's own messages or tool output.
- Caching AI speech audio between presses.

**Anti-goals** (it has failed if any of these happens)
- Speech starts without a press.
- The microphone picks up the reading and dictates it into the input box.
- Anything of speech is loaded, or any request made, before the first press,
  or at all while Voice is off (CLAUDE.md, "Optional features: off unless
  enabled, not loaded while off").
- Conversation mode, merged in v1.2.99 and v1.3.1 and not yet run on
  Windows, behaves differently because of this change. (The maintainer's
  separate request of 2026-11-28, that speech in Conversation mode must not
  trigger its own listening and stop itself, is handled outside this
  feature: here the microphone is never open during a reading.)

## 4. Approaches

**A. A small reader beside dictation, on the existing speech engines
(chosen).** A new owner of the two engines that already exist: Windows
speech (`src/ui/voice_tts_win.h`) and AI speech
(`src/ui/voice_aispeech_win.h`). Both are addressed by a token, and both
already serve two owners, Conversation mode and Play sample. The reader is
a third owner with its own token. A pure state machine in `src/core`
decides what happens on a press, on each engine message and on each stop
reason; thin Win32 glue starts and stops the engine and repaints the
button. Trade-off: one more owner of the engine slots (§5), and the chat
list view gains a second clickable element per reply.

**B. Run the reading through Conversation mode's engine.** Start the
conversation machinery in a speak-only state. It would reuse its speech
queue and its stop paths. Rejected: that state machine also owns the
microphone, turn-taking and interrupting; a speak-only path through it
changes code that has never run on Windows, which is the fourth anti-goal.

**C. Windows voice only.** The simplest: no network, no key, no cost.
Rejected as the design because it ignores the AI speech choice that
v1.3.1 added and the user may have set; it is what approach A does anyway
when Speech is set to the Windows voice.

The maintainer handed the "how" to the main session; A is its pick.

## 5. How it is built (decided)

- **Core state machine**, new `src/core/voice_read.c/.h`, native-tested.
  To the user there are two states: idle, and reading (from the press until
  the reply ends, fails or is stopped; the button shows Stop throughout).
  It holds which reply is being read (the chat it belongs to and the id of
  the row that carries the turn's button). Inputs: a press on reply X, whether a dictation is recording,
  the engine's messages, and each stop reason of §2. Outputs: start engine,
  speak text, stop, release, repaint reply X, show error. Message handling
  ignores anything not carrying the reader's token and the current reading's
  id, so a late message from a stopped reading cannot restart or mislabel
  anything.
- **The two engines do not send the same messages**, and the machine must
  not wait for one that never comes. Windows speech starts asynchronously
  and posts `VTT_READY`, then `VTT_EXITED` when its thread ends. AI speech
  starts synchronously and posts neither (`src/ui/voice_aispeech_win.h`).
  The builder models "waiting for the engine" only for Windows speech.
- **Its own token**, beside `VOICE_TTS_SAMPLE_TOKEN` (0x7FFFFFF0) and
  `VOICE_AIS_SAMPLE_TOKEN` (0x7FFFFFF2). `voice_on_tts`
  (`src/ui/voice_win.c`) lets the reader see messages that carry its token
  and then passes every message on to `voice_conv_on_tts`
  (`src/ui/voice_conv_win.c`) as it does today. Nothing is diverted: that
  function is where every token's `VTT_EXITED` is cleaned up, whoever owns
  it, and it already ignores the rest of a token that is not the
  conversation's.
- **Engine choice and routing** reuse the router in `src/ui/voice_win.c`
  (`rc_tts_start`, `rc_tts_speak`, `rc_tts_stop`, `rc_tts_quit`) or the same
  calls beneath it. `rc_speech_engine()` returns AI speech only in
  Conversation mode today; the engine choice must follow
  `voice_speech_engine` in both modes for the reader without changing what
  Conversation mode gets.
- **Engine slots.** Each engine has two context slots. The reader exists
  only in Dictation mode and the conversation only in Conversation mode, so
  the worst case stays two: reader plus Play sample. A Mode switch tears the
  reader down (stop reason 5) before a conversation starts. If a slot is
  nevertheless taken, the press fails with the engine's existing "busy"
  path as an error (§2), never a crash or a silent nothing.
- **Lifetime.** Nothing is created until the first press. The engine
  context is then kept, stopped but alive, so the next press starts
  quickly, and is released on any reason in stop list item 5, when the last
  chat that could show the button goes away, and at exit. Release joins the
  engine thread as the existing teardown does. **After any engine error the
  context is released at once**, as Conversation mode does on every
  `VTT_ERROR` (`src/ui/voice_conv_win.c`): a Windows speech thread that
  failed stays in its loop and would otherwise hold one of the two slots.
  The next press starts a fresh one.
- **One gate.** The reader's start function refuses unless Voice is on,
  the AI assistant is on and Mode is Dictation, the same test that shows the
  button, and refuses while a dictation is recording. It is the only place a
  reading can begin.
- **The button** is drawn by the chat list view (`src/ui/chat_listview.c`)
  as part of an assistant item, the way the code-block copy glyph is:
  owner-drawn, hit-tested through the existing hover machinery
  (`chatlv_hover_hit`, `ns_hover`), no child window. The item's measured
  height grows by one button row only while the button is shown, so with
  Voice off or in Conversation mode the layout is unchanged pixel for pixel.
  Icons come from `ns_icon_draw`; a speaker icon is new, and `NS_ICON_STOP`
  exists. Colours, sizes and scaling follow the design system rules in
  CLAUDE.md (tokens, `ns_scale`, `SP_*`/`SZ_*`). The hover ids have room
  above `0x60000000` (`src/ui/chat_listview.c`).
- **Settings.** The speech rows are switched in two places today, and both
  change: `voice_speech_enables()` (`src/core/voice_config.h`) takes
  "conversation enabled" as its first input, greys the Speech row and hides
  the AI rows on it; and `voice_mode_enables().conversation` switches the
  Windows voice row, its notes and Play sample (`src/ui/settings.c`,
  `voice_enable_sync`). All of these follow "Voice on" instead, while the
  Conversation section's own rows keep the conversation test. The rows move
  under the new "Speech" heading (§2); the page's layout, its help text for
  Speech (which says Conversation mode today) and the `ui_gallery` captures
  of the Voice page change with it. No new setting and no config field: the
  feature is on whenever Voice is on and Mode is Dictation.
- **Dictation start stops the reading** before the microphone opens, in the
  one place a recording begins (`start_recording()`, `src/ui/voice_win.c`),
  so both the mic button and the key are covered.
- **The turn rule lives in `src/core`**, beside the chat message list
  (`src/core/chat_msg.c`): one function that says which row, if any,
  carries a turn's button, and one that builds the turn's speakable text.
  The list view and the reader both ask it; neither works a turn out for
  itself.
- **Hooks that exist:** `voice_on_chat_changed` (`src/ui/ai_chat.c`) and
  `voice_on_panel_closed` (`src/ui/window.c`) cover stop reason 4. Message
  ids are never reused when the chat list is rebuilt (`src/core/chat_msg.c`),
  so "which reply is being read" can be held as an id.

## 6. Testing

- Native tests for "which row carries the turn's button" and "the turn's
  speakable text" over a chat message list, in `src/core`: a plain turn, a
  turn with tools and three text rows, a turn still streaming, a turn with a
  command awaiting approval, a turn with no assistant text, the last turn of
  the chat, and a stopped turn.
- Native tests for `voice_read`: every state and input, including press
  while starting, press on the reply being read, press on another reply, a
  late done or error from a stopped reading, an error while starting, each
  stop reason, and the gate refusing in Conversation mode and with Voice
  off.
- Native tests for the changed `voice_speech_enables()` table: Dictation
  with Voice on enables the speech rows; Voice off disables them; the
  Conversation rows are unchanged.
- List view: the button's geometry and hit-testing are tested where the
  existing chat list-view tests can reach (native if the geometry is pure,
  `wintest` otherwise): present only on the last assistant text row of a
  finished turn, absent on the turn's earlier rows, absent while streaming, absent with the feature off, and the item height
  with the feature off equals today's.
- `--ui-demo`: a state that shows a chat with the button idle on one reply
  and reading on another, so the gallery captures it on all four themes.
- Not loaded while off: the exe's DLL self-report under Wine shows that
  with Voice on in Dictation mode and no press, no speech DLL has been
  requested; the existing Voice-off assertion keeps passing.
- Windows hand checks, written into the notes' Open list in the same pull
  request: the button on the latest and an older reply with the Windows
  voice and with AI speech; Stop; switching replies mid-reading; dictation
  key during a reading; Escape; tab switch; an AI speech failure (wrong
  key); the Speech rows in Settings in Dictation mode; 100 % and 150 %
  scaling on a light and a dark theme.

## 7. Left to the builder

- The exact module split of the Win32 glue (in `voice_win.c`, or a new
  `voice_read_win.c`) and the token's value.
- How the list view learns "feature on" and "which reply is being read"
  (a setter called by the glue, or a query), provided the list view itself
  starts no speech.
- Whether a reply with nothing speakable shows a disabled button.
- Where exactly the error text appears, following Dictation mode's own
  error display.
- The icon's drawing.

## 8. Process

Per CLAUDE.md and the `feature-workflow` skill: plan by
`superpowers:writing-plans`; Sonnet implements each task test-first; Opus
reviews each task; one final whole-branch review by Fable; the version is
one above `main`'s at build time; draft pull request; the maintainer's
go-ahead before it is marked ready or merged.
