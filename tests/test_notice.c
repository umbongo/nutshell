#include "test_framework.h"
#include "notice.h"
#include "settings_layout.h"
#include "settings_help.h"
#include <string.h>

static unsigned char key_index(const char *key)
{
    size_t n = 0;
    const ConfigSettingKey *k = config_setting_keys(&n);
    for (size_t i = 0; i < n; i++)
        if (strcmp(k[i].key, key) == 0) return (unsigned char)i;
    return 255;
}

static void rep_init(ConfigLoadReport *r, ConfigLoadStatus st)
{
    memset(r, 0, sizeof(*r));
    r->status = st;
}

static void rep_add(ConfigLoadReport *r, const char *key)
{
    r->missing[r->missing_count++] = key_index(key);
}

int test_notice_warning_copy(void)
{
    TEST_BEGIN();
    Notice n;
    notice_no_folder(&n);
    ASSERT_EQ(n.tone, NOTICE_WARNING);
    ASSERT_STR_EQ(n.title, "Settings can't be saved this run");
    ASSERT_STR_EQ(n.opening,
        "Nutshell couldn't find a folder for nutshell.config: neither its own "
        "folder nor %LOCALAPPDATA% is available. Settings and sessions won't "
        "be saved until it is restarted with one available.");
    ASSERT_EQ(n.item_count, 0);
    ASSERT_STR_EQ(n.action, "");
    ASSERT_EQ(n.action_page, -1);

    notice_unreadable(&n);
    ASSERT_STR_EQ(n.title, "Your configuration couldn't be read");
    ASSERT_STR_EQ(n.opening,
        "nutshell.config may be locked by another program, or larger than "
        "expected. This run uses the standard settings and saves nothing, so "
        "your file is left untouched. Restart Nutshell once the file can be "
        "read.");

    notice_invalid(&n, "nutshell.config.bad-20261123-101500");
    ASSERT_STR_EQ(n.title, "Your configuration couldn't be loaded");
    ASSERT_STR_EQ(n.opening,
        "nutshell.config wasn't valid, so it was kept beside the new one as "
        "nutshell.config.bad-20261123-101500. This run starts with the "
        "standard settings.");
    ASSERT_TRUE(strchr(n.opening, '`') == NULL);

    notice_save_failed_launch(&n);
    ASSERT_STR_EQ(n.title, "Your configuration couldn't be saved");
    ASSERT_STR_EQ(n.opening,
        "nutshell.config couldn't be written, so what was just added (the "
        "\"Local shell\" profile, new settings, or a new file) will be gone "
        "at the next start.");

    notice_save_failed(&n, "The profile was not written to disk.");
    ASSERT_STR_EQ(n.title, "Your changes couldn't be saved");
    ASSERT_STR_EQ(n.opening, "The profile was not written to disk.");
    ASSERT_EQ(n.tone, NOTICE_WARNING);
    TEST_END();
}

int test_notice_first_run_copy(void)
{
    TEST_BEGIN();
    Notice n;
    notice_first_run(&n, "1.2.90", NULL);
    ASSERT_EQ(n.tone, NOTICE_INFO);
    ASSERT_STR_EQ(n.title, "Welcome to Nutshell 1.2.90");
    ASSERT_STR_EQ(n.opening,
        "No configuration was found, so a new one has been created beside "
        "nutshell.exe. These optional features start switched off:");
    ASSERT_EQ(n.item_count, 2);
    ASSERT_STR_EQ(n.items[0].label, "AI assistant");
    ASSERT_STR_EQ(n.items[0].value, "off until you choose to turn it on");
    ASSERT_STR_EQ(n.items[1].label, "Voice");
    ASSERT_STR_EQ(n.items[1].value, "off until you choose to turn it on");
    ASSERT_STR_EQ(n.closing, "Everything else uses the standard settings.");
    ASSERT_STR_EQ(n.action, "Review in Settings");
    ASSERT_EQ(n.action_page, SETTINGS_PAGE_AI_PROVIDER);

    notice_first_run(&n, "1.2.90", "C:\\Users\\a\\AppData\\Local\\Nutshell");
    ASSERT_STR_EQ(n.opening,
        "No configuration was found, so a new one has been created in "
        "C:\\Users\\a\\AppData\\Local\\Nutshell. These optional features "
        "start switched off:");
    TEST_END();
}

