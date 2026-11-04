/* SCCT Fix: a d3d9.dll for Splinter Cell: Chaos Theory (co-op).
 *
 * Put next to splintercell3.exe in the game's System folder. Windows loads a
 * DLL from the program's own folder before the system one, so the game gets
 * this d3d9.dll; it loads the real one from System32 and passes every call on,
 * changing only these things:
 *
 *   - 60 FPS cap: Present() waits until the next 1/60 s is due. Co-op runs the
 *     same simulation on both PCs in lockstep, and a frame rate far above 60
 *     made the two drift apart (the game disconnects when you draw a weapon).
 *   - Borderless window: CreateDevice()/Reset() always ask for a window (no
 *     exclusive fullscreen, no V-Sync), and the window is made to fill its
 *     monitor with no border.
 *   - Alt+Tab: the game grabs Alt+Tab for itself (RegisterHotKey "UnrealAltTab")
 *     and minimizes on focus loss; both are blocked.
 *   - Crash log: a crash's code and address are written to SCCTFix.log.
 *   - HUD fix (optional, SCCTFix.ini [HUD] Fix=1): the game draws its HUD in
 *     640x480 steps, x scaled by width/640 and y by height/480, so on a wide
 *     screen it is stretched sideways. With it, the in-game HUD's x is scaled
 *     like y (menus and videos as before). Drawing only: the co-op simulation
 *     isn't touched.
 *
 * This is a line-for-line rewrite of the original SCCT Fix d3d9.dll (made with
 * its author's permission): same names, same values, same order of calls, so
 * it behaves exactly the same in co-op. With the HUD fix off (the default)
 * nothing else is changed in the game.
 *
 * Build: fix/build_fix.sh (clang + lld, 32-bit, no C runtime).
 */
typedef unsigned int UINT;
typedef unsigned long DWORD, ULONG;
typedef long LONG, HRESULT;
typedef int BOOL;
typedef void *HANDLE, *HWND, *HMODULE, *HMONITOR, *HINSTANCE;
typedef unsigned short WORD, ATOM;
typedef unsigned char BYTE;
typedef long long LONGLONG;
typedef struct { LONG left, top, right, bottom; } RECT;
typedef struct { DWORD cbSize; RECT rcMonitor, rcWork; DWORD dwFlags; } MONITORINFO;
typedef struct { DWORD ExceptionCode, ExceptionFlags; void *ExceptionRecord, *ExceptionAddress; } EXCEPTION_RECORD;
typedef struct { EXCEPTION_RECORD *ExceptionRecord; void *ContextRecord; } EXCEPTION_POINTERS;

#define WINAPI __stdcall
#define IMP __declspec(dllimport)
IMP HANDLE WINAPI CreateFileA(const char *, DWORD, DWORD, void *, DWORD, DWORD, HANDLE);
IMP BOOL WINAPI DisableThreadLibraryCalls(HMODULE);
IMP DWORD WINAPI GetModuleFileNameA(HMODULE, char *, DWORD);
IMP HMODULE WINAPI GetModuleHandleA(const char *);
IMP void *WINAPI GetProcAddress(HMODULE, const char *);
IMP UINT WINAPI GetSystemDirectoryA(char *, UINT);
IMP ATOM WINAPI GlobalFindAtomA(const char *);
IMP HMODULE WINAPI LoadLibraryA(const char *);
IMP BOOL WINAPI QueryPerformanceCounter(LONGLONG *);
IMP BOOL WINAPI QueryPerformanceFrequency(LONGLONG *);
IMP void *WINAPI SetUnhandledExceptionFilter(void *);
IMP void WINAPI Sleep(DWORD);
IMP BOOL WINAPI VirtualProtect(void *, unsigned long, DWORD, DWORD *);
IMP BOOL WINAPI WriteFile(HANDLE, const void *, DWORD, DWORD *, void *);
IMP BOOL WINAPI GetMonitorInfoA(HMONITOR, MONITORINFO *);
IMP LONG WINAPI GetWindowLongA(HWND, int);
IMP BOOL WINAPI GetWindowRect(HWND, RECT *);
IMP BOOL WINAPI IsIconic(HWND);
IMP HMONITOR WINAPI MonitorFromWindow(HWND, DWORD);
IMP BOOL WINAPI RegisterHotKey(HWND, int, UINT, UINT);
IMP LONG WINAPI SetWindowLongA(HWND, int, LONG);
IMP BOOL WINAPI SetWindowPos(HWND, HWND, int, int, int, int, UINT);
IMP BOOL WINAPI ShowWindow(HWND, int);
IMP BOOL WINAPI UnregisterHotKey(HWND, int);
IMP int __cdecl wsprintfA(char *, const char *, ...);
IMP UINT WINAPI timeBeginPeriod(UINT);
IMP UINT WINAPI GetPrivateProfileIntA(const char *, const char *, int, const char *);

