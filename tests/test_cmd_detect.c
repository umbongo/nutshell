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
