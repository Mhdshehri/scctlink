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
 *     Single player may choose another limit (SCCTFix.ini [Display]
 *     SingleFPS); during an SCCTLink co-op session it is 60 regardless.
 *   - Borderless window: CreateDevice()/Reset() always ask for a window (no
 *     exclusive fullscreen, no V-Sync), and the window is made to fill its
 *     monitor with no border.
 *   - Alt+Tab: the game grabs Alt+Tab for itself (RegisterHotKey "UnrealAltTab")
 *     and minimizes on focus loss; both are blocked.
 *   - Crash log: a crash's code and address are written to SCCTFix.log.
 *   - Screen resolution: in its borderless window the game renders at the
 *     monitor's own resolution (2K, ultrawide, ...); see below.
 *   - Raw mouse (optional, SCCTFix.ini [Mouse] Raw=1): the mouse is read as
 *     Windows Raw Input (no acceleration, any polling rate); see below.
 *   - HUD fix (optional, SCCTFix.ini [HUD] Fix=1): the game stretches its
 *     640x480 HUD, menus and text sideways on a wide screen. With it they are
 *     drawn 4:3 in the middle, not stretched (ported from ThirteenAG's
 *     FusionFix, MIT; see the HUD fix below). Drawing only: the co-op
 *     simulation isn't touched.
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
IMP DWORD WINAPI GetPrivateProfileStringA(const char *, const char *, const char *, char *, DWORD, const char *);

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
   Ported from ThirteenAG's SplinterCellChaosTheory.FusionFix (MIT License,
   Copyright (c) 2018 ThirteenAG; see THIRD-PARTY.txt), rewritten in plain C
   for this DLL, for the Steam splintercell3.exe (fixed address 0x10900000).

   The game draws its HUD, menus and text in 640x480 steps and stretches x to
   the screen: x' = x * (2/640) - 1. With the fix the scale and the offset are
   ours, x' = x * 2h/(480w) - (4/3)(h/w): the same 640x480 picture, 4:3 in the
   middle of the screen, not stretched. Pictures meant to fill the screen (and
   the camera page) keep the game's numbers, the scope's borders reach the
   edges, and the loading bar and the scope are sized to match.

   Places the game reads its numbers from are pointed at ours (the operand of
   one instruction each). Where we need to look at what is being drawn, the
   instruction is replaced by a jump to a small stub that saves the registers
   and the FPU, calls our C function, and then runs the replaced bytes. Every
   place is checked byte for byte first; if one doesn't match (not the Steam
   copy), nothing is changed. */
IMP void *WINAPI VirtualAlloc(void *, unsigned long, DWORD, DWORD);

typedef struct { DWORD efl, edi, esi, ebp, esp, ebx, edx, ecx, eax; } Regs; /* pushfd + pushad */
typedef void (*MidFn)(Regs *);
typedef unsigned short *(__fastcall *FullNameFn)(void *, void *, unsigned short *);

#define HUD_ORIG_SCALE 0.003125f /* 2/640 */
static float g_scale = HUD_ORIG_SCALE, g_off = 1.0f;  /* 4:3: 2h/(480w) and (4/3)(h/w) */
static float g_dscale = HUD_ORIG_SCALE, g_doff = 1.0f; /* what the picture being drawn uses */
static float g_tscale = 2.0f;                          /* text: (4/3)(h/w) * 2 */
static float g_lbscale = 1.0f / 640.0f, g_lbpos = 56.0f; /* loading bar */
static int g_scope = 640, g_side;  /* scope width; (480*w/h - 640)/2, the width of each side bar */
static BYTE *g_inmenu;             /* the game's "a menu is up" byte */
static DWORD g_shown[128];         /* menu pages drawn since the last change */
static int g_nshown;
static int g_hud = -1;             /* -1: not read yet */
static char g_ini[300];

/* Pages that are menus (CRC32 of the page name): Briefing, CDKeyEntry, Camera,
   ChatIngame, ContextMenu, Controls_Popup_Joy_Selection, Controls_Popup_Selection,
   Controls_keyboard, CreateAccount, CreateProfile, Difficulty, DisplayAdvanced,
   DisplaySettings, Empty, EnterServerPassword, Extras, Footer_training2,
   FriendsContext, FriendsManagement, GameOverOnline, LoadOut, LoadOut2,
   LoadingScreen, LobbyLan, LobbyOnline, Login, ManageAccount, ManageGame,
   MenuCoop, MenuGameOver, MenuGameType, MenuLoadGame, MenuLoadLevel,
   MenuLobbyList, MenuOnline, MenuRoomCreateLan, MenuRoomListLan, MenuSaveGame,
   MenuSettings, MenuSolo, Movies, MusicsBio, MusicsList, OfflineOnline,
   OnlineCreate, OnlineESRB, OnlineFilter, OnlineQuickMatchFilter,
   OnlineTermsOfUse, OptionPopup, OverallAchievement, Pause, PauseOnline,
   PrivateMessage, Profiles, Progress, RoomOnline, SoundSettings, TrainingVideo,
   TrainingVideoMain, opticcable, LoadOut_Infos, LoadOut_Selection (all "P_..."). */
