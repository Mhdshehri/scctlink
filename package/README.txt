SCCTLink 0.9.9.8 - Splinter Cell: Chaos Theory co-op and Versus with Steam friends
==============================================================================

No VPN (no Hamachi, Radmin or ZeroTier), no port forwarding, no IP addresses.
The game's own LAN mode is used; SCCTLink carries it through Steam.
Works for the co-op campaign (2 players) and for Spies vs. Mercenaries /
Versus (up to 4 players).

Both players need:
  * Splinter Cell: Chaos Theory (Steam), the SAME version and the SAME mods
  * for co-op: the SCCT Fix installed (Settings tab, one click - see below)
  * Steam running and logged in (online, not offline mode)
  * each other on their Steam friends list
  * this folder unzipped somewhere (Desktop is fine). Keep SCCTLink.exe,
    steam_api.dll and SCCTFix-d3d9.dll together.
  * the SAME SCCTLink version (0.9.9.8).


HOW TO PLAY
-----------
Everyone double-clicks SCCTLink.exe. Windows asks "Do you want to allow..."
(administrator rights: SCCTLink changes the game's settings files, which are
inside Program Files). A window opens (dark, with a Light mode switch at the
top right).

FIRST TIME, CO-OP: start the game once from Steam (so it makes its settings
files) and close it. Then in SCCTLink open the Settings tab and click
"Install" in the SCCT Fix banner at the top. Both players do this once. It
turns on the widescreen HUD and raw mouse too.
If Windows says "Windows protected your PC" the first time: click
"More info", then "Run anyway" (SCCTLink isn't signed with a paid
certificate; the files' SHA-256 checksums are in SHA256SUMS.txt).

  * The button next to "Join a friend" says Co-op or Versus. Click it to
    switch. Everyone picks the same one.

HOST (the one whose game runs the mission/match)
  1. Click "Host a game". The lobby code (like 1MCE-2B2) shows up and is
     already copied. Send it to your friend in Steam chat if they need it
     ("Copy code" copies it again).
  2. Click "Start Splinter Cell" (or "Start Versus"), then in the game:
       Co-op:  Co-op -> LAN -> Create game.
       Versus: create a LAN game.
  3. The list shows the friends linked to you. Leave SCCTLink open.

FRIEND(S)
  1. Click "Join a friend". The host shows up in the list: pick them and
     click Join (or double-click). Not there? Click Refresh - it asks your
     friends' SCCTLink directly, even when Steam shows the host as playing
     Splinter Cell. Or paste the host's lobby code in the box and click
     "Join this code".
  2. When the log says "Linked", click "Start Splinter Cell" (or
     "Start Versus") and join the host from the game's LAN list:
       Co-op:  Co-op -> LAN -> Join game.    Versus: the LAN game list.
  3. Leave SCCTLink open while you play.

Live stats = text that floats on top of the game, with no background and a
             dark edge so it reads on any scene: while linked, the ping,
             packet loss and packets per second for each friend. Drag it
             anywhere; right-click it to close.

Windows Firewall may ask about Splinter Cell or SCCTLink. You can press
Cancel: the game traffic only goes over this PC's own loopback addresses
(127.x.x.x), so nothing has to be opened to your network or the internet.


THE TABS AT THE BOTTOM
----------------------
How to   = the steps for the host or the friend, ticked as they happen
           ([x] done, [>] do this now), and what to watch out for.
Log      = everything SCCTLink did. A dot on "Log" means a warning arrived.
           "Copy log" copies it - send it with your friend's when something
           doesn't work. "Report" opens a new problem report on GitHub and
           the folder with scctlink.log: drag the log into the report (and
           your friend's too). Reports are public; the log has no IP
           addresses, but it has your Steam friends' names.
Mods     = the game files check, kept simple: your name and your mods, then
           your friend's name and their mods, then a green "Everything
           matches" - or each difference in red, for example
               Only on your PC: system/dinput8.dll
               Different version: system/core.u
               FOV: you 90, Friend 75 - must be the same   (co-op only)
           In co-op FOV, the SCCT Fix and fullscreen must match;
           the resolution doesn't have to. As soon as SCCTLink opens it lists
           your own mods (the game doesn't have to be running). "Check again"
           after you change something; the Log has every detail. Only file
           names relative to the game folder, sizes, checksums and those
           settings are sent - nothing about your PC.
Settings = on top, the fix of this mode: installed (green) or not (red),
           with its Install / Update / Remove button. Below it:
           Display: resolution and FOV (co-op), Max FPS (Versus), and the
           I / O switches "Widescreen HUD" and "Skip intro videos".
           Mouse: "Raw mouse" (co-op), "Mouse fix (125 Hz)" (Versus),
           sensitivity, and aim turn (co-op) or multiplier (Versus).
           Versus also has "Per class": FOV and turn speed for spies and
           mercenaries. "Reset to defaults" is on the left, "Apply" on the
           right (co-op also has "Use friend's": copies your friend's values
           into the fields). Close the game first (it writes its settings
           back when it exits). The first time a file is changed, the
           original is kept next to it as <name>.scctlink-backup - to undo,
           delete the changed file and remove ".scctlink-backup" from the
           backup's name.
           FOV: 75 is the game's own view. With the SCCT Fix a wide screen
           sees more on the sides by itself; a bigger number widens the
           view in normal play (not when aiming, using gadgets or in
           cutscenes). Each player's own choice.
           Aim turn: how fast you turn while aiming (the game's 0.50 = half
           speed; 1.00 = as fast as normal). In co-op both players need the
           same aim turn speed.
           The SCCT Fix has the widescreen HUD built in: you don't need
           another widescreen mod (the Mods tab says so if one is there -
           remove it, the two change the same code). No FPS limit to set
           in co-op: the SCCT Fix keeps both PCs at 60. For single player
           the button "Single player: 60 FPS" switches to your screen's
           refresh rate or no limit; as soon as you Host or Join a co-op
           game in SCCTLink, the SCCT Fix goes back to 60 by itself (and
           back to your choice when you Stop). Without SCCTLink (direct LAN
           co-op) keep it at 60.
           Raw mouse (part of the SCCT Fix, on when it's installed): the
           mouse is read as Windows Raw Input - no acceleration, fast moves
           aren't cut short, any polling rate. Each player's own choice.
           In Versus the mouse settings are in your Versus profile: start
           Versus once and make one first.


SCCT FIX (needed for co-op)
---------------------------
Without it, co-op disconnects as soon as someone draws a weapon or does an
action. Chaos Theory's co-op runs the same simulation on both PCs and ends
the session the moment they drift apart.

"Install" (SCCT Fix, top of the Settings tab) copies SCCTFix-d3d9.dll into the game's System folder as
d3d9.dll and sets StartupFullscreen=False in SplinterCell3.ini. The DLL caps
the game at 60 FPS, runs it in a borderless window, keeps Alt+Tab working and
writes System\SCCTFix.log if the game crashes. With it on both PCs, co-op
works: shooting, jumping, changing maps, no disconnects.
  * Close the game first. BOTH players need it - the Mods tab compares it.
  * SCCTLink only copies the file if its SHA-256 is exactly the one it pins
    (see SHA256SUMS.txt). It's built from the source in fix/d3d9fix.c: no
    network functions, starts no programs, only writes its log.
  * If another d3d9.dll is there (another mod), it is renamed to
    d3d9.dll.scctlink-backup. "Remove" puts it back and sets
    fullscreen again.


VERSUS NOTES
------------
  * Everyone needs the same Versus version (vanilla, or the same release of
    Enhanced SCCT Versus / SCCT Versus Reloaded) - mixing versions won't connect.
  * With Enhanced SCCT Versus / Reloaded, keep the server unlisted
    (console: listed false) so it doesn't appear on the public master
    server - that would share your IP, which is what SCCTLink avoids.
    You don't need their port forwarding/UPnP when using SCCTLink.
  * The Mods tab in Versus compares only the Versus folder (and in co-op
    only the co-op files), and shows who has SCCT Versus Reloaded (also in
    Enhanced SCCT Versus) installed and who doesn't.


BEFORE YOU PLAY (avoids disconnects)
------------------------------------
  * Install the SCCT Fix on both PCs (co-op).
  * Same files on both PCs: same patch, same language, same mods (or none).
    The Mods tab shows every difference.
  * Same FOV on both PCs (the Mods tab flags differences; the
    Settings tab or "Use friend's" fixes them).
  * If co-op saves/settings act strange: back up, then delete the game's
    settings folder in %PROGRAMDATA%\Ubisoft\ on both PCs so both start fresh.


LAG TIPS
--------
  * Stutter / bad mouse aim: set your mouse to 125 Hz polling in its software,
    and set every BiasCut value in SplinterCell3Settings.ini to 0.0
    (removes mouse acceleration).
  * Lower "Shadow resolution" / turn off shadow filtering if FPS dips.
  * AMD GPUs with broken thermal vision need a community fix - search the
    Steam guide "Improvements for Splinter Cell: Chaos Theory".
  * Lag: close downloads/streams on both PCs; use a wired connection if you can.
    Click Details in SCCTLink to see the ping and packet loss between you.
    Steam picks the relay route; SCCTLink itself adds about 1-2 ms.
  Only download mods from their official pages, and keep both PCs identical.


SAFETY
------
  * Neither player ever sees the other's IP address. SCCTLink turns off
    Steam's direct connections, so traffic always goes through Valve's relay
    whatever Steam's own "IP sharing" setting is. If the game writes your
    LAN address into a packet, SCCTLink replaces it before it leaves your PC.
  * No ports are opened and no router settings change. SCCTLink writes
    steam_appid.txt, scctlink.log and scctlink_cache.txt (checksums of the game
    files, so it opens quickly) in its own folder. In the game folder it
    only writes when you click "Apply" (the .ini files, with backups) or
    "Install" / "Remove" (SCCT Fix) (System\d3d9.dll and StartupFullscreen).
  * Only the host's Steam friends who joined the lobby get through (others
    are refused by Steam before any data arrives). The lobby is friends-only;
    anyone else is blocked and shown in the window.
  * Only Splinter Cell itself can use the connection: packets from any other
    program on your PC are dropped (and noted in scctlink.log).
  * Nothing is injected into the game by SCCTLink. Chaos Theory's LAN mode has
    no anti-cheat and its online servers are gone, so there is nothing to be
    banned from. Steam shows you as "playing Spacewar" - that's Valve's free
    test app that SCCTLink uses for the lobby and relay; this is normal for
    friend-to-friend mods.
  * steam_api.dll is Valve's signed file (Steamworks.NET 2025.164.1).
    SCCTLink checks its SHA-256 and refuses any other copy:
    fdafb7c65ec7d4182b42bf92cc8671c68e4dc547cc6e613899d2618a47eb65da
  * Your antivirus may warn about an unsigned new .exe. The full source is at
    github.com/Mhdshehri/scctlink - you can build it yourself.


TROUBLESHOOTING
---------------
  "Steam is not running" / "offline mode"  -> start Steam, go online, retry.
  The friend's SCCTLink doesn't find the host -> click Refresh, or paste the
    host's lobby code in the box and click "Join this code".
  The host's game is not in the LAN list:
    1. The host must be in Co-op -> LAN -> Create game first.
    2. Both click Details: "Your game: running" must show on both sides, and the
       friend should see the host's ports listed.
    3. Restart the friend's game while SCCTLink stays connected.
    4. Click "Report" in the Log tab and drag both scctlink.log files into
       the report. They contain no IP addresses (written as LAN-IP#1 etc).
  Co-op disconnects when someone draws a weapon -> install the SCCT Fix on
    both PCs (Settings tab), then check the Mods tab: everything must match.
  "different SCCTLink version" -> both need the same SCCTLink zip.
  "Install" (SCCT Fix) says the file is missing or not the expected one ->
    unzip SCCTLink again; SCCTFix-d3d9.dll must be next to SCCTLink.exe.


VERSION HISTORY
---------------
0.6: the friend could see the host's game in the LAN list but not join. The
game hides the host's real address inside an encrypted packet, so the
friend's game tried to reach it directly instead of through Steam. SCCTLink
decrypts that packet, puts in the address that goes through Steam, and
encrypts it again.
0.6.1: fixes the checksum at the end of the game's packets when SCCTLink
swaps an address in them (otherwise the game would drop them).
0.6.2: the encrypted reply also carries an MD5 of its contents; SCCTLink now
redoes that MD5 (in 0.6.1 the game stopped listing the host).
0.6.3: the game also sends every network address of the PC (Wi-Fi +
Ethernet, say) inside COMPRESSED messages (LZO). SCCTLink now decompresses
those messages, hides every address, and sends them uncompressed.
0.6.4: every game message ends in its own checksum byte, and the game
silently dropped messages whose address SCCTLink had swapped (stuck on
"Joining"). SCCTLink now redoes it. Joining works with the real game.
0.7: the game files check, and scctlink.log keeps the last game packets
before a disconnect, with times, so a disconnect can be traced.
0.7.1: the Mods tab and "Check again"; works with real game folders.
0.8.5: the Mods tab shows and compares resolution, FOV and FPS limits; the
Settings tab changes them (with backups); a closed lobby isn't tried again.
0.9: "Install FPS fix" in the Settings tab. With it, co-op works with the
real game: shooting, jumping, changing maps, no disconnects.
0.9.1: renamed to "SCCT Fix" (it does more than the frame cap); it also sets
StartupFullscreen=False, like the setup that was tested.
0.9.2: code clean-up (unused code removed), the SCCT Fix button also checks
StartupFullscreen when the DLL is already there, guides updated.
0.9.3: security review: .ini files are written to a new file first and then
swapped in (a failed write never leaves half a file), oversized .ini files
are left alone, less memory for a stalled TCP stream.
0.9.5: opens faster (game file checksums are cached and only changed files
are read again), "Live stats" window on top of the game (the game's FPS
even playing alone; ping, packet loss and packets per second while linked),
a reminder in co-op when the SCCT Fix isn't installed.
0.9.6: "Report" button in the Log tab (a GitHub problem report, filled in,
plus the log file to drag into it).
0.9.7: FOV buttons for the screen's shape in the Settings tab (4:3 = 75,
16:10 = 85, 16:9 = 91).
0.9.8: the Mods tab is simpler and in colors (names, mods, green when
everything matches, red for each difference); in Versus the co-op settings
and the SCCT Fix are hidden.
0.9.9: Settings: mouse sensitivity, aim turn speed and Reset; Versus gets its
own Settings (mouse sensitivity, mouse multiplier); the Mods tab says on top
whether all party members match.
0.9.9.8 (testing): co-op draws at the resolution you choose (or your
screen's; the SCCT Fix opens a window always and is DPI aware, so a scaled
2K desktop isn't shown as 1920x1080); the FOV works: the game ignores
DesiredFOV, so the SCCT Fix sets the camera's FOV (from ThirteenAG's
FusionFix) - wide screens see more on the sides by themselves, and the FOV
number (75 = the game's) widens the normal view, each player's own; settings
also go to the copies the game really reads in ProgramData; each time Versus
starts the Versus Fix sets its start resolution (Default.ini) and
ScreenRes=-1 in every profile and in DefaultProfile.ini (new profiles), so a
profile doesn't pick 1600x1200 again (also when Versus closes); with a
resolution chosen in SCCTLink, Versus' profile scripts (a new profile,
entering Multiplayer, the game's own resolution menu) no longer change it; Live stats show the connection only
(no FPS), with a thicker dark edge; the co-op 60 FPS signal is also a file,
System\SCCTLink.coop.
The Versus Fix also brings back the mercenary's flashlight on today's
graphics cards (no 3D-Analyze needed): Versus is told depth textures aren't
available, so it uses its own ps 1.4 shadow buffer (Flashlight=0 in
SCCTVersusFix.ini turns this off).
0.9.9.7 (testing): Raw mouse is now in the SCCT Fix (Windows Raw Input,
ported from ThirteenAG's FusionFix: no acceleration, any polling rate); the
Settings tab is rebuilt (a banner for the fix, sections, I / O switches,
Reset on its own); no FPS limit or widescreen FOV factor in co-op any more
(the SCCT Fix keeps 60 FPS and has its own widescreen HUD); the Mods tab
says when another widescreen mod is there; single player can have more FPS
("Single player: 60 FPS / screen's Hz / no limit") - during an SCCTLink co-op
session the SCCT Fix goes back to 60 by itself; the first install also skips
the intro videos; the window fits small screens; "Check again" finds Steam
if it was started after SCCTLink; "Update" replaces an older Versus Fix.
0.9.9.6 (testing): new co-op HUD fix, ported from ThirteenAG's FusionFix (MIT,
see THIRD-PARTY.txt): the HUD, menus and text are drawn 4:3 in the middle
instead of stretched; full-screen pictures, the scope and the loading bar
fit the wide screen.
0.9.9.5 (testing): "Skip intro" sets the three startup videos aside (renamed,
not deleted; off gives them back); the co-op HUD fix's places can be chosen
(SCCTFix.ini [HUD] Sites, testing).
0.9.9.4 (testing): co-op HUD fix only for the in-game HUD (menus and videos
as before); "Skip intro" (co-op Settings); Versus Fix mouse handed out one
event at a time (Versus reads it so).
0.9.9.3 (testing): the SCCT Fix is now our own build (fix/d3d9fix.c, same
behaviour as the original) with an optional HUD fix for wide screens (co-op
Settings: "HUD fix"); "Update SCCT Fix" replaces the original.
0.9.9.2 (testing): the Versus Fix (Settings tab, Versus): no 30 FPS limit,
the mouse as 125 reports a second (fast mice made Versus' aim zigzag), FOV
for spies and mercenaries, a wider resolution, the HUD not stretched,
turning speed.
0.9.9.1: Live stats shows Versus' frame rate (Direct3D 8, read from the
graphics kernel's events); Versus' resolution field removed (Versus resets it).
0.9.8.2: Raw mouse button in the Settings tab (no mouse acceleration); the
SCCT Fix's source code (fix/d3d9fix.c).
0.9.8.1: the Mods tab in two columns (you on the left, your friend on the
right); each mode compares only its own files, and Versus shows who has
SCCT Versus Reloaded; Live stats float over the game with no background.

Tested end to end on a simulated network: separate "PCs", a stand-in for the
game that finds LAN games by broadcast (including the real game's encrypted
discovery packet), embeds its address in packets and uses UDP and TCP, and a
stand-in for Steam with 10% packet loss and 60 ms delay. Packets arrived
byte-for-byte exact and in order. Versus was tested the same way with a host
and two friends on three "PCs". Co-op was tested with the real game between
two homes. If something doesn't work, click "Report" and attach both
scctlink.log files.


SCCTLink by Mhdshehri (github.com/Mhdshehri). MIT license. Not affiliated with Ubisoft or Valve.
