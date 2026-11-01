/* SCCT Versus Fix (testing): a d3d8.dll for Splinter Cell: Chaos Theory Versus.
 *
 * Goes next to SCCT_Versus.ex in Versus\System. Windows loads a DLL from the
 * program's own folder first, so Versus gets this d3d8.dll; Direct3DCreate8 is
 * passed straight on to the real one in System32. The graphics are not touched.
 *
 * What it does: Versus caps how fast you can turn, whatever the mouse
 * sensitivity (the spy's camera: m_yawSpeed 40000 = ~220 degrees a second,
 * m_pitchSpeed 25000; the mercenary's aim follows the mouse through a spring:
 * fDefReticleYawSpringFactor 4.5, fDefReticlePitchSpringFactor 12). These are
 * default values in SBase.u; this DLL finds them in the game's memory and
 * multiplies them by the numbers in SCCTVersusFix.ini (1.0 = the game's own).
 * The game's files are not changed, so it still plays with everyone.
 *
 * The values are found by the properties around them (which are left alone),
 * checked again every 3 seconds (new rounds make new objects), and the ini is
 * read again each time, so a change applies without restarting.
 * SCCTVersusFix.log says what was found.
 */
typedef unsigned int UINT;
typedef unsigned long DWORD;
typedef long LONG;
typedef int BOOL;
typedef void *HANDLE, *HMODULE, *HINSTANCE;
typedef unsigned long SIZE_T;
typedef struct {
    void *BaseAddress, *AllocationBase;
    DWORD AllocationProtect;
    SIZE_T RegionSize;
    DWORD State, Protect, Type;
} MEMORY_BASIC_INFORMATION;

#define WINAPI __stdcall
#define IMP __declspec(dllimport)
IMP HANDLE WINAPI CreateFileA(const char *, DWORD, DWORD, void *, DWORD, DWORD, HANDLE);
IMP BOOL WINAPI DisableThreadLibraryCalls(HMODULE);
IMP DWORD WINAPI GetModuleFileNameA(HMODULE, char *, DWORD);
IMP void *WINAPI GetProcAddress(HMODULE, const char *);
IMP UINT WINAPI GetSystemDirectoryA(char *, UINT);
IMP HMODULE WINAPI LoadLibraryA(const char *);
IMP void WINAPI Sleep(DWORD);
IMP BOOL WINAPI WriteFile(HANDLE, const void *, DWORD, DWORD *, void *);
IMP BOOL WINAPI CloseHandle(HANDLE);
IMP HANDLE WINAPI CreateThread(void *, SIZE_T, DWORD(WINAPI *)(void *), void *, DWORD, DWORD *);
IMP SIZE_T WINAPI VirtualQuery(const void *, MEMORY_BASIC_INFORMATION *, SIZE_T);
IMP DWORD WINAPI GetPrivateProfileStringA(const char *, const char *, const char *, char *, DWORD, const char *);
IMP BOOL WINAPI SetThreadPriority(HANDLE, int);
IMP HANDLE WINAPI GetCurrentThread(void);
IMP int __cdecl wsprintfA(char *, const char *, ...);
IMP HANDLE WINAPI GetCurrentProcess(void);
IMP BOOL WINAPI ReadProcessMemory(HANDLE, const void *, void *, SIZE_T, SIZE_T *);
IMP BOOL WINAPI WriteProcessMemory(HANDLE, void *, const void *, SIZE_T, SIZE_T *);
IMP BOOL WINAPI VirtualProtect(void *, SIZE_T, DWORD, DWORD *);
IMP HMODULE WINAPI GetModuleHandleA(const char *);
IMP BOOL WINAPI QueryPerformanceCounter(long long *);
IMP BOOL WINAPI QueryPerformanceFrequency(long long *);

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

int _fltused = 0x9875; /* the compiler marks floating-point use with this (no C runtime here) */
static HANDLE g_log = (HANDLE)-1;
static char g_dir[300]; /* Versus\System\ */

static unsigned slen(const char *s)
{
    unsigned n = 0;
    while (s[n]) n++;
    return n;
}
static void log_line(const char *s)
{
    DWORD w;
    if (g_log != (HANDLE)-1) WriteFile(g_log, s, slen(s), &w, 0);
}

