# UNKNOWN as a fourth safety category, and a five-mode auto-approve

**Date**: 2026-09-11
**Also fixes**: security audit **C2** — the Linux SAFE tier is a fallthrough, so any command
the classifier has never seen classifies SAFE and auto-approves at the lowest auto-approve
level. The audit proposed dropping unrecognised commands to WRITE; this is the better
version of that fix, because "I do not recognise this" is not the same claim as "this writes".
**Builds on**: `2026-09-11-command-classification-coverage.md` (the rules) and
`2026-09-11-platform-plumbing-design.md` (getting the right rules to the right device).

---

## 1. The change in one paragraph

`CmdSafetyLevel` gains `CMD_UNKNOWN`, which is what a command gets when no rule claims it.
Both fallthroughs feed it: the Linux classifier's (today SAFE — the C2 bug) and every network
classifier's (today a conservative WRITE). Session auto-approve stops being a threshold and
becomes a **set** of permitted categories, because the five modes asked for are not nested:
"safe and write" deliberately excludes unknown.

---

## 2. `CmdSafetyLevel`

```c
typedef enum {
    CMD_SAFE     = 0,  /* provably read-only: matched an explicit safe rule */
    CMD_UNKNOWN  = 1,  /* no rule claimed it -- could be anything */
    CMD_WRITE    = 2,  /* modifies state, recoverable */
    CMD_CRITICAL = 3   /* outage, data loss or lock-out */
} CmdSafetyLevel;
```

`CMD_UNKNOWN` is inserted at 1, renumbering WRITE and CRITICAL. Nothing persists a numeric
safety level — `ApprovalEntry.safety` is runtime state and every other reference is symbolic —
so the renumber is safe. The ordering keeps `safety > CMD_SAFE` meaningful as "not provably
read-only", which is exactly the set that `permit_write` gates, so every existing
`> CMD_SAFE` test keeps working and keeps meaning the right thing.

**`permit_write` gates UNKNOWN**, like WRITE and CRITICAL. With Read + write off, a session
runs only what is provably read-only. This is a real tightening: commands that today fall
through to SAFE and run will now be blocked in a read-only session unless they are on the
allow-list. That is the point of C2, and the allow-list in §3 is sized to match.

---

## 3. Linux: SAFE becomes an allow-list

`classify_linux_segment()` currently ends `return redir;` — SAFE unless something claimed it.
It now ends in `CMD_UNKNOWN`, and a command reaches SAFE only by matching an explicit rule:

- the existing safe subcommand rules (`systemctl status`, `git log`, `docker ps`, `kubectl
  get`, `crontab -l`, the package-manager query forms, `ufw status`, …), and
- a new `linux_safe_cmds[]` table, which is §3.3 of the coverage spec lifted verbatim: the
  file readers, the text tools, the process/memory/disk inspectors, the network diagnostics,
  the package queries, the read-only container and orchestration verbs.

Redirects still raise: `cat x > y` is WRITE, not SAFE, because `scan_redirects()` runs first
and the result is the max of the two.

A command on the allow-list that is *also* matched by a write or critical rule keeps the
higher level — the allow-list is consulted last, not first, so `tar` (write list) and
`find ... -delete` (critical) are unaffected by anything in it.

**Network platforms** end in `CMD_UNKNOWN` too, replacing their `CMD_WRITE` fallthroughs. An
unrecognised line on a switch is now honestly labelled rather than guessed at. Operators who
want the old behaviour pick the "safe, unknown and write" mode.

---

## 4. Aggregation: a mask, not a maximum

A pipeline is classified per segment. With non-nested modes, collapsing segments to a maximum
loses the information the gate needs: `unknown-thing | grep x` has segments {UNKNOWN, SAFE},
maximum UNKNOWN — fine — but `unknown-thing | tee /etc/f` has {UNKNOWN, WRITE}, maximum WRITE,
and under "safe and write" that would auto-approve **despite containing an unknown segment**.

So classification exposes both:

```c
/* Bit per category present across the command's segments. */
#define CMD_MASK_OF(level)  (1u << (unsigned)(level))
unsigned cmd_classify_mask(const char *command, CmdPlatform platform);
```

- `cmd_classify()` keeps returning the maximum, which is what the approval card's chip shows.
- `cmd_classify_mask()` returns the set, which is what the auto-approve gate tests.
- The `worst == CMD_CRITICAL` early-out in `cmd_classify_ex()` goes away: it would truncate
  the mask. Scanning every segment costs nothing measurable.

`ApprovalEntry` carries `unsigned safety_mask` alongside `safety`.

---

## 5. The five modes

