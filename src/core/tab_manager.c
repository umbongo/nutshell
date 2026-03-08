#include "tab_manager.h"
#include <string.h>
#include <stdio.h>

void tabmgr_init(TabManager *m)
{
    memset(m, 0, sizeof(TabManager));
    m->active_index = -1;
    m->next_id      = 1;
}

int tabmgr_add(TabManager *m, const char *title, void *user_data)
{
    if (m->count >= TABS_MAX) return -1;
    int idx = m->count++;
    snprintf(m->tabs[idx].title, sizeof(m->tabs[idx].title), "%s",
             title ? title : "");
    m->tabs[idx].user_data = user_data;
    m->tabs[idx].status    = TAB_IDLE;
    m->tabs[idx].id        = m->next_id++;
    if (m->active_index < 0) m->active_index = 0;
    return idx;
}

void tabmgr_remove(TabManager *m, int index)
{
    if (index < 0 || index >= m->count) return;

    for (int i = index; i < m->count - 1; i++) {
        m->tabs[i] = m->tabs[i + 1];
    }
    m->count--;

    if (m->count == 0) {
        m->active_index = -1;
    } else if (m->active_index >= m->count) {
        m->active_index = m->count - 1;
    } else if (m->active_index == index) {
        /* Closed tab was active: prefer the previous one */
        if (m->active_index > 0) m->active_index--;
        /* else stay at 0 — the old tab[1] is now tab[0] */
    }
}

void tabmgr_set_active(TabManager *m, int index)
{
    if (index < 0 || index >= m->count) return;
    m->active_index = index;
}

int tabmgr_get_active(const TabManager *m)
{
    return m->active_index;
}

void *tabmgr_get_user_data(const TabManager *m, int index)
{
    if (index < 0 || index >= m->count) return NULL;
    return m->tabs[index].user_data;
}

void tabmgr_set_status(TabManager *m, int index, TabStatus status)
{
    if (index < 0 || index >= m->count) return;
    m->tabs[index].status = status;
}

TabStatus tabmgr_get_status(const TabManager *m, int index)
{
    if (index < 0 || index >= m->count) return TAB_IDLE;
    return m->tabs[index].status;
}

int tabmgr_count(const TabManager *m)
{
    return m->count;
}

int tabmgr_get_id(const TabManager *m, int index)
{
    if (index < 0 || index >= m->count) return -1;
    return m->tabs[index].id;
}

int tabmgr_navigate(TabManager *m, int delta)
{
    if (m->count == 0) return -1;
    int idx = m->active_index + delta;
    idx = idx % m->count;
    if (idx < 0) idx += m->count;
    m->active_index = idx;
    return idx;
}

int tabmgr_find(const TabManager *m, const void *user_data)
{
    for (int i = 0; i < m->count; i++) {
        if (m->tabs[i].user_data == user_data) return i;
    }
    return -1;
}

void tabmgr_set_connect_info(TabManager *m, int index,
                              const char *username, const char *host,
                              unsigned long long connect_ms)
{
    if (index < 0 || index >= m->count) return;
    TabEntry *e = &m->tabs[index];
    snprintf(e->username, sizeof(e->username), "%s", username ? username : "");
    snprintf(e->host,     sizeof(e->host),     "%s", host     ? host     : "");
    e->connect_ms = connect_ms;
}

void tabmgr_set_logging(TabManager *m, int index, int logging)
{
    if (index < 0 || index >= m->count) return;
    m->tabs[index].logging = logging;
}

int tabmgr_get_logging(const TabManager *m, int index)
{
    if (index < 0 || index >= m->count) return 0;
    return m->tabs[index].logging;
}

/* ---- Button tooltip text (pure layout, no Win32 dependency) -------------- */
#define BTN_SIZE_LAYOUT  24
#define BTN_GAP_LAYOUT   2

const char *tabs_btn_tooltip_at(int mx, int client_width)
{
    /* [+] button: x=[4, 4+BTN_SIZE] */
    if (mx >= 4 && mx <= 4 + BTN_SIZE_LAYOUT)
        return "Session Manager";

    /* Right-side buttons */
    int cogX   = client_width - BTN_SIZE_LAYOUT - 4;
    int aiX    = cogX - BTN_SIZE_LAYOUT - BTN_GAP_LAYOUT;
    int rightX = aiX - BTN_SIZE_LAYOUT - BTN_GAP_LAYOUT;
    int leftX  = rightX - BTN_SIZE_LAYOUT - BTN_GAP_LAYOUT;

    if (mx >= cogX && mx <= cogX + BTN_SIZE_LAYOUT)
        return "Settings";
    if (mx >= aiX && mx <= aiX + BTN_SIZE_LAYOUT)
        return "AI Assist\nOpen the AI assistant.\nAsk questions or let it run commands\non your terminal.";
    if (mx >= leftX && mx <= leftX + BTN_SIZE_LAYOUT)
        return "Previous tab";
    if (mx >= rightX && mx <= rightX + BTN_SIZE_LAYOUT)
        return "Next tab";

    return NULL;
}
