/* plat_win.c - Windows side of plat.h. Win32 functions are declared here by
 * hand (no Windows SDK), imported from kernel32/ws2_32/user32/shell32.
 * winmm.dll and iphlpapi.dll are loaded from System32 only, at run time. */
#include "plat.h"

#define WINAPI __attribute__((stdcall))
#define IMP __declspec(dllimport)
typedef void *HANDLE;
typedef int BOOL;
typedef unsigned int UINT;
typedef unsigned long DWORD;
typedef uintptr_t SOCKET;

IMP void WINAPI Sleep(DWORD ms);
IMP u64 WINAPI GetTickCount64(void);
IMP BOOL WINAPI SetConsoleCtrlHandler(BOOL(WINAPI *h)(DWORD), BOOL add);
IMP DWORD WINAPI GetModuleFileNameA(HANDLE mod, char *buf, DWORD n);
IMP BOOL WINAPI SetCurrentDirectoryA(const char *dir);
IMP HANDLE WINAPI GlobalAlloc(UINT flags, size_t n);
IMP void *WINAPI GlobalLock(HANDLE h);
IMP BOOL WINAPI GlobalUnlock(HANDLE h);
IMP BOOL WINAPI SetConsoleTitleA(const char *t);
IMP BOOL WINAPI SetConsoleOutputCP(UINT cp);
IMP void *WINAPI LoadLibraryExA(const char *path, void *file, DWORD flags);
IMP void *WINAPI GetProcAddress(void *mod, const char *name);
IMP HANDLE WINAPI OpenProcess(DWORD access, BOOL inherit, DWORD pid);
IMP BOOL WINAPI QueryFullProcessImageNameA(HANDLE h, DWORD flags, char *name, DWORD *size);
IMP BOOL WINAPI CloseHandle(HANDLE h);
IMP DWORD WINAPI GetCurrentProcessId(void);
#define LOAD_LIBRARY_SEARCH_SYSTEM32 0x800
#define PROCESS_QUERY_LIMITED_INFORMATION 0x1000
IMP BOOL WINAPI OpenClipboard(HANDLE w);
IMP BOOL WINAPI EmptyClipboard(void);
IMP HANDLE WINAPI SetClipboardData(UINT fmt, HANDLE h);
IMP BOOL WINAPI CloseClipboard(void);
IMP HANDLE WINAPI ShellExecuteA(HANDLE w, const char *op, const char *file, const char *params, const char *dir, int show);

struct sockaddr_in4 { uint16_t family; uint16_t port; uint32_t addr; uint8_t zero[8]; };
typedef struct { UINT count; SOCKET fd[256]; } fd_set_w; /* select() reads `count` entries */
struct timeval_w { long sec; long usec; };
struct hostent_w { char *name; char **aliases; short type; short len; char **addrs; };
IMP int WINAPI WSAStartup(uint16_t ver, void *data);
IMP SOCKET WINAPI socket(int af, int type, int proto);
IMP int WINAPI bind(SOCKET s, const void *addr, int len);
IMP int WINAPI connect(SOCKET s, const void *addr, int len);
IMP int WINAPI listen(SOCKET s, int backlog);
IMP SOCKET WINAPI accept(SOCKET s, void *addr, int *len);
IMP int WINAPI send(SOCKET s, const void *p, int n, int flags);
IMP int WINAPI recv(SOCKET s, void *p, int n, int flags);
IMP int WINAPI sendto(SOCKET s, const void *p, int n, int flags, const void *to, int tolen);
IMP int WINAPI recvfrom(SOCKET s, void *p, int n, int flags, void *from, int *fromlen);
IMP int WINAPI select(int nfds, fd_set_w *r, fd_set_w *w, fd_set_w *e, const struct timeval_w *t);
IMP int WINAPI closesocket(SOCKET s);
IMP int WINAPI ioctlsocket(SOCKET s, long cmd, unsigned long *arg);
IMP int WINAPI getsockname(SOCKET s, void *addr, int *len);
IMP int WINAPI setsockopt(SOCKET s, int level, int opt, const void *val, int len);
IMP int WINAPI gethostname(char *name, int len);
IMP struct hostent_w *WINAPI gethostbyname(const char *name);
IMP int WINAPI WSAGetLastError(void);
IMP int WINAPI WSAIoctl(SOCKET s, DWORD code, void *in, DWORD inlen, void *out, DWORD outlen, DWORD *ret, void *ov, void *cb);

int _kbhit(void);
int _getch(void);
char *_ui64toa(u64 v, char *buf, int radix);
u64 _strtoui64(const char *s, char **end, int radix);