int test_notice_upgrade_ai_item(void)
{
    TEST_BEGIN();
    Notice n;
    ConfigLoadReport r;
    Settings s;
    config_default_settings(&s);
    rep_init(&r, CONFIG_LOAD_OK);
    rep_add(&r, "ai_enabled");
    r.ai_enabled_decided = 1;

    s.ai_enabled = 1;
    ASSERT_EQ(notice_upgrade(&n, "1.2.90", &r, &s), 1);
    ASSERT_EQ(n.tone, NOTICE_INFO);
    ASSERT_STR_EQ(n.title, "Welcome to Nutshell 1.2.90");
    ASSERT_STR_EQ(n.opening,
        "This version introduces new settings, and your configuration has "
        "been updated to include them:");
    ASSERT_EQ(n.item_count, 1);
    ASSERT_STR_EQ(n.items[0].label, "AI assistant");
    ASSERT_STR_EQ(n.items[0].value, "on, as your API key is already set up");
    ASSERT_STR_EQ(n.closing, "Everything else is just as you left it.");
    ASSERT_STR_EQ(n.action, "Review in Settings");
    ASSERT_EQ(n.action_page, SETTINGS_PAGE_AI_PROVIDER);

    s.ai_enabled = 0;
    ASSERT_EQ(notice_upgrade(&n, "1.2.90", &r, &s), 1);
    ASSERT_STR_EQ(n.items[0].value, "off until you choose to turn it on");
    TEST_END();
}

int test_notice_upgrade_other_keys_and_none(void)
{
    TEST_BEGIN();
    Notice n;
    ConfigLoadReport r;
    Settings s;
    config_default_settings(&s);
    rep_init(&r, CONFIG_LOAD_OK);
    rep_add(&r, "ai_enabled");
    rep_add(&r, "ai_thinking_mode");
    rep_add(&r, "logging_enabled");
    ASSERT_EQ(notice_upgrade(&n, "1.2.90", &r, &s), 1);
    ASSERT_EQ(n.item_count, 3);
    const SettingsHelp *h = settings_help_get(SH_AI_THINKING_MODE);
    ASSERT_STR_EQ(n.items[1].label, "Thinking display");
    ASSERT_STR_EQ(n.items[1].value, h->default_text);
    ASSERT_STR_EQ(n.items[2].label, "Session logging");
    ASSERT_STR_EQ(n.items[2].value, "the standard setting");

    rep_init(&r, CONFIG_LOAD_OK);
    ASSERT_EQ(notice_upgrade(&n, "1.2.90", &r, &s), 0);
    ASSERT_EQ(n.item_count, 0);
    ASSERT_EQ(notice_upgrade(&n, "1.2.90", NULL, &s), 0);

    /* The first listed setting without a settings page: no page. */
    rep_init(&r, CONFIG_LOAD_OK);
    rep_add(&r, "logging_enabled");
    ASSERT_EQ(notice_upgrade(&n, "1.2.90", &r, &s), 1);
    ASSERT_EQ(n.action_page, -1);

    /* AI key missing, first-load rule not run: s->ai_enabled decides. */
    rep_init(&r, CONFIG_LOAD_OK);
    rep_add(&r, "ai_enabled");
    s.ai_enabled = 1;
    ASSERT_EQ(notice_upgrade(&n, "1.2.90", &r, &s), 1);
    ASSERT_STR_EQ(n.items[0].value, "on, as your API key is already set up");
    TEST_END();
}

int test_notice_upgrade_caps_items(void)
{
    TEST_BEGIN();
    Notice n;
    ConfigLoadReport r;
    Settings s;
    config_default_settings(&s);
    rep_init(&r, CONFIG_LOAD_OK);
    size_t nk = 0;
    (void)config_setting_keys(&nk);
    for (size_t i = 0; i < nk && i < 30; i++) {
        r.missing[r.missing_count++] = (unsigned char)i;
    }
    ASSERT_TRUE(r.missing_count > NOTICE_ITEMS_MAX);
    ASSERT_EQ(notice_upgrade(&n, "1.2.90", &r, &s), 1);
    ASSERT_EQ(n.item_count, NOTICE_ITEMS_MAX);
    ASSERT_STR_EQ(n.items[NOTICE_ITEMS_MAX - 1].label, "And more");
    char want[160];
    (void)snprintf(want, sizeof(want),
                   "%d other settings, each at its standard value",
                   (int)r.missing_count - (NOTICE_ITEMS_MAX - 1));
    ASSERT_STR_EQ(n.items[NOTICE_ITEMS_MAX - 1].value, want);

    /* Exactly the cap: all listed, no "And more". */
    r.missing_count = NOTICE_ITEMS_MAX;
    ASSERT_EQ(notice_upgrade(&n, "1.2.90", &r, &s), 1);
    ASSERT_EQ(n.item_count, NOTICE_ITEMS_MAX);
    ASSERT_TRUE(strcmp(n.items[NOTICE_ITEMS_MAX - 1].label, "And more") != 0);
    TEST_END();
}

