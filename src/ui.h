/* ui.h - what the SCCTLink engine (scctlink.c) needs from a front end.
 * Two front ends exist: ui_win.c (the Windows window) and ui_console.c
 * (a text version, used by the automated tests). The engine runs on its own
 * thread in the window version; every function here must be thread-safe there.
 * Same design as CoFLink's window. */
#pragma once
#include "mini.h"

#define SCCTLINK_VERSION "1.0"

typedef enum {
    CMD_NONE,
    CMD_HOST,        /* start hosting */
    CMD_JOIN,        /* start looking for a host */
    CMD_JOIN_LOBBY,  /* arg = lobby id */
    CMD_LAUNCH,      /* start Splinter Cell (co-op) or Versus */
    CMD_REFRESH,     /* look for a hosting friend right now */
    CMD_STATUS,      /* print a short status line */
    CMD_DETAILS,     /* print ports, ping, packet loss */
    CMD_SET_VERSUS,  /* arg = 1: Versus (Spies vs. Mercenaries), 0: co-op. Only when stopped. */
    CMD_KICK,        /* host: arg = friend's Steam id */
    CMD_RECHECK,     /* list the game files again and compare with the friends again */
    CMD_STATS,       /* arg 1: the live stats window is open (measure the game's frame rate), 0: closed */
    CMD_SCCTFIX,      /* arg 1: put the SCCT Fix (d3d9.dll + StartupFullscreen=False) into the game's System folder, 0: take it out */
    CMD_RAWMOUSE,     /* arg 1: UseDIMouse=True in SplinterCell3.ini (the game reads the mouse itself, no acceleration), 0: False */
    CMD_SKIPINTRO,    /* arg 1: no startup videos (SplinterCell3.ini [GENERAL]), 0: the game's own */
    CMD_CHUD,         /* arg 1/0: the SCCT Fix's HUD fix (co-op; System\\SCCTFix.ini) */
    CMD_SPFPS,        /* arg: the SCCT Fix's single-player FPS limit: 60, 0xffff (the screen's), 0 (none); co-op stays 60 */
    CMD_VFIX,         /* arg 1: install the Versus Fix (Versus\System\d3d8.dll), 0: remove it */
    CMD_APPLY_VFIX,   /* arg = vfix_pack(): the Versus Fix's settings (SCCTVersusFix.ini) */
    CMD_APPLY_VFIX2,  /* arg = spy turn x100 | merc turn x100 << 16 */
    CMD_APPLY_MOUSE,  /* arg = mouse_pack(sensitivity, aim turn speed x100): co-op's mouse settings */
    CMD_APPLY_VERSUS, /* arg = versus_pack(w, h, sensitivity x100, mouse multiplier x100): Versus' settings */
    CMD_RESET_SETTINGS, /* arg 0: co-op's settings back to the game's defaults, 1: Versus' */
    CMD_APPLY_SETTINGS, /* arg = settings_pack(): write resolution / FOV / FPS limit into the game's .ini files */
    CMD_STOP,        /* stop hosting/joining, back to the start screen */
    CMD_QUIT,
} CmdKind;

typedef struct { CmdKind kind; u64 arg; } Cmd;

enum { MODE_IDLE = 0, MODE_HOST = 1, MODE_JOIN = 2 };

typedef struct {
    int mode;
    bool versus;          /* Versus instead of co-op */
    u64 lobby;            /* host: own lobby; join: lobby joined */
    uint8_t done;         /* "How to" steps finished, one bit each (see make_status) */
    char status[160];     /* one-line summary for the big status label */
    char stats[400];      /* the live stats window: the game's frame rate, then one line per linked friend */
} UiState;

#define UI_NAME 72
typedef struct { u64 id, lobby; char name[UI_NAME]; } UiPerson;

enum { LIST_FRIENDS_ONLINE = 0 /* host: friends linked now */, LIST_HOSTING = 1 };

void ui_log(const char *line); /* lines starting with "!" are warnings */
/* something the player is waiting for happened (a friend linked, the host's
   game is up): the window flashes in the taskbar and plays a short sound */
void ui_alert(const char *what);
void ui_state(const UiState *s);
/* The "Mods" tab: the game files check as a checklist. warn: something differs
   (a dot on the tab). */
