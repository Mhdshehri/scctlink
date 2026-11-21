/* ui_win.c - the SCCTLink window (Windows), the same window as CoFLink's in
 * green. One program for every player: Co-op or Versus, "Host a game" or
 * "Join a friend", the friends list, the lobby code, "How to" and the log.
 * The engine (scctlink.c) runs on a second thread; this file only shows its
 * state and sends it commands. Win32 is declared by hand (no Windows SDK). */
#include "ui.h"
#include "plat.h"

#define WINAPI __attribute__((stdcall))
#define IMP __declspec(dllimport)
typedef void *HANDLE, *HWND, *HINSTANCE, *HICON, *HCURSOR, *HBRUSH, *HFONT, *HDC, *HMENU;
typedef int BOOL;
typedef unsigned int UINT;
typedef unsigned long DWORD;
typedef uintptr_t WPARAM;
typedef intptr_t LPARAM, LRESULT;
typedef uint16_t WCHAR;
typedef LRESULT(WINAPI *WNDPROC)(HWND, UINT, WPARAM, LPARAM);

typedef struct {
    UINT cbSize, style;
    WNDPROC proc;
    int clsExtra, wndExtra;
    HINSTANCE inst;
    HICON icon;
    HCURSOR cursor;
    HBRUSH background;
    const WCHAR *menu, *className;
    HICON iconSm;
} WNDCLASSEXW;
typedef struct { long left, top, right, bottom; } RECT;
typedef struct { HWND hwnd; UINT msg; WPARAM wp; LPARAM lp; DWORD time; long x, y; DWORD priv; } MSG;
typedef struct { void *a, *b; long c, d; void *e, *f; } CRITICAL_SECTION; /* 24 bytes on x86 */

IMP uint16_t WINAPI RegisterClassExW(const WNDCLASSEXW *c);
IMP HWND WINAPI CreateWindowExW(DWORD ex, const WCHAR *cls, const WCHAR *title, DWORD style, int x, int y,
                                int w, int h, HWND parent, HMENU id, HINSTANCE inst, void *param);
IMP LRESULT WINAPI DefWindowProcW(HWND, UINT, WPARAM, LPARAM);
IMP BOOL WINAPI GetMessageW(MSG *m, HWND w, UINT a, UINT b);
IMP BOOL WINAPI TranslateMessage(const MSG *m);
IMP LRESULT WINAPI DispatchMessageW(const MSG *m);
IMP BOOL WINAPI IsDialogMessageW(HWND w, MSG *m);
IMP void WINAPI PostQuitMessage(int code);
IMP LRESULT WINAPI SendMessageW(HWND w, UINT m, WPARAM wp, LPARAM lp);
IMP BOOL WINAPI SetWindowTextW(HWND w, const WCHAR *t);
IMP int WINAPI GetWindowTextW(HWND w, WCHAR *buf, int n);
IMP uintptr_t WINAPI SetTimer(HWND w, uintptr_t id, UINT ms, void *fn);
IMP int WINAPI MessageBoxW(HWND w, const WCHAR *text, const WCHAR *cap, UINT type);
IMP HCURSOR WINAPI LoadCursorW(HINSTANCE i, const WCHAR *name);
IMP HICON WINAPI LoadIconW(HINSTANCE i, const WCHAR *name);
IMP BOOL WINAPI ShowWindow(HWND w, int cmd);
IMP BOOL WINAPI IsWindowVisible(HWND w);
IMP BOOL WINAPI UpdateWindow(HWND w);
IMP BOOL WINAPI EnableWindow(HWND w, BOOL on);
IMP BOOL WINAPI DestroyWindow(HWND w);
IMP HDC WINAPI GetDC(HWND w);
IMP int WINAPI ReleaseDC(HWND w, HDC dc);
IMP BOOL WINAPI AdjustWindowRectEx(RECT *r, DWORD style, BOOL menu, DWORD ex);
IMP int WINAPI GetSystemMetrics(int i);
IMP HFONT WINAPI CreateFontW(int h, int w, int esc, int orient, int weight, DWORD italic, DWORD underline,
                            DWORD strike, DWORD charset, DWORD outprec, DWORD clipprec, DWORD quality,
                            DWORD pitch, const WCHAR *face);
IMP int WINAPI GetDeviceCaps(HDC dc, int i);
IMP void WINAPI InitCommonControls(void);
IMP HANDLE WINAPI CreateThread(void *sec, size_t stack, DWORD(WINAPI *fn)(void *), void *arg, DWORD flags, DWORD *id);
IMP DWORD WINAPI WaitForSingleObject(HANDLE h, DWORD ms);
IMP void WINAPI InitializeCriticalSection(CRITICAL_SECTION *cs);
IMP void WINAPI EnterCriticalSection(CRITICAL_SECTION *cs);
IMP void WINAPI LeaveCriticalSection(CRITICAL_SECTION *cs);
IMP int WINAPI MultiByteToWideChar(UINT cp, DWORD flags, const char *s, int n, WCHAR *w, int wn);
IMP int WINAPI WideCharToMultiByte(UINT cp, DWORD flags, const WCHAR *w, int wn, char *s, int n, const char *d, BOOL *u);
IMP HINSTANCE WINAPI GetModuleHandleW(const WCHAR *name);
IMP HBRUSH WINAPI CreateSolidBrush(DWORD color);
IMP HANDLE WINAPI CreatePen(int style, int width, DWORD color);
IMP HANDLE WINAPI SelectObject(HDC dc, HANDLE obj);
IMP BOOL WINAPI DeleteObject(HANDLE obj);
IMP HANDLE WINAPI GetStockObject(int i);
IMP DWORD WINAPI SetTextColor(HDC dc, DWORD c);
IMP DWORD WINAPI SetBkColor(HDC dc, DWORD c);
IMP int WINAPI SetBkMode(HDC dc, int mode);
IMP BOOL WINAPI RoundRect(HDC dc, int l, int t, int r, int b, int w, int h);
IMP int WINAPI FillRect(HDC dc, const RECT *r, HBRUSH b);
IMP int WINAPI DrawTextW(HDC dc, const WCHAR *t, int n, RECT *r, UINT fmt);
IMP BOOL WINAPI GetClientRect(HWND w, RECT *r);
IMP BOOL WINAPI RedrawWindow(HWND w, const RECT *r, HANDLE rgn, UINT flags);
IMP BOOL WINAPI SetWindowPos(HWND w, HWND after, int x, int y, int cx, int cy, UINT flags);
IMP BOOL WINAPI SetLayeredWindowAttributes(HWND w, DWORD key, unsigned char alpha, DWORD flags);
typedef struct { UINT size; HWND hwnd; DWORD flags; UINT count; DWORD timeout; } FLASHWINFO;
IMP BOOL WINAPI FlashWindowEx(const FLASHWINFO *f);
IMP HWND WINAPI GetForegroundWindow(void);
IMP BOOL WINAPI MessageBeep(UINT type);
IMP void *WINAPI LoadLibraryExA(const char *path, void *file, DWORD flags);
IMP void *WINAPI GetProcAddress(void *mod, const char *name);
typedef struct { long x, y; } POINT;
IMP BOOL WINAPI GetCursorPos(POINT *p);
IMP BOOL WINAPI ScreenToClient(HWND w, POINT *p);
IMP HANDLE WINAPI LoadImageW(HINSTANCE i, const WCHAR *name, UINT type, int cx, int cy, UINT flags);
IMP BOOL WINAPI DrawIconEx(HDC dc, int x, int y, HICON icon, int cx, int cy, UINT step, HBRUSH flicker, UINT flags);

#define WM_DESTROY 0x2
#define WM_CLOSE 0x10
#define WM_SETFONT 0x30
#define WM_GETTEXTLENGTH 0xE
#define WM_COMMAND 0x111
#define WM_TIMER 0x113
#define EM_SETSEL 0xB1
#define EM_SCROLLCARET 0xB7
#define EM_REPLACESEL 0xC2
#define EM_SETLIMITTEXT 0xC5
#define EM_SETREADONLY 0xCF
#define LB_ADDSTRING 0x180
#define LB_RESETCONTENT 0x184
#define LB_SETCURSEL 0x186
#define LB_GETCURSEL 0x188
#define LB_GETTEXT 0x189
#define LBN_DBLCLK 2
#define WM_ERASEBKGND 0x14
#define WM_DRAWITEM 0x2B
#define WM_MEASUREITEM 0x2C
#define WM_CTLCOLOREDIT 0x133
#define WM_CTLCOLORLISTBOX 0x134
#define WM_CTLCOLORSTATIC 0x138
#define BS_OWNERDRAW 0xBL
#define LBS_OWNERDRAWFIXED 0x10L
#define LBS_HASSTRINGS 0x40L
#define ODT_LISTBOX 2
#define ODS_SELECTED 0x1
#define ODS_DISABLED 0x4
#define ODS_FOCUS 0x10
#define DT_CENTER 0x1
#define DT_VCENTER 0x4
#define DT_SINGLELINE 0x20
#define DT_END_ELLIPSIS 0x8000
#define TRANSPARENT_BK 1
#define NULL_BRUSH 5
#define RGBC(r, g, b) ((DWORD)((r) | ((g) << 8) | ((b) << 16)))
#define WS_CHILD 0x40000000L
#define WS_VISIBLE 0x10000000L
#define WS_TABSTOP 0x10000L
#define WS_VSCROLL 0x200000L
#define WS_CAPTION 0xC00000L
#define WS_SYSMENU 0x80000L
#define WS_MINIMIZEBOX 0x20000L
#define ES_MULTILINE 0x4L
#define ES_AUTOVSCROLL 0x40L
#define ES_AUTOHSCROLL 0x80L
#define ES_READONLY 0x800L
#define LBS_NOTIFY 0x1L
#define LBS_NOINTEGRALHEIGHT 0x100L
#define CP_UTF8 65001
#define MB_ICONWARNING 0x30
#define MB_YESNO 0x4
#define MB_ICONQUESTION 0x20
#define IDYES 6
#define LOGPIXELSY 90
#define FW_NORMAL 400
#define FW_BOLD 700
#define CLEARTYPE_QUALITY 5

#define WTEXT(s) L##s /* only for ASCII literals: wchar_t is 16-bit on this target */

enum {
    ID_HOST = 101, ID_JOIN, ID_STOP, ID_STATUS, ID_LOBBY, ID_COPYID, ID_JOINID,
    ID_LISTLABEL, ID_LIST, ID_ACTION, ID_LAUNCH, ID_COPYCONN, ID_LOG, ID_TITLE, ID_REFRESH, ID_ACCENT, ID_THEME, ID_TAB_HOWTO, ID_TAB_LOG, ID_COPYLOG, ID_REPORT, ID_HOWTO,
    ID_LOBBYLABEL, ID_TAB_MODS, ID_MODS, ID_MODS2, ID_MODSDIV, ID_MODSTOP, ID_RECHECK,
    /* the Settings tab */
    ID_TAB_SET, ID_SET_NOTE, ID_SL_RES, ID_SL_X, ID_SL_FOV, ID_SET_W, ID_SET_H, ID_SET_FOV,
    ID_SF_RES, ID_SF_FOV, ID_SET_FRIEND, ID_SET_FIX, ID_SET_MOUSE,
    ID_SL_MOUSE, ID_SET_SENS, ID_SL_TURN, ID_SET_TURN, ID_SF_MOUSE, ID_SET_RESET,
    /* Versus Fix (Versus only) */
    ID_SL_VFPS, ID_SET_VFPS, ID_SET_VHUD, ID_SET_VRATE, ID_SL_VFOV, ID_SET_VFOVS, ID_SET_VFOVM, ID_SL_VTURN, ID_SET_VTURNS,
    ID_SET_VTURNM, ID_SL_VSPY1, ID_SL_VMERC1, ID_SL_VSPY2, ID_SL_VMERC2, ID_SF_VFOV, ID_SET_CHUD, ID_SET_INTRO,
    /* the Settings tab's banner, section titles and notes */
    ID_SET_BANNER, ID_SET_BANSUB, ID_SEC_DISP, ID_SEC_MOUSE, ID_SEC_CLASS, ID_SET_RAWNOTE, ID_SET_SPNOTE, ID_SET_VRATENOTE, ID_SET_SPFPS,
    ID_SET_COOP60, ID_SET_APPLY, ID_STATS,
    ID_NAV_PLAY, /* the sidebar's "Play" (the other pages use the ID_TAB_* buttons) */
    ID_PLAYFIX,  /* Play: the fix's state; a button to install it when it's missing */
    ID_STATUS2,  /* Play: the status' second line (what to do) */
    ID_GAMEMODE, /* keep last */
};

typedef struct { UINT type, id, item, action, state; HWND hwnd; HDC dc; RECT rc; uintptr_t data; } DrawItem;
typedef struct { UINT type, id, item, width, height; uintptr_t data; } MeasureItem;

/* ---------- shared with the engine thread ---------- */

static CRITICAL_SECTION g_cs;
static char g_pend[65536];
static int g_npend;
static UiState g_state, g_shown;
static bool g_state_dirty;
static UiPerson g_lists[2][64];
static int g_nlist[2];
static bool g_list_dirty[2];
static bool g_alert, g_warned; /* g_warned: a "!" line arrived */
static char g_mods_text[16384];
static bool g_mods_dirty, g_mods_warn;
static UiSettings g_set_in;
static bool g_set_dirty;
static Cmd g_cmds[32];
static int g_cmd_head, g_cmd_tail;

void ui_log(const char *line)
{
    EnterCriticalSection(&g_cs);
    int n = (int)strlen(line);
    if (line[0] == '!') g_warned = true;
    if (g_npend + n + 2 < (int)sizeof g_pend) {
        memcpy(g_pend + g_npend, line, (size_t)n);
        g_npend += n;
        g_pend[g_npend++] = '\r';
        g_pend[g_npend++] = '\n';
    }
    LeaveCriticalSection(&g_cs);
}

void ui_mods(const char *text, bool warn)
{
    EnterCriticalSection(&g_cs);
    sfmt(g_mods_text, sizeof g_mods_text, "%s", text);
    g_mods_dirty = true;
    g_mods_warn = warn;
    LeaveCriticalSection(&g_cs);
}

void ui_settings(const UiSettings *s)
{
    EnterCriticalSection(&g_cs);
    g_set_in = *s;
    g_set_dirty = true;
    LeaveCriticalSection(&g_cs);
}

void ui_alert(const char *what)
{
    (void)what;
    EnterCriticalSection(&g_cs);
    g_alert = true;
    LeaveCriticalSection(&g_cs);
}

void ui_state(const UiState *s)
{
    EnterCriticalSection(&g_cs);
    g_state = *s;
    g_state_dirty = true;
    LeaveCriticalSection(&g_cs);
}

void ui_list(int which, int n, const UiPerson *p)
{
    if (which < 0 || which > 1) return;
    EnterCriticalSection(&g_cs);
    if (n > 64) n = 64;
    bool same = n == g_nlist[which];
    for (int i = 0; same && i < n; i++)
        same = g_lists[which][i].id == p[i].id && g_lists[which][i].lobby == p[i].lobby &&
               strcmp(g_lists[which][i].name, p[i].name) == 0;
    if (!same) {
        for (int i = 0; i < n; i++) g_lists[which][i] = p[i];
        g_nlist[which] = n;
        g_list_dirty[which] = true;
    }
    LeaveCriticalSection(&g_cs);
}

u64 ui_offer_hosts(int n, const UiPerson *p, u64 *reject)
{
    (void)reject;
    ui_list(LIST_HOSTING, n, p); /* the player picks one and clicks Join */
    return 0;
}

static void push_cmd(CmdKind k, u64 arg)
{
    EnterCriticalSection(&g_cs);
    int next = (g_cmd_tail + 1) % 32;
    if (next != g_cmd_head) {
        g_cmds[g_cmd_tail].kind = k;
        g_cmds[g_cmd_tail].arg = arg;
        g_cmd_tail = next;
    }
    LeaveCriticalSection(&g_cs);
}

bool ui_poll(Cmd *c)
{
    bool got = false;
    EnterCriticalSection(&g_cs);
    if (g_cmd_head != g_cmd_tail) {
        *c = g_cmds[g_cmd_head];
        g_cmd_head = (g_cmd_head + 1) % 32;
        got = true;
    }
    LeaveCriticalSection(&g_cs);
    return got;
}

/* ---------- window ---------- */

