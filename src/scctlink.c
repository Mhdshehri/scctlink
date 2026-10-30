/* SCCTLink - play Splinter Cell: Chaos Theory co-op (LAN mode) with a Steam
 * friend: no VPN, no port forwarding, no IP addresses.
 *
 * Both players run SCCTLink. The host creates a friends-only Steam lobby
 * (through Valve's free "Spacewar" test app, AppID 480); the friend joins it.
 * Each side then makes the other player's game look like a second PC on a LAN
 * that only exists on this computer's loopback addresses (127.x.y.z):
 *
 *   - SCCTLink reads which UDP/TCP ports splintercell3.exe has open (the same
 *     list Windows' "netstat -ano" shows) and sends that list to the other side.
 *   - For every port of the other player's game it opens the same port on a
 *     private loopback address that stands for that player. When your game
 *     sends there, the packet goes to the other side through Steam and comes
 *     out of the same port number into their game.
 *   - LAN discovery broadcasts from your game (the "LAN games" list) are
 *     caught on the other player's ports and passed on the same way.
 *   - TCP connections are carried as streams.
 *   - If the game puts this PC's LAN address inside a packet, it is replaced
 *     with the loopback address that stands for this PC on the other side, so
 *     no real address ever reaches the other player.
 *
 * SCCTLink doesn't change the game's network code. It also checks that both
 * games match (files, settings) and can install the SCCT Fix (a small d3d9.dll:
 * 60 FPS cap, borderless window) that co-op needs. Same design as the author's
 * CoFLink. MIT license. */
#include "steam.h"
#include "plat.h"
#include "ui.h"

#define VERSION "0.9.4"
#define SCCT_APPID 13570
#define SPACEWAR_APPID 480
#define MAX_PEERS 3
#define BUF_SIZE 65536
#define CH_GAME 0
#define CH_CTL 1
#define CH_TCP 2
#define PROTO '3' /* relay protocol; both players need the same */
#define KEEPALIVE_MS 1000
#define PEER_TIMEOUT_MS 30000
#define HOST_QUIET_MS 15000
#define MAX_GAME_PKT 8192
#define MAX_PKTS_PER_SEC 4000
#define MAX_ALIAS 64
#define MAX_CATCH 64
#define MAX_STREAMS 32
#define TCP_CHUNK 16000
#define TCP_OUTBUF_MAX (2 << 20)
#define HDR 7 /* kind, src port, dst port, seq */

static void *U, *F, *MM, *NM, *NU, *AP;
static int32_t g_pipe;
static u64 g_me, g_lobby, g_host, g_joining;
static int g_mode;      /* MODE_IDLE / MODE_HOST / MODE_JOIN */
static bool g_is_host;  /* g_mode == MODE_HOST */
static bool g_steam_ok;
static int g_max_players = 2, g_max_players_opt;
static bool g_no_launch, g_verbose;
static const char *g_exe = "splintercell3";
static const char *g_exe_opt;
static bool g_versus; /* Spies vs. Mercenaries (SCCT_Versus.exe), up to 4 players */
/* What the player does in the game menus, per mode. */
#define HOST_MENU (g_versus ? "In Versus: create a LAN game" : "Co-op -> LAN -> Create game")
#define JOIN_MENU (g_versus ? "In Versus: open the LAN game list and join the host" : "Co-op -> LAN -> Join game")
#define MODE_NAME (g_versus ? "Versus" : "Co-op")
static uint8_t g_buf[BUF_SIZE];
static uint8_t g_out[BUF_SIZE + 64];

/* ---------- output ---------- */

static void say(const char *fmt, ...)
{
    char b[1200];
    va_list ap;
    va_start(ap, fmt);
#ifdef _WIN32
    _vsnprintf(b, sizeof b, fmt, ap);
#else
    vsnprintf(b, sizeof b, fmt, ap);
#endif
    va_end(ap);
    b[sizeof b - 1] = 0;
    ui_log(b);
    log_line(b);
}

/* Debug detail: only in the log file (and the console with --verbose). */
static void note(const char *fmt, ...)
{
    char b[1200];
    va_list ap;
    va_start(ap, fmt);
#ifdef _WIN32
    _vsnprintf(b, sizeof b, fmt, ap);
#else
    vsnprintf(b, sizeof b, fmt, ap);
#endif
    va_end(ap);
    b[sizeof b - 1] = 0;
    char l[1220];
    sfmt(l, sizeof l, "  . %s", b);
    if (g_verbose) ui_log(l);
    log_line(l);
}

static const char *idstr(u64 v)
{
    static char ring[4][24];
    static int k;
    char *o = ring[k++ & 3];
    u64_to_str(v, o);
    return o;
}

/* Steam names are chosen by other people: print them with control characters
   replaced, so a name can't mess with the console. */
static const char *clean(const char *n)
{
    static char ring[4][72];
    static int k;
    char *o = ring[k++ & 3];
    int i = 0;
    for (; n && n[i] && i < 64; i++) {
        unsigned char c = (unsigned char)n[i];
        o[i] = (c < 0x20 || c == 0x7f) ? '?' : (char)c;
    }
    if (n && n[i]) { /* cut short: don't leave half of a UTF-8 character (e.g. Arabic) */
        int j = i;
        while (j > 0 && ((unsigned char)o[j - 1] & 0xC0) == 0x80) j--;
        if (j > 0) {
            unsigned char lead = (unsigned char)o[j - 1];
            int need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
            if (j - 1 + need > i) i = j - 1; /* incomplete: drop it */
        }
    }
    o[i] = 0;
    return o;
}

static const char *who(u64 id)
{
    if (id == g_me) return clean(S.Friends_GetPersonaName(F));
    const char *n = S.Friends_GetFriendPersonaName(F, id);
    return (n && *n && strcmp(n, "[unknown]") != 0) ? clean(n) : idstr(id);
}

static const char *codestr(u64 lobby)
{
    static char ring[4][16];
    static int k;
    char *o = ring[k++ & 3];
    lobby_code(lobby, o);
    return o;
}

/* ---------- addresses ---------- */

static uint32_t g_my_ips[16];  /* this PC's LAN addresses (never sent anywhere) */
static int g_nmy_ips;
static u64 g_ips_at;

static void refresh_my_ips(void)
{
    if (g_ips_at && now_ms() - g_ips_at < 5000) return;
    g_ips_at = now_ms();
    g_nmy_ips = local_ipv4s(g_my_ips, 16);
}

static bool is_my_ip(uint32_t ip)
{
    for (int i = 0; i < g_nmy_ips; i++) if (g_my_ips[i] == ip) return true;
    return false;
}

static void ip_text(uint32_t ip, char out[16])
{
    const uint8_t *b = (const uint8_t *)&ip;
    sfmt(out, 16, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
}

/* For logs: never print a real LAN address, only what kind it is. */
static const char *ip_kind(uint32_t ip)
{
    static char ring[4][24];
    static int k;
    char *o = ring[k++ & 3];
    if (ip == 0) return "any";
    if (is_loop(ip)) { ip_text(ip, o); return o; }
    for (int i = 0; i < g_nmy_ips; i++) if (g_my_ips[i] == ip) { sfmt(o, 24, "LAN-IP#%d", i + 1); return o; }
    return "other-IP";
}

/* A loopback address that stands for another PC. Its dotted text has the same
   length as that PC's real LAN address (so the text form can be swapped too),
   and its bytes are unusual (no zeros) so the swap can't hit ordinary data. */
static uint32_t make_alias(int text_len, int k)
{
    static const int A[] = { 7, 73, 173 }, B[] = { 3, 37, 137 };
    if (text_len < 9 || text_len > 15) text_len = 12;
    int need = text_len - 6; /* digits in the last three octets */
    for (int ai = 0; ai < 3; ai++)
        for (int bi = 0; bi < 3; bi++) {
            int lc = need - (ai + 1) - (bi + 1);
            if (lc < 1 || lc > 3) continue;
            int c = lc == 1 ? 1 + k : lc == 2 ? 11 + k * 10 + k : 191 + k;
            return ip4(127, A[ai], B[bi], c);
        }
    return ip4(127, 73, 37, 191 + k);
}

static int ip_text_len(uint32_t ip) { char t[16]; ip_text(ip, t); return (int)strlen(t); }

/* `ip` as dotted text of exactly `len` characters. Octets below 8 may get
   leading zeros ("127.007.3.1"): Windows' inet_addr, which the game uses to
   read addresses, takes "007" as octal 7, the same number. So one loopback
   address can stand in for LAN addresses of any length (a PC with two network
   adapters, say). False if it can't be made that long. */
static bool ip_text_len_exact(uint32_t ip, int len, char out[16])
{
    ip_text(ip, out);
    int extra = len - (int)strlen(out);
    if (extra == 0) return true;
    if (extra < 0 || len > 15) return false;
    const uint8_t *b = (const uint8_t *)&ip;
    int pad[4] = { 0, 0, 0, 0 };
    for (int i = 3; i >= 1 && extra > 0; i--) /* never the first octet */
        if (b[i] < 8) { pad[i] = extra < 2 ? extra : 2; extra -= pad[i]; }
    if (extra) return false;
    int k = 0;
    for (int i = 0; i < 4; i++) {
        if (i) out[k++] = '.';
        for (int z = 0; z < pad[i]; z++) out[k++] = '0';
        char t[4];
        int m = 0, v = b[i];
        do { t[m++] = (char)('0' + v % 10); v /= 10; } while (v);
        while (m) out[k++] = t[--m];
    }
    out[k] = 0;
    return true;
}

/* Replace every copy of `from` (4 raw bytes, dotted text, UTF-16 text) with `to`. */
static int swap_ip(uint8_t *p, int n, uint32_t from, uint32_t to)
{
    int hits = 0;
    uint8_t f[4], t[4];
    memcpy(f, &from, 4);
    memcpy(t, &to, 4);
    for (int i = 0; i + 4 <= n; i++)
        if (p[i] == f[0] && !memcmp(p + i, f, 4)) { memcpy(p + i, t, 4); hits++; i += 3; }
    /* the same address in the other byte order (a number in memory order) */
    uint8_t fr[4] = { f[3], f[2], f[1], f[0] }, tr[4] = { t[3], t[2], t[1], t[0] };
    for (int i = 0; i + 4 <= n; i++)
        if (p[i] == fr[0] && !memcmp(p + i, fr, 4)) { memcpy(p + i, tr, 4); hits++; i += 3; }
    char ft[16], tt[16];
    ip_text(from, ft);
    int L = (int)strlen(ft);
    if (!ip_text_len_exact(to, L, tt)) return hits;
    for (int i = 0; i + L <= n; i++) {
        if (p[i] == (uint8_t)ft[0] && !memcmp(p + i, ft, (size_t)L)) {
            bool digit_before = i > 0 && p[i - 1] >= '0' && p[i - 1] <= '9';
            bool digit_after = i + L < n && p[i + L] >= '0' && p[i + L] <= '9';
            if (!digit_before && !digit_after) { memcpy(p + i, tt, (size_t)L); hits++; i += L - 1; }
        }
    }
    for (int i = 0; i + 2 * L <= n; i++) {
        bool m = true;
        for (int j = 0; j < L && m; j++) m = p[i + 2 * j] == (uint8_t)ft[j] && p[i + 2 * j + 1] == 0;
        if (!m) continue;
        for (int j = 0; j < L; j++) p[i + 2 * j] = (uint8_t)tt[j];
        hits++;
        i += 2 * L - 1;
    }
    return hits;
}

/* ---------- Quazal Net-Z (Chaos Theory's network library) ----------
   The game's LAN discovery reply carries "prudp:/address=<LAN IP>;port=9103"
   and the joining game connects to exactly that address. The reply is a PRUDP
   v0 USER packet: 10-byte header, payload, 1-byte checksum, and the payload is
   RC4-encrypted (a fresh RC4 for every packet) with key = MD5 applied 4099
   times to "LAN" (found in splintercell3.exe; test/fake_scct.py derives it).
   So the address is decrypted, swapped like any other copy, and the packet is
   encrypted again with a new checksum. Same length, nothing else changes. */
static const uint8_t NETZ_LAN_KEY[16] = {
    0x18, 0xa9, 0xe8, 0x23, 0xdd, 0x0c, 0x13, 0xa0, 0x4a, 0x09, 0x7d, 0x51, 0x6c, 0xf8, 0xe1, 0x8a
};
#define NETZ_HDR 10

static void rc4_crypt(const uint8_t *key, int klen, uint8_t *d, int n)
{
    uint8_t S[256];
    for (int i = 0; i < 256; i++) S[i] = (uint8_t)i;
    for (int i = 0, j = 0; i < 256; i++) {
        j = (j + S[i] + key[i % klen]) & 255;
        uint8_t t = S[i]; S[i] = S[j]; S[j] = t;
    }
    for (int k = 0, i = 0, j = 0; k < n; k++) {
        i = (i + 1) & 255;
        j = (j + S[i]) & 255;
        uint8_t t = S[i]; S[i] = S[j]; S[j] = t;
        d[k] ^= S[(S[i] + S[j]) & 255];
    }
}

/* PRUDP v0 checksum without the access-key part (that part is kept from the
   original packet, so it works whatever the key is). */
static uint8_t netz_sum(const uint8_t *d, int n)
{
    uint32_t words = 0, sum = 0;
    int w = n / 4;
    for (int i = 0; i < w; i++) words += (uint32_t)d[4 * i] | (uint32_t)d[4 * i + 1] << 8 | (uint32_t)d[4 * i + 2] << 16 | (uint32_t)d[4 * i + 3] << 24;
    for (int i = w * 4; i < n; i++) sum += d[i];
    sum += (words & 255) + (words >> 8 & 255) + (words >> 16 & 255) + (words >> 24);
    return (uint8_t)sum;
}

static bool netz_user(const uint8_t *d, int n) { return n >= NETZ_HDR + 2 && n <= MAX_GAME_PKT && (d[2] & 7) == 5; }

/* Decrypt the payload of a Net-Z discovery packet into out (for swapping or the log). */
static int netz_open(const uint8_t *d, int n, uint8_t *out)
{
    if (!netz_user(d, n)) return -1;
    int len = n - NETZ_HDR - 1;
    memcpy(out, d + NETZ_HDR, (size_t)len);
    rc4_crypt(NETZ_LAN_KEY, 16, out, len);
    return len;
}

/* MD5 (RFC 1321): the discovery payload ends in the MD5 of the rest of it
   (Quazal's MD5Checksum), so it must be redone after a swap. */
static void md5(const uint8_t *msg, int len, uint8_t out[16])
{
    static const uint32_t K[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391 };
    static const uint8_t R[64] = { 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
        5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 4, 11, 16, 23, 4, 11, 16, 23,
        4, 11, 16, 23, 4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21 };
    uint32_t h0 = 0x67452301, h1 = 0xefcdab89, h2 = 0x98badcfe, h3 = 0x10325476;
    uint64_t bits = (uint64_t)len * 8;
    int total = ((len + 8) / 64 + 1) * 64;
    for (int off = 0; off < total; off += 64) {
        uint8_t blk[64];
        for (int i = 0; i < 64; i++) {
            int k = off + i;
            blk[i] = k < len ? msg[k] : k == len ? 0x80 : k >= total - 8 ? (uint8_t)(bits >> (8 * (k - (total - 8)))) : 0;
        }
        uint32_t M[16], a = h0, b = h1, c = h2, dd = h3;
        for (int i = 0; i < 16; i++) M[i] = (uint32_t)blk[4 * i] | (uint32_t)blk[4 * i + 1] << 8 | (uint32_t)blk[4 * i + 2] << 16 | (uint32_t)blk[4 * i + 3] << 24;
        for (int i = 0; i < 64; i++) {
            uint32_t f;
            int g;
            if (i < 16) { f = (b & c) | (~b & dd); g = i; }
            else if (i < 32) { f = (dd & b) | (~dd & c); g = (5 * i + 1) % 16; }
            else if (i < 48) { f = b ^ c ^ dd; g = (3 * i + 5) % 16; }
            else { f = c ^ (b | ~dd); g = (7 * i) % 16; }
            uint32_t t = dd;
            dd = c;
            c = b;
            uint32_t x = a + f + K[i] + M[g];
            b = b + (x << R[i] | x >> (32 - R[i]));
            a = t;
        }
        h0 += a; h1 += b; h2 += c; h3 += dd;
    }
    uint32_t h[4] = { h0, h1, h2, h3 };
    for (int i = 0; i < 16; i++) out[i] = (uint8_t)(h[i / 4] >> (8 * (i % 4)));
}

static int netz_swap(uint8_t *d, int n, uint32_t from, uint32_t to)
{
    static uint8_t plain[MAX_GAME_PKT];
    int len = netz_open(d, n, plain);
    if (len <= 0) return 0;
    uint8_t sum[16];
    bool has_md5 = false;
    if (len > 16) { md5(plain, len - 16, sum); has_md5 = !memcmp(sum, plain + len - 16, 16); }
    int hits = swap_ip(plain, has_md5 ? len - 16 : len, from, to);
    if (!hits) return 0; /* finding the exact address text proves it really was Net-Z */
    if (has_md5) md5(plain, len - 16, plain + len - 16);
    uint8_t key_part = (uint8_t)(d[n - 1] - netz_sum(d, n - 1));
    rc4_crypt(NETZ_LAN_KEY, 16, plain, len);
    memcpy(d + NETZ_HDR, plain, (size_t)len);
    d[n - 1] = (uint8_t)(key_part + netz_sum(d, n - 1));
    return hits;
}

/* Every PRUDP packet ends in a 1-byte checksum (sum of the bytes plus a key
   byte per stream; Quazal's code at 0x10dcdde0). After swapping an address in
   a plain game packet the checksum must be fixed, or the game drops the packet.
   The key byte isn't known in advance, so it is learned: when the packets one
   game port sends to one stream keep giving the same key byte, that port speaks
   PRUDP and its swapped packets get a new checksum with that key. */
#define NETZ_LEARN 64
static struct { uint16_t port; uint8_t dir, stream, key, seen; } g_nzk[NETZ_LEARN];
static int g_nnzk;

static bool netz_key_known(int dir, uint16_t port, const uint8_t *d, int n, uint8_t key)
{
    if (n < NETZ_HDR + 1 || n > MAX_GAME_PKT) return false;
    uint8_t stream = d[0];
    int i;
    for (i = 0; i < g_nnzk; i++)
        if (g_nzk[i].port == port && g_nzk[i].dir == dir && g_nzk[i].stream == stream) break;
    if (i == g_nnzk) {
        if (g_nnzk == NETZ_LEARN) i = (int)(port % NETZ_LEARN); /* full: reuse a slot */
        else g_nnzk++;
        g_nzk[i].port = port; g_nzk[i].dir = (uint8_t)dir; g_nzk[i].stream = stream;
        g_nzk[i].key = key; g_nzk[i].seen = 0;
    }
    bool known = g_nzk[i].seen >= 3 && g_nzk[i].key == key;
    if (g_nzk[i].key == key) { if (g_nzk[i].seen < 255) g_nzk[i].seen++; }
    else { g_nzk[i].key = key; g_nzk[i].seen = 1; }
    return known;
}

/* LZO1X decompression (the format of LZO 1.08's lzo1x_decompress, which the
   game links: it calls lzo_init(0x1080, ...) at 0x10d9d080). Quazal compresses
   game messages with it, so an address inside one isn't visible as text.
   Returns the decompressed length, or -1 for anything malformed. */
static int lzo1x_decompress(const uint8_t *in, int in_len, uint8_t *out, int out_max)
{
    const uint8_t *ip = in, *ip_end = in + in_len;
    uint8_t *op = out, *op_end = out + out_max;
    unsigned t;
    const uint8_t *m_pos;
#define NEED_IP(x) do { if ((size_t)(ip_end - ip) < (size_t)(x)) return -1; } while (0)
#define NEED_OP(x) do { if ((size_t)(op_end - op) < (size_t)(x)) return -1; } while (0)
#define LOOKBEHIND() do { if (m_pos < out || m_pos >= op) return -1; } while (0)
    NEED_IP(1);
    if (*ip > 17) {
        t = (unsigned)(*ip++ - 17);
        if (t < 4) goto match_next;
        NEED_OP(t); NEED_IP(t + 1);
        do *op++ = *ip++; while (--t > 0);
        goto first_literal_run;
    }
    for (;;) {
        NEED_IP(3);
        t = *ip++;
        if (t >= 16) goto match;
        if (t == 0) {
            while (*ip == 0) { t += 255; ip++; NEED_IP(1); if (t > (unsigned)in_len) return -1; }
            t += 15u + *ip++;
        }
        NEED_OP(t + 3); NEED_IP(t + 4);
        for (unsigned k = 0; k < t + 3; k++) *op++ = *ip++;
first_literal_run:
        t = *ip++;
        if (t >= 16) goto match;
        m_pos = op - (1 + 0x0800);
        m_pos -= t >> 2;
        m_pos -= *ip++ << 2;
        LOOKBEHIND(); NEED_OP(3);
        *op++ = *m_pos++; *op++ = *m_pos++; *op++ = *m_pos;
        goto match_done;
        for (;;) {
match:
            if (t >= 64) {
                m_pos = op - 1;
                m_pos -= (t >> 2) & 7;
                m_pos -= *ip++ << 3;
                t = (t >> 5) - 1;
                LOOKBEHIND(); NEED_OP(t + 2);
                goto copy_match;
            } else if (t >= 32) {
                t &= 31;
                if (t == 0) {
                    NEED_IP(1);
                    while (*ip == 0) { t += 255; ip++; NEED_IP(1); if (t > (unsigned)out_max) return -1; }
                    t += 31u + *ip++;
                }
                NEED_IP(2);
                m_pos = op - 1;
                m_pos -= (ip[0] >> 2) + (ip[1] << 6);
                ip += 2;
            } else if (t >= 16) {
                m_pos = op;
                m_pos -= (t & 8) << 11;
                t &= 7;
                if (t == 0) {
                    NEED_IP(1);
                    while (*ip == 0) { t += 255; ip++; NEED_IP(1); if (t > (unsigned)out_max) return -1; }
                    t += 7u + *ip++;
                }
                NEED_IP(2);
                m_pos -= (ip[0] >> 2) + (ip[1] << 6);
                ip += 2;
                if (m_pos == op) return (int)(op - out); /* end of stream */
                m_pos -= 0x4000;
            } else {
                m_pos = op - 1;
                m_pos -= t >> 2;
                NEED_IP(1);
                m_pos -= *ip++ << 2;
                LOOKBEHIND(); NEED_OP(2);
                *op++ = *m_pos++; *op++ = *m_pos;
                goto match_done;
            }
            LOOKBEHIND(); NEED_OP(t + 2);
copy_match:
            *op++ = *m_pos++; *op++ = *m_pos++;
            do *op++ = *m_pos++; while (--t > 0);
match_done:
            t = ip[-2] & 3u;
            if (t == 0) break;
match_next:
            NEED_OP(t); NEED_IP(t + 1);
            do *op++ = *ip++; while (--t > 0);
            t = *ip++;
        }
    }
#undef NEED_IP
#undef NEED_OP
#undef LOOKBEHIND
}

static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

/* Quazal messages inside a DATA payload: [u32 length][length bytes][checksum],
   one after another, where checksum = netz_sum(length + bytes) with key 0
   (the game checks it, at 0x10dcde90, and drops a message that doesn't match).
   True if buf is exactly such a sequence with valid checksums. */
static bool netz_msgs_ok(const uint8_t *b, int n)
{
    int o = 0, k = 0;
    while (o < n) {
        if (n - o < 5) return false;
        uint32_t L = le32(b + o);
        if (L > (uint32_t)(n - o - 5)) return false;
        int end = o + 4 + (int)L;
        if (netz_sum(b + o, end - o) != b[end]) return false;
        o = end + 1;
        k++;
    }
    return k > 0;
}

/* Swap inside each message's bytes and redo its checksum. */
static int netz_msgs_swap(uint8_t *b, int n, uint32_t from, uint32_t to)
{
    int o = 0, hits = 0;
    while (o < n) {
        uint32_t L = le32(b + o);
        int end = o + 4 + (int)L;
        int h = swap_ip(b + o + 4, (int)L, from, to);
        if (h) { b[end] = netz_sum(b + o, end - o); hits += h; }
        o = end + 1;
    }
    return hits;
}

/* A PRUDP v0 DATA packet: [10-byte header][fragment id][ratio][messages][checksum].
   ratio 0 = messages as they are; otherwise LZO-compressed (LZO 1.08). An
   address inside a message is swapped and the message checksum redone; a
   compressed payload is then sent uncompressed (ratio 0, which the game uses
   itself), so the packet can get longer.
   Returns hits, or -1 if this isn't a Net-Z DATA packet with messages. */
static int netz_data_swap(uint8_t *d, int *pn, int cap, uint32_t from, uint32_t to, uint8_t key)
{
    static uint8_t msg[MAX_GAME_PKT];
    int n = *pn;
    if (n < NETZ_HDR + 8 || (d[2] & 7) != 2 || (d[2] >> 3) & (1 | 8) || d[NETZ_HDR] != 0) return -1;
    uint8_t *pl = d + NETZ_HDR + 1;
    int plen = n - NETZ_HDR - 2;
    if (pl[0] == 0) {
        if (!netz_msgs_ok(pl + 1, plen - 1)) return -1;
        int hits = netz_msgs_swap(pl + 1, plen - 1, from, to);
        if (hits) d[n - 1] = (uint8_t)(key + netz_sum(d, n - 1));
        return hits;
    }
    int dl = lzo1x_decompress(pl + 1, plen - 1, msg, (int)sizeof msg);
    if (dl <= 0 || !netz_msgs_ok(msg, dl)) return -1;
    int hits = netz_msgs_swap(msg, dl, from, to);
    if (!hits) return 0;
    int nn = NETZ_HDR + 1 + 1 + dl + 1;
    if (nn > cap || nn > MAX_GAME_PKT) return 0; /* too big uncompressed: leave it */
    d[NETZ_HDR + 1] = 0;
    memcpy(d + NETZ_HDR + 2, msg, (size_t)dl);
    d[nn - 1] = (uint8_t)(key + netz_sum(d, nn - 1));
    *pn = nn;
    return hits;
}

/* Swap in the packet as it is, inside Net-Z's encrypted discovery packets and
   inside its compressed game messages. dir/port: where the packet comes from
   (0 = this PC's game, 1 = the peer's), for fixing PRUDP checksums; port 0 =
   not a UDP game packet (TCP). *pn may grow (up to cap). */
static int swap_ip_all(uint8_t *d, int *pn, int cap, uint32_t from, uint32_t to, int dir, uint16_t port)
{
    int n = *pn;
    if (netz_user(d, n)) {
        int hits = netz_swap(d, n, from, to);
        return hits + swap_ip(d, n, from, to);
    }
    bool prudp = false;
    uint8_t key = 0;
    int hits = 0;
    if (port && n > NETZ_HDR) {
        key = (uint8_t)(d[n - 1] - netz_sum(d, n - 1));
        prudp = netz_key_known(dir, port, d, n, key);
        /* valid message checksums inside prove it's the game's PRUDP, so the
           packet's own last byte gives the key even before it is learned */
        hits = netz_data_swap(d, pn, cap, from, to, key);
        if (hits >= 0) return hits; /* game messages: handled (or left alone) whole */
        hits = 0;
    }
    int h2 = swap_ip(d, n, from, to);
    if (h2 && prudp) d[n - 1] = (uint8_t)(key + netz_sum(d, n - 1));
    return hits + h2;
}

/* ---------- the game ---------- */

static GameNet G;
static u64 g_scan_at;
static bool g_was_running;

/* force 0: at most once a second; 1: at most every 100 ms; 2: now */
static void scan_game(int force)
{
    u64 t = now_ms();
    if (!force && g_scan_at && t - g_scan_at < 1000) return;
    if (force == 1 && g_scan_at && t - g_scan_at < 100) return;
    g_scan_at = t;
    game_scan(g_exe, &G);
    refresh_my_ips();
    /* Only call the game closed after it has been gone for 5 s, so a short
       glitch never makes the two sides flap between running / not running. */
    static u64 seen_at;
    if (G.running) seen_at = t;
    else if (g_was_running && t - seen_at < 5000) G.running = true;
    if (G.running != g_was_running) {
        g_was_running = G.running;
        say(G.running ? "Splinter Cell is running." : "Splinter Cell is not running (or was closed).");
    }
}

static const Bound *game_udp(uint16_t port)
{
    for (int i = 0; i < G.nudp; i++) if (G.udp[i].port == port) return &G.udp[i];
    return 0;
}

static bool game_owns_udp(uint16_t port, bool rescan)
{
    if (game_udp(port)) return true;
    if (!rescan) return false;
    scan_game(true);
    return game_udp(port) != 0;
}

static bool game_owns_tcp(uint16_t port)
{
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < G.ntcpc; i++) if (G.tcpc[i] == port) return true;
        for (int i = 0; i < G.ntcpl; i++) if (G.tcpl[i].port == port) return true;
        if (!pass) scan_game(2); /* a new connection: worth an immediate look */
    }
    return false;
}

