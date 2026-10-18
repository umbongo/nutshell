# Auto-approve Button on the AI Panel's Policy Row — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (- [ ]) syntax for tracking.

**Goal:** Replace the policy pill's hidden unattended rail with an explicit Auto-approve button: one level plus an on/off switch, compact labels on a narrow row, per-element tooltips, a confirmation before Critical runs unattended, and no all-approved batch built for a hidden tab.

**Architecture:** `CmdPolicy {allowed, unattended}` keeps its shape. `cmd_policy_clamp()` now enforces "`unattended` is `POLICY_NONE` or equal to `allowed`", and the UI changes the policy only through `cmd_policy_set_allowed()` and the new `cmd_policy_set_auto()`. All geometry (five shrink tiers, button rect, pill clip, busy-label rect, hit-test) and all wording (labels, tooltips, confirmation text) live in `src/core` and are tested natively. `src/ui` only measures text, paints and wires messages.

**Tech Stack:** C11, MinGW-w64 cross-compile (`x86_64-w64-mingw32-gcc`) for the exe, native `gcc` for `make test`, custom `tests/test_framework.h`, Win32 GDI/GDI+ and comctl32 tooltips, PowerShell integration harness (`tests/integration`).

**Spec:** `docs/superpowers/specs/2026-10-18-ai-auto-approve-button-design.md`

## Global Constraints

- **Version bump:** before any build that produces a binary, raise the patch in `src/ui/resource.h` (`APP_VERSION` and `APP_VERSION_BINARY`) and README's `**Version**:` line. After `1.0.99` comes `1.1.0` (`1,1,0,0`). This plan builds the exe once, in Task 18. Main is at `1.2.36` while this plan is written, so that task goes to `1.2.37`, or to one patch above `main` if `main` has moved.
- **Build and test:** `make clean && make release` (on the MSYS2 host: `mingw32-make clean && mingw32-make release`), then `make test` after every change. The native baseline on this branch is 2,957 tests with 0 failed.
- **Warnings:** code must compile clean under `-Werror -Wpedantic -Wshadow -Wformat=2 -Wconversion` with both gcc and MinGW. On a Linux box without MinGW, check each touched `src/core`/`src/config` file with the strict native compile: `for f in <files>; do gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow -Wformat=2 -Wconversion -D_POSIX_C_SOURCE=200809L -Isrc -Isrc/core -Isrc/config -Isrc/crypto -Isrc/term -Isrc/ssh -Isrc/ui -c "$f" -o /dev/null || echo "FAIL $f"; done` (prints nothing on success).
- **Design-system gate** (`tests/test_ui_tokens.c`, exact allow-list): no `RGB(` in `src/ui` outside `ns_draw.c`, no `MulDiv(x, dpi, 96)`, no `CreateFont`. Colours come from `ns_tokens()` surfaces (`.base/.hover/.label`), `text_main`, `text_dim`, `text_disabled` and `border`; use `rgb_alpha()` for blends. Geometry goes through `ns_scale(px, dpi)` and the `SP_*`/`SZ_*` grid, radii through `ns_type_pill()`, strokes through `STROKE_*`, fonts through `ns_font(FONT_CAPTION, dpi)`, and hover through `ns_hover`.
- **Testable logic in `src/core`:** `src/ui` is in `NON_TEST_SRCS` and the native suite never compiles it.
- **`src/ui` tasks on Linux:** there is no MinGW here. Where a MinGW toolchain exists, the implementer compile-checks each touched `src/ui` file as an object only (`make src/ui/<file>.o`, or `mingw32-make src/ui/<file>.o` on the MSYS2 host), which builds no binary and so needs no version bump. The full `make clean && make release` runs once, in Task 18, after the single bump. Without MinGW, the implementer relies on `make test` and a careful reading against the code quoted in the task. The maintainer builds the exe before the PR leaves draft. **The MinGW build is expected to break from Task 4 until Task 9:** Tasks 4 and 5 change `ns_policy_layout()`/`ai_status_layout()`, whose only callers are in `src/ui`. The native suite stays green at the end of every task.
- **Line numbers** are those of the plan's base commit (`6865cd8`). Earlier tasks shift them, so find each block by the code or text quoted with it.
- **Line endings:** `tests/runner.c` is CRLF in the index; every other file in this plan is LF. After editing `tests/runner.c`, run `sed -i 's/\r*$/\r/' tests/runner.c`.
- **Staging:** stage files by path (`git add <path> ...`), then read every `??` line of `git status --short` before committing. `git add -u` skips new files.
- **Commit messages** (CLAUDE.md "Git commits"): an imperative subject ending in the version this work ships in, `(v1.2.37)`, and a body written as if Thomas wrote it. Before the trailer, a `Models:` line names every model that touched the change and what it did. Close with the `Co-Authored-By:` trailer your tooling adds. The commands below assume `claude-sonnet-5` implements; substitute your own model id and trailer if you are a different model.
- **No release on a policy change** (spec section 6). `apply_policy_change()` keeps its unblock/re-block passes and gains nothing that approves waiting rows.

## Review Focus

The five input classes or failure modes the spec implies but no task's tests would otherwise exercise, most likely to bite a user first. Each test below has been added to its owning task.

1. **A display at 125 % or 150 % scaling.** Rounding in `ns_scale()` moves every tier threshold, but the spec tests only one width per threshold. **Test:** `test_ai_status_layout_thresholds_120_and_144` (Task 5) checks that each of the four thresholds flips the tier exactly at 120 and 144 DPI.
2. **No context meter.** With no key or an unknown model, `context_limit == 0`, so `meter_text_w` is 0 and tiers 1 and 2 cannot exist. The busy label must still get a rect that ends before the button. **Test:** `test_ai_status_layout_no_meter_goes_straight_to_tier3` (Task 5).
3. **A panel narrower than the button itself.** The dock minimum is 10 %, and a small window makes the row narrower than the button. The button then sits partly left of the row, the clip falls left of the pill, and nothing may overlap or take a click. **Tests:** `test_ai_status_layout_width_sweep_invariants` (Task 5) sweeps every width from 0 to 1400 at 96 and 192 DPI. `test_ns_policy_layout_clip_left_of_pill_hits_nothing` (Task 4).
4. **The longest tooltip overflowing the panel's tooltip buffer.** The longest is the compact Unknown tip with the Auto-approve suffix, about 250 bytes. A truncated tip would end mid-sentence. **Test:** `test_policy_tips_fit_the_tooltip_buffer` (Task 3) checks that every cell and button tip, in both label sets and both button states, fits `CMD_POLICY_TIP_MAX` and ends with a full stop.
5. **A second toggle while the Critical box is up.** A double click, an accelerator or a posted id can arrive inside the box's modal loop and must be a no-op. **Test:** the gate-tier harness case `ai_auto_approve_critical_asks_first` (Task 16) posts `4015` a second time while the box is up. It asserts that exactly one box exists and that the policy is unchanged after No.

---

### Task 1: The invariant and the setters in `cmd_policy`

**Files**
- Modify: `src/core/cmd_policy.h` (prototype comments at lines 47-64, new prototypes after line 64)
- Modify: `src/core/cmd_policy.c` (lines 14-77 `stop_in_range` through `cmd_policy_blocks`; `cmd_policy_caption` lines 156-170)
- Test: `tests/test_cmd_policy.c` (replace `test_policy_token_round_trips_every_legal_pair` lines 301-323; append new tests)
- Test: `tests/test_cmd_batch.c` (`test_cmd_batch_add_copies_defaults` lines 48-64)
- Test: `tests/test_config.c` (lines 1050-1062, 1066-1089, 1139-1171, 1173-1196, 1276-1296)
- Test: `tests/runner.c` (declarations after line 2321, calls after line 5464)

**Interfaces**
- Consumes: none.
- Produces:
  - `void cmd_policy_clamp(CmdPolicy *p);`: ranges clamped, then a split rounds down to `POLICY_NONE`.
  - `void cmd_policy_set_allowed(CmdPolicy *p, int stop);`: normalises first; the button follows the level when on.
  - `void cmd_policy_set_unattended(CmdPolicy *p, int stop);`: tests only; range clamp, keeps splits.
  - `void cmd_policy_set_auto(CmdPolicy *p, int on);`
  - `int cmd_policy_auto_on(CmdPolicy p);`
  - `int cmd_policy_needs_confirm(CmdPolicy cur, CmdPolicy next);`
  - `unsigned cmd_policy_status_word(CmdPolicy p);`: bits 0-7 the level, `0x100` the button.

- [ ] **Step 1: Write the failing tests.** Append to `tests/test_cmd_policy.c`:

```c
/* ---- 2026-10-18: one level plus an Auto-approve switch ----------------- */
/* docs/superpowers/specs/2026-10-18-ai-auto-approve-button-design.md s2:
 * `unattended` is POLICY_NONE or equal to `allowed`. */

int test_policy_clamp_rounds_a_split_down(void) {
    TEST_BEGIN();
    for (int a = 0; a < POLICY_STOP_COUNT; a++) {
        for (int u = POLICY_NONE; u <= a; u++) {
            CmdPolicy p; p.allowed = a; p.unattended = u;
            cmd_policy_clamp(&p);
            int want = (u == POLICY_NONE || u == a) ? u : POLICY_NONE;
            if (p.allowed != a || p.unattended != want) {
                printf("  clamp {%d,%d} -> {%d,%d}, expected {%d,%d}\n",
                       a, u, p.allowed, p.unattended, a, want);
                _tf_local_fail = 1;
            }
        }
    }
    /* from_token goes through clamp, so a split token loads with the
     * button off -- and still parses. */
    CmdPolicy t;
    ASSERT_EQ(cmd_policy_from_token("write/read", &t), 1);
    ASSERT_EQ(t.allowed, (int)CMD_WRITE);
    ASSERT_EQ(t.unattended, POLICY_NONE);
    TEST_END();
}

int test_policy_set_allowed_moves_both_markers_with_auto_on(void) {
    TEST_BEGIN();
    CmdPolicy p = cmd_policy_default();
    cmd_policy_set_auto(&p, 1);                 /* {read, read} */
    cmd_policy_set_allowed(&p, CMD_WRITE);
    ASSERT_EQ(p.allowed, (int)CMD_WRITE);
    ASSERT_EQ(p.unattended, (int)CMD_WRITE);
    cmd_policy_set_allowed(&p, CMD_UNKNOWN);
    ASSERT_EQ(p.allowed, (int)CMD_UNKNOWN);
    ASSERT_EQ(p.unattended, (int)CMD_UNKNOWN);
    cmd_policy_set_allowed(&p, 17);             /* clamps, button follows */
    ASSERT_EQ(p.allowed, (int)CMD_CRITICAL);
    ASSERT_EQ(p.unattended, (int)CMD_CRITICAL);
    cmd_policy_set_allowed(&p, -3);
    ASSERT_EQ(p.allowed, (int)CMD_READ);
    ASSERT_EQ(p.unattended, (int)CMD_READ);
    TEST_END();
}

int test_policy_set_allowed_leaves_auto_off(void) {
    TEST_BEGIN();
    CmdPolicy p = cmd_policy_default();
    cmd_policy_set_allowed(&p, CMD_CRITICAL);
    ASSERT_EQ(p.allowed, (int)CMD_CRITICAL);
    ASSERT_EQ(p.unattended, POLICY_NONE);
    cmd_policy_set_allowed(&p, CMD_UNKNOWN);
    ASSERT_EQ(p.unattended, POLICY_NONE);
    TEST_END();
}

int test_policy_set_allowed_on_a_split_turns_auto_off(void) {
    TEST_BEGIN();
    /* A split that bypassed clamp (only tests can make one) is normalised
     * first, so raising the level never raises the unattended marker. */
    CmdPolicy p; p.allowed = CMD_WRITE; p.unattended = CMD_READ;
    cmd_policy_set_allowed(&p, CMD_CRITICAL);
    ASSERT_EQ(p.allowed, (int)CMD_CRITICAL);
    ASSERT_EQ(p.unattended, POLICY_NONE);
    TEST_END();
}

int test_policy_set_auto_on_and_off(void) {
    TEST_BEGIN();
    CmdPolicy p = cmd_policy_default();
    cmd_policy_set_auto(&p, 1);
    ASSERT_EQ(p.allowed, (int)CMD_READ);
    ASSERT_EQ(p.unattended, (int)CMD_READ);
    ASSERT_EQ(cmd_policy_auto_on(p), 1);
    cmd_policy_set_allowed(&p, CMD_WRITE);
    cmd_policy_set_auto(&p, 0);
    ASSERT_EQ(p.allowed, (int)CMD_WRITE);
    ASSERT_EQ(p.unattended, POLICY_NONE);
    ASSERT_EQ(cmd_policy_auto_on(p), 0);
    /* Garbage is clamped before the switch is applied. */
    p.allowed = 99; p.unattended = -7;
    cmd_policy_set_auto(&p, 1);
    ASSERT_EQ(p.allowed, (int)CMD_CRITICAL);
    ASSERT_EQ(p.unattended, (int)CMD_CRITICAL);
    cmd_policy_set_auto(NULL, 1);               /* must not crash */
    TEST_END();
}

int test_policy_auto_on_reads_a_split_as_off(void) {
    TEST_BEGIN();
    CmdPolicy p;
    p.allowed = CMD_WRITE; p.unattended = CMD_READ;
    ASSERT_EQ(cmd_policy_auto_on(p), 0);
    p.allowed = CMD_WRITE; p.unattended = CMD_WRITE;
    ASSERT_EQ(cmd_policy_auto_on(p), 1);
    p.allowed = CMD_READ; p.unattended = POLICY_NONE;
    ASSERT_EQ(cmd_policy_auto_on(p), 0);
    TEST_END();
}

int test_policy_no_sequence_of_set_allowed_and_set_auto_splits(void) {
    TEST_BEGIN();
    /* Six gestures -- set_allowed(0..3), set_auto(0), set_auto(1) -- in
     * every sequence of four, from each of the eight legal states. */
    int checked = 0;
    for (int a0 = 0; a0 < POLICY_STOP_COUNT; a0++) {
        for (int on0 = 0; on0 < 2; on0++) {
            for (int seq = 0; seq < 6 * 6 * 6 * 6; seq++) {
                CmdPolicy p;
                p.allowed = a0;
                p.unattended = on0 ? a0 : POLICY_NONE;
                int s = seq;
                for (int step = 0; step < 4; step++) {
                    int g = s % 6;
                    s /= 6;
                    if (g < 4) cmd_policy_set_allowed(&p, g);
                    else       cmd_policy_set_auto(&p, g - 4);
                    if (p.unattended != POLICY_NONE && p.unattended != p.allowed) {
                        printf("  start {%d,%d} seq %d step %d -> split {%d,%d}\n",
                               a0, on0, seq, step, p.allowed, p.unattended);
                        _tf_local_fail = 1;
                    }
                }
                checked++;
            }
        }
    }
    ASSERT_EQ(checked, 4 * 2 * 1296);
    TEST_END();
}

int test_policy_needs_confirm_only_when_entering_critical_auto(void) {
    TEST_BEGIN();
    CmdPolicy off_w;  off_w.allowed = CMD_WRITE;    off_w.unattended = POLICY_NONE;
    CmdPolicy on_w;   on_w.allowed  = CMD_WRITE;    on_w.unattended  = CMD_WRITE;
    CmdPolicy off_c;  off_c.allowed = CMD_CRITICAL; off_c.unattended = POLICY_NONE;
    CmdPolicy on_c;   on_c.allowed  = CMD_CRITICAL; on_c.unattended  = CMD_CRITICAL;
    ASSERT_EQ(cmd_policy_needs_confirm(off_c, on_c), 1);  /* button on at Critical */
    ASSERT_EQ(cmd_policy_needs_confirm(on_w, on_c), 1);   /* Critical with button on */
    ASSERT_EQ(cmd_policy_needs_confirm(on_c, on_c), 0);   /* already there */
    ASSERT_EQ(cmd_policy_needs_confirm(off_w, off_c), 0); /* Critical, button off */
    ASSERT_EQ(cmd_policy_needs_confirm(off_w, on_w), 0);  /* button on at Write */
    ASSERT_EQ(cmd_policy_needs_confirm(on_c, off_c), 0);  /* turning it off */
    ASSERT_EQ(cmd_policy_needs_confirm(cmd_policy_default(), on_c), 1);
    TEST_END();
}

int test_policy_status_word_packs_level_and_button(void) {
    TEST_BEGIN();
    CmdPolicy p;
    p.allowed = CMD_WRITE; p.unattended = POLICY_NONE;
    ASSERT_EQ((int)cmd_policy_status_word(p), 0x002);
    p.allowed = CMD_WRITE; p.unattended = CMD_WRITE;
    ASSERT_EQ((int)cmd_policy_status_word(p), 0x102);
    p.allowed = CMD_CRITICAL; p.unattended = CMD_CRITICAL;
    ASSERT_EQ((int)cmd_policy_status_word(p), 0x103);
    p.allowed = CMD_WRITE; p.unattended = CMD_READ;   /* a split reads as off */
    ASSERT_EQ((int)cmd_policy_status_word(p), 0x002);
    TEST_END();
}
```

In `tests/test_cmd_policy.c`, replace `test_policy_token_round_trips_every_legal_pair` (lines 301-323) with:

```c
int test_policy_token_round_trips_every_legal_pair(void) {
    TEST_BEGIN();
    /* Since 2026-10-18 the legal pairs are (a, none) and (a, a): eight of
     * them round-trip exactly. A split pair (none < u < a) still writes and
     * parses, and comes back with the button off. */
    int pairs = 0, kept = 0;
    for (int a = 0; a < POLICY_STOP_COUNT; a++) {
        for (int u = POLICY_NONE; u <= a; u++) {
            CmdPolicy p; p.allowed = a; p.unattended = u;
            char tok[32];
            int n = cmd_policy_to_token(p, tok, sizeof(tok));
            if (n <= 0) { printf("  empty token for {%d,%d}\n", a, u); _tf_local_fail = 1; continue; }

            int want_u = (u == POLICY_NONE || u == a) ? u : POLICY_NONE;
            CmdPolicy back;
            if (!cmd_policy_from_token(tok, &back) ||
                back.allowed != a || back.unattended != want_u) {
                printf("  \"%s\" -> {%d,%d}, expected {%d,%d}\n",
                       tok, back.allowed, back.unattended, a, want_u);
                _tf_local_fail = 1;
            }
            if (back.allowed == a && back.unattended == u) kept++;
            pairs++;
        }
    }
    ASSERT_EQ(pairs, 14);
    ASSERT_EQ(kept, 8);
    TEST_END();
}
```

In `tests/test_cmd_batch.c`, replace `test_cmd_batch_add_copies_defaults` (lines 48-64) with:

```c
int test_cmd_batch_add_copies_defaults(void) {
    TEST_BEGIN();
    CmdBatchSet set;
    cmd_batch_set_init(&set);
    ApprovalQueue defaults;
    chat_approval_init(&defaults);
    /* A non-default policy -- Write with Auto-approve on -- must carry over
     * from the session into a freshly added batch. */
    cmd_policy_set_allowed(&defaults.policy, CMD_WRITE);
    cmd_policy_set_auto(&defaults.policy, 1);
    CmdBatch *a = cmd_batch_add(&set, &defaults, NULL);
    ASSERT_NOT_NULL(a);
    ASSERT_EQ(a->q.policy.allowed, CMD_WRITE);
    ASSERT_EQ(a->q.policy.unattended, CMD_WRITE);

    /* cmd_batch_add() clamps what it copies, so a split default (only a
     * test can make one) arrives with the button off (spec 2026-10-18 s2). */
    cmd_policy_set_unattended(&defaults.policy, CMD_READ);
    CmdBatch *b = cmd_batch_add(&set, &defaults, NULL);
    ASSERT_NOT_NULL(b);
    ASSERT_EQ(b->q.policy.allowed, CMD_WRITE);
    ASSERT_EQ(b->q.policy.unattended, POLICY_NONE);
    cmd_batch_set_free(&set);
    TEST_END();
}
```

In `tests/test_config.c`:

(a) In `test_config_validate_review_fix_settings_clamp`, replace the last block (lines 1057-1062, from `/* In-range values pass through untouched. */` to the second `ASSERT_EQ`) with:

```c
    /* A split marker rounds down to "button off"; a matched pair passes
     * through untouched (spec 2026-10-18 section 2). */
    s.ai_policy_default.allowed = CMD_WRITE;
    s.ai_policy_default.unattended = CMD_READ;
    settings_validate(&s);
    ASSERT_EQ(s.ai_policy_default.allowed, (int)CMD_WRITE);
    ASSERT_EQ(s.ai_policy_default.unattended, POLICY_NONE);

    s.ai_policy_default.allowed = CMD_WRITE;
    s.ai_policy_default.unattended = CMD_WRITE;
    settings_validate(&s);
    ASSERT_EQ(s.ai_policy_default.allowed, (int)CMD_WRITE);
    ASSERT_EQ(s.ai_policy_default.unattended, (int)CMD_WRITE);
```

(b) In `test_config_roundtrip_review_fix_settings`, change the two `CMD_READ` uses for `unattended` (lines 1074 and 1084) to `CMD_WRITE`.

(c) Replace the body of `test_config_policy_token_round_trips_every_pair` (lines 1139-1171) with:

```c
int test_config_policy_token_round_trips_every_pair(void)
{
    TEST_BEGIN();
    /* All 14 (allowed, unattended) pairs save and load; the 8 legal ones
     * come back unchanged and the 6 split ones come back with the button
     * off (spec 2026-10-18 section 2). */
    int pairs = 0;
    for (int a = 0; a < POLICY_STOP_COUNT; a++) {
        for (int u = POLICY_NONE; u <= a; u++) {
            Config *orig = config_new_default();
            ASSERT_NOT_NULL(orig);
            orig->settings.ai_policy_default.allowed = a;
            orig->settings.ai_policy_default.unattended = u;

            int rc = config_save(orig, TMP_CFG);
            ASSERT_EQ(rc, 0);

            Config *loaded = config_load(TMP_CFG);
            ASSERT_NOT_NULL(loaded);
            int want_u = (u == POLICY_NONE || u == a) ? u : POLICY_NONE;
            if (loaded->settings.ai_policy_default.allowed != a ||
                loaded->settings.ai_policy_default.unattended != want_u) {
                printf("  {%d,%d} round-tripped as {%d,%d}, expected {%d,%d}\n", a, u,
                       loaded->settings.ai_policy_default.allowed,
                       loaded->settings.ai_policy_default.unattended, a, want_u);
                _tf_local_fail = 1;
            }
            config_free(orig);
            config_free(loaded);
            remove(TMP_CFG);
            pairs++;
        }
    }
    ASSERT_EQ(pairs, 14);
    TEST_END();
}
```

(d) In `test_config_save_writes_policy_key_and_neither_old_key`, change line 1179 to `orig->settings.ai_policy_default.unattended = CMD_WRITE;`, and change line 1190 to `ASSERT_TRUE(strstr(buf, "\"ai_policy_default\": \"write/write\"") != NULL);`.

(e) In `test_config_load_policy_key_wins_over_both_old_keys`, change the token `write/unknown` (line 1286) to `write/write` and the expectation at line 1292 to `ASSERT_EQ(cfg->settings.ai_policy_default.unattended, (int)CMD_WRITE);`.

In `tests/runner.c`, add after `int test_policy_caption_reads_as_a_sentence(void);` (line 2321):

```c
int test_policy_clamp_rounds_a_split_down(void);
int test_policy_set_allowed_moves_both_markers_with_auto_on(void);
int test_policy_set_allowed_leaves_auto_off(void);
int test_policy_set_allowed_on_a_split_turns_auto_off(void);
int test_policy_set_auto_on_and_off(void);
int test_policy_auto_on_reads_a_split_as_off(void);
int test_policy_no_sequence_of_set_allowed_and_set_auto_splits(void);
int test_policy_needs_confirm_only_when_entering_critical_auto(void);
int test_policy_status_word_packs_level_and_button(void);
```

and after `    failed += test_policy_caption_reads_as_a_sentence();` (line 5464):

```c
    failed += test_policy_clamp_rounds_a_split_down();
    failed += test_policy_set_allowed_moves_both_markers_with_auto_on();
    failed += test_policy_set_allowed_leaves_auto_off();
    failed += test_policy_set_allowed_on_a_split_turns_auto_off();
    failed += test_policy_set_auto_on_and_off();
    failed += test_policy_auto_on_reads_a_split_as_off();
    failed += test_policy_no_sequence_of_set_allowed_and_set_auto_splits();
    failed += test_policy_needs_confirm_only_when_entering_critical_auto();
    failed += test_policy_status_word_packs_level_and_button();
```

Then `sed -i 's/\r*$/\r/' tests/runner.c`.

- [ ] **Step 2: Run the tests and see them fail.** Run `make test 2>&1 | tail -20`. Expected: a link failure, `undefined reference to 'cmd_policy_set_auto'`, `'cmd_policy_auto_on'`, `'cmd_policy_needs_confirm'` and `'cmd_policy_status_word'`.

- [ ] **Step 3: Implement.** In `src/core/cmd_policy.h`, replace the three prototypes `cmd_policy_clamp`, `cmd_policy_set_allowed` and `cmd_policy_set_unattended` together with their comments (lines 47-64) with:

```c
/* Normalise anything that arrived from disk or from a sloppy caller:
 * `allowed` into 0..3 and `unattended` into POLICY_NONE..allowed, then a
 * split marker (POLICY_NONE < unattended < allowed) rounds DOWN to
 * POLICY_NONE. The invariant this enforces, since 2026-10-18
 * (docs/superpowers/specs/2026-10-18-ai-auto-approve-button-design.md
 * section 2): `unattended` is POLICY_NONE or equal to `allowed` -- the UI
 * shows the policy as one level plus an Auto-approve button. NULL-safe. */
void cmd_policy_clamp(CmdPolicy *p);

/* Move the level (the ceiling) to `stop`, clamped to 0..3. The policy is
 * normalised first, so a split turns Auto-approve off. Then, if
 * Auto-approve is on, the unattended marker moves with the level; if it
 * is off it stays off. NULL-safe. */
void cmd_policy_set_allowed(CmdPolicy *p, int stop);

/* TESTS ONLY: test_chat_approval.c, test_cmd_batch.c and
 * test_cmd_policy.c exercise the gate under split policies. No UI code
 * calls this. Clamps `stop` to POLICY_NONE..allowed and does NOT
 * normalise a split. NULL-safe. */
void cmd_policy_set_unattended(CmdPolicy *p, int stop);

/* Auto-approve on (unattended = allowed) or off (POLICY_NONE). The policy
 * is clamped first. NULL-safe. */
void cmd_policy_set_auto(CmdPolicy *p, int on);

/* 1 when Auto-approve is on: after cmd_policy_clamp(), unattended ==
 * allowed. A split reads as off. */
int cmd_policy_auto_on(CmdPolicy p);

/* 1 when moving from `cur` to `next` turns on "everything runs without
 * asking" -- `next` is Critical with Auto-approve on and `cur` is not. The
 * AI panel and Settings ask for confirmation exactly then. */
int cmd_policy_needs_confirm(CmdPolicy cur, CmdPolicy next);

/* The policy packed for the AI panel's WM_AI_GET_POLICY reply (the
 * integration harness reads it): bits 0-7 the level, bit 8 (0x100) set
 * when Auto-approve is on. */
unsigned cmd_policy_status_word(CmdPolicy p);
```

and add to the comment of `cmd_policy_cycle_unattended` (line 69): `No UI code calls this since 2026-10-18; it keeps splits, like cmd_policy_set_unattended().`

In `src/core/cmd_policy.c`, replace lines 14-77, from `static int stop_in_range` through the end of `cmd_policy_blocks`, with:

```c
static int stop_in_range(int stop)
{
    return stop >= POLICY_NONE && stop < POLICY_STOP_COUNT;
}

/* Range only: `allowed` into 0..3, `unattended` into POLICY_NONE..allowed.
 * Keeps a split. The gate and the test-only setters use this so a split
 * policy a test hands them is judged exactly as given. */
static void clamp_range(CmdPolicy *p)
{
    if (p->allowed < CMD_READ) p->allowed = CMD_READ;
    if (p->allowed > POLICY_STOP_COUNT - 1) p->allowed = POLICY_STOP_COUNT - 1;
    if (p->unattended < POLICY_NONE) p->unattended = POLICY_NONE;
    if (p->unattended > p->allowed) p->unattended = p->allowed;
}

CmdPolicy cmd_policy_default(void)
{
    CmdPolicy p;
    p.allowed = CMD_READ;
    p.unattended = POLICY_NONE;
    return p;
}

void cmd_policy_clamp(CmdPolicy *p)
{
    if (!p) return;
    clamp_range(p);
    if (p->unattended != POLICY_NONE && p->unattended != p->allowed)
        p->unattended = POLICY_NONE;     /* a split rounds down: button off */
}

void cmd_policy_set_allowed(CmdPolicy *p, int stop)
{
    if (!p) return;
    cmd_policy_clamp(p);
    int on = (p->unattended != POLICY_NONE);
    p->allowed = stop;
    p->unattended = POLICY_NONE;
    clamp_range(p);                      /* clamps the level into 0..3 */
    if (on) p->unattended = p->allowed;
}

void cmd_policy_set_unattended(CmdPolicy *p, int stop)
{
    if (!p) return;
    p->unattended = stop;
    clamp_range(p);
}

void cmd_policy_set_auto(CmdPolicy *p, int on)
{
    if (!p) return;
    cmd_policy_clamp(p);
    p->unattended = on ? p->allowed : POLICY_NONE;
}

int cmd_policy_auto_on(CmdPolicy p)
{
    cmd_policy_clamp(&p);
    return p.unattended != POLICY_NONE;
}

int cmd_policy_needs_confirm(CmdPolicy cur, CmdPolicy next)
{
    cmd_policy_clamp(&cur);
    cmd_policy_clamp(&next);
    int next_all = next.allowed == CMD_CRITICAL && next.unattended == CMD_CRITICAL;
    int cur_all  = cur.allowed == CMD_CRITICAL && cur.unattended == CMD_CRITICAL;
    return next_all && !cur_all;
}

unsigned cmd_policy_status_word(CmdPolicy p)
{
    cmd_policy_clamp(&p);
    return (unsigned)p.allowed | (p.unattended != POLICY_NONE ? 0x100u : 0u);
}

void cmd_policy_cycle_allowed(CmdPolicy *p)
{
    if (!p) return;
    cmd_policy_clamp(p);
    int next = p->allowed + 1;
    if (next > POLICY_STOP_COUNT - 1) next = CMD_READ;
    cmd_policy_set_allowed(p, next);
}

void cmd_policy_cycle_unattended(CmdPolicy *p)
{
    if (!p) return;
    clamp_range(p);
    int next = p->unattended + 1;
    if (next > p->allowed) next = POLICY_NONE;
    cmd_policy_set_unattended(p, next);
}

unsigned cmd_policy_unattended_mask(CmdPolicy p)
{
    clamp_range(&p);
    if (p.unattended < CMD_READ) return 0u;
    return (1u << (unsigned)(p.unattended + 1)) - 1u;
}

int cmd_policy_blocks(CmdPolicy p, CmdSafetyLevel worst)
{
    clamp_range(&p);
    return (int)worst > p.allowed;
}
```

In `cmd_policy_caption`, change `cmd_policy_clamp(&p);` to `clamp_range(&p);`. The caption describes whatever policy it is handed, including a test's split. `cmd_policy_to_token` and `cmd_policy_from_token` keep `cmd_policy_clamp`, so tokens are written and read normalised.

- [ ] **Step 4: Run the tests.** Run `make test 2>&1 | tail -3`. Expected: `Tests Run: 2966, Failed: 0`, which is the 2,957 baseline plus 9. Then run the strict compile for `src/core/cmd_policy.c` (Global Constraints), which should print nothing.

- [ ] **Step 5: Commit.**

```bash
git add src/core/cmd_policy.h src/core/cmd_policy.c tests/test_cmd_policy.c tests/test_cmd_batch.c tests/test_config.c tests/runner.c
git status --short
git commit -F - <<'EOF'
feat(ai): make the command policy one level plus an Auto-approve switch (v1.2.37)

cmd_policy_clamp() now rounds a split unattended marker down to none, so
every policy that enters from disk, Settings, the demo seed or a new batch
is either "button off" or "unattended equals allowed". set_allowed() moves
the marker with the level when the button is on; set_auto(), auto_on(),
needs_confirm() and status_word() are new. set_unattended() stays for the
gate tests and keeps splits. Config tests that saved a split now expect it
back with the button off.

Models: claude-sonnet-5 (implementation); claude-opus-5-5 (plan)

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
```

---

### Task 2: A batch built for a hidden tab never approves on arrival

**Files**
- Modify: `src/core/cmd_policy.h` (after the `cmd_policy_status_word` prototype added in Task 1)
- Modify: `src/core/cmd_policy.c` (after `cmd_policy_status_word`)
- Test: `tests/test_cmd_policy.c` (append), `tests/test_chat_approval.c` (include at line 12, append), `tests/runner.c`

**Interfaces**
- Consumes: `void cmd_policy_set_allowed(CmdPolicy *p, int stop);`, `void cmd_policy_set_auto(CmdPolicy *p, int on);`, `void cmd_policy_clamp(CmdPolicy *p);` (Task 1).
- Produces: `CmdPolicy cmd_policy_attended(CmdPolicy p);`, the policy clamped with `unattended = POLICY_NONE`. Task 11 uses it in the background reply path.

- [ ] **Step 1: Write the failing tests.** Append to `tests/test_cmd_policy.c`:

```c
int test_policy_attended_keeps_the_ceiling(void) {
    TEST_BEGIN();
    CmdPolicy p; p.allowed = CMD_READ; p.unattended = CMD_READ;
    CmdPolicy a = cmd_policy_attended(p);
    ASSERT_EQ(a.allowed, (int)CMD_READ);
    ASSERT_EQ(a.unattended, POLICY_NONE);
    p.allowed = 99; p.unattended = 99;
    a = cmd_policy_attended(p);
    ASSERT_EQ(a.allowed, (int)CMD_CRITICAL);
    ASSERT_EQ(a.unattended, POLICY_NONE);
    TEST_END();
}
```

In `tests/test_chat_approval.c`, add `#include "cmd_batch.h"` after `#include "ai_prompt.h"` (line 11), then append:

```c
/* 2026-10-18 auto-approve spec, section 6: a reply for a tab that is not
 * displayed builds its batch under the session's policy with the button
 * forced off, so nothing in it is approved on arrival -- even when the
 * session itself would run everything. */
int test_approval_attended_policy_approves_nothing_on_arrival(void) {
    TEST_BEGIN();
    static const char *const cmds[4] = {
        "ls -la", "frobnicate", "mv a b", "rm -rf /tmp"   /* R, U, W, C */
    };
    CmdPolicy session = cmd_policy_default();
    cmd_policy_set_allowed(&session, CMD_CRITICAL);
    cmd_policy_set_auto(&session, 1);
    ASSERT_EQ(session.unattended, (int)CMD_CRITICAL);

    /* The session's own policy would approve all four. */
    ApprovalQueue live;
    chat_approval_init(&live);
    live.policy = session;
    for (int i = 0; i < 4; i++)
        ASSERT_TRUE(chat_approval_add_session(&live, cmds[i], CMD_PLATFORM_LINUX, 0) >= 0);
    for (int i = 0; i < live.count; i++)
        ASSERT_EQ((int)live.entries[i].status, (int)APPROVE_APPROVED);

    /* The background path: cmd_batch_add() under the attended copy. */
    CmdBatchSet set;
    cmd_batch_set_init(&set);
    ApprovalQueue defaults;
    chat_approval_init(&defaults);
    defaults.policy = cmd_policy_attended(session);
    ASSERT_EQ(defaults.policy.allowed, (int)CMD_CRITICAL);
    ASSERT_EQ(defaults.policy.unattended, POLICY_NONE);
    CmdBatch *b = cmd_batch_add(&set, &defaults, NULL);
    ASSERT_NOT_NULL(b);
    if (b) {
        for (int i = 0; i < 4; i++)
            ASSERT_TRUE(chat_approval_add_session(&b->q, cmds[i], CMD_PLATFORM_LINUX, 0) >= 0);
        ASSERT_EQ(b->q.count, 4);
        for (int i = 0; i < b->q.count; i++)
            ASSERT_EQ((int)b->q.entries[i].status, (int)APPROVE_PENDING);
        ASSERT_EQ(chat_approval_next_approved(&b->q), -1);
        ASSERT_EQ(chat_approval_needs_user(&b->q), 1);
    }
    cmd_batch_set_free(&set);
    TEST_END();
}
```

In `tests/runner.c`, add `int test_policy_attended_keeps_the_ceiling(void);` after `int test_policy_status_word_packs_level_and_button(void);` and `int test_approval_attended_policy_approves_nothing_on_arrival(void);` after `int test_approval_add_session_contradicted_takes_worse(void);` (line 2797). Add `    failed += test_policy_attended_keeps_the_ceiling();` after `    failed += test_policy_status_word_packs_level_and_button();` and `    failed += test_approval_attended_policy_approves_nothing_on_arrival();` after `    failed += test_approval_add_session_contradicted_takes_worse();` (line 6000). Then run `sed -i 's/\r*$/\r/' tests/runner.c`.

- [ ] **Step 2: Run the tests and see them fail.** Run `make test 2>&1 | tail -20`. Expected: `undefined reference to 'cmd_policy_attended'`.

- [ ] **Step 3: Implement.** In `src/core/cmd_policy.h`, after the `cmd_policy_status_word` prototype:

```c
/* `p` with Auto-approve forced off: clamped, then unattended = POLICY_NONE.
 * The ceiling is kept, so blocked rows stay blocked. A batch built for a
 * tab that is not displayed is built under this, so nothing in it runs
 * before the user has seen it (spec 2026-10-18 section 6). */
CmdPolicy cmd_policy_attended(CmdPolicy p);
```

In `src/core/cmd_policy.c`, after `cmd_policy_status_word`:

```c
CmdPolicy cmd_policy_attended(CmdPolicy p)
{
    cmd_policy_clamp(&p);
    p.unattended = POLICY_NONE;
    return p;
}
```

- [ ] **Step 4: Run the tests.** Run `make test 2>&1 | tail -3`. Expected: `Tests Run: 2968, Failed: 0`. The strict compile of `src/core/cmd_policy.c` should print nothing.

- [ ] **Step 5: Commit.**

```bash
git add src/core/cmd_policy.h src/core/cmd_policy.c tests/test_cmd_policy.c tests/test_chat_approval.c tests/runner.c
git status --short
git commit -F - <<'EOF'
feat(ai): add cmd_policy_attended for batches built while a tab is hidden (v1.2.37)

The policy with Auto-approve forced off and the ceiling kept. A test pins
that a batch added under it has no approved row even when the session
itself would run every category unattended.

Models: claude-sonnet-5 (implementation); claude-opus-5-5 (plan)

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
```

---

### Task 3: Compact labels, tooltip text and the confirmation text

**Files**
- Modify: `src/core/cmd_policy.h`: remove the `cmd_policy_tip` prototype and comment (base lines 108-112); add new declarations after `cmd_policy_stop_label` (base line 88).
- Modify: `src/core/cmd_policy.c`: remove `cmd_policy_tip` (lines 172-201); add tables and functions.
- Test: `tests/test_cmd_policy.c`: remove `test_policy_tip_covers_raise_lower_toggle_and_refusal` (base lines 387-417, with its comment); append new tests.
- Test: `tests/runner.c`: remove the `test_policy_tip_covers_raise_lower_toggle_and_refusal` declaration (line 2322) and call (line 5465); register new tests.

**Interfaces**
- Consumes: `const char *cmd_policy_stop_label(int stop);` (existing).
- Produces:
  - `const char *cmd_policy_stop_short(int stop);` returns `"R"`/`"U"`/`"W"`/`"C"`, and `""` out of range.
  - `const char *cmd_policy_auto_label(int compact);` returns `"Auto-approve"` or `"Auto"`.
  - `#define CMD_POLICY_TIP_MAX 512`
  - `#define CMD_POLICY_CONFIRM_TEXT "Run every command the assistant proposes, including ones that can destroy data, without asking?"`
  - `int cmd_policy_cell_tip(int level, int compact, int auto_on, char *buf, size_t cap);`
  - `int cmd_policy_auto_tip(int on, int allowed, char *buf, size_t cap);`
  - Both tip functions return the length written, or 0 with `buf[0] = '\0'` for a level out of range.

- [ ] **Step 1: Write the failing tests.** Delete `test_policy_tip_covers_raise_lower_toggle_and_refusal` from `tests/test_cmd_policy.c`, then append:

```c
/* ---- 2026-10-18: labels and tooltips ----------------------------------- */

#define TIP_UNKNOWN_FULL "Unknown: commands the classifier cannot place, including wrappers such as sh -c or xargs that hide what they run. Click to allow up to this level; Write and Critical stay blocked."
#define TIP_AUTO_SUFFIX  " With Auto-approve on, commands up to this level run without asking."

int test_policy_stop_short_labels(void) {
    TEST_BEGIN();
    ASSERT_STR_EQ(cmd_policy_stop_short(CMD_READ), "R");
    ASSERT_STR_EQ(cmd_policy_stop_short(CMD_UNKNOWN), "U");
    ASSERT_STR_EQ(cmd_policy_stop_short(CMD_WRITE), "W");
    ASSERT_STR_EQ(cmd_policy_stop_short(CMD_CRITICAL), "C");
    ASSERT_STR_EQ(cmd_policy_stop_short(POLICY_NONE), "");
    ASSERT_STR_EQ(cmd_policy_stop_short(4), "");
    TEST_END();
}

int test_policy_auto_label_full_and_compact(void) {
    TEST_BEGIN();
    ASSERT_STR_EQ(cmd_policy_auto_label(0), "Auto-approve");
    ASSERT_STR_EQ(cmd_policy_auto_label(1), "Auto");
    ASSERT_STR_EQ(CMD_POLICY_CONFIRM_TEXT,
        "Run every command the assistant proposes, including ones that can "
        "destroy data, without asking?");
    TEST_END();
}

int test_policy_cell_tip_full_labels_button_off(void) {
    TEST_BEGIN();
    char buf[CMD_POLICY_TIP_MAX];
    cmd_policy_cell_tip(CMD_READ, 0, 0, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "Read: commands that only inspect, such as ls, cat or git status. Click to make this the highest level the assistant may run; anything above it is blocked.");
    cmd_policy_cell_tip(CMD_UNKNOWN, 0, 0, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, TIP_UNKNOWN_FULL);
    cmd_policy_cell_tip(CMD_WRITE, 0, 0, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "Write: commands that change files or state, such as rm, mv or apt install. Click to allow up to this level; Critical stays blocked.");
    cmd_policy_cell_tip(CMD_CRITICAL, 0, 0, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "Critical: commands that can destroy data or the system, such as rm -rf / or mkfs. Click to allow everything. Use with care.");
    TEST_END();
}

int test_policy_cell_tip_compact_and_auto_on(void) {
    TEST_BEGIN();
    static const char *const letters[POLICY_STOP_COUNT] = { "R", "U", "W", "C" };
    char full[CMD_POLICY_TIP_MAX], tip[CMD_POLICY_TIP_MAX], want[CMD_POLICY_TIP_MAX];
    for (int k = 0; k < POLICY_STOP_COUNT; k++) {
        cmd_policy_cell_tip(k, 0, 0, full, sizeof(full));

        cmd_policy_cell_tip(k, 1, 0, tip, sizeof(tip));
        snprintf(want, sizeof(want), "%s = %s", letters[k], full);
        ASSERT_STR_EQ(tip, want);

        cmd_policy_cell_tip(k, 0, 1, tip, sizeof(tip));
        snprintf(want, sizeof(want), "%s%s", full, TIP_AUTO_SUFFIX);
        ASSERT_STR_EQ(tip, want);

        cmd_policy_cell_tip(k, 1, 1, tip, sizeof(tip));
        snprintf(want, sizeof(want), "%s = %s%s", letters[k], full, TIP_AUTO_SUFFIX);
        ASSERT_STR_EQ(tip, want);
    }
    cmd_policy_cell_tip(CMD_UNKNOWN, 1, 1, tip, sizeof(tip));
    ASSERT_STR_EQ(tip, "U = " TIP_UNKNOWN_FULL TIP_AUTO_SUFFIX);
    TEST_END();
}

int test_policy_auto_tip_off_and_on(void) {
    TEST_BEGIN();
    char buf[CMD_POLICY_TIP_MAX];
    cmd_policy_auto_tip(0, CMD_WRITE, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "Auto-approve: off. Click to run commands up to the Write level without asking. Anything above Write stays blocked.");
    cmd_policy_auto_tip(1, CMD_WRITE, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "Auto-approve: on. Commands up to Write run without asking; anything above is blocked. Keep this tab shown: commands proposed while it is hidden wait for your approval. Click to turn off.");
    cmd_policy_auto_tip(0, CMD_READ, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "Auto-approve: off. Click to run commands up to the Read level without asking. Anything above Read stays blocked.");
    TEST_END();
}

/* Review Focus 4: the longest tip (compact Unknown with the Auto-approve
 * suffix) must fit ai_chat.c's tooltip buffer whole. */
int test_policy_tips_fit_the_tooltip_buffer(void) {
    TEST_BEGIN();
    char buf[CMD_POLICY_TIP_MAX];
    for (int k = 0; k < POLICY_STOP_COUNT; k++) {
        for (int compact = 0; compact < 2; compact++) {
            for (int on = 0; on < 2; on++) {
                int n = cmd_policy_cell_tip(k, compact, on, buf, sizeof(buf));
                if (n <= 0 || n >= CMD_POLICY_TIP_MAX - 1 || buf[n - 1] != '.') {
                    printf("  cell tip k=%d compact=%d on=%d: n=%d\n", k, compact, on, n);
                    _tf_local_fail = 1;
                }
            }
        }
        for (int on = 0; on < 2; on++) {
            int n = cmd_policy_auto_tip(on, k, buf, sizeof(buf));
            if (n <= 0 || n >= CMD_POLICY_TIP_MAX - 1 || buf[n - 1] != '.') {
                printf("  auto tip on=%d level=%d: n=%d\n", on, k, n);
                _tf_local_fail = 1;
            }
        }
    }
    TEST_END();
}

int test_policy_tips_reject_out_of_range_and_null(void) {
    TEST_BEGIN();
    char buf[CMD_POLICY_TIP_MAX];
    buf[0] = 'x';
    ASSERT_EQ(cmd_policy_cell_tip(POLICY_NONE, 0, 0, buf, sizeof(buf)), 0);
    ASSERT_STR_EQ(buf, "");
    ASSERT_EQ(cmd_policy_cell_tip(4, 0, 0, buf, sizeof(buf)), 0);
    ASSERT_EQ(cmd_policy_cell_tip(CMD_READ, 0, 0, NULL, 16), 0);
    ASSERT_EQ(cmd_policy_cell_tip(CMD_READ, 0, 0, buf, 0), 0);
    ASSERT_EQ(cmd_policy_auto_tip(1, CMD_READ, NULL, 16), 0);
    ASSERT_EQ(cmd_policy_auto_tip(1, CMD_READ, buf, 0), 0);
    /* An out-of-range level is clamped, never printed as "Nothing". */
    cmd_policy_auto_tip(0, 99, buf, sizeof(buf));
    ASSERT_TRUE(strstr(buf, "Critical") != NULL);
    cmd_policy_auto_tip(0, -5, buf, sizeof(buf));
    ASSERT_TRUE(strstr(buf, "Read") != NULL);
    TEST_END();
}
```

In `tests/runner.c`, delete `int test_policy_tip_covers_raise_lower_toggle_and_refusal(void);` and `    failed += test_policy_tip_covers_raise_lower_toggle_and_refusal();`. After `int test_policy_attended_keeps_the_ceiling(void);` add:

```c
int test_policy_stop_short_labels(void);
int test_policy_auto_label_full_and_compact(void);
int test_policy_cell_tip_full_labels_button_off(void);
int test_policy_cell_tip_compact_and_auto_on(void);
int test_policy_auto_tip_off_and_on(void);
int test_policy_tips_fit_the_tooltip_buffer(void);
int test_policy_tips_reject_out_of_range_and_null(void);
```

and after `    failed += test_policy_attended_keeps_the_ceiling();` add:

```c
    failed += test_policy_stop_short_labels();
    failed += test_policy_auto_label_full_and_compact();
    failed += test_policy_cell_tip_full_labels_button_off();
    failed += test_policy_cell_tip_compact_and_auto_on();
    failed += test_policy_auto_tip_off_and_on();
    failed += test_policy_tips_fit_the_tooltip_buffer();
    failed += test_policy_tips_reject_out_of_range_and_null();
```

Then run `sed -i 's/\r*$/\r/' tests/runner.c`.

- [ ] **Step 2: Run the tests and see them fail.** Run `make test 2>&1 | tail -20`. Expected: compile errors in `test_cmd_policy.c`, `'CMD_POLICY_TIP_MAX' undeclared` and `'CMD_POLICY_CONFIRM_TEXT' undeclared`.

- [ ] **Step 3: Implement.** In `src/core/cmd_policy.h`, delete the `cmd_policy_tip` prototype and its comment. After the `cmd_policy_stop_label` prototype add:

```c
/* One-letter label for a stop on the compact pill (a narrow AI panel):
 * "R", "U", "W", "C". Out of range, POLICY_NONE included, returns "". */
const char *cmd_policy_stop_short(int stop);

/* The Auto-approve button's label: "Auto-approve", or "Auto" when
 * `compact` is non-zero. */
const char *cmd_policy_auto_label(int compact);

/* Room for any tooltip below, including the NUL. ai_chat.c's tooltip
 * buffer is this size. */
#define CMD_POLICY_TIP_MAX 512

/* The question asked before everything runs unattended (Critical with
 * Auto-approve on), in the AI panel and on Save in Settings. */
#define CMD_POLICY_CONFIRM_TEXT \
    "Run every command the assistant proposes, including ones that can " \
    "destroy data, without asking?"

/* Tooltip for the cell of `level` (0..3): what the level covers and what
 * a click does. `compact` prefixes the letter ("R = Read: ..."); `auto_on`
 * appends " With Auto-approve on, commands up to this level run without
 * asking." Returns the length in `buf`; out of range gives "" and 0. */
int cmd_policy_cell_tip(int level, int compact, int auto_on, char *buf, size_t cap);

/* Tooltip for the Auto-approve button in state `on`, naming the level
 * `allowed` (clamped to 0..3). Returns the length in `buf`. */
int cmd_policy_auto_tip(int on, int allowed, char *buf, size_t cap);
```

In `src/core/cmd_policy.c`, delete `cmd_policy_tip`. After the `k_stop_labels` table add:

```c
static const char *const k_stop_short[POLICY_STOP_COUNT] = { "R", "U", "W", "C" };

/* Tooltip text, spec 2026-10-18 section 4, word for word. */
static const char *const k_cell_tips[POLICY_STOP_COUNT] = {
    "Read: commands that only inspect, such as ls, cat or git status. "
    "Click to make this the highest level the assistant may run; anything "
    "above it is blocked.",
    "Unknown: commands the classifier cannot place, including wrappers such "
    "as sh -c or xargs that hide what they run. Click to allow up to this "
    "level; Write and Critical stay blocked.",
    "Write: commands that change files or state, such as rm, mv or apt "
    "install. Click to allow up to this level; Critical stays blocked.",
    "Critical: commands that can destroy data or the system, such as "
    "rm -rf / or mkfs. Click to allow everything. Use with care."
};
static const char k_auto_suffix[] =
    " With Auto-approve on, commands up to this level run without asking.";
```

and, after `cmd_policy_stop_label`:

```c
const char *cmd_policy_stop_short(int stop)
{
    if (stop < CMD_READ || stop > POLICY_STOP_COUNT - 1) return "";
    return k_stop_short[stop];
}

const char *cmd_policy_auto_label(int compact)
{
    return compact ? "Auto" : "Auto-approve";
}

int cmd_policy_cell_tip(int level, int compact, int auto_on, char *buf, size_t cap)
{
    if (!buf || cap == 0) return 0;
    if (level < CMD_READ || level > POLICY_STOP_COUNT - 1) {
        buf[0] = '\0';
        return 0;
    }
    snprintf(buf, cap, "%s%s%s%s",
             compact ? k_stop_short[level] : "",
             compact ? " = " : "",
             k_cell_tips[level],
             auto_on ? k_auto_suffix : "");
    return (int)strlen(buf);
}

int cmd_policy_auto_tip(int on, int allowed, char *buf, size_t cap)
{
    if (!buf || cap == 0) return 0;
    if (allowed < CMD_READ) allowed = CMD_READ;
    if (allowed > POLICY_STOP_COUNT - 1) allowed = POLICY_STOP_COUNT - 1;
    const char *lvl = cmd_policy_stop_label(allowed);
    if (on)
        snprintf(buf, cap,
                 "Auto-approve: on. Commands up to %s run without asking; "
                 "anything above is blocked. Keep this tab shown: commands "
                 "proposed while it is hidden wait for your approval. "
                 "Click to turn off.", lvl);
    else
        snprintf(buf, cap,
                 "Auto-approve: off. Click to run commands up to the %s "
                 "level without asking. Anything above %s stays blocked.",
                 lvl, lvl);
    return (int)strlen(buf);
}
```

`ai_chat.c` still calls `cmd_policy_tip`. Task 10 removes the call. Until then only the MinGW build is affected; see Global Constraints.

- [ ] **Step 4: Run the tests.** Run `make test 2>&1 | tail -3`. Expected: `Tests Run: 2974, Failed: 0` (2968 − 1 + 7). The strict compile of `src/core/cmd_policy.c` should print nothing.

- [ ] **Step 5: Commit.**

```bash
git add src/core/cmd_policy.h src/core/cmd_policy.c tests/test_cmd_policy.c tests/runner.c
git status --short
git commit -F - <<'EOF'
feat(ai): add compact labels and per-element tooltip text for the policy row (v1.2.37)

R/U/W/C and "Auto" for a narrow row, the four cell tips with the
Auto-approve suffix and compact prefix, the button's on/off tip naming
the level, and the Critical confirmation text, all in core and tested.
cmd_policy_tip goes with the rail it described.

Models: claude-sonnet-5 (implementation); claude-opus-5-5 (plan)

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
```

---

### Task 4: `ns_policy_layout` loses the rail and gains a clip; `HIT_POLICY_AUTO`

**Files**
- Modify: `src/core/ns_layout.h`: enum lines 67-78; `NsPolicyLayout` and the three prototypes with their comments, lines 86-128.
- Modify: `src/core/ns_layout.c`: `ns_policy_layout` and `ns_policy_hit`, lines 301-375.
- Test: `tests/test_ns_layout.c`: `test_hit_copy_appended_without_renumbering` (lines 624-632); replace the whole policy section, from the `/* ====` line above the `ns_policy_layout / ns_policy_width / ns_policy_hit` banner (line 855) to the end of the file.
- Test: `tests/runner.c`: declarations lines 2805-2824, calls lines 6008-6027.

**Interfaces**
- Consumes: none.
- Produces:

```c
enum { ..., HIT_COPY, HIT_POLICY_AUTO };   /* HIT_POLICY_AUTO == 10 */
typedef struct {
    NsRect cell[NS_POLICY_STOPS];   /* painted cells, touching, full r.h */
    NsRect hit[NS_POLICY_STOPS];    /* click targets; zero-size when clipped */
    int clip_right;                 /* exclusive x nothing paints past */
    int total_w;
} NsPolicyLayout;
int  ns_policy_width(const int stop_text_w[NS_POLICY_STOPS], int dpi);   /* unchanged */
void ns_policy_layout(NsRect r, const int stop_text_w[NS_POLICY_STOPS],
                      int clip_right, int hit_h, int dpi, NsPolicyLayout *out);
int  ns_policy_hit(const NsPolicyLayout *l, int x, int y, int *stop_out);  /* HIT_POLICY_ALLOWED or HIT_NONE */
```

- [ ] **Step 1: Write the failing tests.** In `test_hit_copy_appended_without_renumbering`, add before `TEST_END();`:

```c
    /* 2026-10-18: the Auto-approve button's id is appended after HIT_COPY;
     * HIT_POLICY_UNATTENDED keeps its value and is never produced. */
    ASSERT_EQ(HIT_POLICY_AUTO, 10);
```

Replace everything from the `ns_policy_layout / ns_policy_width / ns_policy_hit` banner comment to the end of `tests/test_ns_layout.c` with:

```c
/* ===========================================================================
 * ns_policy_layout / ns_policy_width / ns_policy_hit -- the status line's
 * policy pill: four touching cells, one band each, cut at `clip_right`
 * (docs/superpowers/specs/2026-10-18-ai-auto-approve-button-design.md s3).
 * ===========================================================================
 */

static const int POLICY_TEXT_W[NS_POLICY_STOPS] = { 40, 30, 50, 60 };
/* At 96 DPI the cells are 56, 46, 66, 76 wide (text + 2 * SP_SM). */

/* ---- cells touch, in order, sum to total_w == ns_policy_width() -------- */

int test_ns_policy_layout_cells_touch_and_sum_to_total_w_96(void)
{
    TEST_BEGIN();
    NsRect r = { 10, 5, 0, 28 };
    r.w = ns_policy_width(POLICY_TEXT_W, 96);
    NsPolicyLayout out;
    ns_policy_layout(r, POLICY_TEXT_W, r.x + r.w, r.h, 96, &out);

    ASSERT_EQ(out.cell[0].x, r.x);
    for (int k = 1; k < NS_POLICY_STOPS; k++) {
        ASSERT_EQ(out.cell[k].x, out.cell[k - 1].x + out.cell[k - 1].w);
        ASSERT_EQ(out.cell[k].y, r.y);
        ASSERT_EQ(out.cell[k].h, r.h);
    }
    int sum = 0;
    for (int k = 0; k < NS_POLICY_STOPS; k++) sum += out.cell[k].w;
    ASSERT_EQ(sum, out.total_w);
    ASSERT_EQ(out.total_w, ns_policy_width(POLICY_TEXT_W, 96));
    ASSERT_EQ(out.clip_right, r.x + r.w);
    TEST_END();
}

int test_ns_policy_layout_cells_touch_and_sum_to_total_w_192(void)
{
    TEST_BEGIN();
    NsRect r = { 10, 5, 0, 56 };
    r.w = ns_policy_width(POLICY_TEXT_W, 192);
    NsPolicyLayout out;
    ns_policy_layout(r, POLICY_TEXT_W, r.x + r.w, r.h, 192, &out);

    ASSERT_EQ(out.cell[0].x, r.x);
    for (int k = 1; k < NS_POLICY_STOPS; k++) {
        ASSERT_EQ(out.cell[k].x, out.cell[k - 1].x + out.cell[k - 1].w);
        ASSERT_EQ(out.cell[k].y, r.y);
        ASSERT_EQ(out.cell[k].h, r.h);
    }
    int sum = 0;
    for (int k = 0; k < NS_POLICY_STOPS; k++) sum += out.cell[k].w;
    ASSERT_EQ(sum, out.total_w);
    ASSERT_EQ(out.total_w, ns_policy_width(POLICY_TEXT_W, 192));
    TEST_END();
}

/* ---- the hit rect is the cell's column, max(hit_h, r.h) tall, centred -- */

int test_ns_policy_layout_hit_rect_spans_taller_box_96(void)
{
    TEST_BEGIN();
    NsRect r = { 0, 14, 0, 28 };
    r.w = ns_policy_width(POLICY_TEXT_W, 96);
    NsPolicyLayout out;
    ns_policy_layout(r, POLICY_TEXT_W, r.x + r.w, 56, 96, &out);
    for (int k = 0; k < NS_POLICY_STOPS; k++) {
        ASSERT_EQ(out.hit[k].x, out.cell[k].x);
        ASSERT_EQ(out.hit[k].w, out.cell[k].w);
        ASSERT_EQ(out.hit[k].h, 56);
        ASSERT_EQ(out.hit[k].y, r.y + (r.h - 56) / 2);
    }
    TEST_END();
}

int test_ns_policy_layout_hit_rect_spans_taller_box_192(void)
{
    TEST_BEGIN();
    NsRect r = { 0, 28, 0, 56 };
    r.w = ns_policy_width(POLICY_TEXT_W, 192);
    NsPolicyLayout out;
    ns_policy_layout(r, POLICY_TEXT_W, r.x + r.w, 112, 192, &out);
    for (int k = 0; k < NS_POLICY_STOPS; k++) {
        ASSERT_EQ(out.hit[k].x, out.cell[k].x);
        ASSERT_EQ(out.hit[k].w, out.cell[k].w);
        ASSERT_EQ(out.hit[k].h, 112);
        ASSERT_EQ(out.hit[k].y, r.y + (r.h - 112) / 2);
    }
    TEST_END();
}

/* ---- point-in-cell hit-tests to (HIT_POLICY_ALLOWED, stop) ------------- */

int test_ns_policy_hit_all_four_cells(void)
{
    TEST_BEGIN();
    NsRect r = { 20, 20, 0, 28 };
    r.w = ns_policy_width(POLICY_TEXT_W, 96);
    NsPolicyLayout out;
    ns_policy_layout(r, POLICY_TEXT_W, r.x + r.w, r.h, 96, &out);
    for (int k = 0; k < NS_POLICY_STOPS; k++) {
        int stop = -99;
        NsRect c = out.cell[k];
        ASSERT_EQ(ns_policy_hit(&out, c.x + c.w / 2, c.y + c.h / 2, &stop), HIT_POLICY_ALLOWED);
        ASSERT_EQ(stop, k);
        /* The bottom row of the cell, where the rail used to be, is the
         * same cell now. */
        stop = -99;
        ASSERT_EQ(ns_policy_hit(&out, c.x + 1, c.y + c.h - 1, &stop), HIT_POLICY_ALLOWED);
        ASSERT_EQ(stop, k);
    }
    TEST_END();
}

int test_ns_policy_hit_outside_returns_none(void)
{
    TEST_BEGIN();
    NsRect r = { 20, 20, 0, 28 };
    r.w = ns_policy_width(POLICY_TEXT_W, 96);
    NsPolicyLayout out;
    ns_policy_layout(r, POLICY_TEXT_W, r.x + r.w, r.h, 96, &out);

    int cy = r.y + r.h / 2;
    int stop = -99;
    ASSERT_EQ(ns_policy_hit(&out, r.x - 5, cy, &stop), HIT_NONE);
    ASSERT_EQ(stop, -1);
    stop = -99;
    ASSERT_EQ(ns_policy_hit(&out, r.x + out.total_w + 5, cy, &stop), HIT_NONE);
    ASSERT_EQ(stop, -1);
    int cx = r.x + out.total_w / 2;
    stop = -99;
    ASSERT_EQ(ns_policy_hit(&out, cx, r.y - 5, &stop), HIT_NONE);
    ASSERT_EQ(stop, -1);
    stop = -99;
    ASSERT_EQ(ns_policy_hit(&out, cx, r.y + r.h + 5, &stop), HIT_NONE);
    ASSERT_EQ(stop, -1);
    TEST_END();
}

/* ---- NULL-safety, zero-height rect, negative stop_text_w --------------- */

int test_ns_policy_layout_null_out_is_noop(void)
{
    TEST_BEGIN();
    NsRect r = { 0, 0, 200, 28 };
    ns_policy_layout(r, POLICY_TEXT_W, 200, 28, 96, NULL); /* must not crash */
    TEST_END();
}

int test_ns_policy_width_null_stop_text_w_safe(void)
{
    TEST_BEGIN();
    int pad = ns_scale(SP_SM, 96);
    ASSERT_EQ(ns_policy_width(NULL, 96), 4 * (2 * pad > 1 ? 2 * pad : 1));
    TEST_END();
}

int test_ns_policy_layout_null_stop_text_w_no_negative_widths(void)
{
    TEST_BEGIN();
    NsRect r = { 0, 0, 200, 28 };
    NsPolicyLayout out;
    ns_policy_layout(r, NULL, 200, 28, 96, &out);
    for (int k = 0; k < NS_POLICY_STOPS; k++) {
        ASSERT_TRUE(out.cell[k].w >= 1);
    }
    TEST_END();
}

int test_ns_policy_layout_zero_height_rect_safe(void)
{
    TEST_BEGIN();
    NsRect r = { 0, 0, 200, 0 };
    NsPolicyLayout out;
    ns_policy_layout(r, POLICY_TEXT_W, 200, 0, 96, &out); /* must not crash */
    for (int k = 0; k < NS_POLICY_STOPS; k++) {
        ASSERT_TRUE(out.cell[k].w >= 1);
        ASSERT_EQ(ns_policy_hit(&out, out.cell[k].x, 0, NULL), HIT_NONE);
    }
    TEST_END();
}

int test_ns_policy_layout_negative_stop_text_w_no_negative_widths(void)
{
    TEST_BEGIN();
    NsRect r = { 0, 0, 200, 28 };
    int text_w[NS_POLICY_STOPS] = { 40, -50, 50, 60 };
    NsPolicyLayout out;
    ns_policy_layout(r, text_w, 200, 28, 96, &out);
    ASSERT_TRUE(out.cell[1].w >= 1);
    int pad = ns_scale(SP_SM, 96);
    int expect = 2 * pad;
    if (expect < 1) expect = 1;
    ASSERT_EQ(out.cell[1].w, expect);
    ASSERT_TRUE(ns_policy_width(text_w, 96) > 0);
    TEST_END();
}

int test_ns_policy_hit_null_layout_safe(void)
{
    TEST_BEGIN();
    int stop = -99;
    ASSERT_EQ(ns_policy_hit(NULL, 5, 5, &stop), HIT_NONE);
    ASSERT_EQ(stop, -1);
    TEST_END();
}

int test_ns_policy_hit_null_stop_out_safe(void)
{
    TEST_BEGIN();
    NsRect r = { 0, 0, 0, 28 };
    r.w = ns_policy_width(POLICY_TEXT_W, 96);
    NsPolicyLayout out;
    ns_policy_layout(r, POLICY_TEXT_W, r.x + r.w, r.h, 96, &out);
    NsRect c = out.hit[0];
    ASSERT_EQ(ns_policy_hit(&out, c.x + c.w / 2, c.y + c.h / 2, NULL), HIT_POLICY_ALLOWED);
    TEST_END();
}

/* ---- hit_h shorter than r.h still gives a target >= the painted cell --- */

int test_ns_policy_layout_hit_h_smaller_than_r_h_96(void)
{
    TEST_BEGIN();
    NsRect r = { 0, 0, 0, 28 };
    r.w = ns_policy_width(POLICY_TEXT_W, 96);
    NsPolicyLayout out;
    ns_policy_layout(r, POLICY_TEXT_W, r.x + r.w, 10 /* hit_h < r.h */, 96, &out);
    for (int k = 0; k < NS_POLICY_STOPS; k++) {
        ASSERT_TRUE(out.hit[k].h >= out.cell[k].h);
    }
    TEST_END();
}

int test_ns_policy_layout_hit_h_smaller_than_r_h_192(void)
{
    TEST_BEGIN();
    NsRect r = { 0, 0, 0, 56 };
    r.w = ns_policy_width(POLICY_TEXT_W, 192);
    NsPolicyLayout out;
    ns_policy_layout(r, POLICY_TEXT_W, r.x + r.w, 20 /* hit_h < r.h */, 192, &out);
    for (int k = 0; k < NS_POLICY_STOPS; k++) {
        ASSERT_TRUE(out.hit[k].h >= out.cell[k].h);
    }
    TEST_END();
}

/* ---- the clip (spec section 3, tier 5) --------------------------------- */

int test_ns_policy_layout_clip_marks_partial_cells_unhittable(void)
{
    TEST_BEGIN();
    NsRect r = { 10, 0, 0, 28 };
    r.w = ns_policy_width(POLICY_TEXT_W, 96);
    NsPolicyLayout full, out;
    ns_policy_layout(r, POLICY_TEXT_W, r.x + r.w, r.h, 96, &full);
    /* One pixel into cell 2: cells 0 and 1 whole, 2 cut, 3 gone. */
    int clip = full.cell[2].x + 1;
    ns_policy_layout(r, POLICY_TEXT_W, clip, r.h, 96, &out);
    ASSERT_EQ(out.clip_right, clip);
    for (int k = 0; k < NS_POLICY_STOPS; k++) {
        /* Painted geometry does not move; only hittability changes. */
        ASSERT_EQ(out.cell[k].x, full.cell[k].x);
        ASSERT_EQ(out.cell[k].w, full.cell[k].w);
    }
    ASSERT_TRUE(out.hit[0].w > 0 && out.hit[0].h > 0);
    ASSERT_TRUE(out.hit[1].w > 0 && out.hit[1].h > 0);
    ASSERT_EQ(out.hit[2].w, 0);
    ASSERT_EQ(out.hit[3].w, 0);
    TEST_END();
}

int test_ns_policy_layout_clip_on_a_cell_edge_keeps_that_cell(void)
{
    TEST_BEGIN();
    NsRect r = { 10, 0, 0, 28 };
    r.w = ns_policy_width(POLICY_TEXT_W, 96);
    NsPolicyLayout full, out;
    ns_policy_layout(r, POLICY_TEXT_W, r.x + r.w, r.h, 96, &full);
    /* clip_right is exclusive: a cell ending exactly on it is whole. */
    int clip = full.cell[1].x + full.cell[1].w;
    ns_policy_layout(r, POLICY_TEXT_W, clip, r.h, 96, &out);
    ASSERT_TRUE(out.hit[1].w > 0);
    ASSERT_EQ(out.hit[2].w, 0);
    ns_policy_layout(r, POLICY_TEXT_W, clip - 1, r.h, 96, &out);
    ASSERT_EQ(out.hit[1].w, 0);
    TEST_END();
}

int test_ns_policy_hit_refuses_a_clipped_cell(void)
{
    TEST_BEGIN();
    NsRect r = { 10, 0, 0, 28 };
    r.w = ns_policy_width(POLICY_TEXT_W, 96);
    NsPolicyLayout full, out;
    ns_policy_layout(r, POLICY_TEXT_W, r.x + r.w, r.h, 96, &full);
    int clip = full.cell[2].x + 20;           /* cell 2's left 20 px still paint */
    ns_policy_layout(r, POLICY_TEXT_W, clip, r.h, 96, &out);
    int stop = -99;
    ASSERT_EQ(ns_policy_hit(&out, full.cell[2].x + 5, 14, &stop), HIT_NONE);
    ASSERT_EQ(stop, -1);
    stop = -99;
    ASSERT_EQ(ns_policy_hit(&out, full.cell[1].x + 5, 14, &stop), HIT_POLICY_ALLOWED);
    ASSERT_EQ(stop, 1);
    TEST_END();
}

/* Review Focus 3: a row narrower than the button puts the clip left of the
 * pill -- nothing may take a click. */
int test_ns_policy_layout_clip_left_of_pill_hits_nothing(void)
{
    TEST_BEGIN();
    NsRect r = { 10, 0, 0, 28 };
    r.w = ns_policy_width(POLICY_TEXT_W, 96);
    NsPolicyLayout out;
    ns_policy_layout(r, POLICY_TEXT_W, r.x - 30, r.h, 96, &out);
    for (int k = 0; k < NS_POLICY_STOPS; k++) {
        ASSERT_EQ(out.hit[k].w, 0);
        ASSERT_TRUE(out.cell[k].w >= 1);
        int stop = -99;
        ASSERT_EQ(ns_policy_hit(&out, out.cell[k].x + 2, 14, &stop), HIT_NONE);
        ASSERT_EQ(stop, -1);
    }
    TEST_END();
}
```

In `tests/runner.c`, replace the declaration block (lines 2805-2824) with:

```c
int test_ns_policy_layout_cells_touch_and_sum_to_total_w_96(void);
int test_ns_policy_layout_cells_touch_and_sum_to_total_w_192(void);
int test_ns_policy_layout_hit_rect_spans_taller_box_96(void);
int test_ns_policy_layout_hit_rect_spans_taller_box_192(void);
int test_ns_policy_hit_all_four_cells(void);
int test_ns_policy_hit_outside_returns_none(void);
int test_ns_policy_layout_null_out_is_noop(void);
int test_ns_policy_width_null_stop_text_w_safe(void);
int test_ns_policy_layout_null_stop_text_w_no_negative_widths(void);
int test_ns_policy_layout_zero_height_rect_safe(void);
int test_ns_policy_layout_negative_stop_text_w_no_negative_widths(void);
int test_ns_policy_hit_null_layout_safe(void);
int test_ns_policy_hit_null_stop_out_safe(void);
int test_ns_policy_layout_hit_h_smaller_than_r_h_96(void);
int test_ns_policy_layout_hit_h_smaller_than_r_h_192(void);
int test_ns_policy_layout_clip_marks_partial_cells_unhittable(void);
int test_ns_policy_layout_clip_on_a_cell_edge_keeps_that_cell(void);
int test_ns_policy_hit_refuses_a_clipped_cell(void);
int test_ns_policy_layout_clip_left_of_pill_hits_nothing(void);
```

and the call block (lines 6008-6027) with:

```c
    failed += test_ns_policy_layout_cells_touch_and_sum_to_total_w_96();
    failed += test_ns_policy_layout_cells_touch_and_sum_to_total_w_192();
    failed += test_ns_policy_layout_hit_rect_spans_taller_box_96();
    failed += test_ns_policy_layout_hit_rect_spans_taller_box_192();
    failed += test_ns_policy_hit_all_four_cells();
    failed += test_ns_policy_hit_outside_returns_none();
    failed += test_ns_policy_layout_null_out_is_noop();
    failed += test_ns_policy_width_null_stop_text_w_safe();
    failed += test_ns_policy_layout_null_stop_text_w_no_negative_widths();
    failed += test_ns_policy_layout_zero_height_rect_safe();
    failed += test_ns_policy_layout_negative_stop_text_w_no_negative_widths();
    failed += test_ns_policy_hit_null_layout_safe();
    failed += test_ns_policy_hit_null_stop_out_safe();
    failed += test_ns_policy_layout_hit_h_smaller_than_r_h_96();
    failed += test_ns_policy_layout_hit_h_smaller_than_r_h_192();
    failed += test_ns_policy_layout_clip_marks_partial_cells_unhittable();
    failed += test_ns_policy_layout_clip_on_a_cell_edge_keeps_that_cell();
    failed += test_ns_policy_hit_refuses_a_clipped_cell();
    failed += test_ns_policy_layout_clip_left_of_pill_hits_nothing();
```

Then run `sed -i 's/\r*$/\r/' tests/runner.c`.

- [ ] **Step 2: Run the tests and see them fail.** Run `make test 2>&1 | tail -20`. Expected: compile errors in `test_ns_layout.c`, `'HIT_POLICY_AUTO' undeclared` and `'NsPolicyLayout' has no member named 'hit'` / `'clip_right'`.

- [ ] **Step 3: Implement.** In `src/core/ns_layout.h`, change the enum's last line from `HIT_COPY            /* appended: the numeric values above are unchanged */` to:

```c
    HIT_COPY,           /* appended: the numeric values above are unchanged */
    HIT_POLICY_AUTO     /* appended 2026-10-18: the status line's Auto-approve
                         * button. HIT_POLICY_UNATTENDED keeps its value but is
                         * never produced since the rail went. */
```

Replace the `NsPolicyLayout` comment and struct, and the comments and prototypes of `ns_policy_layout` and `ns_policy_hit` (lines 86-128, keeping `ns_policy_width` and its comment), with:

```c
/* One pill, NS_POLICY_STOPS touching cells, one band each: clicking a cell
 * sets the level (docs/superpowers/specs/2026-10-18-ai-auto-approve-button-
 * design.md section 3). `cell[k]` is the painted cell (full height of the
 * input rect). `hit[k]` is its click target: the cell's column, max(hit_h,
 * r.h) tall and centred on r -- or zero-size when the cell does not end at
 * or before `clip_right`, so a cell the Auto-approve button covers in part
 * neither takes a click nor shows a tooltip. `clip_right` is the exclusive
 * x past which nothing of the pill paints (ns_draw_policy() clips to it).
 * `total_w` is the pill's unclipped width, matching ns_policy_width(). */
typedef struct {
    NsRect cell[NS_POLICY_STOPS];
    NsRect hit[NS_POLICY_STOPS];
    int clip_right;
    int total_w;
} NsPolicyLayout;
```