static int plan(NoticeQueue *q, ConfigLoadStatus st, int no_folder, int saved,
                int ok, int missing, const char *fallback)
{
    static ConfigLoadReport r;
    static Settings s;
    NoticeLaunch in;
    rep_init(&r, st);
    config_default_settings(&s);
    if (missing) rep_add(&r, "ai_enabled");
    if (st == CONFIG_LOAD_INVALID)
        snprintf(r.bad_path, sizeof(r.bad_path),
                 "C:\\app\\nutshell.config.bad-20261123-101500");
    memset(&in, 0, sizeof(in));
    in.status = st; in.no_folder = no_folder; in.rep = &r;
    in.saved = saved; in.save_ok = ok; in.version = "1.2.90";
    in.fallback_folder = fallback; in.s = &s;
    notice_launch_plan(q, &in);
    return q->count;
}

int test_notice_launch_plan_table(void)
{
    TEST_BEGIN();
    NoticeQueue q;
    /* MISSING */
    ASSERT_EQ(plan(&q, CONFIG_LOAD_MISSING, 0, 1, 1, 0, NULL), 1);
    ASSERT_EQ(q.n[0].tone, NOTICE_INFO);
    ASSERT_STR_EQ(q.n[0].title, "Welcome to Nutshell 1.2.90");
    ASSERT_TRUE(strstr(q.n[0].opening, "beside nutshell.exe") != NULL);
    ASSERT_EQ(plan(&q, CONFIG_LOAD_MISSING, 0, 1, 1, 0, "D:\\Local"), 1);
    ASSERT_TRUE(strstr(q.n[0].opening, "created in D:\\Local.") != NULL);
    ASSERT_EQ(plan(&q, CONFIG_LOAD_MISSING, 0, 1, 0, 0, NULL), 1);
    ASSERT_STR_EQ(q.n[0].title, "Your configuration couldn't be saved");
    /* OK */
    ASSERT_EQ(plan(&q, CONFIG_LOAD_OK, 0, 1, 1, 1, NULL), 1);
    ASSERT_STR_EQ(q.n[0].items[0].label, "AI assistant");
    ASSERT_STR_EQ(q.n[0].closing, "Everything else is just as you left it.");
    ASSERT_EQ(plan(&q, CONFIG_LOAD_OK, 0, 1, 0, 1, NULL), 1);
    ASSERT_STR_EQ(q.n[0].title, "Your configuration couldn't be saved");
    ASSERT_EQ(plan(&q, CONFIG_LOAD_OK, 0, 1, 0, 0, NULL), 1); /* local profile added */
    ASSERT_STR_EQ(q.n[0].title, "Your configuration couldn't be saved");
    ASSERT_EQ(plan(&q, CONFIG_LOAD_OK, 0, 0, 0, 0, NULL), 0);
    ASSERT_EQ(plan(&q, CONFIG_LOAD_OK, 0, 1, 1, 0, NULL), 0); /* only a profile: silent */
    /* INVALID */
    ASSERT_EQ(plan(&q, CONFIG_LOAD_INVALID, 0, 1, 1, 0, NULL), 1);
    ASSERT_STR_EQ(q.n[0].title, "Your configuration couldn't be loaded");
    ASSERT_TRUE(strstr(q.n[0].opening, "as nutshell.config.bad-20261123-101500.") != NULL);
    ASSERT_TRUE(strchr(q.n[0].opening, '\\') == NULL);
    ASSERT_EQ(plan(&q, CONFIG_LOAD_INVALID, 0, 1, 0, 0, NULL), 2);
    ASSERT_STR_EQ(q.n[0].title, "Your configuration couldn't be loaded");
    ASSERT_STR_EQ(q.n[1].title, "Your configuration couldn't be saved");
    /* UNREADABLE: never an info notice, even with a save */
    ASSERT_EQ(plan(&q, CONFIG_LOAD_UNREADABLE, 0, 0, 0, 0, NULL), 1);
    ASSERT_STR_EQ(q.n[0].title, "Your configuration couldn't be read");
    ASSERT_EQ(plan(&q, CONFIG_LOAD_UNREADABLE, 0, 1, 1, 1, NULL), 1);
    ASSERT_EQ(q.n[0].tone, NOTICE_WARNING);
    /* no folder */
    ASSERT_EQ(plan(&q, CONFIG_LOAD_MISSING, 1, 0, 0, 0, NULL), 1);
    ASSERT_STR_EQ(q.n[0].title, "Settings can't be saved this run");
    ASSERT_EQ(plan(&q, CONFIG_LOAD_MISSING, 1, 1, 1, 0, NULL), 1);
    ASSERT_EQ(q.n[0].tone, NOTICE_WARNING);
    TEST_END();
}

