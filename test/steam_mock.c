/* steam_mock.c - fake Steam for testing SCCTLink on one Linux machine.
 * Each process is one "Steam user" (env MOCK_ID). Messages go over Unix
 * datagram sockets in MOCK_DIR, so the two players can sit in separate network
 * namespaces (separate "PCs"). Lobbies, invites and "friend is playing"
 * live as small files in MOCK_DIR. Like real Steam:
 *  - the first message from a stranger is held and raises SessionRequest (1251)
 *    until AcceptSessionWithUser; sending to someone accepts them implicitly;
 *  - CreateLobby gives LobbyCreated twice (call result + broadcast) then LobbyEnter;
 *  - JoinLobby gives LobbyEnter as a call result; members joining/leaving raise
 *    LobbyChatUpdate on the others;
 *  - an invite raises GameLobbyJoinRequested (333) on the friend.
 * MOCK_LOSS=N drops N% of unreliable messages; MOCK_DELAY_MS=N delays every message. */
#define _GNU_SOURCE
#include "../src/steam.h"
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/file.h>
#include <time.h>
#include <unistd.h>

SteamApi S;

static u64 me;
static int sock = -1;
static char dir[512];
static int loss, delay_ms;
static u64 accepted[64];
static int naccepted;
static u64 requested[64];
static int nrequested;
static u64 my_lobby;
static int ice_off = -1; /* SetGlobalConfigValueInt32(P2P_Transport_ICE_Enable) */
static u64 next_call = 1000;

typedef struct Msg { void *data; int size; u64 sender; int ch; u64 due; struct Msg *next; } Msg;
static Msg *held, *ready[4], *delayed;
static u64 mono_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (u64)t.tv_sec * 1000 + (u64)t.tv_nsec / 1000000; }

typedef struct Cb { int id; uint8_t p[300]; int size; u64 call; struct Cb *next; } Cb;
static Cb *cbq, *cur, *results;

static void addr_of(u64 id, struct sockaddr_un *a) { memset(a, 0, sizeof *a); a->sun_family = AF_UNIX; snprintf(a->sun_path, sizeof a->sun_path, "%s/s%llu", dir, (unsigned long long)id); }

static void push_cb(int id, const void *p, int size, u64 call)
{
    Cb *c = calloc(1, sizeof *c);
    c->id = id; memcpy(c->p, p, (size_t)size); c->size = size; c->call = call;
    Cb **t = &cbq;
    while (*t) t = &(*t)->next;
    *t = c;
}

static void push_result(int id, const void *p, int size, u64 call)
{
    Cb *r = calloc(1, sizeof *r);
    r->id = id; memcpy(r->p, p, (size_t)size); r->size = size; r->call = call;
    r->next = results; results = r;
    uint8_t done[16] = { 0 };
    memcpy(done, &call, 8);
    int32_t cbid = id; uint32_t sz = (uint32_t)size;
    memcpy(done + 8, &cbid, 4); memcpy(done + 12, &sz, 4);
    push_cb(703, done, 16, 0);
}

/* ---- lobby files: "owner <id>", "member <id>", "data <k> <v>" lines ---- */

typedef struct { u64 owner; u64 mem[16]; int nmem; char k[8][32], v[8][64]; int nd; bool exists; } Lobby;

/* Lobby files are shared by several test processes: every read-modify-write
   happens under a lock, as it would on Steam's servers (from CoFLink 0.3). */
static int lock_fd = -1;
static void lobby_lock(void)
{
    if (lock_fd < 0) {
        char p[600]; snprintf(p, sizeof p, "%s/lobby.lock", dir);
        lock_fd = open(p, O_CREAT | O_RDWR, 0600);
    }
    if (lock_fd >= 0) flock(lock_fd, LOCK_EX);
}
static void lobby_unlock(void) { if (lock_fd >= 0) flock(lock_fd, LOCK_UN); }

