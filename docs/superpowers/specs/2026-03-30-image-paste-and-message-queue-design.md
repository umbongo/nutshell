# Image Paste + Message Queue

**Date**: 2026-03-30
**Status**: Approved

## Problem

1. Users cannot paste images into the AI chat input. Screenshots and diagrams are a natural way to ask "what's this?" or "fix this error" but the input only accepts text.
2. When AI is processing, the send button becomes a cancel button. Users who want to queue a follow-up message must wait for the response to finish.

## Solution

### Feature 1: Image Paste

Intercept `WM_PASTE` in `InputSubclassProc`. When the clipboard contains a bitmap (`CF_DIB`), convert it to PNG via GDI+ (already linked), base64-encode it, and store it as an attachment on `AiChatData`. Show "[Image attached]" in the input. When sending, build the OpenAI multimodal content array format instead of a plain string.

### Feature 2: Message Queue

When the user presses Enter while AI is busy, store the message (text + optional attachment) in a single queue slot. Show it in chat as a user bubble with a "[Queued — Cancel]" label. Clicking Cancel removes it and restores the text to the input. When the current stream completes, auto-send the queued message.

---

## Detailed Design

### 1. New Structures

**In `src/core/ai_prompt.h`:**

```c
/* Attached image for multimodal messages */
typedef struct {
    char *base64_url;    /* "data:image/png;base64,..." (heap) */
    int width, height;   /* original pixel dimensions */
} AiAttachment;
```

Bump `AI_BODY_MAX` from 65536 to 524288 (512KB) to accommodate base64-encoded images.

**In `src/core/chat_msg.h`:**

Add a `queued` flag to `ChatMsgItem`:
```c
struct {
    /* (existing user fields, currently empty union arm) */
} user;  /* — but user items have no union arm yet, so: */
```
Actually, user items currently use no union arm. Add a `queued` field directly to `ChatMsgItem` (not in the union) since it's a display-only flag:
```c
int queued;  /* 1 = pending in queue, shown with [Cancel] label */
```

**In `AiChatData` (src/ui/ai_chat.c):**

```c
/* Pending image attachment for the next message */
AiAttachment *pending_attachment;

/* Message queue (single slot) */
struct {
    char text[AI_MSG_MAX];
    AiAttachment *attachment;
    ChatMsgItem *chat_item;   /* the user bubble in the list */
    int active;               /* 1 = a message is queued */
} queued_msg;
```

### 2. Base64 Encoding

The existing `b64_encode` in `src/crypto/crypto.c` is `static`. Rather than exposing crypto internals, add a new utility file:

**New file: `src/core/base64.h` / `src/core/base64.c`**

Minimal public API:
```c
size_t base64_encode(const unsigned char *src, size_t src_len,
                     char *dst, size_t dst_size);
```

Implementation uses the same OpenSSL `EVP_EncodeBlock` that crypto.c uses. This keeps crypto module encapsulated.

### 3. Image Clipboard Extraction

**In `InputSubclassProc` (src/ui/ai_chat.c):**

Add `WM_PASTE` handler before the `DefSubclassProc` fallthrough:

1. Call `IsClipboardFormatAvailable(CF_DIB)`
2. If true: `OpenClipboard`, `GetClipboardData(CF_DIB)`, extract the `BITMAPINFOHEADER` + pixel data
3. Create GDI+ `Bitmap` from the DIB via `GdipCreateBitmapFromGdiDib`
4. Save to an `IStream` in PNG format using `GdipSaveImageToStream` with the PNG encoder CLSID
5. Read the stream bytes, base64-encode them
6. Build `"data:image/png;base64,<encoded>"` string and store in `d->pending_attachment`
7. Insert "[Image attached] " text at cursor in the input via `EM_REPLACESEL`. This placeholder is cosmetic only — on send, the actual text typed by the user is sent as the text content, and the "[Image attached]" prefix is stripped
8. `CloseClipboard`, return 0 (consumed)
9. If no `CF_DIB`: fall through to `DefSubclassProc` for normal text paste

**GDI+ initialization**: Call `GdiplusStartup` once during AI chat window creation (`WM_CREATE`) and `GdiplusShutdown` on `WM_DESTROY`. Store the token in `AiChatData`.

### 4. Multimodal JSON Building

**In `src/core/ai_prompt.c`:**

