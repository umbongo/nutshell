/* src/core/chat_msg.h */
#ifndef NUTSHELL_CHAT_MSG_H
#define NUTSHELL_CHAT_MSG_H

#include <stddef.h>
#include "cmd_classify.h"

typedef enum {
    CHAT_ITEM_USER,
    CHAT_ITEM_AI_TEXT,
    CHAT_ITEM_COMMAND,
    CHAT_ITEM_STATUS
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
        } ai;
        struct {
            char *command;
            CmdSafetyLevel safety;
            int approved;       /* -1=pending, 0=denied, 1=approved */
            int blocked;
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

/* Set command fields on a CHAT_ITEM_COMMAND item. command string is copied. */
int chat_msg_set_command(ChatMsgItem *item, const char *command,
                         CmdSafetyLevel safety, int blocked);

/* Set thinking text on a CHAT_ITEM_AI_TEXT item. Copied to heap. */
int chat_msg_set_thinking(ChatMsgItem *item, const char *thinking_text);

/* Get item count. */
int chat_msg_count(const ChatMsgList *list);

#endif /* NUTSHELL_CHAT_MSG_H */