static bool udp_busy(uint16_t port)
{
    for (int i = 0; i < G.nbusy; i++) if (G.busy_udp[i] == port) return true;
    return false;
}

/* Where to deliver into the local game for a port it has open. */
static uint32_t deliver_ip(uint32_t bound) { return bound == 0 ? ip4(127, 0, 0, 1) : bound; }

/* ---------- Steam helpers ---------- */

static void ident_of(uint8_t b[IDENT_SIZE], u64 id)
{
    memset(b, 0, IDENT_SIZE);
    S.Ident_SetSteamID64(b, id);
}

static void send_msg(u64 to, const void *p, int n, bool reliable, int ch)
{
    uint8_t id[IDENT_SIZE];
    ident_of(id, to);
    int flags = (reliable ? SEND_RELIABLE : SEND_UNRELIABLE) | SEND_NONAGLE;
    for (int tries = 0; tries < 200; tries++) {
        int r = S.NM_SendMessageToUser(NM, id, p, (uint32_t)n, flags, ch);
        if (r == 1 || !reliable) return;
        if (r != 25 /* k_EResultLimitExceeded: send buffer full */) return;
        S.MD_RunFrame(g_pipe);
        sleep_ms(5);
    }
}

static void send_ctl(u64 to, const void *p, int n) { send_msg(to, p, n, true, CH_CTL); }
static void send_ctl1(u64 to, char c) { char m[2] = { c, PROTO }; send_ctl(to, m, 2); }
static bool kicked(u64 id);

static void close_session(u64 with)
{
    uint8_t id[IDENT_SIZE];
    ident_of(id, with);
    S.NM_CloseSessionWithUser(NM, id);
}

static bool in_lobby(u64 id)
{
    if (!g_lobby) return false;
    int n = S.MM_GetNumLobbyMembers(MM, g_lobby);
    for (int i = 0; i < n; i++)
        if (S.MM_GetLobbyMemberByIndex(MM, g_lobby, i) == id) return true;
    return false;
}

typedef struct { u64 window; int count; } Rate;
static bool rate_ok(Rate *r, u64 t)
{
    if (t - r->window >= 1000) { r->window = t; r->count = 0; }
    return ++r->count <= MAX_PKTS_PER_SEC;
}

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static uint16_t get16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static void put32(uint8_t *p, uint32_t v) { put16(p, (uint16_t)(v >> 16)); put16(p + 2, (uint16_t)v); }
static uint32_t get32(const uint8_t *p) { return ((uint32_t)get16(p) << 16) | get16(p + 2); }
static u64 rd64(const uint8_t *p) { u64 v; memcpy(&v, p, 8); return v; }
static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }

/* ---------- peers ---------- */

typedef struct { uint16_t port; sock_t s; u64 pkts; } Alias;      /* UDP on the peer's loopback address */
typedef struct { uint16_t port; sock_t s; } Listener;              /* TCP on the peer's loopback address */
typedef struct {
    uint32_t sid;
    sock_t s;
    uint8_t *out; int nout;  /* data waiting for the local game to read */
} Stream;

typedef struct {
    u64 id;
    int slot;
    uint32_t alias;          /* loopback address that stands for this peer on this PC */
    uint32_t my_alias;       /* loopback address that stands for this PC on the peer's PC */
    bool welcomed, warned_silent, said_flow, ports_known;
    u64 last_heard, last_ka, last_ports_sent, first_up_ms;
    uint16_t rudp[MAX_GPORTS]; int nrudp; /* the peer's game's UDP ports */
    uint16_t rtcp[MAX_GPORTS]; int nrtcp; /* the peer's game's TCP listen ports */
    bool rrunning;
    Alias als[MAX_ALIAS]; int nals;
    Listener lis[MAX_GPORTS]; int nlis;
    Stream st[MAX_STREAMS]; int nst;
    u64 up, down, lost, reordered, swapped, dropped_noport, cap_in, cap_out;
    uint16_t tx_seq, rx_seq; bool rx_started;
    double rtt; bool have_rtt;
    u64 st_at, st_down, st_up, st_lost; /* live stats: counts at the last sample */
    int pps_in, pps_out; double loss_avg; bool have_loss;
    Rate rate;
    char *mf; int nmf;        /* the peer's game file list, as it arrives */
    bool mf_sent, mf_done, mf_found;
} Peer;

static Peer g_peers[MAX_PEERS];
static int g_npeers;
static u64 g_ignored[16];
static int g_nignored;
static uint32_t g_stream_ctr;
static int g_my_text_len = 12;

static Peer *find_peer(u64 id)
{
    for (int i = 0; i < g_npeers; i++)
        if (g_peers[i].id == id) return &g_peers[i];
    return 0;
}

static Peer *peer_of_alias(uint32_t ip)
{
    for (int i = 0; i < g_npeers; i++) if (g_peers[i].alias == ip) return &g_peers[i];
    return 0;
}

static int primary_text_len(void)
{
    refresh_my_ips();
    return g_nmy_ips ? ip_text_len(g_my_ips[0]) : 12;
}

static Peer *add_peer(u64 id)
{
    if (g_npeers >= MAX_PEERS) return 0;
    Peer *p = &g_peers[g_npeers];
    memset(p, 0, sizeof *p);
    p->id = id;
    bool used[MAX_PEERS] = { 0 };
    for (int i = 0; i < g_npeers; i++) used[g_peers[i].slot] = true;
    while (used[p->slot]) p->slot++;
    p->last_heard = now_ms();
    p->rate.window = now_ms();
    g_npeers++;
    return p;
}

static void stream_close(Peer *p, int i, bool tell)
{
    Stream *s = &p->st[i];
    if (tell) { uint8_t m[5] = { 'C' }; put32(m + 1, s->sid); send_msg(p->id, m, 5, true, CH_TCP); }
    sock_close(s->s);
    free(s->out);
    p->st[i] = p->st[--p->nst];
}

static void peer_sockets_close(Peer *p)
{
    free(p->mf);
    p->mf = 0;
    p->nmf = 0;
    for (int i = 0; i < p->nals; i++) sock_close(p->als[i].s);
    for (int i = 0; i < p->nlis; i++) sock_close(p->lis[i].s);
    while (p->nst) stream_close(p, 0, false);
    p->nals = p->nlis = 0;
}

static void ring_dump(const char *why);
static void mods_forget(u64 id);
static void mods_publish(void);
static void settings_publish(void);
static void mods_local_text(void);

static void drop_peer(u64 id, const char *why)
{
    for (int i = 0; i < g_npeers; i++) {
        if (g_peers[i].id != id) continue;
        say("- %s disconnected (%s).", who(id), why);
        ring_dump("the link to a friend ended");
        mods_forget(id);
        peer_sockets_close(&g_peers[i]);
        close_session(id);
        g_peers[i] = g_peers[--g_npeers];
        return;
    }
}

/* Friends the host kicked: blocked until the host stops hosting (from CoFLink 0.9). */
static u64 g_kicked[32], g_kick_told[32];
static int g_nkicked;

static int kicked_index(u64 id)
{
    for (int i = 0; i < g_nkicked; i++)
        if (g_kicked[i] == id) return i;
    return -1;
}
static bool kicked(u64 id) { return kicked_index(id) >= 0; }

/* Who may reach the host's game: only people in the lobby who are also on the
   host's own Steam friends list (and not kicked). Everyone else is dropped. */
static bool allowed_peer(u64 id)
{
    return id && id != g_me && !kicked(id) && in_lobby(id) && S.Friends_HasFriend(F, id, FRIEND_IMMEDIATE);
}

/* tell a kicked friend, at most every 2 s */
static void tell_kicked(u64 id)
{
    int k = kicked_index(id);
    if (k >= 0 && now_ms() - g_kick_told[k] > 2000) { send_ctl1(id, 'Z'); g_kick_told[k] = now_ms(); }
}

static void note_ignored(u64 id)
{
    if (kicked(id)) { tell_kicked(id); return; }
    /* Right after someone joins, Steam may not have filled in the friends list
       or the lobby yet: only report a block that still holds 3 s later. */
    static u64 first_id[16], first_at[16];
    int slot = -1;
    for (int i = 0; i < 16; i++) if (first_id[i] == id) slot = i;
    if (slot < 0) {
        for (int i = 0; i < 16 && slot < 0; i++) if (!first_id[i]) slot = i;
        if (slot < 0) slot = (int)(now_ms() % 16);
        first_id[slot] = id;
        first_at[slot] = now_ms();
        return;
    }
    if (now_ms() - first_at[slot] < 3000) return;
    for (int i = 0; i < g_nignored; i++)
        if (g_ignored[i] == id) return;
    if (g_nignored < 16) g_ignored[g_nignored++] = id;
    if (in_lobby(id))
        say("! Blocked %s: they are in the lobby but not on your Steam friends list.", who(id));
    else
        say("! Blocked traffic from %s: they are not in your lobby.", who(id));
}

/* Host: "Kick" button. The friend is cut off and can't link again until the
   host clicks Stop (from CoFLink 0.9). */
static void kick_peer(u64 id)
{
    if (!g_is_host || !id) return;
    bool linked = find_peer(id) != 0;
    if (!kicked(id) && g_nkicked < 32) { g_kick_told[g_nkicked] = 0; g_kicked[g_nkicked++] = id; }
    tell_kicked(id);
    if (linked) drop_peer(id, "kicked by you");
    say("%s is kicked. They can't link to this game again until you click Stop.", who(id));
}

/* "1.2.3" -> comparable number */
static uint32_t ver_num(const char *v)
{
    uint32_t r = 0;
    for (int part = 0; part < 3; part++) {
        uint32_t x = 0;
        while (*v >= '0' && *v <= '9') { if (x < 999) x = x * 10 + (uint32_t)(*v - '0'); v++; }
        r = r * 1000 + x;
        if (*v == '.') v++;
    }
    return r;
}

