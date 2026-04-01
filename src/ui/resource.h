#ifndef NUTSHELL_RESOURCE_H
#define NUTSHELL_RESOURCE_H

#define APP_VERSION        "1.0.30"
#define APP_VERSION_BINARY  1,0,30,0

#define IDI_APPICON         100
#define IDD_SESSION_MANAGER 101
#define IDR_FONT_INTER      200
#define IDR_FONT_INTER_BOLD 201
#define IDR_ACORN_PNG       202

/* Menu bar command IDs */
#define IDM_FILE_NEW_SESSION  2001
#define IDM_FILE_CONNECT      2002
#define IDM_FILE_DISCONNECT   2003
#define IDM_FILE_LOG_START    2004
#define IDM_FILE_LOG_STOP     2005
#define IDM_FILE_SAVE_AI      2007
#define IDM_FILE_EXIT         2006
#define IDM_EDIT_COPY         2010
#define IDM_EDIT_PASTE        2011
#define IDM_EDIT_SELECT_ALL   2012
#define IDM_EDIT_SETTINGS     2013
#define IDM_VIEW_AI_CHAT      2020
#define IDM_VIEW_FULLSCREEN   2021
#define IDM_VIEW_AI_UNDOCK    2022
#define IDM_HELP_GUIDE        2029
#define IDM_ABOUT             2030

/* Session list panel */
#define IDC_LIST_SESSIONS   1000
#define IDC_BTN_NEW         1001
#define IDC_BTN_EDIT        1002
#define IDC_BTN_DELETE      1003

/* Connection form */
#define IDC_EDIT_NAME       1010
#define IDC_EDIT_HOST       1011
#define IDC_EDIT_PORT       1012
#define IDC_EDIT_USER       1013
#define IDC_COMBO_AUTH      1014
#define IDC_EDIT_PASS       1015
#define IDC_STATIC_KEY      1016
#define IDC_EDIT_KEYPATH    1017
#define IDC_BTN_SAVE        1018
#define IDC_BTN_BROWSE_KEY  1019
#define IDC_EDIT_AI_NOTES   1020

/* Inline command approval buttons (in chat_listview → WM_COMMAND to chat panel) */
#define IDC_CMD_APPROVE_BASE 3000  /* 3000..3015 for up to 16 commands */
#define IDC_CMD_DENY_BASE    3020  /* 3020..3035 for up to 16 commands */
#define IDC_CMD_APPROVE_ALL  3040
#define IDC_AUTO_APPROVE     3041
#define IDC_ACTIVITY_RETRY   3042  /* Retry link in activity indicator */
#define IDC_CHATLIST_PASTE   3043  /* Right-click paste from chat listview */
#define IDC_CMD_EXPAND_ALL   3044  /* Expand collapsed command list */
#define IDC_CMD_APPROVE_SEL  3045  /* Approve selected (ticked) commands */
#define IDC_CMD_CANCEL_ALL   3046  /* Cancel (deny) all pending commands */
#define IDC_CMD_TOGGLE_BASE  3050  /* 3050..3065 for up to 16 command tickboxes */

#endif
