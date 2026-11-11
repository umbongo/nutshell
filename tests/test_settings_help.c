#include "test_framework.h"
#include "settings_help.h"
#include "settings_layout.h"
#include "settings_debug.h"
#include "settings_entry.h"
#include "search_providers.h"
#include "cmd_policy.h"
#include "ai_prompt.h"
#include "tool_decision.h"
#include "app_font.h"
#include "config.h"
#include "dlog_types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- local helpers ---------------------------------------------------- */

/* Writes v with thousands separators: 10080 -> "10,080". */
static void fmt_thousands(int v, char *buf, size_t n)
{
    char raw[32];
    (void)snprintf(raw, sizeof(raw), "%d", v);
    size_t len = strlen(raw);
    size_t o = 0;
    for (size_t i = 0; i < len && o + 2 < n; i++) {
        if (i > 0 && (len - i) % 3 == 0) buf[o++] = ',';
        buf[o++] = raw[i];
    }
    buf[o] = '\0';
}

/* The text after the first "\n\n", up to the "\nDefault: " line when there is
 * one. Returns a pointer into a static buffer; valid until the next call. */
static const char *body_of(const SettingsHelp *e)
{
    static char buf[SETTINGS_HELP_TEXT_MAX + 8];
    const char *p = strstr(e->text, "\n\n");
    if (p == NULL) { buf[0] = '\0'; return buf; }
    p += 2;
    const char *d = strstr(p, "\nDefault: ");
    size_t len = d != NULL ? (size_t)(d - p) : strlen(p);
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    memcpy(buf, p, len);
    buf[len] = '\0';
    return buf;
}

static const char *heading_of(const SettingsHelp *e)
{
    return e->title != NULL ? e->title : e->label;
}

/* Debugging level rows, in SH_DBG_LEVEL_RENDER..GENERAL order. */
static const int k_level_cat[6] = {
    DLOG_RENDER, DLOG_TERM, DLOG_AI, DLOG_SSH, DLOG_SHELL, DLOG_GENERAL
};

static int level_index(int id)
{
    return id - (int)SH_DBG_LEVEL_RENDER;
}

static int is_level_id(int id)
{
    return id >= (int)SH_DBG_LEVEL_RENDER && id <= (int)SH_DBG_LEVEL_GENERAL;
}

static int count_substr(const char *s, const char *sub)
{
    int c = 0;
    size_t n = strlen(sub);
    for (const char *p = strstr(s, sub); p != NULL; p = strstr(p + n, sub)) c++;
    return c;
}

/* ---- tests ------------------------------------------------------------ */

#define N(x) { x, #x }
static const struct { SettingsHelpId id; const char *name; } k_names[] = {
    N(SH_COLOUR_SCHEME), N(SH_TERMINAL_FONT), N(SH_FONT_SIZE), N(SH_AI_FONT),
    N(SH_SCROLLBACK), N(SH_PASTE_DELAY), N(SH_PASTE_CONFIRM),
    N(SH_LOG_DIR), N(SH_LOG_FORMAT),
    N(SH_SSH_IDLE),
    N(SH_SESSION_MGR_STARTUP), N(SH_AUTOCONNECT), N(SH_AUTOCONNECT_SESSION),
    N(SH_AI_PROVIDER), N(SH_AI_KEY), N(SH_AI_MODEL), N(SH_AI_REFRESH), N(SH_AI_BASE_URL),
    N(SH_AI_CONTEXT_LINES), N(SH_AI_SYSTEM_NOTES), N(SH_AI_MARKDOWN),
    N(SH_AI_POLICY_ALLOWED), N(SH_AI_POLICY_AUTO), N(SH_AI_TOOL_TIMEOUT), N(SH_AI_TOOL_ROUNDS),
    N(SH_AI_WEB_FETCH), N(SH_AI_SEARCH), N(SH_AI_SEARCH_URL), N(SH_AI_MAX_RESULTS),
    N(SH_DBG_ENABLE), N(SH_DBG_LEVEL_RENDER), N(SH_DBG_LEVEL_TERM), N(SH_DBG_LEVEL_AI),
    N(SH_DBG_LEVEL_SSH), N(SH_DBG_LEVEL_SHELL), N(SH_DBG_LEVEL_GENERAL),
    N(SH_DBG_RECORDER), N(SH_DBG_SCREENSHOT), N(SH_DBG_CONTENT), N(SH_DBG_FOLDER),
    N(SH_DBG_BROWSE), N(SH_DBG_OPEN), N(SH_DBG_MAX_FILE), N(SH_DBG_MAX_TOTAL), N(SH_DBG_USAGE),
    N(SH_DBG_CURRENT)
};
#undef N

