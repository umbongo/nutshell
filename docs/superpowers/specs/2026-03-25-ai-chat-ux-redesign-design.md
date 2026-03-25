# AI Chat Panel UX Redesign — Design Specification

**Date:** 2026-03-25
**Branch:** ai_assist_ui
**Status:** Draft

---

## 1. Overview

Replace the current RichEdit-based AI chat display with a custom owner-drawn scrollable message list. This gives full control over message layout, inline controls (command approval buttons, collapsible thinking regions), and visual styling. The goal is a ChatGPT/Copilot-like experience within Nutshell's side panel.

### Design Principles

- **Security-first**: Command safety classification with permit_write gating. Input sanitization on all AI-provided content. No arbitrary code execution without explicit user approval.
- **Test-driven development**: Comprehensive unit tests for all new modules covering typical use cases and corner cases. Tests written before implementation.
- **Modular architecture**: Each concern (message list, command classification, thinking display, activity monitoring) is a separate module with clean interfaces, permitting easy integration of new functionality.
- **Minimal coupling**: New modules depend on abstract interfaces, not concrete Win32 controls. Core logic (command classification, message model) is portable C testable on Linux.

---

## 2. Architecture

### 2.1 Message Item Model

Replace the single RichEdit control with a structured message list. Each conversation element is a discrete `ChatMsgItem` in a doubly-linked list.

```c
typedef enum {
    CHAT_ITEM_USER,      /* User prompt */
    CHAT_ITEM_AI_TEXT,   /* AI response with markdown */
    CHAT_ITEM_THINKING,  /* Collapsible thinking region (child of AI item) */
    CHAT_ITEM_COMMAND,   /* Command block with approve/deny */
    CHAT_ITEM_STATUS     /* System messages, errors, denied notices */
} ChatItemType;

typedef struct ChatMsgItem {
    ChatItemType type;
    int id;                      /* Unique item ID */
    int measured_height;         /* Cached pixel height */
    int dirty;                   /* 1 = needs remeasure/repaint */
    char *text;                  /* Heap-allocated content (UTF-8) */
    size_t text_len;

    /* Type-specific fields */
    union {
        struct {
            char *thinking_text;     /* Heap-allocated thinking content */
            int thinking_collapsed;  /* 1 = collapsed (default) */
            float thinking_elapsed;  /* Seconds spent thinking */
            int thinking_complete;   /* 1 = streaming finished */
        } ai;
        struct {
            char command[1024];
            int safety_tier;         /* 1=critical, 2=high, 3=moderate, 4=low */
            int requires_permit;     /* 1 = needs permit_write */
            int approved;            /* -1=pending, 0=denied, 1=approved */
            int blocked;             /* 1 = blocked by permit_write=off */
        } cmd;
    } u;

    struct ChatMsgItem *next;
    struct ChatMsgItem *prev;
} ChatMsgItem;
```

### 2.2 Module Breakdown

| Module | File(s) | Responsibility | Testable on Linux |
|--------|---------|---------------|-------------------|
| **Message Model** | `chat_msg.h/c` | Item creation, list management, serialization | Yes |
| **Command Classifier** | `cmd_classify.h/c` | Command safety classification across all platforms | Yes |
| **Markdown Renderer** | `md_render.h/c` | Markdown-to-GDI rendering (abstract paint interface) | Partial (layout logic yes, GDI no) |
| **Message List View** | `chat_listview.h/c` | Owner-draw scrollable panel, hit testing, virtual scroll | No (Win32) |
| **Thinking Controller** | `chat_thinking.h/c` | Thinking state machine, collapse/expand, streaming | Yes |
| **Activity Monitor** | `chat_activity.h/c` | Heartbeat tracking, stall detection, phase transitions | Yes |
| **Command Approval** | `chat_approval.h/c` | Approval flow state machine, session-level auto-approve | Yes |
| **AI Chat Window** | `ai_chat.c` (existing) | Integrates all modules, handles Win32 messages | No (Win32) |

### 2.3 Rendering Pipeline

```
User action / AI stream chunk
        |
        v
  ChatMsgItem list updated
        |
        v
  Dirty items remeasured (height recalculated)
        |
        v
  Total content height updated -> scrollbar range set
        |
        v
  InvalidateRect on visible viewport
        |
        v
  WM_PAINT: iterate visible items, call per-type paint function
        |
        v
  Child HWNDs (buttons) positioned via MoveWindow during paint
```

