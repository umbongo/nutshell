#ifndef NUTSHELL_PAINT_COVER_H
#define NUTSHELL_PAINT_COVER_H

/*
 * paint_cover -- does a WM_PAINT rectangle completely cover a terminal
 * row's rectangle?
 *
 * renderer_draw() clears a row's dirty flag once it has drawn the row.
 * That is only correct when the paint rectangle contained the whole row:
 * a narrow, partial update region (the right-edge gutter exposed when the
 * scrollbar hides on entry to the alternate screen, 2026-10-14) paints
 * only the columns inside it, yet the old code marked every row it
 * iterated clean. Freshly dirtied rows were then skipped by every later
 * repaint, leaving a band of stale content on screen until a click or
 * resize forced a full-width paint.
 *
 * With the flag cleared only for rows the paint fully contained, a
 * partially painted row stays dirty and is finished by the next full-width
 * paint (the poll loop invalidates the whole terminal rect while a
 * connected session has a dirty row; other sessions wait for the next
 * full invalidate, as they did before). Pure and portable so the decision
 * is unit-tested; renderer_draw (src/ui, Win32, not in the test build)
 * calls it per row.
 *
 * Coordinates are pixels. Both rectangles follow Win32 RECT convention:
 * left/top inclusive, right/bottom exclusive. A row rectangle is
 * [content_left, content_right) x [row_top, row_bottom).
 */
int paint_covers_rect(long paint_left, long paint_top,
                      long paint_right, long paint_bottom,
                      long rect_left, long rect_top,
                      long rect_right, long rect_bottom);

#endif /* NUTSHELL_PAINT_COVER_H */
