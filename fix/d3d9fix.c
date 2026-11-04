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
static void mid_hook(DWORD at, unsigned len, MidFn fn)
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
    memcpy(p, (void *)at, len); /* the replaced instructions (none of them relative) */
    p += len;
    *p++ = 0xe9;
    put32(p, at + len - (DWORD)(p + 4));
    p += 4;
    g_stubs_used += (int)((p - s + 15) & ~15);
    BYTE j[8] = { 0xe9, 0, 0, 0, 0, 0x90, 0x90, 0x90 };
    put32(j + 1, (DWORD)s - (at + 5));
    code_write((void *)at, j, len);
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
    g_stubs = VirtualAlloc(0, 4096, 0x3000 /* MEM_COMMIT|MEM_RESERVE */, 0x40 /* PAGE_EXECUTE_READWRITE */);
    if (!g_stubs) {
        log_write("HUD fix: no memory, left off\r\n", 30);
        return;
    }
    for (i = 0; i < sizeof ops / sizeof *ops; i++) {
        DWORD a = (DWORD)op_to[i];
        code_write((void *)(ops[i].at + ops[i].len - 4), &a, 4);
    }
    for (j = 0; j < sizeof mids / sizeof *mids; j++) mid_hook(mids[j].at, mids[j].len, mid_fn[j]);
    wsprintfA(b, "HUD fix: on (4:3 HUD, %ux%u, side %d)\r\n", w, h, g_side);
    log_write(b, slen(b));
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
