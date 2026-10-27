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
 * The game is not modified. Pattern: coop-anything HXCoop relay,
 * playbook/networking-steam-spacewar.md, and the author's CoFLink. MIT license. */
#include "steam.h"
#include "plat.h"

#define VERSION "0.1"
#define SCCT_APPID 13570
#define SPACEWAR_APPID 480
#define MAX_PEERS 3
#define BUF_SIZE 65536
#define CH_GAME 0
#define CH_CTL 1
#define CH_TCP 2
#define PROTO '1'
#define KEEPALIVE_MS 1000
#define PEER_TIMEOUT_MS 30000
#define HOST_QUIET_MS 15000
#define MAX_GAME_PKT 8192
#define MAX_PKTS_PER_SEC 4000
#define MAX_ALIAS 64
#define MAX_CATCH 64
#define MAX_STREAMS 32
#define TCP_CHUNK 16000
#define TCP_OUTBUF_MAX (8 << 20)
#define HDR 7 /* kind, src port, dst port, seq */

static void *U, *F, *MM, *NM, *NU;
static int32_t g_pipe;
static u64 g_me, g_lobby, g_host, g_joining;
static bool g_is_host;
static int g_max_players = 2;
static bool g_no_launch, g_auto_yes, g_verbose;
static const char *g_exe = "splintercell3";
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
    printf("%s\n", b);
    fflush(0);
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
    if (g_verbose) { printf("  . %s\n", b); fflush(0); }
    char l[1220];
    sfmt(l, sizeof l, "  . %s", b);
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
    o[i] = 0;
    return o;
}

static const char *who(u64 id)
{
    if (id == g_me) return clean(S.Friends_GetPersonaName(F));
    const char *n = S.Friends_GetFriendPersonaName(F, id);
    return (n && *n && strcmp(n, "[unknown]") != 0) ? clean(n) : idstr(id);
}

