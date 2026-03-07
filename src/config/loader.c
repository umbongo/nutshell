#include "config.h"
#include "json_parser.h"
#include "xmalloc.h"
#include "crypto.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

/* ---- File I/O helper ------------------------------------------------------ */

/* M-3: reject config files larger than 1 MB to prevent abuse. */
#define MAX_CONFIG_FILE_SIZE (1024L * 1024L)

/* Read entire file at path into a heap buffer (null-terminated).
 * Returns NULL if the file cannot be opened, read, or exceeds the size limit.
 * Caller must free() the returned pointer. */
static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long sz = ftell(f);
    if (sz < 0 || sz > MAX_CONFIG_FILE_SIZE) {  /* M-3: size limit */
        fclose(f);
        return NULL;
    }
    rewind(f);

    char *buf = xmalloc((size_t)sz + 1u);
    size_t rd = fread(buf, 1u, (size_t)sz, f);
    fclose(f);
    buf[rd] = '\0';
    return buf;
}

/* ---- JSON string output helper -------------------------------------------- */

/* Write s as a quoted, JSON-escaped string to f.
 * Format strings are fixed literals — no user-controlled format args. */
static void fprint_json_str(FILE *f, const char *s)
{
    fputc('"', f);
    for (const char *p = s; *p != '\0'; p++) {
        unsigned char c = (unsigned char)*p;
        switch (c) {
            case '"':  fputs("\\\"", f); break;
            case '\\': fputs("\\\\", f); break;
            case '\n': fputs("\\n",  f); break;
            case '\r': fputs("\\r",  f); break;
            case '\t': fputs("\\t",  f); break;
            default:
                if (c < 0x20) {
                    /* L-3: escape control characters below 0x20 per RFC 8259 */
                    fprintf(f, "\\u%04x", (unsigned int)c);
                } else {
                    fputc((int)c, f);
                }
                break;
        }
    }
    fputc('"', f);
}

/* ---- Safe string copy into a fixed-size field ----------------------------- */

/* Copy src into dst[dst_size], always null-terminating.  Uses snprintf so
 * the compiler can verify the format string at compile time. */
static void field_copy(char *dst, size_t dst_size, const char *src)
{
    if (!src) {
        dst[0] = '\0';
        return;
    }
    (void)snprintf(dst, dst_size, "%s", src);
}

/* ---- Public API ----------------------------------------------------------- */

/* Allowed discrete font sizes — must match k_font_sizes in settings.c and
 * k_allowed_sizes in window.c. */
static const int k_allowed_sizes[] = { 6, 8, 10, 12, 14, 16, 18, 20 };
#define NUM_ALLOWED_SIZES ((int)(sizeof(k_allowed_sizes) / sizeof(k_allowed_sizes[0])))

void settings_validate(Settings *s)
{
    if (!s) return;
    if (s->font[0] == '\0') {
        (void)snprintf(s->font, sizeof(s->font), "%s", "Consolas");
    }
    /* Snap font_size to nearest allowed discrete size */
    {
        int best = k_allowed_sizes[0];
        int best_dist = abs(s->font_size - best);
        for (int i = 1; i < NUM_ALLOWED_SIZES; i++) {
            int dist = abs(s->font_size - k_allowed_sizes[i]);
            if (dist < best_dist) {
                best = k_allowed_sizes[i];
                best_dist = dist;
            }
        }
        s->font_size = best;
    }
    if (s->scrollback_lines < 100)    s->scrollback_lines = 100;
    if (s->scrollback_lines > 50000)  s->scrollback_lines = 50000;
    if (s->paste_delay_ms < 0)    s->paste_delay_ms = 0;
    if (s->paste_delay_ms > 5000) s->paste_delay_ms = 5000;
}

void config_default_settings(Settings *s)
{
    memset(s, 0, sizeof(*s));
    field_copy(s->font,                 sizeof(s->font),                 "Consolas");
    s->font_size        = 10;
    s->scrollback_lines = 10000;
    s->paste_delay_ms   = 350;
    s->logging_enabled  = 0;
    field_copy(s->log_format,           sizeof(s->log_format),           "%Y-%m-%d_%H-%M-%S");
    field_copy(s->host_key_verification,sizeof(s->host_key_verification),"tofu");
    field_copy(s->foreground_colour,    sizeof(s->foreground_colour),    "#000000");
    field_copy(s->background_colour,    sizeof(s->background_colour),    "#FFFFFF");
    field_copy(s->ai_provider,          sizeof(s->ai_provider),          "deepseek");
    /* ai_api_key defaults to empty (already zeroed by memset) */
}

Profile *config_profile_new(void)
{
    Profile *p = xcalloc(1u, sizeof(Profile));
    p->port      = 22;
    p->auth_type = AUTH_PASSWORD;
    return p;
}

