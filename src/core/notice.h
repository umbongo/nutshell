#ifndef NUTSHELL_NOTICE_H
#define NUTSHELL_NOTICE_H

#include <stddef.h>
#include "../config/config.h"

/*
 * notice -- the content and ordering of every themed notice dialog
 * (src/ui/notice_dlg.c) as portable data: the launch warnings, the upgrade
 * and first-run notices, the queue the launch fills and the plain-text copy.
 * No Win32. Every string is ASCII (the dialog is ANSI).
 */

typedef enum { NOTICE_INFO = 0, NOTICE_WARNING = 1 } NoticeTone;

#define NOTICE_ITEMS_MAX 24

typedef struct { char label[64]; char value[160]; } NoticeItem;

typedef struct {
    NoticeTone tone;
    char title[128];
    char opening[768];
    NoticeItem items[NOTICE_ITEMS_MAX];
    int  item_count;           /* beyond the cap the last item is "And more" */
    char closing[256];
    char action[48];           /* "" = no action button */
    int  action_page;          /* SETTINGS_PAGE_*, -1 = none */
} Notice;

#define NOTICE_QUEUE_MAX 4

typedef struct { Notice n[NOTICE_QUEUE_MAX]; int count; } NoticeQueue;

void notice_queue_init(NoticeQueue *q);
/* Warnings are shown before information, each group in push order. Returns 1
 * when queued, 0 when the queue is full (or an argument is NULL). */
int  notice_queue_push(NoticeQueue *q, const Notice *n);

void notice_no_folder(Notice *n);
void notice_unreadable(Notice *n);
/* `bad_name` is the base name of the backup (ConfigLoadReport.bad_path). */
void notice_invalid(Notice *n, const char *bad_name);
void notice_save_failed_launch(Notice *n);
/* Settings / Session Manager: `text` (the caller's existing message) is the
 * opening line. */
void notice_save_failed(Notice *n, const char *text);

/* The upgrade notice for the keys `rep` lists as missing. Returns 0, leaving
 * a blank notice, when nothing was added. */
int  notice_upgrade(Notice *n, const char *version, const ConfigLoadReport *rep,
                    const Settings *s);
/* `folder_or_null`: NULL = the file went beside nutshell.exe. */
void notice_first_run(Notice *n, const char *version, const char *folder_or_null);

typedef struct {
    ConfigLoadStatus status;
    int no_folder;
    const ConfigLoadReport *rep;
    int saved;                 /* a save was attempted */
    int save_ok;
    const char *version;
    const char *fallback_folder; /* NULL when beside the exe */
    const Settings *s;
} NoticeLaunch;

/* Fills `q` (as pushed, in the order they will be shown) with what a launch
 * owes the user. */
void notice_launch_plan(NoticeQueue *q, const NoticeLaunch *in);

/* Whether the launch saves the config. `added` is a first-run file or
 * settings an upgrade adds; `local_profile` is the Local shell profile
 * being created. Under --ui-demo nothing is saved: the demo runs on the
 * user's real config and keeps the Local shell profile in memory. */
int notice_launch_save_wanted(int demo, int added, int local_profile);

/* title, blank line, opening, "- label: value" lines, blank line, closing;
 * LF line ends, NUL-terminated, truncated at cap. Returns the length
 * written (NUL excluded). */
size_t notice_plain_text(const Notice *n, char *out, size_t cap);

#endif /* NUTSHELL_NOTICE_H */