static HWND g_wnd, g_c[ID_GAMEMODE + 1];
static HFONT g_font, g_bold, g_big, g_mid, g_icon, g_icon_big; /* g_mid: 14 px bold; g_icon*: Segoe MDL2 Assets (Windows 10 and 11) */

/* Icons: Segoe MDL2 Assets glyphs */
#define IC_HOME 0xE80F
#define IC_PUZZLE 0xEA86
#define IC_SETTINGS 0xE713
#define IC_LIST 0xE8FD
#define IC_HELP 0xE897
#define IC_MOON 0xE708
#define IC_SUN 0xE706
#define IC_PLAY 0xE768
#define IC_COPY 0xE8C8
#define IC_REFRESH 0xE72C
#define IC_PERSON 0xE77B
#define IC_STATS 0xE9D9
#define IC_HOST 0xE704  /* a broadcast-like mark */
#define IC_JOIN 0xE8FA  /* add friend */
#define IC_STOP 0xE71A
#define IC_INFO 0xE946
#define IC_DOWNLOAD 0xE896
#define IC_CHECK 0xE73E
#define IC_KICK 0xE8F8  /* block contact */
#define IC_SEARCH 0xE721
#define IC_DELETE 0xE74D
static int g_dpi = 96;
static HANDLE g_thread;
static bool g_closing;
static u64 g_close_ms;
static UiPerson g_view[2][64]; /* what the list box shows (GUI thread copy) */
static int g_nview[2];
static int g_list_shown = -1;
enum { TAB_HOWTO, TAB_LOG, TAB_MODS, TAB_SET, TAB_PLAY };
static int g_tab = TAB_PLAY; /* the page shown (picked in the sidebar) */
static bool g_log_unseen;  /* a warning arrived while "How to" was shown: red dot on Log */
static bool g_mods_unseen; /* the game files differ: dot on "Mods" */
static UiSettings g_set;   /* what the Settings tab shows */
static bool g_set_warn;    /* its note is a warning */

static int S_(int v) { return v * g_dpi / 96; }

/* ---------- dark / light theme ---------- */

typedef struct {
    bool dark;
    DWORD bg, panel, text, dim, accent, accent_down, btn, btn_down, border, dis_bg, dis_text, status, sel_text;
    DWORD side, navsel, field; /* the sidebar, its picked page, text boxes and lists */
} Theme;

/* Green like the SCCTLink icon. The page, cards on it (panel), a darker sidebar. */
static const Theme DARK = { true,
    RGBC(22, 23, 26), RGBC(31, 32, 36), RGBC(232, 230, 227), RGBC(139, 142, 150),
    RGBC(22, 138, 52), RGBC(14, 102, 38), RGBC(42, 44, 49), RGBC(34, 35, 40), RGBC(50, 52, 58),
    RGBC(36, 37, 41), RGBC(92, 95, 103), RGBC(87, 214, 120), RGBC(255, 255, 255),
    RGBC(16, 17, 19), RGBC(38, 40, 45), RGBC(24, 25, 28) };
/* light: soft gray page, white cards with thin gray outlines */
static const Theme LIGHT = { false,
    RGBC(244, 245, 247), RGBC(255, 255, 255), RGBC(28, 30, 34), RGBC(106, 112, 122),
    RGBC(30, 128, 56), RGBC(20, 98, 42), RGBC(255, 255, 255), RGBC(234, 236, 240), RGBC(220, 223, 228),
    RGBC(238, 240, 243), RGBC(166, 171, 180), RGBC(30, 128, 56), RGBC(255, 255, 255),
    RGBC(233, 235, 239), RGBC(255, 255, 255), RGBC(248, 249, 251) };

/* The window (96 dpi units): the sidebar on the left, the page on the right. */
#define SIDE_W 168
#define CX 188  /* the page's left edge */
#define CW 552  /* its width */
#define WIN_W 760
#define WIN_H 504

static Theme T;
/* text boxes and the list sit inside rounded frames drawn in the theme's
   colors (Windows' own borders are black in both themes) */
typedef struct { int id, x, y, w, h, tab; } Frame; /* tab: drawn only on that tab (-1: always) */
static Frame g_frames[24];
static int g_nframes;
static HBRUSH g_br_bg, g_br_panel, g_br_accent, g_br_border, g_br_side, g_br_field;

static void draw_frames(HDC dc)
{
    HANDLE p = CreatePen(0, 1, T.border);
    HANDLE ob = SelectObject(dc, g_br_field), op = SelectObject(dc, p);
    for (int i = 0; i < g_nframes; i++) {
        Frame *f = &g_frames[i];
        if (!IsWindowVisible(g_c[f->id])) continue;
        RoundRect(dc, S_(f->x), S_(f->y), S_(f->x + f->w), S_(f->y + f->h), S_(8), S_(8));
    }
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(p);
}

/* A rounded box in fill with an edge (96 dpi units). */
static void rbox(HDC dc, int x, int y, int w, int h, int r, DWORD fill, DWORD edge)
{
    HBRUSH b = CreateSolidBrush(fill);
    HANDLE p = CreatePen(0, 1, edge);
    HANDLE ob = SelectObject(dc, b), op = SelectObject(dc, p);
    RoundRect(dc, S_(x), S_(y), S_(x + w), S_(y + h), S_(r), S_(r));
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(b);
    DeleteObject(p);
}
/* an icon (one glyph) centred in r, device pixels */
static void glyph(HDC dc, WCHAR g, RECT r, HFONT f, DWORD color)
{
    WCHAR t[2] = { g, 0 };
    HANDLE of = SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT_BK);
    SetTextColor(dc, color);
    DrawTextW(dc, t, 1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, of);
}
/* a button's face: an icon and its text, centred together (icon 0: text only) */
#define DT_CALCRECT 0x400
static void icon_text(HDC dc, RECT r, WCHAR ic, const WCHAR *t, int n, HFONT f, DWORD color)
{
    HANDLE of = SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT_BK);
    SetTextColor(dc, color);
    RECT m = { 0, 0, 2000, 100 };
    DrawTextW(dc, t, n, &m, DT_SINGLELINE | DT_CALCRECT);
    int iw = ic ? S_(16) + S_(8) : 0, tw = m.right - m.left, room = r.right - r.left - S_(12);
    if (iw + tw > room) tw = room - iw;
    int x = (r.left + r.right - iw - tw) / 2;
    if (ic) { RECT ir = { x, r.top, x + S_(16), r.bottom }; glyph(dc, ic, ir, g_icon, color); }
    SelectObject(dc, f);
    SetTextColor(dc, color);
    RECT tr = { x + iw, r.top, x + iw + tw, r.bottom };
    DrawTextW(dc, t, n, &tr, DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(dc, of);
}
/* a card on the page */
static void card(HDC dc, int x, int y, int w, int h) { rbox(dc, x, y, w, h, 14, T.panel, T.border); }
/* a ring (circle outline) of pen width k, centre cx, cy, radius r (device pixels) */
static void ring(HDC dc, int cx, int cy, int r, int k, DWORD color)
{
    HANDLE p = CreatePen(0, k, color);
    HANDLE ob = SelectObject(dc, GetStockObject(NULL_BRUSH)), op = SelectObject(dc, p);
    RoundRect(dc, cx - r, cy - r, cx + r, cy + r, 2 * r, 2 * r);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(p);
}
/* text at x, y, w, h (96 dpi units) */
static void label(HDC dc, const WCHAR *t, int x, int y, int w, int h, HFONT f, DWORD color, UINT fmt)
{
    HANDLE of = SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT_BK);
    SetTextColor(dc, color);
    RECT r = { S_(x), S_(y), S_(x + w), S_(y + h) };
    DrawTextW(dc, t, -1, &r, fmt | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(dc, of);
}
/* The SCCTLink mark: the program's own icon (res/scctlink.ico has 16 to 256 px,
   so Windows picks a sharp one for the screen's scale); drawn by hand only if
   that fails. */
static void draw_logo(HDC dc, int x, int y, int size)
{
    static HICON ic; /* loaded once: the size never changes while SCCTLink runs */
    int px = S_(size);
    if (!ic) ic = (HICON)LoadImageW(GetModuleHandleW(0), (const WCHAR *)(uintptr_t)1, 1 /* IMAGE_ICON */, px, px, 0);
    if (ic && DrawIconEx(dc, S_(x), S_(y), ic, px, px, 0, 0, 3 /* DI_NORMAL */)) return;
    HBRUSH b = CreateSolidBrush(RGBC(26, 26, 30));
    HANDLE p = CreatePen(0, S_(2) > 1 ? S_(2) : 1, RGBC(31, 157, 58));
    HANDLE ob = SelectObject(dc, b), op = SelectObject(dc, p);
    RoundRect(dc, S_(x), S_(y), S_(x + size), S_(y + size), S_(size / 3), S_(size / 3));
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(b);
    DeleteObject(p);
    int r = S_(size) * 17 / 100, k = S_(size) / 12 > 1 ? S_(size) / 12 : 2, cy = S_(y) + S_(size) / 2;
    ring(dc, S_(x) + S_(size) * 39 / 100, cy, r, k, RGBC(51, 216, 90));
    ring(dc, S_(x) + S_(size) * 62 / 100, cy, r, k, RGBC(236, 236, 236));
}

static bool fix_ok(void);
static void draw_set_deco(HDC dc);
static DWORD banner_fill(void)
{
    return fix_ok() ? (T.dark ? RGBC(22, 48, 31) : RGBC(232, 246, 236)) : (T.dark ? RGBC(42, 23, 24) : RGBC(252, 236, 236));
}
static HBRUSH banner_brush(void) /* kept until the color changes */
{
    static HBRUSH b;
    static DWORD c = 0xffffffff;
    DWORD f = banner_fill();
    if (f != c || !b) {
        if (b) DeleteObject(b);
        b = CreateSolidBrush(f);
        c = f;
    }
    return b;
}

static void *sys_proc(const char *dll, const char *fn)
{
    void *m = LoadLibraryExA(dll, 0, 0x800 /* only from System32 */);
    return m ? GetProcAddress(m, fn) : 0;
}

/* Title bar and scrollbars follow the theme on Windows 10 (1809+) and 11;
   older Windows just keeps its normal title bar. */
static void apply_system_parts(void)
{
    typedef long(WINAPI * DwmSet)(HWND, DWORD, const void *, DWORD);
    typedef long(WINAPI * SetTheme)(HWND, const WCHAR *, const WCHAR *);
    static DwmSet dwm;
    static SetTheme theme;
    static bool looked;
    if (!looked) {
        looked = true;
        dwm = (DwmSet)sys_proc("dwmapi.dll", "DwmSetWindowAttribute");
        theme = (SetTheme)sys_proc("uxtheme.dll", "SetWindowTheme");
    }
    if (dwm) {
        BOOL on = T.dark;
        if (dwm(g_wnd, 20, &on, sizeof on) != 0) dwm(g_wnd, 19, &on, sizeof on); /* dark title bar */
    }
    if (theme) {
        const WCHAR *name = T.dark ? WTEXT("DarkMode_Explorer") : WTEXT("Explorer");
        theme(g_c[ID_LOG], name, 0);
        theme(g_c[ID_HOWTO], name, 0);
        theme(g_c[ID_MODS], name, 0);
        theme(g_c[ID_MODS2], name, 0);
        theme(g_c[ID_MODSTOP], name, 0);
        theme(g_c[ID_LIST], name, 0);
    }
}

static void save_theme(void) { file_write_text("scctlink_theme.txt", T.dark ? "dark\n" : "light\n"); }

static bool saved_light(void)
{
    FILE *f = fopen("scctlink_theme.txt", "rb");
    if (!f) return false; /* first start: dark */
    char b[16] = { 0 };
    fread(b, 1, sizeof b - 1, f);
    fclose(f);
    return b[0] == 'l';
}

static void set_theme(bool dark)
{
    T = dark ? DARK : LIGHT;
    if (g_br_bg) {
        DeleteObject(g_br_bg); DeleteObject(g_br_panel); DeleteObject(g_br_accent); DeleteObject(g_br_border);
        DeleteObject(g_br_side); DeleteObject(g_br_field);
    }
    g_br_side = CreateSolidBrush(T.side);
    g_br_field = CreateSolidBrush(T.field);
    g_br_border = CreateSolidBrush(T.border);
    g_br_bg = CreateSolidBrush(T.bg);
    g_br_panel = CreateSolidBrush(T.panel);
    g_br_accent = CreateSolidBrush(T.accent);
    if (g_wnd) {
        SetWindowTextW(g_c[ID_THEME], T.dark ? WTEXT("Light theme") : WTEXT("Dark theme"));
        apply_system_parts();
        RedrawWindow(g_wnd, 0, 0, 0x1 | 0x4 | 0x80 | 0x400); /* invalidate, erase, children, frame */
    }
}

static bool is_primary(UINT id) { return id == ID_ACTION || id == ID_LAUNCH || id == ID_SET_APPLY; }
static bool is_tab(UINT id)
{
    return id == ID_NAV_PLAY || id == ID_TAB_HOWTO || id == ID_TAB_LOG || id == ID_TAB_MODS || id == ID_TAB_SET || id == ID_THEME;
}
static int tab_of(UINT id)
{
    return id == ID_NAV_PLAY ? TAB_PLAY : id == ID_TAB_LOG ? TAB_LOG : id == ID_TAB_MODS ? TAB_MODS : id == ID_TAB_SET ? TAB_SET
         : id == ID_TAB_HOWTO ? TAB_HOWTO : -1;
}
static DWORD red_text(void) { return T.dark ? RGBC(240, 98, 98) : RGBC(196, 40, 40); }

/* The sidebar's pages: the picked one on a lighter pill with a green bar on its left. */
static void draw_tab(DrawItem *d)
{
    bool on = tab_of(d->id) == g_tab;
    FillRect(d->dc, &d->rc, g_br_side);
    if (on) {
        HBRUSH b = CreateSolidBrush(T.navsel);
        HANDLE p = CreatePen(0, 1, T.dark ? T.navsel : T.border);
        HANDLE ob = SelectObject(d->dc, b), op = SelectObject(d->dc, p);
        RoundRect(d->dc, d->rc.left, d->rc.top, d->rc.right, d->rc.bottom, S_(10), S_(10));
        SelectObject(d->dc, ob);
        SelectObject(d->dc, op);
        DeleteObject(b);
        DeleteObject(p);
        RECT u = { d->rc.left + S_(6), d->rc.top + S_(9), d->rc.left + S_(9), d->rc.bottom - S_(9) };
        FillRect(d->dc, &u, g_br_accent);
    }
    WCHAR w[32];
    int n = GetWindowTextW(d->hwnd, w, 32);
    WCHAR ic = d->id == ID_NAV_PLAY ? IC_HOME : d->id == ID_TAB_MODS ? IC_PUZZLE : d->id == ID_TAB_SET ? IC_SETTINGS
             : d->id == ID_TAB_LOG ? IC_LIST : d->id == ID_TAB_HOWTO ? IC_HELP : T.dark ? IC_SUN : IC_MOON;
    RECT ir = { d->rc.left + S_(16), d->rc.top, d->rc.left + S_(34), d->rc.bottom };
    glyph(d->dc, ic, ir, g_icon, on ? T.text : T.dim);
    HANDLE of = SelectObject(d->dc, on ? g_bold : g_font);
    SetBkMode(d->dc, TRANSPARENT_BK);
    SetTextColor(d->dc, on ? T.text : T.dim);
    RECT tr = d->rc;
    tr.left += S_(44);
    DrawTextW(d->dc, w, n, &tr, DT_VCENTER | DT_SINGLELINE);
    SelectObject(d->dc, of);
    if ((d->id == ID_TAB_LOG && g_log_unseen) || (d->id == ID_TAB_MODS && g_mods_unseen)) { /* dot: something needs a look */
        DWORD c = d->id == ID_TAB_LOG ? red_text() : T.status;
        HBRUSH b = CreateSolidBrush(c);
        HANDLE p = CreatePen(0, 1, c);
        HANDLE ob = SelectObject(d->dc, b), op = SelectObject(d->dc, p);
        int r = S_(8), x = d->rc.right - S_(20), y = (d->rc.top + d->rc.bottom - r) / 2;
        RoundRect(d->dc, x, y, x + r, y + r, r, r);
        SelectObject(d->dc, ob);
        SelectObject(d->dc, op);
        DeleteObject(b);
        DeleteObject(p);
    }
}

