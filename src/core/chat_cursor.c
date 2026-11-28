#include "chat_cursor.h"

ChatCursorKind chat_cursor_for_hit(ChatHitKind kind, int dragging)
{
    if (dragging) return CHAT_CURSOR_IBEAM;
    switch (kind) {
    case CHAT_HIT_TEXT:   return CHAT_CURSOR_IBEAM;
    case CHAT_HIT_ACTION: return CHAT_CURSOR_HAND;
    case CHAT_HIT_NONE:
    default:              return CHAT_CURSOR_ARROW;
    }
}
