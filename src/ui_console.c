/* ui_console.c - text front end for the SCCTLink engine. Used by the automated
 * tests (Linux, mock Steam):
 *   scctlink host [--versus] [--players N]
 *   scctlink join [lobby code] [--versus] [--no-launch] [--yes]
 *   scctlink idle                       (start on the start screen, like the window)
 * Keys (one per line on stdin): L paste lobby code, ^V join the code on the
 * clipboard, S status, D details, G start game, R refresh, V/C switch to
 * Versus/Co-op, X stop, H host, J join, K kick, M check the game files again,
 * T live stats (next line: 1 open, 0 close), A apply settings (next line: width height fov fps wsfactor*100), F SCCT Fix
 * (next line: 1 install, 0 remove), Q quit - the window's buttons. */
#include "ui.h"
#include "plat.h"

static bool g_yes;

void ui_log(const char *line)
{
    printf("%s\n", line);
    fflush(0);
}

void ui_state(const UiState *s)
{
    static char last[sizeof s->stats];
    if (!s->stats[0] || !strcmp(last, s->stats)) return;
    memcpy(last, s->stats, sizeof last);
    for (const char *l = s->stats; *l;) { /* one "[stats]" line per line */
        const char *e = strchr(l, '\n');
        int n = e ? (int)(e - l) : (int)strlen(l);
        printf("[stats] %.*s\n", n, l);
        l = e ? e + 1 : l + n;
    }
    fflush(0);
}
void ui_settings(const UiSettings *s)
{
    printf("[settings] scctfix %d mine %dx%d fov %d fps %d ws %d", s->scctfix, s->w, s->h, s->fov, s->fps, s->wsfov);
    if (s->friend_) printf(" | %s %dx%d fov %d fps %d ws %d", s->friend_name, s->fw, s->fh, s->ffov, s->ffps, s->fwsfov);
    printf("\n");
    fflush(0);
}
void ui_mods(const char *text, bool warn)
{
    printf("[mods%s]\n", warn ? " !" : "");
    for (const char *l = text; *l;) {
        const char *e = l;
        while (*e && *e != '\n') e++;
        int n = (int)(e - l);
        if (n && l[n - 1] == '\r') n--;
        printf("[mods] %.*s\n", n, l);
        l = *e ? e + 1 : e;
    }
    fflush(0);
}
void ui_alert(const char *what) { printf("[alert] %s\n", what); fflush(0); }
static u64 g_last_peer; /* tests: K kicks the last linked friend */
void ui_list(int which, int n, const UiPerson *p) { if (which == LIST_FRIENDS_ONLINE && n > 0) g_last_peer = p[n - 1].id; }

static bool ask_yes(const char *q)
{
    if (g_yes) return true;
    printf("%s (Y/N): ", q);
    fflush(0);
    char line[16];
    if (!read_line(line, sizeof line)) return false;
    return line[0] == 'y' || line[0] == 'Y';
}

u64 ui_offer_hosts(int n, const UiPerson *p, u64 *reject)
{
    if (n == 1) {
        char q[160];
        sfmt(q, sizeof q, "%s is hosting. Join their game?", p[0].name);
        if (ask_yes(q)) return p[0].lobby;
        *reject = p[0].lobby;
        ui_log("OK, not joining. Still looking...");
    } else if (n > 1) {
        ui_log("Several friends are hosting:");
        for (int i = 0; i < n; i++) printf("  %d. %s\n", i + 1, p[i].name);
        printf("Number to join: ");
        fflush(0);
        char line[32];
        int k = read_line(line, sizeof line) ? atoi(line) : 0;
        if (k >= 1 && k <= n) return p[k - 1].lobby;
    }
    return 0;
}

