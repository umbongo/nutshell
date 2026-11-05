#!/usr/bin/env python3
"""Manual keyboard-interactive SSH test server for Nutshell.

A hand-check tool, NOT part of `make test` and never run in CI.  It needs
paramiko (python3 -m pip install --user paramiko) and nothing else outside
the standard library.

    python3 tests/manual/kbdint_server.py --scenario password
    python3 tests/manual/kbdint_server.py --self-test

The only user is "alice".  Every successful login opens a shell channel that
prints "kbdint-ok <scenario>" and closes.  The host key is generated in memory
at start and never written to disk, so each run has a new fingerprint.

Scenarios (answers are fixed; they are test data, not secrets):

  password           "Password:" (echo off)                    answer: secret
  password-otp       "Password:", then "Verification code:"    secret, 123456
  zero-rounds        two zero-prompt rounds that carry only an
                     instruction, then "Password:"             secret
  wrong-disconnect   "Password:"; any answer but "secret"
                     closes the transport
  duo-menu           a 300-byte instruction listing three
                     options, prompt "Passcode or option
                     (1-3):" (echo on); "1" succeeds after a
                     5 s sleep (a push approval)               1
  kbd-and-password   offers "password,keyboard-interactive";
                     both paths accept "secret"
"""

import argparse
import ipaddress
import logging
import socket
import sys
import threading
import time

try:
    import paramiko
except ImportError:  # reported by require_paramiko(), so --help still works
    paramiko = None

USER = "alice"
SECRET = "secret"
OTP = "123456"
DUO_DELAY_SECONDS = 5.0
DUO_INSTRUCTION_BYTES = 300

PARAMIKO_MISSING = "paramiko is not installed; pip install --user paramiko"

_DUO_TEXT = (
    "Duo two-factor login for alice\n"
    "\n"
    "Choose how to authenticate:\n"
    " 1. Duo Push to +1 XXX-XXX-1234\n"
    " 2. Phone call to +1 XXX-XXX-1234\n"
    " 3. SMS passcodes to +1 XXX-XXX-1234\n"
    "\n"
    "Enter a passcode or an option number."
)


def _fit_bytes(text, size):
    """Return text widened with a filler line to exactly `size` ASCII bytes."""
    filler = "\nNutshell manual test server, scenario duo-menu. "
    out = text + filler
    while len(out.encode("ascii")) < size:
        out += "x"
    return out[:size]


DUO_INSTRUCTION = _fit_bytes(_DUO_TEXT, DUO_INSTRUCTION_BYTES)

# A scenario is data.  Each round is (instructions, [(prompt, echo, answer)]);
# a round with no prompts is a zero-prompt round (the client answers with an
# empty list).  The login succeeds when every round is answered correctly.
#   on_wrong: "fail" answers AUTH_FAILED, "disconnect" closes the transport.
#   delay:    seconds to sleep before accepting the final correct answer.
#   password: also accept the plain "password" method with SECRET.
SCENARIOS = {
    "password": {
        "rounds": [("", [("Password:", False, SECRET)])],
        "on_wrong": "fail", "delay": 0.0, "password": False,
    },
    "password-otp": {
        "rounds": [
            ("", [("Password:", False, SECRET)]),
            ("", [("Verification code:", False, OTP)]),
        ],
        "on_wrong": "fail", "delay": 0.0, "password": False,
    },
    "zero-rounds": {
        "rounds": [
            ("Welcome to the zero-round test.\nThis round has no prompts.", []),
            ("Second round, still no prompts.\nOne more to go.", []),
            ("", [("Password:", False, SECRET)]),
        ],
        "on_wrong": "fail", "delay": 0.0, "password": False,
    },
    "wrong-disconnect": {
        "rounds": [("", [("Password:", False, SECRET)])],
        "on_wrong": "disconnect", "delay": 0.0, "password": False,
    },
    "duo-menu": {
        "rounds": [(DUO_INSTRUCTION,
                    [("Passcode or option (1-3):", True, "1")])],
        "on_wrong": "fail", "delay": DUO_DELAY_SECONDS, "password": False,
    },
    "kbd-and-password": {
        "rounds": [("", [("Password:", False, SECRET)])],
        "on_wrong": "fail", "delay": 0.0, "password": True,
    },
}


def allowed_auths(scenario):
    if SCENARIOS[scenario]["password"]:
        return "password,keyboard-interactive"
    return "keyboard-interactive"


class AuthState:
    """One keyboard-interactive attempt; knows nothing about paramiko.

    start() and respond() return ("query", instructions, [(prompt, echo)]),
    ("ok",), ("fail",) or ("disconnect",).
    """

    def __init__(self, scenario):
        self.spec = SCENARIOS[scenario]
        self.index = 0

    def _query(self):
        instructions, prompts = self.spec["rounds"][self.index]
        return ("query", instructions, [(p, e) for p, e, _ in prompts])

    def start(self):
        self.index = 0
        return self._query()

    def respond(self, responses):
        _instructions, prompts = self.spec["rounds"][self.index]
        if list(responses) != [answer for _p, _e, answer in prompts]:
            return (self.spec["on_wrong"] == "disconnect"
                    and ("disconnect",) or ("fail",))
        self.index += 1
        if self.index < len(self.spec["rounds"]):
            return self._query()
        if self.spec["delay"] > 0:
            time.sleep(self.spec["delay"])
        return ("ok",)


