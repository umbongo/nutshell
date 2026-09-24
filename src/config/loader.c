#include "config.h"
#include "ai_prompt.h"
#include "app_font.h"
#include "ui_theme.h"
#include "json_parser.h"
#include "xmalloc.h"
#include "secure_zero.h"
#include "crypto.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <dirent.h>
#include <errno.h>
#ifdef _WIN32
#include <windows.h>
#endif

/* ---- File I/O helper ------------------------------------------------------ */

/* M-3: reject config files larger than the size limit, to prevent abuse.
 * M3 (2026-09-24 review): a real config with a large ai_system_notes
 * (AI_NOTES_MAX 2560 bytes) and many profiles can get closer to 1 MB than
 * expected once several are saved with long notes each; raised to 8 MB --
 * still nowhere near what a legitimate config approaches, but with real
 * headroom before a real user's file is mistaken for abuse. */
#define MAX_CONFIG_FILE_SIZE (8L * 1024L * 1024L)

/* M3: distinguishes "no file there yet" (the normal first run -- fine to
 * write defaults) from "a file is there but this process could not read
 * it" (locked by another program or AV, too large, a permissions problem
 * -- NOT fine to silently overwrite with defaults, since whatever is in it
 * is still the user's real config) and from "read fine, but not valid
 * config JSON" (handled separately -- see config_backup_unparseable_file()). */
typedef enum {
    CONFIG_READ_OK        = 0,
    CONFIG_READ_MISSING   = 1,
    CONFIG_READ_UNREADABLE = 2,
} ConfigReadStatus;

/* Read entire file at path into a heap buffer (null-terminated), and report
 * why in *status when it fails. Caller must free() the returned pointer. */
