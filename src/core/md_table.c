/* src/core/md_table.c — pure markdown-table parsing and column-fitting
 * helpers. See md_table.h. No Win32 dependency; fully testable on Linux.
 */

#include "md_table.h"
#include "markdown.h"
#include <string.h>

int md_table_split_row(const char *line, MdTableCell *cells, int max_cells)
{
    if (!line || !cells || max_cells <= 0) return 0;

    int len = (int)strlen(line);
    int lo = 0, hi = len;

    while (lo < hi && (line[lo] == ' ' || line[lo] == '\t')) lo++;
    while (hi > lo && (line[hi - 1] == ' ' || line[hi - 1] == '\t')) hi--;

    if (lo < hi && line[lo] == '|') lo++;
    if (hi > lo && line[hi - 1] == '|') {
        int esc = (hi - 2 >= lo) && line[hi - 2] == '\\';
        if (!esc) hi--;
    }

    while (lo < hi && (line[lo] == ' ' || line[lo] == '\t')) lo++;
    while (hi > lo && (line[hi - 1] == ' ' || line[hi - 1] == '\t')) hi--;

    int count = 0;
    int cell_start = lo;
    int i = lo;
    while (i <= hi) {
        int is_delim = 0;
        if (i == hi) {
            is_delim = 1;
        } else if (line[i] == '|') {
            int esc = (i > lo) && line[i - 1] == '\\';
            if (!esc) is_delim = 1;
        }

        if (is_delim) {
            int s = cell_start, e = i;
            while (s < e && (line[s] == ' ' || line[s] == '\t')) s++;
            while (e > s && (line[e - 1] == ' ' || line[e - 1] == '\t')) e--;

            if (count < max_cells) {
                cells[count].start = line + s;
                cells[count].len = e - s;
                count++;
            } else {
                break;
            }
            cell_start = i + 1;
        }
        i++;
    }

    return count;
}

int md_table_alignments(const char *sep_line, int *align, int max_cols)
{
    if (!sep_line || !align || max_cols <= 0) return 0;
    if (!md_is_table_separator(sep_line)) return 0;

    MdTableCell cells[64];
    int cap = max_cols < 64 ? max_cols : 64;
    int n = md_table_split_row(sep_line, cells, cap);

    for (int i = 0; i < n; i++) {
        const char *s = cells[i].start;
        int slen = cells[i].len;
        int left = (slen > 0 && s[0] == ':');
        int right = (slen > 0 && s[slen - 1] == ':');
        if (left && right)
            align[i] = MD_ALIGN_CENTER;
        else if (right)
            align[i] = MD_ALIGN_RIGHT;
        else
            align[i] = MD_ALIGN_LEFT;
    }

    return n;
}

void md_table_fit_columns(const int *natural, const int *minimum, int ncols,
                          int avail, int *out)
{
    if (ncols <= 0 || !natural || !minimum || !out) return;

    int total = 0;
    for (int i = 0; i < ncols; i++) {
        out[i] = natural[i];
        total += natural[i];
    }
    if (total <= avail) return;

    int min_total = 0;
    for (int i = 0; i < ncols; i++) min_total += minimum[i];
    if (min_total >= avail) {
        for (int i = 0; i < ncols; i++) out[i] = minimum[i];
        return;
    }

    int excess = total - avail;

    /* Shrink the widest column(s) first, floored at their own minimum,
     * splitting the removal evenly among tied-widest columns each round,
     * until `excess` reaches 0. Each round finds how far the current
     * widest tier can shrink before either meeting the next tier down or
     * the highest per-column minimum among the tied columns -- whichever
     * is greater -- caps the drop by the excess actually left to remove,
     * and (when integer division leaves a remainder smaller than the tie
     * count) falls back to trimming 1px at a time. */
    while (excess > 0) {
        int maxw = -1;
        for (int i = 0; i < ncols; i++)
            if (out[i] > minimum[i] && out[i] > maxw) maxw = out[i];
        if (maxw < 0) break;  /* every column is already at its floor */

        int cnt = 0;
        for (int i = 0; i < ncols; i++)
            if (out[i] == maxw && out[i] > minimum[i]) cnt++;

        int target = -1;
        for (int i = 0; i < ncols; i++)
            if (out[i] < maxw && out[i] > target) target = out[i];

        int floor_of_group = -1;
        for (int i = 0; i < ncols; i++)
            if (out[i] == maxw && out[i] > minimum[i]
                    && minimum[i] > floor_of_group)
                floor_of_group = minimum[i];
        if (target < floor_of_group) target = floor_of_group;

        int drop_per_col = maxw - target;
        int max_drop_by_excess = excess / cnt;
        if (max_drop_by_excess < drop_per_col)
            drop_per_col = max_drop_by_excess;

        if (drop_per_col <= 0) {
            int rem = excess;
            for (int i = 0; i < ncols && rem > 0; i++) {
                if (out[i] == maxw && out[i] > minimum[i]) {
                    out[i]--;
                    rem--;
                    excess--;
                }
            }
            continue;
        }

        for (int i = 0; i < ncols; i++) {
            if (out[i] == maxw && out[i] > minimum[i]) {
                out[i] -= drop_per_col;
                excess -= drop_per_col;
            }
        }
    }
}
