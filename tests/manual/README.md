# Manual keyboard-interactive checks

`kbdint_server.py` is a small SSH server that speaks keyboard-interactive
authentication on a script. It lets you watch Nutshell's prompt dialog, the
instruction box, the round cap and the timeouts without a real sshd with PAM
or Duo behind it.

**Nothing here runs in `make test`, in CI or in any gate.** It is a hand tool.
If paramiko is missing the script says so and does nothing else.

## Install

```
sudo apt install python3-paramiko       # Debian / Ubuntu
```

or, in a virtual environment, `pip install paramiko`. Debian and Ubuntu refuse
`pip install --user` against the system Python (PEP 668), so do not use it.
The system `/usr/bin/python3` may already have paramiko; a plain `python3` that
is a virtual environment without it will not, so run the script with the
interpreter that does, as in the examples below. Python 3 and paramiko are all
it needs.

The host key is generated in memory at every start and never written to disk,
so it changes on every start: Nutshell shows the first-connect or the
changed-key dialog each time.

## Run

```
python3 tests/manual/kbdint_server.py --scenario password
```

It listens on `127.0.0.1:2222` as user `alice`; `--port` changes the port.
To reach it from the Windows box, bind to the LAN address of the Linux box:

```
python3 tests/manual/kbdint_server.py --scenario duo-menu --bind 192.168.1.20
```

`--bind` with any non-loopback address prints a warning: anyone who can reach
the port can log in as `alice` with the fixed answers below. Use a trusted LAN.
Ctrl+C stops the server; it serves one scenario per run, so restart it to
switch.

`--self-test` runs every scenario against itself with a paramiko client (the
right answers, then a wrong one) and prints `PASS <scenario>` or
`FAIL <scenario>` per line; the exit code is 0 only when all pass. On
2026-11-05 `/usr/bin/python3 tests/manual/kbdint_server.py --self-test` passed
all six scenarios with paramiko 2.12.0.

## Point Nutshell at it

On Windows, make a password profile: host is the Linux box (or `127.0.0.1` for
a tunnel), port `2222`, user `alice`, authentication Password. Leave the saved
password empty for most scenarios. Every successful login opens a shell that
prints `kbdint-ok <scenario>` and closes the channel.

## Scenarios and what each should show

| Scenario | Server does | Nutshell should |
|---|---|---|
| `password` | one round, `Password:` (echo off); answer `secret` | show the prompt dialog with the masked field and the save tick; with the right answer, log in and print `kbdint-ok password`; with a wrong one, report the failure (the server refuses and lets the client retry) |
| `password-otp` | `Password:` then `Verification code:`; answers `secret`, `123456` | ask the second prompt only after the first is answered; a `Verification code:` prompt is not password-like, so it is never auto-answered and has no save tick; log in |
| `zero-rounds` | two rounds with an instruction and no prompts, then `Password:` | answer the empty rounds without asking you anything (the instruction text may flash or show), then ask for `Password:`; log in |
| `wrong-disconnect` | `Password:`; any answer but `secret` closes the connection | on a wrong answer, end at once with the connection error, not a hang; the right answer logs in |
| `duo-menu` | a 300-byte instruction listing three options, prompt `Passcode or option (1-3):` (echo on); `1` succeeds after 5 s | show the whole instruction in the scrolling read-only box, the prompt as the label with the echo visible, and no save tick; after answering `1`, wait about 5 s (the 60 s limit is far off) and log in; any other answer is refused |
| `kbd-and-password` | offers `password,keyboard-interactive` | try the offered methods in the order the spec sets (keyboard-interactive first for a profile without a saved password; the saved password first when there is one); both accept `secret` and log in |

Things worth trying on any scenario: Cancel at a prompt (ends at once), close
the window while a prompt is open (ends at once), and leave a prompt open for
over a minute before answering (user time never counts against the timeout).
`--verbose` shows paramiko's own log.

The Windows hand-tests the spec lists (a real sshd with PAM, `PASSCODE:`
prompts, `AuthenticationMethods publickey,keyboard-interactive`, silent-server
timeouts) need a real server; this one covers the prompt sequences only.
