/* SCCT Versus Fix: a d3d8.dll for Splinter Cell: Chaos Theory Versus.
 *
 * Goes next to SCCT_Versus.ex in Versus\System. Windows loads a DLL from the
 * program's own folder first, so Versus gets this d3d8.dll; Direct3DCreate8 is
 * passed on to the real one in System32 (the drawing itself is not changed).
 * SCCTLink installs it and writes its settings to SCCTVersusFix.ini; what it
 * did is in SCCTVersusFix.log. The game's files are never changed, so it plays
 * with everyone (with or without the fix).
 *
 *   [Display] MaxFPS      Versus holds itself at half the screen's refresh (30 FPS
 *                         at 60 Hz). Only that one division is pointed at our
 *                         number (see patch_limiter). 0 = no limit.
 *             Resolution  e.g. 1920x1080: replaces the menu's last choice
 *                         (1600x1200, the most Versus offers) - pick it in the game.
 *             HUD         1: the HUD keeps its shape on wide screens (it was
 *                         stretched: x scaled by width/640, y by height/480).
 *   [Mouse]   Rate125     1: the game gets the mouse as 125 reports a second.
 *                         Faster mice (500/1000 Hz) make Versus' aim slow and
 *                         zigzag ("pixel jumping"); 125 Hz is what it handles.
 *   [FOV]     Spy, Merc   field of view (game: 75 and 95). 0 = the game's own.
 *   [Turning] Spy, Merc   how fast you can turn (game's cap: 1.0; the spy's camera
 *                         turns at most ~220 degrees a second). SpyVertical and
 *                         MercVertical: the same for up/down (1.0 if missing).
 *
 * The values in memory are found by the data around them, re-checked every few
 * seconds (new rounds make new objects), and the ini is read again each time.
 * Memory is read and written through Read/WriteProcessMemory, so memory the
 * game frees meanwhile is just skipped.
 *
 * Build: fix/build_fix.sh (clang + lld, 32-bit, no C runtime).
 */
typedef unsigned int UINT;
typedef unsigned long DWORD;
typedef long LONG;
typedef int BOOL;
typedef void *HANDLE, *HMODULE, *HINSTANCE;
typedef unsigned long SIZE_T;
typedef unsigned short WCHAR16;
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
IMP HANDLE WINAPI GetCurrentProcess(void);
IMP BOOL WINAPI ReadProcessMemory(HANDLE, const void *, void *, SIZE_T, SIZE_T *);
IMP BOOL WINAPI WriteProcessMemory(HANDLE, void *, const void *, SIZE_T, SIZE_T *);
IMP BOOL WINAPI VirtualProtect(void *, SIZE_T, DWORD, DWORD *);
IMP HMODULE WINAPI GetModuleHandleA(const char *);
IMP BOOL WINAPI QueryPerformanceCounter(long long *);
IMP BOOL WINAPI QueryPerformanceFrequency(long long *);
IMP UINT WINAPI timeBeginPeriod(UINT);
IMP int __cdecl wsprintfA(char *, const char *, ...);

/* no C runtime */
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
static unsigned slen(const char *s)
{
    unsigned n = 0;
    while (s[n]) n++;
    return n;
}

static HANDLE g_log = (HANDLE)-1;
static char g_dir[300]; /* Versus\System\ */
static long long g_qpf;

static void log_line(const char *s)
{
    DWORD w;
    if (g_log != (HANDLE)-1) WriteFile(g_log, s, slen(s), &w, 0);
}

/* ---- SCCTVersusFix.ini ---- */
static void read_str(const char *sec, const char *key, char *v, DWORD n)
{
    char ini[320];
    wsprintfA(ini, "%sSCCTVersusFix.ini", g_dir);
    GetPrivateProfileStringA(sec, key, "", v, n, ini);
}
static float read_float(const char *sec, const char *key, float def)
{
    char v[32];
    read_str(sec, key, v, sizeof v);
    if (!v[0]) return def;
    float r = 0, scale = 1;
    int dot = 0;
    for (int i = 0; v[i]; i++) {
        if (v[i] == '.' || v[i] == ',') { dot = 1; continue; }
        if (v[i] < '0' || v[i] > '9') break;
        if (dot) { scale /= 10; r += (v[i] - '0') * scale; }
        else r = r * 10 + (v[i] - '0');
    }
    return r;
}
static int read_int(const char *sec, const char *key, int def)
{
    char v[32];
    read_str(sec, key, v, sizeof v);
    if (!v[0]) return def;
    int r = 0;
    for (int i = 0; v[i] >= '0' && v[i] <= '9'; i++) r = r * 10 + (v[i] - '0');
    return r;
}