static char *read_file_status(const char *path, ConfigReadStatus *status)
{
    if (status) *status = CONFIG_READ_UNREADABLE;
    errno = 0;
    FILE *f = fopen(path, "rb");
    if (!f) {
        /* ENOENT is specifically "not there"; anything else (EACCES, too
         * many open files, a sharing violation surfaced some other way) is
         * "there, but this process couldn't get at it". */
        if (status) *status = (errno == ENOENT) ? CONFIG_READ_MISSING
                                                 : CONFIG_READ_UNREADABLE;
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
    if (status) *status = CONFIG_READ_OK;
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

/* ---- Encrypted-secret load/save helpers ------------------------------------
 *
 * Every secret (profile password, AI API key) shares one contract:
 *
 *   Load:
 *     - A DPAPI blob ("$dpapi$v1$...") that decrypts -> plaintext used.
 *     - A legacy blob ("$aes256gcm$v1$...", MachineGuid-derived key) that
 *       decrypts -> plaintext used, and *out_migrated is set so the caller
 *       re-saves once (writing it back as DPAPI; the legacy code stays for
 *       this migration read only, new writes never produce it again).
 *     - Any blob that fails to decrypt (wrong user/machine, or corrupt) ->
 *       plaintext left empty, but the raw blob string is preserved
 *       verbatim so a save before the user supplies a new value writes it
 *       back unchanged (moving the config back to its original PC/user
 *       still works).
 *     - A bare (unencrypted) value -- a very old config, or a hand-edited
 *       one -- is used as plaintext directly.
 *
 *   Save:
 *     - Plaintext present -> encrypt fresh with DPAPI; any previously
 *       preserved foreign/corrupt blob is dropped (the user supplied a new
 *       value, so there is nothing left to preserve it for).
 *     - Plaintext empty, a preserved blob present -> write the preserved
 *       blob back verbatim, unre-encrypted.
 *     - Plaintext empty, nothing preserved -> write "" (this is also what
 *       happens when the user explicitly clears a password that had
 *       decrypted successfully: no blob was ever preserved for it, so
 *       clearing the field naturally clears the blob on save too).
 */

/* Preserve `raw` verbatim into preserved_out, UNLESS it doesn't fit --
 * field_copy()'s snprintf would silently truncate it, and a truncated
 * blob is corrupt: it will never decrypt again, yet save_secret() would
 * happily write the truncated garbage straight back to disk. Drop it
 * instead (leave preserved_out empty, same as "no blob") and log why, so
 * the secret is simply lost -- same outcome as if it had never been set --
 * rather than silently corrupted. A well-formed blob from this program
 * never gets close to CFG_BLOB_MAX; this only fires for a hand-edited or
 * otherwise corrupt config file. */
static void preserve_blob_or_drop(const char *raw, char *preserved_out,
                                   size_t preserved_cap)
{
    size_t len = strlen(raw);
    if (len >= preserved_cap) {
        fprintf(stderr,
                "nutshell: a stored secret blob is %zu bytes, longer than "
                "the %zu-byte limit -- dropping it rather than saving it "
                "back truncated (which would corrupt it). The affected "
                "password or API key will need to be re-entered.\n",
                len, preserved_cap - 1u);
        preserved_out[0] = '\0';
        return;
    }
    field_copy(preserved_out, preserved_cap, raw);
}

/* L (2026-09-24 review): non-zero when `s` has the shape of an
 * encrypted-blob prefix this build doesn't recognise -- "$<name>$v<digits>$..."
 * -- as opposed to a plain password or API key a user or a hand edit would
 * type, which essentially never starts with '$'. crypto_is_dpapi() and
 * crypto_is_encrypted() must already have said no by the time load_secret()
 * calls this. Treating a string shaped like this as plaintext (the old
 * behaviour) meant re-encrypting whatever followed the second '$' as if it
 * were the real secret -- silently and permanently discarding a blob format
 * this build doesn't understand (a future Nutshell version's, or another
 * program's) the next time the config saves. Preserving it verbatim
 * instead, exactly like a recognised-but-undecryptable blob, keeps it
 * intact for whatever does understand it. */
static int looks_like_unknown_blob(const char *s)
{
    if (s[0] != '$') return 0;
    const char *p = strchr(s + 1, '$');
    if (!p || p == s + 1) return 0; /* "$$..." or no closing '$': not this shape */
    const char *q = p + 1;
    if (*q != 'v' || !isdigit((unsigned char)q[1])) return 0;
    q++;
    while (isdigit((unsigned char)*q)) q++;
    return *q == '$';
}

/* Load one secret field from its raw JSON string value. `plain_out` and
 * `preserved_out` are cleared first, so exactly one of "decrypted
 * plaintext" or "blob preserved verbatim" holds afterward (both empty for
 * a missing/empty raw value). Sets *out_migrated to 1 (never clears it)
 * when a legacy blob, or a bare (unencrypted) value, was found -- either
 * way the caller should re-save once so the secret ends up DPAPI-encrypted
 * on disk (M-3: a bare value is no better than the legacy format -- it
 * isn't encrypted at all -- so it needs the same migration re-save). */
static void load_secret(const char *raw, char *plain_out, size_t plain_cap,
                         char *preserved_out, size_t preserved_cap,
                         int *out_migrated)
{
    plain_out[0] = '\0';
    preserved_out[0] = '\0';

    if (!raw || raw[0] == '\0') {
        return;
    }

    if (crypto_is_dpapi(raw)) {
        char tmp[CFG_STR_MAX];
        if (crypto_decrypt_dpapi(raw, tmp, sizeof(tmp)) == CRYPTO_OK) {
            field_copy(plain_out, plain_cap, tmp);
        } else {
            preserve_blob_or_drop(raw, preserved_out, preserved_cap);
        }
        secure_zero(tmp, sizeof(tmp));
        return;
    }

    if (crypto_is_encrypted(raw)) {
        char tmp[CFG_STR_MAX];
        if (crypto_decrypt(raw, tmp, sizeof(tmp)) == CRYPTO_OK) {
            field_copy(plain_out, plain_cap, tmp);
            if (out_migrated) *out_migrated = 1;
        } else {
            preserve_blob_or_drop(raw, preserved_out, preserved_cap);
        }
        secure_zero(tmp, sizeof(tmp));
        return;
    }

    /* L: shaped like an encrypted blob this build just doesn't recognise --
     * not a plain password/API key at all -- preserve it verbatim instead
     * of treating it as plaintext (which would re-encrypt it as the "real"
     * secret and lose the original for good on the next save). */
    if (looks_like_unknown_blob(raw)) {
        preserve_blob_or_drop(raw, preserved_out, preserved_cap);
        return;
    }

    /* Not a recognised encrypted-blob prefix at all: a bare (unencrypted)
     * value -- a very old config, or a hand edit. Used as plaintext
     * directly, but flagged for migration (M-3) so the caller re-saves it
     * encrypted rather than leaving it sitting in the clear on disk
     * indefinitely, exactly as a decrypted legacy blob is above. */
    field_copy(plain_out, plain_cap, raw);
    if (out_migrated) *out_migrated = 1;
}

/* Prepare one secret field's JSON string value into `out` (the raw string
 * content only -- the caller writes the surrounding quotes via
 * fprint_json_str()). See the load/save contract above.
 *
 * M-2: when `plain` is non-empty and DPAPI encryption fails, returns the
 * crypto error and leaves `out` untouched instead of falling back to an
 * empty string. The old behaviour -- write "" on encrypt failure -- meant
 * a save (including the automatic migration re-save inside config_load())
 * could silently wipe a password or API key that a moment earlier had
 * decrypted, or was preserved, just fine. config_save() calls this for
 * every secret BEFORE writing anything, and aborts the whole save (config
 * on disk left untouched) if any of them fails -- see there. */
static int secret_prepare(const char *plain, const char *preserved,
                           char *out, size_t out_cap)
{
    if (plain[0] != '\0') {
        char enc[CFG_BLOB_MAX];
        int rc = crypto_encrypt_dpapi(plain, enc, sizeof(enc));
        if (rc != CRYPTO_OK) {
            secure_zero(enc, sizeof(enc));
            return rc;
        }
        field_copy(out, out_cap, enc);
        secure_zero(enc, sizeof(enc));
        return CRYPTO_OK;
    }
    if (preserved[0] != '\0') {
        field_copy(out, out_cap, preserved);
        return CRYPTO_OK;
    }
    out[0] = '\0';
    return CRYPTO_OK;
}

/* ---- Unparseable-config backup ---------------------------------------- */

#define MAX_BAD_BACKUPS 5

/* Split `path` into its directory (no trailing separator; "." when path
 * has no directory component) and base filename, for building sibling
 * "<base>.bad-*" backup names. */
static void split_dir_base(const char *path, char *dir_out, size_t dir_cap,
                            const char **base_out)
{
    const char *slash  = strrchr(path, '/');
    const char *bslash = strrchr(path, '\\');
    if (bslash && (!slash || bslash > slash)) slash = bslash;
    if (slash) {
        size_t dir_len = (size_t)(slash - path);
        if (dir_len >= dir_cap) dir_len = dir_cap - 1u;
        memcpy(dir_out, path, dir_len);
        dir_out[dir_len] = '\0';
        *base_out = slash + 1;
    } else {
        (void)snprintf(dir_out, dir_cap, ".");
        *base_out = path;
    }
}

/* Keep at most MAX_BAD_BACKUPS "<base>.bad-*" files beside `path`, deleting
 * the rest -- the suffix is a decimal Unix timestamp, so lexicographic
 * order is chronological order. Best-effort: opendir()/remove() failures
 * just leave extra backup files lying around, which is harmless. */
static void config_prune_old_backups(const char *path)
{
    char dir[512];
    const char *base;
    split_dir_base(path, dir, sizeof(dir), &base);

    char prefix[300];
    (void)snprintf(prefix, sizeof(prefix), "%s.bad-", base);
    size_t prefix_len = strlen(prefix);

    /* Pass 1: track the MAX_BAD_BACKUPS newest matching names. */
    char keep[MAX_BAD_BACKUPS][300];
    size_t keep_n = 0u;

    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (strncmp(de->d_name, prefix, prefix_len) != 0) continue;
        if (keep_n < MAX_BAD_BACKUPS) {
            (void)snprintf(keep[keep_n], sizeof(keep[keep_n]), "%s", de->d_name);
            keep_n++;
        } else {
            size_t oldest = 0u;
            for (size_t i = 1u; i < keep_n; i++) {
                if (strcmp(keep[i], keep[oldest]) < 0) oldest = i;
            }
            if (strcmp(de->d_name, keep[oldest]) > 0) {
                (void)snprintf(keep[oldest], sizeof(keep[oldest]), "%s", de->d_name);
            }
        }
    }
    closedir(d);

    if (keep_n < MAX_BAD_BACKUPS) return; /* at/under the cap already */

    /* Pass 2: delete every match not in `keep`. */
    d = opendir(dir);
    if (!d) return;
    while ((de = readdir(d)) != NULL) {
        if (strncmp(de->d_name, prefix, prefix_len) != 0) continue;
        int kept = 0;
        for (size_t i = 0u; i < keep_n; i++) {
            if (strcmp(de->d_name, keep[i]) == 0) { kept = 1; break; }
        }
        if (!kept) {
            /* dir[512] + '/' + d_name (up to sizeof(de->d_name), 260 on
             * MinGW) + NUL: size generously so -Wformat-truncation can
             * prove this never truncates. */
            char full[900];
            (void)snprintf(full, sizeof(full), "%s/%s", dir, de->d_name);
            (void)remove(full);
        }
    }
    closedir(d);
}

/* True when a file already exists at `path` (any type, readable or not) --
 * used only to pick a free backup name below, so a false negative (a file
 * that exists but this process cannot even stat) just means that name is
 * tried and its own rename fails, same as before this existed. */
static int file_exists_stdio(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f) { fclose(f); return 1; }
    return 0;
}

