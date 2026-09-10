# Command classification coverage — review and expansion

**Date**: 2026-09-11
**Subject**: `src/core/cmd_classify.c` / `.h`, `tests/test_cmd_classify.c`
**Goal**: review what is currently classified SAFE / WRITE / CRITICAL, close the gaps, and
extend coverage to the device families we actually drive: Linux distributions, Palo Alto
PAN-OS, Cisco (IOS/IOS-XE/NX-OS/ASA), HP (ProCurve and Comware), Aruba (OS-CX and ArubaOS),
plus Juniper Junos, Fortinet FortiOS, VyOS and MikroTik RouterOS.

---

## 1. Review of the current classifier

### 1.1 What the three levels mean

| Level | Meaning | Gate |
|---|---|---|
| `CMD_SAFE` | read-only, no state change | auto-approves at every auto-approve level |
| `CMD_WRITE` | changes state, recoverable | needs `permit_write` |
| `CMD_CRITICAL` | outage, data loss or lock-out | needs `permit_write` **and** a visual warning |

The dividing line used throughout this document: **CRITICAL is "a careful engineer would
schedule this"** — it drops traffic, reboots, erases config, removes an object, or can lock
the operator out. **WRITE is "reversible with another command"**. Anything that only reads is
SAFE.

### 1.2 Coverage today

- Linux: 18 critical commands, 1 critical prefix, ~40 write commands, subcommand rules for
  `systemctl`, `docker`, `kubectl`, `iptables`, `ip`, `git`, `crontab`, `sed`, `curl`, `wget`,
  plus redirect scanning, pipe-target scanning and a SQL-in-DB-CLI scanner.
- Cisco IOS, NX-OS, ASA; Aruba OS-CX, ArubaOS; PAN-OS: one `classify_*_segment` each,
  ~950 lines total, defaulting to `CMD_WRITE` for anything unrecognised.

### 1.3 Findings

**F1 — the vendor rules never run.** Both call sites pass `CMD_PLATFORM_LINUX` literally
(`src/ui/ai_chat.c:3600`, `:3749`); `AiSessionState.platform` is declared and never assigned.
Everything below is dead code until that is plumbed (security audit H2). *Out of scope for
this change by decision, but it is the single change that makes the rest of this document have
any runtime effect.*

**F2 — `|` is treated as a shell pipe on every platform except PAN-OS.** `show running-config |
include ospf` on IOS splits into two segments; the second, `include ospf`, hits the
network-device `CMD_WRITE` fallthrough. A read-only command classifies WRITE on IOS, NX-OS,
ASA and both Aruba families. Every CLI in this document uses `|` as a display filter, not a
pipe.

**F3 — device-filesystem rules only look at token 2 and only match `flash`.**
`delete /force /recursive flash:x` (the standard IOS form) has `/force` as token 2, so it
misses. `bootflash:`, `disk0:`, `slot0:`, `harddisk:`, `usb0:`, `nvram:` never match at all, so
`delete bootflash:nxos.bin` on NX-OS classifies WRITE.

**F4 — `configure replace` misses.** The rule matches the literal token `config`; IOS spells it
`configure replace flash:foo`. Wholesale config replacement classifies WRITE.

**F5 — package managers are flat WRITE.** `apt-get purge`, `dnf remove`, `rpm -e`,
`dpkg --purge`, `pacman -R` remove software from a live host — CRITICAL, not WRITE. In the
other direction `apt list`, `dnf info`, `rpm -qa`, `pacman -Q` are pure queries and should be
SAFE. Only the Debian/RedHat/Arch families are known at all: no `zypper` (SUSE), `apk`
(Alpine), `snap`, `flatpak`, `emerge`, `nix-env`, `rpm`, `dpkg`.

**F6 — SysV/OpenRC service management is missing.** `systemctl` is handled, but `service nginx
stop`, `/etc/init.d/nginx stop`, `rc-service`, `initctl` and `telinit 0` are not. `systemctl
poweroff|reboot|halt|isolate|kill` are missing from the subcommand table and fall through to
the flat `systemctl` handling.