static bool ask_yes(const char *question)
{
    if (g_auto_yes) return true;
    printf("%s (Y/N): ", question);
    fflush(0);
    char line[16];
    if (!read_line(line, sizeof line)) return false;
    return line[0] == 'y' || line[0] == 'Y';
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

/* Replace every copy of `from` (4 raw bytes, dotted text, UTF-16 text) with `to`. */
static int swap_ip(uint8_t *p, int n, uint32_t from, uint32_t to)
{
    int hits = 0;
    uint8_t f[4], t[4];
    memcpy(f, &from, 4);
    memcpy(t, &to, 4);
    for (int i = 0; i + 4 <= n; i++)
        if (p[i] == f[0] && !memcmp(p + i, f, 4)) { memcpy(p + i, t, 4); hits++; i += 3; }
    char ft[16], tt[16];
    ip_text(from, ft);
    ip_text(to, tt);
    int L = (int)strlen(ft);
    if ((int)strlen(tt) != L) return hits;
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
    u64 up, down, lost, reordered, swapped, dropped_noport;
    uint16_t tx_seq, rx_seq; bool rx_started;
    double rtt; bool have_rtt;
    Rate rate;
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
    for (int i = 0; i < p->nals; i++) sock_close(p->als[i].s);
    for (int i = 0; i < p->nlis; i++) sock_close(p->lis[i].s);
    while (p->nst) stream_close(p, 0, false);
    p->nals = p->nlis = 0;
}

static void drop_peer(u64 id, const char *why)
{
    for (int i = 0; i < g_npeers; i++) {
        if (g_peers[i].id != id) continue;
        say("- %s disconnected (%s).", who(id), why);
        peer_sockets_close(&g_peers[i]);
        close_session(id);
        g_peers[i] = g_peers[--g_npeers];
        return;
    }
}

/* Who may reach the host's game: only people in the lobby who are also on the
   host's own Steam friends list. Everyone else is dropped. */
static bool allowed_peer(u64 id)
{
    return id && id != g_me && in_lobby(id) && S.Friends_HasFriend(F, id, FRIEND_IMMEDIATE);
}

static void note_ignored(u64 id)
{
    for (int i = 0; i < g_nignored; i++)
        if (g_ignored[i] == id) return;
    if (g_nignored < 16) g_ignored[g_nignored++] = id;
    if (in_lobby(id))
        say("! Blocked %s: they are in the lobby but not on your Steam friends list.", who(id));
    else
        say("! Blocked traffic from %s: they are not in your lobby.", who(id));
}

/* ---------- UDP: alias sockets and broadcast catchers ---------- */

static Alias *alias_get(Peer *p, uint16_t port, bool create)
{
    for (int i = 0; i < p->nals; i++) if (p->als[i].port == port) return &p->als[i];
    if (!create || p->nals >= MAX_ALIAS || !p->alias) return 0;
    sock_t s = udp_open(p->alias, port, UDP_REUSE);
    if (s == BADSOCK) {
        static uint16_t warned[64]; static int nw;
        bool seen = false;
        for (int i = 0; i < nw; i++) seen |= warned[i] == port;
        if (!seen && nw < 64) {
            warned[nw++] = port;
            say("! Could not open UDP port %u on %s (another program uses it).", (unsigned)port, ip_kind(p->alias));
        }
        return 0;
    }
    Alias *a = &p->als[p->nals++];
    a->port = port; a->s = s; a->pkts = 0;
    note("UDP %s:%u stands for %s's game port %u", ip_kind(p->alias), (unsigned)port, who(p->id), (unsigned)port);
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
    if (!udp_busy(port)) {
        sock_t s = udp_open(0, port, UDP_BROADCAST | UDP_REUSE);
        if (s != BADSOCK) { c->s[c->ns++] = s; note("listening for LAN broadcasts on port %u", (unsigned)port); return; }
    }
    const Bound *b = game_udp(port);
    if (b && b->ip == 0) {
        /* Our own game uses the port on all addresses: listen on each LAN
           address only (Windows allows a specific address next to "all"). */
        for (int i = 0; i < g_nmy_ips && c->ns < 8; i++) {
            sock_t s = udp_open(g_my_ips[i], port, UDP_BROADCAST | UDP_REUSE);
            if (s != BADSOCK) c->s[c->ns++] = s;
        }
        if (c->ns) { note("listening for LAN broadcasts on port %u (per LAN address)", (unsigned)port); return; }
    }
    if (b || udp_busy(port)) {
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

/* Payload from the local game, going to a peer: hide this PC's LAN addresses. */
static int hide_my_ips(Peer *p, uint8_t *d, int n)
{
    int hits = 0;
    for (int i = 0; i < g_nmy_ips; i++) hits += swap_ip(d, n, g_my_ips[i], p->my_alias);
    if (hits && !p->swapped) say("(The game put your LAN address in a packet. SCCTLink replaced it, so %s never sees it.)", who(p->id));
    p->swapped += (u64)hits;
    return hits;
}

/* Payload from a peer: their idea of this PC's address becomes the real one. */
static void unhide_my_ip(Peer *p, uint8_t *d, int n)
{
    if (g_nmy_ips && p->my_alias) swap_ip(d, n, p->my_alias, g_my_ips[0]);
}

static void send_udp_frame(Peer *p, char kind, uint16_t src, uint16_t dst, const uint8_t *d, int n)
{
    if (n <= 0 || n > MAX_GAME_PKT) return;
    g_out[0] = (uint8_t)kind;
    put16(g_out + 1, src);
    put16(g_out + 3, dst);
    put16(g_out + 5, p->tx_seq++);
    memcpy(g_out + HDR, d, (size_t)n);
    hide_my_ips(p, g_out + HDR, n);
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
        return;
    }
    Alias *a = alias_get(p, src, true);
    if (!a) return;
    int len = n - HDR;
    memcpy(g_buf, m + HDR, (size_t)len);
    unhide_my_ip(p, g_buf, len);
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
            hide_my_ips(p, g_out + 5, r);
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
        unhide_my_ip(p, g_buf, len);
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
        if (p->rrunning)
            say("%s's Splinter Cell is running (%d UDP / %d TCP ports).", who(p->id), nu, nt);
        else
            say("%s has not started Splinter Cell yet.", who(p->id));
    }
    p->ports_known = true;
    for (int i = 0; i < nu; i++) alias_get(p, p->rudp[i], true);
    listeners_update(p);
    catchers_update();
}

/* ---------- join side state ---------- */

static u64 g_host_last, g_last_hello, g_launch_ms;
static bool g_host_warned, g_reminded;
static u64 g_rejected[16];
static int g_nrejected;

static Peer *host_peer(void) { return g_npeers ? &g_peers[0] : 0; }

static void reset_join_state(void)
{
    if (g_npeers) { peer_sockets_close(&g_peers[0]); g_npeers = 0; }
    g_lobby = g_host = 0;
    g_host_warned = false;
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
    say("Joining lobby %s ...", idstr(lobby));
    S.MM_JoinLobby(MM, lobby);
}

static void send_hello(void)
{
    uint8_t m[3] = { 'H', PROTO, (uint8_t)g_my_text_len };
    send_ctl(g_host, m, 3);
    g_last_hello = now_ms();
}

static void launch_game(void)
{
    say("");
    say("=== Now start Splinter Cell: Chaos Theory ===");
    if (!g_no_launch) {
        char url[80];
        sfmt(url, sizeof url, "steam://run/%d", SCCT_APPID);
        open_url(url);
        say("Starting it through Steam. (Ubisoft Connect / other copies: start it yourself.)");
    }
    say("In the game: Co-op -> LAN -> Join game. The host's game shows up in the LAN list.");
    say("If Windows Firewall asks about Splinter Cell or SCCTLink, you can press Cancel:");
    say("SCCTLink only uses this PC's own loopback addresses, nothing is opened to the internet.");
    say("Keys:  S = status   D = details   Q = quit");
    say("");
    g_launch_ms = now_ms();
}

static void on_lobby_entered(u64 lobby, uint32_t response)
{
    if (g_is_host || lobby == g_lobby) return; /* LobbyEnter can arrive twice */
    g_joining = 0;
    if (response != 1) {
        const char *why = response == 2 ? "it doesn't exist any more" :
                          response == 3 ? "you are not allowed to join" :
                          response == 4 ? "it is full" : "Steam refused";
        say("! Could not join that lobby: %s.", why);
        return;
    }
    const char *tag = S.MM_GetLobbyData(MM, lobby, "scctlink");
    if (!tag || strcmp(tag, "1") != 0) {
        say("! That lobby is not a Splinter Cell co-op game (SCCTLink). Leaving it.");
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
    S.Friends_SetRichPresence(F, "status", "Playing Splinter Cell Chaos Theory co-op (SCCTLink)");
    send_hello();
}

/* ---------- host: lobby + invites ---------- */

static void on_lobby_created(int32_t result, u64 lobby)
{
    if (!g_is_host || g_lobby) return; /* arrives twice on real Steam */
    if (result != 1) {
        say("! Steam could not create a lobby (error %d). Check your internet and restart SCCTLink.", result);
        g_quit = 1;
        return;
    }
    g_lobby = lobby;
    char conn[64];
    sfmt(conn, sizeof conn, "+connect_lobby %s", idstr(lobby));
    S.MM_SetLobbyData(MM, lobby, "scctlink", "1");
    S.MM_SetLobbyData(MM, lobby, "scctlink_ver", VERSION);
    S.MM_SetLobbyJoinable(MM, lobby, true);
    S.Friends_SetRichPresence(F, "status", "Hosting Splinter Cell Chaos Theory co-op (SCCTLink)");
    S.Friends_SetRichPresence(F, "connect", conn);
    clipboard_set(idstr(lobby));
    say("");
    say("=== Lobby is ready (id %s, copied to clipboard) ===", idstr(lobby));
    say("1. Start Splinter Cell: Chaos Theory (press G here to start it through Steam).");
    say("   Co-op -> LAN -> Create game, as if your friend sat on your home network.");
    say("2. Tell your friend to run Join-SCCT.bat. It finds you on its own.");
    say("   If it doesn't: press I here to send them a Steam invite, or send them");
    say("   this lobby id, which they paste after pressing L in their window: %s", idstr(lobby));
    say("If Windows Firewall asks about Splinter Cell or SCCTLink, you can press Cancel.");
    say("Keys:  I = invite   G = start game   S = status   D = details   Q = quit");
    say("");
}

static void invite_menu(void)
{
    u64 ids[200];
    int n = 0, total = S.Friends_GetFriendCount(F, FRIEND_IMMEDIATE);
    say("Friends online:");
    for (int i = 0; i < total && n < 200; i++) {
        u64 id = S.Friends_GetFriendByIndex(F, i, FRIEND_IMMEDIATE);
        if (S.Friends_GetFriendPersonaState(F, id) == 0) continue;
        ids[n++] = id;
        say("  %2d. %s", n, who(id));
    }
    if (!n) { say("  (nobody is online)"); return; }
    printf("Number to invite (Enter = cancel): ");
    fflush(0);
    char line[32];
    if (!read_line(line, sizeof line) || !line[0]) { say("Cancelled."); return; }
    int k = atoi(line);
    if (k < 1 || k > n) { say("No friend with that number."); return; }
    if (S.MM_InviteUserToLobby(MM, g_lobby, ids[k - 1]))
        say("Invite sent to %s. They must run Join-SCCT.bat, then accept it.", who(ids[k - 1]));
    else
        say("! Steam would not send the invite.");
}

static void status(bool details)
{
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
        for (int k = 0; k < p->nals; k++) say("  UDP %u  (%s packets in)", (unsigned)p->als[k].port, idstr(p->als[k].pkts));
        for (int k = 0; k < p->nlis; k++) say("  TCP %u", (unsigned)p->lis[k].port);
        say("  open TCP streams: %d. Addresses hidden: %s.", p->nst, idstr(p->swapped));
    }
    for (int i = 0; i < g_ncatch; i++)
        say("LAN broadcast port %u: %s (%s caught)", (unsigned)g_catch[i].port,
            g_catch[i].ns ? "listening" : "in use, skipped", idstr(g_catch[i].pkts));
}

/* ---------- callbacks ---------- */

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
                S.MM_LeaveLobby(MM, g_lobby);
                reset_join_state();
            }
        }
        break;
    }
    case CB_GameLobbyJoinRequested:
        if (size >= 16) {
            say("Accepted invite from %s.", who(rd64(p + 8)));
            request_join(rd64(p));
        }
        break;
    case CB_GameRichPresenceJoinRequested: {
        if (size < 9) break;
        char conn[257] = { 0 };
        memcpy(conn, p + 8, (size_t)(size - 8 < 256 ? size - 8 : 256));
        const char *c = strstr(conn, "+connect_lobby ");
        if (c) request_join(str_to_u64(c + 15));
        break;
    }
    case CB_NetMsgSessionRequest: {
        u64 from = S.Ident_GetSteamID64(p);
        /* host: accept, every message is checked against the lobby + friends list */
        bool ok = g_is_host ? true : from == g_host;
        if (ok) S.NM_AcceptSessionWithUser(NM, p);
        break;
    }
    case CB_NetMsgSessionFailed:
        say("(Steam: a connection attempt failed; it will retry.)");
        break;
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

