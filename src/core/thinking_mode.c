#include "thinking_mode.h"

#include <string.h>

ThinkMode think_mode_next(ThinkMode m)
{
    switch (m) {
    case THINK_MODE_COLLAPSED: return THINK_MODE_COMPACT;
    case THINK_MODE_COMPACT:   return THINK_MODE_EXTENDED;
    case THINK_MODE_EXTENDED:  return THINK_MODE_OPEN;
    case THINK_MODE_OPEN:      return THINK_MODE_COLLAPSED;
    default:                   return THINK_MODE_COMPACT;
    }
}

ThinkMode think_mode_effective(ThinkPref pref, int streaming)
{
    switch (pref) {
    case THINK_PREF_COLLAPSED: return THINK_MODE_COLLAPSED;
    case THINK_PREF_COMPACT:   return THINK_MODE_COMPACT;
    case THINK_PREF_OPEN:      return THINK_MODE_OPEN;
    case THINK_PREF_EXTENDED:  return THINK_MODE_EXTENDED;
    default:
        return streaming ? THINK_MODE_EXTENDED : THINK_MODE_COLLAPSED;
    }
}

ThinkPref think_pref_from_mode(ThinkMode m)
{
    switch (m) {
    case THINK_MODE_COMPACT: return THINK_PREF_COMPACT;
    case THINK_MODE_EXTENDED: return THINK_PREF_EXTENDED;
    case THINK_MODE_OPEN:    return THINK_PREF_OPEN;
    default:                 return THINK_PREF_COLLAPSED;
    }
}

const char *think_pref_name(ThinkPref p)
{
    switch (p) {
    case THINK_PREF_COLLAPSED: return "collapsed";
    case THINK_PREF_COMPACT:   return "compact";
    case THINK_PREF_OPEN:      return "open";
    case THINK_PREF_EXTENDED:  return "extended";
    default:                   return "auto";
    }
}

ThinkPref think_pref_parse(const char *s)
{
    if (!s) return THINK_PREF_AUTO;
    if (strcmp(s, "collapsed") == 0) return THINK_PREF_COLLAPSED;
    if (strcmp(s, "compact") == 0)   return THINK_PREF_COMPACT;
    if (strcmp(s, "open") == 0)      return THINK_PREF_OPEN;
    if (strcmp(s, "extended") == 0)  return THINK_PREF_EXTENDED;
    return THINK_PREF_AUTO;
}

int think_mode_body_cap(ThinkMode m, int line_h, int open_cap)
{
    switch (m) {
    case THINK_MODE_COMPACT: return THINK_COMPACT_LINES * line_h;
    case THINK_MODE_EXTENDED: return THINK_EXTENDED_LINES * line_h;
    case THINK_MODE_OPEN:    return open_cap;
    default:                 return 0;
    }
}

int think_mode_follows_tail(ThinkMode m)
{
    return m == THINK_MODE_COMPACT || m == THINK_MODE_EXTENDED;
}

int think_compact_offset(int full_h, int vis_h)
{
    return full_h > vis_h ? full_h - vis_h : 0;
}

unsigned int think_mode_label_colour(const ThemeChatColors *chat, ThinkMode m)
{
    switch (m) {
    case THINK_MODE_COMPACT:
    case THINK_MODE_EXTENDED: return chat->thinking_compact;
    case THINK_MODE_OPEN:    return chat->thinking_open;
    default:                 return chat->thinking_collapsed;
    }
}

size_t think_trimmed_len(const char *s, size_t len)
{
    if (!s) return 0;
    while (len > 0) {
        char c = s[len - 1];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n' &&
            c != '\f' && c != '\v')
            break;
        len--;
    }
    return len;
}