**F7 — storage and kernel-level commands are missing.** No `fsck`, `mdadm`, `cryptsetup`,
`dmsetup`, `blkdiscard`, `sgdisk`, `swapoff`, `umount`, `mount`, `chroot`, `insmod`, `rmmod`,
`modprobe -r`, `sysctl -w`, `kexec`, `setenforce`. `swapoff -a` on a busy host is an outage.

**F8 — `ufw status` classifies WRITE** (the whole `ufw` verb falls to WRITE after the `reset`
check). `iptables-save` / `nft list ruleset` are unknown; `iptables-restore` (wholesale ruleset
replacement) is unknown and lands SAFE via the Linux fallthrough.

**F9 — `awk` is WRITE, `find` is SAFE.** `awk` is listed WRITE because a program can redirect
to a file; but `find / -delete` and `find . -exec rm -rf {} +` classify SAFE, which is the
larger hole (already noted as audit C2).

**F10 — container/orchestration coverage stops at `docker` and `kubectl`.** No `podman`,
`docker compose`, `helm`, `terraform`, `ansible-playbook`. `docker stop`/`kill` and `kubectl
drain`/`cordon`/`rollout undo` are missing from their subcommand tables.

**F11 — Cisco `no <x>` is uniformly WRITE** apart from `router` / `spanning-tree vlan` / `vlan`.
`no username`, `no aaa`, `no access-list`, `no interface`, `no ip routing`, `no line` all
either drop traffic or lock the operator out.

**F12 — NX-OS `rollback` is WRITE** (`rollback running-config checkpoint X` replaces the
running config) and `no feature X` is CRITICAL only for `nv overlay`; disabling any feature
discards that feature's entire configuration.

**F13 — ASA `clear configure <feature>` is only CRITICAL for `all`.** `clear configure
access-list` wipes every ACL. `clear xlate` / `clear conn` drop live NAT and connection state.

**F14 — Aruba OS-CX `boot` is WRITE.** On AOS-CX `boot system` *is* the reboot command.
`start-shell` (drops to bash) is unclassified.

**F15 — PAN-OS `set cli` classifies WRITE.** It only changes the operator's own output format.
Conversely `set deviceconfig system ip-address` — a change that can strand the management
session — is plain WRITE like any other `set`.

**F16 — no HP support at all**, in either flavour: ProCurve/ProVision (`erase startup-config`,
`boot system flash primary`) or Comware/H3C (`reboot`, `reset saved-configuration`, `undo` as
the negation prefix, `display` as the show verb). Under the Linux ruleset a Comware `reboot`
classifies SAFE.

---

## 2. Shared mechanics to change

**M1 — display filters.** In `cmd_classify_ex`, treat `|` as a segment separator **only** for
`CMD_PLATFORM_LINUX`. Every other platform uses it as a display filter. (Fixes F2; the existing
PAN-OS special case generalises.)

**M2 — device-filesystem helper.** Add

```c
static int is_device_fs_token(const char *tok, size_t len);
```

matching, case-insensitively, a token that starts with one of: `flash:`, `bootflash:`,
`disk0:`, `disk1:`, `slot0:`, `slot1:`, `nvram:`, `harddisk:`, `usb0:`, `usb1:`, `system:`,
`cf:`, `unix:`, `sup-bootflash:`, `volatile:`, `logflash:`. Rules for `delete`, `format`,
`erase`, `squeeze` and `copy` scan **every** remaining token with it, not just token 2.
(Fixes F3.)

**M3 — new platform enum values, appended** so existing values keep their numbers:

```c
CMD_PLATFORM_HP_PROCURVE,
CMD_PLATFORM_HP_COMWARE,
CMD_PLATFORM_JUNOS,
CMD_PLATFORM_FORTIOS,
CMD_PLATFORM_VYOS,
CMD_PLATFORM_MIKROTIK
```

