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
    CMD_PLATFORM_PANOS,
    CMD_PLATFORM_HP_PROCURVE,
    CMD_PLATFORM_HP_COMWARE,
    CMD_PLATFORM_JUNOS,
    CMD_PLATFORM_FORTIOS,
    CMD_PLATFORM_VYOS,
    CMD_PLATFORM_MIKROTIK,
    /* Not a device family: the platform of a session that has not been told
     * what it is talking to, and whose banner has not resolved it. Classifies
     * as Linux plus an overlay of network verbs that are destructive on some
     * CLI and are not Linux commands at all. Appended last so the values above
     * keep their numbers. */
    CMD_PLATFORM_UNKNOWN
} CmdPlatform;

/* Classify a single command string.
 * Returns CMD_SAFE for NULL or empty input.
 * For pipelines/semicolons, returns the highest risk level across all segments. */
CmdSafetyLevel cmd_classify(const char *command, CmdPlatform platform);

/* Classify with detail: fills reason buffer with human-readable explanation.
 * reason_buf may be NULL. Returns safety level. */
CmdSafetyLevel cmd_classify_ex(const char *command, CmdPlatform platform,
                                char *reason_buf, size_t reason_buf_size);

/* Config-token and UI-label mappings for the platform setting.
 * cmd_platform_from_name() maps an unrecognised or NULL name -- and the
 * "auto" token, which means "no override, detect it" -- to
 * CMD_PLATFORM_UNKNOWN, which is the correct state for a session awaiting
 * detection. cmd_platform_name() returns the stable config token. */
CmdPlatform cmd_platform_from_name(const char *name);
const char *cmd_platform_name(CmdPlatform platform);
const char *cmd_platform_label(CmdPlatform platform);

/* Number of entries in the platform dropdown, "Auto-detect" included, and the
 * config token / label for dropdown row `index`. Returns NULL out of range. */
int         cmd_platform_choice_count(void);
const char *cmd_platform_choice_name(int index);
const char *cmd_platform_choice_label(int index);

#endif /* NUTSHELL_CMD_CLASSIFY_H */
