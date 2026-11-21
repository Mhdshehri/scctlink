SCCTLink 1.5 - Splinter Cell: Chaos Theory online with your Steam friends
=============================================================================

SCCTLink lets you play Chaos Theory's co-op campaign (2 players) and
Versus (up to 4 players) online with your Steam friends. It only works
between Steam friends: nobody else can see your game or join it.

The game's online servers are gone, so only LAN play is left. SCCTLink
carries the game's LAN traffic through Steam, so the game thinks you're on
the same network. No VPN, nothing to set up on your router, and neither
of you sees the other's IP.


WHAT YOU NEED
-------------
  * Splinter Cell: Chaos Theory on Steam
  * Steam open and online
  * The other players on your Steam friends list
  * The same SCCTLink version as your friends (1.5)

Keep the files in this folder together. Open SCCTLink.exe. Windows asks for
administrator rights: SCCTLink changes the game's settings files, which are
inside Program Files. If Windows says "Windows protected your PC", click
"More info", then "Run anyway" (SCCTLink is new and not signed with a paid
certificate).

The pages are on the left: Play, Mods, Settings, Log, How to. On Play,
pick Co-op or Versus at the top. Everyone picks the same one.


CO-OP
-----
First time (both players, once):
  1. Start the game once from Steam and close it.
  2. In SCCTLink, click "Install SCCT Fix" on Play (or "Install" on the
     Settings page).
  3. Start the game once more and close it. From then on it's ready.

Host:
  1. On Play, pick "Host a game" and click "Host".
     For 60 FPS in missions, turn on "60 FPS when you host" in Settings
     first (your friend's game follows yours).
  2. Click "Start Splinter Cell", then in the game:
     Co-op -> LAN -> Create game.
  3. Leave SCCTLink open while you play.

Friend:
  1. On Play, pick "Join a friend" and click "Join". Then pick the host in
     the list and click "Join".
     Not in the list? Click "Refresh", or paste the host's lobby code
     (like 1MCE-2B2) and click "Join this code".
  2. When SCCTLink says you're linked, click "Start Splinter Cell", then
     in the game: Co-op -> LAN -> Join game, and pick the host.
  3. Leave SCCTLink open while you play.


VERSUS
------
SCCTLink is made for playing Versus with your friends, in a private game.
To play online with other people (public servers), use Enhanced SCCT
Versus: github.com/Joshhhuaaa/EnhancedSCCTVersus

First time (everyone, once):
  1. Start Versus once, make a profile, and close the game.
  2. In SCCTLink, pick Versus at the top of Play and click
     "Install Versus Fix" (or "Install" on the Settings page).

Host:    pick "Host a game", click "Host", then "Start Versus" and create
         a LAN game.
Friends: pick "Join a friend", click "Join", pick the host and click
         "Join". When you're linked, click "Start Versus" and join from the
         LAN list.


THE PAGES
---------
Play      Co-op or Versus, the status (red while the fix is missing, with
          an Install button), Host or Join and the button that starts it
          (Stop while running), friends hosting, the lobby code, and
          "Start Splinter Cell".
Mods      Compares your game with your friend's. Green when all party
          members match; every difference in red. "Check again" after you
          change something.
Settings  The fix for this mode (Install / Update / Remove), then the
          display, co-op and mouse settings. Close the game before you
          click "Apply". The first time a file is changed, the original is
          kept next to it as <name>.scctlink-backup.
Log       Everything SCCTLink did. "Copy log" copies it; "Report" opens a
          problem report on GitHub and the folder with scctlink.log.
          Reports are public. The log has no IP addresses, but it has your
          Steam friends' names.
How to    Your steps, ticked off as they happen ([x] done, [>] do this now).

Live stats: text on top of the game with the ping and packet loss for
each friend. Drag it anywhere; right-click it to close.
Light / Dark theme: at the bottom of the left side.


WHAT THE FIXES SOLVE
--------------------
SCCT Fix (co-op and story):
  * Co-op disconnects: the game's drawing changed the co-op simulation
    (what each PC draws depends on its camera and frame rate), so the two
    PCs drifted apart. The fix stops that. Different frame rates no longer
    disconnect: both PCs run at the slower one's pace.
  * Co-op at 60 FPS: in missions the game's co-op runs at about 34 FPS.
    With "60 FPS when you host" (Settings, Co-op) it runs at about 60, at
    normal game speed. The host picks it; the friend's game follows.
  * Single player: 60, your screen's refresh rate, or FPS unlock (the
    game itself stops at 100).
  * Stretched HUD on wide screens, wrong resolution, FOV that can't be
    changed, mouse acceleration, Alt+Tab problems (borderless window).

Versus Fix:
  * The 30 FPS limit, aim zigzag with fast mice, going back to 1600x1200,
    the mercenary's flashlight not lighting anything, and one FOV and turn
    speed for everyone (now per class). Fullscreen off = a borderless window.
    Not fixed yet: the HUD is still stretched on wide screens.


TROUBLESHOOTING
---------------
Your friend can't find the host
  -> The host must be in Co-op -> LAN -> Create game first. The friend
     clicks "Refresh", or uses the lobby code.
The host's game isn't in the LAN list
  1. Both click "Details": the Log must say "Your game: running" on both PCs.
  2. The friend restarts the game while SCCTLink stays linked.
  3. Still nothing? Both click "Report" and attach scctlink.log.
Co-op disconnects
  -> The SCCT Fix on both PCs, and the Mods page must say all party
     members match. Playing over LAN without SCCTLink? Set the
     single-player frame rate to 60 FPS on both PCs.
The resolution or settings didn't change
  -> Close the game before "Apply" (it saves its own settings when it
     closes). Co-op, first install: start the game once more. Versus:
     make a profile first.
Co-op runs at about 34 FPS in a mission
  -> That's the game's own co-op: it moves both PCs forward in fixed steps,
     about 34 a second. The host can turn on "60 FPS when you host" in
     Settings (Co-op) before hosting. Both need SCCTLink 1.5.
Versus crashes, minimizes or flickers
  -> Turn Fullscreen off in Settings (Versus) and click "Apply".
"Different SCCTLink version"
  -> Everyone needs the same SCCTLink zip.
Windows Firewall asks about Splinter Cell or SCCTLink
  -> You can click Cancel. The traffic only goes through your own PC.
Thermal vision shows no heat on AMD graphics cards
  -> A known problem of the game itself. There is a community fix for it,
     but it isn't part of SCCTLink. Not tested yet: whether co-op works
     when only one player has it.


SAFETY
------
  * Neither player sees the other's IP: everything goes through Valve's
    relay, and SCCTLink hides your LAN address inside the game's packets.
  * Only the host's Steam friends can join. No ports are opened.
  * Only Splinter Cell can use the connection; other programs are blocked.
  * Chaos Theory's LAN mode has no anti-cheat and its servers are gone, so
    there's nothing to be banned from. Steam shows you as "playing
    Spacewar": Valve's free test app that SCCTLink uses for the lobby.
  * The files' SHA-256 checksums are in SHA256SUMS.txt. The full source is
    at github.com/Mhdshehri/scctlink.


SCCTLink by Mhdshehri (github.com/Mhdshehri). MIT license.
What changed in each version: CHANGELOG.md on GitHub.
Not affiliated with Ubisoft or Valve.
