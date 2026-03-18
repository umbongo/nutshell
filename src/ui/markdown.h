#ifndef NUTSHELL_MARKDOWN_H
#define NUTSHELL_MARKDOWN_H

#include <string.h>

/*
 * Lightweight markdown parser for the AI Assist chat display.
 * Pure C, no Win32 dependency — fully testable on Linux.
 *
 * Two levels of parsing:
 *   1. Block-level: classify each line (heading, code fence, rule, list, table, etc.)
 *   2. Inline-level: find bold, italic, code, strikethrough spans within a line.
 */

/* ---- Block-level line classification ---- */

typedef enum {
    MD_LINE_PARAGRAPH,    /* Normal text */
    MD_LINE_HEADING,      /* # / ## / ### */
    MD_LINE_CODE_FENCE,   /* ``` */
    MD_LINE_CODE,         /* Inside a code block */
    MD_LINE_HRULE,        /* --- / *** / ___ */
    MD_LINE_ULIST,        /* - / * / + item */
    MD_LINE_OLIST,        /* 1. item */
    MD_LINE_TABLE,        /* | col | col | */
    MD_LINE_BLOCKQUOTE,   /* > text */
    MD_LINE_EMPTY         /* Blank line */
} MdLineType;

typedef struct {
    MdLineType type;
    int heading_level;    /* 1-3 for headings, 0 otherwise */
    int content_offset;   /* Byte offset where content starts (after marker) */
} MdLineInfo;

/* Classify a single line (no trailing \n or \r\n).
 * `in_code_block`: current code fence state (toggled by CODE_FENCE lines). */
static inline MdLineInfo md_classify_line(const char *line, int in_code_block)
{
    MdLineInfo info = { MD_LINE_PARAGRAPH, 0, 0 };

    if (!line || !line[0]) {
        info.type = MD_LINE_EMPTY;
        return info;
    }

    /* Code fence: ``` at start of line */
    if (line[0] == '`' && line[1] == '`' && line[2] == '`') {
        info.type = MD_LINE_CODE_FENCE;
        return info;
    }

    /* Inside a code block: everything is code */
    if (in_code_block) {
        info.type = MD_LINE_CODE;
        return info;
    }

    /* Heading: # at start */
    if (line[0] == '#') {
        int level = 0;
        while (line[level] == '#' && level < 6) level++;
        if (level <= 3 && (line[level] == ' ' || line[level] == '\0')) {
            info.type = MD_LINE_HEADING;
            info.heading_level = level;
            info.content_offset = (line[level] == ' ') ? level + 1 : level;
            return info;
        }
    }

    /* Horizontal rule: 3+ of the same char (- * _) with only spaces */
    if (line[0] == '-' || line[0] == '*' || line[0] == '_') {
        char ch = line[0];
        int count = 0;
        int all_match = 1;
        for (int i = 0; line[i]; i++) {
            if (line[i] == ch) count++;
            else if (line[i] != ' ') { all_match = 0; break; }
        }
        if (all_match && count >= 3) {
            /* Disambiguate: "- text" is a list, "---" is a rule */
            if (ch == '-' && line[1] == ' ') {
                /* It's a list item, fall through */
            } else if (ch == '*' && line[1] == ' ') {
                /* It's a list item, fall through */
            } else {
                info.type = MD_LINE_HRULE;
                return info;
            }
        }
    }

    /* Blockquote: > at start */
    if (line[0] == '>' && (line[1] == ' ' || line[1] == '\0')) {
        info.type = MD_LINE_BLOCKQUOTE;
        info.content_offset = (line[1] == ' ') ? 2 : 1;
        return info;
    }

    /* Unordered list: - / * / + followed by space */
    if ((line[0] == '-' || line[0] == '*' || line[0] == '+') && line[1] == ' ') {
        info.type = MD_LINE_ULIST;
        info.content_offset = 2;
        return info;
    }

    /* Ordered list: digits followed by . and space */
    {
        int i = 0;
        while (line[i] >= '0' && line[i] <= '9') i++;
        if (i > 0 && line[i] == '.' && line[i + 1] == ' ') {
            info.type = MD_LINE_OLIST;
            info.content_offset = i + 2;
            return info;
        }
    }

    /* Table: starts with | */
    if (line[0] == '|') {
        info.type = MD_LINE_TABLE;
        return info;
    }

    return info;
}

/* ---- Inline span parsing ---- */

typedef enum {
    MD_SPAN_TEXT,         /* Plain text */
    MD_SPAN_BOLD,         /* **text** or __text__ */
    MD_SPAN_ITALIC,       /* *text* or _text_ */
    MD_SPAN_BOLD_ITALIC,  /* ***text*** */
    MD_SPAN_CODE,         /* `text` */
    MD_SPAN_STRIKETHROUGH /* ~~text~~ */
} MdSpanType;

