/* tun_win.c - the "SCCTLink" virtual network adapter on Windows (VPN mode,
 * experimental), with Wintun: https://www.wintun.net/ (by the WireGuard
 * project; its driver is signed by Microsoft). wintun.dll (the x86 build from
 * the official wintun-0.14.1.zip) sits next to SCCTLink.exe and is loaded only
 * if its SHA-256 is exactly the pinned one. The x86 build handles 64-bit
 * Windows itself (WOW64).
 *
 * Creating an adapter needs administrator rights. The adapter gets
 * 10.231.0.x/24, no gateway (so it never carries internet traffic), metric 1
 * (so the game's LAN broadcasts go out on it) and MTU 1400. Windows Firewall
 * is told to leave this one adapter alone: only SCCTLink puts packets into it,
 * and SCCTLink lets in nothing but the game's own traffic from linked
 * friends. Everything is undone, and the adapter removed, when SCCTLink closes. */
#include "plat.h"

typedef unsigned long DWORD;
typedef int BOOL;
typedef void *HANDLE;
#define WINAPI __stdcall
#define IMP __declspec(dllimport)

IMP void *WINAPI LoadLibraryExA(const char *path, void *file, unsigned long flags);
IMP void *WINAPI GetProcAddress(void *mod, const char *name);
IMP DWORD WINAPI GetLastError(void);
IMP DWORD WINAPI WaitForSingleObject(HANDLE h, DWORD ms);
IMP BOOL WINAPI GetExitCodeProcess(HANDLE h, DWORD *code);
IMP BOOL WINAPI CloseHandle(HANDLE h);
IMP unsigned WINAPI GetSystemDirectoryA(char *buf, unsigned n);
typedef struct { DWORD cb; char *reserved, *desktop, *title; DWORD x, y, w, h, cx, cy, fill, flags;
                 uint16_t show, reserved2; void *reserved3; HANDLE in, out, err; } StartupInfoA;
typedef struct { HANDLE process, thread; DWORD pid, tid; } ProcInfo;
IMP BOOL WINAPI CreateProcessA(const char *app, char *cmd, void *pa, void *ta, BOOL inherit, DWORD flags,
                               void *env, const char *dir, StartupInfoA *si, ProcInfo *pi);
bool sha256_file(const char *path, char hex[65]);

/* SHA-256 of bin\x86\wintun.dll inside the official wintun-0.14.1.zip
   (zip: 07c256185d6ee3652e09fa55c0b673e2624b565e02c4b9091c79ca7d2f24ef51) */
#define WINTUN_SHA256 "d694fa46ab4cfebcb2632d094c7aa97278eef2f8052438621766d863ae98a931"

typedef struct { uint32_t a; uint16_t b, c; uint8_t d[8]; } Guid;
typedef void *(WINAPI *CreateAdapterFn)(const uint16_t *name, const uint16_t *type, const Guid *guid);
typedef void *(WINAPI *OpenAdapterFn)(const uint16_t *name);
typedef void(WINAPI *CloseAdapterFn)(void *adapter);
typedef void *(WINAPI *StartSessionFn)(void *adapter, DWORD capacity);
typedef void(WINAPI *EndSessionFn)(void *session);
typedef uint8_t *(WINAPI *ReceiveFn)(void *session, DWORD *size);
typedef void(WINAPI *ReleaseFn)(void *session, const uint8_t *packet);
typedef uint8_t *(WINAPI *AllocSendFn)(void *session, DWORD size);
typedef void(WINAPI *SendFn)(void *session, const uint8_t *packet);

static struct {
    CreateAdapterFn create; OpenAdapterFn open; CloseAdapterFn close;
    StartSessionFn start; EndSessionFn end; ReceiveFn recv; ReleaseFn release;
    AllocSendFn alloc; SendFn send;
} W;

struct Tun { void *adapter, *session; };

#define NAME_A "SCCTLink"
static const uint16_t NAME_W[] = { 'S', 'C', 'C', 'T', 'L', 'i', 'n', 'k', 0 };
/* a fixed id, so Windows reuses the same adapter entry every time */
static const Guid ADAPTER_GUID = { 0x5cc71e4b, 0x2d3a, 0x4f61, { 0x9a, 0x07, 0x53, 0x43, 0x43, 0x54, 0x4c, 0x6b } };

/* Run a Windows tool (netsh, PowerShell) without a window; its exit code, or -1. */
static int run_hidden(const char *exe_in_system32, const char *args)
{
    char sys[300], cmd[2048];
    unsigned n = GetSystemDirectoryA(sys, sizeof sys);
    if (!n || n >= sizeof sys) return -1;
    sfmt(cmd, sizeof cmd, "\"%s\\%s\" %s", sys, exe_in_system32, args);
    StartupInfoA si;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    ProcInfo pi;
    memset(&pi, 0, sizeof pi);
    if (!CreateProcessA(0, cmd, 0, 0, 0, 0x08000000 /* CREATE_NO_WINDOW */, 0, 0, &si, &pi)) return -1;
    WaitForSingleObject(pi.process, 20000);
    DWORD code = (DWORD)-1;
    GetExitCodeProcess(pi.process, &code);
    CloseHandle(pi.thread);
    CloseHandle(pi.process);
    return (int)code;
}

#define PS "WindowsPowerShell\\v1.0\\powershell.exe"
#define PS_ARGS "-NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "

