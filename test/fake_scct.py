"""A stand-in for splintercell3.exe on one "PC" (a Linux network namespace).

The real game's LAN protocol is not documented, so this fake covers the usual
LAN-game behaviours a relay has to survive:
  * discovery by broadcast: the joiner broadcasts QUERY to the host's beacon
    port; the host answers with its own LAN address inside the packet, both
    as 4 raw bytes and as text (the joiner must connect to what it reads);
  * the host also announces itself by broadcast to the joiner's port (push);
  * game traffic on UDP, echoed back by the host;
  * a TCP lobby connection.
Sockets are bound to the PC's LAN address, like a game that picks the
"primary" network adapter. The process renames itself to "splintercell3"
so SCCTLink finds it by name.

usage: fake_scct.py host <lan ip> <bcast>
       fake_scct.py join <lan ip> <bcast> [n_stream]
The joiner runs a scripted test and prints RESULT lines.
"""
import ctypes
import json
import random
import socket
import struct
import sys
import threading
import time

ctypes.CDLL(None).prctl(15, b"splintercell3", 0, 0, 0)  # PR_SET_NAME

GAME, BEACON, LOBBY, PUSH = 7776, 8888, 7777, 9999


def host(ip, bcast):
    g = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    g.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 8 << 20)
    g.bind((ip, GAME))
    b = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    b.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    b.bind((ip, BEACON))
    t = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    t.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    t.bind((ip, LOBBY))
    t.listen(4)
    print("HOST READY", flush=True)

    def beacon():
        while True:
            d, a = b.recvfrom(2000)
            if d.startswith(b"QUERY"):
                txt = f"{ip}:{LOBBY}".encode()
                b.sendto(b"ANNOUNCE" + socket.inet_aton(ip) + struct.pack(">H", GAME) + bytes([len(txt)]) + txt, a)

    def push():
        while True:
            b.sendto(b"HELLO-LAN" + socket.inet_aton(ip), (bcast, PUSH))
            time.sleep(0.3)

    def game():
        while True:
            d, a = g.recvfrom(70000)
            if d.startswith(b"BURST"):
                for i in range(int(d[5:])):
                    g.sendto(i.to_bytes(4, "big") + bytes((i + k) & 255 for k in range(1396)), a)
            else:
                g.sendto(b"S:" + d, a)

    def lobby():
        while True:
            c, a = t.accept()

            def serve(c=c, a=a):
                print("LOBBY CLIENT " + a[0], flush=True)
                c.sendall(b"WELCOME you are " + a[0].encode() + b"\n")
                buf = b""
                while True:
                    d = c.recv(65536)
                    if not d:
                        break
                    buf += d
                    while b"\n" in buf:
                        line, buf = buf.split(b"\n", 1)
                        c.sendall(b"T:" + line + b"\n")
                c.close()
            threading.Thread(target=serve, daemon=True).start()

    for f in (beacon, push, game, lobby):
        threading.Thread(target=f, daemon=True).start()
    while True:
        time.sleep(1)


def result(k, v):
    print("RESULT " + json.dumps([k, v]), flush=True)


def join(ip, bcast, n_stream):
    q = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    q.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    q.bind((ip, 0))
    q.settimeout(0.5)
    p = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)  # listens for the host's push broadcasts
    p.bind(("0.0.0.0", PUSH))
    p.settimeout(0.2)
    print("JOIN READY", flush=True)
    t0 = time.time()
    ann = None
    while time.time() - t0 < 30 and not ann:
        q.sendto(b"QUERY", (bcast, BEACON))
        try:
            d, a = q.recvfrom(2000)
            if d.startswith(b"ANNOUNCE"):
                ann = (d, a)
        except socket.timeout:
            pass
    if not ann:
        result("found_host", False)
        return
    d, a = ann
    hip = socket.inet_ntoa(d[8:12])
    gport = struct.unpack(">H", d[12:14])[0]
    txt = d[15:15 + d[14]].decode()
    result("found_host", True)
    result("announce_from", a[0])
    result("announce_ip", hip)
    result("announce_text", txt)
    result("discovery_s", round(time.time() - t0, 2))

    # push broadcast from the host reaches us
    got_push = None
    end = time.time() + 5
    while time.time() < end and not got_push:
        try:
            dd, aa = p.recvfrom(2000)
            if dd.startswith(b"HELLO-LAN"):
                got_push = (aa[0], socket.inet_ntoa(dd[9:13]))
        except socket.timeout:
            pass
    result("push", list(got_push) if got_push else None)

    g = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    g.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 8 << 20)
    g.bind((ip, 0))
    g.settimeout(1.0)
    dest = (hip, gport)

    # ping-pong
    ok = 0
    rtts = []
    for i in range(200):
        m = b"ping-%d-" % i + b"x" * 50
        s0 = time.perf_counter()
        g.sendto(m, dest)
        try:
            r, _ = g.recvfrom(70000)
            ok += r == b"S:" + m
            rtts.append((time.perf_counter() - s0) * 1000)
        except socket.timeout:
            pass
    rtts.sort()
    result("pingpong_ok", ok)
    result("rtt_median_ms", round(rtts[len(rtts) // 2], 2) if rtts else None)

    # stream: many mixed-size packets back-to-back
    rnd = random.Random(7)
    sent = []
    for i in range(n_stream):
        body = i.to_bytes(4, "big") + bytes(rnd.randrange(256) for _ in range(rnd.randrange(16, 1400)))
        sent.append(body)
        g.sendto(body, dest)
        if i % 10 == 9:
            time.sleep(0.005)
    got = []
    try:
        while True:
            got.append(g.recvfrom(70000)[0])
    except socket.timeout:
        pass
    exact = all(x[:2] == b"S:" and x[2:] == sent[int.from_bytes(x[2:6], "big")] for x in got)
    idx = [int.from_bytes(x[2:6], "big") for x in got]
    result("stream", [len(got), exact, idx == sorted(idx)])

    # burst: like a level load
    g.sendto(b"BURST300", dest)
    got = []
    try:
        while True:
            got.append(g.recvfrom(70000)[0])
    except socket.timeout:
        pass
    okb = all(len(x) == 1400 and x[4:] == bytes((int.from_bytes(x[:4], "big") + k) & 255 for k in range(1396)) for x in got)
    idx = [int.from_bytes(x[:4], "big") for x in got]
    result("burst", [len(got), okb, idx == sorted(idx)])

    # TCP lobby at the address the host gave as text
    lip, lport = txt.rsplit(":", 1)
    try:
        c = socket.create_connection((lip, int(lport)), timeout=5)
        c.settimeout(5)
        f = c.makefile("rb")
        welcome = f.readline().decode().strip()
        big = bytes(random.Random(3).randrange(1, 256) for _ in range(300000)).replace(b"\n", b"A")
        c.sendall(b"hello\n" + big + b"\n")
        l1 = f.readline()
        l2 = f.readline()
        result("tcp", [welcome, l1 == b"T:hello\n", l2 == b"T:" + big + b"\n"])
        c.close()
    except Exception as e:
        result("tcp", ["error " + str(e), False, False])
    print("JOIN DONE", flush=True)
    while True:
        time.sleep(1)


if __name__ == "__main__":
    if sys.argv[1] == "host":
        host(sys.argv[2], sys.argv[3])
    else:
        join(sys.argv[2], sys.argv[3], int(sys.argv[4]) if len(sys.argv) > 4 else 2000)
