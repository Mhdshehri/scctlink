"""End-to-end tests for SCCTLink with the mock Steam (Linux, run as root).

Each player is a separate "PC": its own network namespace with its own LAN
address and broadcast domain, so LAN discovery broadcasts can't leak between
them except through SCCTLink. Mock Steam talks over Unix sockets in a shared
folder. fake_scct.py plays the role of splintercell3.exe on each PC.
Prints ALL OK or FAIL.
"""
import fcntl
import json
import os
import re
import shutil
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, "build", "scctlink_mock")
FAKE = os.path.join(ROOT, "test", "fake_scct.py")
NSDIR = "/run/netns"
FAILS = []
LOGS = []


def check(cond, what):
    print(("  ok   " if cond else "  FAIL ") + what, flush=True)
    if not cond:
        FAILS.append(what)


# ---------- "PCs" ----------

CFG = r"""
import fcntl, socket, struct, sys
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
def req(name, code, addr):
    fcntl.ioctl(s, code, struct.pack('16sH2s4s8s', name, socket.AF_INET, b'\0\0', socket.inet_aton(addr), b'\0' * 8))
req(b'lo:1', 0x8916, sys.argv[1]); req(b'lo:1', 0x891c, '255.255.255.0'); req(b'lo:1', 0x891a, sys.argv[2])
fl = struct.unpack('16sh14s', fcntl.ioctl(s, 0x8913, struct.pack('16sh14s', b'lo', 0, b'')))[1]
fcntl.ioctl(s, 0x8914, struct.pack('16sh14s', b'lo', fl | 1, b''))
"""


class PC:
    def __init__(self, name, ip):
        self.name, self.ip = name, ip
        self.bcast = ip.rsplit(".", 1)[0] + ".255"
        os.makedirs(NSDIR, exist_ok=True)
        self.path = os.path.join(NSDIR, "scct_" + name)
        self.close()
        open(self.path, "w").close()
        subprocess.check_call(["unshare", "--net=" + self.path, "true"])
        subprocess.check_call(["nsenter", "--net=" + self.path, sys.executable, "-I", "-c", CFG, ip, self.bcast])

    def cmd(self, args):
        return ["nsenter", "--net=" + self.path] + args

    def close(self):
        if os.path.exists(self.path):
            subprocess.call(["umount", self.path], stderr=subprocess.DEVNULL)
            os.unlink(self.path)


class Proc:
    def __init__(self, name, argv, env=None, stdin=subprocess.DEVNULL):
        self.name = name
        self.log = ""
        e = dict(os.environ, ASAN_OPTIONS="detect_leaks=0", **(env or {}))
        self.p = subprocess.Popen(argv, env=e, stdin=stdin, stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT, text=True)
        self.t = threading.Thread(target=self._read, daemon=True)
        self.t.start()
        LOGS.append(self)

    def _read(self):
        for line in self.p.stdout:
            self.log += line

    def wait_for(self, pattern, timeout=15):
        end = time.time() + timeout
        while time.time() < end:
            m = re.search(pattern, self.log)
            if m:
                return m
            time.sleep(0.05)
        return None

    def results(self):
        out = {}
        for line in self.log.splitlines():
            if line.startswith("RESULT "):
                k, v = json.loads(line[7:])
                out[k] = v
        return out

    def stop(self):
        if self.p.poll() is None:
            self.p.send_signal(signal.SIGINT)
            try:
                self.p.wait(5)
            except subprocess.TimeoutExpired:
                self.p.kill()
                self.p.wait(2)
        self.t.join(2)


def relay(pc, name, mock_id, friends, args, mockdir, extra=None, stdin=subprocess.DEVNULL):
    env = dict(MOCK_DIR=mockdir, MOCK_ID=str(mock_id), MOCK_FRIENDS=friends,
               SCCTLINK_LOG=os.path.join(mockdir, f"log_{name}.txt"))
    env.update(extra or {})
    return Proc(name, pc.cmd([BIN] + args), env, stdin)


def fake(pc, role, *extra):
    return Proc("game-" + pc.name, pc.cmd([sys.executable, "-I", FAKE, role, pc.ip, pc.bcast] + [str(x) for x in extra]))


def is_alias(ip):
    return ip.startswith("127.") and ip != "127.0.0.1"


