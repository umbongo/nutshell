/* tests/test_cmd_policy.c -- the status line's one policy control, as a model.
 * See docs/superpowers/specs/2026-09-11-status-policy-control-design.md. */
#include "test_framework.h"
#include "cmd_policy.h"
#include <string.h>

/* ---- defaults and clamping -------------------------------------------- */

int test_policy_default_is_read_only_nothing_unattended(void) {
    TEST_BEGIN();
    CmdPolicy p = cmd_policy_default();
    ASSERT_EQ(p.allowed, (int)CMD_READ);
    ASSERT_EQ(p.unattended, POLICY_NONE);
    TEST_END();
}

int test_policy_clamp_normalises_garbage(void) {
    TEST_BEGIN();
    CmdPolicy p;

    /* Ceiling below the scale and above it. */
    p.allowed = -7; p.unattended = POLICY_NONE;
    cmd_policy_clamp(&p);
    ASSERT_EQ(p.allowed, (int)CMD_READ);

    p.allowed = 99; p.unattended = POLICY_NONE;
    cmd_policy_clamp(&p);
    ASSERT_EQ(p.allowed, (int)CMD_CRITICAL);

    /* Unattended below POLICY_NONE. */
    p.allowed = CMD_WRITE; p.unattended = -42;
    cmd_policy_clamp(&p);
    ASSERT_EQ(p.unattended, POLICY_NONE);

    /* The invariant: an inverted pair clamps the unattended marker down,
     * never the ceiling up. */
    p.allowed = CMD_READ; p.unattended = CMD_CRITICAL;
    cmd_policy_clamp(&p);
    ASSERT_EQ(p.allowed, (int)CMD_READ);
    ASSERT_EQ(p.unattended, (int)CMD_READ);

    cmd_policy_clamp(NULL);   /* must not crash */
    TEST_END();
}

/* ---- the setters and the invariant ------------------------------------ */

int test_policy_set_allowed_clamps_and_drags_unattended_down(void) {
    TEST_BEGIN();
    CmdPolicy p = cmd_policy_default();

    cmd_policy_set_allowed(&p, CMD_CRITICAL);
    cmd_policy_set_unattended(&p, CMD_CRITICAL);
    ASSERT_EQ(p.unattended, (int)CMD_CRITICAL);

    /* Lowering the ceiling must take the unattended marker with it. */
    cmd_policy_set_allowed(&p, CMD_UNKNOWN);
    ASSERT_EQ(p.allowed, (int)CMD_UNKNOWN);
    ASSERT_EQ(p.unattended, (int)CMD_UNKNOWN);

    cmd_policy_set_allowed(&p, CMD_READ);
    ASSERT_EQ(p.unattended, (int)CMD_READ);

    /* Out-of-range stops clamp into the scale. */
    cmd_policy_set_allowed(&p, -3);
    ASSERT_EQ(p.allowed, (int)CMD_READ);
    cmd_policy_set_allowed(&p, 17);
    ASSERT_EQ(p.allowed, (int)CMD_CRITICAL);

    cmd_policy_set_allowed(NULL, CMD_WRITE);   /* must not crash */
    TEST_END();
}

int test_policy_set_unattended_never_raises_the_ceiling(void) {
    TEST_BEGIN();
    CmdPolicy p = cmd_policy_default();   /* read / none */

    cmd_policy_set_unattended(&p, CMD_CRITICAL);
    ASSERT_EQ(p.allowed, (int)CMD_READ);      /* ceiling untouched */
    ASSERT_EQ(p.unattended, (int)CMD_READ);   /* request clamped to it */

    cmd_policy_set_allowed(&p, CMD_WRITE);
    cmd_policy_set_unattended(&p, CMD_UNKNOWN);
    ASSERT_EQ(p.unattended, (int)CMD_UNKNOWN);

    cmd_policy_set_unattended(&p, POLICY_NONE);
    ASSERT_EQ(p.unattended, POLICY_NONE);
    ASSERT_EQ(p.allowed, (int)CMD_WRITE);

    cmd_policy_set_unattended(&p, -9);
    ASSERT_EQ(p.unattended, POLICY_NONE);

    cmd_policy_set_unattended(NULL, CMD_READ);  /* must not crash */
    TEST_END();
}