void config_profile_free(Profile *p)
{
    if (!p) {
        return;
    }
    /* L-1: use volatile zero so the compiler cannot elide the wipe. */
    volatile char *vp = p->password;
    for (size_t i = 0; i < sizeof(p->password); i++) vp[i] = 0;
    free(p);
}

Config *config_new_default(void)
{
    Config *cfg = xcalloc(1u, sizeof(Config));
    config_default_settings(&cfg->settings);
    vec_init(&cfg->profiles);
    return cfg;
}

void config_free(Config *cfg)
{
    if (!cfg) {
        return;
    }
    size_t n = vec_size(&cfg->profiles);
    for (size_t i = 0u; i < n; i++) {
        config_profile_free((Profile *)vec_get(&cfg->profiles, i));
    }
    vec_free(&cfg->profiles);
    free(cfg);
}

Config *config_load(const char *path)
{
    if (!path) {
        return NULL;
    }

    char *src = read_file(path);
    if (!src) {
        return NULL;
    }

    JsonNode *root = json_parse(src);
    free(src);
    if (!root || root->type != JSON_OBJECT) {
        json_free(root);
        return NULL;
    }

    Config *cfg = config_new_default();

    /* ---- Settings ---- */
    const JsonNode *jset = json_obj_get(root, "settings");
    if (jset && jset->type == JSON_OBJECT) {
        Settings *s = &cfg->settings;
        const char *sv;

        if ((sv = json_obj_str(jset, "font"))) {
            field_copy(s->font, sizeof(s->font), sv);
        }
        s->font_size = (int)json_obj_num(jset, "font_size",
                                         (double)s->font_size);
        s->scrollback_lines = (int)json_obj_num(jset, "scrollback_lines",
                                                (double)s->scrollback_lines);
        s->paste_delay_ms = (int)json_obj_num(jset, "paste_delay_ms",
                                              (double)s->paste_delay_ms);
        s->logging_enabled = json_obj_bool(jset, "logging_enabled",
                                           s->logging_enabled);
        if ((sv = json_obj_str(jset, "log_format"))) {
            field_copy(s->log_format, sizeof(s->log_format), sv);
        }
        if ((sv = json_obj_str(jset, "log_dir"))) {
            field_copy(s->log_dir, sizeof(s->log_dir), sv);
        }
        if ((sv = json_obj_str(jset, "host_key_verification"))) {
            field_copy(s->host_key_verification,
                       sizeof(s->host_key_verification), sv);
        }
        if ((sv = json_obj_str(jset, "foreground_colour"))) {
            field_copy(s->foreground_colour, sizeof(s->foreground_colour), sv);
        }
        if ((sv = json_obj_str(jset, "background_colour"))) {
            field_copy(s->background_colour, sizeof(s->background_colour), sv);
        }
        if ((sv = json_obj_str(jset, "ai_provider"))) {
            field_copy(s->ai_provider, sizeof(s->ai_provider), sv);
        }
        if ((sv = json_obj_str(jset, "ai_custom_url"))) {
            field_copy(s->ai_custom_url, sizeof(s->ai_custom_url), sv);
        }
        if ((sv = json_obj_str(jset, "ai_custom_model"))) {
            field_copy(s->ai_custom_model, sizeof(s->ai_custom_model), sv);
        }
        if ((sv = json_obj_str(jset, "ai_api_key"))) {
            if (crypto_is_encrypted(sv)) {
                char plaintext[256];
                if (crypto_decrypt(sv, plaintext, sizeof(plaintext)) == CRYPTO_OK) {
                    field_copy(s->ai_api_key, sizeof(s->ai_api_key), plaintext);
                    memset(plaintext, 0, sizeof(plaintext));
                }
            } else {
                field_copy(s->ai_api_key, sizeof(s->ai_api_key), sv);
            }
        }
        settings_validate(s);
    }

    /* ---- Profiles ---- */
    JsonNode *jprofs = json_obj_get(root, "profiles");
    if (jprofs && jprofs->type == JSON_ARRAY) {
        size_t n = vec_size(&jprofs->as.arr);
        for (size_t i = 0u; i < n; i++) {
            const JsonNode *jp = (const JsonNode *)vec_get(&jprofs->as.arr, i);
            if (!jp || jp->type != JSON_OBJECT) {
                continue;
            }
            Profile *pr = config_profile_new();
            const char *sv;

            if ((sv = json_obj_str(jp, "name"))) {
                field_copy(pr->name, sizeof(pr->name), sv);
            }
            if ((sv = json_obj_str(jp, "host"))) {
                field_copy(pr->host, sizeof(pr->host), sv);
            }
            pr->port = (int)json_obj_num(jp, "port", 22.0);
            if ((sv = json_obj_str(jp, "username"))) {
                field_copy(pr->username, sizeof(pr->username), sv);
            }
            sv = json_obj_str(jp, "auth_type");
            if (sv && strcmp(sv, "key") == 0) {
                pr->auth_type = AUTH_KEY;
            } else {
                pr->auth_type = AUTH_PASSWORD;
            }
            if ((sv = json_obj_str(jp, "password"))) {
                if (crypto_is_encrypted(sv)) {
                    char plaintext[256];
                    if (crypto_decrypt(sv, plaintext, sizeof(plaintext)) == CRYPTO_OK) {
                        field_copy(pr->password, sizeof(pr->password), plaintext);
                        memset(plaintext, 0, sizeof(plaintext));
                    }
                    /* On decrypt failure, leave password empty */
                } else {
                    field_copy(pr->password, sizeof(pr->password), sv);
                }
            }
            if ((sv = json_obj_str(jp, "key_path"))) {
                field_copy(pr->key_path, sizeof(pr->key_path), sv);
            }
            vec_push(&cfg->profiles, pr);
        }
    }

    json_free(root);
    return cfg;
}

