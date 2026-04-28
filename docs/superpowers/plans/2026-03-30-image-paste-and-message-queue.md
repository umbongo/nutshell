# Image Paste + Message Queue — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Allow users to paste images into AI chat (sent as base64 in OpenAI multimodal format) and queue follow-up messages while AI is processing.

**Architecture:** Intercept WM_PASTE for CF_DIB clipboard data, convert to PNG via GDI+ flat API, base64-encode, store as attachment. Modify JSON builder to emit content arrays for image messages. Add single-slot queue with cancel support.

**Tech Stack:** C, Win32 GDI+, OpenSSL EVP (base64), MinGW cross-compile

**Spec:** `docs/superpowers/specs/2026-03-30-image-paste-and-message-queue-design.md`

---

### Task 1: Create base64 encoding module

**Files:**
- Create: `src/core/base64.h`
- Create: `src/core/base64.c`
- Create: `tests/test_base64.c`
- Modify: `tests/runner.c`
- Modify: `Makefile`

- [ ] **Step 1: Write tests for base64 encoding**

In `tests/test_base64.c`:

```c
/* tests/test_base64.c */
#include "test_framework.h"
#include "base64.h"
#include <string.h>

static void test_base64_empty(void)
{
    TEST_BEGIN("base64 encode empty input");
    char out[8];
    size_t n = base64_encode((const unsigned char *)"", 0, out, sizeof(out));
    ASSERT_EQ((int)n, 0);
    TEST_END();
}

static void test_base64_hello(void)
{
    TEST_BEGIN("base64 encode 'Hello'");
    char out[16];
    size_t n = base64_encode((const unsigned char *)"Hello", 5, out, sizeof(out));
    ASSERT_TRUE(n > 0);
    ASSERT_STR_EQ(out, "SGVsbG8=");
    TEST_END();
}

static void test_base64_padding(void)
{
    TEST_BEGIN("base64 encode with different padding");
    char out[16];
    /* 1 byte → 4 chars with == padding */
    size_t n = base64_encode((const unsigned char *)"A", 1, out, sizeof(out));
    ASSERT_TRUE(n > 0);
    ASSERT_STR_EQ(out, "QQ==");
    /* 2 bytes → 4 chars with = padding */
    n = base64_encode((const unsigned char *)"AB", 2, out, sizeof(out));
    ASSERT_TRUE(n > 0);
    ASSERT_STR_EQ(out, "QUI=");
    /* 3 bytes → 4 chars, no padding */
    n = base64_encode((const unsigned char *)"ABC", 3, out, sizeof(out));
    ASSERT_TRUE(n > 0);
    ASSERT_STR_EQ(out, "QUJD");
    TEST_END();
}

static void test_base64_small_buffer(void)
{
    TEST_BEGIN("base64 encode buffer too small");
    char out[4]; /* needs 8 for "Hello" */
    size_t n = base64_encode((const unsigned char *)"Hello", 5, out, sizeof(out));
    ASSERT_EQ((int)n, 0);
    TEST_END();
}

static void test_base64_binary(void)
{
    TEST_BEGIN("base64 encode binary data with nulls");
    unsigned char bin[] = {0x00, 0xFF, 0x80, 0x01};
    char out[16];
    size_t n = base64_encode(bin, 4, out, sizeof(out));
    ASSERT_TRUE(n > 0);
    ASSERT_STR_EQ(out, "AP+AAQ==");
    TEST_END();
}

void test_base64(void)
{
    test_base64_empty();
    test_base64_hello();
    test_base64_padding();
    test_base64_small_buffer();
    test_base64_binary();
}
```

- [ ] **Step 2: Register tests in runner.c**

In `tests/runner.c`, add declaration and call:

```c
extern void test_base64(void);
```

And in `main()`, add:

```c
test_base64();
```

- [ ] **Step 3: Create base64 header**

In `src/core/base64.h`:

```c
/* src/core/base64.h */
#ifndef NUTSHELL_BASE64_H
#define NUTSHELL_BASE64_H

#include <stddef.h>

/* Base64-encode src_len bytes from src into dst.
 * dst_size must be at least ((src_len + 2) / 3) * 4 + 1.
 * Returns number of chars written (excluding NUL), or 0 on error. */
size_t base64_encode(const unsigned char *src, size_t src_len,
                     char *dst, size_t dst_size);

#endif /* NUTSHELL_BASE64_H */
```

- [ ] **Step 4: Implement base64 encoding**

In `src/core/base64.c`:

```c
/* src/core/base64.c */
#include "base64.h"
#include <openssl/evp.h>

size_t base64_encode(const unsigned char *src, size_t src_len,
                     char *dst, size_t dst_size)
{
    if (!src || !dst || dst_size == 0) return 0;
    if (src_len == 0) { dst[0] = '\0'; return 0; }

    size_t needed = (size_t)(((src_len + 2) / 3) * 4 + 1);
    if (dst_size < needed) return 0;

    int n = EVP_EncodeBlock((unsigned char *)dst, src, (int)src_len);
    if (n <= 0) return 0;
    dst[n] = '\0';
    return (size_t)n;
}
```

- [ ] **Step 5: Run tests**

Run: `make test 2>&1 | tail -20`
Expected: All tests pass including new base64 tests

- [ ] **Step 6: Commit**

```bash
git add src/core/base64.h src/core/base64.c tests/test_base64.c tests/runner.c
git commit -m "feat: add base64 encoding module"
```

---

### Task 2: Add AiAttachment struct and update AiMessage

**Files:**
- Modify: `src/core/ai_prompt.h`
- Modify: `src/core/ai_prompt.c`

- [ ] **Step 1: Add AiAttachment struct and update AiMessage in ai_prompt.h**

After the existing `#define AI_BODY_MAX` line, change:

```c
#define AI_BODY_MAX    65536
```

to:

```c
#define AI_BODY_MAX    524288   /* 512KB — accommodates base64 images */
```

Before the `AiMessage` struct, add:

```c
/* Attached image for multimodal messages (heap-allocated) */
typedef struct {
    char *base64_url;    /* "data:image/png;base64,..." */
    int width, height;   /* original pixel dimensions */
} AiAttachment;
```

In the `AiMessage` struct, add an attachment pointer:

```c
typedef struct {
    AiRole role;
    char content[AI_MSG_MAX];
    AiAttachment *attachment;  /* NULL for text-only messages */
} AiMessage;
```

Add helper declarations:

```c
/* Free an AiAttachment and its contents. Sets *att to NULL. */
void ai_attachment_free(AiAttachment **att);

/* Duplicate an AiAttachment (deep copy). Returns NULL on alloc failure. */
AiAttachment *ai_attachment_dup(const AiAttachment *att);
```

- [ ] **Step 2: Implement attachment helpers and update ai_conv_add/ai_conv_reset in ai_prompt.c**

Add at the top of `ai_prompt.c` (after includes):

```c
void ai_attachment_free(AiAttachment **att)
{
    if (!att || !*att) return;
    free((*att)->base64_url);
    free(*att);
    *att = NULL;
}

AiAttachment *ai_attachment_dup(const AiAttachment *att)
{
    if (!att) return NULL;
    AiAttachment *dup = calloc(1, sizeof(*dup));
    if (!dup) return NULL;
    if (att->base64_url) {
        dup->base64_url = _strdup(att->base64_url);
        if (!dup->base64_url) { free(dup); return NULL; }
    }
    dup->width = att->width;
    dup->height = att->height;
    return dup;
}
```

In `ai_conv_add`, after setting content, initialize attachment:

```c
    m->attachment = NULL;
```

In `ai_conv_reset`, before `memset`, free all attachments:

```c
    for (int i = 0; i < conv->msg_count; i++)
        ai_attachment_free(&conv->messages[i].attachment);
```

- [ ] **Step 3: Update ai_build_request_body_ex to accept attachment for stream thread arg**