#define AF_INET 2
#define SOCK_STREAM 1
#define SOCK_DGRAM 2
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17
#define SOL_SOCKET 0xffff
#define SO_REUSEADDR 4
#define SO_BROADCAST 0x20
#define SO_RCVBUF 0x1002
#define TCP_NODELAY 1
#define FIONBIO ((long)0x8004667E)
#define SIO_UDP_CONNRESET 0x9800000C
#define WSAEWOULDBLOCK 10035
#define WSAECONNRESET 10054
#define WSAEMSGSIZE 10040
#define INVALID ((SOCKET)~(uintptr_t)0)

volatile int g_quit;
static char g_exe_dir[600];

static uint16_t be16(uint16_t v) { return (uint16_t)((v >> 8) | (v << 8)); }

static BOOL WINAPI on_ctrl(DWORD ev)
{
    g_quit = 1;
    if (ev == 2 /* close */ || ev >= 5 /* logoff/shutdown */)
        Sleep(2000); /* give the main loop time to leave the lobby */
    return 1;
}

typedef DWORD(WINAPI *GetTable_t)(void *table, DWORD *size, BOOL order, unsigned long af, int cls, unsigned long reserved);
static GetTable_t pGetExtendedUdpTable, pGetExtendedTcpTable;

void plat_init(void)
{
    static uint8_t wsa[512];
    WSAStartup(0x0202, wsa);
    /* 1 ms timer resolution, so relayed packets are not held for Windows'
       default 15.6 ms tick (that would add lag and hurt sync). System DLLs
       are loaded from System32 only, never from the download folder. */
    void *winmm = LoadLibraryExA("winmm.dll", 0, LOAD_LIBRARY_SEARCH_SYSTEM32);
    UINT(WINAPI * tbp)(UINT) = winmm ? (UINT(WINAPI *)(UINT))GetProcAddress(winmm, "timeBeginPeriod") : 0;
    if (tbp) tbp(1);
    void *iph = LoadLibraryExA("iphlpapi.dll", 0, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (iph) {
        pGetExtendedUdpTable = (GetTable_t)GetProcAddress(iph, "GetExtendedUdpTable");
        pGetExtendedTcpTable = (GetTable_t)GetProcAddress(iph, "GetExtendedTcpTable");
    }
    SetConsoleCtrlHandler(on_ctrl, 1);
    SetConsoleOutputCP(65001);
    DWORD n = GetModuleFileNameA(0, g_exe_dir, sizeof g_exe_dir - 1);
    g_exe_dir[n] = 0;
    for (int i = (int)n - 1; i >= 0; i--)
        if (g_exe_dir[i] == '\\' || g_exe_dir[i] == '/') { g_exe_dir[i] = 0; break; }
    SetCurrentDirectoryA(g_exe_dir); /* steam_appid.txt, steam_api.dll and the log live here */
}

const char *exe_dir(void) { return g_exe_dir; }
u64 now_ms(void) { return GetTickCount64(); }
void sleep_ms(int ms) { Sleep((DWORD)ms); }

static void nonblock(SOCKET s)
{
    unsigned long on = 1;
    ioctlsocket(s, FIONBIO, &on);
}

static bool bind4(SOCKET s, uint32_t ip, uint16_t port)
{
    struct sockaddr_in4 a = { AF_INET, be16(port), ip, {0} };
    return bind(s, &a, sizeof a) == 0;
}

sock_t udp_open(uint32_t ip, uint16_t port, int flags)
{
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID) return BADSOCK;
    BOOL on = 1;
    int big = 4 << 20;
    if (flags & UDP_BROADCAST) setsockopt(s, SOL_SOCKET, SO_BROADCAST, &on, sizeof on);
    if (flags & UDP_REUSE) setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, &big, sizeof big);
    if (!bind4(s, ip, port)) { closesocket(s); return BADSOCK; }
    nonblock(s);
    /* Without this, an ICMP "port unreachable" (game not started yet) makes
       every later recv on the socket fail with WSAECONNRESET. */
    BOOL off = 0;
    DWORD ret = 0;
    WSAIoctl(s, SIO_UDP_CONNRESET, &off, sizeof off, 0, 0, &ret, 0, 0);
    return (sock_t)s;
}

static int dgram_result(int r)
{
    if (r >= 0) return r > 0 ? r : 0;
    int e = WSAGetLastError();
    if (e == WSAECONNRESET || e == WSAEMSGSIZE) return 0; /* skip, keep draining */
    return -1;
}

