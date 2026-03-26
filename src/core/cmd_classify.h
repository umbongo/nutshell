/* src/core/cmd_classify.h */
#ifndef NUTSHELL_CMD_CLASSIFY_H
#define NUTSHELL_CMD_CLASSIFY_H

#include <stddef.h>

typedef enum {
    CMD_SAFE,       /* Read-only, always allowed */
    CMD_WRITE,      /* Modifies state, requires permit_write */
    CMD_CRITICAL    /* Can cause outage/data loss, requires permit_write + visual warning */
} CmdSafetyLevel;

typedef enum {
    CMD_PLATFORM_LINUX,
    CMD_PLATFORM_CISCO_IOS,
    CMD_PLATFORM_CISCO_NXOS,
    CMD_PLATFORM_CISCO_ASA,
    CMD_PLATFORM_ARUBA_CX,
    CMD_PLATFORM_ARUBA_OS,
    CMD_PLATFORM_PANOS
} CmdPlatform;

/* Classify a single command string.
 * Returns CMD_SAFE for NULL or empty input.
 * For pipelines/semicolons, returns the highest risk level across all segments. */
CmdSafetyLevel cmd_classify(const char *command, CmdPlatform platform);

/* Classify with detail: fills reason buffer with human-readable explanation.
 * reason_buf may be NULL. Returns safety level. */
CmdSafetyLevel cmd_classify_ex(const char *command, CmdPlatform platform,
                                char *reason_buf, size_t reason_buf_size);

#endif /* NUTSHELL_CMD_CLASSIFY_H */
