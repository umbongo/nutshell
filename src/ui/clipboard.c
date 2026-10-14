#include "clipboard.h"

#ifdef _WIN32

void clip_set_utf8(const char *utf8, size_t len)
{
    if (!utf8 || len == 0) return;
    int wneed = MultiByteToWideChar(CP_UTF8, 0, utf8, (int)len, NULL, 0);
    if (wneed <= 0) return;
    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, ((size_t)wneed + 1) * sizeof(wchar_t));
    if (!hg) return;
    wchar_t *dst = (wchar_t *)GlobalLock(hg);
    if (!dst) { GlobalFree(hg); return; }
    int written = MultiByteToWideChar(CP_UTF8, 0, utf8, (int)len, dst, wneed);
    dst[written > 0 ? written : 0] = L'\0';
    GlobalUnlock(hg);
    /* On success the clipboard owns hg; on failure it is still ours. */
    if (!SetClipboardData(CF_UNICODETEXT, hg)) GlobalFree(hg);
}

int clip_copy_utf8(HWND owner, const char *utf8, size_t len)
{
    if (!utf8 || len == 0) return 0;
    if (!OpenClipboard(owner)) return 0;
    EmptyClipboard();
    clip_set_utf8(utf8, len);
    CloseClipboard();
    return 1;
}

#endif /* _WIN32 */
