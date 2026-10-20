#include "shortcuts.h"
#include "resource.h"   /* IDM_* -- -Isrc/ui is on both build paths */
#include <string.h>

#define S1 "Sessions and tabs"
#define S2 "Copy, paste and selection"
#define S3 "Scrolling and zoom"
#define S4 "Keys the terminal sends to the shell"
#define S5 "AI Assist panel"
#define S6 "Window and help"
#define S7 "Mouse"

#define NO_SESSION "with no live session: the menu"

static const Shortcut TABLE[] = {
    /* 1. Sessions and tabs */
    { S1, "Ctrl+Shift+T", "New session (opens the Session Manager)",
      "from the terminal, not the AI panel", SC_MENU_ACCEL, IDM_FILE_NEW_SESSION },
    { S1, "Ctrl+Shift+W", "Close the active tab",
      "from the terminal", SC_MENU_ACCEL, IDM_FILE_CLOSE_TAB },
    { S1, "Alt+F4",       "Exit", "", 0u, 0 },
    { S1, "Alt",          "Open the menu bar", "", 0u, 0 },
    { S1, "Alt+Space",    "Open the system menu", "", 0u, 0 },

    /* 2. Copy, paste and selection */
    { S2, "Ctrl+Shift+C", "Copy the selection", "",
      SC_SELECTION_SPEC | SC_MENU_ACCEL, IDM_EDIT_COPY },
    { S2, "Ctrl+C",       "Interrupt (^C); never copies", "",
      SC_SELECTION_SPEC, 0 },
    { S2, "Ctrl+V",       "Paste",
      "a confirmation first when Settings says so; Enter confirms",
      SC_SELECTION_SPEC | SC_MENU_ACCEL, IDM_EDIT_PASTE },
    { S2, "Ctrl+Shift+V, Shift+Insert", "Paste, as Ctrl+V", "", 0u, 0 },
    { S2, "Edit > Select All", "Select everything held and copy it",
      "Ctrl+A goes to the shell", SC_MENU_PATH | SC_SELECTION_SPEC, 0 },

    /* 3. Scrolling and zoom */
    { S3, "Shift+PgUp", "History up a page", "", 0u, 0 },
    { S3, "Shift+PgDn", "History down a page", "", 0u, 0 },
    { S3, "PgUp",       "History up a page (sent to vim, less)", "", 0u, 0 },
    { S3, "PgDn",       "History down a page (sent to vim, less)", "", 0u, 0 },
    { S3, "Ctrl+PgUp, Ctrl+PgDn", "Sent to the program, always", "", 0u, 0 },
    { S3, "Ctrl+=",     "Zoom the terminal font in", "US layout", 0u, 0 },
    { S3, "Ctrl+-",     "Zoom the terminal font out", "US layout", 0u, 0 },

    /* 4. Keys the terminal sends to the shell */
    { S4, "Ctrl+Space",      "NUL (0x00)", "", 0u, 0 },
    { S4, "Shift+Tab",       "Back-tab", "", 0u, 0 },
    { S4, "Ctrl+Arrows, Ctrl+Home, Ctrl+End", "Sent with the modifier", "", 0u, 0 },
    { S4, "Alt+key",         "ESC, then the key", NO_SESSION, 0u, 0 },
    { S4, "Ctrl+Alt+letter", "ESC, then the control byte",
      "unless the layout makes it a character", 0u, 0 },
    { S4, "F10, Shift+F10",  "Sent to the shell, not the menu", NO_SESSION, 0u, 0 },
    { S4, "Backspace",       "BS over SSH, DEL in a local shell", "", 0u, 0 },
    { S4, "Edit > Send Key", "Send the next key raw, or with Alt", "",
      SC_MENU_PATH, 0 },

    /* 5. AI Assist panel */
    { S5, "Ctrl+Shift+Space", "Show or hide the panel",
      "from the terminal, not from inside the panel", SC_MENU_ACCEL, IDM_VIEW_AI_CHAT },
    { S5, "Enter",       "Send",
      "while a reply streams or commands run: stop them", 0u, 0 },
    { S5, "Shift+Enter", "New line", "", 0u, 0 },
    { S5, "Ctrl+V",      "Paste text, or attach an image from the clipboard", "", 0u, 0 },
    { S5, "Ctrl+=",      "Zoom the chat in", "US layout", 0u, 0 },
    { S5, "Ctrl+-",      "Zoom the chat out", "US layout", 0u, 0 },
    { S5, "Ctrl+C",      "Copy the selected messages", "conversation list focused", 0u, 0 },
    { S5, "Ctrl+A",      "Select all messages", "list focused", 0u, 0 },
    { S5, "Esc",         "Clear the selection", "list focused", 0u, 0 },
    { S5, "PgUp, PgDn, Home, End, Up, Down", "Scroll the conversation",
      "list focused", 0u, 0 },

    /* 6. Window and help */
    { S6, "F11",          "Fullscreen", "from the terminal",
      SC_MENU_ACCEL, IDM_VIEW_FULLSCREEN },
    { S6, "Ctrl+Shift+M", "Mark an issue in the debug log", "from the terminal",
      SC_MENU_ACCEL, IDM_HELP_MARK_ISSUE },
    { S6, "Esc, Enter",   "Close a dialog", "most dialogs", 0u, 0 },
    { S6, "Help > Keyboard Shortcuts", "This sheet", "", SC_MENU_PATH, 0 },

    /* 7. Mouse */
    { S7, "Drag",               "Select and copy", "", SC_SELECTION_SPEC, 0 },
    { S7, "Double-click",       "Select a word", "", SC_SELECTION_SPEC, 0 },
    { S7, "Triple-click",       "Select a line", "", SC_SELECTION_SPEC, 0 },
    { S7, "Drag past the edge", "Scroll while selecting", "", SC_SELECTION_SPEC, 0 },
    { S7, "Right-click",        "Paste, as Ctrl+V", "", 0u, 0 },
    { S7, "Wheel",              "Scroll three lines", "", 0u, 0 },
    { S7, "Ctrl+Wheel",         "Zoom the terminal font", "", 0u, 0 },
    { S7, "Drag in the AI panel", "Select messages",
      "nothing copied until Ctrl+C", 0u, 0 },
    { S7, "Double-click in the AI panel", "Select a word", "", 0u, 0 },
    { S7, "Triple-click in the AI panel", "Select a line", "", 0u, 0 },
    { S7, "Right-click in the AI panel", "Copy, Select all, Paste into message",
      "", 0u, 0 },
};