static void scan_for_host(bool verbose)
{
    static u64 noted[32];
    static int nnoted;
    u64 lobs[32], ids[32];
    int n = 0, total = S.Friends_GetFriendCount(F, FRIEND_IMMEDIATE);
    for (int i = 0; i < total && n < 32; i++) {
        u64 id = S.Friends_GetFriendByIndex(F, i, FRIEND_IMMEDIATE);
        uint8_t info[32] = { 0 };
        if (!S.Friends_GetFriendGamePlayed(F, id, info)) continue;
        u64 game = rd64(info), lob = rd64(info + 16);
        if ((game & 0xFFFFFF) != SPACEWAR_APPID) continue;
        if (!lob) {
            S.Friends_RequestFriendRichPresence(F, id);
            const char *rp = S.Friends_GetFriendRichPresence(F, id, "connect");
            const char *c = rp ? strstr(rp, "+connect_lobby ") : 0;
            if (c) lob = str_to_u64(c + 15);
        }
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
        ids[n] = id;
        lobs[n++] = lob;
    }
    if (n == 1) {
        char q[160];
        sfmt(q, sizeof q, "%s is hosting. Join their game?", who(ids[0]));
        if (ask_yes(q)) request_join(lobs[0]);
        else if (g_nrejected < 16) { g_rejected[g_nrejected++] = lobs[0]; say("OK, not joining. Still looking..."); }
    } else if (n > 1) {
        say("Several friends are hosting:");
        for (int i = 0; i < n; i++) say("  %d. %s", i + 1, who(ids[i]));
        printf("Number to join: ");
        fflush(0);
        char line[32];
        int k = read_line(line, sizeof line) ? atoi(line) : 0;
        if (k >= 1 && k <= n) request_join(lobs[k - 1]);
    } else if (verbose) {
        say("No friend is hosting yet. Waiting...");
        say("  Or: accept the host's Steam invite, or press L and paste their lobby id.");
    }
}

