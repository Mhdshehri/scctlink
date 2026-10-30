SCCTLink 0.9.1 - Splinter Cell: Chaos Theory co-op and Versus with Steam friends
==============================================================================

No VPN (no Hamachi, Radmin or ZeroTier), no port forwarding, no IP addresses.
The game's own LAN mode is used; SCCTLink carries it through Steam.
Works for the co-op campaign (2 players) and for Spies vs. Mercenaries /
Versus (up to 4 players).

Both players need:
  * Splinter Cell: Chaos Theory, the SAME version and the SAME mods
    (see "Before you play" - a mismatch is the #1 cause of co-op disconnects)
  * Steam running and logged in (online, not offline mode)
  * each other on their Steam friends list
  * this folder unzipped somewhere (Desktop is fine). Keep SCCTLink.exe and
    steam_api.dll together.
  * the SAME SCCTLink version (0.9.1) - older versions can't link with it.


HOW TO PLAY
-----------
Everyone double-clicks SCCTLink.exe. A window opens (dark, with a Light
mode switch at the top right).

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

The box at the bottom has two tabs:
  How to = the steps for the host or the friend, ticked as they happen
           ([x] done, [>] do this now), and what to watch out for.
  Log    = everything SCCTLink did. A dot on "Log" means a warning arrived.
           "Copy log" copies it - send it with your friend's when something
           doesn't work.
MODS TAB (game files check): next to "Log". As soon as SCCTLink opens it
lists your game's mods (the game doesn't have to be running). When you link,
it compares your game's files with your friend's (Steam copies) as a
checklist - same game version / same mods / same game files - and lists what
differs (also in the Log). "Check again" after you change mods. For example
    only on your PC: system/dinput8.dll
    different version: system/core.u