_Base = paramiko.ServerInterface if paramiko else object


class KbdIntServer(_Base):
    """The paramiko ServerInterface for one connection."""

    def __init__(self, scenario, log):
        super().__init__()
        self.scenario = scenario
        self.log = log
        self.transport = None  # set by the caller; needed to disconnect
        self.state = None
        self.shell_requested = threading.Event()

    def _verdict(self, verdict):
        kind = verdict[0]
        if kind == "query":
            query = paramiko.InteractiveQuery("", verdict[1])
            for prompt, echo in verdict[2]:
                query.add_prompt(prompt, echo)
            self.log("round %d: %d prompt(s)"
                     % (self.state.index + 1, len(verdict[2])))
            return query
        if kind == "ok":
            self.log("login accepted")
            return paramiko.AUTH_SUCCESSFUL
        if kind == "disconnect":
            self.log("wrong answer: closing the transport")
            if self.transport is not None:
                self.transport.close()
            return paramiko.AUTH_FAILED
        self.log("wrong answer: refused")
        return paramiko.AUTH_FAILED

    def get_allowed_auths(self, username):
        return allowed_auths(self.scenario)

    def check_auth_interactive(self, username, submethods):
        if username != USER:
            self.log("unknown user")
            return paramiko.AUTH_FAILED
        self.state = AuthState(self.scenario)
        return self._verdict(self.state.start())

    def check_auth_interactive_response(self, responses):
        if self.state is None:
            return paramiko.AUTH_FAILED
        return self._verdict(self.state.respond(responses))

    def check_auth_password(self, username, password):
        if (SCENARIOS[self.scenario]["password"] and username == USER
                and password == SECRET):
            self.log("password accepted")
            return paramiko.AUTH_SUCCESSFUL
        self.log("password refused")
        return paramiko.AUTH_FAILED

    def check_channel_request(self, kind, chanid):
        if kind == "session":
            return paramiko.OPEN_SUCCEEDED
        return paramiko.OPEN_FAILED_ADMINISTRATIVELY_PROHIBITED

    def check_channel_pty_request(self, channel, term, width, height,
                                  pixelwidth, pixelheight, modes):
        return True

    def check_channel_shell_request(self, channel):
        self.shell_requested.set()
        return True


def handle_connection(conn, addr, scenario, host_key, log):
    tag = "%s:%s" % (addr[0], addr[1])

    def say(msg):
        log("[%s] %s" % (tag, msg))

    transport = paramiko.Transport(conn)
    transport.add_server_key(host_key)
    server = KbdIntServer(scenario, say)
    server.transport = transport
    try:
        transport.start_server(server=server)
    except (paramiko.SSHException, EOFError, OSError) as exc:
        say("no session: %s" % exc)
        transport.close()
        return
    try:
        chan = transport.accept(30)
        if chan is None:
            say("closed without a channel")
            return
        server.shell_requested.wait(10)
        chan.send("kbdint-ok %s\r\n" % scenario)
        chan.send_exit_status(0)
        chan.close()
        deadline = time.time() + 10
        while transport.is_active() and time.time() < deadline:
            time.sleep(0.1)
    finally:
        transport.close()


def make_host_key():
    return paramiko.RSAKey.generate(2048)  # in memory only; never saved


def is_loopback(host):
    if host == "localhost":
        return True
    try:
        return ipaddress.ip_address(host).is_loopback
    except ValueError:
        return False


class Listener:
    def __init__(self, bind, port, scenario, host_key, log):
        self.scenario = scenario
        self.host_key = host_key
        self.log = log
        self.stop = threading.Event()
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind((bind, port))
        self.sock.listen(5)
        self.sock.settimeout(0.5)
        self.port = self.sock.getsockname()[1]

    def serve(self):
        while not self.stop.is_set():
            try:
                conn, addr = self.sock.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            self.log("connection from %s:%s" % addr)
            threading.Thread(
                target=handle_connection, name="conn", daemon=True,
                args=(conn, addr, self.scenario, self.host_key, self.log),
            ).start()
        self.sock.close()


# ---------------------------------------------------------------- self-test

def _client_handler(scenario, wrong, seen):
    """auth_interactive handler: right answers, or wrong ones when `wrong`."""
    spec = SCENARIOS[scenario]
    answers = {}
    for _instr, prompts in spec["rounds"]:
        for prompt, _echo, answer in prompts:
            answers[prompt] = answer

    def handler(_title, instructions, prompt_list):
        seen.append((instructions, [(p, bool(e)) for p, e in prompt_list]))
        out = []
        for prompt, _echo in prompt_list:
            answer = answers.get(prompt, "")
            out.append(answer + "-wrong" if wrong else answer)
        return out
    return handler