/* No C runtime: the compiler may still copy structs with these (and marks float use with _fltused). */
int _fltused = 0x9875;
void *memcpy(void *d, const void *s, unsigned n)
{
    unsigned char *o = d;
    const unsigned char *i = s;
    while (n--) *o++ = *i++;
    return d;
}
void *memset(void *d, int c, unsigned n)
{
    unsigned char *o = d;
    while (n--) *o++ = (unsigned char)c;
    return d;
}

/* D3DPRESENT_PARAMETERS (56 bytes) */
typedef struct {
    UINT BackBufferWidth, BackBufferHeight, BackBufferFormat, BackBufferCount;
    UINT MultiSampleType;
    DWORD MultiSampleQuality;
    UINT SwapEffect;
    HWND hDeviceWindow;
    BOOL Windowed, EnableAutoDepthStencil;
    UINT AutoDepthStencilFormat;
    DWORD Flags;
    UINT FullScreen_RefreshRateInHz, PresentationInterval;
} PRESENT_PARAMS;

#define D3DFMT_A8R8G8B8 21
#define D3DFMT_X8R8G8B8 22
#define D3DSWAPEFFECT_DISCARD 1
#define D3DPRESENT_INTERVAL_IMMEDIATE 0x80000000u

typedef void *(WINAPI *Create9Fn)(UINT);
typedef HRESULT(WINAPI *CreateDeviceFn)(void *, UINT, UINT, HWND, DWORD, PRESENT_PARAMS *, void **);
typedef HRESULT(WINAPI *ResetFn)(void *, PRESENT_PARAMS *);
typedef HRESULT(WINAPI *PresentFn)(void *, const void *, const void *, HWND, const void *);

static HANDLE g_log = (HANDLE)-1;
static Create9Fn g_create9;
static CreateDeviceFn g_create_device;
static LONGLONG g_freq;
static LONGLONG g_frame;   /* 1/60 s in counter ticks */
static LONGLONG g_spin;    /* the last 2 ms are waited by spinning, not Sleep */
static HWND g_hwnd;        /* the game's window */
static ResetFn g_reset;
static PresentFn g_present;
static DWORD g_frames;
static LONGLONG g_next;    /* when the next frame is due */

static unsigned slen(const char *s)
{
    unsigned n = 0;
    while (s[n]) n++;
    return n;
}

static void log_write(const char *s, DWORD n)
{
    DWORD w;
    if (g_log != (HANDLE)-1) WriteFile(g_log, s, n, &w, 0);
}

/* Replace the game's own import of user32!name with hook (its import table). */
static void patch_import(const char *name, void *hook)
{
    unsigned char *base = (unsigned char *)GetModuleHandleA(0);
    DWORD rva = *(DWORD *)(base + *(LONG *)(base + 0x3c) + 0x80); /* the import directory */
    void *target = GetProcAddress(GetModuleHandleA("user32.dll"), name);
    if (!rva || !target) return;
    for (DWORD *desc = (DWORD *)(base + rva); desc[3] /* Name */; desc += 5) {
        void **iat = (void **)(base + desc[4] /* FirstThunk */);
        if (!*iat) continue;
        for (; *iat; iat++) {
            if (*iat != target) continue;
            DWORD old;
            VirtualProtect(iat, 4, 4 /* PAGE_READWRITE */, &old);
            *iat = hook;
            VirtualProtect(iat, 4, old, &old);
            log_write("Patched import\r\n", 16);
        }
    }
}

/* The game registers Alt+Tab as its own hotkey: say yes, but don't. */
static BOOL WINAPI hook_RegisterHotKey(HWND w, int id, UINT mods, UINT vk)
{
    if (vk == 9 /* VK_TAB */) {
        log_write("Blocked Alt+Tab hotkey\r\n", 24);
        return 1;
    }
    return RegisterHotKey(w, id, mods, vk);
}

/* ...and minimizes itself when it loses focus: not the game's window. */
static BOOL WINAPI hook_ShowWindow(HWND w, int cmd)
{
    if (g_hwnd == w && (cmd == 2 || cmd == 6 || cmd == 7 || cmd == 11)) /* the SW_*MINIMIZE* commands */
        return 1;
    return ShowWindow(w, cmd);
}