static void version_notice(u64 who_id, const char *theirs)
{
    if (!theirs[0] || !strcmp(theirs, VERSION)) return;
    if (ver_num(theirs) > ver_num(VERSION))
        say("! %s has a newer SCCTLink (%s, you have " VERSION "). Update yours: github.com/Mhdshehri/scctlink", who(who_id), clean(theirs));
    else
        say("! %s has an older SCCTLink (%s, you have " VERSION "). Ask them to update.", who(who_id), clean(theirs));
}

/* ---------- UDP: alias sockets and broadcast catchers ---------- */

static bool g_said_admin;

static Alias *alias_get(Peer *p, uint16_t port, bool create)
{
    for (int i = 0; i < p->nals; i++) if (p->als[i].port == port) return &p->als[i];
    if (!create || p->nals >= MAX_ALIAS || !p->alias) return 0;
    /* Open the other game's port on its private address early: Windows still
       lets our own game open the same port number on all addresses later. */
    sock_t s = udp_open(p->alias, port, UDP_REUSE);
    uint16_t local = port;
    if (s == BADSOCK) {
        /* Usually our own game holds this port number. Use another port on the
           same private address: the game answers to wherever a packet came from. */
        int err = sock_last_error();
        s = udp_open(p->alias, 0, 0);
        if (s == BADSOCK) { note("could not open any UDP port on %s (error %d)", ip_kind(p->alias), err); return 0; }
        local = sock_local_port(s);
        static uint16_t warned[64]; static int nw;
        bool seen = false;
        for (int i = 0; i < nw; i++) seen |= warned[i] == port;
        if (!seen && nw < 64) {
            warned[nw++] = port;
            say("(UDP port %u is busy on this PC - error %d. %s's port %u goes through port %u here instead.)",
                (unsigned)port, err, who(p->id), (unsigned)port, (unsigned)local);
            if (sock_err_denied(err) && !g_said_admin) {
                g_said_admin = true;
                say("! Windows refused a port that Splinter Cell uses. If the game runs as administrator,");
                say("  close SCCTLink and start it as administrator too (right-click SCCTLink.exe).");
            }
        }
    }
    Alias *a = &p->als[p->nals++];
    a->port = port; a->s = s; a->pkts = 0;
    note("UDP %s:%u stands for %s's game port %u", ip_kind(p->alias), (unsigned)local, who(p->id), (unsigned)port);
    return a;
}

typedef struct { uint16_t port; sock_t s[8]; int ns; u64 pkts; bool given_up; } Catcher;
static Catcher g_catch[MAX_CATCH];
static int g_ncatch;

static Catcher *catcher_find(uint16_t port)
{
    for (int i = 0; i < g_ncatch; i++) if (g_catch[i].port == port) return &g_catch[i];
    return 0;
}

/* Catch this PC's LAN broadcasts to `port` (the game's "find LAN games"
   traffic), without taking the port away from anything that already uses it. */
static void catcher_ensure(uint16_t port)
{
    Catcher *c = catcher_find(port);
    if (c && (c->ns || c->given_up)) return;
    if (!c) {
        if (g_ncatch >= MAX_CATCH) return;
        c = &g_catch[g_ncatch++];
        memset(c, 0, sizeof *c);
        c->port = port;
    }
    /* Never take the port on all addresses (0.0.0.0): both PCs run the same
       game, so this is a port our own game opens too, and it could then fail
       to create its game (seen in testing). Listen on this PC's own network
       addresses only - Windows lets the game have 0.0.0.0 next to them. */
    uint32_t at[8];
    int nat = broadcast_listen_addrs(at, 8);
    for (int i = 0; i < nat && c->ns < 8; i++) {
        sock_t s = udp_open(at[i], port, UDP_BROADCAST | UDP_REUSE);
        if (s != BADSOCK) c->s[c->ns++] = s;
    }
    if (c->ns) { note("listening for LAN broadcasts on port %u (on this PC's network addresses)", (unsigned)port); return; }
    const Bound *b = game_udp(port);
    if (b || udp_busy(port) || nat) {
        c->given_up = true;
        note("not catching broadcasts on port %u: it is in use", (unsigned)port);
    }
}

static void catchers_update(void)
{
    if (!G.running) return;
    for (int i = 0; i < g_npeers; i++) {
        Peer *p = &g_peers[i];
        if (!p->welcomed) continue;
        for (int k = 0; k < p->nrudp; k++) catcher_ensure(p->rudp[k]);
    }
}

/* Payload from the local game, going to a peer: hide this PC's LAN addresses.
   Returns the new length (a compressed game message can come out longer). */
static int hide_my_ips(Peer *p, uint8_t *d, int n, int cap, uint16_t port)
{
    int hits = 0;
    if (port && n > NETZ_HDR && !netz_user(d, n) && !g_nmy_ips) /* still learn the checksum key */
        netz_key_known(0, port, d, n, (uint8_t)(d[n - 1] - netz_sum(d, n - 1)));
    for (int i = 0; i < g_nmy_ips; i++) hits += swap_ip_all(d, &n, cap, g_my_ips[i], p->my_alias, 0, port);
    if (hits && !p->swapped) say("(The game put your LAN address in a packet. SCCTLink replaced it, so %s never sees it.)", who(p->id));
    p->swapped += (u64)hits;
    return n;
}

/* Payload from a peer: their idea of this PC's address becomes the real one. */
static int unhide_my_ip(Peer *p, uint8_t *d, int n, int cap, uint16_t port)
{
    if (g_nmy_ips && p->my_alias) swap_ip_all(d, &n, cap, p->my_alias, g_my_ips[0], 1, port);
    return n;
}

static bool memmem_has(const uint8_t *d, int n, const char *needle)
{
    int k = (int)strlen(needle);
    for (int i = 0; i + k <= n; i++) if (!memcmp(d + i, needle, (size_t)k)) return true;
    return false;
}

/* The last packets each way, kept in memory and written to scctlink.log when a
   session ends (a friend disconnects, the game closes its ports, Stop, Details),
   so the log shows what happened right before a disconnect. */
#define RING 160
static struct { u64 t; uint8_t out; uint16_t src, dst; int n; uint8_t d[48]; } g_ring[RING];
static int g_ring_at, g_ring_n;
static u64 g_t0;

static void ring_add(bool out, uint16_t src, uint16_t dst, const uint8_t *d, int n)
{
    int i = g_ring_at;
    g_ring[i].t = now_ms();
    g_ring[i].out = out;
    g_ring[i].src = src;
    g_ring[i].dst = dst;
    g_ring[i].n = n;
    memcpy(g_ring[i].d, d, (size_t)(n < 48 ? n : 48));
    g_ring_at = (i + 1) % RING;
    if (g_ring_n < RING) g_ring_n++;
}

static void ring_dump(const char *why)
{
    if (!g_ring_n) return;
    char line[300];
    sfmt(line, sizeof line, "---- the last %d game packets before: %s ----", g_ring_n, why);
    log_line(line);
    static const char hx[] = "0123456789abcdef";
    for (int j = 0; j < g_ring_n; j++) {
        int i = (g_ring_at - g_ring_n + j + RING) % RING;
        unsigned ms = (unsigned)(g_ring[i].t - g_t0); /* 32-bit: no 64-bit division helper in the Windows build */
        sfmt(line, sizeof line, "  [%u.%03us] %s %u->%u len %d:", ms / 1000, ms % 1000,
             g_ring[i].out ? "out" : "in ", (unsigned)g_ring[i].src, (unsigned)g_ring[i].dst, g_ring[i].n);
        int k = (int)strlen(line), m = g_ring[i].n < 48 ? g_ring[i].n : 48;
        for (int b = 0; b < m && k < (int)sizeof line - 8; b++) {
            if (b % 4 == 0) line[k++] = ' ';
            line[k++] = hx[g_ring[i].d[b] >> 4];
            line[k++] = hx[g_ring[i].d[b] & 15];
        }
        line[k] = 0;
        log_line(line);
    }
    log_line("---- end ----");
    g_ring_n = 0;
}

/* Packet log for finding out what the game sends (only in scctlink.log):
   the first packets each way, the first ones on every new port pair, and
   every PRUDP disconnect, after this PC's addresses were hidden. */
#define CAPTURE_MAX 80
static void capture(Peer *p, bool out, uint16_t src, uint16_t dst, const uint8_t *d, int n, const char *what)
{
    ring_add(out, src, dst, d, n);
    static struct { uint16_t a, b; uint8_t out, cnt; } pairs[64];
    static int npairs;
    int pi;
    for (pi = 0; pi < npairs; pi++) if (pairs[pi].a == src && pairs[pi].b == dst && pairs[pi].out == out) break;
    if (pi == npairs && npairs < 64) { pairs[pi].a = src; pairs[pi].b = dst; pairs[pi].out = out; pairs[pi].cnt = 0; npairs++; }
    bool new_pair = pi < 64 && pairs[pi].cnt < 20;
    if (pi < 64 && pairs[pi].cnt < 255) pairs[pi].cnt++;
    bool disc = n > NETZ_HDR && !netz_user(d, n) && (d[2] & 7) == 3;
    u64 *cnt = out ? &p->cap_out : &p->cap_in;
    if (*cnt >= CAPTURE_MAX && !what && !new_pair && !disc) return;
    (*cnt)++;
    char line[600];
    unsigned ms = (unsigned)(now_ms() - g_t0);
    sfmt(line, sizeof line, "  [%u.%03us] %s %s %u->%u len %d%s%s:", ms / 1000, ms % 1000,
         out ? "out" : "in ", out ? "to" : "from",
         (unsigned)src, (unsigned)dst, n, disc ? " (PRUDP DISCONNECT)" : what ? " " : "", what ? what : "");
    int k = (int)strlen(line);
    static const char hx[] = "0123456789abcdef";
    for (int i = 0; i < n && i < 160 && k < (int)sizeof line - 8; i++) {
        if (i % 4 == 0) line[k++] = ' ';
        line[k++] = hx[d[i] >> 4];
        line[k++] = hx[d[i] & 15];
    }
    if (n > 160) { memcpy(line + k, " ...", 4); k += 4; }
    line[k] = 0;
    log_line(line);
    /* Net-Z discovery: also log what's inside (any address that isn't loopback is masked) */
    static uint8_t plain[MAX_GAME_PKT];
    int len = netz_open(d, n, plain);
    if (len <= 0 && n > NETZ_HDR + 3 && (d[2] & 7) == 2 && d[NETZ_HDR] == 0 && d[NETZ_HDR + 1] != 0) {
        /* a compressed game message: log what's inside */
        len = lzo1x_decompress(d + NETZ_HDR + 2, n - NETZ_HDR - 3, plain, (int)sizeof plain);
        if (len <= 0 || !netz_msgs_ok(plain, len)) len = -1;
    }
    if (len > 0 && (*cnt <= 12 || what || memmem_has(plain, len, "address="))) {
        char t[260];
        int m = 0;
        for (int i = 0; i < len && m < (int)sizeof t - 1; i++) t[m++] = plain[i] >= 32 && plain[i] < 127 ? (char)plain[i] : '.';
        t[m] = 0;
        for (char *a = strstr(t, "address="); a; a = strstr(a + 1, "address=")) {
            char *v = a + 8;
            if (strncmp(v, "127.", 4)) for (; *v && *v != ';'; v++) if (*v >= '0' && *v <= '9') *v = '#';
        }
        sfmt(line, sizeof line, "    Net-Z: %s", t);
        log_line(line);
    }
}

static void send_udp_frame(Peer *p, char kind, uint16_t src, uint16_t dst, const uint8_t *d, int n)
{
    if (n <= 0 || n > MAX_GAME_PKT) return;
    g_out[0] = (uint8_t)kind;
    put16(g_out + 1, src);
    put16(g_out + 3, dst);
    put16(g_out + 5, p->tx_seq++);
    memcpy(g_out + HDR, d, (size_t)n);
    n = hide_my_ips(p, g_out + HDR, n, (int)sizeof g_out - HDR, src);
    capture(p, true, src, dst, g_out + HDR, n, kind == 'B' ? "(LAN broadcast)" : 0);
    send_msg(p->id, g_out, n + HDR, false, CH_GAME);
    if (!p->up++) p->first_up_ms = now_ms();
}

/* A UDP frame from a peer: send it out of the same port, on the peer's
   loopback address, into the local game. */
static void deliver_udp(Peer *p, const uint8_t *m, int n)
{
    if (n <= HDR || n - HDR > MAX_GAME_PKT) return;
    uint16_t src = get16(m + 1), dst = get16(m + 3), seq = get16(m + 5);
    if (p->rx_started) {
        uint16_t gap = (uint16_t)(seq - p->rx_seq);
        if (gap == 0 || gap > 32768) p->reordered++;
        else { p->lost += (u64)(gap - 1); p->rx_seq = seq; }
    } else { p->rx_started = true; p->rx_seq = seq; }
    if (!src || !dst) return;
    const Bound *b = game_udp(dst);
    if (!b) { scan_game(true); b = game_udp(dst); }
    if (!b) {
        if (!p->dropped_noport++) note("nothing in the game listens on UDP %u yet (from %s)", (unsigned)dst, who(p->id));
        if (p->dropped_noport <= 20) capture(p, false, src, dst, m + HDR, n - HDR, "(DROPPED: your game has no such port open)");
        return;
    }
    capture(p, false, src, dst, m + HDR, n - HDR, 0);
    Alias *a = alias_get(p, src, true);
    if (!a) return;
    int len = n - HDR;
    memcpy(g_buf, m + HDR, (size_t)len);
    len = unhide_my_ip(p, g_buf, len, (int)sizeof g_buf, src);
    udp_sendto(a->s, g_buf, len, deliver_ip(b->ip), dst);
    a->pkts++;
    p->down++;
    if (!p->said_flow) {
        p->said_flow = true;
        say("Game traffic is flowing between you and %s.", who(p->id));
    }
}

static bool from_local_game(uint32_t ip, uint16_t port)
{
    if (!(is_loop(ip) || is_my_ip(ip))) return false; /* never relay another PC's packets */
    if (peer_of_alias(ip)) return false;                /* our own injected packets */
    return game_owns_udp(port, true);
}

static void poll_aliases(Peer *p)
{
    for (int i = 0; i < p->nals; i++) {
        Alias *a = &p->als[i];
        for (int k = 0; k < 512; k++) {
            uint32_t ip; uint16_t port;
            int r = udp_recvfrom(a->s, g_buf, BUF_SIZE, &ip, &port);
            if (r < 0) break;
            if (r == 0 || r > MAX_GAME_PKT) continue;
            if (!from_local_game(ip, port)) { note("dropped a packet to %u from a program that is not the game", (unsigned)a->port); continue; }
            send_udp_frame(p, 'U', port, a->port, g_buf, r);
        }
    }
}

static void poll_catchers(void)
{
    for (int c = 0; c < g_ncatch; c++) {
        Catcher *ct = &g_catch[c];
        for (int j = 0; j < ct->ns; j++) {
            for (int k = 0; k < 256; k++) {
                uint32_t ip; uint16_t port;
                int r = udp_recvfrom(ct->s[j], g_buf, BUF_SIZE, &ip, &port);
                if (r < 0) break;
                if (r == 0 || r > MAX_GAME_PKT) continue;
                if (!from_local_game(ip, port)) continue;
                if (!ct->pkts++) note("caught the game's LAN broadcast to port %u", (unsigned)ct->port);
                for (int i = 0; i < g_npeers; i++) {
                    Peer *p = &g_peers[i];
                    if (!p->welcomed) continue;
                    send_udp_frame(p, 'B', port, ct->port, g_buf, r); /* copies, so g_buf stays intact */
                }
            }
        }
    }
}

/* ---------- TCP streams ---------- */

static Stream *stream_find(Peer *p, uint32_t sid, int *idx)
{
    for (int i = 0; i < p->nst; i++) if (p->st[i].sid == sid) { if (idx) *idx = i; return &p->st[i]; }
    return 0;
}

static Stream *stream_add(Peer *p, uint32_t sid, sock_t s)
{
    if (p->nst >= MAX_STREAMS) { sock_close(s); return 0; }
    Stream *x = &p->st[p->nst++];
    memset(x, 0, sizeof *x);
    x->sid = sid;
    x->s = s;
    return x;
}

static void listeners_update(Peer *p)
{
    for (int k = 0; k < p->nrtcp; k++) {
        uint16_t port = p->rtcp[k];
        bool have = false;
        for (int i = 0; i < p->nlis; i++) have |= p->lis[i].port == port;
        if (have || p->nlis >= MAX_GPORTS) continue;
        sock_t s = tcp_listen(p->alias, port);
        if (s == BADSOCK) { note("could not open TCP %u on %s", (unsigned)port, ip_kind(p->alias)); continue; }
        p->lis[p->nlis].port = port;
        p->lis[p->nlis++].s = s;
        note("TCP %s:%u stands for %s's game port %u", ip_kind(p->alias), (unsigned)port, who(p->id), (unsigned)port);
    }
}

static void poll_tcp(Peer *p)
{
    for (int i = 0; i < p->nlis; i++) {
        uint32_t ip; uint16_t port;
        sock_t s;
        while ((s = tcp_accept(p->lis[i].s, &ip, &port)) != BADSOCK) {
            if (!(is_loop(ip) || is_my_ip(ip)) || !game_owns_tcp(port)) {
                note("refused a TCP connection to %u from a program that is not the game", (unsigned)p->lis[i].port);
                sock_close(s);
                continue;
            }
            uint32_t sid = (g_is_host ? 0x80000000u : 0) | (++g_stream_ctr & 0x7fffffffu);
            if (!stream_add(p, sid, s)) continue;
            uint8_t m[9] = { 'O' };
            put32(m + 1, sid);
            put16(m + 5, p->lis[i].port);
            put16(m + 7, port);
            send_msg(p->id, m, 9, true, CH_TCP);
            note("TCP stream %u opened to %s's port %u", (unsigned)sid, who(p->id), (unsigned)p->lis[i].port);
        }
    }
    for (int i = 0; i < p->nst; i++) {
        Stream *x = &p->st[i];
        /* flush what is waiting for the game */
        while (x->nout) {
            int w = tcp_send(x->s, x->out, x->nout);
            if (w < 0) { stream_close(p, i--, true); goto next; }
            if (w == 0) break;
            memmove(x->out, x->out + w, (size_t)(x->nout - w));
            x->nout -= w;
        }
        for (int k = 0; k < 64; k++) {
            int r = tcp_recv(x->s, g_out + 5, TCP_CHUNK);
            if (r == 0) break;
            if (r < 0) { stream_close(p, i--, true); goto next; }
            g_out[0] = 'D';
            put32(g_out + 1, x->sid);
            r = hide_my_ips(p, g_out + 5, r, r, 0);
            send_msg(p->id, g_out, r + 5, true, CH_TCP);
            p->up++;
        }
    next:;
    }
}

