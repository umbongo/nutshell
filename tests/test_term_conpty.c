#include "test_framework.h"
#include "term.h"
#include "term_extract.h"
#include "term_colors.h"
#include "ui_theme.h"
#include "theme.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ===========================================================================
 * Recorded ConPTY byte streams through the emulator
 * (docs/superpowers/specs/2026-09-22-local-shell-design.md section 8).
 *
 * A local session's bytes come from Windows's pseudo-console, not from an
 * SSH server, and they are not the same bytes: ConPTY re-renders whatever
 * the child wrote into its own screen updates, opens with
 * ESC[?9001h ESC[?1004h (win32-input-mode and focus-event reporting, both of
 * which handle_private_mode discards), clears with ESC[2J ESC[H, and sets
 * the window title with an OSC 0 string. Nothing else in the suite feeds
 * the emulator that dialect.
 *
 * The fixture is a real recording made on the maintainer's Windows box on
 * 2026-09-22: Git for Windows bash 5 -- which is what local_shell_resolve()
 * picked on that host at the time (busybox has since been removed as a
 * local-shell option entirely, 2026-09-24) -- running `ls -l` on a
 * four-entry directory, captured straight off the pseudo-console's pipe.
 * It is a fixture, not a golden master of the emulator: what is asserted is
 * that the *content* survives the dialect.
 *
 * Runs natively, on any host: it is a file of bytes and term_process().
 * ===========================================================================
 */

#define CONPTY_LS_FIXTURE "tests/fixtures/conpty_ls_gitbash.bin"

/* The fixture is read relative to the repo root, which is where `make test`
 * runs from. Returns NULL (and the test skips) if it is not there, the same
 * courtesy test_ui_tokens.c's gates extend to a wrong working directory. */
static char *read_fixture(const char *path, size_t *len_out)
{
    *len_out = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long len = ftell(f);
    if (len <= 0) { fclose(f); return NULL; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    char *buf = (char *)malloc((size_t)len + 1u);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1u, (size_t)len, f);
    fclose(f);
    buf[got] = '\0';
    *len_out = got;
    return buf;
}

/* The recorded `ls -l` lands on the screen: the total line, every entry with
 * its mode and size, and nothing from the ConPTY control traffic. */
int test_term_conpty_ls_screen_contents(void)
{
    TEST_BEGIN();
    size_t len = 0;
    char *bytes = read_fixture(CONPTY_LS_FIXTURE, &len);
    if (!bytes) {
        printf("  [conpty fixture] %s not readable from cwd -- skipping\n",
               CONPTY_LS_FIXTURE);
        TEST_END();
    }
    ASSERT_TRUE(len > 100u);

    Terminal *term = term_init(30, 100, 500);
    ASSERT_NOT_NULL(term);

    /* Fed in two uneven chunks, because the real transport delivers the
     * stream in whatever pieces ReadFile happens to return -- an escape
     * sequence split across a chunk boundary must still parse. */
    size_t first = len / 3u;
    term_process(term, bytes, first);
    term_process(term, bytes + first, len - first);

    char screen[8192];
    size_t n = term_extract_last_n(term, 40, screen, sizeof(screen));
    ASSERT_TRUE(n > 0u);

    ASSERT_TRUE(strstr(screen, "total 3") != NULL);
    ASSERT_TRUE(strstr(screen, "alpha.txt") != NULL);
    ASSERT_TRUE(strstr(screen, "bravo.txt") != NULL);
    ASSERT_TRUE(strstr(screen, "run.sh") != NULL);
    ASSERT_TRUE(strstr(screen, "subdir") != NULL);

    /* Modes and sizes, so this is the real listing and not just the names
     * appearing somewhere. */
    ASSERT_TRUE(strstr(screen, "-rw-r--r--") != NULL);
    ASSERT_TRUE(strstr(screen, "-rwxr-xr-x") != NULL);
    ASSERT_TRUE(strstr(screen, "drwxr-xr-x") != NULL);

    /* No control traffic leaked through as text: no raw ESC, and neither
     * the OSC title string nor a private-mode number on the screen. */
    ASSERT_TRUE(strchr(screen, 0x1B) == NULL);
    ASSERT_TRUE(strstr(screen, "?9001") == NULL);
    ASSERT_TRUE(strstr(screen, "?1004") == NULL);
    ASSERT_TRUE(strstr(screen, "bash.exe") == NULL);

    term_free(term);
    free(bytes);
    TEST_END();
}

/* Byte-at-a-time is the worst case for the state machine and must give the
 * same screen as the two-chunk feed above. */
int test_term_conpty_ls_byte_at_a_time_matches(void)
{
    TEST_BEGIN();
    size_t len = 0;
    char *bytes = read_fixture(CONPTY_LS_FIXTURE, &len);
    if (!bytes) {
        printf("  [conpty fixture] %s not readable from cwd -- skipping\n",
               CONPTY_LS_FIXTURE);
        TEST_END();
    }

    Terminal *whole = term_init(30, 100, 500);
    Terminal *drip  = term_init(30, 100, 500);
    ASSERT_NOT_NULL(whole);
    ASSERT_NOT_NULL(drip);

    term_process(whole, bytes, len);
    for (size_t i = 0; i < len; i++) term_process(drip, bytes + i, 1u);

    char a[8192], b[8192];
    size_t na = term_extract_last_n(whole, 40, a, sizeof(a));
    size_t nb = term_extract_last_n(drip,  40, b, sizeof(b));
    ASSERT_TRUE(na > 0u);
    ASSERT_EQ((int)na, (int)nb);
    ASSERT_STR_EQ(a, b);

    term_free(whole);
    term_free(drip);
    free(bytes);
    TEST_END();
}

/* The opening bytes on their own: ConPTY's two private modes are consumed,
 * the screen is cleared, and the cursor goes home -- so a local session
 * starts on a blank screen rather than showing the sequence as text.
 * Recorded on the same host: ESC[?9001h ESC[?1004h ESC[?25l ESC[2J ESC[m
 * ESC[H. No DSR (ESC[6n) or DA (ESC[c / ESC[>c) query appears anywhere in
 * the stream, which is why poll() has no write-back reply hook: the spec
 * made that hook conditional on this recording showing one. */
int test_term_conpty_opening_sequence_is_consumed(void)
{
    TEST_BEGIN();
    static const char OPEN[] =
        "\x1b[?9001h\x1b[?1004h\x1b[?25l\x1b[2J\x1b[m\x1b[H";

    Terminal *term = term_init(24, 80, 200);
    ASSERT_NOT_NULL(term);
    term_process(term, "stale text from before", 22);
    term_process(term, OPEN, sizeof(OPEN) - 1u);

    char screen[4096];
    (void)term_extract_last_n(term, 30, screen, sizeof(screen));
    /* ESC[2J cleared what was there, and nothing was printed as text. */
    ASSERT_TRUE(strstr(screen, "stale text") == NULL);
    ASSERT_TRUE(strstr(screen, "9001") == NULL);
    ASSERT_TRUE(strstr(screen, "1004") == NULL);
    ASSERT_TRUE(strchr(screen, 0x1B) == NULL);
    ASSERT_EQ(term->cursor.row, 0);
    ASSERT_EQ(term->cursor.col, 0);

    /* The fixture itself contains no terminal query. */
    size_t len = 0;
    char *bytes = read_fixture(CONPTY_LS_FIXTURE, &len);
    if (bytes) {
        for (size_t i = 0; i + 3u < len; i++) {
            if (bytes[i] != 0x1B || bytes[i + 1] != '[') continue;
            ASSERT_TRUE(!(bytes[i + 2] == '6' && bytes[i + 3] == 'n'));
            ASSERT_TRUE(bytes[i + 2] != 'c');
            ASSERT_TRUE(!(bytes[i + 2] == '>' && bytes[i + 3] == 'c'));
        }
        free(bytes);
    }

    term_free(term);
    TEST_END();
}

/* ===========================================================================
 * Windows PowerShell 5.1 through ConPTY (spec
 * docs/superpowers/specs/2026-10-19-light-theme-ansi-contrast-design.md,
 * sections 1 and 5.0). Captured on the maintainer's Windows box on
 * 2026-10-20 (Nutshell v1.2.41, a local PowerShell tab at 29 x 71, 192 DPI;
 * PSReadLine 2.0.0, which has no inline prediction) from the debug log's
 * term.rx lines, with the log's escaping undone:
 *   conpty_ps51_tab_completion.bin  log lines 15-51: the spawn, the prompt,
 *                                   "Cl" typed and Tab cycled to Clear-Host
 *   conpty_ps51_commands.bin        log lines 90-112: a three-command line,
 *                                   its error, recalled and run again, then
 *                                   Get-Command pwsh -ErrorAction ...
 * The hand-written `93m` string in tests/test_term.c stays: it tests prompt
 * detection, which these captures do not need.
 * ===========================================================================
 */

#define PS51_TAB_FIXTURE      "tests/fixtures/conpty_ps51_tab_completion.bin"
#define PS51_COMMANDS_FIXTURE "tests/fixtures/conpty_ps51_commands.bin"

/* A row's printable ASCII, '?' for anything else, trailing blanks cut. */
static void row_text(const TermRow *row, int cols, char *out, size_t cap)
{
    size_t n = 0;
    for (int c = 0; c < cols && n + 1u < cap; c++) {
        uint32_t cp = row->cells[c].codepoint;
        out[n++] = (cp >= 0x20u && cp < 0x7Fu) ? (char)cp : (cp == 0u ? ' ' : '?');
    }
    while (n > 0u && out[n - 1u] == ' ') n--;
    out[n] = '\0';
}

/* The cell's painted foreground on theme `t` (0..NUM_UI_THEMES-1). */
static uint32_t painted_fg(int t, const TermCell *cell)
{
    static ThemeTokens tok;
    ui_theme_resolve(ui_theme_get(t), &tok);
    uint32_t fg = 0u, bg = 0u;
    term_colors_resolve(&tok.term, cell, 0u, &fg, &bg);
    return fg;
}

/* What a palette cell must paint on every theme: the floored value on the
 * light themes (and at least their ratio), the raw palette on the dark. */
static void check_indexed(const TermCell *cell, uint8_t mode, uint8_t index,
                          uint32_t on_onyx_light, uint32_t on_moss_mist, int *fail)
{
    if (cell->attr.fg_mode != mode || cell->attr.fg_index != index ||
        cell->attr.bg_mode != COLOR_DEFAULT) {
        printf("  cell U+%04X: fg_mode=%d fg_index=%d bg_mode=%d, expected %d/%d on the default bg\n",
               (unsigned)cell->codepoint, (int)cell->attr.fg_mode, (int)cell->attr.fg_index,
               (int)cell->attr.bg_mode, (int)mode, (int)index);
        *fail = 1;
        return;
    }
    for (int t = 0; t < NUM_UI_THEMES; t++) {
        const ThemeColors *base = ui_theme_get(t);
        uint32_t fg = painted_fg(t, cell);
        uint32_t want = color256_to_rgb(index);
        if (strcmp(base->name, "Onyx Light") == 0) want = on_onyx_light;
        if (strcmp(base->name, "Moss & Mist") == 0) want = on_moss_mist;
        if (fg != want) {
            printf("  index %d on %s paints %06X, expected %06X\n", (int)index,
                   base->name, (unsigned)fg, (unsigned)want);
            *fail = 1;
        }
        if (base->terminal_min_contrast > 0.0 &&
            theme_contrast(fg, base->terminal_bg) < base->terminal_min_contrast) {
            printf("  index %d on %s is below its floor\n", (int)index, base->name);
            *fail = 1;
        }
    }
}

/* PSReadLine 2.0's command token (SGR 93), redrawn on every Tab with
 * cursor moves, ECH and Backspace, fed a byte at a time: it ends as
 * "Clear-Host" in palette index 11, floored to 727223 on the light themes
 * and the raw FFFF55 on the dark ones. */
int test_term_conpty_ps51_tab_completion_token_is_floored(void)
{
    TEST_BEGIN();
    size_t len = 0;
    char *bytes = read_fixture(PS51_TAB_FIXTURE, &len);
    if (!bytes) {
        printf("  [ps51 fixture] %s not readable from cwd -- skipping\n", PS51_TAB_FIXTURE);
        TEST_END();
    }
    ASSERT_EQ(len, 757u);
    Terminal *term = term_init(29, 71, 500);
    ASSERT_NOT_NULL(term);
    if (!term) { free(bytes); TEST_END(); }
    for (size_t i = 0; i < len; i++) term_process(term, bytes + i, 1u);

    const TermRow *row = term->lines[term_screen_to_phys(term, 0)];
    char text[128];
    row_text(row, term->cols, text, sizeof(text));
    ASSERT_STR_EQ(text, "PS C:\\Users\\thoma> Clear-Host");
    ASSERT_EQ(term->cursor.row, 0);
    ASSERT_EQ(term->cursor.col, 29);
    int fail = 0;
    for (int c = 19; c < 29; c++)
        check_indexed(&row->cells[c], COLOR_ANSI16, 11, 0x727223u, 0x727223u, &fail);
    ASSERT_EQ(fail, 0);
    ASSERT_EQ(row->cells[0].attr.fg_mode, COLOR_DEFAULT);
    int onyx_light = ui_theme_find("Onyx Light");
    ASSERT_TRUE(onyx_light > 0);                           /* 0 is also "not found" */
    ASSERT_EQ(painted_fg(onyx_light, &row->cells[0]), 0x1D1D1Fu);   /* Onyx Light terminal_fg */
    term_free(term);
    free(bytes);
    TEST_END();
}

/* The rest of PSReadLine 2.0's palette as it arrives: command tokens 93,
 * arguments 37, parameters 90, and the error record in 256-colour 9. All
 * on the default background, so all floored on the light themes and raw on
 * the dark ones. */
int test_term_conpty_ps51_commands_are_floored(void)
{
    TEST_BEGIN();
    size_t len = 0;
    char *bytes = read_fixture(PS51_COMMANDS_FIXTURE, &len);
    if (!bytes) {
        printf("  [ps51 fixture] %s not readable from cwd -- skipping\n", PS51_COMMANDS_FIXTURE);
        TEST_END();
    }
    ASSERT_EQ(len, 1042u);
    Terminal *term = term_init(29, 71, 500);
    ASSERT_NOT_NULL(term);
    if (!term) { free(bytes); TEST_END(); }
    size_t first = len / 3u;
    term_process(term, bytes, first);
    term_process(term, bytes + first, len - first);

    const TermRow *cmd = NULL, *err = NULL;
    for (int r = 0; r < term->rows; r++) {
        const TermRow *row = term->lines[term_screen_to_phys(term, r)];
        char text[128];
        row_text(row, term->cols, text, sizeof(text));
        if (strcmp(text, "PS C:\\Users\\thoma> Get-Command pwsh -ErrorAction SilentlyContinue") == 0)
            cmd = row;
        static const char err_head[] = "InvalidArgument: (:) [Select-Object]";
        if (strncmp(text, err_head, sizeof(err_head) - 1u) == 0)
            err = row;
    }
    ASSERT_NOT_NULL(cmd);
    ASSERT_NOT_NULL(err);
    int fail = 0;
    if (cmd) {
        check_indexed(&cmd->cells[19], COLOR_ANSI16, 11, 0x727223u, 0x727223u, &fail); /* G of Get-Command */
        check_indexed(&cmd->cells[31], COLOR_ANSI16, 7,  0x707070u, 0x6B6B6Bu, &fail); /* p of pwsh */
        check_indexed(&cmd->cells[36], COLOR_ANSI16, 8,  0x6C6C6Cu, 0x6C6C6Cu, &fail); /* - of -ErrorAction */
    }
    if (err)
        check_indexed(&err->cells[0], COLOR_256, 9, 0xC24040u, 0xC24040u, &fail);    /* I of InvalidArgument */
    ASSERT_EQ(fail, 0);
    term_free(term);
    free(bytes);
    TEST_END();
}

/* SYNTHETIC, UNVERIFIED: PowerShell 7's inline prediction. No PowerShell 7
 * capture exists (the maintainer's box has none; PSReadLine 2.0 has no
 * predictions), so these bytes are written by hand from PSReadLine's
 * documented defaults: 2.2 and later paint the prediction 97;2;3, 2.1
 * 38;5;238. With dim it paints fainter than the typed token on both light
 * themes; 238 (444444) is above 4.5:1 already, so it paints as sent --
 * apart from the typed text but darker than it. Replace with a capture if
 * one is ever made. */
int test_term_conpty_pwsh7_prediction_synthetic(void)
{
    TEST_BEGIN();
    static const struct { const char *bytes; uint32_t on_onyx_light, on_moss_mist; } v[2] = {
        { "\x1b[2J\x1b[HPS C:\\> \x1b[93mG\x1b[97;2;3met-ChildItem\x1b[0m\x1b[12D",
          0xA3A3A4u, 0xA1A2A1u },
        { "\x1b[2J\x1b[HPS C:\\> \x1b[93mG\x1b[38;5;238met-ChildItem\x1b[0m\x1b[12D",
          0x444444u, 0x444444u },
    };
    for (int k = 0; k < 2; k++) {
        Terminal *term = term_init(29, 71, 500);
        ASSERT_NOT_NULL(term);
        if (!term) continue;
        term_process(term, v[k].bytes, strlen(v[k].bytes));
        const TermRow *row = term->lines[term_screen_to_phys(term, 0)];
        ASSERT_EQ(term->cursor.col, 9);                    /* back after the typed G */
        const TermCell *typed = &row->cells[8], *pred = &row->cells[9];
        ASSERT_EQ(typed->codepoint, (uint32_t)'G');
        ASSERT_EQ(pred->codepoint, (uint32_t)'e');
        for (int t = 0; t < NUM_UI_THEMES; t++) {
            const ThemeColors *base = ui_theme_get(t);
            uint32_t pf = painted_fg(t, pred), tf = painted_fg(t, typed);
            if (strcmp(base->name, "Onyx Light") == 0) { ASSERT_EQ(pf, v[k].on_onyx_light); }
            if (strcmp(base->name, "Moss & Mist") == 0) { ASSERT_EQ(pf, v[k].on_moss_mist); }
            if (base->terminal_min_contrast > 0.0) {
                ASSERT_TRUE(pf != tf);
                if (k == 0) { ASSERT_TRUE(theme_lstar(pf) > theme_lstar(tf) + 10.0); }
            }
        }
        term_free(term);
    }
    TEST_END();
}

/* The resize fixtures (tests/fixtures/conpty_resize_*.bin, recorded by the
 * wintest case test_local_pty_resize_parity) carry an in-band marker,
 * ESC ] 9999 ; resize=COLSxROWS BEL, where each ResizePseudoConsole was
 * made, so a replay knows the model's size at that point. The emulator must
 * ignore it: no text, no cursor move, no title. */
int test_term_conpty_resize_marker_is_inert(void)
{
    TEST_BEGIN();
    static const char MARK[] = "\x1b]9999;resize=25x12\x07";
    Terminal *plain  = term_init(10, 40, 100);
    Terminal *marked = term_init(10, 40, 100);
    ASSERT_NOT_NULL(plain);
    ASSERT_NOT_NULL(marked);

    term_process(plain, "abc\r\ndef", 8);
    term_process(marked, "abc\r\ndef", 8);
    term_process(marked, MARK, sizeof(MARK) - 1u);
    term_process(plain, "ghi", 3);
    term_process(marked, "ghi", 3);

    char a[1024], b[1024];
    (void)term_extract_last_n(plain, 10, a, sizeof(a));
    (void)term_extract_last_n(marked, 10, b, sizeof(b));
    ASSERT_STR_EQ(a, b);
    ASSERT_TRUE(strstr(b, "9999") == NULL);
    ASSERT_TRUE(strstr(b, "resize") == NULL);
    ASSERT_EQ(marked->cursor.row, plain->cursor.row);
    ASSERT_EQ(marked->cursor.col, plain->cursor.col);
    ASSERT_STR_EQ(marked->title, plain->title);

    term_free(plain);
    term_free(marked);
    TEST_END();
}