/* M4: picks a "<path>.bad-<unix timestamp>[-<n>]" name that does not
 * currently exist, trying suffixes -1, -2, ... when the bare timestamp (or
 * an earlier suffix) is already taken -- e.g. two unparseable loads within
 * the same second. Returns 1 and fills `out`, or 0 if every candidate up to
 * a generous cap is taken (essentially unreachable in practice). */
#define MAX_BAD_BACKUP_ATTEMPTS 100

static int pick_unique_bad_path(const char *path, char *out, size_t out_size)
{
    long ts = (long)time(NULL);
    for (int attempt = 0; attempt < MAX_BAD_BACKUP_ATTEMPTS; attempt++) {
        int n = (attempt == 0)
            ? snprintf(out, out_size, "%s.bad-%ld", path, ts)
            : snprintf(out, out_size, "%s.bad-%ld-%d", path, ts, attempt);
        if (n < 0 || (size_t)n >= out_size) return 0;
        if (!file_exists_stdio(out)) return 1;
    }
    return 0;
}

/* A config file that exists but fails to parse (corrupt, hand-edited
 * badly, truncated by a crash, ...) used to simply vanish the moment the
 * caller fell back to config_new_default() and saved -- config_save()
 * overwrites `path` unconditionally, so the original content was gone for
 * good. Rename it out of the way first, to "<path>.bad-<unix timestamp>"
 * (M4: uniqued with a counter suffix rather than REPLACE_EXISTING, so a
 * second unparseable load in the same second cannot clobber the first
 * backup), so it survives that overwrite; the caller's own message tells
 * the user. Returns 1 on success, 0 when the original is NOT safely
 * preserved (no free name found, or the rename itself failed) -- M4: the
 * caller must treat that the same as "unreadable" (config_load_ex()'s
 * CONFIG_LOAD_UNREADABLE) and not save defaults over it, rather than
 * assuming (as the old, unchecked call did) that the backup always
 * succeeded. */
