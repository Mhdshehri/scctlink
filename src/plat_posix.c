/* plat_posix.c - Linux side of plat.h, used only by the mock test build. */
#define _GNU_SOURCE
#include "plat.h"
#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

volatile int g_quit;
static int g_sock_err;

static void on_sig(int s) { (void)s; g_quit = 1; }

void plat_init(void)
{
    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);
    signal(SIGPIPE, SIG_IGN);
    setvbuf(stdout, 0, _IONBF, 0);
}

const char *exe_dir(void) { const char *d = getenv("MOCK_EXE_DIR"); return d && *d ? d : "."; } /* tests: each "PC" has its own SCCTLink folder */

u64 now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (u64)t.tv_sec * 1000 + (u64)t.tv_nsec / 1000000;
}

void sleep_ms(int ms) { usleep((useconds_t)ms * 1000); }

static bool bind4(int s, uint32_t ip, uint16_t port)
{
    struct sockaddr_in a = { 0 };
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = ip;
    return bind(s, (void *)&a, sizeof a) == 0;
}

sock_t udp_open(uint32_t ip, uint16_t port, int flags)
{
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) return BADSOCK;
    int on = 1;
    if (flags & UDP_BROADCAST) setsockopt(s, SOL_SOCKET, SO_BROADCAST, &on, sizeof on);
    if (flags & UDP_REUSE) setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
    int big = 4 << 20;
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, &big, sizeof big);
    if (!bind4(s, ip, port)) { g_sock_err = errno; close(s); return BADSOCK; }
    fcntl(s, F_SETFL, O_NONBLOCK);
    return s;
}

static int dgram_result(ssize_t r)
{
    if (r >= 0) return r > 0 ? (int)r : 0;
    if (errno == ECONNREFUSED) return 0;
    return -1;
}

int udp_recvfrom(sock_t s, void *p, int n, uint32_t *ip, uint16_t *port)
{
    struct sockaddr_in a;
    socklen_t len = sizeof a;
    int r = dgram_result(recvfrom((int)s, p, (size_t)n, 0, (void *)&a, &len));
    if (r > 0) { *ip = a.sin_addr.s_addr; *port = ntohs(a.sin_port); }
    return r;
}

int udp_sendto(sock_t s, const void *p, int n, uint32_t ip, uint16_t port)
{
    struct sockaddr_in a = { 0 };
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = ip;
    return (int)sendto((int)s, p, (size_t)n, 0, (void *)&a, sizeof a);
}

uint16_t sock_local_port(sock_t s)
{
    struct sockaddr_in a;
    socklen_t len = sizeof a;
    if (getsockname((int)s, (void *)&a, &len) != 0) return 0;
    return ntohs(a.sin_port);
}

void sock_close(sock_t s) { if (s != BADSOCK) close((int)s); }
int sock_last_error(void) { return g_sock_err; }
bool sock_err_denied(int err) { return err == EACCES; }

sock_t tcp_listen(uint32_t ip, uint16_t port)
{
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return BADSOCK;
    int on = 1; /* SCCTLink's own loopback address: let it reopen past TIME_WAIT */
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
    if (!bind4(s, ip, port) || listen(s, 8) != 0) { close(s); return BADSOCK; }
    fcntl(s, F_SETFL, O_NONBLOCK);
    return s;
}

static void tcp_tune(int s)
{
    int on = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &on, sizeof on);
    fcntl(s, F_SETFL, O_NONBLOCK);
}

sock_t tcp_accept(sock_t l, uint32_t *ip, uint16_t *port)
{
    struct sockaddr_in a;
    socklen_t len = sizeof a;
    int s = accept((int)l, (void *)&a, &len);
    if (s < 0) return BADSOCK;
    *ip = a.sin_addr.s_addr;
    *port = ntohs(a.sin_port);
    tcp_tune(s);
    return s;
}

sock_t tcp_connect(uint32_t bind_ip, uint16_t bind_port, uint32_t ip, uint16_t port)
{
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return BADSOCK;
    if (!bind4(s, bind_ip, bind_port) && (bind_port == 0 || !bind4(s, bind_ip, 0))) { close(s); return BADSOCK; }
    struct sockaddr_in a = { 0 };
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = ip;
    if (connect(s, (void *)&a, sizeof a) != 0) { close(s); return BADSOCK; }
    tcp_tune(s);
    return s;
}