/* ---- patching helpers ---- */
static void *hook_slot(void *obj, int slot, void *hook) /* a COM method; returns the original */
{
    void **vt = *(void ***)obj, *orig = vt[slot];
    if (orig == hook) return 0;
    DWORD old;
    VirtualProtect(&vt[slot], 4, 4, &old);
    vt[slot] = hook;
    VirtualProtect(&vt[slot], 4, old, &old);
    return orig;
}
static int patch_import(const char *dll, const char *fn, void *hook) /* the game's own import table */
{
    unsigned char *base = (unsigned char *)GetModuleHandleA(0);
    DWORD rva = *(DWORD *)(base + *(LONG *)(base + 0x3c) + 0x80);
    HMODULE m = GetModuleHandleA(dll);
    void *target = m ? GetProcAddress(m, fn) : 0;
    int k = 0;
    if (!rva || !target) return 0;
    for (DWORD *desc = (DWORD *)(base + rva); desc[3]; desc += 5)
        for (void **iat = (void **)(base + desc[4]); *iat; iat++)
            if (*iat == target) {
                DWORD old;
                VirtualProtect(iat, 4, 4, &old);
                *iat = hook;
                VirtualProtect(iat, 4, old, &old);
                k++;
            }
    return k;
}

/* ---- the frame limiter ----
   Versus waits each frame until (2.0 / refresh rate) - 1 ms have passed. The code
   is in SCCT_Versus.ex (Steam copy, fixed address 0x10900000):
       1095e356: fdivr dword ptr [0x10c10acc]   ; 2.0 / refresh   (d8 3d cc 0a c1 10)
   The constant 2.0 is used in 66 other places, so only this instruction is
   pointed at our number: refresh / MaxFPS. Any other exe: left alone. */
static float g_frame_div = 2.0f; /* the game's own until the device exists */
static int g_limiter_patched;
static void patch_limiter(void)
{
    unsigned char *ins = (unsigned char *)0x1095e356;
    static const unsigned char want[6] = { 0xd8, 0x3d, 0xcc, 0x0a, 0xc1, 0x10 };
    MEMORY_BASIC_INFORMATION mi;
    if (!VirtualQuery(ins, &mi, sizeof mi) || mi.State != 0x1000) return;
    for (int i = 0; i < 6; i++)
        if (ins[i] != want[i]) { log_line("frame limiter: not the Steam copy's code - left alone\r\n"); return; }
    DWORD old, a = (DWORD)&g_frame_div;
    VirtualProtect(ins + 2, 4, 0x40 /* PAGE_EXECUTE_READWRITE */, &old);
    memcpy(ins + 2, &a, 4);
    VirtualProtect(ins + 2, 4, old, &old);
    g_limiter_patched = 1;
}
static void set_frame_rate(int refresh)
{
    int fps = read_int("Display", "MaxFPS", 60);
    if (refresh <= 0) refresh = 60;
    g_frame_div = fps <= 0 ? 0.0f : (float)refresh / (float)fps;
    char b[100];
    if (fps <= 0) wsprintfA(b, "frame rate: no limit (screen %d Hz)\r\n", refresh);
    else wsprintfA(b, "frame rate: up to %d FPS (screen %d Hz)\r\n", fps, refresh);
    log_line(b);
}

