/* notice.c -- see notice.h. */

#include "notice.h"
#include "opt_feature.h"
#include "settings_help.h"
#include <stdio.h>
#include <string.h>

#define VAL_AI_ON   "on, as your API key is already set up"
#define VAL_OFF     "off until you choose to turn it on"
#define VAL_DEFAULT "the standard setting"

static void blank(Notice *n, NoticeTone tone)
{
    memset(n, 0, sizeof(*n));
    n->tone = tone;
    n->action_page = -1;
}

static void set(char *dst, size_t cap, const char *src)
{
    (void)snprintf(dst, cap, "%s", src ? src : "");
}

/* ---- The OK-only message boxes ---- */

typedef enum { ARG_NONE, ARG_TEXT, ARG_NAME, ARG_TITLE } MsgArg;

typedef struct {
    NoticeTone tone;
    MsgArg arg;
    const char *title;    /* ARG_TITLE: the fallback title */
    const char *opening;  /* ARG_TEXT: the fallback; ARG_NAME: see notice_msg */
} MsgRow;

static const MsgRow k_msgs[NOTICE_MSG_COUNT] = {
    /* window.c on_tab_close */
    [NOTICE_MSG_CLOSE_CONNECTING] = { NOTICE_INFO, ARG_NONE, "Close Tab",
        "Connection in progress. Please wait." },
    /* window.c, tabs_add failed */
    [NOTICE_MSG_TAB_LIMIT] = { NOTICE_INFO, ARG_NONE, "Tab Limit",
        "Maximum number of tabs reached." },
    /* window.c on_status_click */
    [NOTICE_MSG_NO_RECONNECT_PROFILE] = { NOTICE_INFO, ARG_NONE, "Reconnect",
        "No connection profile available for reconnection." },
    /* window.c open_debug_log_folder */
    [NOTICE_MSG_DEBUG_LOG_MEM_ONLY] = { NOTICE_INFO, ARG_NONE, "Debug Log Folder",
        "No log folder is writable, so Nutshell is keeping the log in memory only. Mark Issue still works." },
    /* window.c open_debug_log_folder */
    [NOTICE_MSG_DEBUG_LOG_PATH_INVALID] = { NOTICE_WARNING, ARG_NONE, "Debug Log Folder",
        "The log folder path is not valid." },
    /* window.c, a session name that is not saved; the opening names it */
    [NOTICE_MSG_SESSION_NOT_FOUND] = { NOTICE_WARNING, ARG_NAME, "Session Not Found",
        "Session \"%s\" not found." },  /* formatted in notice_msg */
    /* window.c, a failed connection; the opening is the error text */
    [NOTICE_MSG_CONNECTION_ERROR] = { NOTICE_WARNING, ARG_TEXT, "Connection Error",
        "The connection failed." },
    /* session_manager.c Edit */
    [NOTICE_MSG_SELECT_TO_EDIT] = { NOTICE_INFO, ARG_NONE, "Edit",
        "Select a session first." },
    /* session_manager.c Delete */
    [NOTICE_MSG_SELECT_TO_DELETE] = { NOTICE_INFO, ARG_NONE, "Delete",
        "Select a session to delete." },
    /* session_manager.c Save */
    [NOTICE_MSG_HOSTNAME_SAVE] = { NOTICE_WARNING, ARG_NONE, "Save",
        "Please enter a hostname." },
    /* session_manager.c Connect */
    [NOTICE_MSG_HOSTNAME_CONNECT] = { NOTICE_WARNING, ARG_NONE, "Connect",
        "Please enter a hostname." },
    /* settings.c debug_open_folder */
    [NOTICE_MSG_LOG_FOLDER_INVALID] = { NOTICE_WARNING, ARG_NONE, "Open Log Folder",
        "That log folder path is not valid." },
    /* settings.c models refresh; the opening is the result text */
    [NOTICE_MSG_MODELS_ERROR] = { NOTICE_WARNING, ARG_TEXT, "Refresh Models",
        "Error: the models request failed." },
    /* settings.c models refresh */
    [NOTICE_MSG_MODELS_NONE] = { NOTICE_INFO, ARG_NONE, "Refresh Models",
        "No models found." },
    /* settings.c models refresh */
    [NOTICE_MSG_MODELS_NO_URL] = { NOTICE_WARNING, ARG_NONE, "Refresh Models",
        "Cannot determine models URL for this provider." },
    /* settings.c models refresh */
    [NOTICE_MSG_MODELS_NO_KEY] = { NOTICE_WARNING, ARG_NONE, "Refresh Models",
        "Enter an API key first." },
    /* open_dir.h, the folder is missing; the caller's title */
    [NOTICE_MSG_FOLDER_MISSING] = { NOTICE_WARNING, ARG_TITLE, "Open Folder",
        "That folder does not exist and could not be created." },
    /* open_dir.h, the path is a file; the caller's title */
    [NOTICE_MSG_FOLDER_IS_FILE] = { NOTICE_WARNING, ARG_TITLE, "Open Folder",
        "That path is a file, not a folder, so it was not opened. Choose a folder for the log." },
    /* ai_chat.c Save */
    [NOTICE_MSG_CHAT_SAVE_FAILED] = { NOTICE_WARNING, ARG_NONE, "Save Error",
        "Failed to save file." },
};

