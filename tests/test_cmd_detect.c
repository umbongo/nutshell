/* tests/test_cmd_detect.c */
#include "test_framework.h"
#include "cmd_detect.h"
#include <string.h>

/* --- NULL / empty input --- */

int test_cmd_detect_null_text(void) {
    TEST_BEGIN();
    CmdDetectConfidence conf = CMD_DETECT_BANNER; /* poisoned, must be overwritten */
    CmdPlatform p = cmd_detect_platform(NULL, 0, &conf);
    ASSERT_EQ((int)p, (int)CMD_PLATFORM_UNKNOWN);
    ASSERT_EQ((int)conf, (int)CMD_DETECT_NONE);
    TEST_END();
}

int test_cmd_detect_empty_text(void) {
    TEST_BEGIN();
    CmdDetectConfidence conf = CMD_DETECT_BANNER;
    CmdPlatform p = cmd_detect_platform("", 0, &conf);
    ASSERT_EQ((int)p, (int)CMD_PLATFORM_UNKNOWN);
    ASSERT_EQ((int)conf, (int)CMD_DETECT_NONE);
    TEST_END();
}

int test_cmd_detect_null_confidence_out(void) {
    TEST_BEGIN();
    /* confidence_out may be NULL -- must not crash, and the platform still
     * resolves. */
    const char *text = "tom@webhost:~$";
    CmdPlatform p = cmd_detect_platform(text, strlen(text), NULL);
    ASSERT_EQ((int)p, (int)CMD_PLATFORM_LINUX);
    TEST_END();
}

/* --- 3.1 banner signals: one realistic multi-line banner per platform --- */

int test_cmd_detect_banner_nxos(void) {
    TEST_BEGIN();
    const char *banner =
        "Cisco Nexus Operating System (NX-OS) Software\r\n"
        "TAC support: http://www.cisco.com/tac\r\n"
        "Copyright (c) 2002-2023, Cisco Systems, Inc. All rights reserved.\r\n"
        "switch#";
    CmdDetectConfidence conf = CMD_DETECT_NONE;
    CmdPlatform p = cmd_detect_platform(banner, strlen(banner), &conf);
    ASSERT_EQ((int)p, (int)CMD_PLATFORM_CISCO_NXOS);
    ASSERT_EQ((int)conf, (int)CMD_DETECT_BANNER);
    TEST_END();
}

int test_cmd_detect_banner_asa(void) {
    TEST_BEGIN();
    const char *banner =
        "Cisco Adaptive Security Appliance Software Version 9.16(1)\r\n"
        "Compiled on Fri 01-Oct-21 12:00 by builders\r\n"
        "ciscoasa>";
    CmdDetectConfidence conf = CMD_DETECT_NONE;
    CmdPlatform p = cmd_detect_platform(banner, strlen(banner), &conf);
    ASSERT_EQ((int)p, (int)CMD_PLATFORM_CISCO_ASA);
    ASSERT_EQ((int)conf, (int)CMD_DETECT_BANNER);
    TEST_END();
}

int test_cmd_detect_banner_ios(void) {
    TEST_BEGIN();
    const char *banner =
        "Cisco IOS Software, C3560 Software (C3560-IPSERVICESK9-M), Version 15.2(4)E\r\n"
        "Copyright (c) 1986-2018 by Cisco Systems, Inc.\r\n"
        "Compiled Thu 07-Jun-18 by prod_rel_team\r\n"
        "Router>";
    CmdDetectConfidence conf = CMD_DETECT_NONE;
    CmdPlatform p = cmd_detect_platform(banner, strlen(banner), &conf);
    ASSERT_EQ((int)p, (int)CMD_PLATFORM_CISCO_IOS);
    ASSERT_EQ((int)conf, (int)CMD_DETECT_BANNER);
    TEST_END();
}

int test_cmd_detect_banner_aruba_cx(void) {
    TEST_BEGIN();
    const char *banner =
        "ArubaOS-CX\r\n"
        "(C) Copyright 2017-2023 Hewlett Packard Enterprise Development LP\r\n"
        "switch#";
    CmdDetectConfidence conf = CMD_DETECT_NONE;
    CmdPlatform p = cmd_detect_platform(banner, strlen(banner), &conf);
    ASSERT_EQ((int)p, (int)CMD_PLATFORM_ARUBA_CX);
    ASSERT_EQ((int)conf, (int)CMD_DETECT_BANNER);
    TEST_END();
}

int test_cmd_detect_banner_aruba_os(void) {
    TEST_BEGIN();
    const char *banner =
        "ArubaOS (MODEL: Aruba7210)\r\n"
        "Version 8.10.0.0\r\n"
        "Website: http://www.arubanetworks.com\r\n"
        "(Aruba7210) *#";
    CmdDetectConfidence conf = CMD_DETECT_NONE;
    CmdPlatform p = cmd_detect_platform(banner, strlen(banner), &conf);
    ASSERT_EQ((int)p, (int)CMD_PLATFORM_ARUBA_OS);
    ASSERT_EQ((int)conf, (int)CMD_DETECT_BANNER);
    TEST_END();
}

int test_cmd_detect_banner_procurve(void) {
    TEST_BEGIN();
    const char *banner =
        "HP J9280A ProCurve Switch 2610-24\r\n"
        "Software revision Y.11.55\r\n"
        "HP2610#";
    CmdDetectConfidence conf = CMD_DETECT_NONE;
    CmdPlatform p = cmd_detect_platform(banner, strlen(banner), &conf);
    ASSERT_EQ((int)p, (int)CMD_PLATFORM_HP_PROCURVE);
    ASSERT_EQ((int)conf, (int)CMD_DETECT_BANNER);
    TEST_END();
}