int test_settings_help_table_matches_enum(void) {
    TEST_BEGIN();
    size_t n = 0;
    const SettingsHelp *t = settings_help_table(&n);
    ASSERT_NOT_NULL(t);
    ASSERT_EQ(n, (size_t)SH_COUNT);
    ASSERT_EQ(SH_COUNT, 46);
    ASSERT_EQ(sizeof(k_names) / sizeof(k_names[0]), (size_t)SH_COUNT);
    ASSERT_NOT_NULL(settings_help_table(NULL));
    if (t != NULL && n == (size_t)SH_COUNT) {
        for (size_t i = 0; i < n; i++) {
            ASSERT_EQ(t[i].id, (SettingsHelpId)i);
            ASSERT_NOT_NULL(t[i].name);
            if (t[i].name != NULL) ASSERT_STR_EQ(t[i].name, k_names[i].name);
            ASSERT_EQ(k_names[i].id, (SettingsHelpId)i);
        }
    }
    TEST_END();
}

int test_settings_help_get_out_of_range(void) {
    TEST_BEGIN();
    const SettingsHelp *t = settings_help_table(NULL);
    ASSERT_NULL(settings_help_get((int)SH_NONE));
    ASSERT_NULL(settings_help_get((int)SH_COUNT));
    ASSERT_NULL(settings_help_get(-5));
    ASSERT_TRUE(settings_help_get((int)SH_DBG_CURRENT) == &t[SH_DBG_CURRENT]);
    ASSERT_TRUE(settings_help_get((int)SH_COLOUR_SCHEME) == &t[SH_COLOUR_SCHEME]);
    TEST_END();
}

int test_settings_help_labels_and_pages(void) {
    TEST_BEGIN();
    size_t n = 0;
    const SettingsHelp *t = settings_help_table(&n);
    int per_page[SETTINGS_PAGE_COUNT];
    for (int p = 0; p < SETTINGS_PAGE_COUNT; p++) per_page[p] = 0;
    int prev_nav = -1;
    for (size_t i = 0; i < n; i++) {
        ASSERT_NOT_NULL(t[i].label);
        if (t[i].label == NULL) continue;
        ASSERT_TRUE(t[i].label[0] != '\0');
        ASSERT_TRUE(t[i].page >= SETTINGS_PAGE_APPEARANCE && t[i].page < SETTINGS_PAGE_COUNT);
        if (t[i].page < SETTINGS_PAGE_APPEARANCE || t[i].page >= SETTINGS_PAGE_COUNT) continue;
        per_page[t[i].page]++;
        for (size_t j = 0; j < i; j++) {
            if (t[j].page == t[i].page && t[j].label != NULL
                && strcmp(t[j].label, t[i].label) == 0) {
                printf("  duplicate label on page %d: \"%s\"\n", t[i].page, t[i].label);
                ASSERT_TRUE(0);
            }
        }
        int nav = settings_nav_index_of_page(t[i].page);
        ASSERT_TRUE(nav >= 0);
        ASSERT_TRUE(nav >= prev_nav);
        prev_nav = nav;
    }
    for (int p = SETTINGS_PAGE_APPEARANCE; p <= SETTINGS_PAGE_DEBUG; p++) {
        if (per_page[p] < 1) printf("  page %d has no entry\n", p);
        ASSERT_TRUE(per_page[p] >= 1);
    }
    ASSERT_EQ(per_page[SETTINGS_PAGE_ABOUT], 0);
    TEST_END();
}