Each gets a `classify_<name>_segment()` and a `switch` case, following the existing shape:
first token via `next_token`, `tok_eq_ci` / `tok_prefix_ci` comparisons, `reason_buf` filled
with a short human phrase, and a conservative `CMD_WRITE` fallthrough.

**M4 — a token-scan helper for verb-anywhere CLIs** (MikroTik puts the verb last):

```c
static int seg_has_token_ci(const char *seg, size_t len, const char *lit);
```

---

## 3. Linux — additions

Existing entries stay. `sudo` / `su` / `doas` escalation, redirects, pipe targets and the SQL
scanner are unchanged.

### 3.1 CRITICAL additions

| Command / form | Why |
|---|---|
| `fsck`, `e2fsck`, `xfs_repair`, `resize2fs`, `blkdiscard`, `sgdisk`, `mdadm`, `cryptsetup`, `dmsetup`, `losetup` | destroys or restructures a filesystem |
| `swapoff`, `umount`, `chroot`, `kexec`, `telinit` | takes resources away from a running system |
| `insmod`, `rmmod`, `modprobe -r` | kernel module removal |
| `setenforce` | disables SELinux enforcement |
| `iptables-restore`, `nft delete`, `nft -f`, `ufw disable`, `ufw reset`, `firewall-cmd --panic-on`, `firewall-cmd --complete-reload` | wholesale firewall change |
| `systemctl poweroff` / `reboot` / `halt` / `isolate` / `kill` / `set-default` | subcommand table (F6) |
| `service <x> stop`, `/etc/init.d/<x> stop`, `rc-service <x> stop`, `initctl stop` | SysV / OpenRC / upstart service stop |
| `apt`/`apt-get remove`, `purge`, `autoremove`; `dnf`/`yum remove`, `erase`, `autoremove`; `zypper rm`; `pacman -R`/`-Rns`; `apk del`; `rpm -e`; `dpkg -r`/`--purge`/`-P`; `snap remove`; `flatpak uninstall`; `emerge --unmerge`/`-C`; `npm uninstall -g`; `pip uninstall` | removes software from a live host (F5) |
| `apt-get dist-upgrade`, `dnf distro-sync`, `pacman -Syu`, `zypper dup`, `do-release-upgrade` | whole-system upgrade |
| `userdel`, `groupdel`, `deluser`, `passwd -d`, `passwd -l`, `usermod -L` | account removal / lock-out |
| `docker stop` / `kill` / `rmi` / `prune` / `volume rm` / `network rm`; `docker compose down`; the `podman` mirrors | container outage (F10) |
| `kubectl drain` / `cordon` / `rollout undo` / `replace --force`; `helm uninstall`, `helm rollback` | workload eviction / rollback |
| `terraform destroy`, `terraform apply -auto-approve` | infrastructure teardown |
| `nmcli con down`, `nmcli con delete`, `nmcli device disconnect`; `ifdown`; `netplan apply`; `ifconfig <if> down` | drops the interface you are connected over |
| `tc qdisc del`, `ip netns delete` | traffic-control / namespace teardown |
| `find` with `-delete` or `-exec rm` anywhere in the segment | scan all tokens (F9) |
| `zfs rollback`, `btrfs subvolume delete` | alongside the existing `zpool destroy` / `zfs destroy` |

### 3.2 WRITE additions

`mount`, `sysctl -w`, `chattr`, `setfacl`, `visudo`, `groupmod`, `chgrp`, `install`, `patch`,
`unzip`, `bunzip2`, `xz`, `7z`, `logrotate`, `update-grub` / `grub2-mkconfig`, `dracut`,
`update-initramfs`, `ldconfig`, `timedatectl set-*`, `hostnamectl set-hostname`, `date -s`,
`ntpdate`, the package-manager install/update paths (`apt install`, `apt update`,
`dnf install`, `zypper in`, `apk add`, `snap install`, `flatpak install`, `pip install`,
`npm install`, `gem install`, `cargo install`, `go install`), `docker start` / `restart` /
`pull` / `tag` / `commit`, `docker compose up`, the `podman` equivalents, `helm install` /
`upgrade` / `repo add`, `terraform apply` / `init`, `ansible`, `ansible-playbook`,
`kubectl annotate` / `label` / `uncordon`, `crontab -e`, `at`, `batch`, `systemd-run`,
`systemctl daemon-reload`, `nmcli con up` / `modify`, `ip netns add`, `brctl`, `ovs-vsctl`,
`wg set`, `dmesg -C`, `journalctl --vacuum-*`, `logger`.