/* ---- Direct3D 8: straight to the real one; its CreateDevice is watched ---- */
typedef long(WINAPI *CreateDevFn)(void *, UINT, UINT, void *, DWORD, DWORD *, void **);
static CreateDevFn g_create_dev;
static long WINAPI hook_CreateDevice(void *d3d, UINT ad, UINT type, void *wnd, DWORD flags, DWORD *pp, void **out)
{
    if (g_limiter_patched) set_frame_rate((int)pp[11] ? (int)pp[11] : 60);
    long hr = g_create_dev(d3d, ad, type, wnd, flags, pp, out);
    char b[120]; /* D3DPRESENT_PARAMETERS (8): width, height, ..., windowed at 7 */
    wsprintfA(b, "screen: %lux%lu%s\r\n", pp[0], pp[1], pp[7] ? " (window)" : "");
    log_line(b);
    return hr;
}
typedef void *(WINAPI *Create8Fn)(UINT);
static void patch_dinput(void);
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
    }
    return d3d;
}

/* ---- the mouse as Versus expects it: 125 reports a second ----
   Each read's movement is summed and handed back as one report per 8 ms that
   passed (125 a second): the same total movement, in the steps Versus handles.
   Buttons and the wheel are passed on unchanged and first. */
typedef long(WINAPI *DI8CreateFn)(HINSTANCE, DWORD, const void *, void **, void *);
typedef long(WINAPI *DICreateDevFn)(void *, const DWORD *, void **, void *);
typedef long(WINAPI *GetDataFn)(void *, DWORD, DWORD *, DWORD *, DWORD);
static DI8CreateFn g_di8create;
static DICreateDevFn g_di_create_dev;
static GetDataFn g_get_data;
static void *g_mice[4]; /* the game makes more than one mouse device */
static int g_nmice, g_rate125 = 1;
static const DWORD GUID_SYSMOUSE[4] = { 0x6F1D2B60, 0x11CFD5A0, 0x4544C7BF, 0x00005453 }; /* {6F1D2B60-D5A0-11CF-BFC7-444553540000} */
static int is_mouse(void *d)
{
    for (int i = 0; i < g_nmice; i++) if (g_mice[i] == d) return 1;
    return 0;
}
/* Versus reads the mouse one event at a time (it asks for 1, again and again,
   until none are left). So at the first read of a frame the fix takes all of
   the mouse's events from DirectInput itself, regroups the movement into one
   report per 8 ms that passed, and then hands those out one by one. */