int test_settings_help_text_shape(void) {
    TEST_BEGIN();
    size_t n = 0;
    const SettingsHelp *t = settings_help_table(&n);
    for (size_t i = 0; i < n; i++) {
        const SettingsHelp *e = &t[i];
        if (e->text == NULL) { ASSERT_NOT_NULL(e->text); continue; }
        int ok = 1;
        const char *h = heading_of(e);
        size_t hl = strlen(h);
        if (strncmp(e->text, h, hl) != 0 || e->text[hl] != '\n' || e->text[hl + 1] != '\n') {
            printf("  %s: first line is not the heading\n", e->name);
            ok = 0;
        }
        size_t len = strlen(e->text);
        if (len > SETTINGS_HELP_TEXT_MAX) {
            printf("  %s: %zu bytes, over %d\n", e->name, len, SETTINGS_HELP_TEXT_MAX);
            ok = 0;
        }
        const char *body = body_of(e);
        if (strlen(body) < 100) { printf("  %s: body under 100 bytes\n", e->name); ok = 0; }
        for (size_t k = 0; k < len; k++) {
            unsigned char c = (unsigned char)e->text[k];
            if (!((c >= 0x20 && c <= 0x7E) || c == '\n')) {
                printf("  %s: byte 0x%02X at %zu\n", e->name, c, k);
                ok = 0;
                break;
            }
        }
        if (strstr(e->text, "  ") != NULL) { printf("  %s: double space\n", e->name); ok = 0; }
        if (strstr(e->text, " \n") != NULL) { printf("  %s: trailing space\n", e->name); ok = 0; }
        if (len == 0 || e->text[len - 1] != '.') { printf("  %s: no final period\n", e->name); ok = 0; }
        if (e->kind == SH_KIND_SETTING) {
            int sentences = count_substr(body, ". ") + 1;
            if (sentences < 2 || sentences > 6) {
                printf("  %s: %d sentences\n", e->name, sentences);
                ok = 0;
            }
            if (e->default_text == NULL) { printf("  %s: no default_text\n", e->name); ok = 0; }
        } else {
            if (e->default_text != NULL) { printf("  %s: default_text on non-setting\n", e->name); ok = 0; }
            if (strstr(e->text, "\nDefault: ") != NULL) { printf("  %s: Default line on non-setting\n", e->name); ok = 0; }
        }
        ASSERT_TRUE(ok);
    }
    TEST_END();
}

/* The default of each setting, rendered from a Settings the way the table
 * writes it. Returns 0 for an id the test does not know. */