static int config_backup_unparseable_file(const char *path)
{
    char bad_path[700];
    if (!pick_unique_bad_path(path, bad_path, sizeof(bad_path))) {
        return 0;
    }
#ifdef _WIN32
    /* No REPLACE_EXISTING: pick_unique_bad_path() already confirmed this
     * exact name is free, so replacing anything here would only ever paper
     * over a TOCTOU race, never a real need. */
    int ok = MoveFileExA(path, bad_path, 0) ? 1 : 0;
#else
    int ok = (rename(path, bad_path) == 0) ? 1 : 0;
#endif
    config_prune_old_backups(path);
    return ok;
}

/* ---- Public API ----------------------------------------------------------- */

void settings_validate(Settings *s)
{
    if (!s) return;
    if (s->font[0] == '\0') {
        (void)snprintf(s->font, sizeof(s->font), "%s", APP_FONT_DEFAULT);
    }
    if (s->ai_font[0] == '\0') {
        (void)snprintf(s->ai_font, sizeof(s->ai_font), "%s", APP_FONT_AI_DEFAULT);
    }
    s->font_size = app_font_snap_size(s->font_size);
    if (s->scrollback_lines < 100)    s->scrollback_lines = 100;
    if (s->scrollback_lines > 50000)  s->scrollback_lines = 50000;
    if (s->paste_delay_ms < 0)    s->paste_delay_ms = 0;
    if (s->paste_delay_ms > 5000) s->paste_delay_ms = 5000;
    if (s->ai_max_search_results < 1)  s->ai_max_search_results = 1;
    if (s->ai_max_search_results > 20) s->ai_max_search_results = 20;
    if (s->ai_max_context_lines < 1)     s->ai_max_context_lines = 1;
    if (s->ai_max_context_lines > 50000) s->ai_max_context_lines = 50000;
    if (s->ssh_user_idle_timeout_mins < 0)     s->ssh_user_idle_timeout_mins = 0;
    if (s->ssh_user_idle_timeout_mins > 10080) s->ssh_user_idle_timeout_mins = 10080;
    if (s->auto_connect != 0) s->auto_connect = 1;
    if (s->paste_confirm != 0) s->paste_confirm = 1;
    if (s->open_session_manager_at_start != 0) s->open_session_manager_at_start = 1;
    cmd_policy_clamp(&s->ai_policy_default);
}

void config_default_settings(Settings *s)
{
    memset(s, 0, sizeof(*s));
    field_copy(s->font,                 sizeof(s->font),                 APP_FONT_DEFAULT);
    field_copy(s->ai_font,              sizeof(s->ai_font),              APP_FONT_AI_DEFAULT);
    s->font_size        = APP_FONT_DEFAULT_SIZE;
    s->scrollback_lines = 10000;
    s->paste_delay_ms   = 350;
    s->logging_enabled  = 0;
    s->debug_terminal   = 0;
    field_copy(s->log_format,           sizeof(s->log_format),           "%Y-%m-%d_%H-%M-%S");
    field_copy(s->host_key_verification,sizeof(s->host_key_verification),"tofu");
    field_copy(s->foreground_colour,    sizeof(s->foreground_colour),    "#E0E0E0");
    field_copy(s->background_colour,    sizeof(s->background_colour),    "#121212");
    field_copy(s->colour_scheme,        sizeof(s->colour_scheme),        "Onyx Synapse");
    field_copy(s->ai_provider,          sizeof(s->ai_provider),          AI_DEFAULT_PROVIDER);
    /* ai_api_key defaults to empty (already zeroed by memset) */
    field_copy(s->ai_search_provider, sizeof(s->ai_search_provider), "duckduckgo-api");
    /* ai_search_url defaults to empty (already zeroed by memset) */
    s->ai_max_search_results = 7;
    s->ai_web_fetch_enabled = 0;
    s->ssh_user_idle_timeout_mins = 0;
    s->markdown_render_enabled = 1;
    s->ai_max_context_lines = 1000;
    s->auto_connect = 0;
    /* auto_connect_session defaults to empty (already zeroed by memset) */
    s->paste_confirm = 1;
    s->open_session_manager_at_start = 0;
    s->ai_policy_default = cmd_policy_default();
}

Profile *config_profile_new(void)
{
    Profile *p = xcalloc(1u, sizeof(Profile));
    p->port      = 22;
    p->auth_type = AUTH_PASSWORD;
    field_copy(p->kind, sizeof(p->kind), "ssh");
    field_copy(p->platform, sizeof(p->platform), "auto");
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
    /* Wipe the one secret that lives directly in Settings (profile
     * passwords are wiped by config_profile_free() above already). */
    secure_zero(cfg->settings.ai_api_key, sizeof(cfg->settings.ai_api_key));
    free(cfg);
}

