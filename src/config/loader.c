#include "config.h"
#include "json_parser.h"
#include "xmalloc.h"
#include "crypto.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- File I/O helper ------------------------------------------------------ */

/* Read entire file at path into a heap buffer (null-terminated).
 * Returns NULL if the file cannot be opened or read.
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
    if (sz < 0) {
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
            default:   fputc((int)c, f); break;
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

void settings_validate(Settings *s)
{
    if (!s) return;
    if (s->font[0] == '\0') {
        (void)snprintf(s->font, sizeof(s->font), "%s", "Consolas");
    }
    if (s->font_size < 6)  s->font_size = 6;
    if (s->font_size > 72) s->font_size = 72;
    if (s->scrollback_lines < 100)    s->scrollback_lines = 100;
    if (s->scrollback_lines > 50000)  s->scrollback_lines = 50000;
    if (s->paste_delay_ms < 0)    s->paste_delay_ms = 0;
    if (s->paste_delay_ms > 5000) s->paste_delay_ms = 5000;
}

void config_default_settings(Settings *s)
{
    memset(s, 0, sizeof(*s));
    field_copy(s->font,                 sizeof(s->font),                 "Consolas");
    s->font_size        = 12;
    s->scrollback_lines = 10000;
    s->paste_delay_ms   = 350;
    s->logging_enabled  = 0;
    field_copy(s->log_format,           sizeof(s->log_format),           "%Y-%m-%d_%H-%M-%S");
    field_copy(s->host_key_verification,sizeof(s->host_key_verification),"tofu");
    field_copy(s->foreground_colour,    sizeof(s->foreground_colour),    "#0C0C0C");
    field_copy(s->background_colour,    sizeof(s->background_colour),    "#F2F2F2");
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
    /* Zero password field before freeing (security hygiene). */
    memset(p->password, 0, sizeof(p->password));
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

    FILE *f = fopen(path, "w");
    if (!f) {
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
    return 0;
}