#define QMAX 96
static struct MouseQ {
    void *dev;
    DWORD ev[QMAX][5]; /* DIDEVICEOBJECTDATA: offset, data, time stamp, sequence, app data */
    int n, pos;
    long long last;
} g_q[4];
static struct MouseQ *mouse_q(void *dev)
{
    for (int i = 0; i < 4; i++) if (g_q[i].dev == dev) return &g_q[i];
    for (int i = 0; i < 4; i++) if (!g_q[i].dev) { g_q[i].dev = dev; return &g_q[i]; }
    return 0;
}
static long fill_q(struct MouseQ *q, void *dev, DWORD size)
{
    static DWORD raw[256 * 5];
    long dx = 0, dy = 0, hr = 0;
    DWORD stamp = 0, seq = 0;
    q->n = q->pos = 0;
    for (int round = 0; round < 8; round++) { /* everything DirectInput has (normally one round) */
        DWORD got = 256;
        hr = g_get_data(dev, 20, raw, &got, 0);
        if (hr < 0) return hr;
        for (DWORD i = 0; i < got; i++) {
            DWORD *e = raw + i * 5;
            if (e[0] == 0 || e[0] == 4) { /* DIMOFS_X / DIMOFS_Y */
                if (e[0] == 0) dx += (long)e[1]; else dy += (long)e[1];
                stamp = e[2]; seq = e[3];
            } else if (q->n < QMAX) { /* buttons, wheel: as they came */
                memcpy(q->ev[q->n++], e, 20);
            }
        }
        if (got < 256) break;
    }
    long long now;
    QueryPerformanceCounter(&now);
    long long since = q->last ? now - q->last : 0;
    q->last = now;
    if (!dx && !dy) return hr;
    DWORD reports = g_qpf ? (DWORD)((float)since * 125.0f / (float)g_qpf + 0.5f) : 1;
    if (reports < 1) reports = 1;
    if (reports > 16) reports = 16;
    for (DWORD r = 0; r < reports; r++) {
        long px = dx * (long)(r + 1) / (long)reports - dx * (long)r / (long)reports;
        long py = dy * (long)(r + 1) / (long)reports - dy * (long)r / (long)reports;
        long vals[2] = { px, py };
        for (int k = 0; k < 2 && q->n < QMAX; k++) {
            if (!vals[k]) continue;
            DWORD *e = q->ev[q->n++];
            e[0] = k ? 4 : 0; e[1] = (DWORD)vals[k]; e[2] = stamp; e[3] = seq + r; e[4] = 0;
        }
    }
    (void)size;
    return hr;
}
static long WINAPI hook_GetDeviceData(void *dev, DWORD size, DWORD *data, DWORD *n, DWORD flags)
{
    struct MouseQ *q;
    if (!g_rate125 || !is_mouse(dev) || !n || size < 16 || size > 20 || !(q = mouse_q(dev)))
        return g_get_data(dev, size, data, n, flags);
    if (!data) { q->n = q->pos = 0; return g_get_data(dev, size, data, n, flags); } /* flush: pass on */
    long hr = 0;
    if (q->pos >= q->n) { /* a new frame: take everything from DirectInput */
        hr = fill_q(q, dev, size);
        if (hr < 0) { *n = 0; return hr; }
    }
    DWORD want = *n, give = 0;
    while (give < want && q->pos + (int)give < q->n) {
        memcpy((char *)data + give * size, q->ev[q->pos + give], size);
        give++;
    }
    if (!(flags & 1 /* DIGDD_PEEK */)) q->pos += (int)give;
    *n = give;
    return give && hr == 1 /* DI_BUFFEROVERFLOW */ ? hr : 0;
}
static long WINAPI hook_DICreateDevice(void *di, const DWORD *guid, void **out, void *outer)
{
    long hr = g_di_create_dev(di, guid, out, outer);
    if (hr >= 0 && out && *out && guid && guid[0] == GUID_SYSMOUSE[0] && guid[1] == GUID_SYSMOUSE[1] &&
        guid[2] == GUID_SYSMOUSE[2] && guid[3] == GUID_SYSMOUSE[3]) {
        if (g_nmice < 4) g_mice[g_nmice++] = *out;
        void *o = hook_slot(*out, 10, (void *)hook_GetDeviceData); /* IDirectInputDevice8::GetDeviceData */
        if (o) g_get_data = (GetDataFn)o;
    }
    return hr;
}
static long WINAPI hook_DirectInput8Create(HINSTANCE inst, DWORD ver, const void *riid, void **out, void *outer)
{
    long hr = g_di8create(inst, ver, riid, out, outer);
    if (hr >= 0 && out && *out) {
        void *o = hook_slot(*out, 3, (void *)hook_DICreateDevice); /* IDirectInput8::CreateDevice */
        if (o) g_di_create_dev = (DICreateDevFn)o;
    }
    return hr;
}
static void patch_dinput(void)
{
    static int done;
    if (done) return;
    HMODULE di = GetModuleHandleA("dinput8.dll"); /* loaded with the game (no loading inside DllMain) */
    void *target = di ? GetProcAddress(di, "DirectInput8Create") : 0;
    if (!target) return;
    g_di8create = (DI8CreateFn)target;
    if (patch_import("dinput8.dll", "DirectInput8Create", (void *)hook_DirectInput8Create)) done = 1;
}

/* ---- values in the game's memory ---- */
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
#define F_95 0x42BE0000u
#define F_75 0x42960000u
#define I_MINPITCH 0xFFFFC004u /* -16380 */
#define I_MAXPITCH 0x00003FFCu /*  16380 */