int test_cmd_detect_banner_comware(void) {
    TEST_BEGIN();
    const char *banner =
        "Comware Software, Version 7.1.070, Release 0723P05\r\n"
        "Copyright (c) 2010-2020 New H3C Technologies Co., Ltd.\r\n"
        "<HPE5130>";
    CmdDetectConfidence conf = CMD_DETECT_NONE;
    CmdPlatform p = cmd_detect_platform(banner, strlen(banner), &conf);
    ASSERT_EQ((int)p, (int)CMD_PLATFORM_HP_COMWARE);
    ASSERT_EQ((int)conf, (int)CMD_DETECT_BANNER);
    TEST_END();
}

int test_cmd_detect_banner_panos(void) {
    TEST_BEGIN();
    const char *banner =
        "PAN-OS 10.1.6\r\n"
        "Palo Alto Networks, Inc.\r\n"
        "admin@PA-VM>";
    CmdDetectConfidence conf = CMD_DETECT_NONE;
    CmdPlatform p = cmd_detect_platform(banner, strlen(banner), &conf);
    ASSERT_EQ((int)p, (int)CMD_PLATFORM_PANOS);
    ASSERT_EQ((int)conf, (int)CMD_DETECT_BANNER);
    TEST_END();
}

int test_cmd_detect_banner_junos(void) {
    TEST_BEGIN();
    const char *banner =
        "Juniper Networks, Inc. srx1500 internet router\r\n"
        "JUNOS Software Release [20.4R3.8]\r\n"
        "admin@srx1500>";
    CmdDetectConfidence conf = CMD_DETECT_NONE;
    CmdPlatform p = cmd_detect_platform(banner, strlen(banner), &conf);
    ASSERT_EQ((int)p, (int)CMD_PLATFORM_JUNOS);
    ASSERT_EQ((int)conf, (int)CMD_DETECT_BANNER);
    TEST_END();
}

int test_cmd_detect_banner_fortios(void) {
    TEST_BEGIN();
    const char *banner =
        "FortiGate-60F v7.0.7,build0426,220301 (GA)\r\n"
        "FortiOS 7.0\r\n"
        "FGT60F # ";
    CmdDetectConfidence conf = CMD_DETECT_NONE;
    CmdPlatform p = cmd_detect_platform(banner, strlen(banner), &conf);
    ASSERT_EQ((int)p, (int)CMD_PLATFORM_FORTIOS);
    ASSERT_EQ((int)conf, (int)CMD_DETECT_BANNER);
    TEST_END();
}

int test_cmd_detect_banner_vyos(void) {
    TEST_BEGIN();
    const char *banner =
        "Welcome to VyOS!\r\n"
        "\r\n"
        "Last login: Mon Sep  1 08:00:00 2026 from 10.0.0.5\r\n"
        "vyos@vyos:~$";
    CmdDetectConfidence conf = CMD_DETECT_NONE;
    CmdPlatform p = cmd_detect_platform(banner, strlen(banner), &conf);
    ASSERT_EQ((int)p, (int)CMD_PLATFORM_VYOS);
    ASSERT_EQ((int)conf, (int)CMD_DETECT_BANNER);
    TEST_END();
}

int test_cmd_detect_banner_routeros(void) {
    TEST_BEGIN();
    const char *banner =
        "MikroTik RouterOS 7.12.1 (c) 1999-2023 http://www.mikrotik.com/\r\n"
        "\r\n"
        "[admin@MikroTik] > ";
    CmdDetectConfidence conf = CMD_DETECT_NONE;
    CmdPlatform p = cmd_detect_platform(banner, strlen(banner), &conf);
    ASSERT_EQ((int)p, (int)CMD_PLATFORM_MIKROTIK);
    ASSERT_EQ((int)conf, (int)CMD_DETECT_BANNER);
    TEST_END();
}

int test_cmd_detect_banner_linux(void) {
    TEST_BEGIN();
    const char *banner =
        "Welcome to Ubuntu 22.04.3 LTS (GNU/Linux 5.15.0-91-generic x86_64)\r\n"
        "\r\n"
        "Last login: Mon Sep  1 08:00:00 2026 from 10.0.0.5\r\n"
        "tom@webhost:~$";
    CmdDetectConfidence conf = CMD_DETECT_NONE;
    CmdPlatform p = cmd_detect_platform(banner, strlen(banner), &conf);
    ASSERT_EQ((int)p, (int)CMD_PLATFORM_LINUX);
    ASSERT_EQ((int)conf, (int)CMD_DETECT_BANNER);
    TEST_END();
}

/* Banner signal wins over a prompt shape present in the same capture, even
 * an ambiguous one -- "Router>" alone would be the ambiguous hostname>
 * shape, but the IOS banner above it resolves the session with confidence
 * to spare. */
int test_cmd_detect_banner_wins_over_ambiguous_prompt(void) {
    TEST_BEGIN();
    const char *text =
        "Cisco IOS Software, C3560 Software (C3560-IPSERVICESK9-M), Version 15.2(4)E\r\n"
        "Router>";
    CmdDetectConfidence conf = CMD_DETECT_NONE;
    CmdPlatform p = cmd_detect_platform(text, strlen(text), &conf);
    ASSERT_EQ((int)p, (int)CMD_PLATFORM_CISCO_IOS);
    ASSERT_EQ((int)conf, (int)CMD_DETECT_BANNER);
    TEST_END();
}

/* --- 3.2 prompt shapes: one case per row, no banner present --- */

int test_cmd_detect_prompt_comware(void) {
    TEST_BEGIN();
    const char *text = "some scrolled output\r\n<HPE5130>";
    CmdDetectConfidence conf = CMD_DETECT_NONE;
    CmdPlatform p = cmd_detect_platform(text, strlen(text), &conf);
    ASSERT_EQ((int)p, (int)CMD_PLATFORM_HP_COMWARE);
    ASSERT_EQ((int)conf, (int)CMD_DETECT_PROMPT);
    TEST_END();
}

