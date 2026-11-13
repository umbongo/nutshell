# Audit tags cited in the code

Comments in `src/` and `tests/` cite short tags (`H-7`, `M2`, "audit C2") that
come from several security reviews. The 2026-09-09 security-audit spec that the
tests cite (`docs/superpowers/specs/2026-09-09-security-audit.md`) was never
committed; b5d3887 (2026-09-09, "fix(security): reject control characters in AI
commands, bound two buffers, clamp the VT parser", v1.1.10) added its
regressions. This page says what each tag means and where it is cited, so a tag
in a comment can be resolved without that document. Locations are
`file:function` (no line numbers, which drift); a name in `src/` is where the
fix lives and a name in `tests/` is the regression test or its section heading.

The same letter-and-number pair is reused by different reviews, so a tag is only
unambiguous together with its origin. Table 2 therefore has one row per
(tag, origin) and says which area of the code each sense belongs to.

Not audit tags, and not listed: `C0` and `C1` as the names of the control
character ranges (U+0000-001F and U+0080-009F; they appear in the paste filter,
the UTF-8 decoder, the terminal text extractors and the AI command filter's
comments), `C2` as the UTF-8 lead byte 0xC2 in `ai_prompt.c` and
`string_utils` tests, `C3` as the lead byte 0xC3 in `test_ai_chat.c`, `L1`/`L2`
as relative luminances in `theme.h`, and `L1` to `L6` as row labels in test
data (`test_color_consistency.c`, `test_term_extract.c`).

## Table 1: the hyphenated series (H-n, M-n, L-n, C-n, I-n)

Origin: the 2026-03-04 commit 6e1ccae, "Fix all high/medium/low security
vulnerabilities; set SSH timeout to 4s", whose message lists the series (H-1
and M-6 from that list are not cited in the code; I-2 is cited but is not in
the message). Four tags were
reused by the 2026-09-24 hardening work and carry a second sense, marked
(2026-09-24); H-6 and H-7 exist only in that later work (9415d39).

