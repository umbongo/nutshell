#include "thinking_mode.h"

#include <string.h>

ThinkMode think_mode_next(ThinkMode m)
{
    switch (m) {
    case THINK_MODE_COLLAPSED: return THINK_MODE_COMPACT;
    case THINK_MODE_COMPACT:   return THINK_MODE_OPEN;
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
    default:
        return streaming ? THINK_MODE_COMPACT : THINK_MODE_COLLAPSED;
    }
}

ThinkPref think_pref_from_mode(ThinkMode m)
{
    switch (m) {
    case THINK_MODE_COMPACT: return THINK_PREF_COMPACT;
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
    default:                   return "auto";
    }
}

ThinkPref think_pref_parse(const char *s)
{
    if (!s) return THINK_PREF_AUTO;
    if (strcmp(s, "collapsed") == 0) return THINK_PREF_COLLAPSED;
    if (strcmp(s, "compact") == 0)   return THINK_PREF_COMPACT;
    if (strcmp(s, "open") == 0)      return THINK_PREF_OPEN;
    return THINK_PREF_AUTO;
}

int think_mode_body_cap(ThinkMode m, int line_h, int open_cap)
{
    switch (m) {
    case THINK_MODE_COMPACT: return THINK_COMPACT_LINES * line_h;
    case THINK_MODE_OPEN:    return open_cap;
    default:                 return 0;
    }
}

int think_compact_offset(int full_h, int vis_h)
{
    return full_h > vis_h ? full_h - vis_h : 0;
}

unsigned int think_mode_label_colour(const ThemeChatColors *chat, ThinkMode m)
{
    switch (m) {
    case THINK_MODE_COMPACT: return chat->thinking_compact;
    case THINK_MODE_OPEN:    return chat->thinking_open;
    default:                 return chat->thinking_collapsed;
    }
}
