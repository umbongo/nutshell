# Acorn Watermark Empty State

**Date:** 2026-03-30
**Status:** Approved

## Overview

Two changes to Nutshell's startup behavior and empty-window appearance:

1. **Remove auto-open session manager** — The session manager dialog no longer opens automatically on startup. Users open it via the tab "+" button or File menu.
2. **Acorn watermark on blank window** — When no sessions are open, the terminal area displays the acorn logo as a ghosted watermark centered in the window.

## Watermark Specification

- **Source image:** `images/nutshell_acorn_transparent.png` (2058x2048 RGBA PNG)
- **Rendering:** Embedded as `RCDATA` resource, loaded via GDI+ flat C API
- **Opacity:** ~10% (alpha 0.10 via GDI+ color matrix) — barely visible ghost effect
- **Margins:** 15% border on all four sides of the terminal area — logo draws within the inner 70%
- **Aspect ratio:** Locked. The shorter dimension of the available area constrains the logo size; the logo is centered in the remaining space.
- **Scaling:** Logo resizes dynamically as the window is resized. GDI+ interpolation provides smooth scaling from the full-resolution source.
- **Visibility:** Only shown when `g_active_session == NULL`. Disappears immediately when a session connects. Reappears when all sessions are closed.

## Architecture

### Resource Embedding

Add the PNG to `src/ui/resource.rc` as an `RCDATA` resource:
```
IDR_ACORN_PNG  RCDATA  "../../images/nutshell_acorn_transparent.png"
```

Define `IDR_ACORN_PNG` (e.g., 202) in `resource.h`.

### GDI+ Integration

- **Initialization:** Call `GdiplusStartup()` during `WM_CREATE` in `window.c`. Store the token globally.
- **Image loading:** Load the PNG from resource into a `GpImage*` once at startup. Cache the handle.
- **Shutdown:** Call `GdiplusShutdown()` during `WM_DESTROY`.
- **Linker:** Add `-lgdiplus` to the Makefile link flags.

### Paint Logic (window.c WM_PAINT)

In the existing `else` branch (no active session), after filling the background:

1. Compute the terminal area rect (below tabs, left of AI panel/scrollbar).
2. Inset by 15% on each side to get the drawing box.
3. Fit the acorn (square aspect) into the drawing box: `size = min(box_w, box_h)`.
4. Center the fitted square within the drawing box.
5. Create a `GpImageAttributes` with a color matrix setting alpha to 0.10.
6. Call `GdipDrawImageRectRectI()` to render the cached image into the computed rect.

### Startup Change (window.c WM_CREATE)

Remove the line:
```c
PostMessage(hwnd, WM_SHOW_SESSION_MANAGER, 0, 0);
```

The `WM_SHOW_SESSION_MANAGER` handler and `on_tab_new()` remain intact for manual invocation.

## Files Modified

| File | Change |
|------|--------|
| `src/ui/window.c` | Remove auto-open PostMessage; add GDI+ init/shutdown; add watermark paint logic |
| `src/ui/resource.h` | Add `IDR_ACORN_PNG` define |
| `src/ui/resource.rc` | Add RCDATA entry for acorn PNG |
| `Makefile` | Add `-lgdiplus` to link flags |
| `src/ui/resource.h` | Version bump |
| `README.md` | Version bump |
| `src/ui/nutshell.rc` | Version bump |

## Testing

- **Manual:** Launch Nutshell. Verify no session manager dialog appears. Verify acorn watermark is visible as a faint ghost centered in the dark terminal area. Resize window and verify the logo scales proportionally with locked aspect ratio and 15% margins. Open a session and verify watermark disappears. Close all sessions and verify it reappears.
- **Build:** `make clean && make release` must compile without warnings under `-Werror`. `make test` must pass (GDI+ code is in `src/ui/`, excluded from test builds).

## Non-Goals

- No text label below the logo (e.g., "Open a session to get started").
- No animation or fade-in effect.
- No theme-aware color adjustment — the 10% opacity works on both dark themes since the image blends toward the background naturally.