static const DWORD menu_pages[] = {
    0xe8f02806, 0xf78dffb0, 0x68ebff61, 0x3682029b, 0x5532b191, 0x65bc5619, 0xcda29572, 0x8f89f2d1,
    0xe5e6a3c4, 0x19875f6f, 0xbb54b1c8, 0x9c0296d2, 0xc5c95e66, 0x2c456936, 0xf6e75782, 0x756c74b5,
    0x6b5313b3, 0x55aaac71, 0x678d2a91, 0xb9343e38, 0x0ca4daf2, 0x496a89e7, 0x0ee09cdd, 0xeb2c8c40,
    0x0558e138, 0xee8a9fe2, 0xd2ea6988, 0x826adfdf, 0xe7e5a8e8, 0xfcb4d544, 0xc4962134, 0x2c94c951,
    0x6287dd82, 0x29433a8c, 0x8235aa00, 0x43dd2958, 0x22252404, 0xfa838a18, 0x5b0e73d9, 0x11d9a141,
    0x95e9fc54, 0xdf04a9eb, 0xb4c54f9f, 0xc28e5106, 0x86538305, 0x7fd1bec3, 0x76413ce3, 0x24153b0e,
    0x072db961, 0xdbe91b11, 0x76252a34, 0x9318c61f, 0x67b36f30, 0x4f31ed30, 0xe0115896, 0x49202fe0,
    0x592528cc, 0xfe963758, 0x7b6657b9, 0xe2e33fea, 0x2638f1c8, 0xf53fe929, 0x25ff3b68,
};
#define PAGE_CAMCONTROL 0x6699e306u

static int page_shown(DWORD h)
{
    for (int i = 0; i < g_nshown; i++) if (g_shown[i] == h) return 1;
    return 0;
}
static int menu_shown(void)
{
    for (unsigned i = 0; i < sizeof menu_pages / sizeof *menu_pages; i++) if (page_shown(menu_pages[i])) return 1;
    return 0;
}
/* The text has "Texture <...>" names; compare our ASCII with the game's UTF-16. */
static int wstarts(const unsigned short *w, const char *a)
{
    while (*a) if (*w++ != (unsigned char)*a++) return 0;
    return 1;
}
static int wequal(const unsigned short *w, const char *a) { return wstarts(w, a) && !w[slen(a)]; }
static int wcontains(const unsigned short *w, const char *a)
{
    for (; *w; w++) if (wstarts(w, a)) return 1;
    return 0;
}

/* The menu manager's tick: its object's +0x4D39 is the "menu up" byte. */
static void on_menuflag(Regs *r) { g_inmenu = (BYTE *)(r->esi + 0x4D39); }
/* A menu item being drawn (thiscall, ecx = item; +0x20 bit 0 = visible, +0x14 = page hash). */
static void on_menutick(Regs *r)
{
    const BYTE *it = (const BYTE *)r->ecx;
    DWORD h = *(const DWORD *)(it + 0x14);
    if (!(it[0x20] & 1) || page_shown(h)) return;
    if (g_nshown < (int)(sizeof g_shown / sizeof *g_shown)) g_shown[g_nshown++] = h;
}
/* A menu item deleted, or a level loaded: start over. */
static void on_menuclear(Regs *r) { (void)r; g_nshown = 0; }

