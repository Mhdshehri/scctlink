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
LOBBY_HIGH = 0x0186000000000000  # every Steam lobby id starts like this


def code_of(lobby):
    """the short lobby code SCCTLink shows (lobbycode.c)"""
    alpha = "0123456789ABCDEFGHJKMNPQRSTVWXYZ"
    v = int(lobby) & 0xFFFFFFFF
    c = "".join(alpha[(v >> (5 * (6 - i))) & 31] for i in range(7))
    return c[:4] + "-" + c[4:]
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
if len(sys.argv) > 3:  # a second network adapter (address of another length), like Wi-Fi + Ethernet
    req(b'lo:2', 0x8916, sys.argv[3]); req(b'lo:2', 0x891c, '255.255.255.0')
fl = struct.unpack('16sh14s', fcntl.ioctl(s, 0x8913, struct.pack('16sh14s', b'lo', 0, b'')))[1]
fcntl.ioctl(s, 0x8914, struct.pack('16sh14s', b'lo', fl | 1, b''))
"""


class PC:
    def __init__(self, name, ip, ip2=None):
        self.name, self.ip, self.ip2 = name, ip, ip2
        self.bcast = ip.rsplit(".", 1)[0] + ".255"
        os.makedirs(NSDIR, exist_ok=True)
        self.path = os.path.join(NSDIR, "scct_" + name)
        self.close()
        open(self.path, "w").close()
        subprocess.check_call(["unshare", "--net=" + self.path, "true"])
        subprocess.check_call(["nsenter", "--net=" + self.path, sys.executable, "-I", "-c", CFG, ip, self.bcast] + ([ip2] if ip2 else []))

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


def fake(pc, role, *extra, name="splintercell3"):
    return Proc("game-" + pc.name, pc.cmd([sys.executable, "-I", FAKE, role, pc.ip, pc.bcast] + [str(x) for x in extra]),
                dict({"FAKE_NAME": name}, **({"FAKE_IP2": pc.ip2} if getattr(pc, "ip2", None) else {})))


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
    check(j.wait_for(r"user1001's Splinter Cell is running \(4 UDP / 1 TCP") is not None,
          "joiner learned the host game's ports (4 UDP, 1 TCP)")
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
    A = PC("a", "10.99.0.1", "10.88.200.240")   # host PC, with a second adapter
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
        check(r.get("announce_ip_le") == r.get("announce_from"), f"...and where it was stored as a little-endian number ({r.get('announce_ip_le')})")
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
        nu = r.get("netz_url") or ""
        check(nu.startswith(r.get("announce_from", "?") + ";"),
              f"Net-Z: the host's address inside the ENCRYPTED discovery reply was replaced too ({nu})")
        check(r.get("netz_join") is True, "Net-Z: joining at that address reaches the host's game (the real game's join)")
        check(r.get("netz_hello") == "URLOK",
              f"Net-Z: the joiner's own address in its game packets was replaced, checksum still valid ({r.get('netz_hello')})")
        check(r.get("netz_stations") == r.get("announce_from"),
              f"Net-Z: the host's address in its game packets was replaced, checksum still valid ({r.get('netz_stations')})")
        st2 = r.get("netz_stations2") or ["", False]
        same = False
        try:
            same = socket.inet_aton(st2[0]) == socket.inet_aton(r.get("announce_from", ""))
        except OSError:
            pass
        check(same and st2[1] is True,
              f"Net-Z: the host's SECOND adapter address inside an LZO-compressed message was replaced too, and joining there works ({st2})")
        check("10.88.200.240" not in jg.log, "the host's second address never reached the joiner's game")
        m = hg.wait_for(r"LOBBY CLIENT (\S+)", 2)
        check(m is not None and is_alias(m.group(1)), f"host's game sees the joiner at a loopback address ({m and m.group(1)})")
        check("10.42.7.23" not in hg.log, "the joiner's real address never reached the host's game")
        check(h.wait_for(r"Game traffic is flowing between you and user1002") is not None, "host was told the games are talking")

        print("[2] a stranger who is not the host's friend")
        C = PC("c", "192.168.50.9")
        st_ = relay(C, "stranger", 1009, "1001", ["join", "--no-launch", "--yes"], d)
        procs.append(st_)
        check(h.wait_for(r"Blocked user1009: they are not on your Steam friends list") is not None,
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
        lob555 = LOBBY_HIGH + 555
        with open(os.path.join(d, f"lobby_{lob555}"), "w") as f:
            f.write("owner 1007\nmember 1007\n")
        with open(os.path.join(d, "playing_1007"), "w") as f:
            f.write(f"{lob555}\n")
        x = relay(B, "J6", 1008, "1007", ["join", "--no-launch", "--yes"], d)
        procs.append(x)
        check(x.wait_for(r"not a Splinter Cell game") is not None, "joiner refused the foreign lobby")
        time.sleep(4)
        check(x.log.count(f"Joining lobby {code_of(lob555)}") == 1, "and did not keep retrying it")
        x.stop()
        os.unlink(os.path.join(d, "playing_1007"))

        print("[7] invite, and pasting the lobby id with L")
        h7 = relay(A, "H7", 1031, "1032,1033,1034", ["host"], d)
        procs.append(h7)
        check(h7.wait_for(r"Lobby is ready") is not None, "host ready")
        code = re.search(r"code (\S+),", h7.log).group(1)
        lobn = LOBBY_HIGH | int("".join("{:05b}".format("0123456789ABCDEFGHJKMNPQRSTVWXYZ".index(ch)) for ch in code.replace("-", "")), 2)
        lob = str(lobn)
        check(code == code_of(lobn), f"lobby shown as a short code ({code})")
        c7 = relay(B, "C7", 1032, "", ["join", "--no-launch"], d)
        procs.append(c7)
        check(c7.wait_for(r"No friend is hosting yet") is not None, "joiner with no visible host waits")
        with open(os.path.join(d, "invite_1032.tmp"), "w") as f:  # atomic, like a real invite
            f.write(f"{lob} 1031\n")
        os.rename(os.path.join(d, "invite_1032.tmp"), os.path.join(d, "invite_1032"))
        check(c7.wait_for(r"Accepted invite from user1031") is not None, "joiner saw the Steam invite")
        check(c7.wait_for(r"Connected to user1031 through Steam") is not None, "and connected through it")
        c7.stop()
        D = PC("d", "172.16.3.4")
        e = dict(os.environ, MOCK_DIR=d, MOCK_ID="1033", MOCK_FRIENDS="", ASAN_OPTIONS="detect_leaks=0")
        q = subprocess.Popen(D.cmd([BIN, "join", "--no-launch"]), env=e, stdin=subprocess.PIPE,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        time.sleep(1.0)
        q.stdin.write("L\n" + code.lower().replace("0", "o") + "\n"); q.stdin.flush()  # typed sloppily
        time.sleep(3.0)
        q.send_signal(signal.SIGINT)
        out, _ = q.communicate(timeout=5)
        check("Paste the host's lobby code" in out, "L asked for the lobby code")
        check("Joined user1031's Splinter Cell lobby" in out, "the code (lower case, O for 0) joined the host's lobby")
        check("Connected to user1031 through Steam" in out, "a friend joining by id gets connected")
        D.close()

        print("[7b] Ctrl+V with the lobby id on the clipboard joins straight away")
        E = PC("e", "172.16.9.9")
        e = dict(os.environ, MOCK_DIR=d, MOCK_ID="1034", MOCK_FRIENDS="", MOCK_CLIPBOARD=f" {code}\r\n",
                 ASAN_OPTIONS="detect_leaks=0")
        q = subprocess.Popen(E.cmd([BIN, "join", "--no-launch"]), env=e, stdin=subprocess.PIPE,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        time.sleep(1.0)
        q.stdin.write("^V\n"); q.stdin.flush()
        time.sleep(3.0)
        q.send_signal(signal.SIGINT)
        out, _ = q.communicate(timeout=5)
        check("Lobby code from the clipboard." in out, "Ctrl+V read the code from the clipboard")
        check("Connected to user1031 through Steam" in out, "and joined the host")
        E.close()
        h7.stop()

        print("[7c] Steam shows no lobby and no rich presence: the joiner asks the host's SCCTLink directly")
        h7c = relay(A, "H7c", 1081, "1082", ["host"], d)
        procs.append(h7c)
        check(h7c.wait_for(r"Lobby is ready") is not None, "host ready")
        j7c = relay(B, "J7c", 1082, "1081", ["join", "--no-launch", "--yes"], d,
                    {"MOCK_HIDE_LOBBY": "1", "MOCK_HIDE_RP": "1"})
        procs.append(j7c)
        check(j7c.wait_for(r"Connected to user1081 through Steam", timeout=20) is not None, "found and joined the host anyway")
        j7c.stop()
        s7 = relay(B, "S7c", 1083, "1081", ["join", "--no-launch", "--yes"], d,
                   {"MOCK_HIDE_LOBBY": "1", "MOCK_HIDE_RP": "1"})
        procs.append(s7)
        check(s7.wait_for(r"is hosting\. Join", timeout=8) is None, "a non-friend asking directly gets no answer")
        s7.stop(); h7c.stop()

        print("[7e] Steam shows no game at all for the host (private game details): found anyway")
        h7e = relay(A, "H7e", 1111, "1112", ["host"], d)
        procs.append(h7e)
        check(h7e.wait_for(r"Lobby is ready") is not None, "host ready")
        j7e = relay(B, "J7e", 1112, "1111", ["join", "--no-launch", "--yes"], d, {"MOCK_NO_GAMEINFO": "1"})
        procs.append(j7e)
        check(j7e.wait_for(r"Connected to user1111 through Steam", timeout=20) is not None, "joiner asked the host's SCCTLink and joined")
        j7e.stop(); h7e.stop()

        print("[7g] the host started the game, so Steam shows them in Splinter Cell (not Spacewar): found anyway")
        h7g = relay(A, "H7g", 1141, "1142", ["host"], d)
        procs.append(h7g)
        check(h7g.wait_for(r"Lobby is ready") is not None, "host ready")
        j7g = relay(B, "J7g", 1142, "1141", ["join", "--no-launch", "--yes"], d, {"MOCK_FRIEND_APP": "13570"})
        procs.append(j7g)
        check(j7g.wait_for(r"Connected to user1141 through Steam", timeout=12) is not None, "asked the host's SCCTLink and joined")
        j7g.stop(); h7g.stop()

        print("[7h] Refresh: finds a host right away and says so")
        e = dict(os.environ, MOCK_DIR=d, MOCK_ID="1152", MOCK_FRIENDS="1151", ASAN_OPTIONS="detect_leaks=0", MOCK_NO_GAMEINFO="1")
        q = subprocess.Popen(B.cmd([BIN, "idle", "--no-launch"]), env=e, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, text=True)
        time.sleep(1)
        q.stdin.write("R\n"); q.stdin.flush()
        time.sleep(3)
        h7h = relay(A, "H7h", 1151, "1152", ["host"], d)
        procs.append(h7h)
        h7h.wait_for(r"Lobby is ready")
        q.stdin.write("R\n"); q.stdin.flush()
        time.sleep(1.2)
        q.stdin.write("N\n"); q.stdin.flush()
        time.sleep(1)
        q.stdin.write("Q\n"); q.stdin.flush()
        try:
            out, _ = q.communicate(timeout=8)
        except subprocess.TimeoutExpired:
            q.kill(); out, _ = q.communicate()
        check("Nobody is hosting right now." in out, "Refresh with nobody hosting says so")
        check("user1151 is hosting. Join their game?" in out, "Refresh after the friend hosts finds them at once")
        h7h.stop()

        print("[7d] an older SCCTLink host is reported clearly")
        lob777 = LOBBY_HIGH + 777
        with open(os.path.join(d, f"lobby_{lob777}"), "w") as f:
            f.write("owner 1091\nmember 1091\ndata scctlink 1\ndata scctlink_ver 0.3\ndata scctlink_mode coop\n")
        o = relay(B, "O7d", 1092, "1091", ["join", "--no-launch", code_of(lob777)], d)
        procs.append(o)
        check(o.wait_for(r"user1091 uses a different SCCTLink version \(0\.3\)") is not None, "joiner told: different version (0.3)")
        o.stop()

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

        print("[10] Versus (Spies vs. Mercenaries): host + 2 friends, SCCT_Versus.exe")
        C = PC("c", "192.168.50.9")
        hv = relay(A, "HV", 1051, "1052,1053", ["host", "--versus"], d)
        procs.append(hv)
        check(hv.wait_for(r"Creating a friends-only Versus lobby for 4 players") is not None, "Versus lobby has room for 4 players")
        check(hv.wait_for(r"Lobby is ready") is not None, "host ready")
        hvg = fake(A, "host", name="SCCT_Versus")
        procs.append(hvg)
        check(hv.wait_for(r"Splinter Cell is running") is not None, "host's SCCTLink found SCCT_Versus and its ports")
        jv1 = relay(B, "JV1", 1052, "1051", ["join", "--versus", "--yes"], d)
        jv2 = relay(C, "JV2", 1053, "1051", ["join", "--versus", "--yes"], d)
        procs += [jv1, jv2]
        for jv in (jv1, jv2):
            check(jv.wait_for(r"Connected to user1051 through Steam") is not None, f"{jv.name} connected")
        check("would open steam://" not in jv1.log, "Versus joiner doesn't launch the co-op game through Steam")
        g1 = fake(B, "join", 500, name="SCCT_Versus")
        g2 = fake(C, "join", 500, name="SCCT_Versus")
        procs += [g1, g2]
        for g in (g1, g2):
            check(g.wait_for("JOIN DONE", timeout=90) is not None, f"{g.name} finished its session")
            r = g.results()
            st = r.get("stream") or [0, False, False]
            check(r.get("found_host") and is_alias(r.get("announce_from", "")), f"{g.name} found the host in its LAN list")
            check(r.get("pingpong_ok") == 200 and st == [500, True, True], f"{g.name}: every packet back, exact and in order ({st})")
            t = r.get("tcp") or ["", False, False]
            check(t[1] and t[2], f"{g.name}: TCP works")
        clients = set(re.findall(r"LOBBY CLIENT (\S+)", hvg.log))
        check(len(clients) == 2 and all(is_alias(c) for c in clients), f"host's game sees the two friends as two different loopback addresses ({clients})")

        print("[11] a co-op joiner is told when the host is playing Versus")
        D = PC("d", "172.16.3.4")
        jc = relay(D, "JC", 1054, "1051", ["join", "--no-launch", "--yes"], d)
        procs.append(jc)
        check(jc.wait_for(r"user1051 is hosting Versus, but you picked Co-op\. Click Stop, switch to Versus") is not None, "joiner got a clear message")
        check(jc.wait_for(r"Connected to user1051", timeout=3) is None, "and did not connect")
        for p in (hv, jv1, jv2, hvg, g1, g2, jc):
            p.stop()
        C.close(); D.close()
        check(all("ERROR: AddressSanitizer" not in p.log and "runtime error" not in p.log for p in procs),
              "no memory errors (Versus)")

        print("[12] start screen: switch to Versus, then Host (the window's buttons)")
        e = dict(os.environ, MOCK_DIR=d, MOCK_ID="1061", MOCK_FRIENDS="1062", ASAN_OPTIONS="detect_leaks=0")
        q = subprocess.Popen(A.cmd([BIN, "idle"]), env=e, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, text=True)
        time.sleep(1)
        q.stdin.write("V\nH\n"); q.stdin.flush()
        time.sleep(3)
        q.stdin.write("V\nX\nC\nQ\n"); q.stdin.flush()
        try:
            out, _ = q.communicate(timeout=8)
        except subprocess.TimeoutExpired:
            q.kill(); out, _ = q.communicate()
        check("Mode: Versus (SCCT_Versus.exe, up to 4 players)." in out, "Versus picked on the start screen")
        check("Creating a friends-only Versus lobby for 4 players" in out, "Host then hosts Versus for 4")
        check("Click Stop first, then switch between Co-op and Versus." in out, "the mode can't change while hosting")
        check("Mode: Co-op (the co-op campaign, up to 2 players)." in out, "after Stop it switches back to Co-op")
        check(q.returncode == 0 and "SCCTLink closed." in out, "Q closes cleanly")

        print("[12b] a game sitting in the menu (no network sockets yet) is reported once, not over and over")
        hm = relay(A, "HM", 1071, "1072", ["host"], d)
        procs.append(hm)
        check(hm.wait_for(r"Lobby is ready") is not None, "host ready")
        idle = Proc("idle", A.cmd([sys.executable, "-I", "-c",
                                   "import ctypes,time;ctypes.CDLL(None).prctl(15,b'splintercell3',0,0,0);time.sleep(8)"]))
        procs.append(idle)
        time.sleep(6)
        check(hm.log.count("Splinter Cell is running.") == 1 and "not running" not in hm.log,
              f"'running' printed once ({hm.log.count('Splinter Cell is running.')} times)")
        idle.stop()
        check(hm.wait_for(r"Splinter Cell is not running", timeout=10) is not None, "closing the game is still noticed")
        hm.stop()

        print("[12c] both games use the same port numbers on all addresses, like the real game")
        hc = relay(A, "HC", 1131, "1132", ["host"], d)
        procs.append(hc)
        check(hc.wait_for(r"Lobby is ready") is not None, "host ready")
        hgc = Proc("game-a2", A.cmd([sys.executable, "-I", FAKE, "host", A.ip, A.bcast]), {"FAKE_BIND_ANY": "1", "FAKE_SKIP_NETZ": "1"})
        procs.append(hgc)
        check(hgc.wait_for("HOST READY") is not None, "host's game holds 0.0.0.0:7776 and 0.0.0.0:8888")
        jc2 = relay(B, "JC2", 1132, "1131", ["join", "--no-launch", "--yes"], d)
        procs.append(jc2)
        check(jc2.wait_for(r"user1131's Splinter Cell is running") is not None, "joiner connected and knows the host's ports")
        time.sleep(1)
        jgc = Proc("game-b2", B.cmd([sys.executable, "-I", FAKE, "join", B.ip, B.bcast, "300"]), {"FAKE_BIND_ANY": "1", "FAKE_SKIP_NETZ": "1"})
        procs.append(jgc)
        check(jgc.wait_for("JOIN DONE", timeout=90) is not None, "joiner's game (also on 7776/8888) finished")
        r = jgc.results()
        st = r.get("stream") or [0, False, False]
        check(r.get("found_host") and r.get("pingpong_ok") == 200 and st == [300, True, True],
              f"the friend found the host and joined: every packet back, in order ({r.get('pingpong_ok')}, {st})")
        for x in (hc, jc2, hgc, jgc):
            x.stop()

        print("[12d] the friend is already linked when the host creates the game (SCCTLink must not take its ports)")
        hd = relay(A, "HD", 1161, "1162", ["host"], d)
        procs.append(hd)
        check(hd.wait_for(r"Lobby is ready") is not None, "host ready")
        jd = relay(B, "JD", 1162, "1161", ["join", "--no-launch", "--yes"], d)
        procs.append(jd)
        check(jd.wait_for(r"Connected to user1161") is not None, "friend linked first")
        hold = Proc("hold-b", B.cmd([sys.executable, "-I", "-c",
            "import socket,time,ctypes;ctypes.CDLL(None).prctl(15,b'splintercell3',0,0,0)\n"
            "s=socket.socket(2,2);s.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1);s.bind(('0.0.0.0',7776));time.sleep(30)"]))
        procs.append(hold)
        menu = Proc("menu-a", A.cmd([sys.executable, "-I", "-c",   # host's game sitting in its main menu
            "import time,ctypes;ctypes.CDLL(None).prctl(15,b'splintercell3',0,0,0);time.sleep(8)"]))
        procs.append(menu)
        check(hd.wait_for(r"user1162's Splinter Cell is running \(1 UDP", timeout=10) is not None,
              "the host's SCCTLink knows the friend's game holds 7776 (and prepares for it)")
        time.sleep(3)
        udp = subprocess.run(A.cmd(["cat", "/proc/net/udp"]), capture_output=True, text=True).stdout
        taken = [l for l in udp.splitlines() if re.search(r"\b00000000:(1E60|22B8)\b", l)]
        check(not taken, f"SCCTLink did not take 0.0.0.0:7776 or 0.0.0.0:8888 on the host's PC ({len(taken)})")
        menu.stop()
        hgd = Proc("game-a3", A.cmd([sys.executable, "-I", FAKE, "host", A.ip, A.bcast]), {"FAKE_BIND_ANY": "1", "FAKE_SKIP_NETZ": "1"})
        procs.append(hgd)
        check(hgd.wait_for("HOST READY", timeout=10) is not None and "Address already in use" not in hgd.log,
              "the host's game could still open 0.0.0.0:7776 and 8888 and create its game")
        for x in (hd, jd, hold, hgd):
            x.stop()

        print("[14] privacy: Steam direct connections off; the lobby isn't a Steam \"Join Game\" button")
        hp = relay(A, "HP", 1171, "1172", ["host"], d)
        procs.append(hp)
        check(hp.wait_for(r"Lobby is ready") is not None, "host ready")
        check(os.path.exists(os.path.join(d, "rp_scctlink_lobby_1171")) and not os.path.exists(os.path.join(d, "rp_connect_1171")),
              "lobby id published under scctlink_lobby, not Steam's connect key")
        jp = relay(B, "JP", 1172, "1171", ["join", "--no-launch", "--yes", "--verbose"], d, {"MOCK_HIDE_LOBBY": "1"})
        procs.append(jp)
        check(jp.wait_for(r"Connected to user1171 through Steam") is not None, "found through the scctlink_lobby key")

        print("[15] kick: the host removes a friend, who can't link again until the host stops")
        hp.p.stdin = None
        jp.stop(); hp.stop()
        check("[mock] direct connections were off" in hp.log, "Steam direct (ICE) connections were turned off")
        e = dict(os.environ, MOCK_DIR=d, MOCK_ID="1181", MOCK_FRIENDS="1182", ASAN_OPTIONS="detect_leaks=0")
        hk = subprocess.Popen(A.cmd([BIN, "host"]), env=e, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, text=True)
        time.sleep(1.5)
        jk = relay(B, "JK", 1182, "1181", ["join", "--no-launch", "--yes"], d)
        procs.append(jk)
        check(jk.wait_for(r"Connected to user1181 through Steam") is not None, "friend linked")
        time.sleep(2)
        hk.stdin.write("K\n"); hk.stdin.flush()
        check(jk.wait_for(r"user1181 removed you from their game \(kicked\)", timeout=8) is not None, "the friend is told they were kicked")
        time.sleep(5)
        check(jk.log.count("Connected to user1181 through Steam") == 1, "and does not get linked again")
        hk.stdin.write("Q\n"); hk.stdin.flush()
        try:
            hout, _ = hk.communicate(timeout=8)
        except subprocess.TimeoutExpired:
            hk.kill(); hout, _ = hk.communicate()
        check("user1182 is kicked." in hout, "host log says so")
        check("[alert] friend linked" in hout, "host got the 'friend linked' alert")
        jk.stop()

        print("[16] game files check: a mod on one PC only is reported on both sides")
        gd = os.path.join(d, "games")

        def game_dir(name, files):
            root = os.path.join(gd, name)
            for rel, data in files.items():
                path = os.path.join(root, *rel.split("/"))
                os.makedirs(os.path.dirname(path), exist_ok=True)
                with open(path, "wb") as f:
                    f.write(data)
            return root
        base = {"System/splintercell3.exe": b"MZ" + b"\1" * 5000, "System/Core.u": b"core" * 300,
                "Maps/1_1_0Tbilisi.scl": b"map" * 900, "System/SplinterCell3.ini": b"[Engine]\nres=1920\n"}
        for k in range(1500):  # a real game folder: the file list is ~100 KB, sent in pieces
            base[f"Textures/Pack{k:04d}_with_a_long_name_like_the_real_ones.utx"] = b"t" * (k + 1)
        modded = dict(base)
        modded["System/dinput8.dll"] = b"MZasi-loader"
        modded["System/scripts/SplinterCellChaosTheory.WidescreenFix.asi"] = b"MZwsfix"
        modded["System/SplinterCell3.ini"] = b"[Engine]\nres=2560\n"  # settings differ on purpose: not reported
        other = dict(base)
        other["System/Core.u"] = b"core" * 299 + b"CORE"  # same size, other content (another patch)
        ga, gb, gc = game_dir("a", modded), game_dir("b", other), game_dir("c", base)
        hm = relay(A, "HM", 1191, "1192", ["host"], d, {"MOCK_GAME_DIR": ga})
        procs.append(hm)
        check(hm.wait_for(r"\[mods\] Mod files found \(2\)") is not None,
              "Mods tab: the host's own mods are listed right away (no friend, no game running)")
        check(hm.wait_for(r"Lobby is ready") is not None, "host ready")
        jm = relay(B, "JM", 1192, "1191", ["join", "--no-launch", "--yes"], d, {"MOCK_GAME_DIR": gb})
        procs.append(jm)
        check(jm.wait_for(r"Connected to user1191 through Steam") is not None, "friend linked")
        check(hm.wait_for(r"Game files differ from user1192's", timeout=15) is not None, "host is told the games differ")
        check(hm.wait_for(r"only on your PC: system/dinput8\.dll", timeout=3) is not None, "...the mod loader only on the host's PC")
        check(hm.wait_for(r"only on your PC: system/scripts/splintercellchaostheory\.widescreenfix\.asi", timeout=3) is not None,
              "...the widescreen fix only on the host's PC")
        check(hm.wait_for(r"different version: system/core\.u", timeout=3) is not None, "...a game file with the same size but other content")
        check(jm.wait_for(r"only on user1191's PC: system/dinput8\.dll", timeout=15) is not None, "the friend sees the same, from their side")
        check(hm.wait_for(r"\[mods\] \[x\] Same game version", timeout=3) is not None, "Mods tab: [x] same game version")
        check(hm.wait_for(r"\[mods\] \[  \] Same mods - 2 differences", timeout=3) is not None, "Mods tab: [  ] same mods, 2 differences")
        check(hm.wait_for(r"\[mods\] \[  \] Same game files - 1 difference", timeout=3) is not None, "Mods tab: [  ] same game files, 1 difference")
        check("splintercell3.ini" not in hm.log + jm.log, "settings files (.ini) are not compared")
        check(gd not in hm.log + jm.log and gd not in open(os.path.join(d, "log_HM.txt")).read(), "the game folder's path is never sent or logged")
        hm.stop(); jm.stop()
        hs = relay(A, "HS", 1193, "1194", ["host"], d, {"MOCK_GAME_DIR": gc})
        procs.append(hs)
        check(hs.wait_for(r"Lobby is ready") is not None, "host ready")
        gdd = game_dir("d", base)
        js = relay(B, "JS", 1194, "1193", ["join", "--no-launch", "--yes"], d, {"MOCK_GAME_DIR": gdd}, stdin=subprocess.PIPE)
        procs.append(js)
        check(js.wait_for(r"Game files check: same as user1193's \(1503 files\)", timeout=15) is not None, "identical games: 'same' on the friend's side")
        check(hs.wait_for(r"Game files check: same as user1194's", timeout=15) is not None, "...and on the host's side")
        check(hs.wait_for(r"\[mods\] All 1503 files are the same", timeout=3) is not None, "Mods tab: all the same")
        with open(os.path.join(gdd, "System", "d3d9.dll"), "wb") as f:  # the friend installs a mod...
            f.write(b"MZreshade")
        js.p.stdin.write("M\n"); js.p.stdin.flush()  # ...and clicks "Check again"
        check(js.wait_for(r"Game files checked again", timeout=10) is not None, "Check again: the friend's game is listed again")
        check(js.wait_for(r"only on your PC: system/d3d9\.dll", timeout=10) is not None, "...the new mod shows up on the friend's side")
        check(hs.wait_for(r"only on user1194's PC: system/d3d9\.dll", timeout=10) is not None, "...and on the host's side, without clicking anything")
        hs.stop(); js.stop()

        print("[17] VPN mode (experimental): a virtual adapter on each PC, the game's packets carried unchanged")
        hv = relay(A, "HV", 1201, "1202", ["host", "--vpn"], d, stdin=subprocess.PIPE)
        procs.append(hv)
        check(hv.wait_for(r"VPN mode: your PC is 10\.231\.0\.1 on the virtual LAN") is not None, "host's virtual adapter is up at 10.231.0.1")
        jv = relay(B, "JV", 1202, "1201", ["join", "--no-launch", "--yes", "--vpn"], d)
        procs.append(jv)
        check(jv.wait_for(r"VPN mode with user1201: your game sees them at 10\.231\.0\.1") is not None, "friend switched to VPN mode")
        check(hv.wait_for(r"VPN mode with user1202: your game sees them at 10\.231\.0\.2") is not None, "host switched to VPN mode")
        hgv = Proc("game-av", A.cmd([sys.executable, "-I", FAKE, "host", "10.231.0.1", "10.231.0.255"]), {"FAKE_BIND_ANY": "1"})
        procs.append(hgv)
        check(hgv.wait_for("HOST READY") is not None, "host's game is up")
        time.sleep(1.5)
        jgv = Proc("game-bv", B.cmd([sys.executable, "-I", FAKE, "join", "10.231.0.2", "10.231.0.255", "500"]), {"FAKE_BIND_ANY": "1"})
        procs.append(jgv)
        check(jgv.wait_for("JOIN DONE", timeout=90) is not None, "friend's game finished its session")
        r = jgv.results()
        check(r.get("found_host") is True and r.get("announce_from") == "10.231.0.1", f"found the host by LAN broadcast at 10.231.0.1 ({r.get('announce_from')})")
        check(r.get("announce_ip") == "10.231.0.1", "the address inside the game's packet is real and needed no swapping")
        check(r.get("pingpong_ok") == 200, f"200/200 game packets went there and back ({r.get('pingpong_ok')})")
        st = r.get("stream") or [0, False, False]
        check(st == [500, True, True], f"500 mixed-size packets: all back, exact, in order ({st})")
        t = r.get("tcp") or ["", False, False]
        check(t[1] and t[2], "TCP lobby connection works both ways, 300 KB message included")
        check((r.get("netz_url") or "").startswith("10.231.0.1;") and r.get("netz_join") is True, f"Net-Z: encrypted discovery and join, untouched ({r.get('netz_url')})")
        check(r.get("netz_hello") == "URLOK", "Net-Z: the friend's own address in its messages is right as it is")
        m = hgv.wait_for(r"LOBBY CLIENT (\S+)", 2)
        check(m is not None and m.group(1) == "10.231.0.2", f"host's game sees the friend at 10.231.0.2 ({m and m.group(1)})")
        sneak = subprocess.run(B.cmd([sys.executable, "-I", "-c",
                                      "import socket;s=socket.socket(2,2);s.settimeout(2);s.sendto(b'SNEAK',('10.231.0.1',7776))\n"
                                      "try:\n print('GOT', s.recvfrom(100))\nexcept OSError: print('NOTHING')"]),
                               capture_output=True, text=True, timeout=10).stdout
        check("NOTHING" in sneak, "another program on the friend's PC can't use the virtual adapter")
        hv.p.stdin.write("D\n"); hv.p.stdin.flush()  # Details: runs the VPN test
        check(hv.wait_for(r"direct: 20 of 20 answered", timeout=8) is not None, "VPN test: 20/20 direct test packets answered")
        check(hv.wait_for(r"LAN broadcast \(10\.231\.0\.255\): 5 of 5 answered", timeout=3) is not None, "VPN test: LAN broadcasts reach the friend")
        check(hv.wait_for(r"VPN test: OK", timeout=3) is not None, "VPN test: OK")
        check(jv.wait_for(r"VPN test: test packets from 10\.231\.0\.1 arrived here", timeout=3) is not None, "the friend's side says the test packets arrived")
        hv.p.stdin.write("X\n"); hv.p.stdin.flush()  # Stop
        time.sleep(1.5)
        check("VPN mode: virtual adapter removed" in open(os.path.join(d, "log_HV.txt")).read(), "the adapter is removed when the host clicks Stop")
        hv.stop(); jv.stop(); hgv.stop(); jgv.stop()

        print("[13] bad internet: 10% packet loss each way and 60 ms delay each way")
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