Config *config_load_ex(const char *path, ConfigLoadStatus *status_out)
{
    if (status_out) *status_out = CONFIG_LOAD_MISSING;

    if (!path) {
        return NULL;
    }

    ConfigReadStatus rs;
    char *src = read_file_status(path, &rs);
    if (!src) {
        if (status_out) {
            *status_out = (rs == CONFIG_READ_MISSING) ? CONFIG_LOAD_MISSING
                                                       : CONFIG_LOAD_UNREADABLE;
        }
        return NULL;
    }

    JsonNode *root = json_parse(src);
    free(src);
    if (!root || root->type != JSON_OBJECT) {
        json_free(root);
        /* The file exists (read_file_status() succeeded) but is not valid
         * config JSON: preserve it under a new name before the caller's
         * fallback to defaults gets a chance to overwrite it. See the
         * comment on config_backup_unparseable_file(). M4: only report this
         * as the (safe-to-overwrite) INVALID case when the backup actually
         * succeeded -- a failed backup leaves the original the only copy of
         * whatever was in it, exactly like an unreadable file, so the
         * caller must not save over it either. */
        int backed_up = config_backup_unparseable_file(path);
        if (status_out) {
            *status_out = backed_up ? CONFIG_LOAD_INVALID : CONFIG_LOAD_UNREADABLE;
        }
        return NULL;
    }

    if (status_out) *status_out = CONFIG_LOAD_OK;

    Config *cfg = config_new_default();

    /* Set by load_secret() when a legacy AES-GCM blob decrypted fine, so we
     * know to re-save once with DPAPI before returning -- see the bottom of
     * this function. Without that resave, a plaintext-equivalent legacy
     * blob (a MachineGuid-derived key is much weaker than DPAPI) could sit
     * in the file indefinitely if the user never happens to trigger a save
     * some other way. */
    int migrated = 0;

    /* ---- Settings ---- */
    const JsonNode *jset = json_obj_get(root, "settings");
    if (jset && jset->type == JSON_OBJECT) {
        Settings *s = &cfg->settings;
        const char *sv;

        if ((sv = json_obj_str(jset, "font"))) {
            field_copy(s->font, sizeof(s->font), sv);
        }
        if ((sv = json_obj_str(jset, "ai_font"))) {
            field_copy(s->ai_font, sizeof(s->ai_font), sv);
        }
        s->font_size = (int)json_obj_num(jset, "font_size",
                                         (double)s->font_size);
        s->scrollback_lines = (int)json_obj_num(jset, "scrollback_lines",
                                                (double)s->scrollback_lines);
        s->paste_delay_ms = (int)json_obj_num(jset, "paste_delay_ms",
                                              (double)s->paste_delay_ms);
        s->logging_enabled = json_obj_bool(jset, "logging_enabled",
                                           s->logging_enabled);
        s->debug_terminal  = json_obj_bool(jset, "debug_terminal",
                                           s->debug_terminal);
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
        if ((sv = json_obj_str(jset, "colour_scheme"))) {
            field_copy(s->colour_scheme, sizeof(s->colour_scheme), sv);
        }
        /* Migration: if no colour_scheme was set (legacy config), derive
         * from the default theme and set the scheme name. */
        if (s->colour_scheme[0] == '\0') {
            const ThemeColors *def = ui_theme_get(0);
            field_copy(s->colour_scheme, sizeof(s->colour_scheme), def->name);
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
            load_secret(sv, s->ai_api_key, sizeof(s->ai_api_key),
                        s->ai_api_key_enc_preserved, sizeof(s->ai_api_key_enc_preserved),
                        &migrated);
        }
        if ((sv = json_obj_str(jset, "ai_system_notes"))) {
            field_copy(s->ai_system_notes, sizeof(s->ai_system_notes), sv);
        }
        if ((sv = json_obj_str(jset, "ai_search_provider"))) {
            field_copy(s->ai_search_provider, sizeof(s->ai_search_provider), sv);
        }
        if ((sv = json_obj_str(jset, "ai_search_url"))) {
            field_copy(s->ai_search_url, sizeof(s->ai_search_url), sv);
        }
        s->ai_max_search_results = (int)json_obj_num(jset, "ai_max_search_results",
                                                     (double)s->ai_max_search_results);
        s->ai_web_fetch_enabled = json_obj_bool(jset, "ai_web_fetch_enabled",
                                                s->ai_web_fetch_enabled);
        s->ssh_user_idle_timeout_mins =
            (int)json_obj_num(jset, "ssh_user_idle_timeout_mins",
                              (double)s->ssh_user_idle_timeout_mins);
        s->markdown_render_enabled = json_obj_bool(jset, "markdown_render_enabled",
                                                   s->markdown_render_enabled);
        s->ai_max_context_lines = (int)json_obj_num(jset, "ai_max_context_lines",
                                                     (double)s->ai_max_context_lines);
        s->auto_connect = json_obj_bool(jset, "auto_connect", s->auto_connect);
        if ((sv = json_obj_str(jset, "auto_connect_session"))) {
            field_copy(s->auto_connect_session,
                       sizeof(s->auto_connect_session), sv);
        }
        s->paste_confirm = json_obj_bool(jset, "paste_confirm", s->paste_confirm);
        s->open_session_manager_at_start =
            json_obj_bool(jset, "open_session_manager_at_start",
                          s->open_session_manager_at_start);
        /* The old "ai_auto_approve_all" boolean key is dropped and ignored
         * on read -- no migration, callers get the default. */

        /* The session policy for new sessions. First match wins:
         *
         *   1. "ai_policy_default", the one key this version writes.
         *   2. "ai_auto_approve_mode" (v1.1.16): a five-mode auto-approve
         *      set, with no ceiling of its own.
         *   3. "ai_auto_approve_default" (pre-v1.1.16): the numeric form of
         *      the same thing, 0 -> off, 1 -> safe, 2 -> safe+write,
         *      3 -> all (old 2 lands on the *third* mode, not the second --
         *      inserting CMD_UNKNOWN shifted what the numbers mean).
         *
         * Rules 2 and 3 both land on ceiling `read`, and on unattended
         * `read` for any mode except "off". That looks lossy, and is -- but
         * only of intent that never took effect. The old permit-write flag
         * was per session and ALWAYS started off, so a fresh session blocked
         * everything above READ before the auto-approve gate ever saw it:
         * READ was the only category that could actually run unattended,
         * whatever the stored mode said. Migrating the mode's top category
         * onto the new ceiling instead would start new sessions more
         * permissively than the same config starts them today, and a
         * migration must never do that. The user raises the ceiling
         * deliberately, in Settings or in the status line.
         *
         * Save writes "ai_policy_default" only; neither old key is written
         * back. */
        if ((sv = json_obj_str(jset, "ai_policy_default"))) {
            cmd_policy_from_token(sv, &s->ai_policy_default);
        } else {
            /* Was the superseded setting on at all? "off" and anything
             * unrecognised count as off, so a corrupt value can only ever
             * migrate to the safer state. */
            static const char *const k_legacy_on_modes[5] = {
                "safe", "safe+unknown", "safe+write", "safe+unknown+write", "all"
            };
            int legacy_on = 0;
            if ((sv = json_obj_str(jset, "ai_auto_approve_mode"))) {
                for (int i = 0; i < 5; i++)
                    if (strcmp(sv, k_legacy_on_modes[i]) == 0) { legacy_on = 1; break; }
            } else {
                int num = (int)json_obj_num(jset, "ai_auto_approve_default", 0.0);
                legacy_on = (num >= 1 && num <= 3);
            }
            s->ai_policy_default.allowed = CMD_READ;
            s->ai_policy_default.unattended = legacy_on ? CMD_READ : POLICY_NONE;
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
            /* Missing key (older config) keeps the "ssh" default that
             * config_profile_new() already set. */
            if ((sv = json_obj_str(jp, "kind"))) {
                field_copy(pr->kind, sizeof(pr->kind), sv);
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
                load_secret(sv, pr->password, sizeof(pr->password),
                            pr->password_enc_preserved, sizeof(pr->password_enc_preserved),
                            &migrated);
            }
            if ((sv = json_obj_str(jp, "key_path"))) {
                field_copy(pr->key_path, sizeof(pr->key_path), sv);
            }
            if ((sv = json_obj_str(jp, "shell"))) {
                field_copy(pr->shell, sizeof(pr->shell), sv);
            }
            if ((sv = json_obj_str(jp, "ai_notes"))) {
                field_copy(pr->ai_notes, sizeof(pr->ai_notes), sv);
            }
            /* Missing key (older config) keeps the "auto" default that
             * config_profile_new() already set. */
            if ((sv = json_obj_str(jp, "platform"))) {
                field_copy(pr->platform, sizeof(pr->platform), sv);
            }
            vec_push(&cfg->profiles, pr);
        }
    }

    json_free(root);

    /* At least one legacy-encrypted secret decrypted successfully above:
     * re-save immediately so it is written back as DPAPI rather than
     * lingering in the weaker MachineGuid-derived format. A failed save
     * here just means the migration is retried on the next load/save --
     * the config the caller gets back is correct either way. */
    if (migrated) {
        (void)config_save(cfg, path);
    }

    return cfg;
}