int test_policy_invariant_holds_after_any_sequence(void) {
    TEST_BEGIN();
    /* Every (band, stop) gesture from every reachable policy leaves the
     * invariant intact -- the property the whole control rests on. */
    static const int stops[6] = { -9, POLICY_NONE, 0, 1, 2, 3 };
    for (int a0 = 0; a0 < POLICY_STOP_COUNT; a0++) {
        for (int u0 = POLICY_NONE; u0 <= a0; u0++) {
            for (int i = 0; i < 6; i++) {
                CmdPolicy p; p.allowed = a0; p.unattended = u0;
                cmd_policy_set_allowed(&p, stops[i]);
                if (p.allowed < CMD_READ || p.allowed > CMD_CRITICAL ||
                    p.unattended < POLICY_NONE || p.unattended > p.allowed) {
                    printf("  set_allowed(%d) from {%d,%d} -> {%d,%d}\n",
                           stops[i], a0, u0, p.allowed, p.unattended);
                    _tf_local_fail = 1;
                }
                CmdPolicy q; q.allowed = a0; q.unattended = u0;
                cmd_policy_set_unattended(&q, stops[i]);
                if (q.allowed != a0 || q.unattended < POLICY_NONE ||
                    q.unattended > q.allowed) {
                    printf("  set_unattended(%d) from {%d,%d} -> {%d,%d}\n",
                           stops[i], a0, u0, q.allowed, q.unattended);
                    _tf_local_fail = 1;
                }
            }
        }
    }
    TEST_END();
}

int test_policy_cycles_wrap_through_every_stop(void) {
    TEST_BEGIN();
    CmdPolicy p = cmd_policy_default();

    /* Ceiling: read -> unknown -> write -> critical -> read. */
    cmd_policy_cycle_allowed(&p); ASSERT_EQ(p.allowed, (int)CMD_UNKNOWN);
    cmd_policy_cycle_allowed(&p); ASSERT_EQ(p.allowed, (int)CMD_WRITE);
    cmd_policy_cycle_allowed(&p); ASSERT_EQ(p.allowed, (int)CMD_CRITICAL);
    cmd_policy_cycle_allowed(&p); ASSERT_EQ(p.allowed, (int)CMD_READ);

    /* Unattended: none -> read -> ... -> allowed -> none. The cycle stops
     * at the ceiling, so at ceiling read it is a two-state toggle. */
    ASSERT_EQ(p.unattended, POLICY_NONE);
    cmd_policy_cycle_unattended(&p); ASSERT_EQ(p.unattended, (int)CMD_READ);
    cmd_policy_cycle_unattended(&p); ASSERT_EQ(p.unattended, POLICY_NONE);

    cmd_policy_set_allowed(&p, CMD_WRITE);
    cmd_policy_cycle_unattended(&p); ASSERT_EQ(p.unattended, (int)CMD_READ);
    cmd_policy_cycle_unattended(&p); ASSERT_EQ(p.unattended, (int)CMD_UNKNOWN);
    cmd_policy_cycle_unattended(&p); ASSERT_EQ(p.unattended, (int)CMD_WRITE);
    cmd_policy_cycle_unattended(&p); ASSERT_EQ(p.unattended, POLICY_NONE);

    cmd_policy_cycle_allowed(NULL);      /* must not crash */
    cmd_policy_cycle_unattended(NULL);
    TEST_END();
}

/* ---- the two decisions ------------------------------------------------- */

int test_policy_blocks_above_the_ceiling(void) {
    TEST_BEGIN();
    /* expected[allowed][worst] -- 1 = blocked */
    static const int expected[4][4] = {
        /* worst:     READ UNK WRITE CRIT */
        /* read     */ { 0, 1, 1, 1 },
        /* unknown  */ { 0, 0, 1, 1 },
        /* write    */ { 0, 0, 0, 1 },
        /* critical */ { 0, 0, 0, 0 }
    };
    for (int a = 0; a < POLICY_STOP_COUNT; a++) {
        for (int w = 0; w < POLICY_STOP_COUNT; w++) {
            CmdPolicy p; p.allowed = a; p.unattended = POLICY_NONE;
            int got = cmd_policy_blocks(p, (CmdSafetyLevel)w);
            if (got != expected[a][w]) {
                printf("  allowed=%d worst=%d: expected %d got %d\n",
                       a, w, expected[a][w], got);
                _tf_local_fail = 1;
            }
        }
    }
    TEST_END();
}