static void on_tcp_msg(Peer *p, const uint8_t *m, int n)
{
    if (n < 5) return;
    uint32_t sid = get32(m + 1);
    int idx;
    Stream *x = stream_find(p, sid, &idx);
    if (m[0] == 'O' && n >= 9 && !x) {
        uint16_t dst = get16(m + 5), src = get16(m + 7);
        uint32_t to = 0;
        bool found = false;
        for (int i = 0; i < G.ntcpl && !found; i++)
            if (G.tcpl[i].port == dst) { to = deliver_ip(G.tcpl[i].ip); found = true; }
        sock_t s = found ? tcp_connect(p->alias, src, to, dst) : BADSOCK;
        if (s == BADSOCK) {
            note("TCP stream to the game's port %u failed", (unsigned)dst);
            uint8_t c[5] = { 'C' }; put32(c + 1, sid); send_msg(p->id, c, 5, true, CH_TCP);
            return;
        }
        stream_add(p, sid, s);
        note("TCP stream %u from %s into the game's port %u", (unsigned)sid, who(p->id), (unsigned)dst);
    } else if (m[0] == 'D' && x) {
        int len = n - 5;
        memcpy(g_buf, m + 5, (size_t)len);
        len = unhide_my_ip(p, g_buf, len, len, 0);
        p->down++;
        int w = x->nout ? 0 : tcp_send(x->s, g_buf, len);
        if (w < 0) { stream_close(p, idx, true); return; }
        if (w < len) {
            if (x->nout + len - w > TCP_OUTBUF_MAX) { stream_close(p, idx, true); return; }
            uint8_t *nb = realloc(x->out, (size_t)(x->nout + len - w));
            if (!nb) { stream_close(p, idx, true); return; }
            x->out = nb;
            memcpy(x->out + x->nout, g_buf + w, (size_t)(len - w));
            x->nout += len - w;
        }
    } else if (m[0] == 'C' && x) {
        /* deliver what's left, then close */
        if (x->nout) tcp_send(x->s, x->out, x->nout);
        stream_close(p, idx, false);
    }
}

/* ---------- port lists ---------- */

static void send_ports(Peer *p)
{
    uint8_t m[4 + 4 * MAX_GPORTS];
    int k = 0;
    m[k++] = 'P';
    m[k++] = G.running ? 1 : 0;
    uint16_t u[MAX_GPORTS]; int nu = 0;
    for (int i = 0; i < G.nudp; i++) {
        bool dup = false;
        for (int j = 0; j < nu; j++) dup |= u[j] == G.udp[i].port;
        if (!dup) u[nu++] = G.udp[i].port;
    }
    m[k++] = (uint8_t)nu;
    m[k++] = (uint8_t)G.ntcpl;
    for (int i = 0; i < nu; i++) { put16(m + k, u[i]); k += 2; }
    for (int i = 0; i < G.ntcpl; i++) { put16(m + k, G.tcpl[i].port); k += 2; }
    send_ctl(p->id, m, k);
    p->last_ports_sent = now_ms();
}

static u64 g_ports_sig;

static u64 ports_signature(void)
{
    u64 h = G.running ? 1469598103934665603ULL : 7;
    for (int i = 0; i < G.nudp; i++) h = (h ^ G.udp[i].port) * 1099511628211ULL;
    for (int i = 0; i < G.ntcpl; i++) h = (h ^ (G.tcpl[i].port + 70000u)) * 1099511628211ULL;
    return h;
}

static void on_ports(Peer *p, const uint8_t *m, int n)
{
    if (n < 4) return;
    int nu = m[2], nt = m[3];
    if (nu > MAX_GPORTS || nt > MAX_GPORTS || 4 + 2 * (nu + nt) > n) return;
    bool was = p->rrunning;
    p->rrunning = m[1] & 1;
    p->nrudp = nu;
    p->nrtcp = nt;
    for (int i = 0; i < nu; i++) p->rudp[i] = get16(m + 4 + 2 * i);
    for (int i = 0; i < nt; i++) p->rtcp[i] = get16(m + 4 + 2 * nu + 2 * i);
    if (!p->ports_known || p->rrunning != was) {
        if (p->rrunning) {
            say("%s's Splinter Cell is running (%d UDP / %d TCP ports).", who(p->id), nu, nt);
            if (!g_is_host) ui_alert("host's game is up");
        }
        else
            say("%s has not started Splinter Cell yet.", who(p->id));
    }
    p->ports_known = true;
    for (int i = 0; i < nu; i++) alias_get(p, p->rudp[i], true);
    listeners_update(p);
    catchers_update();
}

/* ---------- join side state ---------- */

static u64 g_host_last, g_last_hello;
static bool g_host_warned;
static u64 g_rejected[16];
static int g_nrejected;

static Peer *host_peer(void) { return !g_is_host && g_npeers ? &g_peers[0] : 0; }

static void reset_join_state(void)
{
    if (!g_is_host && g_npeers) { peer_sockets_close(&g_peers[0]); g_npeers = 0; }
    g_lobby = g_host = 0;
    g_host_warned = false;
}

static void lobby_gone(void)
{
    S.MM_LeaveLobby(MM, g_lobby);
    reset_join_state();
    S.Friends_ClearRichPresence(F);
}

static void request_join(u64 lobby)
{
    if (g_is_host || !lobby || lobby == g_lobby || lobby == g_joining) return;
    if (g_lobby) {
        send_ctl1(g_host, 'B');
        S.MM_LeaveLobby(MM, g_lobby);
        reset_join_state();
    }
    g_joining = lobby;
    say("Joining lobby %s ...", codestr(lobby));
    S.MM_JoinLobby(MM, lobby);
}

static void send_hello(void)
{
    uint8_t m[3 + 16] = { 'H', PROTO, (uint8_t)g_my_text_len };
    int vl = (int)strlen(VERSION);
    memcpy(m + 3, VERSION, (size_t)vl);   /* older hosts ignore the extra bytes */
    send_ctl(g_host, m, 3 + vl);
    g_last_hello = now_ms();
}

static void linked_message(void)
{
    say("");
    say("=== Linked to %s. Now join from the game: ===", who(g_host));
    say("1. Click \"Start %s\" (or start it yourself).", g_versus ? "Versus" : "Splinter Cell");
    say("2. %s. The host's game is in the LAN list.", JOIN_MENU);
    say("If Windows Firewall asks about Splinter Cell or SCCTLink, you can press Cancel:");
    say("SCCTLink only uses this PC's own loopback addresses, nothing is opened to the internet.");
    say("");
}

static void on_lobby_entered(u64 lobby, uint32_t response)
{
    if (g_is_host || lobby == g_lobby) return; /* LobbyEnter can arrive twice */
    g_joining = 0;
    if (response != 1) {
        const char *why = response == 2 ? "it doesn't exist any more" :
                          response == 3 ? "you are not allowed to join" :
                          response == 4 ? "it is full" : "Steam refused";
        bool known = false; /* LobbyEnter can arrive twice: say it once */
        for (int i = 0; i < g_nrejected; i++) known |= g_rejected[i] == lobby;
        if (!known) {
            say("! Could not join that lobby: %s.", why);
            /* a closed lobby can still be named for a while: don't try it again */
            if (response == 2 && g_nrejected < 16) g_rejected[g_nrejected++] = lobby;
        }
        return;
    }
    const char *tag = S.MM_GetLobbyData(MM, lobby, "scctlink");
    if (!tag || strcmp(tag, "1") != 0) {
        say("! That lobby is not a Splinter Cell game (SCCTLink). Leaving it.");
        S.MM_LeaveLobby(MM, lobby);
        if (g_nrejected < 16) g_rejected[g_nrejected++] = lobby;
        return;
    }
    const char *proto = S.MM_GetLobbyData(MM, lobby, "scctlink_proto");
    if (!proto || proto[0] != PROTO || proto[1]) {
        const char *ver = S.MM_GetLobbyData(MM, lobby, "scctlink_ver");
        say("! %s uses a different SCCTLink version (%s). You both need SCCTLink " VERSION ".",
            who(S.MM_GetLobbyOwner(MM, lobby)), ver && *ver ? clean(ver) : "older");
        S.MM_LeaveLobby(MM, lobby);
        if (g_nrejected < 16) g_rejected[g_nrejected++] = lobby;
        return;
    }
    const char *mode = S.MM_GetLobbyData(MM, lobby, "scctlink_mode");
    bool their_versus = mode && !strcmp(mode, "versus");
    if (their_versus != g_versus) {
        say("! %s is hosting %s, but you picked %s. Click Stop, switch to %s, then join again.",
            who(S.MM_GetLobbyOwner(MM, lobby)), their_versus ? "Versus" : "Co-op", MODE_NAME,
            their_versus ? "Versus" : "Co-op");
        S.MM_LeaveLobby(MM, lobby);
        if (g_nrejected < 16) g_rejected[g_nrejected++] = lobby;
        return;
    }
    g_lobby = lobby;
    g_host = S.MM_GetLobbyOwner(MM, lobby);
    g_host_last = now_ms();
    Peer *p = add_peer(g_host);
    if (!p) return;
    say("Joined %s's Splinter Cell lobby. Connecting to them through Steam...", who(g_host));
    S.Friends_SetRichPresence(F, "status", g_versus ? "Playing Splinter Cell Versus (SCCTLink)"
                                                    : "Playing Splinter Cell co-op (SCCTLink)");
    send_hello();
}

/* ---------- host: lobby ---------- */

static void on_lobby_created(int32_t result, u64 lobby)
{
    if (!g_is_host || g_lobby) return; /* arrives twice on real Steam */
    if (result != 1) {
        say("! Steam could not create a lobby (error %d). Check your internet and try again.", result);
        g_mode = MODE_IDLE;
        g_is_host = false;
        return;
    }
    g_lobby = lobby;
    S.MM_SetLobbyData(MM, lobby, "scctlink", "1");
    S.MM_SetLobbyData(MM, lobby, "scctlink_ver", VERSION);
    S.MM_SetLobbyData(MM, lobby, "scctlink_mode", g_versus ? "versus" : "coop");
    { char pr[2] = { PROTO, 0 }; S.MM_SetLobbyData(MM, lobby, "scctlink_proto", pr); }
    S.MM_SetLobbyJoinable(MM, lobby, true);
    S.Friends_SetRichPresence(F, "status", g_versus ? "Hosting Splinter Cell Versus (SCCTLink)"
                                                    : "Hosting Splinter Cell co-op (SCCTLink)");
    /* Not "connect": with that key Steam shows friends a "Join Game" button,
       which only starts Spacewar (from CoFLink 0.9.4). */
    S.Friends_SetRichPresence(F, "scctlink_lobby", idstr(lobby));
    clipboard_set(codestr(lobby));
    say("");
    say("=== Lobby is ready (code %s, copied to clipboard) ===", codestr(lobby));
    say("1. Click \"Start %s\", then %s.", g_versus ? "Versus" : "Splinter Cell", HOST_MENU);
    if (g_versus)
        say("   Up to %d players. Keep \"listed\" off so the game isn't on a public list.", g_max_players);
    say("2. Your friend opens SCCTLink, picks %s and clicks \"Join a friend\" (if you don't", MODE_NAME);
    say("   show up, send them the lobby code above to paste into their SCCTLink).");
    say("If Windows Firewall asks about Splinter Cell or SCCTLink, you can press Cancel.");
    say("");
}

/* Host: the friends linked through SCCTLink right now (shown in the window). */
static void publish_connected(void)
{
    static UiPerson list[MAX_PEERS];
    int n = 0;
    for (int i = 0; i < g_npeers; i++) {
        list[n].id = g_peers[i].id;
        list[n].lobby = 0;
        sfmt(list[n].name, UI_NAME, "%s%s", who(g_peers[i].id),
             g_peers[i].down && g_peers[i].up ? "  (in game)" : "  (linked)");
        n++;
    }
    ui_list(LIST_FRIENDS_ONLINE, n, list);
}

static void status(bool details)
{
    if (g_mode == MODE_IDLE) { say("Status: stopped (%s).", MODE_NAME); return; }
    if (!g_npeers) {
        say("Status: %s. Splinter Cell %s.", g_is_host ? "lobby open, nobody connected yet" : "looking for the host",
            G.running ? "is running" : "is not running");
    }
    for (int i = 0; i < g_npeers; i++) {
        Peer *p = &g_peers[i];
        double loss = p->down + p->lost ? 100.0 * (double)p->lost / (double)(p->down + p->lost) : 0;
        say("Status: %s - %s. Ping %d ms, packets sent %s, received %s, lost %.1f%%, out of order %s.",
            who(p->id),
            p->down && p->up ? "playing" : p->welcomed ? "connected, waiting for the games to talk" : "connecting",
            p->have_rtt ? (int)(p->rtt + 0.5) : -1, idstr(p->up), idstr(p->down), loss, idstr(p->reordered));
    }
    if (!details) return;
    say("Your game: %s", G.running ? "running" : "not running");
    for (int i = 0; i < G.nudp; i++) say("  UDP %s:%u", ip_kind(G.udp[i].ip), (unsigned)G.udp[i].port);
    for (int i = 0; i < G.ntcpl; i++) say("  TCP listen %s:%u", ip_kind(G.tcpl[i].ip), (unsigned)G.tcpl[i].port);
    for (int i = 0; i < g_npeers; i++) {
        Peer *p = &g_peers[i];
        say("%s's game (at %s on this PC):", who(p->id), ip_kind(p->alias));
        for (int k = 0; k < p->nals; k++)
            say("  UDP %u -> here %u  (%s packets in)", (unsigned)p->als[k].port, (unsigned)sock_local_port(p->als[k].s),
                idstr(p->als[k].pkts));
        for (int k = 0; k < p->nlis; k++) say("  TCP %u", (unsigned)p->lis[k].port);
        say("  open TCP streams: %d. Addresses hidden: %s. Packets for ports your game hasn't open: %s.",
            p->nst, idstr(p->swapped), idstr(p->dropped_noport));
    }
    for (int i = 0; i < g_ncatch; i++)
        say("LAN broadcast port %u: %s (%s caught)", (unsigned)g_catch[i].port,
            g_catch[i].ns ? "listening" : "in use, skipped", idstr(g_catch[i].pkts));
}

/* ---------- callbacks ---------- */

static void start_mode(int mode);

static void accept_lobby(u64 lobby)
{
    if (!lobby) return;
    if (g_mode != MODE_JOIN) start_mode(MODE_JOIN);
    if (g_mode == MODE_JOIN) { g_joining = 0; request_join(lobby); }
}

static void handle_cb(int cb, uint8_t *p, int size)
{
    switch (cb) {
    case CB_LobbyCreated:
        if (size >= 16) on_lobby_created((int32_t)rd32(p), rd64(p + 8));
        break;
    case CB_LobbyEnter:
        if (size >= 20) on_lobby_entered(rd64(p), rd32(p + 16));
        break;
    case CB_LobbyChatUpdate: {
        if (size < 28 || rd64(p) != g_lobby) break;
        u64 changed = rd64(p + 8);
        uint32_t st = rd32(p + 24);
        if (st & 1) {
            if (g_is_host && changed != g_me) say("%s joined your lobby.", who(changed));
        } else if (st & (CHAT_LEFT | CHAT_DISCONNECTED | CHAT_KICKED | CHAT_BANNED)) {
            if (g_is_host) {
                if (find_peer(changed)) drop_peer(changed, "left the lobby");
                else say("%s left your lobby.", who(changed));
            } else if (changed == g_host) {
                say("! %s stopped hosting. Waiting for them to host again...", who(g_host));
                lobby_gone();
            }
        }
        break;
    }
    case CB_GameLobbyJoinRequested:
        /* The player accepted a Steam invite (or clicked Join Game): that is
           their yes. Joins from the start screen too - unless they are hosting. */
        if (size >= 16) {
            if (g_is_host) { say("Got a Steam invite, but you are hosting. Click Stop first, then accept it again."); break; }
            say("Accepted invite from %s.", who(rd64(p + 8)));
            accept_lobby(rd64(p));
        }
        break;
    case CB_NetMsgSessionRequest: {
        u64 from = S.Ident_GetSteamID64(p);
        /* host: accept, every message is checked against the lobby + friends list */
        bool ok = g_is_host ? S.Friends_HasFriend(F, from, FRIEND_IMMEDIATE) && !kicked(from)
                  : (g_host && from == g_host) ||
                    (g_mode == MODE_JOIN && !g_lobby && S.Friends_HasFriend(F, from, FRIEND_IMMEDIATE));
        if (ok) S.NM_AcceptSessionWithUser(NM, p);
        else if (g_is_host && kicked(from)) tell_kicked(from);
        else if (g_is_host) {
            static u64 told[16];
            bool seen = false;
            for (int i = 0; i < 16; i++) seen |= told[i] == from;
            if (!seen) {
                for (int i = 0; i < 16; i++) if (!told[i]) { told[i] = from; break; }
                say("! Blocked %s: they are not on your Steam friends list.", who(from));
            }
        }
        break;
    }
    case CB_NetMsgSessionFailed: {
        /* only worth a line for the player we are actually playing with
           (asking a friend who doesn't run SCCTLink fails quietly) */
        u64 who_ = size >= 16 ? S.Ident_GetSteamID64(p) : 0;
        if (who_ && find_peer(who_)) say("(Steam: the connection to %s dropped for a moment; it retries.)", who(who_));
        break;
    }
    default:
        break;
    }
}

static void pump_callbacks(void)
{
    S.MD_RunFrame(g_pipe);
    CallbackMsg m;
    while (S.MD_GetNextCallback(g_pipe, &m)) {
        if (m.callback == CB_SteamAPICallCompleted && m.size >= 16) {
            u64 call = rd64(m.param);
            int cb = (int)rd32(m.param + 8);
            uint32_t sz = rd32(m.param + 12);
            uint8_t *buf = sz < 65536 ? calloc(1, sz + 16) : 0;
            bool failed = false;
            if (buf && S.MD_GetAPICallResult(g_pipe, call, buf, (int)sz, cb, &failed) && !failed)
                handle_cb(cb, buf, (int)sz);
            else if (cb == CB_LobbyEnter && g_joining) {
                say("! Joining the lobby failed. Retrying in a moment.");
                g_joining = 0;
            }
            free(buf);
        } else {
            handle_cb(m.callback, m.param, m.size);
        }
        S.MD_FreeLastCallback(g_pipe);
    }
}

/* ---------- join: find a hosting friend ---------- */

static bool rejected(u64 lobby)
{
    for (int i = 0; i < g_nrejected; i++)
        if (g_rejected[i] == lobby) return true;
    return false;
}

/* Direct discovery (from CoFLink 0.3 and 0.8.1): ask friends' SCCTLink "are
   you hosting?" ('Q'). A host answers 'L' + lobby id, only to people on its
   own friends list. */
typedef struct { u64 friend_, lobby, at, asked; } Answer;
#define NANS 256
static Answer g_ans[NANS];

static Answer *answer_for(u64 friend_, bool make)
{
    int free_ = -1, oldest = 0;
    for (int i = 0; i < NANS; i++) {
        if (g_ans[i].friend_ == friend_) return &g_ans[i];
        if (!g_ans[i].friend_ && free_ < 0) free_ = i;
        if (g_ans[i].asked < g_ans[oldest].asked) oldest = i;
    }
    if (!make) return 0;
    if (free_ < 0) free_ = oldest; /* full: reuse the one asked longest ago */
    memset(&g_ans[free_], 0, sizeof g_ans[free_]);
    g_ans[free_].friend_ = friend_;
    return &g_ans[free_];
}

static int g_last_found_n = -1;
static int g_refresh;      /* Refresh button: 1 = scan now, 2 = scan again shortly and report */
static u64 g_refresh_at;