Config *config_load(const char *path)
{
    return config_load_ex(path, NULL);
}

int config_save(const Config *cfg, const char *path)
{
    if (!cfg || !path || path[0] == '\0') {
        /* An empty path means the caller could not find anywhere safe to
         * write (see config_fallback_path()) -- never fall through to
         * building a relative ".tmp" path, which would silently land in
         * the process's current working directory. */
        return -1;
    }

    const Settings *s = &cfg->settings;
    size_t n = vec_size(&cfg->profiles);

    /* M-2: encrypt every secret BEFORE opening or writing the temp file,
     * and abort the whole save -- the file on disk is left completely
     * untouched -- the moment any of them fails. secret_prepare() used to
     * be called while streaming the file (as save_secret()) and fell back
     * to writing "" on a DPAPI failure, so a save (including the automatic
     * migration re-save inside config_load()) could silently wipe a
     * password or API key that had decrypted, or was preserved, just fine
     * a moment earlier. Preparing every blob up front also means each
     * secret is only ever encrypted once per save. */
    char ai_api_key_blob[CFG_BLOB_MAX];
    if (secret_prepare(s->ai_api_key, s->ai_api_key_enc_preserved,
                        ai_api_key_blob, sizeof(ai_api_key_blob)) != CRYPTO_OK) {
        secure_zero(ai_api_key_blob, sizeof(ai_api_key_blob));
        return -1;
    }

    char (*pw_blobs)[CFG_BLOB_MAX] = NULL;
    if (n > 0u) {
        pw_blobs = xmalloc(n * sizeof(*pw_blobs));
        for (size_t i = 0u; i < n; i++) {
            const Profile *pr = (const Profile *)vec_get(&cfg->profiles, i);
            if (secret_prepare(pr->password, pr->password_enc_preserved,
                                pw_blobs[i], CFG_BLOB_MAX) != CRYPTO_OK) {
                for (size_t j = 0u; j <= i; j++) {
                    secure_zero(pw_blobs[j], CFG_BLOB_MAX);
                }
                free(pw_blobs);
                secure_zero(ai_api_key_blob, sizeof(ai_api_key_blob));
                return -1;
            }
        }
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
        if (pw_blobs) {
            for (size_t i = 0u; i < n; i++) secure_zero(pw_blobs[i], CFG_BLOB_MAX);
            free(pw_blobs);
        }
        secure_zero(ai_api_key_blob, sizeof(ai_api_key_blob));
        return -1;
    }

    fputs("{\n  \"settings\": {\n", f);
    fputs("    \"font\": ", f);
    fprint_json_str(f, s->font);
    fputs(",\n", f);
    fputs("    \"ai_font\": ", f);
    fprint_json_str(f, s->ai_font);
    fputs(",\n", f);
    fprintf(f, "    \"font_size\": %d,\n", s->font_size);
    fprintf(f, "    \"scrollback_lines\": %d,\n", s->scrollback_lines);
    fprintf(f, "    \"paste_delay_ms\": %d,\n", s->paste_delay_ms);
    fprintf(f, "    \"logging_enabled\": %s,\n",
            s->logging_enabled ? "true" : "false");
    fprintf(f, "    \"debug_terminal\": %s,\n",
            s->debug_terminal ? "true" : "false");
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
    fputs("    \"colour_scheme\": ", f);
    fprint_json_str(f, s->colour_scheme);
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
    fprint_json_str(f, ai_api_key_blob);
    fputs(",\n", f);
    fputs("    \"ai_system_notes\": ", f);
    fprint_json_str(f, s->ai_system_notes);
    fputs(",\n", f);
    fputs("    \"ai_search_provider\": ", f);
    fprint_json_str(f, s->ai_search_provider);
    fputs(",\n", f);
    fputs("    \"ai_search_url\": ", f);
    fprint_json_str(f, s->ai_search_url);
    fputs(",\n", f);
    fprintf(f, "    \"ai_max_search_results\": %d,\n", s->ai_max_search_results);
    fprintf(f, "    \"ai_web_fetch_enabled\": %s,\n",
            s->ai_web_fetch_enabled ? "true" : "false");
    fprintf(f, "    \"ssh_user_idle_timeout_mins\": %d,\n",
            s->ssh_user_idle_timeout_mins);
    fprintf(f, "    \"markdown_render_enabled\": %s,\n",
            s->markdown_render_enabled ? "true" : "false");
    fprintf(f, "    \"ai_max_context_lines\": %d,\n", s->ai_max_context_lines);
    fprintf(f, "    \"auto_connect\": %s,\n",
            s->auto_connect ? "true" : "false");
    fputs("    \"auto_connect_session\": ", f);
    fprint_json_str(f, s->auto_connect_session);
    fputs(",\n", f);
    fprintf(f, "    \"paste_confirm\": %s,\n",
            s->paste_confirm ? "true" : "false");
    fprintf(f, "    \"open_session_manager_at_start\": %s,\n",
            s->open_session_manager_at_start ? "true" : "false");
    {
        char policy_tok[32];
        cmd_policy_to_token(s->ai_policy_default, policy_tok, sizeof(policy_tok));
        fputs("    \"ai_policy_default\": ", f);
        fprint_json_str(f, policy_tok);
    }
    fputs("\n", f);
    fputs("  },\n  \"profiles\": [\n", f);

    for (size_t i = 0u; i < n; i++) {
        const Profile *pr = (const Profile *)vec_get(&cfg->profiles, i);
        fputs("    {\n", f);
        fputs("      \"name\": ", f);
        fprint_json_str(f, pr->name);
        fputs(",\n", f);
        fputs("      \"kind\": ", f);
        fprint_json_str(f, pr->kind);
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
        fprint_json_str(f, pw_blobs[i]);
        fputs(",\n", f);
        fputs("      \"key_path\": ", f);
        fprint_json_str(f, pr->key_path);
        fputs(",\n", f);
        fputs("      \"shell\": ", f);
        fprint_json_str(f, pr->shell);
        fputs(",\n", f);
        fputs("      \"ai_notes\": ", f);
        fprint_json_str(f, pr->ai_notes);
        fputs(",\n", f);
        fputs("      \"platform\": ", f);
        fprint_json_str(f, pr->platform);
        fputs("\n    }", f);
        if (i + 1u < n) {
            fputc(',', f);
        }
        fputc('\n', f);
    }

    fputs("  ]\n}\n", f);

    /* M5: a write error anywhere above (disk full, a transient I/O error)
     * only ever shows up here, at fflush/fclose time -- stdio buffers
     * output and a single fputs()/fprintf() call has no useful return value
     * to check for it. ferror() must be read BEFORE fclose(), which resets
     * the stream's error indicator; fclose()'s own return additionally
     * catches the final flush failing. Either one means the temp file is
     * incomplete or corrupt: remove it (it holds nothing the real config
     * file at `path` doesn't already have -- only encrypted blobs, no
     * plaintext secrets, but still not worth leaving around) and leave the
     * real file at `path` completely untouched, rather than renaming
     * something broken over it. */
    int write_failed = ferror(f);
    if (fclose(f) != 0) write_failed = 1;
    if (write_failed) {
        (void)remove(tmp_path);
        free(tmp_path);
        if (pw_blobs) {
            for (size_t i = 0u; i < n; i++) secure_zero(pw_blobs[i], CFG_BLOB_MAX);
            free(pw_blobs);
        }
        secure_zero(ai_api_key_blob, sizeof(ai_api_key_blob));
        return -1;
    }

    /* Atomically replace the real config file with the completed temp file.
     * M5: MOVEFILE_WRITE_THROUGH makes MoveFileExA wait for the rename (and
     * the data behind it) to actually reach disk before returning, instead
     * of a cached, technically-successful rename that a crash or power loss
     * moments later could still lose -- the whole point of the temp-file
     * dance above. */
#ifdef _WIN32
    int moved = MoveFileExA(tmp_path, path,
                            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)
                ? 0 : -1;
#else
    int moved = rename(tmp_path, path);
#endif
    if (moved != 0) {
        /* The temp file never made it into place -- clean it up rather
         * than leaving an orphaned ".tmp" (holding the same encrypted
         * blobs as the real file) beside the config indefinitely. The real
         * file at `path`, if any, is untouched either way. */
        (void)remove(tmp_path);
    }
    free(tmp_path);

    if (pw_blobs) {
        for (size_t i = 0u; i < n; i++) secure_zero(pw_blobs[i], CFG_BLOB_MAX);
        free(pw_blobs);
    }
    secure_zero(ai_api_key_blob, sizeof(ai_api_key_blob));

    return moved;
}