int test_notice_queue_order_and_full(void)
{
    TEST_BEGIN();
    NoticeQueue q;
    Notice info, w1, w2, w3, extra;
    notice_queue_init(&q);
    ASSERT_EQ(q.count, 0);
    notice_first_run(&info, "1.2.90", NULL);
    notice_unreadable(&w1);
    notice_save_failed(&w2, "second");
    notice_save_failed(&w3, "third");
    ASSERT_EQ(notice_queue_push(&q, &info), 1);
    ASSERT_EQ(notice_queue_push(&q, &w1), 1);
    ASSERT_EQ(notice_queue_push(&q, &w2), 1);
    ASSERT_EQ(q.n[0].tone, NOTICE_WARNING);
    ASSERT_STR_EQ(q.n[0].title, w1.title);
    ASSERT_STR_EQ(q.n[1].opening, "second"); /* stable among warnings */
    ASSERT_EQ(q.n[2].tone, NOTICE_INFO);
    ASSERT_EQ(notice_queue_push(&q, &w3), 1);
    ASSERT_STR_EQ(q.n[2].opening, "third");
    ASSERT_EQ(q.n[3].tone, NOTICE_INFO);
    notice_no_folder(&extra);
    ASSERT_EQ(notice_queue_push(&q, &extra), 0);
    ASSERT_EQ(q.count, NOTICE_QUEUE_MAX);
    TEST_END();
}

int test_notice_plain_text(void)
{
    TEST_BEGIN();
    Notice n;
    char buf[1024];
    ConfigLoadReport r;
    Settings s;
    config_default_settings(&s);
    rep_init(&r, CONFIG_LOAD_OK);
    rep_add(&r, "ai_enabled");
    ASSERT_EQ(notice_upgrade(&n, "1.2.90", &r, &s), 1);
    size_t len = notice_plain_text(&n, buf, sizeof(buf));
    ASSERT_EQ(len, strlen(buf));
    ASSERT_STR_EQ(buf,
        "Welcome to Nutshell 1.2.90\n\n"
        "This version introduces new settings, and your configuration has "
        "been updated to include them:\n"
        "- AI assistant: off until you choose to turn it on\n"
        "\nEverything else is just as you left it.\n");

    notice_unreadable(&n);
    notice_plain_text(&n, buf, sizeof(buf));
    ASSERT_TRUE(strncmp(buf, "Your configuration couldn't be read\n\nnutshell.config may", 56) == 0);
    ASSERT_TRUE(buf[strlen(buf) - 1] == '\n');

    /* truncation */
    char small[16];
    len = notice_plain_text(&n, small, sizeof(small));
    ASSERT_EQ(len, 15u);
    ASSERT_EQ(small[15], '\0');
    ASSERT_STR_EQ(small, "Your configurat");
    char one[1] = { 'x' };
    ASSERT_EQ(notice_plain_text(&n, one, 1), 0u);
    ASSERT_EQ(one[0], '\0');
    ASSERT_EQ(notice_plain_text(&n, NULL, 0), 0u);
    TEST_END();
}