int tcp_send(sock_t s, const void *p, int n)
{
    ssize_t r = send((int)s, p, (size_t)n, MSG_NOSIGNAL);
    if (r >= 0) return (int)r;
    return (errno == EAGAIN || errno == EWOULDBLOCK) ? 0 : -1;
}

int tcp_recv(sock_t s, void *p, int n)
{
    ssize_t r = recv((int)s, p, (size_t)n, 0);
    if (r > 0) return (int)r;
    if (r == 0) return -1;
    return (errno == EAGAIN || errno == EWOULDBLOCK) ? 0 : -1;
}

void wait_socks(const sock_t *s, int n, int ms)
{
    struct pollfd p[256];
    int k = 0;
    for (int i = 0; i < n && k < 256; i++)
        if (s[i] != BADSOCK) { p[k].fd = (int)s[i]; p[k].events = POLLIN; k++; }
    if (k == 0) { sleep_ms(ms); return; }
    poll(p, (nfds_t)k, ms);
}

/* ---- game ports from /proc ---- */

static bool name_match(const char *name, const char *prefix)
{
    for (; *prefix; prefix++, name++)
        if (tolower((unsigned char)*name) != tolower((unsigned char)*prefix)) return false;
    return true;
}

typedef struct { unsigned long ino[1024]; int n; } Inodes;
static Inodes self_ino;

static bool has_ino(const Inodes *in, unsigned long ino)
{
    for (int i = 0; i < in->n; i++) if (in->ino[i] == ino) return true;
    return false;
}

/* Parse /proc/net/udp or tcp. kind: 0 udp, 1 tcp */
static void collect(const char *fddir, Inodes *in)
{
    DIR *fd = opendir(fddir);
    struct dirent *x;
    while (fd && (x = readdir(fd))) {
        char lp[600], tgt[64];
        snprintf(lp, sizeof lp, "%s/%.255s", fddir, x->d_name);
        ssize_t n = readlink(lp, tgt, sizeof tgt - 1);
        if (n <= 0) continue;
        tgt[n] = 0;
        unsigned long ino;
        if (sscanf(tgt, "socket:[%lu]", &ino) == 1 && in->n < 1024) in->ino[in->n++] = ino;
    }
    if (fd) closedir(fd);
}

static void parse_net(const char *path, int kind, const Inodes *mine, GameNet *g)
{
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[512];
    if (!fgets(line, sizeof line, f)) { fclose(f); return; }
    while (fgets(line, sizeof line, f)) {
        unsigned int lip, lport, rip, rport, st;
        unsigned long ino;
        if (sscanf(line, " %*d: %8X:%4X %8X:%4X %2X %*s %*s %*s %*s %*s %lu", &lip, &lport, &rip, &rport, &st, &ino) != 6)
            continue;
        /* /proc shows the address as a host-order number of the network-order bytes */
        uint32_t ip = (uint32_t)lip;
        if (kind == 0) {
            if (!has_ino(&self_ino, ino) && g->nbusy < 512) g->busy_udp[g->nbusy++] = (uint16_t)lport;
            if (has_ino(mine, ino) && g->nudp < MAX_GPORTS) g->udp[g->nudp++] = (Bound){ ip, (uint16_t)lport };
        } else if (has_ino(mine, ino)) {
            if (st == 0x0A) { if (g->ntcpl < MAX_GPORTS) g->tcpl[g->ntcpl++] = (Bound){ ip, (uint16_t)lport }; }
            else if (g->ntcpc < MAX_GPORTS) g->tcpc[g->ntcpc++] = (uint16_t)lport;
        }
    }
    fclose(f);
}

void game_scan(const char *exe_prefix, GameNet *g)
{
    memset(g, 0, sizeof *g);
    static Inodes mine;
    mine.n = 0;
    self_ino.n = 0;
    collect("/proc/self/fd", &self_ino);
    DIR *d = opendir("/proc");
    struct dirent *e;
    while (d && (e = readdir(d))) {
        if (!isdigit((unsigned char)e->d_name[0])) continue;
        char p[300], comm[64] = { 0 };
        snprintf(p, sizeof p, "/proc/%s/comm", e->d_name);
        FILE *f = fopen(p, "r");
        if (!f) continue;
        if (!fgets(comm, sizeof comm, f)) comm[0] = 0;
        fclose(f);
        if (!name_match(comm, exe_prefix)) continue;
        char ns1[64] = { 0 }, ns2[64] = { 0 };
        snprintf(p, sizeof p, "/proc/%s/ns/net", e->d_name);
        if (readlink(p, ns1, sizeof ns1 - 1) <= 0 || readlink("/proc/self/ns/net", ns2, sizeof ns2 - 1) <= 0 || strcmp(ns1, ns2)) continue;
        g->running = true;
        snprintf(p, sizeof p, "/proc/%s/fd", e->d_name);
        collect(p, &mine);
    }
    if (d) closedir(d);
    parse_net("/proc/net/udp", 0, &mine, g);
    parse_net("/proc/net/tcp", 1, &mine, g);
}

