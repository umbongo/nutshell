#ifndef NUTSHELL_CONFIG_H
#define NUTSHELL_CONFIG_H

#include <stddef.h>

#define CFG_STR_MAX ((size_t)256)

#include "profile.h"
#include "../core/vector.h"

typedef struct {
    char font[CFG_STR_MAX];
    int font_size;
    int scrollback_lines;
    int paste_delay_ms;
    int logging_enabled;
    char log_format[CFG_STR_MAX];
    char log_dir[CFG_STR_MAX];
    char host_key_verification[CFG_STR_MAX];
    char foreground_colour[CFG_STR_MAX];
    char background_colour[CFG_STR_MAX];
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

#endif