**Virtual scrolling**: Only items intersecting the visible viewport are painted. A running `y_offset` accumulator determines which items are visible given the current scroll position. This keeps performance O(visible items) regardless of conversation length.

---

## 3. Visual Layout

### 3.1 Message Styling — Bubble Hybrid

**User messages**: Right-aligned rounded bubble with accent background color. No avatar. Text is white/light on the accent bubble.

```
                          ┌─────────────────────┐
                          │ List files in /etc/  │
                          └─────────────────────┘
```

**AI messages**: Left-aligned with avatar circle + name. Full width for content, command blocks, and thinking regions.

```
  (N) Nutshell
      > Thinking (2.1s)        [collapsed, clickable]
      I'll list the directory.
      ┌─ Command ──────────── read-only ─┐
      │ ls -la /etc/                      │
      ├──────────────────────────────────-┤
      │ [Allow]  [Deny]                   │
      └──────────────────────────────────-┘
```

**Status messages**: Centered, italic, dimmed. Used for "[commands denied]", connection errors, etc.

### 3.2 Spacing and Typography

- **Inter-message gap**: 12px (DPI-scaled)
- **User bubble padding**: 10px horizontal, 8px vertical
- **AI content left indent**: 30px (avatar width + gap)
- **Font**: Inherits from Nutshell's configured UI font
- **Code blocks**: Monospace font (ai_font_name), dark background fill, 6px padding
- **Headers ("You", "Nutshell")**: Bold, 11pt, role-specific color

### 3.3 Theme Integration

All colors sourced from `ThemeColors` struct. New fields added:

```c
/* Chat message colors (added to ThemeColors) */
COLORREF chat_user_bubble;      /* User bubble background */
COLORREF chat_user_text;        /* User bubble text */
COLORREF chat_ai_accent;        /* AI avatar and name color */
COLORREF chat_cmd_bg;           /* Command block background */
COLORREF chat_cmd_border;       /* Command block border */
COLORREF chat_cmd_text;         /* Command text (monospace) */
COLORREF chat_thinking_border;  /* Thinking region left border */
COLORREF chat_thinking_text;    /* Thinking content text */
COLORREF chat_status_text;      /* Status message text */
COLORREF chat_indicator_green;  /* Healthy activity dot */
COLORREF chat_indicator_yellow; /* Slow activity dot */
COLORREF chat_indicator_red;    /* Stalled activity dot */
```

---

## 4. Collapsible Thinking

### 4.1 States

| State | Display | Behavior |
|-------|---------|----------|
| **Collapsed (default)** | `> Thinking (2.1s)` | Single clickable line. Arrow points right. |
| **Expanded** | `v Thinking (2.1s)` + content region | Scrollable region with left border accent. Max 300px height. |
| **Streaming collapsed** | `> Thinking... (4.2s)` | Timer updates live. Content accumulates in background. |
| **Streaming expanded** | `v Thinking... (4.2s)` + live content | Auto-scrolls to bottom as chunks arrive. |
| **Complete** | Arrow + `Thinking (7.8s)` | Final elapsed time. No more updates. |

### 4.2 Behavior Rules

1. **Default collapsed** — new thinking regions start collapsed.
2. **Toggle persistence** — last toggle state carries to the next AI message. If user expanded thinking on message 3, message 4 still starts collapsed (default) but user preference is remembered if they explicitly set it.
3. **Mid-stream toggle** — user can collapse/expand at any time during streaming. Content continues accumulating regardless.
4. **Thinking region is a child of the AI message item** — not a separate top-level item. This keeps the visual hierarchy clear.
5. **Elapsed time** — tracked from first thinking token to last. Updated every 100ms during streaming via timer.
6. **Max height**: 300px (DPI-scaled). Internal vertical scroll if content exceeds this.
7. **Min height**: 30px (DPI-scaled) when expanded with minimal content.

### 4.3 Rendering

- Left border: 3px solid, theme `chat_thinking_border` color
- Background: slightly darkened/lightened from panel background (theme-dependent)
- Text: plain monospace, theme `chat_thinking_text` color
- No markdown rendering in thinking — plain text only