static void lobby_path(u64 l, char *out) { snprintf(out, 600, "%s/lobby_%llu", dir, (unsigned long long)l); }

static Lobby lobby_read(u64 l)
{
    Lobby L = { 0 };
    char p[600]; lobby_path(l, p);
    FILE *f = fopen(p, "r");
    if (!f) return L;
    L.exists = true;
    char line[200];
    while (fgets(line, sizeof line, f)) {
        unsigned long long id;
        char k[32], v[64];
        if (sscanf(line, "owner %llu", &id) == 1) L.owner = id;
        else if (sscanf(line, "member %llu", &id) == 1 && L.nmem < 16) L.mem[L.nmem++] = id;
        else if (sscanf(line, "data %31s %63s", k, v) == 2 && L.nd < 8) { strcpy(L.k[L.nd], k); strcpy(L.v[L.nd++], v); }
    }
    fclose(f);
    return L;
}

static void lobby_write(u64 l, const Lobby *L)
{
    char p[600], tmp[610]; lobby_path(l, p);
    snprintf(tmp, sizeof tmp, "%s.%llu.tmp", p, (unsigned long long)me);
    FILE *f = fopen(tmp, "w");
    fprintf(f, "owner %llu\n", (unsigned long long)L->owner);
    for (int i = 0; i < L->nmem; i++) fprintf(f, "member %llu\n", (unsigned long long)L->mem[i]);
    for (int i = 0; i < L->nd; i++) fprintf(f, "data %s %s\n", L->k[i], L->v[i]);
    fclose(f);
    rename(tmp, p);
}

static void playing_write(u64 l)
{
    char p[600]; snprintf(p, sizeof p, "%s/playing_%llu", dir, (unsigned long long)me);
    if (!l) { unlink(p); return; }
    FILE *f = fopen(p, "w"); fprintf(f, "%llu\n", (unsigned long long)l); fclose(f);
}

static Lobby last_seen;

static void watch_lobby(void)
{
    if (!my_lobby) return;
    Lobby L = lobby_read(my_lobby);
    for (int i = 0; i < last_seen.nmem; i++) { /* who left? */
        bool still = false;
        for (int j = 0; j < L.nmem; j++) still |= L.mem[j] == last_seen.mem[i];
        if (!still) {
            uint8_t u[32] = { 0 }; uint32_t st = 2;
            memcpy(u, &my_lobby, 8); memcpy(u + 8, &last_seen.mem[i], 8); memcpy(u + 16, &last_seen.mem[i], 8); memcpy(u + 24, &st, 4);
            push_cb(506, u, 32, 0);
        }
    }
    for (int j = 0; j < L.nmem; j++) { /* who joined? */
        bool was = false;
        for (int i = 0; i < last_seen.nmem; i++) was |= L.mem[j] == last_seen.mem[i];
        if (!was && last_seen.exists) {
            uint8_t u[32] = { 0 }; uint32_t st = 1;
            memcpy(u, &my_lobby, 8); memcpy(u + 8, &L.mem[j], 8); memcpy(u + 16, &L.mem[j], 8); memcpy(u + 24, &st, 4);
            push_cb(506, u, 32, 0);
        }
    }
    last_seen = L;
}

static void check_invites(void)
{
    char p[600]; snprintf(p, sizeof p, "%s/invite_%llu", dir, (unsigned long long)me);
    FILE *f = fopen(p, "r");
    if (!f) return;
    unsigned long long l = 0, from = 0;
    if (fscanf(f, "%llu %llu", &l, &from) == 2) {
        uint8_t u[16]; u64 L = l, F = from;
        memcpy(u, &L, 8); memcpy(u + 8, &F, 8);
        push_cb(333, u, 16, 0);
    }
    fclose(f);
    unlink(p);
}

/* ---- networking ---- */

static bool is_accepted(u64 id)
{
    for (int i = 0; i < naccepted; i++) if (accepted[i] == id) return true;
    return false;
}