/* One HUD picture about to be drawn: its rectangle (640x480 steps) is on the stack. */
static void on_image(Regs *r)
{
    static unsigned short name[512];
    short *rc = (short *)(r->esp + 0x40); /* left, right, top, bottom */
    DWORD color = *(DWORD *)(r->esp + 0x3c);
    void *tex = *(void **)(r->esi + 0x14);
    g_dscale = g_scale;
    g_doff = g_off;
    if (menu_shown() || !g_inmenu || *g_inmenu || !tex) return;
    name[0] = 0;
    const unsigned short *n = ((FullNameFn)0x10cd8f90)(tex, 0, name); /* UObject::GetFullName */
    if (!n) n = name;
    if (wcontains(n, "LOADING_")) return;
    short l = rc[0], rt = rc[1], t = rc[2], bt = rc[3];
    if ((l == 0 && rt == 640) || (l == -2 && rt == 639 && t == -2 && bt == 479) ||
        (l == -1 && rt == 640 && t == -2 && bt == 479) || page_shown(PAGE_CAMCONTROL)) {
        g_dscale = HUD_ORIG_SCALE; /* fills the screen: as the game draws it */
        g_doff = 1.0f;
        return;
    }
    if (wequal(n, "Texture hud_PC.mgb.GOGGLES_BORDER")) {
        if ((l == -1 && rt == 256) || (l == -2 && rt == 255) || (l == -61 && rt == 319) || (l == -60 && rt == 320)) {
            rc[0] -= g_side;
            rc[1] -= g_side;
        } else if (((l == 382 || l == 383 || l == 384) && (rt == 639 || rt == 640 || rt == 641)) ||
                   (l == 319 && rt == 699) || (l == 320 && rt == 700)) {
            rc[0] += g_side + 1;
            rc[1] += g_side + 1;
        }
    } else if (wequal(n, "Texture system_PC.mgb.Green") && color == 0x1bffffff && t == -3 && bt == 483) {
        if (rt == 62 || rt == 649) { /* the weapon scope's sides */
            if (l == -1) rc[0] -= g_side;
            else if (l == 579) rc[1] += g_side;
        }
    }
}
/* The scope's width argument. */
static void on_scope(Regs *r) { *(int *)(r->esp + 0x68) = g_scope; }

static BYTE *g_stubs;
static int g_stubs_used;

static void put32(BYTE *p, DWORD v) { memcpy(p, &v, 4); }
static void code_write(void *at, const void *b, unsigned n)
{
    DWORD old;
    VirtualProtect(at, n, 0x40 /* PAGE_EXECUTE_READWRITE */, &old);
    memcpy(at, b, n);
    VirtualProtect(at, n, old, &old);
}
static BYTE g_skip; /* set by a gate's function: return 0 from the hooked function instead of running it */
/* gate: -1 = always go on; else the hooked function's "ret n" bytes count, and the
   function may set g_skip to return 0 without running it (only at a function's entry) */
static void mid_hook(DWORD at, unsigned len, MidFn fn, int gate)
{
    BYTE *s = g_stubs + g_stubs_used, *p = s;
    static const BYTE pre[] = { 0x60, 0x9c, 0x89, 0xe0,            /* pushad; pushfd; mov eax, esp */
                                0x81, 0xec, 108, 0, 0, 0,          /* sub esp, 108 */
                                0xdd, 0x34, 0x24, 0x50, 0xe8 };    /* fnsave [esp]; push eax; call */
    static const BYTE post[] = { 0x83, 0xc4, 0x04, 0xdd, 0x24, 0x24, /* add esp, 4; frstor [esp] */
                                 0x81, 0xc4, 108, 0, 0, 0, 0x9d, 0x61 }; /* add esp, 108; popfd; popad */
    memcpy(p, pre, sizeof pre);
    p += sizeof pre;
    put32(p, (DWORD)fn - (DWORD)(p + 4));
    p += 4;
    memcpy(p, post, sizeof post);
    p += sizeof post;
    if (gate >= 0) { /* cmp byte [g_skip], 0; jne skip */
        *p++ = 0x80; *p++ = 0x3d; put32(p, (DWORD)&g_skip); p += 4; *p++ = 0;
        *p++ = 0x75; *p++ = (BYTE)(len + 5);
    }
    memcpy(p, (void *)at, len); /* the replaced instructions (none of them relative) */
    p += len;
    *p++ = 0xe9;
    put32(p, at + len - (DWORD)(p + 4));
    p += 4;
    if (gate >= 0) { /* skip: mov byte [g_skip], 0; xor eax, eax; ret gate */
        *p++ = 0xc6; *p++ = 0x05; put32(p, (DWORD)&g_skip); p += 4; *p++ = 0;
        *p++ = 0x31; *p++ = 0xc0;
        *p++ = 0xc2; *p++ = (BYTE)gate; *p++ = (BYTE)(gate >> 8);
    }
    g_stubs_used += (int)((p - s + 15) & ~15);
    BYTE j[8] = { 0xe9, 0, 0, 0, 0, 0x90, 0x90, 0x90 };
    put32(j + 1, (DWORD)s - (at + 5));
    code_write((void *)at, j, len);
}
static int stubs_ready(void)
{
    if (!g_stubs) g_stubs = VirtualAlloc(0, 4096, 0x3000 /* MEM_COMMIT|MEM_RESERVE */, 0x40 /* PAGE_EXECUTE_READWRITE */);
    if (!g_stubs) log_write("no memory for the hooks\r\n", 26);
    return g_stubs != 0;
}

