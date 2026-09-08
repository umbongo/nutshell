/* src/core/chat_msg.h */
#ifndef NUTSHELL_CHAT_MSG_H
#define NUTSHELL_CHAT_MSG_H

#include <stddef.h>
#include "cmd_classify.h"

typedef enum {
    CHAT_ITEM_USER,
    CHAT_ITEM_AI_TEXT,
    CHAT_ITEM_COMMAND,
    CHAT_ITEM_STATUS,
    CHAT_ITEM_TOOL_CALL,    /* "using tool: <name>" status-style row */
    CHAT_ITEM_TOOL_RESULT   /* tool result (brief, collapsible) */
} ChatItemType;

typedef struct ChatMsgItem {
    ChatItemType type;
    int id;
    int measured_height;
    int dirty;
    char *text;
    size_t text_len;

    union {
        struct {
            char *thinking_text;
            int thinking_collapsed;
            float thinking_elapsed;
            int thinking_complete;
            int thinking_scroll_y;   /* internal scroll offset for expanded thinking */
            int thinking_autoscroll; /* 1 = auto-scroll to bottom during streaming */
            int thinking_user_set;   /* 1 = user has manually opened or closed this
                                       * item's Thinking disclosure; once set, the
                                       * WM_AI_STREAM handler in ai_chat.c must never
                                       * change thinking_collapsed on this item again. */
        } ai;
        struct {
            char *command;
            CmdSafetyLevel safety;
            int approved;       /* -1=pending, 0=denied, 1=approved */
            int blocked;
            int selected;       /* 1=tickbox checked for batch approve */
            int settled;        /* 1=finalized, rendered inline, not in active container */
            int batch;          /* CmdBatch id this command belongs to; 0 for
                                  * demo/legacy rows (see src/core/cmd_batch.h) */
            int container_scroll; /* per-container scroll offset, px -- only
                                    * meaningful on a container's first item
                                    * (see chat_listview.c's cmd_container_measure) */
        } cmd;
    } u;

    struct ChatMsgItem *next;
    struct ChatMsgItem *prev;
} ChatMsgItem;

typedef struct {
    ChatMsgItem *head;
    ChatMsgItem *tail;
    int count;
    int next_id;
} ChatMsgList;

/* Initialize a message list. */
void chat_msg_list_init(ChatMsgList *list);

/* Create and append an item. Returns the new item, or NULL on alloc failure.
 * text is copied (heap-allocated). */
ChatMsgItem *chat_msg_append(ChatMsgList *list, ChatItemType type, const char *text);

/* Remove an item from the list and free it. */
void chat_msg_remove(ChatMsgList *list, ChatMsgItem *item);

/* Free all items in the list. */
void chat_msg_list_clear(ChatMsgList *list);

/* Update item text (re-allocates). Marks item dirty. Returns 0 on success. */
int chat_msg_set_text(ChatMsgItem *item, const char *text);

/* Set command fields on a CHAT_ITEM_COMMAND item. command string is copied.
 * Does not touch u.cmd.batch -- callers tag the item's batch separately
 * with chat_msg_set_batch(), typically right after creating it. */
int chat_msg_set_command(ChatMsgItem *item, const char *command,
                         CmdSafetyLevel safety, int blocked);

/* Set the batch id on a CHAT_ITEM_COMMAND item (see cmd_batch.h). No-op on
 * a non-command item. */
void chat_msg_set_batch(ChatMsgItem *item, int batch);

/* Set thinking text on a CHAT_ITEM_AI_TEXT item. Copied to heap. */
int chat_msg_set_thinking(ChatMsgItem *item, const char *thinking_text);

/* Get item count. */
int chat_msg_count(const ChatMsgList *list);

/* Position of `item` among the list's unsettled CHAT_ITEM_COMMAND items
 * that share its batch id, in list order (0-based) -- matches the order
 * the batch's ApprovalQueue entries were added in, so q.entries[i] is the
 * i-th such item. Returns -1 if `item` is NULL, not a command, or already
 * settled. */
int chat_msg_batch_index(const ChatMsgList *list, const ChatMsgItem *item);

/* First unsettled CHAT_ITEM_COMMAND item in the list with u.cmd.batch ==
 * batch, in list order. NULL if none. */
ChatMsgItem *chat_msg_batch_first(const ChatMsgList *list, int batch);

/* Mark every unsettled CHAT_ITEM_COMMAND item with u.cmd.batch == batch as
 * settled (u.cmd.settled = 1) and dirty (for remeasure). Returns the
 * number of items settled. */
int chat_msg_batch_settle(ChatMsgList *list, int batch);

#endif /* NUTSHELL_CHAT_MSG_H */