static int scan_for_host(bool verbose)
{
    /* Ways to see a hosting friend: (1) the lobby in their "game played" info,
       (2) "scctlink_lobby" in their rich presence - both only while
       Steam shows them in SCCTLink ("Spacewar"). Once the host starts the game,
       Steam shows "playing Splinter Cell" instead and both go blank, so (3)
       SCCTLink asks friends' SCCTLink directly: every few seconds for friends in
       Spacewar or Splinter Cell, every 30 s for other online friends (game
       details can be private), and everyone at once on Refresh. */
    static u64 noted[32];
    static int nnoted;
    static UiPerson found[32];
    int n = 0, total = S.Friends_GetFriendCount(F, FRIEND_IMMEDIATE);
    for (int i = 0; i < total && n < 32; i++) {
        u64 id = S.Friends_GetFriendByIndex(F, i, FRIEND_IMMEDIATE);
        uint8_t info[32] = { 0 };
        bool playing = S.Friends_GetFriendGamePlayed(F, id, info);
        uint32_t app = playing ? (uint32_t)(rd64(info) & 0xFFFFFF) : 0;
        bool in_link = app == SPACEWAR_APPID, in_game = app == SCCT_APPID;
        u64 lob = in_link ? rd64(info + 16) : 0;
        u64 t = now_ms();
        if (!lob && in_link) {
            S.Friends_RequestFriendRichPresence(F, id);
            const char *rp = S.Friends_GetFriendRichPresence(F, id, "scctlink_lobby");
            if (rp && *rp) lob = str_to_u64(rp);
        }
        if (!lob) {
            Answer *a = answer_for(id, false);
            if (a && a->lobby && t - a->at < 15000) {
                lob = a->lobby;
            } else if (S.Friends_GetFriendPersonaState(F, id) != 0) { /* online */
                u64 every = (in_link || in_game) ? 4000 : 30000;
                if (g_refresh == 1) every = 0;
                if (!a) a = answer_for(id, true);
                if (!a->asked || t - a->asked > every) { send_ctl1(id, 'Q'); a->asked = t; }
            }
        }
        if (!lob && !in_link) continue;
        if (!lob) {
            bool seen = false;
            for (int k = 0; k < nnoted; k++) seen |= noted[k] == id;
            if (!seen && nnoted < 32) {
                noted[nnoted++] = id;
                say("(%s is running SCCTLink/Spacewar, but no lobby is visible yet - still checking.)", who(id));
            }
            continue;
        }
        if (rejected(lob)) continue;
        found[n].id = id;
        found[n].lobby = lob;
        sfmt(found[n].name, UI_NAME, "%s", who(id));
        n++;
    }
    if (n != g_last_found_n && n > 0)
        say(n == 1 ? "Found %s hosting." : "Found %s friends hosting.", n == 1 ? found[0].name : idstr((u64)n));
    g_last_found_n = n;
    u64 reject = 0;
    u64 pick = ui_offer_hosts(n, found, &reject);
    if (reject && g_nrejected < 16) g_rejected[g_nrejected++] = reject;
    if (pick) request_join(pick);
    else if (!n && verbose) {
        say("No friend is hosting yet. Waiting...");
        say("  Or: paste the host's lobby code.");
    }
    return n;
}

/* ---------- game files check ----------
   Co-op drops when the two games aren't identical (a mod on one PC only, a
   different patch). Each SCCTLink lists the game's files - name, size and
   SHA-256 for the program files - and sends the list to the other; both
   show what differs. Settings (.ini) and logs are left out: they differ
   between PCs on purpose. Paths are relative to the game folder, so nothing
   about the PC (user name, drive) is sent. */
#define MF_MAX 2000
#define MF_HASH_MAX (64u << 20) /* bigger files: compared by size only */
typedef struct { char path[100]; u64 size, mtime; char hash[17]; } MFile; /* mtime: only for the local cache, never sent */
static MFile g_mfs[MF_MAX];
static int g_nmfs = -1;           /* -1: not made yet; 0: game folder not found */
static char *g_mf_text;
static int g_mf_len;
static char g_mf_dir[700];

bool sha256_file(const char *path, char hex[65]);

/* Hashing the program files takes seconds, so the checksums are kept in
   scctlink_cache.txt next to SCCTLink.exe (file name, size, modified time,
   checksum) and a file is only hashed again when its size or time changed. */
#define CACHE_FILE "scctlink_cache.txt"
static MFile *g_cache;
static int g_ncache, g_hashed, g_cached;
/* which game folder the cache is for, without writing the folder's path down */
static const char *cache_tag(void)
{
    static char t[24];
    u64 h = 1469598103934665603ULL;
    for (const char *c = g_mf_dir; *c; c++) h = (h ^ (uint8_t)(*c | 32)) * 1099511628211ULL;
    u64_to_str(h, t);
    return t;
}
static void cache_load(void)
{
    g_ncache = 0;
    if (!g_cache && !(g_cache = calloc(MF_MAX, sizeof *g_cache))) return;
    char path[800];
    sfmt(path, sizeof path, "%s" PATHSEP CACHE_FILE, exe_dir());
    FILE *f = fopen(path, "rb");
    if (!f) return;
    char line[300];
    bool ok = fgets(line, sizeof line, f) && !strncmp(line, "SCCTLINK-CACHE 1\t", 17);
    if (ok) { /* only for the same game folder */
        char want[64];
        sfmt(want, sizeof want, "SCCTLINK-CACHE 1\t%s\n", cache_tag());
        ok = !strcmp(line, want);
    }
    while (ok && g_ncache < MF_MAX && fgets(line, sizeof line, f)) {
        char *t1 = strchr(line, '\t'), *t2 = t1 ? strchr(t1 + 1, '\t') : 0, *t3 = t2 ? strchr(t2 + 1, '\t') : 0;
        if (!t3 || t1 - line >= (int)sizeof g_cache[0].path) continue;
        MFile *c = &g_cache[g_ncache];
        memcpy(c->path, line, (size_t)(t1 - line)); c->path[t1 - line] = 0;
        *t2 = *t3 = 0;
        c->size = str_to_u64(t1 + 1);
        c->mtime = str_to_u64(t2 + 1);
        int hl = 0;
        while (hl < 16 && ((t3[1 + hl] >= '0' && t3[1 + hl] <= '9') || (t3[1 + hl] >= 'a' && t3[1 + hl] <= 'f'))) hl++;
        if (hl != 16) continue;
        memcpy(c->hash, t3 + 1, 16); c->hash[16] = 0;
        g_ncache++;
    }
    fclose(f);
}
static const char *cache_hash(const char *key, u64 size, u64 mtime)
{
    for (int i = 0; i < g_ncache; i++)
        if (g_cache[i].size == size && g_cache[i].mtime == mtime && !strcmp(g_cache[i].path, key)) return g_cache[i].hash;
    return 0;
}
static void cache_save(void)
{
    size_t cap = (size_t)g_nmfs * 160 + 800, k = 0;
    char *t = malloc(cap);
    if (!t) return;
    sfmt(t, cap, "SCCTLINK-CACHE 1\t%s\n", cache_tag());
    k = strlen(t);
    for (int i = 0; i < g_nmfs; i++) {
        MFile *f = &g_mfs[i];
        if (f->path[0] == '@' || f->hash[0] == '-') continue;
        char sz[24], mt[24];
        u64_to_str(f->size, sz);
        u64_to_str(f->mtime, mt);
        sfmt(t + k, cap - k, "%s\t%s\t%s\t%s\n", f->path, sz, mt, f->hash);
        k += strlen(t + k);
    }
    char path[800];
    sfmt(path, sizeof path, "%s" PATHSEP CACHE_FILE, exe_dir());
    file_write_text(path, t);
    free(t);
}

static bool mf_skip(const char *name)
{
    const char *dot = 0;
    for (const char *c = name; *c; c++) if (*c == '.') dot = c;
    if (!dot) return false;
    static const char *const ext[] = { ".ini", ".log", ".txt", ".sav", ".bak", ".tmp", ".dmp", ".lnk", ".url",
                                       ".pdf", ".htm", ".html", ".rtf", ".old", ".crash", ".zip", ".rar", ".7z",
                                       ".scctlink-backup", ".scctlink-new" /* SCCTLink's own copies of changed files */ };
    for (unsigned i = 0; i < sizeof ext / sizeof *ext; i++) {
        const char *a = dot, *b = ext[i];
        while (*a && *b && ((*a | 32) == *b || *a == *b)) { a++; b++; }
        if (!*a && !*b) return true;
    }
    return false;
}

typedef struct { const char *rel; bool hash; int depth; } MfWalk; /* rel: path as on disk */
static void mf_walk(const char *rel, bool hash, int depth);

static void mf_add(void *ctx, const char *name, bool is_dir, u64 size, u64 mtime)
{
    MfWalk *w = ctx;
    char rel[200], key[200];
    sfmt(rel, sizeof rel, "%s%s%s", w->rel, w->rel[0] ? "/" : "", name);
    memcpy(key, rel, sizeof key);
    for (char *c = key; *c; c++) if (*c >= 'A' && *c <= 'Z') *c = (char)(*c + 32); /* Windows ignores case */
    if (is_dir) {
        if (w->depth > 0) {
            /* program folders (System, scripts, Versus\System...) get hashed */
            bool h = w->hash || !strcmp(key, "system") || !strcmp(key, "scripts") || !strcmp(key, "versus")
                     || !strcmp(key, "versus/system") || strstr(key, "/scripts") || strstr(key, "/plugins");
            mf_walk(rel, h, w->depth - 1);
        }
        return;
    }
    if (mf_skip(name) || g_nmfs >= MF_MAX || strlen(key) >= sizeof g_mfs[0].path) return;
    MFile *f = &g_mfs[g_nmfs++];
    memcpy(f->path, key, strlen(key) + 1);
    f->size = size;
    f->mtime = mtime;
    memcpy(f->hash, "-", 2);
    if (w->hash && size <= MF_HASH_MAX) {
        const char *c = cache_hash(key, size, mtime);
        if (c) { memcpy(f->hash, c, 17); g_cached++; return; }
        g_hashed++;
        char full[1000], hex[65];
        sfmt(full, sizeof full, "%s" PATHSEP "%s", g_mf_dir, rel);
#ifdef _WIN32
        for (char *c = full; *c; c++) if (*c == '/') *c = '\\';
#endif
        if (sha256_file(full, hex)) { memcpy(f->hash, hex, 16); f->hash[16] = 0; }
    }
}

static void mf_walk(const char *rel, bool hash, int depth)
{
    char dir[1000];
    if (rel[0]) sfmt(dir, sizeof dir, "%s" PATHSEP "%s", g_mf_dir, rel);
    else sfmt(dir, sizeof dir, "%s", g_mf_dir);
#ifdef _WIN32
    for (char *c = dir; *c; c++) if (*c == '/') *c = '\\';
#endif
    MfWalk w = { rel, hash, depth };
    dir_list(dir, mf_add, &w);
}

static int mf_cmp(const void *a, const void *b) { return strcmp(((const MFile *)a)->path, ((const MFile *)b)->path); }

/* qsort isn't in the Windows build's import list: a small insertion sort. */
static void mf_sort(MFile *f, int n)
{
    static MFile t;
    for (int i = 1; i < n; i++) {
        t = f[i];
        int j = i - 1;
        while (j >= 0 && mf_cmp(&f[j], &t) > 0) { f[j + 1] = f[j]; j--; }
        f[j + 1] = t;
    }
}

/* ---------- settings that must match ----------
   Resolution (shown only), FOV and frame rate limits (must match: different
   values make co-op drift apart). Read from the game's .ini files and the
   widescreen fix's .ini, sent along with the file list as entries named
   "@key" with the value in place of the checksum. */
static bool ini_get(const char *path, const char *section, const char *key, char *out, int n)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    static char buf[1 << 19];
    int len = (int)fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    if (len <= 0) return false;
    buf[len] = 0;
    bool in_sec = !section;
    int kl = (int)strlen(key);
    for (char *l = buf; *l;) {
        char *e = l;
        while (*e && *e != '\n') e++;
        char save = *e;
        *e = 0;
        while (*l == ' ' || *l == '\t') l++;
        if (*l == '[') {
            if (section) {
                char *c = l + 1, *x = (char *)section;
                while (*c && *x && ((*c | 32) == (*x | 32))) { c++; x++; }
                in_sec = !*x && *c == ']';
            }
        } else if (in_sec) {
            int i = 0;
            while (i < kl && l[i] && ((l[i] | 32) == (key[i] | 32))) i++;
            char *v = l + i;
            while (*v == ' ' || *v == '\t') v++;
            if (i == kl && *v == '=') {
                v++;
                while (*v == ' ' || *v == '\t') v++;
                int m = 0;
                while (v[m] && v[m] != '\r' && v[m] != ' ' && v[m] != '\t' && v[m] != ';' && !(v[m] == '/' && v[m + 1] == '/') && m < n - 1) m++;
                memcpy(out, v, (size_t)m);
                out[m] = 0;
                /* "90.000000" -> "90" */
                char *dot = strchr(out, '.');
                if (dot) { bool zeros = true; for (char *z = dot + 1; *z; z++) zeros &= *z == '0'; if (zeros) *dot = 0; }
                *e = save;
                return m > 0;
            }
        }
        *e = save;
        l = *e ? e + 1 : e;
    }
    return false;
}

typedef struct { const char *key, *label; bool must_match; } SetDef;
static const SetDef SETS[] = {
    { "res", "Resolution", false },
    { "fov", "FOV", true },
    { "fps", "FPS limit (game)", true },
    { "ws_res", "Resolution (widescreen fix)", false },
    { "ws_fov", "FOV (widescreen fix)", true },
    { "ws_fps", "FPS limit (widescreen fix)", true },
    { "scctfix", "SCCT Fix (60 FPS cap, borderless)", true },
    { "fullscreen", "Starts in fullscreen", true },
};
#define NSETS ((int)(sizeof SETS / sizeof *SETS))

static void set_add(const char *key, const char *value)
{
    if (!value[0] || g_nmfs >= MF_MAX) return;
    for (int i = 0; i < g_nmfs; i++)
        if (g_mfs[i].path[0] == '@' && !strcmp(g_mfs[i].path + 1, key)) return; /* first found wins */
    MFile *f = &g_mfs[g_nmfs++];
    sfmt(f->path, sizeof f->path, "@%s", key);
    f->size = 0;
    sfmt(f->hash, sizeof f->hash, "%s", value);
}

/* the user settings file can also live in ProgramData\Ubisoft\... */
static char g_user_ini[800], g_ws_ini[900];
static void find_user_ini(const char *dir, int depth);
static void user_ini_cb(void *ctx, const char *name, bool is_dir, u64 size, u64 mtime)
{
    (void)size; (void)mtime;
    const char *dir = ctx;
    char path[800], low[64];
    sfmt(path, sizeof path, "%s" PATHSEP "%s", dir, name);
    sfmt(low, sizeof low, "%s", name);
    for (char *c = low; *c; c++) if (*c >= 'A' && *c <= 'Z') *c = (char)(*c + 32);
    if (is_dir) { if (!g_user_ini[0]) find_user_ini(path, -1); return; }
    if (!strcmp(low, "splintercell3user.ini") && !g_user_ini[0]) sfmt(g_user_ini, sizeof g_user_ini, "%s", path);
}
static void find_user_ini(const char *dir, int depth)
{
    static int left;
    if (depth >= 0) left = depth; else if (--left < 0) { left++; return; }
    dir_list(dir, user_ini_cb, (void *)dir);
    if (depth < 0) left++;
}

static void ws_ini_cb(void *ctx, const char *name, bool is_dir, u64 size, u64 mtime)
{
    (void)size; (void)mtime;
    if (is_dir) return;
    const char *dir = ctx;
    char low[128];
    sfmt(low, sizeof low, "%s", name);
    for (char *c = low; *c; c++) if (*c >= 'A' && *c <= 'Z') *c = (char)(*c + 32);
    size_t L = strlen(low);
    if (L < 4 || strcmp(low + L - 4, ".ini") || !strstr(low, "widescreen")) return;
    char path[900], v[24], x[24], y[24];
    sfmt(path, sizeof path, "%s" PATHSEP "%s", dir, name);
    if (!g_ws_ini[0]) sfmt(g_ws_ini, sizeof g_ws_ini, "%s", path);
    if (ini_get(path, 0, "ResX", x, sizeof x) && ini_get(path, 0, "ResY", y, sizeof y)) {
        char r[17];
        sfmt(r, sizeof r, "%sx%s", x, y);
        set_add("ws_res", strcmp(r, "0x0") ? r : "auto");
    }
    if (ini_get(path, 0, "FOVFactor", v, sizeof v) || ini_get(path, 0, "FOV", v, sizeof v)) set_add("ws_fov", v);
    if (ini_get(path, 0, "FPSLimit", v, sizeof v)) set_add("ws_fps", strcmp(v, "0") ? v : "off");
}

static int scctfix_state(void);
static void settings_collect(void)
{
    char sys[800], path[900], x[24], y[24], v[24];
    sfmt(sys, sizeof sys, "%s" PATHSEP "System", g_mf_dir);
    /* the user's own settings first (ProgramData), then the game folder's */
    g_user_ini[0] = 0;
    g_ws_ini[0] = 0;
    const char *pd = getenv("PROGRAMDATA");
    if (pd && *pd) {
        char ubi[700];
        sfmt(ubi, sizeof ubi, "%s" PATHSEP "Ubisoft", pd);
        find_user_ini(ubi, 3);
    }
    sfmt(path, sizeof path, "%s" PATHSEP "SplinterCell3User.ini", sys);
    const char *users[2] = { g_user_ini, path };
    for (int i = 0; i < 2; i++) {
        if (!users[i][0]) continue;
        if (ini_get(users[i], 0, "DesiredFOV", v, sizeof v) || ini_get(users[i], 0, "DefaultFOV", v, sizeof v)) set_add("fov", v);
        if (ini_get(users[i], "WinDrv.WindowsClient", "FullscreenViewportX", x, sizeof x) &&
            ini_get(users[i], "WinDrv.WindowsClient", "FullscreenViewportY", y, sizeof y)) {
            char r[17];
            sfmt(r, sizeof r, "%sx%s", x, y);
            set_add("res", r);
        }
    }
    sfmt(path, sizeof path, "%s" PATHSEP "SplinterCell3.ini", sys);
    if (ini_get(path, "WinDrv.WindowsClient", "FullscreenViewportX", x, sizeof x) &&
        ini_get(path, "WinDrv.WindowsClient", "FullscreenViewportY", y, sizeof y)) {
        char r[17];
        sfmt(r, sizeof r, "%sx%s", x, y);
        set_add("res", r);
    }
    if (ini_get(path, "SIMULATION", "MAXFPS", v, sizeof v)) set_add("fps", v);
    if (ini_get(path, 0, "DesiredFOV", v, sizeof v)) set_add("fov", v);
    /* the ThirteenAG widescreen fix keeps its .ini in scripts\ (System\scripts or the game folder) */
    sfmt(path, sizeof path, "%s" PATHSEP "scripts", sys);
    dir_list(path, ws_ini_cb, path);
    sfmt(path, sizeof path, "%s" PATHSEP "scripts", g_mf_dir);
    dir_list(path, ws_ini_cb, path);
    set_add("scctfix", scctfix_state() == 1 ? "installed" : "not installed");
    sfmt(path, sizeof path, "%s" PATHSEP "SplinterCell3.ini", sys);
    if (ini_get(path, "WinDrv.WindowsClient", "StartupFullscreen", v, sizeof v)) set_add("fullscreen", (v[0] | 32) == 't' ? "yes" : "no");
}

