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

import os
ctypes.CDLL(None).prctl(15, os.environ.get("FAKE_NAME", "splintercell3").encode(), 0, 0, 0)  # PR_SET_NAME

GAME, BEACON, LOBBY, PUSH = 7776, 8888, 7777, 9999
NETZ_DISC, NETZ_GAME = 9100, 9103  # Chaos Theory's real ports (Quazal Net-Z)


# --- Quazal Net-Z LAN discovery, as the real game does it ---------------------
# PRUDP v0 USER packet: 10-byte header, payload, 1-byte checksum. The payload is
# RC4-encrypted with key = MD5 applied 4099 times to "LAN" (fresh RC4 for every
# packet). The reply carries "prudp:/address=<LAN IP>;port=9103" inside the
# encrypted part, and the joiner connects to exactly that address.
def _netz_key():
    import hashlib
    k = b"LAN"
    for _ in range(0x1003):
        k = hashlib.md5(k).digest()
    return k


NETZ_KEY = _netz_key()


def rc4(key, data):
    S = list(range(256))
    j = 0
    for i in range(256):
        j = (j + S[i] + key[i % len(key)]) & 255
        S[i], S[j] = S[j], S[i]
    i = j = 0
    out = bytearray()
    for b in data:
        i = (i + 1) & 255
        j = (j + S[i]) & 255
        S[i], S[j] = S[j], S[i]
        out.append(b ^ S[(S[i] + S[j]) & 255])
    return bytes(out)


def netz_checksum(data):
    n = len(data) // 4
    t = sum(struct.unpack_from("<%dI" % n, data)) & 0xFFFFFFFF
    return (sum(data[n * 4:]) + sum(struct.pack("<I", t))) & 0xFF


def netz_packet(plain):
    import hashlib
    plain = plain + hashlib.md5(plain).digest()  # Quazal's MD5Checksum, as the real game
    p = bytes([0x61, 0x61, 0x05, 0x00]) + bytes(6) + rc4(NETZ_KEY, plain)
    return p + bytes([netz_checksum(p)])


NETZ_DATA_KEY = 0x5A  # checksum key byte of the game's data stream (any value; SCCTLink learns it)


def netz_data(payload):
    p = bytes([0x31, 0x31, 0x02, 0x00]) + bytes(6) + b"\0" + payload
    return p + bytes([(NETZ_DATA_KEY + netz_checksum(p)) & 0xFF])


def netz_data_open(p):
    if len(p) < 12 or p[2] & 7 != 2 or (NETZ_DATA_KEY + netz_checksum(p[:-1])) & 0xFF != p[-1]:
        return None  # a bad checksum: the real game drops the packet
    return p[11:-1]


def lzo_pack(data, ref=None):
    """A real LZO1X stream: literals, one back-reference (ref = (start, length)
    of a repeat inside data) and the end marker. Enough to hide text the way
    the real game's compressed messages do."""
    def lit_first(b):
        assert 4 <= len(b) <= 238
        return bytes([17 + len(b)]) + b

    def lit(b):
        n = len(b) - 3
        assert n >= 1
        if n <= 15:
            return bytes([n]) + b
        out = bytearray([0])
        n -= 15
        while n > 255:
            out.append(0)
            n -= 255
        return bytes(out) + bytes([n]) + b
    if not ref:
        out = lit_first(data[:200])
        rest = data[200:]
        if rest:
            out += bytes([0x20 | 1, 0, 0])  # (not used: messages stay short)
        return out + b"\x11\x00\x00"
    start, length = ref
    src = data.index(data[start:start + length])  # earlier copy
    assert src < start and 3 <= length - 2 <= 31
    dist = start - src
    out = lit_first(data[:start])
    out += bytes([0x20 | (length - 2), ((dist - 1) << 2) & 0xFF, (dist - 1) >> 6])
    out += lit(data[start + length:])
    return out + b"\x11\x00\x00"