typedef struct {
    MdSpanType type;
    int start;            /* Byte offset of content start (after markers) */
    int end;              /* Byte offset of content end (before closing markers) */
} MdSpan;

#define MD_MAX_SPANS 128

/* Parse inline spans in a line. Returns the number of spans found.
 * Spans are written to `out` (caller provides array of MD_MAX_SPANS). */
static inline int md_parse_inline(const char *line, int len, MdSpan *out)
{
    if (!line || len <= 0 || !out) return 0;

    int count = 0;
    int i = 0;
    int text_start = 0;

    while (i < len && count < MD_MAX_SPANS - 1) {
        /* Backtick: inline code */
        if (line[i] == '`') {
            /* Emit preceding plain text */
            if (i > text_start) {
                out[count].type = MD_SPAN_TEXT;
                out[count].start = text_start;
                out[count].end = i;
                count++;
            }
            int close = i + 1;
            while (close < len && line[close] != '`') close++;
            if (close < len) {
                out[count].type = MD_SPAN_CODE;
                out[count].start = i + 1;
                out[count].end = close;
                count++;
                i = close + 1;
                text_start = i;
                continue;
            }
            /* No closing backtick — treat as text */
            i++;
            continue;
        }

        /* Strikethrough: ~~ */
        if (line[i] == '~' && i + 1 < len && line[i + 1] == '~') {
            if (i > text_start) {
                out[count].type = MD_SPAN_TEXT;
                out[count].start = text_start;
                out[count].end = i;
                count++;
            }
            int start = i + 2;
            int close = start;
            while (close + 1 < len && !(line[close] == '~' && line[close + 1] == '~'))
                close++;
            if (close + 1 < len) {
                out[count].type = MD_SPAN_STRIKETHROUGH;
                out[count].start = start;
                out[count].end = close;
                count++;
                i = close + 2;
                text_start = i;
                continue;
            }
            i += 2;
            continue;
        }

        /* Bold/italic: * or _ */
        if (line[i] == '*' || line[i] == '_') {
            char marker = line[i];
            int stars = 0;
            while (i + stars < len && line[i + stars] == marker) stars++;

            if (stars >= 2) {
                /* Bold (**) or bold+italic (***) */
                int is_bold_italic = (stars >= 3);
                int open_len = is_bold_italic ? 3 : 2;
                MdSpanType stype = is_bold_italic ? MD_SPAN_BOLD_ITALIC : MD_SPAN_BOLD;

                /* Find closing markers */
                int start = i + open_len;
                int close = start;
                while (close < len) {
                    if (line[close] == marker) {
                        int cstars = 0;
                        while (close + cstars < len && line[close + cstars] == marker)
                            cstars++;
                        if (cstars >= open_len) {
                            /* Found closing */
                            if (i > text_start) {
                                out[count].type = MD_SPAN_TEXT;
                                out[count].start = text_start;
                                out[count].end = i;
                                count++;
                            }
                            out[count].type = stype;
                            out[count].start = start;
                            out[count].end = close;
                            count++;
                            i = close + open_len;
                            text_start = i;
                            goto next_char;
                        }
                    }
                    close++;
                }
                /* No closing — fall through to try single * italic */
            }

            if (stars >= 1) {
                /* Italic (*) */
                int start = i + 1;
                int close = start;
                while (close < len) {
                    if (line[close] == marker &&
                        (close + 1 >= len || line[close + 1] != marker)) {
                        /* Found single closing marker */
                        if (i > text_start) {
                            out[count].type = MD_SPAN_TEXT;
                            out[count].start = text_start;
                            out[count].end = i;
                            count++;
                        }
                        out[count].type = MD_SPAN_ITALIC;
                        out[count].start = start;
                        out[count].end = close;
                        count++;
                        i = close + 1;
                        text_start = i;
                        goto next_char;
                    }
                    close++;
                }
            }

            /* No closing marker — treat as regular text */
            i += stars;
            continue;
        }

        i++;
        next_char: ;
    }

    /* Emit trailing plain text */
    if (text_start < len && count < MD_MAX_SPANS) {
        out[count].type = MD_SPAN_TEXT;
        out[count].start = text_start;
        out[count].end = len;
        count++;
    }

    return count;
}

/* Helper: check if a table separator line (|---|---|) */
static inline int md_is_table_separator(const char *line)
{
    if (!line || line[0] != '|') return 0;
    for (int i = 1; line[i]; i++) {
        if (line[i] != '-' && line[i] != '|' && line[i] != ' ' && line[i] != ':')
            return 0;
    }
    return 1;
}

#endif /* NUTSHELL_MARKDOWN_H */