/* ---- diagnostics (testing): frame rate, the mouse data the game reads, display modes ----
   Written to SCCTVersusFix.log once a second while something happens. */
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
static long long g_qpf, g_sec_start;
static volatile long g_frames, g_mreads, g_mevents, g_mdx, g_mdy, g_mmax, g_mframe_dx, g_mframe_max;
static void stats_frame(void)
{
    long long now;
    QueryPerformanceCounter(&now);
    if (!g_sec_start) g_sec_start = now;
    g_frames++;
    long f = g_mframe_dx < 0 ? -g_mframe_dx : g_mframe_dx;
    if (f > g_mframe_max) g_mframe_max = f;
    g_mframe_dx = 0;
    if (now - g_sec_start >= g_qpf) {
        if (g_mevents || g_frames) {
            char b[200];
            wsprintfA(b, "fps %ld | mouse: reads %ld, events %ld, dx %ld, dy %ld, biggest event %ld, biggest x per frame %ld\r\n",
                      g_frames, g_mreads, g_mevents, g_mdx, g_mdy, g_mmax, g_mframe_max);
            static int lines;
            if (lines++ < 400) log_line(b); /* keep the log small */
        }
        g_sec_start = now;
        g_frames = g_mreads = g_mevents = g_mdx = g_mdy = g_mmax = g_mframe_max = 0;
    }
}

/* Direct3D 8: CreateDevice (IDirect3D8 slot 15) -> Present (IDirect3DDevice8 slot 15) */
typedef long(WINAPI *PresentFn)(void *, const void *, const void *, void *, const void *);
typedef long(WINAPI *CreateDevFn)(void *, UINT, UINT, void *, DWORD, DWORD *, void **);
typedef long(WINAPI *EnumModesFn)(void *, UINT, UINT, DWORD *);
typedef UINT(WINAPI *ModeCountFn)(void *, UINT);
static PresentFn g_present;
static CreateDevFn g_create_dev;
static EnumModesFn g_enum_modes;
static ModeCountFn g_mode_count;
static long WINAPI hook_Present(void *dev, const void *a, const void *b, void *w, const void *r)
{
    long hr = g_present(dev, a, b, w, r);
    stats_frame();
    return hr;
}
static long WINAPI hook_CreateDevice(void *d3d, UINT ad, UINT type, void *wnd, DWORD flags, DWORD *pp, void **out)
{
    long hr = g_create_dev(d3d, ad, type, wnd, flags, pp, out);
    char b[200];
    /* D3DPRESENT_PARAMETERS (8): width, height, format, count, multisample, swap effect, window, windowed, ... */
    wsprintfA(b, "CreateDevice %lux%lu format %lu windowed %lu refresh %lu interval %lu hr 0x%08lX\r\n", pp[0], pp[1], pp[2],
              pp[7], pp[11], pp[12], hr);
    log_line(b);
    if (hr >= 0 && out && *out) {
        void *o = hook_slot(*out, 15, (void *)hook_Present);
        if (o) g_present = (PresentFn)o;
    }
    return hr;
}
static int g_modes_logged;
static long WINAPI hook_EnumAdapterModes(void *d3d, UINT ad, UINT i, DWORD *mode)
{
    long hr = g_enum_modes(d3d, ad, i, mode);
    if (hr >= 0 && g_modes_logged < 200) { /* D3DDISPLAYMODE: width, height, refresh, format */
        char b[100];
        wsprintfA(b, "  mode %u: %lux%lu %luHz format %lu\r\n", i, mode[0], mode[1], mode[2], mode[3]);
        log_line(b);
        g_modes_logged++;
    }
    return hr;
}
static UINT WINAPI hook_GetAdapterModeCount(void *d3d, UINT ad)
{
    UINT n = g_mode_count(d3d, ad);
    char b[80];
    wsprintfA(b, "display modes offered: %u\r\n", n);
    if (g_modes_logged < 200) log_line(b);
    return n;
}

/* DirectInput 8: the game's import of DirectInput8Create -> CreateDevice (slot 3)
   -> the mouse's GetDeviceData (slot 10) / GetDeviceState (slot 9) */
