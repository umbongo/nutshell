#include "test_framework.h"
#include "dll_record.h"
#include <stdio.h>
#include <string.h>

int test_dll_record_note_and_query(void)
{
    TEST_BEGIN();
    dll_record_reset();
    ASSERT_EQ(dll_record_was_requested("winhttp.dll"), 0);
    dll_record_note("winhttp.dll");
    ASSERT_EQ(dll_record_was_requested("winhttp.dll"), 1);
    ASSERT_EQ(dll_record_was_requested("riched20.dll"), 0);
    dll_record_reset();
    TEST_END();
}

int test_dll_record_case_insensitive(void)
{
    TEST_BEGIN();
    dll_record_reset();
    dll_record_note("Riched20.dll");
    ASSERT_EQ(dll_record_was_requested("riched20.DLL"), 1);
    ASSERT_EQ(dll_record_was_requested("RICHED20.DLL"), 1);
    dll_record_reset();
    TEST_END();
}

int test_dll_record_duplicates_once(void)
{
    TEST_BEGIN();
    dll_record_reset();
    dll_record_note("a.dll");
    dll_record_note("A.DLL");
    dll_record_note("a.dll");
    char buf[256];
    size_t n = dll_record_format(buf, sizeof(buf), NULL, 0);
    ASSERT_STR_EQ(buf, "requested: a.dll\n");
    ASSERT_EQ(n, strlen(buf));
    dll_record_reset();
    TEST_END();
}

int test_dll_record_null_and_empty_ignored(void)
{
    TEST_BEGIN();
    dll_record_reset();
    dll_record_note(NULL);
    dll_record_note("");
    ASSERT_EQ(dll_record_was_requested(NULL), 0);
    ASSERT_EQ(dll_record_was_requested(""), 0);
    char buf[64];
    ASSERT_EQ(dll_record_format(buf, sizeof(buf), NULL, 0), (size_t)0);
    ASSERT_STR_EQ(buf, "");
    TEST_END();
}

int test_dll_record_cap_of_64(void)
{
    TEST_BEGIN();
    dll_record_reset();
    char name[32];
    for (int i = 0; i < 70; i++) {
        (void)snprintf(name, sizeof(name), "d%d.dll", i);
        dll_record_note(name);
    }
    ASSERT_EQ(dll_record_was_requested("d0.dll"), 1);
    ASSERT_EQ(dll_record_was_requested("d63.dll"), 1);
    ASSERT_EQ(dll_record_was_requested("d64.dll"), 0);
    ASSERT_EQ(dll_record_was_requested("d69.dll"), 0);
    /* a name already held is still found once the table is full */
    dll_record_note("d5.dll");
    dll_record_reset();
    TEST_END();
}

int test_dll_record_overlong_name_ignored(void)
{
    TEST_BEGIN();
    dll_record_reset();
    char big[200];
    memset(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    dll_record_note(big);
    ASSERT_EQ(dll_record_was_requested(big), 0);
    dll_record_reset();
    TEST_END();
}

int test_dll_record_format_with_modules(void)
{
    TEST_BEGIN();
    dll_record_reset();
    dll_record_note("one.dll");
    dll_record_note("two.dll");
    const char *mods[] = { "kernel32.dll", "user32.dll" };
    char buf[512];
    size_t n = dll_record_format(buf, sizeof(buf), mods, 2);
    ASSERT_STR_EQ(buf,
        "requested: one.dll\nrequested: two.dll\n"
        "module: kernel32.dll\nmodule: user32.dll\n");
    ASSERT_EQ(n, strlen(buf));
    dll_record_reset();
    TEST_END();
}

int test_dll_record_format_modules_only(void)
{
    TEST_BEGIN();
    dll_record_reset();
    const char *mods[] = { "m.dll" };
    char buf[64];
    dll_record_format(buf, sizeof(buf), mods, 1);
    ASSERT_STR_EQ(buf, "module: m.dll\n");
    TEST_END();
}

int test_dll_record_format_truncates_whole_lines(void)
{
    TEST_BEGIN();
    dll_record_reset();
    dll_record_note("aaaa.dll");
    dll_record_note("bbbb.dll");
    char buf[24]; /* "requested: aaaa.dll\n" is 20 bytes; a second does not fit */
    size_t n = dll_record_format(buf, sizeof(buf), NULL, 0);
    ASSERT_STR_EQ(buf, "requested: aaaa.dll\n");
    ASSERT_EQ(n, (size_t)20);
    ASSERT_EQ(dll_record_format(buf, 0, NULL, 0), (size_t)0);
    dll_record_reset();
    TEST_END();
}