static int render_default(const Settings *s, int id, char *out, size_t n)
{
    const char *str = NULL;
    switch (id) {
    case SH_COLOUR_SCHEME:        str = s->colour_scheme; break;
    case SH_TERMINAL_FONT:        str = s->font; break;
    case SH_AI_FONT:              str = s->ai_font; break;
    case SH_LOG_DIR:              str = s->log_dir; break;
    case SH_LOG_FORMAT:           str = s->log_format; break;
    case SH_AUTOCONNECT_SESSION:  str = s->auto_connect_session; break;
    case SH_AI_PROVIDER:          str = s->ai_provider; break;
    case SH_AI_KEY:               str = s->ai_api_key; break;
    case SH_AI_MODEL:             str = s->ai_custom_model; break;
    case SH_AI_BASE_URL:          str = s->ai_custom_url; break;
    case SH_AI_SYSTEM_NOTES:      str = s->ai_system_notes; break;
    case SH_AI_SEARCH_URL:        str = s->ai_search_url; break;
    case SH_DBG_FOLDER:           str = s->debug_log_dir; break;
    default: break;
    }
    if (str != NULL) {
        (void)snprintf(out, n, "%s", str[0] != '\0' ? str : "empty");
        return 1;
    }
    int num = 0, thousands = 0, isnum = 1;
    switch (id) {
    case SH_SCROLLBACK:        num = s->scrollback_lines; thousands = 1; break;
    case SH_PASTE_DELAY:       num = s->paste_delay_ms; thousands = 1; break;
    case SH_SSH_IDLE:          num = s->ssh_user_idle_timeout_mins; thousands = 1; break;
    case SH_AI_CONTEXT_LINES:  num = s->ai_max_context_lines; thousands = 1; break;
    case SH_AI_TOOL_TIMEOUT:   num = s->ai_tool_timeout_sec; thousands = 1; break;
    case SH_DBG_MAX_TOTAL:     num = s->debug_max_total_mb; thousands = 1; break;
    case SH_FONT_SIZE:         num = s->font_size; break;
    case SH_AI_TOOL_ROUNDS:    num = s->ai_tool_auto_rounds; break;
    case SH_AI_MAX_RESULTS:    num = s->ai_max_search_results; break;
    case SH_DBG_MAX_FILE:      num = s->debug_max_file_mb; break;
    default: isnum = 0; break;
    }
    if (isnum) {
        if (thousands) fmt_thousands(num, out, n);
        else (void)snprintf(out, n, "%d", num);
        return 1;
    }
    int flag = -1;
    switch (id) {
    case SH_PASTE_CONFIRM:        flag = s->paste_confirm; break;
    case SH_SESSION_MGR_STARTUP:  flag = s->open_session_manager_at_start; break;
    case SH_AUTOCONNECT:          flag = s->auto_connect; break;
    case SH_AI_MARKDOWN:          flag = s->markdown_render_enabled; break;
    case SH_AI_POLICY_AUTO:       flag = cmd_policy_auto_on(s->ai_policy_default); break;
    case SH_AI_WEB_FETCH:         flag = s->ai_web_fetch_enabled; break;
    case SH_DBG_ENABLE:           flag = s->debug_enabled; break;
    case SH_DBG_SCREENSHOT:       flag = s->debug_screenshot; break;
    case SH_DBG_CONTENT:          flag = s->debug_include_content; break;
    default: break;
    }
    if (flag >= 0) {
        (void)snprintf(out, n, "%s", flag ? "on" : "off");
        return 1;
    }
    if (id == SH_AI_POLICY_ALLOWED) {
        (void)snprintf(out, n, "%s", cmd_policy_stop_label(s->ai_policy_default.allowed));
        return 1;
    }
    if (id == SH_AI_SEARCH) {
        (void)snprintf(out, n, "%s",
                       search_provider_label(search_provider_index(s->ai_search_provider)));
        return 1;
    }
    if (is_level_id(id)) {
        (void)snprintf(out, n, "%s",
                       settings_debug_level_label(s->debug_levels[k_level_cat[level_index(id)]]));
        return 1;
    }
    if (id == SH_DBG_RECORDER) {
        (void)snprintf(out, n, "%s, at %s", s->debug_recorder ? "on" : "off",
                       settings_debug_level_label(s->debug_recorder_level));
        return 1;
    }
    return 0;
}

int test_settings_help_defaults_match_config(void) {
    TEST_BEGIN();
    Settings s;
    config_default_settings(&s);
    size_t n = 0;
    const SettingsHelp *t = settings_help_table(&n);
    for (size_t i = 0; i < n; i++) {
        const SettingsHelp *e = &t[i];
        if (e->kind != SH_KIND_SETTING) continue;
        if (e->text == NULL || e->default_text == NULL) { ASSERT_TRUE(0); continue; }
        /* The text ends with "\nDefault: <default_text>." */
        char tail[512];
        (void)snprintf(tail, sizeof(tail), "\nDefault: %s.", e->default_text);
        size_t tl = strlen(tail), xl = strlen(e->text);
        if (xl < tl || strcmp(e->text + xl - tl, tail) != 0) {
            printf("  %s: text does not end with the default line\n", e->name);
            ASSERT_TRUE(0);
        }
        char want[256];
        if (!render_default(&s, (int)e->id, want, sizeof(want))) {
            printf("  %s: the test has no renderer for this setting\n", e->name);
            ASSERT_TRUE(0);
            continue;
        }
        size_t wl = strlen(want);
        if (strncmp(e->default_text, want, wl) != 0
            || (e->default_text[wl] != '\0' && strncmp(e->default_text + wl, " (", 2) != 0)) {
            printf("  %s: default_text \"%s\" does not start with \"%s\"\n",
                   e->name, e->default_text, want);
            ASSERT_TRUE(0);
        }
    }
    TEST_END();
}