/* The window: no border, covering its monitor (only changed when it isn't already). */
static void borderless(HWND w)
{
    if (!w || IsIconic(w)) return;
    MONITORINFO mi;
    mi.cbSize = sizeof mi;
    if (!GetMonitorInfoA(MonitorFromWindow(w, 1 /* MONITOR_DEFAULTTOPRIMARY */), &mi)) return;
    int mw = mi.rcMonitor.right - mi.rcMonitor.left, mh = mi.rcMonitor.bottom - mi.rcMonitor.top;
    LONG style = GetWindowLongA(w, -16 /* GWL_STYLE */), ex = GetWindowLongA(w, -20 /* GWL_EXSTYLE */);
    LONG nstyle = (style & 0x06000000 /* WS_CLIPSIBLINGS | WS_CLIPCHILDREN */) | (LONG)0x90000000 /* WS_POPUP | WS_VISIBLE */;
    LONG nex = ex & (LONG)0xfffdfcf6; /* no DLGMODALFRAME, TOPMOST, WINDOWEDGE, CLIENTEDGE, STATICEDGE */
    RECT r;
    GetWindowRect(w, &r);
    if (style == nstyle && ex == nex && r.left == mi.rcMonitor.left && r.top == mi.rcMonitor.top &&
        r.right - r.left == mw && r.bottom - r.top == mh)
        return;
    SetWindowLongA(w, -16, nstyle);
    SetWindowLongA(w, -20, nex);
    SetWindowPos(w, (HWND)-2 /* HWND_NOTOPMOST */, mi.rcMonitor.left, mi.rcMonitor.top, mw, mh,
                 0x230 /* SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE */);
}

/* Always a window: no exclusive fullscreen, no V-Sync, a 32-bit back buffer. */
static void windowed(PRESENT_PARAMS *pp)
{
    pp->Windowed = 1;
    pp->FullScreen_RefreshRateInHz = 0;
    pp->SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp->PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    if (pp->BackBufferFormat - D3DFMT_A8R8G8B8 >= 2) pp->BackBufferFormat = D3DFMT_X8R8G8B8;
}

/* ---- the HUD fix ----
   splintercell3.exe (Steam copy, fixed address 0x10900000) scales what it draws
   in 640x480 steps: x by width * (1/640) (constant at 0x110d5cb8), y by
   height * (1/480). The same constant also sizes the menus and the videos, so
   only the three places that draw the in-game HUD are pointed at our own number
   (each "fmul dword ptr [0x110d5cb8]", d8 0d b8 5c 0d 11), and that number is
   height / (480 * width): x scaled like y, no stretching. */
static float g_hudx = 0.0015625f; /* 1/640 until the screen size is known */
static int g_hud = -1;           /* -1: not read yet */
static char g_ini[300];
static void hud_fix(UINT w, UINT h)
{
    static const DWORD sites[3] = { 0x10c864b3, 0x10c86e09, 0x10c8720b };
    static const unsigned char want[6] = { 0xd8, 0x0d, 0xb8, 0x5c, 0x0d, 0x11 };
    static int patched;
    if (g_hud < 0) g_hud = GetPrivateProfileIntA("HUD", "Fix", 0, g_ini);
    if (!g_hud || !w || !h) return;
    g_hudx = (float)h / (480.0f * (float)w);
    if (patched) return;
    for (int k = 0; k < 3; k++) {
        unsigned char *ins = (unsigned char *)sites[k];
        for (int i = 0; i < 6; i++) if (ins[i] != want[i]) return; /* not the Steam copy's code: left alone */
    }
    for (int k = 0; k < 3; k++) {
        unsigned char *ins = (unsigned char *)sites[k];
        DWORD old, a = (DWORD)&g_hudx;
        VirtualProtect(ins + 2, 4, 0x40 /* PAGE_EXECUTE_READWRITE */, &old);
        memcpy(ins + 2, &a, 4);
        VirtualProtect(ins + 2, 4, old, &old);
    }
    patched = 1;
    log_write("HUD fix: on\r\n", 13);
}

static void log_mode(const char *what, const PRESENT_PARAMS *pp, HRESULT hr)
{
    char b[160];
    wsprintfA(b, "%s %ux%u fmt=%u ms=%u windowed=%d hr=0x%08lX\r\n", what, pp->BackBufferWidth, pp->BackBufferHeight,
              pp->BackBufferFormat, pp->MultiSampleType, pp->Windowed, hr);
    log_write(b, slen(b));
}

/* Point a COM object's method (vtable slot) at hook; returns the original. */
static void *hook_slot(void *obj, int slot, void *hook)
{
    void **vt = *(void ***)obj, *orig = vt[slot];
    if (orig == hook) return 0;
    DWORD old;
    VirtualProtect(&vt[slot], 4, 4, &old);
    vt[slot] = hook;
    VirtualProtect(&vt[slot], 4, old, &old);
    return orig;
}