static void accept_id(u64 id)
{
    if (is_accepted(id)) return;
    if (naccepted < 64) accepted[naccepted++] = id;
    Msg **pp = &held; /* release held messages */
    while (*pp) {
        Msg *m = *pp;
        if (m->sender == id) {
            *pp = m->next; m->next = 0;
            Msg **t = &ready[m->ch]; while (*t) t = &(*t)->next; *t = m;
        } else pp = &m->next;
    }
}

static void take(Msg *m)
{
    if (is_accepted(m->sender)) {
        Msg **t = &ready[m->ch]; while (*t) t = &(*t)->next; *t = m;
    } else {
        m->next = held; held = m;
        bool asked = false;
        for (int i = 0; i < nrequested; i++) asked |= requested[i] == m->sender;
        if (!asked && nrequested < 64) {
            requested[nrequested++] = m->sender;
            uint8_t ident[IDENT_SIZE] = { 0 };
            S.Ident_SetSteamID64(ident, m->sender);
            push_cb(1251, ident, 136, 0);
        }
    }
}

typedef struct Out { struct sockaddr_un a; int n; struct Out *next; uint8_t b[]; } Out;
static Out *outq;
static int noutq;
static void out_flush(void)
{
    while (outq) {
        if (sendto(sock, outq->b, (size_t)outq->n, MSG_DONTWAIT, (void *)&outq->a, sizeof outq->a) < 0 && errno == EAGAIN) return;
        Out *o = outq; outq = o->next; free(o); noutq--; /* sent (or the receiver is gone) */
    }
}

static void poll_net(void)
{
    out_flush();
    static uint8_t b[70000];
    ssize_t r;
    while ((r = recv(sock, b, sizeof b, 0)) > 0) {
        if (r < 18) continue;
        Msg *m = calloc(1, sizeof *m);
        m->ch = b[0] < 4 ? b[0] : 0;
        memcpy(&m->sender, b + 2, 8);
        memcpy(&m->due, b + 10, 8);
        m->due += (u64)delay_ms;
        m->size = (int)r - 18;
        m->data = malloc((size_t)m->size + 1);
        memcpy(m->data, b + 18, (size_t)m->size);
        Msg **t = &delayed; while (*t) t = &(*t)->next; *t = m; /* keep arrival order */
    }
    u64 now = mono_ms();
    while (delayed && delayed->due <= now) { Msg *m = delayed; delayed = m->next; m->next = 0; take(m); }
}

/* ---- the API ---- */

static int m_InitFlat(char *err)
{
    const char *id = getenv("MOCK_ID");
    const char *d = getenv("MOCK_DIR");
    if (!id || !d) { strcpy(err, "MOCK_ID/MOCK_DIR not set"); return 1; }
    me = strtoull(id, 0, 10);
    snprintf(dir, sizeof dir, "%s", d);
    loss = getenv("MOCK_LOSS") ? atoi(getenv("MOCK_LOSS")) : 0;
    delay_ms = getenv("MOCK_DELAY_MS") ? atoi(getenv("MOCK_DELAY_MS")) : 0;
    srand((unsigned)me);
    sock = socket(AF_UNIX, SOCK_DGRAM, 0);
    struct sockaddr_un a;
    addr_of(me, &a);
    unlink(a.sun_path);
    int big = 8 << 20;
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &big, sizeof big);
    if (bind(sock, (void *)&a, sizeof a) != 0) { strcpy(err, "mock socket busy"); return 1; }
    fcntl(sock, F_SETFL, O_NONBLOCK);
    return 0;
}
static bool m_IsSteamRunning(void) { return true; }
static void m_Shutdown(void) { if (ice_off == 1) printf("[mock] direct connections were off\n"); playing_write(0); struct sockaddr_un a; addr_of(me, &a); unlink(a.sun_path); }
static int32_t m_GetHSteamPipe(void) { return 1; }
static void m_MD_Init(void) {}
static void m_MD_RunFrame(int32_t p) { (void)p; poll_net(); watch_lobby(); check_invites(); }
static bool m_MD_GetNextCallback(int32_t p, CallbackMsg *m)
{
    (void)p;
    if (!cbq) return false;
    cur = cbq; cbq = cbq->next;
    m->user = 1; m->callback = cur->id; m->param = cur->p; m->size = cur->size;
    return true;
}
static void m_MD_FreeLastCallback(int32_t p) { (void)p; free(cur); cur = 0; }
static bool m_MD_GetAPICallResult(int32_t p, u64 call, void *buf, int size, int cb, bool *failed)
{
    (void)p; (void)cb;
    for (Cb **r = &results; *r; r = &(*r)->next) {
        if ((*r)->call != call) continue;
        Cb *x = *r; *r = x->next;
        memcpy(buf, x->p, (size_t)(size < x->size ? size : x->size));
        *failed = false;
        free(x);
        return true;
    }
    *failed = true;
    return false;
}
static int iface;
static void *m_iface(void) { return &iface; }
static u64 m_GetSteamID(void *s) { (void)s; return me; }
static bool m_BLoggedOn(void *s) { (void)s; return true; }
static const char *m_GetPersonaName(void *s) { (void)s; static char n[32]; snprintf(n, 32, "user%llu", (unsigned long long)me); return n; }