int notice_msg(Notice *n, NoticeMsgId id, const char *arg)
{
    if (!n) return 0;
    if ((int)id < 0 || (int)id >= (int)NOTICE_MSG_COUNT) {
        blank(n, NOTICE_INFO);
        return 0;
    }
    const MsgRow *r = &k_msgs[(int)id];
    int have = arg && arg[0];
    blank(n, r->tone);
    set(n->title, sizeof(n->title), r->arg == ARG_TITLE && have ? arg : r->title);
    if (r->arg == ARG_NAME)
        (void)snprintf(n->opening, sizeof(n->opening), "Session \"%s\" not found.",
                       arg ? arg : "");
    else
        set(n->opening, sizeof(n->opening),
            r->arg == ARG_TEXT && have ? arg : r->opening);
    return 1;
}

/* ---- Warnings ---- */

void notice_no_folder(Notice *n)
{
    blank(n, NOTICE_WARNING);
    set(n->title, sizeof(n->title), "Settings can't be saved this run");
    set(n->opening, sizeof(n->opening),
        "Nutshell couldn't find a folder for nutshell.config: neither its own "
        "folder nor %LOCALAPPDATA% is available. Settings and sessions won't "
        "be saved until it is restarted with one available.");
}

void notice_unreadable(Notice *n)
{
    blank(n, NOTICE_WARNING);
    set(n->title, sizeof(n->title), "Your configuration couldn't be read");
    set(n->opening, sizeof(n->opening),
        "nutshell.config may be locked by another program, or larger than "
        "expected. This run uses the standard settings and saves nothing, so "
        "your file is left untouched. Restart Nutshell once the file can be "
        "read.");
}

void notice_invalid(Notice *n, const char *bad_name)
{
    blank(n, NOTICE_WARNING);
    set(n->title, sizeof(n->title), "Your configuration couldn't be loaded");
    (void)snprintf(n->opening, sizeof(n->opening),
        "nutshell.config wasn't valid, so it was kept beside the new one as "
        "%s. This run starts with the standard settings.",
        (bad_name && bad_name[0]) ? bad_name : "a .bad- backup copy");
}

void notice_save_failed_launch(Notice *n)
{
    blank(n, NOTICE_WARNING);
    set(n->title, sizeof(n->title), "Your configuration couldn't be saved");
    set(n->opening, sizeof(n->opening),
        "nutshell.config couldn't be written, so what was just added (the "
        "\"Local shell\" profile, new settings, or a new file) will be gone "
        "at the next start.");
}

void notice_save_failed(Notice *n, const char *text)
{
    blank(n, NOTICE_WARNING);
    set(n->title, sizeof(n->title), "Your changes couldn't be saved");
    set(n->opening, sizeof(n->opening), text);
}