```c
/* Lay out the policy pill inside `r` (cells span all of r.h). A cell k is
 * hittable when cell[k].x + cell[k].w <= clip_right. NULL-safe; a
 * non-positive r.h lays out zero-height cells without crashing. */
void ns_policy_layout(NsRect r, const int stop_text_w[NS_POLICY_STOPS],
                      int clip_right, int hit_h, int dpi,
                      NsPolicyLayout *out);

/* Hit-test a point against a laid-out pill. Returns HIT_POLICY_ALLOWED with
 * `*stop_out` set to the cell when (x, y) is inside that cell's hit rect,
 * otherwise HIT_NONE with `*stop_out = -1`. Zero-size (clipped) rects
 * never hit. NULL `l` and NULL `stop_out` are safe. */
int ns_policy_hit(const NsPolicyLayout *l, int x, int y, int *stop_out);
```

In `src/core/ns_layout.c`, replace `ns_policy_layout` and `ns_policy_hit` (lines 301-375) with:

```c
void ns_policy_layout(NsRect r, const int stop_text_w[NS_POLICY_STOPS],
                      int clip_right, int hit_h, int dpi,
                      NsPolicyLayout *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));

    int pad_sm = ns_scale(SP_SM, dpi);
    int H = (hit_h > r.h) ? hit_h : r.h;
    int hit_top = r.y + (r.h - H) / 2;

    int x = r.x;
    for (int k = 0; k < NS_POLICY_STOPS; k++) {
        int w = policy_cell_w(stop_text_w, k, pad_sm);

        out->cell[k].x = x;
        out->cell[k].y = r.y;
        out->cell[k].w = w;
        out->cell[k].h = r.h;

        if (x + w <= clip_right) {
            out->hit[k].x = x;
            out->hit[k].y = hit_top;
            out->hit[k].w = w;
            out->hit[k].h = H;
        }
        x += w;
    }
    out->total_w = x - r.x;
    out->clip_right = clip_right;
}

int ns_policy_hit(const NsPolicyLayout *l, int x, int y, int *stop_out)
{
    if (stop_out) *stop_out = -1;
    if (!l) return HIT_NONE;

    for (int k = 0; k < NS_POLICY_STOPS; k++) {
        if (point_in(l->hit[k], x, y)) {
            if (stop_out) *stop_out = k;
            return HIT_POLICY_ALLOWED;
        }
    }
    return HIT_NONE;
}
```

- [ ] **Step 4: Run the tests.** Run `make test 2>&1 | tail -3`. Expected: `Tests Run: 2973, Failed: 0`. The section had 20 tests and now has 19, so the total is 2974 − 20 + 19. The strict compile of `src/core/ns_layout.c` should print nothing.

- [ ] **Step 5: Commit.**

```bash
git add src/core/ns_layout.h src/core/ns_layout.c tests/test_ns_layout.c tests/runner.c
git status --short
git commit -F - <<'EOF'
feat(ui-core): make the policy pill one band per cell with a clip (v1.2.37)

ns_policy_layout() drops the rail bands and the rail fill and takes a
clip_right: a cell that does not end at or before it is unhittable, so a
cell the Auto-approve button covers in part takes no click. HIT_POLICY_AUTO
is appended after HIT_COPY; the pinned enum test covers it.

Models: claude-sonnet-5 (implementation); claude-opus-5-5 (plan)

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
```

---

### Task 5: `ai_status_layout` — five tiers, the button, the clip and the label rect

**Files**
- Modify: `src/core/ai_panel_layout.h` (the status-line comment, struct and prototype, lines 33-44)
- Modify: `src/core/ai_panel_layout.c` (`ai_status_layout`, lines 55-100; `ai_status_hit` appended after it)
- Test: `tests/test_ai_panel.c` (includes at lines 1-4; replace the `ai_status_layout` section, lines 175 up to the `/* ---- thinking_layout` banner)
- Test: `tests/runner.c` (declarations lines 2875-2881, calls lines 6071-6077)

**Interfaces**
- Consumes: `void ns_policy_layout(NsRect r, const int stop_text_w[NS_POLICY_STOPS], int clip_right, int hit_h, int dpi, NsPolicyLayout *out);`, `int ns_policy_hit(const NsPolicyLayout *l, int x, int y, int *stop_out);`, `HIT_POLICY_ALLOWED`, `HIT_POLICY_AUTO` (Task 4).
- Produces:

```c
typedef struct {
    int policy_full_w;     /* ns_policy_width() over the full labels */
    int policy_compact_w;  /* ns_policy_width() over "R" "U" "W" "C" */
    int auto_full_w;       /* text width of "Auto-approve" */
    int auto_compact_w;    /* text width of "Auto" */
    int meter_text_w;      /* text width of "used / limit"; 0 = no meter */
} AiStatusMeasure;
enum { AI_STATUS_TIER_FULL = 1, AI_STATUS_TIER_METER_TEXT = 2, AI_STATUS_TIER_NO_METER = 3,
       AI_STATUS_TIER_COMPACT = 4, AI_STATUS_TIER_CLIPPED = 5 };
typedef struct {
    NsRect policy;      /* pill rect: x = status.x, w = the chosen label set's width */
    int    policy_clip; /* exclusive right x for the pill: auto_btn.x - SP_XS */
    NsRect auto_btn;    /* right-aligned, SP_SM in from the row's right edge */
    NsRect meter_bar, meter_text;
    NsRect label;       /* busy label: visible pill right + SP_MD .. auto_btn.x - SP_SM; zero when < 1 px */
    int    compact;     /* 1 = R/U/W/C and "Auto" */
    int    tier;        /* AI_STATUS_TIER_* */
} AiStatusLayout;
void ai_status_layout(NsRect status, int dpi, const AiStatusMeasure *m, AiStatusLayout *out);
int  ai_status_hit(const AiStatusLayout *sl, const NsPolicyLayout *pl, int x, int y, int *stop_out);
     /* HIT_POLICY_AUTO, HIT_POLICY_ALLOWED (+ *stop_out), or HIT_NONE */
```

Tier rule. Here `sm = ns_scale(SP_SM)`, `md = ns_scale(SP_MD)`, `bar = ns_scale(60)`, `bf = auto_full_w + 2sm` and `bc = auto_compact_w + 2sm`:

| Tier | Condition |
|---|---|
| 1 | meter present and `w >= full + md + bar + sm + text + sm + bf + sm` |
| 2 | meter present and `w >= full + md + text + sm + bf + sm` |
| 3 | `w >= full + md + bf + sm` |
| 4 | `w >= compact + md + bc + sm` |
| 5 | otherwise |

- [ ] **Step 1: Write the failing tests.** In `tests/test_ai_panel.c`, add after `#include "ai_panel_states.h"`:

```c
#include "ns_scale.h"
#include "ns_type.h"
```

Replace the whole `ai_status_layout` section (from `/* ---- ai_status_layout ----` up to the `/* ---- thinking_layout` banner) with:

```c
/* ---- ai_status_layout (2026-10-18 auto-approve spec, section 3) ------------ */

/* Measured widths at 96 DPI, near the real caption font. Thresholds for
 * these (SP_SM 8, SP_MD 12, bar 60, button 72+16 / 28+16):
 *   tier 1 >= 200 + 12 + 60 + 8 + 64 + 8 + 88 + 8 = 448
 *   tier 2 >= 200 + 12 + 64 + 8 + 88 + 8           = 380
 *   tier 3 >= 200 + 12 + 88 + 8                    = 308
 *   tier 4 >=  80 + 12 + 44 + 8                    = 144 */
static const AiStatusMeasure STATUS_M96 = {
    .policy_full_w = 200, .policy_compact_w = 80,
    .auto_full_w = 72, .auto_compact_w = 28, .meter_text_w = 64
};

static AiStatusLayout status_at(int w, int dpi, const AiStatusMeasure *m)
{
    NsRect status = { 10, 4, w, ns_scale(SZ_CTRL_H, dpi) };
    AiStatusLayout out;
    ai_status_layout(status, dpi, m, &out);
    return out;
}

/* The pill as it is seen: its rect cut at policy_clip. */
static NsRect visible_pill(const AiStatusLayout *l)
{
    NsRect v = l->policy;
    if (v.x + v.w > l->policy_clip) v.w = l->policy_clip - v.x;
    if (v.w < 0) v.w = 0;
    return v;
}

static AiStatusMeasure scaled_measure(int dpi)
{
    AiStatusMeasure m;
    m.policy_full_w    = ns_scale(STATUS_M96.policy_full_w, dpi);
    m.policy_compact_w = ns_scale(STATUS_M96.policy_compact_w, dpi);
    m.auto_full_w      = ns_scale(STATUS_M96.auto_full_w, dpi);
    m.auto_compact_w   = ns_scale(STATUS_M96.auto_compact_w, dpi);
    m.meter_text_w     = ns_scale(STATUS_M96.meter_text_w, dpi);
    return m;
}

/* The four thresholds, from the rule in ai_panel_layout.h. */
static void status_thresholds(int dpi, const AiStatusMeasure *m, int t[4])
{
    int sm = ns_scale(SP_SM, dpi), md = ns_scale(SP_MD, dpi), bar = ns_scale(60, dpi);
    int bf = m->auto_full_w + 2 * sm, bc = m->auto_compact_w + 2 * sm;
    t[0] = m->policy_full_w + md + bar + sm + m->meter_text_w + sm + bf + sm;
    t[1] = m->policy_full_w + md + m->meter_text_w + sm + bf + sm;
    t[2] = m->policy_full_w + md + bf + sm;
    t[3] = m->policy_compact_w + md + bc + sm;
}

int test_ai_status_layout_tier_inside_each_band_96(void)
{
    TEST_BEGIN();
    static const int widths[5] = { 600, 400, 330, 200, 100 };
    for (int i = 0; i < 5; i++) {
        AiStatusLayout l = status_at(widths[i], 96, &STATUS_M96);
        ASSERT_EQ(l.tier, i + 1);
        ASSERT_EQ(l.compact, i >= 3 ? 1 : 0);
        ASSERT_EQ(l.meter_bar.w > 0, i == 0);
        ASSERT_EQ(l.meter_text.w > 0, i <= 1);
        ASSERT_EQ(l.policy.w, i >= 3 ? 80 : 200);
        ASSERT_EQ(l.auto_btn.w, i >= 3 ? 44 : 88);
    }
    TEST_END();
}

int test_ai_status_layout_tier1_geometry_96(void)
{
    TEST_BEGIN();
    AiStatusLayout l = status_at(600, 96, &STATUS_M96);
    /* Row {10, 4, 600, 28}: button 88 wide ending 8 in from 610. */
    ASSERT_EQ(l.auto_btn.x, 514);
    ASSERT_EQ(l.policy_clip, 510);
    ASSERT_EQ(l.meter_text.x, 442);    /* 514 - 8 - 64 */
    ASSERT_EQ(l.meter_bar.x, 374);     /* 442 - 8 - 60 */
    ASSERT_EQ(l.label.x, 222);         /* 10 + 200 + 12 */
    ASSERT_EQ(l.label.w, 284);         /* to 514 - 8 */
    ASSERT_EQ(l.policy.x, 10);
    ASSERT_EQ(l.policy.h, 20);         /* SZ_CTRL_H - 2 * SP_XS */
    ASSERT_EQ(l.policy.y, 8);
    ASSERT_EQ(l.auto_btn.y, l.policy.y);
    ASSERT_EQ(l.auto_btn.h, l.policy.h);
    TEST_END();
}

int test_ai_status_layout_thresholds_96(void)
{
    TEST_BEGIN();
    static const int t[4] = { 448, 380, 308, 144 };
    int calc[4];
    status_thresholds(96, &STATUS_M96, calc);
    for (int i = 0; i < 4; i++) {
        ASSERT_EQ(calc[i], t[i]);
        AiStatusLayout at = status_at(t[i], 96, &STATUS_M96);
        AiStatusLayout below = status_at(t[i] - 1, 96, &STATUS_M96);
        ASSERT_EQ(at.tier, i + 1);
        ASSERT_EQ(below.tier, i + 2);
    }
    TEST_END();
}

/* Review Focus 1: at 125 % and 150 % scaling every threshold still flips
 * the tier exactly. */
int test_ai_status_layout_thresholds_120_and_144(void)
{
    TEST_BEGIN();
    static const int dpis[2] = { 120, 144 };
    for (int di = 0; di < 2; di++) {
        int dpi = dpis[di];
        AiStatusMeasure m = scaled_measure(dpi);
        int t[4];
        status_thresholds(dpi, &m, t);
        for (int i = 0; i < 4; i++) {
            AiStatusLayout at = status_at(t[i], dpi, &m);
            AiStatusLayout below = status_at(t[i] - 1, dpi, &m);
            if (at.tier != i + 1 || below.tier != i + 2) {
                printf("  dpi %d threshold %d (%d px): tier %d at, %d below\n",
                       dpi, i, t[i], at.tier, below.tier);
                _tf_local_fail = 1;
            }
        }
    }
    TEST_END();
}

/* Review Focus 2: no context meter (no key, unknown model) -- tiers 1 and 2
 * cannot exist, and the busy label still ends before the button. */
int test_ai_status_layout_no_meter_goes_straight_to_tier3(void)
{
    TEST_BEGIN();
    AiStatusMeasure m = STATUS_M96;
    m.meter_text_w = 0;
    AiStatusLayout l = status_at(600, 96, &m);
    ASSERT_EQ(l.tier, AI_STATUS_TIER_NO_METER);
    ASSERT_EQ(l.compact, 0);
    ASSERT_EQ(l.meter_bar.w, 0);
    ASSERT_EQ(l.meter_text.w, 0);
    ASSERT_TRUE(l.label.w > 0);
    ASSERT_EQ(l.label.x + l.label.w, l.auto_btn.x - 8);
    ASSERT_EQ(status_at(308, 96, &m).tier, AI_STATUS_TIER_NO_METER);
    ASSERT_EQ(status_at(307, 96, &m).tier, AI_STATUS_TIER_COMPACT);
    TEST_END();
}

/* Review Focus 3: every width from 0 to 1400 at 96 and 192 DPI -- the
 * button keeps its rect at the right edge, the clip sits SP_XS left of
 * it, the pill (as seen), the button, the meter and the label never
 * overlap one another (label and meter are alternatives and may), and
 * widening the row never moves to a narrower tier. */
int test_ai_status_layout_width_sweep_invariants(void)
{
    TEST_BEGIN();
    static const int dpis[2] = { 96, 192 };
    for (int di = 0; di < 2; di++) {
        int dpi = dpis[di];
        AiStatusMeasure m = scaled_measure(dpi);
        int sm = ns_scale(SP_SM, dpi), xs = ns_scale(SP_XS, dpi);
        int prev_tier = AI_STATUS_TIER_CLIPPED;
        for (int w = 0; w <= 1400; w++) {
            NsRect status = { 10, 4, w, ns_scale(SZ_CTRL_H, dpi) };
            AiStatusLayout l;
            ai_status_layout(status, dpi, &m, &l);
            NsRect pill = visible_pill(&l);
            int bw = (l.compact ? m.auto_compact_w : m.auto_full_w) + 2 * sm;
            int bad = 0;
            if (l.tier < 1 || l.tier > 5 || l.tier > prev_tier) bad = 1;
            if (l.compact != (l.tier >= AI_STATUS_TIER_COMPACT)) bad = 1;
            if (l.auto_btn.w != bw) bad = 1;
            if (l.auto_btn.x + l.auto_btn.w != status.x + w - sm) bad = 1;
            if (l.policy_clip != l.auto_btn.x - xs) bad = 1;
            if (l.label.w < 0) bad = 1;
            if (l.label.w > 0 && l.label.x + l.label.w != l.auto_btn.x - sm) bad = 1;
            if (rects_overlap(pill, l.auto_btn) || rects_overlap(pill, l.meter_bar) ||
                rects_overlap(pill, l.meter_text) || rects_overlap(pill, l.label) ||
                rects_overlap(l.auto_btn, l.meter_bar) ||
                rects_overlap(l.auto_btn, l.meter_text) ||
                rects_overlap(l.auto_btn, l.label) ||
                rects_overlap(l.meter_bar, l.meter_text)) bad = 1;
            if (!rect_inside(l.meter_bar, status) || !rect_inside(l.meter_text, status) ||
                !rect_inside(l.label, status)) bad = 1;
            if (bad) {
                printf("  dpi %d w %d: tier %d compact %d btn {%d,%d} clip %d "
                       "pill {%d,%d} label {%d,%d}\n", dpi, w, l.tier, l.compact,
                       l.auto_btn.x, l.auto_btn.w, l.policy_clip, pill.x, pill.w,
                       l.label.x, l.label.w);
                _tf_local_fail = 1;
                break;
            }
            prev_tier = l.tier;
        }
    }
    TEST_END();
}

int test_ai_status_layout_negative_and_null_inputs(void)
{
    TEST_BEGIN();
    NsRect status = { 0, 0, 600, 28 };
    AiStatusMeasure bad = { -40, -10, -5, -5, -10 };
    AiStatusLayout out;
    ai_status_layout(status, 96, &bad, &out);
    ASSERT_EQ(out.policy.w, 0);
    ASSERT_EQ(out.meter_text.w, 0);
    ASSERT_EQ(out.meter_bar.w, 0);
    ASSERT_EQ(out.auto_btn.w, 16);                 /* 0 + 2 * SP_SM */
    ai_status_layout(status, 96, NULL, &out);      /* NULL measure = all zero */
    ASSERT_EQ(out.tier, AI_STATUS_TIER_NO_METER);
    ai_status_layout(status, 96, &STATUS_M96, NULL);   /* must not crash */
    NsRect neg = { 0, 0, -50, 28 };
    ai_status_layout(neg, 96, &STATUS_M96, &out);
    ASSERT_EQ(out.tier, AI_STATUS_TIER_CLIPPED);
    ASSERT_EQ(out.label.w, 0);
    TEST_END();
}

int test_ai_status_hit_cells_button_and_clipped_cell(void)
{
    TEST_BEGIN();
    int full_text[NS_POLICY_STOPS]  = { 40, 30, 50, 60 };  /* cells 56 46 66 76 */
    int short_text[NS_POLICY_STOPS] = { 8, 8, 8, 8 };      /* cells 24 each */
    AiStatusMeasure m = STATUS_M96;
    m.policy_full_w = ns_policy_width(full_text, 96);      /* 244 */
    m.policy_compact_w = ns_policy_width(short_text, 96);  /* 96 */

    NsRect wide = { 10, 4, 600, 28 };
    AiStatusLayout sl;
    NsPolicyLayout pl;
    ai_status_layout(wide, 96, &m, &sl);
    ns_policy_layout(sl.policy, full_text, sl.policy_clip, wide.h, 96, &pl);
    for (int k = 0; k < NS_POLICY_STOPS; k++) {
        int stop = -99;
        NsRect c = pl.cell[k];
        ASSERT_EQ(ai_status_hit(&sl, &pl, c.x + c.w / 2, c.y + c.h / 2, &stop), HIT_POLICY_ALLOWED);
        ASSERT_EQ(stop, k);
    }
    int stop = -99;
    NsRect b = sl.auto_btn;
    ASSERT_EQ(ai_status_hit(&sl, &pl, b.x + b.w / 2, b.y + b.h / 2, &stop), HIT_POLICY_AUTO);
    ASSERT_EQ(stop, -1);
    ASSERT_EQ(ai_status_hit(&sl, &pl, pl.cell[3].x + pl.cell[3].w + 2, 18, &stop), HIT_NONE);

    /* Tier 5: row 100 wide, button at 58..102, clip 54. Compact cell 0 is
     * 10..34 (whole), cell 1 is 34..58 (cut at 54: unhittable). */
    NsRect narrow = { 10, 4, 100, 28 };
    ai_status_layout(narrow, 96, &m, &sl);
    ASSERT_EQ(sl.tier, AI_STATUS_TIER_CLIPPED);
    ns_policy_layout(sl.policy, short_text, sl.policy_clip, narrow.h, 96, &pl);
    stop = -99;
    ASSERT_EQ(ai_status_hit(&sl, &pl, 20, 18, &stop), HIT_POLICY_ALLOWED);
    ASSERT_EQ(stop, 0);
    stop = -99;
    ASSERT_EQ(ai_status_hit(&sl, &pl, 40, 18, &stop), HIT_NONE);
    ASSERT_EQ(stop, -1);
    b = sl.auto_btn;
    ASSERT_EQ(ai_status_hit(&sl, &pl, b.x + b.w / 2, b.y + b.h / 2, NULL), HIT_POLICY_AUTO);

    stop = -99;
    ASSERT_EQ(ai_status_hit(NULL, NULL, 0, 0, &stop), HIT_NONE);
    ASSERT_EQ(stop, -1);
    TEST_END();
}
```

In `tests/runner.c`, replace the declaration block (lines 2875-2881) with:

```c
int test_ai_status_layout_tier_inside_each_band_96(void);
int test_ai_status_layout_tier1_geometry_96(void);
int test_ai_status_layout_thresholds_96(void);
int test_ai_status_layout_thresholds_120_and_144(void);
int test_ai_status_layout_no_meter_goes_straight_to_tier3(void);
int test_ai_status_layout_width_sweep_invariants(void);
int test_ai_status_layout_negative_and_null_inputs(void);
int test_ai_status_hit_cells_button_and_clipped_cell(void);
```

and the call block (lines 6071-6077) with:

```c
    failed += test_ai_status_layout_tier_inside_each_band_96();
    failed += test_ai_status_layout_tier1_geometry_96();
    failed += test_ai_status_layout_thresholds_96();
    failed += test_ai_status_layout_thresholds_120_and_144();
    failed += test_ai_status_layout_no_meter_goes_straight_to_tier3();
    failed += test_ai_status_layout_width_sweep_invariants();
    failed += test_ai_status_layout_negative_and_null_inputs();
    failed += test_ai_status_hit_cells_button_and_clipped_cell();
```

Then run `sed -i 's/\r*$/\r/' tests/runner.c`.

- [ ] **Step 2: Run the tests and see them fail.** Run `make test 2>&1 | tail -20`. Expected: compile errors in `test_ai_panel.c`, `unknown type name 'AiStatusMeasure'` and `'AI_STATUS_TIER_NO_METER' undeclared`.

- [ ] **Step 3: Implement.** In `src/core/ai_panel_layout.h`, replace lines 33-44 (from `/* Status-line controls, left to right:` through the `ai_status_layout` prototype) with:

```c
/* Status-line controls, left to right (docs/superpowers/specs/
 * 2026-10-18-ai-auto-approve-button-design.md section 3): the policy pill
 * (sliced by ns_policy_layout() -- this module only places it), free
 * space, the context meter (bar + "used / limit" text) or the busy label,
 * and the Auto-approve button at the right edge. The caller measures both
 * label sets; this picks one of five tiers as the row narrows:
 *   1 full labels, meter bar and text   2 full labels, meter text only
 *   3 full labels, no meter             4 compact labels R U W C / "Auto"
 *   5 compact, and the pill clipped at the button's left edge - SP_XS.
 * With sm = SP_SM, md = SP_MD, bar = 60 px, bf/bc = button text + 2*sm
 * (all through ns_scale): tier 1 needs full + md + bar + sm + text + sm +
 * bf + sm; tier 2 full + md + text + sm + bf + sm; tier 3 full + md + bf +
 * sm; tier 4 compact + md + bc + sm. No meter (text 0) skips 1 and 2. */
typedef struct {
    int policy_full_w;     /* ns_policy_width() over the full labels */
    int policy_compact_w;  /* ns_policy_width() over "R" "U" "W" "C" */
    int auto_full_w;       /* text width of "Auto-approve" */
    int auto_compact_w;    /* text width of "Auto" */
    int meter_text_w;      /* text width of "used / limit"; 0 = no meter */
} AiStatusMeasure;

enum {
    AI_STATUS_TIER_FULL = 1, AI_STATUS_TIER_METER_TEXT = 2,
    AI_STATUS_TIER_NO_METER = 3, AI_STATUS_TIER_COMPACT = 4,
    AI_STATUS_TIER_CLIPPED = 5
};

/* `policy` is the pill: x = status.x, w = the chosen label set's width (it
 * may run past `policy_clip` in tier 5; nothing paints or hits there).
 * `auto_btn` ends SP_SM in from the row's right edge in every tier. The
 * meter rects end at auto_btn.x - SP_SM. `label` is the busy label's
 * rect: from the visible pill's right edge + SP_MD to auto_btn.x - SP_SM,
 * zero-size when that is under 1 px. ai_chat.c and ns_draw.c compute no
 * rect of their own. */
typedef struct {
    NsRect policy;
    int    policy_clip;
    NsRect auto_btn;
    NsRect meter_bar, meter_text;
    NsRect label;
    int    compact;
    int    tier;
} AiStatusLayout;

/* NULL `m` is all zero; negative widths count as 0. NULL `out` is a no-op. */
void ai_status_layout(NsRect status, int dpi, const AiStatusMeasure *m,
                       AiStatusLayout *out);

/* Hit-test the status line: HIT_POLICY_AUTO inside sl->auto_btn,
 * otherwise ns_policy_hit() on `pl` (HIT_POLICY_ALLOWED with *stop_out, or
 * HIT_NONE). `*stop_out` is -1 unless a cell was hit. NULL-safe. */
int ai_status_hit(const AiStatusLayout *sl, const NsPolicyLayout *pl,
                  int x, int y, int *stop_out);
```