void ui_mods(const char *text, bool warn);

/* The "Settings" tab: this PC's values (0 = not found) and the linked friend's. */
typedef struct {
    bool found;                 /* the game's folder was found */
    int w, h, fov, fps, wsfov;  /* wsfov: the widescreen fix's FOV factor x100 */
    bool have_fov, have_ws;     /* there is a FOV setting / the widescreen fix to write to */
    bool friend_;               /* a friend's values are known */
    int fw, fh, ffov, ffps, fwsfov;
    char friend_name[64];
    int sens, turn, fsens, fturn; /* co-op: mouse sensitivity, aim turn speed (m_turnMul) x100; f*: the friend's */
    int vw, vh, vsens, vmult;   /* Versus: resolution, mouse sensitivity x100, mouse multiplier x100 (0 = not found) */
    bool have_vprofile;         /* a Versus profile was found (its mouse settings can be changed) */
    int skipintro;              /* co-op startup videos skipped: 1, 0, -1 no game folder */
    int chud;                   /* co-op HUD fix: 1 on, 0 off, -1 no game folder */
    int spfps;                  /* single-player FPS limit (SCCT Fix): 60, -1 the screen's, 0 none; -2 no game folder */
    int vfix;                   /* the Versus Fix: 0 not there, 1 installed, 2 another d3d8.dll, -1 no Versus */
    int vf_fps, vf_w, vf_h, vf_spyfov, vf_mercfov, vf_spyturn, vf_mercturn; /* 0 = the game's own; turns x100 */
    bool vf_hud, vf_rate125, vf_full;
    int rawmouse, frawmouse;    /* UseDIMouse: 1 on, 0 off, -1 not found (frawmouse: the friend's) */
    int scctfix;                 /* the SCCT Fix: 0 not there, 1 installed, 2 another d3d9.dll is there, -1 no game folder */
    char note[200];             /* what the last Apply did */
    bool note_warn;
} UiSettings;
void ui_settings(const UiSettings *s);
/* width 13 bits, height 13, FOV 8, FPS 10, widescreen FOV factor x100 12; 0 = leave as it is */
static inline u64 settings_pack(int w, int h, int fov, int fps, int ws)
{
    return (u64)(w & 8191) | (u64)(h & 8191) << 13 | (u64)(fov & 255) << 26 | (u64)(fps & 1023) << 34 | (u64)(ws & 4095) << 44;
}
/* Versus Fix: FPS 11 bits (0 = no limit), width 13, height 13, HUD 1, 125 Hz mouse 1, spy FOV 8, merc FOV 8 */
static inline u64 vfix_pack(int fps, int w, int h, int hud, int rate, int spyfov, int mercfov, int full)
{
    return (u64)(fps & 2047) | (u64)(w & 8191) << 11 | (u64)(h & 8191) << 24 | (u64)(hud & 1) << 37 | (u64)(rate & 1) << 38 |
           (u64)(spyfov & 255) << 39 | (u64)(mercfov & 255) << 47 | (u64)(full & 1) << 55;
}
static inline u64 mouse_pack(int sens, int turn) { return (u64)(sens & 65535) | (u64)(turn & 65535) << 16; }
static inline u64 versus_pack(int w, int h, int sens, int mult)
{
    return (u64)(w & 8191) | (u64)(h & 8191) << 13 | (u64)(sens & 65535) << 26 | (u64)(mult & 65535) << 42;
}
void ui_list(int which, int n, const UiPerson *p);
/* Friends found hosting. Return the lobby to join now, or 0. Set *reject to a
   lobby the user said no to (so it isn't offered again). */
u64 ui_offer_hosts(int n, const UiPerson *p, u64 *reject);
bool ui_poll(Cmd *c);

/* engine entry: runs until CMD_QUIT. first = optional starting command. */
typedef struct {
    int max_players;      /* 0 = 2 for co-op, 4 for Versus */
    bool no_launch;       /* tests: never start the game */
    bool versus;
    bool verbose;         /* show network details in the log */
    const char *exe;      /* game file name if it isn't the usual one */
} EngineOpts;
void engine_run(const EngineOpts *o, Cmd first);

/* lobbycode.c */
void lobby_code(u64 lobby, char out[16]);
u64 lobby_parse(const char *s);