### 3.3 SAFE additions (explicit allow-list entries)

`ls`, `dir`, `cat`, `tac`, `less`, `more`, `head`, `tail`, `grep`, `egrep`, `fgrep`, `rg`,
`wc`, `sort`, `uniq`, `cut`, `tr`, `column`, `diff`, `cmp`, `md5sum`, `sha1sum`, `sha256sum`,
`file`, `stat`, `readlink`, `realpath`, `basename`, `dirname`, `tree`, `pwd`, `echo`, `printf`,
`true`, `false`, `date` *(bare)*, `cal`, `uptime`, `w`, `who`, `whoami`, `id`, `groups`,
`uname`, `hostname` *(bare)*, `lsb_release`, `arch`, `nproc`, `free`, `vmstat`, `iostat`,
`mpstat`, `sar`, `pidstat`, `ps`, `pstree`, `top`, `htop`, `df`, `du`, `lsblk`, `blkid`,
`findmnt`, `mount` *(bare)*, `lsof`, `lspci`, `lsusb`, `lscpu`, `lsmod`, `dmidecode`,
`sensors`, `getenforce`, `sestatus`, `env`, `printenv`, `locale`, `which`, `whereis`, `type`,
`man`, `history`, `netstat`, `ss`, `ip addr` / `route` / `link` / `neigh` *(show forms)*,
`arp`, `route -n`, `ping`, `ping6`, `traceroute`, `tracepath`, `mtr`, `dig`, `host`,
`nslookup`, `whois`, `journalctl`, `dmesg` *(bare)*, `last`, `lastlog`, `systemctl status` /
`is-active` / `is-enabled` / `list-units` / `list-timers` / `cat` / `show`, `iptables -L` /
`-S`, `iptables-save`, `nft list`, `ufw status`, `firewall-cmd --list-all` / `--state`,
`apt list` / `show` / `search` / `policy`, `dnf list` / `info` / `search` / `repolist`,
`yum list` / `info`, `rpm -q*`, `dpkg -l` / `-L` / `-S`, `pacman -Q*`, `zypper se` / `if` /
`lr`, `apk info`, `snap list`, `flatpak list`, `pip list` / `show` / `freeze`, `npm ls` /
`view`, `docker ps` / `images` / `logs` / `inspect` / `stats` / `top`, `kubectl get` /
`describe` / `logs` / `top` / `explain` / `api-resources` / `version`, `helm list` / `status` /
`get` / `history`, `terraform plan` / `show` / `output` / `validate`.

> This list is written so that it can be lifted verbatim into the SAFE **allow-list** that
> audit finding C2 calls for. Until that inversion lands, unknown Linux commands still fall
> through to SAFE — these entries make the common read-only set explicit and give the
> allow-list a ready-made table.

---

## 4. Cisco IOS / IOS-XE — additions

**SAFE**: existing (`show`, `ping`, `traceroute`, `terminal`, `enable`, `disable`, `exit`,
`end`, `dir`, `verify`) plus `more`, `where`, `who`, `reload cancel` *(cancels a pending
reload — explicitly SAFE)*.

**CRITICAL additions**