In `src/core/ai_panel_layout.c`, replace `ai_status_layout` (lines 55-100) with:

```c
static int nonneg(int v) { return v > 0 ? v : 0; }

void ai_status_layout(NsRect status, int dpi, const AiStatusMeasure *m,
                       AiStatusLayout *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));

    int full_w = m ? nonneg(m->policy_full_w) : 0;
    int comp_w = m ? nonneg(m->policy_compact_w) : 0;
    int afull  = m ? nonneg(m->auto_full_w) : 0;
    int acomp  = m ? nonneg(m->auto_compact_w) : 0;
    int text_w = m ? nonneg(m->meter_text_w) : 0;

    int pad_xs = ns_scale(SP_XS, dpi);
    int pad_sm = ns_scale(SP_SM, dpi);
    int gap_md = ns_scale(SP_MD, dpi);
    int bar_w  = ns_scale(60, dpi);
    int seg_h  = ns_scale(SZ_CTRL_H, dpi) - 2 * pad_xs;
    if (seg_h < 0) seg_h = 0;
    int seg_y  = status.y + (status.h - seg_h) / 2;
    int w      = nonneg(status.w);

    int btn_full = afull + 2 * pad_sm;
    int btn_comp = acomp + 2 * pad_sm;
    int need_full    = full_w + gap_md + bar_w + pad_sm + text_w + pad_sm + btn_full + pad_sm;
    int need_text    = full_w + gap_md + text_w + pad_sm + btn_full + pad_sm;
    int need_nometer = full_w + gap_md + btn_full + pad_sm;
    int need_comp    = comp_w + gap_md + btn_comp + pad_sm;

    int tier;
    if (text_w > 0 && w >= need_full)      tier = AI_STATUS_TIER_FULL;
    else if (text_w > 0 && w >= need_text) tier = AI_STATUS_TIER_METER_TEXT;
    else if (w >= need_nometer)            tier = AI_STATUS_TIER_NO_METER;
    else if (w >= need_comp)               tier = AI_STATUS_TIER_COMPACT;
    else                                   tier = AI_STATUS_TIER_CLIPPED;
    out->tier = tier;
    out->compact = (tier >= AI_STATUS_TIER_COMPACT) ? 1 : 0;

    int btn_w = out->compact ? btn_comp : btn_full;
    int right = status.x + w;
    out->auto_btn.x = right - pad_sm - btn_w;
    out->auto_btn.y = seg_y;
    out->auto_btn.w = btn_w;
    out->auto_btn.h = seg_h;

    out->policy.x = status.x;
    out->policy.y = seg_y;
    out->policy.w = out->compact ? comp_w : full_w;
    out->policy.h = seg_h;
    out->policy_clip = out->auto_btn.x - pad_xs;

    int meter_right = out->auto_btn.x - pad_sm;
    if (tier <= AI_STATUS_TIER_METER_TEXT) {
        out->meter_text.x = meter_right - text_w;
        out->meter_text.y = seg_y;
        out->meter_text.w = text_w;
        out->meter_text.h = seg_h;
        if (tier == AI_STATUS_TIER_FULL) {
            out->meter_bar.x = out->meter_text.x - pad_sm - bar_w;
            out->meter_bar.y = seg_y;
            out->meter_bar.w = bar_w;
            out->meter_bar.h = seg_h;
        }
    }

    int pill_right = out->policy.x + out->policy.w;
    if (pill_right > out->policy_clip) pill_right = out->policy_clip;
    int label_x = pill_right + gap_md;
    if (meter_right - label_x > 0) {
        out->label.x = label_x;
        out->label.y = seg_y;
        out->label.w = meter_right - label_x;
        out->label.h = seg_h;
    }
}

int ai_status_hit(const AiStatusLayout *sl, const NsPolicyLayout *pl,
                  int x, int y, int *stop_out)
{
    if (stop_out) *stop_out = -1;
    if (sl) {
        NsRect b = sl->auto_btn;
        if (b.w > 0 && b.h > 0 && x >= b.x && x < b.x + b.w &&
            y >= b.y && y < b.y + b.h)
            return HIT_POLICY_AUTO;
    }
    return ns_policy_hit(pl, x, y, stop_out);
}
```

`ai_panel_layout.c` already includes `ns_scale.h` and `ns_type.h`, which this code needs.

- [ ] **Step 4: Run the tests.** Run `make test 2>&1 | tail -3`. Expected: `Tests Run: 2974, Failed: 0` (2973 − 7 + 8). The strict compile of `src/core/ai_panel_layout.c` should print nothing.

- [ ] **Step 5: Commit.**

```bash
git add src/core/ai_panel_layout.h src/core/ai_panel_layout.c tests/test_ai_panel.c tests/runner.c
git status --short
git commit -F - <<'EOF'
feat(ui-core): lay out the status line in five tiers with an Auto-approve button (v1.2.37)

ai_status_layout() takes both measured label sets and returns the pill,
its clip, the button at the right edge, the meter and the busy label's
rect, and the tier. ai_status_hit() maps the button and the cells. Tests
cover every tier, both sides of each threshold at 96/120/144 DPI, the
no-meter case and a 0-1400 px sweep for overlaps.

Models: claude-sonnet-5 (implementation); claude-opus-5-5 (plan)

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
```

---

### Task 6: Loader — the token on disk after the change

**Files**
- Modify: `src/config/loader.c` (comment above the `ai_policy_default` read, lines 752-775)
- Test: `tests/test_config.c` (append after `test_config_load_inverted_policy_token_clamps_down`, which ends near line 1352)
- Test: `tests/runner.c` (declarations after line 614, calls after line 3887)

**Interfaces**
- Consumes: `int cmd_policy_from_token(const char *token, CmdPolicy *out);` and `int cmd_policy_to_token(CmdPolicy p, char *buf, size_t cap);`. Since Task 1 both normalise through `cmd_policy_clamp`.
- Produces: no new symbols. The on-disk contract is pinned by four tests.

- [ ] **Step 1: Write the tests.** Append to `tests/test_config.c`:

```c
/* ============================================================
 * ai_policy_default after the Auto-approve button
 * (2026-10-18-ai-auto-approve-button-design.md section 2)
 * ============================================================ */

static Config *load_policy_token(const char *token)
{
    char json[256];
    snprintf(json, sizeof(json),
             "{\"settings\": {\"font\": \"Consolas\", "
             "\"ai_policy_default\": \"%s\"}, \"profiles\": []}", token);
    FILE *f = test_fopen_private(TMP_CFG);
    if (!f) return NULL;
    fputs(json, f);
    fclose(f);
    return config_load(TMP_CFG);
}

int test_config_load_split_policy_token_turns_auto_off(void)
{
    TEST_BEGIN();
    Config *cfg = load_policy_token("write/read");   /* an older exe's split */
    ASSERT_NOT_NULL(cfg);
    if (cfg) {
        ASSERT_EQ(cfg->settings.ai_policy_default.allowed, (int)CMD_WRITE);
        ASSERT_EQ(cfg->settings.ai_policy_default.unattended, POLICY_NONE);
        config_free(cfg);
    }
    remove(TMP_CFG);
    TEST_END();
}

int test_config_load_matched_policy_token_is_auto_on(void)
{
    TEST_BEGIN();
    Config *cfg = load_policy_token("write/write");
    ASSERT_NOT_NULL(cfg);
    if (cfg) {
        ASSERT_EQ(cfg->settings.ai_policy_default.allowed, (int)CMD_WRITE);
        ASSERT_EQ(cfg->settings.ai_policy_default.unattended, (int)CMD_WRITE);
        config_free(cfg);
    }
    remove(TMP_CFG);
    TEST_END();
}

int test_config_load_read_none_policy_token_unchanged(void)
{
    TEST_BEGIN();
    Config *cfg = load_policy_token("read/none");
    ASSERT_NOT_NULL(cfg);
    if (cfg) {
        ASSERT_EQ(cfg->settings.ai_policy_default.allowed, (int)CMD_READ);
        ASSERT_EQ(cfg->settings.ai_policy_default.unattended, POLICY_NONE);
        config_free(cfg);
    }
    remove(TMP_CFG);
    TEST_END();
}

int test_config_save_after_load_writes_the_normalised_token(void)
{
    TEST_BEGIN();
    Config *cfg = load_policy_token("critical/unknown");
    ASSERT_NOT_NULL(cfg);
    if (cfg) {
        ASSERT_EQ(config_save(cfg, TMP_CFG), 0);
        config_free(cfg);
        FILE *f = fopen(TMP_CFG, "r");
        ASSERT_NOT_NULL(f);
        if (f) {
            char buf[8192];
            size_t n = fread(buf, 1, sizeof(buf) - 1, f);
            buf[n] = '\0';
            fclose(f);
            ASSERT_TRUE(strstr(buf, "\"ai_policy_default\": \"critical/none\"") != NULL);
            ASSERT_NULL(strstr(buf, "critical/unknown"));
        }
    }
    remove(TMP_CFG);
    TEST_END();
}
```

In `tests/runner.c`, after `int test_config_load_inverted_policy_token_clamps_down(void);` (line 614):

```c
int test_config_load_split_policy_token_turns_auto_off(void);
int test_config_load_matched_policy_token_is_auto_on(void);
int test_config_load_read_none_policy_token_unchanged(void);
int test_config_save_after_load_writes_the_normalised_token(void);
```

After `    failed += test_config_load_inverted_policy_token_clamps_down();` (line 3887):

```c
    failed += test_config_load_split_policy_token_turns_auto_off();
    failed += test_config_load_matched_policy_token_is_auto_on();
    failed += test_config_load_read_none_policy_token_unchanged();
    failed += test_config_save_after_load_writes_the_normalised_token();
```

Then run `sed -i 's/\r*$/\r/' tests/runner.c`.

- [ ] **Step 2: Run the tests.** Run `make test 2>&1 | grep -E "test_config_(load_split|load_matched|load_read_none|save_after_load)|Tests Run"`. Expected: all four `[PASS]`, because Task 1 already normalises inside `cmd_policy_from_token`. This task pins the on-disk contract. To prove the tests can fail, temporarily delete the split-rounding `if` in `cmd_policy_clamp` (`src/core/cmd_policy.c`) and rerun. `test_config_load_split_policy_token_turns_auto_off` and `test_config_save_after_load_writes_the_normalised_token` must then fail. Restore the line with `git checkout src/core/cmd_policy.c`.

- [ ] **Step 3: Implement (comment only).** In `src/config/loader.c`, add to the end of the comment block above `if ((sv = json_obj_str(jset, "ai_policy_default")))` (just before its closing `*/`, line 775):

```c
         *
         * Since 2026-10-18 (2026-10-18-ai-auto-approve-button-design.md
         * section 2) cmd_policy_from_token() rounds a split marker down:
         * an older exe's "write/read" loads as "write/none" (Auto-approve
         * off) and is written back that way on the next save. Lossy and
         * one-way, by the maintainer's decision. The legacy migrations
         * above land on "read/none" or "read/read", both legal.
```

- [ ] **Step 4: Run the tests.** Run `make test 2>&1 | tail -3`. Expected: `Tests Run: 2978, Failed: 0`. The strict compile of `src/config/loader.c` should print nothing.

- [ ] **Step 5: Commit.**

```bash
git add src/config/loader.c tests/test_config.c tests/runner.c
git status --short
git commit -F - <<'EOF'
test(config): pin the policy token's on-disk form after the Auto-approve change (v1.2.37)

A split token from an older exe loads with Auto-approve off and is saved
back normalised; a matched pair loads with it on; read/none is unchanged.

Models: claude-sonnet-5 (implementation); claude-opus-5-5 (plan)

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
```

---

### Task 7: `--ui-demo` states `policy-auto` and `policy-compact`

**Files**
- Modify: `src/core/ui_demo.h`: the comment at lines 23-34, and new prototypes after `ui_demo_states` (line 69).
- Modify: `src/core/ui_demo.c`: `STATE_NAMES` (lines 13-17), `ui_demo_build` dispatch (after the `batches` line, ~line 539), new functions at the end of the file.
- Test: `tests/test_ui_demo.c`: `test_ui_demo_states_list_ends_in_all` (lines 31-44), includes at lines 1-4, new tests appended.
- Test: `tests/runner.c`: declarations after line 3190, calls after line 6407.

**Interfaces**
- Consumes: `void cmd_policy_set_allowed(...)`, `void cmd_policy_set_auto(...)`, `void cmd_policy_clamp(...)` (Task 1); `void ai_status_layout(...)`, `AiStatusMeasure`, `AI_STATUS_TIER_COMPACT` (Task 5); `int ns_scale(int px, int dpi);`.
- Produces:
  - `CmdPolicy ui_demo_policy(const char *state);`
  - `int ui_demo_panel_width_px(const char *state, int dpi);`
  - State names `"policy-auto"` and `"policy-compact"`. Neither is part of `"all"`.

- [ ] **Step 1: Write the failing tests.** In `tests/test_ui_demo.c`, add after `#include "chat_msg.h"`:

```c
#include "ai_panel_layout.h"
#include "ns_scale.h"
```

Replace `test_ui_demo_states_list_ends_in_all` with:

```c
int test_ui_demo_states_list_ends_in_all(void)
{
    TEST_BEGIN();
    int count = 0;
    const char *const *states = ui_demo_states(&count);
    ASSERT_NOT_NULL(states);
    ASSERT_EQ(count, 16);
    ASSERT_STR_EQ(states[count - 1], "all");
    ASSERT_STR_EQ(states[count - 2], "policy-compact");
    ASSERT_STR_EQ(states[count - 3], "policy-auto");
    ASSERT_STR_EQ(states[count - 4], "settings-debug");
    ASSERT_STR_EQ(states[count - 5], "cmddetail");
    ASSERT_STR_EQ(states[count - 6], "thinking");
    ASSERT_STR_EQ(states[count - 7], "local");
    TEST_END();
}
```

Append:

```c
/* 2026-10-18 auto-approve: "policy-auto" is "executing" (a running card)
 * with the demo tab's policy at Write and Auto-approve on. */
int test_ui_demo_policy_auto_is_executing_with_auto_on(void)
{
    TEST_BEGIN();
    AiConversation conv, conv2;
    ApprovalQueue approval, approval2, ea, ea2;
    ASSERT_TRUE(ui_demo_state_valid("policy-auto"));
    ASSERT_EQ(ui_demo_build("policy-auto", &conv, &approval, &approval2, NULL, 0), 0);
    ASSERT_EQ(ui_demo_build("executing", &conv2, &ea, &ea2, NULL, 0), 0);
    ASSERT_EQ(conv.msg_count, conv2.msg_count);
    ASSERT_EQ(approval.count, ea.count);
    ASSERT_EQ(count_status(&approval, APPROVE_EXECUTING), 1);
    ASSERT_EQ(approval2.count, 0);
    CmdPolicy p = ui_demo_policy("policy-auto");
    ASSERT_EQ(p.allowed, (int)CMD_WRITE);
    ASSERT_EQ(p.unattended, (int)CMD_WRITE);
    ASSERT_TRUE(!ui_demo_scrolls_to_top("policy-auto"));
    TEST_END();
}

/* "policy-compact" is "approval" in a panel narrow enough for R U W C. */
int test_ui_demo_policy_compact_matches_approval(void)
{
    TEST_BEGIN();
    AiConversation conv, conv2;
    ApprovalQueue approval, approval2, aa, aa2;
    ASSERT_TRUE(ui_demo_state_valid("policy-compact"));
    ASSERT_EQ(ui_demo_build("policy-compact", &conv, &approval, &approval2, NULL, 0), 0);
    ASSERT_EQ(ui_demo_build("approval", &conv2, &aa, &aa2, NULL, 0), 0);
    ASSERT_EQ(conv.msg_count, conv2.msg_count);
    ASSERT_EQ(approval.count, aa.count);
    ASSERT_TRUE(!ui_demo_scrolls_to_top("policy-compact"));
    TEST_END();
}

int test_ui_demo_policy_for_other_states_is_write_off(void)
{
    TEST_BEGIN();
    static const char *const others[4] = { "chat", "approval", "all", "policy-compact" };
    for (int i = 0; i < 4; i++) {
        CmdPolicy p = ui_demo_policy(others[i]);
        ASSERT_EQ(p.allowed, (int)CMD_WRITE);
        ASSERT_EQ(p.unattended, POLICY_NONE);
    }
    CmdPolicy n = ui_demo_policy(NULL);
    ASSERT_EQ(n.allowed, (int)CMD_WRITE);
    ASSERT_EQ(n.unattended, POLICY_NONE);
    TEST_END();
}

int test_ui_demo_panel_width_only_for_policy_compact(void)
{
    TEST_BEGIN();
    ASSERT_EQ(ui_demo_panel_width_px("policy-compact", 96), 260);
    ASSERT_EQ(ui_demo_panel_width_px("policy-compact", 192), 520);
    ASSERT_EQ(ui_demo_panel_width_px("policy-auto", 96), 0);
    ASSERT_EQ(ui_demo_panel_width_px("all", 96), 0);
    ASSERT_EQ(ui_demo_panel_width_px(NULL, 96), 0);
    /* With caption-font widths measured near enough at 96 DPI (full pill
     * 200, compact 96, "Auto-approve" 68, "Auto" 24, no meter), that width
     * lands on the compact tier, not the clipped one. */
    AiStatusMeasure m = { 200, 96, 68, 24, 0 };
    NsRect status = { 0, 0, ui_demo_panel_width_px("policy-compact", 96), 28 };
    AiStatusLayout l;
    ai_status_layout(status, 96, &m, &l);
    ASSERT_EQ(l.compact, 1);
    ASSERT_EQ(l.tier, AI_STATUS_TIER_COMPACT);
    TEST_END();
}
```

In `tests/runner.c`, after `int test_ui_demo_settings_debug_is_an_empty_state(void);` (line 3190):

```c
int test_ui_demo_policy_auto_is_executing_with_auto_on(void);
int test_ui_demo_policy_compact_matches_approval(void);
int test_ui_demo_policy_for_other_states_is_write_off(void);
int test_ui_demo_panel_width_only_for_policy_compact(void);
```

After `    failed += test_ui_demo_settings_debug_is_an_empty_state();` (line 6407):

```c
    failed += test_ui_demo_policy_auto_is_executing_with_auto_on();
    failed += test_ui_demo_policy_compact_matches_approval();
    failed += test_ui_demo_policy_for_other_states_is_write_off();
    failed += test_ui_demo_panel_width_only_for_policy_compact();
```

Then run `sed -i 's/\r*$/\r/' tests/runner.c`.

- [ ] **Step 2: Run the tests and see them fail.** Run `make test 2>&1 | tail -20`. Expected: `undefined reference to 'ui_demo_policy'` and `'ui_demo_panel_width_px'`.

- [ ] **Step 3: Implement.** In `src/core/ui_demo.c`, change `STATE_NAMES` to:

```c
static const char *const STATE_NAMES[] = {
    "chat", "approval", "executing", "tool", "error", "empty",
    "nokey", "nosession", "batches", "local", "thinking", "cmddetail",
    "settings-debug", "policy-auto", "policy-compact", "all"
};
```

and update the comment above it to read `... (not "local", "thinking", "cmddetail", "settings-debug", "policy-auto" or "policy-compact", which reuse or swap content).` In `ui_demo_build`, after `if (is_all || strcmp(state, "batches") == 0) build_batches(conv, approval, approval2);` add:

```c
    /* "policy-auto" (2026-10-18) is "executing" -- a running card -- and the
     * win32 side seeds the tab's policy from ui_demo_policy(): Write with
     * Auto-approve on, so the pill's solid cells and the filled button
     * show. "policy-compact" is "approval" in a panel ui_demo_panel_width_px()
     * makes narrow enough for R U W C and "Auto". Neither is part of "all". */
    if (strcmp(state, "policy-auto") == 0)
        build_executing(conv, approval);
    if (strcmp(state, "policy-compact") == 0)
        build_approval(conv, approval);
```

Append to the end of `src/core/ui_demo.c`:

```c
CmdPolicy ui_demo_policy(const char *state)
{
    CmdPolicy p = cmd_policy_default();
    cmd_policy_set_allowed(&p, CMD_WRITE);
    if (state && strcmp(state, "policy-auto") == 0)
        cmd_policy_set_auto(&p, 1);
    cmd_policy_clamp(&p);
    return p;
}

int ui_demo_panel_width_px(const char *state, int dpi)
{
    if (state && strcmp(state, "policy-compact") == 0)
        return ns_scale(260, dpi);
    return 0;
}
```

Add `#include "ns_scale.h"` after `#include "ai_agentic.h"` in `src/core/ui_demo.c`; it is not included yet. In `src/core/ui_demo.h`, extend the state comment (lines 23-34) with: `"policy-auto" and "policy-compact" (2026-10-18, auto-approve button) reuse "executing" and "approval"; the win32 side seeds the tab's policy from ui_demo_policy() and, for "policy-compact", docks the panel at ui_demo_panel_width_px(). Neither is part of "all".` After the `ui_demo_states` prototype add:

```c
/* The policy the demo tab starts with: Write with Auto-approve on for
 * "policy-auto", Write with it off for every other state and for NULL --
 * so the gallery shows tinted, solid and blocked cells. Always clamped. */
CmdPolicy ui_demo_policy(const char *state);

/* Docked panel width in device pixels for `state` at `dpi`, or 0 for "use
 * the normal default". Only "policy-compact" has one: ns_scale(260, dpi),
 * narrow enough for the status line's compact labels. */
int ui_demo_panel_width_px(const char *state, int dpi);
```

- [ ] **Step 4: Run the tests.** Run `make test 2>&1 | tail -3`. Expected: `Tests Run: 2982, Failed: 0`. `test_ui_demo_build_all_is_union` still passes at 17 messages and 9 + 2 entries, which proves neither new state leaks into `"all"`. The `test_cli_ui_demo_*` tests stay green because `cli_args.c` validates through `ui_demo_state_valid()`. The strict compile of `src/core/ui_demo.c` should print nothing.

- [ ] **Step 5: Commit.**

```bash
git add src/core/ui_demo.h src/core/ui_demo.c tests/test_ui_demo.c tests/runner.c
git status --short
git commit -F - <<'EOF'
feat(demo): add policy-auto and policy-compact demo states (v1.2.37)

policy-auto is a running card with the tab at Write and Auto-approve on;
policy-compact is the approval card in a panel narrow enough for R U W C.
ui_demo_policy() and ui_demo_panel_width_px() give window.c what to seed.

Models: claude-sonnet-5 (implementation); claude-opus-5-5 (plan)

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
```

---

### Task 8: `ns_draw` — the clipped pill and the Auto-approve button

**Files**
- Modify: `src/ui/ns_draw.h` (`ns_draw_policy` comment and prototype, lines 101-122; new prototype after it)
- Modify: `src/ui/ns_draw.c` (`ns_draw_policy`, lines 348-435; new `ns_draw_auto_button` after it)

**Interfaces**
- Consumes: `NsPolicyLayout` with `cell[]`, `clip_right` and `total_w` (Task 4); `int cmd_policy_auto_on(CmdPolicy p);` (Task 1); `const char *cmd_policy_stop_short(int stop);` and `const char *cmd_policy_auto_label(int compact);` (Task 3); `int ns_type_pill(int height);`.
- Produces:

```c
void ns_draw_policy(HDC hdc, const NsPolicyLayout *l, CmdPolicy policy,
                    int compact, int hover_stop, const ThemeTokens *tokens,
                    HFONT font, int dpi);
void ns_draw_auto_button(HDC hdc, const NsRect *r, CmdPolicy policy,
                         int compact, int hover, const ThemeTokens *tokens,
                         HFONT font, int dpi);
```

This task cannot be compiled natively (`src/ui` is excluded). There is no failing native test. The check is the native suite staying green plus an object compile where MinGW exists.

- [ ] **Step 1: Replace the header declaration.** In `src/ui/ns_draw.h`, replace the `ns_draw_policy` comment and prototype with:

```c
/* The AI status line's policy pill: four touching cells (Read / Unknown /
 * Write / Critical, or R / U / W / C when `compact`), laid out by
 * ns_policy_layout(). See
 * docs/superpowers/specs/2026-10-18-ai-auto-approve-button-design.md s3.
 *
 * The intent surface follows `policy.allowed` (Read -> success, Unknown ->
 * info, Write -> warning, Critical -> danger). Cells at or below the level
 * are a wash of the intent over bg_secondary with text_main labels -- or,
 * with Auto-approve on, solid intent .base with .label text, so the pill
 * itself says "these run on their own". Cells above the level are plain
 * bg_secondary with text_disabled labels. `hover_stop` (-1 for none) steps
 * that cell one state brighter. Everything paints inside
 * IntersectClipRect(..., l->clip_right, ...), so a cell the Auto-approve
 * button covers in part never paints under it. Radius ns_type_pill(),
 * stroke STROKE_HAIRLINE through ns_scale(). */
void ns_draw_policy(HDC hdc, const NsPolicyLayout *l, CmdPolicy policy,
                    int compact, int hover_stop, const ThemeTokens *tokens,
                    HFONT font, int dpi);

/* The Auto-approve button: a pill the pill's height. Off: bg_secondary
 * (.hover when `hover`) outlined in `border`, label in text_dim. On: the
 * level's intent .base (.hover when `hover`) with the intent's .label
 * text. Label "Auto-approve", or "Auto" when `compact`. No pressed state. */
void ns_draw_auto_button(HDC hdc, const NsRect *r, CmdPolicy policy,
                         int compact, int hover, const ThemeTokens *tokens,
                         HFONT font, int dpi);
```