def session(title, mockdir, host_pc, join_pc, extra=None, n_stream=2000, join_first=False):
    """Host and joiner relays + fake games. Returns (host relay, join relay, host game, join game)."""
    print(title, flush=True)
    h = relay(host_pc, "host", 1001, "1002", ["host"], mockdir, extra)
    check(h.wait_for(r"Lobby is ready") is not None, "host created a friends-only lobby")
    hg = fake(host_pc, "host")
    check(hg.wait_for("HOST READY") is not None, "host's game is up (LAN game created)")
    check(h.wait_for(r"Splinter Cell is running") is not None, "host's SCCTLink found splintercell3 and its ports")
    j = relay(join_pc, "join", 1002, "1001", ["join", "--no-launch", "--yes"], mockdir, extra)
    check(j.wait_for(r"Connected to user1001 through Steam") is not None, "joiner found the host by itself and connected")
    check(j.wait_for(r"user1001's Splinter Cell is running \(2 UDP / 1 TCP") is not None,
          "joiner learned the host game's ports (2 UDP, 1 TCP)")
    jg = fake(join_pc, "join", n_stream)
    check(jg.wait_for("JOIN DONE", timeout=90) is not None, "joiner's game finished its scripted session")
    return h, j, hg, jg


def main():
    if os.geteuid() != 0:
        print("run as root (network namespaces)")
        return 1
    if not os.path.exists(BIN):
        print("build first")
        return 1
    d = tempfile.mkdtemp(prefix="scctlink-mock-")
    os.chmod(d, 0o777)
    A = PC("a", "10.99.0.1")   # host PC
    B = PC("b", "10.42.7.23")  # joiner PC: a different LAN, as over the internet
    procs = []
    try:
        print("[0] the two PCs really are separate")
        probe = Proc("probe", B.cmd([sys.executable, "-I", "-c",
                                     "import socket;s=socket.socket(2,2);s.settimeout(1)\n"
                                     "try:\n s.sendto(b'x',('10.99.0.1',7776));print('SENT')\nexcept OSError as e: print('UNREACHABLE',e)"]))
        probe.p.wait(5)
        probe.t.join(2)
        check("UNREACHABLE" in probe.log, "PC B cannot reach PC A's address directly (only through SCCTLink)")

        h, j, hg, jg = session("[1] LAN co-op through the Steam relay", d, A, B)
        procs += [h, j, hg, jg]
        r = jg.results()
        check(r.get("found_host") is True, "joiner's game found the host in its LAN list (broadcast discovery)")
        check(is_alias(r.get("announce_from", "")), f"the host appears at a private loopback address ({r.get('announce_from')})")
        check(r.get("announce_ip") == r.get("announce_from"),
              f"host's LAN address inside the packet was replaced with that loopback address ({r.get('announce_ip')})")
        check(r.get("announce_text", "").split(":")[0] == r.get("announce_from"),
              f"...also where the game wrote it as text ({r.get('announce_text')})")
        check("10.99.0.1" not in jg.log, "the host's real address never reached the joiner's game")
        check(r.get("push") and is_alias(r["push"][0]) and r["push"][1] == r["push"][0],
              f"host's LAN announcement broadcast reached the joiner, address hidden ({r.get('push')})")
        check(r.get("pingpong_ok") == 200, f"200/200 game packets went to the host's game and back ({r.get('pingpong_ok')})")
        st = r.get("stream") or [0, False, False]
        check(st[0] == 2000, f"2000 mixed-size packets sent back-to-back: all came back ({st[0]})")
        check(st[1], "every packet came back byte-for-byte identical")
        check(st[2], "packets stayed in order")
        b = r.get("burst") or [0, False, False]
        check(b == [300, True, True], f"a 300 x 1400-byte burst (level load) arrived intact and in order ({b})")
        check((r.get("rtt_median_ms") or 99) < 5, f"relay adds little delay: median round trip {r.get('rtt_median_ms')} ms (mock Steam, no internet)")
        t = r.get("tcp") or ["", False, False]
        check(t[1] and t[2], "TCP lobby connection works both ways, including a 300 KB message")
        m = hg.wait_for(r"LOBBY CLIENT (\S+)", 2)
        check(m is not None and is_alias(m.group(1)), f"host's game sees the joiner at a loopback address ({m and m.group(1)})")
        check("10.42.7.23" not in hg.log, "the joiner's real address never reached the host's game")
        check(h.wait_for(r"Game traffic is flowing between you and user1002") is not None, "host was told the games are talking")

        print("[2] a stranger who is not the host's friend")
        C = PC("c", "192.168.50.9")
        st_ = relay(C, "stranger", 1009, "1001", ["join", "--no-launch", "--yes"], d)
        procs.append(st_)
        check(h.wait_for(r"Blocked user1009: they are in the lobby but not on your Steam friends list") is not None,
              "host blocked the stranger (in the lobby, but not a Steam friend)")
        check(st_.wait_for(r"Connected to user1001", timeout=4) is None, "the stranger never got connected")
        st_.stop()
        C.close()

        print("[3] the joiner quits; the host keeps running")
        j.stop()
        check(h.wait_for(r"user1002 disconnected \(quit SCCTLink\)") is not None, "host dropped the joiner cleanly")
        check(h.p.poll() is None, "host's SCCTLink is still running")
        jg.stop()

        print("[4] reconnect after quitting (same game still running on the host)")
        j2 = relay(B, "join2", 1002, "1001", ["join", "--no-launch", "--yes"], d)
        procs.append(j2)
        check(j2.wait_for(r"Connected to user1001 through Steam") is not None, "joiner reconnected")
        jg2 = fake(B, "join", 300)
        procs.append(jg2)
        check(jg2.wait_for("JOIN DONE", timeout=60) is not None, "joiner's game ran again")
        r2 = jg2.results()
        check(r2.get("found_host") and r2.get("pingpong_ok") == 200, "found the host again and every packet came back")
        j2.stop(); jg2.stop()

        print("[5] host quits")
        j3 = relay(B, "join3", 1002, "1001", ["join", "--no-launch", "--yes"], d)
        procs.append(j3)
        check(j3.wait_for(r"Connected to user1001") is not None, "joiner connected")
        h.stop()
        check(j3.wait_for(r"user1001 closed SCCTLink|stopped hosting") is not None, "joiner was told the host left")
        time.sleep(1)
        check(j3.p.poll() is None, "joiner keeps running, waiting for the host to come back")
        j3.stop(); hg.stop()
        check(all("ERROR: AddressSanitizer" not in p.log and "runtime error" not in p.log for p in procs),
              "no memory errors (AddressSanitizer / UBSan)")

        print("[6] a Spacewar lobby that is not SCCTLink is refused")
        with open(os.path.join(d, "lobby_555"), "w") as f:
            f.write("owner 1007\nmember 1007\n")
        with open(os.path.join(d, "playing_1007"), "w") as f:
            f.write("555\n")
        x = relay(B, "J6", 1008, "1007", ["join", "--no-launch", "--yes"], d)
        procs.append(x)
        check(x.wait_for(r"not a Splinter Cell co-op game") is not None, "joiner refused the foreign lobby")
        time.sleep(4)
        check(x.log.count("Joining lobby 555") == 1, "and did not keep retrying it")
        x.stop()
        os.unlink(os.path.join(d, "playing_1007"))

        print("[7] invite, and pasting the lobby id with L")
        h7 = relay(A, "H7", 1031, "1032,1033", ["host"], d)
        procs.append(h7)
        check(h7.wait_for(r"Lobby is ready") is not None, "host ready")
        lob = re.search(r"id (\d+)", h7.log).group(1)
        c7 = relay(B, "C7", 1032, "", ["join", "--no-launch"], d)
        procs.append(c7)
        check(c7.wait_for(r"No friend is hosting yet") is not None, "joiner with no visible host waits")
        with open(os.path.join(d, "invite_1032"), "w") as f:
            f.write(f"{lob} 1031\n")
        check(c7.wait_for(r"Accepted invite from user1031") is not None, "joiner saw the Steam invite")
        check(c7.wait_for(r"Connected to user1031 through Steam") is not None, "and connected through it")
        c7.stop()
        D = PC("d", "172.16.3.4")
        e = dict(os.environ, MOCK_DIR=d, MOCK_ID="1033", MOCK_FRIENDS="", ASAN_OPTIONS="detect_leaks=0")
        q = subprocess.Popen(D.cmd([BIN, "join", "--no-launch"]), env=e, stdin=subprocess.PIPE,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        time.sleep(1.0)
        q.stdin.write("L\n" + lob + "\n"); q.stdin.flush()
        time.sleep(3.0)
        q.send_signal(signal.SIGINT)
        out, _ = q.communicate(timeout=5)
        check("Paste the host's lobby id" in out, "L asked for the lobby id")
        check("Joined user1031's Splinter Cell lobby" in out, "pasting the id joined the host's lobby")
        check("Connected to user1031 through Steam" in out, "a friend joining by id gets connected")
        h7.stop()
        D.close()

        print("[8] confirmation: without --yes the joiner asks first, and 'no' joins nothing")
        h8 = relay(A, "H8", 1041, "1042", ["host"], d)
        procs.append(h8)
        h8.wait_for(r"Lobby is ready")
        e = dict(os.environ, MOCK_DIR=d, MOCK_ID="1042", MOCK_FRIENDS="1041", ASAN_OPTIONS="detect_leaks=0")
        q = subprocess.Popen(B.cmd([BIN, "join", "--no-launch"]), env=e, stdin=subprocess.PIPE,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        try:
            out, _ = q.communicate("n\n", timeout=6)
        except subprocess.TimeoutExpired:
            q.send_signal(signal.SIGINT)
            out, _ = q.communicate(timeout=5)
        check("Join their game? (Y/N)" in out, "joiner asked before joining")
        check("Joining lobby" not in out, "answering no joined nothing")
        h8.stop()

        print("[9] a program that is not the game can't use the relay")
        h9, j9, hg9, jg9 = session("    (setup)", d, A, B, n_stream=200)
        procs += [h9, j9, hg9, jg9]
        alias = jg9.results().get("announce_from")
        sneak = Proc("sneak", B.cmd([sys.executable, "-I", "-c",
                                     "import socket,sys;s=socket.socket(2,2);s.settimeout(1);s.sendto(b'SNEAK',(sys.argv[1],7776))\n"
                                     "try: print('GOT',s.recvfrom(100))\nexcept Exception: print('NOTHING')", alias]))
        sneak.p.wait(5); sneak.t.join(2)
        check("NOTHING" in sneak.log, "another program on the joiner's PC got nothing through")
        check(j9.wait_for(r"from a program that is not the game", timeout=3) is not None or
              "not the game" in open(os.path.join(d, "log_join.txt")).read(),
              "SCCTLink dropped it and wrote that in its log")
        for p in (h9, j9, hg9, jg9):
            p.stop()

        print("[10] bad internet: 10% packet loss each way and 60 ms delay each way")
        h10, j10, hg10, jg10 = session("    (setup)", d, A, B, extra={"MOCK_LOSS": "10", "MOCK_DELAY_MS": "60"}, n_stream=1000)
        procs += [h10, j10, hg10, jg10]
        r = jg10.results()
        check(r.get("found_host") is True, "discovery still works (the game repeats its LAN query)")
        st = r.get("stream") or [0, False, False]
        check(650 <= st[0] < 1000, f"about 81% of round trips survive, as expected for 10% each way ({st[0]}/1000)")
        check(st[1] and st[2], "what arrives is exact and in order (the game re-sends the rest itself)")
        rtt = r.get("rtt_median_ms") or 0
        check(115 <= rtt < 140, f"round trip = 2 x 60 ms + under 20 ms relay overhead ({rtt} ms)")
        t = r.get("tcp") or ["", False, False]
        check(t[1] and t[2], f"TCP is carried reliably despite the loss ({str(t)[:80]})")
        for p in (h10, j10, hg10, jg10):
            p.stop()
        check(all("ERROR: AddressSanitizer" not in p.log and "runtime error" not in p.log for p in procs),
              "no memory errors anywhere (AddressSanitizer / UBSan)")
    finally:
        for p in LOGS:
            p.stop()
        if FAILS and os.environ.get("SHOWLOGS"):
            for p in LOGS:
                print(f"===== {p.name}\n{p.log[-3000:]}")
        for pc in (A, B):
            pc.close()
        if os.environ.get("KEEP"):
            print("logs kept in", d)
        else:
            shutil.rmtree(d, ignore_errors=True)
    print("ALL OK" if not FAILS else f"FAIL ({len(FAILS)}): " + "; ".join(FAILS))
    return 0 if not FAILS else 1


if __name__ == "__main__":
    sys.exit(main())