static int plausible(DWORD now, DWORD orig) /* ours or the game's: within 20x of the game's value */
{
    float v = f_of(now), o = f_of(orig);
    return v >= o / 20 && v <= o * 20;
}
static int fov_ok(DWORD v) { float f = f_of(v); return f >= 20 && f <= 170; }
static void put(DWORD *game, DWORD now, DWORD want)
{
    SIZE_T k;
    if (now != want) WriteProcessMemory(GetCurrentProcess(), game, &want, 4, &k);
}
static void put_bytes(void *game, const void *src, SIZE_T n)
{
    SIZE_T k;
    WriteProcessMemory(GetCurrentProcess(), game, src, n, &k);
}

static struct {
    float spy_yaw, spy_pitch, merc_yaw, merc_pitch, spy_fov, merc_fov;
    int hud;
    char res[16];       /* "1920x1080" or "" */
    WCHAR16 wres[16];
    char label[20];     /* "1920 * 1080" */
    WCHAR16 wlabel[20];
} S;
static int g_cams, g_mercs, g_fovs, g_huds, g_ress;
static int g_fov_back; /* words between SpyFOV and the mercenary's yaw spring, once found */

/* the spy's camera (turn speed caps) and the mercenary's aim (springs; the FOVs are just before) */
static void scan_words(const DWORD *w, SIZE_T n, DWORD *game)
{
    for (SIZE_T i = 16; i + 4 < n; i++) {
        DWORD a = w[i];
        if (a == I_MINPITCH && w[i + 1] == I_MAXPITCH && w[i + 2] == F_0_2 && w[i + 3] == F_30000 &&
            plausible(w[i - 2], F_40000) && plausible(w[i - 1], F_25000)) {
            put(game + i - 2, w[i - 2], u_of(f_of(F_40000) * S.spy_yaw));
            put(game + i - 1, w[i - 1], u_of(f_of(F_25000) * S.spy_pitch));
            g_cams++;
        } else if (a == F_0_1 && w[i + 1] == F_0_15 && w[i + 2] == F_0_25 && w[i - 1] == F_60000 &&
                   plausible(w[i - 3], F_4_5) && plausible(w[i - 2], F_12)) {
            put(game + i - 3, w[i - 3], u_of(f_of(F_4_5) * S.merc_yaw));
            put(game + i - 2, w[i - 2], u_of(f_of(F_12) * S.merc_pitch));
            g_mercs++;
            /* DefenderFOV, SpyFOV: the first time by their own values (95, 75), then by where they were */
            if (!g_fov_back)
                for (int k = 1; k <= 12; k++)
                    if (w[i - 3 - k] == F_75 && w[i - 4 - k] == F_95) { g_fov_back = k; break; }
            if (g_fov_back) {
                SIZE_T s = i - 3 - (SIZE_T)g_fov_back;
                if (fov_ok(w[s]) && fov_ok(w[s - 1])) {
                    put(game + s, w[s], S.spy_fov > 0 ? u_of(S.spy_fov) : F_75);
                    put(game + s - 1, w[s - 1], S.merc_fov > 0 ? u_of(S.merc_fov) : F_95);
                    g_fovs++;
                }
            }
        }
    }
}

/* The HUD: SHud.PostRender computes
       ResolutionRatioX = SizeX / 640.0;  ResolutionRatioY = SizeY / 480.0;
   In memory its script is (object references are 4-byte pointers):
       0f 01 [RatioX] ac 01 [SizeX] 1e 00 00 20 44 16  0f 01 [RatioY] ac 01 [SizeY] 1e 00 00 f0 43 16
   Pointing the first one at SizeY / 480 makes RatioX = RatioY: no stretching. */
