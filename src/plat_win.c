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
IMP HANDLE WINAPI CreateToolhelp32Snapshot(DWORD flags, DWORD pid);
IMP DWORD WINAPI GetFileAttributesA(const char *path);
typedef struct { DWORD cb; char *reserved, *desktop, *title; DWORD x, y, w, h, cx, cy, fill, flags;
                 uint16_t show, reserved2; void *reserved3; HANDLE in, out, err; } StartupInfoA; /* 68 bytes */
typedef struct { HANDLE process, thread; DWORD pid, tid; } ProcInfo;
IMP BOOL WINAPI CreateProcessA(const char *app, char *cmd, void *pa, void *ta, BOOL inherit, DWORD flags,
                               void *env, const char *dir, StartupInfoA *si, ProcInfo *pi);
IMP BOOL WINAPI Process32First(HANDLE snap, void *entry);
IMP BOOL WINAPI Process32Next(HANDLE snap, void *entry);
IMP BOOL WINAPI CloseHandle(HANDLE h);
IMP DWORD WINAPI GetCurrentProcessId(void);
#define LOAD_LIBRARY_SEARCH_SYSTEM32 0x800
IMP BOOL WINAPI OpenClipboard(HANDLE w);
IMP BOOL WINAPI EmptyClipboard(void);
IMP HANDLE WINAPI SetClipboardData(UINT fmt, HANDLE h);
IMP BOOL WINAPI CloseClipboard(void);
IMP HANDLE WINAPI GlobalFree(HANDLE h);
IMP int WINAPI MultiByteToWideChar(UINT cp, DWORD flags, const char *s, int n, void *w, int wn);
IMP HANDLE WINAPI GetClipboardData(UINT fmt);
IMP HANDLE WINAPI GetStdHandle(DWORD which);
IMP BOOL WINAPI GetConsoleMode(HANDLE h, DWORD *mode);
IMP BOOL WINAPI SetConsoleMode(HANDLE h, DWORD mode);
IMP BOOL WINAPI GetNumberOfConsoleInputEvents(HANDLE h, DWORD *n);
IMP BOOL WINAPI ReadConsoleInputW(HANDLE h, void *rec, DWORD len, DWORD *read);
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
static int g_sock_err;
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

static HANDLE g_in;
static bool g_console;

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
    /* Keys are read straight from the console (see key_poll). Turn off
       "Quick Edit": with it on, one click in the window pauses SCCTLink -
       and the game traffic with it - until Esc is pressed. */
    g_in = GetStdHandle((DWORD)-10 /* STD_INPUT_HANDLE */);
    DWORD mode;
    g_console = g_in && GetConsoleMode(g_in, &mode);
    if (g_console)
        SetConsoleMode(g_in, (mode & ~(DWORD)0x40 /* QUICK_EDIT */) | 0x80 /* EXTENDED_FLAGS */);
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
    if (!bind4(s, ip, port)) { g_sock_err = WSAGetLastError(); closesocket(s); return BADSOCK; }
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
int sock_last_error(void) { return g_sock_err; }
bool sock_err_denied(int err) { return err == 10013 /* WSAEACCES */; }

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

/* The game's process ids, from the process list (not from its sockets: a game
   in the menu may have no sockets, or only short-lived ones). */
typedef struct {
    DWORD size, usage, pid;
    uintptr_t heap;
    DWORD module, threads, parent;
    long prio;
    DWORD flags;
    char exe[260];
} ProcEntry; /* PROCESSENTRY32 (ANSI) */

static DWORD g_game_pids[16];
static int g_ngame_pids;

static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

static bool name_match(const char *name, const char *prefix)
{
    for (int i = 0; prefix[i]; i++)
        if (lower(name[i]) != lower(prefix[i])) return false;
    return true;
}