int test_policy_unattended_mask_is_always_a_prefix_set(void) {
    TEST_BEGIN();
    CmdPolicy p;
    p.allowed = CMD_CRITICAL;

    p.unattended = POLICY_NONE;
    ASSERT_EQ((int)cmd_policy_unattended_mask(p), 0);
    p.unattended = CMD_READ;
    ASSERT_EQ((int)cmd_policy_unattended_mask(p), (int)CMD_MASK_OF(CMD_READ));
    p.unattended = CMD_UNKNOWN;
    ASSERT_EQ((int)cmd_policy_unattended_mask(p),
              (int)(CMD_MASK_OF(CMD_READ) | CMD_MASK_OF(CMD_UNKNOWN)));
    p.unattended = CMD_WRITE;
    ASSERT_EQ((int)cmd_policy_unattended_mask(p),
              (int)(CMD_MASK_OF(CMD_READ) | CMD_MASK_OF(CMD_UNKNOWN) |
                    CMD_MASK_OF(CMD_WRITE)));
    p.unattended = CMD_CRITICAL;
    ASSERT_EQ((int)cmd_policy_unattended_mask(p),
              (int)(CMD_MASK_OF(CMD_READ) | CMD_MASK_OF(CMD_UNKNOWN) |
                    CMD_MASK_OF(CMD_WRITE) | CMD_MASK_OF(CMD_CRITICAL)));

    /* An out-of-range marker is clamped, never extrapolated. */
    p.unattended = 99;
    ASSERT_EQ((int)cmd_policy_unattended_mask(p),
              (int)(CMD_MASK_OF(CMD_READ) | CMD_MASK_OF(CMD_UNKNOWN) |
                    CMD_MASK_OF(CMD_WRITE) | CMD_MASK_OF(CMD_CRITICAL)));
    TEST_END();
}

/* The spec's section 3 corollary: because every reachable unattended set is
 * downward-closed, testing the whole segment mask and testing only the worst
 * segment agree -- for all 16 masks and all 5 marker positions. The gate is
 * still written as a set test; this pins the equivalence rather than letting
 * the code assume it. */
int test_policy_mask_gate_equals_worst_segment_gate(void) {
    TEST_BEGIN();
    for (unsigned mask = 0u; mask < 16u; mask++) {
        int worst = -1;
        for (int c = 0; c < POLICY_STOP_COUNT; c++)
            if (mask & CMD_MASK_OF(c)) worst = c;

        for (int u = POLICY_NONE; u < POLICY_STOP_COUNT; u++) {
            CmdPolicy p; p.allowed = CMD_CRITICAL; p.unattended = u;
            int by_mask = cmd_policy_runs_unattended(p, mask);
            int by_worst = (worst >= 0 && worst <= u) ? 1 : 0;
            if (by_mask != by_worst) {
                printf("  mask=0x%X unattended=%d: set test %d, worst test %d\n",
                       mask, u, by_mask, by_worst);
                _tf_local_fail = 1;
            }
        }
    }
    TEST_END();
}

int test_policy_empty_mask_never_runs_unattended(void) {
    TEST_BEGIN();
    CmdPolicy p; p.allowed = CMD_CRITICAL; p.unattended = CMD_CRITICAL;
    ASSERT_EQ(cmd_policy_runs_unattended(p, 0u), 0);
    TEST_END();
}

/* The documented loss (spec section 3): the old "safe + write" mode -- run
 * writes unattended while holding unrecognised commands back -- skips a stop,
 * so no position of the scale reproduces it. If a future change makes it
 * reachable, that is a design change and this test should be the thing that
 * says so. */
int test_policy_cannot_express_read_plus_write_unattended(void) {
    TEST_BEGIN();
    unsigned lost = CMD_MASK_OF(CMD_READ) | CMD_MASK_OF(CMD_WRITE);
    for (int a = 0; a < POLICY_STOP_COUNT; a++) {
        for (int u = POLICY_NONE; u <= a; u++) {
            CmdPolicy p; p.allowed = a; p.unattended = u;
            if (cmd_policy_unattended_mask(p) == lost) {
                printf("  {allowed=%d, unattended=%d} reproduces {READ,WRITE}\n", a, u);
                _tf_local_fail = 1;
            }
            /* And the consequence that matters: a {UNKNOWN, WRITE} pipeline
             * never runs unattended unless UNKNOWN is itself unattended. */
            unsigned mixed = CMD_MASK_OF(CMD_UNKNOWN) | CMD_MASK_OF(CMD_WRITE);
            if (cmd_policy_runs_unattended(p, mixed) && u < CMD_WRITE) {
                printf("  {allowed=%d, unattended=%d} auto-ran a mixed pipeline\n", a, u);
                _tf_local_fail = 1;
            }
        }
    }
    TEST_END();
}