int test_settings_help_model_names_default(void) {
    TEST_BEGIN();
    const SettingsHelp *e = settings_help_get((int)SH_AI_MODEL);
    ASSERT_NOT_NULL(e);
    if (e != NULL) ASSERT_TRUE(strstr(e->text, ai_provider_model("anthropic")) != NULL);
    TEST_END();
}

int test_settings_help_ranges_in_text(void) {
    TEST_BEGIN();
    size_t n = 0;
    const SettingsHelp *t = settings_help_table(&n);
    for (size_t i = 0; i < n; i++) {
        const SettingsHelp *e = &t[i];
        if (e->max <= 0) continue;
        char lo[32], hi[32], want[80];
        if (e->flags & SH_F_THOUSANDS) {
            fmt_thousands(e->min, lo, sizeof(lo));
            fmt_thousands(e->max, hi, sizeof(hi));
        } else {
            (void)snprintf(lo, sizeof(lo), "%d", e->min);
            (void)snprintf(hi, sizeof(hi), "%d", e->max);
        }
        (void)snprintf(want, sizeof(want), "%s to %s", lo, hi);
        if (strstr(body_of(e), want) == NULL) {
            printf("  %s: body lacks \"%s\"\n", e->name, want);
            ASSERT_TRUE(0);
        }
    }
    /* Pin the ranges to the constants the code enforces. */
    const SettingsHelp *e = settings_help_get((int)SH_AI_TOOL_TIMEOUT);
    ASSERT_EQ(e->min, AI_TOOL_TIMEOUT_SEC_MIN);
    ASSERT_EQ(e->max, AI_TOOL_TIMEOUT_SEC_MAX);
    e = settings_help_get((int)SH_AI_TOOL_ROUNDS);
    ASSERT_EQ(e->min, AI_TOOL_ROUNDS_MIN);
    ASSERT_EQ(e->max, AI_TOOL_ROUNDS_MAX);
    e = settings_help_get((int)SH_AI_CONTEXT_LINES);
    ASSERT_EQ(e->max, AI_CONTEXT_LINES_MAX);
    e = settings_help_get((int)SH_DBG_MAX_FILE);
    ASSERT_EQ(e->min, SETTINGS_DEBUG_FILE_MB_MIN);
    ASSERT_EQ(e->max, SETTINGS_DEBUG_FILE_MB_MAX);
    e = settings_help_get((int)SH_DBG_MAX_TOTAL);
    ASSERT_EQ(e->min, SETTINGS_DEBUG_TOTAL_MB_MIN);
    ASSERT_EQ(e->max, SETTINGS_DEBUG_TOTAL_MB_MAX);
    TEST_END();
}

/* The table's ranges for the settings whose limits settings_validate() holds
 * as bare literals: feeding the table's boundary values through it pins the
 * two together. In-range values are kept, values one outside are clamped. */