| Tag | Meaning | Locations |
|---|---|---|
| C-1 | Per-blob random 16-byte PBKDF2 salt embedded in the legacy AES-GCM blobs | `src/crypto/crypto.c:crypto_encrypt` (and the `SALT_LEN` definition) |
| H-2 | `getaddrinfo` instead of `gethostbyname`: thread-safe, IPv4 and IPv6 | `src/term/ssh_session.c:ssh_connect` |
| H-3 | Zero the plaintext password once authentication is done, and the session's copy once the connect result is handed over | `src/ui/window.c:connection_thread`, `src/ui/window.c:WndProc` (connection-done handling) |
| H-4 | Pass the real host-key type (RSA, ECDSA, Ed25519) when adding a known_hosts entry | `src/term/knownhosts.c:map_key_type`, `src/term/knownhosts.c:knownhosts_add` |
| H-6 | (2026-09-24) known_hosts names are stored lowercase; an older mixed-case entry still verifies through one retry with the exact casing, and re-adding replaces it | `src/term/knownhosts.c:build_entry_name`, `src/term/knownhosts.c:knownhosts_lookup`, `src/term/knownhosts.c:remove_entries_ci`, `tests/test_knownhosts.c:test_kh_legacy_mixed_case_compat` (section heading) |
| H-7 | (2026-09-24) known_hosts add is a read-merge-write under a cross-process lock, so two tabs or two processes cannot lose each other's entry | `src/term/knownhosts.c:knownhosts_add`, `src/term/knownhosts.c:load_store_from_disk`, `src/term/knownhosts.c:knownhosts_write_atomic`, `src/term/knownhosts.c:remove_entries_ci`, `tests/test_knownhosts.c:test_kh_two_instances_both_persist`, `tests/test_knownhosts.c:test_kh_concurrent_add_no_lost_update` |
| I-1 | 4-second TCP connect timeout (non-blocking socket plus select) | `src/term/ssh_session.c:ssh_connect` |
| I-2 | Initialise Winsock once for the life of the process | `src/main.c:WinMain` |
| I-3 | Tab limit reached: show an error dialog and clean up | `src/ui/window.c:on_session_connect` |
| L-1 | Wipe secrets with `secure_zero` or a volatile loop, not a `memset` the compiler may drop | `src/config/loader.c:config_profile_free`, `src/crypto/crypto.c:crypto_decrypt`, `src/crypto/crypto.c:crypto_decrypt_with_key`, `src/term/ssh_session.c:ssh_session_free` |
| L-2 | Guard plaintext length (over 64 KB) before the `int` cast into `EVP_EncryptUpdate` | `src/crypto/crypto.c:crypto_encrypt`, `src/crypto/crypto.c:crypto_encrypt_with_key` |
| L-3 | Escape control characters below 0x20 as `\uXXXX` in the JSON the config writer emits (RFC 8259) | `src/config/loader.c:fprint_json_str` |
| L-4 | Explicit bounds check before indexing the CSI parameter array | `src/term/parser.c:term_process` |
| L-5 | `total_read` accumulator is `size_t`, not `int` | `src/config/ssh_io.c:ssh_io_poll` |
| M-1 | Reject plaintext over 64 KB before the base64 size arithmetic can overflow | `src/crypto/crypto.c:crypto_encrypt`, `src/crypto/crypto.c:crypto_encrypt_with_key` |
| M-2 | Two senses. (March) check the `RegQueryValueExA` result when reading MachineGuid, and terminate the key material only on success. (2026-09-24) when a secret cannot be encrypted at save time, abort the whole save and leave the file untouched instead of writing an empty string over the secret | `src/crypto/crypto.c:get_key_material`; `src/config/loader.c:config_save`, `src/config/loader.c:secret_prepare`, `tests/test_config.c:test_config_save_aborts_when_dpapi_encrypt_fails`, `tests/fake_dpapi.h:fake_dpapi_set_fail_protect` |
| M-3 | Two senses. (March) reject a config file over the size limit (since 2026-09-24 the limit is 8 MB). (2026-09-24) a bare, unencrypted secret in the file is flagged for the same migration re-save as a legacy blob | `src/config/loader.c:read_file_status`; `src/config/loader.c:load_secret`, `tests/test_config.c:test_config_bare_plaintext_password_migrates_on_load` |
| M-4 | Two senses. (March) save the config to a temporary file and rename it over the target, so a crash cannot truncate it. (2026-09-24) typing a new secret into an edit box drops any stale foreign or corrupt preserved blob still in memory | `src/config/loader.c:config_save`; `src/config/config.h:config_secret_drop_stale_preserved`, `src/ui/settings.c:SettingsWndProc`, `src/ui/session_manager.c:SessMgrDlgProc`, `tests/test_config.c:test_config_secret_drop_stale_preserved_core` |
| M-5 | Wipe the passphrase and password buffer when the prompt is cancelled, not only when it is accepted | `src/ui/pw_prompt.c:pw_prompt_show` |
| M-7 | Validate the port is 1-65535 before connecting | `src/term/ssh_session.c:ssh_connect` |
| M-8 | Resolve the config file to an absolute, exe-relative path at startup so a file dialog changing the working directory cannot redirect a save (the settings dialog tags it with H6 as well) | `src/ui/window.c:WndProc` (`WM_CREATE`, and the `g_config_path` global), `src/ui/settings.c:SettingsDlgData` (the `config_path` field) |

## Table 2: the unhyphenated series (C1, H2, M3, ...)

Four origins, which reuse the same letters and numbers:

- **2026-09-09 audit**: the never-committed security-audit spec. Its findings
  are C1 to C4, H2, H6 and H9, L3 and L4. C1, C3, C4, H9 and L4 were fixed in
  b5d3887 (v1.1.10); C2 and H2 in PR #24 (v1.1.14 to v1.1.17); H6 with the
  absolute config save path in v1.2.14.