- [ ] **Step 2: Replace the painter.** In `src/ui/ns_draw.c`, replace `ns_draw_policy` with:

```c
void ns_draw_policy(HDC hdc, const NsPolicyLayout *l, CmdPolicy policy,
                    int compact, int hover_stop, const ThemeTokens *tokens,
                    HFONT font, int dpi)
{
    if (!hdc || !l || !tokens) return;
    if (l->total_w <= 0 || l->cell[0].h <= 0) return;
    cmd_policy_clamp(&policy);
    int auto_on = cmd_policy_auto_on(policy);

    const ThemeSurface *intent = policy_intent(tokens, policy.allowed);
    COLORREF surface = NS_CR(tokens->bg_secondary.base);
    int left = l->cell[0].x, top = l->cell[0].y;
    int right = left + l->total_w, bottom = top + l->cell[0].h;
    int clip_r = (l->clip_right < right) ? l->clip_right : right;
    if (clip_r <= left) return;               /* wholly under the button */
    int radius = ns_type_pill(l->cell[0].h);

    /* Fills and labels: inside the clip AND the pill's rounded outline, so
     * plain rect fills give rounded ends without a gap at the joins. */
    int saved = SaveDC(hdc);
    IntersectClipRect(hdc, left, top, clip_r, bottom);
    HRGN pill = CreateRoundRectRgn(left, top, right + 1, bottom + 1,
                                   2 * radius, 2 * radius);
    if (pill) {
        ExtSelectClipRgn(hdc, pill, RGN_AND);
        DeleteObject(pill);
    }
    HFONT old_font = font ? (HFONT)SelectObject(hdc, font) : NULL;
    int old_bk = SetBkMode(hdc, TRANSPARENT);

    for (int k = 0; k < NS_POLICY_STOPS; k++) {
        if (l->cell[k].w <= 0) continue;
        int hot = (hover_stop == k);
        COLORREF fill_cr, label_cr;
        if (k <= policy.allowed && auto_on) {
            fill_cr  = hot ? NS_CR(intent->hover) : NS_CR(intent->base);
            label_cr = NS_CR(intent->label);
        } else if (k <= policy.allowed) {
            fill_cr  = rgb_alpha(NS_CR(intent->base), surface,
                                 hot ? 0.30f : 0.18f);
            label_cr = NS_CR(tokens->text_main);
        } else {
            fill_cr  = hot ? NS_CR(tokens->bg_secondary.hover) : surface;
            label_cr = NS_CR(tokens->text_disabled);
        }
        RECT rc = { l->cell[k].x, l->cell[k].y,
                    l->cell[k].x + l->cell[k].w,
                    l->cell[k].y + l->cell[k].h };
        HBRUSH br = CreateSolidBrush(fill_cr);
        FillRect(hdc, &rc, br);
        DeleteObject(br);
        COLORREF old_tc = SetTextColor(hdc, label_cr);
        draw_utf8_text(hdc, &rc,
                       compact ? cmd_policy_stop_short(k) : cmd_policy_stop_label(k),
                       DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SetTextColor(hdc, old_tc);
    }
    SetBkMode(hdc, old_bk);
    if (old_font) SelectObject(hdc, old_font);
    RestoreDC(hdc, saved);

    /* The outline, clipped to the clip rect only (the round region would
     * halve the antialiased stroke), so a cut pill ends at the clip. */
    saved = SaveDC(hdc);
    IntersectClipRect(hdc, left, top - 1, clip_r, bottom + 1);
    RECT outer = { left, top, right, bottom };
    ns_draw_round_stroke(hdc, &outer, radius, NS_CR(tokens->border),
                         ns_scale(STROKE_HAIRLINE, dpi));
    RestoreDC(hdc, saved);
}

void ns_draw_auto_button(HDC hdc, const NsRect *r, CmdPolicy policy,
                         int compact, int hover, const ThemeTokens *tokens,
                         HFONT font, int dpi)
{
    if (!hdc || !r || !tokens || r->w <= 0 || r->h <= 0) return;
    cmd_policy_clamp(&policy);
    int on = cmd_policy_auto_on(policy);
    const ThemeSurface *intent = policy_intent(tokens, policy.allowed);

    RECT rc = { r->x, r->y, r->x + r->w, r->y + r->h };
    int radius = ns_type_pill(r->h);
    COLORREF fill_cr, label_cr;
    if (on) {
        fill_cr  = hover ? NS_CR(intent->hover) : NS_CR(intent->base);
        label_cr = NS_CR(intent->label);
    } else {
        fill_cr  = hover ? NS_CR(tokens->bg_secondary.hover)
                         : NS_CR(tokens->bg_secondary.base);
        label_cr = NS_CR(tokens->text_dim);
    }
    ns_draw_round_fill(hdc, &rc, radius, fill_cr, 255);
    if (!on)
        ns_draw_round_stroke(hdc, &rc, radius, NS_CR(tokens->border),
                             ns_scale(STROKE_HAIRLINE, dpi));

    HFONT old_font = font ? (HFONT)SelectObject(hdc, font) : NULL;
    int old_bk = SetBkMode(hdc, TRANSPARENT);
    COLORREF old_tc = SetTextColor(hdc, label_cr);
    draw_utf8_text(hdc, &rc, cmd_policy_auto_label(compact),
                   DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SetTextColor(hdc, old_tc);
    SetBkMode(hdc, old_bk);
    if (old_font) SelectObject(hdc, old_font);
}
```

`NS_CR`, `rgb_alpha` and `policy_intent` are the file's existing helpers. `ns_draw.c` is excluded from the `RGB(` gate, and no literal colour is added.

- [ ] **Step 3: Run the tests.** Run `make test 2>&1 | tail -3`. Expected: `Tests Run: 2982, Failed: 0`. This includes `tests/test_ui_tokens.c`'s gates, which scan `src/ui` sources and must stay green. Where MinGW exists, run `make src/ui/ns_draw.o` (or `mingw32-make src/ui/ns_draw.o`), which should compile with no warnings. Delete the object afterwards with `rm -f src/ui/ns_draw.o`. Otherwise, read the new code against `ns_draw_round_fill`/`ns_draw_round_stroke` (lines 86-120) for argument order.

- [ ] **Step 4: Commit.**

```bash
git add src/ui/ns_draw.h src/ui/ns_draw.c
git status --short
git commit -F - <<'EOF'
feat(ui): paint the clipped policy pill and the Auto-approve button (v1.2.37)

The pill paints one band per cell inside IntersectClipRect on the
layout's clip, solid at or below the level when Auto-approve is on, with
compact labels on a narrow row. The button is outlined when off and
filled with the level's intent colour when on; hover through the
caller's ns_hover id.

Models: claude-sonnet-5 (implementation); claude-opus-5-5 (plan)

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
```

---

### Task 9: `ai_chat` — ids, handlers, hover, busy label and the Critical confirmation

**Files**
- Modify: `src/ui/ai_chat.c`:
  - ids at lines 110-116; `WM_AI_*` at lines 126-129
  - `AiChatData` fields after `policy_default` (line 191)
  - `apply_policy_change` (lines 983-1033); new `request_policy_change` after it
  - status-line macros, `ai_chat_status_rect_for_id`, `ai_chat_get_status_paint`, `ai_chat_status_hit` and `ai_chat_status_rect` (lines 2853-3000)
  - `paint_status_line` (lines 3131-3150)
  - `WM_LBUTTONDOWN` status branch (lines 3629-3647)
  - `WM_COMMAND` policy handlers (lines 3816-3860)
  - `do_session_switch` (line 5327)
- Modify: `src/core/cmd_policy.h`: delete the `POLICY_BAND_*` enum and its comment (base lines 36-37), whose last users go in this task.

**Interfaces**
- Consumes:
  - From Task 1: `cmd_policy_set_allowed`, `cmd_policy_set_auto(CmdPolicy *p, int on)`, `int cmd_policy_auto_on(CmdPolicy p)`, `int cmd_policy_needs_confirm(CmdPolicy cur, CmdPolicy next)`, `unsigned cmd_policy_status_word(CmdPolicy p)`.
  - From Task 3: `cmd_policy_stop_short`, `cmd_policy_auto_label`, `CMD_POLICY_CONFIRM_TEXT`.
  - From Task 4: `ns_policy_width`, `ns_policy_layout(r, text_w, clip_right, hit_h, dpi, out)`.
  - From Task 5: `AiStatusMeasure`, `ai_status_layout(status, dpi, &m, &sl)`, `ai_status_hit(&sl, &pl, x, y, &stop)`, `HIT_POLICY_AUTO`.
  - From Task 8: `ns_draw_policy(hdc, &pl, policy, compact, hover_stop, tok, font, dpi)`, `ns_draw_auto_button(hdc, &rect, policy, compact, hover, tok, font, dpi)`.
- Produces:
  - `#define WM_AI_GET_POLICY (WM_USER + 104)`, which returns `cmd_policy_status_word(d->approval_q.policy)`. The harness sends it (Task 14).
  - `#define STATUS_HIT_AUTO NS_POLICY_STOPS` and `#define STATUS_HIT_COUNT (NS_POLICY_STOPS + 1)`.
  - `static void request_policy_change(HWND hwnd, AiChatData *d, CmdPolicy next);`
  - `AiStatusPaint` (unchanged shape: `status`, `sl`, `pl`, `meter_text`, `has_meter`). Task 10 uses it.

This task cannot be compiled natively. There is no failing native test for it; the behaviour is covered by the harness in Tasks 14-16.

- [ ] **Step 1: Ids and the query message.** Replace lines 110-116 (from `#define IDC_CHAT_AUTOAPPROVE 4015` through the `IDC_CHAT_POLICY_AUTO_BASE` comment) with:

```c
#define IDC_CHAT_AUTOAPPROVE 4015  /* toggles the Auto-approve button */
/* The level's absolute setter: post 4030 + stop to put the level on a
 * named stop. The integration harness drives the control through these
 * and 4015 (docs/superpowers/specs/2026-10-18-ai-auto-approve-button-
 * design.md section 2). */
#define IDC_CHAT_POLICY_ALLOW_BASE 4030  /* 4030..4033: allowed = stop 0..3 */
/* 4040..4044 set the old unattended marker. Retired 2026-10-18 with the
 * rail: they are swallowed without effect so a stale caller can never
 * fall through to another handler. */
#define IDC_CHAT_POLICY_RETIRED_BASE 4040
```

After `#define WM_AI_TOOL_MSG ...` (line 129) add:

```c
/* Sent by the integration harness: returns cmd_policy_status_word() of
 * the displayed session's policy -- bits 0-7 the level, bit 8 Auto-approve. */
#define WM_AI_GET_POLICY (WM_USER + 104)
```

- [ ] **Step 2: New panel fields.** After `CmdPolicy policy_default;` and its comment (line 191) add:

```c
    unsigned session_gen;      /* bumped by do_session_switch(): a policy
                                 * confirmation answered after a switch
                                 * applies nothing (request_policy_change()) */
    int policy_confirm_open;   /* 1 while the Critical confirmation is up:
                                 * every policy gesture is then a no-op */
```

- [ ] **Step 3: Hide the tooltip on any change, and the confirming entry point.** In `apply_policy_change`, after the early `return;` for an unchanged policy, add:

```c
    /* The tip on screen describes the old policy; the next hover fetches
     * the new text (spec section 4). */
    if (d->hTooltip) SendMessage(d->hTooltip, TTM_POP, 0, 0);
```

Change the function's comment from `... The unattended marker only affects commands that have not arrived yet ...` to `... Auto-approve only affects commands that have not arrived yet, so turning it on releases nothing already waiting (spec 2026-10-18 section 6: one click on the card's own Run button does that) ...`. Do not add any approval pass.

Directly after `apply_policy_change`, add:

```c
/* Every policy gesture goes through here: the cells, the Auto-approve
 * button, the cycle id 4005 and the harness's posted ids. Moving into
 * "Critical with Auto-approve on" asks first (spec 2026-10-18 section 2).
 * The box runs a modal loop, so: a second gesture while it is up is a
 * no-op, the tooltip is hidden first, and after Yes the change applies
 * only if the panel still exists, the same session is displayed and its
 * policy has not moved. Dispatch ticks and replies keep running during the
 * box; nothing here touches them. */
static void request_policy_change(HWND hwnd, AiChatData *d, CmdPolicy next)
{
    if (!d || d->policy_confirm_open) return;
    CmdPolicy cur = d->approval_q.policy;
    if (!cmd_policy_needs_confirm(cur, next)) {
        apply_policy_change(d, next);
        return;
    }
    unsigned gen = d->session_gen;
    d->policy_confirm_open = 1;
    if (d->hTooltip) SendMessage(d->hTooltip, TTM_POP, 0, 0);
    int answer = MessageBoxA(hwnd, CMD_POLICY_CONFIRM_TEXT, "Auto-approve",
                             MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
    /* The panel may have been destroyed (closed, undocked) while the box was
     * up, freeing d: look it up again from the window. */
    if (!IsWindow(hwnd)) return;
    d = (AiChatData *)(LONG_PTR)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!d) return;
    d->policy_confirm_open = 0;
    if (answer != IDYES || d->session_gen != gen) return;
    if (d->approval_q.policy.allowed != cur.allowed ||
        d->approval_q.policy.unattended != cur.unattended) return;
    apply_policy_change(d, next);
}
```

- [ ] **Step 4: Status-line ids, geometry and hit-test.** Replace the macro block at lines 2853-2859 (from `/* The policy control has eight hittable bands` through `#define STATUS_HIT_COUNT`) with:

```c
/* Status-line hover/hit ids for ns_hover: 0..3 the four cells, 4 the
 * Auto-approve button; -1 stays "nothing" (spec 2026-10-18 section 3). */
#define STATUS_HIT_CELL(stop) (stop)
#define STATUS_HIT_AUTO       NS_POLICY_STOPS
#define STATUS_HIT_COUNT      (NS_POLICY_STOPS + 1)
```

Replace `ai_chat_status_rect_for_id` with:

```c
/* Client rect to invalidate when a hover id gains or loses hover. */
static NsRect ai_chat_status_rect_for_id(const AiStatusPaint *sp, int id)
{
    NsRect z = { 0, 0, 0, 0 };
    if (id >= 0 && id < NS_POLICY_STOPS) return sp->pl.cell[id];
    if (id == STATUS_HIT_AUTO) return sp->sl.auto_btn;
    return z;
}
```

In `ai_chat_get_status_paint`, replace everything from `SIZE sz;` to the end of the function with:

```c
    SIZE sz;
    int full_w[NS_POLICY_STOPS], short_w[NS_POLICY_STOPS];
    for (int k = 0; k < NS_POLICY_STOPS; k++) {
        const char *lbl = cmd_policy_stop_label(k);
        GetTextExtentPoint32A(use_hdc, lbl, (int)strlen(lbl), &sz);
        full_w[k] = sz.cx;
        const char *sh = cmd_policy_stop_short(k);
        GetTextExtentPoint32A(use_hdc, sh, (int)strlen(sh), &sz);
        short_w[k] = sz.cx;
    }
    AiStatusMeasure m;
    memset(&m, 0, sizeof(m));
    m.policy_full_w    = ns_policy_width(full_w, d->dpi);
    m.policy_compact_w = ns_policy_width(short_w, d->dpi);
    {
        const char *af = cmd_policy_auto_label(0);
        const char *ac = cmd_policy_auto_label(1);
        GetTextExtentPoint32A(use_hdc, af, (int)strlen(af), &sz);
        m.auto_full_w = sz.cx;
        GetTextExtentPoint32A(use_hdc, ac, (int)strlen(ac), &sz);
        m.auto_compact_w = sz.cx;
    }

    out->has_meter = d->context_limit > 0;
    if (out->has_meter) {
        int actual = d->actual_input_tokens + d->actual_output_tokens;
        int tokens = actual > 0 ? actual : ai_context_estimate_tokens(&d->conv);
        ai_chat_format_meter_text(tokens, d->context_limit,
                                  out->meter_text, sizeof(out->meter_text));
        GetTextExtentPoint32A(use_hdc, out->meter_text,
                              (int)strlen(out->meter_text), &sz);
        m.meter_text_w = sz.cx;
    }

    if (old) SelectObject(use_hdc, old);
    if (own_hdc) ReleaseDC(d->hwnd, own_hdc);

    ai_status_layout(out->status, d->dpi, &m, &out->sl);
    /* Cells are hit over the full row height; the pill is cut at the
     * layout's clip so a cell under the button neither paints nor hits. */
    ns_policy_layout(out->sl.policy, out->sl.compact ? short_w : full_w,
                     out->sl.policy_clip, out->status.h, d->dpi, &out->pl);
}
```

Replace `ai_chat_status_hit` and `ai_chat_status_rect` with:

```c
/* Hit-test a client point against the status line. Returns 0..3 for a
 * cell, STATUS_HIT_AUTO for the button, or -1. */
static int ai_chat_status_hit(AiChatData *d, int x, int y)
{
    AiStatusPaint sp;
    ai_chat_get_status_paint(d, NULL, &sp);
    int stop = -1;
    int what = ai_status_hit(&sp.sl, &sp.pl, x, y, &stop);
    if (what == HIT_POLICY_ALLOWED) return STATUS_HIT_CELL(stop);
    if (what == HIT_POLICY_AUTO) return STATUS_HIT_AUTO;
    return -1;
}

/* Client rect for a status-line hit id, for targeted invalidation. 0 when
 * the id is out of range or the element is zero-size. */