typedef long(WINAPI *DI8CreateFn)(HINSTANCE, DWORD, const void *, void **, void *);
typedef long(WINAPI *DICreateDevFn)(void *, const DWORD *, void **, void *);
typedef long(WINAPI *GetDataFn)(void *, DWORD, DWORD *, DWORD *, DWORD);
typedef long(WINAPI *GetStateFn)(void *, DWORD, void *);
static DI8CreateFn g_di8create;
static DICreateDevFn g_di_create_dev;
static GetDataFn g_get_data;
static GetStateFn g_get_state;
static void *g_mice[4]; /* the game makes more than one mouse device */
static int g_nmice;
static int is_mouse(void *d) { for (int i = 0; i < g_nmice; i++) if (g_mice[i] == d) return 1; return 0; }
static const DWORD GUID_SYSMOUSE[4] = { 0x6F1D2B60, 0x11CFD5A0, 0x4544C7BF, 0x00005453 }; /* {6F1D2B60-D5A0-11CF-BFC7-444553540000} */
static long WINAPI hook_GetDeviceData(void *dev, DWORD size, DWORD *data, DWORD *n, DWORD flags)
{
    long hr = g_get_data(dev, size, data, n, flags);
    if (is_mouse(dev) && hr >= 0 && data && n && size >= 8) {
        g_mreads++;
        for (DWORD i = 0; i < *n; i++) {
            DWORD *e = (DWORD *)((char *)data + i * size); /* DIDEVICEOBJECTDATA: offset, data, ... */
            long v = (long)e[1];
            if (e[0] == 0) { g_mdx += v; g_mframe_dx += v; } /* DIMOFS_X */
            else if (e[0] == 4) g_mdy += v;                /* DIMOFS_Y */
            else continue;
            g_mevents++;
            if ((v < 0 ? -v : v) > g_mmax) g_mmax = v < 0 ? -v : v;
        }
    }
    return hr;
}
static long WINAPI hook_GetDeviceState(void *dev, DWORD size, void *state)
{
    long hr = g_get_state(dev, size, state);
    if (is_mouse(dev) && hr >= 0 && state && size >= 8) {
        long dx = ((long *)state)[0], dy = ((long *)state)[1];
        g_mreads++;
        if (dx || dy) { g_mevents++; g_mdx += dx; g_mdy += dy; g_mframe_dx += dx; }
    }
    return hr;
}
static long WINAPI hook_DICreateDevice(void *di, const DWORD *guid, void **out, void *outer)
{
    long hr = g_di_create_dev(di, guid, out, outer);
    if (hr >= 0 && out && *out && guid && guid[0] == GUID_SYSMOUSE[0] && guid[1] == GUID_SYSMOUSE[1] &&
        guid[2] == GUID_SYSMOUSE[2] && guid[3] == GUID_SYSMOUSE[3]) {
        if (g_nmice < 4) g_mice[g_nmice++] = *out;
        void *o = hook_slot(*out, 10, (void *)hook_GetDeviceData);
        if (o) g_get_data = (GetDataFn)o;
        o = hook_slot(*out, 9, (void *)hook_GetDeviceState);
        if (o) g_get_state = (GetStateFn)o;
        log_line("mouse device created (DirectInput)\r\n");
    }
    return hr;
}
static long WINAPI hook_DirectInput8Create(HINSTANCE inst, DWORD ver, const void *riid, void **out, void *outer)
{
    long hr = g_di8create(inst, ver, riid, out, outer);
    if (hr >= 0 && out && *out) {
        void *o = hook_slot(*out, 3, (void *)hook_DICreateDevice);
        if (o) g_di_create_dev = (DICreateDevFn)o;
    }
    return hr;
}
/* the game's own import table entry for dinput8!DirectInput8Create */
static int g_di_patched;
static void patch_dinput(void)
{
    if (g_di_patched) return;
    unsigned char *base = (unsigned char *)GetModuleHandleA(0);
    DWORD rva = *(DWORD *)(base + *(LONG *)(base + 0x3c) + 0x80);
    HMODULE di = GetModuleHandleA("dinput8.dll"); /* already loaded with the game (no loading inside DllMain) */
    void *target = di ? GetProcAddress(di, "DirectInput8Create") : 0;
    if (!rva || !target) return;
    g_di8create = (DI8CreateFn)target;
    g_di_patched = 1;
    for (DWORD *desc = (DWORD *)(base + rva); desc[3]; desc += 5)
        for (void **iat = (void **)(base + desc[4]); *iat; iat++)
            if (*iat == target) {
                DWORD old;
                VirtualProtect(iat, 4, 4, &old);
                *iat = (void *)hook_DirectInput8Create;
                VirtualProtect(iat, 4, old, &old);
                log_line("watching the mouse (DirectInput)\r\n");
            }
}