int udp_recvfrom(sock_t s, void *p, int n, uint32_t *ip, uint16_t *port)
{
    struct sockaddr_in4 a;
    int len = sizeof a;
    int r = dgram_result(recvfrom((SOCKET)s, p, n, 0, &a, &len));
    if (r > 0) { *ip = a.addr; *port = be16(a.port); }
    return r;
}

int udp_sendto(sock_t s, const void *p, int n, uint32_t ip, uint16_t port)
{
    struct sockaddr_in4 a = { AF_INET, be16(port), ip, {0} };
    return sendto((SOCKET)s, p, n, 0, &a, sizeof a);
}

uint16_t sock_local_port(sock_t s)
{
    struct sockaddr_in4 a;
    int len = sizeof a;
    if (getsockname((SOCKET)s, &a, &len) != 0) return 0;
    return be16(a.port);
}

void sock_close(sock_t s) { if (s != BADSOCK) closesocket((SOCKET)s); }

static void tcp_tune(SOCKET s)
{
    BOOL on = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &on, sizeof on);
    nonblock(s);
}

sock_t tcp_listen(uint32_t ip, uint16_t port)
{
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID) return BADSOCK;
    BOOL on = 1; /* SCCTLink's own loopback address: let it reopen past TIME_WAIT */
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
    if (!bind4(s, ip, port) || listen(s, 8) != 0) { closesocket(s); return BADSOCK; }
    nonblock(s);
    return (sock_t)s;
}

sock_t tcp_accept(sock_t l, uint32_t *ip, uint16_t *port)
{
    struct sockaddr_in4 a;
    int len = sizeof a;
    SOCKET s = accept((SOCKET)l, &a, &len);
    if (s == INVALID) return BADSOCK;
    *ip = a.addr;
    *port = be16(a.port);
    tcp_tune(s);
    return (sock_t)s;
}

sock_t tcp_connect(uint32_t bind_ip, uint16_t bind_port, uint32_t ip, uint16_t port)
{
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID) return BADSOCK;
    if (!bind4(s, bind_ip, bind_port) && (bind_port == 0 || !bind4(s, bind_ip, 0))) { closesocket(s); return BADSOCK; }
    struct sockaddr_in4 a = { AF_INET, be16(port), ip, {0} };
    if (connect(s, &a, sizeof a) != 0) { closesocket(s); return BADSOCK; }
    tcp_tune(s);
    return (sock_t)s;
}

int tcp_send(sock_t s, const void *p, int n)
{
    int r = send((SOCKET)s, p, n, 0);
    if (r >= 0) return r;
    return WSAGetLastError() == WSAEWOULDBLOCK ? 0 : -1;
}

int tcp_recv(sock_t s, void *p, int n)
{
    int r = recv((SOCKET)s, p, n, 0);
    if (r > 0) return r;
    if (r == 0) return -1;
    return WSAGetLastError() == WSAEWOULDBLOCK ? 0 : -1;
}

void wait_socks(const sock_t *s, int n, int ms)
{
    static fd_set_w r;
    r.count = 0;
    for (int i = 0; i < n && i < 256; i++)
        if (s[i] != BADSOCK) r.fd[r.count++] = (SOCKET)s[i];
    struct timeval_w t = { 0, ms * 1000 };
    if (r.count == 0) { Sleep((DWORD)ms); return; }
    select(0, &r, 0, 0, &t);
}

/* ---- the game's ports, from the same tables "netstat -ano" shows ---- */

typedef struct { DWORD pid; bool match; } PidName;
static PidName g_pids[256];
static int g_npids;
static u64 g_pids_at;

static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

static bool pid_is_game(DWORD pid, const char *prefix)
{
    if (!pid) return false;
    if (now_ms() - g_pids_at > 5000) { g_npids = 0; g_pids_at = now_ms(); }
    for (int i = 0; i < g_npids; i++) if (g_pids[i].pid == pid) return g_pids[i].match;
    bool m = false;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, 0, pid);
    if (h) {
        char path[600];
        DWORD n = sizeof path;
        if (QueryFullProcessImageNameA(h, 0, path, &n)) {
            const char *base = path;
            for (DWORD i = 0; i < n; i++) if (path[i] == '\\' || path[i] == '/') base = path + i + 1;
            m = true;
            for (int i = 0; prefix[i]; i++)
                if (lower(base[i]) != lower(prefix[i])) { m = false; break; }
        }
        CloseHandle(h);
    }
    if (g_npids < 256) { g_pids[g_npids].pid = pid; g_pids[g_npids++].match = m; }
    return m;
}

