/* tests/test_voice_tts_win.c -- the SAPI speech thread and the registry voice
 * list. Built only by `make wintest`. Wine's SAPI may be absent or have no
 * voice: those cases print a SKIP line and assert only the defined outcome. */

#ifdef _WIN32

#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "test_framework.h"
#include "voice_tts_win.h"
#include "voice_voices_win.h"
#include "voice_voices.h"
#include "voice_gate.h"
#include "voice_ledger.h"
#include "ai_gate.h"
#include "dll_record.h"

#define TT_ROOT L"Software\\NutshellTest\\VoiceTts"

static void gates(int on)
{
    ai_gate_set_enabled(on);
    voice_gate_set_enabled(on);
}

static int put_sz(HKEY k, const wchar_t *name, const wchar_t *val)
{
    return RegSetValueExW(k, name, 0, REG_SZ, (const BYTE *)val,
                          (DWORD)((wcslen(val) + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}

/* Creates <TT_ROOT>\<tree>\<sub> with Attributes; a NULL name leaves it out and
 * sets the token's default value to dflt instead. */
static int make_token(const wchar_t *tree, const wchar_t *sub, const wchar_t *name,
                      const wchar_t *dflt, const wchar_t *gender, const wchar_t *lang)
{
    wchar_t path[300];
    swprintf(path, 300, TT_ROOT L"\\%ls\\%ls", tree, sub);
    HKEY k, a;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, path, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL)
        != ERROR_SUCCESS) return 0;
    int ok = 1;
    if (dflt != NULL) ok &= put_sz(k, NULL, dflt);
    if (RegCreateKeyExW(k, L"Attributes", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &a, NULL)
        == ERROR_SUCCESS) {
        if (name != NULL) ok &= put_sz(a, L"Name", name);
        ok &= put_sz(a, L"Gender", gender);
        ok &= put_sz(a, L"Language", lang);
        RegCloseKey(a);
    } else ok = 0;
    RegCloseKey(k);
    return ok;
}

int test_voice_voices_read_hkcu(void)
{
    TEST_BEGIN();
    RegDeleteTreeW(HKEY_CURRENT_USER, TT_ROOT);
    ASSERT_TRUE(make_token(L"Speech\\Voices\\Tokens", L"TTS_A", L"Test Hazel Desktop", NULL, L"Female", L"809;9"));
    ASSERT_TRUE(make_token(L"Speech\\Voices\\Tokens", L"TTS_B", NULL, L"Test Bob", L"male", L"409"));
    ASSERT_TRUE(make_token(L"Speech_OneCore\\Voices\\Tokens", L"MSTTS_C", L"Test Hazel", NULL, L"Female", L"809"));

    VoiceInfo v[8];
    memset(v, 0, sizeof(v));
    const size_t n = voice_voices_read(HKEY_CURRENT_USER,
        TT_ROOT L"\\Speech\\Voices\\Tokens", TT_ROOT L"\\Speech_OneCore\\Voices\\Tokens", v, 8);
    ASSERT_EQ((int)n, 3);

    size_t a = 99, b = 99, c = 99;
    for (size_t i = 0; i < n; i++) {
        if (strstr(v[i].id, "\\TTS_A")) a = i;
        if (strstr(v[i].id, "\\TTS_B")) b = i;
        if (strstr(v[i].id, "\\MSTTS_C")) c = i;
    }
    ASSERT_TRUE(a < n && b < n && c < n);
    ASSERT_STR_EQ(v[a].id, "HKEY_CURRENT_USER\\Software\\NutshellTest\\VoiceTts\\Speech\\Voices\\Tokens\\TTS_A");
    ASSERT_STR_EQ(v[a].name, "Test Hazel Desktop");
    ASSERT_EQ(v[a].female, 1);
    ASSERT_EQ(v[a].onecore, 0);
    ASSERT_STR_EQ(v[a].lang, "809;9");
    ASSERT_STR_EQ(v[b].name, "Test Bob");          /* default-value fallback */
    ASSERT_EQ(v[b].female, 0);
    ASSERT_STR_EQ(v[b].lang, "409");
    ASSERT_EQ(v[c].onecore, 1);
    ASSERT_TRUE(strncmp(v[c].id, "HKEY_CURRENT_USER\\", 18) == 0);

    /* the dedupe keeps the SAPI desktop token over the OneCore one of the same name */
    const size_t m = voice_list_dedupe(v, n);
    ASSERT_EQ((int)m, 2);
    int hazel = -1;
    for (size_t i = 0; i < m; i++) if (strstr(v[i].name, "Hazel")) hazel = (int)i;
    ASSERT_TRUE(hazel >= 0);
    ASSERT_EQ(v[hazel].onecore, 0);

    /* limits and absent paths */
    ASSERT_EQ((int)voice_voices_read(HKEY_CURRENT_USER, TT_ROOT L"\\Speech\\Voices\\Tokens", NULL, v, 1), 1);
    ASSERT_EQ((int)voice_voices_read(HKEY_CURRENT_USER, NULL, NULL, v, 8), 0);
    ASSERT_EQ((int)voice_voices_read(HKEY_CURRENT_USER, L"Software\\NutshellTest\\NoSuch", NULL, v, 8), 0);
    ASSERT_EQ((int)voice_voices_read(HKEY_CURRENT_USER, TT_ROOT L"\\Speech\\Voices\\Tokens", NULL, v, 0), 0);

    RegDeleteTreeW(HKEY_CURRENT_USER, TT_ROOT);
    RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\NutshellTest");   /* only if empty */
    TEST_END();
}

int test_voice_tts_widen_map(void)
{
    TEST_BEGIN();
    /* 'a' (1 byte), euro (3 bytes, 1 unit), U+1F600 (4 bytes, 2 units), invalid 0xFF, 'b' */
    const char s[] = "a\xE2\x82\xAC\xF0\x9F\x98\x80\xFF" "b";
    wchar_t w[16];
    size_t map[17];
    const size_t n = voice_tts_widen(s, sizeof(s) - 1, w, map, 16);
    ASSERT_EQ((int)n, 6);
    ASSERT_EQ((int)map[0], 0);
    ASSERT_EQ((int)map[1], 1);
    ASSERT_EQ((int)map[2], 4);
    ASSERT_EQ((int)map[3], 4);   /* low surrogate maps to the same code point */
    ASSERT_EQ((int)map[4], 8);
    ASSERT_EQ((int)map[5], 9);
    ASSERT_EQ((int)map[6], 10);
    ASSERT_EQ((int)w[1], 0x20AC);
    ASSERT_EQ((int)w[2], 0xD83D);
    ASSERT_EQ((int)w[3], 0xDE00);
    ASSERT_EQ((int)w[4], 0xFFFD);
    ASSERT_EQ((int)w[5], 'b');
    TEST_END();
}

int test_voice_tts_gate_off(void)
{
    TEST_BEGIN();
    voice_ledger_reset();
    dll_record_reset();
    gates(0);
    ASSERT_EQ(voice_tts_start(NULL, 5u, NULL), 0);
    ASSERT_EQ(voice_tts_sample(NULL, NULL), 0);
    ASSERT_EQ(voice_tts_selftest_hold(), 0);
    ASSERT_EQ(voice_tts_running(), 0);
    ASSERT_EQ(voice_ledger_count(VRES_SPVOICE), 0);
    ASSERT_EQ(dll_record_was_requested("sapi.dll"), 0);
    voice_tts_speak(5u, 1, "x", 0);   /* no thread: harmless */
    voice_tts_quit(5u, 100);
    ASSERT_EQ(voice_ledger_underflow(), 0);
    TEST_END();
}

/* Waits up to ms for a WM_VOICE_TTS from `token` of one of the kinds in `mask`
 * (1 << kind); other messages are consumed and freed. Returns a malloc'd
 * message or NULL. */
static VoiceTtsMsg *wait_msg(unsigned token, unsigned mask, DWORD ms)
{
    const DWORD end = GetTickCount() + ms;
    for (;;) {
        MSG m;
        while (PeekMessageW(&m, NULL, WM_VOICE_TTS, WM_VOICE_TTS, PM_REMOVE)) {
            VoiceTtsMsg *vm = (VoiceTtsMsg *)m.lParam;
            if (vm->token == token && (mask & (1u << vm->kind))) return vm;
            free(vm);
        }
        if ((int)(GetTickCount() - end) >= 0) return NULL;
        Sleep(10);
    }
}

static void drain(void)
{
    MSG m;
    while (PeekMessageW(&m, NULL, WM_VOICE_TTS, WM_VOICE_TTS, PM_REMOVE)) free((void *)m.lParam);
}

static HWND tt_window(void)
{
    return CreateWindowExW(0, L"STATIC", L"tts-test", WS_POPUP, 0, 0, 1, 1,
                           NULL, NULL, GetModuleHandleW(NULL), NULL);
}

int test_voice_tts_start_quit_wine(void)
{
    TEST_BEGIN();
    voice_ledger_reset();
    dll_record_reset();
    HWND w = tt_window();
    ASSERT_TRUE(w != NULL);
    gates(1);

    ASSERT_EQ(dll_record_was_requested("sapi.dll"), 0);   /* not before a start */
    ASSERT_EQ(voice_tts_start(w, 9u, NULL), 1);
    ASSERT_EQ(voice_tts_start(w, 9u, NULL), 1);           /* idempotent: still one */
    ASSERT_EQ(voice_ledger_count(VRES_SPVOICE), 1);
    ASSERT_EQ(voice_tts_running(), 1);

    VoiceTtsMsg *m = wait_msg(9u, (1u << VTT_READY) | (1u << VTT_ERROR), 5000);
    ASSERT_TRUE(m != NULL);                                /* a defined outcome, no hang */
    if (m->kind == VTT_ERROR)
        printf("  SKIP: SAPI unavailable here (hr=0x%lx); only the clean teardown is checked\n",
               (unsigned long)m->hr);
    free(m);

    const DWORD t0 = GetTickCount();
    voice_tts_quit(9u, 3000);
    ASSERT_TRUE(GetTickCount() - t0 < 3500);
    ASSERT_EQ(voice_ledger_count(VRES_SPVOICE), 0);
    ASSERT_EQ(voice_tts_running(), 0);
    ASSERT_EQ(dll_record_was_requested("sapi.dll"), 1);
    voice_tts_quit(9u, 100);                               /* idempotent */
    ASSERT_EQ(voice_ledger_underflow(), 0);

    /* the selftest hold: held on success, nothing held on failure */
    if (voice_tts_selftest_hold()) {
        ASSERT_EQ(voice_ledger_count(VRES_SPVOICE), 1);
        voice_tts_selftest_release();
    }
    ASSERT_EQ(voice_ledger_count(VRES_SPVOICE), 0);
    ASSERT_EQ(voice_tts_running(), 0);

    drain();
    gates(0);
    DestroyWindow(w);
    TEST_END();
}

int test_voice_tts_speak_events_wine(void)
{
    TEST_BEGIN();
    voice_ledger_reset();
    HWND w = tt_window();
    ASSERT_TRUE(w != NULL);
    gates(1);
    ASSERT_EQ(voice_tts_start(w, 11u, NULL), 1);
    VoiceTtsMsg *m = wait_msg(11u, (1u << VTT_READY) | (1u << VTT_ERROR), 5000);
    ASSERT_TRUE(m != NULL);
    const int ready = (m->kind == VTT_READY);
    free(m);

    if (!ready) {
        printf("  SKIP: SAPI unavailable here; speak events not exercised\n");
    } else {
        const char *text = "One. Two.";
        voice_tts_volume(11u, VOICE_TTS_DUCK_PERCENT);
        voice_tts_speak(11u, 7, text, 0);
        int words = 0, bad = 0, done = 0, err = 0;
        for (;;) {
            m = wait_msg(11u, (1u << VTT_WORD) | (1u << VTT_DONE) | (1u << VTT_ERROR), 15000);
            if (m == NULL) break;
            if (m->kind == VTT_WORD) {
                words++;
                if (m->reply_id != 7 || m->offset >= strlen(text)) bad++;
            } else if (m->kind == VTT_DONE) {
                done += (m->reply_id == 7);
                free(m);
                break;
            } else {
                err = 1;
                free(m);
                break;
            }
            free(m);
        }
        if (err) {
            printf("  SKIP: SAPI has no usable voice here; speak events not exercised\n");
        } else {
            ASSERT_EQ(done, 1);        /* exactly the DONE of reply 7 */
            ASSERT_EQ(bad, 0);
            (void)words;               /* Wine's engine may emit no word events */

            /* speaking from an offset: absolute offsets, never before `from` */
            voice_tts_speak(11u, 8, text, 5);
            int low = 0;
            for (;;) {
                m = wait_msg(11u, (1u << VTT_WORD) | (1u << VTT_DONE) | (1u << VTT_ERROR), 15000);
                if (m == NULL) break;
                const int last = (m->kind != VTT_WORD);
                if (m->kind == VTT_WORD && m->offset < 5) low++;
                if (m->kind == VTT_DONE) ASSERT_EQ((int)m->reply_id, 8);
                free(m);
                if (last) break;
            }
            ASSERT_EQ(low, 0);
        }
    }

    voice_tts_quit(11u, 3000);
    ASSERT_EQ(voice_ledger_count(VRES_SPVOICE), 0);
    drain();
    gates(0);
    DestroyWindow(w);
    TEST_END();
}

int test_voice_tts_sample_quits_itself(void)
{
    TEST_BEGIN();
    voice_ledger_reset();
    HWND w = tt_window();
    ASSERT_TRUE(w != NULL);
    gates(1);
    ASSERT_EQ(voice_tts_sample(w, NULL), 1);
    VoiceTtsMsg *m = wait_msg(VOICE_TTS_SAMPLE_TOKEN, 1u << VTT_EXITED, 20000);
    ASSERT_TRUE(m != NULL);        /* the sample ends on its own, DONE or ERROR */
    free(m);
    voice_tts_quit(VOICE_TTS_SAMPLE_TOKEN, 3000);   /* the UI reaps on VTT_EXITED */
    ASSERT_EQ(voice_ledger_count(VRES_SPVOICE), 0);
    ASSERT_EQ(voice_tts_running(), 0);
    ASSERT_EQ(voice_ledger_underflow(), 0);
    drain();
    gates(0);
    DestroyWindow(w);
    TEST_END();
}

int test_voice_voices_read_long_names(void)
{
    TEST_BEGIN();
    RegDeleteTreeW(HKEY_CURRENT_USER, TT_ROOT);
    /* a subkey name longer than the 127-char read buffer, sorting before a valid one */
    wchar_t longsub[201];
    for (int i = 0; i < 200; i++) longsub[i] = L'A';
    longsub[200] = L'\0';
    ASSERT_TRUE(make_token(L"Speech\\Voices\\Tokens", longsub, L"Long One", NULL, L"Female", L"809"));
    ASSERT_TRUE(make_token(L"Speech\\Voices\\Tokens", L"TTS_Z", L"Test Zed", NULL, L"Female", L"809"));
    VoiceInfo v[4];
    memset(v, 0, sizeof(v));
    size_t n = voice_voices_read(HKEY_CURRENT_USER, TT_ROOT L"\\Speech\\Voices\\Tokens", NULL, v, 4);
    int zed = 0;
    for (size_t i = 0; i < n; i++) if (strstr(v[i].id, "\\TTS_Z")) zed = 1;
    ASSERT_EQ(zed, 1);   /* the long key is skipped, enumeration goes on */

    /* a tokens path whose UTF-8 form does not fit the id: nothing listed, no wrong id */
    wchar_t deep[400];
    wchar_t pad[231];
    for (int i = 0; i < 230; i++) pad[i] = L'p';
    pad[230] = L'\0';
    swprintf(deep, 400, L"%ls\\%ls\\Tokens", TT_ROOT, pad);
    wchar_t keyp[500];
    swprintf(keyp, 500, L"%ls\\X", deep);
    HKEY k;
    ASSERT_TRUE(RegCreateKeyExW(HKEY_CURRENT_USER, keyp, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL) == ERROR_SUCCESS);
    RegCloseKey(k);
    ASSERT_EQ((int)voice_voices_read(HKEY_CURRENT_USER, deep, NULL, v, 4), 0);

    RegDeleteTreeW(HKEY_CURRENT_USER, TT_ROOT);
    RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\NutshellTest");
    TEST_END();
}

int test_voice_tts_exited_context_restarts(void)
{
    TEST_BEGIN();
    voice_ledger_reset();
    HWND w = tt_window();
    ASSERT_TRUE(w != NULL);
    gates(1);
    ASSERT_EQ(voice_tts_start(w, 21u, NULL), 1);
    VoiceTtsMsg *m = wait_msg(21u, 1u << VTT_EXITED | 1u << VTT_READY, 5000);
    ASSERT_TRUE(m != NULL);
    const int exited = (m->kind == VTT_EXITED);
    free(m);
    if (!exited) {
        printf("  SKIP: SAPI is present: no start-time exit to reuse\n");
        voice_tts_quit(21u, 3000);
    } else {
        /* exited, not reaped: a speak must not vanish silently */
        voice_tts_speak(21u, 5, "Hi there.", 0);
        m = wait_msg(21u, 1u << VTT_ERROR | 1u << VTT_DONE, 2000);
        ASSERT_TRUE(m != NULL);
        ASSERT_EQ((int)m->reply_id, 5);
        free(m);
        /* a start over the exited, unreaped token restarts it: one ledger count, a new outcome */
        drain();
        ASSERT_EQ(voice_tts_start(w, 21u, NULL), 1);
        ASSERT_EQ(voice_ledger_count(VRES_SPVOICE), 1);
        m = wait_msg(21u, 1u << VTT_READY | 1u << VTT_ERROR, 5000);
        ASSERT_TRUE(m != NULL);
        free(m);
        voice_tts_quit(21u, 3000);
    }
    ASSERT_EQ(voice_ledger_count(VRES_SPVOICE), 0);
    ASSERT_EQ(voice_ledger_underflow(), 0);

    /* a stopped Play sample ends and reports it */
    ASSERT_EQ(voice_tts_sample(w, NULL), 1);
    voice_tts_stop(VOICE_TTS_SAMPLE_TOKEN);
    m = wait_msg(VOICE_TTS_SAMPLE_TOKEN, 1u << VTT_EXITED, 5000);
    ASSERT_TRUE(m != NULL);
    free(m);
    voice_tts_quit(VOICE_TTS_SAMPLE_TOKEN, 3000);
    ASSERT_EQ(voice_ledger_count(VRES_SPVOICE), 0);
    drain();
    gates(0);
    DestroyWindow(w);
    TEST_END();
}

int test_voice_tts_reap_by_generation(void)
{
    TEST_BEGIN();
    voice_ledger_reset();
    HWND w = tt_window();
    ASSERT_TRUE(w != NULL);
    gates(1);
    ASSERT_EQ(voice_tts_gen(31u), 0u);                     /* none held: 0 */
    ASSERT_EQ(voice_tts_start(w, 31u, NULL), 1);
    const unsigned held_gen = voice_tts_gen(31u);
    ASSERT_TRUE(held_gen != 0u);
    ASSERT_EQ(voice_tts_gen(32u), 0u);                     /* another token: 0 */
    VoiceTtsMsg *m = wait_msg(31u, 1u << VTT_EXITED | 1u << VTT_READY, 5000);
    ASSERT_TRUE(m != NULL);
    const int exited = (m->kind == VTT_EXITED);
    const unsigned gen1 = m->gen;
    ASSERT_EQ(gen1, held_gen);                             /* the getter is the message's gen */
    free(m);
    if (!exited) {
        printf("  SKIP: SAPI is present: no start-time exit to restart over\n");
        voice_tts_quit(31u, 3000);
    } else {
        ASSERT_EQ(voice_tts_start(w, 31u, NULL), 1);       /* reap and restart */
        m = wait_msg(31u, 1u << VTT_EXITED, 5000);
        ASSERT_TRUE(m != NULL);
        const unsigned gen2 = m->gen;
        free(m);
        ASSERT_TRUE(gen1 != gen2);
        voice_tts_reap(31u, gen1);                         /* the stale one: a no-op */
        ASSERT_EQ(voice_tts_running(), 1);
        ASSERT_EQ(voice_ledger_count(VRES_SPVOICE), 1);
        voice_tts_reap(31u, gen2);                         /* the current one: freed */
        ASSERT_EQ(voice_tts_running(), 0);
        ASSERT_EQ(voice_ledger_count(VRES_SPVOICE), 0);
        voice_tts_reap(31u, gen2);                         /* idempotent */
        ASSERT_EQ(voice_ledger_underflow(), 0);
    }
    drain();
    gates(0);
    DestroyWindow(w);
    TEST_END();
}

#endif /* _WIN32 */