static u64 friends[16];
static int nfriends = -1;
static void load_friends(void)
{
    if (nfriends >= 0) return;
    nfriends = 0;
    const char *f = getenv("MOCK_FRIENDS");
    while (f && *f && nfriends < 16) {
        friends[nfriends++] = strtoull(f, (char **)&f, 10);
        if (*f == ',') f++;
    }
}
static int m_GetFriendCount(void *s, int fl) { (void)s; (void)fl; load_friends(); return nfriends; }
static u64 m_GetFriendByIndex(void *s, int i, int fl) { (void)s; (void)fl; return friends[i]; }
static int m_GetFriendPersonaState(void *s, u64 id) { (void)s; (void)id; return getenv("MOCK_FRIENDS_LOOK_OFFLINE") ? 0 : 1; }
static const char *m_GetFriendPersonaName(void *s, u64 id)
{
    (void)s; static char n[4][32]; static int k; char *o = n[k++ & 3];
    snprintf(o, 32, "user%llu", (unsigned long long)id); return o;
}
static bool m_GetFriendGamePlayed(void *s, u64 id, void *info)
{
    (void)s;
    if (getenv("MOCK_NO_GAMEINFO")) return false; /* friend's game details are private */
    char p[600]; snprintf(p, sizeof p, "%s/playing_%llu", dir, (unsigned long long)id);
    FILE *f = fopen(p, "r");
    if (!f) return false;
    unsigned long long l = 0;
    int ok = fscanf(f, "%llu", &l);
    fclose(f);
    if (ok != 1) return false;
    u64 game = 480, L = getenv("MOCK_HIDE_LOBBY") ? 0 : l;
    if (getenv("MOCK_FRIEND_APP")) { game = strtoull(getenv("MOCK_FRIEND_APP"), 0, 10); L = 0; } /* in another game */
    memset(info, 0, 24);
    memcpy(info, &game, 8);
    memcpy((char *)info + 16, &L, 8);
    return true;
}
static bool m_HasFriend(void *s, u64 id, int fl) { (void)s; (void)fl; load_friends(); for (int i = 0; i < nfriends; i++) if (friends[i] == id) return true; return false; }
static bool m_SetRichPresence(void *s, const char *k, const char *v)
{
    (void)s;
    if (strcmp(k, "connect") && strcmp(k, "scctlink_lobby")) return true;
    char p[600]; snprintf(p, sizeof p, "%s/rp_%s_%llu", dir, k, (unsigned long long)me);
    FILE *f = fopen(p, "w"); fputs(v, f); fclose(f);
    return true;
}
static const char *m_GetFriendRichPresence(void *s, u64 id, const char *k)
{
    (void)s;
    static char v[256];
    v[0] = 0;
    if ((strcmp(k, "connect") && strcmp(k, "scctlink_lobby")) || getenv("MOCK_HIDE_RP")) return v;
    char p[600]; snprintf(p, sizeof p, "%s/rp_%s_%llu", dir, k, (unsigned long long)id);
    FILE *f = fopen(p, "r");
    if (f) { if (!fgets(v, sizeof v, f)) v[0] = 0; fclose(f); }
    return v;
}
static void m_RequestFriendRichPresence(void *s, u64 id) { (void)s; (void)id; }
static void m_ClearRichPresence(void *s)
{
    (void)s;
    char p[600]; snprintf(p, sizeof p, "%s/rp_connect_%llu", dir, (unsigned long long)me);
    unlink(p);
    snprintf(p, sizeof p, "%s/rp_scctlink_lobby_%llu", dir, (unsigned long long)me);
    unlink(p);
}

