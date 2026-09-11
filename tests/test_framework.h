#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#endif

/* Scratch directory for tests that write files.  Linux: /tmp.  Windows: the
 * build/ directory (a MinGW binary has no /tmp), relative to the repo root
 * where `make test` runs. */
#ifdef _WIN32
#define TEST_TMP_DIR "build"
#else
#define TEST_TMP_DIR "/tmp"
#endif

/* Create a scratch file for writing, readable and writable only by its owner.
 * A plain fopen() creates with 0666 masked by the umask, which on a shared
 * machine can leave a world-writable file behind -- and which CodeQL's
 * "file created without restricting permissions" rule flags. Tests that write
 * a config or fixture file should use this instead of fopen(path, "w").
 * Returns NULL on failure, like fopen(). */
static inline FILE *test_fopen_private(const char *path)
{
#ifdef _WIN32
    int fd = _open(path, _O_WRONLY | _O_CREAT | _O_TRUNC, _S_IREAD | _S_IWRITE);
    if (fd < 0) return NULL;
    return _fdopen(fd, "w");
#else
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    if (fd < 0) return NULL;
    return fdopen(fd, "w");
#endif
}

extern int _tf_failed;
extern int _tf_run;

#define TEST_BEGIN() int _tf_local_fail = 0; printf("[RUN ] %s\n", __func__)

#define TEST_END() \
    do { \
        _tf_run++; \
        if (_tf_local_fail == 0) { \
            printf("[PASS] %s\n", __func__); \
            return 0; \
        } else { \
            printf("[FAIL] %s\n", __func__); \
            _tf_failed++; \
            return 1; \
        } \
    } while(0)

#define ASSERT_TRUE(cond) if (!(cond)) { printf("  Assertion failed: %s\n", #cond); _tf_local_fail = 1; }
#define ASSERT_FALSE(cond) if (cond) { printf("  Assertion failed: !(%s)\n", #cond); _tf_local_fail = 1; }
#define ASSERT_EQ(a, b) if ((a) != (b)) { printf("  Assertion failed: %s == %s (%lld != %lld)\n", #a, #b, (long long)(uintptr_t)(a), (long long)(uintptr_t)(b)); _tf_local_fail = 1; }
#define ASSERT_STR_EQ(a, b) if (strcmp(a, b) != 0) { printf("  Assertion failed: strcmp(%s, %s) == 0 (\"%s\" != \"%s\")\n", #a, #b, a, b); _tf_local_fail = 1; }
#define ASSERT_NULL(ptr) if ((ptr) != NULL) { printf("  Assertion failed: %s == NULL\n", #ptr); _tf_local_fail = 1; }
#define ASSERT_NOT_NULL(ptr) if ((ptr) == NULL) { printf("  Assertion failed: %s != NULL\n", #ptr); _tf_local_fail = 1; }

#endif