struct Site { DWORD at; BYTE len; BYTE code[7]; };
/* operand redirects: the 4 bytes at at+len-4 become the address of the number */
static const struct Site ops[] = {
    { 0x10adac39, 6, { 0xd9, 0x05, 0xbc, 0xb6, 0x20, 0x11 } }, /* fld [scale]  picture size */
    { 0x10adac54, 6, { 0xd9, 0x05, 0xbc, 0xb6, 0x20, 0x11 } }, /* fld [scale] */
    { 0x10adaca5, 5, { 0xa1, 0xbc, 0xb6, 0x20, 0x11 } },       /* mov eax, [scale] */
    { 0x10adadb8, 6, { 0xd8, 0x0d, 0xbc, 0xb6, 0x20, 0x11 } }, /* fmul [scale]  picture position */
    { 0x10adadbe, 6, { 0xd8, 0x25, 0x40, 0xa2, 0x0b, 0x11 } }, /* fsub [1.0] */
    { 0x10adadc4, 6, { 0xd9, 0x05, 0xbc, 0xb6, 0x20, 0x11 } }, /* fld [scale] */
    { 0x10b149ce, 6, { 0xd8, 0x3d, 0xac, 0xc1, 0x0b, 0x11 } }, /* fdivr [2.0]  text */
    { 0x10b14bad, 6, { 0xd8, 0x25, 0x40, 0xa2, 0x0b, 0x11 } }, /* fsub [1.0]  text */
    { 0x10a3d916, 6, { 0xd8, 0x0d, 0xb8, 0x5c, 0x0d, 0x11 } }, /* fmul [1/640]  loading bar */
    { 0x10a3d92d, 6, { 0xd8, 0x0d, 0xc4, 0xcf, 0x0b, 0x11 } }, /* fmul [56.0]  loading bar */
};
static float *const op_to[] = { &g_dscale, &g_dscale, &g_dscale, &g_dscale, &g_doff, &g_dscale,
                                &g_tscale, &g_off, &g_lbscale, &g_lbpos };
static const struct Site mids[] = {
    { 0x10ca40d5, 6, { 0xff, 0x91, 0xc4, 0x00, 0x00, 0x00 } },       /* call [ecx+0xc4] */
    { 0x10d30900, 5, { 0x53, 0x8b, 0x5c, 0x24, 0x08 } },             /* push ebx; mov ebx, [esp+8] */
    { 0x10d32220, 7, { 0x56, 0x57, 0x8b, 0xf9, 0x8b, 0x47, 0x28 } }, /* push esi; push edi; mov edi, ecx; mov eax, [edi+28] */
    { 0x10a3faf9, 6, { 0x8b, 0x0d, 0xc8, 0xa2, 0x1f, 0x11 } },       /* after LoadGame: mov ecx, [...] */
    { 0x10adabfa, 5, { 0x33, 0xca, 0x89, 0x48, 0x68 } },             /* xor ecx, edx; mov [eax+68], ecx */
    { 0x10c9a646, 6, { 0x8b, 0x8e, 0xa4, 0x01, 0x00, 0x00 } },       /* mov ecx, [esi+1a4] */
};
static const MidFn mid_fn[] = { on_menuflag, on_menutick, on_menuclear, on_menuclear, on_image, on_scope };

static int site_ok(const struct Site *s)
{
    const BYTE *b = (const BYTE *)s->at;
    for (int i = 0; i < s->len; i++) if (b[i] != s->code[i]) return 0;
    return 1;
}

static void hud_fix(UINT w, UINT h)
{
    static int done;
    char b[96];
    if (g_hud < 0) g_hud = GetPrivateProfileIntA("HUD", "Fix", 0, g_ini);
    if (!g_hud || !w || !h) return;
    float fw = (float)w, fh = (float)h, aspect = fw / fh;
    g_scale = 2.0f * fh / (480.0f * fw);
    g_tscale = (4.0f / 3.0f) / aspect * 2.0f;
    g_off = g_tscale / 2.0f;
    g_side = (int)((480.0f * aspect - 640.0f) / 2.0f);
    g_scope = (int)(fw * (aspect / (4.0f / 3.0f)));
    g_lbscale = 1.0f / (480.0f * aspect);
    g_lbpos = 56.0f + (float)g_side;
    g_dscale = g_scale;
    g_doff = g_off;
    if (done) return;
    done = 1;
    unsigned i;
    for (i = 0; i < sizeof ops / sizeof *ops; i++) if (!site_ok(&ops[i])) break;
    unsigned j;
    for (j = 0; j < sizeof mids / sizeof *mids; j++) if (!site_ok(&mids[j])) break;
    if (i < sizeof ops / sizeof *ops || j < sizeof mids / sizeof *mids) {
        wsprintfA(b, "HUD fix: not this game's code (at 0x%08lX), left off\r\n",
                  i < sizeof ops / sizeof *ops ? ops[i].at : mids[j].at);
        log_write(b, slen(b));
        return;
    }
    if (!stubs_ready()) return;
    for (i = 0; i < sizeof ops / sizeof *ops; i++) {
        DWORD a = (DWORD)op_to[i];
        code_write((void *)(ops[i].at + ops[i].len - 4), &a, 4);
    }
    for (j = 0; j < sizeof mids / sizeof *mids; j++) mid_hook(mids[j].at, mids[j].len, mid_fn[j], -1);
    wsprintfA(b, "HUD fix: on (4:3 HUD, %ux%u, side %d)\r\n", w, h, g_side);
    log_write(b, slen(b));
}