int test_cmd_detect_prompt_aruba_os(void) {
    TEST_BEGIN();
    const char *plain = "some scrolled output\r\n(Aruba7210) #";
    const char *pending = "some scrolled output\r\n(Aruba7210) *#";
    CmdDetectConfidence conf = CMD_DETECT_NONE;
    CmdPlatform p = cmd_detect_platform(plain, strlen(plain), &conf);
    ASSERT_EQ((int)p, (int)CMD_PLATFORM_ARUBA_OS);
    ASSERT_EQ((int)conf, (int)CMD_DETECT_PROMPT);

    conf = CMD_DETECT_NONE;
    p = cmd_detect_platform(pending, strlen(pending), &conf);
    ASSERT_EQ((int)p, (int)CMD_PLATFORM_ARUBA_OS);
    ASSERT_EQ((int)conf, (int)CMD_DETECT_PROMPT);
    TEST_END();
}

int test_cmd_detect_prompt_routeros(void) {
    TEST_BEGIN();
    const char *text = "some scrolled output\r\n[admin@MikroTik] > ";
    CmdDetectConfidence conf = CMD_DETECT_NONE;
    CmdPlatform p = cmd_detect_platform(text, strlen(text), &conf);
    ASSERT_EQ((int)p, (int)CMD_PLATFORM_MIKROTIK);
    ASSERT_EQ((int)conf, (int)CMD_DETECT_PROMPT);
    TEST_END();
}

int test_cmd_detect_prompt_linux(void) {
    TEST_BEGIN();
    const char *tilde = "some scrolled output\r\ntom@webhost:~$";
    const char *path = "some scrolled output\r\nroot@webhost:/var/log#";
    CmdDetectConfidence conf = CMD_DETECT_NONE;
    CmdPlatform p = cmd_detect_platform(tilde, strlen(tilde), &conf);
    ASSERT_EQ((int)p, (int)CMD_PLATFORM_LINUX);
    ASSERT_EQ((int)conf, (int)CMD_DETECT_PROMPT);

    conf = CMD_DETECT_NONE;
    p = cmd_detect_platform(path, strlen(path), &conf);
    ASSERT_EQ((int)p, (int)CMD_PLATFORM_LINUX);
    ASSERT_EQ((int)conf, (int)CMD_DETECT_PROMPT);
    TEST_END();
}

/* --- Both ambiguous shapes: deliberately unresolved --- */

int test_cmd_detect_ambiguous_user_at_host(void) {
    TEST_BEGIN();
    /* Shared by Junos and PAN-OS -- must not guess either way. */
    const char *text = "some scrolled output\r\nadmin@fw1>";
    CmdDetectConfidence conf = CMD_DETECT_BANNER; /* poisoned */
    CmdPlatform p = cmd_detect_platform(text, strlen(text), &conf);
    ASSERT_EQ((int)p, (int)CMD_PLATFORM_UNKNOWN);
    ASSERT_EQ((int)conf, (int)CMD_DETECT_NONE);
    TEST_END();
}

int test_cmd_detect_ambiguous_hostname_hash_or_gt(void) {
    TEST_BEGIN();
    /* Shared by IOS, NX-OS, ASA, ProCurve, Aruba CX, FortiOS -- must not
     * guess among them. */
    const char *hash = "some scrolled output\r\nswitch1#";
    const char *gt = "some scrolled output\r\nrouter1>";
    CmdDetectConfidence conf = CMD_DETECT_BANNER; /* poisoned */
    CmdPlatform p = cmd_detect_platform(hash, strlen(hash), &conf);
    ASSERT_EQ((int)p, (int)CMD_PLATFORM_UNKNOWN);
    ASSERT_EQ((int)conf, (int)CMD_DETECT_NONE);

    conf = CMD_DETECT_BANNER;
    p = cmd_detect_platform(gt, strlen(gt), &conf);
    ASSERT_EQ((int)p, (int)CMD_PLATFORM_UNKNOWN);
    ASSERT_EQ((int)conf, (int)CMD_DETECT_NONE);
    TEST_END();
}

/* --- No prompt at all --- */

int test_cmd_detect_no_prompt_at_all(void) {
    TEST_BEGIN();
    const char *text =
        "just some scrolling log output\r\n"
        "with no recognisable banner or prompt shape at all\r\n";
    CmdDetectConfidence conf = CMD_DETECT_BANNER; /* poisoned */
    CmdPlatform p = cmd_detect_platform(text, strlen(text), &conf);
    ASSERT_EQ((int)p, (int)CMD_PLATFORM_UNKNOWN);
    ASSERT_EQ((int)conf, (int)CMD_DETECT_NONE);
    TEST_END();
}

/* Trailing blank lines (e.g. after a `clear` or extra CRLFs at the end of a
 * capture) must not hide the real prompt above them. */
int test_cmd_detect_trailing_blank_lines(void) {
    TEST_BEGIN();
    const char *text = "tom@webhost:~$\r\n\r\n   \r\n";
    CmdDetectConfidence conf = CMD_DETECT_NONE;
    CmdPlatform p = cmd_detect_platform(text, strlen(text), &conf);
    ASSERT_EQ((int)p, (int)CMD_PLATFORM_LINUX);
    ASSERT_EQ((int)conf, (int)CMD_DETECT_PROMPT);
    TEST_END();
}

/* --- Banner split across two chunks (spec 5.2's poll-tick re-scan) --- */