static void *get_table(GetTable_t fn, int cls)
{
    static uint8_t *buf[2];
    static DWORD cap[2];
    int k = cls == 1 ? 0 : 1;
    if (!fn) return 0;
    for (int tries = 0; tries < 4; tries++) {
        DWORD size = cap[k];
        DWORD r = fn(buf[k], &size, 0, AF_INET, cls, 0);
        if (r == 0) return buf[k];
        if (r != 122 /* ERROR_INSUFFICIENT_BUFFER */) return 0;
        free(buf[k]);
        cap[k] = size + 4096;
        buf[k] = malloc(cap[k]);
        if (!buf[k]) { cap[k] = 0; return 0; }
    }
    return 0;
}

void game_scan(const char *exe_prefix, GameNet *g)
{
    memset(g, 0, sizeof *g);
    DWORD me = GetCurrentProcessId();
    DWORD *u = get_table(pGetExtendedUdpTable, 1 /* UDP_TABLE_OWNER_PID */);
    if (u) {
        DWORD n = u[0];
        for (DWORD i = 0; i < n; i++) {
            DWORD *row = u + 1 + 3 * i; /* addr, port, pid */
            uint16_t port = be16((uint16_t)row[1]);
            if (row[2] != me && g->nbusy < 512) g->busy_udp[g->nbusy++] = port;
            if (pid_is_game(row[2], exe_prefix)) {
                g->running = true;
                if (g->nudp < MAX_GPORTS) g->udp[g->nudp++] = (Bound){ (uint32_t)row[0], port };
            }
        }
    }
    DWORD *t = get_table(pGetExtendedTcpTable, 5 /* TCP_TABLE_OWNER_PID_ALL */);
    if (t) {
        DWORD n = t[0];
        for (DWORD i = 0; i < n; i++) {
            DWORD *row = t + 1 + 6 * i; /* state, addr, port, raddr, rport, pid */
            if (!pid_is_game(row[5], exe_prefix)) continue;
            g->running = true;
            uint16_t port = be16((uint16_t)row[2]);
            if (row[0] == 2 /* LISTEN */) { if (g->ntcpl < MAX_GPORTS) g->tcpl[g->ntcpl++] = (Bound){ (uint32_t)row[1], port }; }
            else if (g->ntcpc < MAX_GPORTS) g->tcpc[g->ntcpc++] = port;
        }
    }
}

int local_ipv4s(uint32_t *out, int max)
{
    char name[256];
    if (gethostname(name, sizeof name) != 0) return 0;
    struct hostent_w *h = gethostbyname(name);
    if (!h || h->type != AF_INET || h->len != 4) return 0;
    int n = 0;
    for (int i = 0; h->addrs[i] && n < max; i++) {
        uint32_t ip;
        memcpy(&ip, h->addrs[i], 4);
        if (!is_loop(ip)) out[n++] = ip;
    }
    return n;
}

int key_poll(void) { return _kbhit() ? _getch() : -1; }

bool read_line(char *buf, int n)
{
    if (!fgets(buf, n, stdin)) return false;
    size_t l = strlen(buf);
    while (l && (buf[l - 1] == '\n' || buf[l - 1] == '\r')) buf[--l] = 0;
    return true;
}

void clipboard_set(const char *text)
{
    size_t n = strlen(text) + 1;
    HANDLE h = GlobalAlloc(2 /* GMEM_MOVEABLE */, n);
    if (!h) return;
    memcpy(GlobalLock(h), text, n);
    GlobalUnlock(h);
    if (OpenClipboard(0)) {
        EmptyClipboard();
        SetClipboardData(1 /* CF_TEXT */, h);
        CloseClipboard();
    }
}

void open_url(const char *url) { ShellExecuteA(0, "open", url, 0, 0, 1); }
void set_title(const char *t) { SetConsoleTitleA(t); }
void u64_to_str(u64 v, char out[24]) { _ui64toa(v, out, 10); }
u64 str_to_u64(const char *s) { return _strtoui64(s, 0, 10); }

bool file_write_text(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    fwrite(text, 1, strlen(text), f);
    fclose(f);
    return true;
}

void log_line(const char *text)
{
    static FILE *f;
    static bool tried;
    if (!tried) { tried = true; f = fopen("scctlink.log", "ab"); }
    if (!f) return;
    fwrite(text, 1, strlen(text), f);
    fwrite("\r\n", 1, 2, f);
    fflush(f);
}