static u64 m_CreateLobby(void *s, int type, int max)
{
    (void)s; (void)type; (void)max;
    u64 l = 0x0186000000000000ULL + me * 10 + (u64)(rand() % 10); /* like real lobby ids */
    Lobby L = { 0 };
    L.owner = me; L.mem[L.nmem++] = me; L.exists = true;
    lobby_write(l, &L);
    my_lobby = l; last_seen = L;
    playing_write(l);
    u64 call = next_call++;
    uint8_t c[16] = { 0 }; int32_t ok = 1;
    memcpy(c, &ok, 4); memcpy(c + 8, &l, 8);
    push_result(513, c, 16, call);
    push_cb(513, c, 16, 0); /* real Steam broadcasts it too */
    uint8_t e[24] = { 0 }; uint32_t resp = 1;
    memcpy(e, &l, 8); memcpy(e + 16, &resp, 4);
    push_cb(504, e, 24, 0);
    return call;
}
static u64 m_JoinLobby(void *s, u64 l)
{
    (void)s;
    u64 call = next_call++;
    lobby_lock();
    Lobby L = lobby_read(l);
    uint32_t resp = 2;
    if (L.exists && L.nmem < 16) {
        L.mem[L.nmem++] = me;
        lobby_write(l, &L);
        my_lobby = l; last_seen = L;
        playing_write(l);
        resp = 1;
    }
    lobby_unlock();
    uint8_t e[24] = { 0 };
    memcpy(e, &l, 8); memcpy(e + 16, &resp, 4);
    push_result(504, e, 24, call);
    return call;
}
static void m_LeaveLobby(void *s, u64 l)
{
    (void)s;
    lobby_lock();
    Lobby L = lobby_read(l);
    if (L.exists) {
        if (L.owner == me) { char p[600]; lobby_path(l, p); unlink(p); }
        else {
            for (int i = 0; i < L.nmem; i++) if (L.mem[i] == me) { L.mem[i] = L.mem[--L.nmem]; break; }
            lobby_write(l, &L);
        }
    }
    lobby_unlock();
    if (my_lobby == l) { my_lobby = 0; memset(&last_seen, 0, sizeof last_seen); }
    playing_write(0);
}
static bool m_SetLobbyData(void *s, u64 l, const char *k, const char *v)
{
    (void)s;
    lobby_lock();
    Lobby L = lobby_read(l);
    if (!L.exists || L.nd >= 8) { lobby_unlock(); return false; }
    snprintf(L.k[L.nd], 32, "%s", k);
    snprintf(L.v[L.nd], 64, "%s", *v ? v : "-");
    for (char *c = L.v[L.nd]; *c; c++) if (*c == ' ') *c = '_';
    L.nd++;
    lobby_write(l, &L);
    lobby_unlock();
    return true;
}
static const char *m_GetLobbyData(void *s, u64 l, const char *k)
{
    (void)s;
    static char v[64];
    Lobby L = lobby_read(l);
    for (int i = 0; i < L.nd; i++) if (!strcmp(L.k[i], k)) { strcpy(v, L.v[i]); return v; }
    return "";
}
static u64 m_GetLobbyOwner(void *s, u64 l) { (void)s; return lobby_read(l).owner; }
static int m_GetNumLobbyMembers(void *s, u64 l) { (void)s; return lobby_read(l).nmem; }
static u64 m_GetLobbyMemberByIndex(void *s, u64 l, int i) { (void)s; Lobby L = lobby_read(l); return i < L.nmem ? L.mem[i] : 0; }
static bool m_InviteUserToLobby(void *s, u64 l, u64 user)
{
    (void)s;
    if (getenv("MOCK_INVITES_LOST")) return true; /* Steam says sent, the friend can't use it */
    char p[600]; snprintf(p, sizeof p, "%s/invite_%llu", dir, (unsigned long long)user);
    FILE *f = fopen(p, "w"); fprintf(f, "%llu %llu\n", (unsigned long long)l, (unsigned long long)me); fclose(f);
    return true;
}
static bool m_SetLobbyJoinable(void *s, u64 l, bool j) { (void)s; (void)l; (void)j; return true; }