/* ---- Information ---- */

static void welcome_title(Notice *n, const char *version)
{
    (void)snprintf(n->title, sizeof(n->title), "Welcome to Nutshell %s",
                   version ? version : "");
}

static int sh_page(int sh_id)
{
    const SettingsHelp *h = sh_id >= 0 ? settings_help_get(sh_id) : NULL;
    return h ? h->page : -1;
}

static void add_item(Notice *n, const char *label, const char *value)
{
    if (n->item_count >= NOTICE_ITEMS_MAX) return;
    NoticeItem *it = &n->items[n->item_count++];
    set(it->label, sizeof(it->label), label);
    set(it->value, sizeof(it->value), value);
}

/* The label of a settings key: its own, else the settings_help label with a
 * trailing ':' dropped. */
static void key_label(const ConfigSettingKey *k, char *out, size_t cap)
{
    if (k->label) { set(out, cap, k->label); return; }
    const SettingsHelp *h = k->sh_id >= 0 ? settings_help_get(k->sh_id) : NULL;
    set(out, cap, h ? h->label : k->key);
    size_t len = strlen(out);
    if (len > 0 && out[len - 1] == ':') out[len - 1] = '\0';
}

int notice_upgrade(Notice *n, const char *version, const ConfigLoadReport *rep,
                   const Settings *s)
{
    blank(n, NOTICE_INFO);
    if (!rep || rep->missing_count == 0) return 0;

    size_t nkeys = 0;
    const ConfigSettingKey *keys = config_setting_keys(&nkeys);
    size_t total = 0;
    for (size_t i = 0; i < rep->missing_count; i++)
        if (rep->missing[i] < nkeys) total++;
    if (total == 0) return 0;

    welcome_title(n, version);
    set(n->opening, sizeof(n->opening),
        "This version introduces new settings, and your configuration has "
        "been updated to include them:");
    set(n->closing, sizeof(n->closing), "Everything else is just as you left it.");
    set(n->action, sizeof(n->action), "Review in Settings");

    size_t shown = 0;
    for (size_t i = 0; i < rep->missing_count; i++) {
        if (rep->missing[i] >= nkeys) continue;
        const ConfigSettingKey *k = &keys[rep->missing[i]];
        if (total > NOTICE_ITEMS_MAX && shown >= NOTICE_ITEMS_MAX - 1) break;
        char label[64];
        const char *value;
        if (strcmp(k->key, "ai_enabled") == 0) {
            set(label, sizeof(label), "AI assistant");
            value = (s && s->ai_enabled) ? VAL_AI_ON : VAL_OFF;
        } else {
            key_label(k, label, sizeof(label));
            const SettingsHelp *h = k->sh_id >= 0 ? settings_help_get(k->sh_id) : NULL;
            value = (h && h->default_text) ? h->default_text : VAL_DEFAULT;
        }
        if (n->item_count == 0) n->action_page = sh_page(k->sh_id);
        add_item(n, label, value);
        shown++;
    }
    if (total > shown) {
        char more[160];
        (void)snprintf(more, sizeof(more),
                       "%u other settings, each at its standard value",
                       (unsigned)(total - shown));
        add_item(n, "And more", more);
    }
    return 1;
}

void notice_first_run(Notice *n, const char *version, const char *folder_or_null)
{
    blank(n, NOTICE_INFO);
    welcome_title(n, version);
    if (folder_or_null && folder_or_null[0]) {
        (void)snprintf(n->opening, sizeof(n->opening),
            "No configuration was found, so a new one has been created in %s. "
            "These optional features start switched off:", folder_or_null);
    } else {
        set(n->opening, sizeof(n->opening),
            "No configuration was found, so a new one has been created beside "
            "nutshell.exe. These optional features start switched off:");
    }
    set(n->closing, sizeof(n->closing), "Everything else uses the standard settings.");
    set(n->action, sizeof(n->action), "Review in Settings");

    size_t nf = 0, nkeys = 0;
    const OptFeatureInfo *f = opt_feature_table(&nf);
    const ConfigSettingKey *keys = config_setting_keys(&nkeys);
    for (size_t i = 0; i < nf; i++) {
        add_item(n, f[i].name, VAL_OFF);
        if (i == 0) {
            for (size_t k = 0; k < nkeys; k++)
                if (strcmp(keys[k].key, f[i].json_key) == 0)
                    n->action_page = sh_page(keys[k].sh_id);
        }
    }
}