Modify `ai_build_request_body_ex` to accept an optional attachment for the last user message. Add a new parameter:

```c
size_t ai_build_request_body_ex(const AiConversation *conv,
                                const AiAttachment *attachment,
                                char *buf, size_t buf_size, int stream);
```

When `attachment` is non-NULL and the last message is a user message, write its content as an array instead of a plain string:

```json
{"role":"user","content":[
  {"type":"text","text":"user's message"},
  {"type":"image_url","image_url":{"url":"data:image/png;base64,..."}}
]}
```

When `attachment` is NULL, the existing plain string format is used unchanged.

All callers of `ai_build_request_body_ex` pass NULL for the new parameter unless they have an attachment.

### 5. Conversation History with Images

The `AiMessage.content` field is a fixed `char[AI_MSG_MAX]` (16KB) which cannot hold a base64 image. For conversation history:

- Store only the text portion in `AiMessage.content` (as today)
- Add an `AiAttachment *attachment` pointer to `AiMessage`
- The attachment is heap-allocated and freed when the conversation is cleared
- When rebuilding the request body, check each message for an attachment

```c
typedef struct {
    AiRole role;
    char content[AI_MSG_MAX];
    AiAttachment *attachment;  /* NULL for text-only messages */
} AiMessage;
```

### 6. Message Queue Flow

**Sending while busy (in `send_user_message` or `IDC_CHAT_SEND` handler):**

Currently `send_user_message` returns early if `d->active_state->busy`. Change:

1. If busy AND no message already queued: store text + attachment in `queued_msg`, set `active = 1`
2. Show user bubble in chat with `item->queued = 1`
3. Clear input, clear `pending_attachment`
4. If busy AND message already queued: remove old queued bubble, replace with new one

**Auto-send on completion (in `WM_AI_RESPONSE` handler):**

After the current response is fully processed (after command extraction and approval setup), check `d->queued_msg.active`. If set:

1. Copy `queued_msg.text` and `queued_msg.attachment` to local variables
2. Clear `queued_msg.active`, set `queued_msg.chat_item->queued = 0`
3. Call the normal send flow (add to conversation, launch stream thread)

**Cancel (click handler in chat_listview.c):**

In `on_lbuttondown`, when a `CHAT_ITEM_USER` with `queued == 1` is clicked in the [Cancel] label area:

1. Post `WM_COMMAND` with a new ID (e.g., `IDC_QUEUE_CANCEL`) to the parent
2. Parent handler: restore text to input, free attachment, remove bubble from chat, clear `queued_msg`

### 7. Rendering the Queued State

**In `paint_user_item` (chat_listview.c):**

After drawing the user text, if `item->queued`:

1. Draw a small "[Queued — Cancel]" label below the bubble text in a muted color
2. "Cancel" portion rendered in the link color (`CLR_LINK_TEXT`) to indicate it's clickable

**In `measure_item`:** Add extra height for the label when `item->queued == 1`.

**In `on_lbuttondown`:** Add hit-test for the Cancel link within queued user items.

---

## Files Modified/Created

| File | Change |
|------|--------|
| `src/core/ai_prompt.h` | Add `AiAttachment` struct, attachment field on `AiMessage`, bump `AI_BODY_MAX` |
| `src/core/ai_prompt.c` | Multimodal JSON builder, attachment parameter, free helpers |
| `src/core/chat_msg.h` | Add `queued` flag to `ChatMsgItem` |
| `src/core/chat_msg.c` | Clear `queued` flag in append/remove |
| `src/core/base64.h` | New: `base64_encode` declaration |
| `src/core/base64.c` | New: `base64_encode` using OpenSSL EVP |
| `src/ui/ai_chat.c` | WM_PASTE image handling, GDI+ init/shutdown, queue logic, attachment storage, cancel handler, modified send flow |
| `src/ui/chat_listview.c` | Render [Queued — Cancel] on user bubbles, hit-test for Cancel click |
| `Makefile` | Add `src/core/base64.o` to build |
| `tests/test_base64.c` | New: tests for base64 encoding |
| `tests/runner.c` | Register base64 tests |

## What stays the same

- Text-only messages use the existing plain string content format
- Providers that don't support vision will receive the image in the standard OpenAI format — the API will either ignore it or return an error, which is shown as a status message
- Existing clipboard text paste behavior unchanged (image paste only triggers on CF_DIB)
- Cancel button for active streaming unchanged
