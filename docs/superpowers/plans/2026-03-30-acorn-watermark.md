# Acorn Watermark Empty State — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Show a ghosted acorn watermark on the blank terminal area when no session is active, and stop auto-opening the session manager on startup.

**Architecture:** Embed the acorn PNG as an RCDATA resource. Use GDI+ flat C API to load it once at startup and render it at ~10% opacity in the WM_PAINT no-session branch. Remove the PostMessage that auto-opens the session manager.

**Tech Stack:** Win32 GDI+, MinGW cross-compile, C11

---

### Task 1: Remove auto-open session manager

**Files:**
- Modify: `src/ui/window.c:1582`

- [ ] **Step 1: Remove the PostMessage call**

In `src/ui/window.c`, delete line 1582:

```c
PostMessage(hwnd, WM_SHOW_SESSION_MANAGER, 0, 0);
```

The `WM_SHOW_SESSION_MANAGER` handler and `on_tab_new()` remain intact — users still open the session manager via the "+" tab button or File > New Session.

- [ ] **Step 2: Commit**

```bash
git add src/ui/window.c
git commit -m "feat: remove auto-open session manager on startup"
```

---

### Task 2: Embed acorn PNG as resource

**Files:**
- Modify: `src/ui/resource.h`
- Modify: `src/ui/resource.rc`

- [ ] **Step 1: Add resource ID to resource.h**

Add after the `IDR_FONT_INTER_BOLD` line (line 9):

```c
#define IDR_ACORN_PNG       202
```

- [ ] **Step 2: Add RCDATA entry to resource.rc**

Add after the `IDD_SESSION_MANAGER` dialog block (after line 53), before the `END` or at the end of the file:

```c
IDR_ACORN_PNG  RCDATA  "../../images/nutshell_acorn_transparent.png"
```

The path is relative to `src/ui/` where the .rc file lives.

- [ ] **Step 3: Build to verify resource compiles**

```bash
make clean && make release
```

Expected: builds successfully, no errors from `windres`.

- [ ] **Step 4: Commit**

```bash
git add src/ui/resource.h src/ui/resource.rc
git commit -m "feat: embed acorn PNG as RCDATA resource"
```

---

### Task 3: Add GDI+ init/shutdown and image loading

**Files:**
- Modify: `src/ui/window.c:1-110` (includes and globals)
- Modify: `src/ui/window.c:1493-1583` (WM_CREATE)
- Modify: `src/ui/window.c:2498-2515` (WM_DESTROY)
- Modify: `Makefile:10-12` (LDFLAGS)

- [ ] **Step 1: Add GDI+ link flag to Makefile**

In `Makefile`, add `-lgdiplus` to the LDFLAGS line, before `-lws2_32`:

Change:
```makefile
          -lws2_32 -lgdi32 -luser32 -lcomctl32 -ldwmapi -lwinhttp -lm
```
To:
```makefile
          -lgdiplus -lws2_32 -lgdi32 -luser32 -lcomctl32 -ldwmapi -lwinhttp -lm
```

- [ ] **Step 2: Add GDI+ includes and globals to window.c**

After the existing includes (after line 33, `#include <dwmapi.h>`), add:

```c
#include <gdiplus.h>       /* GDI+ flat API */
#include <gdiplusflat.h>
```

After the global declarations block (after line 109, `static int g_ai_splitter_dragging = 0;`), add:

```c
/* ---- Acorn watermark state ---- */
static ULONG_PTR g_gdip_token = 0;
static GpImage  *g_acorn_image = NULL;
```

- [ ] **Step 3: Add GDI+ startup and image load to WM_CREATE**

In the `WM_CREATE` handler, just before the existing `return 0;` at line 1583 (after removing the PostMessage in Task 1), add:

```c
            /* Initialize GDI+ for acorn watermark rendering */
            {
                GdiplusStartupInput gdip_input;
                gdip_input.GdiplusVersion = 1;
                gdip_input.DebugEventCallback = NULL;
                gdip_input.SuppressBackgroundThread = FALSE;
                gdip_input.SuppressExternalCodecs = FALSE;
                if (GdiplusStartup(&g_gdip_token, &gdip_input, NULL) == 0) {
                    /* Load acorn PNG from embedded RCDATA resource */
                    HRSRC hRes = FindResource(g_hInst,
                                              MAKEINTRESOURCE(IDR_ACORN_PNG),
                                              RT_RCDATA);
                    if (hRes) {
                        DWORD sz = SizeofResource(g_hInst, hRes);
                        HGLOBAL hMem = LoadResource(g_hInst, hRes);
                        if (hMem && sz > 0) {
                            const void *data = LockResource(hMem);
                            HGLOBAL hBuf = GlobalAlloc(GMEM_MOVEABLE, sz);
                            if (hBuf) {
                                void *buf = GlobalLock(hBuf);
                                memcpy(buf, data, sz);
                                GlobalUnlock(hBuf);
                                IStream *stream = NULL;
                                if (CreateStreamOnHGlobal(hBuf, TRUE,
                                                         &stream) == S_OK) {
                                    GdipCreateBitmapFromStream(stream,
                                        (GpBitmap **)&g_acorn_image);
                                    stream->lpVtbl->Release(stream);
                                }
                            }
                        }
                    }
                }
            }
```

- [ ] **Step 4: Add GDI+ shutdown to WM_DESTROY**

In the `WM_DESTROY` handler, before `PostQuitMessage(0);` (line 2514), add:

```c
            if (g_acorn_image) {
                GdipDisposeImage(g_acorn_image);
                g_acorn_image = NULL;
            }
            if (g_gdip_token) {
                GdiplusShutdown(g_gdip_token);
                g_gdip_token = 0;
            }
```