/* ---- tokens ------------------------------------------------------------ */

int test_policy_stop_names_and_labels(void) {
    TEST_BEGIN();
    ASSERT_STR_EQ(cmd_policy_stop_name(POLICY_NONE), "none");
    ASSERT_STR_EQ(cmd_policy_stop_name(CMD_READ), "read");
    ASSERT_STR_EQ(cmd_policy_stop_name(CMD_UNKNOWN), "unknown");
    ASSERT_STR_EQ(cmd_policy_stop_name(CMD_WRITE), "write");
    ASSERT_STR_EQ(cmd_policy_stop_name(CMD_CRITICAL), "critical");
    ASSERT_STR_EQ(cmd_policy_stop_name(-5), "none");
    ASSERT_STR_EQ(cmd_policy_stop_name(4), "none");

    ASSERT_STR_EQ(cmd_policy_stop_label(POLICY_NONE), "Nothing");
    ASSERT_STR_EQ(cmd_policy_stop_label(CMD_READ), "Read");
    ASSERT_STR_EQ(cmd_policy_stop_label(CMD_UNKNOWN), "Unknown");
    ASSERT_STR_EQ(cmd_policy_stop_label(CMD_WRITE), "Write");
    ASSERT_STR_EQ(cmd_policy_stop_label(CMD_CRITICAL), "Critical");
    ASSERT_STR_EQ(cmd_policy_stop_label(99), "Nothing");

    int stop = 42;
    ASSERT_EQ(cmd_policy_stop_from_name("critical", &stop), 1);
    ASSERT_EQ(stop, (int)CMD_CRITICAL);
    ASSERT_EQ(cmd_policy_stop_from_name("none", &stop), 1);
    ASSERT_EQ(stop, POLICY_NONE);
    ASSERT_EQ(cmd_policy_stop_from_name("safe", &stop), 0);   /* the old token */
    ASSERT_EQ(stop, POLICY_NONE);                             /* left untouched */
    ASSERT_EQ(cmd_policy_stop_from_name("", &stop), 0);
    ASSERT_EQ(cmd_policy_stop_from_name(NULL, &stop), 0);
    ASSERT_EQ(cmd_policy_stop_from_name("read", NULL), 1);
    TEST_END();
}

int test_policy_token_round_trips_every_legal_pair(void) {
    TEST_BEGIN();
    int pairs = 0;
    for (int a = 0; a < POLICY_STOP_COUNT; a++) {
        for (int u = POLICY_NONE; u <= a; u++) {
            CmdPolicy p; p.allowed = a; p.unattended = u;
            char tok[32];
            int n = cmd_policy_to_token(p, tok, sizeof(tok));
            if (n <= 0) { printf("  empty token for {%d,%d}\n", a, u); _tf_local_fail = 1; continue; }

            CmdPolicy back;
            if (!cmd_policy_from_token(tok, &back) ||
                back.allowed != a || back.unattended != u) {
                printf("  \"%s\" -> {%d,%d}, expected {%d,%d}\n",
                       tok, back.allowed, back.unattended, a, u);
                _tf_local_fail = 1;
            }
            pairs++;
        }
    }
    ASSERT_EQ(pairs, 14);   /* 2+3+4+5 legal (allowed, unattended) pairs */
    TEST_END();
}

