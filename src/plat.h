/* plat.h - small OS layer: UDP/TCP sockets on this PC, the game's open ports,
 * time, console, clipboard, log file. Windows: plat_win.c. Linux test build:
 * plat_posix.c. Addresses are IPv4 in network byte order. */
#pragma once
#include "mini.h"

typedef intptr_t sock_t;
#define BADSOCK ((sock_t)-1)

extern volatile int g_quit;

void plat_init(void);            /* sockets, timer resolution, Ctrl+C, cwd = exe folder */
u64 now_ms(void);
void sleep_ms(int ms);

/* ---- UDP ---- */
#define UDP_BROADCAST 1          /* SO_BROADCAST */
#define UDP_REUSE 2              /* SO_REUSEADDR: only for SCCTLink's own unique loopback addresses */
sock_t udp_open(uint32_t ip, uint16_t port, int flags);  /* bind ip:port; BADSOCK on failure */
int udp_recvfrom(sock_t s, void *p, int n, uint32_t *ip, uint16_t *port); /* >0 bytes, 0 skip, <0 empty */
int udp_sendto(sock_t s, const void *p, int n, uint32_t ip, uint16_t port);
uint16_t sock_local_port(sock_t s);
void sock_close(sock_t s);
int sock_last_error(void);      /* error code of the last failed socket call */
bool sock_err_denied(int err);  /* \"access denied\" (another user / administrator holds the port) */

/* ---- TCP (non-blocking after setup) ---- */
sock_t tcp_listen(uint32_t ip, uint16_t port);
sock_t tcp_accept(sock_t l, uint32_t *ip, uint16_t *port);
/* bind to bind_ip:bind_port (port 0 = any), then connect to ip:port (local only, so it is quick) */
sock_t tcp_connect(uint32_t bind_ip, uint16_t bind_port, uint32_t ip, uint16_t port);
int tcp_send(sock_t s, const void *p, int n); /* bytes sent, 0 = would block, <0 = closed */
int tcp_recv(sock_t s, void *p, int n);       /* bytes, 0 = nothing yet, <0 = closed */

void wait_socks(const sock_t *s, int n, int ms); /* until one is readable or ms pass */

/* ---- what the game has open ---- */
#define MAX_GPORTS 64
typedef struct { uint32_t ip; uint16_t port; } Bound; /* ip 0 = all addresses */
typedef struct {
    bool running;
    Bound udp[MAX_GPORTS]; int nudp;     /* every UDP socket of the game */
    Bound tcpl[MAX_GPORTS]; int ntcpl;   /* TCP sockets listening */
    uint16_t tcpc[MAX_GPORTS]; int ntcpc;/* local ports of the game's other TCP sockets */
    uint16_t busy_udp[512]; int nbusy;   /* UDP ports used by any OTHER program */
} GameNet;
/* Fill *g for the process whose file name starts with exe_prefix (case-insensitive). */
void game_scan(const char *exe_prefix, GameNet *g);
int local_ipv4s(uint32_t *out, int max); /* this PC's own IPv4 addresses, loopback excluded */
/* Addresses to listen on for LAN broadcasts without taking 0.0.0.0:
   Windows: this PC's addresses; Linux (tests): the subnet broadcast addresses. */
int broadcast_listen_addrs(uint32_t *out, int max);

/* ---- console etc. ---- */
int key_poll(void);              /* key pressed, or -1. Never blocks. */
bool read_line(char *buf, int n);/* blocking line read from the console */
void clipboard_set(const char *text);
int clipboard_get(char *out, int n); /* text on the clipboard, 0 if none */
#define KEY_PASTE 22                  /* key_poll() value for Ctrl+V */
void open_url(const char *url);
void show_in_folder(const char *path); /* opens the file's folder with the file selected */
bool file_exists(const char *path);
/* Files and folders directly inside dir (not recursive, no "." / ".."). */
typedef void (*DirFn)(void *ctx, const char *name, bool is_dir, u64 size, u64 mtime); /* mtime: any number that changes when the file does */
void dir_list(const char *dir, DirFn fn, void *ctx);
bool run_program(const char *exe, const char *args, const char *dir); /* start it, don't wait */
bool game_is_running(const char *exe_prefix);
/* The game's frame rate, measured by Windows (ETW present events, like
   PresentMon): nothing is put into the game. fps_start/fps_stop turn the
   measuring on and off; fps_get gives frames per second of the process(es)
   found by the last game_scan since the previous call, -1 if unavailable. */
bool fps_start(void);
void fps_stop(void);
int fps_get(void);
#ifdef _WIN32
#define PATHSEP "\\"
#else
#define PATHSEP "/"
#endif
void set_title(const char *t);

void u64_to_str(u64 v, char out[24]);
u64 str_to_u64(const char *s);
bool file_write_text(const char *path, const char *text);
void log_line(const char *text);   /* appends to scctlink.log next to the exe */
const char *exe_dir(void);

static inline uint32_t ip4(int a, int b, int c, int d)
{
    uint8_t x[4] = { (uint8_t)a, (uint8_t)b, (uint8_t)c, (uint8_t)d };
    uint32_t v; memcpy(&v, x, 4); return v;
}
static inline bool is_loop(uint32_t ip) { return ((const uint8_t *)&ip)[0] == 127; }
