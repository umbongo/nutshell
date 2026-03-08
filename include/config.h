#ifndef CONFIG_H
#define CONFIG_H

/*
 * Application configuration — Settings and SSH Profiles.
 *
 * Config is persisted as JSON (config.json).  All string fields use
 * fixed-size arrays so no extra heap allocation is needed per field.
 *
 * Usage:
 *   Config *cfg = config_load("config.json");  // NULL if file absent/bad
 *   if (!cfg) cfg = config_new_default();
 *   // ... use cfg ...
 *   config_free(cfg);
 */

#include "vector.h"
#include <stddef.h>

/* Maximum length for string fields (including null terminator). */
#define CFG_STR_MAX ((size_t)256)

/* ---- Authentication type -------------------------------------------------- */

typedef enum {
    AUTH_PASSWORD = 0,
    AUTH_KEY
} AuthType;

/* ---- Settings ------------------------------------------------------------- */

typedef struct {
    char   font[CFG_STR_MAX];
    int    font_size;
    int    scrollback_lines;
    int    paste_delay_ms;
    int    logging_enabled;
    char   log_format[CFG_STR_MAX];
    char   log_dir[CFG_STR_MAX];
    char   host_key_verification[CFG_STR_MAX];
    char   foreground_colour[CFG_STR_MAX];
    char   background_colour[CFG_STR_MAX];
    char   colour_scheme[CFG_STR_MAX];
    char   ai_provider[CFG_STR_MAX];
    char   ai_api_key[CFG_STR_MAX];
    char   ai_custom_url[CFG_STR_MAX];
    char   ai_custom_model[CFG_STR_MAX];
} Settings;

/* ---- Profile -------------------------------------------------------------- */

typedef struct {
    char     name[CFG_STR_MAX];
    char     host[CFG_STR_MAX];
    int      port;
    char     username[CFG_STR_MAX];
    AuthType auth_type;
    char     password[CFG_STR_MAX];
    char     key_path[CFG_STR_MAX];
} Profile;

/* ---- Config --------------------------------------------------------------- */

typedef struct {
    Settings settings;
    Vector   profiles;  /* Vector of Profile * (owned) */
} Config;

/* Fill *s with compiled-in defaults.  Does not allocate. */
void config_default_settings(Settings *s);

/* Load config.json from path.  Returns NULL if the file cannot be opened
 * or parsed.  Caller must call config_free() on the returned pointer. */
Config *config_load(const char *path);

/* Write cfg to path as JSON.  Returns 0 on success, -1 on I/O error. */
int config_save(const Config *cfg, const char *path);

/* Free cfg and all owned Profile objects.  Safe to call with NULL. */
void config_free(Config *cfg);

/* Allocate a zero-initialised profile with port = 22 and
 * auth_type = AUTH_PASSWORD.  Never returns NULL (aborts on OOM). */
Profile *config_profile_new(void);

/* Free a single profile.  Safe to call with NULL. */
void config_profile_free(Profile *p);

/* Allocate a Config with default settings and an empty profile list.
 * Never returns NULL (aborts on OOM). */
Config *config_new_default(void);

#endif /* CONFIG_H */
