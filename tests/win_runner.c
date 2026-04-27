/* tests/win_runner.c — Minimal Win32 test runner. Builds only under
 * MinGW (via `make wintest`), runs via Wine or on real Windows.
 * Covers tests that require GDI / GDI+ at runtime (icon renderer). */

#ifdef _WIN32

#include <stdio.h>
#include "test_framework.h"

int _tf_failed = 0;
int _tf_run    = 0;

int test_icons_all_render_non_empty(void);
int test_icons_render_at_multiple_sizes(void);

int main(void)
{
    int failed = 0;
    failed += test_icons_all_render_non_empty();
    failed += test_icons_render_at_multiple_sizes();

    printf("\nTests Run: %d, Failed: %d\n", _tf_run, _tf_failed);
    return failed ? 1 : 0;
}

#endif /* _WIN32 */
