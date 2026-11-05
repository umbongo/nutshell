/* pw_prompt_layout.c -- see pw_prompt_layout.h. */

#include "pw_prompt_layout.h"
#include "ns_scale.h"
#include "ns_type.h"

void pw_layout(int dpi, int has_detail, int has_text, int has_save, PwLayout *L)
{
    L->pad  = ns_scale(SP_MD, dpi);
    L->gap  = ns_scale(SP_SM, dpi);
    L->ctlh = ns_scale(SZ_CTRL_H, dpi);
    L->fw   = ns_scale(264, dpi);
    L->bw   = ns_scale(SZ_BTN_MIN_W, dpi);
    L->texth = has_text ? 4 * L->ctlh : 0;
    int y = L->pad;
    L->y_text = y;
    if (has_text) y += L->texth + L->gap;
    L->y_label = y;
    y += L->ctlh + L->gap;
    L->y_detail = y;
    if (has_detail) y += L->ctlh + L->gap;
    L->y_edit = y;
    y += L->ctlh;
    L->y_save = y + L->gap;
    y += has_save ? L->gap + L->ctlh + L->pad : L->pad;
    L->y_btn = y;
    L->client_h = y + L->ctlh + L->pad;
}

size_t pw_text_crlf(const char *in, char *out, size_t cap)
{
    size_t n = 0;
    if (cap == 0) return 0;
    if (in) {
        while (*in) {
            size_t need = 1;
            int pair = (*in == '\n') || (*in == '\r' && in[1] == '\n');
            if (pair) need = 2;
            if (n + need + 1 > cap) break;
            if (pair) {
                out[n++] = '\r';
                out[n++] = '\n';
                in += (*in == '\r') ? 2 : 1;
            } else {
                out[n++] = *in++;
            }
        }
    }
    out[n] = '\0';
    return n;
}