int test_notice_launch_save_wanted(void)
{
    TEST_BEGIN();
    ASSERT_EQ(notice_launch_save_wanted(0, 0, 0), 0);
    ASSERT_EQ(notice_launch_save_wanted(0, 1, 0), 1);
    ASSERT_EQ(notice_launch_save_wanted(0, 0, 1), 1);
    ASSERT_EQ(notice_launch_save_wanted(1, 1, 0), 0);   /* demo: no added fields, no first-run file */
    ASSERT_EQ(notice_launch_save_wanted(1, 0, 0), 0);
    ASSERT_EQ(notice_launch_save_wanted(1, 0, 1), 0);   /* demo: the Local shell profile stays in memory */
    ASSERT_EQ(notice_launch_save_wanted(1, 1, 1), 0);
    ASSERT_EQ(notice_launch_save_wanted(0, 1, 1), 1);
    TEST_END();
}

/* ---- notice_msg: the OK-only message boxes ---- */

typedef struct { NoticeMsgId id; NoticeTone tone; const char *title; const char *text; } MsgRow;

int test_notice_msg_table_matches_todays_boxes(void)
{
    TEST_BEGIN();
    static const MsgRow rows[] = {
        { NOTICE_MSG_CLOSE_CONNECTING, NOTICE_INFO, "Close Tab", "Connection in progress. Please wait." },
        { NOTICE_MSG_TAB_LIMIT, NOTICE_INFO, "Tab Limit", "Maximum number of tabs reached." },
        { NOTICE_MSG_NO_RECONNECT_PROFILE, NOTICE_INFO, "Reconnect", "No connection profile available for reconnection." },
        { NOTICE_MSG_DEBUG_LOG_MEM_ONLY, NOTICE_INFO, "Debug Log Folder", "No log folder is writable, so Nutshell is keeping the log in memory only. Mark Issue still works." },
        { NOTICE_MSG_DEBUG_LOG_PATH_INVALID, NOTICE_WARNING, "Debug Log Folder", "The log folder path is not valid." },
        { NOTICE_MSG_SELECT_TO_EDIT, NOTICE_INFO, "Edit", "Select a session first." },
        { NOTICE_MSG_SELECT_TO_DELETE, NOTICE_INFO, "Delete", "Select a session to delete." },
        { NOTICE_MSG_HOSTNAME_SAVE, NOTICE_WARNING, "Save", "Please enter a hostname." },
        { NOTICE_MSG_HOSTNAME_CONNECT, NOTICE_WARNING, "Connect", "Please enter a hostname." },
        { NOTICE_MSG_LOG_FOLDER_INVALID, NOTICE_WARNING, "Open Log Folder", "That log folder path is not valid." },
        { NOTICE_MSG_MODELS_NONE, NOTICE_INFO, "Refresh Models", "No models found." },
        { NOTICE_MSG_MODELS_NO_URL, NOTICE_WARNING, "Refresh Models", "Cannot determine models URL for this provider." },
        { NOTICE_MSG_MODELS_NO_KEY, NOTICE_WARNING, "Refresh Models", "Enter an API key first." },
        { NOTICE_MSG_CHAT_SAVE_FAILED, NOTICE_WARNING, "Save Error", "Failed to save file." },
    };
    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
        Notice n;
        memset(&n, 0x55, sizeof(n));
        ASSERT_EQ(notice_msg(&n, rows[i].id, NULL), 1);
        ASSERT_EQ(n.tone, rows[i].tone);
        ASSERT_STR_EQ(n.title, rows[i].title);
        ASSERT_STR_EQ(n.opening, rows[i].text);
        ASSERT_EQ(n.item_count, 0);
        ASSERT_EQ(n.closing[0], 0);
        ASSERT_EQ(n.action[0], 0);
        ASSERT_EQ(n.action_page, -1);
    }
    TEST_END();
}

int test_notice_msg_every_id_has_title_and_text(void)
{
    TEST_BEGIN();
    for (int id = 0; id < (int)NOTICE_MSG_COUNT; id++) {
        Notice n;
        ASSERT_EQ(notice_msg(&n, (NoticeMsgId)id, "x"), 1);
        ASSERT_TRUE(n.title[0] != 0);
        ASSERT_TRUE(n.opening[0] != 0);
    }
    TEST_END();
}

