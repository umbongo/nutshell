#ifdef _WIN32

#include <windows.h>
#include "gdip_png.h"
#include <stdlib.h>

/* GDI+ flat API, declared the way ai_chat.c does (C-compatible, no C++
 * headers). Only what the capture needs. */
typedef int GpStatus;
typedef void GpBitmap;
typedef void GpImage;

extern GpStatus __stdcall GdipCreateBitmapFromHBITMAP(HBITMAP hbm, HPALETTE hpal,
                                                       GpBitmap **bitmap);
extern GpStatus __stdcall GdipSaveImageToFile(GpImage *image, const WCHAR *filename,
                                               const CLSID *clsid_encoder,
                                               const void *encoder_params);
extern GpStatus __stdcall GdipDisposeImage(GpImage *image);

/* PNG encoder CLSID: {557CF406-1A04-11D3-9A73-0000F81EF32E} */
static const CLSID k_png_encoder = {
    0x557CF406, 0x1A04, 0x11D3,
    {0x9A, 0x73, 0x00, 0x00, 0xF8, 0x1E, 0xF3, 0x2E}
};

int gdip_png_save_client(HWND hwnd, const char *path)
{
    if (!hwnd || !path || !path[0] || IsIconic(hwnd)) return 0;

    RECT rc;
    if (!GetClientRect(hwnd, &rc)) return 0;
    const int w = (int)(rc.right - rc.left);
    const int h = (int)(rc.bottom - rc.top);
    if (w <= 0 || h <= 0) return 0;

    POINT origin;
    origin.x = 0;
    origin.y = 0;
    if (!ClientToScreen(hwnd, &origin)) return 0;

    int ok = 0;
    HDC screen = GetDC(NULL);
    HDC mem = screen ? CreateCompatibleDC(screen) : NULL;
    HBITMAP dib = NULL;
    if (screen && mem) {
        /* 24 bits per pixel: a 32-bit DIB from BitBlt carries zero alpha,
         * which GDI+ would write out as a fully transparent picture. */
        BITMAPINFO bi;
        ZeroMemory(&bi, sizeof(bi));
        bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth       = w;
        bi.bmiHeader.biHeight      = -h;          /* top-down */
        bi.bmiHeader.biPlanes      = 1;
        bi.bmiHeader.biBitCount    = 24;
        bi.bmiHeader.biCompression = BI_RGB;
        void *bits = NULL;
        dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
        if (dib) {
            HGDIOBJ old = SelectObject(mem, dib);
            BOOL blt = BitBlt(mem, 0, 0, w, h, screen, origin.x, origin.y,
                              SRCCOPY | CAPTUREBLT);
            SelectObject(mem, old);               /* GDI+ wants it deselected */
            if (blt) {
                int wlen = MultiByteToWideChar(CP_ACP, 0, path, -1, NULL, 0);
                WCHAR *wpath = (wlen > 0) ? (WCHAR *)malloc((size_t)wlen * sizeof(WCHAR)) : NULL;
                if (wpath && MultiByteToWideChar(CP_ACP, 0, path, -1, wpath, wlen) > 0) {
                    GpBitmap *bmp = NULL;
                    if (GdipCreateBitmapFromHBITMAP(dib, NULL, &bmp) == 0 && bmp) {
                        ok = (GdipSaveImageToFile((GpImage *)bmp, wpath, &k_png_encoder, NULL) == 0);
                        GdipDisposeImage((GpImage *)bmp);
                    }
                }
                free(wpath);
            }
        }
    }
    if (dib) DeleteObject(dib);
    if (mem) DeleteDC(mem);
    if (screen) ReleaseDC(NULL, screen);
    return ok;
}

#endif /* _WIN32 */