---

## 5. Command Blocks and Approval Flow

### 5.1 Command Block Layout

Each command proposed by the AI gets its own inline block:

```
┌─ Command ─────────────────── [safety tag] ─┐
│                                             │
│  $ ls -la /etc/nginx/                       │  <- monospace, scrollable
│  $ cat /etc/nginx/nginx.conf                │     if multi-line
│                                             │
├─────────────────────────────────────────────┤
│  [Allow]  [Deny]                            │  <- inline buttons
└─────────────────────────────────────────────┘
```

**Safety tag** in top-right corner:
- `read-only` — grey, no permit_write needed
- `write` — orange, requires permit_write
- `critical` — red, requires permit_write + extra visual warning

### 5.2 Blocked Commands

When `permit_write = 0` and a command is classified as write/critical:

```
┌─ Command ──────────────── [blocked] ─┐
│                                       │
│  🔒 rm -rf /tmp/build/               │  <- greyed out text
│                                       │
│  Enable "Permit Write" to approve     │  <- help text
└───────────────────────────────────────┘
```

- No Allow/Deny buttons shown
- Command text greyed out with lock icon
- Help text explains what to do
- The AI receives a system message explaining the block (existing behavior, preserved)

### 5.3 Multiple Commands

When an AI response contains multiple commands:

```
  ┌─ Allow All (3 commands) ─┐    <- ghost/outline button, harder to click
  └──────────────────────────┘

  ┌─ Command 1 ─── read-only ─┐
  │ ls -la /etc/               │
  │ [Allow]  [Deny]            │
  └────────────────────────────┘

  ┌─ Command 2 ─── write ─────┐
  │ cp nginx.conf nginx.bak   │
  │ [Allow]  [Deny]            │
  └────────────────────────────┘

  ┌─ Command 3 ─── read-only ─┐
  │ cat /var/log/nginx/err.log │
  │ [Allow]  [Deny]            │
  └────────────────────────────┘

  Allow all commands this session  <- small text link
```

### 5.4 "Allow All Session" Flow

1. User clicks `Allow all commands this session` text link
2. First click: text changes to `Are you sure? Click again to confirm`
3. Second click within 3 seconds: auto-approve activated
4. Banner appears at top of chat panel: `⚠ Auto-approve active [revoke]`
5. All subsequent commands auto-execute without approval prompts
6. Clicking `[revoke]` disables auto-approve and removes the banner
7. Auto-approve resets on new chat session

### 5.5 Approval State Machine

```
                ┌──────────┐
                │ PENDING  │ <- commands extracted from AI response
                └────┬─────┘
                     │
            ┌────────┼────────┐
            v        v        v
      ┌─────────┐ ┌──────┐ ┌─────────┐
      │APPROVED │ │DENIED│ │ BLOCKED │ <- permit_write=0
      └────┬────┘ └──┬───┘ └─────────┘
           │         │
           v         v
      ┌─────────┐ ┌──────────────┐
      │EXECUTING│ │ Status msg:  │
      └────┬────┘ │"cmd denied"  │
           │      └──────────────┘
           v
      ┌──────────┐
      │COMPLETED │
      └──────────┘
```

---

## 6. Activity Indicator

### 6.1 Dual Placement

**Header bar indicator** (always visible):
- Compact: pulsing dot + one-word status
- Visible even when scrolled up in conversation

**Inline indicator** (below last AI message):
- Full status text with phase detail
- Scrolls with conversation

### 6.2 Phases

| Phase | Inline text | Trigger |
|-------|------------|---------|
| Processing | `● Processing...` | Request sent, no tokens yet |
| Thinking | `● Thinking...` | First thinking token received |
| Responding | `● Responding...` | First content token received |
| Executing | `● Executing 2/5...` | Commands being run |
| Waiting | `● Waiting for output...` | Commands done, collecting output |

### 6.3 Health Detection

A heartbeat timer fires every 1 second and checks `time_since_last_token`:

| Duration | Dot color | Text modifier |
|----------|-----------|--------------|
| 0–10s | Green (pulsing) | Normal status text |
| 10–30s | Yellow (pulsing) | Status + `(slow)` |
| 30s+ | Red (static) | `● Stalled — no response for Xs` + `[Retry]` link |
| Connection lost | Red (static) | `● Connection lost` + `[Retry]` |