| Form | Why |
|---|---|
| `configure replace <fs>` (prefix match on `conf`) | wholesale config replacement (F4) |
| `erase` / `delete` / `format` / `squeeze` with a device-fs token **anywhere** in the segment | catches `delete /force /recursive flash:` (F3) |
| `copy <src> running-config`, `copy <src> startup-config` | replaces the live or boot config |
| `boot system` | changes the next-boot image |
| `default interface <x>` | resets an interface to defaults |
| `no interface`, `no ip routing`, `no username`, `no aaa`, `no line`, `no enable`, `no access-list`, `no ip access-list`, `no crypto`, `no ip nat`, `no standby`, `no vrrp`, `no hsrp` | traffic drop or operator lock-out (F11) |
| `clear ip nat translation`, `clear ip route`, `clear bgp`, `clear ip eigrp`, `clear isis` | NAT / route / adjacency reset (adds to the existing `clear crypto`, `clear ip bgp`, `clear ip ospf`) |
| `hw-module reset`, `hw-module reload`, `microcode reload`, `redundancy reload` | hardware / RP restart (alongside the existing `redundancy force-switchover`) |
| `install add` / `activate` / `commit` / `remove`, `request platform software package install` | IOS-XE image install |
| `commit replace`, `process restart` | IOS-XR |
| `test crash`, `write core` | forced crash / core dump |

**WRITE additions**: `archive`, `license`, `event manager`, `policy-map`, `class-map`, `track`,
`object-group`, `key chain`, `crypto` *(config forms)*, `tunnel`, `vrf`, `ip sla`,
`monitor session`, `mac address-table`, `errdisable`, `power inline`, `privilege`, `aaa`,
`tacacs-server`, `radius-server`, `clock set`, `clear counters`, `clear line`,
`clear arp-cache`, `clear mac address-table`, `clear logging`.

---

## 5. Cisco NX-OS — additions

Inherits IOS. NX-OS-specific:

**CRITICAL**: `rollback` *(currently WRITE — F12)*, `no feature <any>` *(currently only
`nv overlay`)*, `install activate` / `deactivate` / `remove` (alongside the existing
`install all`), `system switchover`, `out-of-service module`, `poweroff module`,
`purge module`, `write erase boot`, `guestshell destroy`, `attach module`, `run bash`
*(drops to a Linux shell on the switch)*, `boot nxos` *(next-boot image)*, `clear ip route`,
`no vlan`, `no vrf context`, `no interface`, `copy <src> running-config`.

**WRITE**: existing `feature` / `checkpoint`, plus `vdc`, `switchto vdc`, `guestshell enable`.

---

## 6. Cisco ASA — additions

**CRITICAL**: `clear configure <anything>` *(not just `all` — F13)*, `clear xlate`,
`clear conn`, `crypto key zeroize`, `no crypto map`, `no access-group`, `no nat`,
`no object-group`, `no route`, `no tunnel-group`, `no username`, `no aaa`, `failover reset`,
`no failover active`, `boot system`, `boot config`, `upgrade`, `hw-module module ... reset`.

**WRITE**: `shun`, `no shun`, `write standby`, `clear local-host`, `clear crashinfo`,
`perfmon`, `capture`, `dhcpd`, `webvpn`, `ssl`.

---

## 7. HP ProCurve / ProVision — new platform `CMD_PLATFORM_HP_PROCURVE`

**SAFE**: `show`, `ping`, `traceroute`, `dir`, `menu`, `getmib`, `walkmib`, `exit`, `end`,
`logout`, `page`, `terminal`.

**WRITE**: `configure`, `write memory`, `vlan`, `interface`, `trunk`, `ip`, `hostname`,
`snmp-server`, `setmib`, `aaa`, `radius-server`, `tacacs-server`, `spanning-tree`, `password`,
`time`, `sntp`, `logging`, `qos`, `lldp`, `stack`, `kill` *(ends another console session)*,
`copy <src> tftp` *(export)*.

**CRITICAL**

