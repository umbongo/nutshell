/* src/core/chat_turn.c -- see chat_turn.h for the rule. */
#include "chat_turn.h"
#include "voice_speak.h"
#include "secure_zero.h"
#include <stdlib.h>
#include <string.h>

int chat_turn_is_text(const ChatMsgItem *it)
{
    if (!it || it->type != CHAT_ITEM_AI_TEXT || !it->text) return 0;
    for (const unsigned char *p = (const unsigned char *)it->text; *p; p++)
        if (*p > ' ') return 1;
    return 0;
}

/* Is this command row keeping its turn open? */
static int cmd_open(const ChatMsgItem *it)
{
    return (!it->u.cmd.settled && !it->u.cmd.quoted)
        || it->u.cmd.run == CHAT_RUN_RUNNING;
}

/* First row of the turn that holds `it`. */
static const ChatMsgItem *turn_start(const ChatMsgItem *it)
{
    while (it->prev && it->prev->type != CHAT_ITEM_USER) it = it->prev;
    return it;
}

/* Scan the turn from `start` (a USER row there is skipped). Returns the last
 * text row, sets *open, and sets *end to the row after the turn (the next
 * USER row, or NULL at the end of the list). */
static const ChatMsgItem *turn_scan(const ChatMsgItem *start, int tail_busy,
                                    int *open, const ChatMsgItem **end)
{
    const ChatMsgItem *last = NULL;
    const ChatMsgItem *it = start;
    *open = 0;
    if (it && it->type == CHAT_ITEM_USER) it = it->next;
    for (; it && it->type != CHAT_ITEM_USER; it = it->next) {
        if (chat_turn_is_text(it)) last = it;
        else if (it->type == CHAT_ITEM_COMMAND && cmd_open(it)) *open = 1;
        if (!it->next && tail_busy) *open = 1;
    }
    if (end) *end = it;
    return last;
}

static const ChatMsgItem *find_row(const ChatMsgList *list, int id)
{
    for (const ChatMsgItem *it = list->head; it; it = it->next)
        if (it->id == id) return it;
    return NULL;
}

int chat_turn_carries_button(const ChatMsgList *list, int item_id, int tail_busy)
{
    if (!list) return 0;
    const ChatMsgItem *row = find_row(list, item_id);
    if (!row || !chat_turn_is_text(row)) return 0;
    int open;
    const ChatMsgItem *last = turn_scan(turn_start(row), tail_busy, &open, NULL);
    return last == row && !open;
}

int chat_turn_refresh(ChatMsgList *list, int on, int tail_busy)
{
    if (!list) return 0;
    int changed = 0;
    ChatMsgItem *it = list->head;
    while (it) {
        /* One turn: [it, end) where end is the next USER row. */
        ChatMsgItem *first = it;
        int open;
        const ChatMsgItem *end;
        const ChatMsgItem *last = turn_scan(first, tail_busy, &open, &end);
        ChatMsgItem *r = first;
        for (; r != end; r = r->next) {
            if (r->type != CHAT_ITEM_AI_TEXT) continue;
            int want = on && !open && r == last;
            if (r->u.ai.turn_btn != want) {
                r->u.ai.turn_btn = want;
                r->dirty = 1;
                changed++;
            }
        }
        /* always advance, whatever the scan reported */
        it = (r == first) ? first->next : r;
    }
    return changed;
}

char *chat_turn_text(const ChatMsgList *list, int item_id, int tail_busy)
{
    if (!chat_turn_carries_button(list, item_id, tail_busy)) return NULL;
    const ChatMsgItem *row = find_row(list, item_id);
    char *out = malloc(1);
    if (!out) return NULL;
    out[0] = '\0';
    size_t len = 0;
    for (const ChatMsgItem *it = turn_start(row); ; it = it->next) {
        if (chat_turn_is_text(it)) {
            char *piece = voice_speak_text(it->text);
            if (!piece) { secure_zero(out, len + 1); free(out); return NULL; }
            size_t pl = strlen(piece);
            while (pl && (unsigned char)piece[pl - 1] <= ' ') pl--;
            if (pl) {
                /* sentence break before the piece, and the room for it */
                char *grown = malloc(len + pl + 3);
                if (!grown) {
                    secure_zero(piece, strlen(piece));
                    free(piece);
                    secure_zero(out, len + 1);
                    free(out);
                    return NULL;
                }
                memcpy(grown, out, len);
                if (len) {
                    char e = grown[len - 1];
                    if (e == '.' || e == '!' || e == '?') {
                        grown[len++] = ' ';
                    } else if (e == ':' || e == ';' || e == ',') {
                        grown[len - 1] = '.';
                        grown[len++] = ' ';
                    } else {
                        grown[len++] = '.';
                        grown[len++] = ' ';
                    }
                }
                memcpy(grown + len, piece, pl);
                len += pl;
                grown[len] = '\0';
                secure_zero(out, strlen(out));
                free(out);
                out = grown;
            }
            secure_zero(piece, strlen(piece));
            free(piece);
        }
        if (it == row) break;
    }
    return out;
}