/* ---- Queue and plan ---- */

void notice_queue_init(NoticeQueue *q)
{
    if (q) memset(q, 0, sizeof(*q));
}

int notice_queue_push(NoticeQueue *q, const Notice *n)
{
    if (!q || !n || q->count >= NOTICE_QUEUE_MAX) return 0;
    int at = q->count;
    if (n->tone == NOTICE_WARNING) {
        at = 0;
        while (at < q->count && q->n[at].tone == NOTICE_WARNING) at++;
        for (int i = q->count; i > at; i--) q->n[i] = q->n[i - 1];
    }
    q->n[at] = *n;
    q->count++;
    return 1;
}

static const char *base_name(const char *path)
{
    const char *b = path ? path : "";
    for (const char *p = b; *p; p++)
        if (*p == '/' || *p == '\\') b = p + 1;
    return b;
}

void notice_launch_plan(NoticeQueue *q, const NoticeLaunch *in)
{
    Notice n;
    if (!q || !in) return;
    notice_queue_init(q);

    if (in->no_folder) {
        notice_no_folder(&n);
        (void)notice_queue_push(q, &n);
        return;
    }
    int wrote = in->saved && in->save_ok;
    int failed = in->saved && !in->save_ok;

    switch (in->status) {
    case CONFIG_LOAD_UNREADABLE:
        notice_unreadable(&n);
        (void)notice_queue_push(q, &n);
        return;
    case CONFIG_LOAD_INVALID:
        notice_invalid(&n, base_name(in->rep ? in->rep->bad_path : NULL));
        (void)notice_queue_push(q, &n);
        break;
    case CONFIG_LOAD_MISSING:
        if (wrote) {
            notice_first_run(&n, in->version, in->fallback_folder);
            (void)notice_queue_push(q, &n);
        }
        break;
    case CONFIG_LOAD_OK:
    default:
        if (wrote && in->rep && in->rep->missing_count > 0 &&
            notice_upgrade(&n, in->version, in->rep, in->s))
            (void)notice_queue_push(q, &n);
        break;
    }
    if (failed) {
        notice_save_failed_launch(&n);
        (void)notice_queue_push(q, &n);
    }
}

/* ---- Plain text ---- */

typedef struct { char *out; size_t cap, len; } Sink;

static void put(Sink *k, const char *s)
{
    for (; *s && k->len + 1 < k->cap; s++) k->out[k->len++] = *s;
}

size_t notice_plain_text(const Notice *n, char *out, size_t cap)
{
    Sink k = { out, cap, 0 };
    if (!out || cap == 0) return 0;
    out[0] = '\0';
    if (!n) return 0;
    put(&k, n->title);
    put(&k, "\n\n");
    put(&k, n->opening);
    put(&k, "\n");
    for (int i = 0; i < n->item_count && i < NOTICE_ITEMS_MAX; i++) {
        put(&k, "- ");
        put(&k, n->items[i].label);
        put(&k, ": ");
        put(&k, n->items[i].value);
        put(&k, "\n");
    }
    if (n->closing[0]) {
        put(&k, "\n");
        put(&k, n->closing);
        put(&k, "\n");
    }
    out[k.len] = '\0';
    return k.len;
}

int notice_launch_save_wanted(int demo, int added, int local_profile)
{
    if (demo) return 0;   /* --ui-demo runs on the real config: save nothing */
    return (added || local_profile) ? 1 : 0;
}