/* Write value for key (in section, or anywhere if section is 0) into an .ini
   file, keeping everything else as it is. Every matching line is changed. The
   first time a file is changed, the original is kept next to it as
   "<name>.scctlink-backup". Returns how many lines were changed (-1: can't write). */
static int ini_set(const char *path, const char *section, const char *key, const char *value)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    static char in[1 << 19], out[(1 << 19) + 4096];
    int len = (int)fread(in, 1, sizeof in - 1, f);
    fclose(f);
    if (len <= 0) return 0;
    if (len == (int)sizeof in - 1) return -1; /* bigger than any game .ini: don't touch it */
    in[len] = 0;
    bool in_sec = !section;
    int kl = (int)strlen(key), vl = (int)strlen(value), o = 0, hits = 0;
    for (int i = 0; i < len;) {
        int e = i;
        while (e < len && in[e] != '\n') e++;
        int b = i;
        while (b < e && (in[b] == ' ' || in[b] == '\t')) b++;
        bool done = false;
        if (b < e && in[b] == '[') {
            if (section) {
                int c = b + 1, x = 0;
                while (c < e && section[x] && ((in[c] | 32) == (section[x] | 32))) { c++; x++; }
                in_sec = !section[x] && c < e && in[c] == ']';
            }
        } else if (in_sec) {
            int k = 0;
            while (k < kl && b + k < e && ((in[b + k] | 32) == (key[k] | 32))) k++;
            int v = b + k;
            while (v < e && (in[v] == ' ' || in[v] == '\t')) v++;
            if (k == kl && v < e && in[v] == '=') {
                v++;
                while (v < e && (in[v] == ' ' || in[v] == '\t')) v++;
                int t = v; /* the old value: up to a space, comment or line end */
                while (t < e && in[t] != '\r' && in[t] != ' ' && in[t] != '\t' && in[t] != ';' && !(in[t] == '/' && t + 1 < e && in[t + 1] == '/')) t++;
                if (o + (v - i) + vl + (e - t) + 1 >= (int)sizeof out) return -1;
                memcpy(out + o, in + i, (size_t)(v - i)); o += v - i;
                memcpy(out + o, value, (size_t)vl); o += vl;
                memcpy(out + o, in + t, (size_t)(e - t)); o += e - t;
                hits++;
                done = true;
            }
        }
        if (!done) {
            if (o + (e - i) + 1 >= (int)sizeof out) return -1;
            memcpy(out + o, in + i, (size_t)(e - i)); o += e - i;
        }
        if (e < len) out[o++] = '\n';
        i = e + 1;
    }
    if (!hits) return 0;
    char bak[1000];
    sfmt(bak, sizeof bak, "%s.scctlink-backup", path);
    if (!file_exists(bak)) {
        FILE *bf = fopen(bak, "wb");
        if (!bf) return -1;
        fwrite(in, 1, (size_t)len, bf);
        fclose(bf);
    }
    /* write a new file next to it first, so a failed write never leaves half a file */
    char tmp[1000];
    sfmt(tmp, sizeof tmp, "%s.scctlink-new", path);
    FILE *w = fopen(tmp, "wb");
    if (!w) return -1;
    bool ok = fwrite(out, 1, (size_t)o, w) == (size_t)o;
    ok &= fclose(w) == 0;
    if (!ok || remove(path) != 0) { remove(tmp); return -1; }
    if (rename(tmp, path) != 0) return -1; /* the new file stays as <name>.scctlink-new, the original as the backup */
    return hits;
}

/* ---------- the SCCT Fix ----------
   A small d3d9.dll: 60 FPS cap, borderless window, Alt+Tab
   works, System\SCCTFix.log on a crash. Chaos Theory's co-op runs the same
   simulation on both PCs and ends the session when they drift apart; with this
   file and StartupFullscreen=False on both PCs it stopped doing that. It ships
   next to SCCTLink.exe as SCCTFix-d3d9.dll and is only ever copied if its
   SHA-256 is exactly this one (checked: no network functions, starts no
   programs, only writes System\SCCTFix.log). */
#define SCCTFIX_FILE "SCCTFix-d3d9.dll"
#define SCCTFIX_SHA "2f05d8e9e7aaf750eb2889c7ba57dd453820e3a794b9ba74c43730446a84cae9"
bool sha256_file(const char *path, char hex[65]);
static void scctfix_paths(char *src, char *dst, char *bak, char *ini, size_t n)
{
    sfmt(src, n, "%s" PATHSEP SCCTFIX_FILE, exe_dir());
    sfmt(dst, n, "%s" PATHSEP "System" PATHSEP "d3d9.dll", g_mf_dir);
    sfmt(bak, n, "%s.scctlink-backup", dst);
    sfmt(ini, n, "%s" PATHSEP "System" PATHSEP "SplinterCell3.ini", g_mf_dir);
}
/* -1 no game folder, 0 not there, 1 installed, 2 another d3d9.dll is there */
static int scctfix_state(void)
{
    if (!g_steam_ok || g_nmfs <= 0 || !g_mf_dir[0]) return -1;
    char src[900], dst[900], bak[900], ini[900], hex[65];
    scctfix_paths(src, dst, bak, ini, sizeof src);
    if (!sha256_file(dst, hex)) return 0;
    return strcmp(hex, SCCTFIX_SHA) ? 2 : 1;
}
static bool copy_file(const char *from, const char *to)
{
    FILE *a = fopen(from, "rb");
    if (!a) return false;
    FILE *b = fopen(to, "wb");
    if (!b) { fclose(a); return false; }
    char buf[8192];
    size_t r;
    bool ok = true;
    while ((r = fread(buf, 1, sizeof buf, a)) > 0) ok &= fwrite(buf, 1, r, b) == r;
    fclose(a);
    return fclose(b) == 0 && ok;
}
static void set_say(const char *fmt, ...);
static void mf_recheck(void);
static UiSettings g_uiset;
static void scctfix_(bool install)
{
    char src[900], dst[900], bak[900], ini[900], hex[65];
    int st = scctfix_state();
    if (st < 0) { set_say("! SCCT Fix: the game's folder wasn't found (only Steam copies)."); return; }
    if (game_is_running(g_exe)) { set_say("! SCCT Fix: close Splinter Cell first."); return; }
    scctfix_paths(src, dst, bak, ini, sizeof src);
    if (!install) {
        if (st != 1) { set_say(st == 2 ? "! SCCT Fix: the d3d9.dll in System is another mod, not the SCCT Fix - left alone." : "SCCT Fix: not installed."); return; }
        if (remove(dst) != 0) { set_say("! SCCT Fix: couldn't remove it."); return; }
        if (file_exists(bak) && rename(bak, dst) == 0) say("SCCT Fix: the earlier d3d9.dll was put back.");
        ini_set(ini, "WinDrv.WindowsClient", "StartupFullscreen", "True");
        set_say("SCCT Fix removed (the game starts in fullscreen again).");
        return;
    }
    if (st != 1) {
        if (!sha256_file(src, hex)) { set_say("! SCCT Fix: " SCCTFIX_FILE " is missing (it must be next to SCCTLink.exe)."); return; }
        if (strcmp(hex, SCCTFIX_SHA)) { set_say("! SCCT Fix: " SCCTFIX_FILE " is not the expected file, so it was NOT installed."); return; }
        if (st == 2) { /* another d3d9.dll (another mod): keep it */
            if (file_exists(bak)) { set_say("! SCCT Fix: there is another d3d9.dll in System and already a backup - move it out yourself first."); return; }
            if (rename(dst, bak) != 0) { set_say("! SCCT Fix: couldn't move the other d3d9.dll aside."); return; }
            say("SCCT Fix: the other d3d9.dll was renamed to d3d9.dll.scctlink-backup.");
        }
        if (!copy_file(src, dst) || !sha256_file(dst, hex) || strcmp(hex, SCCTFIX_SHA)) {
            remove(dst);
            if (st == 2) rename(bak, dst);
            set_say("! SCCT Fix: couldn't copy it into the game's System folder.");
            return;
        }
    }
    /* the tested setup also starts the game in a window (the fix makes it borderless) */
    if (ini_set(ini, "WinDrv.WindowsClient", "StartupFullscreen", "False") < 0)
        say("! SCCT Fix: couldn't set StartupFullscreen=False in SplinterCell3.ini.");
    set_say(st == 1 ? "SCCT Fix: already installed (StartupFullscreen=False checked)."
                    : "SCCT Fix installed: 60 FPS cap, borderless window (StartupFullscreen=False). Both players need it.");
}
/* "Match friend": the friend's resolution, FOV and FPS limits, and the SCCT Fix if they have it. */
static void apply_settings_(u64 a);
static void match_friend_(void)
{
    UiSettings u = g_uiset;
    if (!u.friend_) { set_say("! Match friend: link with a friend first."); return; }
    if (u.ffix == 1 && scctfix_state() != 1) scctfix_(true);
    int w = u.fw && u.fh && (u.fw != u.w || u.fh != u.h) ? u.fw : 0, h = w ? u.fh : 0;
    int fov = u.have_fov && u.ffov && u.ffov != u.fov ? u.ffov : 0;
    int fps = u.ffps && u.ffps != u.fps ? u.ffps : 0;
    int ws = u.have_ws && u.fwsfov && u.fwsfov != u.wsfov ? u.fwsfov : 0;
    if (w || fov || fps || ws) apply_settings_(settings_pack(w, h, fov, fps, ws));
    else if (!g_uiset.note_warn) set_say("Match friend: your settings are already the same as %s's.", u.friend_name);
    if (!g_uiset.note_warn) sfmt(g_uiset.note, sizeof g_uiset.note, "Done: same settings as %s. They apply the next time the game starts.", u.friend_name);
}
static void match_friend(void)
{
    g_uiset.note[0] = 0;
    g_uiset.note_warn = false;
    match_friend_();
    mf_recheck();
    settings_publish();
}

static void scctfix(bool install)
{
    g_uiset.note[0] = 0;
    g_uiset.note_warn = false;
    scctfix_(install);
    mf_recheck(); /* the file list changed: show it and tell the friends */
    settings_publish();
}

/* The Settings tab's Apply. */
/* say() it, and show the first warning (or the last line) under the Settings tab's fields */
static void set_say(const char *fmt, ...)
{
    char b[400];
    va_list ap;
    va_start(ap, fmt);
#ifdef _WIN32
    _vsnprintf(b, sizeof b, fmt, ap);
#else
    vsnprintf(b, sizeof b, fmt, ap);
#endif
    va_end(ap);
    b[sizeof b - 1] = 0;
    say("%s", b);
    if (g_uiset.note_warn) return;
    const char *t = b[0] == '!' ? b + 2 : b;
    if (!strncmp(t, "Settings: ", 10)) t += 10;
    sfmt(g_uiset.note, sizeof g_uiset.note, "%s", t);
    g_uiset.note_warn = b[0] == '!';
}
static void apply_settings_(u64 a);
static void apply_settings(u64 a)
{
    g_uiset.note[0] = 0;
    g_uiset.note_warn = false;
    apply_settings_(a);
    settings_publish();
}
static void apply_settings_(u64 a)
{
    int w = (int)(a & 8191), h = (int)(a >> 13 & 8191), fov = (int)(a >> 26 & 255), fps = (int)(a >> 34 & 1023),
        ws = (int)(a >> 44 & 4095);
    if (!w && !h && !fov && !fps && !ws) { set_say("Settings: nothing changed."); return; }
    if (!g_steam_ok || g_nmfs <= 0 || !g_mf_dir[0]) { set_say("! Settings: the game's folder wasn't found (only Steam copies)."); return; }
    if (game_is_running(g_exe)) { set_say("! Settings: close Splinter Cell first - it writes its settings back when it exits."); return; }
    if ((w || h) && (w < 320 || w > 7680 || h < 200 || h > 4320)) { set_say("! Settings: that resolution doesn't look right."); return; }
    if (fov && (fov < 50 || fov > 130)) { set_say("! Settings: FOV must be between 50 and 130."); return; }
    if (fps && (fps < 20 || fps > 1000)) { set_say("! Settings: the FPS limit must be between 20 and 1000."); return; }
    if (ws && (ws < 50 || ws > 300)) { set_say("! Settings: the widescreen fix's FOV factor must be between 0.50 and 3.00."); return; }
    char sys_ini[900], user_sys[900], v[24];
    sfmt(sys_ini, sizeof sys_ini, "%s" PATHSEP "System" PATHSEP "SplinterCell3.ini", g_mf_dir);
    sfmt(user_sys, sizeof user_sys, "%s" PATHSEP "System" PATHSEP "SplinterCell3User.ini", g_mf_dir);
    const char *users[2] = { g_user_ini, user_sys };
    bool err = false;
    int r;
#define SET(path, sec, key, val) do { r = ini_set(path, sec, key, val); if (r < 0) err = true; } while (0)
    if (w && h) {
        int n = 0;
        sfmt(v, sizeof v, "%d", w); SET(sys_ini, "WinDrv.WindowsClient", "FullscreenViewportX", v); n += r > 0;
        sfmt(v, sizeof v, "%d", h); SET(sys_ini, "WinDrv.WindowsClient", "FullscreenViewportY", v); n += r > 0;
        for (int i = 0; i < 2; i++) if (users[i][0]) {
            sfmt(v, sizeof v, "%d", w); SET(users[i], "WinDrv.WindowsClient", "FullscreenViewportX", v); n += r > 0;
            sfmt(v, sizeof v, "%d", h); SET(users[i], "WinDrv.WindowsClient", "FullscreenViewportY", v); n += r > 0;
        }
        char x[24];
        if (g_ws_ini[0] && ini_get(g_ws_ini, 0, "ResX", x, sizeof x) && strcmp(x, "0")) { /* the fix overrides it */
            sfmt(v, sizeof v, "%d", w); SET(g_ws_ini, 0, "ResX", v);
            sfmt(v, sizeof v, "%d", h); SET(g_ws_ini, 0, "ResY", v);
        }
        set_say(n ? "Settings: resolution set to %dx%d." : "! Settings: the resolution setting wasn't found in the game's files.", w, h);
    }
    if (fov) {
        int n = 0;
        sfmt(v, sizeof v, "%d.000000", fov);
        for (int i = 0; i < 2; i++) if (users[i][0]) {
            SET(users[i], 0, "DesiredFOV", v); n += r > 0;
            SET(users[i], 0, "DefaultFOV", v); n += r > 0;
        }
        SET(sys_ini, 0, "DesiredFOV", v); n += r > 0;
        if (n) set_say("Settings: FOV set to %d.", fov);
        else set_say("! Settings: the game's files have no FOV setting to change%s.", g_ws_ini[0] ? " - use the widescreen fix's FOV factor" : "");
    }
    if (fps) {
        sfmt(v, sizeof v, "%d", fps);
        SET(sys_ini, "SIMULATION", "MAXFPS", v);
        int n = r > 0;
        if (g_ws_ini[0]) { SET(g_ws_ini, 0, "FPSLimit", v); n += r > 0; }
        set_say(n ? "Settings: FPS limit set to %d." : "! Settings: the FPS limit setting wasn't found.", fps);
    }
    if (ws) {
        sfmt(v, sizeof v, "%d.%02d", ws / 100, ws % 100);
        if (!g_ws_ini[0]) set_say("! Settings: the widescreen fix isn't installed, so its FOV factor wasn't set.");
        else {
            SET(g_ws_ini, 0, "FOVFactor", v);
            set_say(r > 0 ? "Settings: the widescreen fix's FOV factor set to %s." : "! Settings: the widescreen fix's .ini has no FOVFactor.", v);
        }
    }
#undef SET
    if (err) set_say("! Settings: some files couldn't be written (are they read-only?).");
    else say("Settings written. Backups of the original files are next to them (*.scctlink-backup).");
    if (!g_uiset.note_warn) sfmt(g_uiset.note, sizeof g_uiset.note, "Saved. They apply the next time Splinter Cell starts.");
    mf_recheck(); /* show the new values and send them to the friends */
}

static const char *set_value(const MFile *f, int n, const char *key)
{
    for (int i = 0; i < n; i++)
        if (f[i].path[0] == '@' && !strcmp(f[i].path + 1, key)) return f[i].hash;
    return 0;
}

static int files_count(void)
{
    int k = 0;
    for (int i = 0; i < g_nmfs; i++) k += g_mfs[i].path[0] != '@';
    return k;
}

/* Make this PC's list once (it takes a second or two: the program files are hashed). */
static void mf_make(void)
{
    if (g_nmfs >= 0) return;
    g_nmfs = 0;
    say("Checking the game files (takes a few seconds the first time)...");
    g_mf_dir[0] = 0;
    if (!(g_steam_ok && AP && S.Apps_GetAppInstallDir && S.Apps_GetAppInstallDir(AP, SCCT_APPID, g_mf_dir, sizeof g_mf_dir) > 0) || !g_mf_dir[0]) {
        note("game files check: the game's folder wasn't found (not a Steam copy?)");
    } else {
        u64 t = now_ms();
        cache_load();
        g_hashed = g_cached = 0;
        mf_walk("", false, 3);
        settings_collect();
        mf_sort(g_mfs, g_nmfs);
        cache_save();
        note("game files check: %d files listed in %d ms (%d checksums made, %d from the cache)", files_count(),
             (int)(now_ms() - t), g_hashed, g_cached);
    }
    mods_local_text();
    mods_publish();
    settings_publish();
    free(g_mf_text);
    g_mf_text = malloc((size_t)g_nmfs * 140 + 16);
    int k = 0;
    if (!g_mf_text) return;
    for (int i = 0; i < g_nmfs; i++) {
        char sz[24];
        u64_to_str(g_mfs[i].size, sz);
        sfmt(g_mf_text + k, 140, "%s\t%s\t%s\n", g_mfs[i].path, sz, g_mfs[i].hash);
        k += (int)strlen(g_mf_text + k);
    }
    g_mf_len = k;
}

/* Files that are mods (or that mods put in the game folder), to name them as such. */
static bool mf_is_mod(const char *path)
{
    const char *name = path;
    for (const char *c = path; *c; c++) if (*c == '/') name = c + 1;
    size_t L = strlen(name);
    if (L > 4 && !strcmp(name + L - 4, ".asi")) return true;
    if (strstr(path, "scripts/") || strstr(path, "plugins/") || strstr(path, "reshade") || strstr(path, "widescreen"))
        return true;
    static const char *const loaders[] = { "dinput8.dll", "d3d9.dll", "d3d8.dll", "dsound.dll", "winmm.dll",
                                           "dxgi.dll", "version.dll", "d3d11.dll", "vorbisfile.dll", "xlive.dll" };
    bool top = name == path || !strncmp(path, "system/", 7) || !strncmp(path, "versus/system/", 14);
    for (unsigned i = 0; i < sizeof loaders / sizeof *loaders; i++)
        if (top && !strcmp(name, loaders[i])) return true;
    return false;
}

/* The "Mods" tab: this PC's game, then one checklist per linked friend. */
static char g_mods_local[4096];
static struct { u64 id; bool warn; char text[6144]; } g_cmp[MAX_PEERS];
static int g_ncmp;

