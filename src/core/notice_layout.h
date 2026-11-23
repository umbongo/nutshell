/* notice_layout.h -- geometry of the notice dialog (src/ui/notice_dlg.c).
 *
 * Portable (ns_scale.h, ns_type.h, ns_layout.h's NsRect). The dialog measures
 * its text and passes the heights in; the rows are laid out top to bottom:
 * title (with the round glyph), opening, the item list, closing, buttons. A
 * 4 px accent bar runs down the left edge. */
#ifndef NOTICE_LAYOUT_H
#define NOTICE_LAYOUT_H

#include "notice.h"
#include "ns_layout.h"

typedef struct {
    int dpi;
    int opening_h;             /* measured height of the opening at text_w */
    int closing_h;             /* 0 = no closing line */
    int item_count;            /* <= NOTICE_ITEMS_MAX */
    int label_w;               /* widest bold label, measured */
    int value_h[NOTICE_ITEMS_MAX]; /* measured at the value column's width */
    int has_action;
} NoticeLayoutIn;

typedef struct {
    NsRect bar, icon, title, opening;
    NsRect label[NOTICE_ITEMS_MAX], value[NOTICE_ITEMS_MAX];
    NsRect closing;            /* empty when closing_h is 0 */
    NsRect action;             /* empty without an action */
    NsRect ok;
    int client_w, client_h;
    int text_w;                /* the width the dialog measures opening/closing with */
} NoticeLayout;

void notice_layout(const NoticeLayoutIn *in, NoticeLayout *out);

#endif