static int hud_at(const unsigned char *b, SIZE_T i)
{
    /* 0  0f 01 [RatioX 2..5] ac 01 [SizeX 8..11] 1e [640.0 13..16] 16
       18 0f 01 [RatioY 20..23] ac 01 [SizeY 26..29] 1e [480.0 31..34] 16 */
    return b[i] == 0x0f && b[i + 1] == 0x01 && b[i + 6] == 0xac && b[i + 7] == 0x01 && b[i + 12] == 0x1e &&
           b[i + 17] == 0x16 && b[i + 18] == 0x0f && b[i + 19] == 0x01 && b[i + 24] == 0xac && b[i + 25] == 0x01 &&
           b[i + 30] == 0x1e && b[i + 31] == 0x00 && b[i + 32] == 0x00 && b[i + 33] == 0xf0 && b[i + 34] == 0x43 &&
           b[i + 35] == 0x16;
}
static int bytes_eq(const unsigned char *a, const unsigned char *b, SIZE_T n)
{
    for (SIZE_T i = 0; i < n; i++) if (a[i] != b[i]) return 0;
    return 1;
}
static const unsigned char RES_OLD[] = "1600x1200", LABEL_OLD[] = "1600 * 1200";
static const WCHAR16 WRES_OLD[] = { '1', '6', '0', '0', 'x', '1', '2', '0', '0', 0 };
static const WCHAR16 WLABEL_OLD[] = { '1', '6', '0', '0', ' ', '*', ' ', '1', '2', '0', '0', 0 };
static void scan_bytes(const unsigned char *b, SIZE_T n, unsigned char *game)
{
    for (SIZE_T i = 0; i + 40 < n; i++) {
        unsigned char c = b[i];
        if (c == 0x0f && S.hud && hud_at(b, i) && b[i + 13] == 0x00 && b[i + 14] == 0x00 && b[i + 15] == 0x20 &&
            b[i + 16] == 0x44) { /* still SizeX / 640.0 */
            static const unsigned char f480[4] = { 0x00, 0x00, 0xf0, 0x43 };
            put_bytes(game + i + 8, b + i + 26, 4); /* SizeY */
            put_bytes(game + i + 13, f480, 4);      /* 480.0 */
            g_huds++;
        } else if (c == '1' && S.res[0]) {
            /* the menu's last resolution and its label, plain or wide text */
            if (bytes_eq(b + i, RES_OLD, 10)) { put_bytes(game + i, S.res, 10); g_ress++; }
            else if (bytes_eq(b + i, LABEL_OLD, 12)) { put_bytes(game + i, S.label, 12); g_ress++; }
            else if (bytes_eq(b + i, (const unsigned char *)WRES_OLD, 20)) { put_bytes(game + i, S.wres, 20); g_ress++; }
            else if (bytes_eq(b + i, (const unsigned char *)WLABEL_OLD, 24)) { put_bytes(game + i, S.wlabel, 24); g_ress++; }
        }
    }
}

#define CHUNK 65536 /* bytes */
static unsigned char g_buf[CHUNK];
static void scan_region(unsigned char *base, SIZE_T bytes)
{
    /* chunks overlap by 64 bytes so nothing on a boundary is missed */
    for (SIZE_T at = 0; at < bytes; at += CHUNK - 64) {
        SIZE_T n = bytes - at < CHUNK ? bytes - at : CHUNK, got = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), base + at, g_buf, n, &got) || got != n) continue;
        scan_words((const DWORD *)g_buf, n / 4, (DWORD *)(base + at));
        if (S.hud || S.res[0]) scan_bytes(g_buf, n, base + at);
        if (n < CHUNK) break;
    }
}

static void read_settings(void)
{
    S.spy_yaw = read_float("Turning", "Spy", 1);
    S.spy_pitch = read_float("Turning", "SpyVertical", 1);
    S.merc_yaw = read_float("Turning", "Merc", 1);
    S.merc_pitch = read_float("Turning", "MercVertical", 1);
    float *t[4] = { &S.spy_yaw, &S.spy_pitch, &S.merc_yaw, &S.merc_pitch };
    for (int i = 0; i < 4; i++) { if (*t[i] < 0.1f) *t[i] = 0.1f; if (*t[i] > 10) *t[i] = 10; }
    S.spy_fov = read_float("FOV", "Spy", 0);
    S.merc_fov = read_float("FOV", "Merc", 0);
    if (S.spy_fov && (S.spy_fov < 40 || S.spy_fov > 150)) S.spy_fov = 0;
    if (S.merc_fov && (S.merc_fov < 40 || S.merc_fov > 150)) S.merc_fov = 0;
    S.hud = read_int("Display", "HUD", 1);
    /* "1920x1080": at most 9 characters, so it fits where "1600x1200" was */
    char r[32];
    read_str("Display", "Resolution", r, sizeof r);
    int w = 0, h = 0, i = 0;
    while (r[i] >= '0' && r[i] <= '9') w = w * 10 + (r[i++] - '0');
    if (r[i] == 'x' || r[i] == 'X') i++;
    while (r[i] >= '0' && r[i] <= '9') h = h * 10 + (r[i++] - '0');
    memset(S.res, 0, sizeof S.res);
    memset(S.wres, 0, sizeof S.wres);
    memset(S.label, 0, sizeof S.label);
    memset(S.wlabel, 0, sizeof S.wlabel);
    if (w >= 640 && w <= 9999 && h >= 480 && h <= 9999 && !(w == 1600 && h == 1200)) {
        wsprintfA(S.res, "%dx%d", w, h);
        wsprintfA(S.label, "%d * %d", w, h);
        for (i = 0; S.res[i]; i++) S.wres[i] = (WCHAR16)S.res[i];
        for (i = 0; S.label[i]; i++) S.wlabel[i] = (WCHAR16)S.label[i];
    }
}