**Pulsing animation**: Opacity cycles 1.0 -> 0.3 -> 1.0 over 1.5 seconds via Win32 timer. Implemented as alpha-blended circle draw (or alternating between two pre-rendered bitmaps for performance).

### 6.4 Retry Behavior

Clicking `[Retry]`:
1. Cancels the current HTTP request (if still open)
2. Re-sends the last user message to the API
3. Resets the activity indicator to "Processing..."
4. Appends a status message: "[retrying request]"

---

## 7. Command Safety Classification

### 7.1 Architecture

The classifier is a standalone portable C module (`cmd_classify.h/c`) with no Win32 dependencies. It takes a command string and returns a safety classification.

```c
typedef enum {
    CMD_SAFE,       /* Read-only, always allowed */
    CMD_WRITE,      /* Modifies state, requires permit_write */
    CMD_CRITICAL    /* Can cause outage/data loss, requires permit_write + visual warning */
} CmdSafetyLevel;

typedef enum {
    CMD_PLATFORM_LINUX,
    CMD_PLATFORM_CISCO_IOS,
    CMD_PLATFORM_CISCO_NXOS,
    CMD_PLATFORM_CISCO_ASA,
    CMD_PLATFORM_ARUBA_CX,
    CMD_PLATFORM_ARUBA_OS,
    CMD_PLATFORM_PANOS
} CmdPlatform;

/* Classify a single command string.
 * platform: target device platform (affects classification rules).
 * Returns safety level. */
CmdSafetyLevel cmd_classify(const char *command, CmdPlatform platform);

/* Classify with detail: fills reason buffer with human-readable explanation.
 * reason_buf: output buffer, reason_buf_size: buffer size.
 * Returns safety level. */
CmdSafetyLevel cmd_classify_ex(const char *command, CmdPlatform platform,
                                char *reason_buf, size_t reason_buf_size);
```

### 7.2 Classification Strategy

**Two-pass approach:**

1. **Prefix match** — check the first token(s) of the command against platform-specific tables. Fast path for the common case.
2. **Pattern scan** — for commands that require deeper inspection (e.g., shell redirects, SQL statements in `mysql -e "..."`, subcommands like `systemctl stop` vs `systemctl status`).

**Platform detection**: The classifier does not auto-detect platforms. The UI sets the platform based on the connected session's device type (configurable in session settings, with auto-detect hints from login banners).

### 7.3 Linux Classification Rules

**Critical commands** (immediate outage/data loss risk):
- `rm`, `shred`, `truncate`, `dd`, `mkfs*`, `wipefs`
- `reboot`, `shutdown`, `poweroff`, `halt`, `init 0/6`
- `kill -9`, `killall`, `pkill`
- `fdisk`, `gdisk`, `parted`, `lvremove`, `vgremove`, `pvremove`, `zpool destroy`, `zfs destroy`
- `iptables -F`, `iptables -P INPUT DROP`, `nft flush`, `ufw reset`
- `ip link set * down`, `ip route del default`, `ip route flush`
- `docker rm -f`, `docker system prune`, `kubectl delete`
- `systemctl stop`, `systemctl disable`, `systemctl mask`
- Database destructive: `DROP`, `DELETE`, `TRUNCATE` in mysql/psql/mongo/redis CLI

**Write commands** (modifies state):
- File: `mv`, `cp`, `mkdir`, `chmod`, `chown`, `ln`, `touch`, `rsync`
- Package: `apt install/remove`, `yum install/remove`, `dnf`, `pacman`, `pip install`, `npm install -g`
- Service: `systemctl start/restart/reload/enable`
- User: `useradd`, `usermod`, `userdel`, `passwd`, `groupadd`
- Network: `ip addr add/del`, `ip route add`, `iptables -A/-I/-D`, `firewall-cmd --add-*`
- Build: `make`, `gcc`, `cmake`, `cargo build`, `go build`
- Container: `docker run/build/exec`, `kubectl apply/create/edit/patch/scale`
- Git write: `git push`, `git reset`, `git rebase`, `git merge`