int test_cmd_detect_banner_split_across_chunks(void) {
    TEST_BEGIN();
    const char *full =
        "Cisco IOS Software, C3560 Software (C3560-IPSERVICESK9-M), Version 15.2(4)E\r\n"
        "Copyright (c) 1986-2018 by Cisco Systems, Inc.\r\n"
        "Router>";
    /* First poll tick only got this much -- the distinguishing "IOS
     * Software" text hasn't arrived yet, so this call must not resolve. */
    const char *prefix = "Cisco IOS Softw";
    CmdDetectConfidence conf = CMD_DETECT_BANNER; /* poisoned */
    CmdPlatform p = cmd_detect_platform(prefix, strlen(prefix), &conf);
    ASSERT_EQ((int)p, (int)CMD_PLATFORM_UNKNOWN);
    ASSERT_EQ((int)conf, (int)CMD_DETECT_NONE);

    /* A later poll tick has the fuller text and resolves it. */
    conf = CMD_DETECT_NONE;
    p = cmd_detect_platform(full, strlen(full), &conf);
    ASSERT_EQ((int)p, (int)CMD_PLATFORM_CISCO_IOS);
    ASSERT_EQ((int)conf, (int)CMD_DETECT_BANNER);
    TEST_END();
}

/* --- PowerShell as a custom local shell ---
 * There is no Windows/PowerShell platform, so neither the banner nor the
 * "PS <path>> " prompt may resolve to a device family: the session must stay
 * on CMD_PLATFORM_UNKNOWN (Linux rules plus the network-verb overlay). In
 * particular the prompt ends in '>' with no ':' before a '$'/'#', so it must
 * not be read as Linux, and the banner's words must not trip a signal. */

int test_cmd_detect_powershell_nologo_prompt_unknown(void) {
    TEST_BEGIN();
    const char *text = "PS C:\\Users\\thoma> ";
    CmdDetectConfidence conf = CMD_DETECT_BANNER; /* poisoned */
    CmdPlatform p = cmd_detect_platform(text, strlen(text), &conf);
    ASSERT_EQ((int)p, (int)CMD_PLATFORM_UNKNOWN);
    ASSERT_EQ((int)conf, (int)CMD_DETECT_NONE);
    TEST_END();
}

int test_cmd_detect_windows_powershell_banner_unknown(void) {
    TEST_BEGIN();
    const char *text =
        "Windows PowerShell\r\n"
        "Copyright (C) Microsoft Corporation. All rights reserved.\r\n"
        "\r\n"
        "Install the latest PowerShell for new features and improvements! "
        "https://aka.ms/PSWindows\r\n"
        "\r\n"
        "PS C:\\Users\\thoma> ";
    CmdDetectConfidence conf = CMD_DETECT_BANNER;
    CmdPlatform p = cmd_detect_platform(text, strlen(text), &conf);
    ASSERT_EQ((int)p, (int)CMD_PLATFORM_UNKNOWN);
    ASSERT_EQ((int)conf, (int)CMD_DETECT_NONE);
    TEST_END();
}

int test_cmd_detect_pwsh7_banner_unknown(void) {
    TEST_BEGIN();
    const char *text =
        "PowerShell 7.4.5\r\n"
        "PS C:\\Windows\\System32> ";
    CmdDetectConfidence conf = CMD_DETECT_BANNER;
    CmdPlatform p = cmd_detect_platform(text, strlen(text), &conf);
    ASSERT_EQ((int)p, (int)CMD_PLATFORM_UNKNOWN);
    ASSERT_EQ((int)conf, (int)CMD_DETECT_NONE);
    TEST_END();
}

/* --- Anchoring: vendor words only count in a real banner line, never
 * mid-sentence --- */

/* A Linux motd that happens to mention several vendor product names in
 * ordinary prose must still resolve to Linux: none of those mentions opens
 * a line, so none of them anchors. */
int test_cmd_detect_linux_motd_mentions_vendor_names_stays_linux(void) {
    TEST_BEGIN();
    const char *text =
        "Welcome to Ubuntu 22.04.3 LTS (GNU/Linux 5.15.0-91-generic x86_64)\r\n"
        "\r\n"
        "This jump host also reaches the Cisco IOS Software core switches,\r\n"
        "the PAN-OS firewalls, the Palo Alto Networks management plane and\r\n"
        "the Juniper Networks JUNOS Software edge routers used by the\r\n"
        "network team.\r\n"
        "\r\n"
        "Last login: Mon Sep  1 08:00:00 2026 from 10.0.0.5\r\n"
        "ops@jumphost:~$";
    CmdDetectConfidence conf = CMD_DETECT_NONE;
    CmdPlatform p = cmd_detect_platform(text, strlen(text), &conf);
    ASSERT_EQ((int)p, (int)CMD_PLATFORM_LINUX);
    ASSERT_EQ((int)conf, (int)CMD_DETECT_BANNER);
    TEST_END();
}

/* A vendor word inside a file the user cats, with no banner text at all,
 * must not resolve to that vendor -- the surrounding Linux prompt (before
 * and after) is the only real evidence, so the session stays Linux. */
int test_cmd_detect_vendor_word_in_catted_file_after_linux_prompt_stays_linux(void) {
    TEST_BEGIN();
    const char *text =
        "tom@webhost:~$ cat notes.txt\r\n"
        "Remember to update the Cisco IOS Software image on the core switch\r\n"
        "and check the PAN-OS firewall license before Friday.\r\n"
        "tom@webhost:~$";
    CmdDetectConfidence conf = CMD_DETECT_NONE;
    CmdPlatform p = cmd_detect_platform(text, strlen(text), &conf);
    ASSERT_EQ((int)p, (int)CMD_PLATFORM_LINUX);
    ASSERT_EQ((int)conf, (int)CMD_DETECT_PROMPT);
    TEST_END();
}

/* "Last login:" is no longer a Linux signal at all (removed 2026-09-24:
 * Junos, PAN-OS and Arista print their own "Last login:" line too, so it
 * does not distinguish Linux from them). On its own, with no distro banner
 * and no resolving prompt shape, this must now stay unresolved. */