static bool load_wintun(char *err, int errn)
{
    static void *dll;
    if (dll) return true;
    char path[700], hex[65];
    sfmt(path, sizeof path, "%s\\wintun.dll", exe_dir());
    if (!sha256_file(path, hex)) {
        sfmt(err, (size_t)errn, "wintun.dll is missing (it must be next to SCCTLink.exe).");
        return false;
    }
    if (strcmp(hex, WINTUN_SHA256) != 0) {
        sfmt(err, (size_t)errn, "wintun.dll is not the expected file, so it was NOT loaded (SHA-256 %s).", hex);
        return false;
    }
    dll = LoadLibraryExA(path, 0, 0x00000008 /* LOAD_WITH_ALTERED_SEARCH_PATH */);
    if (!dll) { sfmt(err, (size_t)errn, "wintun.dll could not be loaded."); return false; }
#define WGET(f, n) if (!(*(void **)&W.f = GetProcAddress(dll, n))) { sfmt(err, (size_t)errn, "wintun.dll has no %s.", n); dll = 0; return false; }
    WGET(create, "WintunCreateAdapter") WGET(open, "WintunOpenAdapter") WGET(close, "WintunCloseAdapter")
    WGET(start, "WintunStartSession") WGET(end, "WintunEndSession") WGET(recv, "WintunReceivePacket")
    WGET(release, "WintunReleaseReceivePacket") WGET(alloc, "WintunAllocateSendPacket") WGET(send, "WintunSendPacket")
#undef WGET
    return true;
}

Tun *tun_open(uint32_t ip, char *err, int errn)
{
    if (!load_wintun(err, errn)) return 0;
    void *a = W.open(NAME_W); /* left over from a crash? reuse it */
    if (!a) a = W.create(NAME_W, NAME_W, &ADAPTER_GUID);
    if (!a) {
        DWORD e = GetLastError();
        if (e == 5) sfmt(err, (size_t)errn, "Creating the virtual adapter needs administrator rights: "
                                            "right-click SCCTLink.exe -> Run as administrator.");
        else sfmt(err, (size_t)errn, "The virtual adapter could not be created (Windows error %lu).", (unsigned long)e);
        return 0;
    }
    const uint8_t *b = (const uint8_t *)&ip;
    char args[600];
    sfmt(args, sizeof args, "interface ipv4 set address name=\"" NAME_A "\" source=static address=%u.%u.%u.%u "
         "mask=255.255.255.0 gateway=none store=active", b[0], b[1], b[2], b[3]);
    if (run_hidden("netsh.exe", args) != 0) {
        sfmt(err, (size_t)errn, "Couldn't give the virtual adapter its address (netsh failed).");
        W.close(a);
        return 0;
    }
    run_hidden("netsh.exe", "interface ipv4 set interface interface=\"" NAME_A "\" metric=1 store=active");
    run_hidden("netsh.exe", "interface ipv4 set subinterface \"" NAME_A "\" mtu=1400 store=active");
    /* Windows Firewall: leave this adapter alone (each profile keeps its other exceptions) */
    run_hidden(PS, PS_ARGS "\"foreach($p in 'Domain','Private','Public'){"
                          "$a=@((Get-NetFirewallProfile -Name $p).DisabledInterfaceAliases|?{$_ -and $_ -ne 'NotConfigured'});"
                          "if($a -notcontains '" NAME_A "'){Set-NetFirewallProfile -Name $p -DisabledInterfaceAliases ($a+'" NAME_A "')}}\"");
    void *s = W.start(a, 0x400000);
    if (!s) {
        sfmt(err, (size_t)errn, "The virtual adapter could not be started (Windows error %lu).", (unsigned long)GetLastError());
        W.close(a);
        return 0;
    }
    Tun *t = calloc(1, sizeof *t);
    t->adapter = a;
    t->session = s;
    return t;
}

int tun_read(Tun *t, uint8_t *buf, int n)
{
    DWORD sz = 0;
    uint8_t *p = W.recv(t->session, &sz);
    if (!p) return 0; /* ERROR_NO_MORE_ITEMS, or the adapter went away */
    int len = (int)sz <= n ? (int)sz : 0;
    if (len) memcpy(buf, p, (size_t)len);
    W.release(t->session, p);
    return len;
}

bool tun_write(Tun *t, const uint8_t *p, int n)
{
    uint8_t *o = W.alloc(t->session, (DWORD)n);
    if (!o) return false; /* ring full: dropped, like a busy network */
    memcpy(o, p, (size_t)n);
    W.send(t->session, o);
    return true;
}

void tun_close(Tun *t)
{
    if (!t) return;
    W.end(t->session);
    W.close(t->adapter); /* removes the adapter */
    /* take the adapter off the firewall's exception lists again */
    run_hidden(PS, PS_ARGS "\"foreach($p in 'Domain','Private','Public'){"
                          "$a=@((Get-NetFirewallProfile -Name $p).DisabledInterfaceAliases|?{$_ -and $_ -ne 'NotConfigured' -and $_ -ne '" NAME_A "'});"
                          "if($a.Count){Set-NetFirewallProfile -Name $p -DisabledInterfaceAliases $a}else{Set-NetFirewallProfile -Name $p -DisabledInterfaceAliases NotConfigured}}\"");
    free(t);
}