static HRESULT WINAPI hook_Present(void *dev, const void *src, const void *dst, HWND w, const void *dirty)
{
    HRESULT hr = g_present(dev, src, dst, w, dirty);
    LONGLONG now;
    QueryPerformanceCounter(&now);
    if (!g_next) g_next = now;
    g_next += g_frame;
    if (now >= g_next) g_next = now; /* late: don't try to catch up */
    else
        while (g_next - now > 0) {
            if (g_next - now > g_spin) Sleep(1);
            QueryPerformanceCounter(&now);
        }
    if (!(++g_frames & 31)) { /* now and then: the game may take Alt+Tab back, or move the window */
        ATOM a = GlobalFindAtomA("UnrealAltTab");
        if (a) UnregisterHotKey(g_hwnd, a);
        borderless(g_hwnd);
    }
    return hr;
}

static HRESULT WINAPI hook_Reset(void *dev, PRESENT_PARAMS *in)
{
    PRESENT_PARAMS pp = *in;
    windowed(&pp);
    HRESULT hr = g_reset(dev, &pp);
    log_mode("Reset", &pp, hr);
    if (hr >= 0) hud_fix(pp.BackBufferWidth, pp.BackBufferHeight);
    return hr;
}

static HRESULT WINAPI hook_CreateDevice(void *d3d, UINT adapter, UINT type, HWND focus, DWORD flags, PRESENT_PARAMS *in,
                                        void **out)
{
    PRESENT_PARAMS pp = *in;
    patch_import("RegisterHotKey", (void *)hook_RegisterHotKey);
    patch_import("ShowWindow", (void *)hook_ShowWindow);
    g_hwnd = pp.hDeviceWindow ? pp.hDeviceWindow : focus;
    windowed(&pp);
    borderless(g_hwnd);
    HRESULT hr = g_create_device(d3d, adapter, type, focus, flags, &pp, out);
    log_mode("CreateDevice", &pp, hr);
    if (hr >= 0) hud_fix(pp.BackBufferWidth, pp.BackBufferHeight);
    if (out && hr >= 0 && *out) {
        void *o = hook_slot(*out, 16, (void *)hook_Reset);
        if (o) g_reset = (ResetFn)o;
        o = hook_slot(*out, 17, (void *)hook_Present);
        if (o) g_present = (PresentFn)o;
    }
    return hr;
}

__declspec(dllexport) void *WINAPI Direct3DCreate9(UINT sdk)
{
    if (!g_create9) {
        char path[260 + 14];
        UINT n = GetSystemDirectoryA(path, 260);
        memcpy(path + n, "\\d3d9.dll", 10);
        g_create9 = (Create9Fn)GetProcAddress(LoadLibraryA(path), "Direct3DCreate9");
        if (!g_create9) {
            log_write("Could not load system d3d9.dll\r\n", 32);
            return 0;
        }
    }
    void *d3d = g_create9(sdk);
    if (!d3d) return 0;
    void *o = hook_slot(d3d, 16, (void *)hook_CreateDevice);
    if (o) g_create_device = (CreateDeviceFn)o;
    return d3d;
}

static LONG WINAPI on_crash(EXCEPTION_POINTERS *e)
{
    char b[120];
    wsprintfA(b, "CRASH code=0x%08lX address=0x%08lX\r\n", e->ExceptionRecord->ExceptionCode,
              (DWORD)e->ExceptionRecord->ExceptionAddress);
    log_write(b, slen(b));
    return 0; /* EXCEPTION_CONTINUE_SEARCH: Windows handles it as usual */
}

BOOL WINAPI DllEntry(HINSTANCE dll, DWORD reason, void *reserved)
{
    (void)reserved;
    if (reason != 1 /* DLL_PROCESS_ATTACH */) return 1;
    DisableThreadLibraryCalls(dll);
    char path[280];
    DWORD n = GetModuleFileNameA(0, path, 280); /* splintercell3.exe: SCCTFix.log goes next to it */
    while (n && path[n - 1] != '\\') n--;
    memcpy(g_ini, path, n);
    memcpy(g_ini + n, "SCCTFix.ini", 12);
    memcpy(path + n, "SCCTFix.log", 12);
    g_log = CreateFileA(path, 0x40000000 /* GENERIC_WRITE */, 1 /* FILE_SHARE_READ */, 0, 2 /* CREATE_ALWAYS */,
                        0x80000000 /* FILE_FLAG_WRITE_THROUGH */, 0);
    log_write("SCCT Fix loaded\r\n", 17);
    QueryPerformanceFrequency(&g_freq);
    g_frame = (DWORD)g_freq / 60;
    g_spin = (DWORD)g_freq / 500;
    timeBeginPeriod(1);
    SetUnhandledExceptionFilter((void *)on_crash);
    patch_import("RegisterHotKey", (void *)hook_RegisterHotKey);
    patch_import("ShowWindow", (void *)hook_ShowWindow);
    return 1;
}