/* ---- Raw mouse (SCCTFix.ini [Mouse] Raw=1) ----
   Ported from ThirteenAG's SplinterCellChaosTheory.FusionFix and its
   RawInputHandler (MIT License, Copyright (c) 2018 ThirteenAG; see
   THIRD-PARTY.txt), rewritten in plain C.

   With the game's own DirectInput mouse off (SplinterCell3.ini UseDIMouse=False,
   SCCTLink sets it), the game turns WM_MOUSEMOVE into mouse moves, with Windows'
   pointer speed and acceleration. With Raw mouse, a thread of ours reads the
   mouse as Windows Raw Input (the counts the mouse sends: no acceleration, any
   polling rate), the game's WM_MOUSEMOVE is dropped, and once a frame, at the
   start of UGameEngine::Tick, the summed counts go to the game through its
   own UWindowsViewport::CauseInputEvent (MouseX 228, MouseY 229, as an axis).
   The game handles them like any mouse move, so co-op is not affected. */
IMP void *WINAPI CreateThread(void *, unsigned long, DWORD(WINAPI *)(void *), void *, DWORD, DWORD *);
IMP ATOM WINAPI RegisterClassExA(const void *);
IMP HWND WINAPI CreateWindowExA(DWORD, const char *, const char *, DWORD, int, int, int, int, HWND, void *, HINSTANCE, void *);
IMP BOOL WINAPI RegisterRawInputDevices(const void *, UINT, UINT);
IMP UINT WINAPI GetRawInputData(void *, UINT, void *, UINT *, UINT);
IMP BOOL WINAPI GetMessageA(void *, HWND, UINT, UINT);
IMP LONG WINAPI DispatchMessageA(const void *);
IMP LONG WINAPI DefWindowProcA(HWND, UINT, UINT, LONG);
IMP HWND WINAPI GetForegroundWindow(void);
IMP HWND WINAPI GetAncestor(HWND, UINT);
IMP BOOL WINAPI SetCursorPos(int, int);
IMP BOOL WINAPI GetClientRect(HWND, RECT *);
typedef struct { LONG x, y; } POINT;
IMP BOOL WINAPI ClientToScreen(HWND, POINT *);

typedef int(__thiscall *CauseInputFn)(void *, int, int, float);
#define CAUSE_INPUT_EVENT ((CauseInputFn)0x10cc28b0) /* UWindowsViewport::CauseInputEvent */
static int g_raw = -1;           /* -1: not read yet */
static int g_raw_on;             /* the hooks are in */
static int g_raw_thread;
static volatile LONG g_rdx, g_rdy; /* counts not given to the game yet */
static BYTE *g_viewport;         /* UWindowsViewport */
static HWND g_vp_hwnd;           /* its window */

static LONG WINAPI raw_wndproc(HWND w, UINT m, UINT wp, LONG lp)
{
    if (m == 0x00ff /* WM_INPUT */) {
        struct { DWORD type, size; HANDLE dev; UINT wp; WORD flags, pad; DWORD buttons, rawbuttons; LONG x, y; DWORD extra; DWORD more[4]; } r;
        UINT n = sizeof r;
        HWND game = g_vp_hwnd;
        if (GetRawInputData((void *)lp, 0x10000003 /* RID_INPUT */, &r, &n, 16) != (UINT)-1 && r.type == 0 /* RIM_TYPEMOUSE */ &&
            !(r.flags & 1 /* MOUSE_MOVE_ABSOLUTE */) && game && GetForegroundWindow() == GetAncestor(game, 2 /* GA_ROOT */)) {
            __atomic_fetch_add(&g_rdx, r.x, __ATOMIC_RELAXED);
            __atomic_fetch_add(&g_rdy, r.y, __ATOMIC_RELAXED);
        }
    }
    return DefWindowProcA(w, m, wp, lp);
}
/* Its own thread and a message-only window: a fast mouse sends thousands of
   WM_INPUT a second, which would flood the game's message queue. */