- [ ] **Step 5: Build to verify GDI+ links and compiles**

```bash
make clean && make release
```

Expected: builds successfully with no warnings. GDI+ symbols resolve.

- [ ] **Step 6: Run tests**

```bash
make test
```

Expected: all tests pass (GDI+ code is in `src/ui/`, excluded from test builds).

- [ ] **Step 7: Commit**

```bash
git add Makefile src/ui/window.c
git commit -m "feat: add GDI+ init and acorn image loading from resource"
```

---

### Task 4: Render watermark in WM_PAINT

**Files:**
- Modify: `src/ui/window.c:2103-2110` (WM_PAINT no-session branch)

- [ ] **Step 1: Add watermark rendering to the no-session paint branch**

Replace the existing `else` block in `WM_PAINT` (lines 2103-2110):

```c
            } else {
                HBRUSH brush = CreateSolidBrush(g_renderer.defaultBg);
                RECT fill = { ps.rcPaint.left, ps.rcPaint.top,
                              term_right_edge < ps.rcPaint.right ? term_right_edge : ps.rcPaint.right,
                              ps.rcPaint.bottom };
                FillRect(hdc, &fill, brush);
                DeleteObject(brush);
            }
```

With:

```c
            } else {
                HBRUSH brush = CreateSolidBrush(g_renderer.defaultBg);
                RECT fill = { ps.rcPaint.left, ps.rcPaint.top,
                              term_right_edge < ps.rcPaint.right ? term_right_edge : ps.rcPaint.right,
                              ps.rcPaint.bottom };
                FillRect(hdc, &fill, brush);
                DeleteObject(brush);

                /* Draw ghosted acorn watermark centered with 15% margins */
                if (g_acorn_image) {
                    int area_w = term_right_edge;
                    int area_h = client.bottom - g_tab_height;
                    /* 15% inset on each side => 70% usable */
                    int box_w = area_w * 70 / 100;
                    int box_h = area_h * 70 / 100;
                    /* Fit square (locked aspect ratio) to shorter dimension */
                    int img_sz = box_w < box_h ? box_w : box_h;
                    if (img_sz > 0) {
                        int cx = (area_w - img_sz) / 2;
                        int cy = g_tab_height + (area_h - img_sz) / 2;

                        GpGraphics *gfx = NULL;
                        GdipCreateFromHDC(hdc, &gfx);
                        if (gfx) {
                            GdipSetInterpolationMode(gfx, 7); /* HighQualityBicubic */

                            /* Color matrix: identity with alpha = 0.10 */
                            ColorMatrix cm = {{{
                                {1, 0, 0, 0, 0},
                                {0, 1, 0, 0, 0},
                                {0, 0, 1, 0, 0},
                                {0, 0, 0, 0.10f, 0},
                                {0, 0, 0, 0, 0}
                            }}};
                            GpImageAttributes *attr = NULL;
                            GdipCreateImageAttributes(&attr);
                            if (attr) {
                                GdipSetImageAttributesColorMatrix(attr,
                                    0, TRUE, &cm, NULL, 0);

                                UINT src_w = 0, src_h = 0;
                                GdipGetImageWidth(g_acorn_image, &src_w);
                                GdipGetImageHeight(g_acorn_image, &src_h);

                                GdipDrawImageRectRectI(gfx, g_acorn_image,
                                    cx, cy, img_sz, img_sz,
                                    0, 0, (INT)src_w, (INT)src_h,
                                    2, /* UnitPixel */
                                    attr, NULL, NULL);
                                GdipDisposeImageAttributes(attr);
                            }
                            GdipDeleteGraphics(gfx);
                        }
                    }
                }
            }
```

- [ ] **Step 2: Build and verify**

```bash
make clean && make release
```

Expected: builds with no warnings under `-Werror`.

- [ ] **Step 3: Run tests**

```bash
make test
```

Expected: all tests pass.

- [ ] **Step 4: Commit**

```bash
git add src/ui/window.c
git commit -m "feat: render ghosted acorn watermark on empty terminal"
```

---

### Task 5: Version bump and final build

**Files:**
- Modify: `src/ui/resource.h:4`
- Modify: `README.md:5`
- Modify: `src/ui/nutshell.rc:13-14,27,32`

- [ ] **Step 1: Bump version to 1.0.13**

In `src/ui/resource.h`, change:
```c
#define APP_VERSION "1.0.12"
```
To:
```c
#define APP_VERSION "1.0.13"
```

In `README.md`, change:
```
**Version**: v1.0.12
```
To:
```
**Version**: v1.0.13
```

In `src/ui/nutshell.rc`, change all `1,0,12,0` to `1,0,13,0` and all `"1.0.12"` to `"1.0.13"`.

- [ ] **Step 2: Final clean build**

```bash
make clean && make release
```

Expected: builds successfully.

- [ ] **Step 3: Run tests**

```bash
make test
```

Expected: all tests pass.

- [ ] **Step 4: Commit**

```bash
git add src/ui/resource.h README.md src/ui/nutshell.rc
git commit -m "feat: acorn watermark empty state, no auto session manager (v1.0.13)"
```

---

## Verification Checklist

- [ ] Launch nutshell.exe — no session manager dialog appears automatically
- [ ] Terminal area shows faint ghosted acorn, centered, with margins
- [ ] Resize window — acorn scales proportionally, stays centered, aspect ratio locked
- [ ] Maximize window — acorn uses shorter dimension, does not stretch
- [ ] Open a session — watermark disappears
- [ ] Close the session — watermark reappears
- [ ] Open AI panel docked — watermark adjusts to narrower terminal area