static int ai_chat_status_rect(AiChatData *d, int id, RECT *out_rc)
{
    if (id < 0 || id >= STATUS_HIT_COUNT) return 0;
    AiStatusPaint sp;
    ai_chat_get_status_paint(d, NULL, &sp);
    NsRect r = ai_chat_status_rect_for_id(&sp, id);
    if (r.w <= 0 || r.h <= 0) return 0;
    SetRect(out_rc, r.x, r.y, r.x + r.w, r.y + r.h);
    return 1;
}
```

- [ ] **Step 5: Paint.** In `paint_status_line`, replace the `/* The one policy control */` block and the `if (d->context_label[0]) { ... }` branch (lines 3131-3150) with:

```c
    /* The policy pill and the Auto-approve button */
    {
        int hot = d->status_hover.hot_id;
        int hover_stop = (hot >= 0 && hot < NS_POLICY_STOPS) ? hot : -1;
        ns_draw_policy(hdc, &sp.pl, d->approval_q.policy, sp.sl.compact,
                       hover_stop, tok, cap_font, d->dpi);
        ns_draw_auto_button(hdc, &sp.sl.auto_btn, d->approval_q.policy,
                            sp.sl.compact, hot == STATUS_HIT_AUTO, tok,
                            cap_font, d->dpi);
    }

    /* Busy override, or the context meter + used/limit numbers. The busy
     * label uses the layout's label rect, which ends SP_SM short of the
     * button, so it can never paint over it (spec section 3). */
    if (d->context_label[0]) {
        if (sp.sl.label.w > 0) {
            RECT rc = ai_chat_to_RECT(sp.sl.label);
            SetTextColor(hdc, theme_cr(tok->text_main));
            DrawTextA(hdc, d->context_label, -1, &rc,
                      DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX |
                      DT_END_ELLIPSIS);
        }
    } else if (sp.has_meter) {
```

The rest of the `else if (sp.has_meter)` branch is unchanged. Delete the now-unused `status_rc.right` adjustment; `status_rc` is still used for the background fill above.

- [ ] **Step 6: Click.** In `WM_LBUTTONDOWN`, replace the `if (hit_id >= 0 && hit_id < STATUS_HIT_COUNT) { ... return 0; }` block (lines 3629-3647) with:

```c
            int hit_id = ai_chat_status_hit(d, mx, my);
            if (hit_id >= 0 && hit_id < NS_POLICY_STOPS) {
                PostMessage(hwnd, WM_COMMAND,
                            MAKEWPARAM(IDC_CHAT_POLICY_ALLOW_BASE + hit_id, 0), 0);
                return 0;
            }
            if (hit_id == STATUS_HIT_AUTO) {
                PostMessage(hwnd, WM_COMMAND, MAKEWPARAM(IDC_CHAT_AUTOAPPROVE, 0), 0);
                return 0;
            }
```

Keep the existing `int hit_id = ai_chat_status_hit(d, mx, my);` line if it is already directly above, rather than declaring it twice. `WM_SETCURSOR` and `WM_MOUSEMOVE` stay as they are: any id `>= 0` gives the hand cursor, and `ns_hover_move` takes ids 0..4.

- [ ] **Step 7: Commands and the query.** Replace the `IDC_CHAT_PERMIT` and `IDC_CHAT_AUTOAPPROVE` cases (lines 3822-3835) and their banner comment with:

```c
        /* ── The policy row ─────────────────────────────────────────
         * Every gesture lands here and goes through request_policy_change(),
         * which asks before Critical runs unattended and is a no-op while
         * that box is up. apply_policy_change() then unblocks or re-blocks
         * pending rows against the new level. */
        case IDC_CHAT_PERMIT:
            if (d) {
                CmdPolicy next = d->approval_q.policy;
                cmd_policy_cycle_allowed(&next);
                request_policy_change(hwnd, d, next);
            }
            return 0;
        case IDC_CHAT_AUTOAPPROVE:
            if (d) {
                CmdPolicy next = d->approval_q.policy;
                cmd_policy_set_auto(&next, !cmd_policy_auto_on(next));
                request_policy_change(hwnd, d, next);
            }
            return 0;
```

In `default:`, replace the two policy `if` blocks (lines 3840-3860) with:

```c
            /* IDC_CHAT_POLICY_ALLOW_BASE + stop -> put the level on that
             * stop; Auto-approve follows it when on (cmd_policy_set_allowed). */
            if (d && ctl_id >= IDC_CHAT_POLICY_ALLOW_BASE &&
                ctl_id < IDC_CHAT_POLICY_ALLOW_BASE + NS_POLICY_STOPS) {
                CmdPolicy next = d->approval_q.policy;
                cmd_policy_set_allowed(&next, ctl_id - IDC_CHAT_POLICY_ALLOW_BASE);
                request_policy_change(hwnd, d, next);
                return 0;
            }
            /* Retired unattended setters: swallowed, no effect. */
            if (ctl_id >= IDC_CHAT_POLICY_RETIRED_BASE &&
                ctl_id <= IDC_CHAT_POLICY_RETIRED_BASE + NS_POLICY_STOPS)
                return 0;
```

In the WndProc's top-level `switch (msg)`, next to `case WM_AI_RESPONSE:`, add:

```c
    case WM_AI_GET_POLICY:
        return d ? (LRESULT)cmd_policy_status_word(d->approval_q.policy) : 0;
```

- [ ] **Step 8: Session generation.** In `do_session_switch`, add as the first statement:

```c
    /* A Critical confirmation still up from the old session must not apply
     * to this one (request_policy_change()). */
    d->session_gen++;
```

- [ ] **Step 9: Remove the band enum.** In `src/core/cmd_policy.h`, delete `enum { POLICY_BAND_ALLOWED = 0, POLICY_BAND_UNATTENDED = 1 };` and its comment. Then run `grep -rn "POLICY_BAND_\|cmd_policy_set_unattended\|cmd_policy_cycle_unattended\|cmd_policy_tip\|IDC_CHAT_POLICY_AUTO_BASE\|hit_label\|hit_rail\|rail_fill" src/ui src/core`. Expected: only the definitions of `cmd_policy_set_unattended` and `cmd_policy_cycle_unattended` in `src/core/cmd_policy.{c,h}`, plus the `cmd_policy_tip` call in the `WM_NOTIFY` branch and the `settings.c`/`window.c` setters. Tasks 10, 12 and 13 remove those.

- [ ] **Step 10: Run the tests.** Run `make test 2>&1 | tail -3`. Expected: `Tests Run: 2982, Failed: 0`, with the `test_ui_tokens` gates green because no `RGB(`, `MulDiv(..., 96)` or `CreateFont` was added. Where MinGW exists, run `make src/ui/ai_chat.o`. It still fails until Task 10 removes the `cmd_policy_tip` call, so the object compile check runs at the end of Task 10. Otherwise, read every replaced block against the surrounding code for variable names (`hwnd`, `d`, `ctl_id`, `mx`, `my`, `tok`, `cap_font`, `sp`).

- [ ] **Step 11: Commit.**

```bash
git add src/ui/ai_chat.c src/core/cmd_policy.h
git status --short
git commit -F - <<'EOF'
feat(ai): drive the policy row's cells and Auto-approve button, with a Critical check (v1.2.37)

The status line measures both label sets and takes the five-tier layout;
a cell click sets the level, the button toggles Auto-approve (4015), and
the old unattended ids 4040-4044 are swallowed. Turning on Critical with
Auto-approve asks first; a second gesture during the box is a no-op and
Yes applies only if the same session and policy are still there. The busy
label uses the layout's rect and stops short of the button.
WM_AI_GET_POLICY reports the level and the button for the harness.

Models: claude-sonnet-5 (implementation); claude-opus-5-5 (plan)

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
```

---

### Task 10: `ai_chat` — one tooltip tool per element

**Files**
- Modify: `src/ui/ai_chat.c`:
  - `tooltip_buf` declaration (line 324)
  - a forward declaration after the `AiChatData` typedef
  - `invalidate_status_line` (lines 963-971)
  - the end of `relayout` (line 2842)
  - a new `status_tools_update` after `ai_chat_status_rect`
  - `WM_CREATE` tooltip setup (lines 3446-3455)
  - `WM_NOTIFY` (lines 3499-3540)

**Interfaces**
- Consumes: `AiStatusPaint` and `ai_chat_get_status_paint(d, NULL, &sp)` (Task 9); `sp.pl.hit[k]` (Task 4); `sp.sl.auto_btn`, `sp.sl.compact`, `sp.sl.meter_bar` and `sp.sl.meter_text` (Task 5); `int cmd_policy_cell_tip(int level, int compact, int auto_on, char *buf, size_t cap);`, `int cmd_policy_auto_tip(int on, int allowed, char *buf, size_t cap);` and `CMD_POLICY_TIP_MAX` (Task 3); `int cmd_policy_auto_on(CmdPolicy p);` (Task 1).
- Produces:
  - `#define STATUS_TOOL_BASE 100`. The tool ids are 100..103 for the cells, 104 for the button and 105 for the context meter.
  - `static void status_tools_update(AiChatData *d);`

- [ ] **Step 1: Buffer size and forward declaration.** Change `char tooltip_buf[512];` to `char tooltip_buf[CMD_POLICY_TIP_MAX];`. After the `AiChatData` typedef's closing `} AiChatData;` add:

```c
static void status_tools_update(AiChatData *d);
```

- [ ] **Step 2: Register the rect tools.** In `WM_CREATE`, replace the whole-window callback tool (the `TOOLINFO ti; ... TTM_ADDTOOL` block, lines 3446-3455) with:

```c
            /* The status line: one rect tool per element -- four cells,
             * the Auto-approve button and the context meter -- so Windows
             * shows and hides the tip per element on its own (spec
             * 2026-10-18 section 4). Rects are set by status_tools_update()
             * after every relayout; text comes from TTN_GETDISPINFO. */
            for (int t = 0; t < STATUS_TOOL_COUNT; t++) {
                TOOLINFO ti;
                memset(&ti, 0, sizeof(ti));
                ti.cbSize   = sizeof(ti);
                ti.uFlags   = TTF_SUBCLASS;
                ti.hwnd     = hwnd;
                ti.uId      = (UINT_PTR)(STATUS_TOOL_BASE + t);
                ti.lpszText = LPSTR_TEXTCALLBACK;
                SetRectEmpty(&ti.rect);
                SendMessage(nd->hTooltip, TTM_ADDTOOL, 0, (LPARAM)&ti);
            }
```

Update the comment above `nd->hTooltip = CreateWindowEx(...)` to say `... plus one rect tool per status-line element (four cells, the Auto-approve button, the context meter) ...` instead of "one whole-window callback-text tool".

Next to the `STATUS_HIT_*` macros from Task 9, add:

```c
/* Tooltip tool ids on the panel: STATUS_TOOL_BASE + hit id (cells 0..3,
 * button 4), then the context meter. */
#define STATUS_TOOL_BASE  100
#define STATUS_TOOL_METER STATUS_HIT_COUNT
#define STATUS_TOOL_COUNT (STATUS_HIT_COUNT + 1)
```

- [ ] **Step 3: Keep the rects current.** After `ai_chat_status_rect`, add:

```c
/* Move each status-line tooltip tool onto its element. Called after every
 * relayout and every status-line invalidation (resize, DPI change, tier
 * change, policy or context change). A clipped cell gets an empty rect, so
 * it shows no tip. */
static void status_tools_update(AiChatData *d)
{
    if (!d || !d->hTooltip || !d->hwnd) return;
    AiStatusPaint sp;
    ai_chat_get_status_paint(d, NULL, &sp);
    for (int t = 0; t < STATUS_TOOL_COUNT; t++) {
        NsRect r = { 0, 0, 0, 0 };
        if (t < NS_POLICY_STOPS) {
            r = sp.pl.hit[t];
        } else if (t == STATUS_HIT_AUTO) {
            r = sp.sl.auto_btn;
        } else if (sp.has_meter &&
                   (sp.sl.meter_bar.w > 0 || sp.sl.meter_text.w > 0)) {
            NsRect a = sp.sl.meter_bar.w > 0 ? sp.sl.meter_bar : sp.sl.meter_text;
            NsRect b = sp.sl.meter_text.w > 0 ? sp.sl.meter_text : sp.sl.meter_bar;
            r.x = a.x; r.y = a.y; r.w = (b.x + b.w) - a.x; r.h = a.h;
        }
        TOOLINFO ti;
        memset(&ti, 0, sizeof(ti));
        ti.cbSize = sizeof(ti);
        ti.hwnd   = d->hwnd;
        ti.uId    = (UINT_PTR)(STATUS_TOOL_BASE + t);
        if (r.w > 0 && r.h > 0)
            SetRect(&ti.rect, r.x, r.y, r.x + r.w, r.y + r.h);
        else
            SetRectEmpty(&ti.rect);
        SendMessage(d->hTooltip, TTM_NEWTOOLRECT, 0, (LPARAM)&ti);
    }
}
```

At the end of `relayout()`, after the composer block, add `    status_tools_update(d);`. At the end of `invalidate_status_line()`, after `InvalidateRect(...)`, add `    status_tools_update(d);`.

- [ ] **Step 4: Fetch the text.** In `WM_NOTIFY`, replace the status-line branch, from `if (d && hdr && hdr->hwndFrom == d->hTooltip &&` through its closing `}` at line ~3539, with:

```c
        if (d && hdr && hdr->hwndFrom == d->hTooltip &&
            (hdr->code == TTN_GETDISPINFOA || hdr->code == TTN_NEEDTEXTA) &&
            hdr->idFrom >= (UINT_PTR)STATUS_TOOL_BASE &&
            hdr->idFrom < (UINT_PTR)(STATUS_TOOL_BASE + STATUS_TOOL_COUNT)) {
            /* Fetched fresh on every show, so the text always matches the
             * current policy and label set. */
            NMTTDISPINFOA *nm = (NMTTDISPINFOA *)lParam;
            int t = (int)(hdr->idFrom - (UINT_PTR)STATUS_TOOL_BASE);
            AiStatusPaint sp;
            ai_chat_get_status_paint(d, NULL, &sp);
            CmdPolicy pol = d->approval_q.policy;
            int on = cmd_policy_auto_on(pol);
            d->tooltip_buf[0] = '\0';
            if (t < NS_POLICY_STOPS) {
                cmd_policy_cell_tip(t, sp.sl.compact, on,
                                    d->tooltip_buf, sizeof(d->tooltip_buf));
            } else if (t == STATUS_HIT_AUTO) {
                cmd_policy_auto_tip(on, pol.allowed,
                                    d->tooltip_buf, sizeof(d->tooltip_buf));
            } else if (sp.has_meter) {
                int actual = d->actual_input_tokens + d->actual_output_tokens;
                int est = (actual > 0) ? 0 : ai_context_estimate_tokens(&d->conv);
                ai_format_context_tooltip(d->actual_input_tokens,
                                          d->actual_output_tokens, est,
                                          d->context_limit, d->conv.model,
                                          d->tooltip_buf, sizeof(d->tooltip_buf));
            }
            nm->lpszText = d->tooltip_buf;
            return 0;
        }
```

The existing `TTM_SETMAXTIPWIDTH, 0, 400` call stays, so the long tips wrap. Do not add a theme claim to the tooltip: it is a system control.

- [ ] **Step 5: Run the tests.** Run `make test 2>&1 | tail -3`. Expected: `Tests Run: 2982, Failed: 0`. Then run `grep -n "cmd_policy_tip\|TTF_IDISHWND" src/ui/ai_chat.c`. Expected: only the `add_tooltip()` helper's `TTF_IDISHWND`, which serves the header buttons. Where MinGW exists, run `make src/ui/ai_chat.o src/ui/ns_draw.o`, which should compile clean with `-Werror`; then `rm -f src/ui/ai_chat.o src/ui/ns_draw.o`.

- [ ] **Step 6: Commit.**

```bash
git add src/ui/ai_chat.c
git status --short
git commit -F - <<'EOF'
feat(ai): give each policy cell and the Auto-approve button its own tooltip (v1.2.37)

The status line's single whole-panel tool becomes six rect tools (four
cells, the button, the context meter) moved with TTM_NEWTOOLRECT after
every relayout, so moving between cells re-triggers the tip. Text comes
from cmd_policy_cell_tip()/cmd_policy_auto_tip() on each show; a clipped
cell has no tool rect.

Models: claude-sonnet-5 (implementation); claude-opus-5-5 (plan)

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
```

---

### Task 11: `ai_chat` — hidden-tab batches wait, and the card starts on switch-back

**Files**
- Modify: `src/ui/ai_chat.c`: the background reply batch build (lines 4364-4368) and `do_session_switch` (after `update_panel_state(d);`, line 5420).

**Interfaces**
- Consumes: `CmdPolicy cmd_policy_attended(CmdPolicy p);` (Task 2); `static void maybe_start_next_batch(AiChatData *d);` (existing, line 1917, defined before `do_session_switch`).
- Produces: no new symbols.

- [ ] **Step 1: Build the background batch attended.** Replace:

```c
                    ApprovalQueue bg_defaults;
                    chat_approval_init(&bg_defaults);
                    bg_defaults.policy = src->policy;
                    cmd_policy_clamp(&bg_defaults.policy);
```

with:

```c
                    /* Built while the tab is hidden: Auto-approve forced
                     * off, so every row is PENDING or BLOCKED and nothing
                     * runs before the user has looked (spec 2026-10-18
                     * section 6). Before this, an all-approved batch built
                     * here ran, stale, after an unrelated later prompt. */
                    ApprovalQueue bg_defaults;
                    chat_approval_init(&bg_defaults);
                    bg_defaults.policy = cmd_policy_attended(src->policy);
```

- [ ] **Step 2: Start the queue on switch-back.** In `do_session_switch`, after `chat_rebuild_display(d);` and `update_panel_state(d);`, add:

```c
    /* Pick up the newly displayed session's queue at once rather than
     * after the user's next prompt: a batch the user approved but that
     * never ran (the switch away cancelled the dispatcher) starts now.
     * Hidden-tab batches are all pending, so they wait for a click. */
    maybe_start_next_batch(d);
```

- [ ] **Step 3: Run the tests.** Run `make test 2>&1 | tail -3`. Expected: `Tests Run: 2982, Failed: 0`. `test_approval_attended_policy_approves_nothing_on_arrival` (Task 2) is the native proof of the rule this path relies on. Where MinGW exists, run `make src/ui/ai_chat.o` and then `rm -f src/ui/ai_chat.o`.

- [ ] **Step 4: Commit.**

```bash
git add src/ui/ai_chat.c
git status --short
git commit -F - <<'EOF'
fix(ai): make a batch proposed while its tab was hidden wait for a look (v1.2.37)

The background reply path builds its batch under the session's policy
with Auto-approve forced off, and switching back to a session starts its
queue at once.

Models: claude-sonnet-5 (implementation); claude-opus-5-5 (plan)

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
```

---

### Task 12: Settings — "Auto-approve by default", its confirmation, and the help text

**Files**
- Modify: `src/ui/resource.h` (line 99)
- Modify: `src/ui/settings.c` (help entries lines 466-474; page build lines 1436-1460; save lines 2033-2045)
- Modify: `src/ui/help_guide.c` (`GUIDE_PART_4`, lines 235-265)

**Interfaces**
- Consumes: `cmd_policy_set_allowed`, `cmd_policy_set_auto`, `cmd_policy_auto_on`, `cmd_policy_needs_confirm` and `cmd_policy_default` (Task 1); `CMD_POLICY_CONFIRM_TEXT` (Task 3).
- Produces: `#define IDC_AI_POLICY_AUTO 3076`, which replaces `IDC_AI_POLICY_UNATTENDED`.

- [ ] **Step 1: The id.** In `src/ui/resource.h`, replace line 99 with:

```c
#define IDC_AI_POLICY_AUTO       3076 /* Settings: "Auto-approve by default" tickbox */
```

- [ ] **Step 2: Help text.** In `settings.c`, replace the two help entries (lines 466-474) with:

```c
    { IDC_AI_POLICY_ALLOWED,
      "The highest level a new session may run: Read, Unknown, Write or "
      "Critical. Commands above it are blocked outright and show as held; "
      "commands at or below it ask first unless Auto-approve is on." },
    { IDC_AI_POLICY_AUTO,
      "Start new sessions with Auto-approve on: commands up to 'Allowed' "
      "run without asking. With 'Allowed' at Critical, Save asks you to "
      "confirm. The AI panel's status line changes both per session." },
```

- [ ] **Step 3: The page.** Replace the policy block at lines 1436-1460 (from `/* The one policy setting, shown as its two markers` through the second combo's `add_ctrl`) with:

```c
            /* The one policy setting: the level, and whether a new session
             * starts with Auto-approve on -- the pill and the button on the
             * AI panel's status line (spec 2026-10-18 section 2). */
            CmdPolicy pol = nd->cfg->settings.ai_policy_default;
            cmd_policy_clamp(&pol);
            {
                HWND lbl = make_label2(nd->hPage, "Allowed:");
                HWND cmb = make_combo2(nd->hPage, (HMENU)IDC_AI_POLICY_ALLOWED,
                                       CBS_DROPDOWNLIST);
                for (int i = 0; i < POLICY_STOP_COUNT; i++)
                    SendMessage(cmb, CB_ADDSTRING, 0,
                                (LPARAM)cmd_policy_stop_label(i));
                SendMessage(cmb, CB_SETCURSEL, (WPARAM)pol.allowed, 0);
                add_ctrl(nd, lbl, cmb, NULL, SETTINGS_PAGE_AI_BEHAVIOUR, 0, 0, 1, 0, 1, 0);
            }
            {
                HWND chk = make_check2(nd->hPage, "Auto-approve by default",
                                       (HMENU)IDC_AI_POLICY_AUTO);
                SendMessage(chk, BM_SETCHECK,
                            cmd_policy_auto_on(pol) ? BST_CHECKED : BST_UNCHECKED, 0);
                add_ctrl(nd, NULL, chk, NULL, SETTINGS_PAGE_AI_BEHAVIOUR, 0, 0, 1, 0, 1, 1);
            }
```

The enclosing `{ ... }` braces of the block stay as they are.

- [ ] **Step 4: Save, with the confirmation.** Replace the save block at lines 2033-2045 (from `/* Command policy for new sessions. The setters clamp,` through its closing brace) with:

```c
            /* Command policy for new sessions: the level and Auto-approve.
             * Critical with Auto-approve on asks first, on every Save (spec
             * 2026-10-18 section 2); No keeps the stored policy as it was
             * and the rest of the page still saves. */
            {
                int a = (int)SendDlgItemMessage(d->hPage, IDC_AI_POLICY_ALLOWED,
                                                CB_GETCURSEL, 0, 0);
                int on = (IsDlgButtonChecked(d->hPage, IDC_AI_POLICY_AUTO)
                              == BST_CHECKED) ? 1 : 0;
                if (a >= 0) {
                    CmdPolicy next = s->ai_policy_default;
                    cmd_policy_set_allowed(&next, a);
                    cmd_policy_set_auto(&next, on);
                    if (!cmd_policy_needs_confirm(cmd_policy_default(), next) ||
                        MessageBoxA(hwnd, CMD_POLICY_CONFIRM_TEXT,
                                    "Auto-approve by default",
                                    MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2)
                            == IDYES)
                        s->ai_policy_default = next;
                }
            }
```

`cmd_policy_needs_confirm(cmd_policy_default(), next)` is 1 exactly when `next` is Critical with Auto-approve on, because the default is Read with it off.

- [ ] **Step 5: The in-app guide.** In `src/ui/help_guide.c`, replace the policy-control bullet in `GUIDE_PART_4`, from `"  - Policy control: one scale of four stops --\r\n"` through `"      session\r\n"`, with:

```c
"  - Policy control: four stops -- Read | Unknown |\r\n"
"      Write | Critical -- and an Auto-approve button on\r\n"
"      the right. Click a stop to make it the highest level\r\n"
"      the assistant may run: commands above it are blocked\r\n"
"      and show as held; commands at or below it ask you\r\n"
"      first. Click Auto-approve to run commands up to that\r\n"
"      level without asking; the stops it covers turn solid.\r\n"
"      Turning it on at Critical asks you to confirm.\r\n"
"      Commands proposed while the tab is hidden always wait\r\n"
"      for you. The control takes its colour from the level\r\n"
"      -- green at Read, blue at Unknown, amber at Write,\r\n"
"      red at Critical. On a narrow panel the labels shorten\r\n"
"      to R U W C and Auto. Hover any of them for a short\r\n"
"      description. New sessions start from Settings > AI\r\n"
"      Assistant > Behaviour > \"Allowed\" and \"Auto-approve\r\n"
"      by default\"; clicking here changes only this session\r\n"
```

Replace `"disabled when none are checked). Rows at or below the\r\n"` and `"unattended marker never reach the card at all -- they run.\r\n"` with:

```c
"disabled when none are checked). With Auto-approve on,\r\n"
"rows at or below the level never reach the card -- they run.\r\n"
```

- [ ] **Step 6: Run the tests.** Run `make test 2>&1 | tail -3`. Expected: `Tests Run: 2982, Failed: 0`. Run `grep -rn "IDC_AI_POLICY_UNATTENDED\|Runs unattended" src/`. Expected: no output. Where MinGW exists, run `make src/ui/settings.o src/ui/help_guide.o` and then `rm -f src/ui/settings.o src/ui/help_guide.o`.

- [ ] **Step 7: Commit.**

```bash
git add src/ui/resource.h src/ui/settings.c src/ui/help_guide.c
git status --short
git commit -F - <<'EOF'
feat(settings): replace "Runs unattended" with an Auto-approve-by-default tickbox (v1.2.37)

Settings > AI Assistant > Behaviour keeps the Allowed combo and gains the
tickbox; Save asks before storing Critical with Auto-approve on, and No
keeps the stored policy. The in-app guide describes the button.

Models: claude-sonnet-5 (implementation); claude-opus-5-5 (plan)

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
```

---

### Task 13: `window.c` — the demo seed and the narrow demo panel

**Files**
- Modify: `src/ui/window.c` (demo seed at lines 1869-1877; docked target width at lines 1940-1945)

**Interfaces**
- Consumes: `CmdPolicy ui_demo_policy(const char *state);` and `int ui_demo_panel_width_px(const char *state, int dpi);` (Task 7); `void cmd_policy_clamp(CmdPolicy *p);` (Task 1).
- Produces: no new symbols.

- [ ] **Step 1: Seed through the clamp.** Replace lines 1869-1877 (the comment `/* Give the demo a policy with both markers off their defaults ...` through `s->ai_state.policy_seeded = 1;`) with:

```c
    /* The demo tab's policy: Write with Auto-approve off, so the gallery
     * shows tinted, plain and blocked cells; Write with it on for
     * --ui-demo=policy-auto, so the solid cells and the filled button
     * show. Through the clamp, like every policy from outside (spec
     * 2026-10-18 section 2). The cards are static data with their own
     * queue policies, so this changes only what the status line paints. */
    s->ai_state.policy = ui_demo_policy(state);
    cmd_policy_clamp(&s->ai_state.policy);
    s->ai_state.policy_seeded = 1;
```

- [ ] **Step 2: The narrow panel.** In the docked branch, change:

```c
            int target = ai_dock_pct_to_px(rc2.right, AI_DOCK_DEFAULT_PCT,
                                           AI_DOCK_MIN_PCT, AI_DOCK_MAX_PCT);
```

to:

```c
            int target = ai_dock_pct_to_px(rc2.right, AI_DOCK_DEFAULT_PCT,
                                           AI_DOCK_MIN_PCT, AI_DOCK_MAX_PCT);
            /* --ui-demo=policy-compact: dock the panel narrow enough for the
             * status line's compact labels, before it is laid out. */
            int demo_w = ui_demo_panel_width_px(state, g_dpi);
            if (demo_w > 0) target = demo_w;
```

- [ ] **Step 3: Run the tests.** Run `make test 2>&1 | tail -3`. Expected: `Tests Run: 2982, Failed: 0`. Run `grep -rn "cmd_policy_set_unattended\|cmd_policy_cycle_unattended" src/ui`. Expected: no output, because no UI code produces a split any more. Where MinGW exists, run `make src/ui/window.o` and then `rm -f src/ui/window.o`.

- [ ] **Step 4: Commit.**

```bash
git add src/ui/window.c
git status --short
git commit -F - <<'EOF'
feat(demo): seed the demo tab's policy from ui_demo_policy (v1.2.37)

The demo seed goes through cmd_policy_clamp like every outside policy;
policy-auto shows the button on, policy-compact docks the panel narrow
enough for R U W C.

Models: claude-sonnet-5 (implementation); claude-opus-5-5 (plan)

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
```

---

### Task 14: Harness — `Set-NutshellAiPolicy -Allowed <level> [-Auto]` and `Get-NutshellAiPolicy`

**Files**
- Modify: `tests/integration/NutshellIT.psm1`: constants near line 263; `Set-NutshellAiPolicy` (lines 835-870); the `Export-ModuleMember` list (line 1409).

**Interfaces**
- Consumes: `WM_AI_GET_POLICY = WM_USER + 104` returning `cmd_policy_status_word()` (Task 9); `IDC_CHAT_AUTOAPPROVE 4015` and `IDC_CHAT_POLICY_ALLOW_BASE 4030` (Task 9).
- Produces:
  - `Get-NutshellAiPolicy -Session $s` returns `@{ Allowed = "Read"|"Unknown"|"Write"|"Critical"; Auto = $true|$false }`.
  - `Set-NutshellAiPolicy -Session $s -Allowed <level> [-Auto]` is absolute: afterwards the button is on exactly when `-Auto` was given. Only `-Allowed Critical -Auto` raises the confirmation box, and that call returns with the box up.

The harness runs only on Windows against a built exe, so there is no native test. Its tests are Tasks 15-16, run by the maintainer.

- [ ] **Step 1: The constant.** After `$script:WM_COMMAND        = 0x0111` add:

```powershell
$script:WM_AI_GET_POLICY  = 0x0400 + 104   # WM_USER + 104, src/ui/ai_chat.c: cmd_policy_status_word()
```

- [ ] **Step 2: Replace the helper.** Replace `function Set-NutshellAiPolicy { ... }` (lines 835-870) with:

```powershell
function Get-NutshellAiPolicy {
    <# The displayed session's policy as the AI panel holds it:
       @{ Allowed = "Read"|"Unknown"|"Write"|"Critical"; Auto = $true|$false }.
       Sends WM_AI_GET_POLICY (WM_USER + 104), answered with
       cmd_policy_status_word(): bits 0-7 the level, bit 8 Auto-approve. Sent
       messages are answered inside a modal loop too, so this works while the
       Critical confirmation box is up. #>
    param([Parameter(Mandatory)] $Session)
    $p = Get-NutshellAiPanel -Session $Session
    if ($p -eq [IntPtr]::Zero) { throw "AI Assist panel is not open" }
    $res = [IntPtr]::Zero
    $ok = [NutshellNative]::SendMessageTimeout($p, $script:WM_AI_GET_POLICY,
        [IntPtr]::Zero, [IntPtr]::Zero, 0x0002, 2000, [ref]$res)   # SMTO_ABORTIFHUNG
    if ($ok -eq [IntPtr]::Zero) { throw "the AI panel did not answer WM_AI_GET_POLICY within 2 s" }
    $word = $res.ToInt64()
    $names = @("Read", "Unknown", "Write", "Critical")
    return @{ Allowed = $names[[int]($word -band 0xFF)]; Auto = (($word -band 0x100) -ne 0) }
}

function Set-NutshellAiPolicy {
    <# Put the displayed session's policy on a named level, with Auto-approve
       on or off (docs/superpowers/specs/2026-10-18-ai-auto-approve-button-design.md).

       -Allowed  the level: Read | Unknown | Write | Critical. Commands above it
                 are blocked and show as held.
       -Auto     Auto-approve on: commands up to -Allowed run without asking.
                 Absent means off -- the call is absolute.

       Posts IDC_CHAT_POLICY_ALLOW_BASE 4030 + level, and IDC_CHAT_AUTOAPPROVE
       4015 (a toggle) only when the button's state, read first with
       Get-NutshellAiPolicy, differs from the one asked for. The button is
       turned off before the level moves and on after it, so only
       -Allowed Critical -Auto raises the confirmation box. That call returns
       with the box up: answer it with
         $dlg = Wait-NutshellDialog -Session $s -Title "Auto-approve"
         Close-NutshellDialog -Dialog $dlg -Button Yes
       Posted, so no foreground window is needed. The old -Unattended
       parameter and ids 4040-4044 were retired in the same change. #>
    param(
        [Parameter(Mandatory)] $Session,
        [Parameter(Mandatory)] [ValidateSet("Read", "Unknown", "Write", "Critical")] [string] $Allowed,
        [switch] $Auto
    )
    $p = Get-NutshellAiPanel -Session $Session
    if ($p -eq [IntPtr]::Zero) { throw "AI Assist panel is not open" }
    $stop = @{ Read = 0; Unknown = 1; Write = 2; Critical = 3 }

    $now = Get-NutshellAiPolicy -Session $Session
    if ($now.Auto -and -not $Auto) {
        [NutshellNative]::PostMessage($p, $script:WM_COMMAND, [IntPtr]4015, [IntPtr]::Zero) | Out-Null
        Start-Sleep -Milliseconds 300
    }
    [NutshellNative]::PostMessage($p, $script:WM_COMMAND,
        [IntPtr](4030 + $stop[$Allowed]), [IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 300
    if ($Auto -and -not $now.Auto) {
        [NutshellNative]::PostMessage($p, $script:WM_COMMAND, [IntPtr]4015, [IntPtr]::Zero) | Out-Null
        Start-Sleep -Milliseconds 300
    }
}
```

- [ ] **Step 3: Export.** In the `Export-ModuleMember` list, change `Send-NutshellAiPrompt, Set-NutshellAiPolicy,` to `Send-NutshellAiPrompt, Get-NutshellAiPolicy, Set-NutshellAiPolicy,`.

- [ ] **Step 4: Check.** Run `grep -rn "Unattended\|4040" tests/integration/`. Expected: only the four `-Unattended` call sites in `cases/20-ai.ps1`, which Task 15 changes, and the "retired" note in the helper's comment. If `pwsh` is installed, run `pwsh -NoProfile -Command "Import-Module ./tests/integration/NutshellIT.psm1 -Force; Get-Command Get-NutshellAiPolicy, Set-NutshellAiPolicy | Format-Table Name"`. Expected: both names listed. Without `pwsh`, read the diff. Run `make test 2>&1 | tail -1`; expected `Tests Run: 2982, Failed: 0`.

- [ ] **Step 5: Commit.**

```bash
git add tests/integration/NutshellIT.psm1
git status --short
git commit -F - <<'EOF'
test(harness): set the AI policy as a level plus -Auto (v1.2.37)

Set-NutshellAiPolicy -Allowed <level> [-Auto] is absolute: it reads the
button through the new WM_AI_GET_POLICY query and posts 4015 only when the
state differs. Get-NutshellAiPolicy reports the level and the button.

Models: claude-sonnet-5 (implementation); claude-opus-5-5 (plan)

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
```

---

### Task 15: Harness — the AI cases and the gallery

**Files**
- Modify: `tests/integration/cases/20-ai.ps1` (lines 22-25, 45, 70-73, 85-92)
- Modify: `tests/integration/cases/30-ui-demo.ps1` (line 34)

**Interfaces**
- Consumes: `Set-NutshellAiPolicy -Session $s -Allowed <level> [-Auto]` (Task 14); demo states `policy-auto` and `policy-compact` (Tasks 7, 13).
- Produces: no new symbols.

- [ ] **Step 1: `ai_runs_read_command_unattended`.** Replace lines 22-25 with:

```powershell
    # Level stays at Read (the default); turn Auto-approve on, so a READ
    # command runs with no card to click.
    Set-NutshellAiPolicy -Session $s -Allowed Read -Auto
```

- [ ] **Step 2: `ai_commands_run_one_at_a_time`.** Replace line 45 with `    Set-NutshellAiPolicy -Session $s -Allowed Read -Auto`.

- [ ] **Step 3: `ai_write_command_held_then_runs_after_allow`.** Replace lines 70-73 with:

```powershell
    # Auto-approve on at Read -- so a WRITE command must be held, not run.
    Set-NutshellAiPolicy -Session $s -Allowed Read -Auto
```

Replace lines 85-92 (the comment block and `Set-NutshellAiPolicy -Session $s -Allowed Write`) with:

```powershell
    # Bug 2 regression: raise the level to Write and run the held command
    # via "Run N selected" (IDC_CMD_APPROVE_SEL) -- before the fix, the
    # write-only batch was never queued (only read commands were), so
    # queued_count stayed 0 and nothing ran even after unblocking.
    # -Allowed Write without -Auto turns the button off, so the card's
    # "Run selected" step still does the running (spec 2026-10-18 s7). The
    # level goes to Write, not Critical: writes allowed, outage-class
    # commands still refused outright.
    Set-NutshellAiPolicy -Session $s -Allowed Write
```

- [ ] **Step 4: The gallery.** In `30-ui-demo.ps1`, change line 34 to:

```powershell
    $galleryStates = @("chat", "approval", "executing", "tool", "error", "empty", "nokey", "nosession", "batches", "thinking", "cmddetail", "settings-debug", "policy-auto", "policy-compact", "all")
```

- [ ] **Step 5: Check.** Run `grep -n "Set-NutshellAiPolicy" tests/integration/cases/20-ai.ps1`. Expected: four lines, all with `-Allowed`, and none with `-Unattended`. Run `make test 2>&1 | tail -1`; expected `Tests Run: 2982, Failed: 0`. The cases themselves run on the Windows box after Task 18's build: `-Tier gate` (`ui_gallery`), then `-Tier ai` once with the key (see Task 18).

- [ ] **Step 6: Commit.**

```bash
git add tests/integration/cases/20-ai.ps1 tests/integration/cases/30-ui-demo.ps1
git status --short
git commit -F - <<'EOF'
test(harness): move the AI cases to -Allowed/-Auto and add the policy demo states (v1.2.37)

The three cases that ran reads unattended now turn Auto-approve on at
Read; the held-write case raises to Write with the button off so "Run
selected" still runs it. The gallery captures policy-auto and
policy-compact in all four themes.

Models: claude-sonnet-5 (implementation); claude-opus-5-5 (plan)

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
```

---

### Task 16: Harness — gate case: Auto-approve at Critical asks first

**Files**
- Modify: `tests/integration/cases/20-ai.ps1` (append after `ai_panel_opens_without_key`)
- Modify: `tests/integration/README.md` (case table rows near lines 176-182)
- Modify: `tests/integration/Run-Integration.ps1` (comment line 38)

**Interfaces**
- Consumes: `Get-NutshellAiPolicy` and `Set-NutshellAiPolicy` (Task 14); `Wait-NutshellDialog`, `Close-NutshellDialog`, `Get-NutshellWindows`, `Get-NutshellAiPanel`, `Send-NutshellCommand`, `Save-NutshellScreenshot`, `Invoke-Case`, `Assert-True`, `$WM_COMMAND`, `$Artifacts` (existing).
- Produces: the gate-tier case `ai_auto_approve_critical_asks_first`.

- [ ] **Step 1: Write the case.** Append to `tests/integration/cases/20-ai.ps1`:

```powershell
# ---- Auto-approve at Critical asks first (gate tier, no key) ------------------
# docs/superpowers/specs/2026-10-18-ai-auto-approve-button-design.md section 2:
# turning the button on while the level is Critical shows a Yes/No box; No
# leaves the policy as it was; a second toggle while the box is up is a no-op
# (Review Focus 5); Yes applies it. Key-free like ai_panel_opens_without_key:
# the policy ids and WM_AI_GET_POLICY are handled in every panel state.
Invoke-Case "ai_auto_approve_critical_asks_first" @{ ai_api_key = "" } {
    param($s)
    Send-NutshellCommand -Session $s -Id 2020 -SettleMs 1500      # IDM_VIEW_AI_CHAT
    $p = Get-NutshellAiPanel -Session $s
    Assert-True ($p -ne [IntPtr]::Zero) "AI Assist panel did not open"

    Set-NutshellAiPolicy -Session $s -Allowed Critical             # button off: no box
    $before = Get-NutshellAiPolicy -Session $s
    Assert-True ($before.Allowed -eq "Critical" -and -not $before.Auto) "setup: expected Critical with Auto-approve off, got $($before.Allowed)/$($before.Auto)"
    Assert-True ((Wait-NutshellDialog -Session $s -Title "Auto-approve" -TimeoutSec 1) -eq [IntPtr]::Zero) "a confirmation appeared for Critical with the button off"

    [NutshellNative]::PostMessage($p, $WM_COMMAND, [IntPtr]4015, [IntPtr]::Zero) | Out-Null   # IDC_CHAT_AUTOAPPROVE
    $dlg = Wait-NutshellDialog -Session $s -Title "Auto-approve" -TimeoutSec 5
    Assert-True ($dlg -ne [IntPtr]::Zero) "no confirmation box for Auto-approve at Critical"

    # A second toggle while the box is up must be a no-op: still one box, and
    # the policy (read through a sent message, answered inside the modal loop)
    # has not moved.
    [NutshellNative]::PostMessage($p, $WM_COMMAND, [IntPtr]4015, [IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 800
    $boxes = @((Get-NutshellWindows -Session $s) | Where-Object { $_ -match "`t#32770`tAuto-approve" })
    Assert-True ($boxes.Count -eq 1) "expected one confirmation box, found $($boxes.Count)"
    $during = Get-NutshellAiPolicy -Session $s
    Assert-True (-not $during.Auto) "Auto-approve turned on while the box was still up"
    Save-NutshellScreenshot -Session $s -Path (Join-Path $Artifacts "ai_auto_approve_confirm.png") | Out-Null

    Close-NutshellDialog -Dialog $dlg -Button No
    Start-Sleep -Milliseconds 500
    $after = Get-NutshellAiPolicy -Session $s
    Assert-True ($after.Allowed -eq "Critical" -and -not $after.Auto) "No changed the policy: now $($after.Allowed)/$($after.Auto)"
    Assert-True ((Wait-NutshellDialog -Session $s -Title "Auto-approve" -TimeoutSec 1) -eq [IntPtr]::Zero) "a second box appeared after No"

    [NutshellNative]::PostMessage($p, $WM_COMMAND, [IntPtr]4015, [IntPtr]::Zero) | Out-Null
    $dlg2 = Wait-NutshellDialog -Session $s -Title "Auto-approve" -TimeoutSec 5
    Assert-True ($dlg2 -ne [IntPtr]::Zero) "no confirmation box the second time"
    Close-NutshellDialog -Dialog $dlg2 -Button Yes
    Start-Sleep -Milliseconds 500
    $on = Get-NutshellAiPolicy -Session $s
    Assert-True ($on.Allowed -eq "Critical" -and $on.Auto) "Yes did not turn Auto-approve on: $($on.Allowed)/$($on.Auto)"

    # The retired unattended id does nothing.
    [NutshellNative]::PostMessage($p, $WM_COMMAND, [IntPtr]4040, [IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 300
    $still = Get-NutshellAiPolicy -Session $s
    Assert-True ($still.Allowed -eq "Critical" -and $still.Auto) "retired id 4040 changed the policy: $($still.Allowed)/$($still.Auto)"
    "Critical + Auto-approve asked first; No kept Critical/off, a second toggle during the box was ignored, Yes turned it on, 4040 is inert"
}
```

- [ ] **Step 2: Document it.** In `tests/integration/README.md`:

  - Change the `ai_runs_read_command_unattended` row's description to: `a prompted \`echo\` runs in the terminal via \`[EXEC]\` once Auto-approve is on at Read`.
  - Change the `ai_write_command_held_then_runs_after_allow` row's description to: `a prompted \`touch\` is held back while the level is Read (Auto-approve on), then runs via "Run N selected" once the level is raised to Write with Auto-approve off`.
  - Change the `ui_gallery` row's count to `(15 states x 4 themes = 60 captures)`.
  - After the `ai_panel_opens_without_key` row add:

```markdown
| `ai_auto_approve_critical_asks_first` | with no key: Auto-approve at Critical raises a Yes/No box; a second toggle during the box is ignored; No leaves Critical/off; Yes turns it on; retired id 4040 does nothing (spec 2026-10-18 section 2) |
```

In `tests/integration/Run-Integration.ps1`, change line 38 to `#   20-ai.ps1            the five key-gated ai_* cases + ai_panel_opens_without_key + ai_auto_approve_critical_asks_first`.

- [ ] **Step 3: Check.** Run `make test 2>&1 | tail -1`; expected `Tests Run: 2982, Failed: 0`. The case runs on the Windows box after Task 18: `tests\integration\Run-Integration.ps1 -Tier gate -Only ai_auto_approve_critical_asks_first`, expected `[PASS]`.

- [ ] **Step 4: Commit.**

```bash
git add tests/integration/cases/20-ai.ps1 tests/integration/README.md tests/integration/Run-Integration.ps1
git status --short
git commit -F - <<'EOF'
test(harness): check that Auto-approve at Critical asks first (v1.2.37)

A gate-tier, key-free case: No keeps the policy, a second toggle during
the box is ignored, Yes applies it, and the retired 4040 id is inert.

Models: claude-sonnet-5 (implementation); claude-opus-5-5 (plan)

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
```

---

### Task 17: Docs — README and the notes' Open list

**Files**
- Modify: `README.md` (Settings bullets lines 206-207; Status Line section lines 225-236)
- Modify: `docs/superpowers/specs/2026-09-06-ui-redesign-notes.md` (the "Policy control follow-ups" item, lines 276-280; a new Open item after it)

**Interfaces**
- Consumes: the behaviour from Tasks 1-13.
- Produces: no code.

- [ ] **Step 1: README, Settings.** Replace the **Allowed** and **Runs unattended** bullets (lines 206-207) with:

```markdown
- **Allowed** — the highest level a new session may run: Read / Unknown / Write / Critical (default: Read). Commands above it are blocked outright and show as held; commands at or below it ask first unless Auto-approve is on.
- **Auto-approve by default** — start new sessions with Auto-approve on, so commands up to **Allowed** run without asking (default: off). With **Allowed** at Critical, Save asks you to confirm. The AI panel's status line changes both per session.
```

- [ ] **Step 2: README, Status Line.** Replace the **Policy control** bullet and its paragraph (lines 225-235, from `- **Policy control** — one scale of four stops` through `... clicking here only changes the current session`) with:

```markdown
- **Policy control** — four stops, **Read** │ **Unknown** │ **Write** │ **Critical**, and an **Auto-approve** button at the right end of the line:
  - **the stops** set the level. Commands classified above it are blocked outright and show as held on the card; commands at or below it ask you first. Click a stop to move the level there
  - **Auto-approve** runs commands up to the level without asking — the stops it covers turn solid. Turning it on at Critical (or moving to Critical while it is on) asks you to confirm first. A reply that arrives while its tab is hidden always waits for you, so keep the tab shown to let the panel work unattended

  The control takes its colour from the level (green at Read, blue at Unknown, amber at Write, red at Critical), so a permissive session stays visible at a glance. On a narrow panel the labels shorten to **R U W C** and **Auto**; hover any stop or the button for a short description. A new session starts from **Allowed** / **Auto-approve by default** in Settings under AI Assistant > Behaviour; clicking here only changes the current session
```

- [ ] **Step 3: Notes.** In `docs/superpowers/specs/2026-09-06-ui-redesign-notes.md`, replace `status line still has no keyboard path (a whole-line design-system job, the four`/`` `WM_COMMAND` ids 4030–4044 are ready for a menu or accelerator).`` (the end of the "Policy control follow-ups" item) with:

```markdown
      status line still has no keyboard path (a whole-line design-system job; the
      `WM_COMMAND` ids 4030–4033 and 4015 are ready for a menu or accelerator, and
      4040–4044 were retired with the rail on 2026-10-18).
```

After that item add:

```markdown
- [ ] Per-session AI dispatcher (follow-up from `2026-10-18-ai-auto-approve-button-design.md`
      section 9): with Auto-approve on, a session processes unattended only while it is the
      displayed tab — a switch cancels the dispatcher, and a reply for a hidden tab builds its
      batch with Auto-approve forced off. Running a background session's batches needs the
      dispatcher to own its terminal, conversation and card list per session instead of the
      panel's active ones, and the switch to stop cancelling. Own spec first.
```

- [ ] **Step 4: Check.** Run `grep -n "Runs unattended\|the rail under\|unattended marker" README.md`. Expected: no output. Run `make test 2>&1 | tail -1`; expected `Tests Run: 2982, Failed: 0`.

- [ ] **Step 5: Commit.**

```bash
git add README.md docs/superpowers/specs/2026-09-06-ui-redesign-notes.md
git status --short
git commit -F - <<'EOF'
docs: describe the Auto-approve button and open the per-session dispatcher item (v1.2.37)

Models: claude-sonnet-5 (implementation); claude-opus-5-5 (plan)

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
```

---

### Task 18: Version bump, release build and the pre-ready checks (Windows dev box)

**Files**
- Modify: `src/ui/resource.h` (lines 4-5)
- Modify: `README.md` (line 5)
- Build output: `build/win/nutshell.exe`

**Interfaces**
- Consumes: everything above.
- Produces: the committed exe at `1.2.37`, or one patch above `main`.

From a cloud or Linux session without MinGW, stop at Step 1. Push the branch and tell the maintainer, in one line, that the pipeline requires a successful `make test` and compiled code before it can merge, and that this cannot be done from a cloud instance.

- [ ] **Step 1: Bump.** Run `git fetch origin main && git show origin/main:src/ui/resource.h | sed -n 4,5p`. If `main` is still at `1.2.36`, set `src/ui/resource.h` lines 4-5 to:

```c
#define APP_VERSION        "1.2.37"
#define APP_VERSION_BINARY  1,2,37,0
```

and `README.md` line 5 to `**Version**: v1.2.37 \`. If `main` has moved, use one patch above it; after `x.y.99` comes `x.(y+1).0`.

- [ ] **Step 2: Build and test.** On the MSYS2 host, run `mingw32-make clean && mingw32-make release`. Expected: `build/win/nutshell.exe` is written with no warnings, under `-Werror`. Then run `mingw32-make test`, expected `Failed: 0`, and `mingw32-make wintest`, expected green. Run `build\win\nutshell.exe --ui-demo=policy-auto`, then `--ui-demo=policy-compact` with `--theme "Onyx Light"`, and check by eye: solid cells and a filled button at Write; R U W C with "Auto"; tooltips on each cell and on the button; the busy label stops before the button.

- [ ] **Step 3: Integration.** Run `tests\integration\Run-Integration.ps1 -Tier gate`. Expected: all `[PASS]`, including `ui_gallery` (60 captures) and `ai_auto_approve_critical_asks_first`. With the key in `tests\integration\.ai_key`, run `-Tier ai -Only ai_runs_read_command_unattended,ai_write_command_held_then_runs_after_allow` once. These are the two AI-tier cases that have never been run (spec section 7, notes item). Record the result in the PR body.

- [ ] **Step 4: Commit.**

```bash
git add src/ui/resource.h README.md build/win/nutshell.exe
git status --short
git commit -F - <<'EOF'
build: rebuild the exe for the Auto-approve button (v1.2.37)

Models: claude-sonnet-5 (implementation); claude-opus-5-5 (plan)

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
git push
```

Then follow CLAUDE.md "Branches, pull requests and the merge gate": `gh pr ready N`, `gh pr checks N --watch` until every check is green, CodeQL included, then `gh pr merge N --merge --auto`. Never merge with a red check.

---

## Self-review

**Spec coverage (sections 2-7):**

| Spec requirement | Task |
|---|---|
| s2 the table: off is `POLICY_NONE`, on is `== allowed` | 1 |
| s2 invariant at `cmd_policy_clamp`: from_token, the Settings default, the demo seed and the background copy all pass through it | 1 (clamp, from_token, `cmd_batch_add`); 12 (Settings save via setters); 13 (demo seed); 11 (`cmd_policy_attended`, which clamps) |
| s2 UI changes the policy only through `set_allowed`/`set_auto`; `set_unattended` stays for tests | 1; 9, 12 and 13 remove every UI call (grep in Steps 9/6/3) |
| s2 test: no `set_allowed`/`set_auto` sequence splits; clamp removes a split | 1 (`..._no_sequence_...`, `..._clamp_rounds_a_split_down`) |
| s2 split config loads as off and is rewritten | 1, 6 |
| s2 cell click → `set_allowed`; button → `set_auto(!on)` | 9 |
| s2 Critical confirmation, text, No keeps the policy | 1 (`needs_confirm`), 3 (text), 9 (panel), 12 (Settings), 16 (harness) |
| s2 re-entrancy: session recorded, tooltip hidden, guard, re-check after Yes | 9 (`session_gen`, `policy_confirm_open`, `TTM_POP`, policy re-check); 16 checks the guard |
| s2 token form kept; Settings tickbox; Allowed combo stays; default `read/none` | 6, 12; `test_config_default_review_fix_settings` (existing) |
| s2 ids: 4030-4033 and 4005 stay, 4015 toggles, 4040-4044 retired | 9; 16 checks 4040 |
| s2 harness helper `-Allowed <level> [-Auto]`, reads state first, `Wait-NutshellDialog` for Critical | 14, 16 |
| s3 order pill → meter/label → button; label rect from layout, used by `context_label` | 5, 9 |
| s3 five shrink tiers from both label sets and the meter widths | 5 |
| s3 `clip_right` on `ns_policy_layout`; clipped cells unhittable; paint inside `IntersectClipRect` | 4, 8 |
| s3 layout returns button, pill and clip, meter, label, compact; UI computes no rect | 5, 9 |
| s3 pill colours, solid when on, `ns_type_pill`, `STROKE_HAIRLINE`, `FONT_CAPTION`, `ns_scale` | 8, 9 (font) |
| s3 button paint, hover via `ns_hover`, no pressed state | 8, 9 |
| s3 `HIT_POLICY_AUTO` appended; pinned enum test | 4 |
| s3 `STATUS_HIT_COUNT = NS_POLICY_STOPS + 1`; button by rect test | 5 (`ai_status_hit`), 9 |
| s3 tests: tiers, thresholds both sides, no-overlap, hit and clipped cell | 4, 5 |
| s4 one rect tool per element, `TTM_NEWTOOLRECT` after relayout, callback text | 10 |
| s4 text in core: `cell_tip(level, compact, auto_on)`, `auto_tip(on, allowed)`; `cmd_policy_tip` removed | 3 |
| s4 `TTM_POP` before the box and on any policy change | 9 |
| s5 no keyboard path | none needed; 17 keeps the notes item |
| s6 no release on policy change | Global Constraints, 9 Step 3 (comment only, no approval pass) |
| s6 background batch built with button off | 2, 11 |
| s6 `do_session_switch` calls `maybe_start_next_batch` | 11 |
| s7 `test_cmd_policy`, `test_ai_panel`/`test_ns_layout`, `test_chat_approval`, loader tests | 1, 3, 4, 5, 2, 6 |
| s7 demo states `policy-auto` and `policy-compact` and their test; gallery in four themes | 7, 13, 15 |
| s7 gate tier with the new helper; held-write case run with `-Allowed Write`, no `-Auto` | 14, 15 |
| s7 new gate case for the box answered No | 16 |
| s7 the two never-run AI cases run once with the key before draft | 18 Step 3 |
| s9 notes Open list item | 17 |

Gaps found while mapping, and fixed inline:

- The context meter's tooltip lived on the whole-window tool, and the spec's per-element list omits it. Task 10 adds a sixth rect tool for it.
- `help_guide.c` describes the rail. Task 12 Step 5 rewrites it.
- The integration README and the `Run-Integration.ps1` case list are fixed in Task 16.
- The three other `-Unattended` call sites in `20-ai.ps1` are fixed in Task 15.

**Placeholder scan:** no "TBD", "TODO" or "similar to Task N", and no step without its code. Task 6 Step 2 says honestly that its tests pass on the first run and gives the mutation that proves they can fail. Tasks 8-13 cannot run a failing native test; each says so and names the harness task that covers it.

**Type and signature consistency:**
- `ns_policy_layout(NsRect, const int[4], int clip_right, int hit_h, int dpi, NsPolicyLayout*)` is the same in Tasks 4, 5, 9 and 10.
- `NsPolicyLayout.hit[]` and `.clip_right` are the same in Tasks 4, 8 and 10.
- `AiStatusLayout` fields `policy`, `policy_clip`, `auto_btn`, `meter_bar`, `meter_text`, `label`, `compact` and `tier` are the same in Tasks 5, 7, 9 and 10.
- `ai_status_layout(NsRect, int, const AiStatusMeasure*, AiStatusLayout*)` is the same in Tasks 5, 7 and 9.
- `ai_status_hit(const AiStatusLayout*, const NsPolicyLayout*, int, int, int*)` is the same in Tasks 5 and 9.
- `ns_draw_policy(hdc, l, policy, compact, hover_stop, tokens, font, dpi)` and `ns_draw_auto_button(hdc, const NsRect*, policy, compact, hover, tokens, font, dpi)` are the same in Tasks 8 and 9.
- `cmd_policy_cell_tip(int, int, int, char*, size_t)` and `cmd_policy_auto_tip(int, int, char*, size_t)` are the same in Tasks 3 and 10.
- `cmd_policy_status_word` bit layout `0x100 | level` is the same in Tasks 1, 9 and 14.
- `STATUS_HIT_AUTO == NS_POLICY_STOPS == 4` and `STATUS_TOOL_BASE + 4` are the same in Tasks 9 and 10.
- `IDC_AI_POLICY_AUTO` 3076 is the same in Task 12 for both `resource.h` and `settings.c`.
- `WM_USER + 104` is the same in Tasks 9 and 14.
- Running test counts: 2957 → 2966 → 2968 → 2974 → 2973 → 2974 → 2978 → 2982, then unchanged.

**Review Focus coverage:**

| Item | Test | Task |
|---|---|---|
| 1 | `test_ai_status_layout_thresholds_120_and_144` | 5 |
| 2 | `test_ai_status_layout_no_meter_goes_straight_to_tier3` | 5 |
| 3 | `test_ai_status_layout_width_sweep_invariants` | 5 |
| 3 | `test_ns_policy_layout_clip_left_of_pill_hits_nothing` | 4 |
| 4 | `test_policy_tips_fit_the_tooltip_buffer` | 3 |
| 5 | the second-toggle block of `ai_auto_approve_critical_asks_first` | 16 |

Every item has a concrete test in its owning task.