/* ---- Profile lookup ------------------------------------------------------- */

/* ASCII case-insensitive equality (config matching is exact, not prefix). */
static int str_ieq(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

Profile *config_find_profile_by_name(const Config *cfg, const char *name)
{
    if (!cfg || !name || name[0] == '\0') {
        return NULL;
    }
    size_t n = vec_size(&cfg->profiles);
    for (size_t i = 0u; i < n; i++) {
        Profile *p = (Profile *)vec_get(&cfg->profiles, i);
        if (p && p->name[0] != '\0' && str_ieq(p->name, name)) {
            return p;
        }
    }
    return NULL;
}

Profile *config_find_profile_by_host(const Config *cfg, const char *host)
{
    if (!cfg || !host || host[0] == '\0') {
        return NULL;
    }
    size_t n = vec_size(&cfg->profiles);
    for (size_t i = 0u; i < n; i++) {
        Profile *p = (Profile *)vec_get(&cfg->profiles, i);
        if (p && p->host[0] != '\0' && str_ieq(p->host, host)) {
            return p;
        }
    }
    return NULL;
}

int config_ensure_local_profile(Config *cfg)
{
    if (!cfg) {
        return 0;
    }
    size_t n = vec_size(&cfg->profiles);
    for (size_t i = 0u; i < n; i++) {
        Profile *p = (Profile *)vec_get(&cfg->profiles, i);
        if (p && strcmp(p->kind, "local") == 0) {
            return 0;
        }
    }

    Profile *p = config_profile_new();
    field_copy(p->name, sizeof(p->name), "Local shell");
    field_copy(p->kind, sizeof(p->kind), "local");
    vec_insert(&cfg->profiles, 0u, p);
    return 1;
}

/* ---- Secret-field helpers (public; see config.h) --------------------- */

void config_secret_drop_stale_preserved(const char *plain, char *preserved,
                                         size_t preserved_cap)
{
    if (plain && plain[0] != '\0' && preserved && preserved_cap > 0u) {
        preserved[0] = '\0';
    }
}

/* L: non-zero when `s` is an absolute Windows path -- a drive letter
 * ("C:\..." or "C:/...") or a UNC prefix ("\\server\..."). %LOCALAPPDATA%
 * is always one of these on a real Windows install; a relative value (a
 * hand-edited or otherwise hostile environment) would make the "absolute,
 * per-user" path this function promises actually resolve against whatever
 * the process's current directory happens to be -- exactly the hazard its
 * own doc comment says building an absolute path here exists to avoid. */
static int is_absolute_windows_path(const char *s)
{
    size_t len = strlen(s);
    if (len >= 3 &&
        ((s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= 'a' && s[0] <= 'z')) &&
        s[1] == ':' && (s[2] == '\\' || s[2] == '/')) {
        return 1;
    }
    if (len >= 2 && s[0] == '\\' && s[1] == '\\') return 1;
    return 0;
}

int config_fallback_path(const char *local_appdata, char *out, size_t out_cap)
{
    if (!local_appdata || local_appdata[0] == '\0' || !out || out_cap == 0u) {
        return 0;
    }
    if (!is_absolute_windows_path(local_appdata)) {
        return 0;
    }
    int n = snprintf(out, out_cap, "%s\\Nutshell\\" CONFIG_FILENAME, local_appdata);
    if (n < 0 || (size_t)n >= out_cap) {
        out[0] = '\0';
        return 0;
    }
    return 1;
}