int test_cmd_detect_last_login_alone_is_no_longer_linux_evidence(void) {
    TEST_BEGIN();
    const char *text =
        "Last login: Mon Sep  1 08:00:00 2026 from 10.0.0.5\r\n"
        "some other scrolled output that is not a prompt";
    CmdDetectConfidence conf = CMD_DETECT_BANNER; /* poisoned */
    CmdPlatform p = cmd_detect_platform(text, strlen(text), &conf);
    ASSERT_EQ((int)p, (int)CMD_PLATFORM_UNKNOWN);
    ASSERT_EQ((int)conf, (int)CMD_DETECT_NONE);
    TEST_END();
}

/* A Junos device's login flow prints its own "Last login:" line, then
 * "--- JUNOS <version> built ..." -- the line "JUNOS " alone does not anchor
 * (it starts with "--- "), so this needs its own needle. */
int test_cmd_detect_junos_after_last_login_banner(void) {
    TEST_BEGIN();
    const char *text =
        "Last login: Mon Sep  1 08:00:00 2026 from 10.0.0.5\r\n"
        "--- JUNOS 21.4R3.15 built 2022-08-19 15:57:59 UTC\r\n"
        "admin@srx1500>";
    CmdDetectConfidence conf = CMD_DETECT_NONE;
    CmdPlatform p = cmd_detect_platform(text, strlen(text), &conf);
    ASSERT_EQ((int)p, (int)CMD_PLATFORM_JUNOS);
    ASSERT_EQ((int)conf, (int)CMD_DETECT_BANNER);
    TEST_END();
}

/* A PAN-OS device's login flow can print an sshd-style "Last login:" line
 * too. With no PAN-OS banner text in this capture and only the ambiguous
 * "user@host>" prompt shape (shared with Junos) on the last line, this must
 * stay CMD_PLATFORM_UNKNOWN -- and, crucially, must NOT resolve to Linux the
 * way it would have while "Last login:" was still a Linux signal. */
int test_cmd_detect_panos_after_last_login_stays_unresolved_not_linux(void) {
    TEST_BEGIN();
    const char *text =
        "Last login: Mon Sep  1 08:00:00 2026 from 10.0.0.5\r\n"
        "admin@fw1>";
    CmdDetectConfidence conf = CMD_DETECT_BANNER; /* poisoned */
    CmdPlatform p = cmd_detect_platform(text, strlen(text), &conf);
    ASSERT_TRUE(p != CMD_PLATFORM_LINUX);
    ASSERT_EQ((int)p, (int)CMD_PLATFORM_UNKNOWN);
    ASSERT_EQ((int)conf, (int)CMD_DETECT_NONE);
    TEST_END();
}

/* --- A banner and a disagreeing last line resolve nothing (H2,
 * 2026-09-24 security review): an anchored banner decides only while the
 * last line's prompt shape is ambiguous, absent, or agrees with it. --- */

/* Hop from a Linux prompt into a switch (real, anchored IOS banner along the
 * way) and back out to a Linux prompt, all within one capture. The banner
 * says IOS, the live last line unambiguously says Linux: the capture is
 * unresolved and flagged CMD_DETECT_CONFLICT -- neither IOS (the banner is
 * history) nor Linux (the banner says the session has been elsewhere). */
int test_cmd_detect_hop_from_linux_to_switch_and_back_is_conflict(void) {
    TEST_BEGIN();
    const char *text =
        "tom@webhost:~$ ssh switch1\r\n"
        "Cisco IOS Software, C3560 Software (C3560-IPSERVICESK9-M), Version 15.2(4)E\r\n"
        "Copyright (c) 1986-2018 by Cisco Systems, Inc.\r\n"
        "switch1#\r\n"
        "switch1#exit\r\n"
        "tom@webhost:~$";
    CmdDetectConfidence conf = CMD_DETECT_NONE;
    CmdPlatform p = cmd_detect_platform(text, strlen(text), &conf);
    ASSERT_EQ((int)p, (int)CMD_PLATFORM_UNKNOWN);
    ASSERT_EQ((int)conf, (int)CMD_DETECT_CONFLICT);
    TEST_END();
}

/* An anchored IOS banner followed by a last line that has the Linux
 * ":"-before-"#" shape ("foo: bar #") is a disagreement too: unresolved,
 * CMD_DETECT_CONFLICT -- and never Linux. */
int test_cmd_detect_ios_banner_then_colon_hash_last_line_never_linux(void) {
    TEST_BEGIN();
    const char *text =
        "Cisco IOS Software, C3560 Software (C3560-IPSERVICESK9-M), Version 15.2(4)E\r\n"
        "Copyright (c) 1986-2018 by Cisco Systems, Inc.\r\n"
        "foo: bar #";
    CmdDetectConfidence conf = CMD_DETECT_NONE;
    CmdPlatform p = cmd_detect_platform(text, strlen(text), &conf);
    ASSERT_TRUE(p != CMD_PLATFORM_LINUX);
    ASSERT_EQ((int)p, (int)CMD_PLATFORM_UNKNOWN);
    ASSERT_EQ((int)conf, (int)CMD_DETECT_CONFLICT);
    TEST_END();
}

/* A file the user cats whose line happens to open with a vendor banner
 * string, under a Linux prompt: the anchored "banner" and the Linux prompt
 * disagree, so this is a conflict -- never IOS. */