| Form | Why |
|---|---|
| `reload`, `boot`, `boot system flash <primary\|secondary>` | reboot / next-boot image |
| `erase startup-config`, `erase all zeroize`, `erase flash` | config or flash wipe |
| `delete <file>` | file removal on flash |
| `copy tftp startup-config`, `copy tftp flash`, `copy xmodem flash`, `copy usb flash` | config or image replacement |
| `no vlan`, `no interface`, `no ip routing`, `no spanning-tree`, `no router`, `no password`, `no aaa`, `no snmp-server` | traffic drop / lock-out |
| `no password manager`, `password clear` | removes the manager password |
| `interface <x> disable` (verb-anywhere scan for `disable`) | port down |
| `factory-reset` | full wipe |

---

## 8. HPE Comware 5/7 (H3C) — new platform `CMD_PLATFORM_HP_COMWARE`

Comware's show verb is `display` and its negation prefix is `undo`.

**SAFE**: `display`, `ping`, `tracert`, `dir`, `more`, `quit`, `return`, `terminal`,
`screen-length`.

**WRITE**: `system-view`, `save` *(including `save force`)*, `interface`, `vlan`,
`ip route-static`, `acl`, `ospf`, `bgp`, `stp`, `sysname`, `local-user`, `nat`,
`security-zone`, `security-policy`, `radius scheme`, `hwtacacs scheme`, `snmp-agent`,
`info-center`, `clock`, `ntp-service`, `irf-port`, `qos`, `port`, `lldp`, `poe`, `debugging`,
`reset counters`, `reset logbuffer`, `undo <anything not listed below>`.

**CRITICAL**

| Form | Why |
|---|---|
| `reboot`, `schedule reboot` | device reboot, immediate or delayed |
| `reset saved-configuration` | erases the startup config |
| `restore factory-default` | full wipe |
| `delete` with `/unreserved` or a `flash:` / `cf:` token | permanent file delete |
| `format` | filesystem format |
| `boot-loader file`, `startup saved-configuration` | next-boot image / config |
| `patch install`, `install activate`, `install commit` | image install |
| `undo interface`, `undo vlan`, `undo local-user`, `undo ospf`, `undo bgp`, `undo irf`, `undo stp`, `undo ip route-static`, `undo acl`, `undo security-policy`, `undo nat` | removes a config block |
| `shutdown` | interface down |
| `reset bgp`, `reset ospf`, `reset ip routing-table`, `reset session` | adjacency / session reset |
| `irf member <n> renumber` | renumbers a stack member (reboots it) |
| `_cmdline-mode`, `xtd-cli-mode` | escapes to the hidden shell |

---

## 9. Aruba OS-CX — additions

**CRITICAL additions**: `boot` and `boot system` *(F14 — on AOS-CX this is the reboot
command)*, `reboot`, `start-shell` *(bash on the switch)*, `no interface`, `no vlan`, `no vrf`,
`no router bgp` / `ospf` / `ospfv3`, `no user`, `no aaa`, `shutdown` *(interface)*,
`copy <src> running-config`, `copy checkpoint <x> running-config`, `vsx update-software`,
`clear ip route all`.

**WRITE additions**: `checkpoint auto`, `checkpoint post-configuration`, `copy running-config
startup-config`, `banner`, `ssh`, `https-server`, `snmp-server`, `sflow`, `lag`, `lacp`,
`spanning-tree`, `vrf`, `dhcp-server`, `clear arp`, `clear lldp neighbors`.

**SAFE additions**: `checkpoint diff`, `diff`, `less`, `top`.

---

## 10. ArubaOS (controller / Instant) — additions

**CRITICAL additions**: `halt`, `disable-ap`, `crypto-local ... zeroize`,
`copy <src> system: partition` *(image install)*, `clear datapath session`, `clear gap-db`,
`convert-aos-ap`, `local-userdb del`, `license del`, `no aaa profile`, `no wlan ssid-profile`,
`no user-role`, `shutdown`, `write erase all`.

**WRITE additions**: `ap-rename`, `ap-regroup`, `provision-ap`, `database synchronize`,
`local-userdb add`, `license add`, `upgrade-profile`, `master-redundancy`, `papi-security`,
`firewall`, `ids`, `wids`.

