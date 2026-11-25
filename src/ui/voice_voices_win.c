#include "voice_voices_win.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

static void to_utf8(const wchar_t *w, char *out, size_t cap)
{
    if (cap == 0) return;
    out[0] = '\0';
    if (!WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)cap, NULL, NULL))
        out[0] = '\0';   /* does not fit: no partial name */
}

/* REG_SZ value `name` of `key` as UTF-8; 0 when absent. */
static int read_sz(HKEY key, const wchar_t *name, char *out, size_t cap)
{
    wchar_t buf[512];
    DWORD type = 0, sz = (DWORD)(sizeof(buf) - sizeof(wchar_t));
    memset(buf, 0, sizeof(buf));
    if (RegQueryValueExW(key, name, NULL, &type, (BYTE *)buf, &sz) != ERROR_SUCCESS
        || type != REG_SZ || buf[0] == L'\0')
        return 0;
    to_utf8(buf, out, cap);
    return out[0] != '\0';
}

static size_t read_tree(HKEY root, const char *root_name, const wchar_t *path, int onecore,
                        VoiceInfo *out, size_t n, size_t max)
{
    HKEY tokens;
    if (path == NULL || n >= max) return n;
    if (RegOpenKeyExW(root, path, 0, KEY_READ, &tokens) != ERROR_SUCCESS) return n;

    char path8[200];
    to_utf8(path, path8, sizeof(path8));
    for (DWORD i = 0; n < max; i++) {
        wchar_t sub[128];
        DWORD sublen = (DWORD)(sizeof(sub) / sizeof(sub[0]));
        if (RegEnumKeyExW(tokens, i, sub, &sublen, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;

        HKEY tk;
        if (RegOpenKeyExW(tokens, sub, 0, KEY_READ, &tk) != ERROR_SUCCESS) continue;
        VoiceInfo *v = &out[n];
        memset(v, 0, sizeof(*v));
        char sub8[128];
        to_utf8(sub, sub8, sizeof(sub8));
        if (snprintf(v->id, sizeof(v->id), "%s\\%s\\%s", root_name, path8, sub8)
            >= (int)sizeof(v->id)) {
            RegCloseKey(tk);
            continue;   /* an id that does not fit would name the wrong token */
        }
        v->onecore = onecore;

        HKEY attr;
        int have_name = 0;
        if (RegOpenKeyExW(tk, L"Attributes", 0, KEY_READ, &attr) == ERROR_SUCCESS) {
            have_name = read_sz(attr, L"Name", v->name, sizeof(v->name));
            char g[32];
            if (read_sz(attr, L"Gender", g, sizeof(g))) v->female = strcasecmp(g, "Female") == 0;
            (void)read_sz(attr, L"Language", v->lang, sizeof(v->lang));
            RegCloseKey(attr);
        }
        if (!have_name && !read_sz(tk, NULL, v->name, sizeof(v->name)))
            snprintf(v->name, sizeof(v->name), "%s", sub8);
        RegCloseKey(tk);
        n++;
    }
    RegCloseKey(tokens);
    return n;
}

size_t voice_voices_read(HKEY root, const wchar_t *sapi_tokens, const wchar_t *onecore_tokens,
                         VoiceInfo *out, size_t max)
{
    const char *root_name;
    if (root == HKEY_LOCAL_MACHINE) root_name = "HKEY_LOCAL_MACHINE";
    else if (root == HKEY_CURRENT_USER) root_name = "HKEY_CURRENT_USER";
    else return 0;
    if (out == NULL || max == 0) return 0;
    size_t n = read_tree(root, root_name, sapi_tokens, 0, out, 0, max);
    return read_tree(root, root_name, onecore_tokens, 1, out, n, max);
}

size_t voice_voices_list(VoiceInfo *out, size_t max)
{
    size_t n = voice_voices_read(HKEY_LOCAL_MACHINE,
                                 L"SOFTWARE\\Microsoft\\Speech\\Voices\\Tokens",
                                 L"SOFTWARE\\Microsoft\\Speech_OneCore\\Voices\\Tokens",
                                 out, max);
    return voice_list_dedupe(out, n);
}