Change the signature in `ai_prompt.h`:

```c
size_t ai_build_request_body_ex(const AiConversation *conv,
                                const AiAttachment *last_user_attachment,
                                char *buf, size_t buf_size, int stream);
```

In `ai_prompt.c`, update the implementation. In the message serialization loop, for the last user message when `last_user_attachment` is non-NULL, write content as an array:

```c
        /* Check if this message needs multimodal content */
        int is_last_user = (conv->messages[i].role == AI_ROLE_USER
                            && i == conv->msg_count - 1
                            && last_user_attachment
                            && last_user_attachment->base64_url);

        if (is_last_user) {
            /* Multimodal content array */
            int rn = snprintf(buf + pos, buf_size - pos,
                              "{\"role\":\"%s\",\"content\":[{\"type\":\"text\",\"text\":", role);
            if (rn < 0 || pos + (size_t)rn >= buf_size) return 0;
            pos += (size_t)rn;

            pos = json_escape_str(conv->messages[i].content, buf, buf_size, pos);
            if (pos == 0) return 0;

            /* Image block */
            rn = snprintf(buf + pos, buf_size - pos,
                          "},{\"type\":\"image_url\",\"image_url\":{\"url\":");
            if (rn < 0 || pos + (size_t)rn >= buf_size) return 0;
            pos += (size_t)rn;

            pos = json_escape_str(last_user_attachment->base64_url,
                                  buf, buf_size, pos);
            if (pos == 0) return 0;

            if (pos + 3 >= buf_size) return 0;
            buf[pos++] = '}';
            buf[pos++] = '}';
            buf[pos++] = ']';
            buf[pos++] = '}';
        } else {
            /* Plain text content (existing code) */
            int rn = snprintf(buf + pos, buf_size - pos,
                              "{\"role\":\"%s\",\"content\":", role);
            if (rn < 0 || pos + (size_t)rn >= buf_size) return 0;
            pos += (size_t)rn;

            pos = json_escape_str(conv->messages[i].content, buf, buf_size, pos);
            if (pos == 0) return 0;

            if (pos + 1 >= buf_size) return 0;
            buf[pos++] = '}';
        }
```

Also check messages earlier in the conversation that have stored attachments:

Actually, to keep it simpler and avoid doubling the loop complexity: only the last user message gets the multimodal format via the `last_user_attachment` parameter. Historical images are not resent (they're too large to keep in context anyway). The text description remains.

- [ ] **Step 4: Update all callers to pass NULL for the new parameter**

In `src/ui/ai_chat.c`, find the call to `ai_build_request_body_ex` in `launch_stream_thread` (around line 591) and add `NULL`:

Change:
```c
    size_t body_len = ai_build_request_body_ex(&d->conv,
                                                arg->body, sizeof(arg->body), 1);
```
to:
```c
    size_t body_len = ai_build_request_body_ex(&d->conv, NULL,
                                                arg->body, sizeof(arg->body), 1);
```

Also update the `StreamThreadArg` body size — it currently uses a fixed buffer that's based on `AI_BODY_MAX`. Check and update if needed.

- [ ] **Step 5: Build and test**

Run: `make clean && make release 2>&1 | tail -5`
Run: `make test 2>&1 | tail -5`
Expected: Both pass

- [ ] **Step 6: Commit**

```bash
git add src/core/ai_prompt.h src/core/ai_prompt.c src/ui/ai_chat.c
git commit -m "feat: add AiAttachment struct and multimodal JSON builder"
```

---

### Task 3: Add `queued` flag to ChatMsgItem and render [Queued — Cancel]

**Files:**
- Modify: `src/core/chat_msg.h`
- Modify: `src/core/chat_msg.c`
- Modify: `src/ui/chat_listview.c`

- [ ] **Step 1: Add queued flag to ChatMsgItem**

In `src/core/chat_msg.h`, add after the `dirty` field:

```c
    int queued;          /* 1 = pending in queue, shown with [Cancel] label */
```

- [ ] **Step 2: Initialize queued flag in chat_msg_append**

In `src/core/chat_msg.c`, in `chat_msg_append`, after `item->dirty = 1;` add:

```c
    item->queued = 0;
```

- [ ] **Step 3: Update measure_item for queued user items**

In `src/ui/chat_listview.c`, in the `CHAT_ITEM_USER` case of `measure_item`, after the existing height calculation, add extra height for the queued label:

Find the `case CHAT_ITEM_USER:` block and after `return total;`, before that return, add:

```c
        /* Extra height for [Queued — Cancel] label */
        if (item->queued)
            total += CLV_SCALE(lv, 16);  /* label line height */
```

- [ ] **Step 4: Render [Queued — Cancel] in paint_user_item**

In `src/ui/chat_listview.c`, in `paint_user_item`, after the `draw_text_utf8` call and `SelectObject(hdc, old_font)`, add:

```c
    /* Draw [Queued — Cancel] label for queued messages */
    if (item->queued) {
        int label_h = CLV_SCALE(lv, 14);
        RECT label_rc;
        label_rc.left   = text_rc.left;
        label_rc.top    = text_rc.bottom + CLV_SCALE(lv, 2);
        label_rc.right  = text_rc.right;
        label_rc.bottom = label_rc.top + label_h;

        HGDIOBJ qf = SelectObject(hdc, lv->hSmallFont ? lv->hSmallFont
                                       : GetStockObject(DEFAULT_GUI_FONT));
        /* "Queued" in muted text */
        SetTextColor(hdc, RGB(160, 160, 160));
        DrawTextA(hdc, "Queued \xe2\x80\x94 ", -1, &label_rc,
                  DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        /* Measure "Queued — " width to position "Cancel" */
        SIZE qs;
        GetTextExtentPoint32A(hdc, "Queued -- ", 10, &qs);
        RECT cancel_rc = label_rc;
        cancel_rc.left += qs.cx;
        SetTextColor(hdc, CLR_RETRY_TEXT);  /* blue link color */
        DrawTextA(hdc, "Cancel", -1, &cancel_rc,
                  DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        SelectObject(hdc, qf);
    }
```

- [ ] **Step 5: Add hit-test for Cancel click in on_lbuttondown**

In `src/ui/chat_listview.c`, in `on_lbuttondown`, before the existing thinking toggle check, add a check for queued user items:

```c
        /* Check click on [Cancel] for queued user messages */
        if (item->type == CHAT_ITEM_USER && item->queued
            && my >= y && my < y + h) {
            /* Cancel link is in the bottom portion of the bubble */
            int label_top = y + h - CLV_SCALE(lv, 16);
            if (my >= label_top) {
                PostMessage(parent, WM_COMMAND,
                            MAKEWPARAM(IDC_QUEUE_CANCEL, 0), 0);
                return 1;
            }
        }
```

This requires defining `IDC_QUEUE_CANCEL` in `src/ui/resource.h`:

```c
#define IDC_QUEUE_CANCEL    220
```

- [ ] **Step 6: Build and test**

Run: `make clean && make release 2>&1 | tail -5`
Run: `make test 2>&1 | tail -5`
Expected: Both pass

- [ ] **Step 7: Commit**

```bash
git add src/core/chat_msg.h src/core/chat_msg.c src/ui/chat_listview.c src/ui/resource.h
git commit -m "feat: queued user message rendering with Cancel link"
```

---

### Task 4: Implement message queue logic

**Files:**
- Modify: `src/ui/ai_chat.c`

- [ ] **Step 1: Add queue fields to AiChatData**

Find the `AiChatData` struct (or the typedef section near the top of ai_chat.c). Add:

```c
    /* Pending image attachment for the next send */
    AiAttachment *pending_attachment;

    /* Message queue (single slot — used when AI is busy) */
    struct {
        char text[2048];
        AiAttachment *attachment;
        ChatMsgItem *chat_item;
        int active;
    } queued_msg;
```

- [ ] **Step 2: Modify IDC_CHAT_SEND to queue when busy**

Change the `IDC_CHAT_SEND` handler from:

```c
case IDC_CHAT_SEND:
    if (d && ACTIVE_BUSY(d)) {
        cancel_active_stream(d);
        chat_msg_append(&d->msg_list, CHAT_ITEM_STATUS,
            "[cancelled]");
        if (d->hChatList) {
            chat_listview_invalidate(d->hChatList);
            chat_listview_scroll_to_bottom(d->hChatList);
        }
    } else {
        send_user_message(d);
    }
```

to:

```c
case IDC_CHAT_SEND:
    if (d && ACTIVE_BUSY(d)) {
        /* Queue message instead of cancelling */
        char qbuf[2048];
        GetWindowText(d->hInput, qbuf, (int)sizeof(qbuf));
        if (qbuf[0] != '\0') {
            /* Remove old queued message if any */
            if (d->queued_msg.active && d->queued_msg.chat_item) {
                chat_msg_remove(&d->msg_list, d->queued_msg.chat_item);
                d->queued_msg.chat_item = NULL;
            }
            ai_attachment_free(&d->queued_msg.attachment);

            /* Store new queued message */
            snprintf(d->queued_msg.text, sizeof(d->queued_msg.text),
                     "%s", qbuf);
            d->queued_msg.attachment = d->pending_attachment;
            d->pending_attachment = NULL;  /* ownership transferred */
            d->queued_msg.active = 1;

            /* Show in chat as queued */
            ChatMsgItem *qi = chat_msg_append(&d->msg_list,
                                               CHAT_ITEM_USER, qbuf);
            if (qi) qi->queued = 1;
            d->queued_msg.chat_item = qi;

            SetWindowText(d->hInput, "");
            if (d->hChatList) {
                chat_listview_invalidate(d->hChatList);
                chat_listview_scroll_to_bottom(d->hChatList);
            }
        }
    } else {
        send_user_message(d);
    }
```

- [ ] **Step 3: Add IDC_QUEUE_CANCEL handler**

In the `WM_COMMAND` switch, add a new case:

```c
case IDC_QUEUE_CANCEL:
    if (d && d->queued_msg.active) {
        /* Restore text to input */
        SetWindowText(d->hInput, d->queued_msg.text);
        /* Remove bubble from chat */
        if (d->queued_msg.chat_item) {
            chat_msg_remove(&d->msg_list, d->queued_msg.chat_item);
            d->queued_msg.chat_item = NULL;
        }
        /* Free attachment and clear queue */
        ai_attachment_free(&d->queued_msg.attachment);
        d->queued_msg.active = 0;
        d->queued_msg.text[0] = '\0';

        if (d->hChatList) {
            chat_listview_invalidate(d->hChatList);
        }
        SetFocus(d->hInput);
    }
    return 0;
```

- [ ] **Step 4: Auto-send queued message on stream completion**

In the `WM_AI_RESPONSE` handler, in the `wParam == 2` (stream complete) path, after the existing command extraction and approval logic completes, add:

Find the end of the wParam==2 block (before the `break;`). Add:

```c
        /* Auto-send queued message if any */
        if (d->queued_msg.active && !d->pending_approval) {
            /* Unqueue: mark bubble as no longer queued */
            if (d->queued_msg.chat_item)
                d->queued_msg.chat_item->queued = 0;

            /* Transfer attachment to pending */
            d->pending_attachment = d->queued_msg.attachment;
            d->queued_msg.attachment = NULL;

            /* Set input text and trigger send */
            SetWindowText(d->hInput, d->queued_msg.text);
            d->queued_msg.active = 0;
            d->queued_msg.text[0] = '\0';
            d->queued_msg.chat_item = NULL;

            /* Don't call send_user_message directly — it would add
             * another user bubble. Instead, post a deferred send. */
            PostMessage(hwnd, WM_COMMAND,
                        MAKEWPARAM(IDC_CHAT_SEND, BN_CLICKED), 0);
        }
```

Wait — this would add a duplicate user bubble since `send_user_message` appends a CHAT_ITEM_USER. The queued bubble already exists. Better approach: modify `send_user_message` to accept optional pre-existing chat item and attachment.

Actually, simplest approach: remove the queued bubble (it was a preview), then call `send_user_message` which creates a new one:

```c
        /* Auto-send queued message if any */
        if (d->queued_msg.active && !d->pending_approval) {
            /* Remove the queued preview bubble */
            if (d->queued_msg.chat_item) {
                chat_msg_remove(&d->msg_list, d->queued_msg.chat_item);
                d->queued_msg.chat_item = NULL;
            }

            /* Set up for send: put text in input, attachment in pending */
            SetWindowText(d->hInput, d->queued_msg.text);
            d->pending_attachment = d->queued_msg.attachment;
            d->queued_msg.attachment = NULL;
            d->queued_msg.active = 0;
            d->queued_msg.text[0] = '\0';

            if (d->hChatList)
                chat_listview_invalidate(d->hChatList);

            /* Deferred send — let message loop process any pending paints first */
            PostMessage(hwnd, WM_COMMAND,
                        MAKEWPARAM(IDC_CHAT_SEND, BN_CLICKED), 0);
        }
```

- [ ] **Step 5: Clean up queue on WM_DESTROY**

In the `WM_DESTROY` handler, add:

```c
    ai_attachment_free(&d->pending_attachment);
    ai_attachment_free(&d->queued_msg.attachment);
```

- [ ] **Step 6: Build and test**

Run: `make clean && make release 2>&1 | tail -5`
Run: `make test 2>&1 | tail -5`
Expected: Both pass

- [ ] **Step 7: Commit**

```bash
git add src/ui/ai_chat.c
git commit -m "feat: message queue with cancel support"
```

---

### Task 5: Implement image paste from clipboard via GDI+

**Files:**
- Modify: `src/ui/ai_chat.c`

This is the most complex task. It requires GDI+ flat API calls, COM stream handling, and clipboard bitmap extraction.

- [ ] **Step 1: Add GDI+ includes and initialization**

At the top of `ai_chat.c`, add after existing includes:

```c
#include "base64.h"
#include <shlwapi.h>   /* for IStream / SHCreateMemStream */
#include <objbase.h>    /* for CoInitialize */

/* GDI+ flat API declarations (avoid C++ headers) */
typedef int GpStatus;
typedef void GpBitmap;
typedef void GpImage;
typedef struct { UINT32 Data1; UINT16 Data2; UINT16 Data3; BYTE Data4[8]; } GPCLSID;
typedef struct { UINT32 Num; UINT32 Size; } EncoderParameters;

extern GpStatus __stdcall GdiplusStartup(ULONG_PTR *token, const void *input, void *output);
extern void     __stdcall GdiplusShutdown(ULONG_PTR token);
extern GpStatus __stdcall GdipCreateBitmapFromHBITMAP(HBITMAP hbm, HPALETTE hpal, GpBitmap **bitmap);
extern GpStatus __stdcall GdipSaveImageToStream(GpImage *image, IStream *stream, const GPCLSID *clsid, const EncoderParameters *params);
extern GpStatus __stdcall GdipDisposeImage(GpImage *image);
extern GpStatus __stdcall GdipGetImageWidth(GpImage *image, UINT *width);
extern GpStatus __stdcall GdipGetImageHeight(GpImage *image, UINT *height);

/* GDI+ startup input structure */
typedef struct {
    UINT32 GdiplusVersion;
    void *DebugEventCallback;
    BOOL SuppressBackgroundThread;
    BOOL SuppressExternalCodecs;
} GdiplusStartupInput;

/* PNG encoder CLSID: {557CF406-1A04-11D3-9A73-0000F81EF32E} */
static const GPCLSID CLSID_PNG = {
    0x557CF406, 0x1A04, 0x11D3,
    {0x9A, 0x73, 0x00, 0x00, 0xF8, 0x1E, 0xF3, 0x2E}
};
```

Add a GDI+ token field to `AiChatData`:

```c
    ULONG_PTR gdip_token;
```

In `WM_CREATE` handler, after other initialization:

```c
    GdiplusStartupInput gdip_in = {1, NULL, FALSE, FALSE};
    GdiplusStartup(&nd->gdip_token, &gdip_in, NULL);
```

In `WM_DESTROY` handler:

```c
    GdiplusShutdown(d->gdip_token);
```

- [ ] **Step 2: Implement clipboard image extraction helper**

Add a static helper function:

```c
/* Extract clipboard bitmap, convert to PNG, return as base64 data URL.
 * Caller must free the returned AiAttachment. Returns NULL on failure. */
static AiAttachment *clipboard_to_png_attachment(HWND hwnd)
{
    if (!IsClipboardFormatAvailable(CF_BITMAP))
        return NULL;
    if (!OpenClipboard(hwnd))
        return NULL;

    HBITMAP hbm = (HBITMAP)GetClipboardData(CF_BITMAP);
    if (!hbm) { CloseClipboard(); return NULL; }

    /* Get bitmap dimensions */
    BITMAP bm_info;
    GetObject(hbm, sizeof(bm_info), &bm_info);

    /* Create GDI+ bitmap from HBITMAP */
    GpBitmap *gpbmp = NULL;
    if (GdipCreateBitmapFromHBITMAP(hbm, NULL, &gpbmp) != 0 || !gpbmp) {
        CloseClipboard();
        return NULL;
    }
    CloseClipboard();  /* Done with clipboard */

    /* Create IStream for PNG output */
    IStream *stream = SHCreateMemStream(NULL, 0);
    if (!stream) {
        GdipDisposeImage((GpImage *)gpbmp);
        return NULL;
    }

    /* Save as PNG */
    if (GdipSaveImageToStream((GpImage *)gpbmp, stream, &CLSID_PNG, NULL) != 0) {
        IStream_Release(stream);
        GdipDisposeImage((GpImage *)gpbmp);
        return NULL;
    }
    GdipDisposeImage((GpImage *)gpbmp);

    /* Read stream bytes */
    STATSTG stat;
    IStream_Stat(stream, &stat, STATFLAG_NONAME);
    size_t png_len = (size_t)stat.cbSize.QuadPart;

    unsigned char *png_buf = malloc(png_len);
    if (!png_buf) { IStream_Release(stream); return NULL; }

    LARGE_INTEGER zero = {{0}};
    IStream_Seek(stream, zero, STREAM_SEEK_SET, NULL);
    ULONG read = 0;
    IStream_Read(stream, png_buf, (ULONG)png_len, &read);
    IStream_Release(stream);

    if (read != (ULONG)png_len) { free(png_buf); return NULL; }

    /* Base64 encode */
    size_t b64_size = ((png_len + 2) / 3) * 4 + 1;
    char *b64 = malloc(b64_size);
    if (!b64) { free(png_buf); return NULL; }

    size_t b64_len = base64_encode(png_buf, png_len, b64, b64_size);
    free(png_buf);
    if (b64_len == 0) { free(b64); return NULL; }

    /* Build data URL */
    const char *prefix = "data:image/png;base64,";
    size_t prefix_len = strlen(prefix);
    char *url = malloc(prefix_len + b64_len + 1);
    if (!url) { free(b64); return NULL; }
    memcpy(url, prefix, prefix_len);
    memcpy(url + prefix_len, b64, b64_len + 1);  /* include NUL */
    free(b64);

    /* Build attachment */
    AiAttachment *att = calloc(1, sizeof(*att));
    if (!att) { free(url); return NULL; }
    att->base64_url = url;
    att->width = bm_info.bmWidth;
    att->height = bm_info.bmHeight;
    return att;
}
```

- [ ] **Step 3: Add WM_PASTE handler in InputSubclassProc**

In `InputSubclassProc`, add before the `WM_KEYDOWN` handler:

```c
    if (msg == WM_PASTE) {
        /* Check for image in clipboard first */
        HWND parent = GetParent(hwnd);
        AiChatData *d = parent
            ? (AiChatData *)GetWindowLongPtr(parent, GWLP_USERDATA) : NULL;
        if (d && IsClipboardFormatAvailable(CF_BITMAP)) {
            AiAttachment *att = clipboard_to_png_attachment(parent);
            if (att) {
                /* Replace any existing attachment */
                ai_attachment_free(&d->pending_attachment);
                d->pending_attachment = att;
                /* Insert placeholder text */
                SendMessageA(hwnd, EM_REPLACESEL, TRUE,
                             (LPARAM)"[Image attached] ");
                return 0;  /* consumed */
            }
        }
        /* Fall through to default text paste */
    }
```

- [ ] **Step 4: Pass attachment when sending**

In `send_user_message`, before `launch_stream_thread(d)`, store the attachment on the conversation's last user message:

```c
    /* Attach image if pending */
    if (d->pending_attachment) {
        int last = d->conv.msg_count - 1;
        if (last >= 0 && d->conv.messages[last].role == AI_ROLE_USER) {
            d->conv.messages[last].attachment =
                ai_attachment_dup(d->pending_attachment);
        }
    }
```

In `launch_stream_thread`, pass the attachment to the JSON builder. Find the `ai_build_request_body_ex` call and change:

```c
    /* Get attachment from last user message if any */
    int last_msg = d->conv.msg_count - 1;
    const AiAttachment *att = (last_msg >= 0)
        ? d->conv.messages[last_msg].attachment : NULL;
    size_t body_len = ai_build_request_body_ex(&d->conv, att,
                                                arg->body, sizeof(arg->body), 1);
```

After launch, clear pending attachment:

```c
    ai_attachment_free(&d->pending_attachment);
```

- [ ] **Step 5: Strip "[Image attached]" prefix from input text before sending**

In `send_user_message`, after `GetWindowText(d->hInput, input, ...)`, add:

```c
    /* Strip image placeholder prefix */
    const char *img_prefix = "[Image attached] ";
    size_t pfx_len = strlen(img_prefix);
    char *text_start = input;
    while (strstr(text_start, img_prefix) == text_start)
        text_start += pfx_len;
    if (text_start != input)
        memmove(input, text_start, strlen(text_start) + 1);
```

- [ ] **Step 6: Update StreamThreadArg body buffer size**

Find the `StreamThreadArg` struct and ensure `body` is large enough:

```c
    char body[AI_BODY_MAX];
```

If it already uses `AI_BODY_MAX`, the bump to 524288 handles it. If it uses a smaller literal, change it to `AI_BODY_MAX`.

- [ ] **Step 7: Build and test**

Run: `make clean && make release 2>&1 | tail -10`
Run: `make test 2>&1 | tail -5`
Expected: Build succeeds (may need `-lshlwapi` added to Makefile if `SHCreateMemStream` is not already resolved). Tests pass.

- [ ] **Step 8: Commit**

```bash
git add src/ui/ai_chat.c
git commit -m "feat: image paste from clipboard with GDI+ PNG conversion"
```

---

### Task 6: Version bump, final build, and test

**Files:**
- Modify: `src/ui/resource.h`
- Modify: `README.md`

- [ ] **Step 1: Bump version**

Check current version and increment patch. Update `APP_VERSION` and `APP_VERSION_BINARY` in `resource.h`, and `**Version**:` in `README.md`.

- [ ] **Step 2: Clean build**

Run: `make clean && make release 2>&1 | tail -10`
Expected: Build succeeds with zero warnings

- [ ] **Step 3: Run tests**

Run: `make test 2>&1 | tail -10`
Expected: All tests pass

- [ ] **Step 4: Commit**

```bash
git add src/ui/resource.h README.md build/win/nutshell.exe
git commit -m "feat: image paste and message queue (v1.0.XX)"
```