---

## 11. PAN-OS — additions

**SAFE additions**: `set cli` *(F15 — output-format preference only)*, `request support info`,
`check`, `debug show`.

**CRITICAL additions**

| Form | Why |
|---|---|
| `set deviceconfig system ip-address` / `netmask` / `default-gateway` | can strand the management session (F15) |
| `request plugins <x> install` | plugin install restarts services |
| `request restart dataplane` | dataplane restart drops all traffic |
| `request ha state suspend` | HA state change (alongside the existing `request high-availability state`) |
| `request global-protect-gateway client-logout`, `request vpn ipsec-sa clear`, `request vpn ike-sa clear` | drops user / VPN sessions |
| `clear dos-block-table` | alongside the existing `clear session all`, `clear log` |
| `load named-configuration` | config replacement, alongside the existing `load config` |

**WRITE additions**: `request content upgrade download`, `request anti-virus upgrade download`,
`request url-filtering update`, `request system external-list refresh`, `save
named-configuration snapshot`, `revert config`.

`commit check` / `commit validate` stays SAFE; bare `commit`, `commit force`, `commit partial`
and `commit-all` stay CRITICAL.

---

## 12. Juniper Junos — new platform `CMD_PLATFORM_JUNOS`

**SAFE**: `show`, `run show`, `ping`, `traceroute`, `monitor traffic`, `monitor interface`,
`file show`, `file list`, `file compare`, `compare`, `help`, `exit`, `quit`, `top`, `up`,
`commit check`, `test policy`, `request support information`.

**WRITE**: `configure`, `edit`, `set`, `deactivate`, `activate`, `rename`, `copy`, `insert`,
`annotate`, `replace pattern`, `load merge`, `load set`, `load patch`, `save`, `rollback`
*(loads a candidate — the `commit` is the gate)*, `commit confirmed` *(auto-reverts, so it is
the safer form)*, `request system snapshot`, `request system license add`, `file copy`,
`file archive`, `op <script>`, `clear interfaces statistics`, `clear log`, `clear arp`.

**CRITICAL**

| Form | Why |
|---|---|
| `commit`, `commit and-quit`, `commit synchronize`, `commit force`, `commit at` | applies the candidate config |
| `delete` *(config mode — removes a hierarchy)* | |
| `load override`, `load replace`, `load factory-default` | replaces the whole config |
| `request system reboot` / `halt` / `power-off` / `zeroize` / `software add` / `partition` | |
| `request chassis cluster failover` | HA failover |
| `restart routing`, `restart <daemon>` | daemon restart |
| `clear bgp neighbor`, `clear ospf neighbor`, `clear isis adjacency`, `clear security flow session all`, `clear system commit` | adjacency / session reset |
| `file delete` | file removal |
| `start shell` | escapes to the FreeBSD shell |

---

## 13. Fortinet FortiOS — new platform `CMD_PLATFORM_FORTIOS`

**SAFE**: `get`, `show`, `end`, `next`, `abort`, `exit`, `execute ping`, `execute traceroute`,
`execute telnet`, `execute ssh`, `execute time`, `diagnose sys top`, `diagnose debug`,
`diagnose sniffer packet`.

**WRITE**: `config`, `edit`, `set`, `unset`, `append`, `clone`, `move`, `rename`,
`execute backup config`, `execute update-now`, `execute date <value>`, `execute ha manage`,
`execute cli`, `diagnose` *(default)*.

**CRITICAL**

| Form | Why |
|---|---|
| `delete` *(inside a config table)* | removes an object |
| `purge` | deletes **every** entry in the current table |
| `execute factoryreset`, `factoryreset2`, `erase-disk`, `formatlogdisk` | wipe |
| `execute reboot`, `execute shutdown` | |
| `execute restore config`, `execute restore image` | config / image replacement |
| `execute ha failover set`, `execute ha synchronize` | HA state change |
| `execute log delete-all` | log destruction |
| `execute router clear bgp`, `execute router clear ospf process` | adjacency reset |
| `execute disconnect-admin-session` | kicks an admin, possibly yourself |
| `diagnose sys kill`, `diagnose hardware test`, `diagnose sys flash format`, `diagnose vpn tunnel down` | process kill / hardware / VPN drop |