**Special patterns requiring deeper scan:**
- Shell redirects: `>`, `>>`, `&>` (except `>/dev/null`, `2>/dev/null`, `2>&1`)
- Pipe to destructive: `| xargs rm`, `| sh`, `| bash`
- `sudo`/`su` prefix: escalates any command
- `sed -i`, `perl -pi -e`: in-place file editing
- `curl -o`/`wget -O`: downloading to file

### 7.4 Cisco IOS/IOS-XE Classification Rules

**Critical:**
- `reload`, `write erase`, `erase startup-config`, `erase nvram:`
- `delete flash:`, `format flash:`, `config replace`
- `shutdown` (interface context)
- `no router ospf/eigrp/bgp`, `clear ip bgp *`, `clear ip ospf process`
- `no spanning-tree vlan`, `spanning-tree mode`
- `no vlan`, VTP mode changes
- `clear crypto sa`, `clear crypto isakmp`
- `redundancy force-switchover`

**Write:**
- `configure terminal`
- `write memory`, `copy running-config startup-config`
- `ip address`, `switchport mode/access vlan`, `channel-group`
- `router ospf/bgp`, `network`, `ip route`, `route-map`, `prefix-list`
- `access-list`, `ip access-group`, `username`, `enable secret`
- `crypto isakmp/ipsec`, `tunnel`
- `hostname`, `banner`, `ntp server`, `logging host`
- `copy tftp: flash:`, `boot system`

**Safe:**
- All `show` commands
- `ping`, `traceroute`
- `terminal length/width/monitor`
- `enable`, `disable`, `exit`, `end`
- `dir`, `verify`

### 7.5 Cisco NX-OS Classification Rules

All IOS rules apply, plus:

**Critical:**
- `reload module`, `install all nxos`
- `no vpc`, `no vpc domain`, `vpc role preempt`
- `no feature nv overlay`, `no nv overlay evpn`
- `system vlan reserve`

**Write:**
- `feature`/`no feature`
- `checkpoint`, `rollback running-config checkpoint`

### 7.6 Cisco ASA Classification Rules

**Critical:**
- `reload`, `write erase`, `clear configure all`, `configure factory-default`
- `no failover`, `failover active`, `failover reload-standby`
- `shutdown` (interface), `no nameif`
- `clear crypto ipsec sa`, `clear crypto isakmp sa`
- `vpn-sessiondb logoff all`, `clear webvpn session all`
- `no context` (multi-context)

**Write:**
- `configure terminal`, `write memory`
- `nat`, `access-list`, `access-group`, `object-group`
- `route`, `router ospf/eigrp`
- `tunnel-group`, `group-policy`, `crypto ikev2`
- `username`, `aaa authentication`
- `policy-map`, `service-policy`
- `security-level`, `nameif`

### 7.7 Aruba OS-CX Classification Rules

**Critical:**
- `reload`, `erase startup-config`, `erase all zeroize`
- `checkpoint rollback`, `boot set-default`
- `no vsx`, `no stacking`, `vsx-sync reset`
- `no spanning-tree`, `delete` (flash files)
- `redundancy switchover`

**Write:**
- `configure terminal`, `write memory`
- `interface` + `shutdown/no shutdown`, `ip address`, `routing`
- `vlan`/`no vlan`, trunk changes
- `router ospf/bgp`, `ip route`, `ip access-list`
- `user`/`no user`, `radius-server`, `aaa authentication`
- `hostname`, `clock set`, `ntp server`
- `interface lag`, `mirror`

### 7.8 ArubaOS (Wireless Controllers) Classification Rules

**Critical:**
- `reload`, `write erase`, `write erase all`, `factory-reset`
- `cluster reset`, `no cluster-profile`
- `apboot`, `ap wipe out`, `clear ap`, `whitelist-db del`
- `delete flash:`, `no vrrp`
- Crypto PKI delete/destroy operations

**Write:**
- `configure terminal`, `write memory`
- `ap-group`, `wlan`, `virtual-ap`, `ap radio-profile`
- `interface vlan`, `ip address`, `vlan`, `trunk`
- `aaa authentication/authorization`, `user-role`, `ip access-list session`
- `ip route`, `router ospf/bgp`
- `hostname`, `ntp server`, `snmp-server`
- `clear datapath session`, `clear dot1x`, `clear user`
- `backup`, `restore`

### 7.9 Palo Alto PAN-OS Classification Rules (Most Thorough)