def lzo_unpack(src):
    """LZO1X decompression (the joiner's game reads the message like the real one)."""
    out = bytearray()
    ip = 0

    def take(k):
        nonlocal ip
        b = src[ip:ip + k]
        ip += k
        return b
    t = src[ip]
    ip += 1
    state = 0
    if t > 17:
        out += take(t - 17)
        state = 4
        t = src[ip]; ip += 1
    while True:
        if state == 0:
            if t < 16:
                if t == 0:
                    while src[ip] == 0:
                        t += 255; ip += 1
                    t += 15 + src[ip]; ip += 1
                out += take(t + 3)
                state = 4
                t = src[ip]; ip += 1
                if t < 16:
                    m = len(out) - 0x801 - (t >> 2) - (src[ip] << 2); ip += 1
                    out += out[m:m + 3]
                    state = t & 3
                    out += take(state)
                    t = src[ip]; ip += 1
                    continue
        elif t < 16:
            m = len(out) - 1 - (t >> 2) - (src[ip] << 2); ip += 1
            out += out[m:m + 2]
            state = t & 3
            out += take(state)
            t = src[ip]; ip += 1
            continue
        if t >= 64:
            ln = (t >> 5) - 1
            m = len(out) - 1 - ((t >> 2) & 7) - (src[ip] << 3); ip += 1
            b0 = t
        elif t >= 32:
            ln = t & 31
            if ln == 0:
                while src[ip] == 0:
                    ln += 255; ip += 1
                ln += 31 + src[ip]; ip += 1
            b0, b1 = src[ip], src[ip + 1]; ip += 2
            m = len(out) - 1 - ((b0 | b1 << 8) >> 2)
        else:
            m = len(out) - ((t & 8) << 11)
            ln = t & 7
            if ln == 0:
                while src[ip] == 0:
                    ln += 255; ip += 1
                ln += 7 + src[ip]; ip += 1
            b0, b1 = src[ip], src[ip + 1]; ip += 2
            m -= (b0 | b1 << 8) >> 2
            if m == len(out):
                return bytes(out)
            m -= 0x4000
        for k in range(ln + 2):
            out.append(out[m + k])
        state = b0 & 3
        out += take(state)
        t = src[ip]; ip += 1


def quazal_msg(body):  # u32 length, 1 byte, body (as the real game's messages)
    return struct.pack("<I", len(body)) + b"\x01" + body


def quazal_payload(m):
    """Message from a DATA payload: ratio byte 0 = plain, else LZO."""
    if not m:
        return None
    raw = m[1:] if m[0] == 0 else lzo_unpack(m[1:])
    if len(raw) < 5 or struct.unpack_from("<I", raw)[0] + 5 != len(raw):
        return None
    return raw[5:]


def netz_open(p):
    import hashlib
    if len(p) < 12 or p[2] & 7 != 5 or netz_checksum(p[:-1]) != p[-1]:
        return None
    m = rc4(NETZ_KEY, p[10:-1])
    if len(m) < 16 or hashlib.md5(m[:-16]).digest() != m[-16:]:
        return None  # the real game drops a reply whose MD5 doesn't match
    return m[:-16]


ANY = os.environ.get("FAKE_BIND_ANY") == "1"  # bind ports on all addresses, like most games


def host(ip, bcast):
    bind_ip = "0.0.0.0" if ANY else ip
    g = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    g.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 8 << 20)
    if ANY:  # Windows lets a game bind 0.0.0.0:port next to SCCTLink's specific addresses; Linux needs this
        for x in (g,):
            x.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    g.bind((bind_ip, GAME))
    b = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    b.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    b.bind((bind_ip, BEACON))
    t = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    t.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    t.bind((bind_ip, LOBBY))
    t.listen(4)
    print("HOST READY", flush=True)

    def beacon():
        while True:
            d, a = b.recvfrom(2000)
            if d.startswith(b"QUERY"):
                txt = f"{ip}:{LOBBY}".encode()
                b.sendto(b"ANNOUNCE" + socket.inet_aton(ip) + struct.pack(">H", GAME) + bytes([len(txt)]) + txt
                         + socket.inet_aton(ip)[::-1], a)  # also as a little-endian number

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

    nd = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    nd.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    nd.bind((bind_ip, NETZ_DISC))
    ng = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    ng.bind((bind_ip, NETZ_GAME))

    def netz_disc():
        while True:
            d, a = nd.recvfrom(2000)
            q = netz_open(d)
            if q is None or q[:4] != b"\x08\x00\x00\x00":
                continue
            url = f"prudp:/address={ip};port={NETZ_GAME}".encode() + b"\0"
            body = (struct.pack("<II", 2, 0) + q[8:12] + b"\x01\x0e\x00SPLINTERCELL3\x00"
                    + struct.pack("<H", len(url)) + url + b"SessionDifficulty=1")
            nd.sendto(netz_packet(body), a)

    def netz_game():
        while True:
            d, a = ng.recvfrom(2000)
            m = netz_data_open(d)
            if m is None:
                continue
            if m.startswith(b"HELLO "):  # the joiner's own station URL, as Net-Z sends it
                said = m[6:].split(b"prudp:/address=", 1)[-1].split(b";", 1)[0].decode()
                m = b"URLOK" if said == a[0] else b"URLBAD " + said.encode()
            elif m == b"STATIONS2":  # both adapters' URLs, LZO-compressed like the real game
                ip2 = os.environ.get("FAKE_IP2", ip)
                u1 = f"prudp:/address={ip};port={NETZ_GAME}".encode() + b"\0"
                u2 = f"prudp:/address={ip2};port={NETZ_GAME}".encode() + b"\0"
                body = b"\x00\x10\x03" + struct.pack("<H", len(u1)) + u1 + struct.pack("<H", len(u2)) + u2 + b"\x01\x00\x00\x01"
                msg = quazal_msg(body)
                k = msg.index(u2)
                common = 0
                while u1[common] == u2[common]:
                    common += 1
                m = b"\x01" + lzo_pack(msg, (k, min(common, 33)))  # the shared "prudp:/address=10." becomes a back-reference
                assert quazal_payload(m) == body
                ng.sendto(netz_data(m), a)
                continue
            elif m == b"STATIONS":
                m = f"prudp:/address={ip};port={NETZ_GAME}".encode() + b"\0"
            else:
                m = b"NZ:" + m
            ng.sendto(netz_data(m), a)

    for f in (beacon, push, game, lobby, netz_disc, netz_game):
        threading.Thread(target=f, daemon=True).start()
    while True:
        time.sleep(1)