```c
typedef enum {
    AUTO_APPROVE_SAFE               = 0,  /* safe only                        */
    AUTO_APPROVE_SAFE_UNKNOWN       = 1,  /* safe + unknown                   */
    AUTO_APPROVE_SAFE_WRITE         = 2,  /* safe + write                     */
    AUTO_APPROVE_SAFE_UNKNOWN_WRITE = 3,  /* safe + unknown + write           */
    AUTO_APPROVE_ALL                = 4   /* safe + unknown + write + critical */
} AutoApproveLevel;

unsigned auto_approve_mask(AutoApproveLevel level);  /* SAFE is in every mode */
```

A command auto-approves when **every** category it contains is permitted:

```c
(entry->safety_mask & ~auto_approve_mask(q->auto_approve_level)) == 0
```

Note what this fixes beyond the new category: the old test let CRITICAL through whenever the
level was `AUTO_APPROVE_ALL` *regardless of the other segments*, and let WRITE through on a
`>=` comparison. The mask form states the rule once, for all four categories.

The old `AUTO_APPROVE_WRITE = 1` is gone. Its value is now `SAFE_UNKNOWN`, so anything that
persisted the old number must be migrated rather than reinterpreted — see §7.

---

## 6. UI

| Surface | Change |
|---|---|
| `chat_listview.c` safety chip | `CMD_UNKNOWN` → label `"UNKNOWN"`, colours from `ns_tokens()->info` (`.base` / `.label`) — the design system's "needs attention, not yet alarming" intent. No new literals. |
| `ai_panel_layout.c` `ai_modes_label()` | `"off"`, `"safe only"`, `"safe + unknown"`, `"safe + write"`, `"safe + unknown + write"`, `"all"` |
| `ai_chat.c` modes-segment click | cycles 0→1→2→3→4→off, instead of 0→1→2→off |
| `settings.c` Auto Approve combo | six items: `Off`, `Safe only`, `Safe + unknown`, `Safe + write`, `Safe + unknown + write`, `All` |

The chip text is already sized for `"CRITICAL"`, so `"UNKNOWN"` needs no layout change.

---

## 7. Config, and migrating the old setting

`Settings.ai_auto_approve_default` stays an `int`, `0 = off` and `1..5` meaning
`AutoApproveLevel + 1`, which preserves the existing `ai_chat.c` convention. What changes is
how it persists: as a **string token** under a new key, matching the platform setting.

| Token | Meaning |
|---|---|
| `off` | auto-approve inactive |
| `safe` | safe only |
| `safe+unknown` | |
| `safe+write` | |
| `safe+unknown+write` | |
| `all` | |

Mappings live in `chat_approval.c` so they are natively testable:

```c
int         auto_approve_mode_from_name(const char *name);  /* -> 0..5, 0 on NULL/garbage */
const char *auto_approve_mode_name(int mode0to5);
const char *auto_approve_mode_label(int mode0to5);          /* UI label */
```

**Migration.** The loader reads the new string key `ai_auto_approve_mode` when present.
When it is absent it falls back to the old numeric `ai_auto_approve_default`, whose four
values map: `0 → off`, `1 → safe`, `2 → safe+write`, `3 → all`. Note that old value 2 must
become new value 3 (`safe+write`) and **not** new value 2, which is now `safe+unknown`. Save
writes the new key only; the old key is not written back.

---

## 8. Test plan

- `tests/test_cmd_classify.c` — an unrecognised command → `CMD_UNKNOWN` on Linux and on each
  network platform; every category of the §3 allow-list still → SAFE; `cat x > y` → WRITE
  (redirect beats the allow-list); `find / -delete` → CRITICAL (critical rules beat it);
  mask tests: `ls | grep x` → {SAFE}; `frobnicate | grep x` → {SAFE, UNKNOWN};
  `frobnicate | tee /etc/f` → {SAFE?, UNKNOWN, WRITE} with the maximum still WRITE.
- `tests/test_chat_approval.c` — the five modes against all four categories, as a table: 20
  cases asserting auto-approve or not; the mixed-pipeline case that motivated the mask
  (`{UNKNOWN, WRITE}` must NOT auto-approve under `SAFE_WRITE`); UNKNOWN blocked when
  `permit_write` is 0; `chat_approval_unblock_all` / `block_pending_writes` with UNKNOWN
  entries.
- `tests/test_config.c` — the new token round-trips; each of the four legacy numeric values
  migrates to the right mode, with `2 → safe+write` called out; a garbage token → off.
- `tests/test_ai_panel.c` (or wherever `ai_modes_label` is covered) — a label per mode.