**Critical (Tier 1):**
- `commit`, `commit force`, `commit-all`, `commit partial`
- `delete` (any, in config mode)
- `rollback`, `load config from/version/last-saved`
- `request restart system/dataplane/management-plane`
- `request shutdown system`
- `request system software install`
- `request system private-data-reset`
- `request certificate delete`
- `request license deactivate`
- `request content upgrade install`, `request anti-virus upgrade install`
- `request wildfire upgrade install`, `request url-filtering upgrade`
- `request high-availability state suspend/functional`
- `request high-availability sync-to-remote`
- `request global-protect-gateway client-logout-all`
- `request batch` (Panorama)
- `clear session all`
- `clear log` (all variants)
- `debug software restart`, `debug dataplane reset`
- `debug system maintenance-mode`
- `delete network interface/zone`, `delete shared log-settings`
- `delete vsys`, `delete global-protect`

**Write (Tier 2):**
- `configure` (entering config mode)
- `set` (any, in config mode)
- `edit`, `rename`, `copy`, `move`, `override`
- `revert`, `save config`, `save named-snapshot`
- `import`, `scp import`
- `request system software download/delete`
- `request certificate generate/import`
- `request license activate/fetch`
- `request content/anti-virus/wildfire upgrade download`
- `request global-protect-gateway client-logout` (specific user)
- `clear session id/filter`, `clear counter`, `clear arp/mac/ndp`
- `clear dns-proxy cache`, `clear user-id-agent/user-cache`
- `clear global-protect-gateway/portal`
- All `set rulebase`, `set network`, `set deviceconfig`, `set mgt-config`
- `set global-protect`, `set user-id-collector`, `set group-mapping`

**Safe:**
- All `show` commands
- `less`, `diff`, `find command keyword`
- `ping`, `traceroute`, `nslookup`
- `test security-policy-match`, `test nat-policy-match`, `test routing fib-lookup`
- `commit validate` (validate only, no apply)
- `request license info`
- `tail follow`
- `scp export`
- `exit`, `quit`, `top`, `up`

**PAN-OS special notes:**
- Candidate/running config model: `set`/`delete` modify candidate only, `commit` activates. Both must be blocked.
- `request` commands are immediate (no commit step).
- Panorama multiplier: `commit-all` and `request batch` affect all managed firewalls.
- Pipe modifiers (`| match`, `| except`) do not change safety level.

### 7.10 Cross-Platform Heuristics

For commands not in explicit lists, apply these fallback rules:

1. **Config mode detection**: If device is in config mode (prompt contains `#`, `(config)`, etc.), treat all input as write.
2. **Negation**: `no <anything>` is write/critical on network devices.
3. **`show`/`display` prefix**: Always safe.
4. **`clear` prefix**: Always write (minimum).
5. **`reload`/`reboot`/`restart`/`shutdown`**: Always critical.
6. **`delete`/`erase`/`format`/`purge`**: Always critical.
7. **`copy`/`write` where destination is config/flash**: Write.
8. **`sudo`/`su` prefix on Linux**: Escalates the classification of the following command.

---

## 8. Testing Strategy

### 8.1 Approach

Test-driven development: tests written before implementation for each module. All portable C modules are tested on Linux using the existing Nutshell test framework (865+ existing tests).

### 8.2 Test Modules

#### Command Classifier Tests (`test_cmd_classify.c`)

**Typical cases per platform:**
- Known safe commands return `CMD_SAFE`
- Known write commands return `CMD_WRITE`
- Known critical commands return `CMD_CRITICAL`
- Platform-specific commands classified correctly

**Corner cases:**
- Empty string input
- NULL input (should not crash)
- Commands with leading/trailing whitespace
- Commands with path prefixes (`/usr/bin/rm` -> `rm`)
- Commands with sudo/su prefix escalation
- Pipe chains: safest classification of most dangerous segment
- Shell redirects: `echo foo > /tmp/bar` classified as write
- Redirect exceptions: `2>/dev/null`, `>/dev/null` do not trigger write
- Quoted strings containing redirect characters (not actual redirects)
- SQL in database CLI: `mysql -e "SELECT ..."` vs `mysql -e "DROP ..."`
- Case sensitivity: `SHOW` vs `show` on network devices
- Subcommand sensitivity: `systemctl status` (safe) vs `systemctl stop` (critical)
- Flag sensitivity: `sed` (safe) vs `sed -i` (write), `curl` (safe) vs `curl -o` (write)
- Commands with semicolons: `ls; rm -rf /` — most dangerous wins
- Commands with `&&`/`||`: compound classification
- Very long commands (buffer boundary testing)
- Commands with unicode/non-ASCII characters
- Network device config mode commands: `set`, `no`, `delete` in isolation
- PAN-OS `commit validate` (safe) vs `commit` (critical)
- PAN-OS `commit force` and `commit-all` (critical)
- PAN-OS `request license info` (safe) vs `request license deactivate` (critical)

