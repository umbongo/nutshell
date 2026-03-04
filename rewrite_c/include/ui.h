#ifndef UI_H
#define UI_H

#ifdef _WIN32
#include <windows.h>

void ui_init(HINSTANCE instance);
void ui_run(void);
#endif

#endif /* UI_H */