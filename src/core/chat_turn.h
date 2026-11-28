#ifndef NUTSHELL_CHAT_TURN_H
#define NUTSHELL_CHAT_TURN_H

/*
 * chat_turn -- which row of the AI chat carries a turn's Read aloud button,
 * and the turn's speakable text. Pure; spec: docs/superpowers/specs/
 * 2026-11-28-voice-read-aloud-design.md section 2.
 *
 * A turn is the run of rows after a CHAT_ITEM_USER row (or from the list
 * head) up to the next CHAT_ITEM_USER row or the end. Status, tool and
 * command rows belong to the turn they sit in; they never end it.
 *
 * Assistant text (chat_turn_is_text): a CHAT_ITEM_AI_TEXT row whose text
 * holds a byte above ' '. A row with only thinking is not text.
 *
 * A turn is open when it is the list's last turn and tail_busy is 1, or when
 * any CHAT_ITEM_COMMAND row in it has (!u.cmd.settled && !u.cmd.quoted) or
 * u.cmd.run == CHAT_RUN_RUNNING.
 *
 * A row carries the button when it is the last assistant-text row of a turn
 * that is not open. tail_busy is the panel's busy flag.
 */

#include "chat_msg.h"

/* 1 when `it` is a CHAT_ITEM_AI_TEXT row with a byte above ' ' in its text. */
int chat_turn_is_text(const ChatMsgItem *it);

/* 1 when the row item_id carries its turn's button under tail_busy. */
int chat_turn_carries_button(const ChatMsgList *list, int item_id, int tail_busy);

/* Recompute u.ai.turn_btn on every CHAT_ITEM_AI_TEXT row (on == 0 clears
 * every flag), mark each row whose flag changed dirty, and return how many
 * changed. One pass over the list. */
int chat_turn_refresh(ChatMsgList *list, int on, int tail_busy);

/* The speakable text of the turn whose button row is item_id: voice_speak_text()
 * of each assistant-text row of the turn up to and including that row, each
 * piece trimmed of trailing whitespace, empty pieces skipped, joined by a
 * sentence break (". " after a word, " " after . ! ?, a trailing : ; , becomes
 * "."). NULL when item_id carries no button under tail_busy, or on out of
 * memory; "" when nothing is speakable. The caller secure_zero()s and frees. */
char *chat_turn_text(const ChatMsgList *list, int item_id, int tail_busy);

#endif