int test_policy_token_rejects_and_clamps_bad_input(void) {
    TEST_BEGIN();
    CmdPolicy p;

    /* Inverted pair parses, but clamps down to the ceiling. */
    ASSERT_EQ(cmd_policy_from_token("write/critical", &p), 1);
    ASSERT_EQ(p.allowed, (int)CMD_WRITE);
    ASSERT_EQ(p.unattended, (int)CMD_WRITE);

    /* Garbage of every shape falls back to the default, never to something
     * more permissive. */
    static const char *const bad[] = {
        "", "read", "/read", "read/", "nonsense/read", "read/nonsense",
        "none/none",                       /* the ceiling has no "none" stop */
        "read/read/read", "safe+write", "critical",
        "averyverylongtokenindeed/read"
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        p.allowed = CMD_CRITICAL; p.unattended = CMD_CRITICAL;
        int ok = cmd_policy_from_token(bad[i], &p);
        if (ok != 0 || p.allowed != CMD_READ || p.unattended != POLICY_NONE) {
            printf("  \"%s\" -> ok=%d {%d,%d}\n", bad[i], ok, p.allowed, p.unattended);
            _tf_local_fail = 1;
        }
    }

    ASSERT_EQ(cmd_policy_from_token(NULL, &p), 0);
    ASSERT_EQ(p.allowed, (int)CMD_READ);
    ASSERT_EQ(cmd_policy_from_token("write/read", NULL), 1);   /* must not crash */

    /* A buffer too small truncates rather than overflowing. */
    char tiny[4];
    CmdPolicy q; q.allowed = CMD_CRITICAL; q.unattended = CMD_CRITICAL;
    int n = cmd_policy_to_token(q, tiny, sizeof(tiny));
    ASSERT_TRUE(n < (int)sizeof(tiny));
    ASSERT_EQ(cmd_policy_to_token(q, NULL, 8), 0);
    ASSERT_EQ(cmd_policy_to_token(q, tiny, 0), 0);
    TEST_END();
}

/* ---- wording ----------------------------------------------------------- */

int test_policy_caption_reads_as_a_sentence(void) {
    TEST_BEGIN();
    char buf[96];
    CmdPolicy p = cmd_policy_default();
    cmd_policy_caption(p, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "allowed to Read, nothing runs unattended");

    cmd_policy_set_allowed(&p, CMD_WRITE);
    cmd_policy_set_unattended(&p, CMD_READ);
    cmd_policy_caption(p, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "allowed to Write, Read runs unattended");

    cmd_policy_set_allowed(&p, CMD_CRITICAL);
    cmd_policy_set_unattended(&p, CMD_CRITICAL);
    cmd_policy_caption(p, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "allowed to Critical, Critical runs unattended");

    ASSERT_EQ(cmd_policy_caption(p, NULL, 16), 0);
    ASSERT_EQ(cmd_policy_caption(p, buf, 0), 0);
    TEST_END();
}

int test_policy_tip_covers_raise_lower_toggle_and_refusal(void) {
    TEST_BEGIN();
    char buf[96];
    CmdPolicy p; p.allowed = CMD_WRITE; p.unattended = CMD_READ;

    /* Ceiling band: above it raises, below it lowers, on it restates. */
    cmd_policy_tip(p, POLICY_BAND_ALLOWED, CMD_CRITICAL, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "Allow Critical commands - they still ask first.");
    cmd_policy_tip(p, POLICY_BAND_ALLOWED, CMD_READ, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "Block anything above Read.");
    cmd_policy_tip(p, POLICY_BAND_ALLOWED, CMD_WRITE, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "Commands above Write are blocked.");

    /* Unattended band: the toggle, a move, and the refusal above the ceiling. */
    cmd_policy_tip(p, POLICY_BAND_UNATTENDED, CMD_READ, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "Stop running anything unattended.");
    cmd_policy_tip(p, POLICY_BAND_UNATTENDED, CMD_WRITE, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "Run Write and below without asking.");
    cmd_policy_tip(p, POLICY_BAND_UNATTENDED, CMD_CRITICAL, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "Allow Critical first to run it unattended.");

    /* Out-of-range stops produce nothing rather than a wrong sentence. */
    ASSERT_EQ(cmd_policy_tip(p, POLICY_BAND_ALLOWED, POLICY_NONE, buf, sizeof(buf)), 0);
    ASSERT_STR_EQ(buf, "");
    ASSERT_EQ(cmd_policy_tip(p, POLICY_BAND_ALLOWED, 4, buf, sizeof(buf)), 0);
    ASSERT_EQ(cmd_policy_tip(p, POLICY_BAND_ALLOWED, CMD_READ, NULL, 16), 0);
    ASSERT_EQ(cmd_policy_tip(p, POLICY_BAND_ALLOWED, CMD_READ, buf, 0), 0);
    TEST_END();
}
