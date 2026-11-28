# Integration harness cases never run

**Status**: NOT RUN
**Needs**: PowerShell 7 on the Windows box (no `pwsh` on `automaton`, so these cases have never been parsed or run); an SSH host that takes a key with no passphrase; an OpenAI key for the voice cases; `build\win\nutshell.exe` from the build under test
**Time**: 20 minutes for the gate tier, longer for ai

The harness (`tests/integration/Run-Integration.ps1`) drives the real exe. It
posts messages, so it works on a locked desktop. Never stop a `nutshell`
process you did not start. The first run of an unparsed case may fail on the
case's own script, not the product: note which.

## Commands

```
.\tests\integration\Run-Integration.ps1 -HostName <host> -User <user> -KeyPath <path to key> -Tier gate
.\tests\integration\Run-Integration.ps1 -HostName <host> -User <user> -KeyPath <path to key> -Tier ai
.\tests\integration\Run-Integration.ps1 -HostName <host> -User <user> -KeyPath <path to key> -Only <case>,<case>
```

`-Tier gate` runs every case that needs no AI key. `-Tier ai` runs the cases
that cost model credits. `-Tier nightly` is empty. Logs and screenshots go to
`tests\integration\artifacts\`. See `tests/integration/README.md`.

## Gate tier

1. **The whole gate tier**
   Do: Run the first command above.
   Expect: Exit code 0 and every case passes. Write down each case that fails.
   If it fails: Note the case names and the first error line of each. Known: `resize_applies_to_inactive_tab` fails on every run since v1.2.13, and `ctrl_w_reaches_shell_and_shift_closes_tab` is intermittent.
   Result:

2. **`notice_dialog_gallery`**
   Do: Run with `-Only notice_dialog_gallery`.
   Expect: Pass, with eight screenshots in `artifacts\gallery\` (info and warning, four themes).
   If it fails: Note the error.
   Result:

3. **Launch and menu cases from PR #106**
   Do: Run `-Only launch_upgrade_adds_fields,launch_ai_off_no_ai_items,menus_open_and_list_items,menus_open_and_list_items_ai_on` and the rewritten `LAUNCH-2` and `LAUNCH-3` (ask the session for their exact names).
   Expect: All pass.
   If it fails: Note the case and error.
   Result:

4. **Self-report cases**
   Do: Run `-Only self_report_ai_off_clean,self_report_voice_off_clean`.
   Expect: Pass. With AI off, `winhttp.dll`, `riched20.dll` and `msftedit.dll` were never requested. With voice off, no `mmdevapi.dll` or `audioses.dll`.
   If it fails: Note which DLL.
   Result:

5. **`settings_help_tool_count`**
   Do: Run `-Only settings_help_tool_count`.
   Expect: Pass. The README says the counts are 1, 1 and 107; PR #112 updated it to 110 (the notes' "107" and the PR's "107 to 110" disagree: if it fails with 110, the case text is out of date).
   If it fails: Note the number found.
   Result:

6. **`ui_gallery`**
   Do: Run `-Only ui_gallery`.
   Expect: Pass with screenshots of every demo state on four themes (the notes say 22 states x 4 = 88 captures after the voice states, with `header-status`, `runlist`, `thinking-compact`, `thinking-open` and `thinking-extended` added since it last ran).
   If it fails: Note the state and theme.
   Result:

7. **Voice gallery and key case**
   Do: Run `-Only settings_voice_gallery,voice_off_right_shift_reaches_input`.
   Expect: Pass.
   If it fails: Note the error.
   Result:

8. **AI panel cases that look for a notice**
   Do: Run `-Only ai_panel_docks_with_key,ai_panel_opens_without_key`.
   Expect: Pass. A `Nutshell_Notice` window counts as an unexpected dialog.
   If it fails: Note the error.
   Result:

9. **Telnet cases**
   Do: Run `-Only telnet_autologin_never_logs_password,telnet_port_refused_reports_in_tab,telnet_port_check_is_passive_and_echoes_locally,telnet_password_box_asks_and_sends,ssh_password_box_cancel_reports_in_tab`.
   Expect: All five pass (`85-telnet.ps1` has never been parsed or run). The last needs the SSH host.
   If it fails: Note the case and error.
   Result:

10. **Help shortcuts sheet cases**
    Do: Run `-Only help_shortcuts_sheet_opens_and_closes,help_shortcuts_sheet_fits_the_screen,help_shortcuts_sheet_gallery`.
    Expect: Pass. The sheet fits one page at 96 DPI on 1920x1040.
    If it fails: Note the error.
    Result:

11. **PR #76's four gate cases and `cli_version_redirect_file`**
    Do: Run the gate tier and read these in the output (case names: ask the session; PR #76 added four selection cases).
    Expect: Pass.
    If it fails: Note the case.
    Result:

## AI tier

12. **The five key-gated AI cases**
    Do: Run the second command above with a real AI key in the harness settings.
    Expect: All pass. They cost model credits.
    If it fails: Note the case and error.
    Result:

13. **Voice fixtures**
    Do: Build the harness exe on automaton (`make harness-exe`) and copy `build\win\nutshell-it.exe` over. Set `NUTSHELL_IT_STT_KEY` to `<your OpenAI key>`. Run `-Only voice_live_dictation_fixture,voice_conversation_card_fixture`.
    Expect: Pass. The first types "check the disk space on this machine" from a synthesised WAV. The second runs a conversation through Hearing, Sending and Waiting for your approval. (Written, never parsed. Likely first fixes: `GetNewClosure()` with the `$Exe` override, the `SpeechAudioFormatInfo` overload, reading the Send button text, the mic click as `WM_COMMAND`. The conversation case does not check that the command ran.)
    If it fails: Note the case and the first error line.
    Result:

## Windows unit tests

14. **`mingw32-make wintest`**
    Do: On the Windows box run `mingw32-make wintest`.
    Expect: No failures. The tests that skip under Wine must pass here: `test_ai_nc_fire_wakes_blocked_winhttp_call`, `test_voice_ws_shutdown_wakes_receiver`, `test_voice_ws_cancel_during_handshake`, `test_voice_live_close_mid_handshake`, `test_ai_stream_ex_cancel_hang`, `test_voice_ais_quit_while_server_hangs`. The `test_chat_select_win_thinking_*` cases and `test_settings_label_win_labels_fit_column` have no recorded Windows run.
    If it fails: Note the test name and output.
    Result:

15. **`mingw32-make test` and a release build**
    Do: `mingw32-make test`, then `mingw32-make clean && mingw32-make release`.
    Expect: Both finish clean with the newer gcc.
    If it fails: Note the first warning or error.
    Result:
