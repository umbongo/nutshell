#ifndef NUTSHELL_CONFIG_H
#define NUTSHELL_CONFIG_H

#include <stddef.h>

/* Config filename — single source of truth. */
#define CONFIG_FILENAME "nutshell.config"

#define CFG_STR_MAX ((size_t)256)
#ifndef AI_NOTES_MAX
#define AI_NOTES_MAX ((size_t)2560)
#endif

#include "profile.h"
#include "../core/vector.h"
#include "../core/cmd_policy.h"

typedef struct {
    char font[CFG_STR_MAX];
    char ai_font[CFG_STR_MAX];
    int font_size;
    int scrollback_lines;
    int paste_delay_ms;
    int logging_enabled;
    char log_format[CFG_STR_MAX];
    char log_dir[CFG_STR_MAX];
    int debug_terminal;  /* raw terminal data log for escape-sequence debugging */
    char host_key_verification[CFG_STR_MAX]; /* "strict" refuses an unknown or changed
                                                 host key without prompting; anything
                                                 else (default "tofu") prompts. No UI. */
    char foreground_colour[CFG_STR_MAX];
    char background_colour[CFG_STR_MAX];
    char colour_scheme[CFG_STR_MAX];
    char ai_provider[CFG_STR_MAX];
    char ai_api_key[CFG_STR_MAX];
    /* Verbatim copy of the last encrypted API-key blob this process could
     * NOT decrypt (moved from another user/PC, or corrupt). See
     * Profile.password_enc_preserved in profile.h for the exact rules --
     * this field follows the same load/save contract. */
    char ai_api_key_enc_preserved[CFG_BLOB_MAX];
    char ai_custom_url[CFG_STR_MAX];
    char ai_custom_model[CFG_STR_MAX];
    char ai_system_notes[AI_NOTES_MAX];
    char ai_search_provider[64];     /* "none", "duckduckgo-api", "duckduckgo-html", "custom" */
    char ai_search_url[256];         /* for custom search endpoint only */
    int  ai_max_search_results;      /* 1-20, default 7 */
    int  ai_web_fetch_enabled;       /* 0 = disabled (default), 1 = enabled */
    int  ssh_user_idle_timeout_mins; /* 0 = never; default 0 */
    int  markdown_render_enabled;    /* render AI replies as markdown */
    int  ai_max_context_lines;       /* terminal lines sent to the AI; 1-50000, default 1000 */
    int  auto_connect;                       /* connect at startup: 0 = off (default) */
    char auto_connect_session[CFG_STR_MAX];  /* session name (or host) to auto-connect */
    int  paste_confirm;                  /* confirm before pasting: 1 = on (default) */
    int  open_session_manager_at_start;  /* show Session Manager at startup: 0 = off (default) */
    CmdPolicy ai_policy_default;         /* Command policy a new AI session starts from:
                                           * the `allowed` ceiling and the `unattended`
                                           * marker. Default {read, none} -- read-only,
                                           * nothing runs without asking. Persisted as one
                                           * "<allowed>/<unattended>" token under
                                           * "ai_policy_default"; the v1.1.16
                                           * "ai_auto_approve_mode" key and the older
                                           * numeric "ai_auto_approve_default" are migrated
                                           * on load (see loader.c). */
} Settings;

typedef struct {
    Settings settings;
    Vector profiles; /* Vector of Profile* */
} Config;

Config *config_new_default(void);
Config *config_load(const char *path);
int config_save(const Config *cfg, const char *path);
void config_free(Config *cfg);

void config_default_settings(Settings *s);
void settings_validate(Settings *s);
Profile *config_profile_new(void);
void config_profile_free(Profile *p);

/* Case-insensitive exact lookup. Empty fields never match; first match
 * wins on duplicates. Returns NULL when not found or on NULL/empty input. */
Profile *config_find_profile_by_name(const Config *cfg, const char *name);
Profile *config_find_profile_by_host(const Config *cfg, const char *host);

/* Insert a saved "Local shell" profile at index 0 when the config has no
 * profile of kind "local" yet (spec section 5). Returns 1 when it
 * inserted one (the caller should then config_save()), 0 when a local
 * profile already existed or cfg is NULL. This is a plain saved profile
 * like any other -- Edit and Delete act on it by index, and deleting it
 * means it does NOT come back unless the whole config has no local
 * profile at the next start. */
int config_ensure_local_profile(Config *cfg);

/* ---- Secret-field helpers (see loader.c's load/save contract comment) ---- */

/* M-4: when the caller has just stored a new, user-supplied value into a
 * secret's plaintext field (a password or API key edit box), call this to
 * drop any foreign/corrupt blob still sitting in the matching `preserved`
 * field. Without it, a value entered and then cleared again later in the
 * same run (without reloading the config from disk) would resurrect the
 * stale blob on save: save writes `preserved` back verbatim the moment
 * `plain` is empty, and nothing else clears it in memory between the two
 * saves. No-op when `plain` is empty (nothing new was supplied). */
void config_secret_drop_stale_preserved(const char *plain, char *preserved,
                                         size_t preserved_cap);

/* Build an absolute, per-user fallback config path,
 * "<local_appdata>\Nutshell\nutshell.config", for use when the caller
 * cannot determine the exe's own directory. Falling back to a bare
 * relative CONFIG_FILENAME instead would silently read or write into the
 * process's current working directory -- whatever that happens to be.
 * Returns 1 and fills `out` when `local_appdata` is non-empty and the
 * result fits; returns 0 (out untouched) otherwise, so the caller can
 * refuse to save rather than fall back to the CWD. Pure string logic --
 * no filesystem access -- so it is callable from a native test without
 * Windows APIs; the caller creates the "Nutshell" directory itself. */
int config_fallback_path(const char *local_appdata, char *out, size_t out_cap);

#endif