int test_cmd_detect_catted_banner_line_under_linux_prompt_is_conflict(void) {
    TEST_BEGIN();
    const char *text =
        "tom@webhost:~$ cat switch1-show-version.txt\r\n"
        "Cisco IOS Software, C3560 Software (C3560-IPSERVICESK9-M), Version 15.2(4)E\r\n"
        "tom@webhost:~$";
    CmdDetectConfidence conf = CMD_DETECT_NONE;
    CmdPlatform p = cmd_detect_platform(text, strlen(text), &conf);
    ASSERT_TRUE(p != CMD_PLATFORM_CISCO_IOS);
    ASSERT_EQ((int)p, (int)CMD_PLATFORM_UNKNOWN);
    ASSERT_EQ((int)conf, (int)CMD_DETECT_CONFLICT);
    TEST_END();
}

/* A banner with an agreeing unambiguous prompt is still a banner result. */
int test_cmd_detect_banner_with_agreeing_prompt_is_banner(void) {
    TEST_BEGIN();
    const char *text =
        "MikroTik RouterOS 7.12.1 (c) 1999-2023 http://www.mikrotik.com/\r\n"
        "[admin@MikroTik] > ";
    CmdDetectConfidence conf = CMD_DETECT_NONE;
    CmdPlatform p = cmd_detect_platform(text, strlen(text), &conf);
    ASSERT_EQ((int)p, (int)CMD_PLATFORM_MIKROTIK);
    ASSERT_EQ((int)conf, (int)CMD_DETECT_BANNER);
    TEST_END();
}

/* VyOS's own default prompt has the Linux shape (it is Debian underneath);
 * that must not read as a banner/prompt conflict -- the banner still wins,
 * confidence BANNER, platform VyOS, not Linux. */
int test_cmd_detect_vyos_banner_and_linux_shaped_prompt_agree(void) {
    TEST_BEGIN();
    const char *text =
        "Welcome to VyOS!\r\n"
        "\r\n"
        "Last login: Mon Sep  1 08:00:00 2026 from 10.0.0.5\r\n"
        "vyos@vyos:~$";
    CmdDetectConfidence conf = CMD_DETECT_NONE;
    CmdPlatform p = cmd_detect_platform(text, strlen(text), &conf);
    ASSERT_EQ((int)p, (int)CMD_PLATFORM_VYOS);
    ASSERT_EQ((int)conf, (int)CMD_DETECT_BANNER);
    TEST_END();
}

/* --- FortiOS's padded-hash prompt shape was removed (item 2): it matched
 * ordinary Linux root prompts too, so both the padded and the tight
 * "hostname#" forms stay ambiguous, with no banner present. --- */

/* The padded "hostname # " form -- no longer claimed for FortiOS. */
int test_cmd_detect_prompt_fortios_padded_hash_now_ambiguous(void) {
    TEST_BEGIN();
    const char *text = "some scrolled output\r\ncore-fw # ";
    CmdDetectConfidence conf = CMD_DETECT_BANNER; /* poisoned */
    CmdPlatform p = cmd_detect_platform(text, strlen(text), &conf);
    ASSERT_EQ((int)p, (int)CMD_PLATFORM_UNKNOWN);
    ASSERT_EQ((int)conf, (int)CMD_DETECT_NONE);
    TEST_END();
}

/* The tight, unpadded "hostname#" shape stays ambiguous, as before. */
int test_cmd_detect_prompt_hostname_hash_tight_stays_ambiguous(void) {
    TEST_BEGIN();
    const char *text = "some scrolled output\r\ncore-fw#";
    CmdDetectConfidence conf = CMD_DETECT_BANNER; /* poisoned */
    CmdPlatform p = cmd_detect_platform(text, strlen(text), &conf);
    ASSERT_EQ((int)p, (int)CMD_PLATFORM_UNKNOWN);
    ASSERT_EQ((int)conf, (int)CMD_DETECT_NONE);
    TEST_END();
}

/* The padded shape's real-world false positives: ordinary Linux root
 * prompts ("/ #" from a minimal/busybox root shell, "host ~ #" from a
 * customised PS1) must never read as FortiOS. */
int test_cmd_detect_linux_root_prompts_not_fortios(void) {
    TEST_BEGIN();
    const char *slash = "some scrolled output\r\n/ #";
    const char *tilde = "some scrolled output\r\nhost ~ #";

    CmdDetectConfidence conf = CMD_DETECT_BANNER; /* poisoned */
    CmdPlatform p = cmd_detect_platform(slash, strlen(slash), &conf);
    ASSERT_TRUE(p != CMD_PLATFORM_FORTIOS);
    ASSERT_EQ((int)p, (int)CMD_PLATFORM_UNKNOWN);
    ASSERT_EQ((int)conf, (int)CMD_DETECT_NONE);

    conf = CMD_DETECT_BANNER;
    p = cmd_detect_platform(tilde, strlen(tilde), &conf);
    ASSERT_TRUE(p != CMD_PLATFORM_FORTIOS);
    ASSERT_EQ((int)p, (int)CMD_PLATFORM_UNKNOWN);
    ASSERT_EQ((int)conf, (int)CMD_DETECT_NONE);
    TEST_END();
}

/* --- Post-resolution: cmd_detect_last_line_contradicts() (item 4) ---
 * Cheap, last-line-only check used after a platform has already resolved.
 * No banner scan -- a banner-looking string elsewhere in the tail is not
 * examined at all, only the last non-empty line's prompt shape. */

int test_cmd_detect_last_line_contradicts_unambiguous_disagreement(void) {
    TEST_BEGIN();
    const char *linux_line = "tom@webhost:~$";
    ASSERT_TRUE(cmd_detect_last_line_contradicts(
        linux_line, strlen(linux_line), CMD_PLATFORM_CISCO_IOS) != 0);
    TEST_END();
}

int test_cmd_detect_last_line_contradicts_agreement_is_not_contradiction(void) {
    TEST_BEGIN();
    const char *linux_line = "tom@webhost:~$";
    ASSERT_TRUE(cmd_detect_last_line_contradicts(
        linux_line, strlen(linux_line), CMD_PLATFORM_LINUX) == 0);
    TEST_END();
}