#define TABLE_N (sizeof(TABLE) / sizeof(TABLE[0]))

const Shortcut *shortcuts_table(size_t *count)
{
    if (count) *count = TABLE_N;
    return TABLE;
}

size_t shortcuts_sections(const char **out, size_t cap)
{
    size_t n = 0;
    for (size_t i = 0; i < TABLE_N; i++) {
        if (i > 0 && strcmp(TABLE[i].section, TABLE[i - 1].section) == 0)
            continue;
        if (out && n < cap) out[n] = TABLE[i].section;
        n++;
    }
    return n;
}

const char *shortcuts_menu_accel(int menu_id)
{
    if (menu_id == 0) return "";
    for (size_t i = 0; i < TABLE_N; i++) {
        if (TABLE[i].menu_id == menu_id) return TABLE[i].chord;
    }
    return "";
}

/* snprintf-style appender: counts every byte, writes what fits. */
typedef struct {
    char  *buf;
    size_t cap;
    size_t len;
} Out;

static void put(Out *o, const char *s)
{
    size_t n = strlen(s);
    if (o->buf && o->cap > 0 && o->len < o->cap - 1) {
        size_t room = o->cap - 1 - o->len;
        size_t k = n < room ? n : room;
        memcpy(o->buf + o->len, s, k);
        o->buf[o->len + k] = '\0';
    }
    o->len += n;
}

size_t shortcuts_readme_block(char *buf, size_t cap)
{
    Out o = { buf, cap, 0 };
    if (buf && cap > 0) buf[0] = '\0';

    put(&o, "\n");
    for (size_t i = 0; i < TABLE_N; i++) {
        const Shortcut *r = &TABLE[i];
        if (i == 0 || strcmp(r->section, TABLE[i - 1].section) != 0) {
            put(&o, "\n#### ");
            put(&o, r->section);
            put(&o, "\n\n| Shortcut | Action | Note |\n| --- | --- | --- |\n");
        }
        put(&o, "| **");
        put(&o, r->chord);
        put(&o, "** | ");
        put(&o, r->action);
        put(&o, " |");
        if (r->condition[0]) {
            put(&o, " ");
            put(&o, r->condition);
            put(&o, " |");
        } else {
            put(&o, " |");
        }
        put(&o, "\n");
    }
    put(&o, "\n");
    return o.len;
}