/* ---- Direct3DCreate8: the real one (its CreateDevice and mode list watched) ---- */
typedef void *(WINAPI *Create8Fn)(UINT);
__declspec(dllexport) void *WINAPI Direct3DCreate8(UINT sdk)
{
    static Create8Fn real;
    if (!real) {
        char path[300];
        UINT n = GetSystemDirectoryA(path, 260);
        memcpy(path + n, "\\d3d8.dll", 10);
        real = (Create8Fn)GetProcAddress(LoadLibraryA(path), "Direct3DCreate8");
        if (!real) { log_line("Could not load system d3d8.dll\r\n"); return 0; }
    }
    patch_dinput(); /* in case it wasn't possible at load */
    void *d3d = real(sdk);
    if (d3d) {
        void *o = hook_slot(d3d, 15, (void *)hook_CreateDevice);
        if (o) g_create_dev = (CreateDevFn)o;
        o = hook_slot(d3d, 6, (void *)hook_GetAdapterModeCount);
        if (o) g_mode_count = (ModeCountFn)o;
        o = hook_slot(d3d, 7, (void *)hook_EnumAdapterModes);
        if (o) g_enum_modes = (EnumModesFn)o;
    }
    return d3d;
}

/* ---- the values ---- */
static float f_of(DWORD u) { union { DWORD u; float f; } x; x.u = u; return x.f; }
static DWORD u_of(float f) { union { DWORD u; float f; } x; x.f = f; return x.u; }

#define F_40000 0x471C4000u
#define F_25000 0x46C35000u
#define F_30000 0x46EA6000u
#define F_0_2 0x3E4CCCCDu
#define F_4_5 0x40900000u
#define F_12 0x41400000u
#define F_60000 0x476A6000u
#define F_0_1 0x3DCCCCCDu
#define F_0_15 0x3E19999Au
#define F_0_25 0x3E800000u
#define I_MINPITCH 0xFFFFC004u /* -16380 */
#define I_MAXPITCH 0x00003FFCu /*  16380 */

/* a value already set by us is "plausible": between 1/20 and 20 times the game's */
static int plausible(DWORD now, DWORD orig)
{
    float v = f_of(now), o = f_of(orig);
    return v >= o / 20 && v <= o * 20;
}

static float read_mult(const char *key)
{
    char ini[320], v[32];
    wsprintfA(ini, "%sSCCTVersusFix.ini", g_dir);
    GetPrivateProfileStringA("Turning", key, "1.0", v, sizeof v, ini);
    /* "1.5" -> 1.5 (no C runtime) */
    float r = 0, scale = 1;
    int i = 0, dot = 0;
    for (; v[i]; i++) {
        if (v[i] == '.' || v[i] == ',') { dot = 1; continue; }
        if (v[i] < '0' || v[i] > '9') break;
        if (dot) { scale /= 10; r += (v[i] - '0') * scale; }
        else r = r * 10 + (v[i] - '0');
    }
    if (r < 0.1f) r = 0.1f;
    if (r > 10) r = 10;
    return r;
}

static int g_cams, g_mercs;
/* The game's memory is copied out and written back with Read/WriteProcessMemory,
   so memory the game frees meanwhile is just skipped (no crash). */
static void put(DWORD *game, DWORD now, DWORD orig, float mult)
{
    if (!plausible(now, orig)) return;
    DWORD want = u_of(f_of(orig) * mult);
    SIZE_T k;
    if (now != want) WriteProcessMemory(GetCurrentProcess(), game, &want, 4, &k);
}