static int m_SendMessageToUser(void *s, const void *ident, const void *data, uint32_t n, int flags, int ch)
{
    (void)s;
    u64 to = S.Ident_GetSteamID64((void *)ident);
    accept_id(to); /* sending accepts the session, as in real Steam */
    if (!(flags & SEND_RELIABLE) && loss && rand() % 100 < loss) return 1;
    static uint8_t b[70000];
    if (n > sizeof b - 18) return 2;
    b[0] = (uint8_t)ch; b[1] = 0;
    memcpy(b + 2, &me, 8);
    u64 t = mono_ms();
    memcpy(b + 10, &t, 8);
    memcpy(b + 18, data, n);
    struct sockaddr_un a;
    addr_of(to, &a);
    /* Like Steam: messages wait in a send queue when the other side is busy,
       instead of blocking the caller (two busy senders would lock each other
       up). The queue is sent in order, on every later call. */
    out_flush();
    if (!outq && sendto(sock, b, n + 18, MSG_DONTWAIT, (void *)&a, sizeof a) >= 0) return 1;
    if (noutq > 50000) return 25; /* k_EResultLimitExceeded, like a full Steam send buffer */
    Out *o = malloc(sizeof *o + n + 18);
    o->a = a; o->n = (int)n + 18; o->next = 0;
    memcpy(o->b, b, (size_t)o->n);
    Out **q = &outq; while (*q) q = &(*q)->next; *q = o;
    noutq++;
    return 1;
}
static int m_ReceiveMessagesOnChannel(void *s, int ch, void **out, int max)
{
    (void)s;
    poll_net();
    int n = 0;
    while (ready[ch] && n < max) { out[n++] = ready[ch]; ready[ch] = ready[ch]->next; }
    return n;
}
static bool m_AcceptSessionWithUser(void *s, const void *ident) { (void)s; accept_id(S.Ident_GetSteamID64((void *)ident)); return true; }
static bool m_CloseSessionWithUser(void *s, const void *ident)
{
    (void)s;
    u64 id = S.Ident_GetSteamID64((void *)ident);
    for (int i = 0; i < naccepted; i++) if (accepted[i] == id) { accepted[i] = accepted[--naccepted]; break; }
    for (int i = 0; i < nrequested; i++) if (requested[i] == id) { requested[i] = requested[--nrequested]; break; }
    return true;
}
/* identity: int32 type (16 = SteamID), int32 size, u64 id at 8 */
static void m_SetSteamID64(void *id, u64 v) { int32_t t = 16, sz = 8; memcpy(id, &t, 4); memcpy((char *)id + 4, &sz, 4); memcpy((char *)id + 8, &v, 8); }
static u64 m_GetSteamID64(void *id) { int32_t t; memcpy(&t, id, 4); if (t != 16) return 0; u64 v; memcpy(&v, (char *)id + 8, 8); return v; }
static void m_Release(void *m) { Msg *x = m; free(x->data); free(x); }
static int m_InitRelay(void *s) { (void)s; return 1; }
static bool m_SetGlobalConfigInt32(void *s, int value, int32_t v) { (void)s; if (value == 104) ice_off = v == 0; return true; }
static uint32_t m_GetAppInstallDir(void *s, uint32_t app, char *buf, uint32_t n)
{
    (void)s; (void)app;
    const char *d = getenv("MOCK_GAME_DIR");
    if (!d || !n) return 0;
    snprintf(buf, n, "%s", d);
    return (uint32_t)strlen(buf) + 1;
}