#### Message Model Tests (`test_chat_msg.c`)

**Typical cases:**
- Create each item type and verify fields
- Append items to list, verify order
- Remove items from list, verify integrity
- Update item text, verify dirty flag set
- Measure height calculation for known text content

**Corner cases:**
- Empty message text
- Very long message text (>64KB)
- Rapid append (100+ items)
- Remove from single-item list
- Remove head/tail of list
- Double-free protection
- UTF-8 multibyte text handling
- Text with embedded null bytes (should be rejected/truncated)

#### Thinking Controller Tests (`test_chat_thinking.c`)

**Typical cases:**
- Start thinking -> receive chunks -> complete: elapsed time correct
- Toggle expand/collapse
- Expand during streaming, verify content visible
- Collapse during streaming, content still accumulates
- Re-expand shows all accumulated content

**Corner cases:**
- Toggle before any thinking received
- Toggle rapidly (debounce)
- Thinking with zero-length content
- Extremely long thinking content (>AI_MSG_MAX)
- Thinking complete with no content tokens following
- Multiple thinking phases in one response

#### Activity Monitor Tests (`test_chat_activity.c`)

**Typical cases:**
- Phase transitions: processing -> thinking -> responding -> executing -> waiting
- Heartbeat: green within 10s, yellow 10-30s, red 30s+
- Token received resets heartbeat timer

**Corner cases:**
- Phase transition from processing directly to responding (no thinking)
- Stall detection at exact threshold boundaries (10s, 30s)
- Connection lost detection
- Retry resets all state
- Multiple rapid token arrivals
- Timer overflow (very long-running request)

#### Command Approval Tests (`test_chat_approval.c`)

**Typical cases:**
- Single command: approve -> execute
- Single command: deny -> status message
- Multiple commands: individual approve/deny
- Multiple commands: allow all
- Session auto-approve: enable, verify commands auto-execute
- Session auto-approve: revoke, verify prompts resume
- Blocked command: permit_write=0 with write command

**Corner cases:**
- Approve after permit_write toggled off (should re-check)
- Auto-approve with critical commands (should still auto-approve — user explicitly opted in)
- Empty command string
- Command with only whitespace
- Approval timeout (user never responds)
- Session switch while commands pending
- Rapid allow/deny clicking (debounce)
- "Are you sure" confirmation timeout (>3 seconds = reset)

### 8.3 Security-Specific Tests

- Command injection: AI-provided text containing shell metacharacters in display (should render as text, never execute)
- XSS-equivalent: AI-provided markdown with embedded control characters
- Buffer overflow: command strings at exactly 1024 bytes (field size limit)
- Buffer overflow: text content exceeding AI_MSG_MAX
- Format string safety: AI text containing `%s`, `%n`, etc. rendered literally
- NULL byte injection in command strings
- Memory leak: create and destroy 1000 message items, verify no leaks
- Double-free: destroy list, then attempt to access items
- Concurrent access: simulate stream chunks arriving while user toggles thinking (thread safety with CRITICAL_SECTION)

---

## 9. Security Considerations

### 9.1 Input Sanitization

All AI-provided text is treated as untrusted:
- Command strings: validated against `cmd_classify()` before any execution path
- Display text: rendered via GDI text functions (no HTML/script interpretation)
- No `printf`-family with AI text as format string — always use `%s` or `snprintf`
- UTF-8 validation before display (reject malformed sequences)
- Command strings truncated to field size (1024 bytes) with null terminator

### 9.2 Permit Write Model

