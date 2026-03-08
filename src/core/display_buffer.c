#include "display_buffer.h"
#include <stdlib.h>
#include <string.h>

void dispbuf_init(DisplayBuffer *db, int rows, int cols)
{
    if (!db) return;
    size_t n = (size_t)rows * (size_t)cols;
    db->cells = (DisplayCell *)calloc(n, sizeof(DisplayCell));
    db->rows = rows;
    db->cols = cols;
}

void dispbuf_free(DisplayBuffer *db)
{
    if (!db) return;
    free(db->cells);
    db->cells = NULL;
    db->rows = 0;
    db->cols = 0;
}

void dispbuf_resize(DisplayBuffer *db, int rows, int cols)
{
    if (!db) return;
    free(db->cells);
    size_t n = (size_t)rows * (size_t)cols;
    db->cells = (DisplayCell *)calloc(n, sizeof(DisplayCell));
    db->rows = rows;
    db->cols = cols;
}

void dispbuf_invalidate(DisplayBuffer *db)
{
    if (!db || !db->cells) return;
    size_t n = (size_t)db->rows * (size_t)db->cols;
    for (size_t i = 0; i < n; i++) {
        db->cells[i].codepoint = 0xFFFFFFFFu;  /* sentinel — won't match real content */
    }
}