/* ---------- control messages ---------- */

static void on_ctl(u64 from, const uint8_t *m, int n)
{
    if (n < 1) return;
    char c = (char)m[0];
    Peer *p = find_peer(from);
    if (g_is_host) {
        if (c == 'B') { if (p) drop_peer(from, "quit SCCTLink"); return; }
        if (!p) {
            if (!allowed_peer(from)) { note_ignored(from); return; }
            p = add_peer(from);
            if (!p) { say("! %s tried to join, but the game is full.", who(from)); return; }
        }
        p->last_heard = now_ms();
        if (c == 'H') {
            if (n < 2 || m[1] != PROTO) {
                say("! %s runs a different SCCTLink version. Both of you need the same version (" VERSION ").", who(from));
                return;
            }
            int their_len = n >= 3 ? m[2] : 12;
            if (!p->welcomed) {
                p->alias = make_alias(their_len, p->slot + 1);
                p->my_alias = make_alias(g_my_text_len, 0);
                p->welcomed = true;
                say("+ %s connected through Steam. Their game shows up on your PC at %s.", who(from), ip_kind(p->alias));
            }
            uint8_t w[10] = { 'W', PROTO };
            memcpy(w + 2, &p->alias, 4);    /* the joiner's address on the host's PC */
            memcpy(w + 6, &p->my_alias, 4); /* the host's address on the joiner's PC */
            send_ctl(from, w, 10);
            send_ports(p);
            for (int i = 0; i < p->nrudp; i++) alias_get(p, p->rudp[i], true);
            listeners_update(p);
        }
    } else {
        if (from != g_host || !p) return;
        g_host_last = now_ms();
        if (c == 'W' && n >= 10 && !p->welcomed) {
            memcpy(&p->my_alias, m + 2, 4);
            memcpy(&p->alias, m + 6, 4);
            if (!is_loop(p->alias) || !is_loop(p->my_alias)) { say("! The host sent a bad address. Ignoring."); return; }
            p->welcomed = true;
            say("Connected to %s through Steam. Their game shows up on your PC at %s.", who(g_host), ip_kind(p->alias));
            send_ports(p);
            for (int i = 0; i < p->nrudp; i++) alias_get(p, p->rudp[i], true);
            listeners_update(p);
            launch_game();
        } else if (c == 'B') {
            say("! %s closed SCCTLink.", who(g_host));
            S.MM_LeaveLobby(MM, g_lobby);
            reset_join_state();
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
            if (sz > 0 && sz < 1024) on_ctl(msg_sender(msgs[i]), msg_data(msgs[i]), sz);
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

static void peer_tick(Peer *p, u64 t)
{
    if (!p->welcomed) return;
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
        say("  %s", g_is_host ? "Make sure you created the co-op game: Co-op -> LAN -> Create game."
                              : "Ask the host to create the co-op game (Co-op -> LAN -> Create game).");
        say("  Press D for details. The file scctlink.log has more.");
    }
}

static void relay_loop(void)
{
    u64 last_scan = 0;
    while (!g_quit) {
        pump_callbacks();
        drain_steam();
        scan_game(false);
        u64 sig = ports_signature();
        if (sig != g_ports_sig) {
            g_ports_sig = sig;
            for (int i = 0; i < g_npeers; i++) if (g_peers[i].welcomed) send_ports(&g_peers[i]);
            catchers_update();
            note("game ports changed: %d UDP, %d TCP listening", G.nudp, G.ntcpl);
        }
        poll_catchers();
        u64 t = now_ms();
        for (int i = 0; i < g_npeers; i++) {
            Peer *p = &g_peers[i];
            peer_tick(p, t);
            if (g_is_host && t - p->last_heard > PEER_TIMEOUT_MS) { drop_peer(p->id, "timed out"); i--; }
        }
        if (!g_is_host) {
            if (g_lobby && host_peer()) {
                Peer *h = host_peer();
                if (!h->welcomed && t - g_last_hello > 2000) send_hello();
                if (t - g_host_last > HOST_QUIET_MS && !g_host_warned) {
                    g_host_warned = true;
                    say("! No answer from %s for 15 s. Is their SCCTLink still open?", who(g_host));
                } else if (t - g_host_last < HOST_QUIET_MS && g_host_warned) {
                    g_host_warned = false;
                    say("%s is back.", who(g_host));
                }
                if (g_launch_ms && !G.running && !g_reminded && t - g_launch_ms > 90000) {
                    g_reminded = true;
                    say("Splinter Cell isn't running yet. Start it, then Co-op -> LAN -> Join game.");
                }
            }
            if (!g_lobby && !g_joining && t - last_scan > 3000) {
                scan_for_host(last_scan == 0);
                last_scan = now_ms();
            }
        }
        int key = key_poll();
        if (key == 'q' || key == 'Q') g_quit = 1;
        if (g_is_host && g_lobby && (key == 'i' || key == 'I')) invite_menu();
        if (key == 's' || key == 'S') status(false);
        if (key == 'd' || key == 'D') status(true);
        if (g_is_host && (key == 'g' || key == 'G')) {
            char url[80];
            sfmt(url, sizeof url, "steam://run/%d", SCCT_APPID);
            open_url(url);
            say("Starting Splinter Cell through Steam...");
        }
        if (!g_is_host && !g_lobby && (key == 'l' || key == 'L')) {
            printf("Paste the host's lobby id (right-click or Ctrl+V), then Enter: ");
            fflush(0);
            char line[64];
            u64 id = read_line(line, sizeof line) ? str_to_u64(line) : 0;
            if (id) { g_joining = 0; request_join(id); }
            else say("That isn't a lobby id. Press L to try again.");
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
        wait_socks(socks, ns, 1);
    }
}

/* ---------- start-up and shut-down ---------- */

static bool steam_start(void)
{
    file_write_text("steam_appid.txt", "480");
    const char *e = steam_load();
    if (e) { say("! %s", e); return false; }
    if (!S.IsSteamRunning()) { say("! Steam is not running. Start Steam, log in, then run this again."); return false; }
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
    if (!U || !F || !MM || !NM || !NU) { say("! Steam interfaces unavailable."); return false; }
    if (!S.User_BLoggedOn(U)) { say("! Steam is in offline mode. Go online and try again."); return false; }
    g_me = S.User_GetSteamID(U);
    S.NU_InitRelayNetworkAccess(NU);
    say("Signed in to Steam as %s.", S.Friends_GetPersonaName(F));
    return true;
}

static void shutdown_all(void)
{
    for (int i = 0; i < g_npeers; i++) send_ctl1(g_peers[i].id, 'B');
    for (int i = 0; i < 20; i++) { S.MD_RunFrame(g_pipe); sleep_ms(10); } /* flush goodbyes */
    if (g_lobby) S.MM_LeaveLobby(MM, g_lobby);
    S.Friends_ClearRichPresence(F);
    for (int i = 0; i < g_npeers; i++) peer_sockets_close(&g_peers[i]);
    for (int i = 0; i < g_ncatch; i++) for (int j = 0; j < g_catch[i].ns; j++) sock_close(g_catch[i].s[j]);
    S.Shutdown();
    say("SCCTLink closed.");
}

static void usage(void)
{
    say("SCCTLink " VERSION " - Splinter Cell: Chaos Theory co-op over Steam (no VPN, no port forwarding)");
    say("  SCCTLink host [--players 2]          host a game for a friend");
    say("  SCCTLink join [lobby id] [--no-launch] join a friend");
    say("  options: --exe <name>  game file name if it isn't splintercell3.exe");
    say("           --verbose     show network details in this window");
}

int main(int argc, char **argv)
{
    plat_init();
    int mode = 0; /* 1 host, 2 join */
    u64 join_lobby = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "host")) mode = 1;
        else if (!strcmp(a, "join")) mode = 2;
        else if (!strcmp(a, "--players") && i + 1 < argc) {
            g_max_players = atoi(argv[++i]);
            if (g_max_players < 2 || g_max_players > MAX_PEERS + 1) g_max_players = 2;
        } else if (!strcmp(a, "--exe") && i + 1 < argc) {
            static char ex[64];
            sfmt(ex, sizeof ex, "%s", argv[++i]);
            char *dot = strstr(ex, ".exe");
            if (dot) *dot = 0;
            if (ex[0]) g_exe = ex;
        } else if (!strcmp(a, "--no-launch")) g_no_launch = true;
        else if (!strcmp(a, "--yes")) g_auto_yes = true;
        else if (!strcmp(a, "--verbose")) g_verbose = true;
        else if (!strcmp(a, "+connect_lobby") && i + 1 < argc) { mode = 2; join_lobby = str_to_u64(argv[++i]); }
        else if (a[0] >= '0' && a[0] <= '9') join_lobby = str_to_u64(a);
        else if (!strcmp(a, "-h") || !strcmp(a, "--help") || !strcmp(a, "/?")) { usage(); return 0; }
    }
    if (!mode) {
        usage();
        printf("\n1 = host a game, 2 = join a friend: ");
        fflush(0);
        char line[16];
        if (!read_line(line, sizeof line)) return 1;
        mode = line[0] == '1' ? 1 : line[0] == '2' ? 2 : 0;
        if (!mode) return 1;
    }
    g_is_host = mode == 1;
    set_title(g_is_host ? "SCCTLink " VERSION " - hosting Splinter Cell co-op" : "SCCTLink " VERSION " - joining Splinter Cell co-op");
    log_line("----");
    say("SCCTLink " VERSION " (%s)", g_is_host ? "host" : "join");
    g_my_text_len = primary_text_len();
    if (!steam_start()) return 1;
    scan_game(true);

    if (g_is_host) {
        say("Creating a friends-only lobby for %d players...", g_max_players);
        S.MM_CreateLobby(MM, LOBBY_FRIENDS_ONLY, g_max_players);
    } else if (join_lobby) {
        request_join(join_lobby);
    } else {
        say("Looking for a friend who is hosting...");
    }
    relay_loop();
    shutdown_all();
    return 0;
}