---

## 14. VyOS — new platform `CMD_PLATFORM_VYOS`

**SAFE**: `show`, `run show`, `compare`, `ping`, `traceroute`, `monitor`, `exit`, `top`, `up`,
`commit-check`.

**WRITE**: `configure`, `set`, `save`, `discard`, `comment`, `rename`, `copy`,
`commit-confirm` *(auto-reverts)*, `generate`, `add system image`,
`clear interfaces counters`.

**CRITICAL**: `commit`, `delete` *(config mode)*, `load`, `merge`, `reboot`, `poweroff`,
`shutdown`, `reset vpn ipsec-peer`, `reset ip bgp`, `reset ospf process`,
`delete system image`, `set system image default-boot`.

---

## 15. MikroTik RouterOS — new platform `CMD_PLATFORM_MIKROTIK`

RouterOS puts the verb **last** (`/ip firewall filter remove numbers=0`), so this classifier
scans every token (helper M4) rather than only the first.

- **CRITICAL** if any token is `remove`, `reset-configuration`, `reset`, `reboot`, `shutdown`,
  `disable` *(a `/interface disable` drops a link)*, `downgrade`, `upgrade`, `format-drive`,
  or the segment contains `backup load` / `routerboard upgrade`.
- **WRITE** if any token is `set`, `add`, `edit`, `comment`, `move`, `enable`, `upload`,
  `fetch`, `import`, or the segment contains `backup save`.
- **SAFE** if the only verb present is `print`, `get`, `find`, `export`, `monitor`,
  `monitor-traffic` or `info`.
- Otherwise WRITE, as with the other network platforms.

Order matters: check CRITICAL first, then WRITE, then SAFE — `/ip firewall filter print` is
SAFE, but `/ip firewall filter remove [find disabled=yes]` contains both `remove` and `find`
and must be CRITICAL.

---

## 16. Deliberately out of scope

- **Cloud CLIs** (`aws`, `az`, `gcloud`, `oci`). Their destructive verbs are a per-service
  matrix (`aws ec2 terminate-instances`, `aws s3 rb`, `az group delete`) with no single shape;
  they deserve their own pass and their own table.
- **Windows / PowerShell targets** — Nutshell drives POSIX and network CLIs today.
- **Audit H2** (plumbing the real platform through to `chat_approval_add`) and **audit C2**
  (inverting the Linux SAFE tier into an allow-list). Both are prerequisites for this coverage
  having any runtime effect; §3.3 is written so the allow-list can be lifted from it directly.

---

## 17. Test plan

`tests/test_cmd_classify.c`, registered in `tests/runner.c`. Every row of every table above
gets at least one positive assertion, plus these corner cases:

- the read-only verb with a display filter (`show running-config | include ospf` → SAFE) on
  every network platform — regression for F2;
- a device-filesystem form with flags before the path (`delete /force /recursive flash:x`) — F3;
- `configure replace flash:x` → CRITICAL — F4;
- one query form per package manager → SAFE, one removal form → CRITICAL — F5;
- `ufw status` → SAFE, `ufw reset` → CRITICAL — F8;
- Comware `reboot` → CRITICAL under `CMD_PLATFORM_HP_COMWARE` **and** SAFE under
  `CMD_PLATFORM_LINUX`, asserted explicitly to document the F1 cross-platform hazard;
- `commit check` (Junos) and `commit validate` (PAN-OS) → SAFE while bare `commit` → CRITICAL;
- `set cli config-output-format set` → SAFE on PAN-OS;
- MikroTik verb-last: `/ip firewall filter print` → SAFE, `/ip firewall filter remove
  numbers=0` → CRITICAL;
- empty string and a lone separator for each new platform → SAFE.