/* one chunk: the spy's camera and the mercenary's aim */
static void scan_chunk(const DWORD *w, SIZE_T n, DWORD *game, float spy, float merc)
{
    for (SIZE_T i = 3; i + 4 < n; i++) {
        DWORD a = w[i];
        if (a == I_MINPITCH && w[i + 1] == I_MAXPITCH && w[i + 2] == F_0_2 && w[i + 3] == F_30000 &&
            plausible(w[i - 2], F_40000) && plausible(w[i - 1], F_25000)) {
            put(game + i - 2, w[i - 2], F_40000, spy);
            put(game + i - 1, w[i - 1], F_25000, spy);
            g_cams++;
        } else if (a == F_0_1 && w[i + 1] == F_0_15 && w[i + 2] == F_0_25 &&
                   plausible(w[i - 3], F_4_5) && plausible(w[i - 2], F_12) && w[i - 1] == F_60000) {
            put(game + i - 3, w[i - 3], F_4_5, merc);
            put(game + i - 2, w[i - 2], F_12, merc);
            g_mercs++;
        }
    }
}

#define CHUNK 16384 /* words */
static DWORD g_buf[CHUNK];
static void scan_region(DWORD *base, SIZE_T words, float spy, float merc)
{
    /* chunks overlap by 8 words so nothing on a boundary is missed */
    for (SIZE_T at = 0; at < words; at += CHUNK - 8) {
        SIZE_T n = words - at < CHUNK ? words - at : CHUNK, got = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), base + at, g_buf, n * 4, &got) || got != n * 4) continue;
        scan_chunk(g_buf, n, base + at, spy, merc);
        if (n < CHUNK) break;
    }
}

static DWORD WINAPI worker(void *arg)
{
    (void)arg;
    SetThreadPriority(GetCurrentThread(), -1 /* below normal */);
    Sleep(5000);
    int last_c = -1, last_m = -1;
    for (;;) {
        float spy = read_mult("Spy"), merc = read_mult("Merc");
        g_cams = g_mercs = 0;
        MEMORY_BASIC_INFORMATION mi;
        unsigned char *p = (unsigned char *)0x10000;
        while ((DWORD)p < 0xFFFE0000u && VirtualQuery(p, &mi, sizeof mi) == sizeof mi) {
            unsigned char *next = (unsigned char *)mi.BaseAddress + mi.RegionSize;
            if (mi.State == 0x1000 /* MEM_COMMIT */ && mi.Type == 0x20000 /* MEM_PRIVATE */ &&
                (mi.Protect & 0xFF) == 0x04 /* PAGE_READWRITE, not guard/nocache */)
                scan_region((DWORD *)mi.BaseAddress, mi.RegionSize / 4, spy, merc);
            if (next <= p) break;
            p = next;
        }
        if (g_cams != last_c || g_mercs != last_m) {
            char b[160];
            wsprintfA(b, "found: spy camera %d, mercenary aim %d (spy x%d.%02d, merc x%d.%02d)\r\n", g_cams, g_mercs,
                      (int)spy, (int)(spy * 100) % 100, (int)merc, (int)(merc * 100) % 100);
            log_line(b);
            last_c = g_cams;
            last_m = g_mercs;
        }
        Sleep(3000);
    }
    return 0;
}

BOOL WINAPI DllEntry(HINSTANCE dll, DWORD reason, void *reserved)
{
    (void)reserved;
    if (reason != 1 /* DLL_PROCESS_ATTACH */) return 1;
    DisableThreadLibraryCalls(dll);
    DWORD n = GetModuleFileNameA(0, g_dir, 280);
    while (n && g_dir[n - 1] != '\\') n--;
    g_dir[n] = 0;
    char path[320];
    wsprintfA(path, "%sSCCTVersusFix.log", g_dir);
    g_log = CreateFileA(path, 0x40000000, 1, 0, 2, 0x80000000, 0);
    log_line("SCCT Versus Fix loaded (testing)\r\n");
    QueryPerformanceFrequency(&g_qpf);
    patch_dinput();
    HANDLE t = CreateThread(0, 0, worker, 0, 0, 0);
    if (t) CloseHandle(t);
    return 1;
}