static void find_game_pids(const char *prefix)
{
    g_ngame_pids = 0;
    HANDLE snap = CreateToolhelp32Snapshot(2 /* TH32CS_SNAPPROCESS */, 0);
    if (snap == (HANDLE)(intptr_t)-1) return;
    ProcEntry e;
    memset(&e, 0, sizeof e);
    e.size = sizeof e;
    for (BOOL ok = Process32First(snap, &e); ok; ok = Process32Next(snap, &e)) {
        e.exe[259] = 0;
        if (name_match(e.exe, prefix) && g_ngame_pids < 16) g_game_pids[g_ngame_pids++] = e.pid;
    }
    CloseHandle(snap);
}

static bool pid_is_game(DWORD pid)
{
    for (int i = 0; i < g_ngame_pids; i++) if (g_game_pids[i] == pid) return true;
    return false;
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
    find_game_pids(exe_prefix);
    g->running = g_ngame_pids > 0;
    DWORD me = GetCurrentProcessId();
    DWORD *u = get_table(pGetExtendedUdpTable, 1 /* UDP_TABLE_OWNER_PID */);
    if (u) {
        DWORD n = u[0];
        for (DWORD i = 0; i < n; i++) {
            DWORD *row = u + 1 + 3 * i; /* addr, port, pid */
            uint16_t port = be16((uint16_t)row[1]);
            if (row[2] != me && g->nbusy < 512) g->busy_udp[g->nbusy++] = port;
            if (pid_is_game(row[2])) {
                if (g->nudp < MAX_GPORTS) g->udp[g->nudp++] = (Bound){ (uint32_t)row[0], port };
            }
        }
    }
    DWORD *t = get_table(pGetExtendedTcpTable, 5 /* TCP_TABLE_OWNER_PID_ALL */);
    if (t) {
        DWORD n = t[0];
        for (DWORD i = 0; i < n; i++) {
            DWORD *row = t + 1 + 6 * i; /* state, addr, port, raddr, rport, pid */
            if (!pid_is_game(row[5])) continue;
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

/* ---- keyboard ----
 * Keys are matched by their position on the keyboard (virtual key code), not
 * by the letter they type, so I, L, S, Y... work with an Arabic (or any other)
 * keyboard layout too. Arabic-Indic digits are read as 0-9. Ctrl+V pastes. */

typedef struct {
    uint16_t type, pad;
    BOOL down;
    uint16_t repeat, vk, scan;
    uint16_t ch;
    DWORD ctrl;
} KeyRec; /* INPUT_RECORD holding a KEY_EVENT_RECORD: 20 bytes */

enum { K_NONE = -1, K_ENTER = '\r', K_BACK = 8, K_ESC = 27, K_PASTE = 22 };

static int map_key(const KeyRec *k)
{
    if (k->type != 1 /* KEY_EVENT */ || !k->down) return K_NONE;
    unsigned vk = k->vk, ch = k->ch;
    bool ctrl = (k->ctrl & (0x4 | 0x8)) != 0 && !(k->ctrl & (0x1 | 0x2)); /* Ctrl without AltGr */
    if (ctrl && vk == 'V') return K_PASTE;
    if (vk == 0x0D) return K_ENTER;
    if (vk == 0x08) return K_BACK;
    if (vk == 0x1B) return K_ESC;
    if (ch >= 0x660 && ch <= 0x669) return '0' + (int)(ch - 0x660); /* Arabic-Indic digits */
    if (ch >= 0x6F0 && ch <= 0x6F9) return '0' + (int)(ch - 0x6F0); /* Persian digits */
    if (vk >= 0x60 && vk <= 0x69) return '0' + (int)(vk - 0x60);     /* number pad */
    if (vk >= 'A' && vk <= 'Z') return (int)vk;                       /* same key, any layout */
    if (vk >= '0' && vk <= '9' && !(k->ctrl & 0x10 /* Shift */)) return (int)vk;
    if (ch >= 0x20 && ch < 0x7F) return (int)ch;                      /* pasted text etc. */
    return K_NONE;
}

static int next_key(bool wait)
{
    for (;;) {
        DWORD n = 0;
        if (!wait && (!GetNumberOfConsoleInputEvents(g_in, &n) || n == 0)) return K_NONE;
        KeyRec r;
        if (!ReadConsoleInputW(g_in, &r, 1, &n) || n == 0) return wait ? K_ESC : K_NONE;
        int k = map_key(&r);
        if (k != K_NONE) return k;
    }
}

int broadcast_listen_addrs(uint32_t *out, int max) { return local_ipv4s(out, max); }

int key_poll(void)
{
    if (!g_console) return _kbhit() ? _getch() : -1;
    return next_key(false);
}

int clipboard_get(char *out, int n)
{
    int len = 0;
    if (!OpenClipboard(0)) return 0;
    HANDLE h = GetClipboardData(1 /* CF_TEXT */);
    const char *t = h ? GlobalLock(h) : 0;
    if (t) {
        for (; t[len] && len < n - 1; len++) out[len] = t[len];
        GlobalUnlock(h);
    }
    out[len] = 0;
    CloseClipboard();
    return len;
}

bool read_line(char *buf, int n)
{
    if (!g_console) {
        if (!fgets(buf, n, stdin)) return false;
        size_t l = strlen(buf);
        while (l && (buf[l - 1] == '\n' || buf[l - 1] == '\r')) buf[--l] = 0;
        return true;
    }
    int len = 0;
    buf[0] = 0;
    for (;;) {
        int k = next_key(true);
        if (k == K_ENTER) { printf("\n"); fflush(0); return true; }
        if (k == K_BACK) {
            if (len) { buf[--len] = 0; printf("\b \b"); fflush(0); }
            continue;
        }
        if (k == K_ESC) { while (len) { buf[--len] = 0; printf("\b \b"); } fflush(0); continue; }
        char add[128];
        int na = 0;
        if (k == K_PASTE) {
            char clip[128];
            int c = clipboard_get(clip, sizeof clip);
            for (int i = 0; i < c && na < 127; i++)
                if (clip[i] >= 0x20 && clip[i] < 0x7F) add[na++] = clip[i];
        } else if (k > 0 && k < 0x7F) {
            add[na++] = (char)k;
        }
        for (int i = 0; i < na && len < n - 1; i++) {
            buf[len++] = add[i];
            printf("%c", add[i]);
        }
        buf[len] = 0;
        fflush(0);
    }
}

void clipboard_set(const char *text)
{
    /* As Unicode, so names in any language (Arabic too) survive; Windows
       makes the plain-text version from it. */
    int wn = MultiByteToWideChar(65001 /* UTF-8 */, 0, text, -1, 0, 0);
    if (wn <= 0) return;
    HANDLE h = GlobalAlloc(2 /* GMEM_MOVEABLE */, (size_t)wn * 2);
    if (!h) return;
    void *p = GlobalLock(h);
    if (!p) { GlobalFree(h); return; }
    MultiByteToWideChar(65001, 0, text, -1, p, wn);
    GlobalUnlock(h);
    if (!OpenClipboard(0)) { GlobalFree(h); return; }
    EmptyClipboard();
    if (!SetClipboardData(13 /* CF_UNICODETEXT */, h)) GlobalFree(h); /* on success Windows owns it */
    CloseClipboard();
}

void open_url(const char *url) { ShellExecuteA(0, "open", url, 0, 0, 1); }

bool file_exists(const char *path)
{
    DWORD a = GetFileAttributesA(path);
    return a != 0xFFFFFFFFu && !(a & 0x10 /* directory */);
}

bool run_program(const char *exe, const char *args, const char *dir)
{
    static char cmd[1400];
    sfmt(cmd, sizeof cmd, "\"%s\" %s", exe, args);
    StartupInfoA si;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    ProcInfo pi;
    memset(&pi, 0, sizeof pi);
    if (!CreateProcessA(exe, cmd, 0, 0, 0, 0, 0, dir, &si, &pi)) return false;
    CloseHandle(pi.thread);
    CloseHandle(pi.process);
    return true;
}

bool game_is_running(const char *prefix)
{
    find_game_pids(prefix);
    return g_ngame_pids > 0;
}
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