int test_settings_help_ranges_match_validate(void) {
    TEST_BEGIN();
    const SettingsHelp *sb = settings_help_get((int)SH_SCROLLBACK);
    ASSERT_NOT_NULL(sb);
    if (sb != NULL) {
        ASSERT_EQ(sb->min, SETTINGS_SCROLLBACK_MIN);
        ASSERT_EQ(sb->max, SETTINGS_SCROLLBACK_MAX);
    }
    const SettingsHelp *idle = settings_help_get((int)SH_SSH_IDLE);
    const SettingsHelp *res = settings_help_get((int)SH_AI_MAX_RESULTS);
    ASSERT_NOT_NULL(idle);
    ASSERT_NOT_NULL(res);
    if (idle == NULL || res == NULL) { TEST_END(); }
    Settings s;
    /* Idle Timeout */
    config_default_settings(&s);
    s.ssh_user_idle_timeout_mins = idle->min;
    settings_validate(&s);
    ASSERT_EQ(s.ssh_user_idle_timeout_mins, idle->min);
    s.ssh_user_idle_timeout_mins = idle->max;
    settings_validate(&s);
    ASSERT_EQ(s.ssh_user_idle_timeout_mins, idle->max);
    s.ssh_user_idle_timeout_mins = idle->min - 1;
    settings_validate(&s);
    ASSERT_EQ(s.ssh_user_idle_timeout_mins, idle->min);
    s.ssh_user_idle_timeout_mins = idle->max + 1;
    settings_validate(&s);
    ASSERT_EQ(s.ssh_user_idle_timeout_mins, idle->max);
    /* Max Results */
    s.ai_max_search_results = res->min;
    settings_validate(&s);
    ASSERT_EQ(s.ai_max_search_results, res->min);
    s.ai_max_search_results = res->max;
    settings_validate(&s);
    ASSERT_EQ(s.ai_max_search_results, res->max);
    s.ai_max_search_results = res->min - 1;
    settings_validate(&s);
    ASSERT_EQ(s.ai_max_search_results, res->min);
    s.ai_max_search_results = res->max + 1;
    settings_validate(&s);
    ASSERT_EQ(s.ai_max_search_results, res->max);
    TEST_END();
}

int test_settings_help_keep_entries(void) {
    TEST_BEGIN();
    size_t n = 0;
    const SettingsHelp *t = settings_help_table(&n);
    for (size_t i = 0; i < n; i++) {
        const SettingsHelp *e = &t[i];
        int expect = e->id == SH_SSH_IDLE || e->id == SH_AI_TOOL_TIMEOUT
                  || e->id == SH_AI_TOOL_ROUNDS || e->id == SH_AI_MAX_RESULTS;
        int has = (e->flags & SH_F_KEEP_OUT_OF_RANGE) != 0;
        if (has != expect) printf("  %s: KEEP_OUT_OF_RANGE flag is %d\n", e->name, has);
        ASSERT_EQ(has, expect);
        if (!has) continue;
        ASSERT_TRUE(strstr(body_of(e), "not saved") != NULL);
        int zero_ok = (e->flags & SH_F_ZERO_MEANS_NEVER) != 0;
        int mid = (e->min + e->max) / 2;
        const int cur = 4242;
        ASSERT_EQ(settings_entry_or_keep(1, (unsigned)e->min, e->min, e->max, zero_ok, cur), e->min);
        ASSERT_EQ(settings_entry_or_keep(1, (unsigned)e->max, e->min, e->max, zero_ok, cur), e->max);
        ASSERT_EQ(settings_entry_or_keep(1, (unsigned)mid, e->min, e->max, zero_ok, cur), mid);
        ASSERT_EQ(settings_entry_or_keep(1, (unsigned)e->max + 1u, e->min, e->max, zero_ok, cur), cur);
        if (e->min > 0)
            ASSERT_EQ(settings_entry_or_keep(1, (unsigned)e->min - 1u, e->min, e->max, zero_ok, cur), cur);
        ASSERT_EQ(settings_entry_or_keep(0, (unsigned)mid, e->min, e->max, zero_ok, cur), cur);
    }
    TEST_END();
}

int test_settings_help_font_sizes_and_notes(void) {
    TEST_BEGIN();
    char list[96] = "";
    size_t used = 0;
    for (int i = 0; i < APP_FONT_NUM_SIZES; i++) {
        const char *sep = i == 0 ? "" : (i == APP_FONT_NUM_SIZES - 1 ? " or " : ", ");
        int w = snprintf(list + used, sizeof(list) - used, "%s%d", sep, k_app_font_sizes[i]);
        if (w < 0 || (size_t)w >= sizeof(list) - used) break;
        used += (size_t)w;
    }
    ASSERT_STR_EQ(list, "6, 8, 10, 12, 14, 16, 18 or 20");
    const SettingsHelp *e = settings_help_get((int)SH_FONT_SIZE);
    ASSERT_NOT_NULL(e);
    if (e != NULL) ASSERT_TRUE(strstr(body_of(e), list) != NULL);
    e = settings_help_get((int)SH_AI_SYSTEM_NOTES);
    ASSERT_NOT_NULL(e);
    char notes[32];
    fmt_thousands((int)AI_NOTES_MAX - 1, notes, sizeof(notes));
    ASSERT_STR_EQ(notes, "2,559");
    if (e != NULL) ASSERT_TRUE(strstr(body_of(e), notes) != NULL);
    TEST_END();
}