static DWORD WINAPI worker(void *arg)
{
    (void)arg;
    SetThreadPriority(GetCurrentThread(), -1 /* below normal */);
    int last[5] = { -1, -1, -1, -1, -1 };
    for (int pass = 0;; pass++) {
        read_settings();
        g_cams = g_mercs = g_fovs = g_huds = g_ress = 0;
        MEMORY_BASIC_INFORMATION mi;
        unsigned char *p = (unsigned char *)0x10000;
        while ((DWORD)p < 0xFFFE0000u && VirtualQuery(p, &mi, sizeof mi) == sizeof mi) {
            unsigned char *next = (unsigned char *)mi.BaseAddress + mi.RegionSize;
            if (mi.State == 0x1000 /* MEM_COMMIT */ && mi.Type == 0x20000 /* MEM_PRIVATE */ &&
                (mi.Protect & 0xFF) == 0x04 /* PAGE_READWRITE, not guard/nocache */)
                scan_region((unsigned char *)mi.BaseAddress, mi.RegionSize);
            if (next <= p) break;
            p = next;
        }
        int now[5] = { g_cams, g_mercs, g_fovs, g_huds, g_ress };
        if (now[0] != last[0] || now[1] != last[1] || now[2] != last[2] || now[3] || now[4]) {
            char b[200];
            wsprintfA(b, "found: spy camera %d, mercenary aim %d, FOV %d; changed now: HUD %d, resolution texts %d\r\n",
                      now[0], now[1], now[2], now[3], now[4]);
            log_line(b);
            for (int i = 0; i < 5; i++) last[i] = now[i];
        }
        Sleep(pass < 60 ? 1000 : 3000); /* quick while the menu and the first round load */
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
    log_line("SCCT Versus Fix loaded\r\n");
    QueryPerformanceFrequency(&g_qpf);
    /* Windows' timer ticks every 15.6 ms unless a program asks for 1 ms (since Windows 10 2004
       only for that program); Versus sleeps between frames, so it would land on ~22 FPS. */
    timeBeginPeriod(1);
    { /* per-monitor DPI aware: on a scaled desktop (125 %, 150 %) Windows would otherwise show Versus smaller and stretched */
        typedef BOOL(WINAPI * SetCtxFn)(HANDLE);
        SetCtxFn f = (SetCtxFn)GetProcAddress(GetModuleHandleA("user32.dll"), "SetProcessDpiAwarenessContext");
        if (f) f((HANDLE)-4 /* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 */);
    }
    g_rate125 = read_int("Mouse", "Rate125", 1) != 0;
    patch_limiter();
    log_line(g_limiter_patched ? "frame limiter: found\r\n" : "frame limiter: not found\r\n");
    patch_dinput();
    log_line(g_rate125 ? "mouse: 125 reports a second\r\n" : "mouse: as it comes\r\n");
    HANDLE t = CreateThread(0, 0, worker, 0, 0, 0);
    if (t) CloseHandle(t);
    return 1;
}
