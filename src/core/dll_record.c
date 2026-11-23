/* src/core/dll_record.c -- see dll_record.h */
#include "dll_record.h"
#include <stdio.h>
#include <string.h>

static char s_names[DLL_RECORD_MAX][DLL_RECORD_NAME];
static size_t s_count;
static int s_lock;

static void lock(void)
{
    while (__atomic_test_and_set(&s_lock, __ATOMIC_ACQUIRE)) { }
}

static void unlock(void)
{
    __atomic_clear(&s_lock, __ATOMIC_RELEASE);
}

static char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

static int eq_nocase(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
        if (lower(*a) != lower(*b)) return 0;
    return *a == *b;
}

static int find_locked(const char *name)
{
    for (size_t i = 0; i < s_count; i++)
        if (eq_nocase(s_names[i], name)) return 1;
    return 0;
}

void dll_record_note(const char *name)
{
    if (name == NULL || name[0] == '\0') return;
    if (strlen(name) >= DLL_RECORD_NAME) return;
    lock();
    if (!find_locked(name) && s_count < DLL_RECORD_MAX) {
        memcpy(s_names[s_count], name, strlen(name) + 1u);
        s_count++;
    }
    unlock();
}

int dll_record_was_requested(const char *name)
{
    if (name == NULL || name[0] == '\0') return 0;
    lock();
    int r = find_locked(name);
    unlock();
    return r;
}

static size_t put_line(char *out, size_t cap, size_t len, const char *tag,
                       const char *name)
{
    int w = snprintf(out + len, cap - len, "%s: %s\n", tag, name);
    if (w < 0 || (size_t)w >= cap - len) {
        out[len] = '\0'; /* does not fit: drop the partial line */
        return len;
    }
    return len + (size_t)w;
}

size_t dll_record_format(char *out, size_t cap, const char *const *modules,
                         size_t n)
{
    if (out == NULL || cap == 0) return 0;
    out[0] = '\0';
    size_t len = 0;
    lock();
    for (size_t i = 0; i < s_count; i++)
        len = put_line(out, cap, len, "requested", s_names[i]);
    unlock();
    for (size_t i = 0; modules != NULL && i < n; i++)
        if (modules[i] != NULL)
            len = put_line(out, cap, len, "module", modules[i]);
    return len;
}

void dll_record_reset(void)
{
    lock();
    s_count = 0;
    unlock();
}