const char *steam_load(void)
{
    S.InitFlat = m_InitFlat; S.IsSteamRunning = m_IsSteamRunning; S.Shutdown = m_Shutdown;
    S.GetHSteamPipe = m_GetHSteamPipe; S.MD_Init = m_MD_Init; S.MD_RunFrame = m_MD_RunFrame;
    S.MD_GetNextCallback = m_MD_GetNextCallback; S.MD_FreeLastCallback = m_MD_FreeLastCallback;
    S.MD_GetAPICallResult = m_MD_GetAPICallResult;
    S.SteamUser = S.SteamFriends = S.SteamMatchmaking = S.SteamNetMsgs = S.SteamNetUtils = S.SteamApps = m_iface;
    S.Apps_GetAppInstallDir = m_GetAppInstallDir;
    S.User_GetSteamID = m_GetSteamID; S.User_BLoggedOn = m_BLoggedOn;
    S.Friends_GetPersonaName = m_GetPersonaName; S.Friends_GetFriendCount = m_GetFriendCount;
    S.Friends_GetFriendByIndex = m_GetFriendByIndex; S.Friends_GetFriendPersonaState = m_GetFriendPersonaState;
    S.Friends_GetFriendPersonaName = m_GetFriendPersonaName; S.Friends_GetFriendGamePlayed = m_GetFriendGamePlayed;
    S.Friends_HasFriend = m_HasFriend; S.Friends_GetFriendRichPresence = m_GetFriendRichPresence; S.Friends_RequestFriendRichPresence = m_RequestFriendRichPresence; S.Friends_SetRichPresence = m_SetRichPresence; S.Friends_ClearRichPresence = m_ClearRichPresence;
    S.MM_CreateLobby = m_CreateLobby; S.MM_JoinLobby = m_JoinLobby; S.MM_LeaveLobby = m_LeaveLobby;
    S.MM_SetLobbyData = m_SetLobbyData; S.MM_GetLobbyData = m_GetLobbyData; S.MM_GetLobbyOwner = m_GetLobbyOwner;
    S.MM_GetNumLobbyMembers = m_GetNumLobbyMembers; S.MM_GetLobbyMemberByIndex = m_GetLobbyMemberByIndex;
    S.MM_InviteUserToLobby = m_InviteUserToLobby; S.MM_SetLobbyJoinable = m_SetLobbyJoinable;
    S.NM_SendMessageToUser = m_SendMessageToUser; S.NM_ReceiveMessagesOnChannel = m_ReceiveMessagesOnChannel;
    S.NM_AcceptSessionWithUser = m_AcceptSessionWithUser; S.NM_CloseSessionWithUser = m_CloseSessionWithUser;
    S.Ident_SetSteamID64 = m_SetSteamID64; S.Ident_GetSteamID64 = m_GetSteamID64;
    S.Msg_Release = m_Release; S.NU_InitRelayNetworkAccess = m_InitRelay; S.NU_SetGlobalConfigInt32 = m_SetGlobalConfigInt32;
    return 0;
}

const void *msg_data(void *m) { return ((Msg *)m)->data; }
int msg_size(void *m) { return ((Msg *)m)->size; }
u64 msg_sender(void *m) { return ((Msg *)m)->sender; }