int local_ipv4s(uint32_t *out, int max)
{
    struct ifaddrs *ifa, *i;
    int n = 0;
    if (getifaddrs(&ifa) != 0) return 0;
    for (i = ifa; i && n < max; i = i->ifa_next) {
        if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET) continue;
        uint32_t ip = ((struct sockaddr_in *)i->ifa_addr)->sin_addr.s_addr;
        if (!is_loop(ip)) out[n++] = ip;
    }
    freeifaddrs(ifa);
    return n;
}

int broadcast_listen_addrs(uint32_t *out, int max)
{
    struct ifaddrs *ifa, *i;
    int n = 0;
    if (getifaddrs(&ifa) != 0) return 0;
    for (i = ifa; i && n < max; i = i->ifa_next) {
        if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || !i->ifa_netmask) continue;
        uint32_t ip = ((struct sockaddr_in *)i->ifa_addr)->sin_addr.s_addr;
        uint32_t mask = ((struct sockaddr_in *)i->ifa_netmask)->sin_addr.s_addr;
        if (is_loop(ip)) continue;
        out[n++] = ip | ~mask; /* the subnet's broadcast address */
    }
    freeifaddrs(ifa);
    return n;
}

/* Tests drive keys through stdin: one key per line. */
int key_poll(void)
{
    static bool eof;
    if (eof) return -1;
    struct pollfd p = { 0, POLLIN, 0 };
    if (poll(&p, 1, 0) <= 0) return -1;
    int c = getchar();
    if (c == EOF) { eof = true; return -1; }
    if (c == '^') { int d; while ((d = getchar()) != EOF && d != '\n') {} return KEY_PASTE; }
    int d;
    while ((d = getchar()) != EOF && d != '\n') {}
    return c;
}

bool read_line(char *buf, int n)
{
    if (!fgets(buf, n, stdin)) return false;
    size_t l = strlen(buf);
    while (l && (buf[l - 1] == '\n' || buf[l - 1] == '\r')) buf[--l] = 0;
    return true;
}

void clipboard_set(const char *t) { (void)t; }
int clipboard_get(char *out, int n)
{   /* tests: the "clipboard" is the env var MOCK_CLIPBOARD */
    const char *c = getenv("MOCK_CLIPBOARD");
    snprintf(out, (size_t)n, "%s", c ? c : "");
    return (int)strlen(out);
}
void open_url(const char *u) { log_line(u); printf("[mock] would open %s\n", u); }
bool file_exists(const char *path) { return access(path, F_OK) == 0; }
bool run_program(const char *exe, const char *args, const char *dir)
{
    printf("[mock] would run %s %s (in %s)\n", exe, args, dir);
    return true;
}
bool game_is_running(const char *prefix) { GameNet g; game_scan(prefix, &g); return g.running; }
void set_title(const char *t) { (void)t; }
void u64_to_str(u64 v, char out[24]) { snprintf(out, 24, "%llu", (unsigned long long)v); }
u64 str_to_u64(const char *s) { return strtoull(s, 0, 10); }

bool file_write_text(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    fputs(text, f);
    fclose(f);
    return true;
}

void log_line(const char *text)
{
    const char *p = getenv("SCCTLINK_LOG");
    if (!p) return;
    FILE *f = fopen(p, "ab");
    if (!f) return;
    fprintf(f, "%s\n", text);
    fclose(f);
}

#include <dirent.h>
#include <sys/stat.h>
void dir_list(const char *dir, DirFn fn, void *ctx)
{
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        struct stat st;
        if (stat(path, &st) != 0) continue;
        fn(ctx, e->d_name, S_ISDIR(st.st_mode), (u64)st.st_size, (u64)st.st_mtim.tv_sec * 1000000000u + (u64)st.st_mtim.tv_nsec);
    }
    closedir(d);
}
