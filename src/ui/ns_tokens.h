#ifndef NUTSHELL_NS_TOKENS_H
#define NUTSHELL_NS_TOKENS_H

/* Accessor for window.c's resolved ThemeTokens (Design-System Foundation,
 * task 2).  ui_theme_resolve() is called once, whenever g_theme is chosen
 * or changed, into a private g_tokens; this header just exposes a
 * read-only pointer to it for other src/ui translation units to use as
 * they migrate off raw ThemeColors/RGB() (Task 4 onward). */

#include "ui_theme.h"

const ThemeTokens *ns_tokens(void);

#endif /* NUTSHELL_NS_TOKENS_H */
