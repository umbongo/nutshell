#ifndef NUTSHELL_CHAT_CURSOR_H
#define NUTSHELL_CHAT_CURSOR_H

/* Which mouse cursor the AI chat list shows (spec
 * 2026-10-18-ai-panel-text-selection-design.md section 8). Pure: the list
 * view (src/ui/chat_listview.c) hit-tests the pointer, names what it found,
 * and maps the answer to a system cursor. */

/* What is under the pointer. */
typedef enum {
    CHAT_HIT_NONE = 0,   /* a margin, a gap between items, a non-selectable
                          * row, nothing */
    CHAT_HIT_TEXT,       /* a run of selectable text a drag would select */
    CHAT_HIT_ACTION,     /* a button, link, chip, tickbox, copy glyph or the
                          * Thinking header: a click does something */
    CHAT_HIT_KIND_COUNT
} ChatHitKind;

typedef enum {
    CHAT_CURSOR_ARROW = 0,
    CHAT_CURSOR_HAND,
    CHAT_CURSOR_IBEAM
} ChatCursorKind;

/* The cursor for `kind`. `dragging` is non-zero while a text selection drag
 * holds the mouse: the I-beam stays wherever the pointer goes. An action wins
 * over text it overlaps (the caller passes CHAT_HIT_ACTION then). An
 * out-of-range kind gets the arrow. */
ChatCursorKind chat_cursor_for_hit(ChatHitKind kind, int dragging);

#endif /* NUTSHELL_CHAT_CURSOR_H */