int test_notice_msg_session_not_found_formats_name(void)
{
    TEST_BEGIN();
    Notice n;
    ASSERT_EQ(notice_msg(&n, NOTICE_MSG_SESSION_NOT_FOUND, "web01"), 1);
    ASSERT_STR_EQ(n.title, "Session Not Found");
    ASSERT_EQ(n.tone, NOTICE_WARNING);
    ASSERT_STR_EQ(n.opening, "Session \"web01\" not found.");
    ASSERT_EQ(notice_msg(&n, NOTICE_MSG_SESSION_NOT_FOUND, "50%s %d"), 1);
    ASSERT_STR_EQ(n.opening, "Session \"50%s %d\" not found.");
    ASSERT_EQ(notice_msg(&n, NOTICE_MSG_SESSION_NOT_FOUND, NULL), 1);
    ASSERT_STR_EQ(n.opening, "Session \"\" not found.");
    TEST_END();
}

int test_notice_msg_text_args(void)
{
    TEST_BEGIN();
    Notice n;
    ASSERT_EQ(notice_msg(&n, NOTICE_MSG_CONNECTION_ERROR, "Host unreachable"), 1);
    ASSERT_STR_EQ(n.opening, "Host unreachable");
    ASSERT_STR_EQ(n.title, "Connection Error");
    ASSERT_EQ(n.tone, NOTICE_WARNING);
    ASSERT_EQ(notice_msg(&n, NOTICE_MSG_CONNECTION_ERROR, NULL), 1);
    ASSERT_STR_EQ(n.opening, "The connection failed.");
    ASSERT_EQ(notice_msg(&n, NOTICE_MSG_CONNECTION_ERROR, ""), 1);
    ASSERT_STR_EQ(n.opening, "The connection failed.");
    ASSERT_EQ(notice_msg(&n, NOTICE_MSG_MODELS_ERROR, "Error: HTTP 401"), 1);
    ASSERT_STR_EQ(n.opening, "Error: HTTP 401");
    ASSERT_EQ(n.tone, NOTICE_WARNING);
    ASSERT_STR_EQ(n.title, "Refresh Models");
    ASSERT_EQ(notice_msg(&n, NOTICE_MSG_MODELS_ERROR, NULL), 1);
    ASSERT_STR_EQ(n.opening, "Error: the models request failed.");
    TEST_END();
}

int test_notice_msg_title_args(void)
{
    TEST_BEGIN();
    Notice n;
    ASSERT_EQ(notice_msg(&n, NOTICE_MSG_FOLDER_MISSING, "Debug Log Folder"), 1);
    ASSERT_STR_EQ(n.title, "Debug Log Folder");
    ASSERT_EQ(n.tone, NOTICE_WARNING);
    ASSERT_STR_EQ(n.opening, "That folder does not exist and could not be created.");
    ASSERT_EQ(notice_msg(&n, NOTICE_MSG_FOLDER_MISSING, NULL), 1);
    ASSERT_STR_EQ(n.title, "Open Folder");
    ASSERT_EQ(notice_msg(&n, NOTICE_MSG_FOLDER_IS_FILE, "Open Log Folder"), 1);
    ASSERT_STR_EQ(n.title, "Open Log Folder");
    ASSERT_STR_EQ(n.opening, "That path is a file, not a folder, so it was not opened. Choose a folder for the log.");
    ASSERT_EQ(n.tone, NOTICE_WARNING);
    TEST_END();
}

int test_notice_msg_long_arg_truncates(void)
{
    TEST_BEGIN();
    static char big[2001];
    memset(big, 'a', 2000);
    big[2000] = 0;
    Notice n;
    ASSERT_EQ(notice_msg(&n, NOTICE_MSG_CONNECTION_ERROR, big), 1);
    ASSERT_EQ(strlen(n.opening), sizeof(n.opening) - 1);
    big[300] = 0;
    ASSERT_EQ(notice_msg(&n, NOTICE_MSG_FOLDER_MISSING, big), 1);
    ASSERT_EQ(strlen(n.title), sizeof(n.title) - 1);
    TEST_END();
}

int test_notice_msg_bad_id(void)
{
    TEST_BEGIN();
    Notice n;
    memset(&n, 0x55, sizeof(n));
    ASSERT_EQ(notice_msg(&n, NOTICE_MSG_COUNT, NULL), 0);
    ASSERT_EQ(n.title[0], 0);
    memset(&n, 0x55, sizeof(n));
    int bad = -1;
    ASSERT_EQ(notice_msg(&n, (NoticeMsgId)bad, NULL), 0);
    ASSERT_EQ(n.title[0], 0);
    ASSERT_EQ(notice_msg(NULL, NOTICE_MSG_TAB_LIMIT, NULL), 0);
    TEST_END();
}
