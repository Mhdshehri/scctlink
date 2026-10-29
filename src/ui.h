/* ui.h - what the SCCTLink engine (scctlink.c) needs from a front end.
 * Two front ends exist: ui_win.c (the Windows window) and ui_console.c
 * (a text version, used by the automated tests). The engine runs on its own
 * thread in the window version; every function here must be thread-safe there.
 * Same design as CoFLink's window. */
#pragma once
#include "mini.h"

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
    CMD_STOP,        /* stop hosting/joining, back to the start screen */
    CMD_QUIT,
} CmdKind;

typedef struct { CmdKind kind; u64 arg; } Cmd;

enum { MODE_IDLE = 0, MODE_HOST = 1, MODE_JOIN = 2 };

typedef struct {
    int mode;
    bool steam_ok;
    bool versus;          /* Versus instead of co-op */
    u64 lobby;            /* host: own lobby; join: lobby joined */
    int friends_connected;
    uint8_t done;         /* "How to" steps finished, one bit each (see make_status) */
    char status[160];     /* one-line summary for the big status label */
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
