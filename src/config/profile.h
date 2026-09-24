#ifndef NUTSHELL_CONFIG_PROFILE_H
#define NUTSHELL_CONFIG_PROFILE_H

#ifdef _WIN32
#include <windows.h>
#else
#include <limits.h>
#ifndef MAX_PATH
#define MAX_PATH 4096
#endif
#endif

typedef enum {
    AUTH_PASSWORD = 0,
    AUTH_KEY = 1
} AuthType;

/* Size of a preserved encrypted-secret blob field (raw "$dpapi$v1$..." or
 * legacy "$aes256gcm$v1$..." string, verbatim). A DPAPI blob for a
 * CFG_STR_MAX(256)-byte plaintext is a few hundred bytes once base64'd
 * (DPAPI's own fixed overhead plus the description string); this leaves
 * comfortable headroom and safely bounds even garbage read from a
 * hand-edited or corrupt config file. */
#ifndef CFG_BLOB_MAX
#define CFG_BLOB_MAX ((size_t)1024)
#endif

typedef struct {
    char name[256];
    char kind[16];        /* "ssh" (default) or "local" */
    char host[256];
    int port;
    char username[256];
    AuthType auth_type;
    char password[256]; // Or passphrase for key
    /* Verbatim copy of the last encrypted password blob this process could
     * NOT decrypt for this profile (moved from another user/PC, or
     * corrupt). Empty when the password decrypted fine or there is none.
     * config_save() writes this back unchanged whenever `password` is
     * empty, so moving the config back to its original PC/user still
     * works; it is only replaced when the user enters a new password. */
    char password_enc_preserved[CFG_BLOB_MAX];
    char key_path[MAX_PATH];
    /* local only: custom command line; empty = automatic. Sized to match
     * src/core/local_shell.h's LOCAL_SHELL_CMD_MAX (1024), not MAX_PATH
     * (260) -- a shell command line is "<quoted path> <args>", which can
     * exceed a bare path's length well before it exceeds the shell's own
     * command-line limit. profile.h can't include local_shell.h (config/
     * doesn't depend on core/), so this is a plain literal kept in step
     * with it by hand; tests/test_local_shell.c's
     * test_local_shell_env_count_never_exceeds_max-style assertions would
     * need updating if the two ever needed to diverge. */
    char shell[1024];
    char platform[32]; // Device platform token: "auto" (default), "linux", "cisco-ios", ...
#ifndef AI_NOTES_MAX
#define AI_NOTES_MAX 2560
#endif
    char ai_notes[AI_NOTES_MAX]; // Per-session notes for AI context
} Profile;

#endif