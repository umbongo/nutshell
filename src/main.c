#include <winsock2.h>  /* must precede windows.h */
#include <windows.h>
#include "ui/ui.h"
#include "ui/icons.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    /* I-2: initialise WSA once for the process lifetime */
    WSADATA wsadata;
    WSAStartup(MAKEWORD(2, 2), &wsadata);

    ns_icons_init();
    ui_init(hInstance);
    ui_run();
    ns_icons_shutdown();

    WSACleanup();
    return 0;
}