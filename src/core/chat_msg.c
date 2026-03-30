/* src/core/chat_msg.c */
#include "chat_msg.h"
#include "secure_zero.h"
#include <stdlib.h>
#include <string.h>

#define CMD_MAX_LEN 1023

void chat_msg_list_init(ChatMsgList *list)
{
    list->head = NULL;
    list->tail = NULL;
    list->count = 0;
    list->next_id = 1;
}

ChatMsgItem *chat_msg_append(ChatMsgList *list, ChatItemType type, const char *text)
{
    ChatMsgItem *item = calloc(1, sizeof(*item));
    if (!item) return NULL;

    item->type = type;
    item->id = list->next_id++;
    item->dirty = 1;
    item->measured_height = 0;

    /* Copy text (treat NULL as empty) */
    const char *src = text ? text : "";
    size_t len = strlen(src);
    item->text = malloc(len + 1);
    if (!item->text) { free(item); return NULL; }
    memcpy(item->text, src, len + 1);
    item->text_len = len;

    /* Type-specific defaults */
    if (type == CHAT_ITEM_AI_TEXT) {
        item->u.ai.thinking_text = NULL;
        item->u.ai.thinking_collapsed = 1;
        item->u.ai.thinking_elapsed = 0.0f;
        item->u.ai.thinking_complete = 0;
        item->u.ai.thinking_scroll_y = 0;
        item->u.ai.thinking_autoscroll = 1;
    } else if (type == CHAT_ITEM_COMMAND) {
        item->u.cmd.command = NULL;
        item->u.cmd.safety = CMD_SAFE;
        item->u.cmd.approved = -1;
        item->u.cmd.blocked = 0;
        item->u.cmd.settled = 0;
    }

    /* Link into list */
    item->prev = list->tail;
    item->next = NULL;
    if (list->tail)
        list->tail->next = item;
    else
        list->head = item;
    list->tail = item;
    list->count++;

    return item;
}

static void free_item(ChatMsgItem *item)
{
    if (!item) return;
    /* Zero sensitive content before free (may contain API reasoning, passwords) */
    if (item->text) {
        secure_zero(item->text, item->text_len);
        free(item->text);
    }
    if (item->type == CHAT_ITEM_AI_TEXT && item->u.ai.thinking_text) {
        size_t tlen = strlen(item->u.ai.thinking_text);
        secure_zero(item->u.ai.thinking_text, tlen);
        free(item->u.ai.thinking_text);
    }
    if (item->type == CHAT_ITEM_COMMAND && item->u.cmd.command) {
        size_t clen = strlen(item->u.cmd.command);
        secure_zero(item->u.cmd.command, clen);
        free(item->u.cmd.command);
    }
    free(item);
}

void chat_msg_remove(ChatMsgList *list, ChatMsgItem *item)
{
    if (!list || !item) return;
    if (item->prev)
        item->prev->next = item->next;
    else
        list->head = item->next;
    if (item->next)
        item->next->prev = item->prev;
    else
        list->tail = item->prev;
    list->count--;
    free_item(item);
}

void chat_msg_list_clear(ChatMsgList *list)
{
    ChatMsgItem *cur = list->head;
    while (cur) {
        ChatMsgItem *next = cur->next;
        free_item(cur);
        cur = next;
    }
    list->head = NULL;
    list->tail = NULL;
    list->count = 0;
}

int chat_msg_set_text(ChatMsgItem *item, const char *text)
{
    if (!item) return -1;
    const char *src = text ? text : "";
    size_t len = strlen(src);
    char *new_text = malloc(len + 1);
    if (!new_text) return -1;
    memcpy(new_text, src, len + 1);
    if (item->text) {
        memset(item->text, 0, item->text_len);
        free(item->text);
    }
    item->text = new_text;
    item->text_len = len;
    item->dirty = 1;
    return 0;
}

int chat_msg_set_command(ChatMsgItem *item, const char *command,
                         CmdSafetyLevel safety, int blocked)
{
    if (!item || item->type != CHAT_ITEM_COMMAND) return -1;
    if (!command) return -1;
    size_t len = strlen(command);
    if (len > CMD_MAX_LEN) return -1; /* reject, don't truncate */

    char *new_cmd = malloc(len + 1);
    if (!new_cmd) return -1;
    memcpy(new_cmd, command, len + 1);

    if (item->u.cmd.command) {
        size_t old_len = strlen(item->u.cmd.command);
        memset(item->u.cmd.command, 0, old_len);
        free(item->u.cmd.command);
    }
    item->u.cmd.command = new_cmd;
    item->u.cmd.safety = safety;
    item->u.cmd.approved = -1;
    item->u.cmd.blocked = blocked;
    item->dirty = 1;
    return 0;
}

int chat_msg_set_thinking(ChatMsgItem *item, const char *thinking_text)
{
    if (!item || item->type != CHAT_ITEM_AI_TEXT) return -1;
    const char *src = thinking_text ? thinking_text : "";
    size_t len = strlen(src);
    char *new_text = malloc(len + 1);
    if (!new_text) return -1;
    memcpy(new_text, src, len + 1);

    if (item->u.ai.thinking_text) {
        size_t old_len = strlen(item->u.ai.thinking_text);
        memset(item->u.ai.thinking_text, 0, old_len);
        free(item->u.ai.thinking_text);
    }
    item->u.ai.thinking_text = new_text;
    item->dirty = 1;
    return 0;
}

int chat_msg_count(const ChatMsgList *list)
{
    return list ? list->count : 0;
}