def _login(port, scenario, wrong):
    """Run one login; return (authenticated, seen_rounds, shell_text)."""
    seen = []
    transport = paramiko.Transport(("127.0.0.1", port))
    try:
        transport.start_client(timeout=15)
        try:
            transport.auth_interactive(
                USER, _client_handler(scenario, wrong, seen))
        except (paramiko.SSHException, EOFError, OSError):
            return False, seen, ""
        if not transport.is_authenticated():
            return False, seen, ""
        chan = transport.open_session()
        chan.invoke_shell()
        text = b""
        deadline = time.time() + 10
        while time.time() < deadline:
            data = chan.recv(1024)
            if not data:
                break
            text += data
        return True, seen, text.decode("utf-8", "replace")
    finally:
        transport.close()


def _password_login(port, password):
    transport = paramiko.Transport(("127.0.0.1", port))
    try:
        transport.start_client(timeout=15)
        try:
            transport.auth_password(USER, password)
        except (paramiko.SSHException, EOFError, OSError):
            return False
        return transport.is_authenticated()
    finally:
        transport.close()


def self_test_scenario(scenario, host_key):
    """Return (passed, detail) for one scenario."""
    spec = SCENARIOS[scenario]
    listener = Listener("127.0.0.1", 0, scenario, host_key, lambda _m: None)
    thread = threading.Thread(target=listener.serve, daemon=True)
    thread.start()
    try:
        ok, seen, shell = _login(listener.port, scenario, wrong=False)
        want = [(ins, [(p, e) for p, e, _a in prompts])
                for ins, prompts in spec["rounds"]]
        if not ok:
            return False, "right answers were refused"
        if seen != want:
            return False, "prompts differ: got %r, want %r" % (seen, want)
        if "kbdint-ok %s" % scenario not in shell:
            return False, "shell said %r" % shell
        ok, _seen, _shell = _login(listener.port, scenario, wrong=True)
        if ok:
            return False, "a wrong answer was accepted"
        if spec["password"]:
            if not _password_login(listener.port, SECRET):
                return False, "password method refused the right password"
            if _password_login(listener.port, "wrong"):
                return False, "password method accepted a wrong password"
        return True, ""
    except Exception as exc:  # a self-test reports, it does not crash
        return False, "%s: %s" % (type(exc).__name__, exc)
    finally:
        listener.stop.set()
        thread.join(5)


def run_self_test():
    host_key = make_host_key()
    failed = 0
    for scenario in SCENARIOS:
        passed, detail = self_test_scenario(scenario, host_key)
        if passed:
            print("PASS %s" % scenario, flush=True)
        else:
            failed += 1
            print("FAIL %s (%s)" % (scenario, detail), flush=True)
    return 1 if failed else 0


# --------------------------------------------------------------------- main

def parse_args(argv):
    parser = argparse.ArgumentParser(
        description="Paramiko keyboard-interactive test server for Nutshell "
                    "(manual check; not part of make test or CI).")
    parser.add_argument("--scenario", choices=sorted(SCENARIOS),
                        help="what the server asks and accepts")
    parser.add_argument("--port", type=int, default=2222,
                        help="TCP port (default 2222)")
    parser.add_argument("--bind", default="127.0.0.1",
                        help="address to listen on (default 127.0.0.1; use "
                             "the PC's LAN address to reach it from the "
                             "Windows box)")
    parser.add_argument("--self-test", action="store_true",
                        help="run every scenario against a paramiko client "
                             "and print PASS/FAIL per scenario")
    parser.add_argument("--verbose", action="store_true",
                        help="show paramiko's own log")
    args = parser.parse_args(argv)
    if not args.self_test and not args.scenario:
        parser.error("--scenario is required (or use --self-test)")
    return args


def main(argv=None):
    args = parse_args(sys.argv[1:] if argv is None else argv)
    if paramiko is None:
        print(PARAMIKO_MISSING, file=sys.stderr)
        return 2
    if not args.verbose:
        logging.getLogger("paramiko").setLevel(logging.CRITICAL)
    if args.self_test:
        return run_self_test()

    def log(msg):
        print(time.strftime("%H:%M:%S ") + msg, flush=True)

    if not is_loopback(args.bind):
        print("WARNING: listening on %s, not loopback: anyone who can reach "
              "this port can log in as %s with the fixed answers in this "
              "script. Use it on a trusted LAN only." % (args.bind, USER),
              file=sys.stderr, flush=True)
    host_key = make_host_key()
    try:
        listener = Listener(args.bind, args.port, args.scenario, host_key, log)
    except OSError as exc:
        print("cannot listen on %s:%d: %s" % (args.bind, args.port, exc),
              file=sys.stderr)
        return 1
    log("scenario %s on %s:%d, user %s, methods %s (Ctrl+C stops)"
        % (args.scenario, args.bind, listener.port, USER,
           allowed_auths(args.scenario)))
    try:
        listener.serve()
    except KeyboardInterrupt:
        listener.stop.set()
        print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