static DWORD WINAPI raw_thread(void *arg)
{
    (void)arg;
    struct { UINT size, style; void *proc; int cls, wnd; HINSTANCE inst; HANDLE icon, cursor, brush; const char *menu, *name; HANDLE sm; } wc;
    memset(&wc, 0, sizeof wc);
    wc.size = sizeof wc;
    wc.proc = (void *)raw_wndproc;
    wc.inst = GetModuleHandleA(0);
    wc.name = "SCCTFixRawMouse";
    RegisterClassExA(&wc);
    HWND w = CreateWindowExA(0, wc.name, "", 0, 0, 0, 0, 0, (HWND)-3 /* HWND_MESSAGE */, 0, wc.inst, 0);
    struct { WORD page, usage; DWORD flags; HWND target; } rid = { 1, 2, 0x100 /* RIDEV_INPUTSINK */, w };
    if (!w || !RegisterRawInputDevices(&rid, 1, sizeof rid)) {
        log_write("Raw mouse: Windows refused it\r\n", 31);
        return 0;
    }
    log_write("Raw mouse: reading\r\n", 20);
    struct { HWND w; UINT m; UINT wp; LONG lp; DWORD t; LONG x, y, priv; } msg;
    while (GetMessageA(&msg, 0, 0, 0) > 0) DispatchMessageA(&msg);
    return 0;
}

