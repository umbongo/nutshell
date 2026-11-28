#ifndef NUTSHELL_MAIN_CHORDS_H
#define NUTSHELL_MAIN_CHORDS_H

/*
 * main_chords -- which key-downs are the main window's own chords (the ones
 * the main window handles before the key encoder, and that the AI panel
 * forwards to it), and the small decisions behind the tab and zoom chords.
 * Portable, no windows.h: src/ui/window.c, ai_chat_input.c and
 * chat_listview.c ask it; tests/test_main_chords.c covers it.
 *
 * Virtual-key codes are written as numbers so this file needs no windows.h;
 * they equal Windows' VK_* values.
 */

#define MCHORD_VK_TAB   0x09
#define MCHORD_VK_SPACE 0x20
#define MCHORD_VK_F11   0x7A

typedef enum {
    MCHORD_NONE = 0,
    MCHORD_FULLSCREEN,   /* F11 */
    MCHORD_NEW_TAB,      /* Ctrl+Shift+T */
    MCHORD_CLOSE_TAB,    /* Ctrl+Shift+W */
    MCHORD_AI_PANEL,     /* Ctrl+Shift+Space, only while the assistant is on */
    MCHORD_MARK_ISSUE,   /* Ctrl+Shift+M */
    MCHORD_TAB_NEXT,     /* Ctrl+Tab */
    MCHORD_TAB_PREV,     /* Ctrl+Shift+Tab */
    MCHORD_ZOOM_RESET    /* Ctrl+0 */
} MainChord;

/* The chord a key-down is, given the key's virtual-key code and the state of
 * Shift, Ctrl and Alt (non-zero = down); MCHORD_NONE when it is none. Ctrl
 * chords need Alt up (AltGr reports Ctrl and Alt together). ai_on is whether
 * the AI assistant is enabled: while off, Ctrl+Shift+Space is no chord. */
MainChord main_chord_classify(unsigned vk, int shift, int ctrl, int alt, int ai_on);

/* 1 when a key-down that is chord `c`, seen by the AI panel's input box or
 * conversation list, is forwarded to the main window. Zoom reset is not: the
 * panel handles it itself, as it does Ctrl+= and Ctrl+-. 0 for MCHORD_NONE. */
int main_chord_forwarded_from_panel(MainChord c);

/* The tab index a tab chord moves to, with wrap-around at the ends. `delta`
 * is +1 or -1. Returns -1 when nothing moves: fewer than two tabs, or a
 * delta that is not +-1. A `current` outside 0..count-1 goes to the first tab
 * (delta > 0) or the last (delta < 0). */
int tab_chord_target(int count, int current, int delta);

/* The size Ctrl+0 restores: the terminal font (ai_panel 0) or the AI
 * panel's chat font (ai_panel non-zero). Both are entries of the zoom table. */
int zoom_reset_size(int ai_panel);

#endif /* NUTSHELL_MAIN_CHORDS_H */