It also shows your resolution, FOV and FPS limits (the game's own and the
widescreen fix's) and compares them: FOV and FPS limits must match (flagged
when they don't), the resolution doesn't have to.
Co-op disconnects when the two games aren't the same: install the same mods
(same versions) on both PCs, or remove them on both. Settings files (.ini)
are not compared. Only file names relative to the game folder, sizes and
checksums are sent - nothing about your PC.
SCCTLink asks for administrator rights when it starts (Windows' "Do you want
to allow..." prompt): the Settings tab writes the game's .ini files, which
are inside Program Files.
SCCT FIX (Settings tab): co-op disconnected as soon as someone drew a weapon
or did an action. Chaos Theory's co-op runs the same simulation on both PCs
and ends the session the moment they drift apart. "Install SCCT Fix" copies
SCCTFix-d3d9.dll into the game's System folder as d3d9.dll (60 FPS cap,
borderless window, Alt+Tab works, System\SCCTFix.log if the game crashes)
and sets StartupFullscreen=False in SplinterCell3.ini. With both, co-op
stopped disconnecting. BOTH players need it - the Mods tab compares it.
Close the game first. If another d3d9.dll is there (another mod) it is
renamed to d3d9.dll.scctlink-backup; "Remove SCCT Fix" puts it back and sets
fullscreen again. SCCTLink only copies the file if its SHA-256 is exactly the
checked one (it has no network code and starts no programs).
SETTINGS TAB: next to "Mods". Change the game's resolution, FOV and FPS
limit (and the widescreen fix's FOV factor, if it's installed) without
editing .ini files. Your friend's values are shown next to yours; "Use
friend's" copies them into the fields, "Apply" saves. Close Splinter Cell
first (it writes its settings back when it exits). The first time a file is
changed, the original is kept next to it as <name>.scctlink-backup - to undo,
delete the changed file and remove ".scctlink-backup" from the backup's name.
After saving, the game files check runs again, so your friend sees the new
values too.
Other buttons: Details = ping, packet loss, ports (in the Log). Stop = back
to the start. Host: pick a friend in the list and click Kick to remove them
(they can't link again until you click Stop).
SCCTLink flashes in the taskbar with a short sound when a friend links (host)
or the host's game is up (friend).
If a friend picked the other mode, SCCTLink says so: click Stop, switch the
mode, join again. If your SCCTLink versions differ, it says who has the newer one.

Windows Firewall may ask about Splinter Cell or SCCTLink. You can press
Cancel: the game traffic only goes over this PC's own loopback addresses
(127.x.x.x), so nothing has to be opened to your network or the internet.

VERSUS NOTES
------------
  * Everyone needs the same Versus version (vanilla, or the same release of
    Enhanced SCCT Versus / SCCT Versus Reloaded) - mixing versions won't connect.
  * With Enhanced SCCT Versus / Reloaded, keep the server unlisted
    (console: listed false) so it doesn't appear on the public master
    server - that would share your IP, which is what SCCTLink avoids.
    You don't need their port forwarding/UPnP when using SCCTLink.


BEFORE YOU PLAY (avoids disconnects and desync)
-----------------------------------------------
  * Same files on both PCs: same patch, same language, and either both use
    the ThirteenAG Widescreen Fix (same version) or neither does. Players
    report co-op dropping after about a minute when only one PC has it.
  * Same field of view on both PCs. If you edit FOV by hand, it is in
    SplinterCell3User.ini (DesiredFOV / DefaultFOV) - both must match.
  * Cap the frame rate (see FPS tips). Very high FPS breaks AI and physics.
  * If co-op saves/settings act strange: back up, then delete the game's
    settings folder in %PROGRAMDATA%\Ubisoft\ on both PCs so both start fresh.


FPS / LAG TIPS
--------------
  * Frame rate: the ThirteenAG Widescreen Fix has an FPS limit
    (FPSLimit in scripts\SplinterCellChaosTheory.WidescreenFix.ini).
    Set 60 on BOTH PCs (30 if a PC struggles). Do not leave it at 1000.
    Official download: github.com/ThirteenAG/WidescreenFixesPack/releases (tag "scct")
  * Stutter / bad mouse aim: set your mouse to 125 Hz polling in its software,
    and set every BiasCut value in SplinterCell3Settings.ini to 0.0
    (removes mouse acceleration).
  * Lower "Shadow resolution" / turn off shadow filtering if FPS dips.
  * AMD GPUs with broken thermal vision need a community fix - search the
    Steam guide "Improvements for Splinter Cell: Chaos Theory".
  * Lag: close downloads/streams on both PCs; use a wired connection if you can.
    Press S in SCCTLink to see the ping and packet loss between you.
    Steam picks the relay route; SCCTLink itself adds about 1-2 ms.
  Only download mods from the official pages above, and keep both PCs identical.


SAFETY
------
  * Neither player ever sees the other's IP address. SCCTLink turns off
    Steam's direct connections, so traffic always goes through Valve's relay
    whatever Steam's own "IP sharing" setting is. If the game writes your
    LAN address into a packet, SCCTLink replaces it before it leaves your PC.
  * Nothing is installed, no ports are opened, no router settings change.
    SCCTLink only writes steam_appid.txt and scctlink.log in its own folder.
  * Only the host's Steam friends who joined the lobby get through (others
    are refused by Steam before any data arrives). The lobby
    is friends-only; anyone else is blocked and shown in the window.
    Co-op lobbies hold 2 players, Versus lobbies 4.
  * Only Splinter Cell itself can use the connection: packets from any other
    program on your PC are dropped (and noted in scctlink.log).
  * The game is not modified and nothing is injected into it. Chaos Theory's
    LAN mode has no anti-cheat and its online servers are gone, so there is
    nothing to be banned from. Steam shows you as "playing Spacewar" - that's
    Valve's free test app that SCCTLink uses for the lobby and relay; this is
    normal for friend-to-friend mods.
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
    4. Send both scctlink.log files to the developer. They contain no IP
       addresses (they are written as LAN-IP#1 etc).
  Disconnects after a minute -> see "Before you play" (mods/FOV/FPS).
  "different SCCTLink version" -> both need the same SCCTLink zip.
  "port ... is busy - error 10013" -> the game runs as administrator: start
    SCCTLink as administrator too (right-click SCCTLink.exe).


VERSION 0.9.1 - WHAT IS TESTED
----------------------------
New in 0.6: the friend could see the host's game in the LAN list but not
join. The game hides the host's real address inside an encrypted packet, so
the friend's game tried to reach it directly instead of through Steam.
SCCTLink 0.6 decrypts that packet, puts in the address that goes through
Steam, and encrypts it again. Both players need 0.6.4 or newer.
0.6.1 also fixes the checksum at the end of the game's packets when
SCCTLink swaps an address in them (otherwise the game would drop them).
0.6.2: in 0.6.1 the game stopped listing the host, because the encrypted
reply also carries an MD5 of its contents; SCCTLink now redoes that MD5.
0.6.3: with 0.6.2 the friend's game connected but kept retrying, because
the game also sends every network address of the PC (Wi-Fi + Ethernet, say)
inside COMPRESSED messages (LZO). SCCTLink now decompresses those messages,
hides every address, and sends them uncompressed (the game accepts both).
0.6.4: the friend got stuck on "Joining": every game message ends in its
own checksum byte, and the game silently dropped messages whose address
SCCTLink had swapped, then asked again forever. SCCTLink now redoes it.
With 0.6.4 joining works with the real game (tested: host and friend in
different homes, into the first map).
0.7: the game files check above, and scctlink.log now keeps the last game
packets before a disconnect, with times, so a disconnect can be traced.
0.7.1: the Mods tab and "Check again"; the check works with real game
folders (0.7 dropped the big file list).
0.8.5: the Mods tab shows and compares resolution, FOV and FPS limits; the
new Settings tab changes them (with backups of the original files); a lobby
that was closed isn't tried again. (VPN mode stays on the experimental
vpn-mode branch only.)
0.9: "Install FPS fix" in the Settings tab. With it, co-op works with the
real game: shooting, jumping, changing maps, no disconnects.
0.9.1: renamed to "SCCT Fix" (it does more than the frame cap); it also sets
StartupFullscreen=False, like the setup that was tested.

Tested end to end on a simulated network: two separate "PCs", a stand-in for
the game that finds LAN games by broadcast (including the real game's
encrypted discovery packet), embeds its address in packets and uses UDP and
TCP, and a stand-in for Steam with 10% packet loss and 60 ms delay.
Packets arrived byte-for-byte exact and in order; the relay added about 1.5 ms.
Versus was tested the same way with a host and two friends on three "PCs".
If something doesn't work, send both scctlink.log files.


SCCTLink by Mhdshehri. MIT license. Not affiliated with Ubisoft or Valve.