def result(k, v):
    print("RESULT " + json.dumps([k, v]), flush=True)


def join(ip, bcast, n_stream):
    held = []
    if ANY:  # the joiner's game holds the same port numbers as the host's
        # (not BEACON: on Linux SCCTLink's per-address broadcast catcher can't hear
        # broadcasts, which Windows delivers to it; the clash that matters is GAME)
        for port in (GAME,):
            x = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            # Windows lets a game bind 0.0.0.0:port next to SCCTLink's 127.x:port
            # (MS "SO_REUSEADDR and SO_EXCLUSIVEADDRUSE" table); Linux needs this flag
            x.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            x.bind(("0.0.0.0", port))
            held.append(x)
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
    le = d[15 + d[14]:19 + d[14]]
    result("announce_ip_le", socket.inet_ntoa(le[::-1]) if len(le) == 4 else None)
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

    if ANY:  # like the real game: game traffic leaves from its own fixed port
        g = held[0]
    else:
        g = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        g.bind((ip, 0))
    g.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 8 << 20)
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
    # Net-Z: find the host by the real game's encrypted discovery, then connect
    # to the address read from inside the encrypted reply
    nq = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    nq.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    nq.bind((ip, 0))
    nq.settimeout(0.5)
    url = None
    end = time.time() + (0 if ANY else 15)  # the 0.0.0.0 session tests port clashes, not Net-Z
    while time.time() < end and not url:
        nq.sendto(netz_packet(struct.pack("<II", 8, 1) + os.urandom(4)), (bcast, NETZ_DISC))
        try:
            d, a = nq.recvfrom(2000)
            r = netz_open(d)
            if r and b"prudp:/address=" in r:
                url = r.split(b"prudp:/address=", 1)[1].split(b"\0", 1)[0].decode()
        except socket.timeout:
            pass
    result("netz_url", url)
    joined = False
    replies = []
    if url:
        addr, port = url.split(";port=")
        dest = (addr, int(port))

        def ask(msg):
            for _ in range(10):
                nq.sendto(netz_data(msg), dest)
                try:
                    d, a = nq.recvfrom(2000)
                    return netz_data_open(d)  # None = bad checksum
                except socket.timeout:
                    pass
            return b"(no answer)"
        joined = ask(b"JOIN") == b"NZ:JOIN"
        for i in range(4):
            ask(b"PING%d" % i)
        replies.append(ask(f"HELLO prudp:/address={ip};port={NETZ_GAME}".encode()))
        replies.append(ask(b"STATIONS"))
        r2 = quazal_payload(ask(b"STATIONS2") or b"")
        urls = []
        if r2:
            for part in r2.split(b"prudp:/address=")[1:]:
                urls.append(part.split(b";", 1)[0].decode())
        ok2 = False
        if len(urls) == 2:
            try:
                dest2 = (socket.inet_ntoa(socket.inet_aton(urls[1])), int(port))  # inet_aton reads "007" like inet_addr
                for _ in range(10):
                    nq.sendto(netz_data(b"JOIN2"), dest2)
                    try:
                        d, a = nq.recvfrom(2000)
                        if netz_data_open(d) == b"NZ:JOIN2":
                            ok2 = True
                            break
                    except socket.timeout:
                        pass
            except OSError:
                pass
        result("netz_stations2", [urls[1] if len(urls) == 2 else repr(urls), ok2])
    result("netz_hello", replies[0].decode(errors="replace") if replies and replies[0] else repr(replies[:1]))
    st = replies[1] if len(replies) > 1 else None
    result("netz_stations", st.split(b"prudp:/address=", 1)[-1].split(b";")[0].decode() if st else repr(st))
    result("netz_join", joined)
    print("JOIN DONE", flush=True)
    while True:
        time.sleep(1)


if __name__ == "__main__":
    if sys.argv[1] == "host":
        host(sys.argv[2], sys.argv[3])
    else:
        join(sys.argv[2], sys.argv[3], int(sys.argv[4]) if len(sys.argv) > 4 else 2000)
