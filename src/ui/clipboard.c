#include "clipboard.h"

#ifdef _WIN32

/* Convert `len` bytes of UTF-8 into a moveable global block holding a
 * NUL-terminated UTF-16 string, or NULL on any failure. */
static HGLOBAL utf8_to_hglobal(const char *utf8, size_t len)
{
    if (!utf8 || len == 0) return NULL;
    int wneed = MultiByteToWideChar(CP_UTF8, 0, utf8, (int)len, NULL, 0);
    if (wneed <= 0) return NULL;
    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, ((size_t)wneed + 1) * sizeof(wchar_t));
    if (!hg) return NULL;
    wchar_t *dst = (wchar_t *)GlobalLock(hg);
    if (!dst) { GlobalFree(hg); return NULL; }
    int written = MultiByteToWideChar(CP_UTF8, 0, utf8, (int)len, dst, wneed);
    dst[written > 0 ? written : 0] = L'\0';
    GlobalUnlock(hg);
    return hg;
}

int clip_set_utf8(const char *utf8, size_t len)
{
    HGLOBAL hg = utf8_to_hglobal(utf8, len);
    if (!hg) return 0;
    /* On success the clipboard owns hg; on failure it is still ours. */
    if (!SetClipboardData(CF_UNICODETEXT, hg)) { GlobalFree(hg); return 0; }
    return 1;
}

int clip_copy_utf8(HWND owner, const char *utf8, size_t len)
{
    /* Build the block before touching the clipboard, so a failed conversion
     * never wipes what the user already has on it. */
    HGLOBAL hg = utf8_to_hglobal(utf8, len);
    if (!hg) return 0;
    if (!OpenClipboard(owner)) { GlobalFree(hg); return 0; }
    int ok = 0;
    if (EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, hg)) ok = 1;
    else GlobalFree(hg);
    CloseClipboard();
    return ok;
}

#endif /* _WIN32 */