- **2026-09-11 classification coverage spec**
  (`docs/superpowers/specs/2026-09-11-command-classification-coverage.md`): its
  section 2 names the shared classifier mechanics M1, M2 and M4 (the cited
  `F1` to `F6`, `F8` and `F9` are findings in the same spec). Only `cmd_classify.c` and its tests
  use these senses.
- **2026-09-24 security review** (the hardening shipped as v1.2.14 and v1.2.15,
  PR #55; 518914e, 0e0dc74): H1 to H4, M1 to M6 for the local shell, config
  loading and saving, and platform detection. The classifier-holes list from the
  PR #50 review (2026-09-23) fed the same pass (22a734c) but its items carry no
  tags in the code.
- **2026-11-11 run_command review** (PR #94, d966713; spec
  `docs/superpowers/specs/2026-11-05-run-command-tool-design.md`): the reviews of
  the `run_command` tool's implementation tasks (B1 to B16, the spec's own task
  numbers; B8, B13 and B15 are cited as task headings too) and their fix
  rounds. Its tags `H3` and `M1` collide with the 2026-09-24 review's `H3` and
  `M1` (and `M1` with the coverage spec's) in the table below: they belong to
  the AI command path (`run_command.c`, `cmd_run_log.h`, the agentic loop), not
  to the local shell or the classifier.

| Tag | Origin | Meaning | Locations |
|---|---|---|---|
| C1 | 2026-09-09 audit | A newline or other control byte inside an `[EXEC]` block could smuggle a second, unclassified command past the approval card; such commands are dropped, the user sees a status line and the model is told to send one single-line command per block (the approval queue and channel write refuse them too) | `src/ui/ai_chat.c:AiChatWndProc`, `tests/test_ai_prompt.c:test_ai_extract_commands_newlines_in_command`, `tests/test_ai_prompt.c:test_ai_extract_commands_ex_embedded_newline_rejected` (section heading), `tests/test_chat_approval.c:test_approval_add_embedded_newline_rejected` |
| C2 | 2026-09-09 audit | The Linux classifier treated "no rule matched" as SAFE, so an unrecognised command auto-approved at the lowest level; it is now UNKNOWN unless on an explicit allow-list (also covers network verbs typed under the Linux ruleset) | `src/core/cmd_classify.c:classify_linux_segment`, `tests/test_cmd_classify.c:test_cmd_classify_linux_unrecognised_is_unknown`, `tests/test_cmd_classify.c:test_cmd_classify_network_verbs_under_linux_are_unknown`, `tests/test_cmd_classify.c:test_cmd_classify_unknown_overlay_write_erase` (and the neighbouring `_overlay_` tests) |
| C3, L3 | 2026-09-09 audit | `bp += snprintf(...)` accumulations added the would-have-written length and ran past the buffer; replaced by `str_append_fmt`, which stops at capacity | `src/core/string_utils.h:str_append_fmt`, `tests/test_string_utils.c` (the `str_append_fmt` section) |
| C4 | 2026-09-09 audit | Erase-in-line and erase-in-display used the cursor column as a bound, which deferred wrap can leave equal to the width, writing one cell past the row; the bound is clamped without disturbing the wrap | `src/term/parser.c:handle_csi`, `tests/test_vt_sequences.c:test_vt_el1_at_deferred_wrap_no_oob` |
| H9, L4 | 2026-09-09 audit | CSI count parameters (insert, delete and erase character, scroll, insert and delete line) are clamped to the screen and the digit accumulator saturates at 65535 instead of overflowing | `src/term/parser.c:handle_csi`, `src/term/parser.c:term_process`, `tests/test_vt_sequences.c:test_vt_csi_digit_saturates_at_65535` |
| H2 | 2026-09-09 audit | Every command was classified with the Linux ruleset whatever the device; the per-profile Platform setting with banner and prompt auto-detection fixes that | `tests/test_config.c` ("Profile platform" section, `test_config_profile_platform_missing_defaults_to_auto`), `tests/runner.c:main` (registration of those tests) |
| H2 | 2026-09-24 review | A vendor banner and a last line that disagrees resolve nothing: the banner decides only while the prompt shape is ambiguous, absent or agrees | `tests/test_cmd_detect.c:test_cmd_detect_hop_from_linux_to_switch_and_back_is_conflict`, `tests/test_cmd_detect.c:test_cmd_detect_scan_linux_then_banner_looking_line_not_ios` |
| H6 | 2026-09-09 audit | Config saves go to the absolute path resolved at startup, never the bare filename relative to a drifting working directory (cited together with M-8) | `src/ui/settings.c:SettingsDlgData` (the `config_path` field) |
| H1 | 2026-09-24 review | A session whose platform host output has contradicted keeps that platform but is judged under the worse of its own ruleset and UNKNOWN's; host output never replaces a resolved platform or clears the contradicted flag | `tests/test_cmd_classify.c:test_cmd_classify_session_contradicted_device_examples`, `tests/test_chat_approval.c:test_approval_add_session_contradicted_takes_worse`, `tests/test_cmd_detect.c:test_cmd_detect_scan_linux_then_banner_looking_line_not_ios` (with H2) |
| H3 | 2026-09-24 review | The local shell's platform lock must read `spec->kind` after `local_shell_resolve_bare()` has reclassified it, not the stale local `kind`; a custom command that is not a known POSIX shell is locked to the strictest ruleset | `src/ui/window.c:start_local_shell`, `src/core/local_shell.c:local_shell_platform_lock`, `tests/test_local_shell.c:test_local_shell_platform_lock_strict_for_unmatched_custom` |
| H4 | 2026-09-24 review | An unquoted custom command that begins with an absolute path and contains spaces is split at most once (whole string, else up to the last space); a split that cuts the path is refused | `src/core/local_shell.c:resolve_unquoted_spaced`, `tests/test_local_shell.c:test_local_shell_resolve_bare_unquoted_spaced_refuses_shorter_match`, `tests/test_local_shell.c:test_local_shell_resolve_bare_unquoted_spaced_refuses_second_argument` |
| M1 | 2026-09-24 review | A local shell spec whose executable ends up empty (for example a first token too long to fit) is refused instead of reaching `CreateProcess` as a NULL application name | `src/core/local_shell.c:local_shell_resolve_bare`, `src/ui/local_pty.c:local_pty_open_ex`, `tests/test_local_shell.c:test_local_shell_resolve_bare_refuses_empty_exe_from_long_token`, `tests/test_local_pty.c:spec_for` |
| M1 | 2026-09-11 coverage spec | On every platform except Linux `\|` is a display filter, not a shell pipe, so the classifier does not split a device segment on it | `src/core/cmd_classify.c:classify_pass`, `src/core/cmd_classify.c:device_shell_floor`, `tests/test_cmd_classify.c:test_cmd_classify_network_display_filter_still_safe` |
| M2 | 2026-09-24 review | A hand-typed custom shell path is canonicalised (`GetFullPathNameA`) and reclassified by exact match to a detected install, or by base name for `powershell`, `pwsh` and `cmd` | `src/core/local_shell.c:reclassify_if_known_shell`, `src/core/local_shell.c:reclassify_by_base_name`, `src/ui/local_shell_probe.c:probe_normalize_path`, `src/core/local_shell.h` (the probe's `normalize_path` member), `tests/test_local_shell.c:test_local_shell_resolve_bare_reclassifies_powershell_by_base_name` |
| M2 | 2026-09-11 coverage spec | Device-filesystem token helper: delete, format, erase, squeeze and copy rules scan every remaining token for a device filesystem prefix, not only token 2 | `src/core/cmd_classify.c:is_device_fs_token`, `src/core/cmd_classify.c:classify_cisco_ios_segment` |
| M3 | 2026-09-24 review | Config loading distinguishes "no file yet" (fine to write defaults) from "file there but unreadable" (locked, too large, permissions: saving is disabled for the session so the user's real config is not overwritten) and from "invalid" | `src/config/config.h:ConfigLoadStatus`, `src/config/loader.c:read_file_status`, `src/config/loader.c:config_load_ex`, `src/ui/window.c:WndProc` (`WM_CREATE`), `src/ui/window.c:active_config_path`, `tests/test_config.c:test_config_load_ex_missing_reports_missing`, `tests/test_config.c:test_config_load_ex_oversized_reports_unreadable` |
| M4 | 2026-09-24 review | An unparseable config is backed up as `<path>.bad-<timestamp>[-n]` with a unique name (never replacing an earlier backup); if the backup fails the load is reported unreadable and saving is disabled | `src/config/loader.c:config_backup_unparseable_file`, `src/config/loader.c:pick_unique_bad_path`, `src/config/loader.c:config_load_ex`, `src/ui/window.c:WndProc`, `tests/test_config.c:test_config_backup_unparseable_never_clobbers_same_second` |
| M4 | 2026-09-11 coverage spec | Verb-anywhere token scan (`seg_has_token_ci`) for RouterOS, whose verb comes last | `src/core/cmd_classify.c:seg_has_token_ci`, `src/core/cmd_classify.c:classify_mikrotik_segment` |
| M5 | 2026-09-24 review | A failed config save is reported, not swallowed: `config_save` checks `ferror` before `fclose` and uses write-through for the rename, and every save site shows an error | `src/config/loader.c:config_save`, `src/ui/session_manager.c:SessMgrDlgProc`, `src/ui/settings.c:SettingsWndProc`, `src/ui/window.c:WndProc` |
| M6 | 2026-09-24 review | `term_extract_last_n_dup` is sized to the content, so a wide prompt row keeps its closing `$`, `#` or `>` (the contradiction check had read four rows into 256 bytes) | `tests/test_term_extract.c:test_extract_last_n_dup_wide_row_keeps_prompt` |
| H3 | 2026-11-11 run_command review (fix round 3) | The rows a reply's card does not show (a call on no card line, such as an over-cap or refused call, and quoted `[EXEC]` commands) are one core text, drawn live under the card and rebuilt identically from the batch or its log record; a reply answered without a card keeps its row for the rebuild | `src/core/run_command.c` (section "Rows for what a reply's card does not show", `row_cut`), `src/core/run_command.h:runcmd_unqueued_row`, `src/core/run_command.h:runcmd_quoted_row`, `src/core/cmd_run_log.h` (the record's rows), `tests/test_cmd_run_log.c:test_cmd_run_log_status_stores_no_card` (section heading), `tests/test_chat_rebuild.c:test_rebuild_replay_no_card_reply_shows_the_live_rows` (section heading), `tests/test_run_command.c` (section heading "H3: the rows of a call that is on no card line") |
| M1 | 2026-11-11 run_command review | The finish's room: the assistant tool turn is already stored, so only the results and the reply are still to come when the conversation limit is checked | `tests/test_agentic_loop.c:test_agentic_room_for_finish` |
| I6, M9 | 2026-11-11 run_command review (review of B1-B11) | The conversation snapshot stays on its question while only notes follow (spec decisions 19 and 22) | `tests/test_ai_body_frame.c:frozen_after_round` (section heading) |
| B16 | 2026-11-11 run_command review | Task B16, the finish function's decisions: a command the dispatcher never wrote is answered as not run, and only a finish that leaves the session in place and starts no request of its own may start the next approved batch (fix round 2) | `tests/test_run_command.c:test_runcmd_results_unsent_command`, `tests/test_run_command.c:test_runcmd_cause_starts_next_table` |