/* Play's Host | Join: what the start button next to it does (while stopped). */
static int g_role; /* 0 host, 1 join */
static bool fix_ok(void);

/* Two halves, the picked one filled green; grey while hosting or joining
   (it can't change then). Co-op | Versus, and Host | Join. */
static void draw_segment(DrawItem *d)
{
    if (d->id == ID_HOST) { /* Host | Join: the picked one, or what is running */
        const UiState *s = &g_shown;
        bool off = d->state & ODS_DISABLED, right = s->mode == MODE_JOIN || (s->mode == MODE_IDLE && g_role == 1);
        FillRect(d->dc, &d->rc, g_br_bg);
        HBRUSH b = CreateSolidBrush(T.dark ? T.side : T.btn_down);
        HANDLE p = CreatePen(0, 1, T.border);
        HANDLE ob = SelectObject(d->dc, b), op = SelectObject(d->dc, p);
        RoundRect(d->dc, d->rc.left, d->rc.top, d->rc.right, d->rc.bottom, S_(14), S_(14));
        SelectObject(d->dc, ob);
        SelectObject(d->dc, op);
        DeleteObject(b);
        DeleteObject(p);
        int mid = (d->rc.left + d->rc.right) / 2, k = S_(4);
        RECT half = { right ? mid + k / 2 : d->rc.left + k, d->rc.top + k, right ? d->rc.right - k : mid - k / 2, d->rc.bottom - k };
        DWORD fill = T.panel, edge = off ? T.border : right ? T.dim : T.accent;
        b = CreateSolidBrush(fill);
        p = CreatePen(0, S_(1) + (off ? 0 : 1), edge);
        ob = SelectObject(d->dc, b);
        op = SelectObject(d->dc, p);
        RoundRect(d->dc, half.left, half.top, half.right, half.bottom, S_(12), S_(12));
        SelectObject(d->dc, ob);
        SelectObject(d->dc, op);
        DeleteObject(b);
        DeleteObject(p);
        /* each half: an icon, the name and a line under it; the picked one bright */
        SetBkMode(d->dc, TRANSPARENT_BK);
        for (int i = 0; i < 2; i++) {
            bool on = (i == 1) == right;
            int l = i ? mid : d->rc.left, r = i ? d->rc.right : mid;
            DWORD ic = !on ? T.dim : i ? T.text : T.status;
            RECT ir = { l + S_(14), d->rc.top, l + S_(40), d->rc.bottom };
            glyph(d->dc, i ? IC_JOIN : IC_HOST, ir, g_icon_big, ic);
            HANDLE of = SelectObject(d->dc, g_bold);
            SetTextColor(d->dc, on ? T.text : T.dim);
            RECT tr = { l + S_(48), d->rc.top + S_(13), r - S_(8), d->rc.top + S_(32) };
            DrawTextW(d->dc, i ? WTEXT("Join a friend") : WTEXT("Host a game"), -1, &tr, DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            SelectObject(d->dc, g_font);
            SetTextColor(d->dc, T.dim);
            RECT sr = { l + S_(48), d->rc.top + S_(32), r - S_(8), d->rc.bottom - S_(10) };
            DrawTextW(d->dc, i ? WTEXT("From the list or a code") : WTEXT("Friends-only lobby"), -1, &sr, DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            SelectObject(d->dc, of);
        }
        return;
    }
    bool off = d->state & ODS_DISABLED;
    FillRect(d->dc, &d->rc, g_br_bg);
    HBRUSH b = CreateSolidBrush(T.dark ? T.side : T.btn_down);
    HANDLE p = CreatePen(0, 1, T.border);
    HANDLE ob = SelectObject(d->dc, b), op = SelectObject(d->dc, p);
    RoundRect(d->dc, d->rc.left, d->rc.top, d->rc.right, d->rc.bottom, S_(12), S_(12));
    SelectObject(d->dc, ob);
    SelectObject(d->dc, op);
    DeleteObject(b);
    DeleteObject(p);
    int mid = (d->rc.left + d->rc.right) / 2, k = S_(3);
    bool vs = g_shown.versus;
    RECT half = { vs ? mid : d->rc.left + k, d->rc.top + k, vs ? d->rc.right - k : mid, d->rc.bottom - k };
    b = CreateSolidBrush(T.navsel);
    p = CreatePen(0, 1, T.dark ? T.navsel : T.border);
    ob = SelectObject(d->dc, b);
    op = SelectObject(d->dc, p);
    RoundRect(d->dc, half.left, half.top, half.right, half.bottom, S_(9), S_(9));
    SelectObject(d->dc, ob);
    SelectObject(d->dc, op);
    DeleteObject(b);
    DeleteObject(p);
    SetBkMode(d->dc, TRANSPARENT_BK);
    RECT l = { d->rc.left, d->rc.top, mid, d->rc.bottom }, r = { mid, d->rc.top, d->rc.right, d->rc.bottom };
    HANDLE of = SelectObject(d->dc, !vs ? g_bold : g_font);
    SetTextColor(d->dc, !vs ? (off ? T.dim : T.text) : T.dim);
    DrawTextW(d->dc, WTEXT("Co-op"), -1, &l, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(d->dc, vs ? g_bold : g_font);
    SetTextColor(d->dc, vs ? (off ? T.dim : T.text) : T.dim);
    DrawTextW(d->dc, WTEXT("Versus"), -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(d->dc, of);
}

/* Play's fix state, at the status card's right: a green "SCCT Fix on" label,
   or a red "Install SCCT Fix" button (same as Settings' Install). */
static void draw_playfix(DrawItem *d)
{
    bool down = d->state & ODS_SELECTED, focus = d->state & ODS_FOCUS, ok = fix_ok(), vs = g_shown.versus;
    FillRect(d->dc, &d->rc, g_br_panel);
    DWORD fill = ok ? (T.dark ? RGBC(22, 48, 31) : RGBC(232, 246, 236)) : down ? (T.dark ? RGBC(70, 30, 32) : RGBC(246, 218, 218))
                                                                            : (T.dark ? RGBC(52, 26, 28) : RGBC(252, 236, 236));
    DWORD edge = ok ? fill : focus ? red_text() : (T.dark ? RGBC(110, 50, 52) : RGBC(230, 180, 180));
    HBRUSH b = CreateSolidBrush(fill);
    HANDLE p = CreatePen(0, 1, edge);
    HANDLE ob = SelectObject(d->dc, b), op = SelectObject(d->dc, p);
    RoundRect(d->dc, d->rc.left, d->rc.top, d->rc.right, d->rc.bottom, S_(12), S_(12));
    SelectObject(d->dc, ob);
    SelectObject(d->dc, op);
    DeleteObject(b);
    DeleteObject(p);
    const WCHAR *t = ok ? (vs ? WTEXT("Versus Fix on") : WTEXT("SCCT Fix on"))
                        : (vs ? (g_set.vfix == 3 ? WTEXT("Update Versus Fix") : WTEXT("Install Versus Fix"))
                              : (g_set.scctfix == 3 ? WTEXT("Update SCCT Fix") : WTEXT("Install SCCT Fix")));
    RECT tr = d->rc;
    if (down) { tr.top += 1; tr.bottom += 1; }
    icon_text(d->dc, tr, ok ? IC_CHECK : IC_DOWNLOAD, t, -1, ok ? g_font : g_bold, ok ? T.status : red_text());
}

/* An I / O switch: a pill (green with I when on, grey with O when off) and its name. */
static bool g_vf_hud, g_vf_rate, g_vf_full = true;
static bool is_switch(UINT id)
{
    return id == ID_SET_CHUD || id == ID_SET_INTRO || id == ID_SET_MOUSE || id == ID_SET_VHUD || id == ID_SET_VRATE || id == ID_SET_COOP60;
}
static bool switch_on(UINT id)
{
    return id == ID_SET_CHUD ? g_set.chud == 1 : id == ID_SET_INTRO ? g_set.skipintro == 1 : id == ID_SET_MOUSE ? g_set.rawmouse == 1
         : id == ID_SET_COOP60 ? g_set.coop60 == 1
         : id == ID_SET_VHUD ? g_vf_full : g_vf_rate;
}
/* Single player's FPS: the name, then 60 | Screen | Unlock (click one) */
#define SPFPS_SEG 146 /* the three parts' width, at the right */
static void draw_spfps(DrawItem *d)
{
    bool off = d->state & ODS_DISABLED;
    FillRect(d->dc, &d->rc, g_br_panel);
    int x = d->rc.right - S_(SPFPS_SEG), sel = g_set.spfps == 60 ? 0 : g_set.spfps == -1 ? 1 : g_set.spfps == 0 ? 2 : -1;
    SetBkMode(d->dc, TRANSPARENT_BK);
    HANDLE of = SelectObject(d->dc, g_font);
    SetTextColor(d->dc, off ? T.dis_text : T.text);
    RECT tr = { d->rc.left, d->rc.top, x - S_(8), d->rc.bottom };
    DrawTextW(d->dc, WTEXT("Single player"), -1, &tr, DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    HBRUSH b = CreateSolidBrush(T.dark ? T.side : T.btn_down);
    HANDLE p = CreatePen(0, 1, T.border);
    HANDLE ob = SelectObject(d->dc, b), op = SelectObject(d->dc, p);
    RoundRect(d->dc, x, d->rc.top, d->rc.right, d->rc.bottom, S_(10), S_(10));
    SelectObject(d->dc, ob);
    SelectObject(d->dc, op);
    DeleteObject(b);
    DeleteObject(p);
    static const WCHAR *const names[3] = { L"60", L"Screen", L"Unlock" };
    int pw = (d->rc.right - x) / 3, k = S_(3);
    for (int i = 0; i < 3; i++) {
        RECT r = { x + i * pw, d->rc.top, i == 2 ? d->rc.right : x + (i + 1) * pw, d->rc.bottom };
        if (i == sel) {
            b = CreateSolidBrush(T.navsel);
            p = CreatePen(0, 1, T.dark ? T.navsel : T.border);
            ob = SelectObject(d->dc, b);
            op = SelectObject(d->dc, p);
            RoundRect(d->dc, r.left + k, r.top + k, r.right - k, r.bottom - k, S_(8), S_(8));
            SelectObject(d->dc, ob);
            SelectObject(d->dc, op);
            DeleteObject(b);
            DeleteObject(p);
        }
        SelectObject(d->dc, i == sel ? g_bold : g_font);
        SetTextColor(d->dc, off ? T.dis_text : i == sel ? T.text : T.dim);
        DrawTextW(d->dc, names[i], -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    SelectObject(d->dc, of);
}

static void draw_switch(DrawItem *d)
{
    /* a settings row: the name on the left, a small switch on the right (greyed: its state, faded) */
    bool off = d->state & ODS_DISABLED, on = switch_on(d->id), focus = d->state & ODS_FOCUS;
    FillRect(d->dc, &d->rc, g_br_panel);
    int h = S_(18), w = S_(34), x = d->rc.right - w, y = (d->rc.top + d->rc.bottom - h) / 2;
    DWORD fill = on ? (off ? (T.dark ? RGBC(30, 70, 42) : RGBC(160, 205, 172)) : T.accent) : (off ? T.dis_bg : T.btn);
    DWORD edge = on ? fill : off ? T.dis_bg : T.border;
    HBRUSH b = CreateSolidBrush(fill);
    HANDLE p = CreatePen(0, 1, focus && !off ? T.text : edge);
    HANDLE ob = SelectObject(d->dc, b), op = SelectObject(d->dc, p);
    RoundRect(d->dc, x, y, x + w, y + h, h, h);
    SelectObject(d->dc, ob);
    SelectObject(d->dc, op);
    DeleteObject(b);
    DeleteObject(p);
    int k = h - S_(4), kx = on ? x + w - k - S_(2) : x + S_(2), ky = y + S_(2); /* the knob */
    DWORD kc = on ? (off ? T.dis_text : RGBC(255, 255, 255)) : off ? T.dis_text : T.dim;
    b = CreateSolidBrush(kc);
    p = CreatePen(0, 1, kc);
    ob = SelectObject(d->dc, b);
    op = SelectObject(d->dc, p);
    RoundRect(d->dc, kx, ky, kx + k, ky + k, k, k);
    SelectObject(d->dc, ob);
    SelectObject(d->dc, op);
    DeleteObject(b);
    DeleteObject(p);
    SetBkMode(d->dc, TRANSPARENT_BK);
    HANDLE of = SelectObject(d->dc, g_font);
    WCHAR t[64];
    int n = GetWindowTextW(d->hwnd, t, 64);
    SetTextColor(d->dc, off ? T.dis_text : T.text);
    RECT tr = { d->rc.left, d->rc.top, x - S_(10), d->rc.bottom };
    DrawTextW(d->dc, t, n, &tr, DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(d->dc, of);
}

/* What a button sits on: a card (panel), the banner, or the page. */
static bool on_card(UINT id)
{
    return id == ID_ACTION || id == ID_REFRESH || id == ID_COPYID || id == ID_JOINID ||
           (id >= ID_SET_NOTE && id <= ID_SET_APPLY && id != ID_SET_NOTE && id != ID_SET_RESET && id != ID_SET_FRIEND && id != ID_SET_APPLY);
}

static void draw_button(DrawItem *d)
{
    if (is_tab(d->id)) { draw_tab(d); return; }
    if (is_switch(d->id)) { draw_switch(d); return; }
    if (d->id == ID_GAMEMODE) { draw_segment(d); return; }
    if (d->id == ID_HOST) { draw_segment(d); return; }
    if (d->id == ID_PLAYFIX) { draw_playfix(d); return; }
    if (d->id == ID_SET_SPFPS) { draw_spfps(d); return; }
    bool down = d->state & ODS_SELECTED, off = d->state & ODS_DISABLED, focus = d->state & ODS_FOCUS;
    bool go_stop = d->id == ID_JOIN && g_shown.mode != MODE_IDLE; /* Play's start button: "Stop" while running */
    DWORD fill, text, edge;
    if (off) { fill = T.dis_bg; text = T.dis_text; edge = T.dis_bg; }
    else if (is_primary(d->id) || (d->id == ID_JOIN && !go_stop)) { fill = down ? T.accent_down : T.accent; text = T.sel_text; edge = fill; }
    else if (d->id == ID_SET_RESET || d->id == ID_STOP || go_stop) { fill = down ? T.btn_down : T.btn; text = red_text(); edge = T.dark ? RGBC(90, 46, 48) : RGBC(230, 180, 180); }
    else { fill = down ? T.btn_down : T.btn; text = T.text; edge = T.border; }
    FillRect(d->dc, &d->rc, d->id == ID_SET_FIX ? banner_brush() : on_card(d->id) ? g_br_panel : g_br_bg);
    HBRUSH b = CreateSolidBrush(fill);
    HANDLE p = CreatePen(0, 1, edge);
    HANDLE ob = SelectObject(d->dc, b), op = SelectObject(d->dc, p);
    int r = S_(10);
    RoundRect(d->dc, d->rc.left, d->rc.top, d->rc.right, d->rc.bottom, r, r);
    if (focus && !off) { /* keyboard focus ring */
        HANDLE fp = CreatePen(0, 1, is_primary(d->id) ? T.sel_text : T.dim);
        SelectObject(d->dc, fp);
        SelectObject(d->dc, GetStockObject(NULL_BRUSH));
        int k = S_(3);
        RoundRect(d->dc, d->rc.left + k, d->rc.top + k, d->rc.right - k, d->rc.bottom - k, r, r);
        SelectObject(d->dc, p);
        DeleteObject(fp);
    }
    SelectObject(d->dc, ob);
    SelectObject(d->dc, op);
    DeleteObject(b);
    DeleteObject(p);
    WCHAR w[64];
    int n = GetWindowTextW(d->hwnd, w, 64);
    RECT tr = d->rc;
    if (down) { tr.top += 1; tr.bottom += 1; }
    bool host = g_shown.mode == MODE_HOST;
    WCHAR ic = d->id == ID_LAUNCH ? IC_PLAY : d->id == ID_STATS ? IC_STATS : d->id == ID_COPYCONN ? IC_INFO
             : d->id == ID_REFRESH ? IC_REFRESH : d->id == ID_COPYID || d->id == ID_COPYLOG ? IC_COPY
             : d->id == ID_JOINID ? IC_JOIN : d->id == ID_ACTION ? (host ? IC_KICK : IC_JOIN)
             : d->id == ID_JOIN ? (go_stop ? IC_STOP : g_role ? IC_JOIN : IC_PLAY)
             : d->id == ID_RECHECK ? IC_REFRESH : d->id == ID_REPORT ? IC_INFO : d->id == ID_SET_FIX ? (fix_ok() ? IC_DELETE : IC_DOWNLOAD) : 0;
    icon_text(d->dc, tr, ic, w, n, is_primary(d->id) || d->id == ID_JOIN ? g_bold : g_font, text);
}

static void draw_list_item(DrawItem *d)
{
    bool sel = d->state & ODS_SELECTED;
    FillRect(d->dc, &d->rc, sel ? g_br_accent : g_br_field);
    if (d->item == (UINT)-1) return;
    WCHAR w[UI_NAME + 8];
    int n = (int)SendMessageW(d->hwnd, LB_GETTEXT, d->item, (LPARAM)w);
    if (n <= 0) return;
    RECT ir = { d->rc.left + S_(8), d->rc.top, d->rc.left + S_(26), d->rc.bottom };
    glyph(d->dc, IC_PERSON, ir, g_icon, sel ? T.sel_text : T.dim);
    HANDLE of = SelectObject(d->dc, g_font);
    SetBkMode(d->dc, TRANSPARENT_BK);
    SetTextColor(d->dc, sel ? T.sel_text : T.text);
    RECT tr = d->rc;
    tr.left += S_(34);
    DrawTextW(d->dc, w, n, &tr, DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(d->dc, of);
}

/* A Mods tab row. Its first character says how: '#' a name (bold), '+' good
   (green), '!' a problem (red), '-' a divider, '.' a hint (grey), ' ' plain. */
static void draw_mods_item(DrawItem *d)
{
    FillRect(d->dc, &d->rc, g_br_panel);
    if (d->item == (UINT)-1) return;
    WCHAR w[320];
    int n = (int)SendMessageW(d->hwnd, LB_GETTEXT, d->item, (LPARAM)w);
    if (n <= 0) return;
    WCHAR kind = w[0];
    if (kind == '-') { /* divider */
        RECT line = { d->rc.left + S_(6), (d->rc.top + d->rc.bottom) / 2, d->rc.right - S_(6), (d->rc.top + d->rc.bottom) / 2 + 1 };
        HBRUSH b = CreateSolidBrush(T.border);
        FillRect(d->dc, &line, b);
        DeleteObject(b);
        return;
    }
    DWORD color = kind == '+' ? T.status : kind == '!' ? (T.dark ? RGBC(240, 98, 98) : RGBC(196, 40, 40)) : kind == '.' ? T.dim : T.text;
    HANDLE of = SelectObject(d->dc, kind == '#' ? g_bold : g_font);
    SetBkMode(d->dc, TRANSPARENT_BK);
    SetTextColor(d->dc, color);
    RECT tr = d->rc;
    tr.left += S_(kind == ' ' ? 18 : 6);
    const WCHAR *t = w + 1;
    if (kind == '+' || kind == '!') { /* a green or red dot in front */
        HBRUSH b = CreateSolidBrush(color);
        HANDLE p = CreatePen(0, 1, color);
        HANDLE ob = SelectObject(d->dc, b), op = SelectObject(d->dc, p);
        int r = S_(8), y = (d->rc.top + d->rc.bottom - r) / 2;
        RoundRect(d->dc, tr.left, y, tr.left + r, y + r, r, r);
        SelectObject(d->dc, ob);
        SelectObject(d->dc, op);
        DeleteObject(b);
        DeleteObject(p);
        tr.left += S_(16);
    }
    DrawTextW(d->dc, t, n - 1, &tr, DT_VCENTER | DT_SINGLELINE | (kind == '!' || kind == ' ' ? 0x4000 /* DT_PATH_ELLIPSIS */ : DT_END_ELLIPSIS));
    SelectObject(d->dc, of);
}

/* colors for labels and text boxes; returns the background brush */
static LRESULT control_colors(UINT msg, HDC dc, HWND ctl_)
{
    if (ctl_ == g_c[ID_MODSDIV]) return (LRESULT)g_br_border; /* the line between you and your friends */
    int sid = 0; /* a label on the Settings page? */
    for (int i = ID_SET_NOTE; i <= ID_SET_APPLY && !sid; i++) if (ctl_ == g_c[i]) sid = i;
    /* text boxes and the friends list: the field color; everything else here is on a card,
       except the Settings note under the cards */
    bool field = msg == WM_CTLCOLOREDIT && ctl_ != g_c[ID_LOG] && ctl_ != g_c[ID_HOWTO];
    for (int i = 0; i < g_nframes && !field; i++) field = ctl_ == g_c[g_frames[i].id]; /* framed boxes, also when greyed */
    bool page = sid == ID_SET_NOTE;
    bool dim = ctl_ == g_c[ID_LOBBYLABEL] || ctl_ == g_c[ID_STATUS2] || ctl_ == g_c[ID_LISTLABEL] || (sid >= ID_SF_RES && sid <= ID_SF_FOV) || sid == ID_SF_MOUSE || sid == ID_SF_VFOV ||
               (sid == ID_SET_NOTE && !g_set_warn) || (sid >= ID_SET_BANSUB && sid <= ID_SET_VRATENOTE);
    DWORD tc = sid == ID_SET_NOTE && g_set_warn ? T.status : dim ? T.dim : T.text;
    if (sid == ID_SET_BANNER) tc = fix_ok() ? T.status : red_text();
    if (ctl_ == g_c[ID_STATUS] && g_set.found && !fix_ok()) tc = red_text(); /* not ready: the fix is missing */
    SetTextColor(dc, tc);
    if (sid == ID_SET_BANNER || sid == ID_SET_BANSUB) {
        SetBkColor(dc, banner_fill());
        return (LRESULT)banner_brush();
    }
    SetBkColor(dc, field ? T.field : page ? T.bg : T.panel);
    return (LRESULT)(field ? g_br_field : page ? g_br_bg : g_br_panel);
}

static WCHAR *wide(const char *s, WCHAR *buf, int n)
{
    int r = MultiByteToWideChar(CP_UTF8, 0, s, -1, buf, n);
    if (r <= 0) buf[0] = 0;
    buf[n - 1] = 0;
    return buf;
}

static void set_text(int id, const char *utf8)
{
    static WCHAR w[512];
    SetWindowTextW(g_c[id], wide(utf8, w, 512));
}

/* For labels updated on every tick: only touch the control when the text changes. */
static void set_text_once(int id, const char *literal)
{
    static const char *last[ID_GAMEMODE + 1];
    if (last[id] == literal) return;
    last[id] = literal;
    set_text(id, literal);
}

static HWND ctl(int id, const WCHAR *cls, const WCHAR *text, DWORD style, DWORD ex, int x, int y, int w, int h, HFONT f)
{
    HWND h_ = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, S_(x), S_(y), S_(w), S_(h), g_wnd,
                              (HMENU)(uintptr_t)id, GetModuleHandleW(0), 0);
    SendMessageW(h_, WM_SETFONT, (WPARAM)f, 1);
    g_c[id] = h_;
    return h_;
}

static HWND ctl(int id, const WCHAR *cls, const WCHAR *text, DWORD style, DWORD ex, int x, int y, int w, int h, HFONT f);

/* a box (text box or list) inside a drawn frame; ix/iy = space inside the frame */
static int g_frame_tab = -1; /* the tab the next framed() boxes belong to */
/* The Play page's list card, the Settings page's cards (96 dpi units) */
#define LIST_Y 216
#define LIST_H 210
#define SET_L CX            /* Settings: left column */
#define SET_R (CX + 282)    /* right column */
#define SET_CW 270          /* a column's width */
#define SET_Y1 88           /* first row of cards */
#define SET_H1 150
#define SET_Y2 250          /* second row */
#define SET_H2 136
static void framed(int id, const WCHAR *cls, DWORD style, int x, int y, int w, int h, int ix, int iy)
{
    if (g_nframes < 24) g_frames[g_nframes++] = (Frame){ id, x, y, w, h, g_frame_tab };
    ctl(id, cls, WTEXT(""), style, 0, x + ix, y + iy, w - 2 * ix, h - 2 * iy, g_font);
}

static void build(void)
{
    const WCHAR *B = WTEXT("BUTTON"), *T_ = WTEXT("STATIC"), *E = WTEXT("EDIT"), *L = WTEXT("LISTBOX");
    const DWORD OD = WS_TABSTOP | BS_OWNERDRAW;
    /* the sidebar: the logo and name are drawn (draw_page), the pages are buttons */
    ctl(ID_NAV_PLAY, B, WTEXT("Play"), OD, 0, 10, 70, 148, 36, g_font);
    ctl(ID_TAB_MODS, B, WTEXT("Mods"), OD, 0, 10, 110, 148, 36, g_font);
    ctl(ID_TAB_SET, B, WTEXT("Settings"), OD, 0, 10, 150, 148, 36, g_font);
    ctl(ID_TAB_LOG, B, WTEXT("Log"), OD, 0, 10, 190, 148, 36, g_font);
    ctl(ID_TAB_HOWTO, B, WTEXT("How to"), OD, 0, 10, WIN_H - 92, 148, 36, g_font);
    ctl(ID_THEME, B, WTEXT(""), OD, 0, 10, WIN_H - 52, 148, 36, g_font);

    /* Play: Co-op | Versus and Stop, the status card, Host and Join, the list card, Start */
    ctl(ID_GAMEMODE, B, WTEXT("Co-op"), OD, 0, CX, 20, 160, 32, g_font);
    ctl(ID_STOP, B, WTEXT("Stop"), OD, 0, CX + CW - 100, 20, 100, 34, g_font);
    ctl(ID_STATUS, T_, WTEXT(""), 0x4000 /* SS_ENDELLIPSIS */, 0, CX + 38, 74, CW - 38 - 186, 22, g_mid);
    ctl(ID_STATUS2, T_, WTEXT(""), 0x4000, 0, CX + 38, 98, CW - 38 - 186, 18, g_font);
    ctl(ID_PLAYFIX, B, WTEXT(""), OD, 0, CX + CW - 16 - 150, 66 + 16, 150, 28, g_bold);
    /* Host | Join (pick one), and the button that starts it (Stop while running) */
    ctl(ID_HOST, B, WTEXT("Host or join"), OD, 0, CX, 138, 400, 64, g_bold);
    ctl(ID_JOIN, B, WTEXT("Host"), OD, 0, CX + 412, 138, 140, 64, g_bold);
    ctl(ID_LISTLABEL, T_, WTEXT(""), 0x4000, 0, CX + 16, LIST_Y + 14, 300, 20, g_font);
    ctl(ID_ACTION, B, WTEXT("Join"), OD, 0, CX + CW - 16 - 196, LIST_Y + 10, 92, 28, g_bold);
    ctl(ID_REFRESH, B, WTEXT("Refresh"), OD, 0, CX + CW - 16 - 96, LIST_Y + 10, 96, 28, g_font);
    framed(ID_LIST, L, WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED |
           LBS_HASSTRINGS, CX + 12, LIST_Y + 44, CW - 24, 108, 4, 4);
    ctl(ID_LOBBYLABEL, T_, WTEXT("Lobby code"), 0, 0, CX + 16, LIST_Y + 170, 80, 20, g_font);
    framed(ID_LOBBY, E, WS_TABSTOP | ES_AUTOHSCROLL, CX + 100, LIST_Y + 164, 180, 30, 8, 7);
    ctl(ID_COPYID, B, WTEXT("Copy code"), OD, 0, CX + 290, LIST_Y + 164, 110, 30, g_font);
    ctl(ID_JOINID, B, WTEXT("Join this code"), OD, 0, CX + 408, LIST_Y + 164, 128, 30, g_font);
    ctl(ID_LAUNCH, B, WTEXT("Start Splinter Cell"), OD, 0, CX, 442, 300, 42, g_bold);
    ctl(ID_STATS, B, WTEXT("Live stats"), OD, 0, CX + 312, 442, 116, 42, g_font);
    ctl(ID_COPYCONN, B, WTEXT("Details"), OD, 0, CX + 436, 442, 116, 42, g_font);

    /* Log and How to: a title (drawn), the box in a card; Log's buttons on the title's line */
    ctl(ID_REPORT, B, WTEXT("Report"), OD, 0, CX + CW - 216, 20, 100, 30, g_font);
    ctl(ID_COPYLOG, B, WTEXT("Copy log"), OD, 0, CX + CW - 108, 20, 108, 30, g_font);
    ctl(ID_LOG, E, WTEXT(""), WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY, 0, CX + 14, 72, CW - 28, 402, g_font);
    ctl(ID_HOWTO, E, WTEXT(""), WS_VSCROLL | ES_MULTILINE | ES_READONLY, 0, CX + 14, 72, CW - 28, 402, g_font);
    SendMessageW(g_c[ID_LOG], EM_SETLIMITTEXT, 0x7FFFFFFE, 0);

    /* Mods: you on the left, your friends on the right, a line between; one line
       per row, drawn in colors (see draw_mods_item). The top line, across both:
       "All party members match" (green) or who doesn't (red). */
    ctl(ID_RECHECK, B, WTEXT("Check again"), OD, 0, CX + CW - 130, 20, 130, 30, g_font);
    const DWORD MODS = WS_VSCROLL | LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | 0x4000 /* LBS_NOSEL */;
    ctl(ID_MODSTOP, L, WTEXT(""), LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | 0x4000 /* LBS_NOSEL */,
        0, CX + 14, 74, CW - 28, 21, g_font);
    ctl(ID_MODS, L, WTEXT(""), MODS, 0, CX + 14, 102, 262, 372, g_font);
    ctl(ID_MODSDIV, T_, WTEXT(""), 0, 0, CX + 14 + 268, 106, 1, 364, g_font);
    ctl(ID_MODS2, L, WTEXT(""), MODS, 0, CX + 14 + 274, 102, CW - 28 - 274, 372, g_font);
    SendMessageW(g_c[ID_MODS], LB_ADDSTRING, 0, (LPARAM)WTEXT(".Checking the game files..."));

    /* Settings: the fix's banner, then cards (their titles are drawn, see
       draw_set_deco); co-op and Versus each show their own controls */
    const DWORD NUM = WS_TABSTOP | ES_AUTOHSCROLL | 0x2000 /* ES_NUMBER */, ELL = 0x4000 /* SS_ENDELLIPSIS */;
    const DWORD TXT = WS_TABSTOP | ES_AUTOHSCROLL;
    g_vf_hud = g_vf_rate = true;
    g_frame_tab = TAB_SET;
    ctl(ID_SET_BANNER, T_, WTEXT(""), ELL, 0, CX + 16, 28, 410, 18, g_bold);
    ctl(ID_SET_BANSUB, T_, WTEXT(""), ELL, 0, CX + 16, 48, 410, 16, g_font);
    ctl(ID_SET_FIX, B, WTEXT("Install"), OD, 0, CX + CW - 16 - 100, 34, 100, 28, g_font);
    ctl(ID_SEC_DISP, T_, WTEXT(""), 0, 0, 0, 0, 1, 1, g_font);  /* section titles: drawn now */
    ctl(ID_SEC_MOUSE, T_, WTEXT(""), 0, 0, 0, 0, 1, 1, g_font);
    ctl(ID_SEC_CLASS, T_, WTEXT(""), 0, 0, 0, 0, 1, 1, g_font);
    /* Display (both): the resolution */
    ctl(ID_SL_RES, T_, WTEXT("Resolution"), ELL, 0, SET_L + 16, SET_Y1 + 40, 80, 18, g_font);
    framed(ID_SET_W, E, NUM, SET_L + 100, SET_Y1 + 34, 64, 28, 6, 6);
    ctl(ID_SL_X, T_, WTEXT("x"), 0x1 /* SS_CENTER */, 0, SET_L + 166, SET_Y1 + 40, 16, 18, g_font);
    framed(ID_SET_H, E, NUM, SET_L + 184, SET_Y1 + 34, 64, 28, 6, 6);
    ctl(ID_SF_RES, T_, WTEXT(""), ELL, 0, SET_L + 16, SET_Y1 + 66, SET_CW - 32, 16, g_font);
    /* co-op: FOV (needs the same number on both PCs, so it's a choice, never automatic) */
    ctl(ID_SL_FOV, T_, WTEXT("FOV"), ELL, 0, SET_L + 16, SET_Y1 + 98, 80, 18, g_font);
    framed(ID_SET_FOV, E, NUM, SET_L + 100, SET_Y1 + 92, 64, 28, 6, 6);
    ctl(ID_SF_FOV, T_, WTEXT(""), ELL, 0, SET_L + 16, SET_Y1 + 124, SET_CW - 32, 16, g_font);
    /* co-op: Game */
    ctl(ID_SET_CHUD, B, WTEXT("Widescreen HUD"), OD, 0, SET_R + 16, SET_Y1 + 32, SET_CW - 32, 26, g_font);
    ctl(ID_SET_INTRO, B, WTEXT("Skip intro videos"), OD, 0, SET_R + 16, SET_Y1 + 64, SET_CW - 32, 26, g_font);
    /* single player's frame rate: click for 60 / the screen's / no limit */
    ctl(ID_SET_SPFPS, B, WTEXT("Single player: 60 FPS"), OD, 0, SET_R + 16, SET_Y1 + 98, SET_CW - 32, 28, g_font);
    /* shown with "FPS unlock": the game itself stops at 100 FPS in play (measured, SCCTFix.log) */
    ctl(ID_SET_SPNOTE, T_, WTEXT(""), ELL, 0, SET_R + 16, SET_Y1 + 130, SET_CW - 32, 16, g_font);
    /* co-op: Co-op (60 FPS when this PC hosts; the joining friend's game follows the host) */
    ctl(ID_SET_COOP60, B, WTEXT("60 FPS when you host"), OD, 0, SET_L + 16, SET_Y2 + 32, SET_CW - 32, 26, g_font);
    /* co-op: Mouse */
    ctl(ID_SET_MOUSE, B, WTEXT("Raw mouse"), OD, 0, SET_R + 16, SET_Y2 + 32, SET_CW - 32, 26, g_font);
    ctl(ID_SET_RAWNOTE, T_, WTEXT("No acceleration, any polling rate"), ELL, 0, SET_R + 16, SET_Y2 + 60, SET_CW - 32, 16, g_font);
    ctl(ID_SL_MOUSE, T_, WTEXT("Sensitivity"), ELL, 0, SET_R + 16, SET_Y2 + 90, 68, 18, g_font);
    framed(ID_SET_SENS, E, TXT, SET_R + 86, SET_Y2 + 84, 54, 28, 6, 6);
    ctl(ID_SL_TURN, T_, WTEXT("Aim turn"), ELL, 0, SET_R + 148, SET_Y2 + 90, 56, 18, g_font);
    framed(ID_SET_TURN, E, TXT, SET_R + 204, SET_Y2 + 84, 50, 28, 6, 6);
    ctl(ID_SF_MOUSE, T_, WTEXT(""), ELL, 0, SET_R + 16, SET_Y2 + 116, SET_CW - 32, 16, g_font);
    /* not shown any more (the SCCT Fix keeps 60 FPS; no other widescreen fix is needed), still read for the check */
    ctl(ID_SF_VFOV, T_, WTEXT(""), 0, 0, 0, 0, 1, 1, g_font);
    /* Versus: Display (the Versus Fix's frame limit) */
    ctl(ID_SL_VFPS, T_, WTEXT("Max FPS (0 = none)"), ELL, 0, SET_L + 16, SET_Y1 + 98, 120, 18, g_font);
    framed(ID_SET_VFPS, E, NUM, SET_L + 140, SET_Y1 + 92, 64, 28, 6, 6);
    /* Versus: Window and mouse */
    ctl(ID_SET_VHUD, B, WTEXT("Fullscreen"), OD, 0, SET_R + 16, SET_Y1 + 32, SET_CW - 32, 26, g_font); /* off: a borderless window */
    ctl(ID_SET_VRATE, B, WTEXT("Mouse fix (125 Hz)"), OD, 0, SET_R + 16, SET_Y1 + 64, SET_CW - 32, 26, g_font);
    ctl(ID_SET_VRATENOTE, T_, WTEXT("For mice faster than 125 Hz (and Steam Link)"), ELL, 0, SET_R + 16, SET_Y1 + 94, SET_CW - 32, 16, g_font);
    /* Versus: Per class (one card across): turn speed and FOV, each box named Spy / Merc */
    ctl(ID_SL_VTURN, T_, WTEXT("Turn speed (%)"), ELL, 0, CX + 16, SET_Y2 + 42, 140, 18, g_font);
    ctl(ID_SL_VSPY1, T_, WTEXT("Spy"), 0, 0, CX + 170, SET_Y2 + 42, 34, 18, g_font);
    framed(ID_SET_VTURNS, E, NUM, CX + 206, SET_Y2 + 36, 64, 28, 6, 6);
    ctl(ID_SL_VMERC1, T_, WTEXT("Merc"), 0, 0, CX + 290, SET_Y2 + 42, 44, 18, g_font);
    framed(ID_SET_VTURNM, E, NUM, CX + 336, SET_Y2 + 36, 64, 28, 6, 6);
    ctl(ID_SL_VFOV, T_, WTEXT("FOV"), ELL, 0, CX + 16, SET_Y2 + 86, 140, 18, g_font);
    ctl(ID_SL_VSPY2, T_, WTEXT("Spy"), 0, 0, CX + 170, SET_Y2 + 86, 34, 18, g_font);
    framed(ID_SET_VFOVS, E, NUM, CX + 206, SET_Y2 + 80, 64, 28, 6, 6);
    ctl(ID_SL_VMERC2, T_, WTEXT("Merc"), 0, 0, CX + 290, SET_Y2 + 86, 44, 18, g_font);
    framed(ID_SET_VFOVM, E, NUM, CX + 336, SET_Y2 + 80, 64, 28, 6, 6);
    g_frame_tab = -1;
    /* under the cards: the note, Reset, Use friend's, Apply */
    ctl(ID_SET_NOTE, T_, WTEXT(""), ELL, 0, CX + 2, SET_Y2 + SET_H2 + 12, CW - 4, 16, g_font);
    ctl(ID_SET_RESET, B, WTEXT("Reset to defaults"), OD, 0, CX, SET_Y2 + SET_H2 + 38, 140, 32, g_font);
    ctl(ID_SET_FRIEND, B, WTEXT("Use friend's"), OD, 0, CX + CW - 236, SET_Y2 + SET_H2 + 38, 120, 32, g_font);
    ctl(ID_SET_APPLY, B, WTEXT("Apply"), OD, 0, CX + CW - 108, SET_Y2 + SET_H2 + 38, 108, 32, g_bold);
}

/* The Settings tab in co-op and in Versus: what each shows. */
static bool set_never(int id)
{
    return id == ID_SF_VFOV || id == ID_SEC_DISP || id == ID_SEC_MOUSE || id == ID_SEC_CLASS; /* titles are drawn now */
}
static bool set_versus_only(int id) { return (id >= ID_SL_VFPS && id <= ID_SF_VFOV) || id == ID_SEC_CLASS || id == ID_SET_VRATENOTE; }
static bool set_coop_only(int id)
{
    return id == ID_SL_FOV || id == ID_SET_FOV || id == ID_SF_FOV || id == ID_SET_FRIEND || id == ID_SET_MOUSE ||
           (id >= ID_SL_MOUSE && id <= ID_SF_MOUSE) || /* Versus: sensitivity is set in the game itself */
           id == ID_SET_CHUD || id == ID_SET_INTRO || id == ID_SET_RAWNOTE || id == ID_SET_SPFPS || id == ID_SET_SPNOTE ||
           id == ID_SET_COOP60;
}
static void move_ctl(int id, int x, int y) { SetWindowPos(g_c[id], 0, S_(x), S_(y), 0, 0, 0x1 | 0x4 | 0x10 /* no size, z-order, activate */); }

/* The page each control is on (-1: the sidebar, always shown). */
static int page_of(int id)
{
    if (id == ID_NAV_PLAY || id == ID_TAB_HOWTO || id == ID_TAB_LOG || id == ID_TAB_MODS || id == ID_TAB_SET || id == ID_THEME) return -1;
    if (id == ID_LOG || id == ID_REPORT || id == ID_COPYLOG) return TAB_LOG;
    if (id == ID_HOWTO) return TAB_HOWTO;
    if (id == ID_MODS || id == ID_MODS2 || id == ID_MODSDIV || id == ID_MODSTOP || id == ID_RECHECK) return TAB_MODS;
    if (id >= ID_SET_NOTE && id <= ID_SET_APPLY) return TAB_SET;
    return TAB_PLAY;
}
/* Shown now: on the page in view, and wanted in this mode / state. */
static bool ctl_wanted(int id)
{
    int pg = page_of(id);
    if (pg >= 0 && pg != g_tab) return false;
    const UiState *s = &g_shown;
    bool idle = s->mode == MODE_IDLE;
    if (pg == TAB_SET) return !set_never(id) && !(s->versus ? set_coop_only(id) : set_versus_only(id));
    if (id == ID_STOP) return false; /* the start button next to Host | Join is Stop while running */
    if (id == ID_ACTION) return !idle;
    if (id == ID_REFRESH) return s->mode != MODE_HOST;
    if (id == ID_PLAYFIX) return g_set.found;
    return true;
}
static void apply_visibility(void)
{
    for (int i = ID_HOST; i <= ID_GAMEMODE; i++) {
        if (!g_c[i]) continue;
        bool want = ctl_wanted(i);
        if (want != (IsWindowVisible(g_c[i]) != 0)) ShowWindow(g_c[i], want ? 5 : 0);
    }
}

static void append_log(const char *utf8)
{
    static WCHAR w[70000];
    wide(utf8, w, 70000);
    HWND h = g_c[ID_LOG];
    int len = (int)SendMessageW(h, WM_GETTEXTLENGTH, 0, 0);
    if (len > 400000) { /* keep the log from growing forever */
        SendMessageW(h, EM_SETSEL, 0, 200000);
        SendMessageW(h, EM_REPLACESEL, 0, (LPARAM)WTEXT(""));
        len = (int)SendMessageW(h, WM_GETTEXTLENGTH, 0, 0);
    }
    SendMessageW(h, EM_SETSEL, (WPARAM)len, len);
    SendMessageW(h, EM_REPLACESEL, 0, (LPARAM)w);
    SendMessageW(h, EM_SCROLLCARET, 0, 0);
}

static void show_list(int which)
{
    HWND lb = g_c[ID_LIST];
    SendMessageW(lb, LB_RESETCONTENT, 0, 0);
    static WCHAR w[UI_NAME + 8];
    for (int i = 0; i < g_nview[which]; i++)
        SendMessageW(lb, LB_ADDSTRING, 0, (LPARAM)wide(g_view[which][i].name, w, UI_NAME + 8));
    if (g_nview[which] == 1) SendMessageW(lb, LB_SETCURSEL, 0, 0);
    g_list_shown = which;
}

/* ---------- "How to": what to do next, for the host or the friend ---------- */

static const char *mark(const UiState *s, int bit, int first_open)
{
    /* plain characters on purpose: every Windows font has them */
    if (s->done & (1 << bit)) return "[x] ";          /* done */
    return bit == first_open ? "[>] " : "[  ] ";       /* [>] = you are here */
}

static void build_howto(const UiState *s, char *o, int n)
{
    int open = 0;
    while (open < 4 && (s->done & (1 << open))) open++;
    const char *game = s->versus ? "Versus" : "Splinter Cell";
    const char *create = s->versus ? "create a LAN game" : "Co-op -> LAN -> Create game";
    const char *joinm = s->versus ? "the LAN game list -> join the host" : "Co-op -> LAN -> Join game -> pick the host";
    if (s->mode == MODE_HOST) {
        sfmt(o, n,
            "HOSTING %s - the others join your game\r\n\r\n"
            "%s1. Lobby ready. Friends find you under \"Join a friend\" (or click Copy code and send it).\r\n"
            "%s2. Click \"Start %s\" -> %s.\r\n"
            "%s3. A friend links: they show up in the list above (SCCTLink flashes).\r\n"
            "%s4. They join from their game's LAN list. Have fun!\r\n\r\n"
            "WATCH OUT\r\n"
            "- Keep SCCTLink open the whole time you play.\r\n"
            "- Only people on your Steam friends list can link. Kick removes someone until you click Stop.\r\n"
            "- Game running as administrator? Then run SCCTLink as administrator too (both the same).\r\n"
            "- Same SCCTLink version, same game files and same mods on every PC (see the Mods tab).\r\n"
            "- Co-op: open the Settings tab and click \"Install\" (SCCT Fix) on every PC, or it disconnects.",
            s->versus ? "VERSUS" : "CO-OP",
            mark(s, 0, open), mark(s, 1, open), game, create, mark(s, 2, open), mark(s, 3, open));
    } else if (s->mode == MODE_JOIN) {
        sfmt(o, n,
            "JOINING %s - you join your friend's game\r\n\r\n"
            "%s1. Pick the host and click Join (or paste their code). Not listed? Click Refresh.\r\n"
            "%s2. Wait until the host's game is up (SCCTLink flashes).\r\n"
            "%s3. Click \"Start %s\" -> %s.\r\n"
            "%s4. You're in. Have fun!\r\n\r\n"
            "WATCH OUT\r\n"
            "- Keep SCCTLink open the whole time you play.\r\n"
            "- Game running as administrator? Then run SCCTLink as administrator too (both the same).\r\n"
            "- Same SCCTLink version, same game files and same mods as the host (see the Mods tab).\r\n"
            "- Co-op: open the Settings tab and click \"Install\" (SCCT Fix; the host needs it too), or it disconnects.\r\n"
            "- Can't join? Click Details, then Log -> Copy log, and send it with the host's log.",
            s->versus ? "VERSUS" : "CO-OP",
            mark(s, 0, open), mark(s, 1, open), mark(s, 2, open), game, joinm, mark(s, 3, open));
    } else {
        sfmt(o, n,
            "HOW TO PLAY\r\n\r\n"
            "First pick Co-op or Versus (the button next to \"Join a friend\"). Everyone picks the same.\r\n"
            "One of you hosts (the game runs on their PC), the others join.\r\n\r\n"
            "- HOST: click \"Host a game\".\r\n"
            "- FRIEND: click \"Join a friend\".\r\n\r\n"
            "Everyone needs: Steam running and online, Splinter Cell: Chaos Theory, the same\r\n"
            "SCCTLink version, and to be Steam friends with the host.\r\n\r\n"
            "First time, co-op (everyone, once): start the game once and close it, then open the\r\n"
            "Settings tab and click \"Install\" (SCCT Fix). Start the game once more and close it.\r\n"
            "First time, Versus (everyone, once): start Versus, make a profile and close it, then\r\n"
            "switch to Versus, open the Settings tab and click \"Install\" (Versus Fix).\r\n"
            "Use the same mods on every PC.\r\n\r\n"
            "In the steps, [x] = done and [>] = what to do now.\r\n"
            "Everything SCCTLink does is in the Log (a dot on Log = something needs a look).\r\n"
            "The Mods tab compares your game files and mods with your friend's when you link.");
    }
}

/* The fix of this mode is installed (the banner is green). */
static bool fix_ok(void) { return g_shown.versus ? g_set.vfix == 1 : g_set.scctfix == 1; }
/* The Settings page: the banner (green: the fix is installed, red: not) and the cards with their titles. */
static void draw_set_deco(HDC dc)
{
    bool ok = fix_ok(), vs = g_shown.versus;
    DWORD edge = ok ? (T.dark ? RGBC(36, 86, 58) : RGBC(150, 205, 165)) : (T.dark ? RGBC(90, 46, 48) : RGBC(230, 180, 180));
    rbox(dc, CX, 20, CW, 56, 14, banner_fill(), edge);
    card(dc, SET_L, SET_Y1, SET_CW, SET_H1);
    card(dc, SET_R, SET_Y1, SET_CW, SET_H1);
    label(dc, WTEXT("Display"), SET_L + 16, SET_Y1 + 10, 200, 18, g_bold, T.text, 0);
    label(dc, vs ? WTEXT("Window and mouse") : WTEXT("Game"), SET_R + 16, SET_Y1 + 10, 200, 18, g_bold, T.text, 0);
    if (vs) {
        card(dc, CX, SET_Y2, CW, SET_H2);
        label(dc, WTEXT("Per class"), CX + 16, SET_Y2 + 10, 200, 18, g_bold, T.text, 0);
    } else {
        card(dc, SET_L, SET_Y2, SET_CW, SET_H2);
        card(dc, SET_R, SET_Y2, SET_CW, SET_H2);
        label(dc, WTEXT("Co-op"), SET_L + 16, SET_Y2 + 10, 200, 18, g_bold, T.text, 0);
        label(dc, WTEXT("Mouse"), SET_R + 16, SET_Y2 + 10, 200, 18, g_bold, T.text, 0);
        label(dc, WTEXT("Your friend's game follows the host."), SET_L + 16, SET_Y2 + 62, SET_CW - 32, 16, g_font, T.dim, 0);
        label(dc, WTEXT("The game's own: about 34 FPS."), SET_L + 16, SET_Y2 + 80, SET_CW - 32, 16, g_font, T.dim, 0);
    }
}

/* The Play page: a dot and the status in a card, with the fix's state on the
   right; the list card. */
static void draw_play(HDC dc)
{
    card(dc, CX, 66, CW, 60);
    /* the dot: red while the fix is missing (co-op disconnects without it), green while hosting or joining */
    DWORD dot = g_set.found && !fix_ok() ? red_text() : g_shown.mode == MODE_IDLE ? T.dim : T.status;
    rbox(dc, CX + 18, 66 + 14, 10, 10, 10, dot, dot);
    card(dc, CX, LIST_Y, CW, LIST_H);
    label(dc, WTEXT("v" SCCTLINK_VERSION), CX + CW - 80, 20, 80, 32, g_font, T.dim, DT_VCENTER | 0x2 /* DT_RIGHT */);
}

/* The whole window's background: the sidebar with the logo, then the page. */
static void draw_page(HDC dc)
{
    RECT r;
    GetClientRect(g_wnd, &r);
    FillRect(dc, &r, g_br_bg);
    RECT side = { 0, 0, S_(SIDE_W), r.bottom };
    FillRect(dc, &side, g_br_side);
    RECT line = { S_(SIDE_W) - 1, 0, S_(SIDE_W), r.bottom };
    FillRect(dc, &line, g_br_border);
    draw_logo(dc, 18, 20, 32);
    label(dc, WTEXT("SCCTLink"), 58, 20, 104, 30, g_mid, T.text, DT_VCENTER);
    switch (g_tab) {
    case TAB_PLAY: draw_play(dc); break;
    case TAB_SET: draw_set_deco(dc); break;
    default: { /* Mods, Log, How to: a title and one card */
        const WCHAR *t = g_tab == TAB_MODS ? WTEXT("Game files and mods") : g_tab == TAB_LOG ? WTEXT("Log") : WTEXT("How to play");
        label(dc, t, CX + 2, 20, 300, 30, g_big, T.text, DT_VCENTER);
        card(dc, CX, 62, CW, WIN_H - 62 - 20);
        break;
    }
    }
    draw_frames(dc);
}

static void show_tab(int tab)
{
    g_tab = tab;
    if (tab == TAB_LOG) g_log_unseen = false;
    if (tab == TAB_MODS) g_mods_unseen = false;
    apply_visibility();
    RedrawWindow(g_wnd, 0, 0, 0x1 | 0x4 | 0x80 /* invalidate, erase, children */);
}

static void update_howto(void)
{
    static UiState last;
    static bool first = true;
    if (!first && last.mode == g_shown.mode && last.done == g_shown.done && last.versus == g_shown.versus) return;
    first = false;
    last = g_shown;
    static char t[4096];
    static WCHAR w[4096];
    build_howto(&g_shown, t, sizeof t);
    SetWindowTextW(g_c[ID_HOWTO], wide(t, w, 4096));
}

static void set_num(int id, int v)
{
    char b[16] = "";
    if (v > 0) sfmt(b, sizeof b, "%d", v);
    set_text(id, b);
}
static void set_ws(int id, int v)
{
    char b[16] = "";
    if (v > 0) sfmt(b, sizeof b, "%d.%02d", v / 100, v % 100);
    set_text(id, b);
}

/* new values from the engine: fill the fields only when this PC's values changed
   (so what the player is typing isn't overwritten when a friend links) */
/* g_vf_hud, g_vf_rate: the Versus Fix's two switches, until Apply */
static void vf_switch_texts(void)
{
    RedrawWindow(g_c[ID_SET_VHUD], 0, 0, 0x1 | 0x4);
    RedrawWindow(g_c[ID_SET_VRATE], 0, 0, 0x1 | 0x4);
}
static void show_settings(const UiSettings *n)
{
    UiSettings o = g_set;
    static int shown_versus = -1; /* the fields hold this mode's values */
    bool vs = g_shown.versus;
    bool first = (!o.found && !o.w && !o.fov && !o.fps) || shown_versus != (int)vs;
    shown_versus = vs;
    g_set = *n;
    if (vs) {
        if (first || o.vsens != n->vsens) set_ws(ID_SET_SENS, n->vsens);
        if (first || o.vmult != n->vmult) set_ws(ID_SET_TURN, n->vmult);
        set_text(ID_SF_MOUSE, n->have_vprofile ? "" : "(no profile yet)");
        /* the Versus Fix */
        if (first || o.vf_w != n->vf_w || o.vf_h != n->vf_h) { set_num(ID_SET_W, n->vf_w); set_num(ID_SET_H, n->vf_h); }
        if (first || o.vf_fps != n->vf_fps) { char f[16]; sfmt(f, sizeof f, "%d", n->vf_fps); set_text(ID_SET_VFPS, f); }
        if (first || o.vf_spyfov != n->vf_spyfov) set_num(ID_SET_VFOVS, n->vf_spyfov ? n->vf_spyfov : 75);
        if (first || o.vf_mercfov != n->vf_mercfov) set_num(ID_SET_VFOVM, n->vf_mercfov ? n->vf_mercfov : 95);
        if (first || o.vf_spyturn != n->vf_spyturn) set_num(ID_SET_VTURNS, n->vf_spyturn); /* x100 = percent */
        if (first || o.vf_mercturn != n->vf_mercturn) set_num(ID_SET_VTURNM, n->vf_mercturn);
        if (first || o.vf_hud != n->vf_hud || o.vf_rate125 != n->vf_rate125 || o.vf_full != n->vf_full) { g_vf_hud = n->vf_hud; g_vf_rate = n->vf_rate125; g_vf_full = n->vf_full; }
        vf_switch_texts();
        set_text(ID_SF_RES, "Versus starts at it");
        set_text(ID_SF_VFOV, "game: spy 75, merc 95");
        bool vok = n->vfix >= 0;
        int vids[] = { ID_SET_VFPS, ID_SET_VHUD, ID_SET_VRATE, ID_SET_VFOVS, ID_SET_VFOVM, ID_SET_VTURNS, ID_SET_VTURNM, ID_SET_W, ID_SET_H };
        for (int i = 0; i < (int)(sizeof vids / sizeof *vids); i++) EnableWindow(g_c[vids[i]], vok);
    } else {
        if (first || o.w != n->w || o.h != n->h) { set_num(ID_SET_W, n->w); set_num(ID_SET_H, n->h); }
        if (first || o.sens != n->sens) set_num(ID_SET_SENS, n->sens);
        if (first || o.turn != n->turn) set_ws(ID_SET_TURN, n->turn);
    }
    EnableWindow(g_c[ID_SET_SENS], vs ? n->have_vprofile : n->sens > 0);
    EnableWindow(g_c[ID_SET_TURN], vs ? n->have_vprofile : n->turn > 0);
    if (first || o.fov != n->fov) set_num(ID_SET_FOV, n->fov);
    char b[160];
    const char *who = n->friend_name[0] ? n->friend_name : "friend";
#define FR(id, have, ...) do { if (!n->friend_) b[0] = 0; else if (have) sfmt(b, sizeof b, __VA_ARGS__); \
                               else sfmt(b, sizeof b, "%s: not set", who); set_text(id, b); } while (0)
    if (!vs) FR(ID_SF_RES, n->fw && n->fh, "%s: %dx%d", who, n->fw, n->fh);
    if (!vs && n->scctfix == 1 && !n->friend_) set_text(ID_SF_RES, "the SCCT Fix draws at it");
    if (!vs && n->scctfix == 1 && !n->friend_) set_text(ID_SF_FOV, "75 = the game's");
    FR(ID_SF_FOV, n->ffov, "%s: %d", who, n->ffov);
    if (!vs) FR(ID_SF_MOUSE, n->fturn, "%s: aim turn %d.%02d%s", who, n->fturn / 100, n->fturn % 100, n->fturn != n->turn ? "  <- must match" : "");
    if (vs) set_text(ID_SF_MOUSE, n->have_vprofile ? "" : "(no profile yet)");
#undef FR
    if (!n->have_fov && !n->friend_) set_text(ID_SF_FOV, "(not in the game's files)");
    EnableWindow(g_c[ID_SET_FOV], n->have_fov);
    /* the banner: the fix, installed or not */
    int fixst = vs ? n->vfix : n->scctfix;
    if (vs) {
        set_text(ID_SET_BANNER, n->vfix < 0 ? "Versus wasn't found" : n->vfix == 1 ? "\xe2\x9c\x93 Versus Fix installed"
                                : n->vfix == 3 ? "Versus Fix: an older version is installed" : "Versus Fix not installed");
        set_text(ID_SET_BANSUB, "no 30 FPS limit, mouse fix, FOV per class, flashlight fix, borderless option");
        set_text(ID_SET_FIX, n->vfix == 1 ? "Remove" : n->vfix == 3 ? "Update" : "Install");
    } else {
        set_text(ID_SET_BANNER, !n->found ? "The game's folder wasn't found" : n->scctfix == 1 ? "\xe2\x9c\x93 SCCT Fix installed"
                                : n->scctfix == 3 ? "SCCT Fix: an older version is installed" : "SCCT Fix not installed - co-op needs it");
        set_text(ID_SET_BANSUB, "Co-op sync fix, 60 FPS co-op, borderless, widescreen HUD, raw mouse");
        set_text(ID_SET_FIX, n->scctfix == 1 ? "Remove" : n->scctfix == 3 ? "Update" : "Install");
    }
    static int last_fixst = -9;
    if (fixst != last_fixst) { /* the banner's color, and the fix's state on Play */
        last_fixst = fixst;
        RedrawWindow(g_wnd, 0, 0, 0x1 | 0x4 | 0x80);
    }
    RedrawWindow(g_c[ID_SET_BANNER], 0, 0, 0x1 | 0x4);
    bool fixed = !vs && n->scctfix == 1;
    EnableWindow(g_c[ID_SET_CHUD], n->chud >= 0 && fixed);
    EnableWindow(g_c[ID_SET_INTRO], n->skipintro >= 0);
    EnableWindow(g_c[ID_SET_COOP60], fixed && n->coop60 >= 0);
    RedrawWindow(g_c[ID_SET_COOP60], 0, 0, 0x1 | 0x4);
    set_text(ID_SET_SPFPS, n->spfps == -1 ? "Single player: screen's Hz" : n->spfps == 0 ? "Single player: FPS unlock"
                         : n->spfps > 0 && n->spfps != 60 ? "Single player: custom FPS" : "Single player: 60 FPS");
    EnableWindow(g_c[ID_SET_SPFPS], fixed && n->spfps > -2);
    set_text(ID_SET_SPNOTE, fixed && n->spfps == 0 ? "FPS unlock: the game's own limit is 100" : "");
    RedrawWindow(g_c[ID_SET_CHUD], 0, 0, 0x1 | 0x4);
    RedrawWindow(g_c[ID_SET_INTRO], 0, 0, 0x1 | 0x4);
    RedrawWindow(g_c[ID_SET_MOUSE], 0, 0, 0x1 | 0x4);
    g_set_warn = n->note_warn;
    if (n->note[0]) set_text(ID_SET_NOTE, n->note);
    else if (vs && n->vfix != 1) set_text(ID_SET_NOTE, "Install the Versus Fix to change these (close Versus first).");
    else if (!vs && n->found && n->scctfix != 1) set_text(ID_SET_NOTE, "Co-op needs the SCCT Fix on both PCs. Close the game first.");
    else set_text(ID_SET_NOTE, "Close the game before changing settings.");
    RedrawWindow(g_c[ID_SET_NOTE], 0, 0, 0x1 | 0x4);
}

static int edit_num(int id, bool ws)
{
    WCHAR w[16];
    int n = GetWindowTextW(g_c[id], w, 16), v = 0, f = 0, d = 0;
    int i = 0;
    while (i < n && w[i] == ' ') i++;
    for (; i < n && w[i] >= '0' && w[i] <= '9'; i++) if (v < 100000) v = v * 10 + (w[i] - '0');
    if (!ws) return v;
    if (i < n && (w[i] == '.' || w[i] == ',')) for (i++; i < n && w[i] >= '0' && w[i] <= '9'; i++) if (d < 2) { f = f * 10 + (w[i] - '0'); d++; }
    while (d < 2) { f *= 10; d++; }
    return v * 100 + f;
}

static void apply_versus_clicked(void)
{
    /* the Versus profile's mouse */
    int sens = 0, mult = 0; /* Versus' own sensitivity is set in the game */
    /* the Versus Fix */
    int w = edit_num(ID_SET_W, false), h = edit_num(ID_SET_H, false), fps = edit_num(ID_SET_VFPS, false),
        sf = edit_num(ID_SET_VFOVS, false), mf = edit_num(ID_SET_VFOVM, false),
        st = edit_num(ID_SET_VTURNS, false), mt = edit_num(ID_SET_VTURNM, false); /* percent = x100 */
    if ((w && !h) || (h && !w)) { g_set_warn = true; set_text(ID_SET_NOTE, "Type both the width and the height."); return; }
    if (sens > 65535 || mult > 65535 || w > 8191 || h > 8191 || fps > 2047 || sf > 255 || mf > 255 || st > 65535 || mt > 65535) {
        g_set_warn = true;
        set_text(ID_SET_NOTE, "One of the values is too big.");
        return;
    }
    int osf = g_set.vf_spyfov ? g_set.vf_spyfov : 75, omf = g_set.vf_mercfov ? g_set.vf_mercfov : 95;
    bool vf = g_set.vfix >= 0 && (w != g_set.vf_w || h != g_set.vf_h || fps != g_set.vf_fps || sf != osf || mf != omf ||
                                  g_vf_full != g_set.vf_full || g_vf_rate != g_set.vf_rate125);
    bool vt = g_set.vfix >= 0 && (st != g_set.vf_spyturn || mt != g_set.vf_mercturn);
    g_set_warn = false;
    set_text(ID_SET_NOTE, "Saving...");
    if (sens || mult || (!vf && !vt)) push_cmd(CMD_APPLY_VERSUS, versus_pack(0, 0, sens, mult));
    if (vf) push_cmd(CMD_APPLY_VFIX, vfix_pack(fps, w, h, g_vf_hud, g_vf_rate, sf == 75 && !g_set.vf_spyfov ? 0 : sf,
                                               mf == 95 && !g_set.vf_mercfov ? 0 : mf, g_vf_full));
    if (vt) push_cmd(CMD_APPLY_VFIX2, (u64)(st & 65535) | (u64)(mt & 65535) << 16);
}

static void apply_clicked(void)
{
    if (g_shown.versus) { apply_versus_clicked(); return; }
    int sens = edit_num(ID_SET_SENS, false), turn = edit_num(ID_SET_TURN, true);
    if (sens == g_set.sens || !g_set.sens) sens = 0;
    if (turn == g_set.turn || !g_set.turn) turn = 0;
    if (sens > 65535 || turn > 65535) { g_set_warn = true; set_text(ID_SET_NOTE, "One of the values is too big."); return; }
    int w = edit_num(ID_SET_W, false), h = edit_num(ID_SET_H, false), fov = edit_num(ID_SET_FOV, false),
        fps = 0, ws = 0; /* the FPS limit and another widescreen mod's FOV factor: not set from here */
    /* only what changed (0 = leave it) */
    if (w == g_set.w && h == g_set.h) w = h = 0;
    if (fov == g_set.fov || !g_set.have_fov) fov = 0;
    if (w > 8191 || h > 8191 || fov > 255 || fps > 1023 || ws > 4095) {
        g_set_warn = true;
        set_text(ID_SET_NOTE, "One of the values is too big.");
        return;
    }
    if ((w && !h) || (h && !w)) {
        g_set_warn = true;
        set_text(ID_SET_NOTE, "Type both the width and the height.");
        return;
    }
    g_set_warn = false;
    set_text(ID_SET_NOTE, "Saving...");
    if (w || h || fov || fps || ws || !(sens || turn)) push_cmd(CMD_APPLY_SETTINGS, settings_pack(w, h, fov, fps, ws));
    if (sens || turn) push_cmd(CMD_APPLY_MOUSE, mouse_pack(sens, turn)); /* after: its note is the one shown */
}

static void use_friends(void)
{
    if (!g_set.friend_) return;
    if (g_set.fw && g_set.fh) { set_num(ID_SET_W, g_set.fw); set_num(ID_SET_H, g_set.fh); }
    if (g_set.ffov && g_set.have_fov) set_num(ID_SET_FOV, g_set.ffov);
    if (g_set.fturn && g_set.turn) set_ws(ID_SET_TURN, g_set.fturn); /* the aim turn speed must match; sensitivity is each player's own */
    g_set_warn = false;
    set_text(ID_SET_NOTE, "Your friend's values are in the fields. Click Apply to save them.");
}

/* "Report": a new GitHub issue, filled in, and the log file shown in its
   folder so it can be dragged into the report. */
static void url_add(char *o, size_t n, size_t *k, const char *s)
{
    static const char hx[] = "0123456789ABCDEF";
    for (; *s && *k + 4 < n; s++) {
        unsigned char c = (unsigned char)*s;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_')
            o[(*k)++] = (char)c;
        else { o[(*k)++] = '%'; o[(*k)++] = hx[c >> 4]; o[(*k)++] = hx[c & 15]; }
    }
    o[*k] = 0;
}

static void report(void)
{
    const UiState *s = &g_shown;
    if (MessageBoxW(g_wnd, WTEXT("SCCTLink opens a new problem report on GitHub (you need to be signed in) and\r\n"
                                 "the folder with scctlink.log.\r\n\r\n"
                                 "Drag scctlink.log into the report, and your friend's scctlink.log too if you can.\r\n"
                                 "Without the logs the problem usually can't be found.\r\n\r\n"
                                 "Reports are public. The log never contains IP addresses, but it has your\r\n"
                                 "Steam friends' names."),
                    WTEXT("SCCTLink - Report a problem"), 0x1 /* OK, Cancel */ | 0x40 /* information */) != 1)
        return;
    char body[1500];
    sfmt(body, sizeof body,
         "**What happened?**\n\n\n"
         "**What were you doing?** (hosting or joining, which map, what you did right before)\n\n\n"
         "**Logs:** drag scctlink.log here (from the folder SCCTLink opened), and your friend's too.\n\n"
         "---\nSCCTLink " SCCTLINK_VERSION ", %s, %s, SCCT Fix %s\n",
         s->versus ? "Versus" : "Co-op",
         s->mode == MODE_HOST ? "hosting" : s->mode == MODE_JOIN ? "joining" : "stopped",
         g_set.scctfix == 1 ? "installed" : g_set.scctfix == 2 ? "not installed (another d3d9.dll)" : "not installed");
    static char url[6000];
    size_t k = 0;
    url[0] = 0;
    sfmt(url, sizeof url, "https://github.com/Mhdshehri/scctlink/issues/new?title=");
    k = strlen(url);
    url_add(url, sizeof url, &k, "Problem: ");
    sfmt(url + k, sizeof url - k, "&body=");
    k = strlen(url);
    url_add(url, sizeof url, &k, body);
    open_url(url);
    char log[800];
    sfmt(log, sizeof log, "%s\\scctlink.log", exe_dir());
    show_in_folder(log);
    ui_log("(Report: GitHub opened in your browser. Drag scctlink.log from the folder that opened into the report.)");
}

static void copy_log(void)
{
    HWND h = g_c[ID_LOG];
    int n = (int)SendMessageW(h, WM_GETTEXTLENGTH, 0, 0);
    WCHAR *w = malloc(((size_t)n + 1) * 2);
    char *u = malloc((size_t)n * 3 + 1);
    if (w && u) {
        GetWindowTextW(h, w, n + 1);
        WideCharToMultiByte(CP_UTF8, 0, w, -1, u, n * 3 + 1, 0, 0);
        clipboard_set(u);
    }
    free(w);
    free(u);
}

static void refresh_controls(void)
{
    const UiState *s = &g_shown;
    static int last_versus = -1, last_mode = -1;
    if (last_versus != (int)s->versus || last_mode != s->mode) { /* the page changes: Co-op <-> Versus, hosting, joining */
        bool vs_changed = last_versus != (int)s->versus;
        last_versus = s->versus;
        last_mode = s->mode;
        if (vs_changed) show_settings(&g_set);
        show_tab(g_tab);
    }
    bool idle = s->mode == MODE_IDLE, host = s->mode == MODE_HOST, join = s->mode == MODE_JOIN;
    EnableWindow(g_c[ID_HOST], idle && !g_closing);
    EnableWindow(g_c[ID_JOIN], !g_closing);
    EnableWindow(g_c[ID_STOP], !idle && !g_closing);
    { /* the start button: what Host | Join picks, or Stop while running */
        static int last = -1;
        int want = idle ? g_role : 2 + (host ? 0 : 1);
        if (want != last) {
            last = want;
            set_text(ID_JOIN, !idle ? "Stop" : g_role ? "Join" : "Host");
            RedrawWindow(g_c[ID_HOST], 0, 0, 0x1 | 0x4);
            RedrawWindow(g_c[ID_JOIN], 0, 0, 0x1 | 0x4);
        }
    }
    EnableWindow(g_c[ID_PLAYFIX], g_set.found && !fix_ok() && !g_closing && (!s->versus || g_set.vfix >= 0));
    EnableWindow(g_c[ID_SET_APPLY], g_set.found && !g_closing);
    EnableWindow(g_c[ID_SET_FRIEND], g_set.friend_ && !g_closing);
    EnableWindow(g_c[ID_SET_FIX], g_set.found && !g_closing && (!s->versus || g_set.vfix >= 0));
    EnableWindow(g_c[ID_SET_RESET], g_set.found && !g_closing);
    EnableWindow(g_c[ID_SET_MOUSE], g_set.found && g_set.rawmouse >= 0 && g_set.scctfix == 1 && !g_closing);
    static int ro = -1;
    int want_ro = host || (join && s->lobby);
    if (want_ro != ro) { SendMessageW(g_c[ID_LOBBY], EM_SETREADONLY, (WPARAM)want_ro, 0); ro = want_ro; }
    EnableWindow(g_c[ID_COPYID], host && s->lobby);
    EnableWindow(g_c[ID_JOINID], (idle || join) && !s->lobby);
    int which = host ? LIST_FRIENDS_ONLINE : LIST_HOSTING;
    if (which != g_list_shown) show_list(which);
    set_text_once(ID_LISTLABEL, host ? "Linked to your game" : "Friends hosting");
    /* the same button is "Join" for the friend and "Kick" for the host */
    set_text_once(ID_ACTION, host ? "Kick" : "Join");
    EnableWindow(g_c[ID_ACTION], !g_closing && ((join && !s->lobby) || (host && g_nview[LIST_FRIENDS_ONLINE] > 0)));
    { /* Join / Kick at the card's right edge when Refresh isn't there (hosting) */
        static int ax = -1;
        int x = host ? CX + CW - 16 - 92 : CX + CW - 16 - 196;
        if (x != ax) { move_ctl(ID_ACTION, x, LIST_Y + 10); ax = x; }
    }
    apply_visibility();
    EnableWindow(g_c[ID_REFRESH], !g_closing && (idle || (join && !s->lobby)));
    EnableWindow(g_c[ID_LAUNCH], !g_closing);
    { /* Co-op | Versus: drawn from g_shown; redraw when it can or can't change */
        static int gm = -1;
        int want = (idle && !g_closing) | (s->versus ? 2 : 0);
        if (want != gm) { gm = want; EnableWindow(g_c[ID_GAMEMODE], idle && !g_closing); RedrawWindow(g_c[ID_GAMEMODE], 0, 0, 0x1 | 0x4); }
    }
    set_text_once(ID_LAUNCH, s->versus ? "Start Versus" : "Start Splinter Cell");
    EnableWindow(g_c[ID_COPYCONN], !idle);
}

/* ---------- the live stats window ----------
   A small window that stays on top of the game (the SCCT Fix runs it
   borderless, so it shows over it): ping, packet loss and packets per second
   for each linked friend, and the game's frame rate (even playing alone).
   Drag it anywhere; right-click or "Live stats" closes it. */
static HWND g_stats;
static char g_stats_text[400];
#define WM_NCHITTEST 0x84
#define WM_NCRBUTTONUP 0xA5

static int stats_lines(void)
{
    int n = 1;
    for (const char *c = g_stats_text; *c; c++) n += *c == '\n';
    return g_stats_text[0] ? n : 1;
}

static void stats_layout(void)
{
    if (g_stats) SetWindowPos(g_stats, 0, 0, 0, S_(420), S_(8 + 20 * stats_lines()), 0x2 | 0x4 | 0x10 /* no move, no z-order, no activate */);
}

/* The live stats float over the game with no background: the window's key
   color is see-through (WS_EX_LAYERED + LWA_COLORKEY), and each line is drawn
   with a dark outline so it reads on any scene. A font without smoothing,
   since smoothed edges would blend with the key color. */
#define STATS_KEY RGBC(255, 0, 255)
static HFONT g_stats_font;

static void outlined(HDC dc, const WCHAR *t, RECT r, DWORD color)
{
    SetTextColor(dc, RGBC(0, 0, 0)); /* a 2-pixel black edge all round */
    int k = S_(2);
    for (int dx = -k; dx <= k; dx++)
        for (int dy = -k; dy <= k; dy++) {
            if (!dx && !dy) continue;
            RECT o = { r.left + dx, r.top + dy, r.right + dx, r.bottom + dy };
            DrawTextW(dc, t, -1, &o, DT_SINGLELINE | DT_END_ELLIPSIS);
        }
    SetTextColor(dc, color);
    DrawTextW(dc, t, -1, &r, DT_SINGLELINE | DT_END_ELLIPSIS);
}

static LRESULT WINAPI stats_proc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_ERASEBKGND: {
        HDC dc = (HDC)wp;
        RECT r;
        GetClientRect(w, &r);
        HBRUSH key = CreateSolidBrush(STATS_KEY);
        FillRect(dc, &r, key);
        DeleteObject(key);
        SetBkMode(dc, TRANSPARENT_BK);
        HANDLE of = SelectObject(dc, g_stats_font);
        static WCHAR wl[200];
        const char *l = g_stats_text[0] ? g_stats_text : "Starting...";
        for (int i = 0; *l; i++) {
            const char *e = strchr(l, '\n');
            int n = e ? (int)(e - l) : (int)strlen(l);
            char line[200];
            if (n > 199) n = 199;
            memcpy(line, l, (size_t)n);
            line[n] = 0;
            RECT lr = { S_(5), S_(5 + 20 * i), r.right - S_(5), S_(25 + 20 * i) };
            outlined(dc, wide(line, wl, 200), lr, RGBC(245, 245, 245));
            l = e ? e + 1 : l + n;
        }
        SelectObject(dc, of);
        return 1;
    }
    case WM_NCHITTEST: return 2;           /* HTCAPTION: drag it from anywhere */
    case WM_NCRBUTTONUP: DestroyWindow(w); return 0;
    case WM_DESTROY:
        g_stats = 0;
        push_cmd(CMD_STATS, 0);
        set_text(ID_STATS, "Live stats");
        return 0;
    default:
        return DefWindowProcW(w, m, wp, lp);
    }
}

static void stats_toggle(void)
{
    if (g_stats) { DestroyWindow(g_stats); return; } /* WM_DESTROY tells the engine */
    if (!g_stats_font)
        g_stats_font = CreateFontW(-S_(15), 0, 0, 0, FW_BOLD, 0, 0, 0, 1, 0, 0, 3 /* NONANTIALIASED_QUALITY */, 0, WTEXT("Segoe UI"));
    g_stats = CreateWindowExW(0x8 | 0x80 | 0x08000000 | 0x80000 /* topmost, tool window, no focus, layered */,
                              WTEXT("SCCTLinkStats"), WTEXT("SCCTLink live stats"), 0x80000000 /* popup, no border */,
                              S_(16), S_(16), S_(420), S_(28), 0, 0, GetModuleHandleW(0), 0);
    if (!g_stats) return;
    SetLayeredWindowAttributes(g_stats, STATS_KEY, 255, 0x1 /* LWA_COLORKEY: the background is see-through */);
    push_cmd(CMD_STATS, 1); /* the connection's numbers */
    stats_layout();
    ShowWindow(g_stats, 4 /* SW_SHOWNOACTIVATE */);
    set_text(ID_STATS, "Hide live stats");
}

static void stats_update(const char *text)
{
    if (!strcmp(text, g_stats_text)) return;
    int before = stats_lines();
    sfmt(g_stats_text, sizeof g_stats_text, "%s", text);
    if (!g_stats) return;
    if (stats_lines() != before) stats_layout();
    RedrawWindow(g_stats, 0, 0, 0x1 | 0x4);
}

static void on_timer(void)
{
    static char pend[65536];
    int n;
    UiState st;
    bool st_dirty, ld[2];
    EnterCriticalSection(&g_cs);
    n = g_npend;
    memcpy(pend, g_pend, (size_t)n);
    g_npend = 0;
    bool alert = g_alert, warned = g_warned;
    g_alert = g_warned = false;
    static char mods[16384];
    bool mods_dirty = g_mods_dirty, mods_warn = g_mods_warn;
    if (mods_dirty) memcpy(mods, g_mods_text, sizeof mods);
    g_mods_dirty = false;
    static UiSettings set;
    bool set_dirty = g_set_dirty;
    if (set_dirty) set = g_set_in;
    g_set_dirty = false;
    st = g_state;
    st_dirty = g_state_dirty;
    g_state_dirty = false;
    for (int k = 0; k < 2; k++) {
        ld[k] = g_list_dirty[k];
        if (ld[k]) {
            g_nview[k] = g_nlist[k];
            for (int i = 0; i < g_nlist[k]; i++) g_view[k][i] = g_lists[k][i];
            g_list_dirty[k] = false;
        }
    }
    LeaveCriticalSection(&g_cs);

    if (n) { pend[n] = 0; append_log(pend); }
    if (mods_dirty) {
        /* your block (up to the first divider) on the left, everyone else's on the right */
        HWND lb = g_c[ID_MODS];
        for (int k = 0; k < 2; k++) {
            SendMessageW(g_c[k ? ID_MODS2 : ID_MODS], 0x000B /* WM_SETREDRAW */, 0, 0);
            SendMessageW(g_c[k ? ID_MODS2 : ID_MODS], LB_RESETCONTENT, 0, 0);
        }
        SendMessageW(g_c[ID_MODSTOP], LB_RESETCONTENT, 0, 0);
        for (const char *l = mods; *l;) {
            const char *e = strchr(l, '\n');
            int len = e ? (int)(e - l) : (int)strlen(l);
            char line[300];
            if (len > 299) len = 299;
            memcpy(line, l, (size_t)len);
            line[len] = 0;
            if (len && line[len - 1] == '\r') line[len - 1] = 0;
            if (line[0] == '-' && lb == g_c[ID_MODS]) line[0] = 0, lb = g_c[ID_MODS2]; /* the column break */
            if (line[0] == '=') { /* the top line */
                static WCHAR w[320];
                SendMessageW(g_c[ID_MODSTOP], LB_ADDSTRING, 0, (LPARAM)wide(line + 1, w, 320));
                l = e ? e + 1 : l + len;
                continue;
            }
            if (line[0] == '!' || line[0] == ' ') /* Windows paths, and the path ellipsis keeps the file name */
                for (char *c = line; *c; c++) if (*c == '/') *c = '\\';
            if (line[0]) {
                static WCHAR w[320];
                SendMessageW(lb, LB_ADDSTRING, 0, (LPARAM)wide(line, w, 320));
            }
            l = e ? e + 1 : l + len;
        }
        for (int k = 0; k < 2; k++) {
            SendMessageW(g_c[k ? ID_MODS2 : ID_MODS], 0x000B /* WM_SETREDRAW */, 1, 0);
            RedrawWindow(g_c[k ? ID_MODS2 : ID_MODS], 0, 0, 0x1 | 0x4);
        }
        RedrawWindow(g_c[ID_MODSTOP], 0, 0, 0x1 | 0x4);
        bool dot = mods_warn && g_tab != TAB_MODS;
        if (dot != g_mods_unseen) { g_mods_unseen = dot; RedrawWindow(g_c[ID_TAB_MODS], 0, 0, 0x1 | 0x4); }
    }
    if (set_dirty) show_settings(&set);
    if (warned && g_tab != TAB_LOG && !g_log_unseen) { /* red dot on "Log" */
        g_log_unseen = true;
        RedrawWindow(g_c[ID_TAB_LOG], 0, 0, 0x1 | 0x4);
    }
    if (alert && GetForegroundWindow() != g_wnd) { /* only when SCCTLink isn't the window in front */
        FLASHWINFO f = { sizeof f, g_wnd, 0x3 | 0xC /* caption + taskbar, until it's clicked */, 0, 0 };
        FlashWindowEx(&f);
        MessageBeep(0x40); /* Windows' "Asterisk" sound */
    }
    if (st_dirty) {
        bool lobby_changed = st.lobby != g_shown.lobby || st.mode != g_shown.mode;
        g_shown = st;
        { /* "Ready (Co-op). Host a game, or join a friend." -> the state, and what to do under it */
            char a[sizeof st.status], *dot = 0;
            sfmt(a, sizeof a, "%s", st.status);
            for (char *c = a; *c && !dot; c++) if (c[0] == '.' && c[1] == ' ') dot = c;
            if (dot) { *dot = 0; set_text(ID_STATUS, a); set_text(ID_STATUS2, dot + 2); }
            else { set_text(ID_STATUS, a); set_text(ID_STATUS2, ""); }
        }
        stats_update(st.stats);
        if (lobby_changed) {
            char id[24] = "";
            if (st.lobby) lobby_code(st.lobby, id);
            if (st.lobby || st.mode != MODE_JOIN) set_text(ID_LOBBY, id);
        }
    }
    for (int k = 0; k < 2; k++)
        if (ld[k] && g_list_shown == k) show_list(k);
    refresh_controls();
    update_howto();

    if (g_closing && (WaitForSingleObject(g_thread, 0) == 0 || now_ms() - g_close_ms > 5000))
        DestroyWindow(g_wnd);
}

static void copy_text(const char *t) { clipboard_set(t); }

static void on_command(int id, int code)
{
    switch (id) {
    case ID_HOST: /* Host | Join: pick (only while stopped) */
        if (g_shown.mode == MODE_IDLE) { g_role = !g_role; RedrawWindow(g_c[ID_HOST], 0, 0, 0x1 | 0x4); }
        break;
    case ID_JOIN: /* the start button */
        if (g_shown.mode != MODE_IDLE) push_cmd(CMD_STOP, 0);
        else push_cmd(g_role ? CMD_JOIN : CMD_HOST, 0);
        break;
    case ID_PLAYFIX: /* Install (or Update) the fix, like Settings' button */
        if (fix_ok()) break;
        g_set_warn = false;
        set_text(ID_SET_NOTE, "Working...");
        if (g_shown.versus) push_cmd(CMD_VFIX, 1);
        else push_cmd(CMD_SCCTFIX, 1);
        break;
    case ID_STOP: push_cmd(CMD_STOP, 0); break;
    case ID_COPYID:
        if (g_shown.lobby) {
            char code[16];
            lobby_code(g_shown.lobby, code);
            copy_text(code);
            ui_log("Lobby code copied. Send it to your friend (Steam chat).");
        }
        break;
    case ID_JOINID: {
        WCHAR w[64];
        char a[64];
        GetWindowTextW(g_c[ID_LOBBY], w, 64);
        WideCharToMultiByte(CP_UTF8, 0, w, -1, a, 64, 0, 0);
        u64 lob = lobby_parse(a);
        if (!lob) {
            MessageBoxW(g_wnd, WTEXT("Paste the lobby code your friend sent you (like 1PCW-4HB) into the Lobby code box first."),
                        WTEXT("SCCTLink"), MB_ICONWARNING);
            break;
        }
        push_cmd(CMD_JOIN_LOBBY, lob);
        break;
    }
    case ID_LIST:
        if (code != LBN_DBLCLK) break;
        /* fall through: double-click = button */
    case ID_ACTION: {
        if (g_shown.mode == MODE_HOST) { /* host: kick the picked friend */
            if (code == LBN_DBLCLK) break; /* never kick by double-click */
            int sel = (int)SendMessageW(g_c[ID_LIST], LB_GETCURSEL, 0, 0);
            if (sel < 0 || sel >= g_nview[LIST_FRIENDS_ONLINE]) {
                MessageBoxW(g_wnd, WTEXT("Pick a friend in the list first."), WTEXT("SCCTLink"), MB_ICONWARNING);
                break;
            }
            UiPerson *p = &g_view[LIST_FRIENDS_ONLINE][sel];
            char name[UI_NAME], q[256];
            sfmt(name, sizeof name, "%s", p->name);
            char *cut = strstr(name, "  ("); /* drop "  (linked)" */
            if (cut) *cut = 0;
            sfmt(q, sizeof q, "Kick %s?\n\nThey are cut off and can't link to your game again until you click Stop.", name);
            static WCHAR wq[256];
            if (MessageBoxW(g_wnd, wide(q, wq, 256), WTEXT("SCCTLink"), MB_YESNO | MB_ICONQUESTION) == IDYES)
                push_cmd(CMD_KICK, p->id);
            break;
        }
        int which = LIST_HOSTING;
        int sel = (int)SendMessageW(g_c[ID_LIST], LB_GETCURSEL, 0, 0);
        if (sel < 0 || sel >= g_nview[which]) {
            MessageBoxW(g_wnd, WTEXT("Pick a friend in the list first."), WTEXT("SCCTLink"), MB_ICONWARNING);
            break;
        }
        push_cmd(CMD_JOIN_LOBBY, g_view[which][sel].lobby);
        break;
    }
    case 1: /* Enter key: join the pasted lobby id, if there is one */
        if (g_shown.mode != MODE_HOST && !g_shown.lobby && SendMessageW(g_c[ID_LOBBY], WM_GETTEXTLENGTH, 0, 0) > 0)
            on_command(ID_JOINID, 0);
        break;
    case ID_LAUNCH: push_cmd(CMD_LAUNCH, 0); break;
    case ID_REFRESH: push_cmd(CMD_REFRESH, 0); break;
    case ID_NAV_PLAY: show_tab(TAB_PLAY); break;
    case ID_TAB_HOWTO: show_tab(TAB_HOWTO); break;
    case ID_TAB_LOG: show_tab(TAB_LOG); break;
    case ID_TAB_MODS: show_tab(TAB_MODS); break;
    case ID_TAB_SET: show_tab(TAB_SET); break;
    case ID_SET_APPLY: apply_clicked(); break;
    case ID_SET_FRIEND: use_friends(); break;
    case ID_STATS: stats_toggle(); break;
    case ID_SET_FIX: /* co-op: the SCCT Fix; Versus: the Versus Fix */
        g_set_warn = false;
        set_text(ID_SET_NOTE, "Working...");
        if (g_shown.versus) push_cmd(CMD_VFIX, g_set.vfix == 1 ? 0 : 1);
        else push_cmd(CMD_SCCTFIX, g_set.scctfix == 1 ? 0 : 1);
        break;
    case ID_SET_INTRO:
        g_set_warn = false;
        set_text(ID_SET_NOTE, "Working...");
        push_cmd(CMD_SKIPINTRO, g_set.skipintro == 1 ? 0 : 1);
        break;
    case ID_SET_COOP60:
        g_set_warn = false;
        set_text(ID_SET_NOTE, "Working...");
        push_cmd(CMD_COOP60, g_set.coop60 == 1 ? 0 : 1);
        break;
    case ID_SET_SPFPS: { /* 60 | Screen | Unlock: the part under the mouse (keyboard: the next one) */
        POINT pt;
        RECT r;
        int part = -1;
        GetClientRect(g_c[ID_SET_SPFPS], &r);
        if (GetCursorPos(&pt) && ScreenToClient(g_c[ID_SET_SPFPS], &pt) && pt.y >= 0 && pt.y < r.bottom) {
            int x = r.right - S_(SPFPS_SEG);
            if (pt.x >= x && pt.x < r.right) part = (int)((pt.x - x) * 3 / (r.right - x));
        }
        if (part < 0) part = g_set.spfps == 60 ? 1 : g_set.spfps == -1 ? 2 : 0;
        int want = part == 0 ? 60 : part == 1 ? -1 : 0;
        if (want == g_set.spfps) break;
        g_set_warn = false;
        set_text(ID_SET_NOTE, "Working...");
        push_cmd(CMD_SPFPS, want == 60 ? 60 : want == -1 ? 0xffff : 0);
        break;
    }
    case ID_SET_CHUD:
        g_set_warn = false;
        set_text(ID_SET_NOTE, "Working...");
        push_cmd(CMD_CHUD, g_set.chud == 1 ? 0 : 1);
        break;
    case ID_SET_VHUD: g_vf_full = !g_vf_full; vf_switch_texts(); set_text(ID_SET_NOTE, "Click Apply to save."); break;
    case ID_SET_VRATE: g_vf_rate = !g_vf_rate; vf_switch_texts(); set_text(ID_SET_NOTE, "Click Apply to save."); break;
    case ID_SET_RESET:
        if (MessageBoxW(g_wnd, g_shown.versus ? WTEXT("Put Versus back to the game's own settings?\n\nMouse sensitivity 10, multiplier 6; the Versus Fix: 60 FPS, mouse 125 Hz, HUD fix on, the game's FOV and turning, the game's resolution list.")
                                              : WTEXT("Put co-op's settings back to the game's own?\n\nFOV 75, mouse sensitivity 100, aim turn speed 0.50.\nThe resolution and the SCCT Fix's switches are kept."),
                        WTEXT("SCCTLink - Reset"), 0x1 | 0x20 /* OK, Cancel, question */) != 1)
            break;
        g_set_warn = false;
        set_text(ID_SET_NOTE, "Working...");
        push_cmd(CMD_RESET_SETTINGS, g_shown.versus ? 1 : 0);
        break;
    case ID_SET_MOUSE: /* UseDIMouse: the game reads the mouse itself, no acceleration */
        g_set_warn = false;
        set_text(ID_SET_NOTE, "Working...");
        push_cmd(CMD_RAWMOUSE, g_set.rawmouse == 1 ? 0 : 1);
        break;
    case ID_RECHECK: push_cmd(CMD_RECHECK, 0); break;
    case ID_REPORT: report(); break;
    case ID_COPYLOG:
        copy_log();
        ui_log("(Log copied. Paste it with Ctrl+V.)");
        break;
    case ID_THEME:
        set_theme(!T.dark);
        save_theme();
        break;
    case ID_COPYCONN: /* "Details": ping, packet loss, ports - shown in the Log */
        push_cmd(CMD_DETAILS, 0);
        show_tab(TAB_LOG);
        break;
    case ID_GAMEMODE: push_cmd(CMD_SET_VERSUS, g_shown.versus ? 0 : 1); break;
    default: break;
    }
}

static LRESULT WINAPI wndproc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_ERASEBKGND:
        draw_page((HDC)wp);
        return 1;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        return control_colors(m, (HDC)wp, (HWND)lp);
    case WM_MEASUREITEM: {
        MeasureItem *mi = (MeasureItem *)lp;
        if (mi->type == ODT_LISTBOX) mi->height = (UINT)S_(mi->id == ID_MODS || mi->id == ID_MODS2 || mi->id == ID_MODSTOP ? 19 : 24);
        return 1;
    }
    case WM_DRAWITEM: {
        DrawItem *d = (DrawItem *)lp;
        if (d->type == ODT_LISTBOX && (d->id == ID_MODS || d->id == ID_MODS2 || d->id == ID_MODSTOP)) draw_mods_item(d);
        else if (d->type == ODT_LISTBOX) draw_list_item(d);
        else draw_button(d);
        return 1;
    }
    case WM_COMMAND:
        on_command((int)(wp & 0xFFFF), (int)((wp >> 16) & 0xFFFF));
        return 0;
    case WM_TIMER:
        on_timer();
        return 0;
    case WM_CLOSE:
        if (!g_closing) {
            g_closing = true;
            g_close_ms = now_ms();
            push_cmd(CMD_QUIT, 0); /* the engine leaves the lobby and says goodbye */
            ui_log("Closing...");
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(w, m, wp, lp);
    }
}

static DWORD WINAPI engine_thread(void *arg)
{
    (void)arg;
    EngineOpts o;
    memset(&o, 0, sizeof o);
    Cmd none = { CMD_NONE, 0 };
    engine_run(&o, none);
    return 0;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    plat_init();
    InitCommonControls();
    InitializeCriticalSection(&g_cs);

    HDC dc = GetDC(0);
    g_dpi = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(0, dc);
    if (g_dpi < 96) g_dpi = 96;
    { /* a small screen (a laptop at 150 %, 1366x768): draw a little smaller so the whole window fits */
        int fit = GetSystemMetrics(62 /* SM_CYMAXIMIZED: the screen's height for a window */) - 16;
        int need = WIN_H + 32; /* the window at 96 dpi, with its title bar */
        if (fit > 300 && need * g_dpi / 96 > fit) g_dpi = fit * 96 / need;
        if (g_dpi < 72) g_dpi = 72;
    }
    const WCHAR *face = WTEXT("Segoe UI");
    g_font = CreateFontW(-S_(13), 0, 0, 0, FW_NORMAL, 0, 0, 0, 1, 0, 0, CLEARTYPE_QUALITY, 0, face);
    g_bold = CreateFontW(-S_(13), 0, 0, 0, 600 /* semibold */, 0, 0, 0, 1, 0, 0, CLEARTYPE_QUALITY, 0, face);
    g_mid = CreateFontW(-S_(15), 0, 0, 0, 600, 0, 0, 0, 1, 0, 0, CLEARTYPE_QUALITY, 0, face);
    g_big = CreateFontW(-S_(17), 0, 0, 0, 600, 0, 0, 0, 1, 0, 0, CLEARTYPE_QUALITY, 0, face);
    g_icon = CreateFontW(-S_(15), 0, 0, 0, FW_NORMAL, 0, 0, 0, 1, 0, 0, CLEARTYPE_QUALITY, 0, WTEXT("Segoe MDL2 Assets"));
    g_icon_big = CreateFontW(-S_(20), 0, 0, 0, FW_NORMAL, 0, 0, 0, 1, 0, 0, CLEARTYPE_QUALITY, 0, WTEXT("Segoe MDL2 Assets"));

    HINSTANCE inst = GetModuleHandleW(0);
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.proc = wndproc;
    wc.inst = inst;
    wc.icon = LoadIconW(inst, (const WCHAR *)(uintptr_t)1);
    wc.iconSm = wc.icon;
    wc.cursor = LoadCursorW(0, (const WCHAR *)(uintptr_t)32512);
    set_theme(!saved_light()); /* dark unless the player picked light before */
    wc.background = 0;         /* painted in WM_ERASEBKGND with the theme color */
    wc.className = WTEXT("SCCTLinkWindow");
    RegisterClassExW(&wc);
    wc.proc = stats_proc;
    wc.className = WTEXT("SCCTLinkStats");
    RegisterClassExW(&wc);
    wc.proc = wndproc;
    wc.className = WTEXT("SCCTLinkWindow");

    DWORD style = WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT r = { 0, 0, S_(WIN_W), S_(WIN_H) };
    AdjustWindowRectEx(&r, style, 0, 0);
    int ww = r.right - r.left, wh = r.bottom - r.top;
    int x = (GetSystemMetrics(0) - ww) / 2, y = (GetSystemMetrics(1) - wh) / 2;
    g_wnd = CreateWindowExW(0, wc.className, WTEXT("SCCTLink " SCCTLINK_VERSION " - Splinter Cell: Chaos Theory"), style, x < 0 ? 0 : x,
                            y < 0 ? 0 : y, ww, wh, 0, 0, inst, 0);
    if (!g_wnd) return 1;
    build();
    set_theme(T.dark); /* button label, title bar, scrollbars */
    show_tab(TAB_PLAY); /* start on "Play" */
    on_timer();
    ShowWindow(g_wnd, 1);
    UpdateWindow(g_wnd);
    SetTimer(g_wnd, 1, 100, 0);

    g_thread = CreateThread(0, 0, engine_thread, 0, 0, 0);
    if (!g_thread) {
        MessageBoxW(g_wnd, WTEXT("Could not start."), WTEXT("SCCTLink"), MB_ICONWARNING);
        return 1;
    }

    MSG msg;
    while (GetMessageW(&msg, 0, 0, 0) > 0) {
        if (IsDialogMessageW(g_wnd, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