int test_settings_help_labels_match_sources(void) {
    TEST_BEGIN();
    for (int k = 0; k < 6; k++) {
        const SettingsHelp *e = settings_help_get((int)SH_DBG_LEVEL_RENDER + k);
        ASSERT_NOT_NULL(e);
        if (e == NULL) continue;
        char want[64];
        (void)snprintf(want, sizeof(want), "%s:", settings_debug_cat_label(k_level_cat[k]));
        ASSERT_STR_EQ(e->label, want);
        for (int l = 0; l < DLOG_LVL_COUNT; l++) {
            const char *name = settings_debug_level_label(l);
            if (strstr(e->text, name) == NULL)
                printf("  %s: text lacks level \"%s\"\n", e->name, name);
            ASSERT_TRUE(strstr(e->text, name) != NULL);
        }
    }
    const SettingsHelp *e = settings_help_get((int)SH_AI_POLICY_ALLOWED);
    ASSERT_NOT_NULL(e);
    if (e != NULL)
        for (int i = 0; i < POLICY_STOP_COUNT; i++) {
            if (strstr(e->text, cmd_policy_stop_label(i)) == NULL)
                printf("  Allowed text lacks \"%s\"\n", cmd_policy_stop_label(i));
            ASSERT_TRUE(strstr(e->text, cmd_policy_stop_label(i)) != NULL);
        }
    e = settings_help_get((int)SH_AI_SEARCH);
    ASSERT_NOT_NULL(e);
    if (e != NULL)
        for (int i = 0; i < search_provider_count(); i++) {
            if (strstr(e->text, search_provider_label(i)) == NULL)
                printf("  Search text lacks \"%s\"\n", search_provider_label(i));
            ASSERT_TRUE(strstr(e->text, search_provider_label(i)) != NULL);
        }
    TEST_END();
}

/* ---- source scan ------------------------------------------------------- */

/* Whole file, as bytes; NULL when it cannot be read. */
static char *read_whole(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long len = ftell(f);
    if (len < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    char *buf = (char *)malloc((size_t)len + 1u);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1u, (size_t)len, f);
    fclose(f);
    buf[got] = '\0';
    return buf;
}

static int is_ident_char(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '_';
}

/* name occurs in src as a whole identifier (SH_AI_SEARCH must not match
 * inside SH_AI_SEARCH_URL, nor FOO_SH_AI_SEARCH). */
static int has_identifier(const char *src, const char *name)
{
    size_t n = strlen(name);
    for (const char *p = strstr(src, name); p != NULL; p = strstr(p + 1, name)) {
        int before = p == src ? 0 : is_ident_char(p[-1]);
        int after = is_ident_char(p[n]);
        if (!before && !after) return 1;
    }
    return 0;
}

/* Every help id is named in src/ui/settings.c, so every control is created
 * with its id. make test runs from the repository root; run from elsewhere the
 * source is not there and the check skips -- except under CI, where it fails. */
int test_settings_help_every_id_in_settings_c(void) {
    TEST_BEGIN();
    char *src = read_whole("src/ui/settings.c");
    if (!src) {
        const char *ci = getenv("CI");
        if (ci && ci[0]) {
            printf("  src/ui/settings.c not readable from the working directory under CI\n");
            ASSERT_TRUE(0);
        } else {
            printf("  [settings_help] src/ui/settings.c not readable from cwd -- skipping\n");
        }
        TEST_END();
    }
    size_t count = 0;
    const SettingsHelp *tab = settings_help_table(&count);
    ASSERT_NOT_NULL(tab);
    for (size_t i = 0; tab != NULL && i < count; i++) {
        int found = has_identifier(src, tab[i].name);
        if (!found) printf("  src/ui/settings.c never names %s\n", tab[i].name);
        ASSERT_TRUE(found);
    }
    free(src);
    TEST_END();
}
