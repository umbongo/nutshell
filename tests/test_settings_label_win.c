/* tests/test_settings_label_win.c -- Win32 test that every Settings label
 * which sits in the right-aligned label column fits it on one line, with
 * 10% to spare. A STATIC
 * wraps rather than clips, so a label wider than settings_metrics.label_w
 * shows its tail cut off (150% scaling showed "Commands as tool" with
 * "calls:" lost). Built only by `make wintest`. */

#ifdef _WIN32

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "test_framework.h"
#include "settings_help.h"
#include "settings_layout.h"
#include "ns_font.h"

/* The ids src/ui/settings.c gives to make_label2() (the debug level labels
 * go through an array there and are all short). */
static const int k_label_ids[] = {
    SH_AI_RUNCMD_MODE, SH_SSH_IDLE, SH_AI_SYSTEM_NOTES, SH_DBG_MAX_FILE,
    SH_AI_CONTEXT_LINES, SH_DBG_MAX_TOTAL, SH_PASTE_DELAY, SH_DBG_CURRENT,
    SH_SCROLLBACK, SH_LOG_FORMAT, SH_AI_FONT, SH_AI_SEARCH, SH_LOG_DIR,
    SH_TERMINAL_FONT, SH_COLOUR_SCHEME, SH_AI_MAX_RESULTS, SH_DBG_FOLDER,
    SH_AI_SEARCH_URL, SH_FONT_SIZE, SH_AI_BASE_URL, SH_AI_PROVIDER,
    SH_AI_POLICY_ALLOWED, SH_AUTOCONNECT_SESSION, SH_AI_KEY, SH_AI_MODEL
};

int test_settings_label_win_labels_fit_column(void)
{
    TEST_BEGIN();
    static const int dpis[] = {96, 120, 144, 192};
    HDC dc = GetDC(NULL);
    ASSERT_NOT_NULL(dc);
    for (size_t d = 0; d < sizeof(dpis) / sizeof(dpis[0]); d++) {
        SettingsMetrics m;
        settings_metrics_init(&m, dpis[d]);
        HGDIOBJ old = SelectObject(dc, ns_font(FONT_BODY, dpis[d]));
        for (size_t i = 0; i < sizeof(k_label_ids) / sizeof(k_label_ids[0]); i++) {
            const char *txt = settings_help_get(k_label_ids[i])->label;
            SIZE sz;
            ASSERT_TRUE(GetTextExtentPoint32A(dc, txt, (int)strlen(txt), &sz));
            /* Wine substitutes a narrower face than Segoe UI, so require
             * 10% headroom rather than a bare fit. */
            if ((int)sz.cx * 100 > m.label_w * 90)
                printf("  label '%s' is %d px at %d dpi, column %d px\n",
                       txt, (int)sz.cx, dpis[d], m.label_w);
            ASSERT_TRUE((int)sz.cx * 100 <= m.label_w * 90);
        }
        SelectObject(dc, old);
    }
    ReleaseDC(NULL, dc);
    TEST_END();
}

#endif /* _WIN32 */