int test_cmd_detect_last_line_contradicts_ambiguous_shape_is_not_contradiction(void) {
    TEST_BEGIN();
    /* "switch1#" is the ambiguous hostname# shape -- not unambiguous
     * evidence of anything, so this must never read as a contradiction,
     * whichever platform the session already resolved to. */
    const char *ambiguous = "switch1#";
    ASSERT_TRUE(cmd_detect_last_line_contradicts(
        ambiguous, strlen(ambiguous), CMD_PLATFORM_CISCO_IOS) == 0);
    ASSERT_TRUE(cmd_detect_last_line_contradicts(
        ambiguous, strlen(ambiguous), CMD_PLATFORM_LINUX) == 0);
    TEST_END();
}

int test_cmd_detect_last_line_contradicts_vyos_linux_shape_agrees(void) {
    TEST_BEGIN();
    const char *linux_shaped = "vyos@vyos:~$";
    ASSERT_TRUE(cmd_detect_last_line_contradicts(
        linux_shaped, strlen(linux_shaped), CMD_PLATFORM_VYOS) == 0);
    TEST_END();
}

int test_cmd_detect_last_line_contradicts_no_line_is_not_contradiction(void) {
    TEST_BEGIN();
    const char *nothing = "just some scrolled log output\r\n";
    ASSERT_TRUE(cmd_detect_last_line_contradicts(
        nothing, strlen(nothing), CMD_PLATFORM_CISCO_IOS) == 0);
    ASSERT_TRUE(cmd_detect_last_line_contradicts(
        NULL, 0, CMD_PLATFORM_CISCO_IOS) == 0);
    TEST_END();
}

/* --- Session state: cmd_detect_scan_step() / cmd_detect_watch_step() ---
 * The rule (H1/H2, 2026-09-24 security review): host output may move an
 * unresolved session to a platform and may set the sticky contradicted
 * flag; it never replaces a platform once set and never clears the flag. */

/* Linux prompt first, then a vendor-banner-looking line (a cat'ed file):
 * the session stays Linux -- never IOS -- and is contradicted. */
int test_cmd_detect_scan_linux_then_banner_looking_line_not_ios(void) {
    TEST_BEGIN();
    CmdPlatform plat = CMD_PLATFORM_UNKNOWN;
    int contradicted = 0;

    const char *t1 = "tom@webhost:~$";
    ASSERT_EQ((int)cmd_detect_scan_step(t1, strlen(t1), &plat, &contradicted),
              (int)CMD_DETECT_PROMPT);
    ASSERT_EQ((int)plat, (int)CMD_PLATFORM_LINUX);
    ASSERT_EQ(contradicted, 0);

    /* Mid-cat: the banner-looking line is in the capture, the prompt has
     * not come back yet -- detection alone would say IOS BANNER. */
    const char *t2 =
        "tom@webhost:~$ cat notes.txt\r\n"
        "Cisco IOS Software, C3560 Software, Version 15.2(4)E\r\n";
    (void)cmd_detect_scan_step(t2, strlen(t2), &plat, &contradicted);
    ASSERT_EQ((int)plat, (int)CMD_PLATFORM_LINUX);
    ASSERT_EQ(contradicted, 1);
    TEST_END();
}

/* The same, but the Linux prompt is back by the time of the read: the
 * capture itself is a conflict. Still Linux, contradicted. */
int test_cmd_detect_scan_linux_then_catted_banner_conflict(void) {
    TEST_BEGIN();
    CmdPlatform plat = CMD_PLATFORM_LINUX;
    int contradicted = 0;
    const char *t =
        "tom@webhost:~$ cat notes.txt\r\n"
        "Cisco IOS Software, C3560 Software, Version 15.2(4)E\r\n"
        "tom@webhost:~$";
    ASSERT_EQ((int)cmd_detect_scan_step(t, strlen(t), &plat, &contradicted),
              (int)CMD_DETECT_CONFLICT);
    ASSERT_EQ((int)plat, (int)CMD_PLATFORM_LINUX);
    ASSERT_EQ(contradicted, 1);
    TEST_END();
}

/* Unresolved, then a vendor banner: resolves to the vendor, not contradicted. */
int test_cmd_detect_scan_unresolved_then_banner_resolves(void) {
    TEST_BEGIN();
    CmdPlatform plat = CMD_PLATFORM_UNKNOWN;
    int contradicted = 0;

    const char *t0 = "connecting...";
    ASSERT_EQ((int)cmd_detect_scan_step(t0, strlen(t0), &plat, &contradicted),
              (int)CMD_DETECT_NONE);
    ASSERT_EQ((int)plat, (int)CMD_PLATFORM_UNKNOWN);

    const char *t1 =
        "Cisco IOS Software, C3560 Software (C3560-IPSERVICESK9-M), Version 15.2(4)E\r\n"
        "switch1>";
    ASSERT_EQ((int)cmd_detect_scan_step(t1, strlen(t1), &plat, &contradicted),
              (int)CMD_DETECT_BANNER);
    ASSERT_EQ((int)plat, (int)CMD_PLATFORM_CISCO_IOS);
    ASSERT_EQ(contradicted, 0);
    TEST_END();
}

/* A vendor session, then a Linux-looking prompt: stays the vendor and is
 * contradicted -- both in the scan window and after it. */
int test_cmd_detect_vendor_then_linux_prompt_stays_vendor_contradicted(void) {
    TEST_BEGIN();
    const char *linux_line = "tom@webhost:~$";

    CmdPlatform plat = CMD_PLATFORM_JUNOS;
    int contradicted = 0;
    ASSERT_EQ((int)cmd_detect_scan_step(linux_line, strlen(linux_line),
                                        &plat, &contradicted),
              (int)CMD_DETECT_PROMPT);
    ASSERT_EQ((int)plat, (int)CMD_PLATFORM_JUNOS);
    ASSERT_EQ(contradicted, 1);

    contradicted = 0;
    cmd_detect_watch_step(linux_line, strlen(linux_line),
                          CMD_PLATFORM_CISCO_IOS, &contradicted);
    ASSERT_EQ(contradicted, 1);
    TEST_END();
}