static void mods_publish(void)
{
    static char t[16384];
    int k = 0;
    bool warn = false;
    sfmt(t, sizeof t, "%s", g_mods_local);
    k = (int)strlen(t);
    for (int i = 0; i < g_ncmp && k < (int)sizeof t - 10; i++) {
        sfmt(t + k, sizeof t - (size_t)k, "\r\n%s", g_cmp[i].text);
        k += (int)strlen(t + k);
        warn |= g_cmp[i].warn;
    }
    if (!g_ncmp) {
        sfmt(t + k, sizeof t - (size_t)k, "\r\nWhen a friend links, their game is compared with yours here.\r\n"
             "(The game doesn't have to be running.)");
    }
    ui_mods(t, warn);
}

/* The Settings tab: numbers from the "@key" entries of a game files list. */
static int num_at(const char *v, int scale) /* "1.25" -> 125 with scale 100 */
{
    if (!v) return 0;
    int i = 0, f = 0, d = 0;
    while (*v >= '0' && *v <= '9') i = i * 10 + (*v++ - '0');
    if (*v == '.') for (v++; *v >= '0' && *v <= '9'; v++) if (d < 2) { f = f * 10 + (*v - '0'); d++; }
    if (scale == 100) { while (d < 2) { f *= 10; d++; } return i * 100 + f; }
    return i;
}
static void set_numbers(const MFile *f, int n, int *w, int *h, int *fov, int *fps, int *ws)
{
    const char *r = set_value(f, n, "res"), *x;
    *w = *h = 0;
    if (r) { *w = num_at(r, 1); if ((x = strchr(r, 'x'))) *h = num_at(x + 1, 1); }
    *fov = num_at(set_value(f, n, "fov"), 1);
    *fps = num_at(set_value(f, n, "fps"), 1);
    if (!*fps) *fps = num_at(set_value(f, n, "ws_fps"), 1);
    *ws = num_at(set_value(f, n, "ws_fov"), 100);
}
static u64 g_uiset_friend;
static void settings_publish(void)
{
    UiSettings *u = &g_uiset;
    u->found = g_nmfs > 0;
    if (u->found) set_numbers(g_mfs, g_nmfs, &u->w, &u->h, &u->fov, &u->fps, &u->wsfov);
    else u->w = u->h = u->fov = u->fps = u->wsfov = 0;
    u->have_fov = u->found && set_value(g_mfs, g_nmfs, "fov");
    u->have_ws = u->found && g_ws_ini[0];
    u->scctfix = scctfix_state();
    ui_settings(u);
}

static void mods_forget(u64 id)
{
    for (int i = 0; i < g_ncmp; i++)
        if (g_cmp[i].id == id) {
            g_cmp[i] = g_cmp[--g_ncmp];
            if (g_uiset_friend == id) { g_uiset_friend = 0; g_uiset.friend_ = false; settings_publish(); }
            mods_publish();
            return;
        }
}

static void mods_local_text(void)
{
    char *o = g_mods_local;
    size_t n = sizeof g_mods_local;
    if (g_nmfs <= 0) {
        sfmt(o, n, "YOUR GAME\r\n[?] The game's folder wasn't found. Only Steam copies of Splinter Cell:\r\n"
                   "    Chaos Theory can be checked.\r\n");
        return;
    }
    int nm = 0;
    for (int i = 0; i < g_nmfs; i++) nm += g_mfs[i].path[0] != '@' && mf_is_mod(g_mfs[i].path);
    sfmt(o, n, "YOUR GAME (Steam folder, %d files checked)\r\n", files_count());
    size_t k = strlen(o);
    for (int d = 0; d < NSETS; d++) { /* the settings that matter for co-op */
        const char *v = set_value(g_mfs, g_nmfs, SETS[d].key);
        if (!v) continue;
        sfmt(o + k, n - k, "%s: %s\r\n", SETS[d].label, v);
        k = strlen(o);
    }
    if (!nm) { sfmt(o + k, n - k, "No mods found: plain game files.\r\n"); return; }
    sfmt(o + k, n - k, "Mod files found (%d):\r\n", nm);
    k = strlen(o);
    int shown = 0;
    for (int i = 0; i < g_nmfs && k < n - 120; i++)
        if (g_mfs[i].path[0] != '@' && mf_is_mod(g_mfs[i].path) && shown++ < 30) {
            sfmt(o + k, n - k, "    %s\r\n", g_mfs[i].path);
            k = strlen(o);
        }
    if (nm > shown) sfmt(o + k, n - k, "    ...and %d more\r\n", nm - shown);
}

/* Send the list in pieces: 'M' PROTO index count bytes... (index from 0). */
#define MF_CHUNK 48000
static void mf_send(Peer *p)
{
    if (p->mf_sent) return;
    p->mf_sent = true;
    mf_make();
    int count = g_mf_len ? (g_mf_len + MF_CHUNK - 1) / MF_CHUNK : 1;
    static uint8_t buf[MF_CHUNK + 8];
    for (int i = 0; i < count; i++) {
        int off = i * MF_CHUNK, len = g_mf_len - off < MF_CHUNK ? g_mf_len - off : MF_CHUNK;
        if (len < 0) len = 0;
        buf[0] = 'M'; buf[1] = PROTO; buf[2] = (uint8_t)i; buf[3] = (uint8_t)count;
        buf[4] = g_nmfs > 0 ? 1 : 0; /* 0: "I couldn't find my game folder" */
        if (len) memcpy(buf + 5, g_mf_text + off, (size_t)len);
        send_ctl(p->id, buf, 5 + len);
    }
}

static bool mf_parse_line(const char *s, int n, MFile *f)
{
    int t1 = -1, t2 = -1;
    for (int i = 0; i < n; i++) if (s[i] == '\t') { if (t1 < 0) t1 = i; else { t2 = i; break; } }
    if (t1 <= 0 || t2 < 0 || t1 >= (int)sizeof f->path) return false;
    memcpy(f->path, s, (size_t)t1); f->path[t1] = 0;
    char num[24]; int nl = t2 - t1 - 1; if (nl <= 0 || nl > 22) return false;
    memcpy(num, s + t1 + 1, (size_t)nl); num[nl] = 0;
    f->size = str_to_u64(num);
    int hl = n - t2 - 1; if (hl <= 0 || hl > 16) return false;
    memcpy(f->hash, s + t2 + 1, (size_t)hl); f->hash[hl] = 0;
    return true;
}

static void mf_report(Peer *p, bool they_found)
{
    char them[96];
    sfmt(them, sizeof them, "%s", who(p->id));
    int ci;
    for (ci = 0; ci < g_ncmp; ci++) if (g_cmp[ci].id == p->id) break;
    if (ci == g_ncmp) { if (g_ncmp == MAX_PEERS) return; g_ncmp++; }
    g_cmp[ci].id = p->id;
    g_cmp[ci].warn = false;
    char *o = g_cmp[ci].text;
    size_t on = sizeof g_cmp[ci].text;
    if (g_nmfs <= 0 || !they_found) {
        const char *where = g_nmfs <= 0 && !they_found ? "either PC" : g_nmfs <= 0 ? "your PC" : "their PC";
        say("Game files check: skipped - the game's folder wasn't found on %s (only Steam copies can be checked).", where);
        sfmt(o, on, "COMPARED WITH %s\r\n[?] Skipped: the game's folder wasn't found on %s.\r\n", them, where);
        mods_publish();
        return;
    }
    static MFile theirs[MF_MAX];
    int nt = 0;
    for (int q = 0; q < p->nmf && nt < MF_MAX;) {
        int e = q;
        while (e < p->nmf && p->mf[e] != '\n') e++;
        if (mf_parse_line(p->mf + q, e - q, &theirs[nt])) nt++;
        q = e + 1;
    }
    g_uiset_friend = p->id; /* their values for the Settings tab's "Use friend's" */
    g_uiset.friend_ = true;
    sfmt(g_uiset.friend_name, sizeof g_uiset.friend_name, "%s", them);
    set_numbers(theirs, nt, &g_uiset.fw, &g_uiset.fh, &g_uiset.ffov, &g_uiset.ffps, &g_uiset.fwsfov);
    {
        const char *fx = set_value(theirs, nt, "scctfix");
        g_uiset.ffix = !fx ? -1 : !strcmp(fx, "installed") ? 1 : 0;
    }
    settings_publish();
    mf_sort(theirs, nt);
    /* three groups: the game program, mods, everything else */
    static char lines[3][4096];
    int cnt[3] = { 0, 0, 0 };
    size_t ln[3] = { 0, 0, 0 };
    lines[0][0] = lines[1][0] = lines[2][0] = 0;
    int diffs = 0, shown = 0;
    char line[300], their_pc[120];
    sfmt(their_pc, sizeof their_pc, "only on %s's PC", them);
    int i = 0, j = 0;
    while (i < g_nmfs || j < nt) {
        if (i < g_nmfs && g_mfs[i].path[0] == '@') { i++; continue; } /* settings: compared below */
        if (j < nt && theirs[j].path[0] == '@') { j++; continue; }
        int c = i >= g_nmfs ? 1 : j >= nt ? -1 : strcmp(g_mfs[i].path, theirs[j].path);
        const char *what = 0, *path = 0;
        if (c < 0) { what = "only on your PC"; path = g_mfs[i++].path; }
        else if (c > 0) { what = their_pc; path = theirs[j++].path; }
        else {
            bool same = g_mfs[i].size == theirs[j].size && !strcmp(g_mfs[i].hash, theirs[j].hash);
            if (!same) { what = "different version"; path = g_mfs[i].path; }
            i++; j++;
        }
        if (!what) continue;
        if (!diffs++) say("! Game files differ from %s's. Co-op can disconnect when the games aren't the same:", them);
        sfmt(line, sizeof line, "    %s: %s", what, path);
        if (shown < 25) { say("%s", line); shown++; }
        else log_line(line);
        int g = (strstr(path, "splintercell3.exe") || strstr(path, "scct_versus.exe")) ? 0 : mf_is_mod(path) ? 1 : 2;
        if (cnt[g]++ < 12 && ln[g] < sizeof lines[g] - 300) {
            sfmt(lines[g] + ln[g], sizeof lines[g] - ln[g], "%s\r\n", line);
            ln[g] += strlen(lines[g] + ln[g]);
        }
    }
    if (diffs > shown) say("    ...and %d more (all in scctlink.log)", diffs - shown);
    /* settings: resolution is only shown; FOV and frame rate limits must match */
    static char setl[2048];
    size_t sl = 0;
    int setdiff = 0;
    setl[0] = 0;
    for (int d = 0; d < NSETS; d++) {
        const char *a = set_value(g_mfs, g_nmfs, SETS[d].key), *b = set_value(theirs, nt, SETS[d].key);
        if (!a && !b) continue;
        bool same = a && b && !strcmp(a, b);
        if (!same && SETS[d].must_match) {
            setdiff++;
            say("! %s differs: you %s, %s %s. Set the same on both PCs.", SETS[d].label, a ? a : "not set", them, b ? b : "not set");
        }
        sfmt(setl + sl, sizeof setl - sl, "    %s: you %s, %s %s%s\r\n", SETS[d].label, a ? a : "not set", them, b ? b : "not set",
             same ? "" : SETS[d].must_match ? "  <- must match" : "  (doesn't need to match)");
        sl += strlen(setl + sl);
    }
    sfmt(o, on, "COMPARED WITH %s\r\n", them);
    size_t k = strlen(o);
    if (setl[0]) {
        if (!setdiff) sfmt(o + k, on - k, "[x] Same settings (FOV, FPS limits, SCCT Fix)\r\n%s", setl);
        else sfmt(o + k, on - k, "[  ] Same settings - %d difference%s:\r\n%s", setdiff, setdiff == 1 ? "" : "s", setl);
        k = strlen(o);
    }
    static const char *const label[3] = { "Same game version (splintercell3.exe)", "Same mods", "Same game files" };
    for (int g = 0; g < 3 && k < on - 200; g++) {
        if (!cnt[g]) sfmt(o + k, on - k, "[x] %s\r\n", label[g]);
        else sfmt(o + k, on - k, "[  ] %s - %d difference%s:\r\n%s%s", label[g], cnt[g], cnt[g] == 1 ? "" : "s",
                  lines[g], cnt[g] > 12 ? "    ...more in the Log\r\n" : "");
        k = strlen(o);
    }
    if (!diffs && !setdiff) {
        say("Game files check: same as %s's (%d files).", them, files_count());
        sfmt(o + k, on - k, "All %d files are the same. Good to go.\r\n", files_count());
    } else if (!diffs) {
        say("Game files check: same as %s's (%d files), but the settings above differ.", them, files_count());
        sfmt(o + k, on - k, "All %d files are the same, but make the settings marked \"must match\" the same on both PCs.\r\n", files_count());
        g_cmp[ci].warn = true;
        ui_alert("settings differ");
    } else {
        say("  Make both games the same: the same mods (same versions) on both PCs, or none.");
        sfmt(o + k, on - k, "Make both games the same: the same mods (same versions) on both PCs, or\r\n"
                            "none. Then click \"Check again\". Co-op can disconnect while they differ.\r\n");
        g_cmp[ci].warn = true;
        ui_alert("game files differ");
    }
    mods_publish();
}

static void mf_recheck(void)
{
    g_nmfs = -1;
    mf_make();
    for (int i = 0; i < g_npeers; i++) {
        Peer *p = &g_peers[i];
        if (!p->welcomed) continue;
        p->mf_sent = false;
        mf_send(p);
        if (p->mf_done) mf_report(p, p->mf_found);
    }
}

static void mf_recv(Peer *p, const uint8_t *m, int n)
{
    if (n < 5) return;
    int idx = m[2], count = m[3];
    if (idx == 0) { free(p->mf); p->mf = 0; p->nmf = 0; p->mf_done = false; } /* a new list (they clicked "Check again") */
    if (p->mf_done) return;
    int len = n - 5;
    if (p->nmf + len > MF_MAX * 140) return;
    char *nb = realloc(p->mf, (size_t)(p->nmf + len + 1));
    if (!nb) return;
    p->mf = nb;
    memcpy(p->mf + p->nmf, m + 5, (size_t)len);
    p->nmf += len;
    if (idx + 1 < count) return;
    p->mf_done = true;
    p->mf_found = m[4] != 0;
    mf_send(p); /* make sure they get ours too */
    mf_make();
    mf_report(p, p->mf_found);
}

/* ---------- control messages ---------- */

static void on_ctl(u64 from, const uint8_t *m, int n)
{
    if (n < 1) return;
    char c = (char)m[0];
    if (c == 'Q') { /* a friend's SCCTLink asks "are you hosting?" */
        if (g_is_host && g_lobby && from != g_me && !kicked(from) && S.Friends_HasFriend(F, from, FRIEND_IMMEDIATE)) {
            uint8_t a[10] = { 'L', PROTO };
            memcpy(a + 2, &g_lobby, 8);
            send_ctl(from, a, 10);
        }
        return;
    }
    if (c == 'L') { /* the answer: that friend hosts this lobby */
        if (g_mode == MODE_JOIN && !g_lobby && n >= 10 && S.Friends_HasFriend(F, from, FRIEND_IMMEDIATE)) {
            Answer *a = answer_for(from, true);
            a->lobby = rd64(m + 2);
            a->at = now_ms();
        }
        return;
    }
    Peer *p = find_peer(from);
    if (g_is_host) {
        if (c == 'B') { if (p) drop_peer(from, "quit SCCTLink"); return; }
        if (!p) {
            if (!allowed_peer(from)) { note_ignored(from); return; }
            p = add_peer(from);
            if (!p) { say("! %s tried to join, but the game is full.", who(from)); return; }
        }
        p->last_heard = now_ms();
        if (c == 'M') { if (p->welcomed) mf_recv(p, m, n); return; }
        if (c == 'H') {
            if (n < 2 || m[1] != PROTO) {
                say("! %s runs a different SCCTLink version. Both of you need the same version (" VERSION ").", who(from));
                static const char x[] = "X" VERSION;
                send_ctl(from, x, (int)sizeof x - 1); /* tell them too */
                return;
            }
            int their_len = n >= 3 ? m[2] : 12;
            if (!p->welcomed) {
                char tv[16] = { 0 };
                if (n > 3) memcpy(tv, m + 3, (size_t)(n - 3 < 15 ? n - 3 : 15));
                version_notice(from, tv);
                p->alias = make_alias(their_len, p->slot + 1);
                p->my_alias = make_alias(g_my_text_len, 0);
                p->welcomed = true;
                say("+ %s connected through Steam. Their game shows up on your PC at %s.", who(from), ip_kind(p->alias));
                ui_alert("friend linked");
            }
            uint8_t w[10 + 16] = { 'W', PROTO };
            memcpy(w + 2, &p->alias, 4);    /* the joiner's address on the host's PC */
            memcpy(w + 6, &p->my_alias, 4); /* the host's address on the joiner's PC */
            int vl = (int)strlen(VERSION);
            memcpy(w + 10, VERSION, (size_t)vl);
            send_ctl(from, w, 10 + vl);
            send_ports(p);
            mf_send(p);
            for (int i = 0; i < p->nrudp; i++) alias_get(p, p->rudp[i], true);
            listeners_update(p);
        }
    } else {
        if (from != g_host || !p) return;
        g_host_last = now_ms();
        if (c == 'M') { if (p->welcomed) mf_recv(p, m, n); return; }
        if (c == 'Z') {
            say("! %s removed you from their game (kicked). You can't link to it again until they host again.", who(g_host));
            if (g_nrejected < 16) g_rejected[g_nrejected++] = g_lobby;
            lobby_gone();
            return;
        }
        if (c == 'X') {
            char v[16] = { 0 };
            memcpy(v, m + 1, (size_t)(n - 1 < 15 ? n - 1 : 15));
            say("! %s uses a different SCCTLink version (%s). You both need SCCTLink " VERSION ".", who(g_host), clean(v));
            return;
        }
        if (c == 'W' && n >= 10 && !p->welcomed) {
            memcpy(&p->my_alias, m + 2, 4);
            memcpy(&p->alias, m + 6, 4);
            if (!is_loop(p->alias) || !is_loop(p->my_alias)) { say("! The host sent a bad address. Ignoring."); return; }
            p->welcomed = true;
            say("Connected to %s through Steam. Their game shows up on your PC at %s.", who(g_host), ip_kind(p->alias));
            {
                char tv[16] = { 0 };
                if (n > 10) memcpy(tv, m + 10, (size_t)(n - 10 < 15 ? n - 10 : 15));
                version_notice(g_host, tv);
            }
            send_ports(p);
            for (int i = 0; i < p->nrudp; i++) alias_get(p, p->rudp[i], true);
            listeners_update(p);
            linked_message();
            mf_send(p);
        } else if (c == 'B') {
            say("! %s closed SCCTLink.", who(g_host));
            ring_dump("the host closed SCCTLink");
            lobby_gone();
            return;
        }
    }
    if (!p) return;
    if (c == 'K' && n >= 9) { uint8_t r[9]; memcpy(r, m, 9); r[0] = 'k'; send_ctl(from, r, 9); }
    else if (c == 'k' && n >= 9) {
        u64 sent = rd64(m + 1), t = now_ms();
        if (t >= sent) {
            double rtt = (double)(t - sent);
            p->rtt = p->have_rtt ? p->rtt * 0.8 + rtt * 0.2 : rtt;
            p->have_rtt = true;
        }
    } else if (c == 'P') on_ports(p, m, n);
}

/* ---------- relay loops ---------- */