int config_save(const Config *cfg, const char *path)
{
    if (!cfg || !path) {
        return -1;
    }

    /* M-4: write to a temp file first, then atomically replace the target.
     * This prevents data loss if the process crashes mid-write. */
    size_t plen = strlen(path);
    char *tmp_path = xmalloc(plen + 5u);
    memcpy(tmp_path, path, plen);
    memcpy(tmp_path + plen, ".tmp", 5u);

    FILE *f = fopen(tmp_path, "w");
    if (!f) {
        free(tmp_path);
        return -1;
    }

    const Settings *s = &cfg->settings;

    fputs("{\n  \"settings\": {\n", f);
    fputs("    \"font\": ", f);
    fprint_json_str(f, s->font);
    fputs(",\n", f);
    fprintf(f, "    \"font_size\": %d,\n", s->font_size);
    fprintf(f, "    \"scrollback_lines\": %d,\n", s->scrollback_lines);
    fprintf(f, "    \"paste_delay_ms\": %d,\n", s->paste_delay_ms);
    fprintf(f, "    \"logging_enabled\": %s,\n",
            s->logging_enabled ? "true" : "false");
    fputs("    \"log_format\": ", f);
    fprint_json_str(f, s->log_format);
    fputs(",\n", f);
    fputs("    \"log_dir\": ", f);
    fprint_json_str(f, s->log_dir);
    fputs(",\n", f);
    fputs("    \"host_key_verification\": ", f);
    fprint_json_str(f, s->host_key_verification);
    fputs(",\n", f);
    fputs("    \"foreground_colour\": ", f);
    fprint_json_str(f, s->foreground_colour);
    fputs(",\n", f);
    fputs("    \"background_colour\": ", f);
    fprint_json_str(f, s->background_colour);
    fputs(",\n", f);
    fputs("    \"ai_provider\": ", f);
    fprint_json_str(f, s->ai_provider);
    fputs(",\n", f);
    fputs("    \"ai_custom_url\": ", f);
    fprint_json_str(f, s->ai_custom_url);
    fputs(",\n", f);
    fputs("    \"ai_custom_model\": ", f);
    fprint_json_str(f, s->ai_custom_model);
    fputs(",\n", f);
    fputs("    \"ai_api_key\": ", f);
    fprint_json_str(f, s->ai_api_key);
    fputs("\n  },\n  \"profiles\": [\n", f);

    size_t n = vec_size(&cfg->profiles);
    for (size_t i = 0u; i < n; i++) {
        const Profile *pr = (const Profile *)vec_get(&cfg->profiles, i);
        fputs("    {\n", f);
        fputs("      \"name\": ", f);
        fprint_json_str(f, pr->name);
        fputs(",\n", f);
        fputs("      \"host\": ", f);
        fprint_json_str(f, pr->host);
        fputs(",\n", f);
        fprintf(f, "      \"port\": %d,\n", pr->port);
        fputs("      \"username\": ", f);
        fprint_json_str(f, pr->username);
        fputs(",\n", f);
        fprintf(f, "      \"auth_type\": \"%s\",\n",
                pr->auth_type == AUTH_KEY ? "key" : "password");
        fputs("      \"password\": ", f);
        if (pr->password[0] != '\0') {
            char enc[512];
            if (crypto_encrypt(pr->password, enc, sizeof(enc)) == CRYPTO_OK) {
                fprint_json_str(f, enc);
            } else {
                fprint_json_str(f, ""); /* write empty on encrypt failure */
            }
        } else {
            fprint_json_str(f, "");
        }
        fputs(",\n", f);
        fputs("      \"key_path\": ", f);
        fprint_json_str(f, pr->key_path);
        fputs("\n    }", f);
        if (i + 1u < n) {
            fputc(',', f);
        }
        fputc('\n', f);
    }

    fputs("  ]\n}\n", f);
    fclose(f);

    /* Atomically replace the real config file with the completed temp file. */
#ifdef _WIN32
    int moved = MoveFileExA(tmp_path, path, MOVEFILE_REPLACE_EXISTING) ? 0 : -1;
#else
    int moved = rename(tmp_path, path);
#endif
    free(tmp_path);
    return moved;
}