bool ui_poll(Cmd *c)
{
    int key = key_poll();
    if (key < 0) return false;
    c->arg = 0;
    c->kind = CMD_NONE;
    char line[128];
    switch (key) {
    case 'q': case 'Q': c->kind = CMD_QUIT; break;
    case 's': case 'S': c->kind = CMD_STATUS; break;
    case 'd': case 'D': c->kind = CMD_DETAILS; break;
    case 'x': case 'X': c->kind = CMD_STOP; break;
    case 'k': case 'K': c->kind = CMD_KICK; c->arg = g_last_peer; break;
    case 'g': case 'G': c->kind = CMD_LAUNCH; break;
    case 'r': case 'R': c->kind = CMD_REFRESH; break;
    case 'h': case 'H': c->kind = CMD_HOST; break;
    case 'j': case 'J': c->kind = CMD_JOIN; break;
    case 'v': case 'V': c->kind = CMD_SET_VERSUS; c->arg = 1; break;
    case 'c': case 'C': c->kind = CMD_SET_VERSUS; c->arg = 0; break;
    case 'm': case 'M': c->kind = CMD_RECHECK; break;
    case 't': case 'T': /* the Live stats window: then "1" (open) or "0" (close) */
        c->kind = CMD_STATS;
        c->arg = read_line(line, sizeof line) && line[0] == '1';
        break;
    case 'f': case 'F': /* the Settings tab's SCCT Fix button: then "1" (install) or "0" (remove) */
        c->kind = CMD_SCCTFIX;
        c->arg = read_line(line, sizeof line) && line[0] == '1';
        break;
    case 'a': case 'A': { /* the Settings tab's Apply: "width height fov fps wsfactor*100" */
        int v[5] = { 0, 0, 0, 0, 0 };
        if (read_line(line, sizeof line)) {
            int k = 0;
            for (char *p = line; *p && k < 5;) {
                while (*p == ' ') p++;
                if (!*p) break;
                v[k++] = atoi(p);
                while (*p && *p != ' ') p++;
            }
        }
        c->kind = CMD_APPLY_SETTINGS;
        c->arg = settings_pack(v[0], v[1], v[2], v[3], v[4]);
        break;
    }
    case KEY_PASTE:
        if (clipboard_get(line, sizeof line) > 0) {
            c->kind = CMD_JOIN_LOBBY;
            c->arg = lobby_parse(line);
            if (c->arg) printf("Lobby code from the clipboard.\n");
        }
        break;
    case 'l': case 'L':
        printf("Paste the host's lobby code, then Enter: ");
        fflush(0);
        if (read_line(line, sizeof line)) { c->kind = CMD_JOIN_LOBBY; c->arg = lobby_parse(line); }
        break;
    default: break;
    }
    return c->kind != CMD_NONE;
}

int main(int argc, char **argv)
{
    plat_init();
    EngineOpts o;
    memset(&o, 0, sizeof o);
    Cmd first = { CMD_NONE, 0 };
    u64 lobby = 0;
    bool idle = false;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "idle")) idle = true;
        else if (!strcmp(a, "host")) first.kind = CMD_HOST;
        else if (!strcmp(a, "join")) first.kind = CMD_JOIN;
        else if (!strcmp(a, "--players") && i + 1 < argc) o.max_players = atoi(argv[++i]);
        else if (!strcmp(a, "--versus")) o.versus = true;
        else if (!strcmp(a, "--exe") && i + 1 < argc) o.exe = argv[++i];
        else if (!strcmp(a, "--no-launch")) o.no_launch = true;
        else if (!strcmp(a, "--verbose")) o.verbose = true;
        else if (!strcmp(a, "--yes")) g_yes = true;
        else if (lobby_parse(a)) lobby = lobby_parse(a); /* a lobby code or full id */
    }
    if (lobby) { first.kind = CMD_JOIN_LOBBY; first.arg = lobby; }
    if (first.kind == CMD_NONE && !idle) {
        printf("usage: scctlink host | join [lobby code] | idle  [--versus]\n");
        return 1;
    }
    engine_run(&o, first);
    return 0;
}