/* UWindowsViewport::ViewportWndProc (thiscall: this, msg, wParam, lParam; ret 0xc) */
static void on_vp_wndproc(Regs *r)
{
    g_viewport = (BYTE *)r->ecx;
    DWORD w = *(DWORD *)(g_viewport + 0x1f0);
    if (w) g_vp_hwnd = *(HWND *)(w + 4);
    if (!g_raw_thread) {
        g_raw_thread = 1;
        if (!CreateThread(0, 0, raw_thread, 0, 0, 0)) log_write("Raw mouse: no thread\r\n", 22);
    }
    if (*(UINT *)(r->esp + 4) == 0x0200 /* WM_MOUSEMOVE: the raw counts move the mouse instead */) g_skip = 1;
}
/* UGameEngine::Tick: once a frame, the counts go to the game */
static void on_engine_tick(Regs *r)
{
    (void)r;
    BYTE *vp = g_viewport;
    LONG dx = __atomic_exchange_n(&g_rdx, 0, __ATOMIC_RELAXED);
    LONG dy = __atomic_exchange_n(&g_rdy, 0, __ATOMIC_RELAXED);
    if (!vp || (!dx && !dy) || GetForegroundWindow() != g_vp_hwnd) return;
    CAUSE_INPUT_EVENT(vp, 228 /* IK_MouseX */, 4 /* IST_Axis */, (float)dx);
    CAUSE_INPUT_EVENT(vp, 229 /* IK_MouseY */, 4, (float)dy);
    if ((vp[0x204] & 1) || *(int *)(vp + 0x218) != -1) { /* the game has the mouse: keep the pointer in the middle */
        RECT rc;
        GetClientRect(g_vp_hwnd, &rc);
        POINT c = { (rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2 };
        ClientToScreen(g_vp_hwnd, &c);
        SetCursorPos(c.x, c.y);
    }
}

static const struct Site raw_sites[] = {
    { 0x10cc37b0, 6, { 0x81, 0xec, 0x8c, 0x03, 0x00, 0x00 } }, /* ViewportWndProc: sub esp, 0x38c */
    { 0x10a43920, 5, { 0x83, 0xec, 0x1c, 0x53, 0x55 } },       /* UGameEngine::Tick: sub esp, 0x1c; push ebx; push ebp */
    { 0x10cc28b0, 6, { 0x56, 0x8b, 0xf1, 0x8b, 0x46, 0x18 } }, /* CauseInputEvent (called, not changed) */
};
static void raw_mouse(void)
{
    if (g_raw < 0) g_raw = GetPrivateProfileIntA("Mouse", "Raw", 0, g_ini);
    if (!g_raw || g_raw_on) return;
    g_raw_on = 1;
    for (unsigned i = 0; i < sizeof raw_sites / sizeof *raw_sites; i++)
        if (!site_ok(&raw_sites[i])) {
            log_write("Raw mouse: not this game's code, left off\r\n", 43);
            return;
        }
    if (!stubs_ready()) return;
    mid_hook(raw_sites[0].at, raw_sites[0].len, on_vp_wndproc, 0xc);
    mid_hook(raw_sites[1].at, raw_sites[1].len, on_engine_tick, -1);
    log_write("Raw mouse: on\r\n", 15);
}

/* ---- the screen's own resolution ----
   Ported from ThirteenAG's FusionFix (MIT, see THIRD-PARTY.txt). In a window
   the game renders at its windowed size (its menu's list stops short of 2K and
   wider screens), and the borderless window stretched that over the screen.
   UD3DRenderDevice::SetRes(viewport, width, height, fullscreen, ...) is given
   the monitor's size instead whenever it isn't fullscreen, so the game draws
   at the screen's real resolution. When the size changes, the device's "reset
   requested" flag (+0x64) is set for the call: the windowed fast path would
   keep the old size's render targets. */
typedef int(__thiscall *SetResFn)(void *, BYTE *, int, int, int, int);
static SetResFn g_setres; /* the original: its first instruction, then a jump back */
static int g_res_w, g_res_h;
static int __fastcall my_setres(BYTE *dev, void *edx, BYTE *vp, int w, int h, int fs, int a5)
{
    (void)edx;
    DWORD wp = vp ? *(DWORD *)(vp + 0x1f0) : 0;
    HWND hw = wp ? *(HWND *)(wp + 4) : 0;
    MONITORINFO mi;
    mi.cbSize = sizeof mi;
    if (!fs && hw && GetMonitorInfoA(MonitorFromWindow(hw, 2 /* MONITOR_DEFAULTTONEAREST */), &mi)) {
        int mw = mi.rcMonitor.right - mi.rcMonitor.left, mh = mi.rcMonitor.bottom - mi.rcMonitor.top;
        if (mw > 0 && mh > 0 && (mw != w || mh != h)) {
            char b[80];
            wsprintfA(b, "Resolution: %dx%d -> %dx%d (the screen's)\r\n", w, h, mw, mh);
            log_write(b, slen(b));
            w = mw;
            h = mh;
        }
    }
    int r;
    if (!fs && w > 0 && h > 0 && (w != g_res_w || h != g_res_h) && g_res_w) {
        int *req = (int *)(dev + 0x64), prev = *req;
        *req = 1;
        r = g_setres(dev, vp, w, h, fs, a5);
        *req = prev;
    } else
        r = g_setres(dev, vp, w, h, fs, a5);
    g_res_w = w;
    g_res_h = h;
    return r;
}
/* The game opens its window at StartupFullscreen from its ini - and the copy it
   reads may be ProgramData's, which SCCTLink can't always reach. The fix runs
   it in a window anyway, so OpenWindow's "StartupFullscreen = 1" is made 0
   (FusionFix does the same): SetRes then gets a window size, made the screen's. */
static const struct Site start_window = { 0x10cc6d59, 7, { 0xc7, 0x85, 0xd8, 0x00, 0x00, 0x00, 0x01 } };
/* Windows scales a program that isn't DPI aware: at 125 % or 150 % the game
   would see a smaller screen (a 2560x1440 one as 2048x1152 or less), render at
   that and be stretched. So the game is made per-monitor DPI aware. */
IMP BOOL WINAPI SetProcessDPIAware(void);
static void dpi_aware(void)
{
    typedef BOOL(WINAPI * SetCtxFn)(HANDLE);
    SetCtxFn f = (SetCtxFn)GetProcAddress(GetModuleHandleA("user32.dll"), "SetProcessDpiAwarenessContext");
    BOOL ok = f ? f((HANDLE)-4 /* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 */) : SetProcessDPIAware();
    log_write(ok ? "DPI aware: yes\r\n" : "DPI aware: no (already set)\r\n", ok ? 16 : 29);
}
static const struct Site res_sites[] = {
    { 0x10c8b860, 5, { 0xa1, 0x94, 0x27, 0x23, 0x11 } }, /* SetRes: mov eax, [0x11232794] */
    { 0x10c8b8f8, 5, { 0x8b, 0x46, 0x64, 0x85, 0xc0 } }, /* ...: mov eax, [esi+0x64] (the reset flag); test eax, eax */
};
static void screen_res(void)
{
    for (unsigned i = 0; i < sizeof res_sites / sizeof *res_sites; i++)
        if (!site_ok(&res_sites[i])) {
            log_write("Resolution: not this game's code, left as it is\r\n", 49);
            return;
        }
    if (site_ok(&start_window)) {
        DWORD zero = 0;
        code_write((void *)(start_window.at + 6), &zero, 4);
        log_write("Window: always (StartupFullscreen ignored)\r\n", 44);
    }
    if (!stubs_ready()) return;
    BYTE *t = g_stubs + g_stubs_used; /* the trampoline: the replaced mov, then on */
    memcpy(t, (void *)0x10c8b860, 5);
    t[5] = 0xe9;
    put32(t + 6, 0x10c8b865 - (DWORD)(t + 10));
    g_stubs_used += 16;
    g_setres = (SetResFn)(void *)t;
    BYTE j[5] = { 0xe9 };
    put32(j + 1, (DWORD)my_setres - (0x10c8b860 + 5));
    code_write((void *)0x10c8b860, j, 5);
    log_write("Resolution: the screen's\r\n", 26);
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

/* ---- the frame rate ----
   Co-op: always 60 (the two PCs run the same simulation in lockstep). Single
   player may choose (SCCTFix.ini [Display] SingleFPS: 60, -1 = the screen's
   refresh rate, 0 = no limit). While SCCTLink is in a co-op session it keeps
   a file, System\SCCTLink.coop (and a named event, Local\SCCTLinkCoop: on
   this PC only, nothing on the network); once a second the fix looks for
   them, and while one is there the limit is 60 whatever the setting. If
   SCCTLink stops without removing the file, single player stays at 60 until
   it runs again: the safe side. */
IMP HANDLE WINAPI OpenEventA(DWORD, BOOL, const char *);
IMP BOOL WINAPI CloseHandle(HANDLE);
IMP DWORD WINAPI GetLastError(void);
IMP BOOL WINAPI EnumDisplaySettingsA(const char *, DWORD, void *);
static int g_fps = 60;        /* the limit now (0: none) */
static LONGLONG g_fps_check;  /* when to look again */
IMP DWORD WINAPI GetFileAttributesA(const char *);
static char g_coop_file[300]; /* System\SCCTLink.coop: SCCTLink keeps it there during a co-op session */
static int coop_session(void)
{
    if (g_coop_file[0] && GetFileAttributesA(g_coop_file) != (DWORD)-1) return 1;
    HANDLE e = OpenEventA(0x00100000 /* SYNCHRONIZE */, 0, "Local\\SCCTLinkCoop");
    if (e) { CloseHandle(e); return 1; }
    return GetLastError() == 5 /* ERROR_ACCESS_DENIED: there, but SCCTLink is administrator */;
}
static int screen_hz(void)
{
    struct { DWORD size; RECT mon, work; DWORD flags; char device[32]; } mi;
    struct { char name[32]; WORD spec, drv, size, extra; DWORD fields; BYTE u[16]; short color, duplex, yres, tt, collate;
             char form[32]; WORD logpix; DWORD bits, w, h, flags, hz; BYTE rest[32]; } dm;
    memset(&mi, 0, sizeof mi);
    memset(&dm, 0, sizeof dm);
    mi.size = sizeof mi;
    dm.size = 156;
    if (g_hwnd && GetMonitorInfoA(MonitorFromWindow(g_hwnd, 1), (MONITORINFO *)&mi) &&
        EnumDisplaySettingsA(mi.device, (DWORD)-1 /* ENUM_CURRENT_SETTINGS */, &dm) && dm.hz > 1 && dm.hz < 1000)
        return (int)dm.hz;
    return 60;
}
static void fps_update(LONGLONG now)
{
    if (now < g_fps_check) return;
    g_fps_check = now + g_freq;
    int want = 60, coop = coop_session();
    if (!coop) {
        int v = GetPrivateProfileIntA("Display", "SingleFPS", 60, g_ini);
        want = v == -1 ? screen_hz() : v == 0 ? 0 : v < 30 ? 30 : v > 1000 ? 1000 : v;
    }
    if (want == g_fps) return;
    g_fps = want;
    if (want) g_frame = (DWORD)g_freq / (DWORD)want;
    g_next = 0;
    char b[80];
    if (coop) wsprintfA(b, "FPS: 60 (co-op session)\r\n");
    else if (want) wsprintfA(b, "FPS: %d (single player)\r\n", want);
    else wsprintfA(b, "FPS: no limit (single player)\r\n");
    log_write(b, slen(b));
}

static HRESULT WINAPI hook_Present(void *dev, const void *src, const void *dst, HWND w, const void *dirty)
{
    HRESULT hr = g_present(dev, src, dst, w, dirty);
    LONGLONG now;
    QueryPerformanceCounter(&now);
    fps_update(now);
    if (!g_next) g_next = now;
    g_next += g_frame;
    if (!g_fps || now >= g_next) g_next = now; /* no limit, or late: don't try to catch up */
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
    if (hr >= 0) raw_mouse();
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
    memcpy(g_coop_file, path, n);
    memcpy(g_coop_file + n, "SCCTLink.coop", 14);
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
    dpi_aware();
    screen_res(); /* before the game sets up its screen */
    return 1;
}
