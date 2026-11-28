# Read Aloud Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** In Dictation mode, a speaker button under each finished AI turn reads that turn aloud with the Speech engine chosen in Settings, and turns into a Stop button while it reads.
**Architecture:** Two pure modules in `src/core`, each native-tested: `chat_turn` (which row carries a turn's button, and the turn's speakable text) and `voice_read` (the reader's state machine, its gate and its error text). A thin Win32 controller, `src/ui/voice_read_win.c`, runs the machine through an ops table (the real one in `voice_win.c` reuses the `rc_tts_*` speech router; the wintest installs fakes), and the chat list view draws and hit-tests the button the way it does the code-block copy glyph.
**Tech Stack:** C17, Win32, MinGW cross-compile, native gcc tests (custom `test_framework.h`), Wine for `wintest`
**Spec:** docs/superpowers/specs/2026-11-28-voice-read-aloud-design.md

## Global Constraints

- Worktree `/home/thomas/nutshell-wt/voice-read-aloud`, branch `feat/voice-read-aloud`. Never touch `~/nutshell` or another worktree.
- The button exists only while `voice_read_shown(opt_feature_active(s, OPT_VOICE), s->voice_mode)` is 1: Voice on (which needs the AI assistant) and Mode Dictation, with the audio thread up. Otherwise the chat looks exactly as today, pixel for pixel.
- Nothing of speech is created before the first press, and nothing at all while Voice is off: `voice_read_win_press()` is the only caller of the engine start op, and it refuses unless `voice_read_gate()` says yes.
- One reader token, `VOICE_READ_TOKEN 0x7FFFFFF3u`. `voice_on_tts` lets the reader observe its messages and then passes every message on to `voice_conv_on_tts` unchanged. Nothing is diverted or freed by the reader.
- Engine choice for the reader is `Settings.voice_speech_engine` in both modes. `rc_speech_engine()` (Conversation's choice) is not changed.
- After any engine error the reader's context is quit (joined) at once.
- Fixed text: `Could not read the reply aloud: the Windows voice did not start.` (Windows speech, any failure); `Could not read the reply aloud: AI speech did not start.` (AI context refused); `Could not read the reply aloud: <voice_ais_failure_reason(hr, resolve_err)>.` (AI speech error); the Settings heading `Speech`. Errors show as a chat status row (`ai_chat_voice_status(panel, text, 1)`, Dictation's own error display).
- No `MessageBox`; no `RGB(` literal in `src/ui`; sizes from `ns_scale()` with `SP_*`/`SZ_*`; colours from `ns_tokens()`; icons from `ns_icon_draw`; no `CreateFont`.
- Logic to be tested goes in `src/core`; `src/ui` files are not in the native build. New `src/core/*.c` and `tests/*.c` are picked up by the Makefile wildcards; every native test is declared and called in `tests/runner.c`. A Win32-only test file goes into `WIN_ONLY_TESTS` and `WIN_TEST_SRCS` and is called from `tests/win_runner.c`.
- CRLF preserved: `Makefile` is CRLF, `src/ui/ai_chat.h` is mixed (keep each line's ending as its neighbours have it). Before every commit `git diff --stat` must equal `git diff --stat --ignore-cr-at-eol`.
- Both builds `-Werror`; MinGW adds `-Wpedantic -Wshadow -Wformat=2 -Wconversion`. A `src/ui` file the wintest does not link is checked with the MinGW syntax check: `make -n -B release | grep -m1 -- '-c src/ui/FILE.c ' | sed -e 's/ -c / -fsyntax-only /' -e 's/ -o [^ ]*$//' | sh` (no output means clean). `-n -B` builds nothing.
- No version bump, no `make release`, no `make selftest-wine` (it builds the exe) and no change to `build/win/nutshell.exe` inside a task.
- Before `xvfb-run -a make wintest`, run `pgrep -a wine`; if another session's run is live, wait for it rather than `wineserver -k`.
- Stage files by path and read every `??` line of `git status --short` before committing.
- Every commit message ends with these lines (after a blank line):

  ```
  Models: claude-sonnet-5-5 (implementation)

  Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_01AHuBtEd8Zi2fpsjiKHXZt4
  ```

## Review Focus

1. **Stop while the Windows voice is still starting** (Stop, dictation key, Escape, tab switch before `VTT_READY`), then the `VTT_READY` arrives: no speech may start, or speech starts without a press. Test: `test_vread_ready_after_stop_is_silent` (Task 2).
2. **Press reply A, then reply B at once on the same context**: A's late `VTT_DONE` or `VTT_ERROR` (same token) must not end B, show an error, or release B's context. Tests: `test_vread_late_reply_of_first_ignored` (Task 2), `test_vread_win_late_error_of_first_keeps_second` (Task 5).
3. **An engine error must not leave a context in a slot**: after any error the context is quit, the next press starts a fresh one, and a start refused because both slots are taken (reader plus Play sample) shows the error rather than nothing. Tests: `test_vread_win_error_releases_then_fresh_start`, `test_vread_win_start_refused_shows_error` (Task 5).
4. **A turn that looks finished but is not**, or the reverse: a command row still running or a card still waiting while the chat's busy flag already reads idle; a status row (our own error note) appended after the reply. The button sits only under a truly finished turn's last text row. Tests: `test_chat_turn_running_command_no_button`, `test_chat_turn_awaiting_approval`, `test_chat_turn_status_after_reply_keeps_button` (Task 1).
5. **Heights**: the button row must measure exactly as it paints (or the activity indicator, hit tests and selection drift), and with the feature off every item keeps today's height. Tests: `test_chat_read_win_painted_equals_measured`, `test_chat_read_win_off_heights_unchanged` (Task 4).

## File Structure

Created:
- `src/core/chat_turn.h`, `src/core/chat_turn.c`: the turn rule (which row carries a turn's button) and the turn's speakable text.
- `tests/test_chat_turn.c`: native tests of the turn rule.
- `src/core/voice_read.h`, `src/core/voice_read.c`: the reader's pure state machine, the gate, the error text.
- `tests/test_voice_read.c`: native tests of the machine.
- `src/ui/voice_read_win.h`, `src/ui/voice_read_win.c`: the reader's Win32 controller; runs the machine through `VoiceReadOps`.
- `tests/test_voice_read_win.c`: wintest of the controller with fake ops.

Modified:
- `src/core/chat_msg.h`: `u.ai.turn_btn`.
- `src/core/voice_config.h`, `src/core/voice_config.c`, `tests/test_voice_config.c`: speech rows follow Voice on.
- `src/core/settings_help.c`, `tests/test_settings_help.c`: Mode, Speech and Windows voice help text.
- `src/ui/settings.c`: the "Speech" heading and row order, the enable sync, Play sample stops a reading.
- `src/core/ai_panel_layout.h`, `src/core/ai_panel_layout.c`, `tests/test_ai_panel.c`: the button's geometry.
- `src/ui/icons.h`, `src/ui/icons.c`, `tests/test_icons.c`: `NS_ICON_SPEAKER`.
- `src/ui/chat_listview.h`, `src/ui/chat_listview.c`, `tests/test_chat_select_win.c`: measure, paint, hover, click.
- `src/ui/ai_chat_internal.h`, `src/ui/ai_chat.h`, `src/ui/ai_chat.c`, `src/ui/ai_chat_voice.c`: the click id, the turn-text and view accessors, tail busy, the demo state.
- `src/ui/voice_win.h`, `src/ui/voice_win.c`: the real ops table and every stop hook.
- `src/ui/window.c`: a new panel learns the view.
- `src/core/ui_demo.c`, `src/core/ui_demo.h`, `tests/test_ui_demo.c`: `--ui-demo=voice-read-aloud`.
- `tests/integration/cases/30-ui-demo.ps1`, `tests/integration/README.md`: the gallery list.
- `src/ui/dll_hook_win.c`, `tests/selftest/check.sh`, `Makefile`: the `dictai` self-report mode and `voice-speech-contexts`.
- `tests/runner.c`, `tests/win_runner.c`, `Makefile` (`WIN_ONLY_TESTS`, `WIN_TEST_SRCS`): registration.
- `docs/superpowers/specs/2026-09-06-ui-redesign-notes.md`: the Windows hand checks.

---

### Task 1: The turn rule in `src/core`

**Files:** Create `src/core/chat_turn.h`, `src/core/chat_turn.c`, `tests/test_chat_turn.c`. Modify `src/core/chat_msg.h` (the `u.ai` struct, lines 50-70), `tests/runner.c`.
**Interfaces:**
- Consumes: `ChatMsgList`, `ChatMsgItem`, `CHAT_RUN_*` (`src/core/chat_msg.h`); `voice_speak_text()` (`src/core/voice_speak.h`).
- Produces:
  - `ChatMsgItem.u.ai.turn_btn` (`int`, 1 = this row carries its turn's button; written only by `chat_turn_refresh()`; 0 from `chat_msg_append`'s `calloc`).
  - `int chat_turn_is_text(const ChatMsgItem *it);`
  - `int chat_turn_carries_button(const ChatMsgList *list, int item_id, int tail_busy);`
  - `int chat_turn_refresh(ChatMsgList *list, int on, int tail_busy);`
  - `char *chat_turn_text(const ChatMsgList *list, int item_id, int tail_busy);`

The rule, written into the header comment:
- A turn is the run of rows after a `CHAT_ITEM_USER` row (or from the list head) up to the next `CHAT_ITEM_USER` row or the end. Status, tool and command rows belong to the turn they sit in; they never end it.
- Assistant text (`chat_turn_is_text`): a `CHAT_ITEM_AI_TEXT` row whose `text` holds a byte above `' '`. A row with only thinking is not text.
- A turn is open when it is the list's last turn and `tail_busy` is 1, or when any `CHAT_ITEM_COMMAND` row in it has (`!u.cmd.settled && !u.cmd.quoted`) or `u.cmd.run == CHAT_RUN_RUNNING`.
- A row carries the button when it is the last assistant-text row of a turn that is not open. `tail_busy` is the panel's busy flag (`busy_of()` in `ai_chat_voice.c`).
- `chat_turn_refresh` with `on == 0` clears every flag. It writes `u.ai.turn_btn` on `CHAT_ITEM_AI_TEXT` rows only (`u` is a union), marks each row whose flag changed `dirty`, and returns how many changed. One pass over the list.
- `chat_turn_text`: NULL when `item_id` names no row that carries the button under `tail_busy`, or on out of memory. Otherwise a malloc'd string: `voice_speak_text()` of each assistant-text row of the turn, in order, up to and including the button row. Each piece has its trailing whitespace trimmed; empty pieces are skipped. Pieces are joined by the sentence break: if the text so far ends in `.`, `!` or `?`, add one space; if it ends in `:`, `;` or `,`, replace that character with `.` and add one space; otherwise add `. `. The result is `""` when nothing is speakable. The caller `secure_zero()`s and frees it.

- [ ] Step 1: Write the failing test. In `tests/test_chat_turn.c` (helpers `U(list, text)`, `A(list, text)`, `C(list, settled, run)` append rows; `C` calls `chat_msg_set_command(it, "ip link", CMD_READ, 0)`, sets `u.cmd.approved = 1`, `u.cmd.settled`, `u.cmd.run`):

  ```c
  int test_chat_turn_plain(void) {
      TEST_BEGIN();
      ChatMsgList l; chat_msg_list_init(&l);
      ChatMsgItem *u = U(&l, "Why is eth0 down?");
      ChatMsgItem *a = A(&l, "The peer stopped sending keepalives.");
      ASSERT_EQ(chat_turn_carries_button(&l, a->id, 0), 1);
      ASSERT_EQ(chat_turn_carries_button(&l, u->id, 0), 0);
      a->dirty = 0;
      ASSERT_EQ(chat_turn_refresh(&l, 1, 0), 1);
      ASSERT_EQ(a->u.ai.turn_btn, 1);
      ASSERT_EQ(a->dirty, 1);
      ASSERT_EQ(chat_turn_refresh(&l, 1, 0), 0);          /* nothing changed */
      chat_msg_list_clear(&l);
      TEST_END();
  }
  int test_chat_turn_tools_three_rows(void) {
      /* U; A1 "Let me check the interface:"; C(1, DONE); A2 "The counters show drops.";
       * C(1, DONE); A3 "Check the cable first." */
      ASSERT_EQ(chat_turn_carries_button(&l, a1->id, 0), 0);
      ASSERT_EQ(chat_turn_carries_button(&l, a2->id, 0), 0);
      ASSERT_EQ(chat_turn_carries_button(&l, a3->id, 0), 1);
      char *t = chat_turn_text(&l, a3->id, 0);
      ASSERT_NOT_NULL(t);
      if (t) ASSERT_STR_EQ(t, "Let me check the interface. The counters show drops. "
                              "Check the cable first.");
      ASSERT_NULL(chat_turn_text(&l, a1->id, 0));           /* not the button row */
  }
  int test_chat_turn_still_streaming(void) {
      /* U0 "First?"; A0 "First answer."; U1 "Second?"; A1 "Partial" */
      ASSERT_EQ(chat_turn_carries_button(&l, a1->id, 1), 0);   /* last turn, busy */
      ASSERT_EQ(chat_turn_carries_button(&l, a0->id, 1), 1);   /* an earlier turn keeps it */
      ASSERT_EQ(chat_turn_carries_button(&l, a1->id, 0), 1);
  }
  int test_chat_turn_awaiting_approval(void) {
      /* U; A "I would run this:"; C(0, CHAT_RUN_NONE) with approved = -1 */
      ASSERT_EQ(chat_turn_carries_button(&l, a->id, 0), 0);   /* busy flag says idle: the card still counts */
  }
  int test_chat_turn_running_command_no_button(void) {
      /* U; A "Running it now."; C(1, CHAT_RUN_RUNNING) */
      ASSERT_EQ(chat_turn_carries_button(&l, a->id, 0), 0);
  }
  int test_chat_turn_no_text(void) {
      /* U; STATUS "Error: HTTP 503"; then U; A "   \n"; then U; A "" with chat_msg_set_thinking(a, "hm") */
      ASSERT_EQ(chat_turn_refresh(&l, 1, 0), 0);
      ASSERT_EQ(chat_turn_is_text(blank), 0);
      ASSERT_EQ(chat_turn_is_text(thinking_only), 0);
  }
  int test_chat_turn_last_turn(void) {
      /* U1; A1 "One."; U2; A2 "Two." */
      ASSERT_EQ(chat_turn_refresh(&l, 1, 0), 2);
      ASSERT_EQ(a1->u.ai.turn_btn + a2->u.ai.turn_btn, 2);
  }
  int test_chat_turn_stopped(void) {
      /* U; A "Let me look."; C(1, CHAT_RUN_ABORTED); STATUS "Stream cancelled by user." */
      ASSERT_EQ(chat_turn_carries_button(&l, a->id, 0), 1);
      /* chat_turn_text(&l, a->id, 0) == "Let me look." */
  }
  int test_chat_turn_status_after_reply_keeps_button(void) {
      /* U; A "Done."; STATUS "Could not read the reply aloud: the Windows voice did not start." */
      ASSERT_EQ(chat_turn_carries_button(&l, a->id, 0), 1);
  }
  int test_chat_turn_refresh_off_clears(void) {
      /* U; A "Done."; C(1, DONE) after A with approved = 1 */
      ASSERT_EQ(chat_turn_refresh(&l, 1, 0), 1);
      ASSERT_EQ(chat_turn_refresh(&l, 0, 0), 1);
      ASSERT_EQ(a->u.ai.turn_btn, 0);
      ASSERT_EQ(chat_turn_refresh(&l, 0, 0), 0);
      ASSERT_EQ(c->u.cmd.approved, 1);                    /* the union of a command row untouched */
      ASSERT_EQ(c->u.cmd.settled, 1);
  }
  int test_chat_turn_text_join(void) {
      /* U; A "Let me check:"; C(1, DONE); A "Done"; C(1, DONE); A "Really?" */
      /* chat_turn_text(button) == "Let me check. Done. Really?" */
      ASSERT_NULL(chat_turn_text(&l, 9999, 0));            /* unknown id */
      ASSERT_NULL(chat_turn_text(&l, u->id, 0));           /* a user row */
  }
  ```
  If `voice_speak_text()` changes one of these plain sentences (it should not), change the fixture, not the rule. Declare and call all eleven in `tests/runner.c`.
- [ ] Step 2: Run it and see it fail. `make -j test 2>&1 | tail -3`: the build fails, with `chat_turn.h` not found and no member `turn_btn`.
- [ ] Step 3: Implement. Add `int turn_btn;` to `u.ai` in `src/core/chat_msg.h`, with a comment naming `chat_turn_refresh()`. Write `src/core/chat_turn.h`, holding the rule above as its comment and the four signatures, and `src/core/chat_turn.c`. `chat_turn_carries_button` and `chat_turn_text` walk back from the row to the turn's start and forward to its end; `chat_turn_refresh` walks the list once, keeping the current turn's last text row and its open flag.
- [ ] Step 4: Run the tests. `make -j test 2>&1 | tail -3` gives `Tests Run: N, Failed: 0`.
- [ ] Step 5: Commit `src/core/chat_msg.h src/core/chat_turn.h src/core/chat_turn.c tests/test_chat_turn.c tests/runner.c`:

  ```
  feat(chat_turn): which row carries a turn's Read aloud button, and its speakable text

  Models: claude-sonnet-5-5 (implementation)

  Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_01AHuBtEd8Zi2fpsjiKHXZt4
  ```

### Task 2: The reader's state machine in `src/core`

**Files:** Create `src/core/voice_read.h`, `src/core/voice_read.c`, `tests/test_voice_read.c`. Modify `tests/runner.c`.
**Interfaces:**
- Consumes: `VOICE_MODE_DICTATION`, `VOICE_SPEECH_WINDOWS`, `VOICE_SPEECH_AI` (`src/core/voice_config.h`); `voice_ais_failure_reason()` (`src/core/voice_aispeech.h`).
- Produces (all in `voice_read.h`):

  ```c
  typedef enum { VRS_IDLE = 0, VRS_STARTING, VRS_SPEAKING } VoiceReadState;
  typedef enum { VRX_DICTATION = 1, VRX_ESCAPE, VRX_CHAT, VRX_PANEL, VRX_SETTINGS, VRX_SAMPLE } VoiceReadStop;
  typedef enum { VRM_READY = 1, VRM_DONE, VRM_ERROR } VoiceReadMsg;
  enum { VRA_START = 1u << 0, VRA_SPEAK = 1u << 1, VRA_STOP = 1u << 2,
         VRA_RELEASE = 1u << 3, VRA_ERROR = 1u << 4, VRA_VIEW = 1u << 5 };
  typedef struct {
      VoiceReadState state;
      int item_id;      /* the button row being read; 0 when idle */
      unsigned serial;  /* reply id of the latest speak; never reused; 0 before the first */
      int ctx;          /* 1 while an engine context is held (kept between readings) */
      int engine;       /* VOICE_SPEECH_* of the held context, or of the one starting */
      int ready;        /* the held context takes a speak now (AI at once, Windows after VRM_READY) */
  } VoiceRead;
  typedef struct { int item_id; int engine; int gate; int has_text; } VoiceReadPress;
  typedef struct { unsigned actions; int engine; unsigned serial; int err_engine; long hr; } VoiceReadOut;

  void voice_read_init(VoiceRead *r);
  int  voice_read_shown(int voice_active, int mode);
  int  voice_read_gate(int voice_active, int mode, int recording);
  VoiceReadOut voice_read_on_press(VoiceRead *r, const VoiceReadPress *p);
  VoiceReadOut voice_read_on_started(VoiceRead *r, int ok);
  VoiceReadOut voice_read_on_engine(VoiceRead *r, VoiceReadMsg kind, unsigned reply_id, long hr);
  VoiceReadOut voice_read_on_stop(VoiceRead *r, VoiceReadStop why);
  void voice_read_error_text(int engine, long hr, const char *resolve_err, char *out, size_t cap);
  ```

The transitions, written into the header comment. They are the decisions the tests pin down:
- `voice_read_shown` = `voice_active && mode == VOICE_MODE_DICTATION`; `voice_read_gate` = shown `&& !recording`.
- Press, `!p->gate`: nothing.
- Press on the row being read (state not idle, same id): `STOP|VIEW`, idle. This is the Stop button.
- Press on another row while reading: `STOP`, then go on as from idle (one combined out).
- From idle with `!has_text`: nothing more. If a reading was stopped in the same press, `VIEW` and idle.
- A held context of another engine: `RELEASE`, then go on as with no context.
- Held and ready: `SPEAK` (serial `++r->serial`), speaking, `VIEW`. Held, not ready: starting (it waits for `VRM_READY`), `VIEW`. None held: `START` (`out.engine`), starting, `VIEW`.
- `on_started(1)` in starting with no context: `ctx = 1`. AI: ready, `SPEAK`, speaking. Windows: not ready, stays starting. `on_started(0)`: `ERROR` (`err_engine` = engine, `hr` 0), `VIEW`, idle. Anywhere else: nothing.
- `VRM_READY`: when held and not ready, it becomes ready; in starting it also does `SPEAK` and moves to speaking. Otherwise nothing. (A ready that comes after the reading stopped speaks nothing.)
- `VRM_DONE`: in speaking with `reply_id == serial`, `VIEW` and idle. Otherwise nothing.
- `VRM_ERROR` with no context, or `reply_id != 0 && reply_id != serial` (an older reply's): nothing. Otherwise `RELEASE` (context, ready 0). If not idle, also `ERROR` (`err_engine`, `hr`), `VIEW`, idle.
- `on_stop`: when not idle, `STOP|VIEW` and idle. `VRX_PANEL`, `VRX_SETTINGS` and `VRX_SAMPLE` also `RELEASE` a held context. No stop is ever an `ERROR`.
- The glue executes an out in this order: `STOP`, `RELEASE`, `START` (and feeds its result to `on_started`), `SPEAK`, `ERROR`, `VIEW`.
- Error text. Windows speech, any `hr`: `Could not read the reply aloud: the Windows voice did not start.` AI with `hr == 0`: `Could not read the reply aloud: AI speech did not start.` AI otherwise: `Could not read the reply aloud: ` + `voice_ais_failure_reason(hr, resolve_err)` + `.`. Cut to `cap`, NUL-terminated.

- [ ] Step 1: Write the failing test. In `tests/test_voice_read.c`, with `static VoiceReadPress P(int id, int engine) { VoiceReadPress p = { id, engine, 1, 1 }; return p; }`:

  ```c
  int test_vread_gate(void) {
      TEST_BEGIN();
      ASSERT_EQ(voice_read_shown(1, VOICE_MODE_DICTATION), 1);
      ASSERT_EQ(voice_read_shown(1, VOICE_MODE_CONVERSATION), 0);
      ASSERT_EQ(voice_read_shown(0, VOICE_MODE_DICTATION), 0);
      ASSERT_EQ(voice_read_gate(1, VOICE_MODE_DICTATION, 0), 1);
      ASSERT_EQ(voice_read_gate(1, VOICE_MODE_DICTATION, 1), 0);
      ASSERT_EQ(voice_read_gate(1, VOICE_MODE_CONVERSATION, 0), 0);
      ASSERT_EQ(voice_read_gate(0, VOICE_MODE_DICTATION, 0), 0);
      TEST_END();
  }
  int test_vread_press_refused_by_gate(void) {
      VoiceRead r; voice_read_init(&r);
      VoiceReadPress p = P(7, VOICE_SPEECH_AI); p.gate = 0;
      ASSERT_EQ(voice_read_on_press(&r, &p).actions, 0u);
      ASSERT_EQ(r.state, VRS_IDLE);
  }
  int test_vread_ai_press_starts_then_speaks(void) {
      VoiceReadOut o = voice_read_on_press(&r, &(VoiceReadPress){ 7, VOICE_SPEECH_AI, 1, 1 });
      ASSERT_EQ(o.actions, VRA_START | VRA_VIEW);
      ASSERT_EQ(o.engine, VOICE_SPEECH_AI);
      ASSERT_EQ(r.state, VRS_STARTING); ASSERT_EQ(r.item_id, 7);
      o = voice_read_on_started(&r, 1);
      ASSERT_EQ(o.actions, VRA_SPEAK); ASSERT_EQ(o.serial, 1u); ASSERT_EQ(r.state, VRS_SPEAKING);
      o = voice_read_on_engine(&r, VRM_DONE, 1u, 0);
      ASSERT_EQ(o.actions, VRA_VIEW); ASSERT_EQ(r.state, VRS_IDLE);
      ASSERT_EQ(r.item_id, 0); ASSERT_EQ(r.ctx, 1);
  }
  int test_vread_windows_waits_ready(void) {
      /* press(7, WINDOWS) -> START|VIEW; started(1) -> 0 actions, STARTING; READY -> SPEAK, serial 1 */
  }
  int test_vread_second_press_reuses_context(void) {
      /* after a finished AI reading: press(9, AI) -> SPEAK|VIEW, serial 2, no START */
  }
  int test_vread_ready_after_stop_is_silent(void) {
      /* press(7, WINDOWS), started(1); press(7) -> STOP|VIEW, IDLE;
       * engine(VRM_READY, 0, 0) -> actions 0, r.ready 1, state IDLE.
       * Repeat with on_stop(VRX_DICTATION) and on_stop(VRX_CHAT) in place of the second press. */
  }
  int test_vread_press_reading_reply_stops(void) {
      /* speaking 7: press(7) -> VRA_STOP | VRA_VIEW, IDLE, ctx 1 */
  }
  int test_vread_press_other_reply_switches(void) {
      /* speaking 7 (serial 1): press(9, AI) -> VRA_STOP | VRA_SPEAK | VRA_VIEW, serial 2, item 9 */
  }
  int test_vread_late_reply_of_first_ignored(void) {
      /* continuing: engine(VRM_DONE, 1, 0) -> 0, still SPEAKING 9;
       * engine(VRM_ERROR, 1, 401) -> 0, ctx still 1 */
  }
  int test_vread_error_while_speaking_releases(void) {
      /* speaking AI, serial s: engine(VRM_ERROR, s, 401) ->
       * VRA_RELEASE | VRA_ERROR | VRA_VIEW, o.hr 401, o.err_engine VOICE_SPEECH_AI, ctx 0, IDLE;
       * the next press(7, AI) -> VRA_START | VRA_VIEW */
  }
  int test_vread_error_while_starting(void) {
      /* press(7, WINDOWS), started(1), engine(VRM_ERROR, 0, (long)0x80004005) ->
       * VRA_RELEASE | VRA_ERROR | VRA_VIEW, err_engine VOICE_SPEECH_WINDOWS */
  }
  int test_vread_start_refused(void) {
      /* press(7, AI), started(0) -> VRA_ERROR | VRA_VIEW, o.hr 0, IDLE, ctx 0 */
  }
  int test_vread_error_after_stop_silent_release(void) {
      /* speaking, serial s; on_stop(VRX_ESCAPE) -> STOP|VIEW; engine(VRM_ERROR, s, -1) -> VRA_RELEASE only */
  }
  int test_vread_stop_reasons(void) {
      /* from speaking: VRX_DICTATION, VRX_ESCAPE, VRX_CHAT -> STOP|VIEW, ctx 1;
       * VRX_PANEL, VRX_SETTINGS, VRX_SAMPLE -> STOP|RELEASE|VIEW, ctx 0; never VRA_ERROR.
       * idle with a context: VRX_SETTINGS -> VRA_RELEASE; VRX_ESCAPE -> 0. idle, none held: each -> 0 */
  }
  int test_vread_empty_reply(void) {
      /* idle: press with has_text 0 -> 0. speaking 9: press(7, has_text 0) -> STOP|VIEW, IDLE */
  }
  int test_vread_engine_change_restarts(void) {
      /* AI context held, idle: press(7, WINDOWS) -> VRA_RELEASE | VRA_START | VRA_VIEW, o.engine WINDOWS */
  }
  int test_vread_unexpected_messages(void) {
      /* fresh: started(1) -> 0; READY -> 0; DONE(1) -> 0; ERROR(0, -1) -> 0 */
  }
  int test_vread_error_text(void) {
      char b[200];
      voice_read_error_text(VOICE_SPEECH_WINDOWS, (long)0x80004005, NULL, b, sizeof b);
      ASSERT_STR_EQ(b, "Could not read the reply aloud: the Windows voice did not start.");
      voice_read_error_text(VOICE_SPEECH_AI, 0, NULL, b, sizeof b);
      ASSERT_STR_EQ(b, "Could not read the reply aloud: AI speech did not start.");
      voice_read_error_text(VOICE_SPEECH_AI, 401, NULL, b, sizeof b);
      ASSERT_STR_EQ(b, "Could not read the reply aloud: the speech provider refused the key (HTTP 401).");
      voice_read_error_text(VOICE_SPEECH_AI, VOICE_AIS_HR_RESOLVE, "Enter a speech API key", b, sizeof b);
      ASSERT_STR_EQ(b, "Could not read the reply aloud: Enter a speech API key.");
      voice_read_error_text(VOICE_SPEECH_AI, 401, NULL, b, 10);
      ASSERT_EQ((int)strlen(b), 9);
  }
  ```
  Declare and call all eighteen in `tests/runner.c`.
- [ ] Step 2: Run it and see it fail. `make -j test 2>&1 | tail -3`: the build fails, with `voice_read.h` not found.
- [ ] Step 3: Implement `src/core/voice_read.h` (the transitions above as its comment) and `src/core/voice_read.c`. No Win32 and no allocation.
- [ ] Step 4: Run the tests. `make -j test 2>&1 | tail -3` gives `Failed: 0`.
- [ ] Step 5: Commit `src/core/voice_read.h src/core/voice_read.c tests/test_voice_read.c tests/runner.c`:

  ```
  feat(voice_read): the Read aloud state machine, its gate and its error text

  Models: claude-sonnet-5-5 (implementation)

  Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_01AHuBtEd8Zi2fpsjiKHXZt4
  ```

### Task 3: Settings: the speech rows follow Voice on, under their own "Speech" heading

**Files:** Modify `src/core/voice_config.h` (lines 70-82), `src/core/voice_config.c` (lines 123-146), `tests/test_voice_config.c` (lines 1420-1490), `src/core/settings_help.c` (the `SH_VOICE_MODE`, `SH_VOICE_WIN_VOICE` and `SH_VOICE_SPEECH` entries, lines 536-587), `tests/test_settings_help.c`, `src/ui/settings.c` (line 225 `hVoiceHead`, `voice_speech_apply` 1180-1232, `voice_enable_sync` 1291-1324, the Voice page 2900-3075, the heading font loop near 3165), `tests/runner.c`.
**Interfaces:**
- Consumes: nothing new.
- Produces:
  - `typedef struct { int key_mode; int mic_delay; int auto_send; int conversation; int speech; } VoiceModeEnables;` with `speech = voice_on`, in either mode.
  - `void voice_speech_enables(int speech_enabled, int engine, const char *provider, int key_shared, int uk_voice_installed, VoiceSpeechEnables *out);`. Only the first parameter is renamed, and the header comment changes from "the Conversation section is live" to "the Speech section is live (Voice on, either mode)". Behaviour is unchanged.

- [ ] Step 1: Write the failing test.

  ```c
  /* tests/test_voice_config.c: extend test_voice_mode_enables */
  voice_mode_enables(1, VOICE_MODE_DICTATION, VOICE_MODE_TOGGLE, &e);
  ASSERT_EQ(e.speech, 1);
  voice_mode_enables(1, VOICE_MODE_CONVERSATION, VOICE_MODE_HOLD, &e);
  ASSERT_EQ(e.speech, 1);
  voice_mode_enables(0, VOICE_MODE_DICTATION, VOICE_MODE_HOLD, &e);
  ASSERT_EQ(e.speech, 0);

  int test_voice_speech_rows_follow_voice_on(void) {
      TEST_BEGIN();
      VoiceModeEnables m; VoiceSpeechEnables e;
      voice_mode_enables(1, VOICE_MODE_DICTATION, VOICE_MODE_TOGGLE, &m);
      ASSERT_EQ(m.conversation, 0);                      /* the Conversation rows unchanged */
      voice_speech_enables(m.speech, VOICE_SPEECH_AI, "custom", 0, 0, &e);
      ASSERT_EQ(e.speech, 1); ASSERT_EQ(e.ai_rows, 1); ASSERT_EQ(e.privacy, 1);
      ASSERT_EQ(e.url, 1); ASSERT_EQ(e.style, 1);
      voice_speech_enables(m.speech, VOICE_SPEECH_WINDOWS, "mistral", 0, 0, &e);
      ASSERT_EQ(e.speech, 1); ASSERT_EQ(e.uk_line, 1);
      voice_mode_enables(0, VOICE_MODE_DICTATION, VOICE_MODE_TOGGLE, &m);
      voice_speech_enables(m.speech, VOICE_SPEECH_AI, "custom", 0, 0, &e);
      ASSERT_EQ(e.speech + e.ai_rows + e.url + e.key + e.style + e.uk_line + e.privacy, 0);
      TEST_END();
  }

  /* tests/test_settings_help.c */
  int test_settings_help_speech_both_modes(void) {
      TEST_BEGIN();
      const SettingsHelp *e = settings_help_get((int)SH_VOICE_SPEECH);
      ASSERT_TRUE(strstr(e->text, "Conversation mode only") == NULL);
      ASSERT_TRUE(strstr(e->text, "speaker button") != NULL);
      e = settings_help_get((int)SH_VOICE_WIN_VOICE);
      ASSERT_TRUE(strstr(e->text, "speaker button") != NULL);
      e = settings_help_get((int)SH_VOICE_MODE);
      ASSERT_TRUE(strstr(e->text, "speaker button") != NULL);
      TEST_END();
  }
  ```
  Rename the first comment of `test_voice_speech_enables` to "Voice off (the Speech section off): everything off". Register the two new tests.
- [ ] Step 2: Run it and see it fail. `make -j test 2>&1 | tail -3`: the build fails, with no member `speech` in `VoiceModeEnables`.
- [ ] Step 3: Implement.
  - `voice_config.[ch]`: as in Produces.
  - `settings_help.c`, the exact new text. Every byte is ASCII, and each entry stays under `SETTINGS_HELP_TEXT_MAX` (700); the `Default:` lines are unchanged.
    - `SH_VOICE_MODE`: `"Mode:\n\nDictation puts your words in the message box for you to read and send with Send, and each finished reply has a speaker button under it that reads that reply aloud. Conversation sends each turn by itself after a pause and reads the assistant's replies aloud. In Conversation mode the voice key always toggles: press it to start a conversation, press it again to end it, or press it to stop a spoken reply. A conversation also ends on Escape while the AI panel has the keyboard focus, when Windows locks, or after 3 minutes with nothing said.\nDefault: Dictation, so your words wait in the box for you to press Send."`
    - `SH_VOICE_SPEECH`: `"Speech:\n\nWho reads the assistant's replies aloud: every reply in Conversation mode, and in Dictation mode the reply whose speaker button you press. Windows speech uses a voice installed on this PC. AI speech sends the reply's text to the speech provider and plays the AI-generated voice it returns; every reply read this way is a new request to the provider. If AI speech fails in Conversation mode, the rest of the reply is read with Windows speech; in Dictation mode the reading stops and the chat says why.\nDefault: Windows speech."`
    - `SH_VOICE_WIN_VOICE`: the first sentence becomes `"The voice that reads the assistant's replies aloud with Windows speech, in Conversation mode and when you press a reply's speaker button."`; the rest stays.
  - `settings.c`:
    - `hVoiceHead[4]`, and the comment lists "Speech" too. The heading-font loop runs `vi < 4`.
    - On the Voice page, insert a block `nd->hVoiceHead[3] = make_hint(nd, "Speech");` after the Auto-send checkbox. Then, in order: Speech, Windows voice, the voice note, the UK note, the AI speech rows, the privacy line and Play sample, each moved unchanged.
    - The "Conversation" heading follows Play sample, and under it: Pause before sending, the echo-cancellation note, the ducking note.
    - In `voice_enable_sync`, `IDC_VOICE_WIN_VOICE`, `IDC_VOICE_PLAY_SAMPLE`, `hVoiceHead[3]`, `hVoiceNoteVoice` and `hVoiceNoteUk` follow `en.speech`, and `voice_speech_apply(d, en.speech)`. `IDC_VOICE_PAUSE`, `hVoiceHead[2]` and `hVoiceNoteAec` keep `en.conversation`, and the ducking line keeps its Mode test.
    - Rename `voice_speech_apply`'s parameter to `speech`, and update the comment above the block ("Dictation greys the Conversation section; the Speech section is live whenever Voice is on").
- [ ] Step 4: Run the tests.
  - `make -j test 2>&1 | tail -3` gives `Failed: 0`.
  - The MinGW syntax check of `src/ui/settings.c` is clean.
  - Hand check (Windows, recorded in Task 9): Settings > Voice in Dictation mode on the four themes. The Speech heading and its rows are live, the AI rows appear when AI speech is chosen, and the Conversation heading with Pause before sending is greyed.
- [ ] Step 5: Commit `src/core/voice_config.h src/core/voice_config.c tests/test_voice_config.c src/core/settings_help.c tests/test_settings_help.c src/ui/settings.c tests/runner.c`:

  ```
  feat(settings): the speech rows are live whenever Voice is on, under a Speech heading

  Models: claude-sonnet-5-5 (implementation)

  Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_01AHuBtEd8Zi2fpsjiKHXZt4
  ```

### Task 4: The button in the chat list view (speaker icon, geometry, measure, paint, hover, click)

**Files:** Modify `src/core/ai_panel_layout.h`, `src/core/ai_panel_layout.c`, `tests/test_ai_panel.c`, `src/ui/icons.h` (the enum, before `NS_ICON_COUNT`), `src/ui/icons.c` (`g_glyphs` after `NS_ICON_EYE`, `k_fill` near line 402), `tests/test_icons.c`, `src/ui/chat_listview.h` (the struct and setters), `src/ui/chat_listview.c` (the hover ids 150-181, `paint_ai_item` 1925-2152, `measure_item`'s AI case 1560-1585, `recalc_layout` 1282-1330, `on_paint`'s hit-list reset, `chatlv_hover_hit` 3811-3870, `chatlv_hover_rect_for_id` 3888, `on_lbuttondown` 4068-4100), `src/ui/ai_chat_internal.h` (the `IDC_CHAT_*` block), `tests/test_chat_select_win.c`, `tests/win_runner.c`, `tests/runner.c`, `Makefile` (`WIN_TEST_SRCS`: add `src/core/chat_turn.c`).
**Interfaces:**
- Consumes: `chat_turn_refresh(ChatMsgList *, int on, int tail_busy)`, `ChatMsgItem.u.ai.turn_btn` (Task 1).
- Produces:
  - `typedef struct { NsRect row, glyph; } ReadButtonLayout;`
  - `int read_button_row_h(int dpi);`, which returns `ns_scale(SP_XS, dpi) + ns_scale(SZ_ICON, dpi)`.
  - `void read_button_layout(int text_left, int text_bottom, int dpi, ReadButtonLayout *out);`. `row = {text_left, text_bottom, ns_scale(SZ_ICON), read_button_row_h}` and `glyph = {text_left, text_bottom + ns_scale(SP_XS), ns_scale(SZ_ICON), ns_scale(SZ_ICON)}`.
  - `NS_ICON_SPEAKER` (= `NS_ICON_EYE + 1`).
  - `#define IDC_CHAT_READ_ALOUD 4017`: posted by the list view, `lParam` = the button row's item id.
  - `void chat_listview_set_read_aloud(HWND hwnd, int on, int reading_id, int disabled);`
  - `void chat_listview_set_tail_busy(HWND hwnd, int busy);`
  - In `ChatListView`: `int read_on, read_id, read_disabled, tail_busy;`, `ClvReadHit read_hits[CLV_READ_HITS_MAX]; int n_read_hits;`, with `typedef struct { int item_id; RECT rc; } ClvReadHit;` and `#define CLV_READ_HITS_MAX 32`.

Decisions for the implementer:
- **Icon.** The speaker glyph: `{ OP_MOVE, 20, 50, OP_LINE, 44, 50, OP_LINE, 72, 26, OP_LINE, 72, 102, OP_LINE, 44, 78, OP_LINE, 20, 78, OP_CLOSE, OP_FILLSTROKE, OP_MOVE, 88, 46, OP_CURVE, 98, 56, 98, 72, 88, 82, OP_STROKE, OP_MOVE, 100, 32, OP_CURVE, 120, 50, 120, 78, 100, 96, OP_STROKE, OP_END }`, with `k_fill[NS_ICON_SPEAKER] = 255`.
- **Layout.** `recalc_layout` calls `chat_turn_refresh(lv->msg_list, lv->read_on, lv->tail_busy)` next to `chat_msg_fold_refresh()`. When it changed rows and the view is not stuck to the bottom, the view anchors as for a fold change: the existing fold anchor block runs when `fold_changed > 0 || turn_changed > 0`.
- **Measure.** `measure_item`'s AI case adds `read_button_row_h(dpi)` when `item->u.ai.turn_btn`.
- **Paint.** `paint_ai_item` takes `read_button_layout(text_rc.left, content_top + text_h, dpi, &rb)` and adds `rb.row.h` to `painted_h` when the flag is set. It draws one glyph in `rb.glyph`:
  - `NS_ICON_STOP` in `ns_tokens()->accent.base` when `item->id == lv->read_id`;
  - otherwise `NS_ICON_SPEAKER` in `text_disabled` while `read_disabled`;
  - otherwise `text_main` while hovered, else `text_dim`.

  The glyph rect goes into `read_hits`, which is cleared where `clv_code_hits_clear` runs at the start of every paint.
- **Hover ids.** `#define CLV_READ_BTN_BASE 0x60000000` and `#define CLV_READ_BTN_ID(iid) (CLV_READ_BTN_BASE + (iid))`, for item ids below `CLV_ITEM_ID_LIMIT`; document them in the id-range comment. `chatlv_hover_rect_for_id` tests `id >= CLV_READ_BTN_BASE` *before* the `id >= CLV_CODE_COPY_BASE` branch, which has no upper bound today.
- **Hit tests.** `chatlv_hover_hit` returns the read id after the code-copy test, but never while `read_disabled`. Hit tests ignore points above `lv->sticky_bottom`, as `chatlv_code_hit_at` does.
- **Click.** `on_lbuttondown`, after the code-copy block: a hit on a read rect returns 1 (consumed). Unless `read_disabled`, it first does `PostMessage(GetParent(lv->hwnd), WM_COMMAND, MAKEWPARAM(4017 /* IDC_CHAT_READ_ALOUD */, 0), (LPARAM)item_id)`. The list view starts no speech.
- **Setters.** `chat_listview_set_read_aloud`: a change of `on` relayouts (`recalc_layout` + `update_scrollbar`), while a change of only `reading_id`/`disabled` invalidates. `chat_listview_set_tail_busy` relayouts on a change. Both are no-ops for a NULL or unchanged state.

- [ ] Step 1: Write the failing test.

  ```c
  /* tests/test_ai_panel.c (native) */
  int test_read_button_layout_96_and_144(void) {
      TEST_BEGIN();
      ReadButtonLayout l;
      ASSERT_EQ(read_button_row_h(96), 24);
      read_button_layout(30, 100, 96, &l);
      ASSERT_EQ(l.glyph.x, 30); ASSERT_EQ(l.glyph.y, 104);
      ASSERT_EQ(l.glyph.w, 20); ASSERT_EQ(l.glyph.h, 20);
      ASSERT_EQ(l.row.y, 100);  ASSERT_EQ(l.row.h, 24);
      ASSERT_EQ(read_button_row_h(144), 36);
      read_button_layout(45, 200, 144, &l);
      ASSERT_EQ(l.glyph.y, 206); ASSERT_EQ(l.glyph.w, 30);
      TEST_END();
  }

  /* tests/test_icons.c (wintest) */
  int test_icons_speaker_renders(void) {
      TEST_BEGIN();
      ASSERT_TRUE(ns_icons_init());
      ASSERT_EQ((int)NS_ICON_SPEAKER, (int)NS_ICON_EYE + 1);   /* appended: no id moved */
      int sp = count_non_bg(NS_ICON_SPEAKER, 32, RGB(0, 0, 0), RGB(255, 255, 255), 64);
      int st = count_non_bg(NS_ICON_STOP, 32, RGB(0, 0, 0), RGB(255, 255, 255), 64);
      ASSERT_TRUE(sp > 0);
      ASSERT_TRUE(sp != st);
      ns_icons_shutdown();
      TEST_END();
  }
  ```
  In `tests/test_chat_select_win.c`, a fixture `fill_read_fixture(list)`: U "Why is eth0 down?"; A1 "Let me check the interface:"; C settled `CHAT_RUN_DONE`; A2 "Check the cable first."; U "And eth1?"; A3 "It is up.". It reuses `view_open`/`view_paint`/`lv_of`, with a view 2000 px tall so every row paints. `host_proc` records each `WM_COMMAND` with id 4017 in `g_read_cmds` and `g_read_cmd_id`. Messages are pumped with `PeekMessage` after a click.

  ```c
  int test_chat_read_win_button_rows(void);          /* on 1, tail_busy 0, after a paint at 96 DPI:
      A1.turn_btn 0, A2 1, A3 1; lv->n_read_hits == 2; each hit rc 20 x 20 */
  int test_chat_read_win_painted_equals_measured(void); /* on 1, at 96 and 144 DPI, markdown on and off:
      every painted item has painted_height == measured_height (check_item) */
  int test_chat_read_win_off_heights_unchanged(void);   /* heights of a view never told about Read aloud
      == heights after set_read_aloud(on 1) then (on 0); and A2(on) - A2(off) == read_button_row_h(dpi) */
  int test_chat_read_win_tail_busy_hides_last(void);    /* set_tail_busy(1): A3.turn_btn 0, A2 1, one hit */
  int test_chat_read_win_click_posts_id(void);          /* WM_LBUTTONDOWN at the centre of A3's hit:
      g_read_cmds 1, g_read_cmd_id == A3->id; set_read_aloud(1, 0, 1) (disabled) and click again:
      g_read_cmds still 1 */
  int test_chat_read_win_reading_draws_stop(void);      /* set_read_aloud(1, A3->id, 0): the pixels in A3's
      hit rc differ from those painted with reading_id 0 (view_pixels, diff_rows over the rect's rows) */
  ```
  Register the native test in `tests/runner.c`, and the seven wintest tests in `tests/win_runner.c`.
- [ ] Step 2: Run it and see it fail. `make -j test 2>&1 | tail -3`: the build fails, with `read_button_layout` undeclared. `pgrep -a wine; xvfb-run -a make wintest 2>&1 | tail -3`: the build fails, with `NS_ICON_SPEAKER` and `chat_listview_set_read_aloud` undeclared.
- [ ] Step 3: Implement as the decisions above.
- [ ] Step 4: Run the tests. `make -j test 2>&1 | tail -3` gives `Failed: 0`, and `pgrep -a wine; xvfb-run -a make wintest 2>&1 | tail -3` gives `Failed: 0`.
- [ ] Step 5: Commit `src/core/ai_panel_layout.h src/core/ai_panel_layout.c tests/test_ai_panel.c src/ui/icons.h src/ui/icons.c tests/test_icons.c src/ui/chat_listview.h src/ui/chat_listview.c src/ui/ai_chat_internal.h tests/test_chat_select_win.c tests/win_runner.c tests/runner.c Makefile`:

  ```
  feat(chat_listview): a speaker button under each finished turn, Stop while it reads

  Models: claude-sonnet-5-5 (implementation)

  Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_01AHuBtEd8Zi2fpsjiKHXZt4
  ```

### Task 5: The reader's Win32 controller, `voice_read_win`

**Files:** Create `src/ui/voice_read_win.h`, `src/ui/voice_read_win.c`, `tests/test_voice_read_win.c`. Modify `tests/win_runner.c`, `Makefile` (`WIN_ONLY_TESTS` += `tests/test_voice_read_win.c`; `WIN_TEST_SRCS` += `src/ui/voice_read_win.c src/core/voice_read.c tests/test_voice_read_win.c`).
**Interfaces:**
- Consumes: everything in `voice_read.h` (Task 2); `VoiceTtsMsg`, `VTT_*` (`src/ui/voice_tts_win.h`); `VoiceBinding`, `voice_binding_same()` (`src/core/voice_ptt.h`); `voice_settings_digest()` (`src/core/voice_config.h`); `opt_feature_active(s, OPT_VOICE)`; `VOICE_AIS_HR_RESOLVE`.
- Produces (`voice_read_win.h`):

  ```c
  #define VOICE_READ_TOKEN 0x7FFFFFF3u   /* beside VOICE_TTS_SAMPLE_TOKEN and VOICE_AIS_SAMPLE_TOKEN */
  typedef struct {
      int   (*start)(unsigned token, int engine);       /* 1 when a context runs under token */
      void  (*speak)(unsigned token, unsigned id, const char *text, size_t from);
      void  (*stop)(unsigned token);
      void  (*quit)(unsigned token);                     /* stop, release and join */
      char *(*turn_text)(int item_id);                   /* malloc'd; NULL: the row carries no button now */
      void  (*view)(HWND panel, int on, int reading_id, int disabled);   /* panel NULL: the current one */
      void  (*note)(const char *text);                   /* an error row in the chat */
      void  (*binding)(VoiceBinding *out);
      int   (*resolve_error)(char *out, size_t cap);     /* 1 with AI speech's resolve reason */
  } VoiceReadOps;
  void voice_read_win_set_ops(const VoiceReadOps *ops);       /* NULL detaches; resets the machine */
  void voice_read_win_press(const Settings *s, int item_id, int recording);   /* the only start */
  void voice_read_win_stop(VoiceReadStop why);
  void voice_read_win_on_tts(const VoiceTtsMsg *m);           /* observes; never frees or consumes */
  void voice_read_win_apply(const Settings *s, int running);  /* after every voice_feature_apply */
  void voice_read_win_set_recording(int on);
  void voice_read_win_chat_check(void);                       /* VRX_CHAT when the binding changed */
  int  voice_read_win_active(void);                           /* 1 while a reading runs */
  void voice_read_win_view_sync(HWND panel);
  ```

Behaviour (one line each where the machine leaves a choice):
- **press.** Without ops, nothing. `gate = running && voice_read_gate(opt_feature_active(s, OPT_VOICE), s->voice_mode, recording)`. Only through the gate is `ops->turn_text(item_id)` called; NULL then means nothing happens. The press passes `{ item_id, s->voice_speech_engine, gate, text && text[0] }`, takes `ops->binding(&bind)`, and holds the text, wiped and freed once it has been spoken or the reading ends.
- **Executing an out.** In the machine's order (Task 2). `START` calls `ops->start(VOICE_READ_TOKEN, out.engine)`, records `voice_settings_digest(s)` on success, and feeds `voice_read_on_started(ok)`, whose out is executed in turn. `SPEAK` calls `ops->speak(VOICE_READ_TOKEN, out.serial, text, 0)`. `ERROR` passes `voice_read_error_text(out.err_engine, out.hr, reason, ...)` to `ops->note`, where `reason` is filled by `ops->resolve_error` only for AI with `VOICE_AIS_HR_RESOLVE`. `VIEW` calls `ops->view(NULL, shown, machine.item_id, recording)`.
- **on_tts.** Only `m->token == VOICE_READ_TOKEN` counts. `VTT_READY`, `VTT_DONE` and `VTT_ERROR` map to `VRM_*` with `m->reply_id` and `m->hr`; `VTT_WORD` and `VTT_EXITED` are ignored (`voice_conv_on_tts` reaps every token's `VTT_EXITED`).
- **apply.** `shown = running && voice_read_shown(opt_feature_active(s, OPT_VOICE), s->voice_mode)`. When `!shown`, or a context is held and `voice_settings_digest(s)` differs from the one recorded at its start, it calls `voice_read_win_stop(VRX_SETTINGS)`. Then it pushes the view.
- **set_recording.** It stores the flag and pushes the view.
- **chat_check.** While active, a binding unlike the press's means `voice_read_win_stop(VRX_CHAT)`.

- [ ] Step 1: Write the failing test. `tests/test_voice_read_win.c` has a fake ops table that counts every call and keeps the last arguments. `turn_text` returns `strdup(g_text)` (or NULL when `g_text` is NULL), and `start` returns `g_start_ok`. The settings fixture `S(engine)` is a zeroed `Settings` with `ai_enabled = 1`, `voice_enabled = 1`, `voice_mode = VOICE_MODE_DICTATION` and `voice_speech_engine = engine`. Each test begins with `voice_read_win_set_ops(&fake)` and `voice_read_win_apply(&s, 1)`.

  ```c
  int test_vread_win_ai_press_speaks(void);          /* press(7): start 1 (token 0x7FFFFFF3, AI), speak 1
      (id 1, text == g_text, from 0), view last (on 1, reading 7, disabled 0) */
  int test_vread_win_windows_waits_ready(void);      /* WINDOWS: press -> start 1, speak 0; on_tts(READY) -> speak 1 */
  int test_vread_win_gate_refuses(void);             /* recording 1, or voice_mode CONVERSATION, or
      voice_enabled 0, or apply(&s, 0) before the press: start 0, turn_text 0, speak 0 */
  int test_vread_win_other_tokens_ignored(void);     /* reading: on_tts with VOICE_TTS_SAMPLE_TOKEN,
      VOICE_AIS_SAMPLE_TOKEN and token 5 (DONE and ERROR): no op called, voice_read_win_active() 1 */
  int test_vread_win_error_releases_then_fresh_start(void); /* WINDOWS reading: on_tts(ERROR, reply 1,
      hr 0x80004005): quit 1, note "Could not read the reply aloud: the Windows voice did not start.",
      view reading 0; press(9): start 2 */
  int test_vread_win_start_refused_shows_error(void);       /* g_start_ok 0, AI: note
      "Could not read the reply aloud: AI speech did not start.", speak 0, view reading 0 */
  int test_vread_win_late_error_of_first_keeps_second(void); /* AI: press(7), press(9);
      on_tts(ERROR, reply 1): quit 0, note 0, active 1; on_tts(DONE, reply 2): view reading 0 */
  int test_vread_win_resolve_reason(void);                   /* AI: on_tts(ERROR, reply 1, VOICE_AIS_HR_RESOLVE),
      fake resolve_error gives "Enter a speech API key": note
      "Could not read the reply aloud: Enter a speech API key." */
  int test_vread_win_empty_text_does_nothing(void);          /* g_text "": start 0, speak 0;
      g_text NULL: start 0 */
  int test_vread_win_settings_change_releases(void);         /* reading AI; apply(s with voice_ai_voice
      changed, 1): stop 1, quit 1, note 0; apply(same s, 1) again: quit still 1 */
  int test_vread_win_mode_switch_releases(void);             /* reading; apply(s with CONVERSATION, 1):
      quit 1, view last on 0 */
  int test_vread_win_sample_stop_is_not_error(void);         /* reading AI; stop(VRX_SAMPLE): stop 1, quit 1, note 0 */
  int test_vread_win_chat_change_stops(void);                /* reading; the fake binding's conv_gen + 1;
      chat_check(): stop 1, quit 0, active 0; same binding: nothing */
  int test_vread_win_recording_disables_view(void);          /* set_recording(1): view last disabled 1 */
  ```
- [ ] Step 2: Run it and see it fail. `pgrep -a wine; xvfb-run -a make wintest 2>&1 | tail -3`: the build fails, with `voice_read_win.h` not found.
- [ ] Step 3: Implement `src/ui/voice_read_win.[ch]` as above. Keep the UI-thread state in static globals, as `voice_conv_win.c` does, and secure-zero the held text.
- [ ] Step 4: Run the tests. `pgrep -a wine; xvfb-run -a make wintest 2>&1 | tail -3` gives `Failed: 0`, and `make -j test 2>&1 | tail -3` gives `Failed: 0`.
- [ ] Step 5: Commit `src/ui/voice_read_win.h src/ui/voice_read_win.c tests/test_voice_read_win.c tests/win_runner.c Makefile`:

  ```
  feat(voice): the Read aloud controller, one token, every stop and error path

  Models: claude-sonnet-5-5 (implementation)

  Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_01AHuBtEd8Zi2fpsjiKHXZt4
  ```

### Task 6: Wire the reader into voice, the AI panel, the window and Settings

**Files:** Modify `src/ui/voice_win.h`, `src/ui/voice_win.c` (`set_ui` 138, `start_recording` 447, `voice_pump_filter` 558-600, `voice_on_chat_changed` 669, `voice_on_chat_changed_msg` 676, the ops table near 925, `voice_on_tts` 970, `voice_on_panel_closed` 1040, `voice_feature_apply` 1064, `voice_feature_shutdown` 1099), `src/ui/ai_chat.h` (the voice block near 200-290), `src/ui/ai_chat_voice.c` (`ai_chat_voice_busy_changed` 430), `src/ui/ai_chat.c` (`WM_COMMAND`, beside `IDC_CMD_DETAIL` near 1240), `src/ui/window.c` (`create_ai_chat` 2534), `src/ui/settings.c` (`voice_play_sample` 1241).
**Interfaces:**
- Consumes: `voice_read_win_*` and `VoiceReadOps` (Task 5); `chat_turn_text()` (Task 1); `chat_listview_set_read_aloud()`, `chat_listview_set_tail_busy()`, `IDC_CHAT_READ_ALOUD` (Task 4).
- Produces:
  - `void voice_read_clicked(int item_id);` (`voice_win.h`): calls `voice_read_win_press(ui_current_settings(), item_id, g_ui != AIV_IDLE)` while `g_audio_up`.
  - `char *ai_chat_voice_turn_text(HWND panel, int item_id);` (`ai_chat.h`): `chat_turn_text(&d->msg_list, item_id, busy_of(d))`, or NULL with no panel.
  - `void ai_chat_voice_read_view(HWND panel, int on, int reading_id, int disabled);` (`ai_chat.h`): `chat_listview_set_read_aloud(d->hChatList, ...)`.

The edits:
- **The real ops table** `k_read_ops` in `voice_win.c`: `rc_tts_start`, `rc_tts_speak`, `rc_tts_stop`, `rc_tts_quit`, plus `rd_turn_text` (`ai_chat_voice_turn_text(ui_ai_panel(), id)`), `rd_view` (`ai_chat_voice_read_view(panel ? panel : ui_ai_panel(), ...)`), `rc_note`, `rd_binding` (`ai_chat_voice_binding(ui_ai_panel(), out)`) and `rc_ai_resolve_error`. `voice_feature_apply` calls `voice_read_win_set_ops(&k_read_ops)` beside `voice_conv_attach`.
- **`voice_feature_apply`.** The on branch ends with `voice_read_win_apply(s, g_audio_up)`. The off branch calls `voice_read_win_stop(VRX_SETTINGS)` and then `voice_read_win_apply(s, 0)` before `voice_tts_quit_all`. `voice_feature_shutdown` calls `voice_read_win_stop(VRX_SETTINGS)` before `voice_tts_quit_all`.
- **`voice_on_tts`.** It calls `voice_read_win_on_tts(m)` first, then the existing sample check and `voice_conv_on_tts(m)` unchanged, then `free(m)`.
- **`start_recording()`.** Its first statement is `voice_read_win_stop(VRX_DICTATION);`. Both the mic button and the key reach it.
- **`set_ui()`.** After `ai_chat_voice_set_ui`, it calls `voice_read_win_set_recording(st != AIV_IDLE)`. The buttons are disabled from the recording's start until the mic button is idle again, transcribing included.
- **`voice_pump_filter`.** After the two existing Escape branches: `if (in == VI_ESCAPE && in_panel && voice_read_win_active()) { voice_read_win_stop(VRX_ESCAPE); return 1; }`. Escape outside the panel still goes to the session.
- **Chat changes.** `voice_on_chat_changed` posts `WM_VOICE_CHAT` also while `voice_read_win_active()`. `voice_on_chat_changed_msg` calls `voice_read_win_chat_check()` first, before its conversation branch returns.
- **`voice_on_panel_closed`.** It calls `voice_read_win_stop(VRX_PANEL)`, whatever `g_audio_up` is.
- **`ai_chat_voice_busy_changed`.** It adds `if (d && d->hChatList) chat_listview_set_tail_busy(d->hChatList, busy_of(d));`. It already runs at every busy change, because the Send button's text flips.
- **`ai_chat.c`.** The `WM_COMMAND` case is `if (d && ctl_id == IDC_CHAT_READ_ALOUD) { voice_read_clicked((int)lParam); return 0; }`.
- **`window.c`.** In `create_ai_chat`, after `ai_chat_voice_attach(hwnd, 1)`, it calls `voice_read_win_view_sync(hwnd)`, because `g_hwndAiChat` is not set yet at that point.
- **`settings.c`.** The first statement of `voice_play_sample` is `voice_read_win_stop(VRX_SAMPLE);`, so the reading stops cleanly before the sample takes the voice or the audio output.

- [ ] Step 1: Write the failing test. This glue is out of reach of both suites: `voice_win.c`, `ai_chat*.c`, `window.c` and `settings.c` are in neither build. Its decisions are pinned by Tasks 1, 2 and 5. Write down the hand checks here, in the order Task 9 records them:
  - (a) the latest and an older reply, Windows voice and AI speech;
  - (b) Stop;
  - (c) switching replies mid-reading;
  - (d) the dictation key and the mic button during a reading;
  - (e) Escape in the panel, and Escape in the terminal reaching the session;
  - (f) a tab switch, New Chat, closing the panel, closing the session;
  - (g) an AI speech failure (a wrong key) shows its row and the button returns;
  - (h) Play sample during a reading;
  - (i) a Settings save that changes Speech, and Mode switched to Conversation;
  - (j) Conversation mode unchanged (no button; replies spoken as before).
- [ ] Step 2: Run it and see it fail. The MinGW syntax check of `src/ui/voice_win.c` fails with `voice_read_win.h` not found, once the include is added first.
- [ ] Step 3: Implement the edits above.
- [ ] Step 4: Run the tests.
  - The MinGW syntax check is clean for `src/ui/voice_win.c`, `src/ui/ai_chat.c`, `src/ui/ai_chat_voice.c`, `src/ui/window.c` and `src/ui/settings.c`.
  - `make -j test 2>&1 | tail -3` gives `Failed: 0`.
  - `pgrep -a wine; xvfb-run -a make wintest 2>&1 | tail -3` gives `Failed: 0`.
  - `grep -n "voice_read_win_on_tts" src/ui/voice_win.c` shows it on the line before `voice_conv_on_tts(m)`, and nothing returns between them.
- [ ] Step 5: Commit `src/ui/voice_win.h src/ui/voice_win.c src/ui/ai_chat.h src/ui/ai_chat_voice.c src/ui/ai_chat.c src/ui/window.c src/ui/settings.c`:

  ```
  feat(voice): Read aloud wired to the panel, dictation, Escape, chat changes and Settings

  Models: claude-sonnet-5-5 (implementation)

  Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_01AHuBtEd8Zi2fpsjiKHXZt4
  ```

### Task 7: `--ui-demo=voice-read-aloud` and the gallery

**Files:** Modify `src/core/ui_demo.c` (`STATE_NAMES` line 23 and the comment above it, `ui_demo_build` 689-730), `src/core/ui_demo.h` (the state comment near line 60), `tests/test_ui_demo.c` (`test_ui_demo_states_list_ends_in_all` 39-60), `src/ui/ai_chat.c` (`ai_chat_apply_demo_extras`, after the voice states near 3038), `tests/integration/cases/30-ui-demo.ps1` (`$galleryStates` line 43 and the comment), `tests/integration/README.md` (the `ui_gallery` row, line 212), `tests/runner.c`.
**Interfaces:**
- Consumes: `chat_turn_refresh()` (Task 1); `chat_listview_set_read_aloud()` (Task 4); `chat_rebuild_replay()` (`src/core/chat_rebuild.h`).
- Produces: the demo state `"voice-read-aloud"`. It sits after `"voice-transcribing"` and before `"settings-voice"`, is not part of `"all"`, and `ui_demo_scrolls_to_top` returns 0 for it. Its conversation is `build_chat(conv)` followed by `build_tool(conv)`.

- [ ] Step 1: Write the failing test.

  ```c
  /* test_ui_demo_states_list_ends_in_all: count 28 -> 29; insert
   *   ASSERT_STR_EQ(states[count - 4], "voice-read-aloud");
   * and move every anchor below it down by one (voice-transcribing is count - 5, ...). */
  int test_ui_demo_voice_read_aloud(void) {
      TEST_BEGIN();
      AiConversation conv; ApprovalQueue a1, a2;
      ASSERT_EQ(ui_demo_state_valid("voice-read-aloud"), 1);
      ASSERT_EQ(ui_demo_scrolls_to_top("voice-read-aloud"), 0);
      ASSERT_EQ(ui_demo_build("voice-read-aloud", &conv, &a1, &a2, NULL, 0), 0);
      ASSERT_EQ(conv.msg_count, 7);          /* system; chat's user + reply; tool's four */
      ASSERT_EQ(a1.count, 0);
      ChatMsgList l; chat_msg_list_init(&l);
      chat_rebuild_replay(&l, &conv, NULL, NULL, NULL);
      ASSERT_EQ(chat_turn_refresh(&l, 1, 0), 2);   /* one button per turn */
      chat_msg_list_clear(&l);
      ai_conv_reset(&conv);
      TEST_END();
  }
  ```
- [ ] Step 2: Run it and see it fail. `make -j test 2>&1 | tail -3`: the two tests above fail (`ui_demo_build` returns -1 for an unknown state).
- [ ] Step 3: Implement.
  - **Core.** Add the name, the build branch and the comments in `ui_demo.c`/`ui_demo.h`.
  - **`ai_chat_apply_demo_extras`.** For this state it runs `chat_turn_refresh(&d->msg_list, 1, 0)` and finds the two button rows, the earlier one and the later one. It then calls `chat_listview_set_read_aloud(d->hChatList, 1, later_id, 0)`, so one reply shows the idle speaker and the other the accent Stop. No engine and no reader is involved; `window.c`'s existing `"voice-"` prefix opens only the voice gate's demo override.
  - **Harness.** Insert `"voice-read-aloud"` after `"voice-transcribing"` in `$galleryStates` and add one comment line. In the README row, change "22 states x 4 themes = 88" to "24 states x 4 themes = 96" (the list holds 23 today) and "the two voice states" to "the three voice states". `automaton` has no `pwsh`, so the case file is checked on Windows (Task 9).
- [ ] Step 4: Run the tests. `make -j test 2>&1 | tail -3` gives `Failed: 0`, and the MinGW syntax check of `src/ui/ai_chat.c` is clean.
- [ ] Step 5: Commit `src/core/ui_demo.c src/core/ui_demo.h tests/test_ui_demo.c src/ui/ai_chat.c tests/integration/cases/30-ui-demo.ps1 tests/integration/README.md tests/runner.c`:

  ```
  feat(ui_demo): a voice-read-aloud state with one reply idle and one reading

  Models: claude-sonnet-5-5 (implementation)

  Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_01AHuBtEd8Zi2fpsjiKHXZt4
  ```

### Task 8: Proof that nothing of speech loads in Dictation mode before a press

**Files:** Modify `src/ui/dll_hook_win.c` (after the `voice-resources` line, near 69), `tests/selftest/check.sh`, `Makefile` (the `selftest-wine` loop and `case`, lines 497-520, and the comment above them).
**Interfaces:**
- Consumes: `voice_tts_running()`, `voice_ais_running()`.
- Produces:
  - A self-report line `voice-speech-contexts: N`, where N = `voice_tts_running() + voice_ais_running()`.
  - A self-test mode `dictai`: Dictation mode, Voice on, AI speech configured, nothing pressed.

- [ ] Step 1: Write the failing test. In the session scratchpad, write two reports:
  - `pass.txt`, a copy of the `requested:`/`module:` lines a passing `ai` run would hold (`requested: winhttp.dll`, `requested: riched20.dll`, `requested: msftedit.dll`, `requested: mmdevapi.dll`, `module: mmdevapi.dll`), then `voice-net-opens: 0`, `voice-resources: 0` and `voice-speech-contexts: 0`;
  - `fail.txt`, the same with `voice-speech-contexts: 1` and `requested: sapi.dll` added.
- [ ] Step 2: Run it and see it fail. `sh tests/selftest/check.sh dictai pass.txt; echo $?` prints the usage line and `2`.
- [ ] Step 3: Implement.
  - **`dll_hook_win.c`.** Write the new line, including the two engine headers.
  - **`check.sh`.** A `dictai)` branch with the same assertions as `ai)`. In every mode but `sapi`, a missing `voice-speech-contexts: N` line or N other than 0 fails. Add `dictai` to the usage line and to the header comment: "dictai (Dictation, Voice on, AI speech configured, nothing pressed: the Read aloud button has created no speech context, `voice-speech-contexts: 0`, and requested neither sapi nor wtsapi32)".
  - **Makefile.** The loop is `for m in off voiceoff on conv sapi ai dictai`, with the case `dictai) sed 's/"voice_mode": "conversation"/"voice_mode": "dictation"/' tests/selftest/nutshell-voice-ai.config > $(SELFTEST_DIR)/$$m/nutshell.config; grep -q '"voice_mode": "dictation"' $(SELFTEST_DIR)/$$m/nutshell.config || { echo "FAIL: the dictai config is not Dictation (the sed matched nothing)"; exit 1; } ;;`, in CRLF like the rest of the file.
- [ ] Step 4: Run the tests.
  - `sh tests/selftest/check.sh dictai pass.txt` prints `PASS (dictai)` and exits 0.
  - `sh tests/selftest/check.sh dictai fail.txt` prints a `FAIL (dictai)` line for each fault and exits 1.
  - `sh tests/selftest/check.sh on pass.txt` still checks `voice-speech-contexts`.
  - The MinGW syntax check of `src/ui/dll_hook_win.c` is clean.
  - `make -n selftest-wine | grep -c dictai` is at least 1.

  The real run, `make selftest-wine`, is the build agent's (After the last task).
- [ ] Step 5: Commit `src/ui/dll_hook_win.c tests/selftest/check.sh Makefile`:

  ```
  test(selftest): dictai mode proves Dictation with AI speech requests no speech before a press

  Models: claude-sonnet-5-5 (implementation)

  Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_01AHuBtEd8Zi2fpsjiKHXZt4
  ```

### Task 9: (moved to the build step)

The Windows hand checks are no longer a task. Since 2026-11-28 every code pull request
carries them as a file under `docs/test-log/` (CLAUDE.md, "Keep the Windows test log"), and
the file's name needs the version, which is known only at build time. The build agent
writes it: see "After the last task", step 3. The checks it must contain, one numbered
check each in the README's Do / Expect / If it fails / Result format:

  ```
        (a) The speaker button on the latest and on an older reply, with the Windows voice
        and with AI speech; one button per turn, under the last text row of a turn that used
        tools. (b) Stop ends the reading at once. (c) Another reply's button mid-reading
        switches to it. (d) The dictation key and the mic button during a reading stop it
        before the microphone opens; the buttons are greyed while recording and
        transcribing. (e) Escape in the AI panel stops it; Escape in the terminal reaches the
        session. (f) A tab switch, New Chat, closing the panel and closing the session stop
        it. (g) An AI speech failure (a wrong key) shows "Could not read the reply aloud: ..."
        in the chat and the button returns. (h) Play sample during a reading stops it with no
        error row. (i) A Settings save that changes Speech, and Mode switched to
        Conversation, stop it; Conversation mode shows no button and speaks as before.
        (j) Settings > Voice in Dictation mode: the Speech heading and its rows live,
        Conversation greyed. (k) 100 % and 150 % scaling on a light and a dark theme.
        (l) `ui_gallery` with `voice-read-aloud`, and `settings_voice_gallery`, on the four
        themes (the case file is unparsed on automaton: no pwsh).
  ```

## After the last task

1. One final whole-branch review by Fable (each task was reviewed by Opus), given the diff `git diff origin/main...HEAD`, the spec and this plan's Review Focus. The maintainer sees its findings before any fix.
2. Its fix wave, then a scoped re-review of the fixes only.
3. One build agent, once, in this order (CLAUDE.md: the exe is built after the final review's fix wave):
   - Merge `origin/main` in.
   - Set the version one above both `main`'s and the branch's last build at build time, in `src/ui/resource.h` (`APP_VERSION`, `APP_VERSION_BINARY`) and in README's `**Version**:` line (after `1.x.99` comes `1.(x+1).0`).
   - Write `docs/test-log/vX.Y.Z-voice-read-aloud.md` from the list under Task 9, in the format of `docs/test-log/README.md`, and add one Open item to `docs/superpowers/specs/2026-09-06-ui-redesign-notes.md` that names the feature and points at that file without repeating the steps.
   - `make clean && make release`, `make -j test`, `make test SAN=1`, `make lint-gate`, `make fuzz-run`, `pgrep -a wine` then `xvfb-run -a make wintest`, `make selftest-wine` (now seven modes, `dictai` included) and `bash .github/scripts/check-version.sh origin/main`.
   - Commit the bump, `build/win/nutshell.exe`, the test-log file and the notes line.
4. Push and open a draft pull request whose body carries the `Models:` line of every model that touched the branch.
5. Merge when ready (CLAUDE.md, the maintainer's rule of 2026-11-28): every check green on the latest commit, up to date with `main`, the review findings fixed after the maintainer has seen them.

## Running beside other branches (maintainer, 2026-11-28: "be wary of merge conflicts")

- Before Task 5 (it adds `tests/test_voice_read_win.c` to the Makefile's `WIN_ONLY_TESTS` and `WIN_TEST_SRCS` lines), merge `origin/main` in: `fix/session-io` changes the same `WIN_ONLY_TESTS` line.
- Before Task 7 (a new `--ui-demo` state and the gallery count), merge `origin/main` in: `fix/ai-panel-small` adds a row to an existing demo state.
- `fix/ai-panel-small` also changes the cursor path in `src/ui/chat_listview.c`, and `fix/voice-self-interrupt` changes `src/ui/voice_conv_win.c`; both are small. A conflict at a merge of `main` is resolved by keeping both sides' intent, by the agent doing that task, and reported.
- New entries in `tests/runner.c` and `tests/win_runner.c` go next to the related module's existing entries, never at the end of the lists.