static void drain_steam(void)
{
    void *msgs[64];
    int n;
    while ((n = S.NM_ReceiveMessagesOnChannel(NM, CH_CTL, msgs, 64)) > 0) {
        for (int i = 0; i < n; i++) {
            int sz = msg_size(msgs[i]);
            const uint8_t *md = msg_data(msgs[i]);
            /* control messages are small, except the game file list ('M', sent in pieces) */
            if (sz > 0 && (sz < 1024 || (md[0] == 'M' && sz <= MF_CHUNK + 8))) on_ctl(msg_sender(msgs[i]), md, sz);
            S.Msg_Release(msgs[i]);
        }
    }
    for (int ch = CH_GAME; ch <= CH_TCP; ch += 2) {
        while ((n = S.NM_ReceiveMessagesOnChannel(NM, ch, msgs, 64)) > 0) {
            for (int i = 0; i < n; i++) {
                void *m = msgs[i];
                u64 from = msg_sender(m);
                const uint8_t *d = msg_data(m);
                int sz = msg_size(m);
                Peer *p = find_peer(from);
                if (!p || !p->welcomed) {
                    if (g_is_host && !p && !allowed_peer(from)) note_ignored(from);
                } else if (sz > 0 && sz <= (ch == CH_GAME ? MAX_GAME_PKT + HDR : TCP_CHUNK + 16) && rate_ok(&p->rate, now_ms())) {
                    p->last_heard = now_ms();
                    if (!g_is_host) g_host_last = now_ms();
                    if (ch == CH_GAME) deliver_udp(p, d, sz);
                    else on_tcp_msg(p, d, sz);
                }
                S.Msg_Release(m);
            }
        }
    }
}

/* Live stats, once a second: packets per second each way and packet loss
   (an average over the last few seconds). */
static void stats_sample(Peer *p, u64 t)
{
    if (!p->st_at) { p->st_at = t; p->st_down = p->down; p->st_up = p->up; p->st_lost = p->lost; return; }
    if (t - p->st_at < 1000) return;
    unsigned ms = (unsigned)(t - p->st_at);
    u64 din = p->down - p->st_down, dout = p->up - p->st_up, dlost = p->lost - p->st_lost;
    p->pps_in = (int)((unsigned)din * 1000u / ms);
    p->pps_out = (int)((unsigned)dout * 1000u / ms);
    if (din + dlost) {
        double l = 100.0 * (double)dlost / (double)(din + dlost);
        p->loss_avg = p->have_loss ? p->loss_avg * 0.7 + l * 0.3 : l;
        p->have_loss = true;
    }
    p->st_at = t; p->st_down = p->down; p->st_up = p->up; p->st_lost = p->lost;
}

static void peer_tick(Peer *p, u64 t)
{
    if (!p->welcomed) return;
    stats_sample(p, t);
    poll_aliases(p);
    poll_tcp(p);
    if (t - p->last_ka >= KEEPALIVE_MS) {
        uint8_t k[9] = { 'K' };
        memcpy(k + 1, &t, 8);
        send_ctl(p->id, k, 9);
        p->last_ka = t;
    }
    if (t - p->last_ports_sent >= 5000) send_ports(p);
    if (G.running && p->rrunning && !p->down && !p->warned_silent && p->first_up_ms && t - p->first_up_ms > 20000) {
        p->warned_silent = true;
        say("! Your game is sending to %s's game, but nothing comes back yet.", who(p->id));
        say("  %s the LAN game first: %s.", g_is_host ? "Make sure you created" : "Ask the host to create", HOST_MENU);
        say("  Click \"Details\" for more. The file scctlink.log has more.");
    }
}

/* ---------- starting the game ---------- */

static void start_game(void)
{
    if (game_is_running(g_exe)) { say("Splinter Cell is already running."); return; }
    if (!g_versus) {
        open_url("steam://run/13570");
        say("Starting Splinter Cell: Chaos Theory through Steam...");
        say("(Ubisoft Connect / other copies: start it yourself.)");
        return;
    }
    /* Versus is its own program: <game folder>\Versus\System\SCCT_Versus.exe */
    char dir[600] = "", sys[700], exe[760];
    if (g_steam_ok && AP && S.Apps_GetAppInstallDir && S.Apps_GetAppInstallDir(AP, SCCT_APPID, dir, sizeof dir) > 0) {
        sfmt(sys, sizeof sys, "%s" PATHSEP "Versus" PATHSEP "System", dir);
        sfmt(exe, sizeof exe, "%s" PATHSEP "SCCT_Versus.exe", sys);
        if (file_exists(exe) && run_program(exe, "", sys)) { say("Starting Versus (SCCT_Versus.exe)..."); return; }
    }
    say("Couldn't find Versus. Start SCCT_Versus.exe yourself (in the game's Versus\\System folder).");
}

/* ---------- modes ---------- */

static bool steam_start(void);

static void set_versus(bool v)
{
    g_versus = v;
    g_exe = g_exe_opt ? g_exe_opt : v ? "SCCT_Versus" : "splintercell3";
    g_max_players = g_max_players_opt ? g_max_players_opt : v ? 4 : 2;
    if (g_max_players > MAX_PEERS + 1) g_max_players = MAX_PEERS + 1;
    g_scan_at = 0;
    g_was_running = false;
}

static void reset_all(void)
{
    for (int i = 0; i < g_npeers; i++) peer_sockets_close(&g_peers[i]);
    g_npeers = 0;
    for (int i = 0; i < g_ncatch; i++)
        for (int j = 0; j < g_catch[i].ns; j++) sock_close(g_catch[i].s[j]);
    g_ncatch = 0;
    g_nignored = 0;
    g_joining = 0;
    g_nrejected = 0;
    g_last_found_n = -1;
    g_refresh = 0;
    memset(g_ans, 0, sizeof g_ans);
    g_lobby = g_host = 0;
    g_host_warned = false;
    g_ports_sig = 0;
    g_nkicked = 0;
}

static void stop_mode(bool quiet)
{
    if (g_mode == MODE_IDLE) return;
    ring_dump("Stop");
    g_ncmp = 0;
    if (g_nmfs >= 0) mods_publish();
    for (int i = 0; i < g_npeers; i++) send_ctl1(g_peers[i].id, 'B');
    for (int i = 0; i < 20; i++) { S.MD_RunFrame(g_pipe); sleep_ms(10); } /* flush goodbyes */
    if (g_lobby) S.MM_LeaveLobby(MM, g_lobby);
    if (g_joining) S.MM_LeaveLobby(MM, g_joining);
    S.Friends_ClearRichPresence(F);
    reset_all();
    g_mode = MODE_IDLE;
    g_is_host = false;
    ui_list(LIST_FRIENDS_ONLINE, 0, 0);
    ui_list(LIST_HOSTING, 0, 0);
    if (!quiet) say("Stopped.");
}

static void start_mode(int mode)
{
    if (g_mode == mode) return;
    stop_mode(true);
    if (!g_steam_ok && !(g_steam_ok = steam_start())) return;
    g_mode = mode;
    g_is_host = mode == MODE_HOST;
    scan_game(2);
    say("");
    if (g_is_host) {
        say("Creating a friends-only %s lobby for %d players...", MODE_NAME, g_max_players);
        S.MM_CreateLobby(MM, LOBBY_FRIENDS_ONLY, g_max_players);
    } else {
        say("Looking for a friend who is hosting %s...", MODE_NAME);
    }
}

static void make_status(UiState *st)
{
    memset(st, 0, sizeof *st);
    st->mode = g_mode;
    st->versus = g_versus;
    st->lobby = g_lobby;
    /* "How to" steps, one bit each */
    if (g_is_host) {
        bool linked = false, playing = false;
        for (int i = 0; i < g_npeers; i++) {
            linked |= g_peers[i].welcomed;
            playing |= g_peers[i].down > 50 && g_peers[i].up > 50;
        }
        st->done = (uint8_t)((g_lobby ? 1 : 0) | (G.running && G.nudp ? 2 : 0) | (linked ? 4 : 0) | (playing ? 8 : 0));
    } else if (g_mode == MODE_JOIN) {
        Peer *h = host_peer();
        bool linked = h && h->welcomed, up = linked && h->rrunning && h->nrudp, playing = h && h->down > 50 && h->up > 50;
        st->done = (uint8_t)((linked ? 1 : 0) | (up ? 2 : 0) | (G.running && linked ? 4 : 0) | (playing ? 8 : 0));
    }
    size_t sk = 0;
    for (int i = 0; i < g_npeers && sk < sizeof st->stats - 80; i++) {
        Peer *p = &g_peers[i];
        if (!p->welcomed) continue;
        char ping[16] = "-", loss[16] = "-";
        if (p->have_rtt) sfmt(ping, sizeof ping, "%d ms", (int)(p->rtt + 0.5));
        if (p->have_loss) { int l = (int)(p->loss_avg * 10 + 0.5); sfmt(loss, sizeof loss, "%d.%d%%", l / 10, l % 10); }
        sfmt(st->stats + sk, sizeof st->stats - sk, "%s%s   ping %s   loss %s   %d/%d pkt/s", sk ? "\n" : "",
             who(p->id), ping, loss, p->pps_in, p->pps_out);
        sk += strlen(st->stats + sk);
    }
    if (g_mode == MODE_IDLE) {
        sfmt(st->status, sizeof st->status, "Ready (%s). Host a game, or join a friend.", MODE_NAME);
    } else if (g_is_host) {
        int playing = 0;
        for (int i = 0; i < g_npeers; i++) playing += g_peers[i].down > 0 && g_peers[i].up > 0;
        if (!g_lobby) sfmt(st->status, sizeof st->status, "Creating lobby...");
        else if (!g_npeers) sfmt(st->status, sizeof st->status, "Hosting %s - waiting for friends to join", MODE_NAME);
        else sfmt(st->status, sizeof st->status, "Hosting %s - %d friend(s) linked, %d in game", MODE_NAME, g_npeers, playing);
    } else {
        Peer *h = host_peer();
        if (g_joining) sfmt(st->status, sizeof st->status, "Joining lobby...");
        else if (!g_lobby) sfmt(st->status, sizeof st->status, "Looking for a friend who is hosting %s...", MODE_NAME);
        else if (!h || !h->welcomed) sfmt(st->status, sizeof st->status, "Reaching %s through Steam...", who(g_host));
        else if (h->down > 50 && h->up > 50) sfmt(st->status, sizeof st->status, "Playing with %s", who(g_host));
        else if (!h->rrunning) sfmt(st->status, sizeof st->status, "Linked - waiting for %s to start the game", who(g_host));
        else if (!G.running) sfmt(st->status, sizeof st->status, "Linked to %s - start the game now", who(g_host));
        else sfmt(st->status, sizeof st->status, "Linked to %s - join from the game's LAN list", who(g_host));
    }
}

static void handle_cmd(const Cmd *c)
{
    switch (c->kind) {
    case CMD_HOST: start_mode(MODE_HOST); break;
    case CMD_JOIN: start_mode(MODE_JOIN); break;
    case CMD_JOIN_LOBBY:
        if (!c->arg) { say("! That isn't a lobby code."); break; }
        if (g_mode != MODE_JOIN) start_mode(MODE_JOIN);
        if (g_mode == MODE_JOIN) { g_joining = 0; request_join(c->arg); }
        break;
    case CMD_LAUNCH:
        if (!g_no_launch) start_game();
        break;
    case CMD_REFRESH:
        if (g_is_host) { say("You are hosting. Refresh is for finding a friend who hosts."); break; }
        if (g_mode != MODE_JOIN) start_mode(MODE_JOIN);
        if (g_mode != MODE_JOIN) break;
        if (g_lobby || g_joining) { say("Already linked. Click Stop first to look for someone else."); break; }
        say("Refreshing...");
        g_nrejected = 0;
        g_last_found_n = -1;
        for (int i = 0; i < NANS; i++) g_ans[i].asked = 0;
        g_refresh = 1;
        break;
    case CMD_KICK: kick_peer(c->arg); break;
    case CMD_STATUS: status(false); break;
    case CMD_DETAILS: status(true); ring_dump("Details"); break;
    case CMD_RECHECK:
        if (!g_steam_ok) { say("Steam isn't running, so the game's folder can't be found."); break; }
        mf_recheck();
        say("Game files checked again.");
        break;
    case CMD_APPLY_SETTINGS: apply_settings(c->arg); break;
    case CMD_SCCTFIX: scctfix(c->arg != 0); break;
    case CMD_MATCH_FRIEND: match_friend(); break;
    case CMD_SET_VERSUS:
        if (g_mode != MODE_IDLE) { say("Click Stop first, then switch between Co-op and Versus."); break; }
        if ((c->arg != 0) != g_versus) {
            set_versus(c->arg != 0);
            say("Mode: %s (%s, up to %d players).", MODE_NAME, g_versus ? "SCCT_Versus.exe" : "the co-op campaign", g_max_players);
        }
        break;
    case CMD_STOP: stop_mode(false); break;
    case CMD_QUIT: g_quit = 1; break;
    default: break;
    }
}

void engine_run(const EngineOpts *o, Cmd first)
{
    g_no_launch = o->no_launch;
    g_verbose = o->verbose;
    g_t0 = now_ms();
    g_max_players_opt = o->max_players;
    if (o->exe && o->exe[0]) {
        static char ex[64];
        sfmt(ex, sizeof ex, "%s", o->exe);
        char *dot = strstr(ex, ".exe");
        if (dot) *dot = 0;
        if (ex[0]) g_exe_opt = ex;
    }
    set_versus(o->versus);
    log_line("----");
    say("SCCTLink " VERSION " - Splinter Cell: Chaos Theory co-op and Versus over Steam (no VPN, no port forwarding)");
    g_my_text_len = primary_text_len();
    /* Connect to Steam right away, so an invite accepted on the start screen
       reaches SCCTLink. If Steam isn't running yet, Host/Join retries. */
    g_steam_ok = steam_start();
    if (g_steam_ok) mf_make(); /* the "Mods" tab shows this PC's game right away, game running or not */
    if (first.kind != CMD_NONE) handle_cmd(&first);

    u64 last_scan = 0, last_friends = 0, last_state = 0;
    UiState prev;
    memset(&prev, 0xff, sizeof prev);
    int prev_mode = -1;
    while (!g_quit) {
        if (g_steam_ok) {
            pump_callbacks();
            drain_steam();
        }
        u64 t = now_ms();
        if (g_mode != prev_mode) { last_scan = 0; last_friends = 0; prev_mode = g_mode; }
        if (g_mode != MODE_IDLE) {
            scan_game(0);
            u64 sig = ports_signature();
            if (sig != g_ports_sig) {
                g_ports_sig = sig;
                for (int i = 0; i < g_npeers; i++) {
                    Peer *p = &g_peers[i];
                    if (!p->welcomed) continue;
                    send_ports(p);
                    for (int k = 0; k < p->nrudp; k++) alias_get(p, p->rudp[k], true);
                }
                catchers_update();
                note("game ports changed: %d UDP, %d TCP listening", G.nudp, G.ntcpl);
                if (!G.nudp) ring_dump("the game closed its network ports");
            }
            poll_catchers();
            for (int i = 0; i < g_npeers; i++) {
                Peer *p = &g_peers[i];
                peer_tick(p, t);
                if (g_is_host && t - p->last_heard > PEER_TIMEOUT_MS) { drop_peer(p->id, "timed out"); i--; }
            }
        }
        if (g_mode == MODE_HOST) {
            if (g_lobby && (!last_friends || t - last_friends > 1000)) { publish_connected(); last_friends = t; }
        } else if (g_mode == MODE_JOIN) {
            Peer *h = host_peer();
            if (g_lobby && h) {
                if (!h->welcomed && t - g_last_hello > 2000) send_hello();
                if (t - g_host_last > HOST_QUIET_MS && !g_host_warned) {
                    g_host_warned = true;
                    say("! No answer from %s for 15 s. Is their SCCTLink still open?", who(g_host));
                } else if (t - g_host_last < HOST_QUIET_MS && g_host_warned) {
                    g_host_warned = false;
                    say("%s is back.", who(g_host));
                }
            }
            if (!g_lobby && !g_joining && g_refresh == 1) {
                scan_for_host(false);
                g_refresh = 2;                 /* answers to direct questions take a moment */
                g_refresh_at = last_scan = now_ms();
            } else if (!g_lobby && !g_joining && g_refresh == 2 && t - g_refresh_at > 1500) {
                if (scan_for_host(false) == 0)
                    say("Nobody is hosting right now. When your friend clicks \"Host a game\", click Refresh again.");
                g_refresh = 0;
                last_scan = now_ms();
            } else if (!g_lobby && !g_joining && (!last_scan || t - last_scan > 3000)) {
                scan_for_host(last_scan == 0);
                last_scan = now_ms();
            }
            if (g_lobby || g_joining) g_refresh = 0;
        }
        Cmd c;
        while (ui_poll(&c)) handle_cmd(&c);

        if (t - last_state > 200) {
            UiState st;
            make_status(&st);
            if (memcmp(&st, &prev, sizeof st) != 0) { ui_state(&st); prev = st; }
            last_state = t;
        }

        sock_t socks[256];
        int ns = 0;
        for (int i = 0; i < g_npeers; i++) {
            Peer *p = &g_peers[i];
            for (int k = 0; k < p->nals && ns < 256; k++) socks[ns++] = p->als[k].s;
            for (int k = 0; k < p->nlis && ns < 256; k++) socks[ns++] = p->lis[k].s;
            for (int k = 0; k < p->nst && ns < 256; k++) socks[ns++] = p->st[k].s;
        }
        for (int i = 0; i < g_ncatch; i++)
            for (int j = 0; j < g_catch[i].ns && ns < 256; j++) socks[ns++] = g_catch[i].s[j];
        wait_socks(socks, ns, g_mode == MODE_IDLE ? 20 : 1);
    }
    if (g_steam_ok) {
        stop_mode(true);
        S.Shutdown();
    }
    say("SCCTLink closed.");
}

/* ---------- start-up ---------- */

static bool steam_start(void)
{
    file_write_text("steam_appid.txt", "480");
    const char *e = steam_load();
    if (e) { say("! %s", e); return false; }
    if (!S.IsSteamRunning()) { say("! Steam is not running. Start Steam, log in, then try again."); return false; }
    char err[1024] = { 0 };
    int r = S.InitFlat(err);
    if (r != 0) {
        say("! Steam would not start the connection (code %d): %s", r, err);
        say("  Make sure Steam is running and logged in (not in offline mode).");
        return false;
    }
    S.MD_Init();
    g_pipe = S.GetHSteamPipe();
    U = S.SteamUser();
    F = S.SteamFriends();
    MM = S.SteamMatchmaking();
    NM = S.SteamNetMsgs();
    NU = S.SteamNetUtils();
    AP = S.SteamApps ? S.SteamApps() : 0;
    if (!U || !F || !MM || !NM || !NU) { say("! Steam interfaces unavailable."); return false; }
    if (!S.User_BLoggedOn(U)) { say("! Steam is in offline mode. Go online and try again."); S.Shutdown(); return false; }
    g_me = S.User_GetSteamID(U);
    S.NU_InitRelayNetworkAccess(NU);
    /* Always through Valve's relay: never share either player's IP address
       for a direct connection, whatever Steam's own "IP sharing" setting is. */
    if (!S.NU_SetGlobalConfigInt32 || !S.NU_SetGlobalConfigInt32(NU, 104 /* P2P_Transport_ICE_Enable */, 0 /* off */))
        say("! Steam would not turn off direct connections; your IP may be shared with the other player.");
    say("Signed in to Steam as %s.", who(g_me));
    return true;
}