/* A different banner in a later scan read never moves a resolved session
 * sideways -- not even banner over prompt. */
int test_cmd_detect_scan_resolved_never_moves_sideways(void) {
    TEST_BEGIN();
    CmdPlatform plat = CMD_PLATFORM_LINUX;
    int contradicted = 0;
    const char *t =
        "Junos OS 21.4R3.15\r\n"
        "admin@srx1500>";
    ASSERT_EQ((int)cmd_detect_scan_step(t, strlen(t), &plat, &contradicted),
              (int)CMD_DETECT_BANNER);
    ASSERT_EQ((int)plat, (int)CMD_PLATFORM_LINUX);
    ASSERT_EQ(contradicted, 1);
    TEST_END();
}

/* The flag is sticky: agreeing reads afterwards never clear it. */
int test_cmd_detect_contradicted_is_sticky(void) {
    TEST_BEGIN();
    CmdPlatform plat = CMD_PLATFORM_CISCO_IOS;
    int contradicted = 0;
    const char *linux_line = "tom@webhost:~$";
    cmd_detect_watch_step(linux_line, strlen(linux_line), plat, &contradicted);
    ASSERT_EQ(contradicted, 1);

    const char *ios = "Cisco IOS Software, Version 15.2(4)E\r\nswitch1#";
    (void)cmd_detect_scan_step(ios, strlen(ios), &plat, &contradicted);
    cmd_detect_watch_step("switch1#", 8, plat, &contradicted);
    ASSERT_EQ((int)plat, (int)CMD_PLATFORM_CISCO_IOS);
    ASSERT_EQ(contradicted, 1);
    TEST_END();
}

/* A conflict seen while still unresolved marks the session, so a later
 * resolution is classified under the worse of the two rulesets. */
int test_cmd_detect_scan_conflict_while_unresolved_marks_session(void) {
    TEST_BEGIN();
    CmdPlatform plat = CMD_PLATFORM_UNKNOWN;
    int contradicted = 0;
    const char *t =
        "Cisco IOS Software, Version 15.2(4)E\r\n"
        "tom@webhost:~$";
    ASSERT_EQ((int)cmd_detect_scan_step(t, strlen(t), &plat, &contradicted),
              (int)CMD_DETECT_CONFLICT);
    ASSERT_EQ((int)plat, (int)CMD_PLATFORM_UNKNOWN);
    ASSERT_EQ(contradicted, 1);

    const char *linux_line = "tom@webhost:~$";
    (void)cmd_detect_scan_step(linux_line, strlen(linux_line), &plat, &contradicted);
    ASSERT_EQ((int)plat, (int)CMD_PLATFORM_LINUX);
    ASSERT_EQ(contradicted, 1);
    TEST_END();
}

/* Agreement is not contradiction: the same platform again, or VyOS's own
 * Linux-shaped prompt; an ambiguous shape is not evidence either. */
int test_cmd_detect_steps_agreement_leaves_session_alone(void) {
    TEST_BEGIN();
    CmdPlatform plat = CMD_PLATFORM_LINUX;
    int contradicted = 0;
    const char *linux_line = "tom@webhost:~$";
    (void)cmd_detect_scan_step(linux_line, strlen(linux_line), &plat, &contradicted);
    cmd_detect_watch_step(linux_line, strlen(linux_line), plat, &contradicted);
    cmd_detect_watch_step("switch1#", 8, plat, &contradicted);
    ASSERT_EQ((int)plat, (int)CMD_PLATFORM_LINUX);
    ASSERT_EQ(contradicted, 0);

    plat = CMD_PLATFORM_VYOS;
    const char *vyos_line = "vyos@vyos:~$";
    (void)cmd_detect_scan_step(vyos_line, strlen(vyos_line), &plat, &contradicted);
    cmd_detect_watch_step(vyos_line, strlen(vyos_line), plat, &contradicted);
    ASSERT_EQ((int)plat, (int)CMD_PLATFORM_VYOS);
    ASSERT_EQ(contradicted, 0);
    TEST_END();
}

/* An unresolved session has nothing to contradict after the window closes,
 * and the watch step never resolves it either. NULL pointers are no-ops. */
int test_cmd_detect_watch_unresolved_and_null_safety(void) {
    TEST_BEGIN();
    int contradicted = 0;
    const char *linux_line = "tom@webhost:~$";
    cmd_detect_watch_step(linux_line, strlen(linux_line),
                          CMD_PLATFORM_UNKNOWN, &contradicted);
    ASSERT_EQ(contradicted, 0);
    cmd_detect_watch_step(NULL, 0, CMD_PLATFORM_LINUX, &contradicted);
    ASSERT_EQ(contradicted, 0);
    cmd_detect_watch_step(linux_line, strlen(linux_line),
                          CMD_PLATFORM_CISCO_IOS, NULL);

    CmdPlatform plat = CMD_PLATFORM_UNKNOWN;
    ASSERT_EQ((int)cmd_detect_scan_step(linux_line, strlen(linux_line), NULL,
                                        &contradicted),
              (int)CMD_DETECT_NONE);
    ASSERT_EQ((int)cmd_detect_scan_step(linux_line, strlen(linux_line), &plat,
                                        NULL),
              (int)CMD_DETECT_NONE);
    ASSERT_EQ((int)plat, (int)CMD_PLATFORM_UNKNOWN);
    TEST_END();
}