- **Default off**: `permit_write = 0` on session start
- **Visual indicator**: Button with red/green dot (existing, preserved)
- **Scope**: Per-session — toggling permit_write in one session doesn't affect others
- **Enforcement point**: Both in UI (greyed commands, no Allow button) AND in execution path (belt-and-suspenders)
- **AI notification**: When commands are blocked, a system message is injected into the conversation telling the AI which commands were blocked and why. This prevents the AI from claiming commands were executed.

### 9.3 Auto-Approve Safety

- Requires deliberate double-click confirmation with timeout
- Visual banner makes the state obvious
- Revocable at any time
- Resets on new session
- Even with auto-approve, `permit_write` still gates dangerous commands — auto-approve only skips the Allow/Deny prompt, not the safety classification

### 9.4 Memory Safety

- All heap allocations checked (xmalloc pattern)
- Secrets (API keys) never stored in message items
- Thinking text zeroed before free (may contain sensitive reasoning)
- Command strings zeroed before free (may contain passwords/keys in arguments)
- Fixed-size buffers use `snprintf` exclusively (no `strcpy`/`strcat`)

---

## 10. Migration Path

### 10.1 Incremental Approach

The rewrite is contained within the display/rendering layer. The external API (`ai_chat.h`) remains unchanged. Migration plan:

1. **Phase 1**: Build and test portable modules (cmd_classify, chat_msg, chat_thinking, chat_activity, chat_approval) with full test coverage. No UI changes yet.
2. **Phase 2**: Build chat_listview (owner-draw panel). Initially render alongside existing RichEdit for comparison.
3. **Phase 3**: Replace RichEdit with chat_listview. Update relayout(), WM_PAINT, and all message handling.
4. **Phase 4**: Integrate command blocks with inline buttons. Remove floating Allow/Deny buttons.
5. **Phase 5**: Integrate activity indicator with health monitoring.
6. **Phase 6**: Polish, theme integration, DPI scaling, keyboard navigation.

### 10.2 Backward Compatibility

- `ai_chat.h` public API unchanged — callers unaffected
- `AiChatData` internal struct gets new fields; old ones removed gradually
- Existing `AiConversation`/`AiSessionState` structures preserved
- Existing command extraction (`ai_extract_commands`) preserved; classifier wraps it
- Existing markdown parser logic reused; rendering target changes from RichEdit to GDI

### 10.3 Files to Modify

| File | Change |
|------|--------|
| `src/ui/ai_chat.c` | Major: replace RichEdit rendering with chat_listview integration |
| `src/ui/ui_theme.h` | Add chat-specific color fields to ThemeColors |
| `src/core/ai_prompt.c` | Refactor `ai_command_is_readonly()` to use new `cmd_classify()` |
| `src/core/ai_prompt.h` | Add `CmdPlatform` to session state |

### 10.4 New Files

| File | Purpose |
|------|---------|
| `src/core/cmd_classify.h/c` | Command safety classifier (portable) |
| `src/core/chat_msg.h/c` | Message item model (portable) |
| `src/core/chat_thinking.h/c` | Thinking state machine (portable) |
| `src/core/chat_activity.h/c` | Activity monitor (portable) |
| `src/core/chat_approval.h/c` | Approval flow state machine (portable) |
| `src/ui/chat_listview.h/c` | Owner-drawn message list (Win32) |
| `src/ui/md_render.h/c` | Markdown-to-GDI renderer (Win32) |
| `tests/test_cmd_classify.c` | Classifier tests |
| `tests/test_chat_msg.c` | Message model tests |
| `tests/test_chat_thinking.c` | Thinking controller tests |
| `tests/test_chat_activity.c` | Activity monitor tests |
| `tests/test_chat_approval.c` | Approval flow tests |

---

## 11. Open Questions

1. **Platform auto-detection**: Should the classifier attempt to detect the connected device's platform from login banners/prompts, or require manual configuration in session settings?
2. **Keyboard navigation**: Should command blocks be focusable via Tab, with Enter to approve and Escape to deny?
3. **Text selection**: The owner-drawn list needs a text selection model for copy/paste. Implement full selection (click-drag across messages) or per-message selection (click to select, Ctrl+C to copy)?
4. **Search**: Should the message list support Ctrl+F to search conversation history?
5. **Message editing**: Should users be able to edit and re-send previous prompts (ChatGPT-style)?
