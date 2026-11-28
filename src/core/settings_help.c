#include "settings_help.h"

#include "settings_layout.h"
#include "voice_config.h"

/*
 * The help table, one entry per Settings control, in the order the dialog
 * shows them (the SettingsHelpId order). `text` is the tooltip: its heading
 * (the title, else the label), a blank line, the body and, for a setting, a
 * "Default:" line. tests/test_settings_help.c checks every default and range
 * here against config_default_settings(), the validators and the label
 * sources. All strings are ASCII.
 */

#define E(id, page, kind, label, title, text, defl, mn, mx, flags) \
    { id, #id, page, kind, label, title, text, defl, mn, mx, flags }

static const SettingsHelp k_help[SH_COUNT] = {

    /* APPEARANCE */
    E(SH_COLOUR_SCHEME, SETTINGS_PAGE_APPEARANCE, SH_KIND_SETTING,
      "Colour Scheme:",
      NULL,
      "Colour Scheme:\n\nThe colours of the whole window: terminal, tab "
      "strip, menus, AI panel and dialogs. Onyx Synapse and Sage & Sand "
      "are dark; Onyx Light and Moss & Mist are light. Pick one and "
      "press Save; every open tab changes at once.\nDefault: Onyx Synapse.",
      "Onyx Synapse",
      0, 0, 0u),
    E(SH_TERMINAL_FONT, SETTINGS_PAGE_APPEARANCE, SH_KIND_SETTING,
      "Terminal Font:",
      NULL,
      "Terminal Font:\n\nThe font the terminal draws text in. Only the "
      "fonts on Nutshell's list that are installed on this PC are "
      "offered; install another from the list (Cascadia Code, Fira Code, "
      "JetBrains Mono, Hack and others) and it appears here. Every open "
      "tab changes when you press Save, and the shell is told the new "
      "column and row count. The AI panel's code font is set separately, "
      "below.\nDefault: Consolas.",
      "Consolas",
      0, 0, 0u),
    E(SH_FONT_SIZE, SETTINGS_PAGE_APPEARANCE, SH_KIND_SETTING,
      "Font Size:",
      NULL,
      "Font Size:\n\nThe terminal font size in points: 6, 8, 10, 12, 14, "
      "16, 18 or 20. A larger size fits fewer columns and rows in the "
      "window, and the shell is told the new size at once. Zooming the "
      "terminal (Ctrl+= and Ctrl+-, or Ctrl and the mouse wheel) steps "
      "through the same sizes, and this box shows where zoom left "
      "it.\nDefault: 10.",
      "10",
      0, 0, 0u),
    E(SH_AI_FONT, SETTINGS_PAGE_APPEARANCE, SH_KIND_SETTING,
      "AI Assist Font:",
      NULL,
      "AI Assist Font:\n\nThe font the AI panel uses for code blocks and "
      "commands; replies and your messages stay in the interface font, "
      "and the terminal is not affected. Only installed fonts from "
      "Nutshell's list are offered. The panel reads it when it is "
      "created: if it has been opened since Nutshell started, the change "
      "shows after Undock or Dock, or after a restart.\nDefault: Consolas.",
      "Consolas",
      0, 0, 0u),

    /* TERMINAL */
    E(SH_SCROLLBACK, SETTINGS_PAGE_TERMINAL, SH_KIND_SETTING,
      "Scrollback Lines:",
      NULL,
      "Scrollback Lines:\n\nLines that scroll off the top of the screen "
      "are kept here so you can scroll back to them; once a tab holds "
      "this many, the oldest are dropped. Set from 100 to 50,000. Memory "
      "is used only as lines are kept, about 20 bytes per character "
      "cell: a new tab costs little, and a full buffer on a 200-column "
      "tab takes about 40 MB at 10,000 lines and 200 MB at 50,000, and "
      "briefly about as much again while the tab is resized. The AI "
      "assistant can read no more than the tab keeps, whatever Max "
      "Terminal Lines says. A change "
      "applies to tabs opened after Save; tabs already open keep their "
      "size.\nDefault: 10,000.",
      "10,000",
      100, 50000, SH_F_THOUSANDS),
    E(SH_PASTE_DELAY, SETTINGS_PAGE_TERMINAL, SH_KIND_SETTING,
      "Paste Delay (ms):",
      NULL,
      "Paste Delay (ms):\n\nA pause between lines when you paste several "
      "lines into the terminal, from 0 to 5,000 milliseconds. Raise it "
      "for a device or slow shell that drops input sent too fast, such "
      "as a router or switch console; 0 sends the whole paste at once. "
      "It is skipped when the program on the other end has turned on "
      "bracketed paste, as most modern shells and editors do, because "
      "that program takes the paste as one block.\nDefault: 350.",
      "350",
      0, 5000, SH_F_THOUSANDS),
    E(SH_PASTE_CONFIRM, SETTINGS_PAGE_TERMINAL, SH_KIND_SETTING,
      "Confirm before pasting",
      NULL,
      "Confirm before pasting\n\nShows what you are about to paste in a "
      "preview before it reaches the terminal, so a clipboard holding "
      "something unexpected (a password, a long script, a line that "
      "would run at once) is caught first. Enter pastes, Tab moves to "
      "Cancel and Escape cancels. Control characters are stripped from "
      "every paste whether this is ticked or not.\nDefault: on.",
      "on",
      0, 0, 0u),

    /* LOGGING */
    E(SH_LOG_DIR, SETTINGS_PAGE_LOGGING, SH_KIND_SETTING,
      "Log Directory:",
      NULL,
      "Log Directory:\n\nThe folder session logs go to; a log is started "
      "and stopped per tab with File > Start Logging and is plain text "
      "with the terminal's colour codes removed. Leave this empty to use "
      "the folder nutshell.exe is in. The folder must already exist: if "
      "it does not, Start Logging does nothing and no log is "
      "written.\nDefault: empty (the folder nutshell.exe is in).",
      "empty (the folder nutshell.exe is in)",
      0, 0, 0u),
    E(SH_LOG_FORMAT, SETTINGS_PAGE_LOGGING, SH_KIND_SETTING,
      "Log Name Format:",
      NULL,
      "Log Name Format:\n\nThe date and time part of each log file's name; "
      "the file is called <time>_<session>.log. Use %Y year, %m month, "
      "%d day, %H hour, %M minute and %S second; %y, %j, %a, %A, %b, %B, "
      "%p, %Z, %z and %% work too. A format with any other % code is "
      "ignored and the default is used. A \\ or / makes a subfolder, "
      "which must already exist, and : * ? \" < > | are refused in a "
      "file name; either way Start Logging then does nothing.\nDefault: "
      "%Y-%m-%d_%H-%M-%S.",
      "%Y-%m-%d_%H-%M-%S",
      0, 0, 0u),

    /* SSH */
    E(SH_SSH_IDLE, SETTINGS_PAGE_SSH, SH_KIND_SETTING,
      "Idle Timeout (mins):",
      NULL,
      "Idle Timeout (mins):\n\nDisconnects an SSH tab after this many "
      "minutes with no activity from you, from 0 to 10,080 (one week); 0 "
      "never disconnects. Keystrokes, mouse-wheel scrolling, switching "
      "tabs and typing in the AI panel count as activity; output from "
      "the server does not. The tab shows \"[Disconnected after N min "
      "idle]\". Local shell and telnet tabs are never disconnected by "
      "this, and a number outside 0 to 10,080 is not saved.\nDefault: 0.",
      "0",
      0, 10080, SH_F_THOUSANDS | SH_F_ZERO_MEANS_NEVER | SH_F_KEEP_OUT_OF_RANGE),

    /* STARTUP */
    E(SH_SESSION_MGR_STARTUP, SETTINGS_PAGE_STARTUP, SH_KIND_SETTING,
      "Open Session Manager at startup",
      NULL,
      "Open Session Manager at startup\n\nOpens the Session Manager as "
      "soon as Nutshell starts, so you can pick a session straight away. "
      "It does not open when Auto-connect at startup connects a session, "
      "when a session is named on the command line, or when Nutshell is "
      "started with -nc or --local.\nDefault: off.",
      "off",
      0, 0, 0u),
    E(SH_AUTOCONNECT, SETTINGS_PAGE_STARTUP, SH_KIND_SETTING,
      "Auto-connect at startup",
      NULL,
      "Auto-connect at startup\n\nConnects the session chosen in Session, "
      "below, as soon as Nutshell starts. A session named on the command "
      "line is opened instead, and starting Nutshell with -nc skips "
      "auto-connect once. If the chosen session no longer exists, "
      "Nutshell says so and opens the Session Manager.\nDefault: off.",
      "off",
      0, 0, 0u),
    E(SH_AUTOCONNECT_SESSION, SETTINGS_PAGE_STARTUP, SH_KIND_SETTING,
      "Session:",
      NULL,
      "Session:\n\nThe saved session Auto-connect at startup opens, listed "
      "by name, or by host for a session without a name. A session that "
      "has since been deleted stays in the list so that saving does not "
      "lose it; pick another to replace it. It can be changed only while "
      "Auto-connect at startup is ticked.\nDefault: empty (none chosen).",
      "empty (none chosen)",
      0, 0, 0u),

    /* AI_ENABLE */
    E(SH_AI_ENABLE, SETTINGS_PAGE_AI_PROVIDER, SH_KIND_SETTING,
      "Enable AI assistant",
      NULL,
      "Enable AI assistant\n\nTurns the AI assistant on or off. While it is "
      "off, Nutshell loads none of the assistant, makes none of its "
      "network requests, and keeps its settings and API key unused. Turning "
      "it on needs no restart. A key you have already saved stays saved "
      "while it is off.\nDefault: Off.",
      "Off",
      0, 0, 0u),

    /* AI_PROVIDER */
    E(SH_AI_PROVIDER, SETTINGS_PAGE_AI_PROVIDER, SH_KIND_SETTING,
      "Provider:",
      NULL,
      "Provider:\n\nThe AI service the assistant uses: anthropic, openai, "
      "gemini, moonshot, deepseek, or custom for any OpenAI-compatible "
      "endpoint given in Base URL. Changing it clears Model; pick or "
      "type one again, or leave Model empty for the provider's default. "
      "Tool calls (web search, web fetch, the debug log) work with "
      "anthropic, openai, deepseek and moonshot; gemini and custom get "
      "them only with Commands as tool calls set to On.\nDefault: anthropic.",
      "anthropic",
      0, 0, 0u),
    E(SH_AI_KEY, SETTINGS_PAGE_AI_PROVIDER, SH_KIND_SETTING,
      "API Key:",
      NULL,
      "API Key:\n\nYour key from the provider's developer console. There "
      "is one key field, so switching provider means entering that "
      "provider's key. It is stored in nutshell.config encrypted with "
      "Windows DPAPI: only your Windows account on this PC can read it, "
      "and on another PC or account you enter it again. Hold the eye "
      "button to see what you typed. The AI button in the tab strip "
      "turns green once a key is set.\nDefault: empty.",
      "empty",
      0, 0, 0u),
    E(SH_AI_MODEL, SETTINGS_PAGE_AI_PROVIDER, SH_KIND_SETTING,
      "Model:",
      NULL,
      "Model:\n\nThe model the assistant uses, typed in or picked from the "
      "list that Refresh models, the button beside it, fills. Left "
      "empty, each provider uses its built-in default, for anthropic "
      "claude-sonnet-4-6; custom has none, so type the name your server "
      "expects, such as llama3.1. A typed name must match the provider's "
      "model id exactly.\nDefault: empty (the provider's default model).",
      "empty (the provider's default model)",
      0, 0, 0u),
    E(SH_AI_REFRESH, SETTINGS_PAGE_AI_PROVIDER, SH_KIND_ACTION,
      "Refresh models",
      NULL,
      "Refresh models\n\nFetches the provider's model list with the API "
      "key above (it asks for one if the box is empty) and fills the "
      "Model list; the button is disabled while it fetches, and an error "
      "opens a message box. For custom, the list comes from the Base URL "
      "cut at /v1/ followed by /v1/models, or, without /v1/, the Base "
      "URL with its last path part replaced by /models. Nothing is saved "
      "until you press Save.",
      NULL,
      0, 0, 0u),
    E(SH_AI_BASE_URL, SETTINGS_PAGE_AI_PROVIDER, SH_KIND_SETTING,
      "Base URL:",
      NULL,
      "Base URL:\n\nThe full endpoint address for the custom provider, for "
      "example https://host/v1/chat/completions; shown only when "
      "Provider is custom. Use https. With plain http to anything but "
      "this PC, your API key and terminal text cross the network "
      "unencrypted, and a warning under the box says so. Refresh models "
      "derives the model list address from this. A custom endpoint gets "
      "tool calls only with Commands as tool calls set to On.\nDefault: empty.",
      "empty",
      0, 0, 0u),
    E(SH_AI_RUNCMD_MODE, SETTINGS_PAGE_AI_PROVIDER, SH_KIND_SETTING,
      "Commands as tool calls:",
      NULL,
      "Commands as tool calls:\n\nHow the assistant runs terminal "
      "commands. On: through its run_command tool, each call an ordinary "
      "command card; for custom it also tells Nutshell the endpoint "
      "accepts the OpenAI \"tools\" field, so web search, web fetch and "
      "the debug log come with it, and a server that silently ignores "
      "tools then leaves the assistant unable to run anything. Off: "
      "[EXEC] markers; web tools stay as they are. Automatic: On for "
      "anthropic, openai and moonshot; Off for deepseek, gemini and "
      "custom, whose tool calls are not yet verified against the live "
      "services. Turn it Off if requests start failing with an error "
      "about tools, or the assistant stops running commands.\n"
      "Default: Automatic.",
      "Automatic",
      0, 0, 0u),

    /* AI_BEHAVIOUR */
    E(SH_AI_CONTEXT_LINES, SETTINGS_PAGE_AI_BEHAVIOUR, SH_KIND_SETTING,
      "Max Terminal Lines:",
      NULL,
      "Max Terminal Lines:\n\nHow many of the latest terminal lines the "
      "assistant reads with every message you send, from 1 to 50,000. "
      "It can read no more than the tab keeps, which is Scrollback "
      "Lines plus the rows on screen. "
      "More lines let it see more of your session, but every line is "
      "sent each time, so cost and reply time grow with it. Lower it if "
      "replies get slow or the context meter on the AI panel fills up; "
      "raise it when the assistant misses output that scrolled "
      "past.\nDefault: 1,000.",
      "1,000",
      1, 50000, SH_F_THOUSANDS),
    E(SH_AI_SYSTEM_NOTES, SETTINGS_PAGE_AI_BEHAVIOUR, SH_KIND_SETTING,
      "System Instructions:",
      NULL,
      "System Instructions:\n\nInstructions sent with every conversation "
      "in every tab, such as how you like answers written or rules the "
      "assistant must follow. Up to 2,559 characters, about 400 words. A "
      "session's own AI Notes, set in the Session Manager, are sent "
      "after these, so they can add detail for one server. Leave it "
      "empty for none.\nDefault: empty.",
      "empty",
      0, 0, 0u),
    E(SH_AI_MARKDOWN, SETTINGS_PAGE_AI_BEHAVIOUR, SH_KIND_SETTING,
      "Render AI markdown",
      NULL,
      "Render AI markdown\n\nShows the assistant's replies formatted: "
      "headings, lists, tables, bold text and code blocks, in the "
      "theme's colours. Untick it to see the raw text with its markdown "
      "symbols, for example to check exactly what the model sent. The "
      "open AI panel changes when you press Save.\nDefault: on.",
      "on",
      0, 0, 0u),
    E(SH_AI_THINKING_MODE, SETTINGS_PAGE_AI_BEHAVIOUR, SH_KIND_SETTING,
      "Thinking display:",
      NULL,
      "Thinking display:\n\nHow a Thinking row shows the model's reasoning. "
      "Automatic shows the last 25 lines while a reply streams and "
      "collapses the row once it is idle. Always collapsed, Always compact "
      "(the last four lines), Always extended (the last 25) and Always open "
      "hold that mode for every Thinking row. "
      "Clicking a Thinking row cycles collapsed, compact, extended, open and saves "
      "that choice here; choosing Automatic resets it.\n"
      "Default: Automatic.",
      "Automatic",
      0, 0, 0u),
    E(SH_AI_POLICY_ALLOWED, SETTINGS_PAGE_AI_BEHAVIOUR, SH_KIND_SETTING,
      "Allowed:",
      NULL,
      "Allowed:\n\nThe highest level of command a new AI session may run: "
      "Read, Unknown, Write or Critical. Commands above it are blocked "
      "and shown as held; commands at or below it ask you first, unless "
      "Auto-approve is on. This is only where a new session starts: the "
      "AI panel's status line changes it for one session. It never holds "
      "a tool call; web_fetch is governed by Auto-approve.\nDefault: Read.",
      "Read",
      0, 0, 0u),
    E(SH_AI_POLICY_AUTO, SETTINGS_PAGE_AI_BEHAVIOUR, SH_KIND_SETTING,
      "Auto-approve by default",
      NULL,
      "Auto-approve by default\n\nStarts new AI sessions with Auto-approve "
      "on, so commands up to the Allowed level run without asking. With "
      "Allowed at Critical, Save asks you to confirm. Even when it is "
      "on, a background tab always asks, and after 3 automatic rounds in "
      "a row the next batch waits for you. With Allowed at Unknown or "
      "above it also lets web_fetch run without a card, up to the "
      "Automatic tool rounds limit.\nDefault: off.",
      "off",
      0, 0, 0u),
    E(SH_AI_TOOL_TIMEOUT, SETTINGS_PAGE_AI_BEHAVIOUR, SH_KIND_SETTING,
      "Tool approval timeout (seconds):",
      NULL,
      "Tool approval timeout (seconds):\n\nHow long a tool approval card "
      "waits for Run or Deny before it expires and the call is not run: "
      "15 to 3,600 seconds, or 0 to wait until you answer, press Stop or "
      "close the tab. A card appears when Auto-approve does not cover "
      "the call, when it comes from a background tab, or after the "
      "Automatic tool rounds limit. A new value applies to replies "
      "started after Save, and a number outside these is not saved. "
      "Command approvals are not affected.\nDefault: 60.",
      "60",
      15, 3600, SH_F_THOUSANDS | SH_F_ZERO_MEANS_NEVER | SH_F_KEEP_OUT_OF_RANGE),
    E(SH_AI_TOOL_ROUNDS, SETTINGS_PAGE_AI_BEHAVIOUR, SH_KIND_SETTING,
      "Automatic tool rounds:",
      NULL,
      "Automatic tool rounds:\n\nHow many rounds in a row the assistant "
      "may run a tool that would otherwise ask (web_fetch today) without "
      "asking, while Auto-approve covers it, from 1 to 20. When the "
      "count is reached, the next such call waits on a card under "
      "\"[Auto-approve paused after N automatic tool rounds]\", for as "
      "long as Tool approval timeout allows. Typing a message or "
      "pressing Run on a card starts the count again; web_search and "
      "read_debug_log never count. A change applies to replies started "
      "after Save; a number outside 1 to 20 is not saved.\nDefault: 3.",
      "3",
      1, 20, SH_F_KEEP_OUT_OF_RANGE),

    /* VOICE */
    E(SH_VOICE_ENABLE, SETTINGS_PAGE_VOICE, SH_KIND_SETTING,
      "Enable voice",
      NULL,
      "Enable voice\n\nTurns voice input on or off. It is off by default, "
      "and while it is off Nutshell loads nothing of voice and Right Shift, "
      "Right Ctrl, F8 and F9 behave as they always did. It needs the AI assistant: with "
      "the assistant off this box is disabled. Audio is sent to the chosen "
      "provider only for a recording you start.\nDefault: Off.",
      "Off",
      0, 0, 0u),
    E(SH_VOICE_STT_USE_AI, SETTINGS_PAGE_VOICE, SH_KIND_SETTING,
      "Use current AI assistant to convert speech to text",
      NULL,
      "Use current AI assistant to convert speech to text\n\nSends your "
      "recording to the AI assistant's own provider, with its API key, so "
      "there is nothing more to set up. It is unavailable when that provider "
      "offers no speech-to-text, and a note beside the box says why. "
      "Untick it to pick a speech provider and key of your own "
      "below.\nDefault: On.",
      "On",
      0, 0, 0u),
    E(SH_VOICE_STT_PROVIDER, SETTINGS_PAGE_VOICE, SH_KIND_SETTING,
      "Provider:",
      NULL,
      "Provider:\n\nThe speech-to-text service that turns your recording "
      "into text: openai, groq, mistral, gemini, or custom for any "
      "OpenAI-compatible server given in Base URL. Changing it refills the "
      "Model list with that provider's models. Shown only when Use current "
      "AI assistant is unticked.\nDefault: openai.",
      "openai",
      0, 0, 0u),
    E(SH_VOICE_STT_KEY, SETTINGS_PAGE_VOICE, SH_KIND_SETTING,
      "API key:",
      NULL,
      "API key:\n\nThe key for the speech-to-text provider above. It is "
      "stored encrypted for your Windows user, like the AI assistant's key, "
      "and sent only to that provider. The eye button beside the box shows "
      "or hides it. Shown only when Use current AI assistant is "
      "unticked.\nDefault: empty.",
      "empty",
      0, 0, 0u),
    E(SH_VOICE_STT_URL, SETTINGS_PAGE_VOICE, SH_KIND_SETTING,
      "Base URL:",
      NULL,
      "Base URL:\n\nThe base URL of the custom speech-to-text server, such "
      "as https://host/v1, not a full endpoint; a pasted "
      "/audio/transcriptions address is accepted and trimmed back. Shown "
      "only when Provider is custom. Use https: with plain http to anything "
      "but this PC, your key and recordings cross the network unencrypted, "
      "and a warning says so.\nDefault: empty.",
      "empty",
      0, 0, 0u),
    E(SH_VOICE_STT_MODEL, SETTINGS_PAGE_VOICE, SH_KIND_SETTING,
      "Model:",
      NULL,
      "Model:\n\nThe speech-to-text model, typed in or picked from the list "
      "for the provider. Left empty, each provider uses its default model. "
      "A typed name must match the provider's model id exactly, and the "
      "list is only a suggestion, so a newer model can be typed "
      "in.\nDefault: empty (the provider's default model).",
      "empty (the provider's default model)",
      0, 0, 0u),
    E(SH_VOICE_STT_LIVE_MODEL, SETTINGS_PAGE_VOICE, SH_KIND_SETTING,
      "Live model:",
      NULL,
      "Live model:\n\nThe model used while the words appear as you speak, "
      "with OpenAI or Mistral. Shown only for those two providers. Left "
      "empty, OpenAI uses gpt-live-transcribe and Mistral uses "
      "voxtral-mini-transcribe-realtime-3. A typed name must match the "
      "provider's model id exactly."
      "\nDefault: the provider's default.",
      "the provider's default",
      0, 0, 0u),
    E(SH_VOICE_LANGUAGE, SETTINGS_PAGE_VOICE, SH_KIND_SETTING,
      "Language:",
      NULL,
      "Language:\n\nTells the speech-to-text service which language you will "
      "speak. Auto-detect lets it guess, which is weakest on short commands. "
      "English (UK) also asks for British spelling where the provider allows "
      "it (OpenAI, Groq, custom servers, Gemini). Not listed? Choose Other... "
      "and type a two-letter ISO 639-1 language code such as cy for Welsh or "
      "mi for Maori, or the language's English name such as Welsh; your "
      "provider must support it.\nDefault: English (UK).",
      "English (UK)",
      0, 0, 0u),
    E(SH_VOICE_LANGUAGE_OTHER, SETTINGS_PAGE_VOICE, SH_KIND_SETTING,
      "Other language:",
      NULL,
      "Other language:\n\nType a two-letter ISO 639-1 language code such as "
      "cy for Welsh or mi for Maori, or the language's English name such as "
      "Welsh. Your speech-to-text provider must support it.\nDefault: empty.",
      "empty",
      0, 0, 0u),
    E(SH_VOICE_DEVICE, SETTINGS_PAGE_VOICE, SH_KIND_SETTING,
      "Input device:",
      NULL,
      "Input device:\n\nThe microphone voice input records from. Windows "
      "default follows whatever Windows uses, and a named microphone is "
      "used only while it is plugged in; otherwise Windows default is used "
      "and your choice is kept. The list is filled after Enable voice has "
      "been saved on.\nDefault: Windows default.",
      "Windows default",
      0, 0, 0u),
    E(SH_VOICE_TEST, SETTINGS_PAGE_VOICE, SH_KIND_ACTION,
      "Test",
      NULL,
      "Test\n\nListens to the microphone and shows the level on the meter until "
      "you press Stop test, change page or close Settings. Nothing is "
      "recorded or sent.",
      NULL,
      0, 0, 0u),
    E(SH_VOICE_MIC_GAIN, SETTINGS_PAGE_VOICE, SH_KIND_SETTING,
      "Microphone gain:",
      "Microphone gain",
      "Microphone gain\n\nBoosts or lowers the microphone in Nutshell only, "
      "before the recording is sent. Other apps are not affected. Watch the "
      "meter while you press Test and aim for Good. The range is -12 to 30 dB.\nDefault: 0 dB.",
      "0 dB",
      VOICE_GAIN_MIN_DB, VOICE_GAIN_MAX_DB, 0u),
    E(SH_VOICE_PTT_KEY, SETTINGS_PAGE_VOICE, SH_KIND_SETTING,
      "Push-to-talk key:",
      NULL,
      "Push-to-talk key:\n\nThe key you press to speak while the AI panel "
      "has focus: Right Shift, Right Ctrl, F8 or F9. Right Shift chords "
      "such as a capital letter, Shift+click, Shift+Insert, Shift+arrows "
      "and Ctrl+Shift+C keep working as they always did, because the "
      "microphone opens only on a clean press of the key alone; a press with "
      "another modifier already held (Ctrl+F8, Shift+Right Ctrl) never starts "
      "one. In Hold to talk, any other key or a mouse click while you "
      "hold it cancels the recording; in Toggle the recording carries on.\n"
      "Default: Right Shift.",
      "Right Shift",
      0, 0, 0u),
    E(SH_VOICE_PTT_MODE, SETTINGS_PAGE_VOICE, SH_KIND_SETTING,
      "Key mode:",
      NULL,
      "Key mode:\n\nToggle records from one clean press of the key to the "
      "next. Hold to talk records while you hold the key and stops when "
      "you let go. A recording stops by itself after 10 minutes when the "
      "words appear as you speak (OpenAI and Mistral), and after 60 seconds "
      "otherwise. Escape throws it away, but only while the AI panel has the "
      "keyboard focus. Hold to talk and the Mic start delay apply in Dictation "
      "only.\nDefault: Toggle.",
      "Toggle",
      0, 0, 0u),
    E(SH_VOICE_MIC_DELAY, SETTINGS_PAGE_VOICE, SH_KIND_SETTING,
      "Mic start delay (ms):",
      NULL,
      "Mic start delay (ms):\n\nThe microphone opens only once the key has "
      "been held alone this long, so a shortcut that begins with the key "
      "never opens it. Set from 0 to 400 milliseconds; a lower number starts "
      "recording sooner, a higher one is safer against accidental use. It "
      "applies to Hold to talk only.\nDefault: 150.",
      "150",
      VOICE_MIC_DELAY_MIN, VOICE_MIC_DELAY_MAX, 0u),
    E(SH_VOICE_AUTO_SEND, SETTINGS_PAGE_VOICE, SH_KIND_SETTING,
      "Send automatically when I finish speaking",
      NULL,
      "Send automatically when I finish speaking\n\nWhen off, your words "
      "appear in the message box for you to read, edit and send with Enter; "
      "nothing is sent until you do. When on, the transcript is sent as soon "
      "as it arrives, as a message you typed would be, and every approval "
      "still applies. Either way, if the box already holds a draft, or the "
      "assistant is busy, your words are added to the box and not sent."
      "\nDefault: Off.",
      "Off",
      0, 0, 0u),
    E(SH_VOICE_MODE, SETTINGS_PAGE_VOICE, SH_KIND_SETTING,
      "Mode:",
      NULL,
      "Mode:\n\nDictation puts your words in the message box for you to read "
      "and send with Send, and each finished reply has a speaker button "
      "under it that reads that reply aloud. Conversation sends each turn "
      "by itself after a pause and reads the assistant's replies aloud. In "
      "Conversation mode "
      "the voice key always toggles: press it to start a conversation, "
      "press it again to end it, or press it to stop a spoken reply. A "
      "conversation also ends on Escape while the AI panel has the keyboard "
      "focus, when Windows locks, or after 3 minutes with nothing said."
      "\nDefault: Dictation, so your words wait in the box for you to press Send.",
      "Dictation, so your words wait in the box for you to press Send",
      0, 0, 0u),
    E(SH_VOICE_PAUSE, SETTINGS_PAGE_VOICE, SH_KIND_SETTING,
      "Pause before sending:",
      NULL,
      "Pause before sending:\n\nIn Conversation mode, how long you must stay "
      "silent before what you said is sent. Set from 0.5 to 3 seconds; a "
      "shorter pause answers sooner but may cut you off mid-thought. It "
      "applies in Conversation mode only.\nDefault: 1.2 seconds.",
      "1.2 seconds",
      0, 0, 0u),
    E(SH_VOICE_WIN_VOICE, SETTINGS_PAGE_VOICE, SH_KIND_SETTING,
      "Windows voice:",
      NULL,
      "Windows voice:\n\nThe voice that reads the assistant's replies aloud "
      "with Windows speech, in Conversation mode and when you press a reply's speaker button. Automatic picks an English (UK) woman's voice "
      "when one is installed, and otherwise the Windows default. To add a "
      "voice, open Windows Settings, then Time & language, then Speech, "
      "and choose Add voices."
      "\nDefault: Automatic (an English (UK) woman's voice if one is installed).",
      "Automatic (an English (UK) woman's voice if one is installed)",
      0, 0, 0u),
    E(SH_VOICE_WIN_VOICE_NAME, SETTINGS_PAGE_VOICE, SH_KIND_SETTING,
      "Windows voice name:",
      NULL,
      "Windows voice name:\n\nThe friendly name of the chosen Windows voice, "
      "kept so Settings can show it when the voice is not installed on this "
      "PC. It is set by the Windows voice choice, not typed."
      "\nDefault: none, as Automatic is used.",
      "none, as Automatic is used",
      0, 0, 0u),
    E(SH_VOICE_SPEECH, SETTINGS_PAGE_VOICE, SH_KIND_SETTING,
      "Speech:",
      NULL,
      "Speech:\n\nWho reads the assistant's replies aloud: every reply in "
      "Conversation mode, and in Dictation mode the reply whose speaker "
      "button you press. Windows speech uses a voice installed on this PC. "
      "AI speech sends the reply's text to the speech provider and plays the "
      "AI-generated voice it returns; every reply read this way is a new "
      "request to the provider. If AI speech fails in Conversation mode, the "
      "rest of the reply is read with Windows speech; in Dictation mode the "
      "reading stops and the chat says why."
      "\nDefault: Windows speech.",
      "Windows speech",
      0, 0, 0u),
    E(SH_VOICE_TTS_PROVIDER, SETTINGS_PAGE_VOICE, SH_KIND_SETTING,
      "Speech provider:",
      NULL,
      "Speech provider:\n\nWho turns the reply text into speech: Mistral "
      "(Voxtral, English (UK) voices), or Custom (an OpenAI-compatible "
      "/audio/speech, for example a Kokoro server on your network). Shown "
      "when Speech is AI speech."
      "\nDefault: Mistral.",
      "Mistral",
      0, 0, 0u),
    E(SH_VOICE_TTS_URL, SETTINGS_PAGE_VOICE, SH_KIND_SETTING,
      "Speech base URL:",
      NULL,
      "Speech base URL:\n\nThe address of the Custom speech server. A pasted "
      "/audio/speech is trimmed. The text of each spoken reply, and the "
      "speech key, go to this server. An http:// address is not encrypted."
      "\nDefault: none.",
      "none",
      0, 0, 0u),
    E(SH_VOICE_TTS_KEY, SETTINGS_PAGE_VOICE, SH_KIND_SETTING,
      "Speech API key:",
      NULL,
      "Speech API key:\n\nShown only when the speech provider is not the "
      "speech-to-text one. It is kept encrypted for this Windows user and "
      "only for the provider it was entered for. When the providers are the "
      "same, the speech-to-text key is used."
      "\nDefault: none; the speech-to-text key is used when the provider is "
      "the same.",
      "none; the speech-to-text key is used when the provider is the same",
      0, 0, 0u),
    E(SH_VOICE_AI_VOICE, SETTINGS_PAGE_VOICE, SH_KIND_SETTING,
      "AI voice:",
      NULL,
      "AI voice:\n\nThe voice the speech provider reads replies in. Empty "
      "means the provider's default, which is Mary for Mistral. Custom needs "
      "one, such as bf_emma on a Kokoro server."
      "\nDefault: the provider's default (for Mistral, Mary, British English).",
      "the provider's default (for Mistral, Mary, British English)",
      0, 0, 0u),
    E(SH_VOICE_AI_MODEL, SETTINGS_PAGE_VOICE, SH_KIND_SETTING,
      "AI speech model:",
      NULL,
      "AI speech model:\n\nThe speech model the provider uses. Empty means "
      "the provider's default. Custom needs one, such as the model name your "
      "server lists."
      "\nDefault: the provider's default.",
      "the provider's default",
      0, 0, 0u),
    E(SH_VOICE_AI_STYLE, SETTINGS_PAGE_VOICE, SH_KIND_SETTING,
      "Accent and style:",
      NULL,
      "Accent and style:\n\nCustom only. What you type here is sent to the "
      "speech server as instructions, for example a calm British accent. "
      "Leave it empty to send none."
      "\nDefault: none.",
      "none",
      0, 0, 0u),
    E(SH_VOICE_PLAY_SAMPLE, SETTINGS_PAGE_VOICE, SH_KIND_ACTION,
      "Play sample",
      NULL,
      "Play sample\n\nSpeaks a short sentence with the chosen speech and "
      "voice, using the values on this page before you press Save. It sends "
      "that sentence to the speech provider when AI speech is chosen.",
      NULL,
      0, 0, 0u),

    /* AI_WEB */
    E(SH_AI_WEB_FETCH, SETTINGS_PAGE_AI_WEB, SH_KIND_SETTING,
      "Permit Web Fetch",
      NULL,
      "Permit Web Fetch\n\nGives the assistant the web_fetch tool, which "
      "downloads an http or https page it names. The address can carry "
      "text from your session to any site, so each fetch asks on a card "
      "unless Auto-approve is on at Unknown or above; a background tab, "
      "and the call after the Automatic tool rounds limit, ask anyway. "
      "It refuses 127.0.0.0/8, 10.0.0.0/8, 172.16.0.0/12, "
      "192.168.0.0/16, 169.254.0.0/16, 0.0.0.0/8, ::1, ::, fc00::/7, "
      "fe80::/10 and IPv4 forms of those in IPv6; 100.64.0.0/10 and "
      "multicast are not refused.\nDefault: off.",
      "off",
      0, 0, 0u),
    E(SH_AI_SEARCH, SETTINGS_PAGE_AI_WEB, SH_KIND_SETTING,
      "Search Engine:",
      NULL,
      "Search Engine:\n\nThe search service behind the assistant's "
      "web_search tool: None, DuckDuckGo (API), DuckDuckGo (HTML) or "
      "Custom. None removes the tool, so the assistant cannot search. "
      "DuckDuckGo (API) falls back to the HTML page when it finds "
      "nothing. Searches run without asking and the query goes to the "
      "search service. Gemini and custom providers get tools only with "
      "Commands as tool calls set to On; without it this has no effect "
      "with them.\nDefault: DuckDuckGo (API).",
      "DuckDuckGo (API)",
      0, 0, 0u),
    E(SH_AI_SEARCH_URL, SETTINGS_PAGE_AI_WEB, SH_KIND_SETTING,
      "Search URL:",
      NULL,
      "Search URL:\n\nYour own search endpoint, used when Search Engine is "
      "Custom and shown only then. Give the address without a query "
      "string: Nutshell adds ?q= and the query, and reads the reply as "
      "DuckDuckGo API JSON. When it returns no results, the search is "
      "retried on DuckDuckGo's HTML page, and left empty with Custom "
      "chosen, searches go to DuckDuckGo (API); either way the query can "
      "reach DuckDuckGo.\nDefault: empty.",
      "empty",
      0, 0, 0u),
    E(SH_AI_MAX_RESULTS, SETTINGS_PAGE_AI_WEB, SH_KIND_SETTING,
      "Max Results:",
      NULL,
      "Max Results:\n\nHow many search results the web_search tool gives "
      "the assistant for each query, from 1 to 20. More results give it "
      "more to choose from, but every result is added to the "
      "conversation and sent with each later message. A number outside 1 "
      "to 20 is not saved.\nDefault: 7.",
      "7",
      1, 20, SH_F_KEEP_OUT_OF_RANGE),

    /* DEBUG */
    E(SH_DBG_ENABLE, SETTINGS_PAGE_DEBUG, SH_KIND_SETTING,
      "Enable debugging",
      NULL,
      "Enable debugging\n\nWrites a detailed log of what Nutshell does to "
      "the log folder, at the levels set below, so you or an AI can find "
      "the cause of a problem. Unticked, only errors and warnings are "
      "written, to the same folder. The default levels cost very little, "
      "so it can stay on. When AI Assist reads the log with its "
      "read_debug_log tool, the text it reads is sent to your AI "
      "provider.\nDefault: off.",
      "off",
      0, 0, 0u),
    E(SH_DBG_LEVEL_RENDER, SETTINGS_PAGE_DEBUG, SH_KIND_SETTING,
      "Rendering:",
      NULL,
      "Rendering:\n\nHow much the log records about drawing the window: "
      "paints, invalidation, the scrollbar, DPI changes and resizing. "
      "Set it to Trace for a display fault such as text that does not "
      "redraw. Levels: Off, Errors (errors and warnings), Info, Debug "
      "and Trace, each adding to the one before; Off records nothing "
      "here, not even for an issue mark.\nDefault: Info.",
      "Info",
      0, 0, 0u),
    E(SH_DBG_LEVEL_TERM, SETTINGS_PAGE_DEBUG, SH_KIND_SETTING,
      "Terminal:",
      NULL,
      "Terminal:\n\nHow much the log records about the terminal emulator: "
      "escape-sequence mode changes, the screen buffer, resizes and the "
      "alternate screen. Use Trace, with Include terminal content, when "
      "output looks wrong; that also records the bytes received. Levels: "
      "Off, Errors (errors and warnings), Info, Debug and Trace, each "
      "adding to the one before; Off records nothing here, not even for "
      "an issue mark.\nDefault: Info.",
      "Info",
      0, 0, 0u),
    E(SH_DBG_LEVEL_AI, SETTINGS_PAGE_DEBUG, SH_KIND_SETTING,
      "AI Assist:",
      NULL,
      "AI Assist:\n\nHow much the log records about the assistant: "
      "requests, streamed replies, tool calls, approvals and the command "
      "dispatcher. Raise it when a reply stalls, a tool call misbehaves "
      "or a command does not run. Levels: Off, Errors (errors and "
      "warnings), Info, Debug and Trace, each adding to the one before; "
      "Off records nothing here, not even for an issue mark.\nDefault: "
      "Info.",
      "Info",
      0, 0, 0u),
    E(SH_DBG_LEVEL_SSH, SETTINGS_PAGE_DEBUG, SH_KIND_SETTING,
      "SSH connection:",
      NULL,
      "SSH connection:\n\nHow much the log records about SSH: connection "
      "stages, the host key, the sign-in method, the channel, keepalives "
      "and timeouts. Raise it when a connection fails, hangs or drops. "
      "Levels: Off, Errors (errors and warnings), Info, Debug and Trace, "
      "each adding to the one before; Off records nothing here, not even "
      "for an issue mark.\nDefault: Info.",
      "Info",
      0, 0, 0u),
    E(SH_DBG_LEVEL_SHELL, SETTINGS_PAGE_DEBUG, SH_KIND_SETTING,
      "Local shell:",
      NULL,
      "Local shell:\n\nHow much the log records about local shell tabs: "
      "which shell was chosen, the console starting and closing, and the "
      "shell exiting. Raise it when a local tab will not start or closes "
      "on its own. Levels: Off, Errors (errors and warnings), Info, "
      "Debug and Trace, each adding to the one before; Off records "
      "nothing here, not even for an issue mark.\nDefault: Info.",
      "Info",
      0, 0, 0u),
    E(SH_DBG_LEVEL_GENERAL, SETTINGS_PAGE_DEBUG, SH_KIND_SETTING,
      "General:",
      NULL,
      "General:\n\nHow much the log records about everything else: "
      "startup, settings, menus, the config file and the logger itself. "
      "Raise it when something goes wrong outside a tab. Levels: Off, "
      "Errors (errors and warnings), Info, Debug and Trace, each adding "
      "to the one before; Off records nothing here, not even for an "
      "issue mark.\nDefault: Info.",
      "Info",
      0, 0, 0u),
    E(SH_DBG_RECORDER, SETTINGS_PAGE_DEBUG, SH_KIND_SETTING,
      "Keep recent detail for issue marks, at",
      "Keep recent detail for issue marks",
      "Keep recent detail for issue marks\n\nKeeps the last few minutes of "
      "detail in a 4 MB memory buffer, at the level chosen beside it, "
      "and writes it to its own file when you mark an issue with "
      "Ctrl+Shift+M. The log file can then stay at Info while an issue "
      "mark still captures Trace detail. A category set to Off is not "
      "recorded, and with debugging off only errors and warnings are "
      "kept.\nDefault: on, at Trace.",
      "on, at Trace",
      0, 0, 0u),
    E(SH_DBG_SCREENSHOT, SETTINGS_PAGE_DEBUG, SH_KIND_SETTING,
      "Save a screenshot with each issue mark",
      NULL,
      "Save a screenshot with each issue mark\n\nSaves a PNG of the "
      "Nutshell window in the log folder each time you mark an issue "
      "with Ctrl+Shift+M. It shows whatever is in the window at that "
      "moment, so look at it before you share the folder.\nDefault: off.",
      "off",
      0, 0, 0u),
    E(SH_DBG_CONTENT, SETTINGS_PAGE_DEBUG, SH_KIND_SETTING,
      "Include terminal content",
      NULL,
      "Include terminal content\n\nAlso records terminal text, AI "
      "commands, chat text and host names in the log. The shell's echo "
      "of what you type is terminal text, so it is recorded too. "
      "Passwords, passphrases and keys, and your keystrokes and pastes "
      "themselves, are never recorded. Leave it off unless a problem "
      "needs the text itself.\nDefault: off.",
      "off",
      0, 0, 0u),
    E(SH_DBG_FOLDER, SETTINGS_PAGE_DEBUG, SH_KIND_SETTING,
      "Log folder:",
      NULL,
      "Log folder:\n\nWhere debug logs, issue marks and screenshots are "
      "written. A relative path is relative to the folder nutshell.exe "
      "is in, and %NAME% environment variables are expanded, for example "
      "%LOCALAPPDATA%\\Nutshell\\logs. Browse picks a folder and Open "
      "shows it in Explorer. An empty box means the default.\nDefault: "
      "logs.",
      "logs",
      0, 0, 0u),
    E(SH_DBG_BROWSE, SETTINGS_PAGE_DEBUG, SH_KIND_ACTION,
      "Browse...",
      NULL,
      "Browse...\n\nPicks the log folder with the Windows folder picker. A "
      "folder inside nutshell.exe's folder is filled in as a relative "
      "path, so the setting keeps working if you move Nutshell together "
      "with its folder.",
      NULL,
      0, 0, 0u),
    E(SH_DBG_OPEN, SETTINGS_PAGE_DEBUG, SH_KIND_ACTION,
      "Open",
      NULL,
      "Open\n\nOpens the log folder in Explorer as it is typed in the box, "
      "before you Save, creating the folder first if it does not exist "
      "yet.",
      NULL,
      0, 0, 0u),
    E(SH_DBG_MAX_FILE, SETTINGS_PAGE_DEBUG, SH_KIND_SETTING,
      "Max file size (MB):",
      NULL,
      "Max file size (MB):\n\nWhen a log file reaches this size, it is "
      "closed and a new one started; from 1 to 100 MB. Smaller files are "
      "quicker for an AI or an editor to open; Keep at most, below, "
      "decides how many are kept.\nDefault: 10.",
      "10",
      1, 100, 0u),
    E(SH_DBG_MAX_TOTAL, SETTINGS_PAGE_DEBUG, SH_KIND_SETTING,
      "Keep at most (MB):",
      NULL,
      "Keep at most (MB):\n\nThe most the log folder may hold, from 5 to "
      "5,000 MB. When it is over, the oldest log files are deleted. The "
      "text beside the box shows how much the folder uses now.\nDefault: "
      "50.",
      "50",
      5, 5000, SH_F_THOUSANDS),
    E(SH_DBG_USAGE, SETTINGS_PAGE_DEBUG, SH_KIND_INFO,
      "Log folder usage",
      NULL,
      "Log folder usage\n\nHow much the log folder holds, in megabytes and "
      "files, measured when Settings opened. When it goes over Keep at "
      "most, the oldest log files are deleted to bring it back under.",
      NULL,
      0, 0, 0u),
    E(SH_DBG_CURRENT, SETTINGS_PAGE_DEBUG, SH_KIND_INFO,
      "Current log file:",
      NULL,
      "Current log file:\n\nThe file Nutshell is writing to now, or why "
      "there is none yet: no file is created until the first line is "
      "written. It is read-only; click into it to select and copy the "
      "path.",
      NULL,
      0, 0, 0u),
};

#undef E

const SettingsHelp *settings_help_get(int id)
{
    if (id < 0 || id >= (int)SH_COUNT) return NULL;
    return &k_help[id];
}

const SettingsHelp *settings_help_table(size_t *count)
{
    if (count != NULL) *count = (size_t)SH_COUNT;
    return k_help;
}
